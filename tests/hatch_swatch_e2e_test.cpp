// editor.panels.hatch_swatch — the list↔overview cross-highlight UI e2e (docs §9, the offscreen
// software-GL lane; modeled on tests/layers_e2e_test.cpp + tests/overview_e2e_test.cpp). Boots the
// shell over a REAL commands::AppState, seeds two bounded raster cells + a shot camera, and draws
// BOTH the LayersPanel and the OverviewPanel bodies in two side-by-side standalone windows. Drives
// the pointer by widget id across the two panels and reads back the shared
// `AppState::hovered_object()` to prove the cross-panel hover identity: hovering a Layers row, an
// overview cell box (resolved via interact::pick), or either panel's camera item all author the
// same id; moving off every item clears it to nullopt (single-writer, D-hatch_swatch-2/5); and
// hover is independent of selection (Constraint 7). The list-side swatch DRAW itself is an ImGui
// draw-list mark (golden N/A); its identity is pinned headless in tests/hatch_swatch_test.cpp. No
// writer edits happen here — hover is transient per-frame UI state, never a transaction.
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/layers_panel.hpp>
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
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "writer_session.hpp"

using ace::app::CanvasView;
using ace::app::LayersPanel;
using ace::app::OverviewPanel;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::commands::AppState;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  explicit ScratchDir(const char* tag)
      : root(std::filesystem::temp_directory_path() /
             (std::string("ace_hatch_swatch_e2e_") + tag)) {
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

struct E2EState {
  AppState* state = nullptr;
  arbc::ObjectId raster_a;
  arbc::ObjectId raster_b;
  arbc::ObjectId camera;
};

std::string ref_layer(arbc::ObjectId id) {
  return "Layers/###layer_row_" + std::to_string(id.value);
}
std::string ref_layer_cam(arbc::ObjectId id) {
  return "Layers/###camera_row_" + std::to_string(id.value);
}
std::string ref_ov_cell(arbc::ObjectId id) {
  return "Overview/###ov_cell_" + std::to_string(id.value);
}
std::string ref_ov_cam(arbc::ObjectId id) {
  return "Overview/###ov_cam_" + std::to_string(id.value);
}

std::optional<arbc::ObjectId> opt(arbc::ObjectId id) { return std::optional<arbc::ObjectId>(id); }

} // namespace

