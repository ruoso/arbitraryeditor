// editor.cells.scoped_edit — the interactive half of D29 as a full-app ImGui Test Engine e2e
// (docs §9, the offscreen software-GL lane; modeled on tests/selection_e2e_test.cpp). Boots the
// shell over a REAL commands::AppState with a nested child composition plus one out-of-scope root
// cell, then proves that ENTERING the child confines editing and interaction to it:
//
//   - enter → an Insert lands in the child (present in scene::cells(child), absent from root);
//   - an out-of-scope canvas click and marquee are inert (the dimmed root cell cannot be grabbed);
//   - Ctrl+A selects only the child's cells (never the root cell);
//   - Delete removes the child's cells and one Undo restores them into the child;
//   - climbing back to Root restores root insertion and makes the root cell pickable again.
//
// Assertions are on MODEL state (scene::cells, state.selection()), never pixels — the byte-exact
// placement is pinned by the render_offline golden in tests/cells_scoped_edit_test.cpp. The scope
// is read/threaded entirely in L1/L4; the ProjectGateway virtuals are untouched (Decision 5), so
// this drives the shipped `insert_cell`/`delete_selected`/`undo` seams unchanged.
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/commands/selection.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/cell.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "writer_session.hpp"

using ace::app::CanvasView;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::commands::AppState;
using ace::dockmodel::ViewType;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  explicit ScratchDir(const char* tag)
      : root(std::filesystem::temp_directory_path() / (std::string("ace_scoped_edit_e2e_") + tag)) {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

struct Seeded {
  arbc::ObjectId root_comp; // the root (lowest-id) composition
  arbc::ObjectId child;     // the nested child composition (the scope to enter)
  arbc::ObjectId root_cell; // an out-of-scope root solid, clickable at [50,62)^2
};

// Root 64x64 with: an out-of-scope RED solid at root [50,62)^2, and a nested wrapper placing a
// 32x32 GREEN child at root [16,48)^2. The child's own fill is its single in-scope cell.
// Composition units map 1:1 to pane pixels (the viewport starts at identity, per selection_e2e).
Seeded seed(AppState& state) {
  arbc::Document& doc = state.document();
  Seeded out;
  out.root_comp = doc.add_composition(64.0, 64.0);

  out.root_cell = doc.add_content(std::make_shared<arbc::SolidContent>(
      arbc::Rgba{0.7F, 0.0F, 0.0F, 1.0F}, arbc::Rect{0.0, 0.0, 12.0, 12.0}));
  doc.attach_layer(out.root_comp,
                   doc.add_layer(out.root_cell, arbc::Affine::translation(50.0, 50.0)));

  out.child = doc.add_composition(32.0, 32.0);
  const arbc::ObjectId child_fill = doc.add_content(std::make_shared<arbc::SolidContent>(
      arbc::Rgba{0.0F, 0.7F, 0.0F, 1.0F}, arbc::Rect{0.0, 0.0, 32.0, 32.0}));
  doc.attach_layer(out.child, doc.add_layer(child_fill, arbc::Affine::identity()));
  (void)ace::scene::add_cell(doc, state.registry(), "org.arbc.nested",
                             std::to_string(out.child.value),
                             arbc::Affine::translation(16.0, 16.0));
  return out;
}

template <class Ready> bool pump_until(ImGuiTestContext* ctx, Ready ready) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (std::chrono::steady_clock::now() < deadline) {
    if (ready()) {
      return true;
    }
    ctx->Yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return ready();
}

class NoopFolderDialog final : public ace::app::FolderDialog {
public:
  void show(Callback) override {}
};
class NoopLauncher final : public ace::platform::ProcessLauncher {
public:
  std::error_code spawn_detached(const std::filesystem::path&,
                                 const std::vector<std::string>&) const override {
    return {};
  }
};

struct E2EState {
  CanvasView* canvas;
  AppState* state;
  ace::app::AppProjectGateway* gateway;
  Seeded ids;
};

std::size_t child_count(AppState& state, arbc::ObjectId child) {
  return ace::scene::cells(state.document(), state.registry(), child).size();
}
std::size_t root_count(AppState& state) {
  return ace::scene::cells(state.document(), state.registry()).size();
}

} // namespace

