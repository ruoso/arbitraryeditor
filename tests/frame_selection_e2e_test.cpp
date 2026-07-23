// editor.cameras.frame_selection — the Frame Selection rail action's UI e2e (docs §9, the
// offscreen software-GL lane; modelled on tests/cells_remove_e2e_test.cpp +
// tests/camera_manip_e2e_test.cpp). Drives the REAL `app::AppProjectGateway` over a REAL
// `commands::AppState` on a ScratchDir project, with a live `CanvasView` and the shipped
// Inspector body, so every mint takes the shipped path: the rail item -> `ProjectGateway` ->
// `apply_edit` -> `interact::pick_targets` -> `interact::selected_extent` ->
// `interact::shot_from_extent` -> `commands::dispatch` -> `scene::add_camera`.
//
// Asserts, by stable widget id and on MODEL state (never pixels — software-GL pixels are
// flaky, per the save_ui/gc_ui precedent): `###frame_selection` is DISABLED with an empty
// selection and enabled once a cell is selected (Constraint 13); one click mints exactly one
// camera named "Camera 1" whose frame maps its output rect onto the selected cell's placed
// extent; the selection is UNCHANGED and no canvas switched cameras (Constraint 10 /
// D-frame_selection-10); a second click on the same selection mints "Camera 2" with identical
// geometry (D-frame_selection-9); a two-cell selection mints ONE camera framing both; Ctrl+Z
// removes the minted camera while the selection still stands; and the minted camera is
// immediately usable by the shipped surfaces — the canvas picker, the Inspector's camera list,
// and its W x H fields, which read back the derived resolution.
#include <ace/app/camera_inspector.hpp>
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
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

#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
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

// The two bounded cells the mint frames. Placed unscaled, so at 1 composition unit = 1 pixel
// (D23) each frames back at its own native 32x32 and the union is an exact 92 x 72.
constexpr double k_cell_edge = 32.0;
constexpr double k_a_x = 10.0;
constexpr double k_a_y = 10.0;
constexpr double k_b_x = 70.0;
constexpr double k_b_y = 50.0;

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_frame_selection_e2e_test") {
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
  arbc::ObjectId cell_a;
  arbc::ObjectId cell_b;
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

bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

// The composition region a camera covers: its output rectangle placed by its frame.
arbc::Rect covered(const ace::scene::Camera& camera) {
  return camera.frame.map_rect(arbc::Rect::from_size(
      static_cast<double>(camera.resolution.width), static_cast<double>(camera.resolution.height)));
}

} // namespace

