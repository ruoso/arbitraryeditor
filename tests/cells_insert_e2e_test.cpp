// editor.cells.model — the rail's "Insert Cell…" modal UI e2e (docs §9, the offscreen
// software-GL lane; modelled on tests/gc_ui_e2e_test.cpp + tests/history_e2e_test.cpp).
// Drives the REAL `app::AppProjectGateway` over a REAL `commands::AppState` on a
// ScratchDir project, with a live `CanvasView` so the insert takes the shipped path:
// `apply_edit` -> `commands::dispatch` -> `scene::add_cell`, at a placement computed
// from the canvas's own transient framing.
//
// Asserts, by stable widget id: (i) the kind list length EQUALS
// `registry.ids().size()` — the no-allowlist property at the UI layer; (ii) selecting
// `org.arbc.raster` presents a prefilled `size` field, and a typed resolution inserts
// a cell attached to the root composition at a non-identity provisional placement;
// (iii) selecting `org.arbc.fade` — whose factory always refuses — leaves the modal
// OPEN with the kind's own error rendered inline and nothing inserted; (iv) Cancel
// mutates nothing. Assertions are on model state, never pixels (software-GL pixels
// are flaky, per the save_ui/gc_ui precedent).
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
#include <ace/scene/cell.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/transform.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

using ace::app::CanvasView;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::commands::AppState;
using ace::dockmodel::ViewType;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_cells_insert_e2e_test") {
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

// TestFunc is a plain function pointer (std::function is disabled in this build), so
// the collaborators are threaded through UserData rather than captured.
struct E2EState {
  ace::dock::Dockspace* dockspace;
  AppState* state;
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

TEST_CASE("cells insert e2e: registry-length kind list, prefilled resolution, inline kind error") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "cells");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  // A root composition to place cells in (a fresh create_project document is empty).
  state.document().add_composition(64.0, 64.0);
  REQUIRE(ace::scene::cells(state.document(), state.registry()).empty());

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
  ace::app::AppProjectGateway gateway(recent, fs, dialog, launcher, "/usr/bin/arbitraryeditor",
                                      state);
  // The shipped wiring (shell.cpp): every insert runs inside CanvasHost::apply_edit
  // (Constraint 5) at a placement computed from the live canvas framing (Constraint 7).
  gateway.set_edit_runner(
      [&canvas](const std::function<void()>& edit) { canvas.apply_edit(edit); });
  gateway.set_view_framing([&canvas] { return canvas.primary_framing(); });
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

  E2EState e2e{&dockspace, &state};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "cells", "insert_modal");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    ace::dock::Dockspace& dockspace = *e2e->dockspace;
    AppState& state = *e2e->state;
    const std::string rail = ace::dock::tool_rail_title();

    // --- (i) Open the modal: the kind list is the WHOLE registry --------------
    ctx->ItemClick((rail + "/###insert_cell").c_str());
    ctx->Yield(2);
    IM_CHECK(dockspace.insert_modal_open());
    // The no-allowlist assertion at the UI layer: one row per registered kind.
    IM_CHECK(dockspace.insert_kinds().size() == state.registry().ids().size());
    IM_CHECK(dockspace.insert_kinds().size() >= 6); // the six built-ins at minimum
    IM_CHECK(ctx->ItemExists(kind_row(dockspace, "org.arbc.raster").c_str()));
    // A kind whose factory ALWAYS refuses is still offered (Constraint 3).
    IM_CHECK(ctx->ItemExists(kind_row(dockspace, "org.arbc.fade").c_str()));

    // --- (ii) Raster: a prefilled, editable resolution inserts a cell --------
    ctx->ItemClick(kind_row(dockspace, "org.arbc.raster").c_str());
    ctx->Yield(2);
    // The resolution field is present and PREFILLED from the root composition
    // (Constraint 8) — never blank, never silently applied.
    IM_CHECK(ctx->ItemExists("Insert Cell/###insert_field0"));
    IM_CHECK(dockspace.insert_field_value(0) == "64x64");

    ctx->ItemInputValue("Insert Cell/###insert_field0", "48x24");
    ctx->Yield(2);
    IM_CHECK(dockspace.insert_field_value(0) == "48x24");
    ctx->ItemClick("Insert Cell/###insert_confirm");
    IM_CHECK(pump_until(ctx, [&] { return !dockspace.insert_modal_open(); }));
    ctx->Yield(2);

    // The cell is attached to the ROOT composition (that is what `cells()` walks),
    // named by kind through the KindBridge, at a computed provisional placement.
    const std::vector<ace::scene::Cell> after =
        ace::scene::cells(state.document(), state.registry());
    IM_CHECK(after.size() == 1);
    IM_CHECK(after[0].kind_id == "org.arbc.raster");
    IM_CHECK(!(after[0].placement == arbc::Affine::identity())); // place_in_view ran
    IM_CHECK(after[0].placement.a > 0.0);
    IM_CHECK(dockspace.insert_error().empty());

    // --- (iii) A refusing kind: the modal STAYS OPEN with its own error -------
    ctx->ItemClick((rail + "/###insert_cell").c_str());
    ctx->Yield(2);
    ctx->ItemClick(kind_row(dockspace, "org.arbc.fade").c_str());
    ctx->Yield(2);
    ctx->ItemClick("Insert Cell/###insert_confirm");
    ctx->Yield(3);
    IM_CHECK(dockspace.insert_modal_open()); // errors are values — the modal persists
    IM_CHECK(!dockspace.insert_error().empty());
    IM_CHECK(dockspace.insert_error().find("org.arbc.fade") != std::string::npos);
    IM_CHECK(ace::scene::cells(state.document(), state.registry()).size() == 1);

    // --- (iv) Cancel mutates nothing -----------------------------------------
    ctx->ItemClick(kind_row(dockspace, "org.arbc.solid").c_str());
    ctx->Yield(2);
    ctx->ItemClick("Insert Cell/###insert_cancel");
    IM_CHECK(pump_until(ctx, [&] { return !dockspace.insert_modal_open(); }));
    ctx->Yield(2);
    IM_CHECK(ace::scene::cells(state.document(), state.registry()).size() == 1);
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

