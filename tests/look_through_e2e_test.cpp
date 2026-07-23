// editor.cameras.look_through — the multi-canvas look-through UI e2e (docs §9, the
// offscreen software-GL lane; modeled on tests/multi_canvas_e2e_test.cpp). Boots the shell
// over a REAL commands::AppState, seeds a cells document + a shot `Hero`, opens a second
// canvas, and drives canvas#2's in-body camera picker BY WIDGET ID. Asserts: (i) picking
// `Hero` sets canvas#2's presenter selection, keeps it rendering, and makes its pane pixels
// differ from canvas#1 (which stays free — per-canvas independence, A5/D19); (ii) a live frame
// edit to `Hero` through the gateway advances canvas#2's sequence (the export-preview-is-live
// property); (iii) picking `Viewport` clears the selection (the free framing is restored);
// (iv) deleting `Hero` while canvas#2 looks through it falls back to free without crashing
// (Constraint 7). NOT a byte-exact golden — software-GL pixels are flaky (the byte-exactness
// lives in tests/look_through_test.cpp's CPU golden). Writer ops run on the MAIN thread.
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/interact/interact.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/project/project.hpp>
#include <ace/render/canvas_host.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/kind_raster/raster_content.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/worker_pool.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h> // ImGuiWindow / ImRect
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

#include "writer_session.hpp"
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
             (std::string("ace_look_through_e2e_") + tag)) {
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

// Where the shot Hero frames (and where the red raster sits): a 174x174 composition region
// at (300,300), FAR from the free viewport's origin-anchored view, so canvas#1 (free) never
// sees the red raster and its centre stays green — a robust per-pane pixel difference.
constexpr double k_hero_at = 300.0;
constexpr int k_hero_res = 174;

// The document a look-through renders through: a 512x512 root composition with a full-frame
// green background under a 174x174 red raster placed at (300,300). Hero frames exactly that
// raster region at NATIVE 1:1 scale (frame == translation(300,300)), so the interactive crop
// composites in ~one pass (no heavy upscale) — fast and settling. canvas#1's free origin view
// never reaches (300,300), so it stays green while the look-through canvas shows red.
arbc::ObjectId seed_cells(AppState& state) {
  arbc::Document& doc = state.document();
  const arbc::ObjectId comp = doc.add_composition(512.0, 512.0);
  const arbc::ObjectId bg =
      doc.add_content(std::make_shared<arbc::SolidContent>(arbc::Rgba{0.0F, 0.35F, 0.0F, 1.0F}));
  doc.attach_layer(comp, doc.add_layer(bg, arbc::Affine::identity()));
  arbc::DecodedImage img;
  img.width = k_hero_res;
  img.height = k_hero_res;
  img.format = arbc::k_working_rgba32f;
  img.bytes.resize(static_cast<std::size_t>(k_hero_res) * k_hero_res * 4 * sizeof(float));
  auto* fp = reinterpret_cast<float*>(img.bytes.data());
  for (int i = 0; i < k_hero_res * k_hero_res; ++i) { // opaque red, premultiplied linear
    fp[i * 4] = 0.6F;
    fp[i * 4 + 1] = 0.0F;
    fp[i * 4 + 2] = 0.0F;
    fp[i * 4 + 3] = 1.0F;
  }
  const arbc::ObjectId raster =
      doc.add_content(std::make_shared<arbc::RasterContent>(std::move(img)));
  doc.attach_layer(comp, doc.add_layer(raster, arbc::Affine::translation(k_hero_at, k_hero_at)));
  return comp;
}

bool layout_contains(const ace::dock::Dockspace& d, const char* id) {
  const std::vector<std::string> ids = d.layout().view_ids();
  return std::find(ids.begin(), ids.end(), std::string(id)) != ids.end();
}

template <class Ready> bool pump_until(ImGuiTestContext* ctx, Ready ready) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
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

// Writer ops run on the MAIN loop thread (the Document's writer), requested by the coroutine
// through flags — exactly as the multi_canvas e2e drives its undo.
struct E2EState {
  CanvasView* canvas;
  ace::dock::Dockspace* dockspace;
  AppState* state;
  arbc::ObjectId hero_id;
  arbc::ObjectId hero_layer;
  arbc::ObjectId comp;
  std::atomic<bool> request_reframe{false};
  std::atomic<bool> reframe_done{false};
  std::atomic<bool> request_delete{false};
  std::atomic<bool> delete_done{false};
  // Pane centres + a both-open snapshot for the per-pane pixel-difference check.
  std::atomic<int> cx1{-1};
  std::atomic<int> cy1{-1};
  std::atomic<int> cx2{-1};
  std::atomic<int> cy2{-1};
  std::atomic<bool> snapshot_now{false};
};

} // namespace

