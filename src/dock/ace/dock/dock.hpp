#pragma once

#include <ace/dockmodel/dockmodel.hpp>
#include <ace/dockmodel/tool_rail.hpp>
#include <ace/dockmodel/view_registry.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ace::dockmodel {
class WorkspaceStore; // the L1 preset store; the switcher drives it by reference
}

namespace ace::dock {

// The dock component. See docs/01-architecture.md §8 (component levelization).
const char* name();

// The Clean up (GC) reclaim report, in the dock's OWN vocabulary (Decision D-gc-5):
// a dock-local POD, NOT `project::GcOutcome` / `arbc::GcReport`. Returning a
// `project`/`arbc` type through the L3 gateway would add a `dock -> project` (or
// `dock -> arbc`) include edge — a `check_levels` DAG change — for a two-field
// report; the arbc -> project -> dock mapping lives in L4 `app` (which sees all
// three) so this seam stays edge-neutral. `ran` is false when no sweep ran (a GC
// error or an unwired gateway), so the confirm modal never claims a phantom reclaim.
struct GcSummary {
  std::uint64_t reclaimed_files = 0; // orphaned blobs deleted (would-be, in a preview)
  std::uint64_t reclaimed_bytes = 0; // their total size
  bool ran = false;                  // a sweep actually completed
};

// One input the Insert Cell modal must collect for a kind's opaque
// `arbc::ContentConfig`, in the dock's OWN vocabulary (A16 / D-cells_model-5): the
// L1 `scene::InsertField` crosses this seam as POD exactly as `GcSummary` mirrors
// the GC report, because `dock` may include neither `ace/scene` nor `ace/commands`.
//
// There is deliberately NO field *type* here. The modal renders every field as free
// text, so the L3 layer structurally cannot branch on a kind — the no-allowlist
// property (Constraint 2) is enforced by what this struct omits.
struct InsertFieldSpec {
  std::string id;      // stable field id the confirm marshals values back by
  std::string label;   // human label
  std::string initial; // prefilled value (a raster's resolution, Constraint 8)
};

// One insertable kind. The gateway returns exactly one of these per
// `arbc::Registry::ids()` entry, unconditionally and in registration order; the rail
// never filters the list it is handed.
struct InsertKindSpec {
  std::string kind_id;    // the registry id, verbatim — opaque to `dock`
  std::string human_name; // what the modal lists
  std::vector<InsertFieldSpec> fields;
};

// The user's answers, keyed by `InsertFieldSpec::id`.
using InsertValues = std::vector<std::pair<std::string, std::string>>;

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

  // Save As (A13 / D-save_as): publish a COPY of the in-process session into a
  // newly chosen directory and open that copy in a sibling editor, leaving THIS
  // process on its original project (process-per-project, D19/A7). Like the
  // New/Open picker this is ASYNCHRONOUS — the L4 impl opens the native folder
  // dialog and, on a later frame, drives `commands::save_project_as` with the
  // chosen target — so it returns void (no synchronous outcome to render). A
  // cancelled pick publishes nothing and execs nothing.
  virtual void save_as() = 0;

  // Clean up (GC): reclaim the in-process session's on-disk `assets/` orphans
  // (D13/§8/A13). Like `save()` this acts on THIS process's one owned session, not
  // a sibling exec. A `preview` (dry-run) run reports the reclaim plan WITHOUT
  // deleting — the rail runs it first to fill the confirm modal (D15 "confirmed op",
  // Constraint 5) — then a `preview=false` run commits the sweep on user confirm.
  // Returns the reclaim counts as a dock-local `GcSummary` (Decision D-gc-5); the
  // L4 impl drives `commands::gc_project` against the one owned `AppState`.
  virtual GcSummary clean_up(bool preview) = 0;

  // Undo / redo the in-process session by navigating libarbc's document-wide
  // transaction journal (D15 / A13 / editor.project.undo). Like `save()`, these act
  // on THIS process's one owned `Document`, not a sibling exec: the L4 impl drives
  // `commands::undo`/`commands::redo` against the held `AppState`. `undo`/`redo`
  // return whether the cursor moved; `can_undo`/`can_redo` gate the keyboard chord
  // so a no-op is never dispatched (Decision D-undo-3). The same dependency
  // inversion — the L3 dockspace reaches the L4 session through this seam, never an
  // illegal `dock -> commands` edge.
  virtual bool undo() = 0;
  virtual bool redo() = 0;
  virtual bool can_undo() const = 0;
  virtual bool can_redo() const = 0;

