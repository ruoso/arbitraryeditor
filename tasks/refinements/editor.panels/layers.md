# editor.panels.layers — Layers list (+ cameras); reorder; referenced-vs-painted; nested expand / enter

## TaskJuggler entry

- **Task:** `editor.panels.layers` (`tasks/00-editor.tji:596-600`, under
  `task panels "Info panels"` at `:588`).
- **Effort:** `3d` · `allocate team`.
- **Depends:** `!inspector` (the sibling `editor.panels.inspector`) and
  `editor.cells.model` (`tasks/00-editor.tji:599`).
- **Note (`.tji:600`):** *"The layer list (cameras section + layers
  front->back); reorder = z-order; the borrowed/owned (referenced-vs-painted)
  distinction shown; a nested cell can be expanded (peek) or entered (isolation
  scope + breadcrumb). Design: D6/D11/D17."*
- **Back-link:** the `.tji` note currently ends `Refinement:
  tasks/refinements/layers.md` (the flat interim path). This refinement lands at
  **`tasks/refinements/editor.panels/layers.md`** (the path the orchestrator
  assigned for this leaf, matching the `editor.panels` area subdir the sibling
  `inspector` also uses). The closer updates the note back-link to the real path
  and adds `complete 100` immediately after `allocate team`
  (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji` here.
- **Downstream dependents:** `editor.panels.overview` (`tasks/00-editor.tji:602-606`,
  `depends !layers`) — the wireframe overview is the co-primary structure view
  (D6) that shares this list's z-order vocabulary, its patterned-fill cell
  identity, and (per Decisions) the **entered-composition scope** this leaf
  publishes.

## Effort estimate

**3 days.** The panel plugs into a shipped seam and reuses a shipped read
vocabulary, so the cost is concentrated in three genuinely new pieces and their
tests: (a) one new L1 `scene` write verb (`reorder_cell`, minted as a mirror of
`scene::set_camera_resolution`, `src/scene/camera.cpp:565`) over the v0.4.0
`Transaction::reorder_layer` facet, plus its `commands::reorder_cell_command`
wrapper; (b) two small L1 `scene` reads — a composition-parameterised
`cells(document, registry, composition)` overload (a one-argument
generalisation of the existing root-only walk) and `nested_composition_of` to
resolve a nested cell to its child composition — that make **expand** and
**enter** data-drivable; (c) the project-level **entered-composition** session
state on `AppState` plus the breadcrumb, modelled on the transient,
fail-safe-on-vanish session-state pattern `editor.cameras.look_through`
established. The L4 `LayersPanel` body itself is the shipped
`register_view_body(ViewType::Layers, …)` six-line pattern
(`src/app/shell.cpp:358-363`). No new component, no new DAG edge, no new
external dependency; the estimate is dominated by the reorder-math + scope
Catch2 units and the ImGui Test Engine e2e, not by new architecture.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.panels.inspector`** (`tasks/refinements/panels/inspector.md`, Done
  2026-07-29) — *the readout vocabulary this list must not fork.* The inspector
  established the **z-order readout** (`scene::z_order_position`,
  `src/scene/ace/scene/cell.hpp:231`, returning `{index, count}` over the
  back→front `cells()` order) and the **provenance readout**
  (`scene::describe_detail_source` over `scene::CellDetail::source`,
  `cell.hpp:168-186`) and staked D-inspector-4: *interactive reorder and
  referenced-vs-painted list styling are the layers list's, not the
  inspector's.* This leaf is the owner the inspector deferred to — it reuses
  `DetailSource`/`describe_detail_source` for the styling and turns the
  `z_order_position` readout into an interactive drag, keeping one ordering and
  one provenance vocabulary across the two panels (D6).
- **`editor.cells.model`** (`tasks/refinements/editor.cells/model.md`, Done
  2026-07-22) — *what there is to list.* `scene::cells(document, registry)`
  (`cell.hpp:214`, impl `src/scene/cell.cpp`) returns every root-composition
  cell in bottom→top layer (z) order as `scene::Cell{ id, layer, kind_id,
  placement, content_bounds, detail, opacity, visible }` (`cell.hpp:183-204`),
  with `org.arbc.camera` layers filtered out through the seeded `KindBridge`
  (D-cells_model-8) and an unresolvable token surfaced as an **empty
  `kind_id`** rather than dropped. Nested cells are the `org.arbc.nested` kind
  (D3) whose config is a child-composition `ObjectId` (D-cells_model, the
  `CanvasRenderer` `DocumentBinding` from
  `editor.canvas.nested_composition_binding`). The classification is by generic
  `arbc::Content` facet, never a `kind_id` string-switch (A16 /
  D-resolution-2), so a foreign/plugin kind lists and classifies automatically.

**Also consumed (transitive, settled):**

- **`editor.cells.selection`** (`tasks/refinements/editor.cells/selection.md`,
  Done 2026-07-23) — the one project-level `commands::Selection`
  (`src/commands/ace/commands/selection.hpp:17-43`) on `AppState::selection()`
  (`app_state.hpp:138`), an ordered set of `arbc::ObjectId` shared across
  canvas, list, and overview (D19). This leaf's rows **reflect and drive** that
  one selection (Select ≠ expand ≠ enter, D17). Selection.md explicitly handed
  D17's expand/enter/breadcrumb to **this** leaf (three times: its "Not in
  scope", its Inputs, its Deferred WBS — *"nested expand/enter + breadcrumb
  (D17) is `editor.panels.layers`"*), and its per-frame `Selection::prune(live)`
  (D-selection-7) guarantees a listed/selected id is never dangling.
- **`editor.cameras.model`** (`tasks/refinements/cameras/model.md`, Done
  2026-07-18) — `scene::cameras(document)` (`src/scene/ace/scene/camera.hpp:134`)
  returns `scene::Camera{ id, layer, name, resolution, frame }`
  (`camera.hpp:113-119`) for the cameras section. Cameras are placed objects of
  the editor's first custom kind `org.arbc.camera` (A14); the model refinement
  parked *"the first camera e2e lands with the first consuming UI leaf
  (`editor.panels.layers`)"* — this is that leaf.
- **`editor.cameras.look_through`** (`tasks/refinements/editor/look_through.md`,
  Done 2026-07-19) — the **precedent for transient, non-persisted session
  state**: `std::optional<arbc::ObjectId>` re-resolved per frame over `pin()`,
  never a transaction, fail-safe when its target id vanishes (D-look_through-1/-7,
  `src/app/ace/app/canvas_view.hpp:117`). The entered-composition scope this
  leaf introduces follows that shape (Decisions).

**Pending (owned here):** the L4 `LayersPanel` body; one L1 `scene::reorder_cell`
verb + its `commands::reorder_cell_command`; the `scene::cells(…, composition)`
overload and `scene::nested_composition_of` reads; the `CellDetail` borrowed/owned
axis; the `AppState` entered-composition session state + breadcrumb path helper;
their Catch2 units, the ImGui Test Engine e2e, and a TSan case. Nothing
downstream is blocked on an unwritten predecessor — every dependency is Done.

## What this task is

The **Layers panel** — the order/hierarchy/naming structure view D6 keeps as
**co-primary** with the overview over one shared selection — drawn into the
already-cataloged `ViewType::Layers` (`src/dockmodel/ace/dockmodel/view_registry.hpp:19`;
singleton descriptor `{ViewType::Layers, "layers", "Layers", false}`,
`src/dockmodel/view_registry.cpp:18`). It has two sections:

1. **Cameras section** — a flat list of `scene::cameras(document)` (name +
   resolution), each row selectable into the shared selection. Cameras are
   sinks that emit no pixels and carry no z-order (A14), so the section is
   **not reorderable**; it is presented in the canonical camera order
   (ascending `ObjectId`, matching `scene::cameras`). D6 keeps cameras managed
   primarily in the overview; the list surfaces them so a camera is selectable
   from the structure view the same way a cell is (D7).

2. **Layers section** — every root-composition cell, drawn **front→back**
   (top-of-list = frontmost = top-of-z). `scene::cells` returns bottom→top
   membership order, so the list renders its reverse. Each row shows the cell's
   name, its **referenced/owned/painted** provenance affordance (D11, reusing
   `DetailSource`), and — for an `org.arbc.nested` cell — a **disclosure
   triangle** to expand.

The panel adds three interactions over that list:

- **Reorder = z-order.** Dragging a layer row to a new position rewrites the
  cell's place in the composition's ordered membership — one undoable scene
  transaction — via the new `scene::reorder_cell` verb over libarbc's
  `Transaction::reorder_layer` (`arbc/model/model.hpp:530`). Reorder targets the
  **active composition** (root, or the entered nested composition when
  isolated).
- **Expand (peek).** A nested-composition row can be expanded to peek its
  children **read-only, still editing the parent** — selection and edits stay
  on the parent scope; expanding is neither selecting nor entering (D17). The
  child rows come from `scene::cells(document, registry, child_composition)`.
- **Enter (isolation scope + breadcrumb).** Double-clicking a nested cell
  **enters** it: the panel sets the project-level **entered-composition** scope,
  the list re-roots to the child's cells, and a **breadcrumb** (`Root ▸ … ▸
  child`) renders at the top; clicking a crumb climbs back out. The scope is
  transient session state (not persisted, not a transaction, D15) and
  fail-safe: if the entered composition is GC'd/undone away, the scope
  re-resolves to Root next frame.

## Why it needs to be done

D6 makes the layer **list** and the wireframe **overview** *co-primary*
structure views over one shared selection, and D17 promises that a nested
composition can be **expanded** or **entered**. Today `ViewType::Layers` is a
cataloged-but-empty view: no body is registered, so the panel launcher opens a
blank window. The inspector deliberately shows z-order and provenance as
**readouts only** and pointed here for the interactive versions
(D-inspector-4); the selection leaf deliberately excluded D17's
expand/enter/breadcrumb and pointed here for all of it. This leaf closes both
gaps — it is the first UI that lets a user **re-stack** cells and **navigate the
composition hierarchy**, and it is the direct predecessor of
`editor.panels.overview`, which reuses this list's z-order/provenance vocabulary
and consumes the entered-composition scope this leaf publishes.

## Inputs / context

**Governing design rows (normative — the constitution):**

- `docs/00-design.md:473` **D6** — *"Keep the layer **list**
  (order/hierarchy/naming) and add a **wireframe overview** … as **co-primary**,
  over one shared selection."* The list is this panel; z-order is shown by the
  order itself; cameras are managed primarily in the overview but surfaced here.
- `docs/00-design.md:474` **D7** — one object shape, one select tool; a camera
  row and a cell row select identically (kind-agnostic selection).
- `docs/00-design.md:478` **D11** — the two asset axes: **editable?** (painted
  raster vs referenced image) and **bytes where?** (owned in `assets/` vs
  borrowed external file); *"The list surfaces both."* Photo = borrowed +
  read-only; paint = owned + editable; paste = owned + read-only.
- `docs/00-design.md:484` **D17** — *"A nested-composition cell can be
  **expanded** (peek children, still editing parent) or **entered**
  (double-click → isolation scope: canvas/list/overview show the child, outside
  dims, breadcrumb climbs out). Select ≠ expand ≠ enter."* — the headline row.
- `docs/00-design.md:224-228` **§6** — the same promise in prose: *"expanded in
  the list to peek its children (still editing the parent) or double-clicked to
  enter — an isolation scope where canvas, list, and overview show the child's
  cells, everything outside dims, and a breadcrumb (Root ▸ … ▸ child) climbs
  back out."* Selection is *"shared across canvas, list, and overview."*
- `docs/00-design.md:485` **D18** — Canvas-is-a-view uniform dockspace; the
  Layers view is one relocatable view among the catalog; layout is local UI
  state, not `project.arbc`.
- `docs/01-architecture.md:422` **A7** / `:428` **A13** — selection and shared
  panels are **project-level** (one `Document`, one `AppState`); a canvas keeps
  no selection state. The entered-composition scope is therefore project-level
  session state, not per-canvas (Decisions).
- `docs/01-architecture.md:429` **A14** — cameras persist as the editor's first
  custom `Content` kind and are placed objects the same shape as cells; they
  contribute zero pixels (no z-order).
- `docs/01-architecture.md:431` **A16** — Registry-driven, **no kind
  allowlist**: provenance/nested classification reads generic facets, never a
  `kind_id` string-switch. The list's referenced-vs-painted styling obeys this.
- `docs/01-architecture.md:432` **A17** — hit-testing lives in L1 `interact`;
  the list does **not** re-implement picking, it drives the shared `Selection`
  directly with L4 apply (as the canvas does).
- `docs/01-architecture.md:433` **A18** — the UI thread reads writer-owned
  `Document` structure only through the shipped scene read-seam / a published
  snapshot; the panel reads `scene::cells`/`scene::cameras` on the UI thread
  exactly as the inspector does (Constraints, TSan case).
- `docs/01-architecture.md:308-344` **§8** levelization DAG (component table
  `:326-340`); `:346-373` **§9** the per-leaf DoD; `:375+` **§9.1** the
  offscreen software-GL ASan lane.

**libarbc API surface (consumed via FetchContent):**

- `arbc::Model::Transaction::reorder_layer(composition, from_index, to_index)`
  (`arbc/model/model.hpp:530`) — *"Move the member at `from_index` to
  `to_index` (a stable move …). Damages the composition once. **No-op if the
  composition is absent, either index is out of range, or the indices are
  equal.**"* Membership order is **bottom→top** (`doc 01:6-11`). Opened via
  `arbc::Document::transact` (`arbc/runtime/document.hpp`).
- `arbc::Content::external_asset_ref()` — the generic facet the borrowed/owned
  axis reads (present ⇒ borrows an external file; D11's *"bytes where?"*),
  already the basis of `scene::classify_detail`'s `ReferencedImage`
  determination (`cell.hpp:162-173`).

**Editor seams this leaf extends:**

- View registration: `ViewType::Layers`
  (`src/dockmodel/ace/dockmodel/view_registry.hpp:19`, singleton descriptor
  `view_registry.cpp:18`); `views::register_view_body(dockmodel::ViewType,
  views::ViewBody)` (`src/views/ace/views/views.hpp:117`, typedef `:111`,
  dispatch `views::draw_view` `:123`); shell binding pattern
  `src/app/shell.cpp:358-363` (teardown clears at `:512`).
- Scene reads: `scene::cells` (`cell.hpp:214`), `scene::Cell` (`cell.hpp:183-204`),
  `scene::CellDetail`/`DetailSource` (`cell.hpp:168-186`), `scene::z_order_position`
  (`cell.hpp:231`), `scene::describe_detail_source` (shipped by inspector),
  `scene::cameras` (`camera.hpp:134`), `scene::Camera` (`camera.hpp:113-119`).
  The composition walk `state->for_each_layer_in(composition, fn)` and
  `scene::root_composition(state)` (`src/scene/cell.cpp:54`, used at
  `cell.cpp:253,290,364`) are already the internals of `cells()` — the
  composition-parameterised overload is a one-argument generalisation of that
  existing walk.
- Write mould: `scene::set_camera_resolution` (decl `camera.hpp:177`, impl
  `src/scene/camera.cpp:565`) — reject-degenerate → resolve → `document.transact(name)`
  → in-place mutate → `txn.commit()` → `bool`.
- Command + dispatch mould: `commands::remove_cells_command`
  (`src/commands/ace/commands/cells.hpp:79`) and `transform_cells_command`
  (`:117`) — a `Command` moved-in, run synchronously on the writer thread, one
  transaction / one journal entry.
- Edit funnel: `app::CanvasView::apply_edit`
  (`src/app/ace/app/canvas_view.hpp:103`) — posts the closure to the single
  document writer thread, blocks, wakes canvases (D-writer_thread-11); the L4
  body dispatches the reorder command through it (never a direct `arbc::Document`
  mutation from L4).
- Selection: `commands::Selection` (`selection.hpp:17-43`) on
  `AppState::selection()` (`app_state.hpp:138`) — `select`/`add`/`toggle`/`items`/`primary`.
- Session-state precedent: `app::CanvasView::Presenter` per-canvas transient
  state (`canvas_view.hpp:117`), the fail-safe-on-vanish shape the scope mirrors.

**Predecessor refinements:** `tasks/refinements/panels/inspector.md` (readout
vocabulary, register_view_body pattern, TSan/e2e rigs),
`tasks/refinements/editor.cells/model.md` (`scene::cells`, KindBridge split,
nested config), `tasks/refinements/editor.cells/selection.md` (shared selection,
D17 hand-off), `tasks/refinements/cameras/model.md` (`scene::cameras`, the
deferred first-camera e2e), `tasks/refinements/editor/look_through.md`
(transient session-state pattern).

**Test rigs:** Catch2 `tests/*_test.cpp` (`TEST_CASE("<leaf>: …")`, appended to
`ace_tests` in the root `CMakeLists.txt` near `:254`); ImGui Test Engine e2e
`tests/*_e2e_test.cpp` (offscreen software-GL, appended to `ace_shell_test` near
`:313-342`, modelled on `tests/inspector_e2e_test.cpp` /
`tests/camera_manip_e2e_test.cpp`); TSan case in `tests/canvas_host_test.cpp`.

## Constraints / requirements

1. **No new view type; register a body into the cataloged `ViewType::Layers`.**
   The panel supplies its body through `views::register_view_body(ViewType::Layers,
   body)` and is bound in the shell exactly as the inspector is
   (`shell.cpp:358-363`, cleared at teardown). `k_view_type_count` stays 8; the
   catalog is `editor.dock.view_registry`'s territory and is not edited
   (D-cells_model-5 / D-view-registry-3).

2. **Front→back list order; reorder maps to bottom→top membership.** The layers
   section renders `scene::cells` reversed (top-of-list = frontmost). A drag
   from list-slot *i* to list-slot *j* is converted to the corresponding
   `(from_index, to_index)` in the composition's **bottom→top** membership
   order and applied with `Transaction::reorder_layer`. The conversion is a
   pure function, unit-tested against the readout `scene::z_order_position`
   already exposes, so the list and the inspector never disagree about which
   cell is "layer N of M".

3. **Reorder is one scene verb, one transaction, one journal entry, undoable.**
   `scene::reorder_cell(document, composition, moved_id, to_index)` is minted as
   an exact mirror of `set_camera_resolution`: resolve the cell's layer and its
   membership index on the live pin, open one `document.transact("reorder_cell")`,
   call `reorder_layer`, commit, return `bool`. It is dispatched through
   `commands::reorder_cell_command` + `CanvasView::apply_edit` — never a direct
   L4 `Document` mutation. An unresolvable id, an out-of-range or equal index,
   or a camera target is a `false`-returning no-op that opens no transaction and
   adds no entry (the `reorder_layer` no-op contract + the `set_camera_resolution`
   mould). The moved cell keeps its `ObjectId`, so selection and `undo` hold on
   the same object across the reorder.

4. **Cameras are not reorderable.** The cameras section is a flat, selectable
   list only; no drag handle, no `reorder` verb touches a camera layer (cameras
   carry no z-order, A14). A camera row selects into the shared selection
   identically to a cell row (D7).

5. **Referenced-vs-painted styling reuses the inspector's generic
   classification.** The provenance affordance is keyed on `scene::CellDetail`,
   not a `kind_id` string (A16 / D-resolution-2): the **editable?** axis from
   `DetailSource` (`PaintedRaster` = owned/painted, `ReferencedImage` =
   referenced, `ResolutionIndependent` = nested/solid) and the **bytes where?**
   axis (owned vs borrowed) from the generic `external_asset_ref()` facet, so
   D11's *"the list surfaces both"* is honored without forking the inspector's
   `describe_detail_source` vocabulary. A future plugin kind is styled
   automatically.

6. **Selection is shared, and Select ≠ expand ≠ enter (D17).** A single click on
   a row applies a selection change to the one `commands::Selection` (Replace;
   Shift = Add; Cmd/Ctrl = Toggle), applied in L4 as the canvas applies its
   `SelectionChange` (D-selection-2). The disclosure triangle **expands**
   (read-only peek, selection unchanged). A double-click on a nested row
   **enters**. These three gestures are distinct and independently tested. The
   panel keeps no selection copy (A7/D19).

7. **Expand is read-only and does not change scope.** Expanding a nested row
   lists its children via `scene::cells(document, registry, child_composition)`;
   those child rows are peek-only (no drag handle, no edit) and the active scope
   stays the parent. `scene::nested_composition_of(document, cell_id)` resolves
   a nested cell to its child composition id (generic `org.arbc.nested` config,
   `nullopt` for a non-nested cell).

8. **Enter is project-level, transient, fail-safe session state.** The entered
   composition is a single `std::optional<arbc::ObjectId>` on `AppState`
   (`nullopt` = Root), re-resolved every frame over `pin()`. Entering/climbing
   is **never a transaction** — no journal entry, no dirty (D15) — and if the
   entered composition id is not a live composition (GC'd, undone away, or a
   foreign document), the scope falls back to Root next frame (the
   look_through fail-safe, D-look_through-7). It is project-level, not
   per-canvas: D17 has the *list and overview* (both singleton panels) reflect
   the same child, so one shared scope is the only representation both panels
   can honor (Decisions).

9. **The breadcrumb is derived, not stored.** `Root ▸ … ▸ child` is computed
   each frame from the entered composition id by walking parent links up to the
   root; clicking crumb *k* sets the scope to that ancestor (or `nullopt` for
   Root). The path helper is a pure L1 function unit-tested independently of
   ImGui.

10. **Reads use the shipped UI-thread scene read-seam (A18).** The panel reads
    `scene::cells`/`scene::cameras`/`nested_composition_of` on the UI thread
    exactly as the inspector's `CameraInspector`/`InspectorPanel` do; it
    introduces no new `Document`-inspection path and calls no writer-thread-only
    libarbc API off the writer thread. Covered by a TSan case.

11. **Levelization (§8) holds with no new edge.** The new write verb and the two
    new reads live in L1 `scene` (already depends on libarbc); the command
    wrapper lives in L1 `commands` (already depends on `scene`); the entered-scope
    field lives on `commands::AppState` (L1); the breadcrumb-path and drag→index
    helpers are pure L1. The ImGui draw code is the L4 `LayersPanel` body,
    reached through the L3 `views::register_view_body` seam. The L1 core gains
    **no** ImGui/GL/SDL include; `scripts/check_levels.py` is not edited.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`, `:365-368`);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.** No
  new component and no `scripts/check_levels.py` edit. `src/scene/` gains
  `reorder_cell`, the `cells(…, composition)` overload, `nested_composition_of`,
  and the `CellDetail` borrowed axis — each including only libarbc + existing
  scene headers; `src/commands/` gains `reorder_cell_command` and the
  `AppState` entered-composition field; `src/app/` (the L4 `LayersPanel` body) is
  the only file that gains ImGui-facing code. Asserted by inspection and by the
  lint.

- **L1 logic — Catch2 unit** (`tests/layers_test.cpp`, new file appended to the
  `ace_tests` source list near `CMakeLists.txt:254`; `TEST_CASE("layers: …")`,
  naming after `tests/inspector_test.cpp`):
  - **Reorder index mapping (Constraint 2):** the pure drag(list-slot *i* →
    slot *j*) → `(from_index, to_index)` membership conversion is pinned against
    hand-computed expectations for a 4-cell composition (front→back list vs
    bottom→top membership), and agrees with `scene::z_order_position` for every
    slot.
  - **`scene::reorder_cell` (Constraint 3):** moving a cell rewrites membership
    order (verified through a re-read `cells()`), is **one transaction / one
    journal entry**, keeps the `ObjectId`, and `undo` restores the prior order
    on the same objects in **one press**; an unresolvable id, an equal/out-of-range
    index, and a camera target each return `false` and mutate nothing (mould:
    `set_camera_resolution`, `src/scene/camera.cpp:565`).
  - **Scope-targeted reorder (Constraint 3/8):** with the scope entered into a
    child composition, `reorder_cell` reorders the **child's** members and
    leaves the root composition untouched.
  - **Provenance + owned/borrowed classification (Constraint 5, D11):** a
    painted raster (owned/editable), a referenced borrowed image
    (referenced/borrowed), a pasted owned read-only image (owned), and a nested
    cell (resolution-independent) each classify to the correct `DetailSource`
    **and** borrowed/owned flag through the generic facets (no `kind_id`
    branch), reusing `describe_detail_source`.
  - **`nested_composition_of` (Constraint 7):** resolves an `org.arbc.nested`
    cell to its child composition id and returns `nullopt` for a
    painted/referenced/solid cell.
  - **`cells(…, composition)` overload (Constraint 7):** walks a given
    (non-root) composition's members in the same bottom→top order and shape as
    the root walk — the expand/enter data source.
  - **Entered-composition scope (Constraint 8/9):** entering sets the scope,
    the derived breadcrumb path is `Root ▸ … ▸ child` for a two-deep nest,
    climbing to crumb *k* pops to that ancestor, entering/climbing adds **zero**
    journal entries and leaves `is_dirty()` unchanged, and a scope naming a
    now-absent composition resolves to Root (fail-safe).
  - **Front→back list ordering (Constraint 1):** the layers section is
    `scene::cells` reversed; the cameras section is `scene::cameras`; a
    `org.arbc.camera` never appears in the layers section (KindBridge split).

- **Rendered output — golden N/A (justified).** The panel composes ImGui chrome
  + text over `AppState`/`scene`, not a libarbc `Document` composition, so there
  is no `render_offline` image to pin (the `view_registry`/inspector precedent).
  Every displayed value is asserted through the L1 units above and the e2e
  below; a non-byte-exact screenshot baseline is captured in the e2e for signal
  only.

- **UI e2e — ImGui Test Engine** (`tests/layers_e2e_test.cpp`, new file appended
  to the `ace_shell_test` list near `CMakeLists.txt:313-342`, offscreen
  software-GL; modelled on `tests/inspector_e2e_test.cpp`, reusing the
  boot-the-real-shell + `register_view_body(ViewType::Layers, …)` rig and driving
  widgets by stable `###` id under the Layers window; rows carry
  `###layer_row_<id>`, disclosure `###layer_expand_<id>`, crumbs
  `###crumb_<id>`). This is also the **first camera e2e**
  (`cameras/model.md`'s parked case):
  - **Two sections render:** a document with cells and cameras shows a cameras
    section and a front→back layers section; row order matches placement, no
    camera in the layers section.
  - **Row select round-trips (Constraint 6, D7):** clicking a cell row sets
    `AppState.selection().primary()` to that cell; clicking a camera row selects
    the camera; Shift adds; Cmd/Ctrl toggles; the canvas/inspector reflect the
    same selection.
  - **Drag-reorder round-trips (Constraint 2/3):** dragging a layer row to a new
    slot changes the membership order read back from `scene::cells`; `undo`
    restores it — one press.
  - **Referenced vs painted affordance:** a painted-raster row and a
    referenced-image row show distinct provenance affordances.
  - **Expand ≠ select (Constraint 6/7):** clicking the disclosure triangle on a
    nested row reveals its children read-only and does **not** change the
    selection.
  - **Enter + breadcrumb + climb (Constraint 8/9, D17):** double-clicking a
    nested row shows the breadcrumb `Root ▸ <child>` and re-roots the layers
    section to the child's cells; clicking the `Root` crumb climbs out and the
    section shows the root cells again — entering/climbing arms neither Save nor
    an undo entry.
  - **Empty states:** an empty composition and an empty selection each show a
    defined empty state with no stale rows.

- **Threading (ASan/TSan).** One case appended to `tests/canvas_host_test.cpp`
  (over `default_interactive_pool_config()`): the UI thread runs the panel's read
  path (`scene::cells(active)` + `scene::cameras` + `nested_composition_of` +
  breadcrumb resolve) and an `apply_edit(reorder_cell_command)` concurrent with
  a live render walk, TSan-clean — mirroring the inspector's UI-thread-read case.
  No new lane, no new suppression.

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed
  lines; clang-format + build clean. Tests ship with the task.

- **Deferred WBS work (closer registers each in the WBS, wired under its area's
  parent so it rolls up to the `capable` milestone):**
  - **`editor.canvas.isolation_scope`** (~2d, `depends !layers`, under
    `editor.canvas`) — the canvas *visual* reflection of the entered scope:
    present the entered composition and **dim everything outside** (D17's
    *"canvas … show the child's cells, everything outside dims"*), consuming
    `AppState`'s entered-composition. This leaf ships the scope model, the list's
    reflection, and the breadcrumb; the **canvas** render reflection is a
    distinct surface (the canvas view, not this L4 panel) and is legitimately a
    separate leaf, not a deferral of this leaf's headline. Note cites this
    refinement.
  - **`editor.cells.scoped_edit`** (~2.5d, `depends !layers, editor.cells.remove`,
    under `editor.cells`) — make the existing insert/delete verbs honor the
    entered composition: `scene::remove_cells`/`add_cell` resolve the **active**
    composition instead of hard-coding `root_composition` (the
    `src/scene/ace/scene/cell.hpp:157` comment already flags *"Deleting from an
    entered/isolated nested scope is `editor.panels.layers`'"* — this leaf
    provides the scope; the thin edit-plumbing that consumes it rides here). Note
    cites this refinement.
  - **Overview reflection** of the entered scope needs **no new task** — it is
    within `editor.panels.overview` (`:602-606`, `depends !layers`), which
    consumes the same `AppState` scope when it is built.

## Decisions

- **D-layers-1 — Register a body into the existing `ViewType::Layers`; no ninth
  view type.**
  The panel plugs into the cataloged singleton `ViewType::Layers` through
  `views::register_view_body`, exactly as the inspector plugs into
  `ViewType::Inspector`.
  *Rationale:* the catalog already reserves `Layers`
  (`view_registry.hpp:19`, `view_registry.cpp:18`) and `editor.cells.model`
  already settled that the view catalog is `editor.dock.view_registry`'s
  territory (`k_view_type_count = 8`, D-cells_model-5 / D-view-registry-3). The
  six-line register-and-bind pattern is shipped and TSan-covered.
  *Alternative rejected:* a new "Cameras" view — D6 keeps cameras managed in the
  overview and surfaced within the list; a standalone cameras view was already
  ruled out by the catalog (no `Cameras` enumerator). **No doc delta required.**

- **D-layers-2 — Reorder is a new L1 `scene::reorder_cell` verb over
  `Transaction::reorder_layer`, dispatched as a `commands` `Command` through
  `apply_edit`.**
  Minted as an exact mirror of `set_camera_resolution`; one transaction, one
  journal entry, `bool`-returning, undoable; the front→back list slot is
  converted to a bottom→top membership index by a pure helper unit-tested
  against `z_order_position`.
  *Rationale:* libarbc v0.4.0 ships the atomic, stable `reorder_layer` facet
  with the exact no-op contract the mould expects (`model.hpp:530`); routing
  through a scene verb + `apply_edit` keeps the one writer-thread funnel
  (D-writer_thread-11) and the L1-testable seam (§9), and keeps the drag and the
  inspector's "layer N of M" readout on one ordering.
  *Alternative rejected:* mutate `arbc::Document` directly from the L4 body — it
  bypasses the writer funnel and puts untestable document logic in L4, the same
  reason D-inspector-2 rejected it. **No doc delta required** (extends the
  shipped `scene` mutator family).

- **D-layers-3 — The entered-composition scope is project-level, transient
  session state on `AppState` — not per-canvas.**
  A single `std::optional<arbc::ObjectId>` (`nullopt` = Root) on the one
  `AppState`, re-resolved each frame, never a transaction, fail-safe when the id
  vanishes.
  *Rationale:* D17/§6 (`docs/00-design.md:224-228`) has *"canvas, list, and
  overview show the child"* — the Layers and Overview panels are **singletons**
  (`view_registry.cpp:18`), so both can only reflect one scope; A7/D19 make
  selection and shared panels project-level. A single project-level scope is the
  only representation both singleton panels can honor. The transient/fail-safe
  shape reuses the look_through precedent (D-look_through-1/-7) rather than
  inventing a second session-state mechanism.
  *Alternative rejected:* a per-canvas scope like `Presenter::active_camera`.
  Rejected because two canvases could then disagree on scope while the single
  Layers/Overview panels can show only one — directly contradicting D17's *"list
  and overview show the child."* (The per-canvas *look-through* camera is
  legitimately per-canvas because each canvas renders through its own camera;
  the *composition scope* is a structural navigation state the singleton
  structure views share.) **No doc delta required** — D17 + A7 + D19 already
  settle project-scoping; this decision applies them, it does not amend them.

- **D-layers-4 — Referenced-vs-painted styling reuses the inspector's generic
  classification and adds only the D11 owned/borrowed axis, both from generic
  facets.**
  The editable? axis comes from `DetailSource`/`describe_detail_source`
  (shipped); the bytes-where? axis is a `bool borrowed` on `CellDetail` derived
  from the generic `external_asset_ref()` facet, filled in the same pinned
  `cells()` walk.
  *Rationale:* D11 says *"the list surfaces both"* axes; the inspector already
  surfaces the editable axis and D-inspector-4 reserved the *list styling* for
  here. Deriving both axes from generic `arbc::Content` facets (never a
  `kind_id` string) keeps A16 / D-resolution-2 and classifies a future plugin
  kind automatically. Adding the flag to the existing `CellDetail` — read once
  on the shared pinned walk — avoids a second document read and keeps the
  inspector and list on one struct.
  *Alternative rejected:* a `kind_id` string-switch for referenced-vs-painted —
  the allowlist A16 forbids; and a separate list-only provenance read — a second
  walk the shared `CellDetail` makes unnecessary. **No doc delta required.**

- **D-layers-5 — Expand peeks read-only via a composition-parameterised `cells`
  overload; it never changes scope or selection.**
  Expanding a nested row lists `scene::cells(document, registry,
  child_composition)` as non-interactive child rows; selection and the active
  scope stay on the parent.
  *Rationale:* D17 is explicit — expand is *"peek children, still editing
  parent"* and *"Select ≠ expand ≠ enter."* The overload is a one-argument
  generalisation of the existing root-only walk (which already resolves a
  composition then calls `for_each_layer_in`, `cell.cpp:253,290`), so it adds
  the expand/enter data source with no new traversal machinery.
  *Alternative rejected:* making expand enter-lite (a shallow scope change) —
  it would collapse the three distinct D17 gestures the design keeps separate.
  **No doc delta required.**

- **D-layers-6 — The canvas dimming and scoped insert/delete are separate leaves;
  this leaf ships the scope model, the list reflection, and the breadcrumb.**
  The entered scope is published on `AppState`; its *canvas* visual reflection
  (present child, dim outside) is `editor.canvas.isolation_scope`, and making the
  pre-existing insert/delete verbs honor the scope is `editor.cells.scoped_edit`.
  *Rationale:* D17's promise spans three surfaces (list, canvas, overview); the
  **list** is this panel and is fully delivered here (re-root + breadcrumb +
  expand-peek + scope-aware reorder). The **canvas** reflection is canvas-render
  code in a different component/leaf, and the insert/delete-into-scope plumbing
  touches `editor.cells`' shipped verbs — separable, concrete, agent-implementable
  follow-ups, not deferrals of this leaf's own headline (which is the list and
  the navigation model). The overview reflection needs no new task —
  `editor.panels.overview` (`depends !layers`) consumes the same scope. Reorder,
  the one edit this panel itself mints, **is** scope-aware here (D-layers-2/-3),
  so "enter" is functional for this panel's own gesture immediately.
  *Alternative rejected:* absorbing canvas dimming and scoped insert/delete into
  this 3d leaf — it would balloon the leaf across three components (canvas
  render + `editor.cells` verbs + this panel) and past its estimate for no
  coherence gain; each follow-up is cleanly ownable by its own component's leaf.
  **No doc delta required.**

## Open questions

(none — all decided.) One item is surfaced to the parking lot in the return
summary rather than encoded as a WBS leaf, because it is a human/design judgment
call, not agent-implementable here: the **enter/expand keyboard and
double-click-vs-modifier bindings** are provisional pending the full input map
(§11 is still open in `docs/00-design.md`); this leaf ships the shipped-idiom
gestures (single-click select, disclosure-triangle expand, double-click enter,
crumb-click climb) and the input-map owner can rebind them later without
touching the scope model.

## Status

**Done** — 2026-07-29.

- **Layers panel body** registered into the cataloged `ViewType::Layers` via `views::register_view_body` — `src/app/ace/app/layers_panel.hpp` (new), `src/app/layers_panel.cpp` (new), wired + torn down in `src/app/shell.cpp`.
- **`scene::reorder_cell`** write verb and **`CellDetail.borrowed`** axis added — `src/scene/ace/scene/cell.hpp`, `src/scene/cell.cpp`; also `cells(…,composition)` overload, `nested_composition_of`, `active_composition`, `composition_path`/`Breadcrumb`, `list_drag_to_membership`/`MembershipMove` pure read helpers in the same files.
- **`commands::reorder_cell_command`** added — `src/commands/ace/commands/cells.hpp`, `src/commands/cells.cpp`; dispatched through `apply_edit`.
- **`AppState::entered_composition`** transient session field (non-transactional, fail-safe, project-level) — `src/commands/ace/commands/app_state.hpp`.
- **Catch2 unit** `tests/layers_test.cpp` (8 `layers: …` cases: reorder index mapping, reorder verb + undo, scope-targeted reorder, provenance/borrowed classification, `nested_composition_of`, `cells(…,composition)` overload, entered-composition scope + breadcrumb + fail-safe, front→back ordering). **ImGui Test Engine e2e** `tests/layers_e2e_test.cpp` (also the first camera e2e). **TSan case** appended to `tests/canvas_host_test.cpp`. **`CMakeLists.txt`** updated.
- **Implementation notes:** (1) `reorder_cell` translates camera-excluded cells-space `to_index` into raw membership indices; (2) panel e2e deliberately omits a canvas (pending `editor.canvas.isolation_scope`'s nested `DocumentBinding`).
- **Deferred WBS:** `editor.canvas.isolation_scope` (canvas dimming of entered scope, ~2d) and `editor.cells.scoped_edit` (scope-aware insert/delete, ~2.5d) registered in WBS per the Deferred WBS section.
