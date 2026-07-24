#include <ace/app/shell.hpp>
#include <ace/dock/welcome.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <GLES3/gl3.h>

// editor.project.welcome — the pre-project launcher's e2e (Acceptance / §9, D26/A22),
// on the tests/open_ui_e2e_test.cpp rig: offscreen SDL + software GL, driving a
// STANDALONE dock::WelcomeWindow by stable widget id — no AppState, no Dockspace, no
// WriterThread, which is the whole claim (launcher mode owns zero Documents). A fake
// ProjectGateway (records calls; scripts the folder pick synchronously) stands in for
// the real OS dialog and sibling spawn. No byte-exact golden: software-GL pixels are
// flaky and the launcher composes chrome, not a Document, so the visual assertion is
// the Test Engine screenshot baseline wired below (capture_pixels), the same
// justification open_ui and tool_rail recorded.

using ace::app::ShellOptions;
using ace::dock::WelcomeWindow;

namespace {

// Records every gateway call and scripts the async folder pick (delivered
// synchronously — determinism for the driven frames).
//
// Unlike the open_ui fake this one models the MRU store, because the welcome's
// refusal STRINGS are resolved against it: the real gateway validates, then records
// MRU-front, then spawns (src/app/project_gateway.cpp:46-71), and `recent_projects()`
// re-prunes on every load — so "still listed after a refusal" means the spawn failed
// and "gone from the list" means the target never validated. `vanished` models the
// second, `spawn_ok` the first.
class FakeGateway final : public ace::dock::ProjectGateway {
public:
  std::optional<std::filesystem::path> next; // the scripted pick result
  bool cancel_next = false;                  // deliver nullopt (a cancelled pick)
  bool vanished = false;                     // the target is not (or is no longer) a project
  bool spawn_ok = true;                      // the sibling exec succeeds

  std::vector<std::filesystem::path> store; // the MRU list, most-recent-first

  std::vector<std::filesystem::path> opened;
  std::vector<std::pair<std::filesystem::path, std::string>> created;
  std::vector<std::filesystem::path> replayed;
  int picks = 0;
  mutable int recent_queries = 0;

  bool open_project(const std::filesystem::path& dir) override {
    opened.push_back(dir);
    if (vanished) {
      forget(dir); // never validated, so never recorded — and a re-load would prune it
      return false;
    }
    record_front(dir); // validated: recorded BEFORE the spawn is attempted
    return spawn_ok;
  }
  bool new_project(const std::filesystem::path& parent, const std::string& name) override {
    if (name.empty()) {
      return false; // mirror the real reject so the modal shows feedback + stays open
    }
    created.emplace_back(parent, name);
    return spawn_ok; // New records nothing: the target does not exist yet
  }
  bool open_recent(const std::filesystem::path& dir) override {
    replayed.push_back(dir);
    if (vanished) {
      forget(dir);
      return false;
    }
    record_front(dir);
    return spawn_ok;
  }
  void pick_folder(std::function<void(std::optional<std::filesystem::path>)> on_pick) override {
    ++picks;
    on_pick(cancel_next ? std::nullopt : next);
  }
  std::vector<std::filesystem::path> recent_projects() const override {
    ++recent_queries;
    return store;
  }
  // The session verbs never fire from a launcher (A22) — inert, exactly as the real
  // session-free ProjectEntryGateway answers them (app_project_gateway_test.cpp).
  bool save() override { return false; }
  bool is_dirty() const override { return false; }
  bool save_as(const std::filesystem::path&, const std::string&) override { return false; }
  ace::dock::GcSummary clean_up(bool) override { return {}; }
  bool undo() override { return false; }
  bool redo() override { return false; }
  bool can_undo() const override { return false; }
  bool can_redo() const override { return false; }

private:
  void forget(const std::filesystem::path& dir) {
    store.erase(std::remove(store.begin(), store.end(), dir), store.end());
  }
  void record_front(const std::filesystem::path& dir) {
    forget(dir);
    store.insert(store.begin(), dir);
  }
};

struct E2EState {
  WelcomeWindow* welcome;
  FakeGateway* gateway;
};

bool capture_pixels(ImGuiID /*viewport_id*/, int x, int y, int w, int h, unsigned int* pixels,
                    void* /*user_data*/) {
  glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  return glGetError() == GL_NO_ERROR;
}

// The scratch prefix the bootstrap's scratch project would appear under
// (src/app/shell.cpp scratch_project_dir): the one externally observable proof that
// `open_or_create_app_state` was never reached.
constexpr const char* k_scratch_prefix = "arbitraryeditor-session-";

std::size_t count_scratch_projects() {
  std::size_t found = 0;
  std::error_code ec;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(std::filesystem::temp_directory_path(), ec)) {
    if (entry.path().filename().string().rfind(k_scratch_prefix, 0) == 0) {
      ++found;
    }
  }
  return found;
}

} // namespace

