// editor.cells.gizmo — the cell transform-gizmo UI e2e (docs §9, offscreen software-GL lane;
// modeled on tests/camera_manip_e2e_test.cpp / tests/selection_e2e_test.cpp). Boots the shell over
// a REAL commands::AppState, seeds two bounded raster cells + a shot, and drives the DIRECT-
// manipulation cell gizmo in the Canvas body by raw mouse position. Asserts, over the ONE selected
// cell (D-gizmo-2): (i) a corner drag scales the PLACEMENT through apply_edit — one journal entry,
// placement changed, content_bounds UNCHANGED (D8) — and undo restores it; (ii) a body drag near a
// second cell's edge SNAPS (asserted on the committed affine) and a Cmd/Ctrl-drag does NOT snap;
// (iii) a rotate-zone drag with Shift commits a 15°-multiple rotation; (iv) an arrow-nudge is one
// entry; (v) a Cmd/Ctrl + edge drag shears; (vi) with TWO cells selected no gizmo appears and a
// body drag does not transform; (vii) a selected camera still drives the shipped frame gizmo; and
// (viii) Space during a body press pans the view, inert on the cell. Writer ops run on the MAIN
// thread through apply_edit.
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/commands/selection.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/interact/interact.hpp>
#include <ace/interact/pick.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
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

constexpr double k_pi = 3.14159265358979323846;
constexpr double k_a_at = 40.0;    // cell A covers [40,100]^2
constexpr double k_b_at = 200.0;   // cell B covers [200,260]^2
constexpr double k_edge = 60.0;    // both cells are 60x60 rasters
constexpr double k_cam_at = 180.0; // Hero frame covers [180,260]x[40,120]
constexpr int k_cam_res = 80;

struct ScratchDir {
  std::filesystem::path root;
  explicit ScratchDir(const char* tag)
      : root(std::filesystem::temp_directory_path() / (std::string("ace_gizmo_e2e_") + tag)) {
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
  arbc::ObjectId cell_a;
  arbc::ObjectId cell_b;
  arbc::ObjectId camera;
};

Seeded seed(AppState& state) {
  arbc::Document& doc = state.document();
  doc.add_composition(320.0, 320.0);
  Seeded out;
  // An OPAQUE backdrop, added FIRST so it sits at the bottom of the stack. A freshly-inserted
  // raster cell is empty (all-transparent), and the canvas render gate withholds an
  // all-transparent frame — so without opaque content the pane never issues its first frame and
  // the `frames_issued >= 1` wait below times out (see tests/export_e2e_test.cpp). Being unbounded
  // it has no extent: every body/handle pick still resolves to the raster cells (added on top of
  // it) and it contributes no snap edge.
  ace::scene::add_cell(doc, state.registry(), "org.arbc.solid", "0.15,0.2,0.25,1",
                       arbc::Affine::identity());
  const auto a = ace::scene::add_cell(doc, state.registry(), "org.arbc.raster", "60x60",
                                      arbc::Affine::translation(k_a_at, k_a_at));
  const auto b = ace::scene::add_cell(doc, state.registry(), "org.arbc.raster", "60x60",
                                      arbc::Affine::translation(k_b_at, k_b_at));
  out.cell_a = a.has_value() ? *a : arbc::ObjectId{};
  out.cell_b = b.has_value() ? *b : arbc::ObjectId{};
  out.camera = ace::scene::add_camera(doc, state.registry(), "Hero",
                                      ace::scene::Resolution{k_cam_res, k_cam_res},
                                      arbc::Affine::translation(k_cam_at, k_a_at));
  return out;
}

bool layout_contains(const ace::dock::Dockspace& d, const char* id) {
  const std::vector<std::string> ids = d.layout().view_ids();
  return std::find(ids.begin(), ids.end(), std::string(id)) != ids.end();
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
  ace::dock::Dockspace* dockspace;
  AppState* state;
  Seeded ids;
  std::atomic<bool> request_undo{false};
  std::atomic<bool> undo_done{false};
};

// The live placement / content extent of a cell (the scene truth the gizmo edits).
std::optional<arbc::Affine> cell_placement(AppState& state, arbc::ObjectId id) {
  for (const ace::scene::Cell& c : ace::scene::cells(state.document(), state.registry())) {
    if (c.id == id) {
      return c.placement;
    }
  }
  return std::nullopt;
}
std::optional<arbc::Rect> cell_bounds(AppState& state, arbc::ObjectId id) {
  for (const ace::scene::Cell& c : ace::scene::cells(state.document(), state.registry())) {
    if (c.id == id) {
      return c.content_bounds;
    }
  }
  return std::nullopt;
}
arbc::Affine hero_frame(AppState& state, arbc::ObjectId id) {
  for (const ace::scene::Camera& c : ace::scene::cameras(state.document())) {
    if (c.id == id) {
      return c.frame;
    }
  }
  return arbc::Affine::identity();
}

} // namespace

