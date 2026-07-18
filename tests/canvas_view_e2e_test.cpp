// editor.canvas.view — Canvas view UI e2e (docs/01-architecture.md §9 :189).
// Reuse the offscreen SDL + software-GL rig: boot the shell over a REAL
// commands::AppState (ScratchDir + create_project), seed the fresh document with a
// solid fill so it has visible content, register the driver-backed Canvas body,
// and drive the default-open canvas#1 view BY STABLE ID. Asserts the window
// exists + is docked, the driver produced a live frame (frames_issued() >= 1), and
// a captured pixel matches the seeded render colour — DISTINCT from the shell clear
// colour (not a byte-exact golden; software-GL pixels are flaky by construction —
// the byte-exactness lives in tests/canvas_view_test.cpp's CPU golden).
#include <ace/app/canvas_view.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/gl/gl.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/render/render.hpp>
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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

#include <GLES3/gl3.h>

using ace::app::CanvasView;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::commands::AppState;
using ace::dockmodel::ViewType;

namespace {

// A temp dir wiped on entry and exit, named distinctly so this suite never collides
// with the other suites in the one ace_shell_test binary.
struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_canvas_view_e2e_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// Reusable screenshot capture wired into the engine's ScreenCaptureFunc. Runs on
// the main thread before present, so the GL context is current.
bool capture_pixels(ImGuiID /*viewport_id*/, int x, int y, int w, int h, unsigned int* pixels,
                    void* /*user_data*/) {
  glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  return glGetError() == GL_NO_ERROR;
}

// TestFunc is a plain function pointer (std::function is disabled in this build),
// so the CanvasView is threaded through UserData rather than captured.
struct E2EState {
  CanvasView* canvas;
};

} // namespace

TEST_CASE("canvas_view e2e: canvas#1 shows the live document, docked and drivable by id") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "canvas");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));

  // A fresh create_project document has no composition and renders transparent, so
  // seed it with a composition + an unbounded solid fill (the probe colour, chosen
  // distinct from the shell clear colour) — the canvas then has content to show.
  const arbc::ObjectId comp =
      state.document().add_composition(static_cast<double>(ace::project::k_probe_width),
                                       static_cast<double>(ace::project::k_probe_height));
  const arbc::ObjectId content = state.document().add_content(
      std::make_shared<arbc::SolidContent>(ace::project::k_probe_color));
  const arbc::ObjectId layer = state.document().add_layer(content, arbc::Affine::identity());
  state.document().attach_layer(comp, layer);

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  // The driver-backed Canvas body over the one owned Document — the same seam the
  // L4 shell uses. Cleared after the loop so the process-global seam never outlives
  // this CanvasView/AppState into a later test in the ace_shell_test binary.
  CanvasView canvas(state);
  ace::views::register_view_body(ViewType::Canvas, [&canvas](std::string_view) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content(static_cast<int>(avail.x), static_cast<int>(avail.y));
  });

  ace::dock::Dockspace dockspace; // default layout → canvas#1 open + docked
  shell.set_draw_content([&dockspace]() { dockspace.draw(); });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  te_io.ScreenCaptureFunc = capture_pixels;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&canvas};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "canvas_view", "shows_live_document");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    CanvasView& canvas = *e2e->canvas;

    // canvas#1 is open + docked by default; let it lay out and render. Yield well
    // past the mid-run edit (below) so the re-rendered frame is pumped too.
    ctx->Yield(4);
    IM_CHECK(ctx->WindowInfo("canvas#1").ID != 0);
    ImGuiWindow* w_canvas = ctx->GetWindowByRef("canvas#1");
    IM_CHECK(w_canvas != nullptr);
    IM_CHECK(w_canvas->DockNode != nullptr);

    // The driver produced at least one live frame of the shared document.
    IM_CHECK(canvas.frames_issued() >= 1);
    ctx->Yield(12);
  };
  ImGuiTestEngine_QueueTest(engine, test);

  // Keep the last read-back frame so we can scan it for the render colour after the
  // queue drains. Read on the main thread (context current) before present.
  std::vector<unsigned char> last_frame;
  auto grab_frame = [&]() {
    const ImGuiIO& io = ImGui::GetIO();
    const int w = static_cast<int>(io.DisplaySize.x);
    const int h = static_cast<int>(io.DisplaySize.y);
    if (w > 0 && h > 0) {
      std::vector<unsigned int> px(static_cast<std::size_t>(w) * h);
      if (capture_pixels(0, 0, 0, w, h, px.data(), nullptr)) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(px.data());
        last_frame.assign(bytes, bytes + px.size() * 4);
      }
    }
  };

  const int k_max_frames = 400;
  int frames = 0;
  while (!ImGuiTestEngine_IsTestQueueEmpty(engine) && frames < k_max_frames) {
    shell.new_frame();
    shell.draw_ui();
    shell.render(grab_frame);
    ImGuiTestEngine_PostSwap(engine);
    ++frames;
  }

  int count_tested = 0, count_success = 0;
  ImGuiTestEngine_GetResult(engine, count_tested, count_success);
  ImGuiTestEngine_Stop(engine);

  // Clear the body before the captured CanvasView dies — the seam is process-global.
  ace::views::register_view_body(ViewType::Canvas, {});

  CHECK(frames < k_max_frames); // the queue drained (no hang / timeout)
  CHECK(count_tested == 1);
  CHECK(count_success == 1);

  // The live render reached the screen: some captured pixel matches the seeded
  // solid sRGB8 colour (within a tolerance for software-GL variance), distinct from
  // the shell clear colour (0.10,0.10,0.12) — proof the canvas shows the render,
  // not the background. NOT a byte-exact golden (D-canvas_view / render_probe).
  const ace::render::Srgb8Image expected =
      ace::render::render_probe_srgb8(ace::project::k_probe_width, ace::project::k_probe_height);
  REQUIRE(expected.pixels.size() >= 4);
  const int er = expected.pixels[0];
  const int eg = expected.pixels[1];
  const int eb = expected.pixels[2];
  const int k_tol = 12;
  bool found = false;
  for (std::size_t i = 0; i + 3 < last_frame.size(); i += 4) {
    if (std::abs(static_cast<int>(last_frame[i]) - er) <= k_tol &&
        std::abs(static_cast<int>(last_frame[i + 1]) - eg) <= k_tol &&
        std::abs(static_cast<int>(last_frame[i + 2]) - eb) <= k_tol) {
      found = true;
      break;
    }
  }
  CHECK(found);

  // Teardown order (shell_e2e rationale): destroy the ImGui context (inside
  // shell.shutdown()) before the engine, and release the canvas texture while the
  // GL context is still valid.
  canvas.destroy();
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}

