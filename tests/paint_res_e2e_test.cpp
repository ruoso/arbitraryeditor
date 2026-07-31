// editor.paint.paint_res — the on-canvas `~ N px on <cell>` detail-floor readout e2e (docs §9, the
// offscreen software-GL lane; rig cloned from tests/brush_e2e_test.cpp + tests/resolution_e2e_test.
// cpp). Boots the shell over a REAL commands::AppState, seeds a bounded org.arbc.raster (plus an
// opaque backdrop so the pane issues its first frame, and a bounded non-raster solid), threads the
// rail's active tool into the Canvas body, and drives the Brush tool by RAW MOUSE POSITION.
//
// There is NO committed screenshot golden: ACE_GOLDEN_DIR is not wired into ace_shell_test and
// software-GL pixels are flaky by construction (the same decision resolution_e2e_test.cpp records;
// resolution.md:415-416 precedent the refinement cites). The readout's number is byte-pinned by the
// L1 unit test tests/paint_res_test.cpp; here we assert the on-canvas cue's scene-truth mirror via
// the `CanvasView::brush_readout()` observability accessor.
//
// Asserts (Acceptance criteria):
//   * healthy readout — with a raster selected and the Brush hovering, the readout has a target and
//     a plausible finite N well above the detail floor.
//   * zoom lowers N (D5) — a wheel zoom-in drops N (finer real detail) while the cell's native
//     resolution is unchanged (not re-gridded, D8).
//   * per-cell / placement (D8) — magnifying the cell's placement halves N with native px
//   unchanged.
//   * detail-floor cue (Constraint 6) — magnifying far enough that N falls to <= 1 flips the
//   readout
//     to the floor verdict and exposes NO resample affordance.
//   * no writable target (Constraint 5) — a non-raster primary shows no px readout; re-selecting
//   the
//     raster brings it back.
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/commands/selection.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/dockmodel/tool_rail.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/interact/pick.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/kind_raster/raster_content.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
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
using ace::dockmodel::ToolId;
using ace::dockmodel::ViewType;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  explicit ScratchDir(const char* tag)
      : root(std::filesystem::temp_directory_path() / (std::string("ace_paint_res_e2e_") + tag)) {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// Geometry in COMPOSITION units, 1:1 with pane-relative device px (canvas#1's viewport starts at
// identity). A 60x60 raster at (40,40) covers [40,100]^2; a 40x40 non-raster solid at (150,150)
// covers [150,190]^2.
constexpr double k_raster_at = 40.0;
constexpr double k_solid_at = 150.0;
constexpr int k_raster_native = 60;

struct Seeded {
  arbc::ObjectId raster;
  arbc::ObjectId solid;
};

Seeded seed(AppState& state) {
  arbc::Document& doc = state.document();
  const arbc::ObjectId comp = doc.add_composition(256.0, 256.0);
  // Opaque unbounded backdrop first, so the pane issues its first (non-transparent) frame.
  ace::scene::add_cell(doc, state.registry(), "org.arbc.solid", "0.15,0.2,0.25,1",
                       arbc::Affine::identity());
  Seeded out;
  const arbc::expected<arbc::ObjectId, std::string> r =
      ace::scene::add_cell(doc, state.registry(), "org.arbc.raster", "60x60",
                           arbc::Affine::translation(k_raster_at, k_raster_at));
  out.raster = r.has_value() ? *r : arbc::ObjectId{};
  // A bounded, pickable, NON-raster cell (a Constraint-5 no-writable-target primary).
  const arbc::Rect extent{0.0, 0.0, 40.0, 40.0};
  out.solid = doc.add_content(
      std::make_shared<arbc::SolidContent>(arbc::Rgba{0.0F, 0.0F, 0.6F, 1.0F}, extent));
  doc.attach_layer(comp,
                   doc.add_layer(out.solid, arbc::Affine::translation(k_solid_at, k_solid_at)));
  return out;
}

// The live scene::Cell for `id` off the pinned snapshot (native px / placement / layer) — the same
// fields the readout reads. Empty if the id no longer resolves.
std::optional<ace::scene::Cell> cell_by_id(AppState& state, arbc::ObjectId id) {
  for (const ace::scene::Cell& c : ace::scene::cells(state.document(), state.registry())) {
    if (c.id == id) {
      return c;
    }
  }
  return std::nullopt;
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
  AppState* state;
  ace::dock::Dockspace* dockspace;
  Seeded ids;
};

} // namespace

