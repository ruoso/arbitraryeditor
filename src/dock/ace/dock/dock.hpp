#pragma once

#include <ace/dockmodel/dockmodel.hpp>
#include <ace/dockmodel/tool_rail.hpp>
#include <ace/dockmodel/view_registry.hpp>

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ace::dockmodel {
class WorkspaceStore; // the L1 preset store; the switcher drives it by reference
}

namespace ace::dock {

// The dock component. See docs/01-architecture.md §8 (component levelization).
const char* name();

// The project-entry seam (docs/01-architecture.md A12, docs/00-design.md D22).
// The tool rail's New / Open / Recent affordances drive this ABSTRACT interface,
// which `dock` (L3) declares and owns; the concrete AppProjectGateway lives in L4
// `app` — the only level permitted SDL — and is the sole holder of the SDL-backed
// native folder dialog, the platform::ProcessLauncher + current_executable_path
// (A7), the dockmodel::RecentProjects store, and the L1 `project` validate/compose
// helpers. So the rail reaches process-launch + SDL through one abstraction it
// declares, never by including `<ace/commands/...>` or `<ace/platform/...>`; the
// dependency is inverted so `dock`'s includes stay within its own header + std.
//
// Every action spawns a NEW sibling editor process (process-per-project, D19/A7)
// and NEVER swaps the current process's one Document — the current window stays up
// (D19's tab analog). Errors are values: the mutating actions return success/
// failure the rail renders as inline feedback (Constraint 7).
class ProjectGateway {
public:
  virtual ~ProjectGateway() = default;

  // Validate `dir` as an existing project, record it MRU-front, and spawn a
  // sibling editor on it. Returns false (no spawn) when `dir` is not a project.
  virtual bool open_project(const std::filesystem::path& dir) = 0;

  // Compose `parent / name` (a not-yet-existing target) and spawn a sibling whose
  // bootstrap create-branch scaffolds it — no second Document is minted here.
  // Returns false on an invalid name; records nothing (the directory is absent).
  virtual bool new_project(const std::filesystem::path& parent, const std::string& name) = 0;

  // Replay a recent project directory: re-order it MRU-front and spawn. Returns
  // false when the directory is no longer a project.
  virtual bool open_recent(const std::filesystem::path& dir) = 0;

  // Open the OS-native folder picker (Constraint 5). Returns IMMEDIATELY; the OS
  // dialog is async, so `on_pick` is invoked on a later frame with the chosen
  // directory, or with nullopt on cancel. The rail composes the chosen path into
  // an open_project / new_project follow-up inside `on_pick`.
  virtual void pick_folder(std::function<void(std::optional<std::filesystem::path>)> on_pick) = 0;

  // The current pruned MRU list (most-recent-first) for the rail to render.
  virtual std::vector<std::filesystem::path> recent_projects() const = 0;

  // Publish the IN-PROCESS session's canonical `project.arbc` (+ owned `assets/`)
  // — the one front-door verb that acts on this process's own Document rather than
  // spawning a sibling (A13). Returns false when the publish failed (the rail
  // renders inline feedback and the session stays dirty). The L4 impl drives
  // `commands::save_project` against the one owned `AppState`.
  virtual bool save() = 0;

  // Whether the session has unpublished workspace-vs-snapshot drift (D16/A13): the
  // rail draws the dirty indicator when true. Conservative — never a false clean.
  virtual bool is_dirty() const = 0;
};

// The default starter arrangement (the eight-type catalog is opened lazily by
// the launcher — tool_rail): a Canvas fills one side; Inspector / Layers /
// Overview share the other side as a tab-group. Deterministic — the "rebuild the
// default" side of the close-everything round-trip.
std::vector<ace::dockmodel::ViewType> default_initial_views();

// The main-viewport dockspace host (D18 fully-uniform shell). It owns a
// ViewRegistry + the authoritative DockLayout (D-view-registry-2): each frame it
// submits a full-viewport DockSpace and draws every open view by its instance id
// via views::draw_view, inside a window carrying a `bool* p_open`. When ImGui
// clears p_open (the tab ✕), it routes the close through the L1 registry so the
// DockLayout — not ImGui's transient tree — stays the single source of truth
// reopen and workspaces read (D-view-registry-6). On the first frame (and after a
// programmatic open/reopen) it translates the DockLayout into ImGui's live tree
// via DockBuilder (io.IniFilename == nullptr → built in-code, not from imgui.ini).
class Dockspace {
public:
  // Seed by opening `initial` view types in order through the owned registry (so
  // instance ids + the multi-instance counter stay consistent). The first fills
  // a lone root leaf; the rest arrange a split-off panel column.
  explicit Dockspace(std::vector<ace::dockmodel::ViewType> initial = default_initial_views());

  // Draw one frame of the dockspace host + every open view body. Call inside the
  // shell's draw-content.
  void draw();

  // The ImGui dockspace node id (an ImGuiID), valid after the first draw(). The
  // e2e uses it to assert the host node exists (DockBuilderGetNode != null).
  unsigned int dockspace_id() const { return dockspace_id_; }

