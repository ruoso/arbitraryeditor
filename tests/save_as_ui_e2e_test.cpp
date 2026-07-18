#include <ace/app/shell.hpp>
#include <ace/dock/dock.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <GLES3/gl3.h>

// editor.project.save_as — the rail's Save As… UI e2e (Acceptance / §9), mirroring
// tests/save_ui_e2e_test.cpp: offscreen SDL + software GL, driving the rail BY
// STABLE WIDGET ID. A fake ProjectGateway (records save_as()) is injected so the
// Save As… button is exercised without a real folder dialog or session — the
// pick→publish→exec follow-up is the L4 AppProjectGateway's job, unit-tested
// headless in app_project_gateway_test.cpp. No byte-exact golden (software-GL pixels
// are flaky, per the open_ui/save e2e precedent); the assertion is on the recorded
// save_as() call driven by the rail button.

using ace::app::Shell;
using ace::app::ShellOptions;

namespace {

// Records save_as(); the other actions are inert (this e2e is scoped to the Save As…
// button wiring, the fork verb of A13/D-save_as).
class FakeGateway final : public ace::dock::ProjectGateway {
public:
  int save_as_calls = 0;

  bool open_project(const std::filesystem::path&) override { return true; }
  bool new_project(const std::filesystem::path&, const std::string&) override { return true; }
  bool open_recent(const std::filesystem::path&) override { return true; }
  void pick_folder(std::function<void(std::optional<std::filesystem::path>)>) override {}
  std::vector<std::filesystem::path> recent_projects() const override { return {}; }
  bool save() override { return true; }
  bool is_dirty() const override { return false; }
  void save_as() override { ++save_as_calls; }
};

struct E2EState {
  ace::dock::Dockspace* dockspace;
  FakeGateway* gateway;
};

bool capture_pixels(ImGuiID /*viewport_id*/, int x, int y, int w, int h, unsigned int* pixels,
                    void* /*user_data*/) {
  glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  return glGetError() == GL_NO_ERROR;
}

} // namespace

TEST_CASE("save_as e2e: the rail's Save As… button drives the gateway's save_as") {
  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  FakeGateway gateway;
  ace::dock::Dockspace dockspace;
  dockspace.set_project_gateway(&gateway);
  shell.set_draw_content([&dockspace]() { dockspace.draw(); });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  te_io.ScreenCaptureFunc = capture_pixels;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState state{&dockspace, &gateway};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "save_as", "rail_save_as");
  test->UserData = &state;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* state = static_cast<E2EState*>(ctx->Test->UserData);
    FakeGateway& gateway = *state->gateway;
    const std::string rail = ace::dock::tool_rail_title();
    const auto rail_ref = [&rail](const char* label) { return rail + "/" + label; };

    // Clicking Save As… drives the gateway's save_as() exactly once (the async
    // pick→publish→exec follow-up is owned by the L4 impl, not asserted here).
    IM_CHECK(gateway.save_as_calls == 0);
    ctx->ItemClick(rail_ref("###save_as").c_str());
    ctx->Yield(2);
    IM_CHECK(gateway.save_as_calls == 1);
  };
  ImGuiTestEngine_QueueTest(engine, test);

  const int k_max_frames = 400;
  int frames = 0;
  while (!ImGuiTestEngine_IsTestQueueEmpty(engine) && frames < k_max_frames) {
    shell.new_frame();
    shell.draw_ui();
    shell.render();
    ImGuiTestEngine_PostSwap(engine);
    ++frames;
  }

  int count_tested = 0, count_success = 0;
  ImGuiTestEngine_GetResult(engine, count_tested, count_success);
  ImGuiTestEngine_Stop(engine);

  CHECK(frames < k_max_frames); // the queue drained (no hang)
  CHECK(count_tested == 1);
  CHECK(count_success == 1);

  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
