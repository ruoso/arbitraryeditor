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
#include <utility>
#include <vector>

#include <GLES3/gl3.h>

// editor.project.save_as + editor.project.dir_is_project — the rail's Save As… UI e2e
// (Acceptance / §9), mirroring tests/save_ui_e2e_test.cpp: offscreen SDL + software GL,
// driving the rail BY STABLE WIDGET ID. A fake ProjectGateway (scripts pick_folder, records
// save_as(parent, name)) is injected so the flow is exercised without a real folder dialog or
// session — the publish→exec follow-up is the L4 AppProjectGateway's job, unit-tested headless
// in app_project_gateway_test.cpp.
//
// Under D27 Save As runs the SAME two-step New runs: pick a PARENT location, then type a
// project name into the shared compose modal ("Save Project As", submit "Save Copy"), because
// the target must not exist and a native folder dialog can only return one that does. So the
// assertions are on the composed (parent, name) pair the button drives, on the refusal string
// left in the rail's inline feedback when the gateway says no, and on the cancel paths. No
// byte-exact golden (software-GL pixels are flaky, per the open_ui/save e2e precedent).

using ace::app::Shell;
using ace::app::ShellOptions;

namespace {

// Scripts the folder pick and records save_as(parent, name); the other actions are inert
// (this e2e is scoped to the Save As… wiring, the fork verb of A13/D-save_as).
class FakeGateway final : public ace::dock::ProjectGateway {
public:
  std::optional<std::filesystem::path> next; // what the scripted pick resolves to
  bool cancel_next = false;                  // …or a cancelled pick
  bool save_as_ok = true;                    // whether the composed target is accepted
  int picks = 0;
  std::vector<std::pair<std::filesystem::path, std::string>> save_as_calls;

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
  void pick_folder(std::function<void(std::optional<std::filesystem::path>)> on_pick) override {
    ++picks;
    on_pick(cancel_next ? std::nullopt : next);
  }
  std::vector<std::filesystem::path> recent_projects() const override { return {}; }
  bool save() override { return true; }
  bool is_dirty() const override { return false; }
  bool save_as(const std::filesystem::path& parent, const std::string& name) override {
    save_as_calls.emplace_back(parent, name);
    return save_as_ok;
  }
  ace::dock::GcSummary clean_up(bool) override { return {}; } // inert (see gc_ui_e2e_test)
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

TEST_CASE("save_as e2e: the rail's Save As… runs the parent-pick + typed-name compose") {
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
    const std::filesystem::path parent = "/tmp/ace_save_as_e2e/a_parent_location";
    const std::string refusal = "Enter a project name that does not already exist here.";

    // 1. A CANCELLED pick opens no modal at all — and records no save_as.
    gateway.cancel_next = true;
    ctx->ItemClick(rail_ref("###save_as").c_str());
    ctx->Yield(2);
    IM_CHECK(gateway.picks == 1);
    IM_CHECK(gateway.save_as_calls.empty());
    IM_CHECK(!state->dockspace->save_as_modal_open());
    IM_CHECK(!ctx->ItemExists("Save Project As/Save Copy"));

    // 2. A resolved pick seeds the parent and opens the compose modal — the SAME widget New
    //    uses (A22 / D-dir_is_project-5), differing only in popup id and submit label.
    gateway.cancel_next = false;
    gateway.next = parent;
    ctx->ItemClick(rail_ref("###save_as").c_str());
    ctx->Yield(2);
    IM_CHECK(state->dockspace->save_as_modal_open());
    IM_CHECK(state->dockspace->save_as_parent() == parent); // the pick seeded the parent
    IM_CHECK(ctx->ItemExists("Save Project As/Save Copy"));

    // 3. A REFUSED copy (the gateway says no — an invalid name, or a target that already
    //    exists): the modal STAYS OPEN and the rail's inline feedback carries the one refusal
    //    string (D-dir_is_project-6/-7), the same channel Save/Open/Recent write.
    gateway.save_as_ok = false;
    ctx->ItemInput("Save Project As/Name");
    ctx->KeyCharsReplace("Taken");
    ctx->ItemClick("Save Project As/Save Copy");
    ctx->Yield(2);
    IM_CHECK(gateway.save_as_calls.size() == 1);
    IM_CHECK(gateway.save_as_calls.back().first == parent);
    IM_CHECK(gateway.save_as_calls.back().second == std::string("Taken"));
    IM_CHECK(ctx->ItemExists("Save Project As/Save Copy")); // still open
    IM_CHECK(state->dockspace->save_as_modal_open());
    IM_CHECK(state->dockspace->project_feedback() == refusal);

    // 4. An accepted copy records exactly one more save_as(parent, name) with the TYPED
    //    values and closes the modal.
    gateway.save_as_ok = true;
    ctx->ItemInput("Save Project As/Name");
    ctx->KeyCharsReplace("Copy");
    ctx->ItemClick("Save Project As/Save Copy");
    ctx->Yield(2);
    IM_CHECK(gateway.save_as_calls.size() == 2);
    IM_CHECK(gateway.save_as_calls.back().first == parent);
    IM_CHECK(gateway.save_as_calls.back().second == std::string("Copy"));
    IM_CHECK(!ctx->ItemExists("Save Project As/Save Copy")); // the modal closed
    IM_CHECK(!state->dockspace->save_as_modal_open());
    IM_CHECK(state->dockspace->project_feedback().empty()); // …and the feedback cleared

    // 5. Cancel records zero save_as calls and closes the modal.
    ctx->ItemClick(rail_ref("###save_as").c_str());
    ctx->Yield(2);
    IM_CHECK(ctx->ItemExists("Save Project As/Cancel"));
    ctx->ItemClick("Save Project As/Cancel");
    ctx->Yield(2);
    IM_CHECK(gateway.save_as_calls.size() == 2); // unchanged
    IM_CHECK(!state->dockspace->save_as_modal_open());
    IM_CHECK(!ctx->ItemExists("Save Project As/Save Copy"));

    // The New modal's own refs are untouched by all of this — one implementation, two specs.
    IM_CHECK(!ctx->ItemExists("New Project/Create"));
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
