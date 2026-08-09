# Parking lot — human/legal judgment items (not WBS tasks)

Items surfaced by refinements that a WBS implementer cannot decide and that do
**not** gate any leaf. Reviewed by a human; not scheduled by the orchestrator.

- **Decidable now** — every input exists; the item waits on a human to choose, and
  re-reading it next cycle will not produce new information.
- **Waiting on evidence** — the call is blocked on something that does not exist
  yet: real use, profiling data, or a leaf that has not shipped. Each names its own
  trigger. Do not re-litigate these until the trigger fires.

A decided item is **deleted**, not annotated; git history is the record. A specific
gap in libarbc belongs in a `ruoso/arbitrarycomposer` issue, not here — file it even
when nothing below is blocked on it, and cite the number in the entry.

---

# Decidable now

*Empty.* Decided items are deleted, not annotated. One that is decided but
deliberately deferred goes to the **M10 post-v1** milestone
(`editor.postv1` in `tasks/00-editor.tji`) rather than staying here — a question
with an answer recorded against it is no longer a question, and leaving it here
invites the decision to be relitigated every pass.

---

# Waiting on evidence

## Provisional input bindings across three shipped panels (§11 input map)

**Source:** `tasks/refinements/editor.panels/layers.md` (editor.panels.layers,
2026-07-29); `tasks/refinements/editor/overview.md` (editor.panels.overview,
2026-07-30); `tasks/refinements/editor.panels/color_eyedrop_cell.md`
(panels.color_eyedrop_cell, 2026-07-30) — D-eyedrop_cell-4.

**Trigger:** §11 input map settled in `docs/00-design.md` (still open under *Not yet
designed* → "Full input map").

Three sets of provisional bindings, carried as one item because they share a trigger
and will be answered in a single input-map pass:

- **Layers panel** — single-click select, disclosure-triangle expand, double-click
  enter, crumb-click climb. The input-map owner may want expand on a key chord, or a
  different double-click-vs-modifier boundary.
- **Overview panel** — double-click-to-enter, zoom-control chord, fit-to-camera click.
- **Eyedropper modifier** — **Alt** for the active-cell isolated sample, read from
  `CanvasInput.alt` (`src/views/ace/views/views.hpp:69`). Alt is free in the
  Eyedropper context and is the conventional "sample this specific thing" modifier,
  so it is a defensible v1 binding; the chord may be confirmed, remapped, or given a
  dedicated modifier. (D24's deep-zoom bindings carry the same provisional status,
  stated in the design row itself rather than here.)

All three are **cheap to move**, which is why none is a WBS task: the model is
independent of the binding and only one L4 dispatch site is rebindable — the panel
body for Layers, `OverviewPanel` for the overview, and `in.alt` in
`dispatch_eyedropper` (`src/app/canvas_view.cpp`) for the eyedropper. The scope model
(`AppState::entered_composition`), the breadcrumb derivation, the L1 path and geometry
helpers, and the navigator seam all stay put when the bindings move.

---

## Own-colour sampling for operator cells

**Source:** `tasks/refinements/editor.panels/color_eyedrop_nested.md`
(panels.color_eyedrop_nested, 2026-07-30) — D-eyedrop_nested-3.

**Trigger:** the still-image editor gains an operator-cell authoring surface.

The Alt-modifier eyedropper's isolation gate returns `std::nullopt` for operator
cells, so an Alt-sample over one falls back gracefully to the composited colour — a
strict superset of prior behaviour, not a regression. Making an operator report its
*own* colour would mean rebuilding the operator plus its input closure into an
anonymous `Document` via the registry codec `deserialize` (`registry.hpp:76-78`),
anchoring it, and calling `render_offline`. That mechanism is understood and ready.

It is unscheduled because **no authorable fixture exists to validate it against**.
The editor holds no kind allowlist, so whatever is registered is offered — but of the
six kinds `register_builtin_kinds` registers (`solid`, `raster`, `tone`, `nested`,
`fade`, `crossfade`; the last two enter via `FadeContent::kind_id` constants, not
string literals, so a grep for `org.arbc.` in `builtin_kinds.cpp` misses them), none
can produce the cell this item is about. `arbc::is_operator` is just
`!content->inputs().empty()` (`operator_graph.hpp:84-86`): `solid`/`raster`/`tone`
are sources with no inputs; `nested` is an operator the eyedropper gate already
carves out by `composition_ref().valid()` (`color.cpp:94-100`); and `fade`/`crossfade`
are operators whose factories **refuse every `ContentConfig` there is** — an
operator's input edges cannot travel one — so no string a user types can mint them.