TEST_CASE("paint_res e2e: the `~ N px on <cell>` readout tracks zoom + placement and states the "
          "detail floor with no resample affordance") {
  ScratchDir scratch("main");
  ace::platform::NativeFileSystem fs;
  ace::testing::WriterSession session(scratch.root / "paint_res");
  REQUIRE(session.ok());
  AppState& state = session.state();
  Seeded ids;
  session.on_writer([&] { ids = seed(state); });
  REQUIRE(ids.raster.valid());
  REQUIRE(ids.solid.valid());
  REQUIRE(state.selection().empty());

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 900;
  opts.height = 640;
  REQUIRE(shell.init(opts));

  CanvasView canvas(state, session.writer());
  ace::dock::Dockspace dockspace;
  ace::views::register_view_body(ViewType::Canvas, [&canvas, &dockspace](std::string_view view_id) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content(view_id, static_cast<int>(avail.x), static_cast<int>(avail.y),
                        dockspace.tools().active());
  });

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
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&canvas, &state, &dockspace, ids};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "paint_res", "readout_zoom_placement_floor");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    CanvasView& canvas = *e2e->canvas;
    AppState& state = *e2e->state;
    ace::dock::Dockspace& dockspace = *e2e->dockspace;
    ace::commands::Selection& sel = state.selection();
    const arbc::ObjectId raster = e2e->ids.raster;
    const arbc::ObjectId solid = e2e->ids.solid;

    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));
    ctx->WindowFocus("canvas#1");
    ctx->Yield(3);

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
    const auto set_tool = [&](ToolId t) {
      dockspace.tools().select(t);
      ctx->Yield(3);
    };

    ctx->MouseMove("canvas#1/##canvas_nav"); // establish the viewport for raw-position moves

    // Select the raster, then switch to Brush and hover over it.
    set_tool(ToolId::Select);
    click_at(at(70.0F, 70.0F));
    IM_CHECK(sel.primary() == raster);
    set_tool(ToolId::Brush);
    ctx->MouseMoveToPos(at(70.0F, 70.0F));
    ctx->Yield(3);

    const std::optional<ace::scene::Cell> rc0 = cell_by_id(state, raster);
    IM_CHECK(rc0.has_value());
    IM_CHECK(rc0->detail.native_pixels.has_value());
    IM_CHECK(rc0->detail.native_pixels->first == k_raster_native);
    IM_CHECK(rc0->detail.native_pixels->second == k_raster_native);

    // --- healthy readout: a target, a plausible finite N above the floor. ----------------------
    const CanvasView::BrushReadout r0 = canvas.brush_readout();
    IM_CHECK(r0.has_target);
    IM_CHECK(r0.valid);
    IM_CHECK(r0.cell_px > 0.0);
    IM_CHECK(std::isfinite(r0.cell_px));
    IM_CHECK(!r0.at_floor);
    IM_CHECK(r0.cell_px > 1.0); // the default 5% brush on a 1:1 cell is well above one native px

    // --- live mid-stroke (D-paint_res-5): the readout reads the PINNED stroke cell during a drag.
    // - Press and hold over the raster; once the stroke opens (brush_stroke_active), the readout
    // still reports N for the pinned cell/placement, not only while merely hovering.
    ctx->MouseMoveToPos(at(70.0F, 70.0F));
    ctx->MouseDown(0);
    ctx->Yield(3); // the press frame opens the stroke; a later frame reads it back
    const CanvasView::BrushReadout during = canvas.brush_readout();
    ctx->MouseUp(0);
    ctx->Yield(2);
    IM_CHECK(during.has_target); // the pinned stroke raster is the target
    IM_CHECK(during.valid);
    IM_CHECK(during.cell_px > 0.0);

    // --- zoom lowers N (D5), native resolution unchanged (D8: not re-gridded). ------------------
    ctx->MouseMoveToPos(at(70.0F, 70.0F));
    ctx->MouseWheelY(5.0F); // positive wheel = zoom IN
    ctx->Yield(3);
    const CanvasView::BrushReadout r1 = canvas.brush_readout();
    IM_CHECK(r1.has_target);
    IM_CHECK(r1.valid);
    IM_CHECK(r1.cell_px < r0.cell_px); // zoomed in => finer real detail => fewer native px covered
    const std::optional<ace::scene::Cell> rc1 = cell_by_id(state, raster);
    IM_CHECK(rc1.has_value());
    IM_CHECK(rc1->detail.native_pixels == rc0->detail.native_pixels); // native grid unchanged (D8)
    // The on-screen ring is screen-locked (0.5 * view_fraction * shorter-pane-edge, independent of
    // zoom): the brush size did not change here, so the ring holds while N dropped — the D5 split.

    // --- per-cell / placement (D8): magnifying the placement 2x roughly halves N, native fixed. -
    const CanvasView::BrushReadout before_scale = canvas.brush_readout();
    const std::optional<ace::scene::Cell> rcp = cell_by_id(state, raster);
    IM_CHECK(rcp.has_value());
    canvas.apply_edit([&] {
      state.document().set_layer_transform(
          rcp->layer, arbc::Affine{2.0, 0.0, 0.0, 2.0, k_raster_at, k_raster_at});
    });
    ctx->Yield(3);
    const CanvasView::BrushReadout after_scale = canvas.brush_readout();
    IM_CHECK(after_scale.valid);
    IM_CHECK(after_scale.cell_px < before_scale.cell_px * 0.6); // ~halved by the 2x placement
    IM_CHECK(after_scale.cell_px > before_scale.cell_px * 0.4);
    const std::optional<ace::scene::Cell> rcp2 = cell_by_id(state, raster);
    IM_CHECK(rcp2.has_value());
    IM_CHECK(rcp2->detail.native_pixels == rc0->detail.native_pixels); // native px unchanged (D8)

    // --- detail-floor cue (Constraint 6): magnify far enough that N <= 1, no resample action. ---
    // Magnifying the cell's placement is the D8 dual of zooming the camera in — both raise device
    // px per native px, so a big placement scale drives N below one native pixel: the detail floor,
    // where a dab maps sub-pixel (§4:120-124). Deterministic (no dependence on the zoom clamp).
    canvas.apply_edit([&] {
      state.document().set_layer_transform(
          rcp->layer, arbc::Affine{80.0, 0.0, 0.0, 80.0, k_raster_at, k_raster_at});
    });
    ctx->MouseMoveToPos(at(70.0F, 70.0F));
    ctx->Yield(3);
    const CanvasView::BrushReadout floored = canvas.brush_readout();
    IM_CHECK(floored.has_target);
    IM_CHECK(floored.valid);
    IM_CHECK(floored.at_floor);       // the brush now maps below one native pixel
    IM_CHECK(floored.cell_px <= 1.0); // == interact::k_detail_floor_px
    // The floor cue STATES the condition; it ships NO resample action (D-paint_res-4 — no verb).
    IM_CHECK(!ctx->ItemExists("canvas#1/Resample to crisp"));
    IM_CHECK(!ctx->ItemExists("canvas#1/##paint_res_resample"));
    // Native resolution is STILL unchanged — the floor is a placement effect, never a re-grid (D8).
    const std::optional<ace::scene::Cell> rcf = cell_by_id(state, raster);
    IM_CHECK(rcf.has_value());
    IM_CHECK(rcf->detail.native_pixels == rc0->detail.native_pixels);

    // Restore the raster's placement so the solid's pick point below is unambiguous (the 80x cell
    // otherwise covers the whole pane). Native px is unaffected either way (D8).
    canvas.apply_edit([&] {
      state.document().set_layer_transform(rcp->layer,
                                           arbc::Affine::translation(k_raster_at, k_raster_at));
    });
    ctx->Yield(3);

    // --- no writable target (Constraint 5): a non-raster primary shows no px readout. -----------
    // Select via the Selection API (not a device-space click): the earlier camera zoom moved the
    // composition->device mapping, so a raw-position click no longer lands on the solid. The
    // readout response to the SELECTION is what this asserts, not the pick path (brush's own e2e
    // pins that).
    set_tool(ToolId::Brush);
    sel.select(solid); // the bounded non-raster cell
    ctx->MouseMoveToPos(at(70.0F, 70.0F));
    ctx->Yield(3);
    IM_CHECK(sel.primary() == solid);
    const CanvasView::BrushReadout no_target = canvas.brush_readout();
    IM_CHECK(!no_target.has_target); // no writable raster => no px number (the ring still draws)
    IM_CHECK(!no_target.valid);

    // Re-selecting the raster brings the readout back (tracks the selected cell, D-paint_res-5).
    sel.select(raster);
    ctx->MouseMoveToPos(at(70.0F, 70.0F));
    ctx->Yield(3);
    IM_CHECK(sel.primary() == raster);
    IM_CHECK(canvas.brush_readout().has_target);
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
