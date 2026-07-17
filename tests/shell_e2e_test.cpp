#include <ace/app/probe.hpp>
#include <ace/app/shell.hpp>
#include <ace/project/project.hpp>
#include <ace/render/render.hpp>
#include <ace/views/views.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <cstdlib>
#include <vector>

#include <GLES3/gl3.h>

// The ImGui Test Engine e2e rig (docs/01-architecture.md §9) — the harness every
// later view leaf's e2e reuses. It boots the shell against the SDL offscreen
// driver + software GL, installs the render_probe pane (which supersedes
// app_shell's placeholder pane, refinement Constraint 7), drives it by its stable
// id, and reads back the frame to assert the SOLID RENDER COLOUR reached the
// screen — not the shell clear colour. This is a presence / distinct-colour
// assertion, NOT a byte-exact golden: software-GL pixels are flaky by
// construction (D-render_probe-5); the byte-exactness lives in the CPU golden
// (tests/render_probe_test.cpp).
using ace::app::ProbeView;
using ace::app::Shell;
using ace::app::ShellOptions;

namespace {

// Reusable screenshot capture wired into the engine's ScreenCaptureFunc (later
// leaves reuse via ctx->CaptureScreenshot). Runs on the main thread during
// PostSwap, so the GL context is current.
bool capture_pixels(ImGuiID /*viewport_id*/, int x, int y, int w, int h, unsigned int* pixels,
                    void* /*user_data*/) {
  glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  return glGetError() == GL_NO_ERROR;
}

} // namespace

TEST_CASE("shell e2e: render_probe pane is drivable by id and shows the render colour") {
  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 400;
  opts.height = 300;
  REQUIRE(shell.init(opts));

  // The probe texture is created after init (GL context current) and drawn every
  // frame via the installed content callback — the same wiring run_editor uses.
  ProbeView probe;
  probe.upload();
  shell.set_draw_content([&probe]() { probe.draw(); });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  te_io.ScreenCaptureFunc = capture_pixels; // register the reusable rig
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  ImGuiTest* test = IM_REGISTER_TEST(engine, "render_probe", "probe_pane");
  test->TestFunc = [](ImGuiTestContext* ctx) {
    // The pane exists and is addressable by its stable id (asserts presence).
    ImGuiTestItemInfo info = ctx->WindowInfo(ace::views::probe_pane_title());
    IM_CHECK(info.ID != 0);
  };
  ImGuiTestEngine_QueueTest(engine, test);

  // Keep the last read-back frame so we can scan it for the render colour after
  // the queue drains. Read on the main thread (context current) before present.
  std::vector<unsigned char> last_frame;
  int captured_w = 0, captured_h = 0;
  auto grab_frame = [&]() {
    const ImGuiIO& io = ImGui::GetIO();
    captured_w = static_cast<int>(io.DisplaySize.x);
    captured_h = static_cast<int>(io.DisplaySize.y);
    if (captured_w > 0 && captured_h > 0) {
      std::vector<unsigned int> px(static_cast<size_t>(captured_w) * captured_h);
      if (capture_pixels(0, 0, 0, captured_w, captured_h, px.data(), nullptr)) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(px.data());
        last_frame.assign(bytes, bytes + px.size() * 4);
      }
    }
  };

  const int k_max_frames = 200;
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

  CHECK(frames < k_max_frames); // the queue drained (no hang / timeout)
  CHECK(count_tested == 1);
  CHECK(count_success == 1);
  CHECK(captured_w == 400);
  CHECK(captured_h == 300);

  // The rendered texture reached the screen: some captured pixel matches the
  // probe's solid sRGB8 colour (within a tolerance for software-GL variance).
  // The probe colour is chosen distinct from the shell clear colour, so a match
  // proves the pane shows the render, not the background (D-render_probe-7).
  const ace::render::Srgb8Image expected =
      ace::render::render_probe_srgb8(ace::project::k_probe_width, ace::project::k_probe_height);
  REQUIRE(expected.pixels.size() >= 4);
  const int er = expected.pixels[0];
  const int eg = expected.pixels[1];
  const int eb = expected.pixels[2];
  const int k_tol = 12;
  bool found = false;
  for (size_t i = 0; i + 3 < last_frame.size(); i += 4) {
    if (std::abs(static_cast<int>(last_frame[i]) - er) <= k_tol &&
        std::abs(static_cast<int>(last_frame[i + 1]) - eg) <= k_tol &&
        std::abs(static_cast<int>(last_frame[i + 2]) - eb) <= k_tol) {
      found = true;
      break;
    }
  }
  CHECK(found);

  // Teardown order: the engine requires the ImGui context it hooked to be
  // destroyed FIRST — ImGui::DestroyContext() (inside shell.shutdown()) unbinds
  // the engine, which ImGuiTestEngine_DestroyContext() then asserts on. Destroy
  // the probe texture while the GL context is still valid.
  probe.destroy();
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
