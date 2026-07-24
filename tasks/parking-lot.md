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

---

## scene/ charter still lists "selection" but Selection lives in commands/

**Source:** `tasks/refinements/editor.cells/selection.md` (cells.selection, 2026-07-23) — Open questions #1.

`docs/01-architecture.md:167` charters `scene/` as "cells · cameras · **selection** · z-order", but the shipped `commands::Selection` lives in `src/commands/` (D-app_state-3). This leaf follows the shipped reality — `commands` is the better home (selection is app state that sits beside `AppState`, not a document projection) and moving the type across components for a one-line charter would be churn with no behavioural change. Whether to correct the §7 charter line is an editorial call for a human; A17 added in this commit documents where hit-testing lives.

---

## arbc::Content::bounds() thread-safety guarantee absent from contract

**Source:** `tasks/refinements/editor.cells/selection.md` (cells.selection, 2026-07-23) — Open questions #2.

`pick_targets` calls `arbc::Content::bounds()` from the UI thread while the render thread walks the same document. The lock-free `pin()` seam covers the record walk, and `bounds()` is an immutable property for every kind shipped today — but `contract/content.hpp:487` states no thread-safety guarantee. A future kind whose bounds change under an `Editable` edit (e.g. a growing raster, per `editor.cells.resolution`'s "resample to crisp") would make it a live read/write pair. The TSan case in `tests/canvas_host_test.cpp` is the tripwire. If it ever fires, the fix is a libarbc-side contract statement, not an editor-side lock — upstream-issue candidate for `ruoso/arbitrarycomposer`.

---

## arbc batch removal verb (remove_contents / CoalesceKey on remove_content)

**Source:** `tasks/refinements/editor.cells/remove.md` (cells.remove, 2026-07-23) — D-cells_remove-2 / Open questions #1.

`Document::remove_content` self-commits with no coalesce hook, so an N-object delete produces N journal entries and requires N undo presses to reverse (D-cells_remove-2). A `remove_contents(std::span<const Removal>)` — or a `CoalesceKey` parameter on `remove_content` — would collapse a multi-select delete to one undo unit. This is the exact mirror of the already-parked `create_content_and_attach` ask (see above). Upstream-issue candidate for `ruoso/arbitrarycomposer`; no editor-side WBS task until the API exists.

---

## arbc runtime.removed_content_reclaim (memory growth on insert/delete cycles)

**Source:** `tasks/refinements/editor.cells/remove.md` (cells.remove, 2026-07-23) — Open questions #2.

`remove_content` deliberately retains the content's binding row and live `Content*` while the journal holds the removal; teardown happens only at document close (or "once `runtime.removed_content_reclaim` lands, the moment the removal leaves history", `document.hpp:123-130`). A long session that repeatedly inserts and deletes large rasters therefore grows monotonically in memory even after history trims. The named library follow-up already exists upstream; the editor cannot fix it host-side. Upstream-issue candidate for `ruoso/arbitrarycomposer`; no editor-side WBS task.

---

## "New Shot From View" — focused canvas vs explicit designation

**Source:** `tasks/refinements/cameras/new_shot_from_view.md` (cameras.new_shot_from_view, 2026-07-23) — Open questions.

`editor.cameras.mint_from_focused_canvas` implements the "follow focus with lowest-id fallback" approach: the gateway reads `CanvasView::focused_framing()` and the mint always promotes the canvas the user was most recently working in. The alternative — an explicit "promote this canvas" affordance in the canvas camera picker — would let the user designate a specific canvas regardless of focus. These two shapes have different discoverability tradeoffs: focus tracking is invisible until it matters (multi-canvas layout); explicit designation adds picker chrome that is meaningless in single-canvas use. Whether the right end state is the focus-tracking rail item alone, the explicit picker, or both is a D23/D18 design call for a human once multi-canvas layouts are in real use. The WBS task ships the focus-tracking path; if explicit designation is the preferred answer, `mint_from_focused_canvas` may need to be reconsidered or extended.

---

## arbc::Journal entry_at()/byte_cost() — upstream any-thread publication

**Source:** `tasks/refinements/canvas/arbc_v030.md` (arbc_v030, 2026-07-23) — Open questions.

`arbc::Journal::entry_at()` and `byte_cost()` remain writer-thread-only at v0.3.0 by
explicit upstream decision (arbc#15 covered `can_undo`/`can_redo`/`depth`/`cursor` and
excluded these two). The editor's fix is a host-side published snapshot in
`commands::AppState` (`editor.canvas.history_published_reads`), which is correct and
sufficient — but the *general* shape (a host UI that wants to browse history off the
writer thread) is the same class of problem arbc#15 solved for the enable state. Whether
libarbc should publish an entry-name view is a **library** design judgment for the
`arbitrarycomposer` maintainer, not editor work. Upstream-issue candidate for
`ruoso/arbitrarycomposer`; no editor-side WBS task.

---

## Suppress the focused-canvas indicator on a single-canvas dock?

**Source:** `tasks/refinements/canvas/focused_canvas_indicator.md`
(canvas.focused_canvas_indicator, 2026-07-23) — Open questions /
D-focused_canvas_indicator-3.

D-focused_canvas_indicator-3 chose to draw the hairline accent border
unconditionally — even on a single-canvas dock where it conveys no
disambiguating information — because suppressing it would trade a one-pixel
constant for a conditional invariant ("bordered pane = where the verb lands"
would stop being universally true) and suppress the affordance precisely when a
new user first learns what it means. The rationale is on the record in the
refinement. If, in practice, the always-on border reads as chrome noise when only
one canvas is open, the fix is a one-condition check in the draw block plus one
e2e phase; the Catch2 rule matrix is unaffected. This is a product-taste call
that warrants real use before deciding.

---

## HostViewport settler attach/detach split (upstream library ask)

**Source:** `tasks/refinements/editor/writer_thread.md` (canvas.writer_thread, 2026-07-23) — D-writer_thread-8 / Open questions #2.

D-writer_thread-8 posts the `HostViewport` constructor and destructor to the writer thread (via `submit_sync` from the render thread) because the settler slot install (`Document::set_external_load_settler`) is writer-thread-only and lives in the `HostViewport` ctor/dtor. If upstream adds an explicit writer-thread `attach`/`detach` pair for the settler — decoupled from object construction — the render thread could manage the full viewport lifecycle itself, the posted ctor/dtor would retire, and D-8 would simplify substantially. Upstream-issue candidate for `ruoso/arbitrarycomposer`; no editor-side WBS task until the API exists.

---

## Sync-submit latency behind a deep async burst

**Source:** `tasks/refinements/editor/writer_thread.md` (canvas.writer_thread, 2026-07-23) — Open questions #3.

`editor.canvas.writer_thread` ships all-sync for result-carrying verbs (D-3). A streamed gesture burst could build a queue depth that makes a subsequent sync `undo` wait. The coalescing key bounds the *commit* cost, not the queue depth. Whether this is observable in practice is unknown; if it bites, adding a bounded depth or a gesture-drop policy would be the fix. Measure on the real pool with a realistic workload before designing anything — this is a data-gated decision, not implementable work today. No WBS task until profiling data exists.

---

## CanvasHost pending_removes drop window (D-canvas_host-pending_removes-drop)

**Source:** fixer fix during `editor.canvas.accent_palette` (2026-07-23).

`CanvasHost::drive_once` step 3 consumes `pending_resizes`/`pending_cameras` per-id
(erasing only those applied to a live entry), but `pending_removes` still uses a bulk
`clear()`. A `remove` arriving after the iteration's `pending_adds.swap` but before the
entry-map lock is released is cleared, and the later `add` then resurrects the entry without
honouring the removal. Whether a remove should pre-empt a still-queued add (the entry never
surfaces) or whether such a race is a caller error is a design call the fixer correctly left
unresolved. No WBS task was registered; fix this when the call is made.

---

## Deferred-external nested child composites blank under real WorkerPool

**Source:** `tasks/refinements/editor/writer_thread.md` (canvas.writer_thread, 2026-07-23) — tech debt note.

Under the inline degenerate `WriterThread` (headless fixtures) a deferred-external nested child composites correctly (byte-exact). Under the real interactive `WorkerPool`, a deferred-external nested child composites blank even pre-settled — a pre-existing behaviour not introduced by this change. The root cause is in libarbc's worker-pool dispatch path for nested child arrivals, not in the editor. Upstream-issue candidate for `ruoso/arbitrarycomposer`; no editor-side WBS task until the library fix exists.

---

## arbc workspace-map reopen binds no Content — the seam that would restore the fast path

**Source:** `tasks/refinements/cameras/reopen_slab_adopt.md` (cameras.reopen_slab_adopt, 2026-07-23) — Open questions (1).

`arbc::Document::open(path, housekeeping)` takes **no `Registry`** and runs **no factory**
(`arbc/runtime/document.hpp:76-85`), so a workspace-mapped reopen restores the record graph
only: `resolve()` is null for every recovered record and `for_each_content()` visits none,
for **every** kind (verified at the v0.3.0 pin for `org.arbc.solid` and `org.arbc.raster`
alike, and pinned by `tests/arbc_pin_test.cpp` / `tests/project_open_test.cpp`). That is why
A19's rebuild-from-canonical policy is permanent, not a stopgap, and why arbc#5's
`KindStateWalker` could not retire it. Restoring D-open-3's durable-by-default fast path
needs one of two library changes: a **registry-aware open**
(`Document::open(path, registry, bridge)` reconstructing each recovered `ContentRecord`
through `registry.factory(kind_id)` plus a state codec), or a **public rebind seam**
(`Document::rebind_content(ObjectId, std::shared_ptr<Content>)`) — which the library today
deliberately does not offer (`arbc/pool/slot_store.hpp:119`: *"it never rebinds and there is
no rebind API"*). A smaller secondary ask: expose `recovered_content_state()` on `Document`,
since `replay_recovered_content_state` is currently unreachable by any host that opens
through `Document` rather than `Model`. Upstream-issue candidates for
`ruoso/arbitrarycomposer`; no editor-side WBS task until the API exists.

---

## Should the workspace-map fast path stay in open_project at all?

**Source:** `tasks/refinements/cameras/reopen_slab_adopt.md` (cameras.reopen_slab_adopt, 2026-07-23) — Open questions (2).

After the map-then-inspect guard, the fast path survives only for **content-free**
workspaces and for the never-saved fallback — a narrow slice. Deleting it would simplify
`open_project` and remove a branch that has never delivered its advertised benefit for a
real project; keeping it preserves A13's crash-recovery of unpublished **record-level** edits
(layer transforms, z-order, composition size) where that is harmless, keeps
`rebuilt_from_canonical` a live signal rather than a constant, and leaves the seam ready
should the library item above land. How much dead-ish machinery to carry against a possible
upstream fix is a human judgment call, not implementable work. No WBS task was created.

---

## A never-saved project still loses its cameras and cells — autosave vs. warn vs. accept

**Source:** `tasks/refinements/cameras/reopen_slab_adopt.md` (cameras.reopen_slab_adopt, 2026-07-23) — Open questions (3); D-slab-3's residual.

`OpenedProject::unbindable_content_records` closes the **silence** (the reopen now reports
how many objects the map could not bind) but not the **loss**: with no `project.arbc` there
is nothing to rebuild from, and no in-repo change fixes that. Publishing a canonical floor at
`create_project` does not work — content is added *after* create, so an empty floor loses it
just as thoroughly, and making it work means re-dumping on every mutation, i.e. autosave,
which contradicts D16's explicit "Save = re-dump `project.arbc`" model. Whether the product
should autosave, or should warn at camera/cell-creation time in a never-saved project, or
should simply accept the loss now that it is announced, is a product decision. No WBS task
was created; the UI half of the *announcement* is the scheduled leaf
`editor.project.reopen_degradation_notice`.

---

## A magnified raster cell never lets the canvas go idle

**Source:** `tasks/refinements/cameras/reopen_slab_adopt.md` (fixer, 2026-07-23) — surfaced while
diagnosing a `ci-gcc-release` hang; NOT caused by that refinement (reproduced 6/6 on clean
`ac321f0`).

After `editor.canvas.nav_aids`' Shift+F frames a 32×32 `org.arbc.raster` cell into the pane
(a ~10× magnification: the scale bar goes 50 → 5 composition units), the render loop **never
settles**. Every `HostViewport::step()` composites a frame and reports
`schedule_follow_up == true` — arbc's `InteractiveRenderer::render_frame` computes it as
`!arrival_device.empty()`, so a refinement arrives, maps to a non-empty device region, and
owes another frame, indefinitely — with `external_loads_ready == 0` and an empty in-flight
tile queue. `frames_issued` then advances at the frame rate forever and the render thread
burns a core for as long as the framing holds. The same fixture is quiet at 1× (one frame,
then idle), and the nested all-`SolidContent` fixture in the same file settles in ~80 ms at
any zoom, so the trigger is magnification of a raster leaf, not the nav aid itself.

This violates `02-architecture#idle-viewport-issues-no-frames` and is a real
battery/CPU defect, but the loop is inside the pinned library: the editor's host merely obeys
the `schedule_follow_up` it is handed. Diagnosing whether the culprit is the tile-cache probe
key at high magnification or the bounded per-frame budget (D-multi_canvas-3) starving the
frame needs library-side instrumentation. Upstream-issue candidate for
`ruoso/arbitrarycomposer`; no editor-side WBS task until the library fix exists. The
editor-side mitigation already landed: `tests/canvas_nav_e2e_test.cpp`'s `settle()` helper is
wall-clock bounded, so a non-idling canvas can no longer hang a lane (unbounded, it tripped
the ImGui Test Engine's 60 s `ConfigWatchdogKillTest` and turned a 16 s `ace_shell_test` into
a 566 s failure).

---

## render_offline version-addressed offline render (exact batch-export coherence)

**Source:** `tasks/refinements/editor.cameras/export.md` (cameras.export, 2026-07-23) — Open questions / D-export-8.

`render_offline(const Document&, const Viewport&, Backend&)` (`arbc/runtime/offline.hpp:20-21`)
pins the **current** version per call, so an edit landing mid-batch can make item 3 reflect a
document state item 1 did not. D-export-8 chose to record and report the incoherence
(`ExportReport::document_changed_during_export`) rather than block it, because the two
host-side alternatives are untenable: blocking the writer for the full batch contradicts D14's
async promise; snapshotting the document requires a full parse-and-rebuild of a serialisation
snapshot per export. Whether libarbc should offer a version-addressed offline render
(`render_offline(const Document&, RevisionId, …)` backed by the library's internal retained
versions) is a **library** design judgement for the `arbitrarycomposer` maintainer — the
editor has no host-side fix that is not a full second document load. Upstream-issue candidate
for `ruoso/arbitrarycomposer`; no editor-side WBS task until the API exists.