TEST_CASE("frame_selection e2e: the rail action mints a camera fit to the selection") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "frame");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  state.document().add_composition(128.0, 128.0);

  // An OPAQUE (and unbounded, D-cells_model-3) backdrop so the composite is never blank — the
  // canvas's content gate withholds an all-transparent frame — plus the two BOUNDED cells the
  // mint actually frames. The backdrop is never selected, so it never enters a union.
  REQUIRE(ace::scene::add_cell(state.document(), state.registry(), "org.arbc.solid",
                               "0.15,0.2,0.25,1", arbc::Affine::identity())
              .has_value());
  const arbc::expected<arbc::ObjectId, std::string> cell_a =
      ace::scene::add_cell(state.document(), state.registry(), "org.arbc.raster", "32x32",
                           arbc::Affine::translation(k_a_x, k_a_y));
  const arbc::expected<arbc::ObjectId, std::string> cell_b =
      ace::scene::add_cell(state.document(), state.registry(), "org.arbc.raster", "32x32",
                           arbc::Affine::translation(k_b_x, k_b_y));
  REQUIRE(cell_a.has_value());
  REQUIRE(cell_b.has_value());
  REQUIRE(ace::scene::cameras(state.document()).empty()); // the editor cannot make one yet

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
  // The shipped wiring (shell.cpp): the whole mint — the pick_targets read AND the add_camera
  // write — runs inside CanvasHost::apply_edit on the writer thread (Constraint 8).
  gateway.set_edit_runner(
      [&canvas](const std::function<void()>& edit) { canvas.apply_edit(edit); });
  gateway.set_view_framing([&canvas] { return canvas.primary_framing(); });
  dockspace.set_project_gateway(&gateway);

  // The dock plus a standalone Inspector window (the same body the shell registers on
  // ViewType::Inspector), so the "immediately usable" assertions drive the shipped widgets.
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

  E2EState e2e{&dockspace, &state, &canvas, *cell_a, *cell_b};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "cameras", "frame_selection_rail");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    AppState& state = *e2e->state;
    CanvasView& canvas = *e2e->canvas;
    const std::string rail = ace::dock::tool_rail_title();
    const std::string frame_item = rail + "/###frame_selection";
    const auto cameras = [&state] { return ace::scene::cameras(state.document()); };
    ctx->Yield(2);
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));

    // --- (i) DISABLED, not hidden, with an empty selection (Constraint 13) ------------
    IM_CHECK(state.selection().empty());
    IM_CHECK(ctx->ItemExists(frame_item.c_str()));
    IM_CHECK((ctx->ItemInfo(frame_item.c_str()).ItemFlags & ImGuiItemFlags_Disabled) != 0);

    state.selection().select(e2e->cell_a);
    ctx->Yield(2);
    IM_CHECK((ctx->ItemInfo(frame_item.c_str()).ItemFlags & ImGuiItemFlags_Disabled) == 0);

    // --- (ii) One click mints exactly one camera fit to the selected cell -------------
    const std::vector<arbc::ObjectId> selection_before = state.selection().items();
    ctx->ItemClick(frame_item.c_str());
    IM_CHECK(pump_until(ctx, [&] { return cameras().size() == 1; }));
    const ace::scene::Camera first = cameras()[0];
    IM_CHECK_STR_EQ(first.name.c_str(), "Camera 1");
    // 1 composition unit = 1 pixel (D23): an unscaled 32x32 cell frames back at 32x32.
    IM_CHECK(first.resolution.width == 32);
    IM_CHECK(first.resolution.height == 32);
    const arbc::Rect region = covered(first);
    IM_CHECK(near(region.x0, k_a_x));
    IM_CHECK(near(region.y0, k_a_y));
    IM_CHECK(near(region.x1, k_a_x + k_cell_edge));
    IM_CHECK(near(region.y1, k_a_y + k_cell_edge));

    // --- (iii) The selection is unchanged and no canvas switched cameras --------------
    // Both are the kind of "helpful" behaviour an implementer might add, so both are named
    // assertions (D-frame_selection-10).
    IM_CHECK(state.selection().items() == selection_before);
    IM_CHECK(state.selection().primary() == e2e->cell_a);
    IM_CHECK(!canvas.look_through("canvas#1").has_value()); // still the FREE viewport
    IM_CHECK(ctx->ItemExists("canvas#1/Viewport"));

    // --- (iv) A second click on the SAME selection advances the auto-name -------------
    ctx->ItemClick(frame_item.c_str());
    IM_CHECK(pump_until(ctx, [&] { return cameras().size() == 2; }));
    const ace::scene::Camera second = cameras()[1];
    IM_CHECK_STR_EQ(second.name.c_str(), "Camera 2");
    IM_CHECK(second.frame == first.frame); // the geometry is identical — only the name moved
    IM_CHECK(second.resolution == first.resolution);

    // --- (v) A two-cell selection mints ONE camera framing BOTH -----------------------
    state.selection().select(e2e->cell_a);
    state.selection().add(e2e->cell_b);
    ctx->Yield(2);
    ctx->ItemClick(frame_item.c_str());
    IM_CHECK(pump_until(ctx, [&] { return cameras().size() == 3; }));
    const ace::scene::Camera both = cameras()[2];
    IM_CHECK_STR_EQ(both.name.c_str(), "Camera 3");
    IM_CHECK(both.resolution.width == 92); // the union spans [10,102] x [10,82]
    IM_CHECK(both.resolution.height == 72);
    const arbc::Rect union_region = covered(both);
    IM_CHECK(union_region.x0 <= k_a_x && union_region.y0 <= k_a_y);
    IM_CHECK(union_region.x1 >= k_b_x + k_cell_edge && union_region.y1 >= k_b_y + k_cell_edge);

    // --- (vi) Ctrl+Z removes the minted camera; the selection still stands ------------
    // ONE undo, even though the create cost two journal entries (D15 / Constraint 9).
    const std::vector<arbc::ObjectId> selection_pre_undo = state.selection().items();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    IM_CHECK(pump_until(ctx, [&] { return cameras().size() == 2; }));
    IM_CHECK(state.selection().items() == selection_pre_undo);
    IM_CHECK(state.selection().size() == 2);

    // --- (vii) The minted camera is immediately usable by the shipped surfaces --------
    // The canvas picker lists it by name (D-look_through-5) — the affordance that was
    // unreachable before this leaf, because a user could not make a camera at all.
    IM_CHECK(ctx->ItemExists("canvas#1/Camera 1"));
    IM_CHECK(ctx->ItemExists("canvas#1/Camera 2"));
    // …and so does the Inspector's camera list, whose W x H fields read back the DERIVED
    // resolution (the documented escape hatch for anything the derivation chose, D9).
    ctx->WindowFocus("Inspector");
    ctx->ItemClick("Inspector/###cam_0");
    ctx->Yield(2);
    IM_CHECK(ctx->ItemReadAsInt("Inspector/Width") == 32);
    IM_CHECK(ctx->ItemReadAsInt("Inspector/Height") == 32);
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

// The unwired gateway's inert defaults (D-frame_selection-8 / D-cells_remove-6): the base-class
// shape every existing gateway fake inherits unchanged, so this leaf's two new virtuals cost
// the other suites no churn.
TEST_CASE("frame_selection gateway: an unwired ProjectGateway has nothing to frame") {
  class BareGateway final : public ace::dock::ProjectGateway {
  public:
    bool open_project(const std::filesystem::path&) override { return false; }
    bool new_project(const std::filesystem::path&, const std::string&) override { return false; }
    bool open_recent(const std::filesystem::path&) override { return false; }
    void pick_folder(std::function<void(std::optional<std::filesystem::path>)>) override {}
    std::vector<std::filesystem::path> recent_projects() const override { return {}; }
    bool save() override { return false; }
    bool is_dirty() const override { return false; }
    void save_as() override {}
    ace::dock::GcSummary clean_up(bool) override { return {}; }
    bool undo() override { return false; }
    bool redo() override { return false; }
    bool can_undo() const override { return false; }
    bool can_redo() const override { return false; }
  };
  BareGateway bare;
  CHECK_FALSE(bare.can_frame_selection());
  CHECK_FALSE(bare.frame_selection());
}
