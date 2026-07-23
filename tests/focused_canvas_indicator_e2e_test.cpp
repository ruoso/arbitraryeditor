// editor.canvas.focused_canvas_indicator — the marker e2e (docs/01-architecture.md §9, the
// offscreen SDL3 + software-GL (llvmpipe) ASan lane). Reuses tests/multi_canvas_mint_e2e_test.cpp's
// rig (ScratchDir, the noop dialog/launcher, the UserData POD, pump_until/settle, the drive loop)
// and tests/multi_canvas_e2e_test.cpp's framebuffer probe (the snapshot atomic, the grab_frame
// lambda handed to shell.render(), the glReadPixels y-flip).
//
// The state assertions are on `CanvasView::indicated_view_id()` — the RULE — and the pixel probe is
// a corroborating second signal, never the only one: a framebuffer read under software GL cannot
// distinguish "the rule is wrong" from "the draw is wrong", so both are pinned separately
// (D-focused_canvas_indicator-5).
//
// Asserts: a single pane is marked; the marker follows WindowFocus to canvas#2; the border is on
// canvas#2 and NOT on canvas#1 (the two-sided probe — a marker on every pane fails, a marker on
// none fails); it SURVIVES the rail click that steals ImGui focus (the phase the leaf exists for:
// at mint time no canvas is focused, so a live IsWindowFocused poll would move or drop the border);
// and closing the marked canvas moves the marker to the lowest-id FALLBACK pane, which the raw
// sticky hint — cleared by reconcile — no longer names.
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/app/view_framing.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/cell.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/expected.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h> // ImRect (ImGuiTestItemInfo::RectFull)
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <GLES3/gl3.h>

using ace::app::AppProjectGateway;
using ace::app::CanvasView;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::commands::AppState;
using ace::dockmodel::ViewType;

namespace {

// The marker's OPAQUE accent (D-focused_canvas_indicator-4). The ±8 band is §9's justified
// tolerance: the probed pixels are ImGui's own flat vector output drawn at alpha 255, so there is
// no blend against the varying canvas texture beneath ("software-GL frames are not byte-comparable"
// applies to composited canvas content, not to a flat triangle), and ±8 absorbs llvmpipe's
// rasteriser rounding without admitting the ~(26,26,31) shell clear colour, the seeded backdrop, or
// the amber (≈223,191,92) camera-frame gizmo that shares the pane edge after the phase-5 mint.
constexpr int k_accent_r = 120;
constexpr int k_accent_g = 200;
constexpr int k_accent_b = 255;
constexpr int k_accent_tol = 8;

struct Frame {
  std::vector<unsigned char> pixels;
  int width = 0;
  int height = 0;
};

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir()
      : root(std::filesystem::temp_directory_path() / "ace_focused_canvas_indicator_e2e_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

class NoopFolderDialog final : public ace::app::FolderDialog {
public:
  void show(Callback) override {}
};
class NoopLauncher final : public ace::platform::ProcessLauncher {
public:
  std::error_code spawn_detached(const std::filesystem::path&,
                                 const std::vector<std::string>&) const override {
    return {};
  }
};

bool capture_pixels(ImGuiID /*viewport_id*/, int x, int y, int w, int h, unsigned int* pixels,
                    void* /*user_data*/) {
  glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  return glGetError() == GL_NO_ERROR;
}

// TestFunc is a plain function pointer (std::function is disabled in this build), so the
// collaborators — and the frame-capture handshake with the main loop — are threaded through
// UserData rather than captured. The test coroutine and the main loop never run concurrently (a
// Yield hands control over), and the atomics carry the ordering across that handoff.
struct E2EState {
  ace::dock::Dockspace* dockspace;
  CanvasView* canvas;
  Frame* frame;
  std::atomic<bool> capture_request{false};
  std::atomic<bool> capture_done{false};
};

template <class Ready> bool pump_until(ImGuiTestContext* ctx, Ready ready) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (std::chrono::steady_clock::now() < deadline) {
    if (ready()) {
      return true;
    }
    ctx->Yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return ready();
}

// Pump until BOTH panes stop publishing, so the pane rects `##canvas_nav` reports have caught up
// with what is actually composited before a pixel is read.
void settle(ImGuiTestContext* ctx, CanvasView& canvas) {
  std::uint64_t last = canvas.frames_issued("canvas#1") + canvas.frames_issued("canvas#2");
  for (int quiet = 0; quiet < 40;) {
    ctx->Yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const std::uint64_t now = canvas.frames_issued("canvas#1") + canvas.frames_issued("canvas#2");
    if (now == last) {
      ++quiet;
    } else {
      quiet = 0;
      last = now;
    }
  }
}

bool layout_contains(const ace::dock::Dockspace& d, const char* id) {
  const std::vector<std::string> ids = d.layout().view_ids();
  return std::find(ids.begin(), ids.end(), std::string(id)) != ids.end();
}

// Is the marker's accent present within a couple of pixels of (cx, cy)? glReadPixels is bottom-up
// while ImGui reports top-left space, hence the row flip. The small neighbourhood absorbs the
// float→pixel rounding of the pane origin; it is far tighter than the ~half-pane distance between
// the two panes' edges, so the two-sided assertion below stays meaningful.
bool accent_near(const Frame& frame, int cx, int cy) {
  constexpr int k_radius = 2;
  for (int dy = -k_radius; dy <= k_radius; ++dy) {
    for (int dx = -k_radius; dx <= k_radius; ++dx) {
      const int x = cx + dx;
      const int y = cy + dy;
      if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) {
        continue;
      }
      const int fb_row = frame.height - 1 - y;
      const std::size_t idx = (static_cast<std::size_t>(fb_row) * frame.width + x) * 4;
      if (idx + 3 >= frame.pixels.size()) {
        continue;
      }
      if (std::abs(static_cast<int>(frame.pixels[idx]) - k_accent_r) <= k_accent_tol &&
          std::abs(static_cast<int>(frame.pixels[idx + 1]) - k_accent_g) <= k_accent_tol &&
          std::abs(static_cast<int>(frame.pixels[idx + 2]) - k_accent_b) <= k_accent_tol) {
        return true;
      }
    }
  }
  return false;
}

} // namespace

