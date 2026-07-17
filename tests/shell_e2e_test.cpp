#include <ace/app/shell.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <vector>

#include <GLES3/gl3.h>

// The ImGui Test Engine e2e rig (docs/01-architecture.md §9) — the central
// deliverable of app_shell and the harness every later view leaf's e2e reuses.
// It boots the shell against the SDL offscreen driver + software GL, drives the
// placeholder pane by widget id, and captures a screenshot baseline of the empty
// shell chrome. Per D-app_shell-4 the "rendered output" DoD here is that capture
// (proving the readback path), NOT a byte-exact golden — those begin at
// render_probe, and a software-GL pixel golden would be flaky by construction.
using ace::app::Shell;
using ace::app::ShellOptions;

namespace {

// Reusable screenshot capture wired into the engine's ScreenCaptureFunc (the rig
// later leaves reuse via ctx->CaptureScreenshot). Runs on the main thread during
// PostSwap, so the GL context is current.
bool capture_pixels(ImGuiID /*viewport_id*/, int x, int y, int w, int h, unsigned int* pixels,
                    void* /*user_data*/) {
  glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  return glGetError() == GL_NO_ERROR;
}

} // namespace

TEST_CASE("shell e2e: placeholder pane is drivable by id and captures a baseline") {
  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 400;
  opts.height = 300;
  REQUIRE(shell.init(opts));

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  te_io.ScreenCaptureFunc = capture_pixels; // register the reusable rig
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  ImGuiTest* test = IM_REGISTER_TEST(engine, "shell", "placeholder_pane");
  test->TestFunc = [](ImGuiTestContext* ctx) {
    ctx->SetRef("Arbitrary Composer");
    ctx->ItemClick("Placeholder"); // asserts the widget exists and is clickable
  };
  ImGuiTestEngine_QueueTest(engine, test);

  // Screenshot baseline: read back the empty shell frame before each present,
  // on the main thread (context current). Proves the capture path; not a golden.
  bool captured = false;
  int captured_w = 0, captured_h = 0;
  auto grab_frame = [&]() {
    const ImGuiIO& io = ImGui::GetIO();
    captured_w = static_cast<int>(io.DisplaySize.x);
    captured_h = static_cast<int>(io.DisplaySize.y);
    if (captured_w > 0 && captured_h > 0) {
      std::vector<unsigned int> px(static_cast<size_t>(captured_w) * captured_h);
      captured = capture_pixels(0, 0, 0, captured_w, captured_h, px.data(), nullptr);
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
  CHECK(captured);
  CHECK(captured_w == 400);
  CHECK(captured_h == 300);

  // Teardown order: the engine requires the ImGui context it hooked to be
  // destroyed FIRST — ImGui::DestroyContext() (inside shell.shutdown()) unbinds
  // the engine, which ImGuiTestEngine_DestroyContext() then asserts on.
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
