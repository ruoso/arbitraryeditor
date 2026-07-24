// editor.cameras.new_shot_from_view — the "New Shot From View" rail action's UI e2e (docs §9,
// the offscreen software-GL lane; modelled on tests/frame_selection_e2e_test.cpp +
// tests/canvas_nav_e2e_test.cpp). Drives the REAL `app::AppProjectGateway` over a REAL
// `commands::AppState` on a ScratchDir project, with a live `CanvasView` and the shipped
// Inspector body, so every mint takes the shipped path: the rail item -> `ProjectGateway` ->
// `apply_edit` -> `CanvasView::primary_framing` -> `interact::new_shot_from_view` ->
// `commands::dispatch` -> `scene::add_camera`.
//
// Asserts, by stable widget id and on MODEL state (never pixels — software-GL pixels are
// flaky, per the save_ui/gc_ui precedent): the item is enabled once a canvas pane is live and
// DISABLED once every canvas is closed (Constraints 3-4 / D18's no-keep-a-canvas guardrail);
// one click mints exactly one camera named "Camera 1" whose resolution is the pane's device
// size and whose frame inverts back to the presenter's viewport camera (the WYSIWYG law); the
// selection and every canvas's look-through camera are UNCHANGED (Constraint 7); a wheel-zoom
// then a second click promotes the NEW framing at the same resolution (the verb tracks the
// live view, not a cached one); Ctrl+Z removes it and a re-click reuses the freed name
// (D-frame_selection-9); the minted camera reaches both the canvas picker and the Inspector's
// W x H fields; and the two mint verbs are INDEPENDENTLY gated (D23).
#include <ace/app/camera_inspector.hpp>
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

#include "writer_session.hpp"

using ace::app::AppProjectGateway;
using ace::app::CameraInspector;
using ace::app::CanvasView;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::app::ViewFraming;
using ace::commands::AppState;
using ace::dockmodel::ViewType;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_new_shot_from_view_e2e_test") {
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
  arbc::ObjectId cell;
};

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

// Pump until the canvas stops publishing new frames — the settled state in which the pane's
// TEXTURE size (what `##canvas_nav` measures) has caught up with its REQUESTED size (what
// `primary_framing()` reports), so the two are comparable.
void settle(ImGuiTestContext* ctx, CanvasView& canvas) {
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

} // namespace

