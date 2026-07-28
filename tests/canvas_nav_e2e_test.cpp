// editor.canvas.nav — Canvas navigation UI e2e (docs/01-architecture.md §9, the
// offscreen software-GL lane; modeled on tests/canvas_view_e2e_test.cpp /
// tests/multi_canvas_e2e_test.cpp). Boots the shell over a REAL commands::AppState,
// seeds a NESTED-composition document (so a deep wheel-zoom engages the library's
// re-anchoring), registers the driver-backed Canvas body, and drives the stable
// canvas#1 view BY ID. Asserts: (i) a simulated wheel over the pane advances the
// published frame AND raises anchor_depth(canvas#1) — zoom engaged end-to-end through
// the interact math + camera channel; (ii) a Space-held drag pans (the frame advances);
// (iii) the scale-bar overlay's labelled length changes after the zoom and, on reset-to-fit
// (F), snaps to the authored-bounds fit — reading MORE units than the initial identity
// framing because the 2048x2048 canvas frames below scale 1 in the pane (editor.canvas.
// fit_bounds / D-fit_bounds-1). NOT a byte-exact golden — ImGui chrome + software-GL pixels are
// flaky by construction (the tool_rail precedent); the byte-exactness lives in the CPU golden
// tests/canvas_host_test.cpp. Drive by widget/view id, assert the resulting state.
#include <ace/app/canvas_view.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/commands/selection.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/cell.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/expected.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h> // ImGuiWindow
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

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
      : root(std::filesystem::temp_directory_path() / (std::string("ace_canvas_nav_e2e_") + tag)) {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// A NESTED document: a root composition holding one layer whose content is a child
// composition placed at the identity edge (the child covers the whole root, so a zoom
// about the pane centre keeps the descendant IN VIEW). Zooming past the well-conditioned
// band re-anchors down into the child, so anchor_depth(canvas#1) rises — the deep-zoom
// rebasing the wheel gesture must engage. The root is the lowest-id composition, so the
// Document-bound viewport anchors there. The canvas is sized LARGER than the shell window
// so the identity framing fills the pane — a zoom about the pane centre then focuses on
// composition content (not empty space beyond the canvas), so a descendant stays in view.
//
// A full-frame INLINE solid background sits on the root UNDER the child: a nested child
// composition renders no straight-alpha coverage in-view on its own (it settles via a
// bridge binding the interactive host does not wire), so without the background the first
// composited frame is all-transparent. Now that editor.canvas.blank_first_frame withholds
// the published sequence until a frame composites non-empty content, that transparent frame
// would never publish and frames_issued("canvas#1") would never advance. The background
// makes the first frame genuinely non-blank (covered content the compositor resolves inline)
// so the content-gated sequence advances; the nested child is retained for the re-anchor
// zoom test, and no assertion below depends on the child's pixels.
constexpr double k_nav_canvas = 2048.0;
void seed_nested(AppState& state) {
  arbc::Document& doc = state.document();
  const arbc::ObjectId root = doc.add_composition(k_nav_canvas, k_nav_canvas);
  const arbc::ObjectId child = doc.add_composition(k_nav_canvas, k_nav_canvas);
  const arbc::ObjectId leaf =
      doc.add_content(std::make_shared<arbc::SolidContent>(ace::project::k_probe_color));
  doc.attach_layer(child, doc.add_layer(leaf, arbc::Affine::identity()));
  const arbc::ObjectId bg =
      doc.add_content(std::make_shared<arbc::SolidContent>(arbc::Rgba{0.05F, 0.05F, 0.08F, 1.0F}));
  doc.attach_layer(root, doc.add_layer(bg, arbc::Affine::identity()));
  doc.attach_layer(root, doc.add_layer(child, arbc::Affine::identity()));
}

// The canvas renders off the UI thread; pump (yielding CPU to the render thread) until
// `ready()` or a wall-clock deadline — holds under a sanitizer build's slowdown.
template <class Ready> bool pump_until(ImGuiTestContext* ctx, Ready ready) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (std::chrono::steady_clock::now() < deadline) {
    if (ready()) {
      return true;
    }
    ctx->Yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return ready();
}

// Let the scene settle so a later frame advance is attributable to the gesture, not a
// still-settling frame racing the assertion. The published sequence is now gated on content
// (editor.canvas.blank_first_frame), so this quiet window soundly detects a settled, non-blank
// scene — it can no longer go "quiet" on a blank first frame that the driver never published,
// which is exactly what made a frame-count settle unsound before this leaf.
//
// BOUNDED by the same wall clock `pump_until` uses, because a quiet window is a HOPE, not a
// guarantee: a canvas is only obliged to idle once the library stops owing it a follow-up frame,
// and this deadline is general lane protection against ANY canvas that fails to reach that state,
// not a workaround for one bug. Unbounded, this helper would never return on such a canvas: the
// ImGui Test Engine's 60s `ConfigWatchdogKillTest` fires, the test is recorded as failed
// (count_success == 0), and the binary burns minutes of CI wall clock doing it (a 16s
// `ace_shell_test` once became a 566s failure this way). The deadline makes the wait an
// optimisation — every assertion downstream of it reads UI-thread state (`scale_bar_units` is
// recomputed from `Presenter::camera` on the drawing thread each frame) or a strict frame-count
// advance, so a settle that times out costs attribution sharpness, not soundness.
//
// The magnified raster that first motivated this deadline is no longer such a canvas. A deep
// magnification of an `org.arbc.raster` cell once left arbc's `InteractiveRenderer::render_frame`
// reporting `schedule_follow_up` forever (arrival damage still mapping to a non-empty device
// region), so the host re-drove forever and `frames_issued` advanced at the frame rate
// indefinitely. ruoso/arbitrarycomposer#18 (libarbc v0.4.0, pinned by editor.canvas.arbc_v040)
// fixed the kind to render at the requested scale, and the `magnified_raster_idle` case below now
// frames such a cell with Shift+F and REQUIREs the canvas goes quiet — proving that defect retired
// while this deadline stays as protection against any future non-idling canvas.
constexpr auto k_settle_budget = std::chrono::seconds(10);
void settle(ImGuiTestContext* ctx, CanvasView& canvas) {
  const auto deadline = std::chrono::steady_clock::now() + k_settle_budget;
  std::uint64_t last = canvas.frames_issued("canvas#1");
  for (int quiet = 0; quiet < 40 && std::chrono::steady_clock::now() < deadline;) {
    ctx->Yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const std::uint64_t now = canvas.frames_issued("canvas#1");
    if (now == last) {
      ++quiet;
    } else {
      quiet = 0;
      last = now;
    }
  }
}

// A single root composition with a full-frame opaque background (so the content-gated frame
// sequence advances) and one small raster cell placed OFF the initial framing — the region the
// nav aid will fit. `cell_out` receives the cell's id for the selection-driven gesture.
constexpr double k_aid_canvas = 256.0;
constexpr double k_aid_cell_x = 180.0; // off-center: [180,180]x[212,212], a 32px raster corner
constexpr double k_aid_cell_y = 180.0;
void seed_with_cell(AppState& state, arbc::ObjectId& cell_out) {
  arbc::Document& doc = state.document();
  const arbc::ObjectId root = doc.add_composition(k_aid_canvas, k_aid_canvas);
  const arbc::ObjectId bg =
      doc.add_content(std::make_shared<arbc::SolidContent>(arbc::Rgba{0.05F, 0.05F, 0.08F, 1.0F}));
  doc.attach_layer(root, doc.add_layer(bg, arbc::Affine::identity()));
  const arbc::expected<arbc::ObjectId, std::string> cell =
      ace::scene::add_cell(doc, state.registry(), "org.arbc.raster", "32x32",
                           arbc::Affine::translation(k_aid_cell_x, k_aid_cell_y));
  REQUIRE(cell.has_value());
  cell_out = *cell;
}

struct E2EState {
  CanvasView* canvas;
  AppState* state = nullptr;
  arbc::ObjectId cell{};
};

} // namespace

