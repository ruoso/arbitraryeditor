// editor.canvas.isolation_scope — Canvas isolation-dim UI e2e (docs/00-design.md D17, the
// docs/01-architecture.md §9 offscreen software-GL lane). Boot the real shell over a real
// commands::AppState seeded with a GREEN parent fill + a RED nested child placed into it, draw the
// driver-backed canvas body, and prove D17's canvas half: entering the child (AppState::
// entered_composition, driven the way the layers e2e drives it) dims everything OUTSIDE the child's
// placed extent while the child stays bright, and climbing back out (nullopt = Root) restores it.
// The byte-exactness of the dim lives in the render_offline goldens
// (tests/isolation_scope_test.cpp); this lane proves the interactive path reflects the one
// project-level scope end-to-end, including across TWO canvases (D19). Pixels are sampled by colour
// (not exact coordinates) for software-GL robustness, comparing the SAME framebuffer offset
// before/after — the layer that entering re-roots is the list, NOT the canvas render
// (D-isolation_scope-2), so a pane pixel maps to the same content.
#include <ace/app/canvas_view.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "writer_session.hpp"
#include <GLES3/gl3.h>

using ace::app::CanvasView;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::commands::AppState;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  explicit ScratchDir(const char* tag)
      : root(std::filesystem::temp_directory_path() / (std::string("ace_iso_e2e_") + tag)) {
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

// Seed the fresh document: a full-frame GREEN parent fill + a RED 32x32 nested child placed at
// (16,16) into root. Returns the child composition id (the scope to enter). WRITER-THREAD only.
arbc::ObjectId seed_nested(AppState& state) {
  arbc::Document& doc = state.document();
  const arbc::ObjectId root = doc.add_composition(64.0, 64.0);
  const arbc::ObjectId green = doc.add_content(std::make_shared<arbc::SolidContent>(
      arbc::Rgba{0.0F, 0.8F, 0.0F, 1.0F}, arbc::Rect{0.0, 0.0, 64.0, 64.0}));
  doc.attach_layer(root, doc.add_layer(green, arbc::Affine::identity()));
  const arbc::ObjectId child = doc.add_composition(32.0, 32.0);
  const arbc::ObjectId red = doc.add_content(std::make_shared<arbc::SolidContent>(
      arbc::Rgba{0.85F, 0.0F, 0.0F, 1.0F}, arbc::Rect{0.0, 0.0, 32.0, 32.0}));
  doc.attach_layer(child, doc.add_layer(red, arbc::Affine::identity()));
  (void)ace::scene::add_cell(doc, state.registry(), "org.arbc.nested", std::to_string(child.value),
                             arbc::Affine::translation(16.0, 16.0));
  return child;
}

// Shared UserData: the coroutine orchestrates timing (frames-advanced waits) while the MAIN loop
// owns the entered_composition writes (the Document is main-thread-confined, like the shell's UI
// thread) and the pixel grabs — the same split the canvas_view undo e2e uses.
struct E2EState {
  CanvasView* canvas;
  bool multi;
  std::atomic<int> phase{0};           // 1=grab+enter, 2=grab-entered, 3=leave, 4=grab-left, 5=done
  std::atomic<bool> enter_done{false}; // main applied the enter + published baseline count
  std::atomic<bool> entered_grabbed{false};
  std::atomic<bool> leave_done{false};
  std::atomic<bool> left_grabbed{false};
  std::atomic<std::uint64_t> before_enter{0};
  std::atomic<std::uint64_t> before_leave{0};
};

// Frames advanced on BOTH canvases beyond `base` (canvas#2 only in the multi case).
bool advanced(const E2EState& e2e, std::uint64_t base) {
  const bool one = e2e.canvas->frames_issued("canvas#1") > base;
  if (!e2e.multi) {
    return one;
  }
  return one && e2e.canvas->frames_issued("canvas#2") > base;
}

void run_test_func(ImGuiTestContext* ctx) {
  auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
  const CanvasView& canvas = *e2e->canvas;
  IM_CHECK(pump_until(ctx, [&] {
    return canvas.frames_issued("canvas#1") >= 1 &&
           (!e2e->multi || canvas.frames_issued("canvas#2") >= 1);
  }));
  ctx->Yield(6); // let the first content frame upload + present so the baseline grab is real.

  e2e->phase.store(1); // main: grab baseline, apply enter, publish before_enter.
  IM_CHECK(pump_until(ctx, [&] { return e2e->enter_done.load(); }));
  // The scope toggle bumps the frame version, so the still scene re-publishes the dimmed frame.
  IM_CHECK(pump_until(ctx, [&] { return advanced(*e2e, e2e->before_enter.load()); }));
  ctx->Yield(6);

  e2e->phase.store(2); // main: grab the dimmed frame.
  IM_CHECK(pump_until(ctx, [&] { return e2e->entered_grabbed.load(); }));

  e2e->phase.store(3); // main: climb out to Root.
  IM_CHECK(pump_until(ctx, [&] { return e2e->leave_done.load(); }));
  IM_CHECK(pump_until(ctx, [&] { return advanced(*e2e, e2e->before_leave.load()); }));
  ctx->Yield(6);

  e2e->phase.store(4); // main: grab the un-dimmed frame.
  IM_CHECK(pump_until(ctx, [&] { return e2e->left_grabbed.load(); }));
  e2e->phase.store(5);
}

// First pixel offset in [x_lo,x_hi) columns whose (r,g,b) satisfies the dominant-channel predicate;
// -1 if none. `want_red` selects a saturated-red pixel (child, inside), else saturated-green
// (parent, outside). `frame` is tightly packed RGBA, `width` its pixel width.
long find_dominant(const std::vector<unsigned char>& frame, int width, int height, int x_lo,
                   int x_hi, bool want_red) {
  for (int y = 0; y < height; ++y) {
    for (int x = x_lo; x < x_hi; ++x) {
      const std::size_t at =
          (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4;
      if (at + 3 >= frame.size()) {
        continue;
      }
      const int r = frame[at];
      const int g = frame[at + 1];
      const int b = frame[at + 2];
      if (want_red && r > 150 && g < 90 && b < 90) {
        return static_cast<long>(at);
      }
      if (!want_red && g > 150 && r < 90 && b < 90) {
        return static_cast<long>(at);
      }
    }
  }
  return -1;
}

// Drive the shell frames until the coroutine reaches phase 5, applying the entered_composition
// writes + colour grabs the coroutine gates through `e2e`. Returns whether all grabs happened.
struct Grabs {
  std::vector<unsigned char> root, entered, left;
};

} // namespace

TEST_CASE(
    "isolation_scope e2e: entering a nested composition dims outside, leaves the child bright") {
  ScratchDir scratch("single");
  ace::testing::WriterSession session(scratch.root / "iso");
  REQUIRE(session.ok());
  AppState& state = session.state();
  arbc::ObjectId child;
  session.on_writer([&] { child = seed_nested(state); });

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  CanvasView canvas(state, session.writer());
  shell.set_draw_content([&canvas]() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("iso_canvas", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content("canvas#1", static_cast<int>(avail.x), static_cast<int>(avail.y));
    ImGui::End();
  });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  te_io.ScreenCaptureFunc = capture_pixels;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&canvas, /*multi=*/false};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "isolation_scope", "single_canvas_dim");
  test->UserData = &e2e;
  test->TestFunc = run_test_func;
  ImGuiTestEngine_QueueTest(engine, test);

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

  Grabs grabs;
  const int k_max_frames = 200000;
  int frames = 0;
  while (frames < k_max_frames &&
         (!ImGuiTestEngine_IsTestQueueEmpty(engine) || e2e.phase.load() < 5)) {
    shell.new_frame();
    shell.draw_ui();
    shell.render(grab_frame);
    ImGuiTestEngine_PostSwap(engine);
    const int ph = e2e.phase.load();
    if (ph == 1 && !e2e.enter_done.load()) {
      grabs.root = last_frame;
      state.entered_composition() = child;
      e2e.before_enter.store(canvas.frames_issued("canvas#1"));
      e2e.enter_done.store(true);
    } else if (ph == 2 && !e2e.entered_grabbed.load()) {
      grabs.entered = last_frame;
      e2e.entered_grabbed.store(true);
    } else if (ph == 3 && !e2e.leave_done.load()) {
      state.entered_composition() = std::nullopt;
      e2e.before_leave.store(canvas.frames_issued("canvas#1"));
      e2e.leave_done.store(true);
    } else if (ph == 4 && !e2e.left_grabbed.load()) {
      grabs.left = last_frame;
      e2e.left_grabbed.store(true);
    }
    ++frames;
    if (ImGuiTestEngine_IsTestQueueEmpty(engine) && e2e.phase.load() < 5) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  int count_tested = 0, count_success = 0;
  ImGuiTestEngine_GetResult(engine, count_tested, count_success);
  ImGuiTestEngine_Stop(engine);

  CHECK(frames < k_max_frames);
  CHECK(count_tested == 1);
  CHECK(count_success == 1);

  const int w = opts.width;
  const int h = opts.height;
  REQUIRE(grabs.root.size() == static_cast<std::size_t>(w) * h * 4);
  REQUIRE(grabs.entered.size() == grabs.root.size());
  REQUIRE(grabs.left.size() == grabs.root.size());

  const long out =
      find_dominant(grabs.root, w, h, 0, w, /*want_red=*/false);            // green: outside child
  const long in = find_dominant(grabs.root, w, h, 0, w, /*want_red=*/true); // red: inside child
  REQUIRE(out >= 0);
  REQUIRE(in >= 0);

  // Entering dims OUTSIDE (the green pixel's green channel drops) and leaves the child bright (the
  // red pixel is ~unchanged). Climbing out restores the outside pixel. Generous margins for
  // software-GL variance; the dim (alpha 0.6 -> ~40% brightness) drops the channel far more.
  const int k_margin = 25;
  const int k_tol = 24;
  CHECK(static_cast<int>(grabs.entered[out + 1]) + k_margin <
        static_cast<int>(grabs.root[out + 1]));
  CHECK(std::abs(static_cast<int>(grabs.entered[in]) - static_cast<int>(grabs.root[in])) <= k_tol);
  CHECK(std::abs(static_cast<int>(grabs.left[out + 1]) - static_cast<int>(grabs.root[out + 1])) <=
        k_tol);

  canvas.destroy();
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}

