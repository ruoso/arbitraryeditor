// editor.panels.overview — the Overview panel UI e2e (docs §9, the offscreen software-GL lane;
// modeled on tests/layers_e2e_test.cpp + tests/canvas_view_e2e_test.cpp). Boots the shell over a
// REAL commands::AppState with a LIVE canvas#1 (so the navigator has a focused framing to drive),
// seeds an unbounded solid + two bounded rasters + a shot camera + a nested composition, and drives
// the OverviewPanel body (the same body the shell registers on ViewType::Overview) by widget id
// under a standalone "Overview" window. Asserts: the schematic renders a box per bounded cell + a
// frame label per camera (no box for the unbounded fill); a box click selects, a camera label
// selects, Shift adds; a marquee multi-selects the bounded rasters (excluding the unbounded fill
// and the out-of-band camera); drag-to-place changes a cell placement and a camera frame, each
// undone in one press; bring-forward/send-back reorders + undoes; the navigator drags/zooms/fits
// the CANVAS camera WITHOUT dirtying the document or adding a journal entry; a freshly-inserted
// cell is placed selected and drag-moves in one entry; entering a nested box re-roots + breadcrumbs
// + drops cameras and a crumb climbs out, arming neither undo nor Save; and an empty composition
// shows a defined empty state. Writer ops run on the MAIN thread through apply_edit / the writer
// session.
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/overview_panel.hpp>
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
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
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
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "writer_session.hpp"

using ace::app::CanvasView;
using ace::app::OverviewPanel;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::commands::AppState;
using ace::dockmodel::ViewType;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  explicit ScratchDir(const char* tag)
      : root(std::filesystem::temp_directory_path() / (std::string("ace_overview_e2e_") + tag)) {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
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
  CanvasView* canvas = nullptr;
  AppState* state = nullptr;
  arbc::ObjectId solid; // unbounded fill — NO box in the schematic
  arbc::ObjectId raster_a;
  arbc::ObjectId raster_b;
  arbc::ObjectId camera;
  arbc::ObjectId nested; // a nested cell in Root (double-click ENTERS it)
  arbc::ObjectId child_comp;
  arbc::ObjectId child_cell;
  arbc::ObjectId empty_nested; // wraps an EMPTY composition
  arbc::ObjectId empty_comp;
  arbc::ObjectId root_comp;
  arbc::ObjectId inserted; // filled in by the insert request
  std::atomic<bool> request_undo{false};
  std::atomic<bool> undo_done{false};
  std::atomic<bool> request_insert{false};
  std::atomic<bool> insert_done{false};
};

std::string ref_cell(arbc::ObjectId id) {
  return "Overview/###ov_cell_" + std::to_string(id.value);
}
std::string ref_cam(arbc::ObjectId id) { return "Overview/###ov_cam_" + std::to_string(id.value); }
std::string ref_camfit(arbc::ObjectId id) {
  return "Overview/###ov_camfit_" + std::to_string(id.value);
}
std::string ref_camlook(arbc::ObjectId id) {
  return "Overview/###ov_camlook_" + std::to_string(id.value);
}
std::string ref_crumb(arbc::ObjectId id) {
  return "Overview/###ov_crumb_" + std::to_string(id.value);
}

std::optional<arbc::Affine> placement_of(AppState& state, arbc::ObjectId id) {
  for (const ace::scene::Cell& c : ace::scene::cells(state.document(), state.registry())) {
    if (c.id == id) {
      return c.placement;
    }
  }
  return std::nullopt;
}
std::optional<arbc::Affine> frame_of(AppState& state, arbc::ObjectId id) {
  for (const ace::scene::Camera& c : ace::scene::cameras(state.document())) {
    if (c.id == id) {
      return c.frame;
    }
  }
  return std::nullopt;
}
std::vector<arbc::ObjectId> layer_ids(AppState& state) {
  std::vector<arbc::ObjectId> ids;
  for (const ace::scene::Cell& c : ace::scene::cells(state.document(), state.registry())) {
    ids.push_back(c.id);
  }
  return ids;
}

} // namespace

