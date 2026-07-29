# editor.panels.inspector — Dense property sheet (transform / appearance / z-order / source)

## TaskJuggler entry

- **Task:** `editor.panels.inspector` (`tasks/00-editor.tji:589-594`, under
  `task panels "Info panels"` at `:588`).
- **Effort:** `2.5d` · `allocate team`.
- **Depends:** `editor.cells.selection` and `editor.dock.view_registry`
  (`tasks/00-editor.tji:592`).
- **Note (`.tji:593`):** *"A dense property sheet for the project selection:
  transform (units), appearance & stacking (opacity/blend/z-order), source
  (referenced/owned info + health). Design: D7 (density) / D11."* The note's
  parenthetical **"D7 (density)"** is imprecise: there is **no** `density`
  row in the constitution (`docs/00-design.md` has no `densit|property|dense`
  match outside physical resolution density). The governing rows are **D7**
  (manipulation model / units, `docs/00-design.md:474`), **D11** (two asset
  axes, `:478`), plus **D5** (`:472`, px is a health readout only), **D6**
  (`:473`, z-order surfacing lives on the list/overview), **D8/D9**
  (`:475-476`, placement never mutates resolution), **D10** (`:477`,
  straight-alpha opacity, no blend-space toggle) and **§6** (`:234-235`,
  `:252-255`, the exact inspector fields). "Dense property sheet" is a
  task-level framing over those rows, not its own decision.