TEST_CASE("focused_canvas_indicator e2e: the marker names the pane the framing verbs act on") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "marker");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  state.document().add_composition(128.0, 128.0);
  // An OPAQUE, unbounded backdrop so neither pane's composite is ever blank (the canvas withholds
  // an all-transparent frame) — and so the pixel under the border is canvas content, which is what
  // makes "the border survived over the pane" a real observation rather than a read of the clear
  // colour.
  REQUIRE(ace::scene::add_cell(state.document(), state.registry(), "org.arbc.solid",
                               "0.15,0.2,0.25,1", arbc::Affine::identity())
              .has_value());

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 900;
  opts.height = 640;
  REQUIRE(shell.init(opts));

  CanvasView canvas(state);
  ace::views::register_view_body(ViewType::Canvas, [&canvas](std::string_view view_id) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content(view_id, static_cast<int>(avail.x), static_cast<int>(avail.y));
  });

  ace::dock::Dockspace dockspace;
  ace::dockmodel::RecentProjects recent(scratch.root / "prefs", fs);
  NoopFolderDialog dialog;
  NoopLauncher launcher;
  AppProjectGateway gateway(recent, fs, dialog, launcher, "/usr/bin/arbitraryeditor", state);
  gateway.set_edit_runner(
      [&canvas](const std::function<void()>& edit) { canvas.apply_edit(edit); });
  // Bound exactly as src/app/shell.cpp binds it: the ONE provider the rail's mint reads. The
  // marker must agree with THIS, which is why phase 5 clicks the real rail item rather than any
  // focus-stealing widget.
  gateway.set_view_framing([&canvas] { return canvas.focused_framing(); });
  dockspace.set_project_gateway(&gateway);

  shell.set_draw_content([&dockspace, &canvas]() {
    dockspace.draw();
    canvas.reconcile(dockspace.layout().view_ids());
  });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  te_io.ScreenCaptureFunc = capture_pixels;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  Frame frame;
  E2EState e2e{&dockspace, &canvas, &frame, {}, {}};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "multi_canvas", "focused_canvas_indicator");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    CanvasView& canvas = *e2e->canvas;
    ace::dock::Dockspace& dockspace = *e2e->dockspace;
    const std::string rail = ace::dock::tool_rail_title();
    const std::string shot_item = rail + "/###new_shot_from_view";

    // Ask the main loop for a framebuffer read-back and wait for it to land.
    auto capture = [&] {
      e2e->capture_done.store(false);
      e2e->capture_request.store(true);
      return pump_until(ctx, [&] { return e2e->capture_done.load(); });
    };
    // Where to look for a pane's border: the LEFT edge at half height. The pane rect comes from
    // "##canvas_nav", the shipped pane-rect probe (tests/camera_manip_e2e_test.cpp), whose top-left
    // IS the content origin the border is inset from. Deliberately not the top edge: the camera
    // picker's "Camera:" text is drawn over the pane's top-left corner right after the marker.
    auto left_edge_mid = [&](const char* nav_ref, int& out_x, int& out_y) {
      const ImGuiTestItemInfo nav = ctx->ItemInfo(nav_ref);
      out_x = static_cast<int>(nav.RectFull.Min.x);
      out_y = static_cast<int>(nav.RectFull.Min.y + nav.RectFull.GetHeight() * 0.5F);
    };
    ctx->Yield(2);

    // --- (1) A single pane is marked ------------------------------------------------
    // D-focused_canvas_indicator-3: the invariant does not vary with how many canvases are open,
    // so the one-canvas dock is marked too — and by the FALLBACK branch, since no canvas has yet
    // held focus.
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));
    settle(ctx, canvas);
    IM_CHECK(canvas.focused_view_id().empty() || canvas.focused_view_id() == "canvas#1");
    IM_CHECK(canvas.indicated_view_id() == "canvas#1");

    // --- (2) Open canvas#2 ----------------------------------------------------------
    IM_CHECK(!layout_contains(dockspace, "canvas#2"));
    ctx->ItemClick((rail + "/Canvas").c_str()); // the Views launcher mints canvas#2
    ctx->Yield(3);
    IM_CHECK(layout_contains(dockspace, "canvas#1"));
    IM_CHECK(layout_contains(dockspace, "canvas#2"));
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#2") >= 1; }));
    settle(ctx, canvas);

    // --- (3) The marker follows WindowFocus -----------------------------------------
    // canvas#2 opens as a tab in the panel node and its body only runs while it is the active tab,
    // so bringing it to the front is also what lets it stamp the hint at all.
    ctx->WindowFocus("canvas#2");
    IM_CHECK(pump_until(ctx, [&] { return canvas.focused_view_id() == "canvas#2"; }));
    IM_CHECK(canvas.indicated_view_id() == "canvas#2");
    settle(ctx, canvas);

    // --- (4) The border is on canvas#2 and NOT on canvas#1 --------------------------
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;
    left_edge_mid("canvas#1/##canvas_nav", x1, y1);
    left_edge_mid("canvas#2/##canvas_nav", x2, y2);
    IM_CHECK(x1 != x2 || y1 != y2); // two genuinely distinct probe sites
    IM_CHECK(capture());
    IM_CHECK(accent_near(*e2e->frame, x2, y2));
    IM_CHECK(!accent_near(*e2e->frame, x1, y1));

    // --- (5) It survives a rail interaction -----------------------------------------
    // The rail item is an ImGui::Selectable in the Tool Rail window, so at mint time NO canvas is
    // focused: a marker driven from a live IsWindowFocused poll would blink off (or move) here,
    // exactly while the user is committing the verb the marker is about. This is the phase the
    // whole leaf exists for.
    IM_CHECK((ctx->ItemInfo(shot_item.c_str()).ItemFlags & ImGuiItemFlags_Disabled) == 0);
    ctx->ItemClick(shot_item.c_str());
    ctx->Yield(3);
    IM_CHECK(canvas.focused_view_id() == "canvas#2");
    IM_CHECK(canvas.indicated_view_id() == "canvas#2");
    settle(ctx, canvas);
    left_edge_mid("canvas#1/##canvas_nav", x1, y1);
    left_edge_mid("canvas#2/##canvas_nav", x2, y2);
    IM_CHECK(capture());
    IM_CHECK(accent_near(*e2e->frame, x2, y2));  // still there…
    IM_CHECK(!accent_near(*e2e->frame, x1, y1)); // …and it did not move to canvas#1

    // --- (6) The FALLBACK pane is marked too ----------------------------------------
    // Closing the marked canvas makes `reconcile` clear the sticky hint, so the raw hint now names
    // nothing while the verbs target the lowest-id pane. A marker read off the raw hint would
    // vanish here; one read off the shared rule follows the verb.
    IM_CHECK(dockspace.close("canvas#2"));
    ctx->Yield(3);
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#2") == 0; }));
    IM_CHECK(!layout_contains(dockspace, "canvas#2"));
    IM_CHECK(canvas.focused_view_id().empty());
    IM_CHECK(canvas.indicated_view_id() == "canvas#1");
    settle(ctx, canvas);
    left_edge_mid("canvas#1/##canvas_nav", x1, y1);
    IM_CHECK(capture());
    IM_CHECK(accent_near(*e2e->frame, x1, y1));
  };
  ImGuiTestEngine_QueueTest(engine, test);

  // The read-back runs on the main thread with the context current, before present — and only when
  // the coroutine asked for one, so no frame pays for a full-screen glReadPixels it will not read.
  auto grab_frame = [&]() {
    if (!e2e.capture_request.load()) {
      return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const int w = static_cast<int>(io.DisplaySize.x);
    const int h = static_cast<int>(io.DisplaySize.y);
    if (w <= 0 || h <= 0) {
      return;
    }
    std::vector<unsigned int> px(static_cast<std::size_t>(w) * h);
    if (!capture_pixels(0, 0, 0, w, h, px.data(), nullptr)) {
      return;
    }
    const auto* bytes = reinterpret_cast<const unsigned char*>(px.data());
    frame.pixels.assign(bytes, bytes + px.size() * 4);
    frame.width = w;
    frame.height = h;
    e2e.capture_request.store(false);
    e2e.capture_done.store(true);
  };

  const int k_max_frames = 200000;
  int frames = 0;
  while (!ImGuiTestEngine_IsTestQueueEmpty(engine) && frames < k_max_frames) {
    shell.new_frame();
    shell.draw_ui();
    shell.render(grab_frame);
    ImGuiTestEngine_PostSwap(engine);
    ++frames;
  }

  int count_tested = 0;
  int count_success = 0;
  ImGuiTestEngine_GetResult(engine, count_tested, count_success);
  ImGuiTestEngine_Stop(engine);
  ace::views::register_view_body(ViewType::Canvas, {});

  CHECK(frames < k_max_frames);
  CHECK(count_tested == 1);
  CHECK(count_success == 1);

  canvas.destroy();
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
