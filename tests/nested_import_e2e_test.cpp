// editor.import.nested — the nested-composition import UI e2e (docs §9, the offscreen software-GL
// lane; modelled on tests/image_import_e2e_test.cpp). Exercises the ONE app import verb
// `AppProjectGateway::insert_nested` through both entry points: the OS file-drop of a `.arbc` (a
// direct verb call with a pane-local device point, the shape the shell's SDL_EVENT_DROP_FILE arm
// produces after branching on the `.arbc` extension) and the "Place composition…" affordance (which
// drives the injected `FileDialog` fake and places at the focused canvas centre). Asserts, on model
// state (never pixels — `ace_shell_test` defines no golden dir, exactly as the image e2e): the
// minted cell is selected, a ResolutionIndependent + BORROWED `org.arbc.nested` reference placed
// 1:1 at the drop point, rendering the doc-05 placeholder in-session (child unresolved until
// reopen, D-nested-3). Plus a headless gateway pass with no shell (drop point, centre, cancel,
// out-of-pane).
#include <ace/app/canvas_view.hpp>
#include <ace/app/file_dialog.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/cell.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/contract/content.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_nested/nested_content.hpp>
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
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "writer_session.hpp"

using ace::app::CanvasView;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::commands::AppState;
using ace::dockmodel::ViewType;
using ace::scene::DetailSource;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_nested_import_e2e_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// A real child `.arbc` on disk to reference — the e2e never reopens (the cell renders the
// placeholder in-session), so a bare 32x32 composition is a valid, honest reference target; the
// full colored-solid round-trip + resolved golden live in the L1 units (tests/nested_import_test).
std::filesystem::path build_child_arbc(const std::filesystem::path& root,
                                       const ace::platform::FileSystem& fs) {
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  AppState child(std::move(*created));
  child.document().add_composition(32.0, 32.0);
  REQUIRE(ace::commands::save_project(child, fs).has_value());
  return ace::project::project_layout(root).canonical;
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
// The shipped file seam, scripted: `show` delivers synchronously — no real OS file picker exists
// offscreen. The same fake backs "Place composition…" as "Place image…".
class ScriptedFileDialog final : public ace::app::FileDialog {
public:
  std::optional<std::filesystem::path> next;
  bool shown = false;
  void show(Callback on_pick) override {
    shown = true;
    on_pick(next);
  }
};

struct E2EState {
  ace::dock::Dockspace* dockspace;
  AppState* state;
  ace::app::AppProjectGateway* gateway;
  CanvasView* canvas;
  std::filesystem::path child;
};

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

// A nested cell is a borrowed, ResolutionIndependent reference (no editable grid, no detail floor).
void check_nested_borrowed(const ace::scene::Cell& cell, const arbc::Document& document) {
  CHECK(cell.kind_id == std::string(arbc::NestedContent::kind_id));
  CHECK(cell.detail.source == DetailSource::ResolutionIndependent);
  CHECK(cell.detail.borrowed);
  CHECK_FALSE(cell.detail.native_pixels.has_value());
  // Read-only + unresolved in-session: the child is `ObjectId{}` until the next reopen
  // (D-nested-3).
  arbc::Content* content = document.resolve(cell.id);
  REQUIRE(content != nullptr);
  CHECK(content->editable() == nullptr);
  CHECK_FALSE(content->composition_ref().valid());
}

} // namespace

// --- The gateway verb, headless (no shell, no canvas) ------------------------

TEST_CASE("nested import gateway: insert_nested mints a borrowed, selected nested reference") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path child = build_child_arbc(scratch.root / "child", fs);

  auto created = ace::project::create_project(fs, scratch.root / "gateway");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  state.document().add_composition(64.0, 64.0);

  ace::dockmodel::RecentProjects recent(scratch.root / "prefs", fs);
  NoopFolderDialog folder;
  NoopLauncher launcher;
  ace::app::AppProjectGateway gateway(recent, fs, folder, launcher, "/usr/bin/arbitraryeditor",
                                      state);
  // No FileDialog wired yet: "Place composition…" is unavailable, and driving place_nested() with
  // no picker installed is a graceful no-op (mints nothing).
  CHECK_FALSE(gateway.can_place_nested());
  gateway.place_nested();
  CHECK(ace::scene::cells(state.document(), state.registry()).empty());

  // A drop at an explicit pane-local device point (no view-framing provider -> the root
  // composition's own extent at identity, so comp == device point).
  const std::string err = gateway.insert_nested(child, arbc::Vec2{20.0, 30.0});
  CHECK(err.empty());

  std::vector<ace::scene::Cell> cells = ace::scene::cells(state.document(), state.registry());
  REQUIRE(cells.size() == 1);
  check_nested_borrowed(cells[0], state.document());
  // 1:1 identity over empty bounds, origin anchored at the comp-space drop point (identity camera).
  CHECK(cells[0].placement.a == 1.0);
  CHECK(cells[0].placement.d == 1.0);
  CHECK(cells[0].placement.tx == 20.0);
  CHECK(cells[0].placement.ty == 30.0);
  CHECK(state.selection().primary() == cells[0].id);

  // The "Place composition…" path: wire the scripted picker; now the affordance is available and a
  // pick imports at the focused view centre (nullopt device point -> pane centre = (32,32)).
  ScriptedFileDialog picker;
  picker.next = child;
  gateway.set_file_dialog(picker);
  CHECK(gateway.can_place_nested());
  gateway.place_nested();
  CHECK(picker.shown);
  cells = ace::scene::cells(state.document(), state.registry());
  REQUIRE(cells.size() == 2);
  check_nested_borrowed(cells[1], state.document());
  CHECK(cells[1].placement.tx == 32.0); // 64x64 root -> centre
  CHECK(cells[1].placement.ty == 32.0);
  CHECK(state.selection().primary() == cells[1].id);

  // A cancelled pick (nullopt) imports nothing.
  picker.next = std::nullopt;
  gateway.place_nested();
  CHECK(ace::scene::cells(state.document(), state.registry()).size() == 2);
}