TEST_CASE("isolation_scope e2e: entering the scope dims BOTH open canvases (D19)") {
  ScratchDir scratch("multi");
  ace::testing::WriterSession session(scratch.root / "iso");
  REQUIRE(session.ok());
  AppState& state = session.state();
  arbc::ObjectId child;
  session.on_writer([&] { child = seed_nested(state); });

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  CanvasView canvas(state, session.writer());
  // Two canvas panes side by side over the ONE project-level scope: the left half draws canvas#1,
  // the right half canvas#2, both looking at the same document.
  shell.set_draw_content([&canvas]() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float half = vp->WorkSize.x * 0.5F;
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(half, vp->WorkSize.y));
    ImGui::Begin("iso_cv1", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content("canvas#1", static_cast<int>(avail.x), static_cast<int>(avail.y));
    ImGui::End();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + half, vp->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x - half, vp->WorkSize.y));
    ImGui::Begin("iso_cv2", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    avail = ImGui::GetContentRegionAvail();
    canvas.draw_content("canvas#2", static_cast<int>(avail.x), static_cast<int>(avail.y));
    ImGui::End();
  });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  te_io.ScreenCaptureFunc = capture_pixels;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&canvas, /*multi=*/true};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "isolation_scope", "multi_canvas_dim");
  test->UserData = &e2e;
  test->TestFunc = run_test_func;
  ImGuiTestEngine_QueueTest(engine, test);

  std::vector<unsigned char> last_frame;
  auto grab_frame = [&]() {
    const ImGuiIO& io = ImGui::GetIO();
    const int fw = static_cast<int>(io.DisplaySize.x);
    const int fh = static_cast<int>(io.DisplaySize.y);
    if (fw > 0 && fh > 0) {
      std::vector<unsigned int> px(static_cast<std::size_t>(fw) * fh);
      if (capture_pixels(0, 0, 0, fw, fh, px.data(), nullptr)) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(px.data());
        last_frame.assign(bytes, bytes + px.size() * 4);
      }
    }
  };

  Grabs grabs;
  const int k_max_frames = 200000;
  int frames = 0;
  while (frames < k_max_frames &&
         (!ImGuiTestEngine_IsTestQueueEmpty(engine) || e2e.phase.load() < 5)) {
    shell.new_frame();
    shell.draw_ui();
    shell.render(grab_frame);
    ImGuiTestEngine_PostSwap(engine);
    const int ph = e2e.phase.load();
    if (ph == 1 && !e2e.enter_done.load()) {
      grabs.root = last_frame;
      state.entered_composition() = child;
      e2e.before_enter.store(canvas.frames_issued("canvas#1"));
      e2e.enter_done.store(true);
    } else if (ph == 2 && !e2e.entered_grabbed.load()) {
      grabs.entered = last_frame;
      e2e.entered_grabbed.store(true);
    } else if (ph == 3 && !e2e.leave_done.load()) {
      state.entered_composition() = std::nullopt;
      e2e.before_leave.store(canvas.frames_issued("canvas#1"));
      e2e.leave_done.store(true);
    } else if (ph == 4 && !e2e.left_grabbed.load()) {
      grabs.left = last_frame;
      e2e.left_grabbed.store(true);
    }
    ++frames;
    if (ImGuiTestEngine_IsTestQueueEmpty(engine) && e2e.phase.load() < 5) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  int count_tested = 0, count_success = 0;
  ImGuiTestEngine_GetResult(engine, count_tested, count_success);
  ImGuiTestEngine_Stop(engine);

  CHECK(frames < k_max_frames);
  CHECK(count_tested == 1);
  CHECK(count_success == 1);

  const int w = opts.width;
  const int h = opts.height;
  REQUIRE(grabs.root.size() == static_cast<std::size_t>(w) * h * 4);
  REQUIRE(grabs.entered.size() == grabs.root.size());

  // A green pixel in the LEFT pane (canvas#1) and one in the RIGHT pane (canvas#2). Entering the
  // one project-level scope dims BOTH (D19) — each pane's green channel drops.
  const long left = find_dominant(grabs.root, w, h, 0, w / 2, /*want_red=*/false);
  const long right = find_dominant(grabs.root, w, h, w / 2, w, /*want_red=*/false);
  REQUIRE(left >= 0);
  REQUIRE(right >= 0);
  const int k_margin = 25;
  CHECK(static_cast<int>(grabs.entered[left + 1]) + k_margin <
        static_cast<int>(grabs.root[left + 1]));
  CHECK(static_cast<int>(grabs.entered[right + 1]) + k_margin <
        static_cast<int>(grabs.root[right + 1]));

  canvas.destroy();
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
