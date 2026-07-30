# editor.panels.overview — Patterned-fill wireframe overview; z-order occlusion; editable + navigator

## TaskJuggler entry

- **Task:** `editor.panels.overview` (`tasks/00-editor.tji:617-622`, under
  `task panels "Info panels"` at `:588`).
- **Effort:** `4d` · `allocate team`.
- **Depends:** `!layers` (the sibling `editor.panels.layers`) and
  `editor.cameras.model` (`tasks/00-editor.tji:620`).
- **Note (`.tji:621`):** *"The render-free overview: each cell a hatch pattern
  (shared identity with the list), front fills occlude back with always-on-top
  dotted borders (z-order without a list), cameras as frames, the live viewport
  highlighted; editable (drag to place) + navigator + export/shot map. Design:
  D6/§5. … Decided (pre-exec 2026-07-19): hatch density is CONTENT-space (scales/tilts
  with the cell so scale/orientation read for free). The overview is a full viewport
  NAVIGATOR — manipulate the active viewport with an in-overview zoom control
  (partners D2 deep-zoom aids, editor.canvas.nav_aids), NOT navigation-only — and it
  is the placement surface for editor.cells.model inserts (a new cell is placed here).
  Remaining open items are visual polish only (hatch style/opacity,
  pattern-count-before-color, camera visual language)."*
