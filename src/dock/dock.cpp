#include <ace/dock/dock.hpp>
#include <ace/dockmodel/tool_rail.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/dockmodel/workspaces.hpp>
#include <ace/views/views.hpp>

#include <imgui.h>
#include <imgui_internal.h> // DockBuilder* (the WIP docking-builder API)

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ace::dock {
namespace {

using ace::dockmodel::LauncherEntry;
using ace::dockmodel::SplitOrientation;
using ace::dockmodel::ToolDescriptor;
using ace::dockmodel::ViewType;

// The fixed rail width: enough to hold the widest text label ("Eyedropper") this
// leaf — icon art is deferred to editor.packaging (Open questions / Constraint).
constexpr float k_rail_width = 140.0f;

// Recursively translate a DockLayout subtree into ImGui dock nodes rooted at
// `node_id`: a split creates two children (H → left|right, V → top|bottom) and
// recurses; a leaf docks each of its view ids into the node so the matching
// window snaps there when it Begin()s (D-dockspace-1/-3).
void build_node(ImGuiID node_id, const ace::dockmodel::DockNode& node) {
  if (node.is_leaf()) {
    for (const std::string& view : node.tabs) {
      ImGui::DockBuilderDockWindow(view.c_str(), node_id);
    }
    return;
  }
  const ImGuiDir dir =
      node.orientation == SplitOrientation::Horizontal ? ImGuiDir_Left : ImGuiDir_Up;
  ImGuiID first_id = 0;
  ImGuiID second_id = 0;
  ImGui::DockBuilderSplitNode(node_id, dir, static_cast<float>(node.ratio), &first_id, &second_id);
  build_node(first_id, node.children[0]);
  build_node(second_id, node.children[1]);
}

// The New Project modal (D-open_ui-4): opened once a folder pick resolves a parent
// location, it collects a project name and, on Create, composes the not-yet-
// existing target through the gateway — which spawns the sibling whose bootstrap
// create-branch scaffolds it (no second Document minted here, D19/A7). Drawn every
// frame from the rail so BeginPopupModal stays balanced; the name buffer + parent
// live on the Dockspace so the state survives the async pick.
void draw_new_project_modal(Dockspace& dockspace, ProjectGateway& gateway) {
  const char* popup_id = "New Project";
  if (dockspace.new_project_modal_open() && !ImGui::IsPopupOpen(popup_id)) {
    ImGui::OpenPopup(popup_id);
  }
  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal(popup_id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped("Location: %s", dockspace.new_project_parent().string().c_str());
    ImGui::InputText("Name", dockspace.new_project_name_buffer(),
                     static_cast<std::size_t>(dockspace.new_project_name_buffer_size()));
    const std::string name(dockspace.new_project_name_buffer());
    if (ImGui::Button("Create")) {
      if (gateway.new_project(dockspace.new_project_parent(), name)) {
        dockspace.project_feedback().clear();
        dockspace.close_new_project_modal();
        ImGui::CloseCurrentPopup();
      } else {
        dockspace.project_feedback() = "Enter a valid project name.";
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      dockspace.close_new_project_modal();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

// The Clean up (GC) confirm modal (D-gc-3 / D15 "GC is a confirmed op, not
// undoable"): opened once the rail's dry-run preview resolves the reclaim counts,
// it surfaces them and, on Clean up, commits a real (`preview=false`) sweep — an
// irreversible delete is never one un-previewed click. Cancel sweeps nothing. Drawn
// every frame from the rail so BeginPopupModal stays balanced; the scripted preview
// lives on the Dockspace so the counts survive across frames. Stable slash-free
// `###` widget ids (`###gc_confirm` / `###gc_cancel`) for the e2e.
void draw_gc_modal(Dockspace& dockspace, ProjectGateway& gateway) {
  const char* popup_id = "Clean Up";
  if (dockspace.gc_modal_open() && !ImGui::IsPopupOpen(popup_id)) {
    ImGui::OpenPopup(popup_id);
  }
  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal(popup_id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    const GcSummary& preview = dockspace.gc_preview();
    ImGui::Text("Reclaim %llu orphaned asset blob(s), freeing %llu bytes?",
                static_cast<unsigned long long>(preview.reclaimed_files),
                static_cast<unsigned long long>(preview.reclaimed_bytes));
    if (ImGui::Button("Clean Up###gc_confirm")) {
      gateway.clean_up(/*preview=*/false); // the committed, irreversible sweep
      dockspace.close_gc_modal();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel###gc_cancel")) {
      dockspace.close_gc_modal(); // a cancelled Clean up sweeps nothing
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

// The Insert Cell modal (editor.cells.model / A16): pick a KIND, fill in whatever
// fields that kind's factory needs, confirm. The kind list is whatever the gateway
// handed over — one entry per `arbc::Registry::ids()` entry — and this code neither
// filters it nor inspects a `kind_id`, which is what makes "no kind allowlist"
// structural rather than a convention: every field is drawn as free text from the
// POD it was given, so there is nowhere for a per-kind branch to live.
//
// A refused insert (a kind whose factory always fails, a malformed config) leaves
// the modal OPEN with the kind's own error rendered inline — errors are values, the
// rail's established contract. Drawn every frame from the rail so BeginPopupModal
// stays balanced; the kind list, the selection, and the field buffers live on the
// Dockspace. Stable slash-free `###` widget ids for the e2e.
void draw_insert_cell_modal(Dockspace& dockspace, ProjectGateway& gateway) {
  const char* popup_id = "Insert Cell";
  if (dockspace.insert_modal_open() && !ImGui::IsPopupOpen(popup_id)) {
    ImGui::OpenPopup(popup_id);
  }
  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal(popup_id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  const std::vector<InsertKindSpec>& kinds = dockspace.insert_kinds();
  ImGui::TextUnformatted("Kind");
  for (std::size_t i = 0; i < kinds.size(); ++i) {
    std::string label = kinds[i].human_name;
    label += "###insert_kind";
    label += std::to_string(i);
    if (ImGui::Selectable(label.c_str(), i == dockspace.insert_selected_kind())) {
      dockspace.select_insert_kind(i);
    }
  }
  ImGui::Separator();
  const std::size_t selected = dockspace.insert_selected_kind();
  if (selected < kinds.size()) {
    const InsertKindSpec& kind = kinds[selected];
    for (std::size_t f = 0; f < kind.fields.size(); ++f) {
      std::string label = kind.fields[f].label;
      label += "###insert_field";
      label += std::to_string(f);
      ImGui::InputText(label.c_str(), dockspace.insert_field_buffer(f),
                       static_cast<std::size_t>(dockspace.insert_field_buffer_size()));
    }
    // Say out loud that the placement is provisional (Constraint 7): the
    // overview-wireframe placement gesture is editor.panels.overview's, and the user
    // should not read this insert as "I chose where it goes".
    ImGui::TextUnformatted("Placement: provisional — centred in the current view.");
    if (!dockspace.insert_error().empty()) {
      ImGui::TextWrapped("%s", dockspace.insert_error().c_str());
    }
    if (ImGui::Button("Insert###insert_confirm")) {
      InsertValues values;
      values.reserve(kind.fields.size());
      for (std::size_t f = 0; f < kind.fields.size(); ++f) {
        values.emplace_back(kind.fields[f].id, dockspace.insert_field_value(f));
      }
      const std::string error = gateway.insert_cell(kind.kind_id, values);
      if (error.empty()) {
        dockspace.insert_error().clear();
        dockspace.close_insert_modal();
        ImGui::CloseCurrentPopup();
      } else {
        dockspace.insert_error() = error; // the kind's own message; the modal stays open
      }
    }
    ImGui::SameLine();
  }
  if (ImGui::Button("Cancel###insert_cancel")) {
    dockspace.close_insert_modal(); // a cancelled insert mutates nothing
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

// The rail's Insert section: the one-shot "put a cell in the composition" entry
// point (D3). A confirmed one-shot op is what the two existing modals are for, so
// this is a modal rather than a ninth view type (D-cells_model-5).
void draw_insert_section(Dockspace& dockspace, ProjectGateway& gateway) {
  ImGui::Separator();
  ImGui::TextUnformatted("Insert");
  if (ImGui::Selectable("Insert Cell…###insert_cell")) {
    dockspace.open_insert_modal(gateway.insert_kinds());
  }
  draw_insert_cell_modal(dockspace, gateway);
}

// The rail's Project section (D18 home base / D22): New / Open / Open Recent, each
// terminating in a sibling `exec` through the gateway (never swapping this
// process's one Document, D19). Present only when a gateway is wired; the folder
// pick is async, so New/Open compose their follow-up inside the pick callback.
void draw_project_section(Dockspace& dockspace, ProjectGateway& gateway) {
  ImGui::Separator();
  ImGui::TextUnformatted("Project");
  // Save acts on THIS process's session (A13), unlike New/Open/Recent below which
  // spawn siblings. It is the primary verb for the current project, so it leads the
  // section. The dirty indicator (workspace-vs-snapshot drift, D16) is drawn only
  // when `is_dirty()` — a disabled marker so it carries a stable widget id the e2e
  // can probe for presence/absence without it being actionable.
  if (ImGui::Selectable("Save###save_project")) {
    if (gateway.save()) {
      dockspace.project_feedback().clear();
    } else {
      dockspace.project_feedback() = "Save failed.";
    }
  }
  if (gateway.is_dirty()) {
    ImGui::BeginDisabled();
    ImGui::SmallButton("Unsaved changes###dirty_indicator");
    ImGui::EndDisabled();
  }
  // Save As sits beside Save — the other in-process verb (A13), but it forks: it
  // publishes a COPY elsewhere and opens that copy in a sibling `exec`, leaving this
  // session put (D-save_as-2). The native folder pick is async, so the gateway owns
  // the whole pick→publish→exec follow-up; the rail just fires the action (no
  // synchronous feedback to render). Stable slash-free `###` id for the e2e.
  if (ImGui::Selectable("Save As…###save_as")) {
    gateway.save_as();
  }
  // Clean up (GC) sits beside Save As — another in-process verb (A13). It reclaims
  // the on-disk `assets/` orphans the grow-only save sink left behind (D13/§8). A
  // confirmed op (D15): the click runs a dry-run PREVIEW and opens a confirm modal
  // with the reclaim counts; only the modal's Clean up commits the real sweep. Stable
  // slash-free `###gc` id for the e2e.
  if (ImGui::Selectable("Clean up…###gc")) {
    dockspace.open_gc_modal(gateway.clean_up(/*preview=*/true));
  }
  // Stable, slash-free `###` widget ids (the visible label carries the ellipsis /
  // path; the id after `###` is what the e2e drives by) so a path in the label
  // never confuses the test-engine ref parser.
  if (ImGui::Selectable("New Project…###new_project")) {
    ProjectGateway* gw = &gateway;
    gw->pick_folder([&dockspace](std::optional<std::filesystem::path> picked) {
      if (picked.has_value()) {
        dockspace.open_new_project_modal(*picked);
      }
    });
  }
  if (ImGui::Selectable("Open Project…###open_project")) {
    ProjectGateway* gw = &gateway;
    gw->pick_folder([&dockspace, gw](std::optional<std::filesystem::path> picked) {
      if (!picked.has_value()) {
        return; // a cancelled pick spawns nothing (Constraint 5)
      }
      if (gw->open_project(*picked)) {
        dockspace.project_feedback().clear();
      } else {
        dockspace.project_feedback() = "That folder is not a project.";
      }
    });
  }
  const std::vector<std::filesystem::path> recent = gateway.recent_projects();
  if (!recent.empty()) {
    ImGui::TextUnformatted("Open Recent");
    for (std::size_t i = 0; i < recent.size(); ++i) {
      const std::filesystem::path& dir = recent[i];
      std::string label = dir.string();
      label += "###recent";
      label += std::to_string(i);
      if (ImGui::Selectable(label.c_str())) {
        if (gateway.open_recent(dir)) {
          dockspace.project_feedback().clear();
        } else {
          dockspace.project_feedback() = "That project is no longer available.";
        }
      }
    }
  }
  if (!dockspace.project_feedback().empty()) {
    ImGui::TextWrapped("%s", dockspace.project_feedback().c_str());
  }
  draw_new_project_modal(dockspace, gateway);
  draw_gc_modal(dockspace, gateway);
}

// The canonical undo affordance (Decision D-undo-3): the keyboard chords Ctrl+Z
// (undo), Ctrl+Shift+Z / Ctrl+Y (redo), handled here in the L3 dockspace — the only
// ImGui level — and routed through the gateway to the L4 session verbs, gated on
// can_undo()/can_redo() so a no-op is never dispatched. There is no menu bar (D18
// uniform shell) and the History panel is deferred, so the chord is undo's only home
// this leaf ships. `IsKeyChordPressed` reads GLOBAL key state and matches modifiers
// EXACTLY (Ctrl+Z and Ctrl+Shift+Z never collide), so the chord fires regardless of
// which docked window holds focus and needs no focus-routing.
void handle_undo_shortcuts(ProjectGateway& gateway) {
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
    if (gateway.can_undo()) {
      gateway.undo();
    }
  }
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z) ||
      ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y)) {
    if (gateway.can_redo()) {
      gateway.redo();
    }
  }
}

} // namespace

const char* name() { return "dock"; }

std::vector<ViewType> default_initial_views() {
  return {ViewType::Canvas, ViewType::Inspector, ViewType::Layers, ViewType::Overview};
}

const char* tool_rail_title() { return "Tool Rail"; }

void draw_tool_rail(Dockspace& dockspace) {
  // Modal tools (D20): a single active-tool selection over the tool catalog. The
  // selection is observable state ONLY — nothing on the canvas reads it yet
  // (D-tool_rail-4). Each button carries a stable text id so the e2e can click it.
  ImGui::TextUnformatted("Tools");
  const ace::dockmodel::ToolId active = dockspace.tools().active();
  for (const ToolDescriptor& tool : ace::dockmodel::tool_catalog()) {
    const std::string label(tool.title);
    if (ImGui::Selectable(label.c_str(), tool.id == active)) {
      dockspace.tools().select(tool.id);
    }
  }

  ImGui::Separator();

  // The view launcher (§10 :446-450, the home base): one entry per view type,
  // click to open — or, for a singleton already present, focus — via the existing
  // Dockspace surface (Dockspace::open is idempotent for singletons, mints a fresh
  // canvas#N for the multi-instance Canvas). `is_open` reflects the authoritative
  // layout (D-tool_rail-5), so with the layout empty one click restores the view —
  // the home-base guarantee that nothing can be lost.
  ImGui::TextUnformatted("Views");
  for (const LauncherEntry& entry : ace::dockmodel::launcher_entries(dockspace.layout())) {
    const std::string label(ace::dockmodel::view_title(entry.type));
    if (ImGui::Selectable(label.c_str(), entry.is_open)) {
      dockspace.open(entry.type);
    }
  }

  // The project-entry affordances (D22 / A12): New / Open / Open Recent, present
  // only when the app wired a gateway (null in the bare-shell smoke).
  if (ProjectGateway* gateway = dockspace.project_gateway()) {
    draw_insert_section(dockspace, *gateway);
    draw_project_section(dockspace, *gateway);
  }

  // Saved workspaces (D18/D21): a one-click switcher over the built-in and user
  // presets plus a "Save current as…" / "Delete" pair, when a store is wired.
  // Applying a preset replaces the layout and re-seeds the ImGui tree
  // (Dockspace::apply_layout). Each preset name is its own stable widget id so
  // the e2e can click it; built-in names never collide with a view/tool title.
  ace::dockmodel::WorkspaceStore* store = dockspace.workspace_store();
  if (store == nullptr) {
    return;
  }
  ImGui::Separator();
  ImGui::TextUnformatted("Workspaces");
  for (const ace::dockmodel::WorkspacePreset& preset : store->presets()) {
    if (ImGui::Selectable(preset.name.c_str())) {
      if (std::optional<ace::dockmodel::DockLayout> layout = store->apply(preset.name)) {
        dockspace.apply_layout(*layout);
      }
    }
  }
  ImGui::InputText("##workspace_name", dockspace.save_name_buffer(),
                   static_cast<std::size_t>(dockspace.save_name_buffer_size()));
  const std::string name(dockspace.save_name_buffer());
  if (ImGui::Button("Save current as") && !name.empty()) {
    store->save(name, dockspace.layout());
  }
  ImGui::SameLine();
  if (ImGui::Button("Delete") && !name.empty()) {
    store->remove(name);
  }
}

void Dockspace::open_insert_modal(std::vector<InsertKindSpec> kinds) {
  insert_kinds_ = std::move(kinds);
  insert_error_.clear();
  select_insert_kind(0);
  insert_modal_open_ = true;
}

void Dockspace::select_insert_kind(std::size_t index) {
  insert_selected_ = index;
  insert_error_.clear();
  insert_fields_.clear();
  if (index >= insert_kinds_.size()) {
    return;
  }
  // Re-seed one buffer per field from that kind's prefill, so a raster's resolution
  // arrives filled in from the composition's own size (Constraint 8) rather than
  // blank or silently applied. A prefill longer than the buffer is truncated.
  for (const InsertFieldSpec& field : insert_kinds_[index].fields) {
    std::array<char, k_insert_field_capacity> buffer{};
    const std::size_t copied = std::min(field.initial.size(), buffer.size() - 1);
    std::copy_n(field.initial.begin(), copied, buffer.begin());
    insert_fields_.push_back(buffer);
  }
}

char* Dockspace::insert_field_buffer(std::size_t field) {
  if (field >= insert_fields_.size()) {
    insert_scratch_.fill('\0');
    return insert_scratch_.data();
  }
  return insert_fields_[field].data();
}

std::string Dockspace::insert_field_value(std::size_t field) const {
  if (field >= insert_fields_.size()) {
    return {};
  }
  return std::string(insert_fields_[field].data());
}

Dockspace::Dockspace(std::vector<ViewType> initial) {
  if (initial.empty()) {
    return;
  }
  // The first view fills a lone root leaf; the second splits off a panel column;
  // the rest tab into that column. Minting flows through the registry so the
  // instance ids and the multi-instance counter stay consistent with the layout.
  const std::string canvas = registry_.open(layout_, initial.front());
  if (initial.size() == 1) {
    return;
  }
  const std::string column = registry_.mint_id(initial[1]);
  layout_.split_leaf(canvas, SplitOrientation::Horizontal, 0.62, column);
  for (std::size_t i = 2; i < initial.size(); ++i) {
    layout_.insert_tab(column, registry_.mint_id(initial[i]));
  }
}

void Dockspace::draw() {
  // The undo/redo keyboard chords route through the wired gateway to the in-process
  // session (D-undo-3), gated on can_undo()/can_redo(). Handled once per frame,
  // before any window, so it fires regardless of docked-window focus; omitted with
  // the rail's Project section when no gateway is wired (the bare-shell smoke).
  if (ProjectGateway* gateway = project_gateway_) {
    handle_undo_shortcuts(*gateway);
  }

  ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImVec2 work_pos = viewport->WorkPos;
  const ImVec2 work_size = viewport->WorkSize;

  // The fixed left tool rail (home base): a real, non-dockable window pinned to
  // the left of the work area, sized deterministically each frame (no viewport
  // work-inset feedback — that lags a frame and jitters the docked geometry). It
  // reserves k_rail_width; the dockspace host below fills the remainder, so no
  // view ever overlaps the rail. Chrome — NoTitleBar/NoResize/NoMove/NoDocking —
  // so it can never be closed or docked (D-tool_rail-3).
  ImGui::SetNextWindowPos(work_pos);
  ImGui::SetNextWindowSize(ImVec2(k_rail_width, work_size.y));
  const ImGuiWindowFlags rail_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::Begin(tool_rail_title(), nullptr, rail_flags)) {
    draw_tool_rail(*this);
  }
  ImGui::End();

  // The uniform dockspace host fills the work area to the RIGHT of the rail — a
  // borderless, immovable host window carrying the DockSpace node, no reserved
  // central node so every node is a peer (D18 / D-dockspace-5). NoBringToFront so
  // the docked view windows always render above the host.
  const ImVec2 host_size(work_size.x - k_rail_width, work_size.y);
  ImGui::SetNextWindowPos(ImVec2(work_pos.x + k_rail_width, work_pos.y));
  ImGui::SetNextWindowSize(host_size);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  const ImGuiWindowFlags host_flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
  ImGui::Begin("##dockspace_host", nullptr, host_flags);
  ImGui::PopStyleVar(3);
  // The node id is hashed against the host window; it is stable per frame and is
  // what the DockBuilder seed + every view's DockBuilderDockWindow target.
  dockspace_id_ = ImGui::GetID("dockspace");
  ImGui::DockSpace(dockspace_id_, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

  if (!layout_.empty() && (!built_ || rebuild_)) {
    // Seed / re-seed the live tree from the authoritative DockLayout: on the
    // first frame, and after a programmatic open/reopen so the new window docks
    // per the model (the model is the source of truth — D-view-registry-6).
    ImGui::DockBuilderRemoveNode(dockspace_id_);
    ImGui::DockBuilderAddNode(dockspace_id_, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id_, host_size);
    build_node(dockspace_id_, layout_.root());
    ImGui::DockBuilderFinish(dockspace_id_);
    built_ = true;
    rebuild_ = false;
  }
  ImGui::End(); // ##dockspace_host

  // Draw every open view by its instance id; a cleared p_open (the tab ✕) routes
  // through the L1 close so the model stays authoritative.
  std::vector<std::string> to_close;
  for (const std::string& id : layout_.view_ids()) {
    bool open = true;
    if (ImGui::Begin(id.c_str(), &open)) {
      views::draw_view(id);
    }
    ImGui::End();
    if (!open) {
      to_close.push_back(id);
    }
  }
  for (const std::string& id : to_close) {
    registry_.close(layout_, id);
  }
}

std::string Dockspace::open(ViewType type) {
  std::string id = registry_.open(layout_, type);
  rebuild_ = true; // the new window must dock into the model layout next frame
  return id;
}

std::string Dockspace::reopen(ViewType type) {
  std::string id = registry_.reopen(layout_, type);
  rebuild_ = true;
  return id;
}

bool Dockspace::close(std::string_view view_id) { return registry_.close(layout_, view_id); }

void Dockspace::apply_layout(const ace::dockmodel::DockLayout& layout) {
  layout_ = layout;
  registry_.adopt(layout_); // no restored slug#N can be re-minted (D-view-registry-4)
  rebuild_ = true;          // re-seed the live ImGui tree next draw() (D-workspaces-5)
}

} // namespace ace::dock