TEST_CASE("canvas_nav e2e: wheel zooms (anchor_depth rises), Space-drag pans, scale bar tracks") {
  ScratchDir scratch("nav");
  ace::platform::NativeFileSystem fs;
  // The writer identity, bound before the document exists and stopped after the canvas
  // is gone (editor.canvas.writer_thread; see tests/writer_session.hpp).
  ace::testing::WriterSession session(scratch.root / "nav");
  REQUIRE(session.ok());
  AppState& state = session.state();
  // Fixture seeding IS a document write: post it to the identity the open just bound
  // (editor.canvas.writer_thread D-1). Assertions stay on this thread.
  session.on_writer([&] { seed_nested(state); });

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  CanvasView canvas(state, session.writer()); // spawns the render thread
  ace::views::register_view_body(ViewType::Canvas, [&canvas](std::string_view view_id) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content(view_id, static_cast<int>(avail.x), static_cast<int>(avail.y));
  });

  ace::dock::Dockspace dockspace; // default layout → canvas#1 open + docked
  shell.set_draw_content([&dockspace]() { dockspace.draw(); });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&canvas};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "canvas_nav", "wheel_pan_scalebar");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    CanvasView& canvas = *e2e->canvas;

    // canvas#1 (default-open) renders off-thread and is docked.
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));
    ImGuiWindow* w = ctx->GetWindowByRef("canvas#1");
    IM_CHECK(w != nullptr);
    IM_CHECK(w->DockNode != nullptr);
    ctx->WindowFocus("canvas#1");
    settle(ctx, canvas);

    const ImVec2 center = ctx->GetWindowByRef("canvas#1")->Rect().GetCenter();
    ctx->MouseSetViewport(ctx->GetWindowByRef("canvas#1"));
    ctx->MouseMoveToPos(center);
    ctx->Yield(2);

    // (i) A wheel over the pane zooms about the cursor: the frame advances AND the deep
    //     zoom re-anchors the nested composition (anchor_depth rises). One big notch in
    //     Fast mode is delivered in a single frame, so k_zoom_base^70 clears the band.
    const std::uint64_t before_zoom = canvas.frames_issued("canvas#1");
    const double units_before = canvas.scale_bar_units("canvas#1");
    IM_CHECK(units_before > 0.0);
    ctx->MouseWheelY(70.0F);
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") > before_zoom; }));
    IM_CHECK(pump_until(ctx, [&] { return canvas.anchor_depth("canvas#1") > 0; }));
    settle(ctx, canvas);

    // (iii) The scale bar tracks the camera: a deeper zoom means FEWER composition units
    //       across the same screen span.
    const double units_zoomed = canvas.scale_bar_units("canvas#1");
    IM_CHECK(units_zoomed > 0.0);
    IM_CHECK(units_zoomed < units_before);

    // (ii) A Space-held drag pans the viewport camera (D9): the frame advances again.
    const std::uint64_t before_pan = canvas.frames_issued("canvas#1");
    ctx->MouseMoveToPos(center);
    ctx->KeyDown(ImGuiKey_Space);
    ctx->MouseDragWithDelta(ImVec2(24.0F, 16.0F));
    ctx->KeyUp(ImGuiKey_Space);
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") > before_pan; }));
    settle(ctx, canvas);

    // Reset-to-fit (F): the camera frames the root composition's authored 2048x2048 canvas
    // into the pane (editor.canvas.fit_bounds / D-fit_bounds-1), the "don't get lost"
    // recovery (D-nav-7). Because the authored canvas is far larger than the pane, the fit
    // camera's scale is < 1 — so the restored scale-bar reads MORE composition units per
    // screen span than the initial identity framing (units_before, scale == 1). That
    // strict-greater proves F now reaches the authored-bounds fit, not device-pixel identity
    // (which would merely restore units_before). It is also > units_zoomed (a zoom-in).
    const std::uint64_t before_reset = canvas.frames_issued("canvas#1");
    ctx->MouseMoveToPos(center);
    ctx->KeyPress(ImGuiKey_F);
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") > before_reset; }));
    settle(ctx, canvas);
    const double units_reset = canvas.scale_bar_units("canvas#1");
    IM_CHECK(units_reset > units_zoomed);
    IM_CHECK(units_reset > units_before);
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

  CHECK(frames < k_max_frames); // the queue drained (no hang / timeout)
  CHECK(count_tested == 1);
  CHECK(count_success == 1);

  // Clean stop→wake→join of the render thread on teardown (Constraint 5).
  canvas.destroy();
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}

