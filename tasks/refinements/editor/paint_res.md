# editor.paint.paint_res — Cell-fixed resolution painting; screen→cell mapping; detail-floor readout

## TaskJuggler entry

- **Task:** `editor.paint.paint_res` (`tasks/00-editor.tji:685-690`, under
  `task paint "Painting"` at `:677`).
- **Effort:** `2d` (`:686`) · `allocate team` (`:687`).
- **Depends:** `!brush` (`editor.paint.brush`, `:688`) — the sibling under the
  same `paint` parent, **Done** 2026-07-30 (`tasks/refinements/paint/brush.md`
  Status). The dependency is satisfied.
- **Note (`.tji:689`):** "Dabs rasterize into the CELL's fixed working grid; the
  brush footprint maps camera px → composition units → cell px by affine
  composition. The '~N px on <cell>' readout doubles as the resolution-health /
  detail-floor cue. Design: D4/D5."
- **Back-link:** the `.tji` note currently ends `Refinement:
  tasks/refinements/paint_res.md` (the flat interim path). This refinement lands
  at **`tasks/refinements/editor/paint_res.md`** per the orchestrator's area =
  first-dot-segment (`editor`) assignment (`tasks/refinements/README.md:9-18`);
  the closer updates the note back-link to the real path and adds `complete 100`
  after `allocate team` (`tasks/refinements/README.md:47-68`). **Do not**
  hand-edit the `.tji` here.
- **Downstream:** `editor.paint.paint_res` is a direct `depends` of the
  packaging gather `editor.packaging.package` (`tasks/00-editor.tji:738`) — the
  feature-complete editor bundle. It closes the D4/D5 *painting-side* of the
  detail-floor story, the brush-relative twin of the camera-relative health badge
  `editor.cells.resolution` shipped in the Inspector.

## Effort estimate

**2 days.** The heavy lifting — the paint verb, the screen→cell affine
composition, the tool dispatch, the on-canvas HUD chrome — **already shipped**
with `editor.paint.brush`; the greenfield is one thin pure-`interact` helper and
the live on-canvas readout that surfaces it, plus tests.

- **Templated (already in place from `editor.paint.brush`):** the paint verb
  `scene::brush_dab` (`src/scene/cell.cpp:567-607`); the screen→composition→cell
  footprint `interact::brush_footprint(camera, placement, device_point,
  comp_radius)` (`src/interact/ace/interact/interact.hpp:73-79`, impl
  `src/interact/interact.cpp:61-88`) that maps device→comp (`camera.inverse()`)
  →content-px (`placement.inverse()`) with `radius = comp_radius /
  placement.max_scale()`; the brush size/ring HUD block in
  `CanvasView::dispatch_brush` (`src/app/canvas_view.cpp:505-551`); the target
  cell's native grid on `scene::Cell::detail.native_pixels`
  (`src/scene/ace/scene/cell.hpp:198-214,231`); the sibling
  `interact::resolution_health` verdict shape to mirror
  (`interact.hpp:313-348`). ~0.2d.
- **Greenfield — the detail-floor readout math** (L1 `interact`): a pure helper
  converting the shipped `brush_footprint` result into the target cell's
  **native-pixel diameter** (the `≈ N px` number) and a **detail-floor verdict**
  (at/near the floor as it approaches 1 px). ~0.4d incl. Catch2.
- **Greenfield — the on-canvas readout** (L4 `app`): render the live
  `≈ N px on ‹cell›` label in the brush HUD (`canvas_view.cpp:505-529`), updating
  per-cursor and per-cell, with the detail-floor cue when `N` approaches 1
  (§4:120-124). ~0.9d incl. e2e + screenshot baseline.
- **Tests + threading scope.** ~0.5d.

