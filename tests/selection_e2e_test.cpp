// editor.cells.selection — the project-level selection UI e2e (docs §9, the offscreen
// software-GL lane; modeled on tests/camera_manip_e2e_test.cpp). Boots the shell over a REAL
// commands::AppState, seeds two overlapping bounded cells plus one shot camera framing them,
// and drives the Canvas body by RAW MOUSE POSITION over "canvas#1/##canvas_nav" — asserting
// against the ONE project-level `state.selection()` (D19/A5/A7), which no surface copies.
//
// Covers, in order: click a cell body; click inside the camera's frame but over a cell (the D7
// click-through at the UI layer); Shift-click add/remove; Cmd/Ctrl-click select-behind cycling
// with wrap; marquee from empty canvas (plain replaces, Shift adds, a bare click clears);
// Cmd/Ctrl-A and Escape; a camera BORDER press that both selects the camera AND still engages
// the shipped frame grab (Constraint 11 — manip did not regress); and a Space-held press that
// pans the VIEW while leaving the selection untouched. Across the whole sequence
// `journal().depth()` and `pin()->revision()` move ONLY for the one deliberate frame-grab
// commit (Constraint 2). A screenshot baseline is captured after the two-cell marquee, where
// the outline chrome adds real signal.
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/commands/selection.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/interact/pick.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/camera.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>

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
      : root(std::filesystem::temp_directory_path() / (std::string("ace_selection_e2e_") + tag)) {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// The seeded geometry, in COMPOSITION units — which map 1:1 to pane-relative device pixels
// because canvas#1's transient viewport camera starts at identity. Two OPAQUE BOUNDED solids
// (so the frame publishes and every extent is real) overlapping in [90,120]^2, and one camera
// framing both with its interior deliberately covering them.
//
// Deliberately NO unbounded background layer: an unbounded cell is hit by a click ANYWHERE
// (Constraint 6), which would make "click empty canvas" untestable at the UI layer.
constexpr double k_cell_edge = 80.0;
constexpr double k_a_at = 40.0; // cell A covers [40,120]^2
constexpr double k_b_at = 90.0; // cell B covers [90,170]^2
constexpr double k_hero_at = 20.0;
constexpr int k_hero_res = 160; // Hero covers [20,180]^2

// Pane-relative probe points (see the geometry above).
constexpr float k_p_a_only = 60.0F;   // (60,60): cell A only, camera interior
constexpr float k_p_b_only = 150.0F;  // (150,150): cell B only, camera interior
constexpr float k_p_overlap = 105.0F; // (105,105): both cells
constexpr float k_p_empty_x = 5.0F;   // (5,60): outside every target and the camera border
constexpr float k_p_empty_y = 60.0F;  // …and below the camera-picker overlay row
constexpr float k_p_corner = 180.0F;  // (180,180): Hero's bottom-right frame corner

struct Seeded {
  arbc::ObjectId cell_a;
  arbc::ObjectId cell_b;
  arbc::ObjectId hero;
  arbc::ObjectId hero_layer;
};

Seeded seed(AppState& state) {
  arbc::Document& doc = state.document();
  const arbc::ObjectId comp = doc.add_composition(256.0, 256.0);
  const arbc::Rect extent{0.0, 0.0, k_cell_edge, k_cell_edge};
  Seeded out;
  out.cell_a = doc.add_content(
      std::make_shared<arbc::SolidContent>(arbc::Rgba{0.6F, 0.0F, 0.0F, 1.0F}, extent));
  doc.attach_layer(comp, doc.add_layer(out.cell_a, arbc::Affine::translation(k_a_at, k_a_at)));
  out.cell_b = doc.add_content(
      std::make_shared<arbc::SolidContent>(arbc::Rgba{0.0F, 0.0F, 0.6F, 1.0F}, extent));
  doc.attach_layer(comp, doc.add_layer(out.cell_b, arbc::Affine::translation(k_b_at, k_b_at)));

  out.hero = ace::scene::add_camera(state.document(), state.registry(), "Hero",
                                    ace::scene::Resolution{k_hero_res, k_hero_res},
                                    arbc::Affine::translation(k_hero_at, k_hero_at));
  out.hero_layer = ace::scene::cameras(state.document()).front().layer;
  return out;
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

// The reusable screenshot capture (the same rig as tests/shell_e2e_test.cpp): reads back the
// frame on the main thread while the GL context is current.
bool capture_pixels(ImGuiID /*viewport_id*/, int x, int y, int w, int h, unsigned int* pixels,
                    void* /*user_data*/) {
  glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  return glGetError() == GL_NO_ERROR;
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
  AppState* state;
  Seeded ids;
  std::atomic<bool> request_undo{false};
  std::atomic<bool> undo_done{false};
  std::atomic<bool> want_shot{false};
  std::atomic<bool> shot_done{false};
};

arbc::Affine hero_frame(AppState& state, arbc::ObjectId id) {
  for (const ace::scene::Camera& c : ace::scene::cameras(state.document())) {
    if (c.id == id) {
      return c.frame;
    }
  }
  return arbc::Affine::identity();
}

} // namespace

TEST_CASE("selection e2e: click, click-through, shift, select-behind, marquee, chords, and the "
          "Space/frame-grab interplay drive ONE project-level selection") {
  ScratchDir scratch("main");
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "sel");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  const Seeded ids = seed(state);
  REQUIRE(ids.cell_a.valid());
  REQUIRE(ids.cell_b.valid());
  REQUIRE(ids.hero.valid());
  REQUIRE(state.selection().empty());

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

  E2EState e2e{&canvas, &state};
  e2e.ids = ids;
  ImGuiTest* test = IM_REGISTER_TEST(engine, "selection", "pick_marquee_chords");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    CanvasView& canvas = *e2e->canvas;
    AppState& state = *e2e->state;
    ace::commands::Selection& sel = state.selection();
    const arbc::Journal& journal = state.document().journal();
    const arbc::ObjectId cell_a = e2e->ids.cell_a;
    const arbc::ObjectId cell_b = e2e->ids.cell_b;
    const arbc::ObjectId hero = e2e->ids.hero;

    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));
    ctx->WindowFocus("canvas#1");
    ctx->Yield(3);

    // The image (== the "##canvas_nav" button) rect: its Min is canvas#1's pane origin, and
    // canvas#1's viewport is identity, so a composition point maps to pane_origin + comp.
    const auto pane_origin = [&]() {
      const ImGuiTestItemInfo info = ctx->ItemInfo("canvas#1/##canvas_nav");
      return ImVec2(info.RectFull.Min.x, info.RectFull.Min.y);
    };
    const ImVec2 origin = pane_origin();
    IM_CHECK(origin.x >= 0.0F && origin.y >= 0.0F);
    const auto at = [&origin](float cx, float cy) { return ImVec2(origin.x + cx, origin.y + cy); };
    const auto click_at = [&](ImVec2 pos) {
      ctx->MouseMoveToPos(pos);
      ctx->MouseDown(0);
      ctx->MouseUp(0);
      ctx->Yield(2);
    };
    const auto drag = [&](ImVec2 from, ImVec2 to) {
      ctx->MouseMoveToPos(from);
      ctx->MouseDown(0);
      ctx->MouseMoveToPos(to);
      ctx->MouseUp(0);
      ctx->Yield(2);
    };

    ctx->MouseMove("canvas#1/##canvas_nav"); // establish the viewport for raw-position moves
    const std::size_t depth_start = journal.depth();
    const std::uint64_t revision_start = state.document().pin()->revision();

    // --- (i) Click a cell BODY. ------------------------------------------------------------
    click_at(at(k_p_a_only, k_p_a_only));
    IM_CHECK(sel.primary() == cell_a);
    IM_CHECK(sel.size() == 1);

    // --- (ii) Click INSIDE the camera's frame but over a cell: the CELL wins (D7's
    // click-through interior, asserted at the UI layer, not just in the unit lane). ----------
    click_at(at(k_p_b_only, k_p_b_only));
    IM_CHECK(sel.primary() == cell_b);
    IM_CHECK(sel.size() == 1);
    IM_CHECK(!sel.contains(hero));

    // --- (iii) Shift-click ADDS, and Shift-clicking the same target removes it. -------------
    ctx->KeyDown(ImGuiMod_Shift);
    click_at(at(k_p_a_only, k_p_a_only));
    IM_CHECK(sel.size() == 2);
    IM_CHECK(sel.contains(cell_a));
    IM_CHECK(sel.contains(cell_b));
    click_at(at(k_p_a_only, k_p_a_only));
    IM_CHECK(sel.size() == 1);
    IM_CHECK(sel.contains(cell_b));
    ctx->KeyUp(ImGuiMod_Shift);
    ctx->Yield(2);

    // --- (iv) Cmd/Ctrl-click cycles down the stack where the two cells overlap, and WRAPS. --
    click_at(at(k_p_overlap, k_p_overlap));
    IM_CHECK(sel.primary() == cell_b); // topmost first
    ctx->KeyDown(ImGuiMod_Ctrl);
    click_at(at(k_p_overlap, k_p_overlap));
    IM_CHECK(sel.primary() == cell_a); // one behind
    IM_CHECK(sel.size() == 1);
    click_at(at(k_p_overlap, k_p_overlap));
    IM_CHECK(sel.primary() == cell_b); // wrapped back to the front
    ctx->KeyUp(ImGuiMod_Ctrl);
    ctx->Yield(2);

    // --- (v) A bare click on EMPTY canvas clears. ------------------------------------------
    click_at(at(k_p_empty_x, k_p_empty_y));
    IM_CHECK(sel.empty());

    // --- (vi) Marquee from empty canvas: Shift ADDS (and a Shift-miss never wipes), plain
    // REPLACES. Hero is bounded, so a marquee that spans the cells also catches its frame —
    // hence containment assertions rather than a size count. -------------------------------
    click_at(at(k_p_a_only, k_p_a_only));
    IM_CHECK(sel.primary() == cell_a);
    ctx->KeyDown(ImGuiMod_Shift);
    drag(at(k_p_empty_x, k_p_empty_y), at(80.0F, 80.0F));
    IM_CHECK(sel.contains(cell_a)); // the Shift-press on a MISS did not wipe it
    IM_CHECK(sel.contains(hero));
    IM_CHECK(!sel.contains(cell_b)); // …and the marquee did not reach B
    ctx->KeyUp(ImGuiMod_Shift);
    ctx->Yield(2);

    drag(at(k_p_empty_x, k_p_empty_y), at(160.0F, 160.0F));
    IM_CHECK(sel.contains(cell_a));
    IM_CHECK(sel.contains(cell_b));

    // The screenshot baseline, captured HERE — with both cells outlined, where the selection
    // chrome carries real signal.
    e2e->want_shot.store(true);
    IM_CHECK(pump_until(ctx, [&] { return e2e->shot_done.load(); }));

    // --- (vii) Cmd/Ctrl-A selects every target; Escape clears. -----------------------------
    ctx->MouseMoveToPos(at(k_p_empty_x, k_p_empty_y)); // the chords are scoped to the HOVERED pane
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
    ctx->Yield(3);
    IM_CHECK(sel.size() == 3); // two cells + the camera: "all" means all (D7/D20)
    IM_CHECK(sel.contains(hero));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    IM_CHECK(sel.empty());

    // Nothing so far was a transaction (Constraint 2 / D15).
    IM_CHECK(journal.depth() == depth_start);
    IM_CHECK(state.document().pin()->revision() == revision_start);

    // --- (viii) A press on the camera BORDER selects the camera AND still engages the shipped
    // frame grab — one gesture, D7's one select tool (Constraint 11: manip did not regress). --
    const arbc::Affine frame_before = hero_frame(state, hero);
    const ImVec2 corner = at(k_p_corner, k_p_corner);
    ctx->MouseMoveToPos(corner);
    ctx->MouseDown(0);
    ctx->Yield(2);
    IM_CHECK(sel.primary() == hero); // selected on the PRESS, before the drag commits
    IM_CHECK(sel.size() == 1);
    ctx->MouseMoveToPos(ImVec2(corner.x + 35.0F, corner.y + 35.0F));
    ctx->MouseUp(0);
    ctx->Yield(3);
    IM_CHECK(journal.depth() == depth_start + 1);         // ONE set_layer_transform for the gesture
    IM_CHECK(!(hero_frame(state, hero) == frame_before)); // …and the frame really moved
    IM_CHECK(sel.contains(hero));                         // the selection survived the drag

    e2e->request_undo.store(true); // restore the pristine frame
    IM_CHECK(pump_until(ctx, [&] { return e2e->undo_done.load(); }));
    e2e->undo_done.store(false);
    ctx->Yield(3);
    IM_CHECK(hero_frame(state, hero) == frame_before);
    IM_CHECK(sel.contains(hero)); // an undo is not a deselect: the object survived, so does the id

    // --- (ix) Space held during a press over a cell pans the VIEW and leaves the selection
    // untouched (Constraint 11). Runs LAST: the pan is a lasting session change. -------------
    click_at(at(k_p_a_only, k_p_a_only));
    IM_CHECK(sel.primary() == cell_a);
    const arbc::Affine view_before = canvas.primary_framing().camera;
    const std::size_t depth_space = journal.depth();
    ctx->KeyDown(ImGuiKey_Space);
    drag(at(k_p_b_only, k_p_b_only), at(k_p_b_only + 30.0F, k_p_b_only + 30.0F));
    ctx->KeyUp(ImGuiKey_Space);
    ctx->Yield(3);
    IM_CHECK(sel.primary() == cell_a); // Space => nav pan, inert on the selection
    IM_CHECK(sel.size() == 1);
    IM_CHECK(!(canvas.primary_framing().camera == view_before)); // …and the VIEW panned
    IM_CHECK(journal.depth() == depth_space);                    // nothing journaled
  };
  ImGuiTestEngine_QueueTest(engine, test);

  // The screenshot rig: capture on request from the main thread, where the GL context is live.
  std::vector<unsigned char> last_frame;
  int captured_w = 0;
  int captured_h = 0;
  auto grab_frame = [&]() {
    if (!e2e.want_shot.load()) {
      return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    captured_w = static_cast<int>(io.DisplaySize.x);
    captured_h = static_cast<int>(io.DisplaySize.y);
    if (captured_w > 0 && captured_h > 0) {
      std::vector<unsigned int> px(static_cast<std::size_t>(captured_w) * captured_h);
      if (capture_pixels(0, 0, 0, captured_w, captured_h, px.data(), nullptr)) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(px.data());
        last_frame.assign(bytes, bytes + px.size() * 4);
      }
    }
    e2e.want_shot.store(false);
    e2e.shot_done.store(true);
  };

  const int k_max_frames = 200000;
  int frames = 0;
  while (!ImGuiTestEngine_IsTestQueueEmpty(engine) && frames < k_max_frames) {
    shell.new_frame();
    shell.draw_ui();
    shell.render(grab_frame);
    ImGuiTestEngine_PostSwap(engine);
    // Undo requests run on THIS (main) thread through the same apply_edit seam the gizmo uses.
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

  // The rendered-output layer for selection chrome is a screenshot BASELINE, not a byte-exact
  // golden (software-GL pixels are flaky, and the outline is ImGui draw-list chrome rather than
  // a libarbc composite — the byte-exactness that matters here is the INVARIANCE pinned in
  // tests/selection_test.cpp).
  CHECK(captured_w == 900);
  CHECK(captured_h == 640);
  CHECK(last_frame.size() == static_cast<std::size_t>(900) * 640 * 4);

  canvas.destroy();
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
