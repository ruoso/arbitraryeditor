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

**A trigger that names an upstream fix must also name the issue that asks for
it** (added at the 2026-08-07 triage). That triage found that *none* of the six
"Human action: file an upstream issue" items had ever been filed — the highest
issue on `ruoso/arbitrarycomposer` was still `#27`, from the pre-v0.4.0 batch, so
every library-gated item was waiting on a release that nobody had requested. The
v0.4.0 triage collapsed 22 items to 6 precisely *because* eleven issues had been
filed and closed; an unfiled item is not waiting on evidence, it is stalled. So:
an item whose trigger is "a new libarbc release that…" is **not** correctly
parked until it carries an issue number. File first, park second.

---

# Decidable now

*Empty.* The 2026-07-28 triage decided the three items that stood here (bundled
font, welcome-window treatment, worker-backed tile dispatch), and the 2026-08-07
triage decided the two that had arrived since — the **Assets view real-body
owner** (subsumed by the shipped Layers panel; no asset-browser leaf) and the
**camera frame gizmo chrome unification** (declined for v1). Git history is the
record of each; the reasoning that survives is carried in the leaf note and in
the architecture rows the decisions touch, not here. A new item lands here only
if it is genuinely waiting on a human choice rather than on evidence.

---

# Waiting on evidence

## Provisional input bindings across three shipped panels (§11 input map)

**Source:** `tasks/refinements/editor.panels/layers.md` (editor.panels.layers,
2026-07-29); `tasks/refinements/editor/overview.md` (editor.panels.overview,
2026-07-30); `tasks/refinements/editor.panels/color_eyedrop_cell.md`
(panels.color_eyedrop_cell, 2026-07-30) — D-eyedrop_cell-4.

**Trigger:** §11 input map settled in `docs/00-design.md` (still listed under
*Not yet designed* → "Full input map", verified 2026-08-07).

*Consolidated at the 2026-08-07 triage* from three separate entries. They shared
one trigger, will be answered in one input-map pass, and re-reading them as three
open items each cycle overstated how much was outstanding. The three sets of
provisional bindings:

- **Layers panel** (`editor.panels.layers`) — single-click select,
  disclosure-triangle expand, double-click enter, crumb-click climb. The
  input-map owner may want expand on a key chord, or a different
  double-click-vs-modifier boundary.
- **Overview panel** (`editor.panels.overview`) — double-click-to-enter,
  zoom-control chord, fit-to-camera click.
- **Eyedropper modifier** (`editor.panels.color_eyedrop_cell`) — **Alt** for the
  active-cell isolated sample, read from `CanvasInput.alt`
  (`src/views/ace/views/views.hpp:69`). Alt is free in the Eyedropper context and
  is the conventional "sample this specific thing" modifier, so it is a
  defensible v1 binding; the chord may be confirmed, remapped, or given a
  dedicated modifier. (D24's deep-zoom navigation bindings carry the same
  provisional status, stated in the design row itself rather than here.)

All three are **cheap to move**, which is why none is a WBS task: in each case
the model is independent of the binding and only one L4 dispatch site is
rebindable — the panel body for Layers, `OverviewPanel` for the overview, and
`in.alt` in `dispatch_eyedropper` (`src/app/canvas_view.cpp`) for the
eyedropper. The scope model (`AppState::entered_composition`), the breadcrumb
derivation, the L1 path and geometry helpers, and the navigator seam all stay put
when the bindings move. Human design call gated on the input map.

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

*Re-checked at the 2026-08-07 triage:* the trigger has moved **further** out, not
closer. `editor.canvas.settler_attach_split` is blocked on
`arbc_router_attach_split`, which is blocked on `arbc_router_split_pin`, which now
waits on upstream **#28**. So the measurement this item asks for is gated on an
upstream release. Nothing to do here until that chain clears; the amendment below
still governs *why* it must wait.

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

