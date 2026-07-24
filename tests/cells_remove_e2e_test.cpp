// editor.cells.remove — the Delete affordances' UI e2e (docs §9, the offscreen software-GL
// lane; modelled on tests/cells_insert_e2e_test.cpp + tests/undo_ui_e2e_test.cpp). Drives the
// REAL `app::AppProjectGateway` over a REAL `commands::AppState` on a ScratchDir project, with
// a live `CanvasView`, so every delete takes the shipped path: the rail item / the chord ->
// `ProjectGateway` -> `apply_edit` -> `commands::dispatch` -> `scene::remove_cell` ->
// `arbc::Document::remove_content`.
//
// Asserts, by stable widget id and on MODEL state (never pixels — software-GL pixels are
// flaky, per the save_ui/gc_ui precedent): the rail's `###delete_selected` is DISABLED with an
// empty selection and enabled once something is selected; clicking it removes exactly that
// object and empties the selection; the `Delete` and `Backspace` chords do the same; Ctrl+Z
// restores the last-deleted object without restoring the selection; the text-input guard
// (Constraint 9) — a `Delete`/`Backspace` typed into the Insert Cell modal's field edits the
// FIELD and deletes nothing; and deleting the camera a canvas is looking through leaves the
// pane rendering through the free viewport rather than a stale frame or a crash.
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
#include <arbc/base/ids.hpp>
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
#include <cstdint>
#include <filesystem>
#include <functional>
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

namespace {

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_cells_remove_e2e_test") {
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
  arbc::ObjectId camera;
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

TEST_CASE("cells remove e2e: the rail action, the Delete/Backspace chords, undo, and the "
          "text-input guard") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  // The writer identity, bound before the document exists and stopped after the canvas
  // is gone (editor.canvas.writer_thread; see tests/writer_session.hpp).
  ace::testing::WriterSession session(scratch.root / "cells");
  REQUIRE(session.ok());
  AppState& state = session.state();
  // Fixture seeding IS a document write: post it to the identity the open just bound
  // (editor.canvas.writer_thread D-1). Assertions stay on this thread.
  session.on_writer([&] { state.document().add_composition(64.0, 64.0); });

  // Three cells to delete one way each, plus a camera for the look-through fail-safe. They
  // are OPAQUE solids rather than (transparent) rasters so the composite is never blank —
  // the canvas's content gate withholds an all-transparent frame, and the look-through
  // assertion below needs the pane to be genuinely issuing frames.
  const char* const k_colors[3] = {"0.6,0,0,1", "0,0.6,0,1", "0,0,0.6,1"};
  for (int i = 0; i < 3; ++i) {
    arbc::expected<arbc::ObjectId, std::string> added = arbc::unexpected<std::string>("unset");
    session.on_writer([&] {
      added =
          ace::scene::add_cell(state.document(), state.registry(), "org.arbc.solid", k_colors[i],
                               arbc::Affine::translation(static_cast<double>(i) * 10.0, 0.0));
    });
    REQUIRE(added.has_value());
  }
  arbc::ObjectId camera;
  session.on_writer([&] {
    camera =
        ace::scene::add_camera(state.document(), state.registry(), "shot",
                               ace::scene::Resolution{32, 24}, arbc::Affine::translation(4.0, 4.0));
  });
  REQUIRE(camera.valid());
  REQUIRE(ace::scene::cells(state.document(), state.registry()).size() == 3);

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
  NoopFolderDialog dialog;
  NoopLauncher launcher;
  ace::app::AppProjectGateway gateway(recent, fs, dialog, launcher, "/usr/bin/arbitraryeditor",
                                      state);
  // The shipped wiring (shell.cpp): every delete runs inside CanvasHost::apply_edit
  // (Constraint 4) — `Document::remove_content` is WRITER-THREAD ONLY.
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

