# Parking lot — human/legal judgment items (not WBS tasks)

Items surfaced by refinements that a WBS implementer cannot decide and that do
**not** gate any leaf. Reviewed by a human; not scheduled by the orchestrator.

Split on one axis, because the two halves want different things from a reviewer
(added at the 2026-07-28 triage — the file used to mix them, so every pass
re-read the whole list as open):

- **Decidable now** — every input exists; the item is waiting on a human to
  choose, and re-reading it next cycle will not produce new information.
- **Waiting on evidence** — the call is blocked on something that does not exist
  yet: real use, profiling data, or a leaf that has not shipped. Each names its
  own trigger. Do not re-litigate these until the trigger fires.

---

# Decidable now

*Empty.* The 2026-07-28 triage decided all three items that stood here — the
bundled font (adopted, scheduled as `editor.cameras.truetype_captions`), the
welcome-window visual treatment (declined for v1) and the worker-backed tile
dispatch (inline decode accepted for v1). Git history is the record of each; the
reasoning that survives is carried in the leaf note and in the architecture rows
the decisions touch, not here. A new item lands here only if it is genuinely
waiting on a human choice rather than on evidence.

---

# Waiting on evidence

## Layers panel — enter/expand keyboard and double-click bindings

**Source:** `tasks/refinements/editor.panels/layers.md` (editor.panels.layers, 2026-07-29) — Open questions.

**Trigger:** §11 input map settled in `docs/00-design.md`.

`editor.panels.layers` ships the shipped-idiom gestures: single-click select, disclosure-triangle expand, double-click enter, crumb-click climb. These bindings are provisional — §11 of `docs/00-design.md` (the input map) is still open, and the input-map owner may want to remap expand to a key chord or change the double-click-vs-modifier boundary. The scope model (`AppState::entered_composition`), the breadcrumb derivation, and all three L1 path helpers are independent of the bindings and need no change when they move; only the L4 dispatch in the panel body is rebindable. No WBS task — this is a human design call gated on the input map.

---

## Assets view real-body owner

**Source:** `tasks/refinements/editor/view_registry.md` (view_registry, 2026-07-17)

**Trigger:** `editor.panels.layers` ships.

The Assets view type is registered and draws a labeled placeholder. Its real
body is a design judgment call: does it warrant a dedicated asset-browser leaf,
or is it subsumed by the Layers list's referenced-vs-painted surface
(`editor.panels.layers`, per D11)? No new WBS leaf was created; the choice is
parked here for human review before a downstream panel-content task is
scheduled.

*Sharpened at the v0.4.0 triage (2026-07-28):* this is not an open-ended design
call — it has a named gate that has not opened. `editor.panels.layers` is still
**incomplete**, so the referenced-vs-painted surface the "is it subsumed?"
question compares against does not exist yet to be looked at. Nothing here is
answerable until that leaf lands; revisit then, not before.

---

## One shared render thread vs N render threads for multi-canvas

**Source:** `tasks/refinements/editor/multi_canvas.md` (multi_canvas, 2026-07-18), D-multi_canvas-2.

**Trigger:** observed head-of-line blocking in real multi-canvas use.

`render::CanvasHost` drives all N canvases from one shared render thread. A5 mandates a shared `WorkerPool` but is silent on render-thread count; D-multi_canvas-2 chose one thread as the conservative baseline (single pool drainer, smaller TSan surface, correct for the realistic N of 2–3 canvases). If future profiling shows head-of-line blocking is observable (e.g. one heavy canvas visibly slowing the others), moving to N render threads would require revisiting the borrowed-pool concurrent-submitter contract with libarbc. That is a monitor-and-decide call gated on real profiling data, not implementable work today. No WBS task was created.

---

## "New Shot From View" — focused canvas vs explicit designation

**Source:** `tasks/refinements/cameras/new_shot_from_view.md` (cameras.new_shot_from_view, 2026-07-23) — Open questions.

**Trigger:** multi-canvas layouts in real use.

