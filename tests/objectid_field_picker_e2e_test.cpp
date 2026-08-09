// editor.cells.objectid_field_picker — the Insert Cell modal renders an `ObjectId` insert-schema
// field as a COMPOSITION PICKER, not free text (A16 / D-objectid_field_picker-3). This e2e drives
// the real shell: open the modal, select `org.arbc.nested`, assert its `child` field renders as a
// combo whose choices are the document's compositions, pick one, confirm, and assert on MODEL state
// (a nested cell now exists whose `composition_ref()` is the picked composition). The A16 witness
// then shows the picker is keyed on the declared field TYPE, not a kind id: an editor-unknown probe
// kind that advertises an `ObjectId` field gets the same picker.
//
// Assertions are on model state, never pixels — the sibling `cells_insert_e2e_test.cpp` convention.
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

#include <arbc/base/ids.hpp>
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
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
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
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_objectid_picker_e2e_test") {
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

// An editor-unknown kind that advertises a library schema with ONE `ObjectId` field — the A16
// witness. The picker must appear for it because its field TYPE is `ObjectId`, never because it is
// `org.arbc.nested`. Its factory always refuses (this e2e never confirms an insert of it), which is
// irrelevant: the picker is a rendering decision the gateway makes from the advertised schema.
constexpr const char* k_probe_kind = "org.example.wrapper_probe";

void register_probe_kind(arbc::Registry& registry) {
  arbc::ContentFactory factory =
      [](arbc::ContentConfig) -> arbc::expected<std::unique_ptr<arbc::Content>, std::string> {
    return arbc::unexpected<std::string>("org.example.wrapper_probe: probe kind is never built");
  };
  arbc::KindInsertSchema schema;
  schema.fields = {
      arbc::KindInsertField{"target", arbc::KindInsertField::Type::ObjectId, "", 1.0, {}, {}}};
  schema.assemble =
      [](std::span<const std::string> values) -> arbc::expected<std::string, std::string> {
    return values.empty() ? std::string() : values.front();
  };
  (void)registry.add(k_probe_kind, std::move(factory), arbc::KindMetadata{"Wrapper Probe", "1"},
                     /*codec=*/std::nullopt, /*binder=*/std::nullopt,
                     /*state_walker=*/std::nullopt, std::move(schema));
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

const ace::dock::InsertKindSpec* insert_spec(const ace::dock::Dockspace& dockspace,
                                             std::string_view kind_id) {
  for (const ace::dock::InsertKindSpec& spec : dockspace.insert_kinds()) {
    if (spec.kind_id == kind_id) {
      return &spec;
    }
  }
  return nullptr;
}

// TestFunc is a plain function pointer (std::function is disabled in this build), so the
// collaborators are threaded through UserData rather than captured.
struct E2EState {
  ace::dock::Dockspace* dockspace;
  AppState* state;
  arbc::ObjectId child; // the seeded sub-composition the picker must offer
};

} // namespace

