# editor.canvas.grid — Composition grid: display toggle + spacing setting + snap-to-grid wiring

## TaskJuggler entry

- **Task:** `editor.canvas.grid` (`tasks/00-editor.tji:389-394`, under
  `task canvas "Canvas & rendering"` at `:216`).
- **Effort:** `2d` (`:390`) · `allocate team` (`:391`).
- **Depends:** `editor.cells.gizmo` (`:392`), which is `complete 100`
  (`tasks/00-editor.tji:542`, Done 2026-07-29) — the dependency is satisfied.
- **Note (`.tji:393`):** "The composition grid `docs/00-design.md:260` names: a
  display toggle + spacing setting (local UI state), and feeding the grid lines
  into the gizmo's already-general snap_placement engine (`interact/pick.hpp`) so a
  placement snaps to grid. Deferred from `editor.cells.gizmo` because
  snap-to-an-invisible-grid with no spacing control is a non-affordance; the snap
  engine was built to consume grid lines the moment a grid exists. Source-of-debt:
  `tasks/refinements/editor/gizmo.md`. Design: `docs/00-design.md` D7/D8 §6."
- **Back-link:** the closer updates the `.tji` note to end
  `Refinement: tasks/refinements/editor/grid.md` and adds `complete 100`
  immediately after `allocate team` (`tasks/refinements/README.md:47-68`). **Do
  not** hand-edit the `.tji` here.
- **Downstream / milestone:** `editor.canvas.grid` is a leaf under the
  `editor.canvas` rollup, which the sole milestone `m9_editor` depends on
  (`tasks/99-milestones.tji:6-8`). It mints **no new WBS leaf** (Acceptance
  criteria), so the closer wires nothing new into a milestone; the rollup absorbs
  the completion.

## Effort estimate

**2 days.** This is the payoff wiring for a seam the predecessor built to be
consumed: `interact::snap_placement` **already** takes `grid_x`/`grid_y` line
spans (`src/interact/ace/interact/pick.hpp:221-226`), and both callers pass the
empty defaults today (`src/app/canvas_view.cpp:595-596`, `:807-808`). The three
shippable pieces are each small and land on existing seams:

- **L1 `interact` — pure grid-line generation.** A new `composition_grid_lines`
  helper beside `snap_placement` that turns a spacing + a composition-space region
  into the `xs`/`ys` line-coordinate lists `snap_placement` and the draw path both
  consume. Pure math over `arbc::Rect`, headless Catch2-testable — the bulk of the
  coverage. ~0.5d incl. units.
- **L4 `app` — per-pane state + draw + snap wiring.** Two `Presenter` session
  fields (`grid_show`, `grid_spacing`, `canvas_view.hpp` beside `look_through`
  `:197`); draw the grid as passive draw-list chrome in `draw_content` after the
  composition blit (`canvas_view.cpp:217`, scale-bar style `:254`); feed the same
  lines into the two `snap_placement` calls when the grid is shown. ~0.7d.
- **L4 `app` — a per-pane "Show grid" toggle + spacing input** overlay control
  beside `draw_camera_picker` (`canvas_view.cpp:334,337`), plus `grid_visible` /
  `grid_spacing` readback accessors for the e2e. ~0.3d.
- **Tests** — Catch2 units (generation + the snap-integration the predecessor could
  not test without a grid), an e2e, a screenshot baseline. ~0.5d.