- **Back-link:** the `.tji` note currently ends `Refinement:
  tasks/refinements/inspector.md` (the flat interim path). This refinement
  lands at **`tasks/refinements/panels/inspector.md`** (the path the
  orchestrator assigned for this leaf). The closer updates the note back-link
  to the real path and adds `complete 100` immediately after `allocate team`
  (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji` here.
- **Downstream dependents:** `editor.panels.layers` (`tasks/00-editor.tji:598`,
  `depends !inspector`) — the layers list is the interactive z-order and
  referenced-vs-painted surface that this readout panel intentionally does
  **not** duplicate (D6).

## Effort estimate

**2.5 days.** The panel *body* is a shipped pattern: `CameraInspector`
(`src/app/ace/app/camera_inspector.hpp:25-42`, `src/app/camera_inspector.cpp:143-253`)
is already registered as the `ViewType::Inspector` body and already reads the
selection, the scene, and cell health, and already dispatches a document edit
through `CanvasView::apply_edit`. The cost is (a) generalizing that body into a
full three-block property sheet keyed to `selection().primary()`; (b) two new
L1 `scene` write verbs (`set_cell_opacity`, `set_cell_visible`) minted as
exact mirrors of `scene::set_camera_resolution` (`src/scene/camera.cpp:565`);
(c) one pure L1 transform-readout helper that decomposes `arbc::Affine`; and
(d) the L1 unit tests + the ImGui Test Engine e2e. No new component, no new
DAG edge, no new external dependency — the estimate is dominated by the tests
and the Affine-decomposition math, not by new architecture.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cells.selection`** (`tasks/refinements/editor.cells/selection.md`,
  Done 2026-07-23) — *what there is to inspect.* The one project-level
  selection is `commands::Selection` (`src/commands/ace/commands/selection.hpp:18`)
  held on `AppState::selection()` (`src/commands/ace/commands/app_state.hpp:138`),
  read via `primary()` (`selection.hpp:28`, invalid `ObjectId{}` when empty)
  and `items()` (`:24`). The inspector reads `primary()` exactly as
  `camera_inspector.cpp:153` already does, and matches it against
  `scene::cells`/`scene::cameras` by `.id`. Critically, the canvas calls
  `Selection::prune(live)` every frame (D-selection-7 / constraint 10), so the
  inspector — named first among the consumers that guarantee protects
  (selection.md constraint 10) — can never resolve a dangling `ObjectId`; it
  may still observe an *empty* selection and must render an empty state.
  Selection carries no transform/appearance data — only an ordered set of
  `arbc::ObjectId` — so every value the sheet shows is resolved fresh from the
  scene, not from the selection.
- **`editor.dock.view_registry`** (`tasks/refinements/editor/view_registry.md`,
  Done 2026-07-17) — *how the panel becomes a view.* `ViewType::Inspector`
  is a fixed, singleton catalog entry (`src/dockmodel/ace/dockmodel/view_registry.hpp:19`);
  the panel supplies its real body through
  `views::register_view_body(dockmodel::ViewType::Inspector, body)`
  (`src/views/ace/views/views.hpp:117`), dispatched by `draw_view`
  (`:123`). The shell already performs this binding for `CameraInspector` at
  `src/app/shell.cpp:358-363` (teardown clears it at `:512`) — the six-line
  pattern this task extends.

**Pending (owned here):** two L1 `scene` verbs (`scene::set_cell_opacity`,
`scene::set_cell_visible`) plus one pure L1 transform-readout helper
(`interact::decompose_placement`), the generalized L4 `InspectorPanel` body,
its Catch2 unit tests, and its ImGui Test Engine e2e. Nothing downstream is
blocked on an unwritten predecessor — both dependencies are Done.

## What this task is

A single **dense, live property sheet** for the primary selected object,
drawn into the `ViewType::Inspector` view. For a **cell** it presents three
stacked blocks:

1. **Transform (units)** — a live readout decomposing the cell's placing-layer
   affine (`scene::Cell::placement`, `src/scene/ace/scene/cell.hpp:192`) into
   **position** (composition units), **placed size** (composition units,
   `placement.map_rect(content_bounds)`), **rotation** (degrees) and **shear**.
   Native/working resolution is shown as a plain readout beside it; the
   *resample-to-crisp* action and the health *badge* are **not** minted here —
   they are owned by `editor.cells.resolution` (`tasks/00-editor.tji:557-562`).
2. **Appearance & stacking** — a **straight-alpha opacity** slider and a
   **visibility** toggle (the two editable controls this task introduces),
   plus a **z-order position** readout ("layer *N* of *M*, back→front"). Blend
   mode is intentionally absent (no libarbc facet exists; D10 forbids a
   blend-space toggle). Interactive re-stacking (drag reorder) is owned by
   `editor.panels.layers` (`:595-599`).
3. **Source** — the **referenced / owned / painted** provenance (D11) read
   from `scene::CellDetail::source` (`cell.hpp:178-186`) and a **health**
   readout ("source-limited" vs "resample to crisp", §6 `:240-242`) reusing
   the shipped `interact::resolution_health` path.

For a **camera** the panel reuses the shipped camera block (output resolution
edit + "look through" + capture health, `camera_inspector.cpp:143-253`);
this task keeps that behavior intact and routes to it when
`selection().primary()` is a camera.

## Why it needs to be done

D7 makes cells and cameras *"one shape … one select tool"* and §6 promises a
per-object inspector as the place where placement, resolution, appearance and
provenance are read precisely — the numeric, always-visible complement to the
canvas's spatial gestures. The shipped Inspector body only covers camera
resolution; a selected **cell** currently has no property surface at all. This
leaf closes that gap and is the direct predecessor of `editor.panels.layers`
(which `depends !inspector`): the layers list reuses the same provenance and
z-order vocabulary this panel establishes, so the readout semantics must land
first.

## Inputs / context

**Governing design rows (normative — the constitution):**

- `docs/00-design.md:474` **D7** — one shape (affine + a resolution number),
  one select tool; *"Drag the extent, type the resolution; the two are always
  independent."* — the units decision.
- `docs/00-design.md:234-235` **§6 cell inspector** — *"position, placed size
  in composition units, rotation, (shear) — and separately the cell's native
  resolution with a resample control."*
- `docs/00-design.md:252-255` **§6 camera inspector** — output resolution
  (W×H + aspect presets), "look through", per-camera resolution-health.
- `docs/00-design.md:472` **D5** — px is a *health/effect readout only*, never
  a primary unit.
- `docs/00-design.md:473` **D6** — z-order is surfaced by the **list**
  (order/hierarchy) and the **overview** (patterned fills); reorder verbs live
  there, not in the inspector.
- `docs/00-design.md:475-476` **D8/D9** — placement editing never mutates
  stored resolution; resample is a separate explicit act.
- `docs/00-design.md:477` **D10** — users work in sRGB / straight-alpha /
  hex-HSV; compositing stays linear; **no v1 blend-space toggle**.
- `docs/00-design.md:478` **D11** — two asset axes (editable? · bytes where?);
  the classification is fixed, and the referenced-vs-painted *styling* is an
  explicit open item (`docs/00-design.md:315`).
- `docs/01-architecture.md:422` **A7** / `:428` **A13** — selection and shared
  panels are **project-level** (one `Document`, one `AppState`); the inspector
  is project-scoped, not per-canvas.
- `docs/01-architecture.md:431` **A16** — the Registry-driven, no-allowlist
  per-kind field-schema precedent (relevant if a future per-kind property
  block is added; **not** exercised by this leaf — see Decisions).
- `docs/01-architecture.md:433` **A18** — the UI thread reads writer-owned
  `Document` structure through a published snapshot or the shipped scene
  read-seam, never through writer-thread-only inspection APIs (see Decisions
  and Constraints).
- `docs/01-architecture.md:308-344` **§8** levelization DAG; `:346-373` **§9**
  the per-leaf DoD.

**Source seams:**

- Panel template: `src/app/ace/app/camera_inspector.hpp:25-42`;
  `src/app/camera_inspector.cpp:143-253` (draw), `:153` (selection read),
  `:177` (`###` stable-id idiom), `:216-218` (`apply_edit` dispatch),
  `:60-113` (`draw_cell_readout` provenance + health block),
  `:39-55` (`worst_cell_health`).
- Edit seam: `CanvasView::apply_edit` (`src/app/ace/app/canvas_view.hpp:103`)
  — posts the closure to the one document writer thread, blocks, wakes canvases
  (D-writer_thread-11).
- Body registration: `views::register_view_body` /
  `views::ViewBody` / `views::draw_view`
  (`src/views/ace/views/views.hpp:117,111,123`); shell binding
  `src/app/shell.cpp:358-363`.
- Scene reads: `scene::Cell` (`src/scene/ace/scene/cell.hpp:188-204`,
  `placement:192`, `content_bounds:200`, `detail:203`);
  `DetailSource` (`:169-173`), `CellDetail` (`:178-186`);
  `scene::cells` (`:214`, impl `src/scene/cell.cpp:273-320`, transform `:316`,
  bounds `:311`); `classify_detail` (`src/scene/cell.cpp:89-106`).
- Health: `interact::resolution_health` /
  `interact::ResolutionHealth` / `ResolutionVerdict`
  (`src/interact/ace/interact/interact.hpp:289,268-275,266`).
- Mutator template: `scene::set_camera_resolution`
  (`src/scene/ace/scene/camera.hpp:177`, impl `src/scene/camera.cpp:565` —
  reject-degenerate → `dynamic_cast` resolve → `document.transact(name)` →
  in-place mutate → `txn.commit()` → `bool`).
- Transform value type: `arbc::Affine { a,b,c,d,tx,ty }` with `map_rect` and
  `identity` (`arbc/base/transform.hpp`, consumed via FetchContent).
- libarbc write facets (behind the new scene verbs):
  `arbc::Model::Transaction::set_opacity(layer, opacity)` and `set_visible`
  (`arbc/model/model.hpp`), `LayerRecord::opacity` and the `k_layer_visible`
  flag (`arbc/model/records.hpp`), opened via `arbc::Document::transact`
  (`arbc/runtime/document.hpp`). **There is no blend-mode facet in libarbc.**

## Constraints / requirements

1. **Units are exactly D7/§6.** Transform values are shown in **composition
   units** (position, placed size) and **degrees** (rotation, with the shear
   angle beside it); native/working resolution is shown as a **pixel count**
   (W×H). "px" appears **only** in the resolution/health readout, never as the
   unit for placement (D5). The transform block **must not** offer any control
   that changes stored resolution (D8/D9).
2. **The transform block is a readout, not a numeric editor.** Placement
   editing is a *drag* gesture owned by `editor/gizmo`
   (`tasks/00-editor.tji:548`, *"Drag the extent"* per D7); the inspector shows
   the decomposed values live and does not mint a second placement-write path.
   (Rationale under Decisions.)
3. **Only two novel edits: opacity and visibility.** Both route through new L1
   `scene` verbs (`set_cell_opacity`, `set_cell_visible`) dispatched via
   `CanvasView::apply_edit` — never a direct `arbc::Document` mutation from the
   L4 body, and never a second edit-funnel. Each verb rejects a non-cell /
   unresolvable id as a `false`-returning no-op (the `set_camera_resolution`
   mould) and keeps the object's `ObjectId`, so the shared selection and
   `undo` hold on the same object across the edit. Opacity is **straight-alpha**
   (D10). This layer/compositing opacity is distinct from the paint-color alpha
   owned by `editor.panels.color` (`:607-611`) — see Decisions.
4. **Z-order is a readout only.** The inspector shows the cell's position in
   the composition's ordered membership (its index in the already-ordered
   `scene::cells` list, with the total count); interactive reorder is owned by
   `editor.panels.layers` (D6). No `reorder`/bring-forward control ships here.
