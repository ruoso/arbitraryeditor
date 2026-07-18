// editor.canvas.frame_sync — Canvas view UI e2e (docs/01-architecture.md §9 :189,
// the UI↔driver-handoff ASan/TSan lane). Reuse the offscreen SDL + software-GL rig:
// boot the shell over a REAL commands::AppState (ScratchDir + create_project), seed
// the fresh document with a solid fill so it has visible content, register the
// driver-backed Canvas body, and drive the default-open canvas#1 view BY STABLE ID.
// The driver now runs OFF the UI thread (editor.canvas.frame_sync), so frames arrive
// asynchronously through the latest-frame double-buffer — the test waits (bounded,
// yielding CPU to the render thread) for a published frame rather than assuming one
// is ready inline. Asserts the window exists + is docked, the driver produced a live
// frame, a captured pixel matches the seeded render colour (distinct from the shell
// clear colour), that an edit submitted THROUGH THE GATEWAY pokes the driver into a
// fresh off-thread frame (sequence advances → gl::update_rgba8 in-place), and that
// teardown stops+joins the render thread cleanly. NOT a byte-exact golden (software-GL
// pixels are flaky — the byte-exactness lives in tests/canvas_driver_test.cpp's CPU
// golden).
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/gl/gl.hpp>
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
#include <imgui_internal.h> // ImGuiWindow
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