TEST_CASE("new_shot_from_view e2e: the rail action promotes the live viewport into a shot") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  // The writer identity, bound before the document exists and stopped after the canvas
  // is gone (editor.canvas.writer_thread; see tests/writer_session.hpp).
  ace::testing::WriterSession session(scratch.root / "shot");
  REQUIRE(session.ok());
  AppState& state = session.state();
  // Fixture seeding IS a document write: post it to the identity the open just bound
  // (editor.canvas.writer_thread D-1). Assertions stay on this thread.
  session.on_writer([&] { state.document().add_composition(128.0, 128.0); });

  // An OPAQUE (and unbounded, D-cells_model-3) backdrop so the composite is never blank — the
  // canvas's content gate withholds an all-transparent frame — plus one BOUNDED cell, used only
  // to enable `Frame Selection` in the independent-gating phase.
  arbc::expected<arbc::ObjectId, std::string> backdrop = arbc::unexpected<std::string>("unset");
  arbc::expected<arbc::ObjectId, std::string> cell = arbc::unexpected<std::string>("unset");
  session.on_writer([&] {
    backdrop = ace::scene::add_cell(state.document(), state.registry(), "org.arbc.solid",
                                    "0.15,0.2,0.25,1", arbc::Affine::identity());
    cell = ace::scene::add_cell(state.document(), state.registry(), "org.arbc.raster", "32x32",
                                arbc::Affine::translation(10.0, 10.0));
  });
  REQUIRE(backdrop.has_value());
  REQUIRE(cell.has_value());
  REQUIRE(ace::scene::cameras(state.document()).empty());

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 900;
  opts.height = 640;
  REQUIRE(shell.init(opts));

  CanvasView canvas(state, session.writer());
  CameraInspector inspector(state, canvas);
  ace::views::register_view_body(ViewType::Canvas, [&canvas](std::string_view view_id) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content(view_id, static_cast<int>(avail.x), static_cast<int>(avail.y));
  });

  ace::dock::Dockspace dockspace;
  ace::dockmodel::RecentProjects recent(scratch.root / "prefs", fs);
  NoopFolderDialog dialog;
  NoopLauncher launcher;
  AppProjectGateway gateway(recent, fs, dialog, launcher, "/usr/bin/arbitraryeditor", state);
  // The shipped wiring (shell.cpp:283/288, unchanged by this leaf — Constraint 12): the whole
  // mint, the ViewFraming READ as well as the add_camera WRITE, runs inside
  // CanvasHost::apply_edit on the writer thread (Constraint 5).
  gateway.set_edit_runner(
      [&canvas](const std::function<void()>& edit) { canvas.apply_edit(edit); });
  gateway.set_view_framing([&canvas] { return canvas.primary_framing(); });
  dockspace.set_project_gateway(&gateway);

  // The dock plus a standalone Inspector window (the same body the shell registers on
  // ViewType::Inspector), so the "reaches both readers" assertions drive the shipped widgets.
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

  E2EState e2e{&dockspace, &state, &canvas, &gateway, *cell};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "cameras", "new_shot_from_view_rail");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    AppState& state = *e2e->state;
    CanvasView& canvas = *e2e->canvas;
    ace::dock::Dockspace& dockspace = *e2e->dockspace;
    AppProjectGateway& gateway = *e2e->gateway;
    const std::string rail = ace::dock::tool_rail_title();
    const std::string shot_item = rail + "/###new_shot_from_view";
    const std::string frame_item = rail + "/###frame_selection";
    const auto cameras = [&state] { return ace::scene::cameras(state.document()); };
    ctx->Yield(2);

    // --- (1) The item exists, and is ENABLED once canvas#1 is live and sized ----------
    // The complementary "disabled with no live pane" half is phase (8), which reaches that
    // state deterministically by closing every canvas; asserting it here would race the very
    // first canvas draw, which has already happened by the time the test body runs.
    IM_CHECK(ctx->ItemExists(shot_item.c_str()));
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));
    settle(ctx, canvas);
    IM_CHECK((ctx->ItemInfo(shot_item.c_str()).ItemFlags & ImGuiItemFlags_Disabled) == 0);

    // The pane-rect probe (camera_manip_e2e_test.cpp:250-271): `##canvas_nav` covers the pane
    // image exactly, so its rect IS the device size the promotion must record. Settled, that
    // equals what `primary_framing()` reports — pump until the two agree so a lagging
    // published frame cannot flake the comparison.
    IM_CHECK(pump_until(ctx, [&] {
      const ImGuiTestItemInfo nav = ctx->ItemInfo("canvas#1/##canvas_nav");
      const ViewFraming vf = canvas.primary_framing();
      return vf.pane_w > 0 && vf.pane_h > 0 &&
             vf.pane_w == static_cast<int>(nav.RectFull.GetWidth()) &&
             vf.pane_h == static_cast<int>(nav.RectFull.GetHeight());
    }));
    const ImGuiTestItemInfo nav = ctx->ItemInfo("canvas#1/##canvas_nav");
    const int pane_w = static_cast<int>(nav.RectFull.GetWidth());
    const int pane_h = static_cast<int>(nav.RectFull.GetHeight());
    const ViewFraming framing_at_mint = canvas.primary_framing();

    // --- (2) One click mints exactly one camera that reproduces the view -------------
    IM_CHECK(state.selection().empty());
    ctx->ItemClick(shot_item.c_str());
    IM_CHECK(pump_until(ctx, [&] { return cameras().size() == 1; }));
    const ace::scene::Camera first = cameras()[0];
    IM_CHECK_STR_EQ(first.name.c_str(), "Camera 1");
    // Resolution IS the pane in device pixels — no clamp, no rounding (amended D23 /
    // D-new_shot_from_view-1).
    IM_CHECK(first.resolution.width == pane_w);
    IM_CHECK(first.resolution.height == pane_h);
    // …and the frame inverts back to the presenter's viewport camera: rendered at its own
    // resolution the shot reproduces exactly what was on screen (the WYSIWYG law).
    IM_CHECK(affine_near(
        ace::interact::viewport_camera_for_shot(first.frame, first.resolution.width,
                                                first.resolution.height, pane_w, pane_h),
        framing_at_mint.camera));

    // --- (3) Nothing else moved (Constraint 7) ---------------------------------------
    // Both are the kind of "helpful" behaviour an implementer might add, so both are named
    // assertions: promoting the view must not CHANGE the view.
    IM_CHECK(state.selection().empty());
    IM_CHECK(!canvas.look_through("canvas#1").has_value()); // still the FREE viewport
    IM_CHECK(ctx->ItemExists("canvas#1/Viewport"));
    IM_CHECK(ctx->ItemExists("canvas#1/Camera 1")); // listed, but not activated for the user

    // --- (4) A wheel-zoom then a second click promotes the NEW framing ---------------
    // The canvas_nav recipe: hover the pane, roll the wheel, wait for the re-render. Same
    // pane => same resolution; different camera => different frame. This is the assertion an
    // implementation caching the framing at wire-up time cannot pass.
    ctx->MouseMove("canvas#1/##canvas_nav"); // establishes hover + the pane's viewport
    ctx->MouseWheelY(3.0F);
    IM_CHECK(pump_until(
        ctx, [&] { return !(canvas.primary_framing().camera == framing_at_mint.camera); }));
    settle(ctx, canvas);
    const ViewFraming zoomed = canvas.primary_framing();
    IM_CHECK(zoomed.pane_w == pane_w);
    IM_CHECK(zoomed.pane_h == pane_h);

    ctx->ItemClick(shot_item.c_str());
    IM_CHECK(pump_until(ctx, [&] { return cameras().size() == 2; }));
    const ace::scene::Camera second = cameras()[1];
    IM_CHECK_STR_EQ(second.name.c_str(), "Camera 2");
    IM_CHECK(second.resolution == first.resolution);
    IM_CHECK(!(second.frame == first.frame));
    IM_CHECK(affine_near(
        ace::interact::viewport_camera_for_shot(second.frame, second.resolution.width,
                                                second.resolution.height, pane_w, pane_h),
        zoomed.camera));

    // --- (5) Ctrl+Z takes the last mint back; a re-click reuses the freed name -------
    // ONE undo, even though the create cost two journal entries (D15).
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    IM_CHECK(pump_until(ctx, [&] { return cameras().size() == 1; }));
    IM_CHECK_STR_EQ(cameras()[0].name.c_str(), "Camera 1");
    ctx->ItemClick(shot_item.c_str());
    IM_CHECK(pump_until(ctx, [&] { return cameras().size() == 2; }));
    // FIRST-FREE `n`, not a monotonic counter (D-frame_selection-9) — which is what makes the
    // name assertable at all.
    IM_CHECK_STR_EQ(cameras()[1].name.c_str(), "Camera 2");

    // --- (6) The minted camera reaches both readers ----------------------------------
    IM_CHECK(ctx->ItemExists("canvas#1/Camera 1"));
    IM_CHECK(ctx->ItemExists("canvas#1/Camera 2"));
    ctx->WindowFocus("Inspector");
    ctx->ItemClick("Inspector/###cam_0");
    ctx->Yield(2);
    IM_CHECK(ctx->ItemReadAsInt("Inspector/Width") == pane_w);
    IM_CHECK(ctx->ItemReadAsInt("Inspector/Height") == pane_h);

    // --- (7) The two mint verbs are INDEPENDENTLY gated (D23) ------------------------
    // With nothing selected exactly one of the pair is live: "frame selection" has no region,
    // while "new shot from view" has a pane. Selecting a cell enables both.
    IM_CHECK(state.selection().empty());
    ctx->Yield(2);
    IM_CHECK((ctx->ItemInfo(frame_item.c_str()).ItemFlags & ImGuiItemFlags_Disabled) != 0);
    IM_CHECK((ctx->ItemInfo(shot_item.c_str()).ItemFlags & ImGuiItemFlags_Disabled) == 0);
    state.selection().select(e2e->cell);
    ctx->Yield(2);
    IM_CHECK((ctx->ItemInfo(frame_item.c_str()).ItemFlags & ImGuiItemFlags_Disabled) == 0);
    IM_CHECK((ctx->ItemInfo(shot_item.c_str()).ItemFlags & ImGuiItemFlags_Disabled) == 0);
    state.selection().clear();

    // --- (8) Closing every canvas DISABLES the item (D-new_shot_from_view-2) ---------
    // D18 has no keep-a-canvas guardrail, so "no viewport" is a reachable product state, not a
    // test artifact. The root-composition fallback that keeps `insert_cell` working must NOT
    // be substituted here: with no pane there is nothing the user could be promoting.
    const std::size_t before_close = cameras().size();
    IM_CHECK(dockspace.close("canvas#1"));
    ctx->Yield(3);
    IM_CHECK(!layout_contains(dockspace, "canvas#1"));
    IM_CHECK(pump_until(ctx, [&] { return canvas.primary_framing().pane_w == 0; }));
    ctx->Yield(2);
    IM_CHECK((ctx->ItemInfo(shot_item.c_str()).ItemFlags & ImGuiItemFlags_Disabled) != 0);
    // The click itself is driven at the seam rather than through `ItemClick`: the test engine's
    // MouseMove requires the target to become the hovered id, which a disabled item never
    // does. This is the exact code path the (ignored) click would run.
    IM_CHECK(!gateway.can_new_shot_from_view());
    IM_CHECK(!gateway.new_shot_from_view()); // refuses BEFORE opening any transaction
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