TEST_CASE("welcome e2e: the three verbs drive the gateway and surface every refusal") {
  ace::app::Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  // The launcher's ENTIRE draw content — no Dockspace, no AppState, no WriterThread.
  FakeGateway gateway;
  WelcomeWindow welcome;
  welcome.set_project_gateway(&gateway);
  shell.set_draw_content([&welcome]() { welcome.draw(); });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  te_io.ScreenCaptureFunc = capture_pixels;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState state{&welcome, &gateway};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "welcome", "three_verbs_and_feedback");
  test->UserData = &state;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* state = static_cast<E2EState*>(ctx->Test->UserData);
    FakeGateway& gateway = *state->gateway;
    WelcomeWindow& welcome = *state->welcome;
    // Every ref composed from the ONE exported symbol (D-welcome-4), never a literal.
    const std::string window = ace::dock::welcome_window_title();
    const auto welcome_ref = [&window](const char* label) { return window + "/" + label; };

    const std::filesystem::path project_dir = "/tmp/ace_welcome_e2e/an_existing_project";
    const std::filesystem::path new_parent = "/tmp/ace_welcome_e2e/a_parent_location";
    const std::filesystem::path recent_dir = "/tmp/ace_welcome_e2e/a_recent_project";

    // Ordered so every NON-latching case runs first: the exit latch is one-way, so a
    // successful verb has to come last.

    // 1. The launcher lists recents from the same store a project window would, queried
    //    every frame (D26 — a launcher and a project window show the same list).
    gateway.store = {recent_dir};
    ctx->Yield(2);
    IM_CHECK(gateway.recent_queries > 0);
    IM_CHECK(ctx->ItemExists(welcome_ref("###welcome_recent0").c_str()));
    IM_CHECK(ctx->ItemExists(welcome_ref("###welcome_new").c_str()));
    IM_CHECK(ctx->ItemExists(welcome_ref("###welcome_open").c_str()));

    // 2. A CANCELLED pick is not a choice (D-welcome-8): a pick is recorded, nothing
    //    spawns, no feedback appears, and the launcher stays up.
    gateway.cancel_next = true;
    ctx->ItemClick(welcome_ref("###welcome_open").c_str());
    ctx->Yield(2);
    IM_CHECK(gateway.picks == 1);
    IM_CHECK(gateway.opened.empty());
    IM_CHECK(welcome.feedback().empty());
    IM_CHECK(!welcome.exit_requested());

    // 3. A non-project pick: the rail's exact string, and no exit.
    gateway.cancel_next = false;
    gateway.next = project_dir;
    gateway.vanished = true;
    ctx->ItemClick(welcome_ref("###welcome_open").c_str());
    ctx->Yield(2);
    IM_CHECK(gateway.opened.size() == 1);
    IM_CHECK(welcome.feedback() == "That folder is not a project.");
    IM_CHECK(!welcome.exit_requested());

    // 4. A VALIDATED target whose spawn failed: the target was recorded MRU-front, so
    //    the welcome can tell this apart from a refused target and says so.
    gateway.vanished = false;
    gateway.spawn_ok = false;
    ctx->ItemClick(welcome_ref("###welcome_open").c_str());
    ctx->Yield(2);
    IM_CHECK(gateway.opened.size() == 2);
    IM_CHECK(welcome.feedback() == "Could not start the editor.");
    IM_CHECK(!welcome.exit_requested());

    // 5. A recent entry that vanished since the list was rendered: refused, pruned, and
    //    reported with the rail's second string. Re-seed the store first — step 4's
    //    failed spawn left its own entry MRU-front.
    gateway.spawn_ok = true;
    gateway.vanished = true;
    gateway.store = {recent_dir};
    ctx->Yield(2);
    ctx->ItemClick(welcome_ref("###welcome_recent0").c_str());
    ctx->Yield(2);
    IM_CHECK(gateway.replayed.size() == 1);
    IM_CHECK(gateway.replayed.back() == recent_dir);
    IM_CHECK(welcome.feedback() == "That project is no longer available.");
    IM_CHECK(!welcome.exit_requested());

    // 6. New: pick a parent, then the SHARED compose modal (D-welcome-7) — same popup
    //    id and same three refs the rail drives. An empty name is refused, the modal
    //    stays open, and nothing exits.
    gateway.vanished = false;
    gateway.next = new_parent;
    ctx->ItemClick(welcome_ref("###welcome_new").c_str());
    ctx->Yield(2); // the modal opens once the pick resolves the parent
    IM_CHECK(ctx->ItemExists("New Project/Create"));
    ctx->ItemClick("New Project/Create"); // empty name -> refused, modal persists
    ctx->Yield(2);
    IM_CHECK(gateway.created.empty());
    // The ONE refusal string (D27 / D-dir_is_project-6) — the launcher and the rail refuse
    // the same targets and say the same thing about it (D26).
    IM_CHECK(welcome.feedback() == "Enter a project name that does not already exist here.");
    IM_CHECK(ctx->ItemExists("New Project/Create"));
    IM_CHECK(!welcome.exit_requested());

    // 7. …and a valid name creates the target AND latches the exit: the sibling exists
    //    before the launcher goes away (D-welcome-8).
    ctx->ItemInput("New Project/Name");
    ctx->KeyCharsReplace("Fresh");
    ctx->ItemClick("New Project/Create");
    ctx->Yield(2);
    IM_CHECK(gateway.created.size() == 1);
    IM_CHECK(gateway.created.back().first == new_parent);
    IM_CHECK(gateway.created.back().second == std::string("Fresh"));
    IM_CHECK(welcome.exit_requested());
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