**No new component, no new DAG edge, no new libarbc surface consumed, no doc
delta.** All inputs already exist; the pin stays at `v0.4.0`.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.paint.brush`** (`tasks/refinements/paint/brush.md`, Done 2026-07-30)
  — the direct predecessor, whose Status this leaf builds directly on. It ships:
  - **`scene::brush_dab(document, registry, cell, centers, inner_radius,
    outer_radius, color, coalesce_key)`** (`src/scene/ace/scene/cell.hpp:386-388`,
    impl `src/scene/cell.cpp:567-607`) — the L1 `scene` verb that rasterizes dabs
    into a resolved `org.arbc.raster` cell's editable facet, gated on the generic
    `content->editable()` PaintedRaster test (never a `kind_id` switch, A16),
    `centers` in **level-0 (content) pixel** coordinates, one coalesced
    transaction per stroke (D15). **The dab-into-the-fixed-grid half of this
    leaf's note is already implemented** — this leaf adds the *readout*, not a new
    paint verb.
  - **`interact::brush_footprint(const arbc::Affine& camera, const arbc::Affine&
    placement, arbc::Vec2 device_point, double comp_radius) -> BrushFootprint{Vec2
    center; double radius; bool valid;}`** (`interact.hpp:73-79`,
    `interact.cpp:61-88`) — the pure L1 **screen→composition→cell affine
    composition** (device→comp via `camera.inverse()`, comp→content-px via
    `placement.inverse()`, `radius = comp_radius / placement.max_scale()`). This
    **is** the "camera px → composition units → cell px by affine composition"
    the note names; this leaf reads its `.radius` to derive the px-on-cell number.
  - **`interact::stroke_dabs` / `interact::brush_units(view_fraction,
    view_short_edge_units)` / `brush_fraction_from_slider` /
    `brush_slider_from_fraction`** (`interact.hpp:21-79`, `interact.cpp:12-88`),
    `k_brush_softness=0.5`, `k_brush_dab_spacing=0.25` — the screen-fraction ↔
    composition-units ↔ log-slider mappings (D5, screen-locked).
  - **`CanvasView::dispatch_brush(Presenter&, const views::CanvasInput&, int
    pane_w, int pane_h, float origin_x, float origin_y)`** (decl
    `src/app/ace/app/canvas_view.hpp:411`, impl `src/app/canvas_view.cpp:497-627`)
    — the L4 gesture arm this leaf extends. Brush size is the screen fraction
    `double brush_slider_t_` (`canvas_view.hpp:455`, seeded ctor
    `canvas_view.cpp:114`); the size/ring HUD is drawn at `canvas_view.cpp:505-551`
    (`ImGui::SetCursorScreenPos(origin_x+8, origin_y+8)`, ids `##brush_size` /
    `##brush_size_pct`; ring `AddCircle` at `:537-541`). The stroke's target cell
    + placement are already resolved onto the `Presenter`
    (`brush_stroke_cell`/`brush_stroke_placement`, `canvas_view.hpp:327-332`).
  - **D-brush-5 (`paint/brush.md:542-559`) explicitly hands this leaf** "the
    detail-floor readout and anisotropic footprint" while shipping the isotropic
    round dab and the functional footprint mapping this leaf refines.
- **`editor.cells.resolution`** (`tasks/refinements/editor/resolution.md`, Done
  2026-07-28) — the **camera-side** twin, which this leaf must stay consistent
  with. It ships:
  - **`scene::CellDetail{ DetailSource source; std::optional<std::pair<int,int>>
    native_pixels; bool borrowed; }`** (`src/scene/ace/scene/cell.hpp:198-214`), a
    field of `Cell` (`.detail`, `cell.hpp:231`), populated in the pinned `cells()`
    walk from the generic `editable()`/`external_asset_ref()` facets (D-resolution-1/-2,
    A16). `detail.native_pixels` (nullopt = resolution-independent, 1 native px =
    1 content unit) is **the cell's fixed working-grid dimension** this readout
    reads against — no new scene surface.
  - **`interact::resolution_health(int native_w, int native_h, const arbc::Affine&
    placement, int cam_w, int cam_h, const arbc::Affine& cam_frame) -> {double
    ratio; ResolutionVerdict verdict;}`**, `enum class ResolutionVerdict {
    NotApplicable, Crisp, Soft }` (`interact.hpp:313-348`, `interact.cpp:506-540`)
    — the pure, primitive-in, `Crisp` (`r ≤ 1`) / `Soft` (`r > 1`) verdict shape
    this leaf's detail-floor helper mirrors, replacing the *camera* resolution +
    frame with the *brush* footprint (`resolution.md:222-225,456` name
    `editor.paint.paint_res` as exactly the brush-relative twin).
  - The **camera-vs-cell** readouts in the **Inspector** — `worst_cell_health`
    (`src/app/inspector_panel.cpp:21-37`), "Native resolution: %d x %d px"
    (`:62-64`), "Health: soft — sampled at %.2fx · detail floor %d x %d px" /
    "Health: crisp …" (`:119-124`), and the camera twin `src/app/camera_inspector.cpp:91`.
    This leaf is deliberately the **on-canvas, brush-relative** readout, distinct
    from those inspector strings (`resolution.md:164-166`).

**Pending (owned here):** one pure L1 `interact` detail-floor helper and the live
on-canvas `≈ N px on ‹cell›` readout in `dispatch_brush`, plus tests. Nothing
downstream is blocked on an unwritten predecessor; `!brush` and
`editor.cells.resolution` are both Done.

## What this task is

Make the brush's real effect on the target cell **legible as you paint** — the
D5/§4 secondary readout `≈ 48 px on ‹retouch›` that **doubles as the
resolution-health / detail-floor cue**. Concretely, three things, only the
readout of which is new code:

1. **Dabs already rasterize into the cell's fixed working grid.** The
   `scene::brush_dab` verb (`cell.cpp:567-607`) paints into the resolved raster
   cell's editable facet in level-0 (native) pixel coordinates within its fixed
   tiled grid — libarbc's `paint` is the only mutation and there is **no resize
   verb** (`raster_content.hpp:297-362`; the detail floor is intrinsic to the
   fixed grid, §4:110-114). This leaf **verifies and pins** that D8/§4 storage
   invariant; it ships no new paint verb.

