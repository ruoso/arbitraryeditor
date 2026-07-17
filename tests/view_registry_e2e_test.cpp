#include <ace/app/shell.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/view_registry.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h> // ImGuiWindow / ImGuiDockNode
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <algorithm>
#include <string>
#include <vector>

// The view-registry UI e2e (docs/01-architecture.md §9 :189): reuse the
// dockspace_e2e rig — offscreen SDL + software GL — and drive open / close /
// reopen / multi-instance BY STABLE VIEW ID against ImGui's live dock tree and
// the authoritative DockLayout. Close is model-authoritative (D-view-registry-6);
// reopen restores the same stable id; two canvases coexist (D18). No byte-exact
// golden (software-GL pixels are flaky; the leaf composes chrome, not a Document).
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::dockmodel::ViewType;

namespace {
bool layout_contains(const ace::dock::Dockspace& d, const char* id) {
  const std::vector<std::string> ids = d.layout().view_ids();
  return std::find(ids.begin(), ids.end(), std::string(id)) != ids.end();
}
} // namespace

TEST_CASE("view_registry e2e: open/close/reopen and multi-instance canvases") {
  // Constructor edge shapes — pure layout construction, no ImGui: an empty seed
  // yields an empty layout; a single-view seed yields a lone root leaf.
  CHECK(ace::dock::Dockspace(std::vector<ViewType>{}).layout().empty());
  CHECK(ace::dock::Dockspace(std::vector<ViewType>{ViewType::Canvas}).layout().view_ids() ==
        std::vector<std::string>{"canvas#1"});

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  // Default arrangement: canvas#1 + Inspector/Layers/Overview (placeholders — the
  // mechanics are body-agnostic). The e2e drives the registry through Dockspace.
  ace::dock::Dockspace dockspace;
  shell.set_draw_content([&dockspace]() { dockspace.draw(); });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  ImGuiTest* test = IM_REGISTER_TEST(engine, "view_registry", "open_close_reopen");
  // TestFunc is a plain function pointer (std::function is disabled in this
  // build), so the Dockspace is threaded through UserData rather than captured.
  test->UserData = &dockspace;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    ace::dock::Dockspace& dockspace = *static_cast<ace::dock::Dockspace*>(ctx->Test->UserData);

    // 1. A registered singleton (Inspector) is present and docked.
    IM_CHECK(ctx->WindowInfo("inspector").ID != 0);
    ImGuiWindow* w_inspector = ctx->GetWindowByRef("inspector");
    IM_CHECK(w_inspector != nullptr);
    IM_CHECK(w_inspector->DockNode != nullptr);
    IM_CHECK(layout_contains(dockspace, "inspector"));

    // 2. Close it via the tab ✕ (WindowClose clicks the window's close button):
    //    ImGui clears p_open, the dockspace routes that through the L1 close, and
    //    the model — the source of truth — drops it (D-view-registry-6). After a
    //    frame it is gone from the layout and no longer an active window.
    ctx->WindowClose("inspector");
    ctx->Yield(3);
    IM_CHECK(!layout_contains(dockspace, "inspector"));
    IM_CHECK(!ctx->GetWindowByRef("inspector")->Active);

    // 3. Reopen via the registry → the window reappears under the SAME id in a
    //    valid dock node (reopen re-seeds the model + re-docks — D-view-registry-6).
    IM_CHECK(dockspace.reopen(ViewType::Inspector) == "inspector");
    ctx->Yield(3);
    IM_CHECK(layout_contains(dockspace, "inspector"));
    IM_CHECK(ctx->WindowInfo("inspector").ID != 0);
    ImGuiWindow* w_reopened = ctx->GetWindowByRef("inspector");
    IM_CHECK(w_reopened != nullptr);
    IM_CHECK(w_reopened->Active);
    IM_CHECK(w_reopened->DockNode != nullptr);

    // 4. Multi-instance: a second Canvas yields two distinct canvas#N windows
    //    coexisting (D18 "multiple canvases side by side").
    IM_CHECK(layout_contains(dockspace, "canvas#1"));
    IM_CHECK(dockspace.open(ViewType::Canvas) == "canvas#2");
    ctx->Yield(3);
    IM_CHECK(layout_contains(dockspace, "canvas#2"));
    IM_CHECK(ctx->WindowInfo("canvas#1").ID != 0);
    IM_CHECK(ctx->WindowInfo("canvas#2").ID != 0);
    IM_CHECK(ctx->GetWindowByRef("canvas#1")->DockNode != nullptr);
    IM_CHECK(ctx->GetWindowByRef("canvas#2")->DockNode != nullptr);
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