TEST_CASE("welcome e2e: dismissal exits cleanly, and the compose modal is guarded from Esc") {
  ace::app::Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  // Its own rig with its own fresh WelcomeWindow (the two-test-two-rig pattern of
  // tests/reopen_degradation_notice_e2e_test.cpp), because the exit latch is one-way:
  // the GUARDED phase has to run before anything latches it.
  FakeGateway gateway;
  WelcomeWindow welcome;
  welcome.set_project_gateway(&gateway);
  shell.set_draw_content([&welcome]() { welcome.draw(); });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  te_io.ScreenCaptureFunc = capture_pixels;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState state{&welcome, &gateway};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "welcome", "dismiss_exits");
  test->UserData = &state;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* state = static_cast<E2EState*>(ctx->Test->UserData);
    FakeGateway& gateway = *state->gateway;
    WelcomeWindow& welcome = *state->welcome;
    const std::string window = ace::dock::welcome_window_title();
    const auto welcome_ref = [&window](const char* label) { return window + "/" + label; };

    ctx->Yield(2);
    IM_CHECK(!welcome.exit_requested());

    // Phase 1 — the guard (Constraint 8 / D-welcome-5). A BeginPopupModal opened with
    // `p_open == nullptr` does not consume Esc, so an UNGUARDED check would exit the
    // launcher out from under a half-typed project name. Open the compose modal, press
    // Esc, and assert the launcher did not latch.
    gateway.next = std::filesystem::path("/tmp/ace_welcome_e2e/a_parent_location");
    ctx->ItemClick(welcome_ref("###welcome_new").c_str());
    ctx->Yield(2);
    IM_CHECK(ctx->ItemExists("New Project/Cancel"));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(4);
    IM_CHECK(!welcome.exit_requested());
    ctx->ItemClick("New Project/Cancel"); // dismiss the modal, not the launcher
    ctx->Yield(2);
    IM_CHECK(!welcome.exit_requested());

    // Phase 2 — dismissal with no popup open exits cleanly and spawns NOTHING (D26):
    // there is nothing to fall back to, and inventing a fallback would re-mint exactly
    // the throwaway project the launcher exists to stop minting.
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);
    IM_CHECK(welcome.exit_requested());
    IM_CHECK(gateway.opened.empty());
    IM_CHECK(gateway.created.empty());
    IM_CHECK(gateway.replayed.empty());
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

TEST_CASE("welcome: run_welcome_launcher drives a no-arg launch without a Document") {
  FakeGateway gateway;
  gateway.store = {std::filesystem::path("/tmp/ace_welcome_e2e/a_recent_project")};

  ShellOptions opts;
  opts.headless = true;
  opts.width = 320;
  opts.height = 240;
  opts.max_frames = 3;
  opts.project_gateway = &gateway;

  // The one externally observable proof that `open_or_create_app_state` was never
  // reached: launcher mode must not leave a scratch project directory (with its live
  // `workspace/` mmap arena) behind under the OS temp dir — which is the litter this
  // leaf exists to remove (D26). A prefix-scoped before/after listing.
  const std::size_t scratch_before = count_scratch_projects();
  REQUIRE(ace::app::run_welcome_launcher(opts) == 0);
  CHECK(count_scratch_projects() == scratch_before);

  // …and the welcome really drew: the injected gateway was queried for recents on the
  // driven frames, through the same injection branch run_editor offers.
  CHECK(gateway.recent_queries > 0);
}

TEST_CASE("welcome: wants_welcome_launcher picks launcher mode only for an interactive no-arg "
          "launch") {
  // The full truth table (A22 / D-welcome-1 / D-welcome-2). Pure — no SDL, no
  // filesystem, no libarbc — which is the point of keeping it a free predicate.
  ShellOptions opts;

  opts.project_dir.clear();
  opts.headless = false;
  CHECK(ace::app::wants_welcome_launcher(opts)); // the ONE launcher case

  opts.headless = true;
  CHECK_FALSE(ace::app::wants_welcome_launcher(opts)); // a driven run keeps scratch

  opts.project_dir = "/tmp/ace_welcome_e2e/some_project";
  opts.headless = false;
  CHECK_FALSE(ace::app::wants_welcome_launcher(opts)); // a path given: project mode

  opts.headless = true;
  CHECK_FALSE(ace::app::wants_welcome_launcher(opts));
}
