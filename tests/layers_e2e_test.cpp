// editor.panels.layers — the Layers panel UI e2e (docs §9, the offscreen software-GL lane; modeled
// on tests/inspector_e2e_test.cpp). Boots the shell over a REAL commands::AppState, seeds a solid
// background + painted/referenced/nested cells + a shot camera, and drives the LayersPanel body
// (the same body the shell registers on ViewType::Layers) by widget id under a standalone "Layers"
// window. Asserts: two sections render (cameras + front→back layers, no camera in the layers
// section); a row select round-trips (Replace / Shift=Add / Cmd-Ctrl=Toggle, cell AND camera);
// distinct painted-vs-referenced provenance affordances; a drag reorders membership + undo
// restores; the disclosure triangle expands (peek children) WITHOUT changing selection; a
// double-click ENTERS (breadcrumb Root ▸ child, re-rooted list) and a crumb climbs out, arming
// neither undo nor Save; and empty states. This is also the FIRST camera e2e (cameras/model.md's
// parked case). Writer ops run on the MAIN thread through apply_edit.
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/layers_panel.hpp>
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
#include <arbc/contract/content.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp> // arbc::KindBridge

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "writer_session.hpp"

using ace::app::CanvasView;
using ace::app::LayersPanel;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::commands::AppState;
using ace::dockmodel::ViewType;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  explicit ScratchDir(const char* tag)
      : root(std::filesystem::temp_directory_path() / (std::string("ace_layers_e2e_") + tag)) {
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

// A referenced-image-shaped kind (external_asset_ref non-empty, editable null) so a row classifies
// ReferencedImage + borrowed through the generic facet — distinct provenance from a painted raster.
class ExternalRefProbe final : public arbc::Content {
public:
  std::optional<arbc::Rect> bounds() const override { return arbc::Rect{0.0, 0.0, 48.0, 48.0}; }
  arbc::Stability stability() const override { return arbc::Stability::Static; }
  std::optional<arbc::TimeRange> time_extent() const override { return std::nullopt; }
  std::optional<arbc::RenderResult> render(const arbc::RenderRequest&,
                                           std::shared_ptr<arbc::RenderCompletion>) override {
    return std::nullopt;
  }
  std::string_view external_asset_ref() const override { return d_uri; }

private:
  std::string d_uri = "file:///borrowed/photo.png";
};

constexpr const char* k_external_probe_kind = "org.example.external_probe";

// Attach a cell into an ARBITRARY composition (add_cell only targets root) — the plumbing the
// scoped-edit follow-up will fold into the verbs. WRITER-THREAD ONLY (called inside on_writer).
arbc::ObjectId attach_into(arbc::Document& document, const arbc::Registry& registry,
                           arbc::ObjectId composition, std::string_view kind_id,
                           std::string_view config, const arbc::Affine& placement) {
  const arbc::ContentFactory* factory = registry.factory(kind_id);
  arbc::expected<std::unique_ptr<arbc::Content>, std::string> made = (*factory)(config);
  arbc::KindBridge bridge;
  ace::project::seed_kind_bridge(bridge, registry);
  const arbc::KindMetadata* metadata = registry.metadata(kind_id);
  const std::uint64_t token = bridge.intern(
      kind_id, metadata != nullptr ? std::string_view(metadata->version) : std::string_view{});
  const arbc::Document::Placed placed = document.create_content_and_attach(
      std::shared_ptr<arbc::Content>(std::move(*made)), token, composition, placement);
  return placed.content;
}

std::vector<arbc::ObjectId> layer_ids(AppState& state) {
  std::vector<arbc::ObjectId> ids;
  for (const ace::scene::Cell& c : ace::scene::cells(state.document(), state.registry())) {
    ids.push_back(c.id);
  }
  return ids;
}

struct E2EState {
  CanvasView* canvas;
  AppState* state;
  arbc::ObjectId bg;
  arbc::ObjectId painted;
  arbc::ObjectId referenced;
  arbc::ObjectId nested;
  arbc::ObjectId child_cell;
  arbc::ObjectId empty_nested;
  arbc::ObjectId root_comp;
  arbc::ObjectId child_comp;
  arbc::ObjectId empty_comp;
  arbc::ObjectId camera;
  std::atomic<bool> request_undo{false};
  std::atomic<bool> undo_done{false};
};

std::string ref_layer(arbc::ObjectId id) {
  return "Layers/###layer_row_" + std::to_string(id.value);
}
std::string ref_camera(arbc::ObjectId id) {
  return "Layers/###camera_row_" + std::to_string(id.value);
}
std::string ref_expand(arbc::ObjectId id) {
  return "Layers/###layer_expand_" + std::to_string(id.value);
}
std::string ref_peek(arbc::ObjectId id) { return "Layers/###peek_row_" + std::to_string(id.value); }
std::string ref_crumb(arbc::ObjectId id) { return "Layers/###crumb_" + std::to_string(id.value); }

} // namespace

