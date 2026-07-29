// editor.canvas.tool_dispatch — the modal-tool → canvas-gesture routing e2e (docs §9, the offscreen
// software-GL lane; rig cloned from tests/selection_e2e_test.cpp). Boots the shell over a REAL
// commands::AppState, seeds two overlapping bounded cells plus one shot camera, threads the rail's
// active tool into the Canvas body via `dockspace.tools().active()` (the wiring this leaf closes),
// and drives "canvas#1/##canvas_nav" by RAW MOUSE POSITION under each modal tool.
//
// Asserts, over the SAME scene, that a plain canvas drag does what the ACTIVE tool says (D20):
//   * Select — a drag on empty canvas marquee-selects, a click picks, and a drag over the selected
//     cell's corner handle transforms it (one journal entry). Regression-guards the predecessor
//     gizmo + pick behaviour now that it is GATED behind Select (D-gizmo-2).
//   * Pan — a left-button drag moves the transient viewport camera and changes NO selection and NO
//     journal cursor (transient-only; Constraint 5 / D15).
//   * Brush / Eyedropper — a left-button drag changes NO selection, NO placement and NO journal,
//     and does NOT pan: the inert seams editor.paint.brush / editor.color.color fill
//     (D-tool_dispatch-3), NOT a select-fallback (D20).
//   * Always-on — with the BRUSH tool active, a Space-drag still pans and the wheel still zooms
//     (Constraint 3): the transient-viewport gestures survive every tool.
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/commands/selection.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/dockmodel/tool_rail.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/interact/pick.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "writer_session.hpp"
#include <GLES3/gl3.h>

using ace::app::CanvasView;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::commands::AppState;
using ace::dockmodel::ToolId;
using ace::dockmodel::ViewType;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  explicit ScratchDir(const char* tag)
      : root(std::filesystem::temp_directory_path() /
             (std::string("ace_tool_dispatch_e2e_") + tag)) {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// Geometry in COMPOSITION units, 1:1 with pane-relative device px (canvas#1's viewport starts at
// identity). Two opaque bounded solids overlapping in [90,120]^2 and one camera framing both — the
// same shape selection_e2e uses, so every extent is real and "click empty canvas" is testable.
constexpr double k_cell_edge = 80.0;
constexpr double k_a_at = 40.0; // cell A covers [40,120]^2
constexpr double k_b_at = 90.0; // cell B covers [90,170]^2
constexpr double k_hero_at = 20.0;
constexpr int k_hero_res = 160; // Hero covers [20,180]^2

constexpr float k_p_a_only = 60.0F;  // (60,60): cell A only, camera interior (click-through)
constexpr float k_p_empty_x = 5.0F;  // (5,60): outside every target and the camera border
constexpr float k_p_empty_y = 60.0F; // …and below the camera-picker overlay row
constexpr float k_a_corner = k_a_at + k_cell_edge; // (120,120): cell A's bottom-right gizmo handle

struct Seeded {
  arbc::ObjectId cell_a;
  arbc::ObjectId cell_b;
  arbc::ObjectId hero;
};

Seeded seed(AppState& state) {
  arbc::Document& doc = state.document();
  const arbc::ObjectId comp = doc.add_composition(256.0, 256.0);
  const arbc::Rect extent{0.0, 0.0, k_cell_edge, k_cell_edge};
  Seeded out;
  out.cell_a = doc.add_content(
      std::make_shared<arbc::SolidContent>(arbc::Rgba{0.6F, 0.0F, 0.0F, 1.0F}, extent));
  doc.attach_layer(comp, doc.add_layer(out.cell_a, arbc::Affine::translation(k_a_at, k_a_at)));
  out.cell_b = doc.add_content(
      std::make_shared<arbc::SolidContent>(arbc::Rgba{0.0F, 0.0F, 0.6F, 1.0F}, extent));
  doc.attach_layer(comp, doc.add_layer(out.cell_b, arbc::Affine::translation(k_b_at, k_b_at)));
  out.hero = ace::scene::add_camera(state.document(), state.registry(), "Hero",
                                    ace::scene::Resolution{k_hero_res, k_hero_res},
                                    arbc::Affine::translation(k_hero_at, k_hero_at));
  return out;
}

// The live placement of a cell — the scene truth the Select gizmo edits (the inert arms must leave
// it untouched). Mirrors tests/gizmo_e2e_test.cpp's helper.
std::optional<arbc::Affine> cell_placement(AppState& state, arbc::ObjectId id) {
  for (const ace::scene::Cell& c : ace::scene::cells(state.document(), state.registry())) {
    if (c.id == id) {
      return c.placement;
    }
  }
  return std::nullopt;
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
  ace::dock::Dockspace* dockspace;
  Seeded ids;
};

} // namespace