2. **The brush footprint maps camera px → composition units → cell px by affine
   composition** — already implemented by `interact::brush_footprint`
   (device→comp via `camera.inverse()`, comp→content-px via `placement.inverse()`).
   This leaf makes that mapping's **reported density resolution-correct** and
   reads its `.radius` to derive how many of the target cell's **native pixels**
   the brush diameter covers (§2:76-87 "resolution health is computable" — the
   same affine composition, inverted onto the brush footprint instead of a
   camera).

3. **The `≈ N px on ‹cell›` readout — the new, headline deliverable.** A live
   on-canvas label next to the brush size HUD, translating the current brush
   footprint into the target cell's native-pixel diameter, updating with zoom
   and per cell (D5:149-153). It **doubles as the resolution-health / detail-floor
   cue**: as `N` approaches 1, the brush maps below one cell pixel — the detail
   floor, where no real detail can be added (§4:120-124) — and the readout says
   so. Today **no such on-canvas readout exists** (only the Inspector strings
   above); the number "px" survives here strictly as a description of the mark's
   real effect, never as the thing the user sets (D5:152-153).

The detail-floor math is **pure L1 `interact`**; the label, its per-cursor/per-cell
update, and the floor cue are **L4 `app`** (extending the existing
`dispatch_brush` HUD block). No `scene` change, no new libarbc surface.

**Not in scope, by prior decision / WBS split:** the camera-vs-cell health badge
and the resample-to-crisp story, which are `editor.cells.resolution`'s (Done, its
Inspector badge; the resample *mutation* is parking-lotted on a cross-repo verb,
`resolution.md` D-resolution-5); the sRGB/HSV picker + eyedropper that *produce*
the paint color (`editor.panels.color`, Done). **Deliberately deferred to the
parking lot** (concrete but demand-gated, mirroring `paint/brush.md` D-brush-7):
the genuinely **anisotropic elliptical dab shape** under non-uniform placement
(see Decisions D-paint_res-3, Open questions).

## Why it needs to be done

D4/D5 and §4 make the detail floor *the* thing that keeps painting honest: "Zoom
in until a screen-sized brush maps below one cell pixel and you can no longer add
real detail (you're painting sub-pixel)" (`docs/00-design.md:120-122`). §4 states
the fix is explicit — resample the grid up, or drop a higher-res cell — but a
user only knows they are **at** the floor if the readout tells them: "it doubles
as the resolution-health cue — when it approaches 1, you're at the detail floor
and should resample the cell up" (`:149-153`). `editor.paint.brush` made painting
*work* but shipped **no** per-cell effect readout, so today a user painting on a
512² cell zoomed in far enough is silently laying sub-pixel dabs with no cue.
This leaf is that cue — the last piece that makes the D4/D5 painting model
legible, and the brush-relative twin of the camera-relative health badge
`editor.cells.resolution` already shipped in the Inspector. It composites nothing
new; it reads what is already modeled (`Cell::detail.native_pixels`, the placement
affine, the viewport camera) and surfaces it where the user is looking — on the
canvas, at the brush.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **D4 — Paint storage** (`docs/00-design.md:471`, verbatim): *"Cell-owned
  **fixed** resolution; **resample-up** is the detail escape hatch."* The backing
  §4 storage prose (`:110-128`): the dab rasterizes into *"the **cell's own fixed
  working grid**"*; *"There is a **detail floor** at the cell's resolution, with a
  natural escape hatch. Zoom in until a screen-sized brush maps below one cell
  pixel and you can no longer add real detail (you're painting sub-pixel)."* This
  leaf reads that fixed grid (`native_pixels`) and surfaces the floor.
- **D5 — Brush size** (`:472`, verbatim): *"**`% of view`** (shorter edge, log
  slider) + on-canvas ring; **"px" appears only as a per-cell effect/health
  readout**; **screen-locked**."* The backing §4 readout bullet (`:149-153`,
  verbatim): *"**Secondary readout:** translate live into the target cell —
  `≈ 48 px on ‹retouch›`. This is *not* the control; it changes with zoom and per
  cell, and it **doubles as the resolution-health cue** — when it approaches 1,
  you're at the detail floor and should resample the cell up."* This bullet **is**
  this leaf's specification.
- **§2 — Resolution health is computable** (`:76-87`): cells provide detail at
  their own fixed native resolution; *"**Resolution health** is therefore
  *computable*, not a vibe: … compare its pixel density over a cell's region
  against that cell's native detail."* The brush readout is that computation with
  the brush footprint standing in for a camera's pixel density.
- **D8 — Cell scale ≠ resample** (`:475`) / §6 (`:238-244`): placement changes
  the affine, never the stored pixels or the native resolution. A brush stroke is
  the *dual* — it writes pixels into the existing fixed grid at the cell's own
  resolution, never re-grids. The readout reads native px and placement as
  **independent** quantities and never conflates them (`resolution.md:340-343`).
