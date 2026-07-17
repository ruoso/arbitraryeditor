#include <ace/app/probe.hpp>
#include <ace/app/shell.hpp>
#include <ace/dock/dock.hpp>
#include <ace/views/views.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h> // ImGuiWindow / ImGuiDockNode / DockNodeGetRootNode
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

// The dockspace UI e2e (docs/01-architecture.md §9 :189 — "open→dock→select…").
// It reuses the ImGui Test Engine rig from tests/shell_e2e_test.cpp: boot the
// shell against the SDL offscreen driver + software GL, install the dockspace
// draw-content (the same wiring run_editor uses), and drive the D18 mechanics
// BY STABLE WIDGET ID — presence, tab-select, and drag-to-dock — asserting each
// against ImGui's live dock tree. No byte-exact golden (software-GL pixels are
// flaky; the dockspace composes chrome, not a Document — refinement Acceptance).
using ace::app::ProbeView;
using ace::app::Shell;
using ace::app::ShellOptions;

TEST_CASE("dockspace e2e: panes are addressable, tab-select and drag-to-dock work") {
  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  // The render_probe pane is the canvas stand-in; its texture is uploaded after
  // init (GL context current). The dockspace tiles the viewport and draws the
  // two placeholder panels; the probe pane is drawn afterward (run_editor wiring).
  ProbeView probe;
  probe.upload();
  ace::dock::Dockspace dockspace(ace::dock::default_layout());
  shell.set_draw_content([&dockspace, &probe]() {
    dockspace.draw();
    probe.draw();
  });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  ImGuiTest* test = IM_REGISTER_TEST(engine, "dockspace", "mechanics");
  test->TestFunc = [](ImGuiTestContext* ctx) {
    const char* probe_id = ace::views::probe_pane_title();
    const char* panel_a = ace::dock::panel_a_title();
    const char* panel_b = ace::dock::panel_b_title();

    // 1. The host and each placeholder pane exist and are addressable by id.
    IM_CHECK(ctx->WindowInfo(probe_id).ID != 0);
    IM_CHECK(ctx->WindowInfo(panel_a).ID != 0);
    IM_CHECK(ctx->WindowInfo(panel_b).ID != 0);

    ImGuiWindow* w_probe = ctx->GetWindowByRef(probe_id);
    ImGuiWindow* w_a = ctx->GetWindowByRef(panel_a);
    ImGuiWindow* w_b = ctx->GetWindowByRef(panel_b);
    IM_CHECK(w_probe != nullptr);
    IM_CHECK(w_a != nullptr);
    IM_CHECK(w_b != nullptr);

    // All panes are docked into the one main-viewport dockspace host (the host
    // node exists and is shared — D18 fully-uniform shell, D-dockspace-5).
    IM_CHECK(w_probe->DockNode != nullptr);
    IM_CHECK(w_a->DockNode != nullptr);
    IM_CHECK(ImGui::DockNodeGetRootNode(w_probe->DockNode) ==
             ImGui::DockNodeGetRootNode(w_a->DockNode));

    // 2. Tab-select: the two panels seed a shared tab-group; click Panel B's tab
    //    and assert it becomes the node's selected (focused) tab.
    IM_CHECK(w_a->DockNode == w_b->DockNode);
    ctx->ItemClick(w_b->TabId); // the window's tab button in the shared tab bar
    IM_CHECK(w_b->DockNode->TabBar != nullptr);
    IM_CHECK(w_b->DockNode->TabBar->SelectedTabId == w_b->TabId);

    // 3. Drag-to-dock: move Panel B into the probe's container as a tab; the
    //    moved pane now shares the target node (the D18 drag/tab mechanic).
    ctx->DockInto(panel_b, probe_id, ImGuiDir_None);
    IM_CHECK(w_b->DockNode != nullptr);
    IM_CHECK(w_b->DockNode == w_probe->DockNode);
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

  // The main-viewport dockspace host node exists and is addressable by its id
  // (the ImGui context is still alive until shutdown() below).
  CHECK(ImGui::DockBuilderGetNode(dockspace.dockspace_id()) != nullptr);

  // Teardown order mirrors shell_e2e: destroy the probe texture while the GL
  // context is valid, shut the shell (which destroys the ImGui context the
  // engine hooked), then destroy the engine.
  probe.destroy();
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