5. **No blend control.** libarbc has no blend-mode facet and D10 forbids a
   user blend-space toggle; the appearance block shows opacity + visibility
   only. The `.tji` note's "blend" is not backed by any decision or library
   facet (Decisions; a future upstream facet is a parking-lot item, not a WBS
   leaf).
6. **Provenance and health are read, not recomputed.** Provenance comes from
   the generic-facet classification `scene::CellDetail::source` (never a kind
   string, `classify_detail` `src/scene/cell.cpp:89-106`); health reuses
   `interact::resolution_health`. The resample *action* and the health *badge
   styling* are `editor.cells.resolution`'s, not this leaf's.
7. **Reads use the shipped scene read-seam (A18).** The panel reads
   `scene::cells`/`scene::cameras`/`interact::resolution_health` on the UI
   thread, identically to the shipped `CameraInspector` (`camera_inspector.cpp:144-145`)
   — it does **not** introduce a new `Document`-inspection path and does not
   call any writer-thread-only libarbc API off the writer thread (Decisions).
8. **Levelization (§8) holds with no new edge.** New L1 write verbs live in
   `scene` (already depends on libarbc, like `set_camera_resolution`); the pure
   transform-readout helper lives in `interact` (already depends on `scene` +
   `arbc::Affine`, as `PickTarget::placement` and `selected_extent` already
   do); the ImGui draw code stays in the L4 `app` body (which already sees
   ImGui), reached through the L3 `views::register_view_body` seam. The L1 core
   gains **no** ImGui/GL/SDL include. `scripts/check_levels.py` is not edited.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.**
  No new component and no `scripts/check_levels.py` edit. `src/scene/`
  (`cell.hpp`/`cell.cpp`) gains two verbs that include only libarbc + existing
  scene headers; `src/interact/` gains a pure decomposition helper that
  includes only `arbc::Affine`/`arbc::Rect` + existing pick/interact headers;
  `src/app/` (the L4 `InspectorPanel` body) is the only file that gains
  ImGui-facing code. Asserted by inspection and by the lint.
