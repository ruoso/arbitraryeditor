#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/commands/cameras.hpp>
#include <ace/commands/cells.hpp>
#include <ace/commands/exec_new.hpp>
#include <ace/interact/interact.hpp>
#include <ace/interact/pick.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/expected.hpp>
#include <arbc/base/geometry.hpp>
#include <arbc/base/transform.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// editor.project.open_ui — the L4 gateway wiring the already-built terminal
// mechanisms (editor.project.open validate helpers, editor.project.exec_new
// sibling spawn, the RecentProjects prefs store) behind the dock::ProjectGateway
// seam. No second Document is ever minted here (D-open_ui-1).

namespace ace::app {

// --- ProjectEntryGateway: the session-free entry verbs (A22 / D-welcome-6) --------
// Moved here verbatim from AppProjectGateway once the pre-project launcher needed a
// gateway with no `AppState` behind it. ONE implementation of validate -> MRU-front ->
// spawn, so a launcher and a project window can never diverge on which targets they
// refuse or on whether a replay re-orders the MRU.

ProjectEntryGateway::ProjectEntryGateway(ace::dockmodel::RecentProjects& recent,
                                         const ace::platform::FileSystem& filesystem,
                                         FolderDialog& dialog,
                                         const ace::platform::ProcessLauncher& launcher,
                                         std::filesystem::path executable)
    : recent_(recent), filesystem_(filesystem), dialog_(dialog), launcher_(launcher),
      executable_(std::move(executable)) {}

ace::dock::ProjectEntryOutcome ProjectEntryGateway::spawn(const std::filesystem::path& dir) {
  // Empty error_code == a successful launch (D-open-6); a non-empty one means the
  // sibling `exec` failed. open_another_project canonicalizes `dir` to an absolute
  // path before handing it to the launcher (D-exec_new-4). This is the ONE place the
  // `error_code` becomes `spawn_failed` (A24 / D-entry_outcome-6) — the mapping cannot
  // drift because all three verbs terminate here.
  if (ace::commands::open_another_project(launcher_, executable_, dir)) {
    return ace::dock::ProjectEntryOutcome::spawn_failed;
  }
  return ace::dock::ProjectEntryOutcome::succeeded;
}

ace::dock::ProjectEntryOutcome ProjectEntryGateway::open_project(const std::filesystem::path& dir) {
  if (!ace::project::is_project_directory(filesystem_, dir)) {
    // A non-project selection surfaces an error and spawns nothing — the user picks a
    // different folder, which is what makes it a `refused_target` rather than a failure.
    return ace::dock::ProjectEntryOutcome::refused_target;
  }
  // Recorded AFTER validating and BEFORE spawning, unchanged (A24 / Constraint 8): a failed
  // prefs write is no longer load-bearing for what the user is told, so it degrades to "the
  // entry is missing from the list" instead of "your project is not a project".
  recent_.add(dir);
  return spawn(dir);
}

ace::dock::ProjectEntryOutcome ProjectEntryGateway::new_project(const std::filesystem::path& parent,
                                                                const std::string& name) {
  const std::optional<std::filesystem::path> target =
      ace::project::compose_new_project_target(parent, name);
  if (!target.has_value()) {
    return ace::dock::ProjectEntryOutcome::refused_target; // empty / invalid / traversing name
  }
  // Pre-check the target HERE, before the spawn (D27 / D-dir_is_project-3). `project::
  // create_project` refuses an existing target too, and that L1 guard is the invariant — but
  // New creates nothing in this process: it spawns a DETACHED sibling (D19/A7) whose bootstrap
  // create-branch would raise the refusal in a window that never appears, leaving the user
  // staring at an unchanged screen having burned a process launch. So this is the entire
  // user-visible half of the rule on the New path, and it is cheap — `filesystem_` is already
  // here for `open_project`'s and `recent_projects`'s validation. The L1 check still stands
  // behind it: between this check and the child's create the directory can appear, and L1 is
  // what guarantees nothing is written into it.
  if (filesystem_.exists(*target)) {
    // Same outcome as an invalid name, deliberately (D-entry_outcome-2 / D-dir_is_project-6):
    // the split is by corrective act, and both are answered by typing a different name.
    return ace::dock::ProjectEntryOutcome::refused_target;
  }
  // Do NOT record: the target does not exist yet — the child's create-branch
  // scaffolds it (D-open_ui-4), and it lands in the recent list on its own open.
  return spawn(*target);
}

ace::dock::ProjectEntryOutcome ProjectEntryGateway::open_recent(const std::filesystem::path& dir) {
  if (!ace::project::is_project_directory(filesystem_, dir)) {
    // Pruned away since the list was last rendered.
    return ace::dock::ProjectEntryOutcome::refused_target;
  }
  recent_.add(dir); // replay re-orders the entry MRU-front
  return spawn(dir);
}

void ProjectEntryGateway::pick_folder(
    std::function<void(std::optional<std::filesystem::path>)> on_pick) {
  dialog_.show(std::move(on_pick));
}

std::vector<std::filesystem::path> ProjectEntryGateway::recent_projects() const {
  // Prune through the L1 predicate, bound to our FileSystem — dockmodel may not
  // depend on `project`, so the validity check is injected here (A12 / §8).
  return recent_.load([this](const std::filesystem::path& dir) {
    return ace::project::is_project_directory(filesystem_, dir);
  });
}

// --- AppProjectGateway: the session-owning half (A13) -----------------------------

AppProjectGateway::AppProjectGateway(ace::dockmodel::RecentProjects& recent,
                                     const ace::platform::FileSystem& filesystem,
                                     FolderDialog& dialog,
                                     const ace::platform::ProcessLauncher& launcher,
                                     std::filesystem::path executable,
                                     ace::commands::AppState& app_state)
    : ProjectEntryGateway(recent, filesystem, dialog, launcher, std::move(executable)),
      app_state_(app_state) {}

bool AppProjectGateway::save() {
  // Publish the in-process session (A13), not a sibling exec. `commands::save_project`
  // dumps `project.arbc` + `assets/` and marks the session clean on success; a
  // returned error value is surfaced to the rail as a failed Save (session stays
  // dirty). Errors are values — never a throw across the seam. Only the CAPTURE crosses to the
  // writer thread (`writer_post_`); the serialize + atomic publish run here, so a disk write
  // never holds the one writer thread against a queued edit (D-writer_thread-7).
  return ace::commands::save_project(app_state_, filesystem_, writer_post_).has_value();
}

bool AppProjectGateway::is_dirty() const { return app_state_.is_dirty(); }

std::size_t AppProjectGateway::reopen_unbindable_count() const {
  return app_state_.unbindable_content_records();
}

bool AppProjectGateway::undo() {
  // Navigate the in-process session's journal (D15 / A13), not a sibling exec.
  // `commands::undo` drives `journal().undo()` as a forward publish and reports whether
  // the cursor moved; it never touches the dirty baseline (D-undo-4). `journal().undo()` is a
  // structural write via `Model::navigate`, so it is WRITER-THREAD ONLY — hand it to the edit
  // runner as a closure, which POSTS it to the document's one writer thread (via
  // CanvasView::apply_edit) and then wakes the canvas (editor.canvas.writer_thread). The render
  // read is lock-free: arbc content bindings publish copy-on-write (#10/#11).
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
  // With a runner installed (the shell binds it to CanvasView::apply_edit) the mutation is
  // POSTED to the document's one writer thread and this call blocks until it has run. Without
  // one — a headless test or a session with no live canvas — run it directly on the calling
  // thread, which IS the one identity there (D-writer_thread-5's degenerate mode by another
  // name), so nothing races it.
  if (run_edit_) {
    run_edit_(edit);
  } else {
    edit();
  }
}

void AppProjectGateway::set_edit_runner(std::function<void(const std::function<void()>&)> runner) {
  run_edit_ = std::move(runner);
}

void AppProjectGateway::set_writer_post(ace::project::WriterPost post) {
  writer_post_ = std::move(post);
}

// ANY-THREAD atomic reads since the v0.3.0 pin (arbc#15): the journal publishes its cursor and
// depth as relaxed atomics, so the rail can poll them every frame off the writer thread with no
// editor-side snapshot (D-writer_thread-12).
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

bool AppProjectGateway::save_as(const std::filesystem::path& parent, const std::string& name) {
  // Save As is NEW's compose shape applied to THIS session (D27 / D-dir_is_project-4): the
  // rail picks the parent through `pick_folder` and types the name into the shared compose
  // modal, so what arrives here is already a parent-plus-name pair and this call is
  // SYNCHRONOUS. That is the whole repair: the previous shape opened the folder dialog itself
  // and dropped the orchestrator's error value on the far side of the async pick, with no rail
  // feedback channel to put it on. Now the outcome is this function's return value and lands
  // in `project_feedback_` on the same frame.
  //
  // The same L1 compose the New path uses — `nullopt` for an empty parent, an empty/blank
  // name, or a name that is not a single path component, so no name can escape `parent` by
  // traversal (D-open_ui-4). No `filesystem_.exists` pre-check is needed (unlike
  // `new_project`'s, D-dir_is_project-3): the publish happens IN THIS PROCESS, so
  // `project::save_project_as`'s own D27 guard refuses an existing target as a value that
  // reaches the user directly — and it refuses before writing anything or touching the
  // launcher. The current session is left untouched either way (D-save_as-2).
  const std::optional<std::filesystem::path> target =
      ace::project::compose_new_project_target(parent, name);
  if (!target.has_value()) {
    return false; // empty / invalid / traversing name: nothing published, nothing spawned
  }
  return ace::commands::save_project_as(app_state_, filesystem_, launcher_, executable_, *target,
                                        writer_post_)
      .has_value();
}

void AppProjectGateway::set_view_framing(std::function<ViewFraming()> framing) {
  view_framing_ = std::move(framing);
}

std::optional<ViewFraming> AppProjectGateway::live_view_framing() const {
  if (view_framing_) {
    const ViewFraming live = view_framing_();
    if (live.pane_w > 0 && live.pane_h > 0) {
      return live;
    }
  }
  return std::nullopt; // no provider, or no canvas pane with area yet
}

ViewFraming AppProjectGateway::view_framing() const {
  if (const std::optional<ViewFraming> live = live_view_framing()) {
    return *live;
  }
  // No live canvas (a headless gateway test, or a session whose Canvas pane has not
  // been sized yet): frame the root composition itself at identity, so the
  // provisional placement is still centred in something real rather than degenerate.
  // This fallback is `insert_cell`'s ALONE — `new_shot_from_view` reads the optional
  // above directly and refuses instead (D-new_shot_from_view-2).
  ViewFraming fallback;
  if (const std::optional<ace::project::CompositionSize> size =
          ace::project::root_composition_size(app_state_.document())) {
    fallback.pane_w = static_cast<int>(size->width);
    fallback.pane_h = static_cast<int>(size->height);
  }
  return fallback;
}

std::vector<ace::dock::InsertKindSpec> AppProjectGateway::insert_kinds() const {
  // The L1 -> dock POD marshalling (D-cells_model-5), the exact shape `clean_up`
  // uses for `GcSummary`: `dock` may include neither `ace/scene` nor `ace/commands`,
  // and L4 is the level that sees both. The list is passed through ENTIRELY — this
  // is a `for` over what `scene::insert_schemas` returned, never a filter.
  const std::vector<ace::scene::KindInsertSchema> schemas = ace::scene::insert_schemas(
      app_state_.registry(), ace::project::root_composition_size(app_state_.document()));
  std::vector<ace::dock::InsertKindSpec> specs;
  specs.reserve(schemas.size());
  for (const ace::scene::KindInsertSchema& schema : schemas) {
    ace::dock::InsertKindSpec spec;
    spec.kind_id = schema.kind_id;
    spec.human_name = schema.human_name;
    spec.fields.reserve(schema.fields.size());
    for (const ace::scene::InsertField& field : schema.fields) {
      spec.fields.push_back(ace::dock::InsertFieldSpec{field.id, field.label, field.initial});
    }
    specs.push_back(std::move(spec));
  }
  return specs;
}

std::string AppProjectGateway::insert_cell(const std::string& kind_id,
                                           const ace::dock::InsertValues& values) {
  const std::vector<ace::scene::KindInsertSchema> schemas = ace::scene::insert_schemas(
      app_state_.registry(), ace::project::root_composition_size(app_state_.document()));
  const ace::scene::KindInsertSchema* schema = nullptr;
  for (const ace::scene::KindInsertSchema& candidate : schemas) {
    if (candidate.kind_id == kind_id) {
      schema = &candidate;
      break;
    }
  }
  if (schema == nullptr) {
    return kind_id + ": not a registered kind";
  }
  // Errors are values at every step, and each step leaves the Document untouched.
  const arbc::expected<std::string, std::string> config = ace::scene::build_config(*schema, values);
  if (!config) {
    return config.error();
  }
  // Placement is a value the CALLER computes (Constraint 6): probe the extent the
  // factory-built content would report, then centre it in the region the pane is
  // currently showing. `editor.panels.overview` later swaps a drag-derived affine in
  // right here, with no change to `scene`.
  const arbc::expected<std::optional<arbc::Rect>, std::string> bounds =
      ace::scene::probe_bounds(app_state_.registry(), kind_id, *config);
  if (!bounds) {
    return bounds.error();
  }
  const ViewFraming framing = view_framing();
  const arbc::Affine placement =
      ace::interact::place_in_view(framing.camera, framing.pane_w, framing.pane_h, *bounds);
  ace::commands::InsertCellOutcome outcome;
  const ace::commands::Command command = ace::commands::insert_cell_command(
      app_state_.registry(), kind_id, *config, placement, outcome);
  // Every UI-driven insert runs inside CanvasView::apply_edit (Constraint 5 /
  // edit_render_sync Constraint 1, which names cells) — never a bare dispatch+poke.
  run_edit([this, &command] { ace::commands::dispatch(app_state_, command); });
  return outcome.error;
}

bool AppProjectGateway::can_delete() const { return ace::commands::can_delete(app_state_); }

std::size_t AppProjectGateway::delete_selected() {
  // Every UI-driven delete runs inside CanvasView::apply_edit (Constraint 4), exactly as
  // insert does: `arbc::Document::remove_content` is WRITER-THREAD ONLY, so a bare dispatch
  // from the UI thread is forbidden. `delete_selection` resolves its targets INSIDE the
  // closure, on the writer thread, against the live document (D-cells_remove-3).
  std::size_t removed = 0;
  run_edit([this, &removed] { removed = ace::commands::delete_selection(app_state_).removed; });
  return removed;
}

bool AppProjectGateway::can_frame_selection() const {
  return ace::commands::can_frame_selection(app_state_);
}

bool AppProjectGateway::frame_selection() {
  // The whole join runs INSIDE the edit closure, on the writer thread, against the live
  // document (D-frame_selection-5 / Constraint 7): `add_camera` opens transactions, and the
  // geometry the transaction is derived from must come from the same generation it lands on —
  // never from a UI-thread pick-target cache taken frames earlier.
  bool minted = false;
  run_edit([this, &minted] {
    const std::vector<ace::interact::PickTarget> targets =
        ace::interact::pick_targets(app_state_.document(), app_state_.registry());
    const std::optional<arbc::Rect> extent =
        ace::interact::selected_extent(targets, app_state_.selection().items());
    if (!extent.has_value()) {
      return; // nothing bounded to frame: mutate nothing, refuse as a value (Constraint 5)
    }
    const ace::interact::ShotFraming shot = ace::interact::shot_from_extent(*extent);
    if (shot.width <= 0 || shot.height <= 0) {
      return; // the degenerate-extent sentinel; still a mutation-free refusal
    }
    ace::commands::AddCameraOutcome outcome;
    const ace::commands::Command command = ace::commands::add_camera_command(
        app_state_.registry(), ace::commands::next_camera_name(app_state_.document()),
        ace::scene::Resolution{shot.width, shot.height}, shot.frame, outcome);
    ace::commands::dispatch(app_state_, command);
    // The mint touches NEITHER the selection NOR any canvas's look-through camera
    // (D-frame_selection-10): framing then re-framing the same set must not need a re-select,
    // and which camera a canvas looks through is transient session state, not scene data.
    minted = outcome.camera.valid();
  });
  return minted;
}

bool AppProjectGateway::can_new_shot_from_view() const {
  // NOT a `commands::` predicate: the question is "does a canvas pane exist and have a
  // size?", which lives in CanvasView/Presenter — L4/L2 session state `commands` (-> {base,
  // project, scene}) structurally cannot see (D-new_shot_from_view-6).
  return live_view_framing().has_value();
}

bool AppProjectGateway::new_shot_from_view() {
  // D23's second mint verb, joined at L4 for the same forced reason as `frame_selection`
  // above (`commands` may not include `interact`, `interact` may not include `commands`) and
  // run entirely INSIDE the edit closure: `add_camera` is writer-thread only, and the pane
  // size the transaction records must be the one live when the transaction lands, not one
  // sampled frames earlier (D-new_shot_from_view-4).
  bool minted = false;
  run_edit([this, &minted] {
    const std::optional<ViewFraming> framing = live_view_framing();
    if (!framing.has_value()) {
      return; // no live, sized pane: refuse as a value, never the composition fallback
    }
    // The derivation ships unchanged (D-new_shot_from_view-1): resolution = the pane in
    // DEVICE PIXELS and frame = the viewport camera inverted, with D23's rounding, clamp and
    // aspect-expansion deliberately not applied — a pane is already a whole number of square
    // pixels bounded by the display, so all three steps are no-ops or guard a hazard that
    // cannot arise here. That is what makes the shot, rendered at its own resolution,
    // reproduce what was on screen.
    const ace::interact::ShotFraming shot =
        ace::interact::new_shot_from_view(framing->camera, framing->pane_w, framing->pane_h);
    if (shot.width <= 0 || shot.height <= 0) {
      return; // the degenerate-pane sentinel; still a mutation-free refusal
    }
    ace::commands::AddCameraOutcome outcome;
    const ace::commands::Command command = ace::commands::add_camera_command(
        app_state_.registry(), ace::commands::next_camera_name(app_state_.document()),
        ace::scene::Resolution{shot.width, shot.height}, shot.frame, outcome);
    ace::commands::dispatch(app_state_, command);
    // One `Camera <n>` sequence shared with `frame_selection`, and the mint touches neither
    // the selection nor any canvas's look-through camera (D-new_shot_from_view-7): promoting
    // the view must not CHANGE the view, or the user cannot promote twice from one place.
    minted = outcome.camera.valid();
  });
  return minted;
}

} // namespace ace::app