The trigger is real rather than rhetorical: `register_extra_kinds` is a live hook
(`project_open.cpp:136,310`), so a plugin or editor-authored kind with non-empty
`inputs()` would surface in the dialog and create the first consumer overnight. The
question underneath — *should* the editor ever author operator cells — is a product
call, and this item resolves as a side effect of answering it.

---

## Full camera gizmo on overview frames

**Source:** `tasks/refinements/editor.panels/overview_gizmo.md`
(editor.panels.overview_gizmo, 2026-07-30) — D-overview_gizmo-4.

**Trigger:** real use showing users reach for camera recrop from the overview rather
than climbing to the canvas.

`editor.panels.overview_gizmo` ships the full scale/rotate/shear gizmo on schematic
**cell** boxes, reusing the cell-gizmo math. Camera frames stay **move-only** —
draggable, but with no scale/rotate/shear handles (`overview_panel.cpp:303`,
D-overview_gizmo-4/7). So what is missing is specifically **aspect-locked recrop and
dutch on a camera from the overview**, not camera manipulation as such.

This is a **capability** the editor has nowhere, not the chrome refactor D-gizmo-6
described (redrawing already-shipped camera frame handles over shared helpers, no
behaviour change) — that one was declined for v1 as churn. The two look alike and are
not. If wanted, this is a small reuse of the canvas camera-gizmo path, not new
geometry.

---

## One shared render thread vs N render threads for multi-canvas

**Source:** `tasks/refinements/editor/multi_canvas.md` (multi_canvas, 2026-07-18), D-multi_canvas-2.

**Trigger:** observed head-of-line blocking in real multi-canvas use.

`render::CanvasHost` drives all N canvases from one shared render thread. A5 mandates a shared `WorkerPool` but is silent on render-thread count; D-multi_canvas-2 chose one thread as the conservative baseline (single pool drainer, smaller TSan surface, correct for the realistic N of 2–3 canvases). If profiling shows head-of-line blocking is observable (e.g. one heavy canvas visibly slowing the others), moving to N render threads would require revisiting the borrowed-pool concurrent-submitter contract with libarbc. That is a monitor-and-decide call gated on real profiling data, not implementable work today.

---

## "New Shot From View" — focused canvas vs explicit designation

**Source:** `tasks/refinements/cameras/new_shot_from_view.md` (cameras.new_shot_from_view, 2026-07-23) — Open questions.

**Trigger:** multi-canvas layouts in real use.

`editor.cameras.mint_from_focused_canvas` implements the "follow focus with lowest-id fallback" approach: the gateway reads `CanvasView::focused_framing()` and the mint always promotes the canvas the user was most recently working in. The alternative — an explicit "promote this canvas" affordance in the canvas camera picker — would let the user designate a specific canvas regardless of focus. These two shapes have different discoverability tradeoffs: focus tracking is invisible until it matters (multi-canvas layout); explicit designation adds picker chrome that is meaningless in single-canvas use. Whether the right end state is the focus-tracking rail item alone, the explicit picker, or both is a D23/D18 design call for a human once multi-canvas layouts are in real use. If explicit designation is the preferred answer, `mint_from_focused_canvas` may need to be reconsidered or extended.

---

## Suppress the focused-canvas indicator on a single-canvas dock?

**Source:** `tasks/refinements/canvas/focused_canvas_indicator.md`
(canvas.focused_canvas_indicator, 2026-07-23) — D-focused_canvas_indicator-3.

**Trigger:** the always-on border reading as chrome noise in real single-canvas use.

D-focused_canvas_indicator-3 chose to draw the hairline accent border
unconditionally — even on a single-canvas dock where it conveys no disambiguating
information — because suppressing it would trade a one-pixel constant for a
conditional invariant ("bordered pane = where the verb lands" would stop being
universally true) and would suppress the affordance precisely when a new user first
learns what it means. If in practice the always-on border reads as chrome noise when
only one canvas is open, the fix is a one-condition check in the draw block plus one
e2e phase; the Catch2 rule matrix is unaffected. A product-taste call that warrants
real use before deciding.

---

## Sync-submit latency behind a deep async burst

**Source:** `tasks/refinements/editor/writer_thread.md` (canvas.writer_thread, 2026-07-23) — Open questions #3.

**Trigger:** profiling on the real pool — **after** `editor.canvas.settler_attach_split` lands.

`editor.canvas.writer_thread` ships all-sync for result-carrying verbs (D-3). A streamed gesture burst could build a queue depth that makes a subsequent sync `undo` wait. The coalescing key bounds the *commit* cost, not the queue depth. Whether this is observable in practice is unknown; if it bites, adding a bounded depth or a gesture-drop policy would be the fix.