- **L1 logic — Catch2 unit** (`tests/inspector_test.cpp`, new file appended to
  the `ace_tests` source list in the root `CMakeLists.txt` near `:254`;
  naming follows `tests/selection_test.cpp` / `tests/camera_manip_test.cpp`,
  i.e. `TEST_CASE("inspector: …")`):
  - **Transform decomposition (Constraint 1):** `decompose_placement` over an
    identity, a pure translation, a rotation, an anisotropic scale, a sheared
    affine, and a rotated+sheared+translated composite recovers position
    (`tx`,`ty`), placed size (via `map_rect` over a known `content_bounds`),
    rotation in degrees, and a shear value that is 0 for a pure
    rotation+scale — pinned against hand-computed expectations. Degenerate
    (zero-`det`) and non-finite affines yield a defined, NaN-free readout.
  - **Z-order position (Constraint 4):** for a three-cell document the
    reported (index, count) matches membership order, and is stable under a
    rename (identity preserved).
  - **`set_cell_opacity` (Constraint 3):** clamps/accepts a straight-alpha
    value, writes `LayerRecord::opacity`, is one transaction / one journal
    entry, keeps the `ObjectId`, and `undo` restores the prior opacity on the
    same object; a non-cell / unresolvable id returns `false` and mutates
    nothing (mould: `set_camera_resolution` no-op-returning-false,
    `src/scene/camera.cpp:565`).
  - **`set_cell_visible` (Constraint 3):** toggles the `k_layer_visible` flag,
    one entry, undoable, same-object; unresolvable id is a `false` no-op.
  - **Provenance readout (Constraint 6, D11):** a painted-raster, a
    referenced-image, and a resolution-independent cell classify to the
    correct `DetailSource` and produce the correct provenance + health
    string, driven through the same `classify_detail`/`resolution_health`
    the panel calls (no kind-string branch).
