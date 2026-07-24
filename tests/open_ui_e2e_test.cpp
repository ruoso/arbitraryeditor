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

// editor.project.open_ui — the rail's Project section UI e2e (Acceptance / §9),
// mirroring tests/tool_rail_e2e_test.cpp: offscreen SDL + software GL, driving the
// rail BY STABLE BUTTON ID. A fake ProjectGateway (records calls; scripts a folder
// pick) is injected so the rail's New / Open / Recent affordances are exercised
// without a real OS dialog or a sibling process. No byte-exact golden (software-GL
// pixels are flaky; the rail composes chrome, not a Document — refinement).

using ace::app::Shell;
using ace::app::ShellOptions;

namespace {

// Records every gateway call and scripts the async folder pick (delivered
// synchronously — determinism for the driven frames).
class FakeGateway final : public ace::dock::ProjectGateway {
public:
  std::optional<std::filesystem::path> next; // the scripted pick result
  bool cancel_next = false;                  // deliver nullopt (a cancelled pick)
  // What the entry verbs report (A24). Scripted directly rather than inferred: the whole
  // point of the outcome seam is that a host cannot reconstruct which half failed.
  ace::dock::ProjectEntryOutcome entry_outcome = ace::dock::ProjectEntryOutcome::succeeded;
  std::vector<std::filesystem::path> recent; // seeded MRU list

  std::vector<std::filesystem::path> opened;
  std::vector<std::pair<std::filesystem::path, std::string>> created;
  std::vector<std::filesystem::path> replayed;
  int picks = 0;
  mutable int recent_queries = 0;

