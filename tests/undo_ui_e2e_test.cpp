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

// editor.project.undo — the undo/redo keyboard-chord UI e2e (Acceptance / §9),
// mirroring tests/save_ui_e2e_test.cpp: offscreen SDL + software GL, driving the
// dockspace via injected key chords. A fake ProjectGateway (records undo()/redo();
// scripts can_undo()/can_redo()) is injected so the chords are exercised without a
// real session. The assertions are on the recorded verb calls: Ctrl+Z drives undo(),
// Ctrl+Shift+Z and Ctrl+Y drive redo(), and each chord is gated — a no-op when the
// matching can_undo()/can_redo() is false (Decision D-undo-3).

using ace::app::Shell;
using ace::app::ShellOptions;

namespace {

// Records undo()/redo() and scripts can_undo()/can_redo(); the entry + Save actions
// are inert (this e2e is scoped to the undo/redo chords).
class FakeGateway final : public ace::dock::ProjectGateway {
public:
  int undo_calls = 0;
  int redo_calls = 0;
  bool can_undo_flag = true;
  bool can_redo_flag = true;

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
  ace::dock::ProjectEntryOutcome save_as(const std::filesystem::path&,
                                         const std::string&) override {
    return ace::dock::ProjectEntryOutcome::refused_target;
  }
  ace::dock::GcSummary clean_up(bool) override { return {}; } // inert (see gc_ui_e2e_test)

  bool undo() override {
    ++undo_calls;
    return true;
  }
  bool redo() override {
    ++redo_calls;
    return true;
  }
  bool can_undo() const override { return can_undo_flag; }
  bool can_redo() const override { return can_redo_flag; }
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
    "undo e2e: Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y drive the gateway, gated on can_undo/can_redo") {
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
  ImGuiTest* test = IM_REGISTER_TEST(engine, "undo", "chords_drive_gateway_gated");
  test->UserData = &state;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* state = static_cast<E2EState*>(ctx->Test->UserData);
    FakeGateway& gateway = *state->gateway;
    ctx->Yield(2); // let the dockspace draw before injecting chords

    // Ctrl+Z drives undo() exactly once (the canonical chord), redo() untouched.
    gateway.can_undo_flag = true;
    gateway.can_redo_flag = true;
    IM_CHECK(gateway.undo_calls == 0);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    ctx->Yield(2);
    IM_CHECK(gateway.undo_calls == 1);
    IM_CHECK(gateway.redo_calls == 0);

    // Ctrl+Shift+Z drives redo().
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z);
    ctx->Yield(2);
    IM_CHECK(gateway.redo_calls == 1);
    IM_CHECK(gateway.undo_calls == 1); // undo not re-triggered by the shift variant

    // Ctrl+Y is the alternate redo chord.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
    ctx->Yield(2);
    IM_CHECK(gateway.redo_calls == 2);

    // Gating: with can_undo() false, Ctrl+Z is a no-op (never dispatches undo()).
    gateway.can_undo_flag = false;
    const int undo_before = gateway.undo_calls;
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    ctx->Yield(2);
    IM_CHECK(gateway.undo_calls == undo_before);

    // Gating: with can_redo() false, both redo chords are no-ops.
    gateway.can_redo_flag = false;
    const int redo_before = gateway.redo_calls;
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z);
    ctx->Yield(2);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
    ctx->Yield(2);
    IM_CHECK(gateway.redo_calls == redo_before);
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
