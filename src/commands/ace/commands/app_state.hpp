#pragma once

#include <ace/commands/history.hpp>
#include <ace/commands/selection.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/platform/result.hpp>
#include <ace/project/gc.hpp>
#include <ace/project/project.hpp>
#include <ace/project/save.hpp>

#include <arbc/contract/registry.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp> // arbc::KindBridge (the document-scoped bridge)
#include <arbc/runtime/raster_tile_store.hpp> // arbc::RasterTileStore (A23; complete: unique_ptr member)

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

namespace ace::commands {

// A move-safe atomic revision slot. `std::atomic` is neither copyable nor movable, and
// `AppState`'s defaulted move is load-bearing (`open_or_create_app_state` returns by value), so
// the atomic is wrapped rather than declared inline — the same problem `history_`'s `unique_ptr`
// indirection solves for `HistoryPublisher`. `k_none` is the "no known-published snapshot this
// session" sentinel; no real document revision can reach it.
class SavedRevision {
public:
  static constexpr std::uint64_t k_none = std::numeric_limits<std::uint64_t>::max();

  SavedRevision() = default;
  SavedRevision(SavedRevision&& other) noexcept
      : value_(other.value_.load(std::memory_order_relaxed)) {}
  SavedRevision& operator=(SavedRevision&& other) noexcept {
    value_.store(other.value_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    return *this;
  }
  SavedRevision(const SavedRevision&) = delete;
  SavedRevision& operator=(const SavedRevision&) = delete;

  std::uint64_t load() const { return value_.load(std::memory_order_relaxed); }
  void store(std::uint64_t revision) { value_.store(revision, std::memory_order_relaxed); }

private:
  std::atomic<std::uint64_t> value_{k_none};
};

// The process's ONE owned project session (docs/00-design.md D19, arch A7,
// refinement Decisions D-app_state-1/2). The app owns exactly one `Document` for
// its whole lifetime (one process = one project; the GC root-set is trivially
// that document), and this is the object that holds it. It takes ownership of the
// `OpenedProject` `editor.project.open` hands back — moving the `Document` in and
// keeping the `ProjectLayout` (so `save` can find `project.arbc`) — seeds a
// persistent kind `Registry` at construction (the D-open-7 deferral this leaf
// collects, reused by `save`/export and the future A6 plugin seam), and carries
// the project-level `Selection`. Move-only, one per process (A7): the single
// owner. The `Document`'s live `HousekeepingThread` (A4) therefore spans the whole
// run and is joined by the `Document` destructor at teardown. Headless — no
// ImGui/GL/SDL (A8); the L4 `app` holds an `AppState` for the process lifetime.
class AppState {
public:
  explicit AppState(project::OpenedProject opened);

  AppState(AppState&&) = default;
  // Move ASSIGNMENT is deleted, and that is a lifetime rule rather than a style choice
  // (A23 / D-raster_tile_store-6). `tiles_` pins `BlockRef`s into `document_`'s pool
  // (`arbc/runtime/raster_tile_store.hpp:141`), and a DEFAULTED move assignment runs
  // member-by-member in declaration order — it would assign `document_` first, freeing the
  // pool while the old memo still pinned into it, i.e. a use-after-free at the next
  // release. The move CONSTRUCTOR is kept and is load-bearing
  // (`open_or_create_app_state` returns by value); nothing in the tree move-ASSIGNS an
  // `AppState` (A7 means there is only ever one), so deleting is strictly better than
  // hand-writing an ordering nobody exercises.
  AppState& operator=(AppState&&) = delete;
  AppState(const AppState&) = delete;
  AppState& operator=(const AppState&) = delete;

  arbc::Document& document() { return *document_; }
  const arbc::Document& document() const { return *document_; }

  // The document's ONE `org.arbc.raster` incremental-save hash memo (A23), minted with the
  // `Document` inside `project` and held here for the process's life. Never null in a live
  // `AppState`. Handed to `project::save_project` so an untouched tile is a memo hit instead
  // of a fresh SHA-256 over its storage bytes; `tiles()->tiles_hashed()` is the behavioural
  // witness that memoisation actually happened — the STRONGER of the two incremental-save
  // counters, because write-if-absent alone would give the right blob count while still
  // re-hashing the whole document (`raster_tile_store.hpp:112-118`, D-raster_tile_store-5).
  arbc::RasterTileStore* tiles() { return tiles_.get(); }
  const arbc::RasterTileStore* tiles() const { return tiles_.get(); }

