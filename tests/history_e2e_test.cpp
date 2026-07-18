// editor.panels.history — History panel UI e2e (docs/01-architecture.md §9 :189).
// Reuse the view_registry_e2e rig — offscreen SDL + software GL — but drive the
// History body over a REAL commands::AppState (ScratchDir + create_project +
// seeded edits, mirroring undo_test.cpp), since the panel reads a real journal and
// loops the shipped undo/redo verbs. Drives BY STABLE VIEW ID ("history") and BY
// STABLE ROW ID (###entryN / ###base): list shape, jump back, jump forward, head
// no-op, jump to base, and the label branches. Asserts journal cursor/revision/row
// state, not pixels (software-GL pixels are flaky; D-history / undo.md rationale).
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h> // ImGuiWindow
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

using ace::app::Shell;
using ace::app::ShellOptions;
using ace::commands::AppState;
using ace::commands::Command;
using ace::commands::dispatch;
using ace::dockmodel::ViewType;

namespace {

// A temp dir wiped on entry and exit, named distinctly so this suite never collides
// with the other suites in the one ace_shell_test binary.
struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_history_e2e_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// A named "add solid content" edit — one Document wrapper call, so exactly one
// journal entry. Distinct names per seed so the panel's per-row labels differ (the
// stable ###ids drive the rows; the visible name feeds the Undo/Redo affordances).
Command add_named_content(const char* name, arbc::ObjectId& out) {
  return Command{name, [&out](arbc::Document& doc) {
                   out = doc.add_content(
                       std::make_shared<arbc::SolidContent>(arbc::Rgba{0.0F, 0.5F, 0.0F, 1.0F}));
                 }};
}

// TestFunc is a plain function pointer (std::function is disabled in this build),
// so the Dockspace + AppState are threaded through UserData rather than captured.
struct E2EState {
  ace::dock::Dockspace* dockspace;
  AppState* state;
};

} // namespace

TEST_CASE("history e2e: list shape + click-to-jump back/forward/head/base over a real journal") {
  // A real workspace-backed session seeded with three distinct edits: cursor at the
  // tip, three undoable entries (the fixture pattern of undo_test.cpp).
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "history");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));

  arbc::ObjectId id{};
  dispatch(state, add_named_content("edit#0", id));
  dispatch(state, add_named_content("edit#1", id));
  dispatch(state, add_named_content("edit#2", id));
  const arbc::Journal& journal = state.document().journal();
  REQUIRE(journal.depth() >= 3);
  REQUIRE(journal.cursor() == journal.depth());

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  // Register the History body over the real session — the same seam the L4 shell
  // uses (D-history-3). Cleared after the loop so the process-global seam never
  // outlives this AppState into a later test in the ace_shell_test binary.
  ace::views::register_view_body(ViewType::History, [&state](std::string_view view_id) {
    ace::views::draw_history(state, view_id);
  });

  ace::dock::Dockspace dockspace;
  shell.set_draw_content([&dockspace]() { dockspace.draw(); });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&dockspace, &state};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "history", "click_to_jump");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    ace::dock::Dockspace& dockspace = *e2e->dockspace;
    AppState& state = *e2e->state;
    const arbc::Journal& journal = state.document().journal();

    // Open the singleton History view and let its body render.
    IM_CHECK(dockspace.open(ViewType::History) == "history");
    ctx->Yield(3);
    IM_CHECK(ctx->WindowInfo("history").ID != 0);

    const std::size_t depth = journal.depth();
    // The three seeded edits are the last three journal indices; the head is the tip.
    const std::size_t i0 = depth - 3;
    const std::size_t i2 = depth - 1;
    const auto entry_ref = [](std::size_t i) {
      return std::string("history/###entry") + std::to_string(i);
    };

    // 1. List shape: the synthetic base row + the entry rows render; cursor is at
    //    the tip (head == last entry).
    IM_CHECK(ctx->ItemExists("history/###base"));
    IM_CHECK(ctx->ItemExists(entry_ref(i0).c_str()));
    IM_CHECK(ctx->ItemExists(entry_ref(i2).c_str()));
    IM_CHECK(journal.cursor() == depth);

    // 2. Jump back: click an earlier entry → cursor lands at index+1 as a FORWARD
    //    publish (revision advances; navigation never rewinds the revision).
    const std::uint64_t rev_before = state.document().pin()->revision();
    ctx->ItemClick(entry_ref(i0).c_str());
    ctx->Yield(2);
    IM_CHECK(journal.cursor() == i0 + 1);
    IM_CHECK(state.document().pin()->revision() > rev_before);

    // 3. Jump forward: click a later (now-dimmed) entry → cursor advances to index+1.
    ctx->ItemClick(entry_ref(i2).c_str());
    ctx->Yield(2);
    IM_CHECK(journal.cursor() == i2 + 1); // == depth

    // 4. Head is a no-op: clicking the current head loops zero times — cursor and
    //    journal depth both unchanged (no forward publish).
    const std::size_t depth_before = journal.depth();
    ctx->ItemClick(entry_ref(i2).c_str());
    ctx->Yield(2);
    IM_CHECK(journal.cursor() == depth);
    IM_CHECK(journal.depth() == depth_before);

    // 5. Jump to base: click the base row → cursor 0 (no head among the entries),
    //    everything redoable, nothing undoable.
    ctx->ItemClick("history/###base");
    ctx->Yield(2);
    IM_CHECK(journal.cursor() == 0);
    IM_CHECK(journal.can_redo());
    IM_CHECK(!journal.can_undo());

    // 6. Return to the tip: redo forward — the head follows the cursor, and the
    //    Undo/Redo affordance labels (exercised across the cursor==0 and cursor>0
    //    states above) track entry_at(cursor-1)/entry_at(cursor).
    ctx->ItemClick(entry_ref(i2).c_str());
    ctx->Yield(2);
    IM_CHECK(journal.cursor() == depth);
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

  // Clear the body before the captured AppState dies — the seam is process-global.
  ace::views::register_view_body(ViewType::History, {});

  CHECK(frames < k_max_frames); // the queue drained (no hang / timeout)
  CHECK(count_tested == 1);
  CHECK(count_success == 1);

  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