  // Every kind the session's `arbc::Registry` advertises, with the fields its
  // factory needs (A16). ONE entry per registered kind, unconditionally — the L4
  // impl marshals `scene::insert_schemas`, which holds no allowlist, and the rail
  // adds none. A kind the editor has never seen arrives with a single free-text
  // `config` field and is fully insertable.
  //
  // Non-pure with an empty default (unlike the entry/session verbs above) so the
  // gateway fakes of unrelated suites need no churn: an unwired gateway simply
  // offers nothing to insert. The shipped L4 impl overrides both.
  virtual std::vector<InsertKindSpec> insert_kinds() const { return {}; }

  // Insert one cell of `kind_id` from the modal's field `values`, at the session's
  // provisional placement (Constraint 7 — the overview-wireframe placement gesture
  // is `editor.panels.overview`'s, so the modal labels this as provisional). Acts on
  // THIS process's one owned session like `save()`; the L4 impl assembles the config
  // through `scene::build_config` and runs the `commands::dispatch` inside
  // `CanvasView::apply_edit` (Constraint 5).
  //
  // Errors are values: returns the EMPTY string on success, else the kind's own
  // error message — which the modal renders inline while staying open, with the
  // document untouched. That is why a kind whose factory always fails
  // (`org.arbc.fade`) is still offered rather than filtered out.
  virtual std::string insert_cell(const std::string& /*kind_id*/, const InsertValues& /*values*/) {
    return "Insert is unavailable.";
  }

  // Delete the project-level selection (editor.cells.remove / D19): the inverse of
  // `insert_cell`, and the seventh verb that acts on THIS process's one owned session
  // rather than spawning a sibling. `can_delete()` gates BOTH affordances — the rail item
  // is disabled (not hidden) when it is false, and the Delete/Backspace chord never
  // dispatches a no-op — exactly the shape `can_undo()`/`undo()` established (D-undo-3).
  // `delete_selected()` returns how many objects actually left the composition.
  //
  // The seam traffics in `bool`/`std::size_t` only: `dock` may include neither
  // `ace/scene` nor `ace/commands` (A12/A13), and two primitives carry everything the rail
  // and the e2e need, so this needs not even a dock-local POD (D-cells_remove-6). Non-pure
  // with inert defaults, exactly as `insert_kinds` above, so the gateway fakes of unrelated
  // suites need no churn: an unwired gateway simply has nothing to delete. The shipped L4
  // impl overrides both.
  virtual bool can_delete() const { return false; }
  virtual std::size_t delete_selected() { return 0; }

  // Frame Selection (editor.cameras.frame_selection / D19/D23): mint a camera fit to the
  // project-level selection's placed extent — the eighth verb that acts on THIS process's one
  // owned session, and the editor's FIRST camera-creation affordance. A rail ACTION, never a
  // fifth modal `ToolId`: D20 rules on this verb by name ("ad-hoc crop = frame a camera (D14),
  // not a mode"). `can_frame_selection()` gates the rail item — disabled, not hidden, when it
  // is false (Constraint 13) — and is deliberately the cheap "is anything selected?" question,
  // so an all-unbounded selection is an enabled item whose click mutates nothing and returns
  // false (D-frame_selection-7).
  //
  // Two `bool`s carry everything the rail and the e2e need, so like `can_delete`/
  // `delete_selected` this seam needs not even a dock-local POD: `dock` may include neither
  // `ace/scene` nor `ace/commands` (A12/A13). Non-pure with inert defaults, so the gateway
  // fakes of unrelated suites need no churn — an unwired gateway simply has nothing to frame.
  // The shipped L4 impl overrides both; `editor.panels.inspector` reuses this same pair.
  virtual bool can_frame_selection() const { return false; }
  virtual bool frame_selection() { return false; }