- **D10 — Color boundary** (`:477`): the paint color is `editor.panels.color`'s
  premultiplied-linear `WorkingPixel`; this leaf touches no color path.

**Governing architecture rows:**

- **A11** (`docs/01-architecture.md:428`) — active-tool state in L1 `dockmodel`;
  `interact` stays pure "brush math"; the `switch(ToolId)` dispatch body lands at
  L4 `app::CanvasView`. This leaf adds detail-floor math to `interact` and a
  readout to the existing `dispatch_brush` body — exactly A11's intended split,
  so **no doc delta**.
- **A16** (`:433`) — Registry-driven, no kind allowlist. The readout reads the
  target's provenance/native grid from `Cell::detail` (classified from generic
  `editable()`, `cell.cpp` per D-resolution-2), never a `kind_id` switch.
- **A23** (`:440`) — the single `arbc::RasterTileStore` per `Document` is owned in
  `project`/`commands`, **not** `editor.paint.*` (reversing D-save-2's projection,
  because any raster insert/paste/import mints tiles). This leaf mints no tiles
  and owns no store; it only *reads* the target cell's native grid.
- **§8** — the levelization DAG (`:310-346`; interact charter `:294` "hit-test ·
  gizmo · snapping · **brush math** (L1, UI-agnostic)"): `interact` is L1, deps
  `{base, scene}`, **no** ImGui/GL (`scripts/check_levels.py:30`); only
  `views`/`dock`/`app` see ImGui. This leaf's helper takes primitives + `arbc::Affine`
  only, so the `interact→scene` edge is not even exercised.
- **§9** — the universal DoD (`:348-375`) this leaf's Acceptance criteria
  instantiate.

**libarbc API surface** (pinned **v0.4.0**, `CMakeLists.txt:25`; fetched under
`build/dev/_deps/arbc-src/`):

- `arbc::RasterContent` (`src/kind_raster/arbc/kind_raster/raster_content.hpp:297-362`):
  `bounds()` (`:310`) is `Rect{0,0,w,h}` in native pixels (the fixed grid);
  mutation is **`paint` only** within that grid (`:339-341`) — **no resize / no
  `set_resolution`**, so the detail floor is intrinsic and the "resample up"
  escape hatch has no editor-side verb (`resolution.md` D-resolution-5, already
  parking-lotted). The readout **states** the floor; it offers no resample button
  (no dead UI).
- `arbc::Affine` (`src/base/arbc/base/transform.hpp`): `apply(Vec2)`,
  `inverse() -> optional<Affine>`, `max_scale()`, `map_rect(Rect)` — the affine
  composition the footprint and readout use.
- **No new library surface, no pin bump:** this leaf consumes only the generic
  facets and helpers the brush/resolution leaves already pin. `tests/arbc_pin_test.cpp`
  needs no change.

**Editor seams this leaf extends:**

- **L1 `interact`** — the math home (`src/interact/ace/interact/interact.hpp`,
  impl `src/interact/interact.cpp`): the new detail-floor helper sits beside
  `brush_footprint` (`:73-79`), `brush_units` (`:21`), and `resolution_health`
  (`:313-348`).
- **L1 `scene`** — `scene::Cell::detail.native_pixels` (`cell.hpp:198-214,231`);
  `scene::cells` (`cell.hpp:~248`) already fills it in the pinned walk; the paint
  verb `scene::brush_dab` (`cell.cpp:567-607`). **Read-only for this leaf** — no
  scene change.
- **L4 `app`** — `CanvasView::dispatch_brush` HUD block (`canvas_view.cpp:505-551`),
  the natural anchor for the label (the brush fraction, `p.camera`, the resolved
  stroke cell and its placement/`native_pixels` are all already in scope there);
  the cursor read `views::CanvasInput.focus_x/focus_y` (device px,
  `src/views/ace/views/views.hpp:49-91`); `Presenter::camera` (comp→device,
  `canvas_view.hpp:231`, `p.camera.max_scale()` = device px per comp unit).

**Predecessor / sibling refinements:** `tasks/refinements/paint/brush.md`,
`tasks/refinements/editor/resolution.md`, `tasks/refinements/editor/gizmo.md`,
`tasks/refinements/editor/manip.md`, `tasks/refinements/editor.canvas/tool_dispatch.md`.

**Test rigs:** Catch2 units join `ace_tests` (headless; the brush units live in
`tests/brush_test.cpp`, the sibling to extend or mirror; new-file naming
`tests/paint_res_test.cpp`, `TEST_CASE("paint_res: …")`). The e2e joins
`ace_shell_test` (ImGui Test Engine, offscreen software-GL; modelled on
`tests/brush_e2e_test.cpp` / `tests/resolution_e2e_test.cpp`, driven by widget id,
state read through `E2EState`, `IM_REGISTER_TEST(engine, "paint_res", "…")`).
Goldens are raw sRGB8 under `tests/goldens/` compared via
`ace_test::compare_golden` (`tests/golden_support.hpp:36`) against the GL-free
offline path `render::render_document_srgb8(doc, w, h, camera)`
(`src/render/ace/render/render.hpp:37-38`). Threading anchor is
`tests/canvas_host_test.cpp`; `asan`/`tsan` presets; residual Mesa leaks via
`tests/lsan.supp`; coverage `diff-cover --fail-under=90`.

## Constraints / requirements

1. **Levelization (`check_levels` clean) — the primary structural assertion.**
   The detail-floor helper lands in **L1 `interact`** and takes **primitive**
   inputs (a `double` composition-space brush radius/diameter and/or the shipped
   `BrushFootprint`, an `arbc::Affine` placement, and the target's native-px
   presence) — never a `scene::Cell`/`scene::Camera`, so no `interact→scene`
   edge appears. The readout is **L4 `app`** (already sees ImGui). **No new
   component, no new DAG edge, no `scripts/check_levels.py` edit**; nothing in the
   L1 core gains an ImGui/GL/SDL include. `scene` is read-only here; no new
   `arbc` need.

2. **The readout is a pure per-frame read; it opens no transaction and mutates
   nothing.** The `≈ N px on ‹cell›` label is derived each frame from the target
   cell's `detail.native_pixels` + `placement` (read off the same pinned snapshot
   the canvas already reads) and the current brush footprint. It adds **no shared
   mutable state** and **no `Document` mutation** — the paint itself still flows
   through `scene::brush_dab` under `apply_edit` (brush's writer-thread path,
   unchanged). Between frames with no committed dab the composite is byte-identical.