- **Rendered output — golden N/A (justified).** The panel composes ImGui
  chrome + text over `AppState`, not a libarbc `Document` composition, so
  there is no `render_offline` image to pin (the `view_registry` precedent,
  view_registry.md). Every displayed value is asserted through the L1 units
  above and the e2e below; a non-byte-exact screenshot baseline is captured in
  the e2e for signal only.
- **UI e2e — ImGui Test Engine** (`tests/inspector_e2e_test.cpp`, new file
  appended to the `ace_shell_test` list in the root `CMakeLists.txt` near
  `:313-342`, offscreen software-GL; modelled on
  `tests/camera_manip_e2e_test.cpp:298-309`, reusing its boot-the-real-shell +
  `register_view_body(ViewType::Inspector, …)` rig and driving widgets by ref
  path under the Inspector window). Interactive widgets carry stable `###`
  ids (`camera_inspector.cpp:177` idiom):
  - **Cell selected → three blocks render:** selecting a cell shows the
    transform readout, the appearance controls, and the source/health block;
    the transform values match the placed cell.
  - **Opacity edit round-trips (Constraint 3):** dragging the opacity slider
    (`ItemInputValue`) then asserting `AppState`/scene shows the new
    `LayerRecord::opacity`; `undo` restores it — one press.
  - **Visibility toggle round-trips:** clicking the visibility control flips
    `k_layer_visible`; `undo` restores it — one press.
  - **Empty selection → empty state:** with nothing selected the panel shows a
    defined empty state and no stale values (exercising the `prune` guarantee).
  - **Camera selected → shipped camera block still works:** the existing
    resolution edit + look-through path is unregressed (re-drives the
    `camera_manip_e2e` resolution assertions through the generalized body).
- **Threading (ASan/TSan).** One case appended to `tests/canvas_host_test.cpp`
  (over `default_interactive_pool_config()`): the UI thread runs the panel's
  read path (`scene::cells` + `decompose_placement` + `resolution_health`) and
  an `apply_edit(set_cell_opacity)` concurrent with a live render walk, TSan-
  clean — mirroring the selection leaf's UI-thread-read case. No new lane, no
  new suppression.
- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed
  lines; clang-format + build clean. Tests ship with the task.
