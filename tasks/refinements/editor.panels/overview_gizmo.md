# editor.panels.overview_gizmo — full scale/rotate/shear gizmo on schematic overview boxes

## TaskJuggler entry

- **Task:** `editor.panels.overview_gizmo` (in `tasks/00-editor.tji`, under `editor.panels`).
- **Effort:** 2d.
- **Depends:** `!overview` (`editor.panels.overview`), `editor.cells.gizmo`.
- **Note:** "Full 8-handle scale/rotate/shear transform gizmo on schematic boxes in the
  overview, reusing the shipped `interact` cell-gizmo handle math (`editor.cells.gizmo`) fed
  through the overview transform. §6 makes the overview the coarse-arrangement surface (move +
  z-order shipped in `editor.panels.overview`); the full gizmo-in-overview is real, separable
  draw+dispatch work. Source-of-debt: `tasks/refinements/editor/overview.md`. Design:
  `docs/00-design.md` D7/D8/§6."
- **Back-link:** `tasks/00-editor.tji:632-637`.
- **Downstream dependents:** none in the current WBS — this is the leaf that completes the
  §6 promise of "the same gizmos and verbs on the schematic boxes," turning the overview from a
  move + z-order surface into the full coarse-arrangement surface.

## Effort estimate

2d. There is **zero new L1 math**: every transform verb, the handle hit-test, the placed-quad
anchor, and the snapping engine already ship as pure L1 `interact` from `editor.cells.gizmo`
(D-gizmo-1: `arbc::Affine` in/out, no ImGui). The `OverviewXform` composition↔panel mapping and
the `commands::transform_cells_command` commit funnel already ship from `editor.panels.overview`.
This leaf is the L4 draw + dispatch that bolts the two shipped seams together: draw the eight
handles anchored to `interact::placed_quad` mapped through the overview transform, hit-test the
grabbed handle through `interact::hit_cell`, drive the matching composer during the drag, and
commit one transaction on release. Most of the two days is the ImGui Test Engine e2e that drives
each handle and the Catch2 units that pin the panel↔composition coordinate contract.

## Inherited dependencies

**Settled (both predecessors are Done, consumed as-is):**

- **`editor.cells.gizmo`** (Done 2026-07-29, `tasks/refinements/editor/gizmo.md`) — ships the entire
  pure-L1 handle vocabulary this leaf reuses verbatim:
  - **`interact::CellHandle`** — the 8-handle enum (`None, Body, Pivot, ScaleTopLeft/TopRight/
    BottomLeft/BottomRight, ScaleLeft/Right/Top/Bottom, Rotate`), `src/interact/ace/interact/interact.hpp:335-348`;
    classifiers `is_cell_corner`/`is_cell_edge`/`is_cell_scale_handle` (`interact.hpp:351-355`,
    `src/interact/interact.cpp:597,609,621`).
  - **The transform composers** — `move_cell` (`interact.hpp:373`, `interact.cpp:634`),
    `scale_cell(placement, extent, handle, pointer, pivot, free_distort, about_pivot)`
    (`interact.hpp:383-384`, `interact.cpp:641`), `rotate_cell` (`interact.hpp:390`, `interact.cpp:687`),
    `shear_cell` (`interact.hpp:398-399`, `interact.cpp:700`); `cell_pivot` (`interact.hpp:360`,
    `interact.cpp:625`), `drag_angle` (`interact.hpp:365`, `interact.cpp:629`).
  - **The gizmo hit-test** — `interact::hit_cell(target, pivot, point, edge_tol, corner_tol)`
    → `CellHandle`, anchored to the exact placed quad (never AABB, D-gizmo-7), `src/interact/ace/interact/pick.hpp:195`,
    `src/interact/pick.cpp:265`. Tolerances are in **composition units** (the caller converts
    screen px through the view scale). `interact::placed_quad(target)` → the four rotated corners,
    `pick.hpp:80`, `pick.cpp:122`.
  - **The snapping engine** — `interact::snap_placement(candidate, moving_extent, others, tol,
    bypass, grid_x, grid_y)` → `SnapResult{placement, guides, snapped_x/y}`, `pick.hpp:243`,
    `pick.cpp:407`; `SnapGuide`/`SnapResult`/`GridLines` (`pick.hpp:200,206,224`).
  - **The group path** — `interact::group_transform` (`interact.hpp:419`) + `interact::selected_extent`
    (`pick.hpp:136`), and the L4 template `CanvasView::draw_group_gizmo` (`src/app/canvas_view.cpp:935`,
    committing via `transform_cells_command` at `:1029`).
  - **The L4 dispatch template** — `CanvasView::draw_cell_gizmos(view_id, Presenter&, …)`,
    `src/app/ace/app/canvas_view.hpp:346`, `src/app/canvas_view.cpp:639`: per-frame `hit_cell` →
    grab stores start state → drag runs the matching composer + `snap_placement` and previews the
    returned affine (no journal) → release commits **one** transaction. Gizmo session-state fields
    on `CanvasView::Presenter` (`canvas_view.hpp:283-293`): `gizmo_cell`, `gizmo_cell_layer`,
    `gizmo_cell_handle`, `gizmo_cell_start`, `gizmo_cell_grab_comp`, `gizmo_cell_extent`,
    `gizmo_cell_pivot`.