// editor.canvas.nav_aids (D24): Shift+F frames the current selection into the pane. Drives the
// stable canvas#1 view by id and asserts on model state (the scale-bar proxy), never pixels:
// (a) selecting an off-framing cell and pressing Shift+F frames it — the scale bar tightens (a
// zoom onto the small cell) and a published frame advances (the fit-to-cell / zoom-to-selection
// reach end-to-end); (b) with an EMPTY selection the same key is a no-op (scale bar unchanged,
// Constraint 6); (c) the document stays NOT dirty across the gesture (a nav aid never transacts,
// Constraint 4). Modeled on the reset-to-fit e2e above + frame_selection_e2e's selection setup.
TEST_CASE("canvas_nav e2e: Shift+F frames the selection; empty is a no-op; never dirties") {
  ScratchDir scratch("aids");
  ace::platform::NativeFileSystem fs;
  ace::testing::WriterSession session(scratch.root / "aids");
  REQUIRE(session.ok());
  AppState& state = session.state();
  arbc::ObjectId cell{};
  session.on_writer([&] { seed_with_cell(state, cell); });
  // A clean baseline at the seeded revision, so "stays not dirty" is a real assertion: the
  // aid mutates nothing, so the revision — and the dirty read — cannot move (Constraint 4).
  state.mark_saved(state.document().pin()->revision());
  REQUIRE_FALSE(state.is_dirty());

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  CanvasView canvas(state, session.writer()); // spawns the render thread
  ace::views::register_view_body(ViewType::Canvas, [&canvas](std::string_view view_id) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content(view_id, static_cast<int>(avail.x), static_cast<int>(avail.y));
  });

  ace::dock::Dockspace dockspace; // default layout → canvas#1 open + docked
  shell.set_draw_content([&dockspace]() { dockspace.draw(); });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&canvas, &state, cell};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "canvas_nav", "frame_selection_view");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    CanvasView& canvas = *e2e->canvas;
    AppState& state = *e2e->state;

    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));
    IM_CHECK(ctx->GetWindowByRef("canvas#1") != nullptr);
    ctx->WindowFocus("canvas#1");
    settle(ctx, canvas);

    const ImVec2 center = ctx->GetWindowByRef("canvas#1")->Rect().GetCenter();
    ctx->MouseSetViewport(ctx->GetWindowByRef("canvas#1"));
    ctx->MouseMoveToPos(center);
    ctx->Yield(2);

    const double units_before = canvas.scale_bar_units("canvas#1");
    IM_CHECK(units_before > 0.0);

    // (b) EMPTY selection: Shift+F is a no-op — the camera (hence the scale bar) is left exactly
    //     where it is (D-nav_aids-5 / Constraint 6). No frame is published for a no-op gesture.
    IM_CHECK(state.selection().empty());
    ctx->MouseMoveToPos(center);
    ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_F);
    ctx->Yield(4);
    IM_CHECK(canvas.scale_bar_units("canvas#1") == units_before);
    IM_CHECK(!state.is_dirty());

    // (a) Select the off-framing cell and press Shift+F: the aid frames the cell's placed extent
    //     into the pane — a large scale onto a 32-unit region, so the scale bar reads FEWER
    //     composition units than the initial framing — and a published frame advances.
    state.selection().select(e2e->cell);
    ctx->Yield(2);
    const std::uint64_t before = canvas.frames_issued("canvas#1");
    ctx->MouseMoveToPos(center);
    ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_F);
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") > before; }));
    settle(ctx, canvas);
    const double units_framed = canvas.scale_bar_units("canvas#1");
    IM_CHECK(units_framed > 0.0);
    IM_CHECK(units_framed < units_before); // zoomed onto the small cell

    // (c) The gesture never transacts: the revision — and the dirty read — did not move.
    IM_CHECK(!state.is_dirty());
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