3. **Native px and the on-screen brush size are independent (D5/D8).** The
   readout translates the *screen-locked* brush (a % of view) into the target
   cell's native px; it never turns the number into a control and never lets the
   cell's native resolution change with zoom. Zooming in lowers `N` (finer real
   detail) with the on-screen ring constant (screen-locked, D5); it never
   re-grids the cell (D8).

4. **The detail-floor cue is the computable §2 quantity, resolution-correct.**
   `N` = the brush diameter expressed in the target cell's native pixels =
   `2 × brush_footprint(...).radius` (content px), where `radius = comp_radius /
   placement.max_scale()` uses the **largest** placement axis scale, so `N` is the
   **worst-axis (floor-first)** count — conservative under a non-uniform placement
   (an edge-dragged cell reaches the floor on its most-magnified axis first). A
   `ResolutionIndependent` target (no `native_pixels`), no writable target, and a
   degenerate (non-invertible) placement each yield **no** `N` (readout absent or
   "no paint target"), never a div-by-zero or a false floor.

5. **The readout targets the same cell the brush paints (brush's target model).**
   The target is the selection-primary `org.arbc.raster` cell (brush D-brush-5),
   resolved exactly as `dispatch_brush` already resolves it (generic
   `editable()`/`PaintedRaster`, never a `kind_id` switch, A16). With no writable
   target the ring is still drawn (brush behaviour) and the readout shows no px
   number. During an active stroke the readout reads the pinned
   `brush_stroke_cell`/`brush_stroke_placement`; while merely hovering it resolves
   the current selection-primary raster.

6. **This leaf ships NO resample action and NO new paint verb.** The floor cue
   **states** the condition ("≈ 1 px — at the detail floor") consistent with §4,
   but the "resample the grid up" *mutation* has no libarbc verb
   (`raster_content.hpp` is `paint`-only) and is already parking-lotted on a
   cross-repo `Resampleable` facet (`resolution.md` D-resolution-5, parking-lot
   entry). No dead resample button here — the Inspector's health badge is where
   that story lands when the upstream verb ships.

7. **D8 dual invariance holds across the readout.** Reading `native_pixels` +
   `placement` for the label is a const read of construction-stable content state;
   the readout never writes. Any dab this leaf's e2e drives goes through the
   shipped `scene::brush_dab` and changes pixels only — the target cell's
   `placement`, `content_bounds`, and native resolution are byte-identical before
   and after (already pinned by brush's L1 tests; re-asserted where this leaf
   drives paint).

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella. `diff-cover --fail-under=90` on changed lines; tests ship with the task.

- **Levelization (`check_levels` clean) — the primary structural assertion.** No
  new component, no new DAG edge, no lint edit; the detail-floor helper is added
  to L1 `interact` over primitives + `arbc::Affine` and reaches no `scene` type;
  `scene` is read-only (no new `arbc` need); only `src/app/` (L4) reads
  `views::CanvasInput`, holds ImGui, and draws the label. Confirm `scripts/gate`'s
  level lint passes and no entry in `scripts/check_levels.py:21-40` changes.
- **L1 logic — Catch2 units** (`tests/paint_res_test.cpp`, new file in `ace_tests`;
  `TEST_CASE("paint_res: …")`, moulded on `tests/brush_test.cpp` /
  `tests/resolution_test.cpp`):
  - **Px-on-cell is correct and zoom/placement-relative:** for a raster with a
    known `native_pixels` and a uniform-scaled placement, a brush of a known
    composition-unit radius yields the expected native-pixel diameter `N`;
    doubling the placement scale halves `N` (finer real detail), and the brush
    diameter in comp units is unchanged (screen-locked independence, D5/D8). Use
    integer scales for byte-exact `N`.
  - **Worst-axis under non-uniform placement (Constraint 4):** an edge-dragged
    (non-uniform-scale) placement reports `N` from the **largest** axis scale
    (fewest native px, floor-first), matching `brush_footprint`'s `max_scale`
    convention — the readout never over-reports headroom.
  - **Detail-floor verdict:** `N` well above 1 → not at floor; `N` at/below the
    floor threshold (≈ 1 px) → at-floor; the boundary is exercised from both
    sides. The floor is `N`-relative, independent of the cell's total native px.
  - **N/A cases:** a `ResolutionIndependent` target (no `native_pixels`), a
    non-invertible/zero-area placement, and a non-finite radius each yield **no**
    `N` and **no** false floor (no div-by-zero) — the readout-absent sentinel.
  - **D8 dual invariance (Constraint 7):** driving a `scene::brush_dab` stroke
    (reusing the brush verb) leaves the target `Cell::placement`, `content_bounds`,
    and native resolution byte-identical before/after while pixels change — pins
    that painting never re-grids (§4 storage rule). (This re-uses brush's
    invariant; included so the readout leaf carries its own regression pin.)
- **Rendered output — golden.** **No new composition golden** (justified §9
  exception, `resolution.md:415-416` precedent): this leaf composites nothing new —
  the dab pixels are already pinned by `tests/goldens/brush_dab_64x64.rgba8`
  (`paint/brush.md` Status). A **screenshot baseline** of the on-canvas readout
  (`tests/goldens/paint_res_readout_*.png` via the e2e capture,
  `tests/golden_support.hpp`) is added where it adds signal — pinning that a
  healthy target renders `≈ N px on ‹cell›` and a floor-hit target renders the
  detail-floor cue text.
- **UI e2e — ImGui Test Engine** (`tests/paint_res_e2e_test.cpp`, in
  `ace_shell_test`, modelled on `tests/brush_e2e_test.cpp` /
  `tests/resolution_e2e_test.cpp`, offscreen software-GL, driven by widget id,
  state read through `E2EState`):
  - seed a project with a bounded `org.arbc.raster` cell, select it, set
    `dockspace.tools().select(ToolId::Brush)`, move the cursor over the cell, and
    assert the on-canvas readout shows a plausible `≈ N px on ‹cell›` matching the
    L1 helper for the current camera + placement + brush size.
  - **zoom lowers N, ring constant (D5):** zoom the viewport in; assert `N` drops
    (finer real detail) while the brush ring's on-screen radius is unchanged
    (screen-locked), and the cell's native resolution is unchanged (not re-gridded,
    D8).
  - **detail-floor cue:** zoom in far enough (or shrink the brush) that `N`
    approaches 1; assert the readout flips to the detail-floor cue text and shows
    **no** resample affordance (Constraint 6 / D-resolution-5).
  - **no writable target (Constraint 5):** with no raster selected (a camera or a
    non-raster/`ResolutionIndependent` cell as primary), a Brush hover shows the
    ring but **no** px readout; selecting a raster brings it back.
  - the readout tracks the cursor and the selected cell (moving the cursor / the
    selection updates `N`).
