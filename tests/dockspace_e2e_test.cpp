#include <ace/app/shell.hpp>
#include <ace/dock/dock.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h> // ImGuiWindow / ImGuiDockNode / DockNodeGetRootNode
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

// The dockspace UI e2e (docs/01-architecture.md §9 :189 — "open→dock→select…").
// It reuses the ImGui Test Engine rig from tests/shell_e2e_test.cpp: boot the
// shell against the SDL offscreen driver + software GL, install the catalog-
// driven dockspace draw-content (the same wiring run_editor uses), and drive the
// D18 mechanics BY STABLE VIEW ID — presence, tab-select, and drag-to-dock —
// asserting each against ImGui's live dock tree. No byte-exact golden (software-GL
// pixels are flaky; the dockspace composes chrome, not a Document — refinement
// Acceptance).
using ace::app::Shell;
using ace::app::ShellOptions;

TEST_CASE("dockspace e2e: views are addressable, tab-select and drag-to-dock work") {
  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  // The default arrangement: a Canvas fills one side; Inspector / Layers /
  // Overview share the other side as a tab-group. Every body is a placeholder
  // here (no Canvas texture registered) — the mechanics are body-agnostic.
  ace::dock::Dockspace dockspace;
  shell.set_draw_content([&dockspace]() { dockspace.draw(); });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  ImGuiTest* test = IM_REGISTER_TEST(engine, "dockspace", "mechanics");
  test->TestFunc = [](ImGuiTestContext* ctx) {
    const char* canvas = "canvas#1";
    const char* inspector = "inspector";
    const char* layers = "layers";

    // 1. The host and each view are addressable by their stable instance id.
    IM_CHECK(ctx->WindowInfo(canvas).ID != 0);
    IM_CHECK(ctx->WindowInfo(inspector).ID != 0);
    IM_CHECK(ctx->WindowInfo(layers).ID != 0);

    ImGuiWindow* w_canvas = ctx->GetWindowByRef(canvas);
    ImGuiWindow* w_inspector = ctx->GetWindowByRef(inspector);
    ImGuiWindow* w_layers = ctx->GetWindowByRef(layers);
    IM_CHECK(w_canvas != nullptr);
    IM_CHECK(w_inspector != nullptr);
    IM_CHECK(w_layers != nullptr);

    // All views are docked into the one main-viewport dockspace host (the host
    // node exists and is shared — D18 fully-uniform shell, D-dockspace-5).
    IM_CHECK(w_canvas->DockNode != nullptr);
    IM_CHECK(w_inspector->DockNode != nullptr);
    IM_CHECK(ImGui::DockNodeGetRootNode(w_canvas->DockNode) ==
             ImGui::DockNodeGetRootNode(w_inspector->DockNode));

    // 2. Tab-select: Inspector / Layers seed a shared tab-group; click Layers'
    //    tab and assert it becomes the node's selected (focused) tab.
    IM_CHECK(w_inspector->DockNode == w_layers->DockNode);
    ctx->ItemClick(w_layers->TabId); // the window's tab button in the shared bar
    IM_CHECK(w_layers->DockNode->TabBar != nullptr);
    IM_CHECK(w_layers->DockNode->TabBar->SelectedTabId == w_layers->TabId);

    // 3. Drag-to-dock: move Layers into the canvas container as a tab; the moved
    //    view now shares the target node (the D18 drag/tab mechanic).
    ctx->DockInto(layers, canvas, ImGuiDir_None);
    IM_CHECK(w_layers->DockNode != nullptr);
    IM_CHECK(w_layers->DockNode == w_canvas->DockNode);
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

  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
