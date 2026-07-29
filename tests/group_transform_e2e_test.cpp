// editor.cells.group_transform — the GROUP transform-gizmo UI e2e (docs §9, offscreen software-GL
// lane; modeled on tests/gizmo_e2e_test.cpp). Boots the shell over a REAL commands::AppState, seeds
// bounded raster cells + a snap neighbour + a shot, and drives the multi-select group gizmo in the
// Canvas body by raw mouse position. Asserts, over a ≥2 selection (D-group_transform-*): (i) a
// corner drag scales BOTH placements through apply_edit as ONE journal entry — the flip of the
// single-object e2e's "two cells → no gizmo, body drag does nothing" — with content_bounds
// UNCHANGED (D8) and one undo restoring both; (ii) a body drag SNAPS the whole union to a neighbour
// and Cmd/Ctrl bypasses; (iii) a Shift rotate commits a 15°-multiple rotation on both; and (iv) a
// group drag over a mixed camera+cell selection re-places the cell AND the camera frame together
// (D7). Writer ops run on the MAIN thread through apply_edit.
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
constexpr double k_a_at = 40.0;  // cell A covers [40,100]^2
constexpr double k_b_at = 160.0; // cell B covers [160,220]x[40,100]
constexpr double k_c_at = 300.0; // cell C (snap neighbour, NOT selected) left edge at 300
constexpr double k_edge = 60.0;  // every raster is 60x60
constexpr double k_cam_at_y = 200.0;
constexpr int k_cam_res = 60; // Hero frame covers [40,100]x[200,260]