// The unwired gateway's inert defaults (D-new_shot_from_view-3 / D-cells_remove-6): the
// base-class shape every existing gateway fake inherits unchanged, so this leaf's two new
// virtuals cost the other suites no churn (Constraint 11).
TEST_CASE("new_shot_from_view gateway: an unwired ProjectGateway has no view to promote") {
  class BareGateway final : public ace::dock::ProjectGateway {
  public:
    // The three entry verbs on the A24 outcome seam (dock::ProjectEntryOutcome): still
    // inert here — this suite drives neither New nor Open nor Recent.
    ace::dock::ProjectEntryOutcome open_project(const std::filesystem::path&) override {
      return ace::dock::ProjectEntryOutcome::refused_target;
    }
    ace::dock::ProjectEntryOutcome new_project(const std::filesystem::path&,
                                               const std::string&) override {
      return ace::dock::ProjectEntryOutcome::refused_target;
    }
    ace::dock::ProjectEntryOutcome open_recent(const std::filesystem::path&) override {
      return ace::dock::ProjectEntryOutcome::refused_target;
    }
    void pick_folder(std::function<void(std::optional<std::filesystem::path>)>) override {}
    std::vector<std::filesystem::path> recent_projects() const override { return {}; }
    bool save() override { return false; }
    bool is_dirty() const override { return false; }
    bool save_as(const std::filesystem::path&, const std::string&) override { return false; }
    ace::dock::GcSummary clean_up(bool) override { return {}; }
    bool undo() override { return false; }
    bool redo() override { return false; }
    bool can_undo() const override { return false; }
    bool can_redo() const override { return false; }
  };
  BareGateway bare;
  CHECK_FALSE(bare.can_new_shot_from_view());
  CHECK_FALSE(bare.new_shot_from_view());
}
