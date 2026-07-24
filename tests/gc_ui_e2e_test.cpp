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

// editor.project.gc — the rail's Clean up… + confirm modal UI e2e (Acceptance / §9),
// mirroring tests/save_ui_e2e_test.cpp: offscreen SDL + software GL, driving the
// rail BY STABLE WIDGET ID. A fake ProjectGateway records clean_up(preview) calls
// and returns a scripted GcSummary, so the confirmed-op flow (D15) is exercised
// without a real session: a Clean up click runs a dry-run PREVIEW and opens the
// confirm modal with the reclaim counts; the modal's Clean Up commits the real
// sweep; Cancel sweeps nothing. No byte-exact golden (software-GL pixels are flaky,
// per the save_ui precedent); the assertions are on the recorded calls, the modal's
// presence, and the previewed counts the Dockspace holds.

using ace::app::Shell;
using ace::app::ShellOptions;

namespace {

// Records clean_up(preview) as two counters and returns a scripted reclaim report;
// every other verb is inert (this e2e is scoped to Clean up + confirm).
class FakeGateway final : public ace::dock::ProjectGateway {
public:
  int preview_calls = 0;
  int commit_calls = 0;
  ace::dock::GcSummary scripted{3, 4096, true}; // reclaimed_files, reclaimed_bytes, ran

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
  bool save() override { return true; }
  bool is_dirty() const override { return false; }
  bool save_as(const std::filesystem::path&, const std::string&) override { return false; }

  ace::dock::GcSummary clean_up(bool preview) override {
    if (preview) {
      ++preview_calls;
    } else {
      ++commit_calls;
    }
    return scripted;
  }

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

TEST_CASE(
    "gc e2e: Clean up previews into a confirm modal; Clean Up commits, Cancel sweeps nothing") {
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
  ImGuiTest* test = IM_REGISTER_TEST(engine, "gc", "rail_clean_up_confirm");
  test->UserData = &state;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* state = static_cast<E2EState*>(ctx->Test->UserData);
    FakeGateway& gateway = *state->gateway;
    const std::string rail = ace::dock::tool_rail_title();
    const auto rail_ref = [&rail](const char* label) { return rail + "/" + label; };

    // Clean up… runs a dry-run PREVIEW and opens the confirm modal with the counts.
    IM_CHECK(gateway.preview_calls == 0);
    ctx->ItemClick(rail_ref("###gc").c_str());
    ctx->Yield(2);
    IM_CHECK(gateway.preview_calls == 1);
    IM_CHECK(gateway.commit_calls == 0); // no commit until the user confirms
    IM_CHECK(ctx->ItemExists("Clean Up/###gc_confirm"));
    // The modal surfaces the previewed reclaim counts (the scripted GcSummary).
    IM_CHECK(state->dockspace->gc_preview().reclaimed_files == 3);
    IM_CHECK(state->dockspace->gc_preview().reclaimed_bytes == 4096);

    // Confirm → the committed (preview=false) sweep runs exactly once; modal closes.
    ctx->ItemClick("Clean Up/###gc_confirm");
    ctx->Yield(2);
    IM_CHECK(gateway.commit_calls == 1);
    IM_CHECK(!ctx->ItemExists("Clean Up/###gc_confirm"));

    // Reopen and Cancel → the preview runs again, but NO new committed sweep fires.
    ctx->ItemClick(rail_ref("###gc").c_str());
    ctx->Yield(2);
    IM_CHECK(gateway.preview_calls == 2);
    IM_CHECK(ctx->ItemExists("Clean Up/###gc_cancel"));
    ctx->ItemClick("Clean Up/###gc_cancel");
    ctx->Yield(2);
    IM_CHECK(gateway.commit_calls == 1); // an irreversible delete is never a mis-click
    IM_CHECK(!ctx->ItemExists("Clean Up/###gc_cancel"));
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