// The L4 gateway seam on its own, with NO shell and NO canvas — the headless half of
// the same path (and the branch a session takes before its Canvas pane is first
// sized): the framing falls back to the root composition's own extent.
TEST_CASE("cells insert gateway: registry-driven kinds, composition-framed fallback placement") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "gateway");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  state.document().add_composition(64.0, 64.0);

  ace::dockmodel::RecentProjects recent(scratch.root / "prefs", fs);
  NoopFolderDialog dialog;
  NoopLauncher launcher;
  ace::app::AppProjectGateway gateway(recent, fs, dialog, launcher, "/usr/bin/arbitraryeditor",
                                      state);
  // Deliberately no set_view_framing / set_edit_runner: the direct-invoke defaults.

  const std::vector<ace::dock::InsertKindSpec> kinds = gateway.insert_kinds();
  CHECK(kinds.size() == state.registry().ids().size());
  const ace::dock::InsertKindSpec* raster = nullptr;
  for (const ace::dock::InsertKindSpec& kind : kinds) {
    if (kind.kind_id == "org.arbc.raster") {
      raster = &kind;
    }
  }
  REQUIRE(raster != nullptr);
  REQUIRE(raster->fields.size() == 1);
  CHECK(raster->fields[0].id == "size");
  CHECK(raster->fields[0].initial == "64x64"); // prefilled from the composition

  // A good insert: the placement is computed from the composition's own extent, so a
  // 32x16 raster is centred and scaled rather than dumped at the origin.
  CHECK(gateway.insert_cell("org.arbc.raster", {{"size", "32x16"}}).empty());
  const std::vector<ace::scene::Cell> cells = ace::scene::cells(state.document(), state.registry());
  REQUIRE(cells.size() == 1);
  CHECK(cells[0].kind_id == "org.arbc.raster");
  CHECK(cells[0].placement.a == 1.0); // 32 units wide -> half of the 64-unit short edge
  CHECK(cells[0].placement.tx == 16.0);
  CHECK(cells[0].placement.ty == 24.0);

  // Every refusal is a value, and none of them mutates the document.
  CHECK(gateway.insert_cell("org.example.nope", {}).find("not a registered kind") !=
        std::string::npos);
  CHECK(gateway.insert_cell("org.arbc.raster", {{"size", "bad"}}).find("org.arbc.raster") !=
        std::string::npos);
  CHECK(gateway.insert_cell("org.arbc.fade", {{"config", ""}}).find("org.arbc.fade") !=
        std::string::npos);
  CHECK(ace::scene::cells(state.document(), state.registry()).size() == 1);
}

// The dockspace's Insert-modal state on its own (no ImGui context needed): the field
// buffers re-seed on a kind switch, and every out-of-range index is inert.
TEST_CASE("cells insert modal state: buffers re-seed per kind and out-of-range is inert") {
  ace::dock::Dockspace dockspace;
  CHECK_FALSE(dockspace.insert_modal_open());

  std::vector<ace::dock::InsertKindSpec> kinds;
  kinds.push_back(ace::dock::InsertKindSpec{
      "org.arbc.raster", "Raster", {ace::dock::InsertFieldSpec{"size", "Resolution", "64x64"}}});
  kinds.push_back(ace::dock::InsertKindSpec{"org.example.probe", "Probe", {}});
  dockspace.open_insert_modal(kinds);

  CHECK(dockspace.insert_modal_open());
  CHECK(dockspace.insert_kinds().size() == 2);
  CHECK(dockspace.insert_selected_kind() == 0);
  CHECK(dockspace.insert_field_value(0) == "64x64"); // seeded from the prefill
  // The buffer is editable in place, exactly as ImGui::InputText writes it.
  dockspace.insert_field_buffer(0)[0] = '3';
  CHECK(dockspace.insert_field_value(0) == "34x64");

  // Switching kinds discards the buffers of the previous one.
  dockspace.select_insert_kind(1);
  CHECK(dockspace.insert_selected_kind() == 1);
  CHECK(dockspace.insert_field_value(0).empty());

  // Out-of-range indices never fault: a scratch buffer and an empty value.
  dockspace.select_insert_kind(99);
  CHECK(dockspace.insert_field_value(0).empty());
  REQUIRE(dockspace.insert_field_buffer(7) != nullptr);
  CHECK(dockspace.insert_field_buffer(7)[0] == '\0');

  dockspace.insert_error() = "boom";
  dockspace.select_insert_kind(0);
  CHECK(dockspace.insert_error().empty()); // a kind switch clears the stale message
  dockspace.close_insert_modal();
  CHECK_FALSE(dockspace.insert_modal_open());
}