TEST_CASE("gizmo e2e: cell transform gizmo — scale, snap, rotate, nudge, shear, gating") {
  ScratchDir scratch("main");
  ace::platform::NativeFileSystem fs;
  ace::testing::WriterSession session(scratch.root / "gz");
  REQUIRE(session.ok());
  AppState& state = session.state();
  Seeded ids;
  session.on_writer([&] { ids = seed(state); });
  REQUIRE(ids.cell_a.valid());
  REQUIRE(ids.cell_b.valid());
  REQUIRE(ids.camera.valid());

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

  shell.set_draw_content([&dockspace, &canvas]() {
    dockspace.draw();
    canvas.reconcile(dockspace.layout().view_ids());
  });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&canvas, &dockspace, &state};
  e2e.ids = ids;
  ImGuiTest* test = IM_REGISTER_TEST(engine, "gizmo", "cell_transform_gizmo");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    CanvasView& canvas = *e2e->canvas;
    ace::dock::Dockspace& dockspace = *e2e->dockspace;
    AppState& state = *e2e->state;
    ace::commands::Selection& sel = state.selection();
    const Seeded ids = e2e->ids;
    const arbc::Journal& journal = state.document().journal();

    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));
    ctx->WindowFocus("canvas#1");
    ctx->Yield(3);

    // canvas#1's viewport is identity: a composition point maps to screen as pane_origin + comp.
    const auto pane_origin = [&]() {
      const ImGuiTestItemInfo info = ctx->ItemInfo("canvas#1/##canvas_nav");
      return ImVec2(info.RectFull.Min.x, info.RectFull.Min.y);
    };
    const ImVec2 origin = pane_origin();
    IM_CHECK(origin.x >= 0.0F && origin.y >= 0.0F);
    const auto at = [&](double cx, double cy) {
      return ImVec2(origin.x + static_cast<float>(cx), origin.y + static_cast<float>(cy));
    };
    const auto drag = [&](ImVec2 from, ImVec2 to) {
      ctx->MouseMoveToPos(from);
      ctx->MouseDown(0);
      ctx->MouseMoveToPos(to);
      ctx->MouseUp(0);
      ctx->Yield(3);
    };
    const auto click = [&](ImVec2 pos) {
      ctx->MouseMoveToPos(pos);
      ctx->MouseDown(0);
      ctx->MouseUp(0);
      ctx->Yield(2);
    };
    const auto undo_one = [&]() {
      e2e->request_undo.store(true);
      IM_CHECK(pump_until(ctx, [&] { return e2e->undo_done.load(); }));
      e2e->undo_done.store(false);
      ctx->Yield(2);
    };

    ctx->MouseMove("canvas#1/##canvas_nav"); // establish the viewport for raw-position moves

    // Select cell A (a body click; the gizmo appears next frame for the ONE selected cell).
    click(at(55.0, 80.0));
    IM_CHECK(sel.primary() == ids.cell_a);
    IM_CHECK(sel.size() == 1);
    const std::optional<arbc::Rect> bounds_seed = cell_bounds(state, ids.cell_a);
    REQUIRE(bounds_seed.has_value());

    // --- (i) corner scale: drag A's bottom-right handle out => placement scales, bounds hold. ----
    {
      const std::size_t depth = journal.cursor();
      const arbc::Affine before = *cell_placement(state, ids.cell_a);
      drag(at(k_a_at + k_edge, k_a_at + k_edge),
           at(k_a_at + k_edge + 40.0, k_a_at + k_edge + 40.0));
      IM_CHECK(journal.cursor() == depth + 1); // ONE journal entry per gesture
      const arbc::Affine after = *cell_placement(state, ids.cell_a);
      IM_CHECK(!(after == before));                            // the placement scaled up
      IM_CHECK(after.a > before.a);                            // ... larger
      IM_CHECK(cell_bounds(state, ids.cell_a) == bounds_seed); // content_bounds UNCHANGED (D8)
      undo_one();
      IM_CHECK(*cell_placement(state, ids.cell_a) == before);
    }

    // --- (ii) body drag snaps A's right edge onto B's left edge; Cmd/Ctrl bypasses snap. ---------
    {
      const std::size_t depth = journal.cursor();
      drag(at(55.0, 80.0), at(55.0 + 95.0, 80.0)); // move A right toward B (right edge -> ~195)
      IM_CHECK(journal.cursor() == depth + 1);
      const arbc::Affine snapped = *cell_placement(state, ids.cell_a);
      IM_CHECK(snapped.tx == Catch::Approx(k_b_at - k_edge).margin(0.5)); // right edge flush on 200
      undo_one();

      const std::size_t depth_c = journal.cursor();
      ctx->KeyDown(ImGuiMod_Ctrl); // Cmd/Ctrl bypasses snap (§6:258)
      drag(at(55.0, 80.0), at(55.0 + 95.0, 80.0));
      ctx->KeyUp(ImGuiMod_Ctrl);
      IM_CHECK(journal.cursor() == depth_c + 1);
      const arbc::Affine free = *cell_placement(state, ids.cell_a);
      IM_CHECK(free.tx == Catch::Approx(k_a_at + 95.0).margin(2.0)); // NOT snapped to 140
      IM_CHECK(!(free.tx == Catch::Approx(k_b_at - k_edge).margin(0.5)));
      undo_one();
    }

    // --- (iii) a rotate-zone drag with Shift commits a 15°-multiple rotation. --------------------
    {
      const std::size_t depth = journal.cursor();
      ctx->KeyDown(ImGuiMod_Shift);
      drag(at(112.0, 112.0), at(40.0, 120.0)); // grab just outside the BR corner, sweep an angle
      ctx->KeyUp(ImGuiMod_Shift);
      IM_CHECK(journal.cursor() == depth + 1);
      const arbc::Affine rot = *cell_placement(state, ids.cell_a);
      IM_CHECK(rot.b != 0.0); // rotated (off-axis linear part)
      const double angle = std::atan2(rot.b, rot.a);
      IM_CHECK(std::abs(std::remainder(angle, k_pi / 12.0)) < 0.03); // a 15° multiple (Shift snap)
      undo_one();
    }

    // --- (iv) arrow-nudge is one committed entry that translates the placement. ------------------
    {
      ctx->MouseMove("canvas#1/##canvas_nav"); // keep the pane hovered
      const std::size_t depth = journal.cursor();
      const arbc::Affine before = *cell_placement(state, ids.cell_a);
      ctx->KeyPress(ImGuiKey_RightArrow);
      ctx->Yield(3);
      IM_CHECK(journal.cursor() == depth + 1);
      const arbc::Affine after = *cell_placement(state, ids.cell_a);
      IM_CHECK(after.tx == Catch::Approx(before.tx + ace::interact::k_cell_nudge));
      undo_one();
    }

    // --- (v) a Cmd/Ctrl + edge drag shears (advanced modifier), about the pivot. -----------------
    {
      const std::size_t depth = journal.cursor();
      ctx->KeyDown(ImGuiMod_Ctrl);
      drag(at(70.0, k_a_at), at(100.0, k_a_at)); // the TOP edge handle, dragged horizontally
      ctx->KeyUp(ImGuiMod_Ctrl);
      IM_CHECK(journal.cursor() == depth + 1);
      const arbc::Affine sheared = *cell_placement(state, ids.cell_a);
      IM_CHECK(sheared.c != 0.0); // a real off-diagonal shear
      undo_one();
    }

    // --- (vi) with TWO cells selected, no transform gizmo; a body drag does not transform. -------
    {
      ctx->KeyDown(ImGuiMod_Shift);
      click(at(k_b_at + 30.0, k_b_at + 30.0)); // shift-add cell B
      ctx->KeyUp(ImGuiMod_Shift);
      IM_CHECK(sel.size() == 2);
      const std::size_t depth = journal.cursor();
      const arbc::Affine before = *cell_placement(state, ids.cell_a);
      drag(at(55.0, 80.0), at(55.0 + 30.0, 80.0)); // a body drag transforms NOTHING now
      IM_CHECK(journal.cursor() == depth);         // no committed transform
      IM_CHECK(*cell_placement(state, ids.cell_a) == before);
      click(at(55.0, 80.0)); // re-select just A for the following sections
      IM_CHECK(sel.primary() == ids.cell_a);
      IM_CHECK(sel.size() == 1);
    }

    // --- (vii) a selected CAMERA still shows and drives the shipped frame gizmo unchanged. -------
    {
      const arbc::Affine frame_before = hero_frame(state, ids.camera);
      const std::size_t depth = journal.cursor();
      drag(at(k_cam_at + k_cam_res, k_a_at + k_cam_res), // Hero's bottom-right frame corner
           at(k_cam_at + k_cam_res + 30.0, k_a_at + k_cam_res + 30.0));
      IM_CHECK(sel.primary() == ids.camera); // the camera got selected + reframed by one gesture
      IM_CHECK(journal.cursor() == depth + 1);
      IM_CHECK(!(hero_frame(state, ids.camera) == frame_before));
      undo_one();
      click(at(55.0, 80.0)); // back to cell A
      IM_CHECK(sel.primary() == ids.cell_a);
    }

    // --- (viii) dragging the pivot dot is UI-only: it commits nothing (D-gizmo-4). ---------------
    {
      const arbc::Affine before = *cell_placement(state, ids.cell_a);
      const std::size_t depth = journal.cursor();
      drag(at(70.0, 70.0), at(85.0, 85.0)); // the pivot dot sits at the placed-box center
      IM_CHECK(journal.cursor() == depth);  // no journal entry
      IM_CHECK(*cell_placement(state, ids.cell_a) == before); // the placement did not change
    }

    // --- (ix) Space during a body press pans the VIEW, inert on the cell (Constraint 10). --------
    // Last, because the Space-drag pans canvas#1's viewport (a lasting session change).
    {
      const arbc::Affine before = *cell_placement(state, ids.cell_a);
      const std::size_t depth = journal.cursor();
      ctx->KeyDown(ImGuiKey_Space);
      drag(at(55.0, 80.0), at(85.0, 110.0));
      ctx->KeyUp(ImGuiKey_Space);
      IM_CHECK(*cell_placement(state, ids.cell_a) == before); // the cell did NOT move
      IM_CHECK(journal.cursor() == depth);                    // nothing journaled
    }

    IM_CHECK(layout_contains(dockspace, "canvas#1"));
  };
  ImGuiTestEngine_QueueTest(engine, test);

  const int k_max_frames = 200000;
  int frames = 0;
  while (!ImGuiTestEngine_IsTestQueueEmpty(engine) && frames < k_max_frames) {
    shell.new_frame();
    shell.draw_ui();
    shell.render();
    ImGuiTestEngine_PostSwap(engine);
    if (e2e.request_undo.exchange(false)) {
      canvas.apply_edit([&] { (void)ace::commands::undo(state); });
      e2e.undo_done.store(true);
    }
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