- **Deferred WBS work. None.** Every out-of-scope neighbor is already a
  scheduled leaf and this refinement cites its owner, so nothing new is
  registered:
  - Interactive z-order reorder + referenced-vs-painted list styling →
    `editor.panels.layers` (`tasks/00-editor.tji:595-599`).
  - Resample-to-crisp action + health *badge* → `editor.cells.resolution`
    (`:557-562`).
  - On-canvas / numeric placement editing → `editor/gizmo` (`:548`).
  - Paint-color picker + its straight-alpha → `editor.panels.color`
    (`:607-611`).
  Two non-WBS items (a possible upstream arbc blend facet; the A18 live-read
  vs snapshot consistency question) are surfaced in the return summary for the
  parking lot — neither is agent-implementable here, so neither is a WBS leaf.

## Decisions

- **D-inspector-1 — The transform block is a live *readout*; placement editing
  stays on the canvas gizmo.**
  The sheet decomposes and displays position/size/rotation/shear but offers no
  numeric write for them.
  *Rationale:* D7 (`docs/00-design.md:474`) is explicit — *"Drag the extent,
  type the resolution"* — placement is a drag gesture and resolution is the
  typed field. `editor/gizmo` (`tasks/00-editor.tji:548`) already owns the
  placement-write path; minting a second numeric placement writer here would
  fork the transform-edit funnel and race the gizmo's verb for no decided
  benefit.
  *Alternative rejected:* editable numeric transform fields, because a "dense
  property sheet" conventionally allows precise entry. Deferred as an
  enhancement, not shipped: D7 leans drag-primary and the two writers would
  need to share one verb the gizmo has not yet published. If precise numeric
  placement is later wanted it is a small addition on top of this readout, not
  a redesign. **No doc delta required.**
- **D-inspector-2 — Opacity and visibility are the only novel edits, via new
  L1 `scene` verbs dispatched through `apply_edit`.**
  `scene::set_cell_opacity` and `scene::set_cell_visible` are minted as exact
  mirrors of `scene::set_camera_resolution` (`src/scene/camera.cpp:565`): one
  `document.transact()` over `Model::Transaction::set_opacity`/`set_visible`,
  in-place, `bool`-returning, undoable.
  *Rationale:* opacity/visibility are the only editable appearance facets
  libarbc exposes (`LayerRecord::opacity`, `k_layer_visible`) that no other
  scheduled leaf owns; routing them through a scene verb + `apply_edit` keeps
  the one writer-thread funnel (D-writer_thread-11) and the L1-testable seam
  (§9). Straight-alpha satisfies D10.
  *Alternative rejected:* mutate `arbc::Document` directly from the L4 body.
  That bypasses the writer funnel and puts untestable document logic in L4.
  **No doc delta required** (extends the shipped `scene` mutator family).
- **D-inspector-3 — Layer/compositing opacity here is distinct from the
  paint-color alpha owned by `editor.panels.color`.**
  The inspector's opacity slider edits `LayerRecord::opacity` (how strongly the
  whole cell composites); the color panel's straight-alpha (`:607-611`) is the
  alpha of the *color you paint with*.
  *Rationale:* they target different records and must not be conflated; D10
  makes both straight-alpha, which is the only thing they share. Staking this
  boundary now prevents the color leaf and this leaf from each thinking they
  own "the opacity slider". **No doc delta required.**
- **D-inspector-4 — Z-order is a readout; interactive reorder is the layers
  list's.**
  The sheet shows "layer *N* of *M*"; no reorder control ships.
  *Rationale:* D6 (`docs/00-design.md:473`) assigns z-order *surfacing and
  reorder* to the **list** and the **overview**; `editor.panels.layers`
  (`tasks/00-editor.tji:595-599`, *"reorder = z-order"*) is the scheduled
  owner and `depends !inspector`. A second reorder surface would duplicate it
  and split the ordering vocabulary the two panels must share. **No doc delta
  required** (respects D6).