struct ScratchDir {
  std::filesystem::path root;
  explicit ScratchDir(const char* tag)
      : root(std::filesystem::temp_directory_path() / (std::string("ace_group_e2e_") + tag)) {
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
  doc.add_composition(400.0, 320.0);
  Seeded out;
  // An OPAQUE unbounded backdrop first (so the render gate issues frames — see gizmo_e2e_test.cpp).
  ace::scene::add_cell(doc, state.registry(), "org.arbc.solid", "0.15,0.2,0.25,1",
                       arbc::Affine::identity());
  const auto a = ace::scene::add_cell(doc, state.registry(), "org.arbc.raster", "60x60",
                                      arbc::Affine::translation(k_a_at, k_a_at));
  const auto b = ace::scene::add_cell(doc, state.registry(), "org.arbc.raster", "60x60",
                                      arbc::Affine::translation(k_b_at, k_a_at));
  // C — a NON-selected neighbour to the right of B, so a group body-move can snap the union's right
  // edge onto C's left edge (Constraint 7).
  ace::scene::add_cell(doc, state.registry(), "org.arbc.raster", "60x60",
                       arbc::Affine::translation(k_c_at, k_a_at));
  out.cell_a = a.has_value() ? *a : arbc::ObjectId{};
  out.cell_b = b.has_value() ? *b : arbc::ObjectId{};
  out.camera = ace::scene::add_camera(doc, state.registry(), "Hero",
                                      ace::scene::Resolution{k_cam_res, k_cam_res},
                                      arbc::Affine::translation(k_a_at, k_cam_at_y));
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

TEST_CASE("group transform e2e: multi-select group gizmo — scale, snap, rotate, mixed camera") {
  ScratchDir scratch("main");
  ace::platform::NativeFileSystem fs;
  ace::testing::WriterSession session(scratch.root / "gt");
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
  opts.width = 1100;
  opts.height = 700;
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
  ImGuiTest* test = IM_REGISTER_TEST(engine, "group_transform", "group_gizmo");
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
    // Select A then Shift-add B → a two-cell selection (the seed's opaque backdrop covers the whole
    // plane, so an empty-canvas marquee can't arm; two clicks build the same ≥2 selection the group
    // gizmo keys on).
    const auto select_ab = [&]() {
      click(at(k_a_at + 30.0, k_a_at + 30.0)); // cell A body
      ctx->KeyDown(ImGuiMod_Shift);
      click(at(k_b_at + 30.0, k_a_at + 30.0)); // shift-add cell B
      ctx->KeyUp(ImGuiMod_Shift);
      IM_CHECK(sel.size() == 2);
    };

    ctx->MouseMove("canvas#1/##canvas_nav"); // establish the viewport for raw-position moves
    select_ab();

    // The union of A[40,100]^2 and B[160,220]x[40,100] is [40,220]x[40,100]; TL=(40,40),
    // BR=(220,100), center pivot=(130,70).

    // --- (i) corner scale over TWO cells: BOTH placements scale, ONE entry, bounds hold, undo.
    // ---- This is the FLIP of the single-object gizmo e2e, where a two-cell body drag transformed
    // nothing.
    {
      const std::optional<arbc::Rect> a_bounds = cell_bounds(state, ids.cell_a);
      const std::optional<arbc::Rect> b_bounds = cell_bounds(state, ids.cell_b);
      REQUIRE(a_bounds.has_value());
      const std::size_t depth = journal.cursor();
      const arbc::Affine a0 = *cell_placement(state, ids.cell_a);
      const arbc::Affine b0 = *cell_placement(state, ids.cell_b);
      drag(at(220.0, 100.0), at(260.0, 140.0)); // union BR handle, dragged outward
      IM_CHECK(journal.cursor() == depth + 1);  // the whole group is ONE journal entry
      const arbc::Affine a1 = *cell_placement(state, ids.cell_a);
      const arbc::Affine b1 = *cell_placement(state, ids.cell_b);
      IM_CHECK(!(a1 == a0)); // BOTH members re-placed
      IM_CHECK(!(b1 == b0));
      IM_CHECK(a1.a > a0.a);                                // scaled up
      IM_CHECK(cell_bounds(state, ids.cell_a) == a_bounds); // content_bounds UNCHANGED (D8)
      IM_CHECK(cell_bounds(state, ids.cell_b) == b_bounds);
      undo_one(); // ONE press restores EVERY member
      IM_CHECK(*cell_placement(state, ids.cell_a) == a0);
      IM_CHECK(*cell_placement(state, ids.cell_b) == b0);
    }

    // --- (ii) body drag snaps the union's right edge onto C's left edge; Cmd/Ctrl bypasses.
    // -------
    {
      const std::size_t depth = journal.cursor();
      const arbc::Affine a0 = *cell_placement(state, ids.cell_a);
      // Drag the union body right by +72: union right (220) -> 292, within tol of C's left edge
      // (300), so the whole group snaps +80 and A slides 40 -> 120.
      drag(at(k_a_at + 30.0, k_a_at + 30.0), at(k_a_at + 30.0 + 72.0, k_a_at + 30.0));
      IM_CHECK(journal.cursor() == depth + 1);
      IM_CHECK(cell_placement(state, ids.cell_a)->tx ==
               Catch::Approx(120.0).margin(0.5)); // snapped
      undo_one();
      IM_CHECK(*cell_placement(state, ids.cell_a) == a0);

      const std::size_t depth_c = journal.cursor();
      ctx->KeyDown(ImGuiMod_Ctrl); // Cmd/Ctrl bypasses snap (§6:258)
      drag(at(k_a_at + 30.0, k_a_at + 30.0), at(k_a_at + 30.0 + 72.0, k_a_at + 30.0));
      ctx->KeyUp(ImGuiMod_Ctrl);
      IM_CHECK(journal.cursor() == depth_c + 1);
      const double free_tx = cell_placement(state, ids.cell_a)->tx;
      IM_CHECK(free_tx == Catch::Approx(112.0).margin(2.0)); // moved by the raw drag, NOT snapped
      IM_CHECK(!(free_tx == Catch::Approx(120.0).margin(0.5)));
      undo_one();
    }

    // --- (iii) a rotate-zone drag with Shift rotates BOTH members by a 15° multiple, one entry.
    // ---
    {
      const std::size_t depth = journal.cursor();
      const arbc::Affine a0 = *cell_placement(state, ids.cell_a);
      const arbc::Affine b0 = *cell_placement(state, ids.cell_b);
      ctx->KeyDown(ImGuiMod_Shift);
      drag(at(232.0, 112.0), at(150.0, 180.0)); // just outside the union BR corner, sweep an angle
      ctx->KeyUp(ImGuiMod_Shift);
      IM_CHECK(journal.cursor() == depth + 1);
      const arbc::Affine a1 = *cell_placement(state, ids.cell_a);
      const arbc::Affine b1 = *cell_placement(state, ids.cell_b);
      IM_CHECK(!(a1 == a0));
      IM_CHECK(!(b1 == b0));
      IM_CHECK(a1.b != 0.0); // rotated (off-axis linear part) about the shared pivot
      const double angle = std::atan2(a1.b, a1.a);
      IM_CHECK(std::abs(std::remainder(angle, k_pi / 12.0)) < 0.03); // a 15° multiple (Shift snap)
      undo_one();
      IM_CHECK(*cell_placement(state, ids.cell_a) == a0);
      IM_CHECK(*cell_placement(state, ids.cell_b) == b0);
    }

    // --- (iv) a Cmd/Ctrl + edge drag shears BOTH members about the shared pivot, one entry.
    // -------
    {
      const std::size_t depth = journal.cursor();
      const arbc::Affine a0 = *cell_placement(state, ids.cell_a);
      const arbc::Affine b0 = *cell_placement(state, ids.cell_b);
      ctx->KeyDown(ImGuiMod_Ctrl);
      drag(at(130.0, 40.0), at(160.0, 40.0)); // the union TOP edge handle, dragged horizontally
      ctx->KeyUp(ImGuiMod_Ctrl);
      IM_CHECK(journal.cursor() == depth + 1);
      IM_CHECK(cell_placement(state, ids.cell_a)->c != 0.0); // a real off-diagonal shear on both
      IM_CHECK(cell_placement(state, ids.cell_b)->c != 0.0);
      undo_one();
      IM_CHECK(*cell_placement(state, ids.cell_a) == a0);
      IM_CHECK(*cell_placement(state, ids.cell_b) == b0);
    }

    // --- (v) dragging the group pivot dot is UI-only: it commits nothing (D-group_transform-5).
    // ---
    {
      const arbc::Affine a0 = *cell_placement(state, ids.cell_a);
      const std::size_t depth = journal.cursor();
      drag(at(130.0, 70.0), at(150.0, 90.0));             // the pivot dot sits at the union center
      IM_CHECK(journal.cursor() == depth);                // no journal entry
      IM_CHECK(*cell_placement(state, ids.cell_a) == a0); // the placement did not change
    }

    // --- (vi) a group drag over a MIXED camera+cell selection re-places the cell AND the frame.
    // ---
    {
      click(at(k_a_at + 30.0, k_a_at + 30.0)); // re-select just A (single selection)
      IM_CHECK(sel.size() == 1);
      ctx->KeyDown(ImGuiMod_Shift);
      click(at(k_a_at, k_cam_at_y)); // shift-add the Hero frame by its top-left border corner
      ctx->KeyUp(ImGuiMod_Shift);
      IM_CHECK(sel.size() == 2);
      IM_CHECK(sel.contains(ids.camera));

      // Union of A[40,100]^2 and Hero[40,100]x[200,260] is [40,100]x[40,260]; BR corner = (100,260)
      // coincides with the Hero frame's BR — the group gizmo (not the frame gizmo) claims it
      // because the camera is part of the ≥2 selection (D-group_transform-4).
      const std::size_t depth = journal.cursor();
      const arbc::Affine a0 = *cell_placement(state, ids.cell_a);
      const arbc::Affine frame0 = hero_frame(state, ids.camera);
      drag(at(100.0, 260.0), at(140.0, 300.0));              // union BR handle
      IM_CHECK(journal.cursor() == depth + 1);               // one entry for the whole mixed group
      IM_CHECK(!(*cell_placement(state, ids.cell_a) == a0)); // the cell moved
      IM_CHECK(!(hero_frame(state, ids.camera) == frame0));  // the camera frame moved too (D7)
      undo_one();
      IM_CHECK(*cell_placement(state, ids.cell_a) == a0);
      IM_CHECK(hero_frame(state, ids.camera) == frame0);
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