// editor.canvas.magnified_raster_idle: assert a magnified raster canvas reaches idle. Before
// libarbc v0.4.0 (ruoso/arbitrarycomposer#18, pinned by editor.canvas.arbc_v040), a raster cell
// viewed at a deep magnification never let the canvas go idle — `InteractiveRenderer::render_frame`
// kept reporting `schedule_follow_up`, so `frames_issued` advanced at the frame rate forever and
// the render thread burned a core (violating the libarbc `idle-viewport-issues-no-frames`
// guarantee). #18 fixed the kind to render at the requested scale under BestEffort; this case
// asserts the fix and guards against a later pin silently regressing it. Mirroring the "already
// quiet" `wheel_pan_scalebar` (nested-solid) and `frame_selection_view` (1x raster) fixtures, it
// reuses `seed_with_cell`: select the 32x32 cell, press Shift+F (editor.canvas.nav_aids) to frame
// the selection into the pane — ~8-10x device magnification, the transient-camera path the user
// drives — confirm the framing engaged, `settle()`, then a bounded post-settle quiescence probe
// that REQUIREs `frames_issued("canvas#1")` does NOT advance. The 1ms-spaced yields give a still-
// spinning render thread wall-clock time to advance, so a held counter proves genuine idle rather
// than a settle-deadline time-out (D-magnified_raster_idle-2). The probe fails on pre-#18 code (it
// sees the counter advance) and holds with the v0.4.0 pin. No pixel/golden claim: the gap is
// never-reaching-idle, whose observable is `frames_issued` steadying, not a composited colour.
TEST_CASE("canvas_nav e2e: a magnified raster cell reaches idle (no forever re-drive)") {
  ScratchDir scratch("magnified");
  ace::platform::NativeFileSystem fs;
  ace::testing::WriterSession session(scratch.root / "magnified");
  REQUIRE(session.ok());
  AppState& state = session.state();
  arbc::ObjectId cell{};
  session.on_writer([&] { seed_with_cell(state, cell); });

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  CanvasView canvas(state, session.writer()); // spawns the render thread
  ace::views::register_view_body(ViewType::Canvas, [&canvas](std::string_view view_id) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content(view_id, static_cast<int>(avail.x), static_cast<int>(avail.y));
  });

  ace::dock::Dockspace dockspace; // default layout → canvas#1 open + docked
  shell.set_draw_content([&dockspace]() { dockspace.draw(); });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&canvas, &state, cell};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "canvas_nav", "magnified_raster_idle");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    CanvasView& canvas = *e2e->canvas;
    AppState& state = *e2e->state;

    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));
    IM_CHECK(ctx->GetWindowByRef("canvas#1") != nullptr);
    ctx->WindowFocus("canvas#1");
    settle(ctx, canvas);

    const ImVec2 center = ctx->GetWindowByRef("canvas#1")->Rect().GetCenter();
    ctx->MouseSetViewport(ctx->GetWindowByRef("canvas#1"));
    ctx->MouseMoveToPos(center);
    ctx->Yield(2);

    // Select the 32x32 raster cell and Shift+F to frame it — fit-to-selection magnifies the
    // 32-unit region into the pane (~8-10x). This is the framing that, pre-#18, left the render
    // thread re-driving forever.
    state.selection().select(e2e->cell);
    ctx->Yield(2);
    const std::uint64_t before = canvas.frames_issued("canvas#1");
    ctx->MouseMoveToPos(center);
    ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_F);
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") > before; }));
    settle(ctx, canvas);

    // The bounded post-settle quiescence probe: after settle() reports quiet, a magnified raster
    // that still owed follow-up frames would keep advancing `frames_issued`. Pump a fixed window
    // (well under k_settle_budget) with 1ms sleeps so a still-spinning render thread has wall-clock
    // time to advance the counter, then REQUIRE it did not — the direct refutation of "the render
    // thread burns a core forever at magnification" (#18 / D-magnified_raster_idle-2).
    //
    // settle()'s quiet window can end MID-refinement: the magnified raster converges via a worker-
    // arrival-driven progressive sequence (degraded frame → tile settles → refined repaint), and
    // under a contended worker pool a tile's device-visible arrival can be spaced WIDER than
    // settle()'s quiet threshold. A single probe would then read a false "idle" and trip over the
    // late arrival frame that lands during the window (the observed flake). So confirm idle by
    // re-settling and re-probing until a probe window HOLDS end-to-end, bounded by the same
    // k_settle_budget: genuine idle is a hard, event-latched state (the render loop blocks on a
    // no-timeout condition variable once no tile is in flight), so it yields a holding window
    // quickly; a canvas that re-drives FOREVER (pre-#18) never does, exhausts the bound, and fails
    // the REQUIRE below — the property this case guards is preserved, only the sampling is
    // hardened.
    const auto quiescent_by = std::chrono::steady_clock::now() + k_settle_budget;
    std::uint64_t idle = canvas.frames_issued("canvas#1");
    bool held = false;
    while (!held && std::chrono::steady_clock::now() < quiescent_by) {
      settle(ctx, canvas);
      idle = canvas.frames_issued("canvas#1");
      held = true;
      for (int i = 0; i < 120; ++i) {
        ctx->Yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (canvas.frames_issued("canvas#1") != idle) {
          held = false; // a late refinement frame landed — settle() had gone quiet too early
          break;
        }
      }
    }
    IM_CHECK(held);
    IM_CHECK(canvas.frames_issued("canvas#1") == idle);
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