- **`editor.panels.overview`** (Done 2026-07-30, `tasks/refinements/editor/overview.md`) — ships the
  panel this leaf extends and the two seams the gizmo threads through:
  - **`ace::app::OverviewXform`** — the composition→panel-pixels mapping, `src/app/overview_panel.cpp:31-51`
    (anon namespace): `to_screen(Vec2)` (`:35`), `to_comp(ImVec2)` → `std::optional<Vec2>` (nullopt-guarded
    inverse, `:40`), `scale()` = uniform px-per-composition-unit (`:49`). Built each frame at
    `overview_panel.cpp:279` from `interact::fit_region` (whole-composition fit; the overview keeps no
    free pan/zoom of its own). Pointer-in-composition is `x.to_comp(GetIO().MousePos)` (`:295`). The
    **screen-constant tolerance** convention this leaf reuses is already shipped here:
    `comp_per_px = 1.0/x.scale()`, `edge_tol = 4*comp_per_px`, `corner_tol = 6*comp_per_px`
    (`overview_panel.cpp:286-288`) — the same tolerances `interact::hit_cell` wants.
  - **The move + commit funnel** — the per-cell hit item is a `###ov_cell_<id>` `InvisibleButton` over the
    placed box's screen AABB (`box_aabb`, `overview_panel.cpp:74`); its drag arms `OverviewPanel::MoveGesture`
    (`src/app/ace/app/overview_panel.hpp:50`, instance `move_` at `:58`), previews UI-only, and on release
    builds `interact::move_cell(start, dx, dy)` → **one** `commands::transform_cells_command` committed via
    `CanvasView::apply_edit` (`overview_panel.cpp:359-403,506-511`), kind-agnostic over cells and camera
    frames, placement-only (D8). Overview drag is **move-only today** — the gizmo replaces the
    `move_cell` branch with the scale/rotate/shear composers on the same funnel.
  - **The scoped pick feed** — the precise target under the pointer resolves via
    `interact::pick(targets, *mouse_comp, edge_tol, corner_tol)` (`overview_panel.cpp:308`) over the
    `interact::pick_targets(document, registry, entered_composition)` set (`overview_panel.cpp:164,307,327,396`),
    so scope confinement + camera-lock-while-entered (D29) already govern which boxes are reachable.
  - **Stable e2e ids** — cell boxes `###ov_cell_<id>`, camera frames `###ov_cam_<id>`, viewport rect
    `###ov_viewport`, crumbs `###ov_crumb_<id>`.

**Pending (owned here):**