TEST_CASE("look_through e2e: pick a shot, live-update, toggle back, and GC-fallback") {
  ScratchDir scratch("main");
  ace::platform::NativeFileSystem fs;
  // The writer identity, bound before the document exists and stopped after the canvas
  // is gone (editor.canvas.writer_thread; see tests/writer_session.hpp).
  ace::testing::WriterSession session(scratch.root / "lt");
  REQUIRE(session.ok());
  AppState& state = session.state();
  // Fixture seeding IS a document write: post it to the identity the open just bound
  // (editor.canvas.writer_thread D-1). Assertions stay on this thread.
  arbc::ObjectId comp;
  session.on_writer([&] { comp = seed_cells(state); });

  // Seed the shot Hero on the MAIN (writer) thread, before the render thread starts: a
  // 174x174 shot framing the red raster region at (300,300) 1:1 (frame == the binding layer's
  // device->comp placement), so looking through it renders red where the free view shows green.
  arbc::ObjectId hero;
  session.on_writer([&] {
    hero = ace::scene::add_camera(state.document(), state.registry(), "Hero",
                                  ace::scene::Resolution{k_hero_res, k_hero_res},
                                  arbc::Affine::translation(k_hero_at, k_hero_at));
  });
  REQUIRE(hero.valid());
  const std::vector<ace::scene::Camera> seeded = ace::scene::cameras(state.document());
  REQUIRE(seeded.size() == 1);
  const arbc::ObjectId hero_layer = seeded.front().layer;

  // Per-canvas independence (A5/D19), proved DETERMINISTICALLY on the real interactive render
  // path (render::CanvasHost/HostViewport) with the inline settle-fully pool — no software-GL
  // flakiness (the same rationale that keeps byte-exactness in look_through_test's CPU golden).
  // Over ONE Document, two entries: one looking through Hero (the red raster crop at (300,300)),
  // one free at identity (the green origin view). Their pixels DIFFER — a shot crop is not the
  // free frame — and each shows the expected colour at its centre. This is the honest,
  // reproducible form of "canvas#2's pane differs from canvas#1's"; the GL e2e below then proves
  // the same wiring lights the real screen (canvas#2 composites a non-blank frame).
  {
    const ace::interact::LookThrough lt =
        ace::interact::look_through(seeded.front().frame, seeded.front().resolution.width,
                                    seeded.front().resolution.height, k_hero_res, k_hero_res);
    REQUIRE(lt.out_w > 0);
    ace::render::CanvasHost host(arbc::WorkerPoolConfig{}, std::chrono::hours(1));
    // Even this deterministic single-threaded host posts its WRITER-THREAD-ONLY slots (the
    // per-document DamageRouter, each HostViewport ctor/dtor) to the document's one identity.
    host.set_writer(&session.writer());
    host.add("look", state.document(), &state.registry());
    host.request_resize("look", lt.out_w, lt.out_h);
    host.request_camera("look", lt.camera);
    host.add("free", state.document(), &state.registry());
    host.request_resize("free", lt.out_w, lt.out_h); // same size so the pixels are comparable
    for (int i = 0; i < 64 && host.drive_once(); ++i) {
    }
    std::uint64_t sl = 0;
    std::uint64_t sf = 0;
    ace::render::Srgb8Image look;
    ace::render::Srgb8Image free;
    REQUIRE(host.consume("look", sl, look));
    REQUIRE(host.consume("free", sf, free));
    const std::size_t mid = (static_cast<std::size_t>(lt.out_h / 2) * lt.out_w + lt.out_w / 2) * 4;
    // The look-through crop centres on the red raster; the free identity view centres on the
    // green background — genuinely distinct pixels over the one shared Document.
    CHECK(look.pixels != free.pixels);
    CHECK(look.pixels[mid] > 120); // red-dominant crop centre
    CHECK(look.pixels[mid + 1] < 80);
    CHECK(free.pixels[mid + 1] > 120); // green-dominant free centre
    CHECK(free.pixels[mid] < 80);
  }

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  CanvasView canvas(state, session.writer());
  ace::views::register_view_body(ViewType::Canvas, [&canvas](std::string_view view_id) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content(view_id, static_cast<int>(avail.x), static_cast<int>(avail.y));
  });

  ace::dock::Dockspace dockspace; // default layout → canvas#1 open + docked

  ace::dockmodel::RecentProjects recent(scratch.root / "prefs", fs);
  NoopFolderDialog dialog;
  NoopLauncher launcher;
  ace::app::AppProjectGateway gateway(recent, fs, dialog, launcher, "/usr/bin/arbitraryeditor",
                                      state);
  gateway.set_edit_runner(
      [&canvas](const std::function<void()>& edit) { canvas.apply_edit(edit); });

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

  E2EState e2e{&canvas, &dockspace, &state};
  e2e.hero_id = hero;
  e2e.hero_layer = hero_layer;
  e2e.comp = comp;
  ImGuiTest* test = IM_REGISTER_TEST(engine, "look_through", "pick_live_toggle_gc");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    CanvasView& canvas = *e2e->canvas;
    ace::dock::Dockspace& dockspace = *e2e->dockspace;
    const std::string rail = ace::dock::tool_rail_title();
    const auto rail_ref = [&rail](const char* label) { return rail + "/" + label; };

    // canvas#1 (default-open) renders off-thread and is docked.
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));

    // Open a SECOND canvas through the tool-rail Views launcher (mints canvas#2).
    IM_CHECK(!layout_contains(dockspace, "canvas#2"));
    ctx->ItemClick(rail_ref("Canvas").c_str());
    ctx->Yield(3);
    IM_CHECK(layout_contains(dockspace, "canvas#2"));
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#2") >= 1; }));

    // Bring canvas#2 to the front so its body (and its picker) draws.
    ctx->WindowFocus("canvas#2");
    ctx->Yield(4);

    // (i) Pick Hero in canvas#2's picker: the selection is set to Hero's ObjectId; canvas#1
    //     stays free (its selection is untouched — per-canvas independence, A5/D19).
    IM_CHECK(!canvas.look_through("canvas#2").has_value());
    const std::uint64_t before_pick = canvas.frames_issued("canvas#2");
    ctx->ItemClick("canvas#2/Hero");
    ctx->Yield(2);
    IM_CHECK(canvas.look_through("canvas#2").has_value());
    IM_CHECK(*canvas.look_through("canvas#2") == e2e->hero_id);
    IM_CHECK(!canvas.look_through("canvas#1").has_value());
    // The switch to the shot crop resizes + reframes canvas#2 → the sequence advances; wait for
    // that fresh crop frame, then let canvas#2 settle to a stable composited frame (bounded, cheap
    // at native scale) before the both-panes snapshot below reads its pixels off the GL screen.
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#2") > before_pick; }));
    {
      std::uint64_t last = canvas.frames_issued("canvas#2");
      int stable = 0;
      for (int i = 0; i < 400 && stable < 8; ++i) {
        ctx->Yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const std::uint64_t now = canvas.frames_issued("canvas#2");
        if (now == last) {
          ++stable;
        } else {
          stable = 0;
          last = now;
        }
      }
    }
    ctx->Yield(4);

    // Record each pane's centre and snapshot a both-open frame for the pixel-difference check.
    const ImVec2 c1 = ctx->GetWindowByRef("canvas#1")->Rect().GetCenter();
    const ImVec2 c2 = ctx->GetWindowByRef("canvas#2")->Rect().GetCenter();
    e2e->cx1.store(static_cast<int>(c1.x));
    e2e->cy1.store(static_cast<int>(c1.y));
    e2e->cx2.store(static_cast<int>(c2.x));
    e2e->cy2.store(static_cast<int>(c2.y));
    e2e->snapshot_now.store(true);
    ctx->Yield(3);

    // (ii) Live update: a frame edit to Hero through the gateway re-derives canvas#2's
    //      look-through viewport next frame and re-submits it (device damage), so its sequence
    //      advances — the export-preview-is-live property. Capturing the count immediately
    //      before requesting the edit keeps the advance attributable without an expensive
    //      pre-settle (the edit itself is what changes the framing).
    const std::uint64_t before_reframe = canvas.frames_issued("canvas#2");
    e2e->request_reframe.store(true);
    IM_CHECK(pump_until(ctx, [&] { return e2e->reframe_done.load(); }));
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#2") > before_reframe; }));

    // (iii) Toggle back to Viewport: the selection clears; the free framing is restored
    //       (Presenter::camera preserved). The switch is device damage, so the frame advances.
    const std::uint64_t before_free = canvas.frames_issued("canvas#2");
    ctx->ItemClick("canvas#2/Viewport");
    ctx->Yield(2);
    IM_CHECK(!canvas.look_through("canvas#2").has_value());
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#2") > before_free; }));

    // Re-select Hero for the GC test via the programmatic setter — the same seam the Overview
    // "look through" button will drive (D-look_through-5). canvas#2 must be looking through Hero
    // when it is deleted.
    const std::uint64_t before_reselect = canvas.frames_issued("canvas#2");
    canvas.set_look_through("canvas#2", e2e->hero_id);
    ctx->Yield(2);
    IM_CHECK(*canvas.look_through("canvas#2") == e2e->hero_id);
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#2") > before_reselect; }));

    // (iv) GC fallback: delete Hero (a scene edit) while canvas#2 looks through it. The
    //      selection's ObjectId is no longer in scene::cameras, so canvas#2 falls back to the
    //      free viewport — no crash, and the frame advances as it reframes to the pane.
    const std::uint64_t before_gc = canvas.frames_issued("canvas#2");
    e2e->request_delete.store(true);
    IM_CHECK(pump_until(ctx, [&] { return e2e->delete_done.load(); }));
    IM_CHECK(ace::scene::cameras(e2e->state->document()).empty());
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#2") > before_gc; }));
    IM_CHECK(canvas.frames_issued("canvas#1") >= 1); // canvas#1 stayed live throughout
  };
  ImGuiTestEngine_QueueTest(engine, test);

  std::vector<unsigned char> last_frame;
  std::vector<unsigned char> both_frame;
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
    // Writer ops on THIS (main) thread — the Document's writer — serialized through apply_edit.
    if (e2e.request_reframe.exchange(false)) {
      canvas.apply_edit([&] {
        // Reframe Hero onto a green region (still native 1:1 linear) — the framing changes, so
        // canvas#2 re-derives + re-submits and its sequence advances (the live-preview property).
        state.document().set_layer_transform(e2e.hero_layer, arbc::Affine::translation(60.0, 60.0));
      });
      e2e.reframe_done.store(true);
    }
    if (e2e.request_delete.exchange(false)) {
      canvas.apply_edit(
          [&] { state.document().remove_content(e2e.hero_id, e2e.comp, e2e.hero_layer); });
      e2e.delete_done.store(true);
    }
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

  // Both panes composited the live render to the screen — each pane's centre pixel is distinct
  // from the shell clear colour (0.10,0.10,0.12 → ~26,26,31), so the look-through canvas AND the
  // free canvas both light the real GL screen over one host (A5). The exact per-canvas pixel
  // DIFFERENCE is proved deterministically above on the inline host (software-GL raster-tile
  // resolution is too flaky for a byte compare — the multi_canvas e2e precedent). glReadPixels is
  // bottom-up, so flip the y captured in ImGui's top-left space.
  CHECK(!both_frame.empty());
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

  canvas.destroy();
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