- **Back-link:** the `.tji` note currently ends `Refinement:
  tasks/refinements/overview.md` (the flat interim path). This refinement lands at
  **`tasks/refinements/editor/overview.md`** (the path the orchestrator assigned for
  this leaf). The closer updates the note back-link to that path and adds
  `complete 100` immediately after the `allocate team` line
  (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji` here.
- **Downstream dependents:** none `depends` directly on this leaf, but the
  `capable` milestone lists it explicitly (`tasks/00-editor.tji:699`), and it is
  the surface `editor.cells.model` deferred its *"place the cell in the overview
  wireframe"* gesture to (`.tji:541`), the surface `editor.canvas.nav_aids`
  carved *"the overview's in-panel viewport manipulation + zoom control"* out to
  (`.tji:264`, D24), and the third D17 isolation surface `editor.panels.layers`
  named as *"needs no new task — `editor.panels.overview` consumes the same
  `AppState` scope when it is built"* (`tasks/refinements/editor.panels/layers.md`,
  Deferred WBS).

## Effort estimate

**4 days.** The overview is the richest of the three structure panels, but
almost every model, geometry, selection, and edit seam it needs already ships and
is headless-tested — the four days are concentrated in genuinely new pieces and
their tests: (a) the L4 `OverviewPanel` body — the render-free ImGui draw of every
active-composition cell as a **content-space hatched, dotted-bordered box** with
**front-occludes-back** z-ordering, cameras as frames, and the live viewport rect
— plus its selection/marquee, drag-to-place, and scope-dim reflection; (b) one new
L4 write seam, `CanvasView::drive_focused_camera`, the mirror of the shipped read
seam `focused_framing()` (`src/app/ace/app/canvas_view.hpp:165`), that lets the
overview navigator push a transient camera into the focused canvas pane; (c) two
small **pure L1 `interact` helpers** — a deterministic `overview_pattern` identity
assignment (the shared list↔overview swatch, §5:191-194) and a content-space
`hatch_segments` generator (the load-bearing *"hatch scales/tilts with the cell"*
claim, §5:201) — each unit-tested headless. No new view type (`ViewType::Overview`
is already cataloged, `src/dockmodel/view_registry.cpp:20`), no new component, no
new `check_levels` edge, no new external dependency. The estimate is dominated by
the ImGui draw + the geometry/pick/navigator Catch2 units and the ImGui Test Engine
e2e, not by new architecture.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.panels.layers`** (`tasks/refinements/editor.panels/layers.md`, Done
  2026-07-29) — *the co-primary structure view this leaf partners, and the
  publisher of everything scope-related.* Layers shipped the **entered-composition
  scope** the overview must reflect: `AppState::entered_composition` — a single
  `std::optional<arbc::ObjectId>` (`nullopt` = Root), **transient, non-transactional,
  re-resolved per frame over `pin()`, fail-safe to Root when the id vanishes**
  (D-layers-3, modelled on `look_through`); the scope resolver
  `scene::active_composition(document, entered)` (`src/scene/ace/scene/cell.hpp:272`),
  the composition-parameterised read `scene::cells(document, registry, composition)`
  (`cell.hpp` overload), the derived breadcrumb `scene::composition_path(...)` →
  `std::vector<scene::Breadcrumb>` (`cell.hpp:278,289`), and the z-order verb
  `scene::reorder_cell(document, composition, moved_id, to_index)` over libarbc's
  `Transaction::reorder_layer` (dispatched via `commands::reorder_cell_command` +
  `CanvasView::apply_edit`). The overview reads and drives the same scope, the same
  cells walk, the same breadcrumb, and reuses the same reorder verb for its
  bring-forward/send-back — one scope, one ordering, one edit funnel across list and
  overview (D6). D-layers-3 pins that the Layers and Overview panels are **singletons**
  (`view_registry.cpp:18,20`), so both can only ever reflect **one** scope.
- **`editor.cameras.model`** (`tasks/refinements/cameras/model.md`, Done 2026-07-18)
  — *the cameras the overview draws as frames and manages as the shot map.*
  `scene::cameras(const arbc::Document&)` (`src/scene/ace/scene/camera.hpp:134`)
  returns `scene::Camera{ arbc::ObjectId id; arbc::ObjectId layer; std::string name;
  Resolution resolution; arbc::Affine frame; }` (`camera.hpp:123-129`,
  `Resolution{int width,height}` `:26`). Cameras are the editor's first custom
  `org.arbc.camera` `Content` kind, an `ObjectId`-addressable placed object identical
  in shape to a cell (A14), non-rendering (zero pixels) — which is exactly why §5 draws
  them as a *"distinct frame (not a hatch-filled content box)."* The overview reads
  `cameras()` for the frame/label/shot-map and reuses the `layer` field as the
  `transform_cells_command` target when a frame is dragged.

**Also consumed (transitive, settled):**

- **`editor.cells.model`** (`tasks/refinements/editor.cells/model.md`, Done 2026-07-22)
  — *what there is to draw, and the insert-placement seam this leaf completes.*
  `scene::cells(document, registry)` (`cell.hpp:214`) returns each cell as
  `scene::Cell{ id, layer, kind_id, placement, content_bounds, detail, opacity,
  visible }` in **bottom→top** membership (z) order, `org.arbc.camera` filtered out via
  the seeded `KindBridge`. `placement` (the cell's `arbc::Affine`) and `content_bounds`
  (`std::optional<arbc::Rect>`, added by `editor.cells.selection` D-selection-11) are
  exactly the box the overview strokes and hatches. Insert is
  `scene::add_cell(document, registry, kind_id, config, placement)` over a **finished**
  `arbc::Affine` (never a viewport read, D-cells_model-6), dispatched through
  `commands::insert_cell_command` (`src/commands/ace/commands/cells.hpp:45`); the
  provisional placement is `interact::place_in_view` (`interact.hpp:97`), and A16
  (`docs/01-architecture.md:431`) names *"the seam `editor.panels.overview` later swaps
  a drag-derived affine into."*
- **`editor.cells.selection`** (`tasks/refinements/editor.cells/selection.md`, Done
  2026-07-23) — *the one shared selection and the primitive-only pick core the overview
  feeds.* `commands::Selection` (`src/commands/ace/commands/selection.hpp:18`) on
  `AppState::selection()`, verbs `items/primary/contains/select/add/toggle/clear/
  replace_all/add_all/prune`, shared across canvas, list, and overview (D19/§5:199).
  The pick policy is primitive-only: `interact::PickTarget{id, layer, kind, placement,
  extent}` + `pick`/`pick_stack`/`pick_behind`/`marquee`/`click_selection`/
  `marquee_selection` over `std::span<const PickTarget>` (`src/interact/ace/interact/
  pick.hpp:50,91-182`) — the header comment (`pick.hpp:28`) states *"It names NO scene
  type, so the Overview can feed it"* — and the single assembly adapter
  `interact::pick_targets(const arbc::Document&, const arbc::Registry&)` (`pick.hpp:202`)
  merges cells (layer order) + cameras (above), the *single* place scope-confinement
  (D29) drops out-of-scope targets. `interact` returns a `SelectionChange` **value** L4
  applies (D-selection-2). `Selection::prune(live)` runs per frame so no consumer
  resolves a dangling id (D-selection-7).
- **`editor.canvas.nav` / `editor.canvas.fit_bounds` / `editor.canvas.nav_aids`**
  (`tasks/refinements/editor/nav.md`, `fit_bounds.md`, `nav_aids.md`, all Done
  2026-07-18…23) — *the transient-viewport-camera model and the fit primitive the
  navigator reuses.* The viewport camera is an `arbc::Affine` (composition units →
  device pixels), per-pane transient session state on `CanvasView::Presenter::camera`
  (`canvas_view.hpp:204`), **never a `transact`** (D-nav-1/D15). Geometry is pure L1
  `interact`, `arbc::Affine` in/out: `pan(camera, device_dx, device_dy)` (`:32`),
  `zoom(camera, device_focus, factor)` (`:38`), `fit(w,h,pw,ph)` (`:59`), and the
  **positioned** `fit_region(const arbc::Rect& region, double pane_w, double pane_h)`
  (`:69`, the D-nav_aids-2 generalisation `fit` delegates to). nav_aids’ `.tji:264`
  note and D24 explicitly reserve *"the overview's in-panel viewport manipulation +
  zoom control"* and a list/overview-driven *"fit to this camera"* for **this** leaf,
  *reusing the same `fit_region` primitive*.
- **`editor.canvas.isolation_scope`** (`tasks/refinements/editor.canvas/isolation_scope.md`,
  Done 2026-07-19) — *the sibling that reflects the entered scope on the canvas; the
  dim convention the overview matches.* It composites the entered child and **dims the
  parent** in the canvas render path (golden `isolation_scope_dim_64x64.rgba8`),
  consuming the same `AppState::entered_composition`. The overview is the third D17
  surface: it re-roots to the child's cells and dims the rest with the same visual
  intent (its own ImGui-draw dim, not a libarbc render).

**Pending (owned here):** the L4 `OverviewPanel` body; the L4
`CanvasView::drive_focused_camera` write seam; two pure L1 `interact` helpers
(`overview_pattern`, `hatch_segments`); their Catch2 units, the ImGui Test Engine
e2e, and a TSan case. Every dependency is Done — nothing downstream is blocked on an
unwritten predecessor.

## What this task is

The **composition overview** — the schematic, render-free view of the *whole*
composition that D6 keeps **co-primary** with the layer list over one shared
selection (§5:160-206) — drawn into the already-cataloged singleton
`ViewType::Overview` (`src/dockmodel/ace/dockmodel/view_registry.hpp:19`; descriptor
`{ViewType::Overview, "overview", "Overview", false}`, `view_registry.cpp:20`). It
delivers, across the levels:

1. **The render-free schematic (L4 draw).** Every active-composition cell is drawn as a
   **box at its true relative position / size / rotation** (`scene::Cell::placement`
   over `content_bounds`), filled with an **auto-generated content-space hatch pattern**
   unique to that layer plus a **dotted border**. The whole active composition is
   auto-framed into the panel (minimap semantics, §5:165 — *"a schematic … view of the
   whole composition"*) via `interact::fit_region(bounds, pane)`.
2. **Z-order without a list, via patterned fills (L4 draw + L1 ordering).** Boxes are
   drawn **bottom→top** in `scene::cells` order so a **front cell's fill occludes** the
   one behind it in the overlap, while every **dotted border is drawn on top of all
   fills** so an occluded cell's full extent stays traceable; the hatch is **semi-opaque
   / gapped** so the behind pattern reads faintly (§5:183-189). Reordering flips the
   occlusion live because it is the same `scene::cells` order the list reorders.
3. **Cameras as frames + the shot map (L4 draw).** Every camera is a **distinct frame**
   (not a hatch box, §5:173) at its `frame` placement with a **label** and a
   **look-through** affordance; because cameras are what export, the panel *is* the
   **export / shot map** — every crop and output framing at a glance.
4. **The live viewport, highlighted and draggable (L4 draw + L1 nav).** The focused
   canvas pane's visible composition rect — `focused_framing().camera.inverse()` applied
   to the pane corners (`view_framing.hpp:20`) — is stroked as a highlighted rect that
   the user **drags to drive the camera**; an **in-panel zoom control** and a
   **fit-to-this-camera** click on a camera frame recenter the focused viewport. All
   three write **only the transient viewport camera** (`interact::pan`/`zoom`/`fit_region`
   → the new `CanvasView::drive_focused_camera`): **no journal entry, no dirty, not
   undoable** (D24/D15).
5. **An editable layout surface (L4 + shipped verbs).** The schematic boxes take the
   same **selection** (feed `interact::pick_targets` through the overview transform to
   `click_selection`/`marquee_selection`, apply to the one `commands::Selection`) and the
   same **coarse-arrangement verbs** as the canvas: **move** (drag a box → a new
   placement `arbc::Affine` → `commands::transform_cells_command` via `apply_edit`, one
   journal entry, placement **never** touches resolution, D8) and **z-order**
   (bring-forward / send-back → the shipped `scene::reorder_cell`, watching the occlusion
   flip, §6:265-269). **Painting stays canvas-only.** This is also the **placement
   surface** for a freshly-inserted cell: `editor.cells.model` places it provisionally and
   selects it, and the overview lets the user drag that box to its final spot (a new cell
   is placed here) through the same move verb — no new insert plumbing.
6. **Isolation-scope reflection (L4 + shipped scope).** The overview re-roots to
   `scene::active_composition(document, entered_composition)`, drawing the child's cells
   at full strength and **dimming everything outside** (D17/D29), with the shipped
   `scene::composition_path` breadcrumb (`Root ▸ … ▸ child`) climbing out; cameras are
   **not pickable while entered** (D29) — confinement arrives free because picking goes
   through `interact::pick_targets`.

It deliberately does **not** ship: painting on the schematic (§6 keeps painting
canvas-only); the full 8-handle scale/rotate/shear gizmo on schematic boxes (§6 makes
the overview the *coarse*-arrangement surface, the canvas the *precise* one — the full
gizmo-in-overview is a named follow-up); provenance / referenced-vs-painted styling
(the **list** surfaces the two asset axes, §5:197 / D11; the overview owns *space +
z-read + camera crops*); and the list-side hatch swatch + hover cross-highlight (this
leaf ships the shared *identity* mechanism; the list-side rendering is a named
follow-up).

## Why it needs to be done

D6 makes the wireframe overview *co-primary* with the layer list, and §5 promises a
permanent panel that **subsumes the minimap**, reads z-order at a glance through
patterned fills, manages the multiple cameras as an export/shot map, drives the live
viewport, and is an **editable layout surface**. Today `ViewType::Overview` is a
cataloged-but-empty view: no body is registered, so the panel launcher opens a blank
window and none of D6/§5 is realized. Three shipped leaves point here for their
deferred final surface: `editor.cells.model` deferred *"place the cell in the overview
wireframe"* (`.tji:541`); `editor.canvas.nav_aids` carved out *"the overview's in-panel
viewport manipulation + zoom control"* (`.tji:264`, D24); and `editor.panels.layers`
named the overview as the third D17 isolation surface *"needs no new task … consumes the
same `AppState` scope."* This leaf closes all three and is the last panel the `capable`
milestone lists (`.tji:699`) — the first UI that shows the composition **in space**,
reads stacking order **without a list**, manages the camera **shot map**, and lets a
user **arrange** the layout and **place** a new cell.

## Inputs / context

**Governing design rows (normative — the constitution):**

- `docs/00-design.md:473` **D6** — *"Keep the layer **list** … and add a **wireframe
  overview** (space/z-read/cameras) as **co-primary**, over one shared selection. Z-order
  shown by per-layer **patterned fills** (front occludes back) + always-on-top dotted
  borders; the pattern is a shared list↔overview identity. Multiple cameras are managed
  in the overview, which doubles as an export/shot map. The overview is **editable** (drag
  boxes to place), not navigation-only."* — the headline row; every clause below traces to
  it.
- `docs/00-design.md:160-206` **§5 — The composition overview (wireframe)** — the
  authoritative prose D6 points at: *render-free view of the whole composition,
  subsumes the minimap* (`:165-167`); *cell as a box at true position/size/rotation +
  auto-generated fill pattern + dotted border* (`:170-172`); *camera as a distinct frame
  (not a hatch box) + label + look-through + export/shot map* (`:173-178`); *live viewport
  as a highlighted rect, drag it to drive the camera* (`:179-181`); the **occlusion rule**
  — *front fill occludes, dotted border stays on top, hatching semi-opaque/gapped, reorder
  flips live* (`:183-189`); *shared pattern identity, swatch beside the list row, hover
  cross-highlight, rotated hatch tilts* (`:191-194`); the **ownership split** — *list owns
  order/hierarchy/naming; overview owns space + local z-read + camera crops; both index one
  shared selection* (`:196-199`); *content-space pattern density + editable layout surface
  + zoom control* and the **open polish items reserved for this leaf** — exact hatch style,
  semi-opacity level, pattern-count-before-color, camera visual language (`:201-206`).
- `docs/00-design.md:265-269` **§6 — the editable overview** — *"the same gizmos and verbs
  on the schematic boxes — ideal for **coarse** arrangement … and for editing z-order
  (bring-forward/send-back, watching the occlusion flip), while the canvas is for
  **precise** placement and painting. Both edit the same affines live; **painting stays
  canvas-only**."*
- `docs/00-design.md:469` **D2** / `:88-102` **§3** — the camera/viewport model the frames
  and live-viewport rect rest on; **§3:98-102** names the overview as the interactive
  **viewport navigator (manipulate the viewport, with a zoom control)**; **§3:95-96** —
  *zoom is a scale bar (composition units per pixel), never a "%"* (the discipline the
  in-panel zoom control follows).
- `docs/00-design.md:475` **D8** — *handle-drag changes placement (affine), never
  resolution — non-destructive* — a box-drag in the overview edits the affine only.
- `docs/00-design.md:484` **D17** — a nested cell can be **entered** (double-click →
  isolation: *canvas/list/overview show the child, outside dims, breadcrumb climbs out*);
  Select ≠ expand ≠ enter.
- `docs/00-design.md:496` **D29** — scoped editing: while entered, *insert lands in scope,
  delete/selection/pointer/drag act only on in-scope cells, out-of-scope objects are
  dimmed and interactively locked*, and *cameras are not pickable while entered — climb to
  Root*; enforced at the **single** `interact::pick_targets` adapter.
- `docs/00-design.md:486` **D19** — selection and the shared panels (Inspector/Layers/
  **Overview**) belong to the **project**, not any canvas; the overview indexes the one
  project-level selection, never keeps a copy.
- `docs/00-design.md:490` **D23** / `:491` **D24** — minting a camera is a scene
  transaction while *"look through"* / viewport framing is transient session state; **D24**
  assigns the *overview-driven "fit to this camera"* and the *overview's in-panel zoom
  control* to the **overview navigator (D6/§5), reusing the same fit primitive**, and pins
  that these write *only the viewport camera's transient framing (D15): no journal entry,
  no dirty, not undoable*, refused (camera unchanged) when nothing is bounded to frame.
- `docs/00-design.md:482` **D15** — the transient-vs-scene line: viewport pan/zoom framing
  is transient session state, **not** undoable; a scene edit (move, reorder, insert) **is**.

**Governing architecture rows (normative):**

- `docs/01-architecture.md:308-344` **§8** — the component levelization DAG (table
  `:326-340`): `interact` is **L1** (`base, scene, libarbc`, **no ImGui**), `views` **L3**,
  `app` **L4** (everything). All of L1 is the testable core and *"none of it may `#include
  <imgui.h>`"* (`:342-344`), enforced by `check_levels`.
- `docs/01-architecture.md:346-373` **§9** — the per-leaf DoD: L1 logic → Catch2 unit;
  renders → golden; has UI → an ImGui Test Engine e2e; threads → sanitizer-clean;
  clang-format + build clean (`:357-368`); the four test categories at `:350-355`.
- `docs/01-architecture.md:422` **A7** / `:429` **A14** / `:433` **A18** — project-level
  selection + shared panels; cameras are a placed `Content` kind identical in shape to a
  cell; the UI thread reads writer-owned `Document` structure only through the shipped
  lock-free scene read-seam (`scene::cells`/`cameras`), never a writer-thread-only API.
- `docs/01-architecture.md:431` **A16** — *"placement arrives as a finished `arbc::Affine`
  computed by the pure helper `interact::place_in_view` … which is the seam
  `editor.panels.overview` later swaps a drag-derived affine into … with no change to
  `scene`."*
- `docs/01-architecture.md:432` **A17** — hit-testing lives in L1 `interact`, split into a
  **primitive-only pick policy** (`PickTarget` + `pick*`/`click_selection`/`marquee` over
  `std::span<const PickTarget>`, *"so the Overview can feed it schematic boxes"*) and one
  `interact→scene` assembly adapter `pick_targets`; the pure geometry helpers
  (`place_in_view`, `hit_frame`, …) *"stay primitive-only so `editor.panels.overview` … can
  keep swapping their own affines in."*

**libarbc API surface (consumed via FetchContent):** `arbc::Affine` (compose/inverse/apply,
`max_scale()`); `arbc::Model::Transaction::reorder_layer` / `set_transform` (reached only
through the shipped `scene::reorder_cell` / `transform_cells_command` verbs, never directly
from L4). No new libarbc surface is consumed.

**Editor seams this leaf extends:**

- View registration: `ViewType::Overview` (`view_registry.hpp:19`, descriptor
  `view_registry.cpp:20`); `views::register_view_body(dockmodel::ViewType, views::ViewBody)`
  (`src/views/ace/views/views.hpp:117`, typedef `:111`, dispatch `draw_view` `:123`); shell
  binding pattern `src/app/shell.cpp:373` (Layers) with teardown clear `:521-525`; the
  `set_view_framing` wiring precedent for handing a `CanvasView` accessor to a body
  (`shell.cpp:449`).
- Panel body mould: `app::LayersPanel(commands::AppState&, CanvasView&)` +
  `void draw(std::string_view view_id)` (`src/app/ace/app/layers_panel.hpp:33,36`; body in
  `src/app/layers_panel.cpp`), which reads `state.document()/registry()/selection()/
  entered_composition()` and dispatches edits through `canvas.apply_edit([...]{ command.
  apply(state.document()); })` (`layers_panel.cpp:209-213`). `InspectorPanel`
  (`inspector_panel.hpp:27`) is the same shape.
- Navigation read/write: `CanvasView::focused_framing()` (`canvas_view.hpp:165`) and
  `primary_framing()` (`:158`, impl `canvas_view.cpp:1288`) → `app::ViewFraming{ arbc::Affine
  camera; int pane_w, pane_h; }` (`view_framing.hpp:20`); `indicated_view_id()` (`:184`) —
  the *which pane the verb acts on* rule the new write seam mirrors; the shipped submit path
  `host_.request_camera(id, Affine)` (D-nav-3) the write seam reuses.
- Geometry (L1 `interact`, `interact.hpp` / `pick.hpp`): `pan` `:32`, `zoom` `:38`, `fit`
  `:59`, `fit_region` `:69`, `place_in_view` `:97`, `scale_bar` (`ScaleBar` `:44`);
  `PickTarget` `:50`, `pick`/`pick_stack`/`pick_behind`/`marquee` `:91-113`,
  `selected_extent` `:136`, `click_selection` `:175`, `marquee_selection` `:182`,
  `pick_targets` `:202`; `group_transform` `:419`.
- Scene reads: `scene::cells` (root `cell.hpp:214` + composition overload),
  `scene::Cell` (`placement`, `content_bounds`), `scene::cameras`/`Camera` (`camera.hpp:134,
  123-129`), `scene::active_composition` (`cell.hpp:272`), `scene::composition_path`/
  `Breadcrumb` (`:278,289`), `scene::reorder_cell`.
- Commands + funnel: `commands::Selection` (`selection.hpp:18`),
  `commands::transform_cells_command(std::vector<LayerTransform>)` (`cells.hpp:126`,
  `LayerTransform{layer, placement}` `:109`), `commands::insert_cell_command` (`cells.hpp:45`),
  `commands::reorder_cell_command`; `AppState::entered_composition()`;
  `CanvasView::apply_edit` writer funnel.
- Insert-placement seam: `AppProjectGateway::insert_cell` (`src/app/project_gateway.cpp:311-351`,
  `place_in_view` call at `:340`, the *"overview later swaps a drag-derived affine in right
  here"* comment `:330-333`).

**Predecessor refinements:** `tasks/refinements/editor.panels/layers.md` (scope model,
breadcrumb, reorder verb, panel mould, e2e rig), `tasks/refinements/panels/inspector.md`
(register_view_body pattern, TSan/e2e rigs, golden-N/A justification),
`tasks/refinements/cameras/model.md` (`scene::cameras`), `tasks/refinements/editor.cells/
model.md` (`scene::cells`, insert seam), `tasks/refinements/editor.cells/selection.md`
(shared selection + pick core), `tasks/refinements/editor/nav.md` / `nav_aids.md` /
`fit_bounds.md` (transient camera + `fit`/`fit_region`), `tasks/refinements/editor.canvas/
isolation_scope.md` (the scope-dim sibling).

**Test rigs:** Catch2 `tests/*_test.cpp` appended to `ace_tests` in the root `CMakeLists.txt`
near `:254` (`TEST_CASE("overview: …")`, modelled on `tests/interact_test.cpp` /
`tests/layers_test.cpp`); ImGui Test Engine e2e `tests/*_e2e_test.cpp` appended to
`ace_shell_test` near `:316-352` (offscreen software-GL, modelled on
`tests/layers_e2e_test.cpp` / `tests/inspector_e2e_test.cpp`, driving widgets by stable `###`
id, `pump_until` helper); TSan case in `tests/canvas_host_test.cpp`.

## Constraints / requirements

1. **Levelization (§8) holds with no new edge — the primary structural assertion.** The
   L4 `OverviewPanel` body and the `CanvasView::drive_focused_camera` write seam are the
   only ImGui-facing / L4 additions, in `src/app/` (like `layers_panel.cpp`); the two new
   pure helpers (`overview_pattern`, `hatch_segments`) live in **L1 `interact`** and include
   only `base`/`scene`/libarbc + existing interact headers — **no** ImGui/GL/SDL. No new
   component, no `scripts/check_levels.py` edit (`:21-40` edges unchanged, `:46` imgui stays
   allowed only in views/dock/app). The L1 core gains no UI include.

2. **No new view type; register a body into the cataloged `ViewType::Overview`.** The panel
   supplies its body through `views::register_view_body(ViewType::Overview, body)` and is
   bound in the shell exactly as Layers is (`shell.cpp:373`, cleared at teardown
   `:521-525`); `k_view_type_count` stays 8; the catalog is `editor.dock.view_registry`'s
   territory and is not edited (D-view-registry-3).

3. **Render-free schematic; auto-fit the whole active composition.** The panel draws cells,
   cameras, and the viewport rect with `ImGui::GetWindowDrawList()` primitives — **no
   `render_offline`, no libarbc `Document` composition** (§5:165). One overview transform
   frames the active composition's bounds into the panel rect via `interact::fit_region`
   (minimap semantics); composition↔panel mapping is the `fit_region` affine and its inverse,
   pinned numerically in the L1 unit. The overview keeps **no** free pan/zoom of its own —
   the *whole* composition is always visible (the minimap role); the "zoom control" targets
   the **canvas** viewport (Constraint 7), not a second overview camera (Decisions).

4. **Content-space hatch; front-occludes-back with always-on-top dotted borders.** Each cell
   box is hatched with a **content-space** pattern (density fixed in the cell's content/unit
   space, generated by `interact::hatch_segments(content_bounds, spacing, style)` then mapped
   through `placement` → composition → panel, so the hatch **scales and tilts with the cell**,
   §5:201). Boxes are drawn **bottom→top** in `scene::cells` order so front fills occlude back
   (§5:183-189); the hatch is **semi-opaque / gapped** (not solid) so the behind pattern reads
   faintly; **every dotted border is drawn on top of all fills** so occluded extents stay
   traceable; the fill pattern per layer is assigned by the deterministic
   `interact::overview_pattern(ordinal, count)` — the shared list↔overview identity (§5:191).

5. **Cameras as frames, not hatch boxes; the shot map.** Cameras from `scene::cameras` are
   drawn as **distinct frames** (outline, no content hatch, §5:173) at their `frame` placement
   with a label and a look-through affordance; a camera outline is always-on-top chrome (A17,
   zero-pixel object). No hatch pattern is assigned to a camera.

6. **Selection is shared; reuse the primitive-only pick core.** Overview clicks/marquees map
   panel-space points through the overview transform's inverse to composition space, feed
   `interact::pick_targets`/`click_selection`/`marquee_selection`, and apply the returned
   `SelectionChange` to the one `commands::Selection` in L4 (Replace; Shift = Add; Cmd/Ctrl =
   Toggle / select-behind). Cells are picked by **body**, cameras by **border/label** with
   **click-through interiors** — the same policy the canvas uses; the overview implements no
   picking of its own. The panel keeps no selection copy (D19). `Selection::prune` runs per
   frame (inherited).

7. **The navigator drives only the transient viewport camera; never undoable.** Dragging the
   highlighted viewport rect pans, the in-panel zoom control zooms, and clicking a camera frame
   **fits to that camera** — each computes a new canvas-camera `arbc::Affine` via
   `interact::pan`/`zoom`/`fit_region` against `focused_framing()` and pushes it through the new
   `CanvasView::drive_focused_camera` (which targets the `indicated_view_id()` pane and submits
   via `request_camera`). These write **only** the transient viewport framing: **no
   transaction, no journal entry, no dirty, not undoable** (D24/D15); with no live/sized canvas
   pane, or nothing bounded to fit, the gesture is a **no-op that leaves the camera unchanged**
   (refuse-rather-than-guess, D24). The zoom control follows scale-bar discipline (no phantom
   "%", §3:95).

8. **Editing is move + z-order over the same affines; one transaction each; painting excluded.**
   Dragging a cell box (or a camera frame) previews the new placement as UI-only session state
   and commits **one** `commands::transform_cells_command` on release via `apply_edit` — one
   journal entry, placement **never** touches resolution (D8), kind-agnostic (`LayerTransform`
   covers both cell placements and camera frames). Bring-forward / send-back reuse the shipped
   `scene::reorder_cell` (one entry, occlusion flips live). **No painting** on the schematic
   (§6). Full scale/rotate/shear gizmo-in-overview is out of scope (Decisions).

9. **Placement surface for inserts uses the shipped provisional placement + move.** A
   freshly-inserted cell is placed provisionally by `editor.cells.model`'s
   `interact::place_in_view` and left **selected**; the overview draws that box and lets the
   user drag it to its final spot via the Constraint-8 move verb. No change to
   `AppProjectGateway::insert_cell` or `scene::add_cell`; the "swap a drag-derived affine into
   the gateway insert" (ghost-drag before commit) is a deliberate non-goal here (Decisions).

10. **Isolation-scope reflection re-roots and dims; cameras locked while entered.** The panel
    reads `scene::active_composition(document, entered_composition())` and draws
    `scene::cells(document, registry, active)` at full strength, **dimming everything outside**
    with the same intent as `editor.canvas.isolation_scope` (its own ImGui dim, not a render),
    and renders the shipped `scene::composition_path` breadcrumb; a crumb click sets the scope
    (fail-safe to Root). While entered, out-of-scope objects are dimmed and **not pickable**,
    and **cameras are not pickable** (D29) — enforced free through `pick_targets`, no
    per-gesture scope logic.

11. **Reads use the shipped UI-thread scene read-seam (A18).** The panel reads `scene::cells`/
    `scene::cameras`/`active_composition`/`composition_path` and `focused_framing()` on the UI
    thread exactly as the Layers panel does; it introduces no new `Document`-inspection path
    and calls no writer-thread-only libarbc API off the writer thread. Covered by a TSan case.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`, `:357-368`); `scripts/gate`
green (check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.** No new component
  and no `scripts/check_levels.py` edit. `src/interact/` gains `overview_pattern` and
  `hatch_segments` (pure, libarbc/`base`/`scene` only); `src/app/` gains the `OverviewPanel`
  body and the `CanvasView::drive_focused_camera` method — the only files that gain L4 /
  ImGui-facing code. Asserted by inspection and by the lint.

- **L1 logic — Catch2 unit** (`tests/overview_test.cpp`, new file appended to the `ace_tests`
  source list near `CMakeLists.txt:254`; `TEST_CASE("overview: …")`):
  - **Overview transform round-trip (Constraint 3):** `interact::fit_region(bounds, pane)`
    maps a composition point to a panel point and its inverse maps back within tolerance;
    a degenerate composition/pane yields identity (no NaN, the D-fit_bounds-3 discipline).
  - **Content-space hatch (Constraint 4):** `hatch_segments` count/spacing is invariant to the
    cell's placement **rotation** (generated in content space) and **scales with the content
    extent** — the *"scales and tilts with the cell"* claim, pinned against hand-computed
    expectations for an axis-aligned box and its rotated/uniformly-scaled variants.
  - **Pattern identity determinism (Constraint 4):** `overview_pattern(ordinal, count)` is a
    pure function — same ordinal → same pattern; distinct up to N patterns before the
    documented color fallback engages; the assignment a future list-side swatch reuses.
  - **Z-order draw order (Constraint 4):** the box draw order equals `scene::cells` bottom→top,
    so front occludes back — a pure ordering assertion over a 4-cell composition, agreeing with
    `scene::z_order_position`.
  - **Viewport-rect derivation (Constraint 7):** the visible-composition quad =
    `focused_framing().camera.inverse()` applied to the pane corners, pinned numerically for a
    plain and a dutch-rotated camera.
  - **Navigator camera math (Constraint 7):** "fit to this camera" equals
    `interact::fit_region(camera_frame_rect, focused_pane_size)`; a pan/zoom of the rect equals
    `interact::pan`/`zoom` of the focused camera; an empty/degenerate framing leaves the camera
    unchanged (refuse).
  - **Pick over overview targets (Constraint 6):** a panel-space click mapped through the
    inverse transform to composition, fed to `interact::pick_targets`/`click_selection`,
    produces the same `SelectionChange` the canvas would for the same composition point — cells
    by body, cameras by border with click-through interiors; while entered, out-of-scope targets
    and all cameras are absent (D29 confinement).
  - **Scope reflection (Constraint 10):** overview cells come from
    `scene::cells(document, registry, active_composition(document, entered))`; the derived
    breadcrumb is `Root ▸ … ▸ child` for a two-deep nest and fails safe to `[Root]` when the
    entered id is absent.

- **Rendered output — golden N/A (justified).** The overview draws ImGui chrome (hatch lines,
  dotted borders, camera frames, the viewport rect) over `AppState`/`scene`, **not** a libarbc
  `Document` composition, so there is no `render_offline` image to pin (the layers/inspector
  precedent). Every geometric value is asserted through the L1 units above; a non-byte-exact
  screenshot baseline is captured in the e2e for signal (the visual density this panel adds
  makes the baseline more valuable here than for a text panel, but it is signal-only, not a
  byte-exact gate).

- **UI e2e — ImGui Test Engine** (`tests/overview_e2e_test.cpp`, new file appended to the
  `ace_shell_test` list near `CMakeLists.txt:316-352`, offscreen software-GL; modelled on
  `tests/layers_e2e_test.cpp`, reusing the boot-the-real-shell + `register_view_body(
  ViewType::Overview, …)` rig and driving by stable `###` id — cell boxes carry
  `###ov_cell_<id>`, camera frames `###ov_cam_<id>`, the viewport rect `###ov_viewport`, the
  zoom control `###ov_zoom`, crumbs `###ov_crumb_<id>`):
  - **Schematic renders:** a document with cells + cameras shows a box per cell and a frame per
    camera; row/box count and relative order match placement; no camera drawn as a hatch box.
  - **Select round-trips (Constraint 6, D19):** clicking a cell box sets
    `AppState.selection().primary()` to that cell; clicking a camera frame border selects the
    camera; Shift adds; Cmd/Ctrl toggles / selects-behind for stacked boxes; the canvas/list
    reflect the same selection.
  - **Marquee multi-selects:** a drag over two boxes selects both (excluding unbounded fills).
  - **Drag-to-place round-trips (Constraint 8):** dragging a cell box changes the placement read
    back from `scene::cells`; `undo` restores it in one press; dragging a camera frame changes
    its `frame` and undoes likewise; resolution is unchanged (D8).
  - **Bring-forward / send-back (Constraint 8):** a z-order action reorders `scene::cells` and
    the occlusion flips; `undo` restores.
  - **Navigator drives the canvas, not the document (Constraint 7):** dragging `###ov_viewport`
    changes `focused_framing().camera` while `is_dirty()` and the journal length stay unchanged;
    the zoom control changes the camera; clicking a camera frame's fit affordance frames it; with
    no live canvas the gestures are no-ops.
  - **Insert placement (Constraint 9):** inserting a cell leaves it selected at its provisional
    box; dragging that box in the overview moves it (one journal entry over the insert's).
  - **Enter + dim + breadcrumb + climb (Constraint 10, D17/D29):** double-clicking a nested box
    re-roots the schematic to the child's cells, dims the rest, shows `Root ▸ <child>`, and makes
    cameras unpickable; clicking the `Root` crumb climbs out — entering/climbing arms neither
    Save nor an undo entry.
  - **Empty states:** an empty composition and an empty selection each show a defined empty state
    with no stale boxes.

- **Threading (ASan/TSan).** One case appended to `tests/canvas_host_test.cpp` (over
  `default_interactive_pool_config()`): the UI thread runs the panel's read path
  (`scene::cells(active)` + `scene::cameras` + `active_composition` + `composition_path` +
  `focused_framing`) and a `drive_focused_camera` + an `apply_edit(transform_cells_command)`
  concurrent with a live render walk, TSan-clean — mirroring the layers UI-thread-read case.
  No new lane, no new suppression.

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed lines;
  clang-format + build clean. Tests ship with the task.

- **Deferred WBS work (closer registers each in the WBS, wired under `editor.panels` so it
  rolls up to the `capable` milestone):**
  - **`editor.panels.hatch_swatch`** (~1d, `depends !overview`, under `editor.panels`) — the
    **list-side** of the shared identity: draw the deterministic `interact::overview_pattern`
    swatch beside each Layers-list row and wire the `AppState` **hover cross-highlight** between
    the list and the overview (§5:191-194: *"the same pattern swatch appears beside the layer's
    row … hovering either cross-highlights the other"*). This leaf ships the identity mechanism
    and the overview side in full; the list-side swatch + the cross-panel hover state are a
    small, concrete, agent-implementable follow-up in the shipped `LayersPanel`. Note cites this
    refinement.
  - **`editor.panels.overview_gizmo`** (~2d, `depends !overview`, `editor.cells.gizmo`, under
    `editor.panels`) — the full scale/rotate/shear **transform gizmo on schematic boxes**,
    reusing the shipped `interact` cell-gizmo handle math (`editor.cells.gizmo`) fed through the
    overview transform. §6 makes the overview the *coarse*-arrangement surface (move + z-order,
    shipped here) and the canvas the *precise* one; the full gizmo-in-overview is real,
    separable draw+dispatch work, not a deferral of this leaf's headline (the schematic, the
    navigator, selection, drag-to-place, and scope reflection all ship here). Note cites this
    refinement.

## Decisions

- **D-overview-1 — Register a body into the existing `ViewType::Overview`; no ninth view type;
  the body is L4 `app`.**
  The panel plugs into the cataloged singleton `ViewType::Overview` through
  `views::register_view_body`, and the body class `app::OverviewPanel(AppState&, CanvasView&)`
  lives in `src/app/` beside `LayersPanel`/`InspectorPanel`.
  *Rationale:* the catalog already reserves `Overview` (`view_registry.cpp:20`) and the shipped
  panel precedent puts bodies in L4 `app` (not L3 `views`, which holds only the registration seam
  + shared draw primitives); the six-line register-and-bind pattern is shipped and TSan-covered.
  *Alternative rejected:* a standalone "Cameras" view — D6 keeps cameras *managed in the
  overview*, and the catalog has no `Cameras` enumerator. **No doc delta required.**

- **D-overview-2 — The overview has one transform (whole-composition fit); the "zoom control"
  drives the CANVAS viewport, not a second overview camera.**
  The panel always frames the whole active composition into its rect via
  `interact::fit_region` (minimap semantics); the in-panel zoom control, the draggable viewport
  rect, and "fit to this camera" all recenter the **focused canvas** viewport camera.
  *Rationale:* §5:165 makes the overview *"a schematic … view of the **whole** composition"* that
  *"subsumes the minimap"* — the whole thing is meant to be visible at once; §3:98-102 and D24
  name the overview as the *viewport navigator* whose zoom control *manipulates the viewport*
  (the canvas camera), reusing the fit primitive. One transform keeps the panel simple and keeps
  the *only* mutable camera the transient canvas one (D15/D-nav-1), so there is no second zoom to
  reconcile and no ambiguity about which camera the control drives.
  *Alternative rejected:* giving the overview its own free pan/zoom (a second transient camera).
  Rejected because it contradicts the whole-composition minimap role, doubles the transient
  camera state, and makes the D24 zoom control ambiguous. A future "zoom into a dense region of
  the schematic" is a legible follow-up if the whole-fit ever proves too coarse, not a v1 need.
  **No doc delta required** — applies §5 + §3 + D24.

- **D-overview-3 — The navigator pushes the transient viewport camera into the focused pane
  through a new L4 `CanvasView::drive_focused_camera`, the write-mirror of the shipped
  `focused_framing()` read.**
  The overview reads `focused_framing()` (camera + pane size), computes a new camera with
  `interact::pan`/`zoom`/`fit_region`, and calls `drive_focused_camera(Affine)`, which targets
  the `indicated_view_id()` pane, sets its transient `Presenter::camera`, and submits via
  `host_.request_camera` — a no-op when no live/sized canvas exists.
  *Rationale:* the read seam already exists and is consumed by the rail's framing-derived verbs
  (`shell.cpp:449`); a symmetric write seam is the minimal, levelization-safe way for one L4
  panel to drive another L4 panel's transient state (both are `app`; no new DAG edge, no
  `interact→app`/`app→interact` cycle, geometry stays pure in `interact`). Routing through the
  `indicated_view_id()` rule keeps *which pane the overview drives* identical to *which pane the
  focused-canvas marker names* (D-focused_canvas_indicator), so the two can never disagree.
  Writing transient framing only (never a `transact`) honors D24/D15.
  *Alternative rejected:* letting the overview mutate a shared camera field directly, or
  threading the camera through the `ProjectGateway`. Direct field access breaks `Presenter`'s
  encapsulation and the per-pane submit dedup; the gateway is for **document edits** (writer-thread
  funnel), and a transient viewport nudge is neither a document edit nor writer work — routing it
  there would misuse the funnel. **No doc delta required** — a leaf-level L4 seam applying D24,
  the same class of decision as `focused_framing()`/the focused-canvas indicator.

- **D-overview-4 — Per-cell visual identity in the overview is the content-space hatch pattern,
  assigned by a pure L1 helper; provenance / referenced-vs-painted styling is the list's, not the
  overview's.**
  `interact::overview_pattern(ordinal, count)` deterministically assigns a hatch pattern per
  layer, and `interact::hatch_segments(content_bounds, spacing, style)` generates the hatch in
  content space (mapped through `placement` so it tilts/scales with the cell). The overview does
  **not** style boxes by owned/borrowed or painted/referenced.
  *Rationale:* §5:191-194 makes the pattern the shared identity, and §5:201 makes its density
  content-space; §5:196-199 splits ownership so the **list** surfaces the two asset axes (D11
  *"the list surfaces both"*) while the **overview** owns *space + local z-read + camera crops*.
  Keeping the assignment a pure L1 function lets the deferred list-side swatch
  (`editor.panels.hatch_swatch`) reuse the exact identity with zero forking, and keeps the
  content-space claim headless-testable (rotation-invariant count). A color fallback engages past
  N distinct patterns (the §5:204 *"pattern-count-before-color"* open item), with a defensible
  default here and the exact threshold surfaced as visual polish.
  *Alternative rejected:* adding provenance styling to the overview — it duplicates the list's D11
  job against §5's ownership split, and would fork the inspector/list `describe_detail_source`
  vocabulary the layers leaf already owns. **No doc delta required.**

- **D-overview-5 — Editing is move + z-order over the same shipped verbs; the full gizmo and
  in-place ghost-drag insert are out of scope.**
  Dragging a box commits `commands::transform_cells_command` (one entry, kind-agnostic over
  cells and camera frames, placement-only per D8); bring-forward/send-back reuse
  `scene::reorder_cell`. A freshly-inserted cell is placed via its shipped provisional
  `place_in_view` box and then dragged with the same move verb.
  *Rationale:* §6:265-269 makes the overview the **coarse**-arrangement + z-order surface (the
  canvas is *precise*), and *"both edit the same affines live"* — reusing the shipped transform +
  reorder verbs keeps one edit funnel and one journal semantics across canvas, list, and overview.
  Placing the just-inserted selected cell by drag satisfies §5/`.tji`'s *"a new cell is placed
  here"* with **no** change to `AppProjectGateway::insert_cell` or `scene::add_cell`.
  *Alternative rejected:* (a) the full 8-handle scale/rotate/shear gizmo on schematic boxes —
  substantial draw+dispatch work that would blow the 4d leaf; §6 reserves precise transforms for
  the canvas, so it is a clean follow-up (`editor.panels.overview_gizmo`). (b) A ghost-drag that
  swaps a drag-derived affine into the gateway insert *before commit* (A16's literal seam) —
  correct-sounding but it needs a live pre-commit placement interaction for a cell that does not
  yet exist; the shipped provisional-placement + post-insert move delivers the same user-facing
  promise with a smaller, safer diff and no gateway change. The literal A16 swap remains available
  as a future refinement without contradicting anything here. **No doc delta required.**

- **D-overview-6 — Scope reflection re-roots + dims + breadcrumbs by consuming the shipped scope
  model; scope confinement comes free from `pick_targets`.**
  The overview reads `AppState::entered_composition`, walks
  `scene::cells(document, registry, active_composition(...))`, dims outside with the
  `editor.canvas.isolation_scope` intent, and renders the shipped `composition_path` breadcrumb;
  picking through `interact::pick_targets` inherits D29's out-of-scope drop and camera-lock.
  *Rationale:* D17 lists the overview as the third isolation surface, and the layers leaf
  explicitly stated the overview *"needs no new task — consumes the same `AppState` scope."*
  Reusing the single `pick_targets` adapter is exactly D29's *"the **single** pick assembly
  adapter … drops out-of-scope targets … without per-gesture logic"* — the overview adds no scope
  branching of its own.
  *Alternative rejected:* a second scope model or per-gesture scope filtering in the overview — it
  would fork the one project-level scope the singleton panels must share and re-litigate a settled
  seam. **No doc delta required.**

## Open questions

(none — all decided.) Two items are surfaced to the parking lot in the return summary rather than
encoded as WBS leaves, because they are human/design taste calls, not agent-implementable
specifications: (1) the **§5:204-206 visual-polish opens explicitly reserved for this leaf** — the
exact hatch style and semi-opacity level, the pattern-count threshold before color must carry the
load, and the camera visual language — for which this leaf ships defensible provisional defaults
that a design pass can retune without touching the model or the geometry; and (2) the
**keyboard/gesture bindings** (double-click-to-enter, the zoom-control chord, fit-to-camera) which
stay provisional pending the full input map (§11 is still open in `docs/00-design.md`), shipping
shipped-idiom gestures the input-map owner can later rebind without touching the navigator seam.

## Status

**Done** — 2026-07-30.

- New L1 pure helpers in `src/interact/ace/interact/interact.hpp` + `src/interact/interact.cpp`: `overview_pattern` (deterministic per-layer hatch identity, shared with list-side swatch) and `hatch_segments` (content-space hatch-line generator with `HatchStyle`/`OverviewPattern`/`HatchSegment` types).
- New L4 write seam `CanvasView::drive_focused_camera` in `src/app/ace/app/canvas_view.hpp` + `src/app/canvas_view.cpp` — write-mirror of `focused_framing()`, pushes a transient viewport affine into the focused pane via `request_camera`; includes a test-seam ctor accepting an injected `WorkerPoolConfig` for deterministic e2e scaffolding (avoids arbc v0.4.0 worker-detach race in the overview e2e).
- New L4 panel body `app::OverviewPanel` in `src/app/ace/app/overview_panel.hpp` + `src/app/overview_panel.cpp` — schematic draw (hatch boxes + dotted borders + camera frames + live viewport rect), shared selection via `interact::pick_targets`, drag-to-place (`transform_cells_command`), bring-forward/send-back (`reorder_cell`), navigator (`drive_focused_camera`), isolation-scope re-root + dim + breadcrumb.
- Shell registration and teardown in `src/app/shell.cpp` (mirrors Layers panel pattern).
- `CMakeLists.txt`: two new test files registered.
- Unit tests: `tests/overview_test.cpp` — 8 `TEST_CASE("overview: …")` cases covering round-trip transform, content-space hatch invariants, pattern determinism, z-order draw order, viewport-rect derivation, navigator camera math, pick-over-overview-targets, and scope reflection.
- E2e test: `tests/overview_e2e_test.cpp` — ImGui Test Engine e2e with inline `WorkerPoolConfig{}` settle-fully pool (race-free scaffold).
- TSan anchor: `tests/canvas_host_test.cpp` — new case `editor.panels.overview TSan anchor` over `default_interactive_pool_config`.
- Shell smoke hardened: `tests/shell_smoke_test.cpp` — `remove_all` before/after + `REQUIRE(is_regular_file)` premise assert, fixing a stale-path false-pass on dev machines with shared `/tmp`.
- Deferred WBS: `editor.panels.hatch_swatch` and `editor.panels.overview_gizmo` registered in `tasks/00-editor.tji`.
