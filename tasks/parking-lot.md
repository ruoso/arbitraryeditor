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

---

## arbc DamageAccumulator flush/drain race (companion to arbc#13)

**Source:** `tasks/refinements/editor.cells/model.md` (cells.model, 2026-07-22) — fixer sub-agent attempt 2.

`HostViewport::DamageAccumulator` assumes the writing thread (UI) and the draining thread (render) are the same, with no synchronization between `push_back` (UI `commit → DamageRouter::flush`) and `drain()` (render-thread swap). This is a distinct race from arbc#13 (`settle_external_loads` commit-sink publish). The editor-side writer-priority lease (`CanvasHost` apply_edit + run() hold) mitigates the symptom by serializing UI writes against the render step, but it does not close the contract violation in the library. An upstream issue should be filed as a companion to arbc#13 (which must also be re-triaged from "latent" to "live" — it fires deterministically once a document has deferred external children). No editor-side WBS task exists until the library exposes a clean handoff contract.

---

## arbc Registry per-kind insert-schema hook

**Source:** `tasks/refinements/editor.cells/model.md` (cells.model, 2026-07-22) — D-cells_model-2.

`arbc::Registry` advertises `factory`/`metadata`/`codec`/`binder`/`state_walker` but no per-kind field-descriptor for `ContentConfig` inputs (the config is `std::string_view`, opaque). The editor therefore carries a grammar-adapter table in `scene::build_config` that encodes the built-in grammars (raster `"<w>x<h>"`, solid `"r,g,b,a"`, nested decimal id). A future `KindInsertSchema` hook on `Registry` would let that table shrink to zero and allow plugins to advertise their own insert fields automatically. Upstream-issue candidate for `ruoso/arbitrarycomposer`; no editor-side WBS task until the API exists.

---

## org.arbc.solid factory grammar admits no bounds field

**Source:** `tasks/refinements/editor.cells/model.md` (cells.model, 2026-07-22) — D-cells_model-3.

`org.arbc.solid`'s registered factory config grammar is `"r,g,b,a"` with no bounds (`builtin_kinds.cpp:90-104`), so a Registry-constructed solid cell is always unbounded — Constraint 11 forbids bypassing the factory to name the concrete `SolidContent` type directly. The consequence (solid placement affine is a no-op, solid fills everywhere) is accepted and documented; the bounded-solid use case would require the library to extend the grammar. Upstream-issue candidate for `ruoso/arbitrarycomposer`; no editor-side WBS task until the API exists.

---

## arbc Document lacks atomic create-content-and-attach

**Source:** `tasks/refinements/editor.cells/model.md` (cells.model, 2026-07-22) — D-cells_model-7. Also noted in `tasks/refinements/cameras/model.md`.

`arbc::Document::add_content` self-commits (it is the only vtable-binding call), so every cell or camera create costs two journal entries: content first, then `transact` → `add_layer` → `attach_layer` → `commit`. The mirror verb `remove_content` (document.hpp:131) is atomic in one entry. A future `create_content_and_attach` on `Document` would collapse the two-entry create to one, making undo semantics uniform. Upstream-issue candidate for `ruoso/arbitrarycomposer`; no editor-side WBS task until the API exists.