TEST_CASE("tool_dispatch e2e: the active modal tool routes a plain canvas drag (Select transforms, "
          "Pan pans, Brush/Eyedropper inert, Space/wheel always-on)") {
  ScratchDir scratch("main");
  ace::platform::NativeFileSystem fs;
  ace::testing::WriterSession session(scratch.root / "td");
  REQUIRE(session.ok());
  AppState& state = session.state();
  Seeded ids;
  session.on_writer([&] { ids = seed(state); });
  REQUIRE(ids.cell_a.valid());
  REQUIRE(ids.cell_b.valid());
  REQUIRE(ids.hero.valid());
  REQUIRE(state.selection().empty());

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 900;
  opts.height = 640;
  REQUIRE(shell.init(opts));

  CanvasView canvas(state, session.writer());
  ace::dock::Dockspace dockspace;
  // The Canvas body threads the rail's ACTIVE TOOL into draw_content — the seam this leaf builds.
  // Registered AFTER `dockspace` exists so the lambda can read `tools().active()` each frame (the
  // exact wiring shell.cpp performs; the tool is set below via `dockspace.tools().select(...)`).
  ace::views::register_view_body(ViewType::Canvas, [&canvas, &dockspace](std::string_view view_id) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content(view_id, static_cast<int>(avail.x), static_cast<int>(avail.y),
                        dockspace.tools().active());
  });

  ace::dockmodel::RecentProjects recent(scratch.root / "prefs", fs);
  NoopFolderDialog dialog;
  NoopLauncher launcher;
  ace::app::AppProjectGateway gateway(recent, fs, dialog, launcher, "/usr/bin/arbitraryeditor",
                                      state);
  gateway.set_edit_runner(
      [&canvas](const std::function<void()>& edit) { canvas.apply_edit(edit); });

  shell.set_draw_content([&dockspace, &canvas]() {
    dockspace.draw();
    canvas.reconcile(dockspace.layout().view_ids());
  });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&canvas, &state, &dockspace, ids};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "tool_dispatch", "select_pan_brush_eyedropper");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    CanvasView& canvas = *e2e->canvas;
    AppState& state = *e2e->state;
    ace::dock::Dockspace& dockspace = *e2e->dockspace;
    ace::commands::Selection& sel = state.selection();
    const arbc::Journal& journal = state.document().journal();
    const arbc::ObjectId cell_a = e2e->ids.cell_a;
    const arbc::ObjectId cell_b = e2e->ids.cell_b;

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
    // Set the active tool and let the Canvas body pick it up before the next gesture (the body
    // reads `tools().active()` each frame; the Yield handoff publishes the write to the draw).
    const auto set_tool = [&](ToolId t) {
      dockspace.tools().select(t);
      ctx->Yield(3);
    };

    ctx->MouseMove("canvas#1/##canvas_nav"); // establish the viewport for raw-position moves

    // --- Select: the gate preserves the predecessor pick + marquee + gizmo. -------------------
    set_tool(ToolId::Select);
    IM_CHECK(dockspace.tools().active() == ToolId::Select);
    click_at(at(k_p_a_only, k_p_a_only));
    IM_CHECK(sel.primary() == cell_a); // a click picks (ObjectInteraction arm ran)

    drag(at(k_p_empty_x, k_p_empty_y), at(160.0F, 160.0F)); // marquee spans both cells
    IM_CHECK(sel.contains(cell_a));
    IM_CHECK(sel.contains(cell_b));

    click_at(at(k_p_a_only, k_p_a_only)); // reselect A alone → its transform gizmo shows
    IM_CHECK(sel.primary() == cell_a);
    IM_CHECK(sel.size() == 1);
    const arbc::Affine place_before = *cell_placement(state, cell_a);
    const std::size_t depth_before_xf = journal.depth();
    drag(at(k_a_corner, k_a_corner), at(k_a_corner + 30.0F, k_a_corner + 30.0F)); // BR handle out
    IM_CHECK(journal.depth() == depth_before_xf + 1);            // ONE set_layer_transform
    IM_CHECK(!(*cell_placement(state, cell_a) == place_before)); // the placement really moved

    // --- Pan: a left-drag pans the transient camera, touching no selection and no journal. -----
    set_tool(ToolId::Pan);
    IM_CHECK(dockspace.tools().active() == ToolId::Pan);
    const arbc::ObjectId sel_before_pan = sel.primary();
    const arbc::Affine view_before_pan = canvas.primary_framing().camera;
    const std::size_t depth_before_pan = journal.depth();
    const std::uint64_t rev_before_pan = state.document().pin()->revision();
    drag(at(k_p_a_only, k_p_a_only), at(k_p_a_only + 40.0F, k_p_a_only + 25.0F)); // plain left-drag
    IM_CHECK(!(canvas.primary_framing().camera == view_before_pan));              // the VIEW panned
    IM_CHECK(sel.primary() == sel_before_pan);     // selection untouched
    IM_CHECK(journal.depth() == depth_before_pan); // no commit (D15)
    IM_CHECK(state.document().pin()->revision() == rev_before_pan);

    // --- Brush: a left-drag is inert — no selection, no placement, no journal, and NO pan. ------
    set_tool(ToolId::Brush);
    IM_CHECK(dockspace.tools().active() == ToolId::Brush);
    {
      const arbc::ObjectId sel_before = sel.primary();
      const arbc::Affine place_b = *cell_placement(state, cell_a);
      const arbc::Affine view_before = canvas.primary_framing().camera;
      const std::size_t depth_before = journal.depth();
      drag(at(k_p_a_only, k_p_a_only), at(k_p_a_only + 40.0F, k_p_a_only + 25.0F));
      IM_CHECK(sel.primary() == sel_before);                    // NOT a select-fallback (D20)
      IM_CHECK(*cell_placement(state, cell_a) == place_b);      // did not transform
      IM_CHECK(journal.depth() == depth_before);                // nothing committed
      IM_CHECK(canvas.primary_framing().camera == view_before); // and Brush is not Pan
    }

    // --- Eyedropper: the inert twin — same guarantees. -----------------------------------------
    set_tool(ToolId::Eyedropper);
    IM_CHECK(dockspace.tools().active() == ToolId::Eyedropper);
    {
      const arbc::ObjectId sel_before = sel.primary();
      const arbc::Affine place_b = *cell_placement(state, cell_a);
      const arbc::Affine view_before = canvas.primary_framing().camera;
      const std::size_t depth_before = journal.depth();
      drag(at(k_p_a_only, k_p_a_only), at(k_p_a_only + 40.0F, k_p_a_only + 25.0F));
      IM_CHECK(sel.primary() == sel_before);
      IM_CHECK(*cell_placement(state, cell_a) == place_b);
      IM_CHECK(journal.depth() == depth_before);
      IM_CHECK(canvas.primary_framing().camera == view_before);
    }

    // --- Always-on under Brush: Space still pans, the wheel still zooms (Constraint 3). --------
    set_tool(ToolId::Brush);
    const arbc::Affine view_before_space = canvas.primary_framing().camera;
    ctx->KeyDown(ImGuiKey_Space);
    drag(at(k_p_a_only, k_p_a_only), at(k_p_a_only + 24.0F, k_p_a_only + 16.0F)); // Space-drag
    ctx->KeyUp(ImGuiKey_Space);
    ctx->Yield(3);
    IM_CHECK(!(canvas.primary_framing().camera == view_before_space)); // Space pans under Brush

    const double units_before = canvas.scale_bar_units("canvas#1");
    IM_CHECK(units_before > 0.0);
    ctx->MouseMoveToPos(at(k_p_a_only, k_p_a_only));
    ctx->MouseWheelY(10.0F); // one big notch zooms in
    ctx->Yield(3);
    const double units_after = canvas.scale_bar_units("canvas#1");
    IM_CHECK(units_after > 0.0);
    IM_CHECK(units_after < units_before); // wheel still zooms under Brush
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
