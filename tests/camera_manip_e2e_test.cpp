// editor.cameras.manip — the camera-frame gizmo + resolution-inspector UI e2e (docs §9, the
// offscreen software-GL lane; modeled on tests/look_through_e2e_test.cpp). Boots the shell over
// a REAL commands::AppState, seeds a cells document + a shot `Hero`, and drives the DIRECT-
// manipulation gizmo in the Canvas body by raw mouse position (querying the pane rect by widget
// id) plus the resolution inspector by widget id. Asserts: (i) a corner drag re-crops Hero's
// frame through apply_edit — ONE journal entry, the frame changed, the resolution UNCHANGED
// (re-crop holds resolution, D8/D9) — and undo restores the frame; (ii) the inspector's W×H edit
// changes the resolution while the frame is byte-identical (one undo step), and an aspect preset
// makes the frame FOLLOW (D-manip-7, one undo step); (iii) Space during a frame grab pans the
// view, inert on the frame (Constraint 7), and the dutch rotate only engages under the R gate
// (D-manip-5); (iv) a second canvas looking through Hero advances its sequence when the frame is
// re-cropped (the export-preview-is-live property, consumed unchanged). Writer ops run on the
// MAIN thread through apply_edit.
#include <ace/app/camera_inspector.hpp>
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
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/worker_pool.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