- The eight-handle gizmo **draw** over the selected overview cell box(es), anchored to
  `interact::placed_quad` and mapped through `OverviewXform::to_screen`.
- The handle **hit-test + drag-dispatch** in `OverviewPanel`: threading `hit_cell` into the existing
  `###ov_cell_<id>` dispatch (`overview_panel.cpp:359-403`) ahead of the plain move → composer → optional
  `snap_placement` → preview → one-transaction commit through the existing funnel.
- A new `OverviewPanel::GizmoGesture` **drag session-state** struct (a sibling of the shipped
  `MoveGesture`/`MarqueeGesture`, `overview_panel.hpp:50-68`), modeled on the `CanvasView::Presenter`
  gizmo fields (`canvas_view.hpp:283-293`) — UI-thread-only, non-journaled.
- The **group** gizmo path for multi-selection (reusing `interact::group_transform` + `selected_extent`).
- The tests.

## What this task is

`editor.panels.overview` shipped the overview as a **move + z-order** surface: dragging a schematic
box repositions it, bring-forward/send-back reorders it, and the occlusion flips live. It explicitly
deferred the **full transform gizmo** — the eight scale/rotate/shear handles — to this leaf
(`tasks/refinements/editor/overview.md`, D-overview-5; registered as `editor.panels.overview_gizmo`).

This task realizes §6:265-269 — "the same gizmos and verbs on the schematic boxes … ideal for
*coarse* arrangement of many cells/cameras and for editing z-order, while the canvas is for *precise*
placement." It draws the shipped **cell gizmo** (D7/D8/§6:231-244 — 8 handles: corners
proportional-by-default / Shift free-distort, edges 1D stretch, corner-outside rotate with Shift 15°
snap, modifier+edge shear, Alt pivot) on the selected overview box, hit-tests and drives it entirely
through the already-shipped L1 `interact` verbs, and commits the resulting `arbc::Affine` through the
overview's existing `transform_cells_command` funnel. Because the boxes are already picked through the
scoped `interact::pick_targets` set, scope confinement (D29) and camera-lock-while-entered come free.

It **does not** ship: any new transform math (all reused from `editor.cells.gizmo`); painting
(§6 keeps painting canvas-only); the full **camera** gizmo (aspect-locked recrop / resolution-hold /
dutch rotation, §6:246-251) on overview camera frames — cameras keep their shipped overview move +
look-through, and precise camera recrop stays canvas-only (see D-overview_gizmo-4).

## Why it needs to be done

§6 (D7/D8) designates the overview as the **coarse-arrangement surface** using *the same gizmos and
verbs* as the canvas. The overview leaf shipped move and z-order but not the handles, so today a user
can slide a box in the overview but cannot scale, rotate, or shear it there — they must switch to the
canvas for any non-translation transform, which defeats "arrange many cells at a glance." This leaf is
the last piece of the §6 overview story, and it is pure draw+dispatch precisely because
`editor.cells.gizmo` was built to make its handle math a reusable L1 seam (D-gizmo-1): the whole point
of that refactor was that a second surface — this one — could reuse it "with no forking."

## Inputs / context

**Governing design rows:**

- **D7 — Manipulation model** (`docs/00-design.md:474`): cells and cameras share one shape (affine +
  resolution) and one select tool; kind-agnostic manipulation.
- **D8 — Cell scale ≠ resample** (`docs/00-design.md:475`): "Handle-drag changes **placement (affine)**,
  never resolution — non-destructive. Corners proportional-by-default (Shift free), edges 1D." The
  load-bearing invariant this leaf must preserve.
- **§6 — Direct manipulation** (`docs/00-design.md:208-269`): the cell-gizmo handle vocabulary
  (`:231-244`), and — decisive for scope — **§6:265-269**: "In the editable overview (§5): the same
  gizmos and verbs on the schematic boxes — ideal for *coarse* arrangement … while the canvas is for
  *precise* placement and painting. Both edit the same affines live; painting stays canvas-only."