- **D-inspector-5 — No blend control; blend is a documented gap, not a WBS
  leaf.**
  The appearance block shows opacity + visibility only.
  *Rationale:* D10 (`docs/00-design.md:477`) forbids a v1 blend-space toggle,
  and libarbc has no per-layer blend-mode facet at all (grep of the pinned
  `arbc/model` records/model finds none; the compositor is a fixed
  source-over). The `.tji` note's "blend" is therefore unbacked by both the
  constitution and the library. A per-layer blend facet would be an **upstream
  arbc** change plus a v1-scope reversal of D10 — human/upstream judgment, not
  agent-implementable here, so it goes to the parking lot, not the WBS.
  **No doc delta required** (D10 already settles the non-goal).
- **D-inspector-6 — Reads use the shipped UI-thread scene read-seam (A18), not
  a new snapshot.**
  The panel reads `scene::cells`/`scene::cameras`/`resolution_health` on the UI
  thread exactly as the shipped `CameraInspector` does
  (`camera_inspector.cpp:144-145`), covered by a TSan case.
  *Rationale:* A18 (`docs/01-architecture.md:433`) mandates that
  *Document-structure* reads go through a writer-built immutable snapshot; the
  shipped scene read-seam is the established path all current panels use and
  the selection leaf already TSan-covers UI-thread scene walks. Introducing an
  inspector-only `Document` snapshot would fork the threading model mid-panel
  for one leaf. If a Document-read snapshot is later mandated, *all* panels
  (History already has one) migrate together under the threading owner — that
  cross-panel consistency call is a parking-lot item, not a per-leaf "audit"
  task.
  *Alternative rejected:* a bespoke inspector snapshot. Premature and
  divergent; the shipped seam + TSan is the consistent, cheaper posture.
  **No doc delta required.**

## Open questions

(none — all decided.) Two items are surfaced to the parking lot in the return
summary rather than encoded as WBS leaves, because neither is
agent-implementable here: (a) whether libarbc should grow a per-layer blend
facet (an upstream + D10-scope call); (b) whether all panels should migrate
their `Document`-structure reads onto A18 published snapshots (a cross-panel
architecture-consistency call owned by the threading work, not this leaf).

## Status

**Done** — 2026-07-29.

- `src/app/ace/app/inspector_panel.hpp`, `src/app/inspector_panel.cpp`: new `InspectorPanel` body registered as `ViewType::Inspector`; three cell blocks (transform readout, opacity/visibility + z-order, provenance + health) and the intact camera block.
- `src/scene/ace/scene/cell.hpp`, `src/scene/cell.cpp`: two new L1 scene verbs `set_cell_opacity` and `set_cell_visible`; `z_order_position` and `describe_detail_source` helpers; `opacity`/`visible` fields on `Cell`.
- `src/interact/ace/interact/interact.hpp`, `src/interact/interact.cpp`: pure L1 `decompose_placement` helper decomposing `arbc::Affine` into position, placed size, rotation (degrees), and shear.
- `src/app/shell.cpp`: registered `InspectorPanel` alongside the existing camera inspector binding.
- `tests/inspector_test.cpp`: 8 Catch2 `inspector:` cases — decomposition (identity/translation/rotation/scale/shear/composite), z-order, `set_cell_opacity` (clamp/undo/no-op), `set_cell_visible` (toggle/undo/no-op), provenance (painted/referenced/resolution-independent).
- `tests/inspector_e2e_test.cpp`: ImGui Test Engine e2e — cell three-block render, opacity/visibility round-trip + undo, empty-selection state, camera block unregressed.
- `tests/canvas_host_test.cpp`: TSan case for UI-thread `scene::cells` + `decompose_placement` + `resolution_health` concurrent with `apply_edit(set_cell_opacity)`.
- `CMakeLists.txt`: added `inspector_test.cpp` and `inspector_e2e_test.cpp` to test target lists.
