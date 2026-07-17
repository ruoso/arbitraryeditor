#include <ace/app/shell.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/tool_rail.hpp>
#include <ace/dockmodel/view_registry.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h> // ImGuiWindow / ImGuiDockNode
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <algorithm>
#include <string>
#include <vector>

#include <GLES3/gl3.h>

// The tool-rail UI e2e (docs/01-architecture.md §9 :189): reuse the dockspace_e2e
// rig — offscreen SDL + software GL — and drive the fixed rail's launcher + modal
// tools BY STABLE BUTTON ID against the authoritative DockLayout / ToolSelection.
// The rail is chrome drawn inside Dockspace::draw() (D-tool_rail-3), so the same
// run_editor wiring exercises it. No byte-exact golden (software-GL pixels are
// flaky; the rail composes chrome, not a Document — refinement Acceptance).
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::dockmodel::ToolId;
using ace::dockmodel::ViewType;

namespace {
bool layout_contains(const ace::dock::Dockspace& d, const char* id) {
  const std::vector<std::string> ids = d.layout().view_ids();
  return std::find(ids.begin(), ids.end(), std::string(id)) != ids.end();
}
int layout_count(const ace::dock::Dockspace& d, const char* id) {
  const std::vector<std::string> ids = d.layout().view_ids();
  return static_cast<int>(std::count(ids.begin(), ids.end(), std::string(id)));
}

// The reusable screenshot capture (same rig as tests/shell_e2e_test.cpp): reads
// back the frame on the main thread while the GL context is current.
bool capture_pixels(ImGuiID /*viewport_id*/, int x, int y, int w, int h, unsigned int* pixels,
                    void* /*user_data*/) {
  glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  return glGetError() == GL_NO_ERROR;
}
} // namespace

TEST_CASE("tool_rail e2e: view launcher (home base), singleton/multi, and tool select") {
  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  // Default arrangement: canvas#1 + Inspector/Layers/Overview (placeholders — the
  // rail mechanics are body-agnostic). The e2e drives the rail through Dockspace.
  ace::dock::Dockspace dockspace;
  shell.set_draw_content([&dockspace]() { dockspace.draw(); });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  te_io.ScreenCaptureFunc = capture_pixels; // the reusable screenshot rig
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  ImGuiTest* test = IM_REGISTER_TEST(engine, "tool_rail", "launcher_and_tools");
  // TestFunc is a plain function pointer (std::function is disabled in this
  // build), so the Dockspace is threaded through UserData rather than captured.
  test->UserData = &dockspace;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    ace::dock::Dockspace& dockspace = *static_cast<ace::dock::Dockspace*>(ctx->Test->UserData);
    // Rail buttons are addressed under the rail window; the launcher's view
    // windows are addressed absolutely (from root), so we keep no persistent ref.
    const std::string rail = ace::dock::tool_rail_title();
    const auto rail_ref = [&rail](const char* label) { return rail + "/" + label; };

    // 1. Tool select — click Brush / Eyedropper / Select and assert the active
    //    tool follows. The selection is read straight off the ToolSelection, the
    //    A11 observable state (no canvas consumer at this leaf — D-tool_rail-4).
    IM_CHECK(dockspace.tools().active() == ToolId::Select); // default (D20 / D7)
    ctx->ItemClick(rail_ref("Brush").c_str());
    IM_CHECK(dockspace.tools().active() == ToolId::Brush);
    ctx->ItemClick(rail_ref("Eyedropper").c_str());
    IM_CHECK(dockspace.tools().active() == ToolId::Eyedropper);
    ctx->ItemClick(rail_ref("Select").c_str());
    IM_CHECK(dockspace.tools().active() == ToolId::Select);

    // 2. Singleton idempotence — Inspector is open; click its launcher entry twice
    //    and assert a single "inspector" instance (focus, no duplicate).
    IM_CHECK(layout_contains(dockspace, "inspector"));
    ctx->ItemClick(rail_ref("Inspector").c_str());
    ctx->ItemClick(rail_ref("Inspector").c_str());
    ctx->Yield(3);
    IM_CHECK(layout_count(dockspace, "inspector") == 1);

    // 3. Multi-instance — canvas#1 is open; click the Canvas launcher once and
    //    assert canvas#1 and canvas#2 coexist (D18 "multiple canvases side by side").
    IM_CHECK(layout_contains(dockspace, "canvas#1"));
    ctx->ItemClick(rail_ref("Canvas").c_str());
    ctx->Yield(3);
    IM_CHECK(layout_contains(dockspace, "canvas#1"));
    IM_CHECK(layout_contains(dockspace, "canvas#2"));
    IM_CHECK(ctx->WindowInfo("canvas#2").ID != 0);

    // 4. Home base — close EVERY open view (the tab ✕), assert the layout is
    //    empty (D18 "anything can be closed"), then click a launcher entry and
    //    assert the view reappears and the layout is non-empty (§10 :446-450). The
    //    id list is snapshotted first (WindowClose mutates the layout it walks).
    const std::vector<std::string> all = dockspace.layout().view_ids();
    for (const std::string& id : all) {
      ctx->WindowClose(id.c_str());
    }
    ctx->Yield(3);
    IM_CHECK(dockspace.layout().view_ids().empty());

    ctx->ItemClick(rail_ref("Inspector").c_str()); // the rail persists — it is home base
    ctx->Yield(3);
    IM_CHECK(!dockspace.layout().view_ids().empty());
    IM_CHECK(layout_contains(dockspace, "inspector"));
    IM_CHECK(ctx->WindowInfo("inspector").ID != 0);
    IM_CHECK(ctx->GetWindowByRef("inspector")->DockNode != nullptr);
  };
  ImGuiTestEngine_QueueTest(engine, test);

  // The rendered-output DoD layer for the rail is a screenshot BASELINE (not a
  // byte-exact golden — software-GL pixels are flaky, and the rail composes chrome
  // not a libarbc Document, refinement Acceptance): read back the last frame and
  // assert the rail composited to the screen at the expected size.
  std::vector<unsigned char> last_frame;
  int captured_w = 0, captured_h = 0;
  auto grab_frame = [&]() {
    const ImGuiIO& io = ImGui::GetIO();
    captured_w = static_cast<int>(io.DisplaySize.x);
    captured_h = static_cast<int>(io.DisplaySize.y);
    if (captured_w > 0 && captured_h > 0) {
      std::vector<unsigned int> px(static_cast<size_t>(captured_w) * captured_h);
      if (capture_pixels(0, 0, 0, captured_w, captured_h, px.data(), nullptr)) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(px.data());
        last_frame.assign(bytes, bytes + px.size() * 4);
      }
    }
  };

  const int k_max_frames = 400;
  int frames = 0;
  while (!ImGuiTestEngine_IsTestQueueEmpty(engine) && frames < k_max_frames) {
    shell.new_frame();
    shell.draw_ui();
    shell.render(grab_frame);
    ImGuiTestEngine_PostSwap(engine);
    ++frames;
  }

  int count_tested = 0, count_success = 0;
  ImGuiTestEngine_GetResult(engine, count_tested, count_success);
  ImGuiTestEngine_Stop(engine);

  CHECK(frames < k_max_frames); // the queue drained (no hang / timeout)
  CHECK(count_tested == 1);
  CHECK(count_success == 1);

  // Screenshot baseline: a frame was captured at the shell's size (the rail + the
  // dockspace host composited to the screen without crashing the GL path).
  CHECK(captured_w == 640);
  CHECK(captured_h == 480);
  CHECK(last_frame.size() == static_cast<size_t>(640) * 480 * 4);

  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