- **D29 — Scoped editing** (`docs/00-design.md:496`): the single pick assembly adapter
  (`interact::pick_targets`) drops out-of-scope targets, so "click-select, marquee, select-all,
  **gizmo-drag**, and frame-selection are confined at once"; cameras are not pickable while entered.
- **§5 / D6** (`docs/00-design.md:160-206,473`): the overview is an editable layout surface; a rotated
  cell's hatch tilts with it, so the gizmo must anchor to the rotated placed quad, not an AABB.

**Governing architecture rows:**

- **§8 — Components & levelization** (`docs/01-architecture.md:310-346`): `interact` is **L1**
  (UI-agnostic, no ImGui/GL/SDL); `app` is **L4**. This leaf adds no component and no DAG edge
  (see Constraints).
- **§9 — Testing & definition of done** (`docs/01-architecture.md:348-375`): L1 logic → Catch2;
  rendered libarbc output → golden; UI behavior → ImGui Test Engine e2e; threads → ASan/TSan;
  `check_levels` clean; ≥90% diff coverage.

**Editor seams this leaf extends** (all cited above under Inherited dependencies): the L1 verbs at
`src/interact/ace/interact/interact.hpp:335-419` and `src/interact/ace/interact/pick.hpp:80,136,195,243`;
the L4 template `CanvasView::draw_cell_gizmos`/`draw_group_gizmo` (`src/app/canvas_view.cpp:639,935`)
and `Presenter` gizmo state (`canvas_view.hpp:283-293`); the overview seams `OverviewXform`
(`overview_panel.cpp:31-51`), the `transform_cells_command` commit (`overview_panel.cpp:508`), and the
scoped `pick_targets` feed (`overview_panel.cpp:164,307,327,396`). The two L4 hook sites: the **dispatch**
hook is the per-cell `###ov_cell_<id>` block in PASS 1 (`overview_panel.cpp:359-403`, where `MoveGesture`
is armed and `move_cell` is called at `:391-393`); the **overlay-draw** hook is in PASS 2 after the dotted
borders (`overview_panel.cpp:565-591`), before the camera/viewport chrome (`:594-624`).

**Predecessor refinements:**

- `tasks/refinements/editor/gizmo.md` — ships all handle math as pure L1; its D-gizmo-1 (L1 seam),
  D-gizmo-7 (anchor to `placed_quad`, never AABB), and its test shape are the mould this leaf mirrors.
  Its D-gizmo-6 (leave the camera frame chrome as-is) is the precedent for D-overview_gizmo-4 below.
- `tasks/refinements/editor/overview.md` — ships the panel, the `OverviewXform` seam, the move +
  z-order commit funnel, and the scoped pick feed; its D-overview-5 deferred exactly this leaf.
- `tasks/refinements/editor.panels/hatch_swatch.md` — the sibling that reuses a shipped L1 `interact`
  seam "with no forking" and keeps all draw in L4; the same section skeleton, golden-N/A justification,
  and levelization-first acceptance style this leaf follows.

**Test rigs:**

- `tests/overview_gizmo_test.cpp` (Catch2, appended to `ace_tests` near `tests/CMakeLists.txt`),
  case names `TEST_CASE("overview_gizmo: …")`.
- `tests/overview_gizmo_e2e_test.cpp` (ImGui Test Engine, appended to `ace_shell_test`),
  `TEST_CASE("overview_gizmo e2e: …")` wrapping `IM_REGISTER_TEST(engine, "overview_gizmo", …)`, driving
  the `###ov_cell_<id>` boxes by raw mouse position (recipe from `gizmo_e2e_test.cpp:186,233` /
  `overview_e2e_test.cpp:168,262`).
- A TSan case appended to `tests/canvas_host_test.cpp` (mirroring the `editor.panels.overview` and
  `editor.cells.gizmo` anchors).

## Constraints / requirements