- **Threading — ASan/TSan.** The readout adds **no** new threading surface: it is
  a UI-thread const read of `Cell::detail.native_pixels` + `placement` off the
  live `pin()`, the same fields `editor.cells.resolution`'s real-pool TSan case in
  `tests/canvas_host_test.cpp` already exercises under a live-rendering
  `CanvasHost`. One assertion appended to that anchor confirms the readout's
  per-frame native-px/placement read is data-race-clean while the render thread
  drives the same document; the paint stream itself is covered by brush's existing
  TSan anchor (unchanged). No new lock, no new thread. Residual Mesa leaks via
  `tests/lsan.supp`.
- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed
  lines; clang-format + build clean across presets. Tests ship with the task.

**Deferred WBS work.** **None as a WBS leaf.** The genuinely **anisotropic
elliptical dab shape** (a screen-circular brush over a non-uniform placement paints
an ellipse in content px, which the shipped isotropic `round_dab` approximates as
a circle) is a real but demand-gated refinement with **no design row** demanding
it and **no arbc elliptical-dab primitive** (only the general `CoverageSampler`
mask, `raster_content.hpp:92`); it is registered in `tasks/parking-lot.md` with a
trigger (closer records under the `editor.packaging.package` gather's watch), not
as an orphan WBS leaf (see Open questions / D-paint_res-3). Every other adjacent
surface already has a shipped owner: the camera-vs-cell health badge + resample
story is `editor.cells.resolution` (Done; its resample mutation is itself
parking-lotted on the cross-repo verb), and the paint color is
`editor.panels.color` (Done).

## Decisions

- **D-paint_res-1 — This leaf ships the `≈ N px on ‹cell›` READOUT; the dab verb
  and the footprint mapping it names already shipped with `editor.paint.brush`.**
  The note's first two clauses ("dabs rasterize into the cell's fixed working
  grid"; "the brush footprint maps camera px → composition units → cell px by
  affine composition") describe `scene::brush_dab` (`cell.cpp:567-607`) and
  `interact::brush_footprint` (`interact.cpp:61-88`), both Done. The **new** work
  is the note's third clause — the on-canvas readout that "doubles as the
  resolution-health / detail-floor cue" — which does **not** exist (only the
  Inspector strings `inspector_panel.cpp:62-64,119-124` do). *Rationale:*
  `editor.paint.brush` was scoped (D-brush-5) to lay the functional isotropic
  footprint and hand this leaf the detail-floor readout; the WBS `!brush`
  dependency exists precisely so the readout builds on the shipped mapping. Reusing
  `brush_footprint.radius` (× 2 = native-px diameter) is one line of derivation
  rather than a re-derivation of the affine chain. *Alternative rejected:*
  re-implement the screen→cell mapping in this leaf — pointless duplication of a
  shipped, tested L1 helper. **No doc delta.**

