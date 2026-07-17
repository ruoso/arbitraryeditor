// editor.dock.workspaces — ImGui Test Engine e2e (docs/01-architecture.md §9).
// Boots the shell + Dockspace + rail switcher wired to a WorkspaceStore over a
// ScratchDir, then drives the rail by stable widget id: apply Compose, apply
// Paint, "Save current as X", re-apply X and assert the persisted round-trip.
// No byte-exact screenshot assertion (software-GL pixels are flaky — §9.1).
#include <ace/app/shell.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/workspaces.hpp>
#include <ace/platform/filesystem.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <filesystem>
#include <string>
#include <system_error>

using ace::app::Shell;
using ace::app::ShellOptions;
using ace::dockmodel::DockLayout;

namespace {

// A throwaway prefs dir under the OS temp dir, wiped on entry/exit (distinct name
// from the L1 store test so the two never collide under a parallel ctest).
struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_workspace_e2e") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

DockLayout builtin_layout(const char* name) {
  for (const ace::dockmodel::WorkspacePreset& preset : ace::dockmodel::workspace_builtins()) {
    if (preset.name == name) {
      return preset.layout;
    }
  }
  return DockLayout{};
}

} // namespace

TEST_CASE("workspaces e2e: apply built-ins, save-as, and re-apply a user preset") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem filesystem;
  ace::dockmodel::WorkspaceStore store(scratch.root, filesystem);

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  ace::dock::Dockspace dockspace;
  dockspace.set_workspace_store(&store);
  shell.set_draw_content([&dockspace]() { dockspace.draw(); });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  ImGuiTest* test = IM_REGISTER_TEST(engine, "workspaces", "apply_save_reapply");
  // TestFunc is a plain function pointer (std::function is disabled), so the
  // Dockspace is threaded through UserData.
  test->UserData = &dockspace;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    ace::dock::Dockspace& dockspace = *static_cast<ace::dock::Dockspace*>(ctx->Test->UserData);

    // 1. Apply the Compose built-in by clicking its rail entry: the live layout
    //    becomes exactly the Compose arrangement.
    ctx->ItemClick("Tool Rail/Compose");
    ctx->Yield(3);
    IM_CHECK(dockspace.layout() == builtin_layout("Compose"));

    // 2. Switch to Paint: the layout changes to the Paint arrangement.
    ctx->ItemClick("Tool Rail/Paint");
    ctx->Yield(3);
    IM_CHECK(dockspace.layout() == builtin_layout("Paint"));

    // 3. Save the current (Paint) layout as "X", then switch away to Compose.
    ctx->ItemInput("Tool Rail/##workspace_name");
    ctx->KeyCharsReplace("X");
    ctx->ItemClick("Tool Rail/Save current as");
    ctx->Yield(3);
    ctx->ItemClick("Tool Rail/Compose");
    ctx->Yield(3);
    IM_CHECK(dockspace.layout() == builtin_layout("Compose"));

    // 4. Re-apply the persisted user preset "X": it round-trips through disk back
    //    to the Paint layout it captured. (ItemClick asserts "X" is listed — the
    //    save persisted and presets() now surfaces it — or the test fails here.)
    ctx->ItemClick("Tool Rail/X");
    ctx->Yield(3);
    IM_CHECK(dockspace.layout() == builtin_layout("Paint"));
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

  CHECK(frames < k_max_frames); // the queue drained (no hang / timeout)
  CHECK(count_tested == 1);
  CHECK(count_success == 1);

  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