1. **Zero new L1 math — reuse the shipped verbs.** Every transform is produced by
   `interact::move_cell`/`scale_cell`/`rotate_cell`/`shear_cell`; every handle is resolved by
   `interact::hit_cell`; every anchor is `interact::placed_quad`; snapping is `interact::snap_placement`.
   No transform arithmetic lives in `overview_panel.cpp`. (D-overview_gizmo-1.)
2. **Composition-space contract.** The L1 verbs operate in composition coordinates. The panel feeds
   them the pointer via `OverviewXform::to_comp` and the placed extent from the same `PickTarget` the
   box is drawn from; it maps the returned quad/handles back to screen via `OverviewXform::to_screen`.
   Never feed panel-pixel coordinates into a composition-space verb (correctness-critical, pinned by
   test).
3. **Screen-constant grab tolerance.** `hit_cell` takes composition-unit tolerances; the panel reuses the
   overview's already-shipped `edge_tol`/`corner_tol` derived from `OverviewXform::scale()`
   (`comp_per_px = 1/scale()`, `overview_panel.cpp:286-288`), so handles are grabbable at a constant pixel
   size regardless of the whole-composition fit zoom — the same screen-constant convention the canvas gizmo
   uses through its view scale. (D-overview_gizmo-3.)
4. **Placement-only, non-destructive (D8).** A handle-drag commits a placement `arbc::Affine` and never
   touches resolution or stored pixels; the committed `transform_cells_command` carries only the affine.
5. **One transaction per gesture.** The drag previews as UI-only session state (no journal, document
   untouched); release commits **exactly one** `transform_cells_command` via `CanvasView::apply_edit`,
   undoable as a unit — the same discipline as the overview move and the canvas gizmo.
6. **Handles are draw-list marks; the grab is resolved by `interact::hit_cell`, not per-handle ImGui
   items.** The gizmo draws its eight handles + rotate ring as draw-list marks (via `OverviewXform::to_screen`
   on `placed_quad`) and resolves the grabbed handle by feeding the composition-space pointer to
   `interact::hit_cell` inside the existing `###ov_cell_<id>` dispatch — it does **not** mint an ImGui item
   per handle. Because the pointer is resolved over the `pick_targets(document, registry, entered_composition)`
   set the overview already builds, scope confinement + camera-lock-while-entered (D29) come free. The one
   L4 mechanical note: rotate/corner handles sit *outside* the box, so the `###ov_cell_<id>` `InvisibleButton`
   hit area (`box_aabb`) must be widened to the gizmo's handle envelope while the box is selected, or the
   outside-the-box handles are hit-tested in the background item — an implementation choice, not a design
   one. (D-overview_gizmo-5.)
7. **Cell boxes only; cameras keep shipped behavior.** The gizmo draws on cell boxes. Camera frames keep
   their shipped overview move + look-through; the full camera gizmo (aspect-locked recrop / dutch,
   §6:246-251) stays canvas-only. While a composition is entered, cameras are not pickable at all (D29),
   so no camera gizmo can appear there regardless. (D-overview_gizmo-4.)
8. **Multi-selection uses the group path.** With >1 object selected, draw one group gizmo around
   `interact::selected_extent` and drive `interact::group_transform`, committing through the same
   `transform_cells_command` funnel — mirroring `CanvasView::draw_group_gizmo`. (D-overview_gizmo-6.)
9. **Levelization (§8).** All handle/transform/snap math stays in L1 `interact` (already shipped); the
   new draw + hit-dispatch + session state is L4 `app` (`overview_panel.cpp`). No `#include <imgui.h>`
   (or GL/SDL) enters L1; the gizmo session-state fields are plain values on the panel's UI-thread-only
   presenter, never journaled (D15). **No new component, no new DAG edge**; `check_levels` stays clean.
10. **Session state is UI-thread-only.** The `OverviewPanel::GizmoGesture` drag state lives beside the
    shipped `MoveGesture`/`MarqueeGesture` (`overview_panel.hpp:50-68`), read/written only on the UI
    thread; the writer thread never touches it, so no new cross-thread shared mutable state is introduced.