  E2EState e2e{&dockspace, &state, &canvas, camera};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "cells", "delete_rail_and_chord");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    ace::dock::Dockspace& dockspace = *e2e->dockspace;
    AppState& state = *e2e->state;
    CanvasView& canvas = *e2e->canvas;
    const std::string rail = ace::dock::tool_rail_title();
    const std::string delete_item = rail + "/###delete_selected";
    const auto cell_ids = [&] { return ace::scene::cells(state.document(), state.registry()); };
    ctx->Yield(2);
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));

    // --- (i) The rail item is DISABLED with an empty selection, enabled with one ------
    IM_CHECK(state.selection().empty());
    IM_CHECK(ctx->ItemExists(delete_item.c_str())); // disabled, NOT hidden (D-cells_remove-6)
    IM_CHECK((ctx->ItemInfo(delete_item.c_str()).ItemFlags & ImGuiItemFlags_Disabled) != 0);

    const std::vector<ace::scene::Cell> initial = cell_ids();
    IM_CHECK(initial.size() == 3);
    state.selection().select(initial[0].id);
    ctx->Yield(2);
    IM_CHECK((ctx->ItemInfo(delete_item.c_str()).ItemFlags & ImGuiItemFlags_Disabled) == 0);

    // --- (ii) Clicking it removes exactly that cell and empties the selection ---------
    const arbc::ObjectId first = initial[0].id;
    ctx->ItemClick(delete_item.c_str());
    IM_CHECK(pump_until(ctx, [&] { return cell_ids().size() == 2; }));
    for (const ace::scene::Cell& cell : cell_ids()) {
      IM_CHECK(cell.id != first);
    }
    IM_CHECK(state.selection().empty());
    IM_CHECK((ctx->ItemInfo(delete_item.c_str()).ItemFlags & ImGuiItemFlags_Disabled) != 0);

    // --- (iii) The `Delete` chord, then `Backspace` (D-cells_remove-5) ----------------
    const arbc::ObjectId second = cell_ids()[0].id;
    state.selection().select(second);
    ctx->Yield(2);
    ctx->KeyPress(ImGuiKey_Delete);
    IM_CHECK(pump_until(ctx, [&] { return cell_ids().size() == 1; }));
    IM_CHECK(state.selection().empty());

    const arbc::ObjectId third = cell_ids()[0].id;
    state.selection().select(third);
    ctx->Yield(2);
    ctx->KeyPress(ImGuiKey_Backspace); // macOS's Delete key reports as Backspace
    IM_CHECK(pump_until(ctx, [&] { return cell_ids().empty(); }));
    IM_CHECK(state.selection().empty());

    // --- (iv) Ctrl+Z restores the last-deleted object; the selection stays empty ------
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    IM_CHECK(pump_until(ctx, [&] { return cell_ids().size() == 1; }));
    IM_CHECK(cell_ids()[0].id == third); // the SAME ObjectId (D15)
    IM_CHECK(state.selection().empty()); // a restore does NOT restore the selection

    // --- (v) The text-input guard (Constraint 9) -------------------------------------
    // With the Insert Cell modal open and the cursor in its config field, Delete and
    // Backspace belong to the FIELD. This is the sharpest regression this leaf can ship.
    const std::size_t cells_before_typing = cell_ids().size();
    state.selection().select(cell_ids()[0].id); // something IS deletable — not vacuous
    ctx->ItemClick((rail + "/###insert_cell").c_str());
    ctx->Yield(2);
    IM_CHECK(dockspace.insert_modal_open());
    ctx->ItemClick(kind_row(dockspace, "org.arbc.raster").c_str());
    ctx->Yield(2);
    IM_CHECK(dockspace.insert_field_value(0) == "64x64");
    ctx->ItemClick("Insert Cell/###insert_field0");
    ctx->Yield(2);
    ctx->KeyPress(ImGuiKey_End);
    ctx->KeyPress(ImGuiKey_Backspace);
    ctx->KeyPress(ImGuiKey_Home);
    ctx->KeyPress(ImGuiKey_Delete);
    ctx->Yield(2);
    // The keys edited the FIELD ("64x64" -> "64x6" -> "4x6") and deleted nothing.
    IM_CHECK_STR_EQ(dockspace.insert_field_value(0).c_str(), "4x6");
    IM_CHECK(cell_ids().size() == cells_before_typing);
    IM_CHECK_NO_RET(!state.selection().empty());
    ctx->ItemClick("Insert Cell/###insert_cancel");
    IM_CHECK(pump_until(ctx, [&] { return !dockspace.insert_modal_open(); }));
    IM_CHECK(cell_ids().size() == cells_before_typing);

    // --- (vi) Deleting the looked-through camera falls back to the free viewport ------
    canvas.set_look_through("canvas#1", e2e->camera);
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));
    ctx->Yield(4);
    const std::uint64_t frames_before = canvas.frames_issued("canvas#1");
    state.selection().select(e2e->camera);
    ctx->Yield(2);
    ctx->ItemClick(delete_item.c_str());
    IM_CHECK(pump_until(ctx, [&] { return ace::scene::cameras(state.document()).empty(); }));
    // The shipped fail-safe (canvas_view.cpp): a look-through whose shot is gone renders
    // through the free viewport — the pane keeps issuing frames rather than stalling.
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") > frames_before; }));
    IM_CHECK(state.selection().empty());
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

// The L4 gateway seam on its own, with NO shell and NO canvas — the headless half of the same
// path, and the branch that proves the two new virtuals are honest without any ImGui.
TEST_CASE("cells remove gateway: can_delete gates, delete_selected removes and clears") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "gateway");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  state.document().add_composition(64.0, 64.0);

  const arbc::expected<arbc::ObjectId, std::string> one = ace::scene::add_cell(
      state.document(), state.registry(), "org.arbc.raster", "8x8", arbc::Affine::identity());
  const arbc::expected<arbc::ObjectId, std::string> two =
      ace::scene::add_cell(state.document(), state.registry(), "org.arbc.raster", "8x8",
                           arbc::Affine::translation(10.0, 0.0));
  REQUIRE(one.has_value());
  REQUIRE(two.has_value());

  ace::dockmodel::RecentProjects recent(scratch.root / "prefs", fs);
  NoopFolderDialog dialog;
  NoopLauncher launcher;
  ace::app::AppProjectGateway gateway(recent, fs, dialog, launcher, "/usr/bin/arbitraryeditor",
                                      state);
  // Deliberately no set_edit_runner: the direct-invoke default (Constraint 4's headless arm).

  CHECK_FALSE(gateway.can_delete());
  CHECK(gateway.delete_selected() == 0);
  CHECK(ace::scene::cells(state.document(), state.registry()).size() == 2);

  state.selection().select(*one);
  state.selection().add(*two);
  CHECK(gateway.can_delete());
  CHECK(gateway.delete_selected() == 2);
  CHECK(ace::scene::cells(state.document(), state.registry()).empty());
  CHECK(state.selection().empty());
  CHECK_FALSE(gateway.can_delete());

  // An unwired gateway's inert defaults (D-cells_remove-6): the base-class shape the six
  // existing fakes inherit unchanged.
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
  CHECK_FALSE(bare.can_delete());
  CHECK(bare.delete_selected() == 0);
}