// --- The live shell e2e: drop + Place + out-of-pane drop through the real gateway ------------

TEST_CASE("nested import e2e: a drop, a Place, and an out-of-pane drop all mint a selected nested "
          "reference") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path child = build_child_arbc(scratch.root / "child", fs);

  ace::testing::WriterSession session(scratch.root / "nested");
  REQUIRE(session.ok());
  AppState& state = session.state();
  session.on_writer([&] { state.document().add_composition(64.0, 64.0); });
  REQUIRE(ace::scene::cells(state.document(), state.registry()).empty());

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
  NoopFolderDialog folder;
  NoopLauncher launcher;
  ScriptedFileDialog picker;
  picker.next = child;
  ace::app::AppProjectGateway gateway(recent, fs, folder, launcher, "/usr/bin/arbitraryeditor",
                                      state);
  gateway.set_edit_runner(
      [&canvas](const std::function<void()>& edit) { canvas.apply_edit(edit); });
  gateway.set_view_framing([&canvas] { return canvas.focused_framing(); });
  gateway.set_file_dialog(picker);
  dockspace.set_project_gateway(&gateway);

  shell.set_draw_content([&dockspace, &canvas]() {
    dockspace.draw();
    canvas.reconcile(dockspace.layout().view_ids());
  });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&dockspace, &state, &gateway, &canvas, child};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "nested", "import");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    AppState& state = *e2e->state;
    ace::app::AppProjectGateway& gateway = *e2e->gateway;
    CanvasView& canvas = *e2e->canvas;
    const std::string rail = ace::dock::tool_rail_title();

    // Let the canvas pane draw so it records its screen rect + framing.
    ctx->ItemClick((rail + "/###insert_cell").c_str()); // open then cancel — just to settle UI
    ctx->Yield(2);
    ctx->ItemClick("Insert Cell/###insert_cancel");
    ctx->Yield(2);

    // --- (1) Drop path: the verb the SDL_EVENT_DROP_FILE `.arbc` arm calls ------
    const std::optional<arbc::Rect> pane = canvas.focused_pane_rect();
    IM_CHECK(pane.has_value());
    const arbc::Vec2 drop{40.0, 30.0};
    const std::string err = gateway.insert_nested(e2e->child, drop);
    IM_CHECK(err.empty());
    IM_CHECK(pump_until(
        ctx, [&] { return ace::scene::cells(state.document(), state.registry()).size() == 1; }));
    ctx->Yield(2);

    std::vector<ace::scene::Cell> cells = ace::scene::cells(state.document(), state.registry());
    IM_CHECK(cells.size() == 1);
    IM_CHECK(cells[0].kind_id == std::string(arbc::NestedContent::kind_id));
    IM_CHECK(cells[0].detail.source == DetailSource::ResolutionIndependent);
    IM_CHECK(cells[0].detail.borrowed);
    IM_CHECK(state.selection().primary() == cells[0].id);
    // Placeholder-live: the child is unresolved in-session (renders the doc-05 placeholder).
    arbc::Content* content = state.document().resolve(cells[0].id);
    IM_CHECK(content != nullptr);
    IM_CHECK(!content->composition_ref().valid());
    // Anchored 1:1 at the comp-space image of the drop point.
    IM_CHECK(cells[0].placement.a == 1.0);
    const arbc::Vec2 comp = canvas.focused_framing().camera.inverse()->apply(drop);
    IM_CHECK(cells[0].placement.tx == comp.x);
    IM_CHECK(cells[0].placement.ty == comp.y);

    // --- (2) Place path: the "Place composition…" affordance drives the FileDialog fake ---
    IM_CHECK(ctx->ItemExists((rail + "/###place_nested").c_str()));
    ctx->ItemClick((rail + "/###place_nested").c_str());
    IM_CHECK(pump_until(
        ctx, [&] { return ace::scene::cells(state.document(), state.registry()).size() == 2; }));
    ctx->Yield(2);
    cells = ace::scene::cells(state.document(), state.registry());
    IM_CHECK(cells.size() == 2);
    IM_CHECK(cells[1].kind_id == std::string(arbc::NestedContent::kind_id));
    IM_CHECK(cells[1].detail.borrowed);
    IM_CHECK(state.selection().primary() == cells[1].id);

    // --- (3) Out-of-pane drop: nullopt device point falls back to the focused-pane centre ---
    const std::string err3 = gateway.insert_nested(e2e->child, std::nullopt);
    IM_CHECK(err3.empty());
    IM_CHECK(pump_until(
        ctx, [&] { return ace::scene::cells(state.document(), state.registry()).size() == 3; }));
    ctx->Yield(2);
    cells = ace::scene::cells(state.document(), state.registry());
    IM_CHECK(cells.size() == 3);
    IM_CHECK(cells[2].detail.borrowed);
    IM_CHECK(state.selection().primary() == cells[2].id);
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
