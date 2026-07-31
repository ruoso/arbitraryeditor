// editor.import.image — the borrowed-image import UI e2e (docs §9, the offscreen software-GL
// lane; modelled on tests/cells_insert_e2e_test.cpp). Exercises the ONE app import verb
// `AppProjectGateway::insert_image` through both entry points: the OS file-drop (a direct verb
// call with a pane-local device point, the shape the shell's SDL_EVENT_DROP_FILE arm produces)
// and the "Place image…" affordance (which drives the injected `FileDialog` fake and places at
// the focused canvas centre). Asserts, on model state (never pixels): the minted cell is
// selected, ReferencedImage + borrowed with native_pixels == the fixture size, placed 1:1 at
// the drop point, and read-only (no editable facet). Plus a pure `drop_device_point` unit
// (Constraint 6 out-of-pane fallback) and a headless gateway pass with no shell.
#include <ace/app/canvas_view.hpp>
#include <ace/app/file_dialog.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/commands/image_import.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/cell.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/contract/content.hpp>
#include <arbc/contract/registry.hpp>
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
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "writer_session.hpp"

using ace::app::CanvasView;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::commands::AppState;
using ace::dockmodel::ViewType;
using ace::scene::DetailSource;

namespace {

constexpr int k_fixture_w = 12;
constexpr int k_fixture_h = 8;
const std::filesystem::path k_fixture = std::filesystem::path(ACE_FIXTURE_DIR) / "photo_12x8.ppm";

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_image_import_e2e_test") {
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
// The shipped file seam, scripted: `show` delivers synchronously, exactly as the export e2e's
// ScriptedFolderDialog does — no real OS file picker exists offscreen.
class ScriptedFileDialog final : public ace::app::FileDialog {
public:
  std::optional<std::filesystem::path> next;
  bool shown = false;
  void show(Callback on_pick) override {
    shown = true;
    on_pick(next);
  }
};

struct E2EState {
  ace::dock::Dockspace* dockspace;
  AppState* state;
  ace::app::AppProjectGateway* gateway;
  CanvasView* canvas;
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

} // namespace

// --- The pure drop-point mapping (Constraint 6) ------------------------------

TEST_CASE("image import drop mapping: in-pane maps to pane-local, out-of-pane falls back") {
  const std::optional<arbc::Rect> pane = arbc::Rect{100.0, 40.0, 500.0, 340.0}; // screen rect

  // A window point inside the pane maps to a pane-LOCAL device point.
  const std::optional<arbc::Vec2> inside =
      ace::app::drop_device_point(arbc::Vec2{160.0, 100.0}, pane);
  REQUIRE(inside.has_value());
  CHECK(inside->x == 60.0); // 160 - 100
  CHECK(inside->y == 60.0); // 100 - 40

  // A point outside the pane -> nullopt (the import verb turns it into the focused-centre
  // fallback rather than losing the drop).
  CHECK_FALSE(ace::app::drop_device_point(arbc::Vec2{10.0, 10.0}, pane).has_value());
  CHECK_FALSE(ace::app::drop_device_point(arbc::Vec2{600.0, 100.0}, pane).has_value());
  // A missing / empty pane rect -> nullopt.
  CHECK_FALSE(ace::app::drop_device_point(arbc::Vec2{160.0, 100.0}, std::nullopt).has_value());
  CHECK_FALSE(
      ace::app::drop_device_point(arbc::Vec2{0.0, 0.0}, std::optional<arbc::Rect>{arbc::Rect{}})
          .has_value());
}

// --- The gateway verb, headless (no shell, no canvas) ------------------------

TEST_CASE("image import gateway: insert_image mints a borrowed, selected, read-only image") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "gateway");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  state.document().add_composition(64.0, 64.0);

  ace::dockmodel::RecentProjects recent(scratch.root / "prefs", fs);
  NoopFolderDialog folder;
  NoopLauncher launcher;
  ace::app::AppProjectGateway gateway(recent, fs, folder, launcher, "/usr/bin/arbitraryeditor",
                                      state);
  // No FileDialog wired yet: the "Place image…" affordance is unavailable, and driving
  // place_image() with no picker installed is a graceful no-op (mints nothing).
  CHECK_FALSE(gateway.can_place_image());
  gateway.place_image();
  CHECK(ace::scene::cells(state.document(), state.registry()).empty());

  // A drop at an explicit pane-local device point (no view-framing provider -> the root
  // composition's own extent at identity, so comp == device point).
  const std::string err = gateway.insert_image(k_fixture, arbc::Vec2{20.0, 30.0});
  CHECK(err.empty());

  std::vector<ace::scene::Cell> cells = ace::scene::cells(state.document(), state.registry());
  REQUIRE(cells.size() == 1);
  CHECK(cells[0].kind_id == std::string(ace::commands::image_kind_id));
  CHECK(cells[0].detail.source == DetailSource::ReferencedImage);
  CHECK(cells[0].detail.borrowed);
  REQUIRE(cells[0].detail.native_pixels.has_value());
  CHECK(cells[0].detail.native_pixels->first == k_fixture_w);
  CHECK(cells[0].detail.native_pixels->second == k_fixture_h);
  // 1:1 native scale centred on the drop point (identity camera -> comp == device point).
  CHECK(cells[0].placement.a == 1.0);
  CHECK(cells[0].placement.tx == 20.0 - static_cast<double>(k_fixture_w) * 0.5);
  CHECK(cells[0].placement.ty == 30.0 - static_cast<double>(k_fixture_h) * 0.5);
  // The imported cell is SELECTED (the drop/Place UX).
  CHECK(state.selection().primary() == cells[0].id);
  // Read-only by construction: `org.arbc.image` overrides no editable() facet.
  arbc::Content* content = state.document().resolve(cells[0].id);
  REQUIRE(content != nullptr);
  CHECK(content->editable() == nullptr);

  // The "Place image…" path: wire the scripted picker; now the affordance is available and a
  // pick imports at the focused view centre (nullopt device point).
  ScriptedFileDialog picker;
  picker.next = k_fixture;
  gateway.set_file_dialog(picker);
  CHECK(gateway.can_place_image());
  gateway.place_image();
  CHECK(picker.shown);
  cells = ace::scene::cells(state.document(), state.registry());
  REQUIRE(cells.size() == 2);
  CHECK(cells[1].detail.source == DetailSource::ReferencedImage);
  CHECK(cells[1].detail.borrowed);
  CHECK(state.selection().primary() == cells[1].id);

  // A cancelled pick (nullopt) imports nothing.
  picker.next = std::nullopt;
  gateway.place_image();
  CHECK(ace::scene::cells(state.document(), state.registry()).size() == 2);
}