`editor.cameras.mint_from_focused_canvas` implements the "follow focus with lowest-id fallback" approach: the gateway reads `CanvasView::focused_framing()` and the mint always promotes the canvas the user was most recently working in. The alternative — an explicit "promote this canvas" affordance in the canvas camera picker — would let the user designate a specific canvas regardless of focus. These two shapes have different discoverability tradeoffs: focus tracking is invisible until it matters (multi-canvas layout); explicit designation adds picker chrome that is meaningless in single-canvas use. Whether the right end state is the focus-tracking rail item alone, the explicit picker, or both is a D23/D18 design call for a human once multi-canvas layouts are in real use. The WBS task ships the focus-tracking path; if explicit designation is the preferred answer, `mint_from_focused_canvas` may need to be reconsidered or extended.

---

## Suppress the focused-canvas indicator on a single-canvas dock?

**Source:** `tasks/refinements/canvas/focused_canvas_indicator.md`
(canvas.focused_canvas_indicator, 2026-07-23) — Open questions /
D-focused_canvas_indicator-3.

**Trigger:** the always-on border reading as chrome noise in real single-canvas use.

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

## Sync-submit latency behind a deep async burst

**Source:** `tasks/refinements/editor/writer_thread.md` (canvas.writer_thread, 2026-07-23) — Open questions #3.

**Trigger:** profiling on the real pool — **after** `editor.canvas.settler_attach_split` lands.

`editor.canvas.writer_thread` ships all-sync for result-carrying verbs (D-3). A streamed gesture burst could build a queue depth that makes a subsequent sync `undo` wait. The coalescing key bounds the *commit* cost, not the queue depth. Whether this is observable in practice is unknown; if it bites, adding a bounded depth or a gesture-drop policy would be the fix. Measure on the real pool with a realistic workload before designing anything — this is a data-gated decision, not implementable work today. No WBS task until profiling data exists.