**No new component, no new DAG edge, no new libarbc surface, no `check_levels`
edit, no doc delta** (§6:260 already charters the composition grid as a snap
target; D-gizmo-5 already built the engine grid-ready and named this leaf the
wiring task).

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cells.gizmo`** (`tasks/refinements/editor/gizmo.md`, Done 2026-07-29)
  — the direct predecessor. It ships **exactly the seam this leaf fills**:
  - **`interact::snap_placement(const arbc::Affine& candidate, const arbc::Rect&
    moving_extent, std::span<const PickTarget> others, double tol, bool bypass =
    false, std::span<const double> grid_x = {}, std::span<const double> grid_y =
    {})`** (`src/interact/ace/interact/pick.hpp:224-226`, impl
    `src/interact/pick.cpp:376`) — the object-relative snap engine, **built
    grid-ready**: the `grid_x`/`grid_y` spans are folded into the candidate line
    set (`pick.cpp:411-416`, `for (double gx : grid_x) xlines.push_back({gx, m.y0,
    m.y1});`) beside cell/camera edges & centers. Its header comment names this
    leaf: *"`grid_x`/`grid_y` are optional composition-space grid lines folded into
    the target set — no caller populates them yet (grid display is
    `editor.canvas.grid`, D-gizmo-5)"* (`pick.hpp:221-222`).
  - **`SnapResult{arbc::Affine placement; std::vector<SnapGuide> guides; bool
    snapped_x, snapped_y;}`** (`pick.hpp:206-211`) and **`SnapGuide{arbc::Vec2 a,
    b;}`** (`:200-203`) — the return shape; a grid snap emits its guide exactly as
    an object snap does (the internal `SnapLine{pos, omin, omax}` carries the
    guide's orthogonal span, `pick.cpp:346-352,411-416`).
  - **The two snap call sites** in `Presenter` (`src/app/canvas_view.cpp:595-596`
    single-object gizmo, `:807-808` group gizmo) that today pass the empty grid
    defaults — the wiring points this leaf populates.
  - **D-gizmo-5** (`gizmo.md:592-609`): *"`snap_placement` … accepts an optional
    grid-line set that no caller populates yet. Grid display + spacing + snap wiring
    is `editor.canvas.grid`."* Rationale it hands down: *"a grid has no referent in
    the editor yet: snapping to an invisible grid with no spacing control is a
    confusing non-affordance … building the engine grid-line-ready makes the grid
    task pure wiring."* This leaf **honours** that split — it supplies the grid's
    referent (a visible, spaced grid) and only then feeds the lines to the engine.
  - The **`Presenter` per-pane session-state pattern** (`canvas_view.hpp:177`;
    transient view state like `camera` `:185`, `look_through` `:197`, and the
    `gizmo_cell_*` drag fields `:219-229`) — the home for the grid's display toggle
    + spacing, which the `.tji` note calls "local UI state."
- **`editor.cells.group_transform`** (`tasks/refinements/editor/group_transform.md`,
  Done 2026-07-29) — reuses `snap_placement` unchanged for the union box against
  non-selected `pick_targets`, and states *"grid snapping stays deferred to
  `editor.canvas.grid`."* Its second call site (`canvas_view.cpp:807-808`) is the
  group half this leaf must also wire, so grid snap applies to both single-object
  and group drags identically.
- **`editor.cameras.manip`** (`tasks/refinements/editor/manip.md`, Done 2026-07-22)
  — grouped "grid" with the snapping engine it deferred to `editor.cells.gizmo`;
  the chain terminates here. It also established the `Presenter::camera` viewport
  affine + the `to_screen`/inverse lambda idiom (`canvas_view.cpp:371-374`) the draw
  path reuses.
- **`interact::place_in_view`** (`src/interact/interact.cpp:88-95`) — the canonical
  "pane rect pulled back through the inverse viewport camera" recipe
  (`view.inverse()->map_rect(arbc::Rect::from_size(pane_w, pane_h))`) that yields
  the **visible composition region** the grid lines cover; `fit_region`
  (`src/interact/ace/interact/interact.hpp:69`) is the sibling viewport helper.

**Pending (owned here):** the pure `composition_grid_lines` generator, the two
`Presenter` grid fields + their accessors, the grid draw, and the two snap-call
wirings. Nothing downstream is blocked on an unwritten predecessor; every input
exists at `v0.4.0`.

## What this task is

Give the composition grid a **referent** and wire it end to end (D7/D8 §6:260):

1. **L1 `interact`** — `interact::composition_grid_lines(double spacing, const
   arbc::Rect& region, int max_lines)`, a pure helper beside `snap_placement`
   (`src/interact/ace/interact/pick.hpp` + `src/interact/pick.cpp`). It returns
   `GridLines{std::vector<double> xs; std::vector<double> ys;}` — the
   composition-space coordinates of a uniform grid of the given `spacing`, anchored
   at the composition origin (lines at integer multiples `k·spacing`), that fall
   within `region`. It is the **single source** the draw path and the snap path
   both consume, so *the lines you snap to are exactly the lines you see*. Guards
   (all → empty result, a safe no-op): `spacing ≤ 0` or non-finite; a
   degenerate/non-finite `region`; a would-be line count exceeding `max_lines` on
   either axis (an over-dense grid at extreme zoom-out **vanishes** rather than
   smearing the pane or ballooning the snap-candidate list).

2. **L4 `app` — per-pane state, draw, snap wiring** (`src/app/ace/app/canvas_view.hpp`
   + `src/app/canvas_view.cpp`):
   - Two `Presenter` session fields — `bool grid_show = false;` and `double
     grid_spacing = 64.0;` (beside `look_through` `:197`) — **not** journaled, not
     document/project state, per-pane (D-grid-1). Plus const readback accessors
     `bool grid_visible(std::string_view view_id) const;` / `double
     grid_spacing(std::string_view view_id) const;` and their `set_*` siblings for
     the e2e, mirroring `look_through`/`set_look_through` (`canvas_view.hpp:114-166`).
   - **Draw:** in `draw_content` (`canvas_view.cpp:98`), when `grid_show`, after the
     composition blit (`draw_canvas_interactive`, `:217`) and in the passive
     draw-list style of the scale bar (`:254`), compute `visible =
     p.camera.inverse()->map_rect(arbc::Rect::from_size(pane_w, pane_h))`, call
     `composition_grid_lines(grid_spacing, visible, kGridLineCap)`, and stroke each
     line as a hairline via the existing `to_screen` lambda (`:371-374`). Passive
     chrome — no hit-testing, drawn under the gizmos/selection (`:263-281`) so those
     stay legible on top.
   - **Snap wiring:** at both `snap_placement` call sites (`:595-596` single-object,
     `:807-808` group), when `grid_show`, pass `g.xs`/`g.ys` from the **same**
     `composition_grid_lines(grid_spacing, visible, …)` into the `grid_x`/`grid_y`
     arguments; when the grid is hidden, pass the empty defaults (D-grid-3 — grid
     snap is coupled to grid visibility). The existing Cmd/Ctrl `bypass` already
     suppresses *all* snapping, grid included (`pick.cpp:381`, §6:258).
   - **Control:** a per-pane "Show grid" checkbox + a spacing `InputInt`/`DragFloat`
     overlay drawn beside `draw_camera_picker` (`:334,337`) — the established
     top-left interactive-overlay slot — using the `###stable_id` label convention
     the export panel uses (`views.cpp:357-384`), so the e2e can address
     `"canvas#N/Show grid###grid_show"` and `"canvas#N/Spacing###grid_spacing"`
     independent of visible text.