  // Catalog-driven mutation the launcher (tool_rail) and the e2e drive by type /
  // id. open/reopen mint through the registry and mark the live tree for a
  // re-seed next draw() so the new window docks; close routes through the model.
  std::string open(ace::dockmodel::ViewType type);
  std::string reopen(ace::dockmodel::ViewType type);
  bool close(std::string_view view_id);

  // The authoritative layout (the set of open views is layout().view_ids()).
  const ace::dockmodel::DockLayout& layout() const { return layout_; }

  // Replace the whole layout with `layout` (a saved workspace preset) and mark
  // the live ImGui tree for a re-seed next draw() (D-workspaces-5, reusing the
  // once-guarded DockBuilder path). Adopts the registry counters first so a
  // restored slug#N can never be re-minted (D-view-registry-4).
  void apply_layout(const ace::dockmodel::DockLayout& layout);

  // The per-user preset store the switcher drives (workspaces). Null until the
  // app wires one at bootstrap; when null the rail omits the Workspaces section.
  void set_workspace_store(ace::dockmodel::WorkspaceStore* store) { workspace_store_ = store; }
  ace::dockmodel::WorkspaceStore* workspace_store() const { return workspace_store_; }

  // The editable "Save current as…" name buffer the rail's InputText writes and
  // the Save/Delete buttons read (a plain char buffer — the build ships no
  // imgui_stdlib std::string overload).
  char* save_name_buffer() { return save_name_.data(); }
  int save_name_buffer_size() const { return static_cast<int>(save_name_.size()); }

  // The rail's active-tool selection (A11). Observable UI state the tool rail
  // mutates on click; nothing on the canvas reads it yet (D-tool_rail-4). The e2e
  // reads it through this accessor to assert the active tool after a click.
  ace::dockmodel::ToolSelection& tools() { return tools_; }
  const ace::dockmodel::ToolSelection& tools() const { return tools_; }

  // The project-entry gateway the rail's New / Open / Recent affordances drive
  // (A12 / D22). Null until the app wires one at bootstrap (or a test injects a
  // fake through ShellOptions); when null the rail omits the Project section.
  void set_project_gateway(ProjectGateway* gateway) { project_gateway_ = gateway; }
  ProjectGateway* project_gateway() const { return project_gateway_; }

  // The New Project modal state the rail drives (a name buffer mirroring the
  // save-name buffer, plus the chosen parent the folder pick seeded). The modal
  // opens only after pick_folder resolves a parent location (D-open_ui-4).
  char* new_project_name_buffer() { return new_project_name_.data(); }
  int new_project_name_buffer_size() const { return static_cast<int>(new_project_name_.size()); }
  bool new_project_modal_open() const { return new_project_modal_open_; }
  const std::filesystem::path& new_project_parent() const { return new_project_parent_; }
  void open_new_project_modal(std::filesystem::path parent) {
    new_project_parent_ = std::move(parent);
    new_project_name_.fill('\0');
    new_project_modal_open_ = true;
  }
  void close_new_project_modal() { new_project_modal_open_ = false; }

  // Inline feedback the rail renders for the last project action (a non-project
  // Open selection, an unavailable recent, an invalid New name). The rail reads
  // and writes it; empty means "no message".
  std::string& project_feedback() { return project_feedback_; }

private:
  ace::dockmodel::ViewRegistry registry_;
  ace::dockmodel::DockLayout layout_;
  ace::dockmodel::ToolSelection tools_; // the rail's active-tool selection (A11)
  ace::dockmodel::WorkspaceStore* workspace_store_ =
      nullptr;                                // preset store (app-wired, may be null)
  ProjectGateway* project_gateway_ = nullptr; // project-entry seam (app-wired, may be null)
  std::array<char, 64> save_name_{};          // "Save current as…" name buffer
  std::array<char, 128> new_project_name_{};  // New Project modal name buffer
  std::filesystem::path new_project_parent_;  // parent the folder pick seeded
  std::string project_feedback_;              // inline feedback for the last action
  bool new_project_modal_open_ = false;       // the New Project modal is showing
  bool built_ = false;                        // DockBuilder seeded at least once
  bool rebuild_ = false;                      // a programmatic open/reopen needs a re-seed
  unsigned int dockspace_id_ = 0;             // ImGuiID; assigned on first draw()
};

// The stable ImGui window id of the fixed tool rail — exposed so the e2e can
// drive the rail's buttons under the same id the rail draws under (Constraint 7).
const char* tool_rail_title();

// Draw the fixed left tool rail (D18 / §10 "home base"): the view launcher (one
// entry per view_catalog() type, click to open/focus via Dockspace::open) plus
// the modal tools (Select/Brush/Eyedropper/Pan over `dockspace.tools()`). Chrome,
// not a view: always present, never closable/dockable (D-tool_rail-3). Called by
// Dockspace::draw() inside the fixed left rail window; the dockspace host fills
// the work area to its right, so the rail reserves space and nothing overlaps it.
void draw_tool_rail(Dockspace& dockspace);

} // namespace ace::dock
