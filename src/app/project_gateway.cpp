#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/commands/exec_new.hpp>
#include <ace/project/project.hpp>

#include <utility>

// editor.project.open_ui — the L4 gateway wiring the already-built terminal
// mechanisms (editor.project.open validate helpers, editor.project.exec_new
// sibling spawn, the RecentProjects prefs store) behind the dock::ProjectGateway
// seam. No second Document is ever minted here (D-open_ui-1).

namespace ace::app {

AppProjectGateway::AppProjectGateway(ace::dockmodel::RecentProjects& recent,
                                     const ace::platform::FileSystem& filesystem,
                                     FolderDialog& dialog,
                                     const ace::platform::ProcessLauncher& launcher,
                                     std::filesystem::path executable,
                                     ace::commands::AppState& app_state)
    : recent_(recent), filesystem_(filesystem), dialog_(dialog), launcher_(launcher),
      executable_(std::move(executable)), app_state_(app_state) {}

bool AppProjectGateway::spawn(const std::filesystem::path& dir) {
  // Empty error_code == a successful launch (D-open-6); a non-empty one means the
  // sibling `exec` failed. open_another_project canonicalizes `dir` to an absolute
  // path before handing it to the launcher (D-exec_new-4).
  return !static_cast<bool>(ace::commands::open_another_project(launcher_, executable_, dir));
}

bool AppProjectGateway::open_project(const std::filesystem::path& dir) {
  if (!ace::project::is_project_directory(filesystem_, dir)) {
    return false; // a non-project selection surfaces an error and spawns nothing
  }
  recent_.add(dir);
  return spawn(dir);
}

bool AppProjectGateway::new_project(const std::filesystem::path& parent, const std::string& name) {
  const std::optional<std::filesystem::path> target =
      ace::project::compose_new_project_target(parent, name);
  if (!target.has_value()) {
    return false; // empty / invalid / traversing name
  }
  // Do NOT record: the target does not exist yet — the child's create-branch
  // scaffolds it (D-open_ui-4), and it lands in the recent list on its own open.
  return spawn(*target);
}

bool AppProjectGateway::open_recent(const std::filesystem::path& dir) {
  if (!ace::project::is_project_directory(filesystem_, dir)) {
    return false; // pruned away since the list was last rendered
  }
  recent_.add(dir); // replay re-orders the entry MRU-front
  return spawn(dir);
}

void AppProjectGateway::pick_folder(
    std::function<void(std::optional<std::filesystem::path>)> on_pick) {
  dialog_.show(std::move(on_pick));
}

std::vector<std::filesystem::path> AppProjectGateway::recent_projects() const {
  // Prune through the L1 predicate, bound to our FileSystem — dockmodel may not
  // depend on `project`, so the validity check is injected here (A12 / §8).
  return recent_.load([this](const std::filesystem::path& dir) {
    return ace::project::is_project_directory(filesystem_, dir);
  });
}

bool AppProjectGateway::save() {
  // Publish the in-process session (A13), not a sibling exec. `commands::save_project`
  // dumps `project.arbc` + `assets/` and marks the session clean on success; a
  // returned error value is surfaced to the rail as a failed Save (session stays
  // dirty). Errors are values — never a throw across the seam.
  return ace::commands::save_project(app_state_, filesystem_).has_value();
}

bool AppProjectGateway::is_dirty() const { return app_state_.is_dirty(); }

bool AppProjectGateway::undo() {
  // Navigate the in-process session's journal (D15 / A13), not a sibling exec.
  // `commands::undo` drives `journal().undo()` as a forward publish and reports whether
  // the cursor moved; it never touches the dirty baseline (D-undo-4). The journal drive
  // MUTATES the Document, which the off-thread render thread reads every frame — hand it to
  // the edit runner as a closure, which runs it on the UI/writer thread (via
  // CanvasHost::apply_edit) and then wakes the canvas (editor.canvas.single_writer). The
  // render read is lock-free: arbc v0.2.0 content bindings publish copy-on-write (#10/#11).
  // This replaces frame_sync's fire-after poke, which mutated the Document before the wake.
  bool moved = false;
  run_edit([this, &moved] { moved = ace::commands::undo(app_state_).moved; });
  return moved;
}

bool AppProjectGateway::redo() {
  bool moved = false;
  run_edit([this, &moved] { moved = ace::commands::redo(app_state_).moved; });
  return moved;
}

void AppProjectGateway::run_edit(const std::function<void()>& edit) {
  // With a runner installed (the shell binds it to CanvasHost::apply_edit) the mutation
  // runs serialized against the render read. Without one — a headless test or a session
  // with no live canvas — run it directly on the calling (writer) thread: behaviour-
  // identical and still single-threaded, so nothing races it.
  if (run_edit_) {
    run_edit_(edit);
  } else {
    edit();
  }
}

void AppProjectGateway::set_edit_runner(std::function<void(const std::function<void()>&)> runner) {
  run_edit_ = std::move(runner);
}

bool AppProjectGateway::can_undo() const { return app_state_.document().journal().can_undo(); }

bool AppProjectGateway::can_redo() const { return app_state_.document().journal().can_redo(); }

ace::dock::GcSummary AppProjectGateway::clean_up(bool preview) {
  // Clean up (GC) acts on the in-process session (A13), not a sibling exec: drive
  // `commands::gc_project` against the one owned `AppState`. This is where the
  // arbc -> project -> dock type mapping lives (D-gc-5): the L1 sweep returns a
  // `project::GcOutcome`, which we re-vocabularize into the dock-local `GcSummary`
  // the rail's confirm modal renders. `preview` (dry-run) reports the plan without
  // deleting; `preview=false` commits. Errors are values — a failed/guarded sweep
  // returns `ran=false` (no phantom reclaim), never a throw across the seam.
  const ace::platform::Result<ace::project::GcOutcome> outcome =
      ace::commands::gc_project(app_state_, /*dry_run=*/preview);
  if (!outcome.has_value()) {
    return {}; // fail-safe: nothing reclaimed, ran = false
  }
  return ace::dock::GcSummary{outcome.value().deleted, outcome.value().bytes_reclaimed, true};
}

void AppProjectGateway::save_as() {
  // Save As is New/Open's async-pick shape (D-save_as-3) applied to THIS session:
  // pick a target folder, then publish a copy there and exec a sibling on it. The
  // orchestrator leaves the current session untouched (D-save_as-2), so this process
  // keeps running on its original project. A cancelled pick does nothing. The
  // callback captures only `this`, whose collaborators (dialog_/launcher_/filesystem_/
  // app_state_) all outlive the gateway; a returned error value from the orchestrator
  // is swallowed here (no rail feedback channel across the async pick this leaf).
  dialog_.show([this](std::optional<std::filesystem::path> picked) {
    if (!picked.has_value()) {
      return;
    }
    ace::commands::save_project_as(app_state_, filesystem_, launcher_, executable_, *picked);
  });
}

} // namespace ace::app