using ace::app::CameraInspector;
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
             (std::string("ace_camera_manip_e2e_") + tag)) {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// A 512x512 root composition: green background under a 174x174 red raster at (40,40) — exactly
// the region Hero frames (frame == translation(40,40) at native 1:1), so the shot's covered
// region [40,214]^2 sits well inside canvas#1's identity-view pane for a deterministic gizmo
// target (and its look-through crop is a modest upscale that settles quickly).
constexpr double k_hero_at = 40.0;
constexpr int k_hero_res = 174;

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
  for (int i = 0; i < k_hero_res * k_hero_res; ++i) {
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

struct E2EState {
  CanvasView* canvas;
  ace::dock::Dockspace* dockspace;
  AppState* state;
  arbc::ObjectId hero_id;
  arbc::ObjectId hero_layer;
  std::atomic<bool> request_undo{false};
  std::atomic<bool> undo_done{false};
};

// The current frame of Hero (the scene truth the gizmo/inspector edit).
arbc::Affine hero_frame(AppState& state, arbc::ObjectId id) {
  for (const ace::scene::Camera& c : ace::scene::cameras(state.document())) {
    if (c.id == id) {
      return c.frame;
    }
  }
  return arbc::Affine::identity();
}
ace::scene::Resolution hero_res(AppState& state, arbc::ObjectId id) {
  for (const ace::scene::Camera& c : ace::scene::cameras(state.document())) {
    if (c.id == id) {
      return c.resolution;
    }
  }
  return {};
}

} // namespace

TEST_CASE("camera_manip e2e: gizmo re-crop, resolution inspector, modifier gating, live preview") {
  ScratchDir scratch("main");
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "cm");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  seed_cells(state);

  const arbc::ObjectId hero = ace::scene::add_camera(
      state.document(), state.registry(), "Hero", ace::scene::Resolution{k_hero_res, k_hero_res},
      arbc::Affine::translation(k_hero_at, k_hero_at));
  REQUIRE(hero.valid());
  const arbc::ObjectId hero_layer = ace::scene::cameras(state.document()).front().layer;

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 900;
  opts.height = 640;
  REQUIRE(shell.init(opts));

  CanvasView canvas(state);
  CameraInspector inspector(state, canvas);
  ace::views::register_view_body(ViewType::Canvas, [&canvas](std::string_view view_id) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content(view_id, static_cast<int>(avail.x), static_cast<int>(avail.y));
  });

  ace::dock::Dockspace dockspace;
  ace::dockmodel::RecentProjects recent(scratch.root / "prefs", fs);
  NoopFolderDialog dialog;
  NoopLauncher launcher;
  ace::app::AppProjectGateway gateway(recent, fs, dialog, launcher, "/usr/bin/arbitraryeditor",
                                      state);
  gateway.set_edit_runner(
      [&canvas](const std::function<void()>& edit) { canvas.apply_edit(edit); });

  // Draw the dock AND a standalone Inspector window each frame (the coroutine drives its widgets
  // by the "Inspector" window ref — the same body the shell registers on ViewType::Inspector).
  shell.set_draw_content([&dockspace, &canvas, &inspector]() {
    dockspace.draw();
    ImGui::Begin("Inspector");
    inspector.draw("inspector");
    ImGui::End();
    canvas.reconcile(dockspace.layout().view_ids());
  });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&canvas, &dockspace, &state};
  e2e.hero_id = hero;
  e2e.hero_layer = hero_layer;
  ImGuiTest* test = IM_REGISTER_TEST(engine, "camera_manip", "gizmo_inspector_gating_preview");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    CanvasView& canvas = *e2e->canvas;
    ace::dock::Dockspace& dockspace = *e2e->dockspace;
    AppState& state = *e2e->state;
    const arbc::ObjectId hero = e2e->hero_id;
    const arbc::Journal& journal = state.document().journal();
    const std::string rail = ace::dock::tool_rail_title();
    // Hero's bottom-right frame corner in composition == its origin + resolution (native 1:1).
    // canvas#1's viewport is identity, so it maps to screen as pane_origin + this.
    const float k_corner = static_cast<float>(k_hero_at + k_hero_res);

    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));
    ctx->WindowFocus("canvas#1");
    ctx->Yield(3);

    // The image (== the "##canvas_nav" button) rect: its Min is canvas#1's pane origin, so a
    // composition point maps to screen as pane_origin + comp (canvas#1's viewport is identity).
    const auto pane_origin = [&]() {
      const ImGuiTestItemInfo info = ctx->ItemInfo("canvas#1/##canvas_nav");
      return ImVec2(info.RectFull.Min.x, info.RectFull.Min.y);
    };

    // --- (i) Gizmo corner re-crop: grab Hero's bottom-right handle and drag it out. ----------
    const ImVec2 origin = pane_origin();
    IM_CHECK(origin.x >= 0.0F && origin.y >= 0.0F);
    // Hero covers composition [40, 40+res]^2; its bottom-right corner is at comp (k_corner,
    // k_corner).
    const ImVec2 br(origin.x + k_corner, origin.y + k_corner);
    const arbc::Affine frame_before = hero_frame(state, hero);
    const ace::scene::Resolution res_before = hero_res(state, hero);
    const std::size_t depth_before = journal.cursor();

    ctx->MouseMove("canvas#1/##canvas_nav"); // establish the viewport for raw-position moves
    ctx->MouseMoveToPos(br);
    ctx->MouseDown(0);
    ctx->MouseMoveToPos(ImVec2(br.x + 40.0F, br.y + 40.0F)); // drag the corner outward
    ctx->MouseUp(0);
    ctx->Yield(3);

    IM_CHECK(journal.cursor() == depth_before + 1); // ONE undo step per gesture (D-manip-4)
    const arbc::Affine frame_after = hero_frame(state, hero);
    IM_CHECK(!(frame_after == frame_before));      // the frame was re-cropped
    IM_CHECK(hero_res(state, hero) == res_before); // re-crop HOLDS resolution (D8/D9)

    // Undo restores the original frame (one clean boundary).
    e2e->request_undo.store(true);
    IM_CHECK(pump_until(ctx, [&] { return e2e->undo_done.load(); }));
    e2e->undo_done.store(false);
    ctx->Yield(2);
    IM_CHECK(hero_frame(state, hero) == frame_before);

    // --- (ii) Resolution inspector: W×H holds the frame; an aspect preset makes it follow. ---
    ctx->WindowFocus("Inspector");
    ctx->Yield(2);
    const arbc::Affine frame_res = hero_frame(state, hero); // == frame_before (restored)
    const std::size_t depth_res = journal.cursor();
    ctx->ItemInputValue("Inspector/Width", 96);
    ctx->ItemInputValue("Inspector/Height", 96);
    ctx->ItemClick("Inspector/Apply Resolution");
    ctx->Yield(3);
    IM_CHECK(journal.cursor() == depth_res + 1);                         // one undo step
    IM_CHECK((hero_res(state, hero) == ace::scene::Resolution{96, 96})); // resolution changed
    IM_CHECK(hero_frame(state, hero) == frame_res);                      // the frame is untouched

    // An aspect preset (2:1): the frame FOLLOWS (D-manip-7) as one undo step.
    const arbc::Affine frame_pre_aspect = hero_frame(state, hero);
    const std::size_t depth_aspect = journal.cursor();
    ctx->ItemClick("Inspector/2:1");
    ctx->Yield(3);
    IM_CHECK(journal.cursor() == depth_aspect + 1); // one undo step for the compound
    IM_CHECK(hero_res(state, hero).width > hero_res(state, hero).height); // now wider (2:1)
    IM_CHECK(!(hero_frame(state, hero) == frame_pre_aspect));             // the frame followed

    // Undo the aspect change, then the resolution change, back to the pristine frame.
    e2e->request_undo.store(true);
    IM_CHECK(pump_until(ctx, [&] { return e2e->undo_done.load(); }));
    e2e->undo_done.store(false);
    ctx->Yield(2);
    e2e->request_undo.store(true);
    IM_CHECK(pump_until(ctx, [&] { return e2e->undo_done.load(); }));
    e2e->undo_done.store(false);
    ctx->Yield(2);
    IM_CHECK((hero_res(state, hero) == ace::scene::Resolution{k_hero_res, k_hero_res}));
    IM_CHECK(hero_frame(state, hero) == frame_before);

    // --- (iii) Dutch rotate engages only under the R gate; committed as one undo step. --------
    // Runs before the view-panning Space test below, so canvas#1's viewport is still identity
    // and the frame corner maps to a known screen point.
    ctx->WindowFocus("canvas#1");
    ctx->Yield(2);
    const ImVec2 origin_d = pane_origin();
    const ImVec2 br_d(origin_d.x + k_corner, origin_d.y + k_corner);
    const std::size_t depth_dutch = journal.cursor();
    ctx->KeyDown(ImGuiKey_R); // the dutch-rotation gate (D-manip-5)
    ctx->MouseMoveToPos(br_d);
    ctx->MouseDown(0);
    ctx->MouseMoveToPos(ImVec2(br_d.x - 30.0F, br_d.y + 30.0F)); // sweep an angle about the center
    ctx->MouseUp(0);
    ctx->KeyUp(ImGuiKey_R);
    ctx->Yield(3);
    IM_CHECK(journal.cursor() == depth_dutch + 1);
    // A dutch is a rotation: the frame's linear part is no longer axis-aligned (b != 0), which a
    // pure re-crop (uniform scale) never produces.
    IM_CHECK(hero_frame(state, hero).b != 0.0);
    e2e->request_undo.store(true); // restore the pristine frame
    IM_CHECK(pump_until(ctx, [&] { return e2e->undo_done.load(); }));
    e2e->undo_done.store(false);
    ctx->Yield(2);
    IM_CHECK(hero_frame(state, hero).b == 0.0);

    // --- (iv) Space during a frame grab pans the VIEW, inert on the frame (Constraint 7). -----
    // Run on the full-size canvas#1 (before a second canvas reshapes the layout). The Space-drag
    // pans canvas#1's viewport (a lasting session change), so it is the last gizmo interaction on
    // canvas#1.
    const ImVec2 origin_s = pane_origin();
    const ImVec2 br_s(origin_s.x + k_corner, origin_s.y + k_corner);
    const arbc::Affine frame_space = hero_frame(state, hero);
    const std::size_t depth_space = journal.cursor();
    ctx->KeyDown(ImGuiKey_Space);
    ctx->MouseMoveToPos(br_s);
    ctx->MouseDown(0);
    ctx->MouseMoveToPos(ImVec2(br_s.x + 30.0F, br_s.y + 30.0F));
    ctx->MouseUp(0);
    ctx->KeyUp(ImGuiKey_Space);
    ctx->Yield(3);
    IM_CHECK(hero_frame(state, hero) == frame_space); // Space => nav pan, the frame did NOT move
    IM_CHECK(journal.cursor() == depth_space);        // and nothing was journaled

    // --- (v) A second canvas looks through Hero and renders (the look-through wiring the live
    // preview consumes). The deterministic "a re-crop changes the look-through output" assertion
    // runs on an inline host after teardown (below) — the same GL-free pattern look_through_test
    // uses, avoiding software-GL render-thread timing.
    ctx->ItemClick((rail + "/Canvas").c_str());
    ctx->Yield(3);
    IM_CHECK(layout_contains(dockspace, "canvas#2"));
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#2") >= 1; }));
    ctx->WindowFocus("canvas#2"); // bring canvas#2 to the front so its look-through body draws
    ctx->Yield(3);
    canvas.set_look_through("canvas#2", hero);
    ctx->Yield(2);
    IM_CHECK(*canvas.look_through("canvas#2") == hero);
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#2") >= 2; }));
    // Hero returns to its pristine frame/resolution after all the undos above (the inline
    // live-preview block below relies on it).
    IM_CHECK(hero_frame(state, hero) == frame_before);
    IM_CHECK((hero_res(state, hero) == ace::scene::Resolution{k_hero_res, k_hero_res}));
  };
  ImGuiTestEngine_QueueTest(engine, test);

  const int k_max_frames = 200000;
  int frames = 0;
  while (!ImGuiTestEngine_IsTestQueueEmpty(engine) && frames < k_max_frames) {
    shell.new_frame();
    shell.draw_ui();
    shell.render();
    ImGuiTestEngine_PostSwap(engine);
    // Undo requests run on THIS (main) thread through the same apply_edit seam as the gizmo edit.
    if (e2e.request_undo.exchange(false)) {
      canvas.apply_edit([&] { (void)ace::commands::undo(state); });
      e2e.undo_done.store(true);
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

  canvas.destroy(); // stop the shell's render thread before driving an inline host single-threaded

  // Live-preview property (D9 / editor.cameras.look_through), proved DETERMINISTICALLY on an
  // inline host driven single-threaded (no render thread) — the GL-free pattern look_through_test
  // uses. A look-through canvas renders Hero's crop; a cameras.manip re-crop of Hero's frame
  // (committed to the Document) changes what the look-through canvas renders — its sequence
  // advances and its framing changes — with NO new look-through wiring (the leaf re-reads
  // scene::cameras each frame). Hero is at its pristine frame/resolution here (all coroutine
  // edits were undone).
  {
    const ace::scene::Camera cam = ace::scene::cameras(state.document()).front();
    const ace::interact::LookThrough lt0 = ace::interact::look_through(
        cam.frame, cam.resolution.width, cam.resolution.height, 128, 128);
    REQUIRE(lt0.out_w > 0);
    ace::render::CanvasHost host(arbc::WorkerPoolConfig{}, std::chrono::hours(1));
    host.add("look", state.document(), &state.registry());
    host.request_resize("look", lt0.out_w, lt0.out_h);
    host.request_camera("look", lt0.camera);
    for (int i = 0; i < 64 && host.drive_once(); ++i) {
    }
    std::uint64_t seq = 0;
    ace::render::Srgb8Image before;
    REQUIRE(host.consume("look", seq, before));
    const std::uint64_t seq_before = seq;

    // A cameras.manip re-crop of Hero (single-threaded on the inline host — no writer race).
    const arbc::Affine recropped = ace::interact::recrop_frame(
        cam.frame, cam.resolution.width, cam.resolution.height,
        ace::interact::FrameHandle::CornerBottomRight, arbc::Vec2{320.0, 320.0});
    CHECK(!(recropped == cam.frame));
    state.document().set_layer_transform(cam.layer, recropped);
    const ace::interact::LookThrough lt1 = ace::interact::look_through(
        recropped, cam.resolution.width, cam.resolution.height, 128, 128);
    host.request_resize("look", lt1.out_w, lt1.out_h);
    host.request_camera("look", lt1.camera);
    for (int i = 0; i < 64 && host.drive_once(); ++i) {
    }
    ace::render::Srgb8Image after;
    REQUIRE(host.consume("look", seq, after));
    CHECK(seq > seq_before);              // the look-through sequence advanced on the re-crop
    CHECK(before.pixels != after.pixels); // and its framing changed (the live preview)
  }

  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