// A temp dir wiped on entry and exit, named distinctly so this suite never collides
// with the other suites in the one ace_shell_test binary.
struct ScratchDir {
  std::filesystem::path root;
  explicit ScratchDir(const char* tag)
      : root(std::filesystem::temp_directory_path() / (std::string("ace_canvas_view_e2e_") + tag)) {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// Reusable screenshot capture wired into the engine's ScreenCaptureFunc. Runs on
// the main thread before present, so the GL context is current.
bool capture_pixels(ImGuiID /*viewport_id*/, int x, int y, int w, int h, unsigned int* pixels,
                    void* /*user_data*/) {
  glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  return glGetError() == GL_NO_ERROR;
}

// Seed a fresh document with a full-frame solid fill (the probe colour, distinct from
// the shell clear colour) so the canvas has content to show. Returns the composition id.
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

// The canvas driver renders on its own thread; the busy no-throttle test loop would
// otherwise starve it, so between UI yields we sleep briefly to hand the render thread
// CPU. Pumps until `ready()` or a wall-clock deadline — deadline-based (not a fixed
// iteration count) so it holds under a sanitizer build's large slowdown.
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

// Inert gateway collaborators — the undo edit path this e2e drives touches none of the
// folder dialog / launcher / recent store.
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

// TestFunc is a plain function pointer (std::function is disabled in this build), so
// the collaborators are threaded through UserData rather than captured.
struct E2EState {
  CanvasView* canvas;
  ace::app::AppProjectGateway* gateway;
  // The ImGui Test Engine runs TestFunc on its own coroutine thread, but the Document
  // is single-writer-thread-confined (libarbc SlotStore) — the writer is the MAIN
  // thread that owns it, exactly as the shell's UI thread is in production. So the
  // coroutine only reads (frames_issued) and requests the edit through this flag; the
  // MAIN render loop performs gateway.undo() (the writer op) when it sees the request.
  std::atomic<bool> request_undo{false};
  std::atomic<bool> undo_moved{false};
};

} // namespace

TEST_CASE(
    "canvas_view e2e: canvas#1 shows the live document off-thread, docked and drivable by id") {
  ScratchDir scratch("show");
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "canvas");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  seed_solid_fill(state);

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  // The driver-backed Canvas body over the one owned Document — the same seam the L4
  // shell uses. Constructing it spawns the render thread; it is joined by
  // canvas.destroy() at teardown. Cleared after the loop so the process-global seam
  // never outlives this CanvasView/AppState into a later test in the binary.
  CanvasView canvas(state);
  ace::views::register_view_body(ViewType::Canvas, [&canvas](std::string_view view_id) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content(view_id, static_cast<int>(avail.x), static_cast<int>(avail.y));
  });

  ace::dock::Dockspace dockspace; // default layout → canvas#1 open + docked
  shell.set_draw_content([&dockspace]() { dockspace.draw(); });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  te_io.ScreenCaptureFunc = capture_pixels;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&canvas, nullptr};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "canvas_view", "shows_live_document");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    CanvasView& canvas = *e2e->canvas;

    // The driver renders OFF the UI thread: wait (bounded) for the double-buffer to
    // publish its first frame rather than assuming a synchronous inline step.
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));
    IM_CHECK(ctx->WindowInfo("canvas#1").ID != 0);
    ImGuiWindow* w_canvas = ctx->GetWindowByRef("canvas#1");
    IM_CHECK(w_canvas != nullptr);
    IM_CHECK(w_canvas->DockNode != nullptr);

    // The off-thread driver produced at least one live frame of the shared document.
    IM_CHECK(canvas.frames_issued("canvas#1") >= 1);

    // The host renders with a REAL shared WorkerPool now (multi_canvas), so the first
    // published frame can be a partial (async tiles still resolving); let the scene SETTLE
    // (its sequence quiet over a window) so the fully-composited frame is uploaded + drawn
    // before the post-loop pixel scan.
    std::uint64_t last = canvas.frames_issued("canvas#1");
    for (int quiet = 0; quiet < 40;) {
      ctx->Yield();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      const std::uint64_t now = canvas.frames_issued("canvas#1");
      if (now == last) {
        ++quiet;
      } else {
        quiet = 0;
        last = now;
      }
    }
    ctx->Yield(4);
  };
  ImGuiTestEngine_QueueTest(engine, test);

  // Keep the last read-back frame so we can scan it for the render colour after the
  // queue drains. Read on the main thread (context current) before present.
  std::vector<unsigned char> last_frame;
  auto grab_frame = [&]() {
    const ImGuiIO& io = ImGui::GetIO();
    const int w = static_cast<int>(io.DisplaySize.x);
    const int h = static_cast<int>(io.DisplaySize.y);
    if (w > 0 && h > 0) {
      std::vector<unsigned int> px(static_cast<std::size_t>(w) * h);
      if (capture_pixels(0, 0, 0, w, h, px.data(), nullptr)) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(px.data());
        last_frame.assign(bytes, bytes + px.size() * 4);
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
    ++frames;
  }

  int count_tested = 0, count_success = 0;
  ImGuiTestEngine_GetResult(engine, count_tested, count_success);
  ImGuiTestEngine_Stop(engine);

  // Clear the body before the captured CanvasView dies — the seam is process-global.
  ace::views::register_view_body(ViewType::Canvas, {});

  CHECK(frames < k_max_frames); // the queue drained (no hang / timeout)
  CHECK(count_tested == 1);
  CHECK(count_success == 1);

  // The live render reached the screen: some captured pixel matches the seeded solid
  // sRGB8 colour (within a tolerance for software-GL variance), distinct from the
  // shell clear colour (0.10,0.10,0.12) — proof the canvas shows the render.
  const ace::render::Srgb8Image expected =
      ace::render::render_probe_srgb8(ace::project::k_probe_width, ace::project::k_probe_height);
  REQUIRE(expected.pixels.size() >= 4);
  const int er = expected.pixels[0];
  const int eg = expected.pixels[1];
  const int eb = expected.pixels[2];
  const int k_tol = 12;
  bool found = false;
  for (std::size_t i = 0; i + 3 < last_frame.size(); i += 4) {
    if (std::abs(static_cast<int>(last_frame[i]) - er) <= k_tol &&
        std::abs(static_cast<int>(last_frame[i + 1]) - eg) <= k_tol &&
        std::abs(static_cast<int>(last_frame[i + 2]) - eb) <= k_tol) {
      found = true;
      break;
    }
  }
  CHECK(found);

  // Teardown: destroy the ImGui context (inside shell.shutdown()) before the engine,
  // and stop+join the render thread + release the canvas texture while the GL context
  // is still valid (canvas.destroy()) — a clean stop→wake→join (Constraint 5).
  canvas.destroy();
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}