TEST_CASE("layers e2e: sections, select, reorder, expand, enter/breadcrumb, empty state") {
  ScratchDir scratch("main");
  ace::platform::NativeFileSystem fs;
  ace::testing::WriterSession session(scratch.root / "lay");
  REQUIRE(session.ok());
  AppState& state = session.state();

  // The referenced-image kind is one the editor's registry does not ship; register it before any
  // write/read touches the registry (the writer is idle until on_writer below).
  arbc::ContentFactory external =
      [](arbc::ContentConfig) -> arbc::expected<std::unique_ptr<arbc::Content>, std::string> {
    return std::unique_ptr<arbc::Content>(std::make_unique<ExternalRefProbe>());
  };
  (void)state.registry().add(k_external_probe_kind, std::move(external),
                             arbc::KindMetadata{"External Probe", "1"});

  E2EState e2e{};
  e2e.state = &state;
  session.on_writer([&] {
    arbc::Document& doc = state.document();
    const arbc::Registry& reg = state.registry();
    doc.add_composition(512.0, 512.0);
    // A solid bottom cell (the reorder drag's target row). This panel e2e drives the standalone
    // Layers window only and does NOT render a canvas, so a nested cell in root is never composited
    // by a render worker (which would require the canvas's nested-composition DocumentBinding).
    const auto bg =
        ace::scene::add_cell(doc, reg, "org.arbc.solid", "0.1,0.2,0.3,1", arbc::Affine::identity());
    const auto painted = ace::scene::add_cell(doc, reg, "org.arbc.raster", "32x32",
                                              arbc::Affine::translation(10.0, 10.0));
    const auto referenced = ace::scene::add_cell(doc, reg, k_external_probe_kind, "",
                                                 arbc::Affine::translation(20, 20));
    REQUIRE(bg.has_value());
    REQUIRE(painted.has_value());
    REQUIRE(referenced.has_value());
    e2e.bg = *bg;
    e2e.painted = *painted;
    e2e.referenced = *referenced;

    // A nested cell wrapping a child composition that has one cell of its own.
    e2e.child_comp = doc.add_composition(64.0, 64.0);
    e2e.child_cell =
        attach_into(doc, reg, e2e.child_comp, "org.arbc.raster", "16x16", arbc::Affine::identity());
    const auto nested =
        ace::scene::add_cell(doc, reg, "org.arbc.nested", std::to_string(e2e.child_comp.value),
                             arbc::Affine::translation(30.0, 30.0));
    REQUIRE(nested.has_value());
    e2e.nested = *nested;

    // A second nested cell wrapping an EMPTY child composition (the empty-state case).
    e2e.empty_comp = doc.add_composition(64.0, 64.0);
    const auto empty_nested =
        ace::scene::add_cell(doc, reg, "org.arbc.nested", std::to_string(e2e.empty_comp.value),
                             arbc::Affine::translation(40.0, 40.0));
    REQUIRE(empty_nested.has_value());
    e2e.empty_nested = *empty_nested;

    e2e.camera = ace::scene::add_camera(doc, reg, "Hero", ace::scene::Resolution{128, 128},
                                        arbc::Affine::scaling(0.5, 0.5));
  });
  REQUIRE(e2e.painted.valid());
  REQUIRE(e2e.nested.valid());
  REQUIRE(e2e.camera.valid());
  e2e.root_comp = ace::scene::active_composition(state.document(), std::nullopt);
  REQUIRE(e2e.root_comp.valid());

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 900;
  opts.height = 640;
  REQUIRE(shell.init(opts));

  // The CanvasView is the edit runner (its apply_edit posts to the one writer thread and pokes the
  // — here empty — canvas host); no canvas body is registered, so no worker renders the document.
  CanvasView canvas(state, session.writer());
  e2e.canvas = &canvas;
  LayersPanel layers(state, canvas);

  ace::dock::Dockspace dockspace;
  ace::dockmodel::RecentProjects recent(scratch.root / "prefs", fs);
  NoopFolderDialog dialog;
  NoopLauncher launcher;
  ace::app::AppProjectGateway gateway(recent, fs, dialog, launcher, "/usr/bin/arbitraryeditor",
                                      state);
  gateway.set_edit_runner(
      [&canvas](const std::function<void()>& edit) { canvas.apply_edit(edit); });

  shell.set_draw_content([&dockspace, &layers]() {
    dockspace.draw();
    ImGui::Begin("Layers");
    layers.draw("layers");
    ImGui::End();
  });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  ImGuiTest* test = IM_REGISTER_TEST(engine, "layers", "sections_select_reorder_expand_enter");
  test->UserData = &e2e;

  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    AppState& state = *e2e->state;
    const arbc::Journal& journal = state.document().journal();

    ctx->WindowFocus("Layers");
    ctx->Yield(3);

    // --- (i) Two sections render; no camera in the layers section (A14). ----------------------
    IM_CHECK(ctx->ItemExists(ref_camera(e2e->camera).c_str()));
    IM_CHECK(ctx->ItemExists(ref_layer(e2e->painted).c_str()));
    IM_CHECK(ctx->ItemExists(ref_layer(e2e->referenced).c_str()));
    IM_CHECK(!ctx->ItemExists(ref_layer(e2e->camera).c_str())); // camera is not a layer row

    // --- (ii) Row select round-trips: Replace / Shift=Add / Cmd-Ctrl=Toggle, cell AND camera. -
    ctx->ItemClick(ref_layer(e2e->painted).c_str());
    ctx->Yield(2);
    IM_CHECK(state.selection().primary() == e2e->painted);
    ctx->ItemClick(ref_camera(e2e->camera).c_str());
    ctx->Yield(2);
    IM_CHECK(state.selection().primary() == e2e->camera); // a camera selects like a cell (D7)

    ctx->KeyDown(ImGuiMod_Shift);
    ctx->ItemClick(ref_layer(e2e->painted).c_str());
    ctx->KeyUp(ImGuiMod_Shift);
    ctx->Yield(2);
    IM_CHECK(state.selection().size() == 2); // Shift ADDED the cell to the camera selection
    IM_CHECK(state.selection().contains(e2e->camera));
    IM_CHECK(state.selection().contains(e2e->painted));

    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick(ref_layer(e2e->painted).c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
    ctx->Yield(2);
    IM_CHECK(!state.selection().contains(e2e->painted)); // Cmd/Ctrl TOGGLED it back off
    IM_CHECK(state.selection().contains(e2e->camera));

    // --- (iii) Distinct painted-vs-referenced provenance affordances (D11). -------------------
    const ImGuiTestItemInfo painted_info = ctx->ItemInfo(ref_layer(e2e->painted).c_str());
    const ImGuiTestItemInfo ref_info = ctx->ItemInfo(ref_layer(e2e->referenced).c_str());
    IM_CHECK(std::strncmp(painted_info.DebugLabel, "[painted", 8) == 0);
    IM_CHECK(std::strncmp(ref_info.DebugLabel, "[referenced", 11) == 0);

    // --- (iv) Drag-reorder round-trips: membership changes, undo restores (one press). --------
    const std::vector<arbc::ObjectId> before = layer_ids(state);
    const std::size_t cursor_before = journal.cursor();
    ctx->ItemDragAndDrop(ref_layer(e2e->painted).c_str(), ref_layer(e2e->bg).c_str());
    ctx->Yield(4);
    const std::vector<arbc::ObjectId> after = layer_ids(state);
    IM_CHECK(after != before);                       // the membership order changed
    IM_CHECK(journal.cursor() == cursor_before + 1); // ONE undo step
    IM_CHECK(after.front() == e2e->painted);         // painted dragged to the bottom of z
    e2e->request_undo.store(true);
    IM_CHECK(pump_until(ctx, [&] { return e2e->undo_done.load(); }));
    e2e->undo_done.store(false);
    ctx->Yield(2);
    IM_CHECK(layer_ids(state) == before); // one-press undo restored the order

    // --- (v) Expand ≠ select: the triangle reveals children read-only, selection unchanged. ---
    state.selection().select(e2e->painted);
    ctx->Yield(2);
    IM_CHECK(!ctx->ItemExists(ref_peek(e2e->child_cell).c_str())); // not yet expanded
    ctx->ItemClick(ref_expand(e2e->nested).c_str());
    ctx->Yield(2);
    IM_CHECK(ctx->ItemExists(ref_peek(e2e->child_cell).c_str())); // children revealed
    IM_CHECK(state.selection().primary() == e2e->painted);        // expand did NOT change selection
    ctx->ItemClick(ref_expand(e2e->nested).c_str());              // collapse
    ctx->Yield(2);
    IM_CHECK(!ctx->ItemExists(ref_peek(e2e->child_cell).c_str()));
    // Expanding a nested row whose child composition is EMPTY peeks a defined "(empty)" state and
    // reveals no child rows (still selection-neutral).
    ctx->ItemClick(ref_expand(e2e->empty_nested).c_str());
    ctx->Yield(2);
    IM_CHECK(state.selection().primary() == e2e->painted);
    ctx->ItemClick(ref_expand(e2e->empty_nested).c_str()); // collapse again
    ctx->Yield(2);

    // --- (vi) Enter + breadcrumb + climb, arming neither undo nor Save. -----------------------
    const std::size_t cursor_pre_enter = journal.cursor();
    const bool dirty_pre_enter = state.is_dirty();
    ctx->ItemDoubleClick(ref_layer(e2e->nested).c_str());
    ctx->Yield(3);
    IM_CHECK(ctx->ItemExists(ref_crumb(e2e->root_comp).c_str()));  // Root crumb
    IM_CHECK(ctx->ItemExists(ref_crumb(e2e->child_comp).c_str())); // child crumb
    IM_CHECK(ctx->ItemExists(ref_layer(e2e->child_cell).c_str())); // list re-rooted to the child
    IM_CHECK(!ctx->ItemExists(ref_layer(e2e->painted).c_str()));   // root cells no longer shown
    IM_CHECK(journal.cursor() == cursor_pre_enter);                // entering is not a transaction
    IM_CHECK(state.is_dirty() == dirty_pre_enter);                 // and does not dirty the session

    ctx->ItemClick(ref_crumb(e2e->root_comp).c_str()); // climb out to Root
    ctx->Yield(3);
    IM_CHECK(ctx->ItemExists(ref_layer(e2e->painted).c_str()));     // root cells shown again
    IM_CHECK(!ctx->ItemExists(ref_layer(e2e->child_cell).c_str())); // child cells gone

    // --- (vii) Empty composition -> a defined empty state with no stale rows. -----------------
    ctx->ItemDoubleClick(ref_layer(e2e->empty_nested).c_str());
    ctx->Yield(3);
    IM_CHECK(ctx->ItemExists("Layers/###layers_empty"));         // the empty state
    IM_CHECK(!ctx->ItemExists(ref_layer(e2e->painted).c_str())); // no stale root rows
    IM_CHECK(!ctx->ItemExists(ref_layer(e2e->child_cell).c_str()));
    ctx->ItemClick(ref_crumb(e2e->root_comp).c_str()); // back to Root for a clean exit
    ctx->Yield(2);
  };
  ImGuiTestEngine_QueueTest(engine, test);

  const int k_max_frames = 200000;
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