## DamageRouter registrant split — arbc#25 successor **filed as arbc#28**; unblocks settler_attach_split

**Source:** `tasks/refinements/canvas/settler_attach_split.md` (re-defer 2026-07-28);
confirmed by `tasks/refinements/canvas/arbc_router_split_pin.md` (re-defer 2026-07-28 —
`arbc_router_split_pin` was picked up and verified the upstream state independently: no
successor tag exists, `DamageRouter::register_sink` still welded to ctor at v0.4.0);
re-confirmed by `tasks/refinements/editor.canvas/arbc_router_split_pin.md` (re-defer
2026-07-29 — second independent verification: latest tag still `v0.4.0`, no
`install_router` flag and no `attach_router`/`detach_router` pair on any branch);
re-confirmed again by `tasks/refinements/editor.canvas/arbc_router_split_pin.md` (re-defer
2026-07-30 — third independent verification: `git fetch --all --tags` pulled nothing newer,
latest tag still `v0.4.0`, no `attach_router`/`detach_router`/`install_router` symbol on any
branch or in any commit in history, all refs grepped).

**Filed upstream:** [`ruoso/arbitrarycomposer#28`](https://github.com/ruoso/arbitrarycomposer/issues/28) (2026-08-07).

**Trigger:** a new libarbc release closing **#28** — moving `DamageRouter::register_sink` (and `Document::set_damage_sink`) out of `HostViewport` ctor/dtor into the `attach_settler()`/`detach_settler()` pair (or a dedicated `attach_router`/`detach_router()` pair), gated by an `install_settler`-style flag.

`editor.canvas.settler_attach_split` was found unimplementable against libarbc v0.4.0: arbc#25 split only the settler slot (`set_external_load_settler`), NOT the `DamageRouter::register_sink` call, out of `HostViewport` ctor/dtor. The `DamageRouter` registration is welded unconditionally to the ctor (`host_viewport.cpp:66-68`) and has no internal synchronization (`damage_router.hpp:29-34`), so render-thread construction with `install_settler=false` still races the router's registrant vector against concurrent writer-thread damage flushes. That is exactly the race the `:1639` TSan anchor's guard note (`canvas_host_test.cpp:1636-1638`) names; the hermetic gcc-tsan lane would report it.

*Filed at the 2026-08-07 triage.* The fourth verification pass (2026-08-07:
`git fetch --all --tags` pulls nothing, latest tag still `v0.4.1`, no
`attach_router`/`detach_router`/`install_router` symbol on any ref) finally
established **why** three passes had found nothing: the successor issue this
entry had been asking a human to file since 2026-07-28 had never been filed. It
now is — **#28**. The re-verification loop is closed: the watch is
`gh issue view 28 --repo ruoso/arbitrarycomposer`, and while it is open there is
nothing to check. `editor.canvas.arbc_router_split_pin` carries
`flags upstream_blocked` so `scripts/unblocked.py` lists it under **WAITING ON
UPSTREAM** rather than READY, which is what let it be picked up and re-deferred
three times (`a7d473c`, `4b7f5b7`, `7f84756`).

**When the trigger fires:**
1. Drop the `upstream_blocked` flag and mark `editor.canvas.arbc_router_split_pin` complete once the pin bump to that release lands.
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

**Filed upstream:** [`ruoso/arbitrarycomposer#33`](https://github.com/ruoso/arbitrarycomposer/issues/33) (2026-08-07).

**Trigger:** a new libarbc release closing **#33** — adding a `KindInsertSchema` for `org.arbc.nested` (with a labelled `ObjectId` field).

libarbc registers `org.arbc.nested` with **no** schema (still true at v0.4.1: `builtin_kinds.cpp:245,255,259` register schemas for the solid, tone and raster kinds; nested's factory grammar parses a bare decimal `ObjectId` at `:184` with no schema beside it), so nested inserts fall to the editor's raw-config box (a decimal `ObjectId` text box), losing `editor.cells.model`'s labelled "Child composition (ObjectId)" field. The correct fix is a cross-repo change to `arbitrarycomposer` to advertise a named field for the nested ObjectId. Once that ships in a future pin, **no editor code is needed** — the field appears automatically through the no-allowlist enumeration, which is exactly what `Registry::insert_schema` (arbc#21) was for. The issue also asks whether the schema field type can be an `ObjectId`/reference variant rather than a plain integer, which would let a host offer a composition picker instead of a number box; either answer closes the editor's gap.

---

## libarbc has no content-resample verb — "resample to crisp" blocked on upstream

**Source:** `tasks/refinements/editor/resolution.md` (cells.resolution, 2026-07-28) — Open questions / D-resolution-5.

**Filed upstream:** [`ruoso/arbitrarycomposer#31`](https://github.com/ruoso/arbitrarycomposer/issues/31) (2026-08-07).

**Trigger:** a new libarbc release closing **#31** — adding a generic `Resampleable` content facet (or equivalent verb).

`arbc::RasterContent` exposes construction + `paint` only (`raster_content.hpp:297-362`); there is no in-place resize verb. Implementing "resample to crisp" (grow a raster's working grid to match or exceed the camera's pixel density) editor-side would require naming the concrete `RasterContent` type (A16 no-allowlist) and owning kind-specific upsampling (levelization — `arbc::media`'s resampler is the kind's floor, not the editor's). The clean fix is an **upstream generic content-resample facet** — a `Resampleable` a kind opts into and the host discovers via a `Content` virtual (a referenced `org.arbc.image` returns `nullptr` → "source-limited"; a raster implements it to resize its grid to a new version via `set_content_state`) — so the host offers "resample to crisp" without naming any kind.

*Filed at the 2026-08-07 triage as **#31**.* Re-verified against v0.4.1 first: `RasterContent`'s public surface is still construction (`raster_content.hpp:299`) plus the two `paint` overloads (`:339,341`) — no in-place resize verb, and nothing on `Content` a host could call generically.

**When the trigger fires** (a libarbc release adding it): bump the pin, then implement the editor-consumer leaf **`editor.cells.resample_apply`** (~1.5d) — wire a "resample to crisp" action in the Inspector through a `commands::Command` + the new verb (one journal entry, undoable, `ObjectId` preserved), gated on `DetailSource::PaintedRaster`, with the `bounds()`-read/write TSan case `selection.md` Open question 2 anticipated. That consumer is genuine agent-implementable work *once the API exists*, so it becomes a WBS leaf then, not now.

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

**Human action:** file upstream issue **if blend modes are wanted**; then revise D10 in `docs/00-design.md`.

*Deliberately NOT filed at the 2026-08-07 triage*, and this is the one library-gated item that stays that way. The other six were filed because the editor wants the capability and only upstream can supply it. This one is different: condition (b) — reversing D10's v1 no-blend-space-toggle constraint — has **not** been decided, and D10 currently says no. Filing a feature request for something the design forbids would ask upstream to build against a decision that has not been made. The ordering is therefore fixed: **decide D10 first, file second.** If D10 is reversed, file the upstream issue then and this entry converts to the same shape as the other six.

---

## A18 snapshot migration for all panels — cross-panel architecture call

**Source:** `tasks/refinements/panels/inspector.md` (panels.inspector, 2026-07-29) — D-inspector-6; implementer return summary parking-lot item (b).

**Trigger:** a human decision that all info panels should read `Document`-structure through A18 published writer-built snapshots rather than through the shipped UI-thread scene read-seam.

`InspectorPanel` reads `scene::cells`/`scene::cameras`/`resolution_health` on the UI thread via the shipped read-seam, exactly as `CameraInspector` does, covered by the TSan case added to `tests/canvas_host_test.cpp`. A18 (`docs/01-architecture.md:433`) mandates `Document`-structure reads go through a writer-built immutable snapshot; the History panel already uses one (`commands::HistoryPublisher` → `journal().history()`). Whether all panels should migrate to a unified snapshot model is a cross-panel architecture-consistency call: the threading owner (editor canvas work) must decide the policy and migrate all panels together. It is not a per-leaf "audit" task — migrating only the inspector would fork the threading model mid-panel-set. D-inspector-6 records the decision and its rationale; this parking-lot entry is the trigger for revisiting it when the architecture owner deems it necessary.

**Human action:** assign to a threading / architecture review once the canvas threading work is complete enough to mandate the migration policy.

*Re-checked at the 2026-08-07 triage:* not yet — the canvas threading work is not
complete. Its three open leaves (`arbc_router_split_pin` →
`arbc_router_attach_split` → `settler_attach_split`) all wait on upstream **#28**,
so the threading owner does not yet have a settled model to mandate a policy from.
Revisit after that chain lands.

---

## arbc nested-render worker-detach race — **survives the v0.4.1 pin**

**Source:** `tasks/refinements/editor/overview.md` (editor.panels.overview, 2026-07-30) — fixer sub-agent return summary (attempts 1–5).

**Filed upstream:** [`ruoso/arbitrarycomposer#29`](https://github.com/ruoso/arbitrarycomposer/issues/29) (2026-08-07).

**Trigger:** a new libarbc release closing **#29** — fixing the `NestedContent::d_doc` detach race in the `WorkerPool` render path, OR adding a libarbc API that lets the interactive render join deferred worker tasks before the per-frame `OperatorBindingScope` destructor runs.

libarbc has a data race in its threaded interactive render path: `NestedContent::render` reads `d_doc` unsynchronized while the frame's `OperatorBindingScope` destructor nulls it (`nested_content.cpp:128-134`) on a worker thread. This fires only when the canvas renders nested compositions (`org.arbc.nested`) through the threaded interactive pool (`default_interactive_pool_config`); the inline pool (`WorkerPoolConfig{}`) is race-free. The editor's overview e2e works around this by using the inline pool for its canvas scaffolding. All live-canvas e2es that render `org.arbc.nested` (isolation_scope e2e, cells_scoped_edit e2e, others) are latently affected. No editor-side fix is appropriate — the race is inside the pinned dep.

*Retitled and re-verified at the 2026-08-07 triage.* This entry read "arbc v0.4.0" while the editor had already moved to the v0.4.1 pin, which invited the assumption that the bump had carried a fix. It had not. Checked directly against `v0.4.1` (`352bc6d`, the file having moved to `src/kind_nested/nested_content.cpp`): `detach()` still nulls `d_doc` at `:133`, and the render path still dereferences it without a barrier at `:528`, `:538`, `:599` and on the deferred worker-task path at `:876`, `:889`, `:905`. v0.4.1's `Content::visit_inputs` work fixed the *adjacent* nested-sample-vs-render conflict (that entry is closed) but not this one — do not read the one as having resolved the other.

---

## Overview visual-polish open items (§5:204-206)

**Source:** `tasks/refinements/editor/overview.md` (editor.panels.overview, 2026-07-30) — Open questions / §5:204-206.

**Trigger:** a design pass after the overview has been used in real compositions.

The overview ships provisional defaults for the three §5:204-206 open polish items: the exact hatch style and semi-opacity level (currently a defensible visual default); the pattern-count threshold before color must carry the load (the `overview_pattern` fallback is parameterized but the threshold is provisional); and the camera visual language (frame outline + label, no affordance chrome beyond the look-through click target). All three are human/design-taste calls that retune without touching the model or geometry. No WBS task — retune the L4 draw constants when a design pass produces a preferred value.

---

## Active color foreground/background slot + fg↔bg swap

**Source:** `tasks/refinements/panels/color.md` (editor.panels.color, 2026-07-30) — D-color-5.

**Trigger:** a paint tool that reads a background color is scheduled (e.g. an eraser or fill tool that needs a distinct background value), or a design row in `docs/00-design.md` explicitly materalizes the foreground/background pair.

*Verified not fired at the 2026-08-07 triage:* no eraser and no fill tool appears anywhere in `tasks/00-editor.tji` or `docs/00-design.md`, so no consumer of a background slot exists or is scheduled.

`editor.panels.color` ships a single foreground active color on `commands::AppState`. D-color-5 explicitly defers a foreground/background pair + swap: the v1 modal set is closed (`{Select, Brush, Eyedropper, Pan}`) with no eraser or fill that reads a background, so materializing the pair now is machinery ahead of a requirement. When a consumer exists, adding a background slot is a small, edge-free extension. No WBS task — enter the WBS when a concrete product need exists.

---

## Cross-cell strokes and advanced brush modes (hardness / eraser / blend / pressure)

**Source:** `tasks/refinements/paint/brush.md` (editor.paint.brush, 2026-07-30) — Open questions / D-brush-7.

**Trigger:** a design row in `docs/00-design.md` demanding one of these features, or a concrete consumer that cannot be served by the current single-cell soft round dab.

`editor.paint.brush` ships a single soft `round_dab` that paints only the selection-primary `org.arbc.raster` cell. D-brush-7 explicitly defers cross-cell stroke distribution and brush hardness/eraser/blend-mode/pressure as parking-lot observations: the arbc dab API supports hard/soft and explicit masks (`raster_content.hpp:99-104`) so each is cheap to add when a consumer asks, but minting controls now is machinery ahead of a requirement. No WBS task — enter the WBS when a design row or concrete product need exists.

---

## Anisotropic (elliptical) dab shape under non-uniform placement

**Source:** `tasks/refinements/editor/paint_res.md` (editor.paint.paint_res, 2026-07-31) — D-paint_res-3 / Open questions.

**Trigger:** a design row in `docs/00-design.md` demanding affine-correct (anisotropic) dabs, **or** the ring-vs-mark mismatch observed in real use on a non-uniformly-scaled cell.

The shipped brush paints an isotropic circular `round_dab` (circular in content px). Under a non-uniform placement scale (an edge-dragged cell, D8 edges are 1D) a screen-circular brush ring maps to an ellipse in content px, so the painted mark does not exactly match the on-screen ring. The detail-floor cue is already worst-axis-correct (`placement.max_scale()`, D-paint_res-3), so the *floor verdict* is honest regardless; only the *mark shape* is approximate. This is agent-implementable when wanted — build an affine-correct `CoverageSampler` in L1 `interact` (map each content pixel back through the composed `camera∘placement` to evaluate the screen-space radial falloff) and add a `scene::brush_dab` overload taking that sampler — but no design row demands elliptical marks and the isotropic dab is exact under the common uniform placement. When the trigger fires, this becomes WBS leaf **`editor.paint.anisotropic_dab`** (~1.5d) wired into the `editor.packaging.package` gather.

---

## libarbc GC blind to owned image blobs — `assets/images/` never rooted or reclaimed

**Source:** `tasks/refinements/editor/paste.md` (editor.import.paste, 2026-07-31) — GC-roots-owned-blob acceptance criterion escape clause.

**Filed upstream:** [`ruoso/arbitrarycomposer#30`](https://github.com/ruoso/arbitrarycomposer/issues/30) (2026-08-07).

**Trigger:** a new libarbc release closing **#30** — a GC mark walk that visits `params.source` refs on the image codec, **and** a reaper pass that scans `assets/images/` in addition to `assets/tiles/`.

libarbc v0.4.1's `gc_project_directory` reaper scans only `assets/tiles/` (`asset_gc.cpp:36`, with the same scope stated in the contract at `asset_gc.hpp:38,44,108`), and the GC mark walk harvests only `params.blobs` (`asset_gc.cpp:65-82`), never `params.source` — which is where the image codec keeps its asset URI (`codec_image.cpp:91-93` writes it, `:98,124` read it) and therefore where `mint_owned_asset` stores the owned-image URI. Consequence: (a) the GC-roots-owned-blob Catch2 test case from the acceptance criteria was NOT landed — neither half is satisfiable against the current library; (b) a paste→undo→save→Clean-Up cycle does NOT reclaim the orphaned owned image blob in practice, even though the design (D13/A23) expects it to. The owned blob is also never leaked (the reaper ignores `assets/images/` entirely), so no spurious deletion occurs. This is a structural library gap, not an editor-side issue.

**The two halves must land together, and the issue says so.** Fixing the reaper *alone* would turn today's benign leak into data loss: every owned image would enumerate as unrooted and be swept. Gap (1) currently masks gap (2), which is the only reason the present state is safe. When the trigger fires, confirm the release carries both before bumping the pin.

**When the trigger fires:** bump the pin and land the GC-roots-owned-blob test that the paste acceptance criteria deferred.

---

## Raw-RGBA clipboard scope and image encoder choice

**Source:** `tasks/refinements/editor/paste.md` (editor.import.paste, 2026-07-31) — D-paste-4 / Open questions.

**Trigger:** real use showing that raw-RGBA-only clipboards are a common source of paste failures, **and** a decision on which image encoder to vendor (imdec is decode-only).

`editor.import.paste` ships encoded-clipboard-only (prefer `image/png`, then any imdec-decodable mime); a clipboard with only raw pixels (no encoded form) is a graceful no-op. Extending v1 to accept raw RGBA would require vendoring an image encoder — imdec is decode-only, so a new dependency (e.g. `stb_image_write.h`, already adjacent to the existing `stb_image_write.h` vendored for export, or `libpng`) would be needed. The correct encoder choice is a dependency/product call, and the limitation is only observable on applications that place raw RGBA on the clipboard without also placing a PNG form (uncommon in practice). No WBS task until the limitation is confirmed painful in real use and an encoder is chosen.

---

## Live in-session resolution of a freshly-placed `org.arbc.nested` external reference

**Source:** `tasks/refinements/editor/nested.md` (editor.import.nested, 2026-07-31) — D-nested-3.

**Filed upstream:** [`ruoso/arbitrarycomposer#32`](https://github.com/ruoso/arbitrarycomposer/issues/32) (2026-08-07).

**Trigger:** a new libarbc release closing **#32** — exposing a public seam to install an external composition into an open `Document` post-deserialize (i.e. a live `ExternalCompositionLoader` path callable by a host at runtime, not just at `load_document` / `open_document` time).

`editor.import.nested` ships the defensible v1: a freshly-placed `org.arbc.nested` cell renders the doc-05 placeholder in the placing session and resolves on the next reopen (inline via the editor's `FilesystemAssetSource`, §4:140-149). libarbc's `ExternalCompositionLoader` is scoped to deserialize time (`LoadContext`-scoped, single-writer, one-load-one-thread); there is no public API to install an external composition into an already-open `Document`. No editor-side fix is possible — the seam must be supplied by the library. The shipped reopen-path is fully testable (save→reopen round-trip golden, `import_nested_64x64.rgba8`) and documents the live case as an honest limitation, not a bug.

*Filed at the 2026-08-07 triage as **#32**.* Re-verified against v0.4.1 first: the loader is still `LoadContext`-scoped throughout — a `LoadContext` owns one and exposes it as `loader()` (`document_serialize.cpp:691,746`), the nested codec receives it and drives it through that context (`codec_nested.cpp:25,105,139`), and `seed()` remains the only entry (`external_composition_loader.cpp:13,18`). No post-load install seam exists on any ref.

**When the trigger fires:** bump the pin and implement a new editor leaf (approximately `editor.import.nested_live_resolve`, ~1d) that calls the seam from the writer thread after a nested cell is placed, wiring it through the existing `FilesystemAssetSource` path.
