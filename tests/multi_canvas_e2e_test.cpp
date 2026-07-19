// editor.canvas.multi_canvas — the N-canvas UI e2e (docs/01-architecture.md §9 :189,
// the escalated UI↔shared-pool ASan/TSan lane). Reuse the offscreen SDL + software-GL
// rig: boot the shell over a REAL commands::AppState, seed a solid fill, register the
// host-backed Canvas body (drawn by per-pane view_id), and drive TWO canvases through
// ONE render::CanvasHost + ONE render thread. Opens a second canvas through the tool-rail
// Views launcher (mints canvas#2), asserts two distinct docked canvas DockNodes coexist —
// both producing live frames off-thread and both showing the render (distinct from the
// shell clear colour, glReadPixels) — that one edit through the gateway fans the poke out
// so BOTH panes' sequences advance, and that closing canvas#2 removes its host entry,
// leaves canvas#1 live, and does not crash. NOT a byte-exact golden (software-GL pixels
// are flaky — byte-exactness lives in tests/canvas_host_test.cpp's CPU golden).
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/project/project.hpp>
#include <ace/render/render.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h> // ImGuiWindow / ImGuiDockNode / ImRect
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <GLES3/gl3.h>

using ace::app::CanvasView;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::commands::AppState;
using ace::dockmodel::ViewType;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  explicit ScratchDir(const char* tag)
      : root(std::filesystem::temp_directory_path() /
             (std::string("ace_multi_canvas_e2e_") + tag)) {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

bool capture_pixels(ImGuiID /*viewport_id*/, int x, int y, int w, int h, unsigned int* pixels,
                    void* /*user_data*/) {
  glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  return glGetError() == GL_NO_ERROR;
}

arbc::ObjectId seed_solid_fill(AppState& state) {
  const arbc::ObjectId comp =
      state.document().add_composition(static_cast<double>(ace::project::k_probe_width),
                                       static_cast<double>(ace::project::k_probe_height));
  const arbc::ObjectId content = state.document().add_content(
      std::make_shared<arbc::SolidContent>(ace::project::k_probe_color));
  const arbc::ObjectId layer = state.document().add_layer(content, arbc::Affine::identity());
  state.document().attach_layer(comp, layer);
  return comp;
}

bool layout_contains(const ace::dock::Dockspace& d, const char* id) {
  const std::vector<std::string> ids = d.layout().view_ids();
  return std::find(ids.begin(), ids.end(), std::string(id)) != ids.end();
}

// The canvases render OFF the UI thread; pump (yielding CPU to the render thread) until
// `ready()` or a wall-clock deadline — holds under a sanitizer build's slowdown.
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

// Threaded through UserData (TestFunc is a plain function pointer). The coroutine only
// reads sequences / requests the edit through a flag; the MAIN loop performs the writer
// op (gateway.undo) — the Document is single-writer-thread-confined, that thread being the
// main one that owns it, exactly as the shell's UI thread is in production.
struct E2EState {
  CanvasView* canvas;
  ace::dock::Dockspace* dockspace;
  std::atomic<bool> request_undo{false};
  std::atomic<bool> undo_moved{false};
  // Pane centres (display coords, top-left origin) captured by the coroutine once both
  // canvases are docked, sampled from the last read-back frame after the queue drains.
  std::atomic<int> cx1{-1};
  std::atomic<int> cy1{-1};
  std::atomic<int> cx2{-1};
  std::atomic<int> cy2{-1};
  // Raised once BOTH canvases are docked + lit, so the main loop snapshots that frame
  // (before canvas#2 is closed) for the per-pane pixel check.
  std::atomic<bool> snapshot_now{false};
};

} // namespace