TEST_CASE(
    "canvas_view e2e: gl::update_rgba8 refreshes in place; a pane resize churns the texture") {
  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 320;
  opts.height = 240;
  REQUIRE(shell.init(opts));

  // gl::update_rgba8 (this leaf's new tile→GL primitive): a same-size in-place
  // refresh of a live texture issues no GL error — the path a re-rendering canvas
  // (a later edit / camera nudge, editor.canvas.frame_sync / nav) takes instead of
  // churning glGen/glDelete.
  std::vector<unsigned char> px(static_cast<std::size_t>(64) * 64 * 4, 64);
  const unsigned int tex = ace::gl::upload_rgba8(px.data(), 64, 64);
  CHECK(tex != 0);
  std::fill(px.begin(), px.end(), static_cast<unsigned char>(200));
  ace::gl::update_rgba8(tex, px.data(), 64, 64);
  CHECK(glGetError() == static_cast<GLenum>(GL_NO_ERROR));
  ace::gl::destroy_texture(tex);

  // CanvasView pane-resize (Constraint 7 — the resize/reallocate branch): a size
  // change reallocates the driver target and swaps the GL texture object, reusing
  // no stale handle. Drive draw_content inside a real ImGui frame (draw_canvas_image
  // issues an ImGui::Image, which needs a current window).
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "resize");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  const arbc::ObjectId comp =
      state.document().add_composition(static_cast<double>(ace::project::k_probe_width),
                                       static_cast<double>(ace::project::k_probe_height));
  const arbc::ObjectId content = state.document().add_content(
      std::make_shared<arbc::SolidContent>(ace::project::k_probe_color));
  const arbc::ObjectId layer = state.document().add_layer(content, arbc::Affine::identity());
  state.document().attach_layer(comp, layer);

  CanvasView canvas(state);
  shell.new_frame();
  ImGui::Begin("canvas_host");
  canvas.draw_content(64, 64); // first frame: a fresh upload
  const std::uint64_t after_first = canvas.frames_issued();
  canvas.draw_content(48, 32); // a size change: destroy the old texture + re-upload
  ImGui::End();
  shell.render();

  CHECK(after_first >= 1);

  canvas.destroy();
  shell.shutdown();
}
