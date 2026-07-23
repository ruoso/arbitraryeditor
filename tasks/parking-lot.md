# Parking lot — human/legal judgment items (not WBS tasks)

Items surfaced by refinements that a WBS implementer cannot decide and that do
**not** gate any leaf. Reviewed by a human; not scheduled by the orchestrator.

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
