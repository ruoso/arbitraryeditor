// editor.import.paste — the owned-image paste UI e2e (docs §9, the offscreen software-GL lane;
// modelled on tests/image_import_e2e_test.cpp). Exercises the ONE app paste verb
// `AppProjectGateway::paste_image` through both triggers: the Ctrl+V chord and the "Paste image"
// affordance, each reading a SCRIPTED `Clipboard` fake (no real OS clipboard exists offscreen).
// Asserts, on model state (never pixels): the minted cell is selected, ReferencedImage +
// OWNED (borrowed == false) with native_pixels == the fixture size, placed 1:1 at the focused-pane
// centre, and read-only; an empty/no-image clipboard is a graceful no-op (no cell, no error, no
// crash); and the Layers provenance readout shows "owned". Plus a headless gateway pass (no shell).
#include <ace/app/canvas_view.hpp>
#include <ace/app/clipboard.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/app/view_framing.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/commands/image_import.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/platform/result.hpp>
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
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_paste_image_e2e_test") {
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
// The shipped clipboard seam, scripted: `read_image` returns whatever `next` holds — the fixture
// bytes (an encoded image on the clipboard) or `nullopt` (an empty / raw-only clipboard). No real
// OS clipboard exists offscreen, exactly as ScriptedFileDialog stands in for the OS file picker.
class ScriptedClipboard final : public ace::app::Clipboard {
public:
  std::optional<ace::app::ClipboardImage> next;
  int reads = 0;
  std::optional<ace::app::ClipboardImage> read_image() override {
    ++reads;
    return next;
  }
};

// The fixture bytes as an encoded clipboard image (mime PNG — imdec sniffs the actual PPM bytes, so
// the mime only flavours the minted blob's extension).
ace::app::ClipboardImage fixture_clip(const ace::platform::FileSystem& fs) {
  ace::platform::Result<std::string> read = fs.read_file(k_fixture);
  REQUIRE(read.has_value());
  ace::app::ClipboardImage image;
  image.bytes = std::move(*read);
  image.mime = "image/png";
  return image;
}

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

// --- The gateway verb, headless (no shell, no canvas) ------------------------

TEST_CASE("paste image gateway: paste_image mints an owned, selected, read-only image") {
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
  // No clipboard wired yet: the "Paste image" affordance is unavailable, and driving paste_image()
  // with no clipboard installed is a graceful no-op (mints nothing).
  CHECK_FALSE(gateway.can_paste_image());
  gateway.paste_image();
  CHECK(ace::scene::cells(state.document(), state.registry()).empty());

  // Wire the scripted clipboard holding the fixture's encoded bytes; now the affordance is
  // available and a paste mints an OWNED image at the focused view centre (nullopt device point).
  ScriptedClipboard clipboard;
  clipboard.next = fixture_clip(fs);
  gateway.set_clipboard(clipboard);
  CHECK(gateway.can_paste_image());
  gateway.paste_image();
  CHECK(clipboard.reads == 1);

  std::vector<ace::scene::Cell> cells = ace::scene::cells(state.document(), state.registry());
  REQUIRE(cells.size() == 1);
  CHECK(cells[0].kind_id == std::string(ace::commands::image_kind_id));
  CHECK(cells[0].detail.source == DetailSource::ReferencedImage);
  CHECK_FALSE(cells[0].detail.borrowed); // OWNED — the project minted it (D11)
  REQUIRE(cells[0].detail.native_pixels.has_value());
  CHECK(cells[0].detail.native_pixels->first == k_fixture_w);
  CHECK(cells[0].detail.native_pixels->second == k_fixture_h);
  // 1:1 native scale (camera-independent, D12/§8).
  CHECK(cells[0].placement.a == 1.0);
  CHECK(cells[0].placement.d == 1.0);
  // The pasted cell is SELECTED (what you brought in is what is selected).
  CHECK(state.selection().primary() == cells[0].id);
  // Read-only by construction: `org.arbc.image` overrides no editable() facet.
  arbc::Content* content = state.document().resolve(cells[0].id);
  REQUIRE(content != nullptr);
  CHECK(content->editable() == nullptr);
  // Owned by URI shape: the authored reference is a project-relative `assets/` URI.
  CHECK(std::string_view(content->external_asset_ref()).rfind("assets/", 0) == 0);

  // An empty / no-image clipboard is a graceful NO-OP: no cell, no throw, the Document untouched.
  clipboard.next = std::nullopt;
  gateway.paste_image();
  CHECK(ace::scene::cells(state.document(), state.registry()).size() == 1);
}

// --- The live shell e2e: Ctrl+V + "Paste image" through the real gateway ------