**Do not measure before the split lands.** `editor.canvas.settler_attach_split`
removes a known contributor to the depth this item is about: today every canvas add,
remove and resize rebuild posts a whole `HostViewport` construction or destruction to
the writer thread via `submit_sync` (`src/render/canvas_renderer.cpp:73,150`,
`src/render/canvas_host.cpp:116`), purely because the settler install lives in the
ctor. After that leaf only the attach/detach pair is posted, so profiling now would
characterise a queue shape that is being retired. The chain is ordinary scheduled work
— `editor.canvas.arbc_v070` → `arbc_router_attach_split` → `settler_attach_split`.

---

## Export destination re-seed: project/layout identity vs service `instance()`

**Source:** `tasks/refinements/cameras/export_destination_reseed.md` (cameras.export_destination_reseed, 2026-07-24) — D-reseed-1 rejected alternative.

**Trigger:** an in-process project reopen ever shipping.

The Export panel's re-seed keys on `ExportService::instance()` — a monotonic per-service id — which is a faithful proxy for the live project because the shipped model is 1:1 service-per-process (`project_gateway.hpp:21-33`, `app_state.hpp:284-285`). If the editor ever grows an in-process project reopen (swapping `Document`/`AppState` under a **surviving** `ExportService` instead of the current detached-sibling-`exec` model), `instance()` would not change across the swap, so the panel would retain the previous project's destination. The fix in that future is to key the re-seed on a project/layout identity instead — D-reseed-1's rejected alternative, correct only under that future architecture.

Note that `editor.project.reconstructing_reopen` does **not** move this trigger: it changes how a project's document is reconstructed on open, not whether the editor reopens a different project in process. Opening another project is still a detached sibling `exec` (`editor.project.exec_new`). If in-process reopen ever ships, revisit `views.cpp:315-317` and `ExportPanel::owner`.

---

## add(X)→remove(X)→add(X) collapsing in one drive iteration

**Source:** `tasks/refinements/canvas/pending_removes_order.md` (canvas.pending_removes_order, 2026-07-28) — D-pending_removes_order-1 accepted consequence.

**Trigger:** real multi-canvas use where a caller posts a rapid add→remove→add triple that collapses into one `drive_once` iteration.

With remove-pre-empts-add (D-pending_removes_order-1), a UI sequence of `add(X)` → `remove(X)` → `add(X)` whose three calls collapse into one drive iteration cancels the final re-add: `pending_removes` erases the queued add from `pending_adds`, and the second `add(X)` lands in the freshly-cleared `pending_adds` before the next swap, so X does not surface until the following iteration. This follows directly from `pending_adds`/`pending_removes` being unsequenced vectors — the host cannot reconstruct sub-iteration ordering — and matches the remove-pre-empts rule. No agent-implementable fix preserves the pre-emption rule without per-call ordering metadata (a sequenced queue, not a set of unordered vectors). Surface this only if the rapid triple is observed causing a user-visible missed canvas; the fix would be a sequenced submission model, an A5/D-pending_removes_order-1 amendment rather than a narrow bug fix.

---

## A18 snapshot migration for all panels

**Source:** `tasks/refinements/panels/inspector.md` (panels.inspector, 2026-07-29) — D-inspector-6.

**Trigger:** a human decision that all info panels should read `Document`-structure through A18 published writer-built snapshots rather than through the shipped UI-thread scene read-seam — which needs the canvas threading work complete enough to mandate a policy. That chain (`editor.canvas.arbc_v070` → `arbc_router_attach_split` → `settler_attach_split`) has not landed.

`InspectorPanel` reads `scene::cells`/`scene::cameras`/`resolution_health` on the UI thread via the shipped read-seam, exactly as `CameraInspector` does, covered by the TSan case in `tests/canvas_host_test.cpp`. A18 (`docs/01-architecture.md:433`) mandates `Document`-structure reads go through a writer-built immutable snapshot; the History panel already uses one (`journal().history()`). Whether all panels should migrate is a cross-panel architecture-consistency call: the threading owner must decide the policy and migrate all panels together. It is not a per-leaf "audit" task — migrating only the inspector would fork the threading model mid-panel-set.

**Human action:** assign to a threading / architecture review once that chain lands.

---

## Overview visual-polish open items (§5:204-206)

**Source:** `tasks/refinements/editor/overview.md` (editor.panels.overview, 2026-07-30) — §5:204-206; `tasks/refinements/editor.panels/overview_gizmo.md` for the fourth.