## Acceptance criteria

The universal DoD (`docs/01-architecture.md` §9) instantiated for this leaf:

**Levelization — `check_levels` clean.** No new component, no new edge; L1 `interact` gains nothing
(all verbs already ship), and no ImGui/GL/SDL include enters L1. `scripts/check_levels.py`
(via `scripts/gate`) passes.

**L1 logic — Catch2 unit** (`tests/overview_gizmo_test.cpp`, appended to `ace_tests`):

- **`overview_gizmo: handle math is view-independent`** — for a box under an arbitrary composition
  affine, feeding a pointer to `hit_cell`/`scale_cell` via an `OverviewXform` fit scale yields the
  **same** `CellHandle` and the **same** resulting placement `arbc::Affine` as feeding the same
  composition-space pointer via a canvas view scale. Pins Constraint 2 — the overview reaches
  composition space correctly, and the reuse of the shipped L1 verbs is faithful (no forked math).
- **`overview_gizmo: grab tolerance is screen-constant`** — a fixed screen-px grab radius converted
  through `OverviewXform::scale()` picks the corner handle at two different overview fit zooms (Constraint 3);
  a composition-constant tolerance would miss the corner at the zoomed-out fit (the negative pin).
- **`overview_gizmo: handle-drag is placement-only (D8)`** — each composer (`move`/`scale` corner
  proportional + Shift free-distort + edge 1D / `rotate` Shift-15° snap / `shear`) driven through the
  overview seam produces an affine change only; the value committed to `transform_cells_command` carries
  placement, never resolution (Constraint 4).
- **`overview_gizmo: one transaction per gesture`** — a preview run mutates neither the document nor the
  journal; a single release yields exactly one journal entry that undo reverts (Constraint 5).
- **`overview_gizmo: scope confines the grab (D29)`** — with `entered_composition` set, the gizmo
  hit-test is offered only the scoped `pick_targets` set: a handle-position over an out-of-scope box
  returns `CellHandle::None`, and a camera returns `None` while entered (Constraint 6/7).
- **`overview_gizmo: group transform`** — a multi-selection maps to `selected_extent` + `group_transform`,
  applying one uniform transform to all members, committed as one transaction (Constraint 8).

**Rendered output — golden N/A (justified).** The overview box is a schematic draw-list mark, not a
libarbc `render_offline` composition, so there is no byte-exact kernel output to compare — the same
justification `editor.panels.overview` recorded (`overview_panel.cpp:53`). The **render-level** D8 proof (handle-scale
makes the cell softer, not cropped/re-gridded) is already pinned by `editor.cells.gizmo`'s goldens
`tests/goldens/gizmo_scale_64x64.rgba8` (single) and `tests/goldens/group_transform_scale_64x64.rgba8`
(group), which exercise the identical `transform_cells_command` / `set_layer_transform` placement path this
leaf commits through — so no new golden adds signal.

**UI e2e — ImGui Test Engine** (`tests/overview_gizmo_e2e_test.cpp`, appended to `ace_shell_test`,
driving `###ov_cell_<id>` by raw mouse position):

- **Eight handles drawn + each drives its transform:** selecting an overview box draws the 8-handle
  gizmo (screenshot baseline over a **rotated** box, proving `placed_quad` anchoring); corner-drag scales
  proportionally, Shift = free distort; edge-drag = 1D stretch; the rotate ring rotates (Shift snaps 15°);
  modifier+edge shears. Each asserts the resulting `scene`/document placement after release.
- **Occlusion flips live:** scaling a box to overlap another updates the hatch-fill occlusion in the same
  frame (screenshot baseline) — the coarse-arrangement payoff of §6.
- **Preview then commit:** mid-drag the document/journal are unchanged; release commits one entry; undo
  restores the pre-drag placement (Constraint 5).
- **Scope confinement:** while a composition is entered, out-of-scope boxes show no gizmo and cannot be
  grabbed; camera frames show no cell gizmo (Constraint 6/7, D29).