TEST_CASE("overview e2e: schematic, select, marquee, edit, navigator, scope, empty state") {
  ScratchDir scratch("main");
  ace::platform::NativeFileSystem fs;
  ace::testing::WriterSession session(scratch.root / "ov");
  REQUIRE(session.ok());
  AppState& state = session.state();

  E2EState e2e;
  e2e.state = &state;
  session.on_writer([&] {
    arbc::Document& doc = state.document();
    const arbc::Registry& reg = state.registry();
    doc.add_composition(160.0, 160.0);
    // Bottom→top: the unbounded solid, then two bounded rasters at distinct spots.
    e2e.solid = *ace::scene::add_cell(doc, reg, "org.arbc.solid", "0.1,0.2,0.3,1",
                                      arbc::Affine::identity());
    e2e.raster_a = *ace::scene::add_cell(doc, reg, "org.arbc.raster", "16x16",
                                         arbc::Affine::translation(10.0, 10.0));
    e2e.raster_b = *ace::scene::add_cell(doc, reg, "org.arbc.raster", "16x16",
                                         arbc::Affine::translation(70.0, 14.0));
    // A shot camera, placed well below the rasters so a rasters-only marquee excludes it.
    e2e.camera = ace::scene::add_camera(doc, reg, "Hero", ace::scene::Resolution{32, 24},
                                        arbc::Affine::translation(110.0, 110.0));
    // A nested composition wrapped by a nested cell in Root (double-click ENTERS it).
    e2e.child_comp = doc.add_composition(48.0, 48.0);
    e2e.child_cell = *ace::scene::add_cell(doc, reg, "org.arbc.raster", "8x8",
                                           arbc::Affine::identity(), e2e.child_comp);
    e2e.nested =
        *ace::scene::add_cell(doc, reg, "org.arbc.nested", std::to_string(e2e.child_comp.value),
                              arbc::Affine::translation(40.0, 40.0));
    // A second nested cell wrapping an EMPTY composition (the empty-state case).
    e2e.empty_comp = doc.add_composition(32.0, 32.0);
    // Placed clear of the other boxes (and the navigator handle) so its double-click is unblocked.
    e2e.empty_nested =
        *ace::scene::add_cell(doc, reg, "org.arbc.nested", std::to_string(e2e.empty_comp.value),
                              arbc::Affine::translation(12.0, 118.0));
  });
  REQUIRE(e2e.raster_a.valid());
  REQUIRE(e2e.camera.valid());
  e2e.root_comp = ace::scene::active_composition(state.document(), std::nullopt);
  REQUIRE(e2e.root_comp.valid());

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 1100;
  opts.height = 760;
  REQUIRE(shell.init(opts));

  // The scaffolding canvas#1 exists only to size the navigator's `focused_framing()` and give it a
  // camera to drive — the OverviewPanel itself is render-free ImGui. Drive it with the
  // deterministic inline settle-fully pool (`WorkerPoolConfig{}`, byte-identical to the threaded
  // path per canvas_renderer.cpp) rather than the shipped threaded interactive pool: this panel's
  // dense navigator gestures re-render canvas#1 far more than the sibling nested-render e2es, and
  // the inline pool keeps every leaf render synchronous within its frame, so a NestedContent leaf
  // can never outlive the frame's operator binding on a worker thread (arbc v0.4.0's cross-frame
  // nested-render detach hazard). Interactive worker threading is covered by the TSan case; nothing
  // the overview panel asserts depends on the pool.
  CanvasView canvas(state, session.writer(), arbc::WorkerPoolConfig{}, std::chrono::hours(1));
  e2e.canvas = &canvas;
  OverviewPanel overview(state, canvas);

  // A LIVE canvas#1 body so `focused_framing()` is sized and the navigator has a camera to drive
  // (the same seam the shell uses). The default dock layout opens canvas#1.
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

  shell.set_draw_content([&dockspace, &overview]() {
    dockspace.draw();
    // A real content region — in the shell the panel fills its dock node; the standalone e2e
    // window must be given a size or the schematic's GetContentRegionAvail() is ~0.
    ImGui::SetNextWindowSize(ImVec2{560.0F, 560.0F}, ImGuiCond_Once);
    ImGui::Begin("Overview");
    overview.draw("overview");
    ImGui::End();
  });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  ImGuiTest* test = IM_REGISTER_TEST(engine, "overview", "schematic_select_edit_navigator_scope");
  test->UserData = &e2e;

  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    AppState& state = *e2e->state;
    CanvasView& canvas = *e2e->canvas;
    const arbc::Journal& journal = state.document().journal();

    // Wait for the live canvas to publish so the navigator's focused framing is sized.
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));
    ctx->WindowFocus("Overview");
    ctx->Yield(3);

    // --- (i) Schematic renders: a box per bounded cell + a camera label; none for the fill. -----
    IM_CHECK(ctx->ItemExists(ref_cell(e2e->raster_a).c_str()));
    IM_CHECK(ctx->ItemExists(ref_cell(e2e->raster_b).c_str()));
    IM_CHECK(ctx->ItemExists(ref_cam(e2e->camera).c_str()));
    IM_CHECK(!ctx->ItemExists(ref_cell(e2e->solid).c_str())); // unbounded fill has no box

    // --- (ii) Select round-trips through the shared selection (D19). ----------------------------
    ctx->ItemClick(ref_cell(e2e->raster_a).c_str());
    ctx->Yield(2);
    IM_CHECK(state.selection().primary() == e2e->raster_a);
    ctx->ItemClick(ref_cam(e2e->camera).c_str()); // a camera label grab selects the camera (D7)
    ctx->Yield(2);
    IM_CHECK(state.selection().primary() == e2e->camera);
    ctx->KeyDown(ImGuiMod_Shift);
    ctx->ItemClick(ref_cell(e2e->raster_a).c_str());
    ctx->KeyUp(ImGuiMod_Shift);
    ctx->Yield(2);
    IM_CHECK(state.selection().size() == 2); // Shift ADDED the cell to the camera selection
    IM_CHECK(state.selection().contains(e2e->raster_a));
    IM_CHECK(state.selection().contains(e2e->camera));

    // --- (iii) Marquee multi-selects the two bounded rasters, excluding the fill and the camera. -
    {
      const ImGuiTestItemInfo a = ctx->ItemInfo(ref_cell(e2e->raster_a).c_str());
      const ImGuiTestItemInfo b = ctx->ItemInfo(ref_cell(e2e->raster_b).c_str());
      IM_CHECK(a.ID != 0 && b.ID != 0);
      const float x0 = std::min(a.RectFull.Min.x, b.RectFull.Min.x) - 6.0F;
      const float y0 = std::min(a.RectFull.Min.y, b.RectFull.Min.y) - 6.0F;
      const float x1 = std::max(a.RectFull.Max.x, b.RectFull.Max.x) + 6.0F;
      const float y1 = std::max(a.RectFull.Max.y, b.RectFull.Max.y) + 6.0F;
      ctx->MouseMoveToPos(ImVec2{x0, y0}); // start on empty schematic (arms the marquee)
      ctx->MouseDown(0);
      ctx->MouseMoveToPos(ImVec2{(x0 + x1) * 0.5F, (y0 + y1) * 0.5F});
      ctx->MouseMoveToPos(ImVec2{x1, y1});
      ctx->MouseUp(0);
      ctx->Yield(2);
      IM_CHECK(state.selection().size() == 2);
      IM_CHECK(state.selection().contains(e2e->raster_a));
      IM_CHECK(state.selection().contains(e2e->raster_b));
      IM_CHECK(!state.selection().contains(e2e->solid));  // unbounded fill excluded (D-selection-5)
      IM_CHECK(!state.selection().contains(e2e->camera)); // out-of-band camera excluded
    }

    // --- (iv) Drag-to-place a cell: placement changes; one-press undo restores (D8). ------------
    {
      const arbc::Affine before = *placement_of(state, e2e->raster_a);
      const std::size_t cursor_before = journal.cursor();
      ctx->ItemDragAndDrop(ref_cell(e2e->raster_a).c_str(), ref_cell(e2e->raster_b).c_str());
      ctx->Yield(4);
      const arbc::Affine after = *placement_of(state, e2e->raster_a);
      IM_CHECK(!(after == before));                    // the placement moved
      IM_CHECK(journal.cursor() == cursor_before + 1); // ONE journal entry
      e2e->request_undo.store(true);
      IM_CHECK(pump_until(ctx, [&] { return e2e->undo_done.load(); }));
      e2e->undo_done.store(false);
      ctx->Yield(2);
      IM_CHECK(*placement_of(state, e2e->raster_a) == before); // one-press undo restored it
    }

    // --- (v) Drag a camera frame: its frame changes, undoes likewise. ---------------------------
    {
      const arbc::Affine before = *frame_of(state, e2e->camera);
      ctx->ItemDragAndDrop(ref_cam(e2e->camera).c_str(), ref_cell(e2e->raster_b).c_str());
      ctx->Yield(4);
      IM_CHECK(!(*frame_of(state, e2e->camera) == before));
      e2e->request_undo.store(true);
      IM_CHECK(pump_until(ctx, [&] { return e2e->undo_done.load(); }));
      e2e->undo_done.store(false);
      ctx->Yield(2);
      IM_CHECK(*frame_of(state, e2e->camera) == before);
    }

    // --- (vi) Bring-forward / send-back reorders the z stack; undo restores. --------------------
    {
      state.selection().select(e2e->raster_a); // raster_a sits above the solid (index 1 of the z)
      ctx->Yield(2);
      const std::vector<arbc::ObjectId> before = layer_ids(state);
      ctx->ItemClick("Overview/###ov_back"); // send raster_a back one step
      ctx->Yield(3);
      const std::vector<arbc::ObjectId> after = layer_ids(state);
      IM_CHECK(after != before); // the membership order flipped (occlusion flips live)
      e2e->request_undo.store(true);
      IM_CHECK(pump_until(ctx, [&] { return e2e->undo_done.load(); }));
      e2e->undo_done.store(false);
      ctx->Yield(2);
      IM_CHECK(layer_ids(state) == before);
      // Bring-forward is the mirror verb (raster_a is at the bottom now, so forward is enabled).
      state.selection().select(e2e->solid); // the solid is bottom-most; forward raises it
      ctx->Yield(2);
      const std::vector<arbc::ObjectId> pre_fwd = layer_ids(state);
      ctx->ItemClick("Overview/###ov_forward");
      ctx->Yield(3);
      IM_CHECK(layer_ids(state) != pre_fwd);
      e2e->request_undo.store(true);
      IM_CHECK(pump_until(ctx, [&] { return e2e->undo_done.load(); }));
      e2e->undo_done.store(false);
      ctx->Yield(2);
      IM_CHECK(layer_ids(state) == pre_fwd);
    }

    // --- (vii) Navigator drives the CANVAS, not the document (Constraint 7, D24/D15). -----------
    {
      IM_CHECK(ctx->ItemExists("Overview/###ov_viewport"));
      const bool dirty_before = state.is_dirty();
      const std::size_t cursor_before = journal.cursor();
      const arbc::Affine cam_before = canvas.focused_framing().camera;
      ctx->ItemDragAndDrop("Overview/###ov_viewport", ref_cell(e2e->raster_a).c_str());
      ctx->Yield(4);
      IM_CHECK(!(canvas.focused_framing().camera == cam_before)); // the viewport camera moved
      IM_CHECK(state.is_dirty() == dirty_before);                 // NOT a document edit
      IM_CHECK(journal.cursor() == cursor_before);                // NO journal entry

      const arbc::Affine cam_after_pan = canvas.focused_framing().camera;
      ctx->ItemClick("Overview/###ov_zoom"); // zoom in
      ctx->Yield(3);
      IM_CHECK(!(canvas.focused_framing().camera == cam_after_pan)); // the zoom moved the camera
      IM_CHECK(journal.cursor() == cursor_before);                   // still no journal entry

      const arbc::Affine cam_after_zoom_in = canvas.focused_framing().camera;
      ctx->ItemClick("Overview/###ov_zoom_out"); // zoom out
      ctx->Yield(3);
      IM_CHECK(!(canvas.focused_framing().camera == cam_after_zoom_in));

      const arbc::Affine cam_after_zoom = canvas.focused_framing().camera;
      ctx->ItemClick(ref_camfit(e2e->camera).c_str()); // fit the viewport to the camera
      ctx->Yield(3);
      IM_CHECK(!(canvas.focused_framing().camera == cam_after_zoom));
      IM_CHECK(state.is_dirty() == dirty_before); // still transient-only

      // Look-through toggles the pane's active shot (transient session state, never a transaction).
      ctx->ItemClick(ref_camlook(e2e->camera).c_str());
      ctx->Yield(3);
      IM_CHECK(canvas.look_through("canvas#1") == std::optional<arbc::ObjectId>(e2e->camera));
      IM_CHECK(journal.cursor() == cursor_before); // look-through is not a document edit
      ctx->ItemClick(ref_camlook(e2e->camera).c_str());
      ctx->Yield(3);
      IM_CHECK(!canvas.look_through("canvas#1").has_value()); // toggled back to the free viewport
    }

    // --- (viii) Insert placement: a freshly-inserted cell is selected + drag-moves. -------------
    {
      e2e->request_insert.store(true);
      IM_CHECK(pump_until(ctx, [&] { return e2e->insert_done.load(); }));
      ctx->Yield(3);
      IM_CHECK(e2e->inserted.valid());
      IM_CHECK(state.selection().primary() ==
               e2e->inserted); // left selected at its provisional box
      IM_CHECK(ctx->ItemExists(ref_cell(e2e->inserted).c_str()));
      const arbc::Affine before = *placement_of(state, e2e->inserted);
      const std::size_t cursor_before = journal.cursor();
      ctx->ItemDragAndDrop(ref_cell(e2e->inserted).c_str(), ref_cell(e2e->raster_b).c_str());
      ctx->Yield(4);
      IM_CHECK(!(*placement_of(state, e2e->inserted) == before));
      IM_CHECK(journal.cursor() == cursor_before + 1); // one entry over the insert's
    }

    // --- (ix) Enter + breadcrumb + climb, arming neither undo nor Save (D17/D29). ---------------
    {
      const std::size_t cursor_pre = journal.cursor();
      const bool dirty_pre = state.is_dirty();
      ctx->ItemDoubleClick(ref_cell(e2e->nested).c_str());
      ctx->Yield(4);
      IM_CHECK(ctx->ItemExists(ref_crumb(e2e->root_comp).c_str()));  // Root crumb
      IM_CHECK(ctx->ItemExists(ref_crumb(e2e->child_comp).c_str())); // child crumb
      IM_CHECK(ctx->ItemExists(ref_cell(e2e->child_cell).c_str()));  // re-rooted to the child
      IM_CHECK(!ctx->ItemExists(ref_cell(e2e->raster_a).c_str()));   // root cells no longer shown
      IM_CHECK(!ctx->ItemExists(ref_cam(e2e->camera).c_str())); // cameras not shown while entered
      IM_CHECK(journal.cursor() == cursor_pre);                 // entering is not a transaction
      IM_CHECK(state.is_dirty() == dirty_pre);                  // and does not dirty the session
      ctx->ItemClick(ref_crumb(e2e->root_comp).c_str());        // climb out to Root
      ctx->Yield(3);
      IM_CHECK(ctx->ItemExists(ref_cell(e2e->raster_a).c_str()));    // root cells shown again
      IM_CHECK(!ctx->ItemExists(ref_cell(e2e->child_cell).c_str())); // child cells gone
    }

    // --- (x) Empty composition -> a defined empty state with no stale boxes. --------------------
    {
      ctx->ItemDoubleClick(ref_cell(e2e->empty_nested).c_str());
      ctx->Yield(4);
      IM_CHECK(ctx->ItemExists("Overview/###ov_empty"));           // the empty state
      IM_CHECK(!ctx->ItemExists(ref_cell(e2e->raster_a).c_str())); // no stale root boxes
      IM_CHECK(!ctx->ItemExists(ref_cell(e2e->child_cell).c_str()));
      ctx->ItemClick(ref_crumb(e2e->root_comp).c_str()); // back to Root for a clean exit
      ctx->Yield(2);
    }
  };
  ImGuiTestEngine_QueueTest(engine, test);

  const int k_max_frames = 300000;
  int frames = 0;
  while (!ImGuiTestEngine_IsTestQueueEmpty(engine) && frames < k_max_frames) {
    shell.new_frame();
    shell.draw_ui();
    shell.render();
    ImGuiTestEngine_PostSwap(engine);
    if (e2e.request_undo.exchange(false)) {
      canvas.apply_edit([&] { (void)ace::commands::undo(state); });
      e2e.undo_done.store(true);
    }
    if (e2e.request_insert.exchange(false)) {
      canvas.apply_edit([&] {
        const auto id = ace::scene::add_cell(state.document(), state.registry(), "org.arbc.raster",
                                             "20x20", arbc::Affine::translation(40.0, 120.0));
        if (id.has_value()) {
          e2e.inserted = *id;
        }
      });
      if (e2e.inserted.valid()) {
        state.selection().select(
            e2e.inserted); // the shipped provisional-placement + selected state
      }
      e2e.insert_done.store(true);
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

  canvas.destroy();
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
