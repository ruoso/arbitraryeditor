#pragma once

#include <ace/app/view_framing.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ace::app {

class FolderDialog;

// The L4 concrete project-entry gateway (docs/01-architecture.md A12, D22). It is
// the sole holder of the native folder dialog seam, the dockmodel::RecentProjects
// prefs store, the platform::ProcessLauncher + current-executable path, and the
// L1 `project` validate/compose helpers. Every mutating action terminates in
// commands::open_another_project — a detached SIBLING `exec` (D19/A7): this
// process's one Document is never swapped and open_project/create_project are
// never called in-process. Errors are values (no throws across the seam).
//
// The Save + dirty query (A13) are the exception: they act on the ONE in-process
// `AppState` this gateway now holds (`save()` -> `commands::save_project`,
// `is_dirty()` -> `AppState::is_dirty()`), not a sibling process. The same seam,
// same dependency inversion — the L3 rail reaches the L4 session through it without
// an illegal `dock -> commands` edge.
class AppProjectGateway final : public ace::dock::ProjectGateway {
public:
  AppProjectGateway(ace::dockmodel::RecentProjects& recent,
                    const ace::platform::FileSystem& filesystem, FolderDialog& dialog,
                    const ace::platform::ProcessLauncher& launcher,
                    std::filesystem::path executable, ace::commands::AppState& app_state);

  bool open_project(const std::filesystem::path& dir) override;
  bool new_project(const std::filesystem::path& parent, const std::string& name) override;
  bool open_recent(const std::filesystem::path& dir) override;
  void pick_folder(std::function<void(std::optional<std::filesystem::path>)> on_pick) override;
  std::vector<std::filesystem::path> recent_projects() const override;
  bool save() override;
  bool is_dirty() const override;
  void save_as() override;
  ace::dock::GcSummary clean_up(bool preview) override;
  bool undo() override;
  bool redo() override;
  bool can_undo() const override;
  bool can_redo() const override;

  // Install the edit-serializing runner the UI-thread edit verbs (undo/redo) funnel
  // their Document mutation through (editor.canvas.edit_render_sync, D-edit_render_sync-2).
  // The runner receives the mutation as a closure, runs it on the UI/writer thread, and
  // wakes the canvas — the shell binds it to `CanvasHost::apply_edit`. The off-thread render
  // read needs no lock: arbc v0.2.0 publishes content bindings copy-on-write (#10/#11), so
  // the render walk reads a stable snapshot while the edit rebinds (editor.canvas.single_writer,
  // superseding edit_render_sync's doc_mu). The runner keeps every edit on the one writer
  // identity, replacing frame_sync's fire-after poke that mutated the Document BEFORE the
  // wake. Default: none, so a
  // headless test or a session without a live canvas runs the mutation directly on the
  // calling thread (behaviour-identical, still single-threaded).
  void set_edit_runner(std::function<void(const std::function<void()>&)> runner);

  // --- Insert Cell (editor.cells.model / A16) --------------------------------
  // Marshal L1 `scene::insert_schemas` — one entry per `registry.ids()` entry, no
  // allowlist — into the dock-local POD the rail renders.
  std::vector<ace::dock::InsertKindSpec> insert_kinds() const override;

  // Assemble the kind's opaque config from `values` (`scene::build_config`), compute
  // the provisional placement from the live canvas framing (`interact::place_in_view`
  // over the extent the factory-built content would report), and dispatch the insert
  // through `run_edit` so the mutation runs inside `CanvasView::apply_edit`
  // (Constraint 5). Returns the kind's own error string, or empty on success.
  std::string insert_cell(const std::string& kind_id,
                          const ace::dock::InsertValues& values) override;

  // --- Delete Selected (editor.cells.remove) ---------------------------------
  // The inverse of `insert_cell`, over the ONE project-level selection (D19). `can_delete`
  // is the cheap gate both affordances poll each frame; `delete_selected` resolves the
  // targets against the live document and dispatches one `remove_cell_command` per object,
  // with the mutation wrapped in `run_edit` so it runs inside `CanvasView::apply_edit`
  // (Constraint 5) — `arbc::Document::remove_content` is WRITER-THREAD ONLY. Returns how
  // many objects actually left the composition (a stale selected id is skipped).
  bool can_delete() const override;
  std::size_t delete_selected() override;

  // --- Frame Selection (editor.cameras.frame_selection / D23) ------------------
  // The ten-line join L1 structurally cannot host (`commands` may not include `interact`,
  // `interact` may not include `commands`), performed INSIDE `run_edit` on the writer thread
  // against the LIVE document (D-frame_selection-5): `interact::pick_targets` ->
  // `interact::selected_extent` -> `interact::shot_from_extent` ->
  // `commands::add_camera_command` -> `dispatch`. Resolving the geometry inside the closure is
  // D-cells_remove-3's rule strengthened — here the EXTENTS, not just the ids, must come from
  // the generation the transaction lands on. `can_frame_selection` is the cheap gate both the
  // rail and (later) the inspector poll every frame; `frame_selection` returns whether a camera
  // was actually minted, so an all-unbounded selection is an honest, mutation-free `false`.
  bool can_frame_selection() const override;
  bool frame_selection() override;

  // Install the source of the provisional placement's framing — the shell binds it to
  // the live canvas's transient viewport camera + pane size, read by value per insert
  // (Constraint 7). Default: none, so a headless gateway (no canvas) frames the root
  // composition's own extent instead.
  void set_view_framing(std::function<ViewFraming()> framing);

private:
  bool spawn(const std::filesystem::path& dir);
  // The framing the provisional placement is computed in: the live canvas's when one
  // is wired and sized, else the root composition's own extent at identity.
  ViewFraming view_framing() const;
  // Run a Document-mutating edit through the installed runner (serialized against the
  // render read via CanvasHost::apply_edit), or directly when none is installed.
  void run_edit(const std::function<void()>& edit);

  ace::dockmodel::RecentProjects& recent_;
  const ace::platform::FileSystem& filesystem_;
  FolderDialog& dialog_;
  const ace::platform::ProcessLauncher& launcher_;
  std::filesystem::path executable_;
  ace::commands::AppState& app_state_; // the one in-process session (A7) Save/dirty drive
  // Serializes a UI-thread edit against the off-thread render read (bound to
  // CanvasHost::apply_edit by the shell); null in headless tests -> run directly.
  std::function<void(const std::function<void()>&)> run_edit_;
  // The live canvas framing source for a cell insert's provisional placement; null in
  // headless tests -> fall back to the root composition's extent.
  std::function<ViewFraming()> view_framing_;
};

} // namespace ace::app