TEST_CASE(
    "canvas_view e2e: an edit through the gateway pokes the driver into a new off-thread frame") {
  ScratchDir scratch("poke");
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "poke");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  const arbc::ObjectId comp = seed_solid_fill(state);

  // A journal tip to navigate: a covering layer (one dispatched transaction). The
  // document now shows the covering colour; undo reverts it — a visible edit whose
  // re-render is what the poke drives.
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

  CanvasView canvas(state); // spawns the render thread

  // The in-process gateway wired to poke the canvas after a moved edit (the shell's
  // wiring, D-frame_sync-2). undo() touches none of the inert collaborators.
  ace::dockmodel::RecentProjects recent(scratch.root / "prefs", fs);
  NoopFolderDialog dialog;
  NoopLauncher launcher;
  ace::app::AppProjectGateway gateway(recent, fs, dialog, launcher, "/usr/bin/arbitraryeditor",
                                      state);
  gateway.set_edit_listener([&canvas]() { canvas.poke(); });

  ace::views::register_view_body(ViewType::Canvas, [&canvas](std::string_view view_id) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content(view_id, static_cast<int>(avail.x), static_cast<int>(avail.y));
  });

  ace::dock::Dockspace dockspace;
  shell.set_draw_content([&dockspace]() { dockspace.draw(); });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  te_io.ScreenCaptureFunc = capture_pixels;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&canvas, &gateway};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "canvas_view", "edit_poke_refreshes");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    CanvasView& canvas = *e2e->canvas;

    // Wait for the first off-thread frame (covering colour) + its GL upload.
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));

    // Let the scene SETTLE (the still-scene early-out): once no new frame lands over a
    // quiet window, the sequence is stable, so a later advance is attributable to the
    // edit and not to a still-settling frame racing the assertion.
    std::uint64_t last = canvas.frames_issued("canvas#1");
    for (int quiet = 0; quiet < 40;) {
      ctx->Yield();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      const std::uint64_t now = canvas.frames_issued("canvas#1");
      if (now == last) {
        ++quiet;
      } else {
        quiet = 0;
        last = now;
      }
    }
    const std::uint64_t before = canvas.frames_issued("canvas#1");

    // Request an edit THROUGH THE GATEWAY, performed on the MAIN thread (the writer):
    // a moved undo pokes the driver, which re-renders the damage off-thread into the
    // double-buffer; the UI then uploads it in place (same size → gl::update_rgba8).
    e2e->request_undo.store(true);
    IM_CHECK(pump_until(ctx, [&] { return e2e->undo_moved.load(); }));
    // The poke drives a fresh off-thread frame; the settled sequence now advances.
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") > before; }));
    IM_CHECK(canvas.frames_issued("canvas#1") > before);
    ctx->Yield(4);
  };
  ImGuiTestEngine_QueueTest(engine, test);

  const int k_max_frames = 200000;
  int frames = 0;
  while (!ImGuiTestEngine_IsTestQueueEmpty(engine) && frames < k_max_frames) {
    shell.new_frame();
    shell.draw_ui();
    shell.render();
    ImGuiTestEngine_PostSwap(engine);
    // The writer op runs on THIS (main) thread — the Document's writer, matching the
    // shell's UI thread. Fires once when the coroutine requests it.
    if (e2e.request_undo.exchange(false) && gateway.can_undo()) {
      e2e.undo_moved.store(gateway.undo());
    }
    ++frames;
  }

  int count_tested = 0, count_success = 0;
  ImGuiTestEngine_GetResult(engine, count_tested, count_success);
  ImGuiTestEngine_Stop(engine);

  ace::views::register_view_body(ViewType::Canvas, {});

  CHECK(frames < k_max_frames);
  CHECK(count_tested == 1);
  CHECK(count_success == 1);

  // Clean stop→wake→join of the render thread on teardown (Constraint 5).
  canvas.destroy();
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}

TEST_CASE(
    "canvas_view e2e: gl::update_rgba8 refreshes in place; a pane resize churns the texture") {
  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 320;
  opts.height = 240;
  REQUIRE(shell.init(opts));

  // gl::update_rgba8 (the tile→GL in-place primitive): a same-size refresh of a live
  // texture issues no GL error — the path a re-rendering canvas takes instead of
  // churning glGen/glDelete.
  std::vector<unsigned char> px(static_cast<std::size_t>(64) * 64 * 4, 64);
  const unsigned int tex = ace::gl::upload_rgba8(px.data(), 64, 64);
  CHECK(tex != 0);
  std::fill(px.begin(), px.end(), static_cast<unsigned char>(200));
  ace::gl::update_rgba8(tex, px.data(), 64, 64);
  CHECK(glGetError() == static_cast<GLenum>(GL_NO_ERROR));
  ace::gl::destroy_texture(tex);

  // CanvasView pane-resize (Constraint 7 — the resize/reallocate branch). The driver
  // renders off-thread, so pump draw_content until the double-buffer publishes the
  // first frame at 64x64 (sleeping to hand the render thread CPU), then a size change
  // reallocates the driver target and swaps the GL texture object. Drive inside a real
  // ImGui frame (draw_canvas_image issues an ImGui::Image, which needs a window).
  ScratchDir scratch("resize");
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "resize");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  seed_solid_fill(state);

  CanvasView canvas(state);
  bool published = false;
  for (int i = 0; i < 600 && !published; ++i) {
    shell.new_frame();
    ImGui::Begin("canvas_host");
    canvas.draw_content("canvas#1", 64, 64); // request 64x64; upload once the frame lands
    ImGui::End();
    shell.render();
    published = canvas.frames_issued("canvas#1") >= 1;
    if (!published) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  CHECK(canvas.frames_issued("canvas#1") >= 1);

  shell.new_frame();
  ImGui::Begin("canvas_host");
  canvas.draw_content("canvas#1", 48, 32); // a size change: destroy the old texture + re-upload
  ImGui::End();
  shell.render();

  canvas.destroy();
  shell.shutdown();
}
