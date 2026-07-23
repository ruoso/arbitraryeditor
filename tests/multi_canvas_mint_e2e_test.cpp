// editor.cameras.mint_from_focused_canvas — the two-canvas UI e2e (docs §9, the offscreen
// software-GL ASan lane). Composes tests/new_shot_from_view_e2e_test.cpp's mint harness (a REAL
// AppProjectGateway over a REAL AppState + a live CanvasView on a ScratchDir project) with
// tests/multi_canvas_e2e_test.cpp's second-canvas recipe, and binds the framing provider the way
// src/app/shell.cpp binds it — to `focused_framing()`.
//
// A separate file rather than a TEST_CASE in multi_canvas_e2e_test.cpp: that file's settle/quiet
// loops are tuned around a pixel snapshot this test does not take. Every assertion here is on
// MODEL state, never pixels (software-GL frames are not byte-comparable in this rig; a camera is
// a NON-RENDERING Content, A14, so no mint can move a golden byte anyway).
//
// Asserts: the sticky hint tracks WindowFocus; the framing-derived verbs read the FOCUSED pane
// (mint and insert alike, through the one provider); the hint SURVIVES the rail click that steals
// focus (a non-sticky implementation falls back to canvas#1 and fails); the rule TRACKS rather
// than latches; a look-through pane promotes the shot it is ACTUALLY SHOWING at its letterboxed
// crop size, not its frozen free camera at the pane size; closing the focused canvas degrades to
// the lowest-id fallback with no stale hint; and closing every canvas still REFUSES the mint.
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/app/view_framing.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/interact/interact.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/expected.hpp>
#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

using ace::app::AppProjectGateway;
using ace::app::CanvasView;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::app::ViewFraming;
using ace::commands::AppState;
using ace::dockmodel::ViewType;

