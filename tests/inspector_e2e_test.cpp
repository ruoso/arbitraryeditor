// editor.panels.inspector — the dense per-object property sheet UI e2e (docs §9, the offscreen
// software-GL lane; modeled on tests/camera_manip_e2e_test.cpp). Boots the shell over a REAL
// commands::AppState, seeds a painted-raster cell + a shot camera, and drives the generalized
// InspectorPanel body (the same body the shell registers on ViewType::Inspector) by widget id
// under a standalone "Inspector" window. Asserts: (i) a selected CELL renders the three blocks
// (transform readout + appearance controls + source/health) — the opacity/visibility controls
// exist; (ii) an opacity edit round-trips through apply_edit (ONE journal entry, LayerRecord
// opacity changed) and undo restores it; (iii) a visibility toggle round-trips and undoes; (iv)
// an empty selection shows a defined empty state with no stale controls; (v) a selected CAMERA
// still drives the shipped resolution block (unregressed). Writer ops run on the MAIN thread
// through apply_edit.
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/inspector_panel.hpp>
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
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "writer_session.hpp"

using ace::app::CanvasView;
using ace::app::InspectorPanel;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::commands::AppState;
using ace::dockmodel::ViewType;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  explicit ScratchDir(const char* tag)
      : root(std::filesystem::temp_directory_path() / (std::string("ace_inspector_e2e_") + tag)) {
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

// Scene-truth readers (off the live pinned snapshot).
double cell_opacity(AppState& state, arbc::ObjectId id) {
  for (const ace::scene::Cell& c : ace::scene::cells(state.document(), state.registry())) {
    if (c.id == id) {
      return c.opacity;
    }
  }
  return -1.0;
}
bool cell_visible(AppState& state, arbc::ObjectId id) {
  for (const ace::scene::Cell& c : ace::scene::cells(state.document(), state.registry())) {
    if (c.id == id) {
      return c.visible;
    }
  }
  return false;
}
ace::scene::Resolution cam_res(AppState& state, arbc::ObjectId id) {
  for (const ace::scene::Camera& c : ace::scene::cameras(state.document())) {
    if (c.id == id) {
      return c.resolution;
    }
  }
  return {};
}

struct E2EState {
  CanvasView* canvas;
  AppState* state;
  arbc::ObjectId cell_id;
  arbc::ObjectId camera_id;
  std::atomic<bool> request_undo{false};
  std::atomic<bool> undo_done{false};
};

} // namespace