  const project::ProjectLayout& layout() const { return layout_; }

  arbc::Registry& registry() { return registry_; }
  const arbc::Registry& registry() const { return registry_; }

  // The DOCUMENT-SCOPED `KindBridge` (D-writer_thread-9), seeded from `registry()` at
  // construction exactly as the save and load bridges are (A14). WRITER-OWNED: the only thing
  // that mutates it is `arbc::settle_external_loads`, which is writer-thread only. It lives here
  // — beside the document, not inside a canvas — because the document holds ONE external-load
  // settler slot, so a per-renderer bridge would intern an arrival into whichever viewport
  // installed last, and would be render-owned state a writer mutates. Every canvas over this
  // document is handed this one bridge.
  arbc::KindBridge& kind_bridge() { return kind_bridge_; }
  const arbc::KindBridge& kind_bridge() const { return kind_bridge_; }

  Selection& selection() { return selection_; }
  const Selection& selection() const { return selection_; }

  // The published journal-shape snapshot the UI reads instead of walking the
  // writer-owned entry vector (arch A18). `history().load()` is any-thread and never
  // null — an empty snapshot is published at construction, so a panel drawn before the
  // first edit still gets a valid pointer. `history().refresh(...)` is writer-thread
  // only; call it through the free `publish_history` below rather than directly.
  HistoryPublisher& history() { return *history_; }
  const HistoryPublisher& history() const { return *history_; }

  // Whether `open_project` rebuilt this session from the canonical `project.arbc`
  // rather than mapping the crash-durable workspace (always false for a fresh
  // `create_project`).
  bool rebuilt_from_canonical() const { return rebuilt_from_canonical_; }

  // How many layer-bound content records this session's open could not bind (A19),
  // ferried verbatim off the bootstrap `OpenedProject` — a VALUE, not an error. It is
  // non-zero only on the one lossy path the editor can produce: a NEVER-SAVED project
  // (no canonical `project.arbc` to rebuild from) whose crash-durable `workspace/` held
  // cells or cameras, which reopens successfully but empty because `Document::open`
  // runs no factory. Zero for a rebuild-from-canonical open, a content-free map, and a
  // fresh `create_project`. The UI reads it through `dock::ProjectGateway` to announce
  // the loss once (D25) instead of presenting an emptied project silently.
  //
  // Immutable after construction — unlike `saved_revision_`, nothing ever writes it
  // again, so it is a plain scalar with no synchronization: the bootstrap thread stores
  // it once, the UI thread reads it thereafter, and the two never interleave.
  std::size_t unbindable_content_records() const { return unbindable_content_records_; }

  // The dirty indicator (D16/A13/D-save-4): workspace-vs-snapshot drift, modelled as
  // session revision-drift and CONSERVATIVE toward "dirty" — it never reports a
  // false clean (telling the user unpublished edits are safely in `project.arbc`
  // when they are not). `saved_revision_` is the last known-published revision this
  // session: set clean at a `rebuilt_from_canonical` open (the workspace was just
  // built from `project.arbc`) and on each successful publish; `nullopt` (dirty) for
  // a fresh `create_project` or a workspace-mapped open (no known-published snapshot
  // this session). Persisting a cross-session baseline is a deliberate non-goal.
  //
  // Race-free across the writer/UI split (D-writer_thread-12): `pin()->revision()` is already an
  // any-thread read of the immutable `DocRoot`, and `saved_revision_` — the one editor-owned
  // scalar here, written by `mark_saved` inside the POSTED save closure and read by the UI thread
  // every frame — is atomic.
  bool is_dirty() const {
    const std::uint64_t saved = saved_revision_.load();
    return saved == SavedRevision::k_none || saved != document_->pin()->revision();
  }

  // Mark the session clean at `revision` — the revision a successful publish dumped.
  // Called by `save_project` after `project::save_project` returns; not part of the
  // edit path (a revision-bumping `dispatch` re-dirties by advancing past it).
  void mark_saved(std::uint64_t revision) { saved_revision_.store(revision); }