namespace {

// Hero's aspect is deliberately EXTREME (4:1) and its framed region deliberately far from
// canvas#2's free view: letterboxing a 4:1 shot into a roughly-4:3 pane can never coincide with
// the pane's own device size, which is what makes phase 8's anti-vacuity guard real.
constexpr int k_hero_w = 64;
constexpr int k_hero_h = 16;
constexpr double k_hero_at = 60.0;

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_multi_canvas_mint_e2e_test") {
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

// TestFunc is a plain function pointer (std::function is disabled in this build), so the
// collaborators are threaded through UserData rather than captured.
struct E2EState {
  ace::dock::Dockspace* dockspace;
  AppState* state;
  CanvasView* canvas;
  AppProjectGateway* gateway;
  arbc::ObjectId hero;
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

// Pump until BOTH panes stop publishing — the settled state in which each pane's texture size
// (what "##canvas_nav" measures) has caught up with its requested size (what the framing
// accessors report), so the two are comparable.
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

bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

bool affine_near(const arbc::Affine& a, const arbc::Affine& b, double tol = 1e-6) {
  return near(a.a, b.a, tol) && near(a.b, b.b, tol) && near(a.c, b.c, tol) && near(a.d, b.d, tol) &&
         near(a.tx, b.tx, tol) && near(a.ty, b.ty, tol);
}

bool layout_contains(const ace::dock::Dockspace& d, const char* id) {
  const std::vector<std::string> ids = d.layout().view_ids();
  return std::find(ids.begin(), ids.end(), std::string(id)) != ids.end();
}

// The composition point a pane's CENTRE is showing — where `interact::place_in_view` centres a
// freshly inserted cell's placed extent, and therefore the model-state signature of "which
// canvas the insert followed".
std::optional<arbc::Vec2> view_centre(const ViewFraming& framing) {
  const std::optional<arbc::Affine> inv = framing.camera.inverse();
  if (!inv || framing.pane_w <= 0 || framing.pane_h <= 0) {
    return std::nullopt;
  }
  return inv->apply(arbc::Vec2{framing.pane_w * 0.5, framing.pane_h * 0.5});
}

// Returns a COPY: every call site passes a `cameras()` temporary, so a pointer/reference into it
// would dangle at the end of the full-expression.
std::optional<ace::scene::Camera> find_camera(const std::vector<ace::scene::Camera>& cameras,
                                              std::string_view name) {
  for (const ace::scene::Camera& cam : cameras) {
    if (cam.name == name) {
      return cam;
    }
  }
  return std::nullopt;
}

// The `###insert_kindN` id of `kind_id` in the modal's snapshot list.
std::string kind_row(const ace::dock::Dockspace& dockspace, std::string_view kind_id) {
  const std::vector<ace::dock::InsertKindSpec>& kinds = dockspace.insert_kinds();
  for (std::size_t i = 0; i < kinds.size(); ++i) {
    if (kinds[i].kind_id == kind_id) {
      return "Insert Cell/###insert_kind" + std::to_string(i);
    }
  }
  return "Insert Cell/###insert_kind_missing";
}

} // namespace

TEST_CASE("mint_from_focused_canvas e2e: the framing verbs follow the focused canvas") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "focus");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  state.document().add_composition(128.0, 128.0);

  // An OPAQUE (and unbounded, D-cells_model-3) backdrop so neither pane's composite is ever
  // blank — the canvas withholds an all-transparent frame, and both panes must publish.
  REQUIRE(ace::scene::add_cell(state.document(), state.registry(), "org.arbc.solid",
                               "0.15,0.2,0.25,1", arbc::Affine::identity())
              .has_value());
  // A pre-seeded shot to look THROUGH in phase 8. Named "Hero", so it never consumes a
  // `Camera <n>` slot and the minted names stay assertable (D-frame_selection-9).
  const arbc::ObjectId hero = ace::scene::add_camera(
      state.document(), state.registry(), "Hero", ace::scene::Resolution{k_hero_w, k_hero_h},
      arbc::Affine::translation(k_hero_at, k_hero_at));
  REQUIRE(hero.valid());
  REQUIRE(ace::scene::cameras(state.document()).size() == 1);

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
  // THE line under test (src/app/shell.cpp): ONE provider, read by BOTH `insert_cell`'s
  // provisional placement and `new_shot_from_view`'s mint (D-mint_from_focused_canvas-4).
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
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&dockspace, &state, &canvas, &gateway, hero};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "multi_canvas", "focused_canvas_mint_and_insert");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    AppState& state = *e2e->state;
    CanvasView& canvas = *e2e->canvas;
    ace::dock::Dockspace& dockspace = *e2e->dockspace;
    AppProjectGateway& gateway = *e2e->gateway;
    const std::string rail = ace::dock::tool_rail_title();
    const std::string shot_item = rail + "/###new_shot_from_view";
    const auto cameras = [&state] { return ace::scene::cameras(state.document()); };
    ctx->Yield(2);

    // --- (1) Two live canvases ------------------------------------------------------
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));
    IM_CHECK(!layout_contains(dockspace, "canvas#2"));
    ctx->ItemClick((rail + "/Canvas").c_str()); // the Views launcher mints canvas#2
    ctx->Yield(3);
    IM_CHECK(layout_contains(dockspace, "canvas#1"));
    IM_CHECK(layout_contains(dockspace, "canvas#2"));
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#2") >= 1; }));

    // --- (2) The sticky hint tracks window focus ------------------------------------
    // canvas#2 opens as a tab in the panel node and its body only runs while it is the active
    // tab, so bringing it to the front is also what lets it stamp the hint at all.
    ctx->WindowFocus("canvas#2");
    IM_CHECK(pump_until(ctx, [&] { return canvas.focused_view_id() == "canvas#2"; }));
    settle(ctx, canvas);

    // --- (3) Make the two panes DISTINGUISHABLE -------------------------------------
    // Without this every later assertion is vacuous: both panes boot at the identity camera,
    // so canvas#1's framing and canvas#2's are byte-identical until one of them navigates.
    ctx->MouseMove("canvas#2/##canvas_nav");
    ctx->MouseWheelY(3.0F);
    IM_CHECK(pump_until(ctx, [&] {
      return !(canvas.focused_framing().camera == canvas.primary_framing().camera);
    }));
    settle(ctx, canvas);
    // Focus survived the wheel, and the hint still names canvas#2.
    IM_CHECK(canvas.focused_view_id() == "canvas#2");

    // The pane-rect probe: "##canvas_nav" covers the pane image exactly, so its rect IS the
    // device size a promotion of THAT pane must record. Pump until the settled framing agrees
    // with it so a lagging published frame cannot flake the comparison.
    IM_CHECK(pump_until(ctx, [&] {
      const ImGuiTestItemInfo nav = ctx->ItemInfo("canvas#2/##canvas_nav");
      const ViewFraming vf = canvas.focused_framing();
      return vf.pane_w > 0 && vf.pane_h > 0 &&
             vf.pane_w == static_cast<int>(nav.RectFull.GetWidth()) &&
             vf.pane_h == static_cast<int>(nav.RectFull.GetHeight());
    }));
    const ImGuiTestItemInfo nav2 = ctx->ItemInfo("canvas#2/##canvas_nav");
    const int pane_w2 = static_cast<int>(nav2.RectFull.GetWidth());
    const int pane_h2 = static_cast<int>(nav2.RectFull.GetHeight());
    const ViewFraming focused_at_mint = canvas.focused_framing();
    const ViewFraming lowest_at_mint = canvas.primary_framing(); // canvas#1's, the OLD source
    IM_CHECK(!affine_near(focused_at_mint.camera, lowest_at_mint.camera));

    // --- (4) The mint promotes canvas#2, not canvas#1 -------------------------------
    ctx->ItemClick(shot_item.c_str());
    IM_CHECK(pump_until(ctx, [&] { return cameras().size() == 2; })); // Hero + the mint
    const std::optional<ace::scene::Camera> first = find_camera(cameras(), "Camera 1");
    IM_CHECK(first.has_value());
    IM_CHECK(first->resolution.width == pane_w2);
    IM_CHECK(first->resolution.height == pane_h2);
    // The frame inverts back to the FOCUSED pane's camera (the WYSIWYG law)…
    IM_CHECK(affine_near(
        ace::interact::viewport_camera_for_shot(first->frame, first->resolution.width,
                                                first->resolution.height, pane_w2, pane_h2),
        focused_at_mint.camera));
    // …and NOT to canvas#1's, which is exactly what the shipped lowest-id rule would have
    // promoted. This one assertion is the whole leaf.
    IM_CHECK(!affine_near(
        ace::interact::viewport_camera_for_shot(first->frame, first->resolution.width,
                                                first->resolution.height, pane_w2, pane_h2),
        lowest_at_mint.camera));

    // --- (5) The hint SURVIVES the click that stole focus ---------------------------
    // The rail item is an ImGui::Selectable in the Tool Rail window, so at mint time NO canvas
    // is focused. A poll-at-query-time implementation sees "nobody focused" on every mint and
    // falls back to canvas#1 — i.e. fails phase 4. This pins the mechanism directly.
    IM_CHECK(canvas.focused_view_id() == "canvas#2");

    // --- (6) An insert follows the SAME focus (one provider) ------------------------
    settle(ctx, canvas);
    const ViewFraming focused_at_insert = canvas.focused_framing();
    const ViewFraming lowest_at_insert = canvas.primary_framing();
    const std::optional<arbc::Vec2> want_centre = view_centre(focused_at_insert);
    const std::optional<arbc::Vec2> other_centre = view_centre(lowest_at_insert);
    IM_CHECK(want_centre.has_value());
    IM_CHECK(other_centre.has_value());

    ctx->ItemClick((rail + "/###insert_cell").c_str());
    ctx->Yield(2);
    IM_CHECK(dockspace.insert_modal_open());
    ctx->ItemClick(kind_row(dockspace, "org.arbc.raster").c_str());
    ctx->Yield(2);
    ctx->ItemInputValue("Insert Cell/###insert_field0", "48x24");
    ctx->Yield(2);
    ctx->ItemClick("Insert Cell/###insert_confirm");
    IM_CHECK(pump_until(ctx, [&] { return !dockspace.insert_modal_open(); }));
    ctx->Yield(2);

    // `interact::place_in_view` centres the content's placed extent in the region the framing
    // describes, so the placed centre IS the composition point at that pane's centre.
    const std::vector<ace::scene::Cell> cells =
        ace::scene::cells(state.document(), state.registry());
    const ace::scene::Cell* raster = nullptr;
    for (const ace::scene::Cell& cell : cells) {
      if (cell.kind_id == "org.arbc.raster") {
        raster = &cell;
      }
    }
    IM_CHECK(raster != nullptr);
    IM_CHECK(raster->content_bounds.has_value());
    const arbc::Rect b = *raster->content_bounds;
    const arbc::Vec2 placed_centre =
        raster->placement.apply(arbc::Vec2{(b.x0 + b.x1) * 0.5, (b.y0 + b.y1) * 0.5});
    IM_CHECK(near(placed_centre.x, want_centre->x, 1e-3));
    IM_CHECK(near(placed_centre.y, want_centre->y, 1e-3));
    // Anti-vacuity: canvas#1 is showing a genuinely different region, so a provider still
    // bound to the lowest-id pane would have dropped the cell somewhere else.
    IM_CHECK(!near(other_centre->x, want_centre->x, 1e-3) ||
             !near(other_centre->y, want_centre->y, 1e-3));
    IM_CHECK(!near(placed_centre.x, other_centre->x, 1e-3) ||
             !near(placed_centre.y, other_centre->y, 1e-3));

    // --- (7) The rule TRACKS; it does not latch ------------------------------------
    ctx->WindowFocus("canvas#1");
    IM_CHECK(pump_until(ctx, [&] { return canvas.focused_view_id() == "canvas#1"; }));
    settle(ctx, canvas);
    IM_CHECK(pump_until(ctx, [&] {
      const ImGuiTestItemInfo nav = ctx->ItemInfo("canvas#1/##canvas_nav");
      const ViewFraming vf = canvas.focused_framing();
      return vf.pane_w > 0 && vf.pane_w == static_cast<int>(nav.RectFull.GetWidth()) &&
             vf.pane_h == static_cast<int>(nav.RectFull.GetHeight());
    }));
    const ImGuiTestItemInfo nav1 = ctx->ItemInfo("canvas#1/##canvas_nav");
    const int pane_w1 = static_cast<int>(nav1.RectFull.GetWidth());
    const int pane_h1 = static_cast<int>(nav1.RectFull.GetHeight());
    const ViewFraming canvas1_framing = canvas.focused_framing();
    // With canvas#1 focused the two accessors agree again — the focused pane IS the lowest-id
    // one, so the fallback and the focus branch coincide (Constraint 3's shape).
    IM_CHECK(affine_near(canvas1_framing.camera, canvas.primary_framing().camera));

    ctx->ItemClick(shot_item.c_str());
    IM_CHECK(pump_until(ctx, [&] { return cameras().size() == 3; }));
    const std::optional<ace::scene::Camera> second = find_camera(cameras(), "Camera 2");
    IM_CHECK(second.has_value());
    IM_CHECK(second->resolution.width == pane_w1);
    IM_CHECK(second->resolution.height == pane_h1);
    IM_CHECK(affine_near(
        ace::interact::viewport_camera_for_shot(second->frame, second->resolution.width,
                                                second->resolution.height, pane_w1, pane_h1),
        canvas1_framing.camera));

    // --- (8) A look-through pane promotes the shot it is ACTUALLY SHOWING -----------
    // Today's `Presenter::camera` is frozen at its last FREE value while looking through a
    // shot (D-look_through-6) while the pane's size is the shot's fitted crop
    // (D-look_through-2); pairing those two describes no view that exists
    // (D-mint_from_focused_canvas-5).
    const std::optional<ace::scene::Camera> hero = find_camera(cameras(), "Hero");
    IM_CHECK(hero.has_value());
    const arbc::Affine hero_frame = hero->frame;
    const ace::interact::LookThrough expect = ace::interact::look_through(
        hero_frame, hero->resolution.width, hero->resolution.height, pane_w2, pane_h2);
    IM_CHECK(expect.out_w > 0 && expect.out_h > 0);
    // The letterboxed crop of a 4:1 shot in this pane is NOT the pane's device size — which is
    // what makes the anti-vacuity guard below able to fail.
    IM_CHECK(!(expect.out_w == pane_w2 && expect.out_h == pane_h2));

    canvas.set_look_through("canvas#2", e2e->hero);
    ctx->WindowFocus("canvas#2");
    IM_CHECK(pump_until(ctx, [&] { return canvas.focused_view_id() == "canvas#2"; }));
    IM_CHECK(pump_until(ctx, [&] {
      const ViewFraming vf = canvas.focused_framing();
      return vf.pane_w == expect.out_w && vf.pane_h == expect.out_h;
    }));
    settle(ctx, canvas);
    const ViewFraming look_framing = canvas.focused_framing();
    IM_CHECK(affine_near(look_framing.camera, expect.camera));

    ctx->ItemClick(shot_item.c_str());
    IM_CHECK(pump_until(ctx, [&] { return cameras().size() == 4; }));
    const std::optional<ace::scene::Camera> third = find_camera(cameras(), "Camera 3");
    IM_CHECK(third.has_value());
    // Resolution = the size the shot OCCUPIES ON SCREEN (D23), i.e. the letterboxed crop…
    IM_CHECK(third->resolution.width == expect.out_w);
    IM_CHECK(third->resolution.height == expect.out_h);
    // …and the frame covers the SAME composition rectangle as Hero, at the new resolution: the
    // affines differ by the native->crop scale, the covered region does not.
    IM_CHECK(near(third->frame.apply(arbc::Vec2{0.0, 0.0}).x,
                  hero_frame.apply(arbc::Vec2{0.0, 0.0}).x, 1e-6));
    IM_CHECK(near(third->frame.apply(arbc::Vec2{0.0, 0.0}).y,
                  hero_frame.apply(arbc::Vec2{0.0, 0.0}).y, 1e-6));
    IM_CHECK(near(
        third->frame
            .apply(arbc::Vec2{static_cast<double>(expect.out_w), static_cast<double>(expect.out_h)})
            .x,
        hero_frame
            .apply(arbc::Vec2{static_cast<double>(hero->resolution.width),
                              static_cast<double>(hero->resolution.height)})
            .x,
        1.0));
    IM_CHECK(near(
        third->frame
            .apply(arbc::Vec2{static_cast<double>(expect.out_w), static_cast<double>(expect.out_h)})
            .y,
        hero_frame
            .apply(arbc::Vec2{static_cast<double>(hero->resolution.width),
                              static_cast<double>(hero->resolution.height)})
            .y,
        1.0));
    // The two ways today's incoherent pair could leak through: the pane's own device size, and
    // canvas#2's FROZEN free camera (still exactly what phase 4 promoted, since nav is inert in
    // look-through mode).
    IM_CHECK(!(third->resolution.width == pane_w2 && third->resolution.height == pane_h2));
    IM_CHECK(!affine_near(
        third->frame,
        ace::interact::new_shot_from_view(focused_at_mint.camera, pane_w2, pane_h2).frame));

    // --- (9) Closing the focused canvas falls back cleanly -------------------------
    IM_CHECK(dockspace.close("canvas#2"));
    ctx->Yield(3);
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#2") == 0; }));
    IM_CHECK(!layout_contains(dockspace, "canvas#2"));
    IM_CHECK(canvas.focused_view_id().empty()); // no stale hint (reconcile cleared it)
    const ViewFraming after_close = canvas.focused_framing();
    const ViewFraming primary_after_close = canvas.primary_framing();
    IM_CHECK(after_close.pane_w == primary_after_close.pane_w);
    IM_CHECK(after_close.pane_h == primary_after_close.pane_h);
    IM_CHECK(affine_near(after_close.camera, primary_after_close.camera));
    IM_CHECK(after_close.pane_w > 0);

    ctx->ItemClick(shot_item.c_str());
    IM_CHECK(pump_until(ctx, [&] { return cameras().size() == 5; }));
    const std::optional<ace::scene::Camera> fourth = find_camera(cameras(), "Camera 4");
    IM_CHECK(fourth.has_value());
    IM_CHECK(fourth->resolution.width == after_close.pane_w);
    IM_CHECK(fourth->resolution.height == after_close.pane_h);
    IM_CHECK(affine_near(ace::interact::viewport_camera_for_shot(
                             fourth->frame, fourth->resolution.width, fourth->resolution.height,
                             after_close.pane_w, after_close.pane_h),
                         after_close.camera));

    // --- (10) Closing EVERY canvas still refuses (D-new_shot_from_view-2 preserved) -
    const std::size_t before_close = cameras().size();
    IM_CHECK(dockspace.close("canvas#1"));
    ctx->Yield(3);
    IM_CHECK(!layout_contains(dockspace, "canvas#1"));
    IM_CHECK(pump_until(ctx, [&] { return canvas.focused_framing().pane_w == 0; }));
    ctx->Yield(2);
    IM_CHECK((ctx->ItemInfo(shot_item.c_str()).ItemFlags & ImGuiItemFlags_Disabled) != 0);
    // Driven at the seam rather than through ItemClick: a disabled item never becomes the
    // hovered id, so MouseMove could never reach it. This is the path the ignored click runs.
    IM_CHECK(!gateway.can_new_shot_from_view());
    IM_CHECK(!gateway.new_shot_from_view());
    ctx->Yield(2);
    IM_CHECK(cameras().size() == before_close);
  };
  ImGuiTestEngine_QueueTest(engine, test);

  const int k_max_frames = 200000;
  int frames = 0;
  while (!ImGuiTestEngine_IsTestQueueEmpty(engine) && frames < k_max_frames) {
    shell.new_frame();
    shell.draw_ui();
    shell.render();
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