*Amended at the v0.4.0 triage (2026-07-28):* do not measure yet — the queue shape
is about to change. `editor.canvas.settler_attach_split` (consuming libarbc
v0.4.0's arbc#25 `attach_settler`/`detach_settler`) removes a **known** contributor
to the depth this item is about: today every canvas add, every remove and every
resize rebuild posts a whole `HostViewport` construction or destruction to the
writer thread via `submit_sync` (`src/render/canvas_renderer.cpp:73,150`,
`src/render/canvas_host.cpp:116`), purely because the settler install lives in the
ctor. After that leaf only the attach/detach pair is posted. Profiling the current
queue would characterise a shape that is being retired, so the measurement this
item asks for belongs after that leaf, not before it.

---

## Export destination re-seed: project/layout identity vs service `instance()` on in-process reopen

**Source:** `tasks/refinements/cameras/export_destination_reseed.md` (cameras.export_destination_reseed, 2026-07-24) — Open questions / D-reseed-1 rejected alternative.

**Trigger:** an in-process project reopen ever shipping.

The Export panel's re-seed keys on `ExportService::instance()` — a monotonic per-service id — which is a faithful proxy for the live project today because the shipped model is 1:1 service-per-process (`project_gateway.hpp:21-33`, `app_state.hpp:284-285`). If the editor ever grows an in-process project reopen (swapping `Document`/`AppState` under a **surviving** `ExportService` instead of the current detached-sibling-`exec` model), `instance()` would not change across the swap, so the panel would retain the previous project's destination. The fix in that future would be to key the re-seed on a project/layout identity rather than the service `instance()` — D-reseed-1's "rejected alternative", which is correct only under that future architecture. This is a design call contingent on an architecture decision no leaf owns today; the WBS records the trigger: if in-process reopen ever ships, revisit `views.cpp:315-317` and `ExportPanel::owner` to key on a stable project identity instead.

*Clarified at the v0.4.0 triage (2026-07-28):* libarbc v0.4.0 does **not** move this
trigger, and the reopen work landing alongside it should not be read as having done
so. `editor.project.reconstructing_reopen` changes how a project's document is
*reconstructed on open*; it does not make the editor reopen a different project
**in process**. Opening another project is still a detached sibling `exec`
(`editor.project.exec_new`), so a surviving `ExportService` across a project swap
remains hypothetical and `instance()` remains a faithful proxy. Unchanged.

---

## DamageRouter registrant split — libarbc arbc#25 successor needed to unblock settler_attach_split

**Source:** `tasks/refinements/canvas/settler_attach_split.md` (re-defer 2026-07-28);
confirmed by `tasks/refinements/canvas/arbc_router_split_pin.md` (re-defer 2026-07-28 —
`arbc_router_split_pin` was picked up and verified the upstream state independently: no
successor tag exists, `DamageRouter::register_sink` still welded to ctor at v0.4.0);
re-confirmed by `tasks/refinements/editor.canvas/arbc_router_split_pin.md` (re-defer
2026-07-29 — second independent verification: latest tag still `v0.4.0`, no
`install_router` flag and no `attach_router`/`detach_router` pair on any branch).

**Trigger:** a new libarbc release that moves `DamageRouter::register_sink` (and `Document::set_damage_sink`) out of `HostViewport` ctor/dtor into the `attach_settler()`/`detach_settler()` pair (or a dedicated `attach_router`/`detach_router()` pair), gated by an `install_settler`-style flag.

`editor.canvas.settler_attach_split` was found unimplementable against libarbc v0.4.0: arbc#25 split only the settler slot (`set_external_load_settler`), NOT the `DamageRouter::register_sink` call, out of `HostViewport` ctor/dtor. The `DamageRouter` registration is welded unconditionally to the ctor (`host_viewport.cpp:66-68`) and has no internal synchronization (`damage_router.hpp:29-34`), so render-thread construction with `install_settler=false` still races the router's registrant vector against concurrent writer-thread damage flushes. That is exactly the race the `:1639` TSan anchor's guard note (`canvas_host_test.cpp:1636-1638`) names; the hermetic gcc-tsan lane would report it.

**Human action:** file a libarbc issue (successor to arbc#25) requesting the DamageRouter split — that `register_sink` (and the single-canvas `set_damage_sink`) be deferred to `attach_settler()`/`detach_settler()` or a new `attach_router()`/`detach_router()` pair, gated by a new flag analogous to `install_settler`.

**When the trigger fires:**
1. Mark `editor.canvas.arbc_router_split_pin` complete once the pin bump to that release lands.
2. Implement `editor.canvas.arbc_router_attach_split` (the editor consumer leaf that wires the deferred router registration into the render-thread construction path).
3. `editor.canvas.settler_attach_split` unblocks automatically once `arbc_router_attach_split` is done.

---

## Camera mutable-state round-trip through the workspace arena on mapped reconstruct reopen

**Source:** `tasks/refinements/editor.project/reconstructing_reopen.md` (project.reconstructing_reopen, 2026-07-28) — Open questions.

**Trigger:** a mapped reconstruct reopen observed reverting an unsaved camera edit.

arbc#19's identity capture snapshots construction identity **at `add_content`** (creation time). A camera re-cropped or renamed *after creation but not yet Saved* should reconstruct to its last-checkpointed state on a mapped reopen, not to its construction defaults. Whether arbc#19's capture-plus-replay actually round-trips a codec-persisted kind's post-creation mutable state **through the workspace arena** (vs only through the canonical `project.arbc`, which a mapped reopen never reads) is a library-side property the editor cannot settle from this side. The `open_project reconstructs cells and cameras live` test (added by `reconstructing_reopen`) encodes the requirement as a tripwire: an edited-then-checkpointed camera must reopen with its edit intact. Constraint 4's reconstruct-or-leave-unbound invariant means the safe failure mode is an unbound, reported record — never a silent revert. If a future mapped reopen is observed to revert an unsaved camera edit, this is a cross-repo library gap implementable only against a future libarbc release; not a WBS leaf.

---

## org.arbc.camera insertability via the cell-insert dialog

**Source:** `tasks/refinements/editor.cells/insert_schema.md` (cells.insert_schema, 2026-07-28) — Open question 1.

**Trigger:** a dialog-inserted camera observed in real use causing initialisation problems (missing `scene::add_camera` state setup).

The no-allowlist enumeration lists `org.arbc.camera` in the insert dialog (it did before this leaf too), and `editor.cells.insert_schema` makes its entry honest (zero fields → a default placeholder camera). But a camera minted via `add_cell` skips the identity/state setup `scene::add_camera` performs (D7 / A14 make cells and cameras one shape), so a dialog-inserted camera may be a subtly under-initialised camera. This is a pre-existing property of the no-allowlist model and a genuine design judgment about the cells-vs-cameras seam — not a mechanical fix. If an allowlist exclusion for `org.arbc.camera` is wanted, it is a D7/A14 amendment, not agent-implementable work.

---

## org.arbc.nested insert schema (cross-repo)

**Source:** `tasks/refinements/editor.cells/insert_schema.md` (cells.insert_schema, 2026-07-28) — Open question 2.

**Trigger:** a new libarbc release that adds a `KindInsertSchema` for `org.arbc.nested` (with a labelled `ObjectId` field).

libarbc v0.4.0 registers `org.arbc.nested` with **no** schema, so nested inserts fall to the editor's raw-config box (a decimal `ObjectId` text box), losing `editor.cells.model`'s labelled "Child composition (ObjectId)" field. The correct fix is a cross-repo change to `arbitrarycomposer` to advertise a named field for the nested ObjectId. Once that ships in a future pin, no editor code is needed — the field appears automatically through the no-allowlist enumeration. File upstream and resolve when the pin lands.

---

## libarbc has no content-resample verb — "resample to crisp" blocked on upstream

**Source:** `tasks/refinements/editor/resolution.md` (cells.resolution, 2026-07-28) — Open questions / D-resolution-5.

**Trigger:** a new libarbc release adding a generic `Resampleable` content facet (or equivalent verb).

`arbc::RasterContent` exposes construction + `paint` only (`raster_content.hpp:297-362`); there is no in-place resize verb. Implementing "resample to crisp" (grow a raster's working grid to match or exceed the camera's pixel density) editor-side would require naming the concrete `RasterContent` type (A16 no-allowlist) and owning kind-specific upsampling (levelization — `arbc::media`'s resampler is the kind's floor, not the editor's). The clean fix is an **upstream generic content-resample facet** — a `Resampleable` a kind opts into and the host discovers via a `Content` virtual (a referenced `org.arbc.image` returns `nullptr` → "source-limited"; a raster implements it to resize its grid to a new version via `set_content_state`) — so the host offers "resample to crisp" without naming any kind.

**Human action:** file an upstream issue against `ruoso/arbitrarycomposer` requesting that `Resampleable` facet / content-resize verb.

**When the trigger fires** (a libarbc release adding it): bump the pin, then implement the editor-consumer leaf **`editor.cells.resample_apply`** (~1.5d) — wire a "resample to crisp" action in the Inspector through a `commands::Command` + the new verb (one journal entry, undoable, `ObjectId` preserved), gated on `DetailSource::PaintedRaster`, with the `bounds()`-read/write TSan case `selection.md` Open question 2 anticipated. That consumer is genuine agent-implementable work *once the API exists*, so it becomes a WBS leaf then, not now.

---

## Camera frame gizmo chrome unification with cell handle helpers

**Source:** `tasks/refinements/editor/gizmo.md` (cells.gizmo, 2026-07-29) — Open questions / D-gizmo-6.

**Trigger:** a decision to refactor the camera frame chrome for aesthetic/consistency reasons after the cell gizmo and camera gizmo have both shipped and been used.

`editor.cells.gizmo` ships the richer cell handle set and the reusable `interact::placed_quad` anchor both gizmos already share, but leaves the shipped, working camera frame chrome (`draw_frame_gizmos`, `src/app/canvas_view.cpp`) as-is (D-gizmo-6). Whether to later refactor the camera frame to draw over the same handle helpers is an aesthetic/consistency call a human should weigh against the churn of touching a shipped, tested gizmo with no behavioural change — it is not a feature and cannot be closed by an implementer, so it is not a WBS leaf.

---

## add(X)→remove(X)→add(X) collapsing in one drive iteration

**Source:** `tasks/refinements/canvas/pending_removes_order.md` (canvas.pending_removes_order, 2026-07-28) — Open questions / D-pending_removes_order-1 accepted consequence.

**Trigger:** real multi-canvas use where a caller posts a rapid add→remove→add triple that collapses into one `drive_once` iteration.

With remove-pre-empts-add (D-pending_removes_order-1), a UI sequence of `add(X)` → `remove(X)` → `add(X)` whose three calls collapse into one drive iteration cancels the final re-add: `pending_removes` erases the queued add from `pending_adds`, and the second `add(X)` lands in the freshly-cleared `pending_adds` before the next swap, so X does not surface until the following iteration. This follows directly from the fact that `pending_adds`/`pending_removes` are unsequenced vectors — the host cannot reconstruct sub-iteration ordering — and matches the triaged "remove pre-empts" rule. No agent-implementable fix exists that preserves the pre-emption rule without adding per-call ordering metadata (a sequenced queue, not a set of unordered vectors). Surface this only if the rapid triple is observed in practice causing a user-visible missed canvas; if it is, the fix would require a sequenced submission model, which is an A5/D-pending_removes_order-1 amendment, not a narrow bug fix.

---

## Per-layer blend facet — upstream arbc change + D10 scope reversal required

**Source:** `tasks/refinements/panels/inspector.md` (panels.inspector, 2026-07-29) — D-inspector-5; implementer return summary parking-lot item (a).

**Trigger:** a new libarbc release adding a per-layer blend-mode facet AND a human decision to reverse D10's v1 no-blend-space-toggle constraint.

D10 (`docs/00-design.md:477`) forbids a v1 blend-space toggle; libarbc v0.4.0 has no per-layer blend-mode facet at all (grep of `arbc/model` records/model confirms none; the compositor is fixed source-over). The inspector's appearance block therefore shows opacity + visibility only (D-inspector-5). A per-layer blend mode would require: (a) an upstream `arbitrarycomposer` change adding the facet; (b) a human decision to reverse D10 for v1 scope. Neither is agent-implementable. If both conditions are met, the inspector appearance block is the natural place to surface it, and the existing `set_cell_opacity`/`set_cell_visible` pattern is the implementation mould.

**Human action:** file upstream issue if blend modes are wanted; then revise D10 in `docs/00-design.md`.

---

## A18 snapshot migration for all panels — cross-panel architecture call

**Source:** `tasks/refinements/panels/inspector.md` (panels.inspector, 2026-07-29) — D-inspector-6; implementer return summary parking-lot item (b).

**Trigger:** a human decision that all info panels should read `Document`-structure through A18 published writer-built snapshots rather than through the shipped UI-thread scene read-seam.

`InspectorPanel` reads `scene::cells`/`scene::cameras`/`resolution_health` on the UI thread via the shipped read-seam, exactly as `CameraInspector` does, covered by the TSan case added to `tests/canvas_host_test.cpp`. A18 (`docs/01-architecture.md:433`) mandates `Document`-structure reads go through a writer-built immutable snapshot; the History panel already uses one (`commands::HistoryPublisher` → `journal().history()`). Whether all panels should migrate to a unified snapshot model is a cross-panel architecture-consistency call: the threading owner (editor canvas work) must decide the policy and migrate all panels together. It is not a per-leaf "audit" task — migrating only the inspector would fork the threading model mid-panel-set. D-inspector-6 records the decision and its rationale; this parking-lot entry is the trigger for revisiting it when the architecture owner deems it necessary.

**Human action:** assign to a threading / architecture review once the canvas threading work is complete enough to mandate the migration policy.

---

## render_loop_liveness_wake re-defer — `pending_external_loads()` is writer-thread-only

**Source:** `tasks/refinements/editor.canvas/render_loop_liveness_wake.md` (re-defer 2026-07-29).

**Trigger:** Either (a) refinement_writer re-issues `render_loop_liveness_wake.md` with a corrected design that avoids the any-thread read of `Document::pending_external_loads()` — e.g. by guarding the read behind a document-scoped `settle_in_flight` check and updating D-1 / D-3 / Constraint 4 and the A4.1b doc delta accordingly — OR (b) a new libarbc release adds an atomic any-thread `pending()` counter to `PendingExternalLoads` (mirroring `d_ready_count` / `ready()`) so the render loop can witness the full request→arrival window without racing the writer-thread settle.

`Document::pending_external_loads()` is **writer-thread-only**: `PendingExternalLoads::pending()` returns `d_pending.size() + d_pending_assets.size()` over plain `std::unordered_map`s explicitly marked "Writer/load-thread-only state" (`pending_external_loads.hpp:127,162-168`), with no mutex. The writer's `settle_external_loads` → `take_pending_asset` does `d_pending_assets.erase(…)` unsynchronized; a concurrent render-thread read of `pending()` races that erase — TSan flags it. The A4.1b lock-free read list (`docs/01-architecture.md:211-214`) names `external_loads_ready()` but **not** `pending_external_loads()`; only `ready()` (the atomic `d_ready_count`) is designated the render-thread poll (`pending_external_loads.hpp:146-154`). The pre-emptive A4.1b amendment at `docs/01-architecture.md:225-233` (committed before implementation confirmed feasibility) claims `Document::pending_external_loads()` as a render-thread poll; that claim is incorrect and must be reverted or corrected when the refinement is re-derived.