TEST_CASE("multi_canvas e2e: two canvases over one host render, dock, fan-out, and close") {
  ScratchDir scratch("two");
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "multi");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  const arbc::ObjectId comp = seed_solid_fill(state);

  // A journal tip to navigate: a covering layer (one dispatched transaction) — undo
  // reverts it, a visible edit whose fan-out re-render both panes must show.
  ace::commands::dispatch(
      state, ace::commands::Command{"cover", [comp](arbc::Document& doc) {
                                      const arbc::ObjectId content =
                                          doc.add_content(std::make_shared<arbc::SolidContent>(
                                              arbc::Rgba{0.9F, 0.1F, 0.1F, 1.0F}));
                                      const arbc::ObjectId layer =
                                          doc.add_layer(content, arbc::Affine::identity());
                                      doc.attach_layer(comp, layer);
                                    }});

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  // ONE host + ONE render thread for ALL canvases (D-multi_canvas-2). The body draws by
  // per-pane view_id, so every canvas#N renders independently.
  CanvasView canvas(state);
  ace::views::register_view_body(ViewType::Canvas, [&canvas](std::string_view view_id) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content(view_id, static_cast<int>(avail.x), static_cast<int>(avail.y));
  });

  ace::dock::Dockspace dockspace; // default layout → canvas#1 open + docked

  // The in-process gateway wired to the shell's edit-serializing runner (D-edit_render_sync-2):
  // a moved edit runs its Document mutation inside CanvasHost::apply_edit's `doc_mu` window and
  // then fans the wake out to ALL canvases (one writer, N observers). The production wiring.
  ace::dockmodel::RecentProjects recent(scratch.root / "prefs", fs);
  NoopFolderDialog dialog;
  NoopLauncher launcher;
  ace::app::AppProjectGateway gateway(recent, fs, dialog, launcher, "/usr/bin/arbitraryeditor",
                                      state);
  gateway.set_edit_runner(
      [&canvas](const std::function<void()>& edit) { canvas.apply_edit(edit); });

  // After the dock draws every body, reconcile the canvas subsystem against the layout —
  // a closed canvas#N leaves view_ids(), freeing its host entry + texture (D-multi_canvas-5).
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

  E2EState e2e{&canvas, &dockspace};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "multi_canvas", "two_render_fanout_close");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    CanvasView& canvas = *e2e->canvas;
    ace::dock::Dockspace& dockspace = *e2e->dockspace;
    const std::string rail = ace::dock::tool_rail_title();
    const auto rail_ref = [&rail](const char* label) { return rail + "/" + label; };

    // canvas#1 (default-open) renders off-thread and is docked.
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));
    IM_CHECK(ctx->WindowInfo("canvas#1").ID != 0);
    ImGuiWindow* w1 = ctx->GetWindowByRef("canvas#1");
    IM_CHECK(w1 != nullptr);
    IM_CHECK(w1->DockNode != nullptr);

    // Open a SECOND canvas through the tool-rail Views launcher (mints canvas#2).
    IM_CHECK(!layout_contains(dockspace, "canvas#2"));
    ctx->ItemClick(rail_ref("Canvas").c_str());
    ctx->Yield(3);
    IM_CHECK(layout_contains(dockspace, "canvas#1"));
    IM_CHECK(layout_contains(dockspace, "canvas#2"));
    IM_CHECK(ctx->WindowInfo("canvas#2").ID != 0);
    ImGuiWindow* w2 = ctx->GetWindowByRef("canvas#2");
    IM_CHECK(w2 != nullptr);
    IM_CHECK(w2->DockNode != nullptr);
    // Two DISTINCT canvas DockNodes coexist.
    IM_CHECK(w1->DockNode != w2->DockNode);

    // canvas#2 renders its own independent frame through the shared host.
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#2") >= 1; }));
    IM_CHECK(canvas.frames_issued("canvas#1") >= 1);
    IM_CHECK(canvas.frames_issued("canvas#2") >= 1);

    // canvas#2 opens as a tab in the panel dock node and its body only runs while it is the
    // active tab; bring it to the front so both panes are visible simultaneously for the
    // pixel check (canvas#1 is its own leaf node). Its published frame is already NON-BLANK
    // (editor.canvas.blank_first_frame gates the sequence on content, so frames_issued >= 1
    // means content is on screen), so the snapshot needs no strictly-fresh frame here — a
    // focus may or may not resize the pane (the dock layout does not guarantee it), and a
    // hard "frames_issued advanced after focus" wait would race on that. Focus, confirm
    // canvas#2 still holds a content frame, and yield generously to let any focus-resize
    // render + upload + draw before snapshotting.
    ctx->WindowFocus("canvas#2");
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#2") >= 1; }));
    ctx->Yield(16);

    // Record each pane's centre for the post-loop pixel check (positions are stable).
    const ImVec2 c1 = ctx->GetWindowByRef("canvas#1")->Rect().GetCenter();
    const ImVec2 c2 = ctx->GetWindowByRef("canvas#2")->Rect().GetCenter();
    e2e->cx1.store(static_cast<int>(c1.x));
    e2e->cy1.store(static_cast<int>(c1.y));
    e2e->cx2.store(static_cast<int>(c2.x));
    e2e->cy2.store(static_cast<int>(c2.y));
    // Snapshot a frame while BOTH panes are still open (canvas#2 is closed below).
    e2e->snapshot_now.store(true);
    ctx->Yield(3);

    // Let BOTH scenes settle (the still-scene early-out) so a later advance is
    // attributable to the edit, not a still-settling frame racing the assertion.
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
    const std::uint64_t before1 = canvas.frames_issued("canvas#1");
    const std::uint64_t before2 = canvas.frames_issued("canvas#2");

    // One edit through the gateway (an undo on the MAIN writer thread) pokes the host,
    // which fans the wake out to EVERY entry: BOTH panes re-render off-thread.
    e2e->request_undo.store(true);
    IM_CHECK(pump_until(ctx, [&] { return e2e->undo_moved.load(); }));
    IM_CHECK(pump_until(ctx, [&] {
      return canvas.frames_issued("canvas#1") > before1 &&
             canvas.frames_issued("canvas#2") > before2;
    }));

    // Close canvas#2 (through the dockspace, the same path the tab ✕ routes through): the
    // reconcile drops its host entry (no longer served → sequence 0) while canvas#1 stays
    // live. No crash.
    IM_CHECK(dockspace.close("canvas#2"));
    ctx->Yield(3);
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#2") == 0; }));
    IM_CHECK(!layout_contains(dockspace, "canvas#2"));
    IM_CHECK(layout_contains(dockspace, "canvas#1"));
    IM_CHECK(canvas.frames_issued("canvas#1") >= 1);
    ctx->Yield(4);
  };
  ImGuiTestEngine_QueueTest(engine, test);

  std::vector<unsigned char> last_frame;
  std::vector<unsigned char> both_frame; // the snapshot with both canvases open + lit
  int captured_w = 0;
  int captured_h = 0;
  auto grab_frame = [&]() {
    const ImGuiIO& io = ImGui::GetIO();
    captured_w = static_cast<int>(io.DisplaySize.x);
    captured_h = static_cast<int>(io.DisplaySize.y);
    if (captured_w > 0 && captured_h > 0) {
      std::vector<unsigned int> px(static_cast<std::size_t>(captured_w) * captured_h);
      if (capture_pixels(0, 0, 0, captured_w, captured_h, px.data(), nullptr)) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(px.data());
        last_frame.assign(bytes, bytes + px.size() * 4);
        // Keep the first frame captured while both panes are open for the pixel check.
        if (e2e.snapshot_now.load() && both_frame.empty()) {
          both_frame = last_frame;
        }
      }
    }
  };

  const int k_max_frames = 200000;
  int frames = 0;
  while (!ImGuiTestEngine_IsTestQueueEmpty(engine) && frames < k_max_frames) {
    shell.new_frame();
    shell.draw_ui();
    shell.render(grab_frame);
    ImGuiTestEngine_PostSwap(engine);
    // The writer op runs on THIS (main) thread — the Document's writer. Fires once when
    // the coroutine requests it.
    if (e2e.request_undo.exchange(false) && gateway.can_undo()) {
      e2e.undo_moved.store(gateway.undo());
    }
    ++frames;
  }

  int count_tested = 0;
  int count_success = 0;
  ImGuiTestEngine_GetResult(engine, count_tested, count_success);
  ImGuiTestEngine_Stop(engine);

  ace::views::register_view_body(ViewType::Canvas, {});

  CHECK(frames < k_max_frames); // the queue drained (no hang / timeout)
  CHECK(count_tested == 1);
  CHECK(count_success == 1);

  // Both panes composited the live render to the screen — each pane's centre pixel is
  // distinct from the shell clear colour (0.10,0.10,0.12 → ~26,26,31). glReadPixels is
  // bottom-up, so flip the y captured in ImGui's top-left space.
  CHECK(!both_frame.empty()); // a both-open frame was snapshotted
  auto pane_is_lit = [&](int cx, int cy) -> bool {
    if (cx < 0 || cy < 0 || cx >= captured_w || cy >= captured_h) {
      return false;
    }
    const int fb_row = captured_h - 1 - cy;
    const std::size_t idx = (static_cast<std::size_t>(fb_row) * captured_w + cx) * 4;
    if (idx + 3 >= both_frame.size()) {
      return false;
    }
    const int r = both_frame[idx];
    const int g = both_frame[idx + 1];
    const int b = both_frame[idx + 2];
    const int k_tol = 16;
    return std::abs(r - 26) > k_tol || std::abs(g - 26) > k_tol || std::abs(b - 31) > k_tol;
  };
  CHECK(pane_is_lit(e2e.cx1.load(), e2e.cy1.load()));
  CHECK(pane_is_lit(e2e.cx2.load(), e2e.cy2.load()));

  // Deterministic teardown: stop+join the one render thread + release all textures while
  // the GL context is valid (canvas.destroy()), then shut the shell + engine down.
  canvas.destroy();
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