TEST_CASE("paste image e2e: Ctrl+V and the affordance both mint a selected owned image") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  ace::testing::WriterSession session(scratch.root / "paste");
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
  ace::views::register_view_body(ViewType::Canvas, [&canvas](std::string_view view_id) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content(view_id, static_cast<int>(avail.x), static_cast<int>(avail.y));
  });

  ace::dock::Dockspace dockspace;
  ace::dockmodel::RecentProjects recent(scratch.root / "prefs", fs);
  NoopFolderDialog folder;
  NoopLauncher launcher;
  ScriptedClipboard clipboard;
  clipboard.next = fixture_clip(fs);
  ace::app::AppProjectGateway gateway(recent, fs, folder, launcher, "/usr/bin/arbitraryeditor",
                                      state);
  gateway.set_edit_runner(
      [&canvas](const std::function<void()>& edit) { canvas.apply_edit(edit); });
  gateway.set_view_framing([&canvas] { return canvas.focused_framing(); });
  gateway.set_clipboard(clipboard);
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
  ImGuiTest* test = IM_REGISTER_TEST(engine, "paste", "image");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    AppState& state = *e2e->state;
    CanvasView& canvas = *e2e->canvas;
    const std::string rail = ace::dock::tool_rail_title();

    // Let the canvas pane draw so it records its screen rect + framing.
    ctx->ItemClick((rail + "/###insert_cell").c_str()); // opens then we cancel — just to settle UI
    ctx->Yield(2);
    ctx->ItemClick("Insert Cell/###insert_cancel");
    ctx->Yield(2);

    // --- Ctrl+V chord: mints an OWNED image at the focused-pane centre ----------
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_V);
    IM_CHECK(pump_until(
        ctx, [&] { return ace::scene::cells(state.document(), state.registry()).size() == 1; }));
    ctx->Yield(2);

    std::vector<ace::scene::Cell> cells = ace::scene::cells(state.document(), state.registry());
    IM_CHECK(cells.size() == 1);
    IM_CHECK(cells[0].detail.source == DetailSource::ReferencedImage);
    IM_CHECK(!cells[0].detail.borrowed); // OWNED (the project minted it, D11)
    IM_CHECK(cells[0].detail.native_pixels.has_value());
    IM_CHECK(cells[0].detail.native_pixels->first == 12);
    IM_CHECK(cells[0].detail.native_pixels->second == 8);
    IM_CHECK(state.selection().primary() == cells[0].id);
    arbc::Content* content = state.document().resolve(cells[0].id);
    IM_CHECK(content != nullptr);
    IM_CHECK(content->editable() == nullptr); // read-only
    // 1:1 at the focused-pane centre: placed composition extent == native px, centred on the pane
    // centre mapped through the live camera.
    IM_CHECK(cells[0].placement.a == 1.0);
    IM_CHECK(cells[0].content_bounds.has_value());
    const arbc::Rect placed = cells[0].placement.map_rect(*cells[0].content_bounds);
    IM_CHECK(std::abs(placed.width() - 12.0) < 1e-6);
    IM_CHECK(std::abs(placed.height() - 8.0) < 1e-6);
    const ace::app::ViewFraming framing = canvas.focused_framing();
    const arbc::Vec2 centre{static_cast<double>(framing.pane_w) * 0.5,
                            static_cast<double>(framing.pane_h) * 0.5};
    const arbc::Vec2 comp = framing.camera.inverse()->apply(centre);
    IM_CHECK(std::abs((placed.x0 + placed.x1) * 0.5 - comp.x) < 1e-6);
    IM_CHECK(std::abs((placed.y0 + placed.y1) * 0.5 - comp.y) < 1e-6);
    // Owned PROVENANCE surfaces: `borrowed == false` is exactly the value the shipped Layers
    // `provenance_tag` maps to "owned" (vs "borrowed"), so the readout shows owned for the paste.
    IM_CHECK(!cells[0].detail.borrowed);

    // --- "Paste image" affordance: same verb, a second owned cell --------------
    IM_CHECK(ctx->ItemExists((rail + "/###paste_image").c_str()));
    ctx->ItemClick((rail + "/###paste_image").c_str());
    IM_CHECK(pump_until(
        ctx, [&] { return ace::scene::cells(state.document(), state.registry()).size() == 2; }));
    ctx->Yield(2);
    cells = ace::scene::cells(state.document(), state.registry());
    IM_CHECK(cells.size() == 2);
    IM_CHECK(cells[1].detail.source == DetailSource::ReferencedImage);
    IM_CHECK(!cells[1].detail.borrowed);
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