**Trigger:** a design pass after the overview has been used in real compositions.

Four provisional visual defaults, all human/design-taste calls that retune the L4 draw
constants without touching the model or geometry:

- the exact hatch style and semi-opacity level;
- the pattern-count threshold before colour must carry the load (the
  `overview_pattern` fallback is parameterized; the threshold is provisional);
- the camera visual language (frame outline + label, no affordance chrome beyond the
  look-through click target);
- the gizmo handle chrome — weight and colour of the drag handles on schematic boxes,
  which `editor.panels.overview_gizmo` ships by reusing the canvas gizmo's palette
  rather than choosing its own.

---

## Active color foreground/background slot + fg↔bg swap

**Source:** `tasks/refinements/panels/color.md` (editor.panels.color, 2026-07-30) — D-color-5.

**Trigger:** a paint tool that reads a background colour is scheduled (an eraser or fill tool needing a distinct background value), or a design row in `docs/00-design.md` materializes the foreground/background pair.

`editor.panels.color` ships a single foreground active colour on `commands::AppState`. D-color-5 defers a foreground/background pair + swap: the v1 modal set is closed (`{Select, Brush, Eyedropper, Pan}`) with no eraser or fill that reads a background, so materializing the pair now is machinery ahead of a requirement. When a consumer exists, adding a background slot is a small, edge-free extension.

---

## Cross-cell strokes and advanced brush modes (hardness / eraser / blend / pressure)

**Source:** `tasks/refinements/paint/brush.md` (editor.paint.brush, 2026-07-30) — D-brush-7.

**Trigger:** a design row in `docs/00-design.md` demanding one of these, or a concrete consumer that cannot be served by the current single-cell soft round dab.

`editor.paint.brush` ships a single soft `round_dab` that paints only the selection-primary `org.arbc.raster` cell. D-brush-7 defers cross-cell stroke distribution and brush hardness/eraser/blend-mode/pressure: the arbc dab API supports hard/soft and explicit masks (`raster_content.hpp:99-104`), so each is cheap to add when a consumer asks, but minting controls now is machinery ahead of a requirement.

---

## Anisotropic (elliptical) dab shape under non-uniform placement

**Source:** `tasks/refinements/editor/paint_res.md` (editor.paint.paint_res, 2026-07-31) — D-paint_res-3.

**Trigger:** a design row in `docs/00-design.md` demanding affine-correct (anisotropic) dabs, **or** the ring-vs-mark mismatch observed in real use on a non-uniformly-scaled cell.

The shipped brush paints an isotropic circular `round_dab` (circular in content px). Under a non-uniform placement scale (an edge-dragged cell; D8 edges are 1D) a screen-circular brush ring maps to an ellipse in content px, so the painted mark does not exactly match the on-screen ring. The detail-floor cue is already worst-axis-correct (`placement.max_scale()`, D-paint_res-3), so the *floor verdict* is honest regardless; only the *mark shape* is approximate, and the isotropic dab is exact under the common uniform placement.

**Implementable as written whenever wanted** — the seam exists at the pin:
`RasterContent::paint` has a `coverage` overload taking a caller-supplied
`CoverageSampler` (`raster_content.hpp:339-340`), itself a plain
`std::function<float(int gx, int gy)>` (`:92`), and the library names "an explicit
alpha mask" as an intended use. So an affine-correct sampler (mapping each content
pixel back through the composed `camera∘placement` to evaluate the screen-space radial
falloff) is an ordinary lambda in L1 `interact`, with no library change and no kind
knowledge, plus a `scene::brush_dab` overload taking it. Becomes WBS leaf
**`editor.paint.anisotropic_dab`** (~1.5d), gathered into `editor.packaging.package`.

---

## Raw-RGBA clipboard scope and image encoder choice

**Source:** `tasks/refinements/editor/paste.md` (editor.import.paste, 2026-07-31) — D-paste-4.

**Trigger:** real use showing raw-RGBA-only clipboards are a common source of paste failures, **and** a decision on which image encoder to vendor (imdec is decode-only).

`editor.import.paste` ships encoded-clipboard-only (prefer `image/png`, then any imdec-decodable mime); a clipboard with only raw pixels is a graceful no-op. Extending v1 to accept raw RGBA needs an image encoder — imdec is decode-only, so a new dependency (`stb_image_write.h`, already vendored for export, or `libpng`) would be required. The encoder choice is a dependency/product call, and the limitation is only observable on applications that place raw RGBA on the clipboard without also placing a PNG form (uncommon in practice).