- **Group gizmo:** a multi-selection shows one gizmo around the union extent; a corner-drag scales all
  members about the group pivot (Constraint 8).

The e2e follows the overview e2e's inline-pool convention for its live-canvas scaffolding
(`WorkerPoolConfig{}`) to sidestep the pinned libarbc nested-render worker-detach race (parking lot,
`tasks/parking-lot.md` "arbc nested-render worker-detach race" — retitled at the 2026-08-07
triage, which re-verified the race survives the v0.4.1 pin and filed it upstream as arbc#29).

**Threading (ASan/TSan).** The gizmo session state is UI-thread-only (Constraint 10), so no new
cross-thread surface is added. A case appended to `tests/canvas_host_test.cpp` repeats overview gizmo
commits (`transform_cells_command`) against a live-rendering real-pool `CanvasHost`, mirroring the
`editor.panels.overview` and `editor.cells.gizmo` anchors; the e2e runs under the `clang-asan` offscreen
lane (§9.1) and must stay clean.

**Coverage.** ≥90% diff coverage on the changed lines (the handle draw, the hit-dispatch, the session
state, the group path), tests shipping in the same commit.

**Deferred WBS work:** none — the leaf is self-contained and completes the §6 overview-gizmo promise.
The full **camera** gizmo on overview frames (aspect-locked recrop / dutch) is deliberately out of scope
(D-overview_gizmo-4) and is an aesthetic/consistency call for the parking lot alongside `editor.cells.gizmo`'s
D-gizmo-6 camera-frame item — not a new WBS leaf.

## Decisions

- **D-overview_gizmo-1 — Zero new L1 math; reuse the shipped `interact` cell-gizmo verbs verbatim through
  the `OverviewXform` seam.** *Rationale:* the charter says so, and `editor.cells.gizmo` (D-gizmo-1) made
  every handle verb pure L1 `arbc::Affine`-in/out precisely so a second surface could reuse it "with no
  forking" — exactly the reuse bias the sibling `hatch_swatch` applied to `interact::overview_pattern`.
  *Alternative rejected:* an overview-specific handle-math path — rejected as a fork of shipped, tested
  L1 logic that would drift from the canvas gizmo.

- **D-overview_gizmo-2 — Commit through the overview's existing `commands::transform_cells_command`
  funnel, not the canvas gizmo's direct `set_layer_transform`.** *Rationale:* the overview move already
  commits via `transform_cells_command` (`overview_panel.cpp:508`), which is kind-agnostic and handles the
  single- and group-object cases uniformly; reusing it keeps a single commit path (one undo entry per
  gesture) in the panel. *Alternative rejected:* mirror the canvas gizmo's `set_layer_transform`
  (`canvas_view.cpp:790`) — rejected as a second commit path in the same panel with no behavioural benefit
  and a divergent undo granularity.

- **D-overview_gizmo-3 — Grab tolerances are screen-constant, converted to composition units via
  `OverviewXform::scale()`.** *Rationale:* handles must be grabbable at a fixed pixel radius regardless of
  the overview's whole-composition fit zoom; `hit_cell` takes composition-unit tolerances, so the panel
  divides the screen-px radius by `scale()` — the same conversion-through-view-scale the canvas gizmo does.
  *Alternative rejected:* composition-constant tolerance — rejected because small boxes in a zoomed-out
  overview would become ungrabbable.

- **D-overview_gizmo-4 — Cell boxes only; cameras keep their shipped overview move + look-through, and
  precise camera recrop stays canvas-only.** *Rationale:* §6:246-251 reserves the aspect-locked /
  resolution-holding / dutch camera gizmo for precise work, and §6:265-269 makes the overview the *coarse*
  surface; the charter scopes this leaf to "schematic overview boxes" reusing the *cell*-gizmo math, and
  `editor.cells.gizmo` likewise shipped the cell handles while leaving the camera frame chrome as-is
  (D-gizmo-6). D29 additionally makes cameras unpickable while entered. *Alternative rejected:* the full
  camera gizmo on overview frames — rejected as out of charter and §6 scope; if wanted later it is a small
  canvas-camera-gizmo reuse, and it belongs in the parking lot as an aesthetic call beside D-gizmo-6, never
  a self-perpetuating "revisit" WBS leaf.

- **D-overview_gizmo-5 — Handles are draw-list marks; the grab resolves via `interact::hit_cell` over the
  scoped pick set, not per-handle ImGui items.** *Rationale:* the overview already resolves the precise box
  under the pointer through `interact::pick` over the scoped `pick_targets` set (the deliberate
  click-through model the overview and `hatch_swatch` D-hatch_swatch-5 rely on), so extending that same
  composition-space pointer path to `hit_cell` is the natural seam — and scope confinement plus
  camera-lock-while-entered (D29) fall out of the same `pick_targets(document, registry, entered)` set for
  free. Drawing handles as draw-list marks (not eight ImGui items per box) matches how the overview already
  draws its box/border/viewport chrome. *Alternative rejected:* mint an `InvisibleButton` per handle —
  rejected (the identical rejection `hatch_swatch` D-hatch_swatch-5 made) because it forks the overview
  interaction model and adds N items per frame; the single existing `###ov_cell_<id>` button's hit area is
  merely widened to the handle envelope while selected.

- **D-overview_gizmo-6 — Multi-selection reuses the shipped `interact::group_transform` +
  `selected_extent` group path (mirroring `CanvasView::draw_group_gizmo`).** *Rationale:* it is the same
  `OverviewXform` seam and shipped L1 math, so the marginal cost is only the draw + dispatch, and it avoids
  a jarring "full gizmo for one object, move-only for many" inconsistency with the canvas. *Alternative
  rejected:* single-selection gizmo only — rejected as inconsistent with the shipped canvas group gizmo and
  cheap to include correctly.

**No doc delta.** §6:265-269 already promises "the same gizmos and verbs on the schematic boxes," and
D7/D8 already govern the handle vocabulary and the placement-only invariant; this leaf realizes those
promises without amending them. No new `D<n>`/`A<n>` row is required.

## Open questions

(none — all decided.) The exact handle-chrome weight/color reuses the canvas gizmo's existing palette;
it is minor visual polish (grouped with the overview's `§5:204-206` polish items in the parking lot), not
a design question, and does not gate the leaf.

## Status

**Done** — 2026-07-30.

- Full 8-handle scale/rotate/shear/move gizmo landed in `src/app/overview_panel.cpp` and `src/app/ace/app/overview_panel.hpp`, threading `interact::hit_cell` + `scale_cell`/`rotate_cell`/`shear_cell`/`move_cell` through the `OverviewXform` seam into the existing `transform_cells_command` commit funnel.
- `OverviewPanel::GizmoGesture` drag session-state added to `overview_panel.hpp` (UI-thread-only, non-journaled), mirroring `MoveGesture`/`MarqueeGesture`.
- Group gizmo path wired via `interact::selected_extent` + `interact::group_transform`, committing as one `transform_cells_command` per gesture.
- Group grab uses the existing `###ov_bg` scale handle; rotate ring excluded from background grab to preserve marquee start; `Pivot` hit remapped to `Body` for center-drag-to-move.
- Catch2 unit tests (6 cases) added in `tests/overview_gizmo_test.cpp`, covering: handle-math view-independence, screen-constant grab tolerance, placement-only (D8), one-transaction-per-gesture, scope-confines-grab (D29), group-transform.
- ImGui Test Engine e2e (1 case) added in `tests/overview_gizmo_e2e_test.cpp`: eight handles + rotated-box `placed_quad` anchor + preview/commit + scope + group.
- TSan anchor case appended to `tests/canvas_host_test.cpp`, mirroring existing `editor.panels.overview` and `editor.cells.gizmo` anchors.
- `CMakeLists.txt` updated to register the new test files.