  // Hand out the next gesture-coalescing key for a continuous gesture (a brush
  // stroke, a handle drag), the collision-free seam `editor.project.undo` ships
  // (Constraint 3 / Decision D-undo-2). A gesture calls this ONCE at gesture-start
  // and stamps every frame's `doc.transact(name).coalesce(key)` with the returned
  // key, so the library folds the whole gesture into ONE journal entry (one undo
  // step); ending the gesture = stop using the key. The counter is a per-session
  // monotonic `std::uint64_t` seeded at 1, so it never returns `k_no_coalesce` (0)
  // and two distinct gestures never share a key even when adjacent — the journal
  // only folds *consecutive* commits with a *matching* non-zero key. Writer-thread
  // only, like every other edit-path call on the one owned `Document` (A4).
  std::uint64_t next_gesture_key() { return next_gesture_key_++; }

private:
  std::unique_ptr<arbc::Document> document_;
  // DECLARED AFTER `document_` ON PURPOSE (Constraint 2 / D-raster_tile_store-6): the memo
  // holds owning `BlockRef` pins into the document's `BigBlockPool`, so reverse-declaration
  // destruction order is what releases those pins BEFORE the pool they point into. Held
  // through a `unique_ptr` — the `history_` idiom below — because `RasterTileStore` owns a
  // `std::mutex` and is therefore neither copyable nor movable, while `AppState` is moved.
  std::unique_ptr<arbc::RasterTileStore> tiles_;
  project::ProjectLayout layout_;
  arbc::Registry registry_;
  // The document-scoped, writer-owned kind bridge (D-writer_thread-9). Declared after
  // `registry_`, which seeds it.
  arbc::KindBridge kind_bridge_;
  Selection selection_;
  bool rebuilt_from_canonical_ = false;
  // The A19 reopen-degradation count, carried off `OpenedProject` and never written
  // again. Plain (non-atomic) on purpose — see the accessor above.
  std::size_t unbindable_content_records_ = 0;
  // The last-published revision this session, or `SavedRevision::k_none` when none is known.
  SavedRevision saved_revision_;
  // The next gesture-coalescing key (D-undo-2): monotonic, seeded at 1 so it never
  // hands out `k_no_coalesce` (0) and never repeats within a session.
  std::uint64_t next_gesture_key_ = 1;
  // The History panel's published read seam (A18 / D-history_published_reads-6). Held
  // through a `unique_ptr` — like `document_` above — because `HistoryPublisher` owns an
  // `std::atomic<std::shared_ptr<...>>`, which is neither copyable nor movable and would
  // otherwise delete the defaulted move above (and with it `open_or_create_app_state`'s
  // return-by-value). Never null in a live `AppState`.
  std::unique_ptr<HistoryPublisher> history_;
};

// A dispatchable editor action (refinement Decision D-app_state-5). This leaf
// ships the SEAM, not the concrete scene-edit taxonomy — those `Command`s land
// with their edit leaves (`editor.cells.*`, `editor.camera.*`). `apply` runs on
// the writer `Document` and, when well-formed, performs exactly one libarbc
// transaction via the `Document` transactional wrappers (`add_composition` /
// `add_content` / `add_layer` / `transact`), i.e. one journal entry, one revision
// bump. `name` labels the action (undo will surface it later).
struct Command {
  std::string name;
  std::function<void(arbc::Document&)> apply;
};

// The journal effect of one dispatched command — the one-command-one-entry
// boundary made observable (the invariant this leaf pins; `editor.project.undo`
// coalesces gestures, `editor.canvas.frame_sync` moves the submit off-thread).
struct DispatchOutcome {
  std::size_t journal_entries_added = 0; // journal().depth() delta (1 for a well-formed edit)
  std::uint64_t revision = 0;            // the document revision after applying
};

// Apply `command` on `state`'s writer `Document` (synchronous, single-writer,
// A4). Returns the journal delta + resulting revision. A command with no `apply`
// (or a no-op body) adds no entry.
DispatchOutcome dispatch(AppState& state, const Command& command);

// The observable result of one undo/redo navigation (Decision D-undo-1). Undo/redo
// are journal-cursor *navigation*, not scene *edits*: they add no `JournalEntry`
// and take no `apply`, so they are plain verbs, not `Command`s on `dispatch`.
// `moved` is the honest signal — `false` when there was nothing to move to (an
// empty journal, the tip, or a rare writer-path allocation failure; the cursor
// stays put), which is normal control flow, not a fault (so no `platform::Result`).
struct UndoOutcome {
  bool moved = false;         // the cursor navigated (a forward publish happened)
  std::uint64_t revision = 0; // the document revision after navigating
  bool can_undo = false;      // whether a further undo is possible
  bool can_redo = false;      // whether a redo is possible
};

// Undo one step: navigate the document journal's cursor back one entry, rebinding
// every touched object to its *before* edge as an ordinary FORWARD publish
// (revision +1) — libarbc's `journal().undo()`, never a reimplemented stack (D15 /
// Constraint 1). Writer-thread, synchronous (A4). Because navigation is a forward
// publish, undo NEVER returns the revision to a prior value and never marks the
// session clean (D-undo-4 / Constraint 4) — it does not touch the dirty baseline.
UndoOutcome undo(AppState& state);

// Redo one step: the symmetric forward navigation to the *after* edge
// (`journal().redo()`). Same writer-thread, forward-publish, dirty-baseline-inert
// contract as `undo`.
UndoOutcome redo(AppState& state);

// The observable result of one multi-step jump (D-history_published_reads-4). `steps`
// is how many single-step verbs actually moved — 0 when the target IS the current
// cursor (clicking the current head is a no-op) — and the rest mirrors `UndoOutcome`'s
// post-navigation readout.
struct NavigateOutcome {
  std::size_t steps = 0;      // single-step verbs that moved (0 = nothing to do)
  std::size_t cursor = 0;     // the journal cursor after navigating
  std::uint64_t revision = 0; // the document revision after navigating
  bool can_undo = false;
  bool can_redo = false;
};

// Navigate the journal cursor to `target_cursor`: clamp the target into `[0, depth()]`,
// then walk toward it one entry at a time through the shipped `undo`/`redo` verbs —
// libarbc exposes single-step navigation only (`arbc/model/journal.hpp:91-92`) — and
// defensively end-stop the moment a verb reports no move. Writer-thread, synchronous
// (A4). Like `undo`/`redo` this is journal NAVIGATION, not an edit: it adds no journal
// entry and never touches the dirty baseline (each step is an ordinary forward publish,
// so the revision advances).
//
// This is the History panel's click-to-jump, lifted out of L3 (D-history_published_reads-4):
// it keeps the whole jump testable headless, and collapses N cross-thread verb calls
// into ONE writer-thread unit of work that publishes ONE snapshot at the end rather
// than one per step.
NavigateOutcome navigate_to(AppState& state, std::size_t target_cursor);

// WRITER THREAD: republish `state`'s history snapshot from the live journal (A18).
// Idempotent and stamp-guarded, so it is cheap to call at every writer-turn exit — the
// `commands` verbs above each call it, AND the L4 shell binds it as `CanvasHost`'s
// post-edit hook, because camera-inspector and manipulator edits run bare `scene::`
// transactions inside a raw `apply_edit` closure and never reach a verb at all
// (D-history_published_reads-3).
void publish_history(AppState& state);

// Resolve a project directory into an `AppState` (refinement Decision
// D-app_state-6): an existing path opens (`project::open_project`), a
// not-yet-existing one is created (`project::create_project`). Errors are values —
// a non-directory (`OpenError::NotADirectory`), an empty directory with no
// project (`NoProject`), a corrupt/faulted open — all ride `platform::Result`'s
// error channel, never a throw. The app-layer bootstrap calls this with a
// `NativeFileSystem` and holds the result for the process lifetime.
platform::Result<AppState> open_or_create_app_state(const platform::FileSystem& fs,
                                                    const std::filesystem::path& root);

// Publish the session's live `Document` as the canonical `project.arbc` (+ owned
// `assets/`) through the L1 `project::save_project` dump (A13/D-save-1) and, on
// success, mark the session clean at the published revision. Errors are values (a
// failed publish returns cleanly and the session stays dirty — the workspace is
// durable regardless). The L4 `AppProjectGateway::save()` drives this against the
// one in-process `AppState`; the rail never reaches into `project` directly (A13).
//
// `post_writer` posts the CAPTURE half onto the document's writer thread and returns when it has
// run (D-writer_thread-7); the serialize + publish half deliberately stays off it, over the
// immutable snapshot. Empty (the headless default) means the caller is already the one writer
// identity. `mark_saved` is stamped from inside that posted capture's caller, on the revision the
// snapshot pinned, and `saved_revision_` is atomic for the UI's per-frame dirty read (D-12).
platform::Result<project::SaveOutcome> save_project(AppState& state, const platform::FileSystem& fs,
                                                    const project::WriterPost& post_writer = {});

// Save As: publish a COPY of the session's live `Document` as a new project under
// `target_root`, then open THAT copy in a detached sibling editor (D-save_as-1/2).
// Composes the two shipped, trusted halves — `project::save_project_as` (the
// publish-copy) and `open_another_project` (the sibling `exec`). Rejects an empty
// `target_root` with `std::errc::invalid_argument` — the launcher is NOT invoked
// and nothing is written (Constraint 5, mirroring `open_another_project`).
// Otherwise canonicalizes the target to an absolute path ONCE and uses that same
// path for both the publish and the exec, so the child never depends on the
// parent's CWD. `target_root` must NOT already exist (D27): `project::save_project_as`
// refuses any existing path with `std::errc::file_exists`, which short-circuits here before
// the launcher is touched, so a refused target publishes nothing and execs nothing. The
// CURRENT session is left untouched: `save_project_as` never
// calls `mark_saved`, never re-points `layout_`, never tears down the window —
// process-per-project owns one `Document` for its lifetime (D19/A7, Constraint 4);
// a fresh sibling process owns the copy. Errors are values: a failed publish execs
// nothing (no sibling on a half-written bundle); a failed exec leaves the published
// copy on disk and returns the error. Never throws.

// How far Save As got before it stopped (A25 / D-save_as_outcome-2).
// `platform::Result<project::SaveOutcome>` cannot answer this: it holds EITHER a value OR a
// `std::error_code`, so a failed call structurally cannot also report that the copy reached
// disk — and the codes OVERLAP across stages, since a canonicalize fault and a `spawn_detached`
// fault are both plain system/generic codes. So no caller could tell "nothing was written" from
// "the copy is on disk but nothing launched", which is exactly the pair the UI must not confuse:
// one is fixed by freeing disk space, the other by opening the copy that already exists.
enum class SaveAsStage {
  refused,        // a bad target the user can retype: nothing written, nothing spawned
  publish_failed, // the target was not refused, but no usable copy was produced
  spawn_failed,   // the copy IS on disk; the sibling `exec` failed
  spawned,        // the copy is on disk AND the sibling editor is running
};

// The stage, the fault that stopped it, and (once the copy reached disk) what was published.
// A plain POD in the house "errors are values" idiom — L4 maps `stage` onto the dock's own
// vocabulary with a total `switch`, so a future stage is a compiler diagnostic rather than an
// `else` fallthrough (D-save_as_outcome-2).
struct SaveAsResult {
  SaveAsStage stage = SaveAsStage::refused;
  std::error_code error;            // empty iff `stage == spawned`
  project::SaveOutcome published{}; // meaningful iff the copy reached disk (spawn_failed/spawned)
};

// The refused-versus-fault split happens HERE, in L1 `commands` (D-save_as_outcome-3): this is
// the level that owns BOTH error vocabularies — `std::errc` from its own guards and `SaveError`
// from `project` — the pairing `D-dir_is_project-2` already blessed on one channel. A publish
// that failed with `std::errc::file_exists` is D27's existing-target guard, i.e. `refused`;
// every other publish fault is `publish_failed`. A CANONICALIZE fault is `publish_failed` too
// (D-save_as_outcome-4): nothing the user retypes resolves it, and "could not save a copy
// there" is literally true.
SaveAsResult save_project_as(AppState& state, const platform::FileSystem& fs,
                             const platform::ProcessLauncher& launcher,
                             const std::filesystem::path& executable,
                             const std::filesystem::path& target_root,
                             const project::WriterPost& post_writer = {});

// Clean up (GC): reclaim the on-disk `assets/` orphans this session's saves left
// behind, through the L1 `project::gc_project` sweep over `state.layout()`
// (D13/§8/D-gc-1). Unlike a dispatched edit this is NOT a transaction: it bumps no
// document revision, adds no journal entry, and does NOT call `mark_saved` — GC is a
// maintenance op over on-disk blobs, not a document edit (D13/D15/Constraint 3), so
// `is_dirty()`, `document().pin()->revision()`, and `layout()` are left UNCHANGED.
// `dry_run` previews the reclaim plan without deleting (the rail's confirm-before-
// sweep). Errors are values. The L4 `AppProjectGateway::clean_up()` drives this
// against the one in-process `AppState`; the on-close sweep drives it silently.
platform::Result<project::GcOutcome> gc_project(AppState& state, bool dry_run);

} // namespace ace::commands