TEST_CASE("inspector e2e: cell blocks, opacity/visibility round-trip, empty state, camera block") {
  ScratchDir scratch("main");
  ace::platform::NativeFileSystem fs;
  ace::testing::WriterSession session(scratch.root / "ins");
  REQUIRE(session.ok());
  AppState& state = session.state();

  // Fixture seeding IS a document write: post it to the identity the open bound. A large painted
  // raster and a camera framing a small region (so its capture is soft — the Source block has a
  // real health verdict). Assertions stay on this thread.
  arbc::ObjectId cell;
  arbc::ObjectId camera;
  session.on_writer([&] {
    state.document().add_composition(512.0, 512.0);
    // An opaque solid background so the canvas composites NON-EMPTY content (the driver withholds
    // the first published frame until then, editor.canvas.blank_first_frame).
    REQUIRE(ace::scene::add_cell(state.document(), state.registry(), "org.arbc.solid",
                                 "0.1,0.2,0.3,1", arbc::Affine::identity())
                .has_value());
    const auto added = ace::scene::add_cell(state.document(), state.registry(), "org.arbc.raster",
                                            "160x160", arbc::Affine::translation(20.0, 20.0));
    REQUIRE(added.has_value());
    cell = *added;
    camera =
        ace::scene::add_camera(state.document(), state.registry(), "Hero",
                               ace::scene::Resolution{256, 256}, arbc::Affine::scaling(0.5, 0.5));
  });
  REQUIRE(cell.valid());
  REQUIRE(camera.valid());

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 900;
  opts.height = 640;
  REQUIRE(shell.init(opts));

  CanvasView canvas(state, session.writer());
  InspectorPanel inspector(state, canvas);
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

  // Draw the dock AND a standalone Inspector window each frame (the same body the shell registers
  // on ViewType::Inspector), driven by the "Inspector" window ref.
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

  E2EState e2e{&canvas, &state};
  e2e.cell_id = cell;
  e2e.camera_id = camera;
  ImGuiTest* test = IM_REGISTER_TEST(engine, "inspector", "blocks_opacity_visibility_empty_camera");
  test->UserData = &e2e;

  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    AppState& state = *e2e->state;
    const arbc::ObjectId cell = e2e->cell_id;
    const arbc::ObjectId camera = e2e->camera_id;
    const arbc::Journal& journal = state.document().journal();

    IM_CHECK(pump_until(ctx, [&] { return e2e->canvas->frames_issued("canvas#1") >= 1; }));

    // --- (i) A selected CELL renders the three blocks — the appearance controls exist. --------
    state.selection().select(cell);
    ctx->Yield(3);
    ctx->WindowFocus("Inspector");
    ctx->Yield(2);
    IM_CHECK(ctx->ItemExists("Inspector/Opacity"));
    IM_CHECK(ctx->ItemExists("Inspector/Visible"));

    // --- (ii) Opacity edit round-trips through apply_edit — one journal entry, undo restores. --
    IM_CHECK(cell_opacity(state, cell) == 1.0); // library default
    const std::size_t depth_op = journal.cursor();
    ctx->ItemInputValue("Inspector/Opacity", 0.30F);
    ctx->Yield(3);
    IM_CHECK(journal.cursor() == depth_op + 1);                  // ONE undo step
    IM_CHECK(std::abs(cell_opacity(state, cell) - 0.30) < 0.02); // the LayerRecord opacity changed
    e2e->request_undo.store(true);
    IM_CHECK(pump_until(ctx, [&] { return e2e->undo_done.load(); }));
    e2e->undo_done.store(false);
    ctx->Yield(2);
    IM_CHECK(cell_opacity(state, cell) == 1.0); // undo restored the prior opacity

    // --- (iii) Visibility toggle round-trips and undoes. -------------------------------------
    IM_CHECK(cell_visible(state, cell));
    const std::size_t depth_vis = journal.cursor();
    ctx->ItemClick("Inspector/Visible");
    ctx->Yield(3);
    IM_CHECK(journal.cursor() == depth_vis + 1);
    IM_CHECK(!cell_visible(state, cell));
    e2e->request_undo.store(true);
    IM_CHECK(pump_until(ctx, [&] { return e2e->undo_done.load(); }));
    e2e->undo_done.store(false);
    ctx->Yield(2);
    IM_CHECK(cell_visible(state, cell));

    // --- (iv) Empty selection -> a defined empty state, no stale controls. -------------------
    state.selection().clear();
    ctx->Yield(3);
    IM_CHECK(!ctx->ItemExists("Inspector/Opacity"));
    IM_CHECK(!ctx->ItemExists("Inspector/Visible"));
    IM_CHECK(!ctx->ItemExists("Inspector/Width"));

    // --- (v) Selected CAMERA -> the shipped resolution block still works (unregressed). ------
    state.selection().select(camera);
    ctx->Yield(3);
    IM_CHECK(ctx->ItemExists("Inspector/Width"));
    const std::size_t depth_res = journal.cursor();
    ctx->ItemInputValue("Inspector/Width", 96);
    ctx->ItemInputValue("Inspector/Height", 96);
    ctx->ItemClick("Inspector/Apply Resolution");
    ctx->Yield(3);
    IM_CHECK(journal.cursor() == depth_res + 1);
    IM_CHECK((cam_res(state, camera) == ace::scene::Resolution{96, 96}));
  };
  ImGuiTestEngine_QueueTest(engine, test);

  const int k_max_frames = 200000;
  int frames = 0;
  while (!ImGuiTestEngine_IsTestQueueEmpty(engine) && frames < k_max_frames) {
    shell.new_frame();
    shell.draw_ui();
    shell.render();
    ImGuiTestEngine_PostSwap(engine);
    // Undo requests run on THIS (main) thread through the same apply_edit seam as the edits.
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

  canvas.destroy();
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