  ace::dock::ProjectEntryOutcome open_project(const std::filesystem::path& dir) override {
    opened.push_back(dir);
    return entry_outcome;
  }
  ace::dock::ProjectEntryOutcome new_project(const std::filesystem::path& parent,
                                             const std::string& name) override {
    if (name.empty()) {
      // Mirror the real reject so the modal shows feedback + stays open.
      return ace::dock::ProjectEntryOutcome::refused_target;
    }
    created.emplace_back(parent, name);
    return entry_outcome;
  }
  ace::dock::ProjectEntryOutcome open_recent(const std::filesystem::path& dir) override {
    replayed.push_back(dir);
    return entry_outcome;
  }
  void pick_folder(std::function<void(std::optional<std::filesystem::path>)> on_pick) override {
    ++picks;
    on_pick(cancel_next ? std::nullopt : next);
  }
  std::vector<std::filesystem::path> recent_projects() const override {
    ++recent_queries;
    return recent;
  }
  // Save + dirty + Save As (A13) — inert here (this e2e exercises New/Open/Recent);
  // the dedicated coverage lives in save_ui_e2e_test.cpp / save_as_ui_e2e_test.cpp.
  bool save() override { return true; }
  bool is_dirty() const override { return false; }
  ace::dock::ProjectEntryOutcome save_as(const std::filesystem::path&,
                                         const std::string&) override {
    return ace::dock::ProjectEntryOutcome::refused_target;
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

TEST_CASE("open_ui e2e: rail New / Open / Recent drive the gateway; cancel spawns nothing") {
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
  ImGuiTest* test = IM_REGISTER_TEST(engine, "open_ui", "rail_project_section");
  test->UserData = &state;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* state = static_cast<E2EState*>(ctx->Test->UserData);
    FakeGateway& gateway = *state->gateway;
    const std::string rail = ace::dock::tool_rail_title();
    const auto rail_ref = [&rail](const char* label) { return rail + "/" + label; };

    const std::filesystem::path project_dir = "/tmp/ace_open_ui_e2e/an_existing_project";
    const std::filesystem::path new_parent = "/tmp/ace_open_ui_e2e/a_parent_location";
    const std::filesystem::path recent_dir = "/tmp/ace_open_ui_e2e/a_recent_project";

    // Seed the recent list so the rail draws an Open Recent entry, then let a frame
    // render it.
    gateway.recent = {recent_dir};
    ctx->Yield(2);

    // 1. Open Project… — the pick returns a project dir; the rail records
    //    open_project(dir) (the fake validates nothing — it just records).
    gateway.cancel_next = false;
    gateway.next = project_dir;
    ctx->ItemClick(rail_ref("###open_project").c_str());
    ctx->Yield(2);
    IM_CHECK(gateway.opened.size() == 1);
    IM_CHECK(gateway.opened.back() == project_dir);

    // 2. A cancelled pick (nullopt) spawns nothing.
    const std::size_t opened_before = gateway.opened.size();
    gateway.cancel_next = true;
    ctx->ItemClick(rail_ref("###open_project").c_str());
    ctx->Yield(2);
    IM_CHECK(gateway.opened.size() == opened_before); // no new open_project

    // 3. Open Recent ▸ — clicking a seeded entry records open_recent(dir).
    ctx->ItemClick(rail_ref("###recent0").c_str());
    ctx->Yield(2);
    IM_CHECK(gateway.replayed.size() == 1);
    IM_CHECK(gateway.replayed.back() == recent_dir);

    // 4. New Project… — the pick returns a parent; the modal collects a name and
    //    Create records new_project(parent, name).
    gateway.cancel_next = false;
    gateway.next = new_parent;
    ctx->ItemClick(rail_ref("###new_project").c_str());
    ctx->Yield(2); // the modal opens once the pick resolves the parent
    ctx->ItemInput("New Project/Name");
    ctx->KeyCharsReplace("Fresh");
    ctx->ItemClick("New Project/Create");
    ctx->Yield(2);
    IM_CHECK(gateway.created.size() == 1);
    IM_CHECK(gateway.created.back().first == new_parent);
    IM_CHECK(gateway.created.back().second == std::string("Fresh"));

    // 5. A non-project Open selection surfaces inline feedback (no doomed spawn in
    //    the real gateway; here the fake reports failure).
    gateway.entry_outcome = ace::dock::ProjectEntryOutcome::refused_target;
    gateway.cancel_next = false;
    gateway.next = project_dir;
    ctx->ItemClick(rail_ref("###open_project").c_str());
    ctx->Yield(2);
    IM_CHECK(!state->dockspace->project_feedback().empty());
    IM_CHECK(state->dockspace->project_feedback() == std::string("That folder is not a project."));

    // 6. An unavailable recent entry also surfaces feedback.
    ctx->ItemClick(rail_ref("###recent0").c_str());
    ctx->Yield(2);
    IM_CHECK(!state->dockspace->project_feedback().empty());
    IM_CHECK(state->dockspace->project_feedback() ==
             std::string("That project is no longer available."));

    // 6b. A FAILED SIBLING EXEC is not a bad folder (A24 / D-entry_outcome-5). The shipped
    //     rail mapped every `false` onto the refusal string, so a broken install told the
    //     user their project folder was not a project and sent them back to the picker.
    //     Both flat verbs must now report the launch failure instead. Feedback is cleared
    //     before each click so neither assertion can pass on the previous step's string.
    gateway.entry_outcome = ace::dock::ProjectEntryOutcome::spawn_failed;
    state->dockspace->project_feedback().clear();
    ctx->ItemClick(rail_ref("###open_project").c_str());
    ctx->Yield(2);
    IM_CHECK(state->dockspace->project_feedback() == std::string("Could not start the editor."));
    state->dockspace->project_feedback().clear();
    ctx->ItemClick(rail_ref("###recent0").c_str());
    ctx->Yield(2);
    IM_CHECK(state->dockspace->project_feedback() == std::string("Could not start the editor."));
    gateway.entry_outcome = ace::dock::ProjectEntryOutcome::succeeded;

    // 7. The New modal rejects an empty name (stays open with feedback), and
    //    Cancel dismisses it without recording a new_project.
    const std::size_t created_before = gateway.created.size();
    gateway.next = new_parent;
    ctx->ItemClick(rail_ref("###new_project").c_str());
    ctx->Yield(2);
    ctx->ItemClick("New Project/Create"); // empty name → rejected, modal persists
    ctx->Yield(2);
    IM_CHECK(gateway.created.size() == created_before);
    // The ONE refusal string both compose failures render (D27 / D-dir_is_project-6): an
    // invalid name and a target that already exists call for the same corrective act.
    IM_CHECK(state->dockspace->project_feedback() ==
             std::string("Enter a project name that does not already exist here."));
    ctx->ItemClick("New Project/Cancel");
    ctx->Yield(2);
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

TEST_CASE("open_ui: run_editor threads an injected gateway into the rail") {
  FakeGateway gateway;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 320;
  opts.height = 240;
  opts.max_frames = 3;
  opts.project_gateway = &gateway;

  // The injected-gateway branch of run_editor: no real AppProjectGateway / SDL
  // dialog is constructed, and the rail queries the injected gateway each frame.
  REQUIRE(ace::app::run_editor(opts) == 0);
  CHECK(gateway.recent_queries > 0);
}
