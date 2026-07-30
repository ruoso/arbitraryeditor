# editor.paint.brush — Brush tool: %-of-view size + ring; dab into org.arbc.raster

## TaskJuggler entry

- **Task:** `editor.paint.brush` (`tasks/00-editor.tji:656-660`, under
  `task paint "Painting"` at `:655`).
- **Effort:** `3d` · `allocate team`.
- **Depends:** `editor.cells.model`, `editor.canvas.tool_dispatch`
  (`tasks/00-editor.tji:659`).
- **Note (`.tji:660`):** "The brush tool: size in % of view (shorter edge, log
  slider) with an on-canvas ring; strokes dab into an org.arbc.raster cell's
  editable facet, coalesced into one undo step. Design: D5." Plus the pre-exec
  decision (2026-07-19): "brush size is **SCREEN-locked only** — a canvas-locked
  (composition-units) mode is deliberately **NOT** offered … No size-lock toggle."
- **Back-link:** the `.tji` note points at the flat path
  `tasks/refinements/brush.md`; the real path is
  **`tasks/refinements/paint/brush.md`** (area = first dot-segment of the
  filename, `tasks/refinements/README.md:9-18`). The closer updates the note
  back-link to the real path and adds `complete 100` after `allocate team`
  (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji` here.
- **Downstream dependents:**
  - `editor.paint.paint_res` (`tasks/00-editor.tji:662-666`, `depends !brush`) —
    cell-fixed resolution painting + the `≈ N px on ‹cell›` detail-floor readout;
    it refines the footprint mapping this leaf establishes (see D-brush-5).
  - `editor.paint.retouch_stack` (`:668-672`, `depends !brush`,
    `editor.import.image`) — the "paint a referenced photo → add a retouch layer
    above" escalation of the no-writable-target case.
  - `editor.panels.layers` (`:642`, `depends … editor.paint.brush …`) and the
    `m9_editor` integration gather (`:715`, via the paint successors) bundle it.

## Effort estimate

**3 days.** The dispatch, tool state, coordinate transforms, coalescing, and
overlay-draw precedents all exist; the greenfield is the stroke/dab math and the
raster-mutation verb:

- **Templated:** the inert arm `CanvasView::dispatch_brush(Presenter&,
  const views::CanvasInput&)` is already wired into the closed tool switch
  (`src/app/canvas_view.cpp:340,381,490-497`); the raster-mutation verb copies
  the `scene::set_cell_opacity` mould (`src/scene/cell.cpp:528-548` — resolve →
  reject-camera → `transact` → mutate → `commit`); the on-canvas ring copies the
  `to_screen` draw-list pattern the canvas gizmos use
  (`draw_cell_gizmos`, `src/app/canvas_view.cpp:641`; the grid draw
  `:305-329`); the size mapping `interact::brush_units` already ships
  (`src/interact/ace/interact/interact.hpp:21`); the whole Catch2 + golden + e2e
  + TSan test shape mirrors `editor.cells.gizmo`
  (`tasks/refinements/editor/gizmo.md`).
- **Greenfield:** the pure stroke geometry (dab-center interpolation along a
  drag segment; the log-slider ↔ view-fraction mapping) in L1 `interact`; the
  `scene::brush_dab` transactional verb driving `arbc::RasterContent::paint`; and
  the per-frame coalesced-commit gesture loop (stroke = one journal entry).

No new component, no new DAG edge, no new external dependency, no libarbc change,
**no doc delta** — `interact` is already chartered "brush math" (A8/§8,
`docs/01-architecture.md:294`), `scene` may already include `arbc/*`
(`scripts/check_levels.py:52`), and the raster dab API is live in the pinned
**arbc v0.4.0**.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.canvas.tool_dispatch`** (`tasks/refinements/editor.canvas/tool_dispatch.md`,
  Done 2026-07-29) — the routing seam this leaf plugs into.
  - The closed, compile-exhaustive tool `switch` over `dockmodel::ToolId`
    (`src/app/ace/app/tool_dispatch.hpp:16,23`, no `default`) routes
    `ToolId::Brush → DispatchArm::Brush → CanvasView::dispatch_brush`
    (`src/app/canvas_view.cpp:340,381`). The active tool is threaded in at
    `src/app/shell.cpp:401-402` (`draw_content(view_id, w, h,
    dockspace.tools().active())`).
  - `dispatch_brush` is the **named, inert arm** this leaf fills
    (decl `src/app/ace/app/canvas_view.hpp:391`; stub
    `src/app/canvas_view.cpp:490-497`, currently `(void)p; (void)in;`). Its own
    doc says: "editor.paint.brush fills this arm with the raster-dab stroke math
    … deliberately does NOT fall back to selection (D20: the tool decides the
    drag)."
  - **The always-on viewport gestures run BEFORE the switch, unconditionally**
    (`src/app/canvas_view.cpp:254-330`): wheel-zoom, Space-pan (D9), grid, scale
    bar. They survive under Brush untouched — this leaf must not duplicate or
    break them (D-tool_dispatch-4).
  - The pointer stream is `views::CanvasInput`
    (`src/views/ace/views/views.hpp:49-84`): button EDGES `pressed`/`released`
    and held `down`; `focus_x/focus_y` (cursor rel. image top-left, device px);
    `press_x/press_y` (drag anchor); `shift/alt/ctrl/super`. Produced by
    `views::draw_canvas_interactive` (`views.hpp:91`), read at
    `canvas_view.cpp:254`.
  - `dockmodel::ToolId` / `ToolSelection` (`src/dockmodel/ace/dockmodel/tool_rail.hpp:18,45-52`,
    default `Select`), reachable via `Dockspace::tools()`
    (`src/dock/ace/dock/dock.hpp:466`) — the e2e drives it with
    `dockspace.tools().select(ToolId::Brush)`.
- **`editor.cells.model`** (`tasks/refinements/editor.cells/model.md`, Done
  2026-07-22) — the thing there is to paint into.
  - `scene::add_cell(document, registry, kind_id, config, placement, entered)`
    (`src/scene/ace/scene/cell.hpp:133-136`) mints an `org.arbc.raster` cell from
    config `"<w>x<h>"`; the factory yields a **transparent** `RasterContent`
    (proven in `tests/layers_e2e_test.cpp:210`). `org.arbc.raster` is registered
    on the session registry by `arbc::register_builtin_kinds`
    (`src/commands/app_state.cpp:57`) — no editor allowlist (D-cells_model-1).
  - `scene::cells(document, registry)` (`cell.hpp:248`) reports each `Cell{id,
    layer, kind_id, placement, content_bounds, detail, opacity, visible}`
    (`cell.hpp:215-238`) in z-order. A **raster** cell is classified
    `DetailSource::PaintedRaster` (`cell.hpp:188-192`) through the **generic**
    `arbc::Content::editable()` facet, never a `kind_id` switch or cast
    (`cell.cpp:113`, A16). `content_bounds` numerically **is** the pixel grid:
    1 native px = 1 content unit before placement (D-resolution-1).
  - The `set_cell_opacity` / `set_cell_visible` verbs (`cell.cpp:528-548+`) are
    the exact mould for a new `scene` mutation verb: `dynamic_cast` reject +
    `document.resolve(id)` → `document.transact(name)` → mutate → `commit()`;
    WRITER-THREAD ONLY, never a direct L4 `Document` mutation.
- **`editor.cells.gizmo`** (`tasks/refinements/editor/gizmo.md`, Done 2026-07-29)
  — the load-bearing precedent for the L1/L4 split, the overlay chrome, the
  single-writer commit, and the D8 discipline (the brush is its **dual**: the
  gizmo changes placement, never pixels; the brush changes pixels, never
  placement).
  - The commit discipline through `CanvasView::apply_edit`
    (`src/app/ace/app/canvas_view.hpp:114`) — posts the mutation to the one
    writer thread (A4.1); reads stay lock-free via `pin()`.
  - The transient-preview / on-release-commit shape (`gizmo_cell_*` Presenter
    session fields; `to_screen` overlay draw) — the brush adapts it (D-brush-3).
- **`editor.cells.one_action_one_entry`** (Done 2026-07-28) — one user-visible
  action = one journal entry, one undo press (D15). The proof obligation is
  entry **count** (`journal().depth()` delta) + undo-wholeness, not final state
  (`src/commands/app_state.cpp:101-114`). A continuous brush stroke coalesces to
  **one** entry (D-brush-3).
- **`editor.canvas.edit_render_sync` / `writer_thread`** — A4.1/A4.1a
  (`docs/01-architecture.md:84-123`): every UI-thread `Document` mutation runs
  inside `apply_edit` (writer identity). A brush stroke is a **stream** of small
  writer-thread mutations — the threading surface it owes coverage for.

**Pending (owned here):** the L1 `interact` stroke/size math, the L1
`scene::brush_dab` raster verb, the L4 `dispatch_brush` gesture loop + ring
overlay + brush size/color session state, and the tests. Nothing downstream is
blocked on an unwritten predecessor; both `depends` are Done.

## What this task is

Make the **Brush tool** paint. When the Brush tool is active
(`dockmodel::ToolId::Brush`) and the user drags on the canvas over the selected
`org.arbc.raster` cell, dabs composite into that cell's fixed working grid via
its editable facet, and the whole stroke is **one undo step**:

1. **Brush size = % of view, screen-locked (D5).** A `Size = % of the shorter
   view edge` value (log slider + numeric field, capped at 100%), surfaced as a
   **live outline ring on the canvas** (the ring is the source of truth; the
   number is a handle). Screen-locked only — a canvas-locked mode is **not**
   offered (pre-exec decision; D5).
2. **The stroke → dabs.** A press-drag-release maps the cursor screen path
   through `camera.inverse()` (composition units) and `placement.inverse()`
   (the target cell's content pixels); dab centers are interpolated along each
   drag segment so a fast drag leaves no gaps; each dab is a soft round falloff
   (`arbc::round_dab`) of the brush radius.
3. **Dab into the raster editable facet (D3/§4, maps table `:532`).** Each dab
   calls `arbc::RasterContent::paint(txn, self, region, color, coverage)` inside
   a document transaction — copying only touched tiles (CoW), compositing the
   paint color premultiplied-linear source-over.
4. **One stroke = one undo press (D15).** Every per-frame commit stamps the same
   `AppState::next_gesture_key()` via `Transaction::coalesce`, so libarbc folds
   the whole stroke into **one** journal entry.

The stroke/size math is **pure L1 `interact`**; the raster-mutation verb is
**L1 `scene`** (beside `set_cell_opacity`); the ImGui ring, the pointer read,
the size/color session state, and the per-frame commit loop are **L4 `app`**
(filling `dispatch_brush`).

**Not in scope, by prior decision / WBS split:** the `≈ N px on ‹cell›`
detail-floor / resolution-health readout and the resolution-correct anisotropic
footprint (`editor.paint.paint_res`, `:662`); the "paint a **referenced** photo
→ add a retouch layer above" escalation (`editor.paint.retouch_stack`, `:668`);
the sRGB/HSV picker + eyedropper that *produce* the paint color
(`editor.panels.color`, `:639` — see D-brush-6); routing `ToolSelection` into
the canvas (`editor.canvas.tool_dispatch`, already shipped). **Deliberately not
built** (parking-lot observations, not WBS leaves — D-brush-7): cross-cell
strokes, brush hardness / eraser / blend modes / pressure.

## Why it needs to be done

D5 and §4 make painting *the* crux of being an image editor: "when a brush drags
across a raster cell, what grid do the dabs land in?" (`docs/00-design.md:105`).
`editor.cells.model` made a raster cell *creatable* and `editor.canvas.tool_dispatch`
made the Brush tool *selectable with an inert arm* — but a Brush drag currently
does nothing (`canvas_view.cpp:490-497`). This leaf is what first makes the
editor able to add original raster detail at all, and it lays the
screen→composition→cell footprint mapping and the raster-mutation seam that
`editor.paint.paint_res` (detail-floor readout) and `editor.paint.retouch_stack`
(non-destructive retouch) both build on. It is also the first **writer-thread
mutation stream** in the editor (many small commits per gesture, unlike the
gizmo's single on-release commit), exercising the A4.1 handoff under load.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **D5 — Brush size** (`docs/00-design.md:472-473`): *"`% of view` (shorter edge,
  log slider) + on-canvas ring; "px" appears only as a per-cell effect/health
  readout; **screen-locked**."* Every size acceptance criterion instantiates
  this row.
- **§4 "The painting model"** (`docs/00-design.md:105-158`) — the normative
  detail:
  - *Storage* (`:110-128`): *"The dab is rasterized into the **cell's own fixed
    working grid** (matching `org.arbc.raster` … a fixed tiled grid at a chosen
    resolution). The camera never owns stored detail."* The detail floor + the
    resample escape hatch are surfaced by the **readout** (`editor.paint.paint_res`).
  - *Brush size* (`:130-158`): *"a **live outline ring on the canvas** (the ring
    is the source of truth; the number is a handle) … **% of the shorter view
    edge** … Cap at 100% … **Logarithmic slider** — real brushes live in the
    ~0.3–15% range — with a numeric field … **Screen-locked** … A canvas-locked
    mode … is deliberately **not** offered."* The `≈ 48 px on ‹cell›` secondary
    readout is explicitly *"not the control"* and is `editor.paint.paint_res`'s.
- **D3 — Cell kinds** (`:470`): painted rasters are a first-class cell kind.
- **D8 — Cell scale ≠ resample** (`:475`): the brush's **dual** invariant —
  painting writes into the cell's existing fixed grid at its own resolution; a
  stroke changes **pixels only**, never the placement affine, `content_bounds`,
  or the native resolution.
- **D15 — Undo = library transactions** (`:482`): *"continuous gestures coalesce
  to one step."* A brush stroke is a scene edit and **is** undoable (§9 lists
  `paint` among the transactional scene edits, `:371`).
- **D20 — Tool rail modal set** (`:487`): Brush is one of the four persistent
  pointer modes; *"the tool decides the drag"* (a Brush drag paints, it does not
  select). The maps table (`:532`) binds *Painting → org.arbc.raster editable
  facet (brush dabs)*.

**Governing architecture rows:**

- **§8 / A8** (`docs/01-architecture.md:310-346`, DAG at `:328-342`): `interact`
  is **L1** "hit-test · gizmo · snapping · **brush math** (L1, UI-agnostic)"
  (`:294`), deps `{base, scene}`, **no** ImGui/GL (`scripts/check_levels.py:30`);
  `scene` is L1, deps `{base, project}` (`check_levels.py:29`); only `views`/
  `dock`/`app` may include ImGui (`check_levels.py:44`). The external-include
  seam lists `scene` among the `arbc`-allowed components
  (`check_levels.py:52`) — the regex buckets **all** `arbc/*` includes together
  (`check_levels.py:64`), so a `scene → arbc/kind_raster` include is the **same
  already-declared edge**, not a new one.
- **§9** (`docs/01-architecture.md:350-375`): the layered DoD — L1 brush math →
  Catch2; rendered output → `render_offline` golden; end-to-end (…→**paint**→…)
  → ImGui Test Engine; threading → ASan/TSan.
- **A4 / A4.1** (`:84-123`): writer identity — the commit runs through
  `apply_edit`, reads stay lock-free via `pin()`.
- **A11** (`:426`): `interact` stays pure math (hit-test/gizmo/snapping/brush);
  the `ToolId` branch lives in `app` (tool_dispatch's split). This leaf adds
  brush math to `interact` and the branch body to `app` — the row's intended
  consumer, so **no doc delta**.

**libarbc API surface** (pinned **v0.4.0**, `CMakeLists.txt:25`; fetched under
`build/dev/_deps/arbc-src/`):

- **The raster dab** — `src/kind_raster/arbc/kind_raster/raster_content.hpp`:
  - `CoverageSampler = std::function<float(int gx, int gy)>` (`:92`) — per-pixel
    coverage over level-0 (native) pixel coords.
  - `round_dab(double cx, double cy, double inner_radius, double outer_radius,
    float opacity) -> CoverageSampler` (`:104`) — the reference round-dab
    generator (`kinds.raster_brush_dab`), libm-free and **byte-exact** (the
    property that lets a golden be byte-exact). A HARD dab is
    `inner==outer`; a SOFT dab has `inner<outer`.
  - `RasterContent : public Content, public Editable` (`:297`),
    `kind_id = "org.arbc.raster"` (`:307`). Dab entry point:
    `void paint(Model::Transaction& txn, ObjectId self, const Rect& region,
    const WorkingPixel& color, const CoverageSampler& coverage)` (`:339`); flat
    replace overload (`:341`). "copies only the touched tiles into a new version
    … adds the touched-tile damage to the transaction. The gesture's coalescing
    is the caller's (via `Transaction::coalesce`)" (`:332-333`). `region` is in
    level-0 pixel (content) coords; `WorkingPixel` is premultiplied-linear.
- **The edit seam** — `arbc::Document::transact(std::string name = {}) ->
  Model::Transaction` (`src/runtime/arbc/runtime/document.hpp:289`); reach the
  content by `Content* Document::resolve(ObjectId) const`
  (`document.hpp:379`).
- **Coalescing** — `Model::Transaction& coalesce(CoalesceKey key)`
  (`src/model/arbc/model/model.hpp:601-602`; `commit()` at `:617`); `0` ==
  `k_no_coalesce` (no coalescing). The key generator is
  `AppState::next_gesture_key()` (`src/commands/ace/commands/app_state.hpp:236`,
  monotonic from 1, never returns 0) — documented for exactly "a brush stroke".
- `arbc::Affine` (`src/base/arbc/base/transform.hpp`): `apply(Vec2)`,
  `inverse() -> optional<Affine>`, `map_rect(Rect)`, `max_scale()`;
  `arbc::Rect`/`arbc::Vec2` (`src/base/arbc/base/geometry.hpp`).

**Editor seams this leaf extends:**

- `interact::brush_units(double view_fraction, double view_short_edge_units)`
  (`src/interact/ace/interact/interact.hpp:21`, impl `src/interact/interact.cpp:12`)
  — the shipped `% of view → composition units` mapping (D5). The new log-slider
  mapping and stroke-dab interpolation sit beside it.
- `scene::set_cell_opacity` (`src/scene/cell.cpp:528-548`) — the resolve →
  reject-camera → `transact` → mutate → `commit` mould for `scene::brush_dab`;
  `scene::Cell` / `scene::cells` (`cell.hpp:215-238,248`); `add_cell`
  (`cell.hpp:133-136`).
- `CanvasView::dispatch_brush` (`src/app/ace/app/canvas_view.hpp:391`, stub
  `canvas_view.cpp:490-497`); `apply_edit` (`canvas_view.hpp:114`);
  `Presenter::camera` (`canvas_view.hpp:230`); the overlay `to_screen` draw
  pattern (`draw_cell_gizmos`, `canvas_view.cpp:641`; grid `:305-329`).
- `views::CanvasInput` (`src/views/ace/views/views.hpp:49-84`).
- `scripts/check_levels.py:29-30,52,64` — the DAG + external-include seam
  (already permits `scene`/`interact` → `arbc/*`).

**Predecessor refinements:** `tasks/refinements/editor.canvas/tool_dispatch.md`,
`tasks/refinements/editor.cells/model.md`, `tasks/refinements/editor/gizmo.md`,
`tasks/refinements/editor.cells/one_action_one_entry.md`,
`tasks/refinements/editor/tool_rail.md`.

**Test rigs:** Catch2 units join `ace_tests` (`CMakeLists.txt:254`); ImGui Test
Engine e2e joins `ace_shell_test` (`CMakeLists.txt:319`). Goldens are raw bytes
under `tests/goldens/` compared via `ace_test::compare_golden`. The L1 mould is
`tests/gizmo_test.cpp` / `tests/camera_manip_test.cpp`; the raw-position
mouse-drive recipe is `tests/selection_e2e_test.cpp:257-265` /
`tests/gizmo_e2e_test.cpp`; the real-pool threading suite is
`tests/canvas_host_test.cpp`.

## Constraints / requirements

1. **Fill the existing arm; add no dispatch mechanism.** Implement
   `CanvasView::dispatch_brush(Presenter&, const views::CanvasInput&)`
   (`canvas_view.cpp:490`). Do **not** add a `Tool` vtable, a fifth `ToolId`, or
   a fallback to selection (D20 / D-tool_dispatch-2/3). The always-on
   wheel-zoom/Space-pan/grid outside the switch (`canvas_view.cpp:254-330`) must
   keep working under Brush unchanged.

2. **Stroke + size math is pure L1 `interact`; the raster verb is L1 `scene`;
   only L4 touches ImGui and commits.** New `interact` functions take/return
   `arbc::Vec2`/`arbc::Rect`/`arbc::Affine` + primitives + `double`s, name **no**
   `scene`/`commands`/`dockmodel`/ImGui/GL type. `scene::brush_dab` names arbc +
   `scene` types only. `interact`/`scene` gain no ImGui/GL/SDL include;
   `check_levels` needs **no edit** (the `scene → arbc/*` edge already exists,
   `check_levels.py:52,64`). No new component, no new DAG edge, no doc delta
   (A8/A11).

3. **Screen-locked size; % of the shorter view edge; ring is the source of
   truth (D5, pre-exec decision).** The size is `% of view` (shorter edge),
   capped at 100%, driven by a **logarithmic** slider + a numeric field, and
   drawn as a live on-canvas ring. **No canvas-locked mode and no size-lock
   toggle** — the value is a screen fraction, `interact::brush_units` maps it to
   composition units (never the reverse). The `%↔units` mapping is
   `brush_units`; the `slider_t↔fraction` log mapping is a new pure `interact`
   helper.

4. **Paint into the selection-primary `org.arbc.raster` cell only; no writable
   target ⇒ a no-op (with the ring still shown).** The target is the selection
   `primary()` when its `Cell.detail` is `PaintedRaster` (classified through the
   generic `editable()` facet, `cell.cpp:113` — never a `kind_id` switch). A
   non-raster / referenced / no selection ⇒ the stroke mutates nothing (the ring
   is still drawn as feedback). Cross-cell strokes and the referenced-photo
   escalation are out of scope (D-brush-5; `editor.paint.retouch_stack`).

5. **The footprint maps through the composed affine, nullopt/finite-guarded.**
   Each dab center maps cursor-device → composition (`p.camera.inverse()`) →
   content px (`cell.placement.inverse()`); the brush radius in content px is the
   composition-unit radius (`brush_units`) divided by the placement scale
   (1 content px = 1 native unit, D-resolution-1). A non-invertible camera or
   placement, a zero-area cell, and a non-finite cursor each yield a **safe
   no-op** — **no NaN written to the document**. (The *resolution-correct
   anisotropic footprint* and the `≈ N px on ‹cell›` readout are
   `editor.paint.paint_res`; this leaf ships the isotropic round dab — D-brush-5.)

6. **A stroke previews live and commits per-frame under ONE coalesce key (D15).**
   Unlike the gizmo (chrome preview, one on-release commit), a brush dab actually
   mutates pixels and must land **live** as the user drags. On `in.pressed` open
   one `state_.next_gesture_key()`; each `in.down` frame, interpolate the dabs
   since the last sample and commit them inside one `apply_edit([&]{ … })` that
   opens `document.transact("brush").coalesce(key)` and calls `RasterContent::paint`
   per dab, then `commit()`. Every frame of the stroke shares the key, so
   `journal().depth()` rises by **exactly 1** per stroke and `undo` reverses the
   whole stroke (D-brush-3). An `in.released` with no motion (a single click)
   still deposits one dab and is one entry.

7. **Dabs never enter the composited image except as committed pixels.** The
   ring, and any size/color HUD, are ImGui draw-list overlay/widgets over the
   pane; they never composite into the `Document`. Between frames with no
   committed dab the composite is byte-identical.

8. **The write goes through `apply_edit`; reads are lock-free `pin()`.**
   Resolving the target cell and assembling the footprint are the same per-frame
   `pin()` reads selection/gizmo already perform; the dab commit is the sole
   mutation and takes writer identity (A4.1). This is a **writer-thread mutation
   stream** (many small commits per gesture) — the threading surface the TSan
   case below scopes. No new lock, no new thread.

9. **D8 dual invariance.** A stroke changes **pixels only**: the target cell's
   `scene::Cell::placement`, `content_bounds`, and the content's native
   resolution are byte-identical before and after every stroke (asserted at L1).
   The brush writes into the cell's existing fixed grid at its own resolution —
   it never resamples or re-grids (§4 storage rule).

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella. `diff-cover --fail-under=90` on changed lines; tests ship with the task.

- **Levelization (`check_levels` clean) — the primary structural assertion.** No
  new component and **no `scripts/check_levels.py` edit**: the stroke/size verbs
  are added to L1 `interact` over `arbc` primitives; `scene::brush_dab` uses the
  already-declared `scene → arbc/*` edge (`check_levels.py:52,64`). Asserted by
  inspection + the lint: `src/interact/` and `src/scene/` gain **no**
  ImGui/GL/SDL and no `ace/commands`/`ace/dockmodel` include; only `src/app/`
  (L4) reads `views::CanvasInput`, holds ImGui, and calls `apply_edit`. **No doc
  delta** (A8/A11 already charter this).

- **L1 logic — Catch2 unit** (`tests/brush_test.cpp`, new file joined to the
  `ace_tests` source list at `CMakeLists.txt:254`; naming
  `TEST_CASE("brush: …")`, moulded on `tests/gizmo_test.cpp`):
  - **Size mapping:** the log-slider `slider_t → view_fraction` mapping is
    monotonic, hits the ~0.3–15% working range at sensible slider positions,
    round-trips with its inverse, and clamps at 100%; `brush_units(fraction,
    edge)` scales linearly (a regression-pin on the existing verb).
  - **Stroke interpolation:** dab centers along a drag segment are spaced ≤ the
    configured fraction of the radius (no gaps); the segment endpoints are
    included; a zero-length segment (single click) yields exactly one dab; a
    non-finite endpoint yields an empty dab set (safe no-op).
  - **Footprint mapping:** for a raster cell with a uniform-scaled + translated
    placement, a cursor at a known device point maps to the expected content-px
    center and radius; a non-invertible camera/placement and a zero-area cell
    yield a safe no-op (no dab, no NaN).
  - **`scene::brush_dab` verb:** painting into a resolved `org.arbc.raster` cell
    composites the dab (a `pin()` read of the content's pixels shows the touched
    tiles changed); an **unknown id**, a **camera** content, and a
    **non-raster** cell each return the no-op sentinel and mutate nothing; the
    call is one transaction.
  - **Coalescing → one entry (Constraint 6, at L1):** driving a multi-dab stroke
    (press → N `down` frames → release) against a live `Document`, all commits
    stamped with one `next_gesture_key()`, leaves `journal().depth()` **+1** and
    `undo()` restores the pre-stroke pixels **byte-equal**; two separate strokes
    are two entries and two undo presses.
  - **D8 dual invariance (Constraint 9):** across every stroke above, the target
    `Cell::placement`, `content_bounds`, and the content's native resolution are
    captured before/after and asserted **identical** — pixels moved, placement
    did not.

- **Rendered output — golden.** One new byte-exact `render_offline` golden
  `tests/goldens/brush_dab_64x64.rgba8` (moulded on
  `tests/goldens/gizmo_scale_64x64.rgba8`): a document with one bounded
  `org.arbc.raster` cell is rendered at 64×64 through a fixed camera, a committed
  brush stroke deposits a soft round dab (`round_dab`, a fixed color), and the
  re-render is compared **byte-exact** — proving the dab composites premultiplied-
  linear source-over to exact pixels (byte-exactness is legitimate because
  `round_dab` is libm-free, `raster_content.hpp:104`). A second Catch2 case
  asserts the composite is **byte-identical** between two no-dab frames
  (Constraint 7), distinguishing a render regression from a brush regression.

- **UI e2e — ImGui Test Engine** (`tests/brush_e2e_test.cpp`, new file joined to
  the `ace_shell_test` source list at `CMakeLists.txt:319`; moulded on
  `tests/gizmo_e2e_test.cpp` / `tests/selection_e2e_test.cpp`, reusing
  `ScratchDir`, `pump_until`, and the raw-position mouse-drive recipe
  `selection_e2e_test.cpp:257-265`). Seeds a project with a bounded
  `org.arbc.raster` cell, selects it, sets `dockspace.tools().select(ToolId::Brush)`,
  and drives `"canvas#1/##canvas_nav"` by raw position:
  - **paint + one undo:** press-drag-release over the cell ⇒ a `pin()` read of
    the cell's content shows changed pixels and `journal().depth()` rose by
    **exactly 1**; `undo` restores the exact prior pixels (one press).
  - **ring feedback:** with Brush active the on-canvas ring is drawn at the
    cursor (screenshot baseline where it adds signal); moving the size slider by
    its stable id changes the ring radius; the size stays constant on screen
    across a zoom (screen-locked, D5).
  - **dispatch gating:** switching to `ToolId::Select` and dragging over the cell
    does **not** paint (`journal().depth()` unchanged); switching back to Brush
    paints again.
  - **nav survives (Constraint 1):** **Space held** during a press over the cell
    pans the view and deposits **no** dab (`journal().depth()` unchanged);
    wheel-zoom under Brush still zooms.
  - **no writable target (Constraint 4):** with **no** raster selected (e.g. a
    camera or a non-raster cell as primary), a Brush drag paints nothing and the
    ring is still shown.
  - across the sequence, `journal().depth()` and `pin()->revision()` move by
    exactly the number of committed strokes.

- **Threading (ASan/TSan).** The brush is a **writer-thread mutation stream**.
  One case appended to `tests/canvas_host_test.cpp` drives a rapid stream of
  `apply_edit([]{ … RasterContent::paint under one coalesce key … })` commits on
  the UI thread against a **live rendering** real-pool `CanvasHost`
  (`default_interactive_pool_config()`, the gizmo TSan anchor — a flat raster
  paint, no nested render, so the `arbc-nested-render-worker-detach-race` inline-
  pool caveat does not apply) while `pin()` reads run each frame; asserts
  sanitizer-clean, a stable final pixel state, and that the whole burst is one
  coalesced journal entry. No new lock, no new thread.

- **No deferred WBS leaves.** Everything adjacent is an already-scheduled leaf:
  the detail-floor / resolution-health readout and the resolution-correct
  anisotropic footprint are `editor.paint.paint_res` (`:662`, `depends !brush`);
  the referenced-photo → retouch-layer escalation is `editor.paint.retouch_stack`
  (`:668`, `depends !brush`); the paint **color** source (sRGB picker +
  eyedropper + the premultiplied-linear translation) is `editor.panels.color`
  (`:639`), which writes into the brush's active-color session field within its
  own charter (D-brush-6 — no new leaf). Cross-cell strokes and brush
  hardness/eraser/blend/pressure are **parking-lot observations**, not WBS leaves
  (D-brush-7): no concrete feature is owed now and each would be speculative
  machinery ahead of a consumer.

## Decisions

- **D-brush-1 — Fill the existing `dispatch_brush` arm; the Brush drag paints and
  does not select.** This leaf implements the named inert arm inside the closed
  tool switch (`canvas_view.cpp:340,381,490`); it adds no `Tool` vtable, no
  `ToolId`, no selection fallback. *Rationale:* `editor.paint.brush` **depends
  on** `editor.canvas.tool_dispatch` (`.tji:659`), which already shipped the arm
  and the routing precisely so the paint leaf could fill it (D-tool_dispatch-3);
  D20 says "the tool decides the drag," so a Brush drag paints unconditionally
  and never falls back to Select. This is the reuse-the-existing-seam bias, and
  it differs from `editor.cells.gizmo` (always-on, tool-independent) exactly
  because the gizmo is **upstream** of tool_dispatch while the brush is
  **downstream**. *Alternative rejected:* an always-on brush that paints
  regardless of tool — it contradicts D20 (a plain Select drag must transform,
  not paint) and ignores the routing the dependency exists to provide. **No doc
  delta.**

- **D-brush-2 — Stroke/size math in L1 `interact`, the raster verb in L1 `scene`,
  ImGui + commit in L4 `app`.** The size mapping (`brush_units` + a log-slider
  helper) and stroke-dab interpolation are pure `interact` functions; the
  `scene::brush_dab` verb resolves the raster and drives `RasterContent::paint`
  under a transaction (the `set_cell_opacity` mould); `dispatch_brush` reads
  `CanvasInput`, draws the ring, and commits through `apply_edit`. *Rationale:*
  A8 charters `interact` as "brush math" and the DAG already lets `scene` include
  `arbc/*` (`check_levels.py:52,64`), so the highest-value geometry and the
  mutation verb are reachable in headless Catch2 (the bulk of the coverage) and
  `views`/ImGui-linked code carries only the input read + draw. This is the split
  the gizmo (D-gizmo-1) and camera inspector already follow. *Alternative
  rejected:* put the footprint math and the transaction in L4 `app` beside the
  ImGui read — it strands the most error-prone geometry in ImGui-linked code
  reachable only through the e2e. **No doc delta** (A8/A11 cover it).

- **D-brush-3 — Per-frame coalesced commit, not gizmo-style preview-on-release.**
  The stroke opens one `AppState::next_gesture_key()` on press; each `down` frame
  commits its dabs through `apply_edit` inside `document.transact("brush").coalesce(key)`;
  the shared key folds the whole stroke into one journal entry (D15).
  *Rationale:* a brush dab *is* the pixel mutation — the user must see the mark
  land as they drag, which a chrome-only preview (the gizmo model) cannot show;
  the library's CoW paint (O(touched tiles)) + `Transaction::coalesce` is built
  for exactly this "many commits, one undo" stroke (`raster_content.hpp:332-333`,
  `app_state.hpp:236` names "a brush stroke"). The proof obligation is entry
  **count**, so a coalesced stream is correct by construction (one_action_one_entry
  D-…-5). *Alternative rejected:* accumulate dabs in a scratch buffer and commit
  once on release (the gizmo's preview-as-chrome shape) — it delays all visible
  feedback to release, which is not painting, and it duplicates the CoW/coalesce
  the library already provides. **No doc delta.**

- **D-brush-4 — Screen-locked size, % of shorter view edge, log slider + numeric,
  ring is the source of truth.** The value is a screen fraction (capped 100%),
  mapped to composition units by `brush_units` (never the reverse), with a pure
  `interact` log-slider helper; the on-canvas ring is the live display. No
  canvas-locked mode, no size-lock toggle. *Rationale:* this is D5 and the §4
  prose verbatim, and the pre-exec decision (2026-07-19) explicitly forbids a
  canvas-locked mode ("it contradicts the model's principle … the detail floor
  lives at the cell, not the brush"). *Alternative rejected:* offering a
  canvas-locked (composition-units) brush — expressly ruled out pre-exec and by
  D5. **No doc delta.**

- **D-brush-5 — Target the selection-primary raster cell with an isotropic round
  dab; the detail-floor readout and anisotropic footprint are
  `editor.paint.paint_res`.** The brush paints the selection `primary()` when it
  is a `PaintedRaster` cell (generic `editable()` classification, never a cast
  for identity); no writable target ⇒ a no-op. The dab is a circular
  `round_dab` in content px, radius = composition radius / placement scale.
  *Rationale:* mirroring the gizmo's single-primary model makes the brush
  testable now over the shipped selection, and §4's detail-floor cue (the `≈ N px
  on ‹cell›` readout) plus the resolution-correct anisotropic footprint are
  precisely what `editor.paint.paint_res` (`:662`, "the brush footprint maps
  camera px → composition units → cell px by affine composition … the readout
  doubles as the resolution-health cue") exists to own — this leaf lays the
  functional (isotropic) mapping that leaf refines. *Alternative rejected:*
  build the anisotropic footprint + health readout here — it collapses the
  deliberate `brush`/`paint_res` WBS split and doubles a 3d leaf. *Alternative
  rejected:* hit-test the topmost raster under the cursor instead of using the
  selection — it makes the target implicit and hard to assert, and the
  referenced-photo case is `retouch_stack`'s. **No doc delta.**

- **D-brush-6 — Ship a default paint color now; `editor.panels.color` wires the
  picker later within its own charter.** The brush holds an active paint
  `WorkingPixel` session field defaulting to **opaque black** (premultiplied-
  linear) and reads it in `dispatch_brush`; it builds no color UI.
  *Rationale:* the paint color is orthogonal to the dab pipeline, and
  `editor.panels.color` (`:639`) is chartered as *"the invisible translator
  to/from the library's premultiplied-linear working space"* — producing the
  active `WorkingPixel` and writing it into the brush's field is that task's job,
  not a separate leaf. `editor.paint.brush` and `editor.panels.color` are
  independent siblings (neither `depends` the other; the `m9_editor` gather
  bundles both), so blocking the brush on color would stall a shippable, testable
  dab pipeline for an orthogonal concern. *Alternative rejected:* make the brush
  depend on `editor.panels.color` — it needlessly serializes two independent
  leaves and the brush is fully testable with a fixed color. The cross-task
  wiring (whether the closer adds a `depends editor.paint.brush` edge to the
  color task, or the field lives in a shared home) is a WBS-owner call surfaced
  in the return summary. **No doc delta.**

- **D-brush-7 — One default soft round dab; hardness/eraser/blend/pressure and
  cross-cell strokes are parking-lot observations, not WBS leaves.** The brush
  ships a single soft `round_dab` falloff and paints only the target cell.
  *Rationale:* D5 asks for size + ring + dab and nothing more; the arbc dab API
  supports hard/soft and explicit masks (`raster_content.hpp:99-104`) so these
  are cheap to add **when a consumer asks**, but minting hardness/eraser/blend
  controls or cross-cell stroke distribution now is machinery ahead of a
  requirement. None is a concrete, closeable feature this leaf owes; each is an
  observation for `tasks/parking-lot.md`, not a WBS leaf (per the "never defer an
  audit/revisit task" rule and the "concrete, agent-implementable" bar).
  *Alternative rejected:* pre-registering `editor.paint.hardness` /
  `editor.paint.eraser` leaves — speculative, no design row demands them. **No
  doc delta.**

## Open questions

(none — all decided.) Two items are recorded for human review rather than the
WBS (they are observations, not "audit" tasks), and go to
`tasks/parking-lot.md`:

1. **Where the authoritative active paint color lives, and whether the color task
   needs a `depends editor.paint.brush` edge.** This leaf ships an opaque-black
   default on a brush-owned session field (D-brush-6); whether
   `editor.panels.color` writes that field directly or the field is hoisted to a
   shared home (e.g. beside `dockmodel::ToolSelection`) is a small structural
   call best made when the color panel is refined — it is not a feature this
   leaf owes.
2. **Cross-cell strokes and brush hardness/eraser/blend/pressure** (D-brush-7) —
   real possible features with no current design row or consumer; recorded so a
   human can decide if/when they enter the WBS rather than being encoded as a
   speculative leaf now.

## Status

**Done** — 2026-07-30.

- `src/interact/ace/interact/interact.hpp`, `src/interact/interact.cpp` — added `brush_fraction_from_slider`/`brush_slider_from_fraction` log-slider helpers and `stroke_dabs`/`brush_footprint` pure L1 math (no ImGui/GL/SDL, no scene/commands types).
- `src/scene/ace/scene/cell.hpp`, `src/scene/cell.cpp` — added `scene::brush_dab` raster verb (L1 `scene`, `set_cell_opacity` mould: resolve → reject-camera → `transact` → `RasterContent::paint` → `commit`).
- `src/app/ace/app/canvas_view.hpp`, `src/app/canvas_view.cpp` — filled `CanvasView::dispatch_brush` (on-canvas ring + log-slider/numeric field + per-frame coalesced stroke via `apply_edit`); Presenter stroke state + shared size/color session fields seeded in ctor.
- `CMakeLists.txt` — wired `tests/brush_test.cpp` into `ace_tests` and `tests/brush_e2e_test.cpp` into `ace_shell_test`.
- `tests/brush_test.cpp` (new) — 12 L1 Catch2 cases (4243 assertions): size mapping, stroke interpolation, footprint, `brush_dab` verb, coalesce→one-entry, D8 dual invariance.
- `tests/brush_e2e_test.cpp` (new) — ImGui Test Engine e2e: paint+undo, dispatch gating, no-target no-op, Space/wheel nav-survives, size-field reachable.
- `tests/canvas_host_test.cpp` — TSan anchor: rapid `apply_edit` brush stream against a live real-pool `CanvasHost`, asserts sanitizer-clean + one coalesced entry.
- `tests/goldens/brush_dab_64x64.rgba8` (new) — byte-exact `render_offline` golden for a committed soft round dab at 64×64.