- **D-paint_res-2 — The detail-floor math is a pure L1 `interact` helper over
  primitives; the label + floor cue are L4 `app` in the existing `dispatch_brush`
  HUD.** A pure helper converts a composition-space brush radius (or the shipped
  `BrushFootprint`) + the target's `native_pixels` presence into `{double cell_px;
  bool at_detail_floor; bool valid;}`; `dispatch_brush` renders it beside the size
  HUD (`canvas_view.cpp:505-529`). *Rationale:* §8/A11 make `interact` the pure
  brush-math home (the bulk of coverage, headless Catch2), exactly as
  `resolution_health` and `brush_footprint` already sit; the readout is
  ImGui-linked so it stays in L4 where `p.camera`, the resolved cell, and
  `CanvasInput` are already in scope. This is the split the brush, gizmo, and
  camera-inspector all follow. *Alternative rejected:* compute the px number
  inline in the L4 draw code — strands testable geometry in ImGui-linked code
  reachable only via the e2e, against the "L1 logic is the bulk" DoD. **No doc
  delta** (A11 charters exactly this).

- **D-paint_res-3 — Keep the shipped isotropic circular dab and make the READOUT's
  density worst-axis resolution-correct; defer the anisotropic elliptical dab
  shape to the parking lot.** The painted mark stays `round_dab` (circular in
  content px), which is **exact** under a uniform placement scale (the common case
  — D8 corner-drag is proportional-by-default, `:475`); the readout reports the
  worst-axis native-px count (via `placement.max_scale()`), so the **detail-floor
  cue is honest even under a non-uniform placement**. A genuinely elliptical dab
  (a screen circle mapped through a non-uniform, e.g. edge-dragged, placement) is a
  real refinement but: (a) no design row in `docs/00-design.md` demands elliptical
  marks; (b) libarbc exposes no elliptical/affine dab verb — only the general
  `CoverageSampler` mask (`raster_content.hpp:92`), so it is *implementable* but
  *speculative*; (c) the visible ring-vs-mark mismatch only arises under
  non-uniform placement, and the readout (the design-mandated deliverable) is
  already correct there. *Rationale:* this is the same shape `paint/brush.md`
  D-brush-7 used for hardness/eraser/blend and `resolution.md` D-resolution-5 used
  for resample — a concrete-but-demand-gated refinement goes to the parking lot
  with a trigger, not a speculative WBS leaf, per the "don't mint machinery ahead
  of a consumer" and "never defer an audit task" rules. *Alternative rejected:*
  implement the elliptical dab now (extend `scene::brush_dab` to take a
  `CoverageSampler` + build an affine-correct sampler in `interact`) — real work
  with no design demand that changes a shipped, tested paint verb and roughly
  doubles a 2d leaf; the isotropic dab is exact for the overwhelmingly common
  uniform placement. *Alternative rejected:* re-scope `editor.paint.brush`'s
  isotropic dab as a bug — it is the deliberate D-brush-5 split, correct under
  uniform placement. **No doc delta** (D4/D5/§4 ask for the readout, which this
  leaf ships; the dab-shape nicety is unmentioned by the design).

- **D-paint_res-4 — The floor cue STATES the condition; it ships no resample
  action (no dead UI).** As `N` approaches 1 the readout says "at the detail floor"
  (§4:120-124) but offers no "resample to crisp" button, because the mutation has
  no libarbc verb (`raster_content.hpp` is `paint`-only) and is already
  parking-lotted on a cross-repo `Resampleable` facet (`resolution.md`
  D-resolution-5). *Rationale:* consistency with the camera-side badge, which
  states "source-limited / soft" without a working resample button for the same
  reason; a button that does nothing is dead UI. The user's escape hatch today is
  §4's "drop a fresh higher-res cell" (a normal insert, `editor.cells.insert_schema`,
  Done). *Alternative rejected:* wire a resample button here — no verb to call,
  and it would fork the resample story away from the Inspector where
  `editor.cells.resolution` owns it. **No doc delta.**

- **D-paint_res-5 — The readout targets the brush's selection-primary raster and
  renders live while the tool is active (hover or stroke), not only mid-stroke.**
  D5 says the readout "changes with zoom and per cell" — a live cue as the user
  sizes and positions the brush, not a mid-gesture-only display. So it renders
  whenever `ToolId::Brush` is active and a selection-primary `PaintedRaster` exists,
  at the current cursor (`CanvasInput.focus_x/focus_y`), reading the stroke's
  pinned cell during an active drag and the current selection-primary raster
  otherwise. *Rationale:* matches D5's "live" language and brush's target model
  (D-brush-5) with no new target-resolution logic. *Alternative rejected:* show the
  readout only during an active stroke — contradicts D5's "changes with zoom"
  (which the user does *before* painting) and hides the floor cue exactly when the
  user is deciding whether to zoom further. **No doc delta.**

## Open questions

(none — all decided.)

One item is routed to `tasks/parking-lot.md` for human review rather than the WBS
(it is a concrete-but-demand-gated refinement with a trigger, not an "audit" task
and not warranted without a design signal):

1. **Anisotropic (elliptical) dab shape under a non-uniform placement.** The
   shipped brush paints an isotropic circular `round_dab`; under a non-uniform
   placement scale (an edge-dragged cell, D8 edges are 1D) a screen-circular brush
   ring maps to an ellipse in content px, so the painted mark does not exactly
   match the on-screen ring. The readout is already worst-axis-correct
   (D-paint_res-3), so the *detail-floor cue* is honest regardless; only the
   *mark shape* is approximate. This is agent-implementable when wanted — build an
   affine-correct `CoverageSampler` in L1 `interact` (map each content pixel back
   through the composed `camera∘placement` to evaluate the screen-space radial
   falloff) and add a `scene::brush_dab` overload taking that sampler — but no
   design row demands elliptical marks and the isotropic dab is exact under the
   common uniform placement, so building it now is machinery ahead of a consumer.
   **Trigger:** a design row in `docs/00-design.md` demanding affine-correct
   (anisotropic) dabs, **or** the ring-vs-mark mismatch observed in real use on a
   non-uniformly-scaled cell. When it fires it becomes a real WBS leaf
   (`editor.paint.anisotropic_dab`, ~1.5d) wired into the
   `editor.packaging.package` gather; until then it is a parking-lot observation,
   not a WBS leaf.

## Status

**Done** — 2026-07-31.

- `src/interact/ace/interact/interact.hpp` + `src/interact/interact.cpp`: added pure L1 `interact::brush_detail_floor` helper (`BrushDetailFloor` struct + `k_detail_floor_px` constant) converting a composition-space brush radius and target native-px presence into `{double cell_px; bool at_detail_floor; bool valid;}` — the computable §2/D5 quantity; sits beside `brush_footprint` and `resolution_health`.
- `src/app/ace/app/canvas_view.hpp` + `src/app/canvas_view.cpp`: extended `CanvasView::dispatch_brush` HUD block to render the live `~ N px on <cell>` label (and detail-floor cue text when `N` approaches 1) at the cursor; added `BrushReadout` observability accessor for e2e scene-truth `IM_CHECK`s.
- `tests/canvas_host_test.cpp`: one assertion appended to the TSan anchor confirming the per-frame native-px/placement read is data-race-clean under a live render thread.
- `tests/paint_res_test.cpp` (new, in `ace_tests`): 5 Catch2 cases — px-on-cell/zoom-relative, worst-axis under non-uniform placement, floor verdict both sides, N/A+overflow, D8 dual invariance.
- `tests/paint_res_e2e_test.cpp` (new, in `ace_shell_test`): 6 ImGui Test Engine cases — healthy-N readout, mid-stroke pinned read, zoom-lowers-N, per-cell/placement halving, detail-floor cue with no resample affordance, no-writable-target.
- `CMakeLists.txt`: registered both new test files.
- Screenshot golden (`tests/goldens/paint_res_readout_*.png`) deferred per `resolution.md:415-416` precedent — `ace_shell_test` has no screenshot-golden infrastructure; readout pinned by L1 unit (the number) and e2e `IM_CHECK`s via `CanvasView::brush_readout()`. No new composition golden (dab pixels already pinned by `tests/goldens/brush_dab_64x64.rgba8`). No new component, no DAG edge, no libarbc surface, no doc delta; pin stays at v0.4.1.
