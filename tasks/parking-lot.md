# Parking lot — human/legal judgment items (not WBS tasks)

Items surfaced by refinements that a WBS implementer cannot decide and that do
**not** gate any leaf. Reviewed by a human; not scheduled by the orchestrator.

---

## History view real-body owner

**Source:** `tasks/refinements/editor/view_registry.md` (view_registry, 2026-07-17)

The History view type is registered and draws a labeled placeholder. Its real
body is a design judgment call: does it get a dedicated panel implementation, or
does its content fold into `editor.project.undo` (`tasks/00-editor.tji` — the
transaction-journal wiring, which already owns the undo history data)? No new
WBS leaf was created; the choice is parked here for human review before a
downstream panel-content task is scheduled.

---

## No-project first-launch UX

**Source:** `tasks/refinements/editor/app_state.md` (app_state, 2026-07-17)

The current bootstrap creates a scratch project when no project path is given, so the single-Document-per-process invariant (A7) always holds and the app is drivable headless. What the first-launch experience should actually *show* the user once a picker exists (`editor.project.open_ui`) is a design judgment call: show the scratch document, show a welcome/recent-projects screen, or block on a picker. This decision is deferred until `editor.project.open_ui` lands. No new WBS task was created; the choice is parked here for human review before that leaf is refined.

---

## Assets view real-body owner

**Source:** `tasks/refinements/editor/view_registry.md` (view_registry, 2026-07-17)

The Assets view type is registered and draws a labeled placeholder. Its real
body is a design judgment call: does it warrant a dedicated asset-browser leaf,
or is it subsumed by the Layers list's referenced-vs-painted surface
(`editor.panels.layers`, per D11)? No new WBS leaf was created; the choice is
parked here for human review before a downstream panel-content task is
scheduled.

---

## Cross-session dirty precision on mapped-workspace reopen

**Source:** `tasks/refinements/editor/save.md` (save, 2026-07-18), D-save-4.

The current dirty model is conservative and session-scoped: a workspace-mapped
reopen (e.g. crash-recovered session) starts dirty even if `project.arbc` is
already up to date, because we cannot prove they are in sync without reloading
the canonical. D-save-4 calls this the honest call — a false-dirty causes an
unnecessary re-dump, which is cheap and idempotent; a false-clean would tell the
user their edits are safe in `project.arbc` when they are not. Improving
precision (persisting a cross-session published-revision sidecar in `workspace/`
so a mapped reopen reads clean) would require touching the shipped open/create
path and adding I/O for a degree of precision the durable-workspace +
idempotent-re-dump model does not need. This is a product-polish judgment call
for a human to weigh; no WBS task was created.

---

## Save As overwrite-with-confirmation UX

**Source:** `tasks/refinements/editor/save_as.md` (save_as, 2026-07-18), D-save_as-4.

`project::save_project_as` refuses to clobber a target directory that already
contains a `project.arbc` — returning an error value rather than silently
replacing another project's canonical. The safe default is implemented and
tested. The "target exists — replace?" confirmation prompt is a UI/UX judgment
call: which layer surfaces the dialog, what copy it shows, whether the picker
itself should filter or warn, and whether a "replace" path should require the
target's own project to be closed first. These are product-design questions that
cannot be mechanically resolved by an agent implementer. No WBS task was
created; the choice is parked here for human review.

---

## One shared render thread vs N render threads for multi-canvas

**Source:** `tasks/refinements/editor/multi_canvas.md` (multi_canvas, 2026-07-18), D-multi_canvas-2.

`render::CanvasHost` drives all N canvases from one shared render thread. A5 mandates a shared `WorkerPool` but is silent on render-thread count; D-multi_canvas-2 chose one thread as the conservative baseline (single pool drainer, smaller TSan surface, correct for the realistic N of 2–3 canvases). If future profiling shows head-of-line blocking is observable (e.g. one heavy canvas visibly slowing the others), moving to N render threads would require revisiting the borrowed-pool concurrent-submitter contract with libarbc. That is a monitor-and-decide call gated on real profiling data, not implementable work today. No WBS task was created.

---

## Async submit-queue (off-UI-thread writer) for large-edit latency

**Source:** `tasks/refinements/editor/frame_sync.md` (frame_sync, 2026-07-18), D-frame_sync-2 / Open questions.

The render thread is off the UI thread and `commands::dispatch`/`undo`/`redo`
remain synchronous on the UI thread (the single writer). This is correct per
A4: `transact` is a cheap model mutation; the expensive work (rendering) is what
moves off-thread. If future profiling ever shows that UI-thread `transact` /
`dispatch` latency is stalling the event loop on large edits (e.g. a bulk
import), moving the writer behind an async submit-queue (with a dedicated writer
thread, a cross-thread command queue, serialized undo-cursor navigation, and a
journal-read guard) may become warranted. That is a monitor-and-decide call
gated on real telemetry, not implementable work today. No WBS task was created;
record here for human review when profiling data is available.
