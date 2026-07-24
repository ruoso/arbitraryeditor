#include <ace/commands/app_state.hpp>
#include <ace/commands/exec_new.hpp>
#include <ace/scene/camera.hpp>

#include <arbc/builtin_kinds.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/model/model.hpp>

#include <filesystem>
#include <system_error>
#include <utility>

namespace ace::commands {
namespace {

// The editor's custom libarbc `Content` kinds — the SINGLE list of "which editor
// kinds exist" (D-reopen-2), reused on BOTH sides of the persist boundary: the
// persistent SAVE registry seeds it beside the built-ins (below), and the LOAD path
// applies it as `open_project`'s extra-kinds callback (`open_or_create_app_state`),
// so a persisted editor kind reopens as its live typed `Content` rather than degrading
// to `arbc::PlaceholderContent` (editor.cameras.reopen_codec). Registers only the
// editor kinds, NOT the built-ins — both call sites seed built-ins separately
// (`register_builtin_kinds`). Idempotent / first-wins (each `register_*_kind` ignores a
// duplicate), so it is safe to apply to a registry that already carries the kind
// (Constraint 4). `commands` is the lowest component that links both `project` and
// `scene`, so naming `scene::register_camera_kind` here adds no `project->scene` edge
// (Constraint 1). When a second custom kind ships, adding it HERE restores it on reopen
// for free.
void register_editor_kinds(arbc::Registry& registry) { scene::register_camera_kind(registry); }

// The raw single-step navigations, WITHOUT the publish epilogue. `undo`/`redo` are these
// plus one `publish_history`; `navigate_to` walks them directly and publishes ONCE at the
// end, so a jump across a long journal rebuilds the snapshot once rather than per step
// (D-history_published_reads-4 (iv)).
UndoOutcome undo_step(AppState& state) {
  arbc::Document& doc = state.document();
  const bool moved = doc.journal().undo();
  return UndoOutcome{moved, doc.pin()->revision(), doc.journal().can_undo(),
                     doc.journal().can_redo()};
}

UndoOutcome redo_step(AppState& state) {
  arbc::Document& doc = state.document();
  const bool moved = doc.journal().redo();
  return UndoOutcome{moved, doc.pin()->revision(), doc.journal().can_undo(),
                     doc.journal().can_redo()};
}

} // namespace

AppState::AppState(project::OpenedProject opened)
    // The document's raster hash memo, taken over from the `OpenedProject` that minted it
    // beside the `Document` (A23): seeded on a rebuild-from-canonical open, cold on a
    // workspace map or a fresh create. `AppState` is its process-lifetime owner for the same
    // reason it owns the persistent `Registry` and the document-scoped `KindBridge` (A7).
    : document_(std::move(opened.document)), tiles_(std::move(opened.tiles)),
      layout_(std::move(opened.layout)), rebuilt_from_canonical_(opened.rebuilt_from_canonical),
      // The A19 degradation count, ferried not recomputed (D-reopen_degradation_notice-1):
      // `project::open_project` produces it once at bootstrap and this is the only place
      // the UI can reach it — the dock's open verbs spawn SIBLING processes and never see
      // an `OpenedProject`, so the in-process session owns the fact for its own window.
      unbindable_content_records_(opened.unbindable_content_records),
      history_(std::make_unique<HistoryPublisher>()) {
  // The persistent, lifetime-scoped kind Registry (D-open-7): seeded once here,
  // not rebuilt per open. `save`/export and the future A6 plugin seam reuse it.
  arbc::register_builtin_kinds(registry_);
  // The editor's custom kinds (editor.cameras.model A14): register `org.arbc.camera`'s
  // factory + codec beside the built-ins so the generic snapshot save
  // (`project::save_project(state.registry())`) serializes persisted shot cameras — the
  // registration wired at a level that already sees `scene`, with no `project->scene`
  // edge (Constraint 1). The SAME registrar restores those kinds on reopen via
  // `open_project`'s callback (`open_or_create_app_state`, D-reopen-2).
  register_editor_kinds(registry_);
  // The document-scoped kind bridge (D-writer_thread-9): seeded from the SAME registry the
  // save/load bridges seed from (A14), so an external arrival the writer settles interns its
  // kind tokens identically to the author-side mints. Every canvas over this document is handed
  // this one bridge — the document holds a single external-load settler slot.
  project::seed_kind_bridge(kind_bridge_, registry_);
  // The dirty baseline (D-save-4): a session rebuilt from the canonical `project.arbc`
  // starts CLEAN (the workspace was just built from the snapshot); a fresh
  // `create_project` or a workspace-mapped open starts DIRTY (`saved_revision_`
  // stays `k_none` — no known-published snapshot this session).
  if (rebuilt_from_canonical_) {
    saved_revision_.store(document_->pin()->revision());
  }
  // The initial publication (A18): a reopened session may already carry journal entries,
  // and a fresh one publishes an empty snapshot — either way the History panel loads a
  // valid, non-null pointer on frame 0, before any edit has run a refresh.
  history_->refresh(document_->journal());
}

void publish_history(AppState& state) { state.history().refresh(state.document().journal()); }

DispatchOutcome dispatch(AppState& state, const Command& command) {
  arbc::Document& doc = state.document();
  const std::size_t before = doc.journal().depth();
  if (command.apply) {
    command.apply(doc); // writer-thread, synchronous (A4); the wrappers self-commit
  }
  DispatchOutcome outcome;
  outcome.journal_entries_added = doc.journal().depth() - before;
  outcome.revision = doc.pin()->revision();
  // The writer-turn epilogue (A18): republish the entry-name snapshot the History panel
  // reads, so `commands` is self-consistent with no L4 present (the headless / Catch2
  // path, where no `CanvasHost` post-edit hook exists). Stamp-guarded, so a command that
  // journals nothing costs two atomic loads.
  publish_history(state);
  return outcome;
}

UndoOutcome undo(AppState& state) {
  // Undo IS the library journal (D15 / Constraint 1): drive the cursor and report
  // whether it moved plus the resulting revision / can-undo / can-redo state. No
  // editor-side inverse list — `journal().undo()` republishes the touched objects at
  // their *before* edge as a forward publish. Writer-thread (A4); `mark_saved` is
  // deliberately NOT called (D-undo-4 / Constraint 4). The published history snapshot is
  // refreshed on the way out (A18): navigation moves the cursor, which is half the
  // panel's model.
  const UndoOutcome outcome = undo_step(state);
  publish_history(state);
  return outcome;
}

UndoOutcome redo(AppState& state) {
  const UndoOutcome outcome = redo_step(state);
  publish_history(state);
  return outcome;
}

NavigateOutcome navigate_to(AppState& state, std::size_t target_cursor) {
  // The bounded, end-stopped single-step walk that used to live in the History panel
  // (D-history-5, now D-history_published_reads-4). Clamp first — a target beyond the tip
  // lands on the tip, and the loops below are their own defence anyway: each stops the
  // moment its verb reports no move, so a rare writer-path allocation failure ends the
  // jump rather than spinning. Navigation goes ONLY through the library's single-step
  // verbs; nothing here reimplements a stack (Constraint 1 / D15).
  arbc::Document& doc = state.document();
  const std::size_t depth = doc.journal().depth();
  const std::size_t target = target_cursor > depth ? depth : target_cursor;

  std::size_t steps = 0;
  while (doc.journal().cursor() > target && undo_step(state).moved) {
    ++steps;
  }
  while (doc.journal().cursor() < target && redo_step(state).moved) {
    ++steps;
  }

  // ONE publication for the whole jump, not one per step (D-history_published_reads-4):
  // the intermediate cursors are never observable, so republishing them is pure cost.
  publish_history(state);
  return NavigateOutcome{steps, doc.journal().cursor(), doc.pin()->revision(),
                         doc.journal().can_undo(), doc.journal().can_redo()};
}

platform::Result<AppState> open_or_create_app_state(const platform::FileSystem& fs,
                                                    const std::filesystem::path& root) {
  if (fs.exists(root)) {
    // Thread the editor-kind registrar into the LOAD path so a rebuild-from-canonical
    // reopen restores persisted cameras as live `scene::CameraContent`, not degraded
    // `PlaceholderContent` (editor.cameras.reopen_codec, D-reopen-1). `project` calls
    // it back typed only on `arbc::Registry` — no `project->scene` edge.
    auto opened = project::open_project(fs, root, register_editor_kinds);
    if (!opened) {
      return opened.error();
    }
    return AppState(std::move(*opened));
  }
  auto created = project::create_project(fs, root);
  if (!created) {
    return created.error();
  }
  return AppState(std::move(*created));
}

platform::Result<project::SaveOutcome> save_project(AppState& state, const platform::FileSystem& fs,
                                                    const project::WriterPost& post_writer) {
  // The dump lives in `project` (D-save-1); `commands` only wires the session into
  // it and updates the dirty baseline. The capture half is POSTED to the writer thread
  // through `post_writer`; the serialize + publish half runs here, off it (D-writer_thread-7).
  // The session's raster memo rides in as the trailing argument (A23): an untouched tile is
  // a memo hit, so a re-save re-hashes only what the user actually touched.
  platform::Result<project::SaveOutcome> published = project::save_project(
      fs, state.layout(), state.document(), state.registry(), post_writer, state.tiles());
  if (!published.has_value()) {
    return published.error(); // a failed publish leaves the session dirty
  }
  state.mark_saved(published.value().revision);
  return *published;
}

platform::Result<project::SaveOutcome>
save_project_as(AppState& state, const platform::FileSystem& fs,
                const platform::ProcessLauncher& launcher, const std::filesystem::path& executable,
                const std::filesystem::path& target_root, const project::WriterPost& post_writer) {
  // Reject an empty target before any I/O or exec (Constraint 5, mirroring
  // `open_another_project`): an empty path would publish into the CWD and spawn a
  // mystery sibling. The launcher is never touched and nothing is written.
  if (target_root.empty()) {
    return std::make_error_code(std::errc::invalid_argument);
  }

  // Canonicalize ONCE to an absolute path so the publish and the sibling `exec`
  // agree on the destination regardless of the child's CWD (Constraint 5). `absolute`
  // roots it lexically at the CWD; `weakly_canonical` then normalizes. Neither throws.
  std::error_code ec;
  std::filesystem::path resolved = std::filesystem::absolute(target_root, ec);
  if (!ec) {
    resolved = std::filesystem::weakly_canonical(resolved, ec);
  }
  if (ec) {
    return ec;
  }

  // Publish a COPY of the LIVE document into the target (D-save_as-1). The capture is POSTED to
  // the writer thread through `post_writer` (D-writer_thread-7); the serialize + publish runs
  // here. The current session is deliberately left untouched — no `mark_saved`, no `layout_`
  // rebind (D-save_as-2 / Constraint 4).
  // Deliberately WITHOUT the session's raster memo: a memo hit skips the asset sink, so
  // sharing it into a fresh destination would publish a copy whose `assets/` is empty (see
  // the note in `project::save_project_as`). The copy costs one full re-hash; the session's
  // own memo is left intact, so the next plain Save is still incremental.
  platform::Result<project::SaveOutcome> published =
      project::save_project_as(fs, resolved, state.document(), state.registry(), post_writer);
  if (!published.has_value()) {
    return published.error(); // a failed publish execs nothing (Constraint 7)
  }

  // Open the copy in a detached sibling editor on the same absolute path. A failed
  // exec leaves the (successfully published) copy on disk and returns the error.
  if (const std::error_code exec_ec = open_another_project(launcher, executable, resolved)) {
    return exec_ec;
  }

  return published;
}

platform::Result<project::GcOutcome> gc_project(AppState& state, bool dry_run) {
  // The sweep lives in `project` (D-gc-1); `commands` only selects this session's
  // layout and drives it. NOT a transaction (Constraint 3): no revision bump, no
  // journal entry, no `mark_saved` — the dirty baseline and revision are untouched.
  // Roots on the on-disk canonical (D-gc-2), so it needs no live-document
  // serialization and no `FileSystem` seam.
  return project::gc_project(state.layout(), dry_run);
}

} // namespace ace::commands
