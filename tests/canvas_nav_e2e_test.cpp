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
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/views/views.hpp>

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
void settle(ImGuiTestContext* ctx, CanvasView& canvas) {
  std::uint64_t last = canvas.frames_issued("canvas#1");
  for (int quiet = 0; quiet < 40;) {
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

struct E2EState {
  CanvasView* canvas;
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