  // New Shot From View (editor.cameras.new_shot_from_view / D23): mint a camera that
  // promotes the viewport's CURRENT framing into a saved shot — D23's second mint verb, the
  // sibling of `frame_selection` above and, like it, a rail ACTION rather than a fifth modal
  // `ToolId` (D20). Where `frame_selection` derives its region from the document (the
  // selection's placed extent), this one derives it from SESSION state: the live canvas
  // pane's transient viewport camera and its device size, so the shot rendered at its own
  // resolution reproduces what was on screen (D23's per-verb clause).
  //
  // `can_new_shot_from_view()` answers "is there a live, SIZED canvas pane?" — the honest
  // precondition, because D18 has no keep-a-canvas guardrail and every canvas can be closed
  // (D-new_shot_from_view-2). Disabled, not hidden, when it is false (§10's fixed rail
  // geometry). `new_shot_from_view()` returns whether a camera was actually minted, so a
  // call with no live pane is a mutation-free `false`.
  //
  // Two `bool`s again — `dock` may include neither `ace/scene` nor `ace/commands` (A12/A16),
  // and the rail has no use for the minted id, so this seam needs not even a dock-local POD
  // (D-new_shot_from_view-3). Non-pure with inert defaults, so the gateway fakes of unrelated
  // suites need no churn. The shipped L4 impl overrides both; `editor.panels.inspector`
  // reuses this same pair.
  virtual bool can_new_shot_from_view() const { return false; }
  virtual bool new_shot_from_view() { return false; }
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

  // The Clean up (GC) confirm-modal state the rail drives (D-gc-3 / D15). Opened
  // once a dry-run preview resolves the reclaim counts; the modal shows them and
  // commits a real sweep on confirm. The scripted preview lives on the Dockspace so
  // the modal renders the same counts every frame until the user acts.
  bool gc_modal_open() const { return gc_modal_open_; }
  const GcSummary& gc_preview() const { return gc_preview_; }
  void open_gc_modal(GcSummary preview) {
    gc_preview_ = preview;
    gc_modal_open_ = true;
  }
  void close_gc_modal() { gc_modal_open_ = false; }

  // --- Insert Cell modal state (editor.cells.model / A16) --------------------
  // The kind list is SNAPSHOT from the gateway when the modal opens, so the modal
  // renders a stable list every frame (the BeginPopupModal balance rule) without
  // re-walking the registry per frame. `dock` never inspects a `kind_id`.
  bool insert_modal_open() const { return insert_modal_open_; }
  void open_insert_modal(std::vector<InsertKindSpec> kinds);
  void close_insert_modal() { insert_modal_open_ = false; }
  const std::vector<InsertKindSpec>& insert_kinds() const { return insert_kinds_; }

  // Which kind's fields the modal is editing, and the switch that re-seeds the
  // field buffers from that kind's prefills (Constraint 8 — a raster's resolution
  // arrives filled in, visible, and editable).
  std::size_t insert_selected_kind() const { return insert_selected_; }
  void select_insert_kind(std::size_t index);

  // The per-field editable buffers (plain char buffers — this build ships no
  // imgui_stdlib std::string overload). `field` indexes the SELECTED kind's fields;
  // an out-of-range index yields a scratch buffer so the modal can never fault.
  char* insert_field_buffer(std::size_t field);
  int insert_field_buffer_size() const { return static_cast<int>(k_insert_field_capacity); }
  std::string insert_field_value(std::size_t field) const;

  // The kind's own error string from the last refused insert — rendered inline
  // while the modal STAYS OPEN (errors are values). Empty means "no message".
  std::string& insert_error() { return insert_error_; }
  const std::string& insert_error() const { return insert_error_; }

  // Inline feedback the rail renders for the last project action (a non-project
  // Open selection, an unavailable recent, an invalid New name). The rail reads
  // and writes it; empty means "no message".
  std::string& project_feedback() { return project_feedback_; }

private:
  static constexpr std::size_t k_insert_field_capacity = 256;

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
  GcSummary gc_preview_;                      // the last Clean up dry-run reclaim counts
  std::vector<InsertKindSpec> insert_kinds_;  // the Insert Cell kind list, snapshot at open
  std::vector<std::array<char, k_insert_field_capacity>>
      insert_fields_; // the selected kind's editable field buffers
  std::array<char, k_insert_field_capacity> insert_scratch_{}; // out-of-range field sink
  std::string insert_error_;                                   // the last refused insert's message
  std::size_t insert_selected_ = 0;                            // index into insert_kinds_
  bool insert_modal_open_ = false;                             // the Insert Cell modal is showing
  bool new_project_modal_open_ = false;                        // the New Project modal is showing
  bool gc_modal_open_ = false;    // the Clean up confirm modal is showing
  bool built_ = false;            // DockBuilder seeded at least once
  bool rebuild_ = false;          // a programmatic open/reopen needs a re-seed
  unsigned int dockspace_id_ = 0; // ImGuiID; assigned on first draw()
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