// --- The live shell e2e: drop + Place through the real gateway ---------------

TEST_CASE("image import e2e: a drop and a Place both mint a selected borrowed image") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  ace::testing::WriterSession session(scratch.root / "img");
  REQUIRE(session.ok());
  AppState& state = session.state();
  session.on_writer([&] { state.document().add_composition(64.0, 64.0); });
  REQUIRE(ace::scene::cells(state.document(), state.registry()).empty());

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 900;
  opts.height = 640;
  REQUIRE(shell.init(opts));

  CanvasView canvas(state, session.writer());
  // Before any pane has drawn/sized, there is no live pane rect — a drop then falls back to the
  // focused centre rather than being lost (focused_pane_rect's no-live-pane branch).
  CHECK_FALSE(canvas.focused_pane_rect().has_value());
  ace::views::register_view_body(ViewType::Canvas, [&canvas](std::string_view view_id) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content(view_id, static_cast<int>(avail.x), static_cast<int>(avail.y));
  });

  ace::dock::Dockspace dockspace;
  ace::dockmodel::RecentProjects recent(scratch.root / "prefs", fs);
  NoopFolderDialog folder;
  NoopLauncher launcher;
  ScriptedFileDialog picker;
  picker.next = k_fixture;
  ace::app::AppProjectGateway gateway(recent, fs, folder, launcher, "/usr/bin/arbitraryeditor",
                                      state);
  gateway.set_edit_runner(
      [&canvas](const std::function<void()>& edit) { canvas.apply_edit(edit); });
  gateway.set_view_framing([&canvas] { return canvas.focused_framing(); });
  gateway.set_file_dialog(picker);
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

  E2EState e2e{&dockspace, &state, &gateway, &canvas};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "image", "import");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    ace::dock::Dockspace& dockspace = *e2e->dockspace;
    AppState& state = *e2e->state;
    ace::app::AppProjectGateway& gateway = *e2e->gateway;
    CanvasView& canvas = *e2e->canvas;
    const std::string rail = ace::dock::tool_rail_title();

    // Let the canvas pane draw so it records its screen rect + framing.
    ctx->ItemClick((rail + "/###insert_cell").c_str()); // opens then we cancel — just to settle UI
    ctx->Yield(2);
    ctx->ItemClick("Insert Cell/###insert_cancel");
    ctx->Yield(2);

    // --- Drop path: the verb the SDL_EVENT_DROP_FILE arm calls -----------------
    // The focused pane's screen rect exists once the canvas has drawn (Constraint 6 mapping).
    const std::optional<arbc::Rect> pane = canvas.focused_pane_rect();
    IM_CHECK(pane.has_value());
    // A pane-local device drop point (as the shell would produce via drop_device_point).
    const arbc::Vec2 drop{40.0, 30.0};
    const std::string err = gateway.insert_image(k_fixture, drop);
    IM_CHECK(err.empty());
    IM_CHECK(pump_until(
        ctx, [&] { return ace::scene::cells(state.document(), state.registry()).size() == 1; }));
    ctx->Yield(2);

    std::vector<ace::scene::Cell> cells = ace::scene::cells(state.document(), state.registry());
    IM_CHECK(cells.size() == 1);
    IM_CHECK(cells[0].detail.source == DetailSource::ReferencedImage);
    IM_CHECK(cells[0].detail.borrowed);
    IM_CHECK(cells[0].detail.native_pixels.has_value());
    IM_CHECK(cells[0].detail.native_pixels->first == 12);
    IM_CHECK(cells[0].detail.native_pixels->second == 8);
    // Selected (drop/Place UX).
    IM_CHECK(state.selection().primary() == cells[0].id);
    // Read-only: no editable facet (the retouch-stack hook fires off this, a later leaf).
    arbc::Content* content = state.document().resolve(cells[0].id);
    IM_CHECK(content != nullptr);
    IM_CHECK(content->editable() == nullptr);
    // 1:1 at the drop point: placed composition extent == native px, centred on the drop point
    // mapped through the live camera.
    IM_CHECK(cells[0].placement.a == 1.0);
    IM_CHECK(cells[0].content_bounds.has_value());
    const arbc::Rect placed = cells[0].placement.map_rect(*cells[0].content_bounds);
    IM_CHECK(std::abs(placed.width() - 12.0) < 1e-6);
    IM_CHECK(std::abs(placed.height() - 8.0) < 1e-6);
    const arbc::Vec2 comp = canvas.focused_framing().camera.inverse()->apply(drop);
    IM_CHECK(std::abs((placed.x0 + placed.x1) * 0.5 - comp.x) < 1e-6);
    IM_CHECK(std::abs((placed.y0 + placed.y1) * 0.5 - comp.y) < 1e-6);

    // --- Place path: the "Place image…" affordance drives the FileDialog fake ---
    IM_CHECK(ctx->ItemExists((rail + "/###place_image").c_str()));
    ctx->ItemClick((rail + "/###place_image").c_str());
    IM_CHECK(pump_until(
        ctx, [&] { return ace::scene::cells(state.document(), state.registry()).size() == 2; }));
    ctx->Yield(2);
    cells = ace::scene::cells(state.document(), state.registry());
    IM_CHECK(cells.size() == 2);
    IM_CHECK(cells[1].detail.source == DetailSource::ReferencedImage);
    IM_CHECK(cells[1].detail.borrowed);
    IM_CHECK(state.selection().primary() == cells[1].id);
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