It deliberately does **not** ship: grid **origin/offset** or rotation controls (the
grid is origin-anchored and axis-aligned — §6:260 names only "the composition
grid," no phase control); adaptive/sub-divided spacing at zoom; snapping to a grid
you cannot see (an explicit non-affordance, D-grid-3); aspect-preset or
rotation-angle snapping (`snap_placement` already owns the 15° rotation snap;
aspect presets remain the camera-inspector's, out of this leaf per D-gizmo-5); any
persistence of grid state across sessions or into the project file (D-grid-1 —
it is transient view state, like pan/zoom).

## Why it needs to be done

§6:260 lists **"the composition grid"** among the shared snap targets, and D7/D8
(`docs/00-design.md:474-475`) make snapping a first-class part of the "drag the
extent" manipulation model. The predecessor `editor.cells.gizmo` built the snap
engine **grid-ready on purpose** — `snap_placement` already folds `grid_x`/`grid_y`
into its candidate set (`pick.cpp:411-416`) — and explicitly deferred the grid's
UI to this leaf because *a grid with no display and no spacing control is a
non-affordance* (D-gizmo-5, `gizmo.md:592-609`). Without this leaf the promised
snap target is dead: the engine has the hook, but no caller ever populates it, so a
user has cell/camera-edge snapping but no regular grid to arrange against — the one
§6 snap target that does not derive from another object. This leaf closes that gap
with the smallest possible surface (one pure generator + two one-line wirings + a
toggle), turning D-gizmo-5's "pure wiring" prediction into shipped behaviour, and
gives group and single-object drags the same grid discipline
(`group_transform.md`).

## Inputs / context

**Governing design docs (normative — the constitution):**

- **§6 "Modifiers & snapping (shared)"** (`docs/00-design.md:257-263`, verbatim):
  *"Shift constrains (aspect / 15° / axis-lock); Alt from center; Cmd/Ctrl
  select-behind and bypass-snap; Space pans the view … Snap targets: cell↔cell
  edges/centers with alignment guides, cell↔camera frame (compose to the crop),
  camera↔cell bounds ('frame this cell exactly'), **the composition grid**, aspect
  presets, rotation angles."* The composition grid is the target this leaf mints
  and wires; Cmd/Ctrl bypass (`:258`) already governs it.
- **D7 — Manipulation model** (`:474`): cells and cameras share one shape and one
  select tool; *"drag the extent, type the resolution; the two are always
  independent."* A grid snap adjusts only the placement `Affine` — the dragged
  extent — never a resolution.
- **D8 — Cell scale ≠ resample** (`:475`): *"Handle-drag changes placement
  (affine), never resolution — non-destructive."* A grid snap is a placement nudge;
  it never touches stored pixels, `content_bounds`, or native resolution (the e2e
  pins native resolution unchanged after a grid snap).
- **§2 — the composition has no resolution** (`:76-81`): the grid lives in
  **composition units** (the space placements and `snap_placement` already work in),
  not pixels — so a grid line is a composition-space coordinate, zoom-independent,
  and the spacing is a composition-unit quantity.

**Governing architecture rows:**

- **A11** — `interact` is chartered as *"hit-test · gizmo · snapping · brush math
  (L1, UI-agnostic)"* (cited `gizmo.md:228,529`); a new pure snapping-geometry
  helper is squarely within that charter — **no doc delta**.
- **A17** — hit-testing/snapping split into the pure `interact` policy core plus the
  one `interact→scene` adapter `pick_targets` (`pick.hpp:245-246`). Grid lines are
  **object-independent** geometry, so `composition_grid_lines` needs neither
  `pick_targets` nor any `scene` type — it is pure `interact` over `arbc::Rect`.
- **§8** — the levelization DAG (`docs/01-architecture.md:308-344`;
  `scripts/check_levels.py:21-40`): `interact → {base, scene}` with `arbc` allowed
  (`check_levels.py:30,50-51`); `views`/`app` are the only ImGui-allowed layers
  (`:34-40,47`). The relevant edges already exist; this leaf adds none.
- **§9** — the universal DoD (`:346-373`) this leaf's Acceptance criteria
  instantiate.

**Editor seams this leaf extends:**

- **Snap engine:** `interact::snap_placement` (`src/interact/ace/interact/pick.hpp:224-226`,
  impl `src/interact/pick.cpp:376`, grid fold `:411-416`); `SnapResult`/`SnapGuide`
  (`pick.hpp:200-211`); internal `SnapLine{pos, omin, omax}` (`pick.cpp:346-352`).
- **Grid-line home:** beside `snap_placement` in `src/interact/ace/interact/pick.hpp`
  + `src/interact/pick.cpp` (co-locating the grid-line producer with its consumer).
- **Visible-region recipe:** `interact::place_in_view`
  (`src/interact/interact.cpp:88-95`, `view.inverse()->map_rect(Rect::from_size(...))`);
  `arbc::Affine::inverse()`/`map_rect`/`apply`
  (`build/dev/_deps/arbc-src/src/base/arbc/base/transform.hpp:25,30,38`);
  `arbc::Rect::from_size`.
- **Canvas draw + state:** `Presenter` (`src/app/ace/app/canvas_view.hpp:177`),
  its `camera` `:185` and `look_through` `:197`; `CanvasView::draw_content`
  (`canvas_view.hpp:78`, `canvas_view.cpp:98`) with the draw-order composition-blit
  `:217` → scale bar `:254` → gizmos `:263-281`; the `to_screen` lambda
  (`canvas_view.cpp:371-374`); the two snap calls (`:595-596`, `:807-808`); the
  overlay-control slot `draw_camera_picker` (`:334,337`); per-pane readback
  accessors (`frames_issued`/`scale_bar_units`/`look_through`, declared
  ~`canvas_view.hpp:114-166`).
- **Overlay-control convention:** `ImGui::SmallButton`/labels in `draw_camera_picker`
  (`canvas_view.cpp:345-355`); the `###stable_id` decoupled-id convention +
  `ImGui::Checkbox`/`ImGui::InputInt` in the export panel (`src/views/views.cpp:357-384`).
  There is **no menu bar** anywhere in the source (grep confirms), so per-pane
  `Presenter` state + a pane overlay is the only established home.

**Predecessor / sibling refinements:** `tasks/refinements/editor/gizmo.md`,
`tasks/refinements/editor/group_transform.md`, `tasks/refinements/editor/manip.md`,
`tasks/refinements/editor/nav.md`.

**libarbc API surface:** consumes only pre-`v0.4.0` value types — `arbc::Affine`
(`apply`/`inverse`/`map_rect`, `transform.hpp:25,30,38`), `arbc::Rect`
(`from_size`), `arbc::Vec2`. **No new library surface, no new pin;**
`tests/arbc_pin_test.cpp` needs no change.

**Test rigs:** `ace_tests` (Catch2, headless) — a new `tests/grid_test.cpp` for the
generator + the snap-integration (patterned on `tests/gizmo_test.cpp`,
`TEST_CASE("grid: …")`); registered in `CMakeLists.txt` (`ace_tests` source list,
~`:232/:246`). `ace_shell_test` (ImGui Test Engine, offscreen software-GL) — a new
`tests/grid_e2e_test.cpp` modelled on `tests/gizmo_e2e_test.cpp` +
`tests/writer_session.hpp` (view-body wiring `gizmo_e2e_test.cpp:206-209`, pane id
`"canvas#1/##canvas_nav"` + `origin` readback `:244-258`, `E2EState` `:149-156`,
overlay `ItemClick` `look_through_e2e_test.cpp:309`); registered ~`CMakeLists.txt:279/291`.
Goldens/screenshots under `tests/goldens/*` via `tests/golden_support.hpp`;
`asan`/`tsan` presets; `tests/lsan.supp`; coverage `diff-cover --fail-under=90`.

## Constraints / requirements

1. **Levelization (`check_levels` clean) — the primary structural assertion.**
   `composition_grid_lines` lands in **L1 `interact`** and takes **primitive**
   inputs only (`double spacing`, `const arbc::Rect& region`, `int max_lines`) →
   `GridLines` of `std::vector<double>` — no `scene`/`app`/ImGui type, no
   `pick_targets`, so no edge beyond the `interact → {base}` + `arbc`-include that
   already exists. The draw, the state, the snap wiring, and the toggle control all
   land in **L4 `app`** (`draw_camera_picker` already lives there and sees ImGui).
   **No `views` change, no new component, no new DAG edge, no `check_levels.py`
   edit;** the L1 core gains no ImGui/GL/SDL include.

2. **`snap_placement`'s signature is not changed — this leaf only populates the
   `grid_x`/`grid_y` params the predecessor already added.** The engine's grid fold
   (`pick.cpp:411-416`) and its competing-target resolution (`best_snap`,
   `pick.cpp:356-372`) are consumed as-is; a grid line competes with object edges
   on equal footing and the nearer wins. No new snap semantics are introduced in
   `interact` beyond the generator.

3. **Grid state is transient per-pane view state (D-grid-1).** `grid_show` /
   `grid_spacing` are `Presenter` session fields — never journaled, never written to
   the document or project, not undoable, not dirtying — exactly like `camera`
   (pan/zoom) and `look_through`. A grid toggle raises **no** `commands::Command`
   and opens **no** transaction.

4. **The lines you snap to are exactly the lines you see (D-grid-2).** The draw path
   and both snap paths call the **same** `composition_grid_lines(grid_spacing,
   visible, …)` over the pane's current visible composition region, so a snapped
   edge always lands on a rendered line. No second grid definition exists.

5. **Grid snapping is coupled to grid visibility (D-grid-3).** Grid lines are fed to
   `snap_placement` **only** when `grid_show` is true; hiding the grid removes grid
   snap (object-edge snapping is unaffected). This is the direct enforcement of
   D-gizmo-5's "no snapping to an invisible grid" — there is no separate,
   independently-toggleable "snap to grid."

6. **A grid snap is a placement nudge, never a resample (D7/D8).** The wiring feeds
   only the candidate placement `Affine` through `snap_placement`; the committed
   result remains one `set_layer_transform`/batch transaction exactly as the gizmo
   already commits (`gizmo.md` D-gizmo-4). Native resolution, `content_bounds`, and
   stored pixels are untouched — the e2e asserts native resolution unchanged after a
   grid snap.

7. **The generator is total and self-limiting.** `composition_grid_lines` never
   divides by zero, never allocates unboundedly, and never returns non-finite
   coordinates: `spacing ≤ 0`/non-finite, a degenerate/non-finite `region`, and an
   over-dense request (line count > `max_lines` per axis) all return an **empty**
   `GridLines`. An empty result draws nothing and snaps to nothing — the grid simply
   isn't there at that spacing/zoom, a defined and testable behaviour.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate`
green (check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.** No new
  component, no new DAG edge, no lint edit; `composition_grid_lines` takes primitive
  `(double, arbc::Rect, int)` and reaches no `scene`/ImGui type; nothing in the L1
  core includes ImGui/GL/SDL. Confirm `scripts/gate`'s level lint passes and no
  entry in `scripts/check_levels.py:21-40` changes.
- **L1 logic — Catch2 units** (`tests/grid_test.cpp`, new in `ace_tests`,
  `TEST_CASE("grid: …")`):
  - **Generation is correct and origin-anchored:** `composition_grid_lines(10,
    Rect{0,0,100,50}, cap)` yields `xs = {0,10,20,…,100}`, `ys = {0,10,…,50}`
    (byte-exact for integer spacing); an **offset** region `Rect{15,15,35,35}`
    spacing `10` yields `xs = ys = {20,30}` (only the multiples of the spacing that
    fall inside `region`); a region spanning the origin includes the **negative**
    multiples (`Rect{-15,-15,15,15}` spacing `10` → `{-10,0,10}`).
  - **Guards (each → empty, no crash):** `spacing ≤ 0`; non-finite `spacing` or
    `region`; a degenerate `region` (`x1 < x0`); an over-dense request whose per-axis
    count exceeds `max_lines`.
  - **Snap integration — the property the predecessor could not test without a
    grid:** feed `composition_grid_lines` output into `snap_placement`'s
    `grid_x`/`grid_y` and assert (a) a moving cell whose edge lands within `tol` of a
    grid line snaps **flush** to that `k·spacing` line and reports a `SnapGuide`;
    (b) outside `tol` the candidate is returned unchanged; (c) `bypass=true`
    (Cmd/Ctrl) returns the candidate unsnapped even with grid lines present; (d) a
    grid line and an object edge both within tolerance resolve to the **nearer**
    (the existing `best_snap`, now exercised with grid inputs); (e) an empty
    `grid_x`/`grid_y` (grid hidden) reproduces the pre-grid object-only snap exactly.
- **Rendered output — golden.** **No `render_offline` composition golden** — the
  grid is **app-layer overlay chrome**, not part of the libarbc composition
  (`render_offline` composites cells/cameras, not editor draw-list chrome); a
  composition golden would be unchanged by this leaf (justified §9 exception). A
  **screenshot baseline** of a canvas pane with the grid shown at a known spacing
  (`tests/goldens/grid_overlay_*.png` via the e2e capture, `tests/golden_support.hpp`)
  pins that lines render at the expected composition-space positions where it adds
  signal.
- **UI e2e — ImGui Test Engine** (`tests/grid_e2e_test.cpp`, in `ace_shell_test`,
  modelled on `tests/gizmo_e2e_test.cpp` + `tests/writer_session.hpp`, offscreen
  software-GL, driven by widget id, state read through `E2EState` + a new
  `grid_visible`/`grid_spacing` accessor):
  - toggle the pane's **"Show grid"** (`ItemClick("canvas#1/Show grid###grid_show")`);
    assert `canvas.grid_visible("canvas#1")` flips true and the grid renders
    (screenshot baseline).
  - set a spacing, then **drag a cell** so an edge falls just inside snap tolerance
    of a grid line; assert the committed placement lands the edge on a `k·spacing`
    line **and** the cell's **native resolution is unchanged** (grid snap is
    placement, not resample — D8).
  - **Cmd/Ctrl-drag** near a grid line ⇒ **no** snap (bypass), matching the gizmo's
    existing bypass case.
  - toggle the grid **off**, drag near where a line was ⇒ **no** grid snap
    (object-edge snapping still works) — pinning the visibility coupling (D-grid-3).
  - drive the **group** path (≥2 selected) near a grid line ⇒ the union snaps to
    grid too (the `:807-808` call site), one journal entry
    (`journal().cursor()` +1).
- **Threading — ASan/TSan.** The grid adds **no** shared mutable state and **no**
  document mutation: `grid_show`/`grid_spacing` are UI-thread-only `Presenter`
  fields, `composition_grid_lines` is a pure function, and the draw/snap paths read
  the composition only through the existing lock-free `pin()`
  (`p.camera.inverse()` and `pick_targets` are already-covered reads). **No new
  cross-thread surface arises**, so no new TSan case is required; the standing
  real-pool anchor in `tests/canvas_host_test.cpp` (UI-thread `pick_targets`/gizmo
  reads while the render thread `drive_once`s the same document, established by
  `gizmo.md`) already covers every read this leaf performs. This is stated
  explicitly rather than adding a redundant lane. Residual Mesa leaks via
  `tests/lsan.supp`.
- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed lines;
  clang-format + build clean across presets. Tests ship with the task.

**Deferred WBS work — none as a WBS leaf.** This leaf is self-contained: the snap
engine, the viewport transform, the visible-region recipe, and the overlay-control
pattern all exist, and the feature ships whole. The out-of-scope surfaces named
under *What this task is* (grid origin/offset & rotation, adaptive sub-division, an
independent snap-without-display toggle, cross-session/project persistence) are
**deliberate design exclusions** (D-grid-1/-3), **not** deferred tasks — none is
minted as a WBS leaf, and none is an "audit"/"revisit" placeholder. If a concrete
user need for grid phase-offset later arises it is a fresh refinement, not a debt
this leaf owes.

## Decisions

- **D-grid-1 — Grid display + spacing are transient per-pane `Presenter` session
  state (L4 `app`), not a shared/global setting and not document/project state.**
  Two fields on `Presenter` (`grid_show`, `grid_spacing`, beside `look_through`
  `canvas_view.hpp:197`), read at draw/drag time, never journaled. *Rationale:* the
  `.tji` note calls this "local UI state"; the established pattern for transient view
  state (pan/zoom `camera`, `look_through`, gizmo drag fields) is exactly per-pane
  `Presenter` session state, and there is **no menu bar** in the source to host a
  global toggle. Per-pane matches D18 multi-canvas — each pane frames the
  composition independently, so each may show or hide its own grid — and it is the
  simplest abstraction with the one call site (the canvas pane) that exists today.
  *Alternative rejected:* a single shared/global grid setting — it needs a new
  observable home (a `dockmodel` value or an app singleton) for no clear benefit over
  per-pane, and cross-pane grid agreement is not something §6 or D7 asks for.
  *Alternative rejected:* journaling grid state into the document/project so it
  persists — it is a view aid, not composition data; journaling it would dirty the
  document and violate the D15 "view state is transient, not a scene transaction"
  line the nav/look-through leaves already hold. **No doc delta required.**

- **D-grid-2 — A single pure L1 generator, `interact::composition_grid_lines`,
  feeds both the draw path and the snap path; the drawn lines and the snapped lines
  are identical by construction.** *Rationale:* co-generating guarantees "you snap to
  the lines you see" (no drift between a display grid and a snap grid — the exact
  confusion D-gizmo-5 warns of), keeps the geometry in the headless-testable L1 core
  (the DoD's "L1 logic is the bulk"), and leaves `snap_placement` untouched since it
  already consumes `grid_x`/`grid_y` (`pick.cpp:411-416`). Placing it in `pick.hpp`
  beside `snap_placement` co-locates the grid-line producer with its consumer.
  *Alternative rejected:* generate the display grid in the L4 draw code and a
  separate snap grid at the call site — two definitions that can silently diverge,
  and the draw-side copy would be untestable without ImGui. *Alternative rejected:*
  compute grid lines inside `snap_placement` from a spacing argument — it would
  couple the pure snap engine to a grid model and duplicate the draw path's need for
  the same lines. **No doc delta required** (a new function in the A11-chartered L1
  `interact` component).

- **D-grid-3 — Grid snapping is coupled to grid visibility: lines feed
  `snap_placement` only when the grid is shown; there is no separate "snap to grid"
  toggle.** *Rationale:* this is the direct payoff of D-gizmo-5's deferral rationale —
  *"snapping to an invisible grid with no spacing control is a confusing
  non-affordance."* Coupling means the user snaps to precisely what they see; hiding
  the grid cleanly removes the behaviour without a second control to reason about.
  Cmd/Ctrl `bypass` remains the universal per-drag snap-off (§6:258), grid included.
  *Alternative rejected:* an independent snap-to-grid toggle decoupled from display —
  it re-introduces the invisible-grid non-affordance and adds a second, easily
  desynchronised control for a workflow (snap to a hidden grid) no design section
  asks for; YAGNI. **No doc delta required.**

- **D-grid-4 — Spacing is in composition units, user-editable, defaulting to `64.0`
  with the grid off by default; the generator is origin-anchored, total, and
  self-limiting.** Lines sit at integer multiples `k·spacing` from the composition
  origin, within the visible region; `spacing ≤ 0`/non-finite, degenerate region, or
  an over-dense count all yield an empty grid. *Rationale:* composition units is the
  space placements and `snap_placement` already work in (§2 — the composition has no
  resolution, so a pixel-spaced grid would be meaningless), making the grid
  zoom-independent and the snap math trivial. Off-by-default keeps the grid an opt-in
  aid rather than imposed chrome; `64.0` is a round, moderate default that reads as a
  usable grid near fit-zoom and is immediately adjustable via the spacing input. The
  self-limiting empty-on-over-dense rule means zooming far out with a fine spacing
  makes the grid **vanish** rather than smear the pane grey or balloon the
  snap-candidate list — a defined, testable degradation. *Alternative rejected:* a
  pixel/device-space grid — it contradicts §2's "no composition resolution," would
  shift under zoom, and would not compose with the composition-unit `snap_placement`.
  *Alternative rejected:* an adaptive spacing that subdivides with zoom — extra
  machinery with no §6 mandate; a fixed user-set spacing plus the vanish-when-dense
  rule covers the need. **No doc delta required.**

- **D-grid-5 — The grid is passive draw-list chrome and a per-pane overlay control,
  drawn in `draw_content` after the composition blit and below the gizmos; the toggle
  lives beside `draw_camera_picker`.** *Rationale:* the grid is a reference aid, not
  an interactive object — it takes no hit-testing and must not occlude the gizmos or
  selection, so it strokes on `GetWindowDrawList()` after the composition (`:217`) and
  before the gizmo overlays (`:263-281`), in the same style as the scale bar
  (`:254`) and focus marker (`:310-329`). The toggle + spacing input reuse the
  `draw_camera_picker` overlay slot (`:334,337`) and the `###stable_id` label
  convention (`views.cpp:357-384`) so the e2e can drive them by a stable id. *Rationale
  cont.:* there is no menu bar to host the control, and a pane overlay keeps the
  per-pane state (D-grid-1) co-located with the pane it governs. *Alternative
  rejected:* a global toolbar/menu control — no such surface exists, and it would
  divorce the control from the per-pane state. **No doc delta required.**

## Open questions

(none — all decided.)

No item is routed to `tasks/parking-lot.md`: every input exists at the current pin,
no cross-repo library verb is needed (unlike `editor.cells.resolution`'s resample
gap), and the two judgment calls this leaf makes — per-pane vs. shared state
(D-grid-1) and the `64.0` default spacing (D-grid-4) — are cleanly decidable from
the established `Presenter` session-state pattern and §2's composition-unit space,
so they are decided here rather than deferred.

## Status

**Done** — 2026-07-29.

- `src/interact/ace/interact/pick.hpp`, `src/interact/pick.cpp` — added `GridLines` struct and pure `composition_grid_lines(spacing, region, max_lines)` generator (L1, total, self-limiting, origin-anchored at integer multiples of spacing).
- `src/app/ace/app/canvas_view.hpp` — added `Presenter` grid fields (`grid_show`/`grid_spacing`) and `grid_visible`/`grid_spacing`/`set_*` accessors for e2e readback; `k_grid_line_cap` constant.
- `src/app/canvas_view.cpp` — passive grid draw in `draw_content` (after composition blit, before gizmos); both `snap_placement` wirings (single-object `:595-596` and group `:807-808`); "Show grid" checkbox + spacing `InputDouble` overlay in `draw_camera_picker`.
- `tests/grid_test.cpp` — 9 Catch2 unit cases (`TEST_CASE("grid: …")`): generation correctness, origin-anchoring, offset regions, negative multiples, all guards (spacing ≤ 0, non-finite, degenerate region, over-dense), and snap-integration properties (a–e).
- `tests/grid_e2e_test.cpp` — ImGui Test Engine e2e: toggle renders via differential pixel check, single-cell grid snap (native res unchanged), Cmd/Ctrl bypass, visibility coupling, group snap (one journal entry).
- `CMakeLists.txt` — registered both `grid_test.cpp` and `grid_e2e_test.cpp` in `ace_tests`/`ace_shell_test`.
