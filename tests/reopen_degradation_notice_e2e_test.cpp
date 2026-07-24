#include <ace/app/shell.hpp>
#include <ace/dock/dock.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <GLES3/gl3.h>

// editor.project.reopen_degradation_notice — the one-shot reopen-degradation notice UI e2e
// (Acceptance / §9), mirroring tests/gc_ui_e2e_test.cpp: offscreen SDL + software GL,
// driving the modal BY STABLE WIDGET ID. A fake ProjectGateway scripts
// `reopen_unbindable_count()`, so the D25 flow is exercised with no real lossy session —
// producing that count is pinned at L1 (tests/project_open_test.cpp) and carrying it is
// pinned at L4 (tests/app_project_gateway_test.cpp); what is left, and what only a driven
// frame loop can answer, is the PRESENTATION contract: a positive count announces itself
// unprompted, Dismiss ends it, and it never comes back.
//
// The two cases are the two halves of Constraint 3. The one-shot latch is the load-bearing
// one: the gateway is a pure reporter that keeps returning the same non-zero count forever,
// so nothing but the Dockspace's seen-flag stops the notice from re-opening on the very next
// frame — a modal that reappears after every dismiss is worse than no notice at all. The
// zero case pins the other direction, that a clean session is never told it lost anything.
//
// No byte-exact golden: software-GL pixels are flaky (the save_ui/gc precedent), and the
// assertions here are on widget presence and the latch, not pixels. The screenshot rig is
// wired as the sibling e2es wire it, but a text modal adds no signal a baseline would catch.

using ace::app::Shell;
using ace::app::ShellOptions;

namespace {

// Scripts the A19 degradation count; every other verb is inert (this e2e is scoped to the
// notice). `reopen_unbindable_count` counts its own calls so the test can prove the rail
// re-queries the seam every frame rather than caching — the latch must be what stops the
// notice, not a stale read.
class FakeGateway final : public ace::dock::ProjectGateway {
public:
  std::size_t scripted_unbindable = 0;
  mutable int count_queries = 0;

  bool open_project(const std::filesystem::path&) override { return true; }
  bool new_project(const std::filesystem::path&, const std::string&) override { return true; }
  bool open_recent(const std::filesystem::path&) override { return true; }
  void pick_folder(std::function<void(std::optional<std::filesystem::path>)>) override {}
  std::vector<std::filesystem::path> recent_projects() const override { return {}; }
  bool save() override { return true; }
  bool is_dirty() const override { return false; }
  bool save_as(const std::filesystem::path&, const std::string&) override { return false; }
  ace::dock::GcSummary clean_up(bool) override { return {}; }
  bool undo() override { return false; }
  bool redo() override { return false; }
  bool can_undo() const override { return false; }
  bool can_redo() const override { return false; }

  std::size_t reopen_unbindable_count() const override {
    ++count_queries;
    return scripted_unbindable;
  }
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

// The dismiss button's stable, slash-free id under the notice popup's title.
const char* k_dismiss_ref = "Project Reopened Incomplete/###reopen_notice_dismiss";

} // namespace

TEST_CASE("reopen-notice e2e: a lossy reopen announces itself once and never reappears") {
  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  FakeGateway gateway;
  gateway.scripted_unbindable = 4;
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
  ImGuiTest* test = IM_REGISTER_TEST(engine, "reopen_notice", "one_shot_dismiss");
  test->UserData = &state;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* state = static_cast<E2EState*>(ctx->Test->UserData);

    // The notice fires UNPROMPTED — no rail click opens it, because the condition it
    // reports is a passive startup fact of the session (D-reopen_degradation_notice-3).
    ctx->Yield(2);
    IM_CHECK(state->dockspace->reopen_notice_open());
    IM_CHECK(state->dockspace->reopen_notice_seen());
    IM_CHECK(ctx->ItemExists(k_dismiss_ref));

    // Dismiss closes it — a single button, because there is nothing to confirm.
    ctx->ItemClick(k_dismiss_ref);
    ctx->Yield(2);
    IM_CHECK(!state->dockspace->reopen_notice_open());
    IM_CHECK(!ctx->ItemExists(k_dismiss_ref));

    // The one-shot latch (Constraint 3): the gateway is a pure reporter and is still
    // answering 4 on every one of these frames, so only the Dockspace's seen-flag keeps
    // the notice from re-opening.
    const int queries_before = state->gateway->count_queries;
    ctx->Yield(20);
    IM_CHECK(state->gateway->count_queries > queries_before); // still being asked...
    IM_CHECK(state->gateway->scripted_unbindable == 4);       // ...and still answering non-zero
    IM_CHECK(!state->dockspace->reopen_notice_open());        // ...and it stays dismissed
    IM_CHECK(!ctx->ItemExists(k_dismiss_ref));
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

TEST_CASE("reopen-notice e2e: a session that lost nothing is never shown the notice") {
  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  FakeGateway gateway;
  gateway.scripted_unbindable = 0; // a clean map reopen, a rebuild, or a fresh create
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
  ImGuiTest* test = IM_REGISTER_TEST(engine, "reopen_notice", "zero_count_never_shown");
  test->UserData = &state;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* state = static_cast<E2EState*>(ctx->Test->UserData);

    ctx->Yield(20);
    IM_CHECK(state->gateway->count_queries > 0);       // the seam WAS consulted...
    IM_CHECK(!state->dockspace->reopen_notice_open()); // ...and reported nothing lost
    IM_CHECK(!state->dockspace->reopen_notice_seen()); // the latch was never even armed
    IM_CHECK(!ctx->ItemExists(k_dismiss_ref));
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

  CHECK(frames < k_max_frames);
  CHECK(count_tested == 1);
  CHECK(count_success == 1);

  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
