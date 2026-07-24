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

// editor.project.save — the rail's Save + dirty indicator UI e2e (Acceptance / §9),
// mirroring tests/open_ui_e2e_test.cpp: offscreen SDL + software GL, driving the
// rail BY STABLE WIDGET ID. A fake ProjectGateway (records save(); scripts
// is_dirty()) is injected so the Save button and the dirty indicator are exercised
// without a real session. No byte-exact golden (software-GL pixels are flaky, per
// the open_ui e2e precedent); the assertions are on the recorded save() call and on
// the indicator's presence/absence via the test engine's item query.

using ace::app::Shell;
using ace::app::ShellOptions;

namespace {

// Records save() and scripts is_dirty(); the entry actions are inert (this e2e is
// scoped to Save + dirty, the in-session verbs of A13).
class FakeGateway final : public ace::dock::ProjectGateway {
public:
  int save_calls = 0;
  bool save_ok = true;
  bool dirty = false;

  // The three entry verbs on the A24 outcome seam (dock::ProjectEntryOutcome): still
  // inert here — this suite drives neither New nor Open nor Recent.
  ace::dock::ProjectEntryOutcome open_project(const std::filesystem::path&) override {
    return ace::dock::ProjectEntryOutcome::succeeded;
  }
  ace::dock::ProjectEntryOutcome new_project(const std::filesystem::path&,
                                             const std::string&) override {
    return ace::dock::ProjectEntryOutcome::succeeded;
  }
  ace::dock::ProjectEntryOutcome open_recent(const std::filesystem::path&) override {
    return ace::dock::ProjectEntryOutcome::succeeded;
  }
  void pick_folder(std::function<void(std::optional<std::filesystem::path>)>) override {}
  std::vector<std::filesystem::path> recent_projects() const override { return {}; }

  bool save() override {
    ++save_calls;
    return save_ok;
  }
  bool is_dirty() const override { return dirty; }
  // Save As (A13/D-save_as) — inert here; the dedicated Save As… button coverage
  // lives in save_as_ui_e2e_test.cpp.
  bool save_as(const std::filesystem::path&, const std::string&) override { return false; }
  // Clean up (GC, A13/editor.project.gc) — inert here; the confirm-flow coverage
  // lives in gc_ui_e2e_test.cpp.
  ace::dock::GcSummary clean_up(bool) override { return {}; }
  // Undo/redo (editor.project.undo) — inert here; the chord coverage lives in
  // undo_ui_e2e_test.cpp.
  bool undo() override { return false; }
  bool redo() override { return false; }
  bool can_undo() const override { return false; }
  bool can_redo() const override { return false; }
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

TEST_CASE("save e2e: the rail's Save button drives the gateway and the dirty indicator tracks it") {
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
  ImGuiTest* test = IM_REGISTER_TEST(engine, "save", "rail_save_and_dirty");
  test->UserData = &state;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* state = static_cast<E2EState*>(ctx->Test->UserData);
    FakeGateway& gateway = *state->gateway;
    const std::string rail = ace::dock::tool_rail_title();
    const auto rail_ref = [&rail](const char* label) { return rail + "/" + label; };

    // The dirty indicator is drawn only when is_dirty() is true.
    gateway.dirty = false;
    ctx->Yield(2);
    IM_CHECK(!ctx->ItemExists(rail_ref("###dirty_indicator").c_str()));

    gateway.dirty = true;
    ctx->Yield(2);
    IM_CHECK(ctx->ItemExists(rail_ref("###dirty_indicator").c_str()));

    // Clicking Save drives the gateway's save() exactly once.
    IM_CHECK(gateway.save_calls == 0);
    ctx->ItemClick(rail_ref("###save_project").c_str());
    ctx->Yield(2);
    IM_CHECK(gateway.save_calls == 1);

    // A successful save clears any feedback; a failed one surfaces it.
    IM_CHECK(state->dockspace->project_feedback().empty());
    gateway.save_ok = false;
    ctx->ItemClick(rail_ref("###save_project").c_str());
    ctx->Yield(2);
    IM_CHECK(gateway.save_calls == 2);
    IM_CHECK(!state->dockspace->project_feedback().empty());

    // Once the session goes clean, the indicator is gone.
    gateway.dirty = false;
    ctx->Yield(2);
    IM_CHECK(!ctx->ItemExists(rail_ref("###dirty_indicator").c_str()));
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