TEST_CASE("objectid field picker e2e: nested child renders as a composition picker, A16 by type") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  ace::testing::WriterSession session(scratch.root / "picker");
  REQUIRE(session.ok());
  AppState& state = session.state();
  // The A16 witness kind — registered on the SAME registry the gateway marshals, before the modal
  // snapshots the kind list. Registration is not a document write, so it needs no writer hop.
  register_probe_kind(state.registry());

  // Seed a root composition AND a sub-composition already wrapped by a nested cell, so
  // `composition_options` (which descends from Root over `composition_ref()`) has a candidate to
  // offer. Seeding IS a document write — post it to the writer identity the open bound.
  arbc::ObjectId child;
  session.on_writer([&] {
    state.document().add_composition(64.0, 64.0);
    child = state.document().add_composition(32.0, 32.0);
    REQUIRE(ace::scene::add_cell(state.document(), state.registry(), "org.arbc.nested",
                                 std::to_string(child.value), arbc::Affine::identity())
                .has_value());
  });
  REQUIRE(child.valid());
  REQUIRE(ace::scene::cells(state.document(), state.registry()).size() == 1);

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

  E2EState e2e{&dockspace, &state, child};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "cells", "objectid_field_picker");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    ace::dock::Dockspace& dockspace = *e2e->dockspace;
    AppState& state = *e2e->state;
    const std::string rail = ace::dock::tool_rail_title();
    const std::string child_decimal = std::to_string(e2e->child.value);

    // --- (i) `org.arbc.nested`'s `child` field renders as a PICKER, not free text ----------
    ctx->ItemClick((rail + "/###insert_cell").c_str());
    ctx->Yield(2);
    IM_CHECK(dockspace.insert_modal_open());
    // The no-allowlist invariant still holds: one row per registered kind.
    IM_CHECK(dockspace.insert_kinds().size() == state.registry().ids().size());

    ctx->ItemClick(kind_row(dockspace, "org.arbc.nested").c_str());
    ctx->Yield(2);
    const ace::dock::InsertKindSpec* nested = insert_spec(dockspace, "org.arbc.nested");
    IM_CHECK(nested != nullptr);
    IM_CHECK(nested->fields.size() == 1);
    IM_CHECK(nested->fields[0].id == "child");
    IM_CHECK(nested->fields[0].picker);              // a combo, structurally NOT an InputText
    IM_CHECK(nested->fields[0].choices.size() == 1); // the one seeded sub-composition
    IM_CHECK(nested->fields[0].choices[0].value == child_decimal);

    // Drive the combo: `ComboClick` opens the field and clicks the choice, and internally asserts
    // the opened window IS a combo (`IsWindowACombo`) — the structural "rendered as a picker, not
    // an InputText" check at the widget layer. Then assert the resolved decimal landed in the
    // buffer.
    IM_CHECK(ctx->ItemExists("Insert Cell/###insert_field0"));
    ctx->SetRef("Insert Cell");
    ctx->ComboClick("###insert_field0/###insert_choice0_0");
    ctx->SetRef("");
    ctx->Yield(2);
    IM_CHECK(dockspace.insert_field_value(0) == child_decimal);

    // --- (ii) Confirm mints a nested cell whose composition_ref() is the picked composition ---
    ctx->ItemClick("Insert Cell/###insert_confirm");
    IM_CHECK(pump_until(ctx, [&] { return !dockspace.insert_modal_open(); }));
    ctx->Yield(2);
    IM_CHECK(dockspace.insert_error().empty());
    const std::vector<ace::scene::Cell> after =
        ace::scene::cells(state.document(), state.registry());
    IM_CHECK(after.size() == 2); // the seed + the picked mint
    // BOTH cells wrap the SAME child composition — the picked value is byte-identical to a typed
    // decimal (the whole point of the leaf), so `composition_ref()` resolves to `child`.
    int wrapping = 0;
    for (const ace::scene::Cell& cell : after) {
      const std::optional<arbc::ObjectId> ref =
          ace::scene::nested_composition_of(state.document(), cell.id);
      if (ref && *ref == e2e->child) {
        ++wrapping;
      }
    }
    IM_CHECK(wrapping == 2);

    // --- (iii) A16 witness: the picker is keyed on field TYPE, not kind id -------------------
    // The editor-unknown probe kind advertises an `ObjectId` field, so it gets the identical
    // picker — no `org.arbc.nested` allowlist anywhere.
    ctx->ItemClick((rail + "/###insert_cell").c_str());
    ctx->Yield(2);
    ctx->ItemClick(kind_row(dockspace, k_probe_kind).c_str());
    ctx->Yield(2);
    const ace::dock::InsertKindSpec* probe = insert_spec(dockspace, k_probe_kind);
    IM_CHECK(probe != nullptr);
    IM_CHECK(probe->fields.size() == 1);
    IM_CHECK(probe->fields[0].picker);              // driven by the ObjectId type, not the kind id
    IM_CHECK(probe->fields[0].choices.size() == 1); // the same shared candidate set
    IM_CHECK(probe->fields[0].choices[0].value == child_decimal);
    IM_CHECK(ctx->ItemExists("Insert Cell/###insert_field0"));
    ctx->SetRef("Insert Cell");
    ctx->ComboClick("###insert_field0/###insert_choice0_0"); // a combo here too (asserts it is one)
    ctx->SetRef("");
    ctx->Yield(2);
    // Never confirm the refusing probe — cancel; the document is left with the two cells above.
    ctx->ItemClick("Insert Cell/###insert_cancel");
    IM_CHECK(pump_until(ctx, [&] { return !dockspace.insert_modal_open(); }));
    ctx->Yield(2);
    IM_CHECK(ace::scene::cells(state.document(), state.registry()).size() == 2);
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