TEST_CASE("hatch_swatch e2e: list↔overview cross-panel hover cross-highlight") {
  ScratchDir scratch("main");
  ace::platform::NativeFileSystem fs;
  ace::testing::WriterSession session(scratch.root / "hs");
  REQUIRE(session.ok());
  AppState& state = session.state();

  E2EState e2e{};
  e2e.state = &state;
  session.on_writer([&] {
    arbc::Document& doc = state.document();
    const arbc::Registry& reg = state.registry();
    doc.add_composition(256.0, 256.0);
    // Two bounded rasters at WELL-SEPARATED spots so each overview box's centre picks only itself.
    e2e.raster_a = *ace::scene::add_cell(doc, reg, "org.arbc.raster", "16x16",
                                         arbc::Affine::translation(20.0, 20.0));
    e2e.raster_b = *ace::scene::add_cell(doc, reg, "org.arbc.raster", "16x16",
                                         arbc::Affine::translation(120.0, 30.0));
    // Filler cells so the composition exceeds `k_overview_hatch_style_count` (6): the swatch/box
    // for the wrapped ordinals inks through the COLOR fallback (hatch_ink's color_index >= 0 arm),
    // which renders every frame and so exercises that path alongside the two hover targets above.
    // They sit clear of raster_a/raster_b/the camera so those pick centres stay unambiguous.
    for (int i = 0; i < 6; ++i) {
      (void)ace::scene::add_cell(doc, reg, "org.arbc.raster", "8x8",
                                 arbc::Affine::translation(20.0 + i * 4.0, 60.0 + i * 4.0));
    }
    // A shot camera — drawn as a frame (no swatch) but a full participant in the id-keyed hover.
    e2e.camera = ace::scene::add_camera(doc, reg, "Hero", ace::scene::Resolution{48, 32},
                                        arbc::Affine::translation(60.0, 150.0));
  });
  REQUIRE(e2e.raster_a.valid());
  REQUIRE(e2e.raster_b.valid());
  REQUIRE(e2e.camera.valid());

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 1100;
  opts.height = 760;
  REQUIRE(shell.init(opts));

  // No canvas body is registered (no live render), so `have_canvas` is false and the overview draws
  // its render-free box/frame schematic — all this test needs. This keeps the run entirely off the
  // worker pool (no nested-render hazard), matching the layers-panel e2e's canvas setup.
  CanvasView canvas(state, session.writer());
  LayersPanel layers(state, canvas);
  OverviewPanel overview(state, canvas);

  ace::dock::Dockspace dockspace;
  ace::dockmodel::RecentProjects recent(scratch.root / "prefs", fs);
  NoopFolderDialog dialog;
  NoopLauncher launcher;
  ace::app::AppProjectGateway gateway(recent, fs, dialog, launcher, "/usr/bin/arbitraryeditor",
                                      state);
  gateway.set_edit_runner(
      [&canvas](const std::function<void()>& edit) { canvas.apply_edit(edit); });

  // Two side-by-side, NON-overlapping windows so a pointer position resolves unambiguously to one
  // panel (the single-writer rule keys off which window holds the pointer). Sizes are pinned so the
  // Layers window has a large empty region below its rows — the "over the window but over no row"
  // spot the clear-to-nullopt assertion needs.
  shell.set_draw_content([&dockspace, &layers, &overview]() {
    dockspace.draw();
    ImGui::SetNextWindowPos(ImVec2{20.0F, 20.0F}, ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2{280.0F, 600.0F}, ImGuiCond_Once);
    ImGui::Begin("Layers");
    layers.draw("layers");
    ImGui::End();
    ImGui::SetNextWindowPos(ImVec2{320.0F, 20.0F}, ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2{560.0F, 600.0F}, ImGuiCond_Once);
    ImGui::Begin("Overview");
    overview.draw("overview");
    ImGui::End();
  });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  ImGuiTest* test = IM_REGISTER_TEST(engine, "hatch_swatch", "cross_panel_hover");
  test->UserData = &e2e;

  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    AppState& state = *e2e->state;

    ctx->WindowFocus("Layers");
    ctx->Yield(3);

    // --- (0) Both panels render the same objects: layer rows + swatch cells in the list, boxes +
    //         frames in the overview. The swatch draw is a draw-list mark (golden N/A) — its
    //         identity is pinned in the unit test; here we assert the rows/boxes it decorates all
    //         exist. ----
    IM_CHECK(ctx->ItemExists(ref_layer(e2e->raster_a).c_str()));
    IM_CHECK(ctx->ItemExists(ref_layer(e2e->raster_b).c_str()));
    IM_CHECK(ctx->ItemExists(ref_layer_cam(e2e->camera).c_str()));
    IM_CHECK(ctx->ItemExists(ref_ov_cell(e2e->raster_a).c_str()));
    IM_CHECK(ctx->ItemExists(ref_ov_cell(e2e->raster_b).c_str()));
    IM_CHECK(ctx->ItemExists(ref_ov_cam(e2e->camera).c_str()));

    // A fresh session has no hover and no selection.
    IM_CHECK(state.hovered_object() == std::nullopt);
    IM_CHECK(state.selection().size() == 0);

    // --- (i) List→overview cross-highlight: hovering a Layers row authors that id. ---------------
    ctx->MouseMove(ref_layer(e2e->raster_a).c_str());
    ctx->Yield(2);
    IM_CHECK(state.hovered_object() == opt(e2e->raster_a));
    IM_CHECK(state.selection().size() == 0); // hover did NOT select (hover ≠ select)

    // Moving off every row — but still inside the Layers window — CLEARS the hover to nullopt
    // (Constraint 5). (120, 520) is within the pinned Layers window rect, well below its rows.
    ctx->MouseMoveToPos(ImVec2{120.0F, 520.0F});
    ctx->Yield(2);
    IM_CHECK(state.hovered_object() == std::nullopt);

    // --- (ii) Overview→list cross-highlight (cell): hovering an overview box's centre authors that
    //          cell id, resolved through interact::pick (D-hatch_swatch-5), so the matching Layers
    //          row lights. ------------------------------------------------------------------------
    ctx->WindowFocus("Overview");
    ctx->MouseMove(ref_ov_cell(e2e->raster_b).c_str());
    ctx->Yield(2);
    IM_CHECK(state.hovered_object() == opt(e2e->raster_b));

    // --- (iii) Camera cross-highlight, both directions (Constraint 4): overview frame label ------
    ctx->MouseMove(ref_ov_cam(e2e->camera).c_str());
    ctx->Yield(2);
    IM_CHECK(state.hovered_object() == opt(e2e->camera));
    // ...and the reverse, the Layers camera row.
    ctx->WindowFocus("Layers");
    ctx->MouseMove(ref_layer_cam(e2e->camera).c_str());
    ctx->Yield(2);
    IM_CHECK(state.hovered_object() == opt(e2e->camera));

    // --- (iv) Hover ≠ select (Constraint 7): with raster_a SELECTED, hovering raster_b sets the
    //          hover to raster_b while the selection stays raster_a — both treatments coexist.
    //          -----
    ctx->ItemClick(ref_layer(e2e->raster_a).c_str());
    ctx->Yield(2);
    IM_CHECK(state.selection().primary() == e2e->raster_a);
    ctx->MouseMove(ref_layer(e2e->raster_b).c_str());
    ctx->Yield(2);
    IM_CHECK(state.hovered_object() == opt(e2e->raster_b)); // hover moved
    IM_CHECK(state.selection().primary() == e2e->raster_a); // selection did NOT
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

  CHECK(frames < k_max_frames);
  CHECK(count_tested == 1);
  CHECK(count_success == 1);

  canvas.destroy();
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