TEST_CASE("cells scoped edit e2e: entering a composition confines insert, pick, select-all, and "
          "delete to it; climbing restores root editing") {
  ScratchDir scratch("main");
  ace::platform::NativeFileSystem fs;
  ace::testing::WriterSession session(scratch.root / "scoped");
  REQUIRE(session.ok());
  AppState& state = session.state();
  Seeded ids;
  session.on_writer([&] { ids = seed(state); });
  REQUIRE(ids.child.valid());
  REQUIRE(ids.root_cell.valid());
  REQUIRE(state.selection().empty());

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 900;
  opts.height = 640;
  REQUIRE(shell.init(opts));

  CanvasView canvas(state, session.writer());
  ace::views::register_view_body(ViewType::Canvas, [&canvas](std::string_view view_id) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content(view_id, static_cast<int>(avail.x), static_cast<int>(avail.y));
  });

  ace::dock::Dockspace dockspace;
  ace::dockmodel::RecentProjects recent(scratch.root / "prefs", fs);
  NoopFolderDialog dialog;
  NoopLauncher launcher;
  ace::app::AppProjectGateway gateway(recent, fs, dialog, launcher, "/usr/bin/arbitraryeditor",
                                      state);
  gateway.set_edit_runner(
      [&canvas](const std::function<void()>& edit) { canvas.apply_edit(edit); });
  gateway.set_view_framing([&canvas] { return canvas.primary_framing(); });

  shell.set_draw_content([&dockspace, &canvas]() {
    dockspace.draw();
    canvas.reconcile(dockspace.layout().view_ids());
  });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&canvas, &state, &gateway, ids};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "scoped_edit", "enter_insert_pick_delete_climb");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    CanvasView& canvas = *e2e->canvas;
    AppState& state = *e2e->state;
    ace::app::AppProjectGateway& gateway = *e2e->gateway;
    ace::commands::Selection& sel = state.selection();
    const arbc::ObjectId child = e2e->ids.child;
    const arbc::ObjectId root_cell = e2e->ids.root_cell;

    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));
    ctx->WindowFocus("canvas#1");
    ctx->Yield(3);

    const auto pane_origin = [&]() {
      const ImGuiTestItemInfo info = ctx->ItemInfo("canvas#1/##canvas_nav");
      return ImVec2(info.RectFull.Min.x, info.RectFull.Min.y);
    };
    const ImVec2 origin = pane_origin();
    IM_CHECK(origin.x >= 0.0F && origin.y >= 0.0F);
    const auto at = [&origin](float cx, float cy) { return ImVec2(origin.x + cx, origin.y + cy); };
    const auto click_at = [&](ImVec2 pos) {
      ctx->MouseMoveToPos(pos);
      ctx->MouseDown(0);
      ctx->MouseUp(0);
      ctx->Yield(2);
    };
    const auto drag = [&](ImVec2 from, ImVec2 to) {
      ctx->MouseMoveToPos(from);
      ctx->MouseDown(0);
      ctx->MouseMoveToPos(to);
      ctx->MouseUp(0);
      ctx->Yield(2);
    };
    ctx->MouseMove("canvas#1/##canvas_nav"); // establish the viewport for raw-position moves

    // --- Baseline at Root: the root cell IS pickable ---------------------------------------------
    click_at(at(56.0F, 56.0F)); // root [50,62)^2
    IM_CHECK(sel.primary() == root_cell);
    sel.clear();
    ctx->Yield(2);

    // --- enter → insert lands in the child (not root) --------------------------------------------
    state.entered_composition() = child;
    ctx->Yield(3);
    const std::size_t child_before = child_count(state, child);
    const std::size_t root_before = root_count(state);
    IM_CHECK(gateway.insert_cell("org.arbc.raster", {{"width", "24"}, {"height", "24"}}).empty());
    IM_CHECK(pump_until(ctx, [&] { return child_count(state, child) == child_before + 1; }));
    IM_CHECK(root_count(state) == root_before); // the root list is untouched (Constraint 6)

    // --- out-of-scope click and marquee are inert (Constraints 7/8) ------------------------------
    sel.clear();
    ctx->Yield(2);
    click_at(at(56.0F, 56.0F)); // the dimmed root cell cannot be grabbed while entered
    IM_CHECK(sel.empty());
    drag(at(48.0F, 50.0F), at(63.0F, 63.0F)); // a marquee purely over the out-of-scope region
    IM_CHECK(sel.empty());

    // --- Ctrl+A selects ONLY the child's cells ---------------------------------------------------
    ctx->MouseMove("canvas#1/##canvas_nav"); // hover the canvas pane (the chord is pane-scoped)
    ctx->Yield(2);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
    ctx->Yield(3);
    const std::size_t in_scope = child_count(state, child);
    IM_CHECK(sel.size() == in_scope);
    IM_CHECK(!sel.contains(root_cell)); // the out-of-scope root cell is never selected

    // --- Delete removes the child's cells; one Undo restores them into the child -----------------
    const std::size_t deleted = gateway.delete_selected();
    IM_CHECK(deleted == in_scope);
    IM_CHECK(pump_until(ctx, [&] { return child_count(state, child) == 0; }));
    IM_CHECK(gateway.can_undo());
    gateway.undo();
    IM_CHECK(pump_until(ctx, [&] { return child_count(state, child) == in_scope; }));

    // --- climb to Root restores root insertion and picking (confinement is scoped to entered) ----
    state.entered_composition() = std::nullopt;
    ctx->Yield(3);
    const std::size_t root_before_climb = root_count(state);
    IM_CHECK(gateway.insert_cell("org.arbc.raster", {{"width", "8"}, {"height", "8"}}).empty());
    IM_CHECK(pump_until(ctx, [&] { return root_count(state) == root_before_climb + 1; }));
    sel.clear();
    ctx->Yield(2);
    click_at(at(56.0F, 56.0F)); // the root cell is pickable again
    IM_CHECK(sel.primary() == root_cell);
  };
  ImGuiTestEngine_QueueTest(engine, test);

  const int k_max_frames = 200000;
  int frames = 0;
  while (!ImGuiTestEngine_IsTestQueueEmpty(engine) && frames < k_max_frames) {
    shell.new_frame();
    shell.draw_ui();
    shell.render();
    ImGuiTestEngine_PostSwap(engine);
    ++frames;
  }

  int count_tested = 0;
  int count_success = 0;
  ImGuiTestEngine_GetResult(engine, count_tested, count_success);
  ImGuiTestEngine_Stop(engine);
  ace::views::register_view_body(ViewType::Canvas, {});

  CHECK(frames < k_max_frames);
  CHECK(count_tested == 1);
  CHECK(count_success == 1);

  canvas.destroy();
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
