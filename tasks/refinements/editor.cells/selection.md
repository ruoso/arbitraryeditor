# editor.cells.selection — Project-level selection; hit-test; marquee; select-behind

## TaskJuggler entry

- **Task:** `editor.cells.selection` (`tasks/00-editor.tji:323-328`, under
  `task cells "Cells & manipulation"` at `:315`).
- **Effort:** `2.5d` · `allocate team`.
- **Depends:** `!model` (`editor.cells.model`) and `editor.cameras.model`
  (`:326`).
- **Note (`.tji:327`):** "One project-level selection (canvases are only
  cameras): click a cell BODY, a camera BORDER/label (interior click-through);
  marquee multi-select; Cmd/Ctrl-click select-behind. Shared across
  canvas/list/overview. Design: D7/D19."
- **Back-link:** the `.tji` note currently ends `Refinement:
  tasks/refinements/selection.md` (the flat interim path). This refinement lands
  at **`tasks/refinements/editor.cells/selection.md`** per the area-subdir layout
  (`tasks/refinements/README.md:9-18`); the closer updates the note back-link to
  the real path and adds `complete 100` after `allocate team`
  (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji` here.
- **Downstream dependents** (why this is the second cells keystone):
  `editor.cells.gizmo` (`!selection`, `:332`), `editor.cells.resolution`
  (`!selection`, `:338`), `editor.cells.remove` (`!selection`, `:344`),
  `editor.panels.inspector` (`editor.cells.selection`, `:354`), and
  `editor.canvas.nav_aids` (`editor.cells.selection`, `:216` — zoom-to-selection
  and fit-to-cell "need the selection model"). Transitively that is the rest of
  `editor.cells` plus the whole of `editor.panels`.

## Effort estimate

**2.5 days.** Three of the four pieces have a shipped mould and one is
greenfield:

- The **container** already exists and is unconsumed —
  `commands::Selection` (`src/commands/ace/commands/selection.hpp:17-43`,
  `src/commands/selection.cpp:7-38`) shipped with `editor.project.app_state` and
  is referenced today by nothing but `commands::AppState`
  (`app_state.hpp:53-54`, `:94`) and one test (`tests/commands_test.cpp:94`).
  This leaf adds two verbs to it, not a new type.
- The **hit-test half for cameras** is shipped: `interact::hit_frame`
  (`src/interact/ace/interact/interact.hpp:180-181`) already implements D7's
  border/label grab with an interior click-through, and the screen↔composition
  mapping + px→composition tolerance recipe is written out at
  `src/app/canvas_view.cpp:208-224`.
- The **object model** is shipped on both sides: `scene::Cell`
  (`src/scene/ace/scene/cell.hpp:124-129`) and `scene::Camera`
  (`src/scene/ace/scene/camera.hpp:123-129`) are the same
  `{content ObjectId, layer ObjectId, placement Affine}` shape by A14's design,
  both read back in layer/z order over the lock-free `pin()` seam.
- **Greenfield:** cell-body hit-test, the z-ordered candidate stack, select-behind
  cycling, marquee overlap, the modifier→selection policy, and the canvas
  wiring + outline chrome. Nothing anywhere in the tree contains `marquee`,
  `pick`, or select-behind code today.

No new component, no new DAG edge, no new external dependency, no libarbc
change. Coverage is Catch2-heavy (the whole policy core is pure), one
byte-equality golden assertion rather than a new golden file, and one e2e.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cells.model`** (`tasks/refinements/editor.cells/model.md`) — the
  thing there is to select. `scene::cells(document, registry)`
  (`cell.hpp:139`, impl `src/scene/cell.cpp:316-353`) walks the root
  composition's layers in z order over `document.pin()`, **filters
  `org.arbc.camera` out** (`cell.cpp:344-346`), and reports an unresolvable kind
  token as a cell with an empty `kind_id` rather than dropping it. D-cells_model-3
  fixed that a factory-built `org.arbc.solid` is **unbounded**
  (`Content::bounds() == nullopt`) — the degenerate extent this leaf's hit-test
  must define behaviour for. D-cells_model-6 fixed that placement enters
  `scene::add_cell` as a finished `arbc::Affine`, keeping `interact`'s geometry
  helpers primitive-only.
- **`editor.cameras.model`** (`tasks/refinements/cameras/model.md`) — A14's
  camera-as-placed-object shape, which exists **specifically so this leaf can
  pick cameras through the same model as cells**: A14 rejects a single
  `CamerasContent` list because it "would break the D7 uniform select/transform
  shape that `cells.selection` … depends on" (`docs/01-architecture.md:305`).
  `scene::cameras(document)` (`camera.hpp:134`) reports `{id, layer, name,
  resolution, frame}` in layer order.
- **`editor.cameras.rename_stable_id`**
  (`tasks/refinements/cameras/rename_stable_id.md`) — D-rename-1/2: a rename is
  one in-place `set_content_state` through the `Editable` facet, so the content
  `ObjectId`, the binding layer, the frame, and layer order all survive it. That
  task exists so an `ObjectId`-keyed selection is safe to build on; this leaf is
  its payoff and pins it with a test.
- **`editor.cameras.manip`** (`tasks/refinements/cameras/manip.md`) — the
  hit-test/direct-manipulation precedent this leaf extends rather than
  duplicates. `interact::hit_frame(frame, native_w, native_h, point, edge_tol,
  corner_tol)` (`interact.hpp:180-181`) is the camera half of D7 already
  implemented, tolerances in **composition units**; D-manip-4 fixed
  preview-as-session-state / commit-one-transaction-on-release; Constraint 7 fixed
  that the border-grab is the **default** behaviour and does **not** wait for
  `editor.canvas.tool_dispatch`. D-manip-6/Constraint 8 explicitly deferred
  "unified `ObjectId`-keyed targeting" **to this leaf**.
- **`editor.project.app_state`** (`tasks/refinements/editor/app_state.md`) —
  D-app_state-3 put the **one project-level `Selection`** on `AppState`
  (`app_state.hpp:53-54`, `:94`), transient, never a transaction, never per
  canvas; D-app_state-4 kept the active tool on `dockmodel::ToolSelection`
  (`src/dockmodel/ace/dockmodel/tool_rail.hpp:45-52`, held by the Dockspace at
  `src/dock/ace/dock/dock.hpp:307`) so no `commands ↔ dockmodel` edge exists.
- **`editor.canvas.nav`** (`tasks/refinements/editor/nav.md`) — the transient
  viewport camera `Presenter::camera` (`src/app/ace/app/canvas_view.hpp:137`) and
  the Space-pan gesture that must stay inert on objects
  (`canvas_view.cpp:316`, `src/views/views.cpp:76-80`).
- **`editor.canvas.edit_render_sync` / `single_writer`** — A4.1/A4.1a
  (`docs/01-architecture.md:84-96`, `:98-123`): every UI-thread `Document`
  *mutation* runs inside `CanvasView::apply_edit`
  (`src/app/ace/app/canvas_view.hpp:77` → `src/render/ace/render/canvas_host.hpp`),
  and *reads* are lock-free via `pin()`. Selection mutates nothing, so it needs
  no lease — but it **reads** the document every frame, which is the threading
  surface this leaf owes coverage for.

**Pending (owned here):** `ace/interact/pick.hpp` (the pick policy core + the one
assembly adapter), two `commands::Selection` verbs, the `scene::Cell` extent
field, the `views` marquee/outline chrome + press-anchor input, the L4 canvas
wiring, and the A17 doc delta. Nothing downstream is blocked on an unwritten
predecessor.

## What this task is

Make the editor's **one project-level selection** actually reachable from a
pointer. Today `commands::Selection` is a shipped, correct, and completely
unused container: nothing hit-tests into it. This leaf builds the four pieces
D7 and `docs/00-design.md:219-228` name, and wires them to the canvas:

1. **Hit-test** — a composition-space point resolved against the z-ordered stack
   of placed objects. A **cell** is grabbed by its **body** (the point mapped
   through the placing affine's inverse and tested against the content's own
   extent — exact for rotated/sheared placements, not an AABB approximation). A
   **camera** is grabbed only by its **border or label**; its **interior is
   click-through** to the cells it frames, which is exactly what the shipped
   `interact::hit_frame` already returns `None` for.
2. **Topmost-first + select-behind** — the full stack under the cursor, ordered
   front-to-back. A plain click takes the front; **Cmd/Ctrl-click cycles down**
   the stack, wrapping.
3. **Marquee** — a composition-space rectangle dragged from empty canvas selects
   every bounded object whose placed quad overlaps it; **Shift adds** to the
   selection instead of replacing it.
4. **Shared, not per-canvas** — the selection is one `commands::Selection` on the
   one `AppState` (D19/A5/A7); the canvas is a *surface over* it, holding no copy.
   The Layers list and the Overview become surfaces over the same object when
   they land, with no change here.

Plus the minimum honest feedback: a **selection outline** on each selected object
(a plain stroked quad — not handles; the handle gizmo is `editor.cells.gizmo`),
the selected camera's frame drawn in its existing `active` style, and a marquee
rectangle while dragging. Select-All (`Cmd/Ctrl-A`) and Deselect (`Escape`) ride
along because they are two lines against the same verbs and
`docs/00-design.md:499-500` leaves the input map open.

**Not in scope, by prior decision:** the transform gizmo and snapping
(`editor.cells.gizmo`, `:329`), the resolution readout and health badge
(`editor.cells.resolution`, `:335`), Delete (`editor.cells.remove`, `:341`), the
inspector body (`editor.panels.inspector`, `:351`), the Layers-list and Overview
surfaces (`:357`, `:363`), nested **expand/enter** and the breadcrumb (D17,
explicitly `editor.panels.layers`' scope per `:359`), and routing the active tool
into pointer gestures (`editor.canvas.tool_dispatch`, `:200`). Per manip's
Constraint 7 the select behaviour here is the **always-on default**, not gated on
`ToolSelection`.

## Why it needs to be done

Every remaining `editor.cells` and `editor.panels` leaf depends on it — four of
them by `!selection` and two by full name. `editor.cells.gizmo` has nothing to
transform, `editor.cells.remove` has nothing to delete, `editor.panels.inspector`
has nothing to inspect, and `editor.canvas.nav_aids`' zoom-to-selection has
nothing to zoom to. `editor.cameras.manip` shipped with a deliberate workaround
for this gap (`src/app/ace/app/camera_inspector.hpp:22-23`: "unified
`ObjectId`-keyed selection are already-scheduled sibling leaves' scope … this leaf
targets a camera by a compact chooser (no selection dependency)") — the compact
chooser is placeholder chrome that this leaf makes redundant.

It is also the first proof of the D7 unification. A14 and
`editor.cameras.rename_stable_id` were both shaped around a future
`ObjectId`-keyed selection; until something actually picks a cell and a camera
through one code path, that claim is untested.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **D7 — Manipulation model** (`docs/00-design.md:474`): *"Cells and cameras
  share **one shape** (affine placement + a resolution number) and **one select
  tool**, differing only in direction … Cells grabbed by body, cameras by
  border/label with click-through interiors."*
- **§6 "Selection (one tool)"** (`docs/00-design.md:219-228`) — the normative
  behaviour spec, quoted in full because every acceptance criterion below
  instantiates one clause of it: *"Click a cell's **body** to select it; click a
  camera's **border or label** — a camera's **interior is click-through** to the
  cells it frames, so a camera never blocks editing the art inside it. Stacked
  cells: topmost first, Cmd/Ctrl-click cycles down ("select behind"). Marquee to
  multi-select, Shift-click to add. Selection is **shared** across canvas, list,
  and overview (§5)."* The tail (expand / double-click-to-enter / breadcrumb,
  *"Select ≠ expand ≠ enter"*) is D17 and belongs to `editor.panels.layers`.
- **§6 "The unifying shape"** (`:210-217`) — a cell and a camera are the *same*
  object shape, differing only in direction; *"the whole interaction reduces to
  one rule."*
- **§6 "Modifiers & snapping (shared)"** (`:257-263`): *"Shift constrains …
  Cmd/Ctrl select-behind and bypass-snap; **Space pans the *view*** (the editing
  camera) — distinct from dragging a camera *object*."* Also the source of the
  deferred *"frame selection"* command (`:262-263`).
- **§6 "Cell gizmo"** (`:230-235`) and **"Camera gizmo"** (`:246-255`) — the
  handles are `editor.cells.gizmo`'s and `editor.cameras.manip`'s; this leaf
  draws the bounding outline only.
- **D19 — Project-scoped state** (`:486`) and **§10** (`:442-451`): *"Selection
  and the shared panels … belong to the **project**, not any canvas — canvases
  are *only* cameras, so N of them share one project-level selection."*
- **D6 / §5** (`:473`, `:182-199`) — the list and the overview *"both index one
  shared selection"*; z-order is a real, user-visible ordering, which is why the
  pick stack must be ordered rather than first-hit-wins.
- **D15** — selection is transient app state, never a scene transaction: clicking
  must add no journal entry and bump no revision.
- **D20** (`:487`) — Select is the *one* tool and cameras are folded into it, not
  a separate mode; wiring `ToolSelection` to canvas behaviour is downstream.
- **§11 "Full input map"** (`:499-500`) — *"The complete keyboard/shortcut set …
  (the pieces are decided; the map isn't written)"*, which is the licence for the
  specific chord choices under Decisions.

**Governing architecture rows:**

- **A5 / A7** (`docs/01-architecture.md:296`, `:298`, prose `:132`) — *"Selection
  and the shared panels are project-level, not per-canvas (D19): a canvas is
  *only* a camera and carries no selection or inspection state."*
- **A8 / §8** (`:299`, `:185-220`) — the levelization DAG; the `interact` row at
  `:210` (`interact` | 1 | base, **scene**, libarbc | **no**).
- **A11** (`:302`) — *"`interact` stays pure math (hit-test/gizmo/snapping/brush)"*;
  the tool→interaction dispatch is promoted into `interact` **when a canvas
  consumer exists**, which is `editor.canvas.tool_dispatch`, not this leaf.
- **A14** (`:305`) — the camera-as-`ObjectId`-addressable-placed-object shape,
  minted so *"`editor.cells.selection` / `editor.cameras.manip` reuse the cell
  object/transform machinery"*.
- **A16** (`:307`) — the primitive-only convention for `interact`'s geometry
  helpers, narrowed by this leaf's A17.
- **A17** (`:308`, **written by this refinement**) — hit-testing lives in L1
  `interact`, split into a primitive-only pick policy and one `interact → scene`
  assembly adapter.
- **A4 / A4.1 / A4.1a** (`:295`, `:84-96`, `:98-123`) — writer identity and the
  `CanvasHost` document lease; reads stay lock-free via `pin()`.
- **§9** (`:222-249`) — the DoD table names **selection** explicitly as L1-logic
  Catch2 territory (`:228`) and **select**/**drag** in the e2e path (`:230`).

**libarbc API surface** (fetched under `build/dev/_deps/arbc-src/`):

- `arbc::Affine` — `src/base/arbc/base/transform.hpp`: `apply`, `det`,
  `inverse() -> std::optional<Affine>` (nullopt when degenerate/non-finite),
  `max_scale()`, `map_rect(const Rect&)`, `compose(outer, inner)`. The inverse is
  what turns a click into content space exactly, for any rotation/shear.
- `arbc::Rect` / `arbc::Vec2` — `src/base/arbc/base/geometry.hpp`: `x0,y0,x1,y1`,
  `width()`, `height()`, `empty()` (*"empty unless x0 < x1 and y0 < y1"*),
  `intersect`.
- `arbc::ObjectId` — `src/base/arbc/base/ids.hpp`: *"Zero is never a valid id"*;
  `ObjectId{}` is the invalid/empty-selection sentinel `Selection::primary()`
  already uses.
- `arbc::Content::bounds() const -> std::optional<arbc::Rect>` —
  `src/contract/arbc/contract/content.hpp:487`; `nullopt` means **unbounded**.
  `CameraContent::bounds()` is deliberately empty (A14: a camera contributes zero
  pixels), so a camera's pickable extent is **not** `bounds()` — it is its output
  rectangle `[0,native_w]×[0,native_h]` under the frame affine.
- `arbc::Document::pin() -> DocStatePtr` and `resolve(ObjectId) -> Content*` —
  `src/runtime/arbc/runtime/document.hpp:262`, `:266`; the lock-free reader seam
  `scene::cells`/`scene::cameras` already use.
- `arbc::CompositionRecord` — `src/model/arbc/model/records.hpp:114-116`: *"the
  ordered layer list (**bottom-to-top**)"*. That is the z-order convention every
  list in this leaf inherits.

**Editor seams this leaf extends:**

- `commands::Selection` — `src/commands/ace/commands/selection.hpp:17-43`
  (`items()`, `primary()`, `contains`, `select`, `add`, `toggle`, `clear`), impl
  `src/commands/selection.cpp:7-38`. Held at
  `src/commands/ace/commands/app_state.hpp:53-54`, member `:94`.
- `interact::FrameHandle` + `interact::hit_frame` —
  `src/interact/ace/interact/interact.hpp:156-168`, `:180-181`; the primitive-only
  convention documented at `:70-75` and `:146-151`.
- `scene::Cell` / `scene::cells` — `src/scene/ace/scene/cell.hpp:124-129`,
  `:139`; impl `src/scene/cell.cpp:316-353`. `scene::probe_bounds`
  (`cell.hpp:102-103`, impl `cell.cpp:273-281`) is the existing `bounds()` read,
  but it is **pre-mint** (a throwaway factory content) — this leaf needs the live
  read off the pinned state.
- `scene::Camera` / `scene::cameras` / `scene::Resolution` —
  `src/scene/ace/scene/camera.hpp:123-129`, `:134`, `:26-31`.
- `views::CanvasInput` + `views::draw_canvas_interactive` —
  `src/views/ace/views/views.hpp:46-67`, `:74`; impl `src/views/views.cpp:49-94`,
  the button-edge/modifier read at `:86-92`, the `##canvas_nav` InvisibleButton at
  `:59-63` (the widget id the e2e drives).
- `app::CanvasView::Presenter` — `src/app/ace/app/canvas_view.hpp:129-158`; the
  per-pane gizmo-grab block at `:151-157` is the exact shape the marquee state
  parallels. `draw_frame_gizmos` — decl `:172-174`, impl
  `src/app/canvas_view.cpp:204-325`, with the screen↔composition mapping and the
  px→composition tolerance conversion at `:208-224`, the camera hover loop at
  `:299-312`, and the grab start at `:316-324`. The per-frame camera read is
  `canvas_view.cpp:62`; there is no `scene::cells` read there yet.
- `scripts/check_levels.py:21-37` — the `ALLOWED` DAG (`scene` `:26`, `interact`
  `:27`, `commands` `:28`), external seams `:42-48`.

**Predecessor refinements:** `tasks/refinements/editor.cells/model.md`,
`tasks/refinements/cameras/model.md`,
`tasks/refinements/cameras/rename_stable_id.md`,
`tasks/refinements/cameras/manip.md`, `tasks/refinements/editor/app_state.md`,
`tasks/refinements/editor/canvas_view.md`, `tasks/refinements/editor/nav.md`.

**Test rigs:** Catch2 units join `ace_tests` (`CMakeLists.txt:219-233`); ImGui
Test Engine e2e joins `ace_shell_test` (`:248-259`). Goldens are raw sRGB8 bytes
under `tests/goldens/` compared via `ace_test::compare_golden`
(`tests/golden_support.hpp:36`). The raw-position mouse-drive recipe over the
canvas pane is `tests/camera_manip_e2e_test.cpp:250-271`; the hit-test unit-test
mould is `tests/camera_manip_test.cpp:240`.

## Constraints / requirements

1. **The selection is ONE project-level `commands::Selection`; no surface keeps a
   copy.** A5/A7/D19 are explicit that a canvas carries no selection state. The
   canvas reads `state_.selection()` fresh each frame and writes through the same
   reference; `Presenter` gains **no** selection member. The only per-pane state
   this leaf adds is the in-progress marquee drag (a gesture, not a selection).

2. **Selecting is never a transaction.** D15/D-app_state-3: no click, drag,
   modifier, or Select-All may open a `Document::transact`, add a
   `JournalEntry`, or bump the revision. Selection changes do **not** go through
   `commands::Command`/`dispatch` and do **not** need `CanvasView::apply_edit`
   (which exists to bind *mutations* to the writer identity, A4.1). Asserted, not
   assumed: the e2e checks `journal().depth()` and `pin()->revision()` are
   unchanged across the whole gesture suite.

3. **Cells are picked by BODY, cameras by BORDER/LABEL only.** A cell hit is
   `placement.inverse().apply(point)` inside the content's extent — the exact
   test for an arbitrary affine, so a rotated or sheared cell picks correctly and
   an AABB approximation is not acceptable. A camera hit is
   `interact::hit_frame(...) != FrameHandle::None`, reusing the shipped function
   verbatim; its interior returns `None`, which **is** D7's click-through. A
   camera must never be pickable by its interior, and a cell must never be
   pickable outside its extent.

4. **Tolerances are composition units, converted from screen pixels by the
   caller.** The `hit_frame` contract (`interact.hpp:178-179`) and the shipped
   recipe (`canvas_view.cpp:222-224`: `edge_tol = 6.0/scale`,
   `corner_tol = 9.0/scale`, `scale = camera.max_scale()`) are reused unchanged,
   so a border stays equally grabbable at every zoom. Cell bodies take no
   tolerance — a body is a filled region, not an outline.

5. **The pick stack is z-ordered, front-to-back, and cameras sit above all
   cells.** Layer order is bottom-to-top (`records.hpp:114-116`), which is the
   order `scene::cells()`/`scene::cameras()` report. Cameras render zero pixels
   (A14), so their outlines are always-on-top chrome and appending them above the
   cells is the visually honest merge — and it is the only merge recoverable from
   the two independently-filtered lists. The existing first-hit-wins camera loop
   (`canvas_view.cpp:299-312`) is superseded by the ordered stack, but its
   *behaviour* for a single camera must not change.

6. **Unbounded content is click-selectable but never marquee-selectable.** A
   factory-built `org.arbc.solid` reports `bounds() == nullopt`
   (D-cells_model-3) and genuinely covers the whole plane, so a click anywhere
   over it is a real hit — it sits at the bottom of the stack and is what
   Cmd/Ctrl-click reaches after the bounded cells above it. A marquee is an
   *enclosure* gesture over extents; an unbounded target would be caught by every
   marquee, which carries no information, so it is excluded. The asymmetry is
   deliberate and tested on both sides.

7. **Marquee overlap is exact for arbitrary placements.** A placed cell is a
   parallelogram, not an AABB. Overlap against the axis-aligned marquee rect uses
   a separating-axis test over the marquee's two axes and the quad's two edge
   normals — cheap, exact, and unit-testable. Touch (any overlap) selects; full
   enclosure is **not** required.

8. **Levelization: the pick policy core names no `scene` type; the one assembly
   adapter is the sole `interact → scene` include.** Per A17, `ace/interact/pick.hpp`
   splits into a primitive-only core (`PickTarget`, `pick`, `pick_stack`,
   `pick_behind`, `marquee`, `click_selection`, `marquee_selection` over
   `std::span<const PickTarget>`) and one adapter
   `pick_targets(const arbc::Document&, const arbc::Registry&)`. `interact` gains
   no ImGui/GL/SDL include and no `commands` include; `commands` gains no
   `interact` include; `scene` gains no `interact` include. `check_levels.py`
   needs **no edit** — the `interact → scene` edge is already declared at `:27`.

9. **`interact` never mutates `commands::Selection`.** The policy functions return
   a `SelectionChange` **value** describing the intended mutation; L4 applies it
   with a four-arm switch onto the shipped `Selection` verbs. This is what keeps
   the whole modifier policy in headless Catch2 without inventing an
   `interact → commands` edge.

10. **Stale ids are pruned, not dereferenced.** Undo, GC, or a future delete can
    remove an object whose id the selection still holds. `Selection` gains
    `prune(std::span<const arbc::ObjectId> live)`, called once per canvas frame
    against the freshly-assembled target list, so no consumer (starting with
    `editor.panels.inspector`) can ever resolve a dangling `ObjectId`. A redo that
    restores the same object does **not** restore the selection — documented, not
    a bug.

11. **The Space-pan and the camera frame-grab stay exactly as they are.** Space
    held ⇒ nav pan, inert on objects (manip Constraint 7, `canvas_view.cpp:316`).
    A press over a camera border both **selects** that camera and **starts** the
    existing frame grab — one gesture, D7's one select tool. A press over a cell
    body selects and does nothing else; the move-drag is `editor.cells.gizmo`'s.
    A press over empty canvas (no hit, Space up) starts a marquee.

12. **Document reads are plain UI-thread reads over `pin()`.** `pick_targets`
    calls `scene::cells`/`scene::cameras` and `Document::resolve(...)->bounds()`;
    it opens no transaction and takes no lease. This is the same per-frame read
    `canvas_view.cpp:62` already performs, extended to cells — but it now also
    calls a `Content` virtual off the render thread's back, which is the specific
    thing the sanitizer case below exists to pin.

13. **Rendered pixels do not change.** Selection chrome is ImGui draw-list output
    over the pane; it never enters the composited image. `render_document_srgb8`
    must be byte-identical before and after any selection gesture.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.** No
  new component and **no `scripts/check_levels.py` edit**: the
  `interact → scene` edge used by `pick_targets` is already declared
  (`check_levels.py:27`, `docs/01-architecture.md:210`). Asserted by inspection
  as well as by the lint: `src/interact/` gains no ImGui/GL/SDL include and no
  `ace/commands` include; `src/commands/` and `src/scene/` gain no `ace/interact`
  include; `src/dock/` is untouched. The one deviation from a stated decision
  (A16's parenthetical "no `interact→scene` edge") is recorded as **A17** in the
  same commit.

- **L1 logic — Catch2 unit** (`tests/selection_test.cpp`, new file added to the
  `ace_tests` source list at `CMakeLists.txt:219-233`; naming follows
  `tests/camera_manip_test.cpp:240`, i.e. `TEST_CASE("selection: …")`):
  - **Cell body hit, exact under an arbitrary affine:** a target with a known
    extent and a rotated + sheared + translated placement is hit for points
    inside the placed parallelogram and missed for points inside its AABB but
    outside the parallelogram — the assertion an AABB implementation fails.
  - **Camera interior is click-through (the D7 assertion):** a camera whose frame
    encloses a cell — a click in the shared interior returns the **cell**; a
    click on the camera's border returns the **camera** with a non-`None`
    `FrameHandle`; a click on its label band returns the camera with
    `FrameHandle::Label`. Delegation to `hit_frame` is asserted by checking the
    returned handle, not re-deriving the geometry.
  - **Z-order and topmost-first:** three overlapping cells at a common point
    yield a `pick_stack` ordered front-to-back matching the reverse of the input
    (layer) order, and `pick` returns its front. A camera overlapping all three
    at its border sorts above every cell.
  - **Select-behind cycles and wraps:** repeated `pick_behind` at a fixed point
    over a 3-deep stack visits front → middle → back → front, driven purely by
    which id is currently selected (no hidden cycle state). With nothing selected
    it behaves as `pick`; with a selected id that is not in the stack it behaves
    as `pick`.
  - **Unbounded content (Constraint 6):** a target with `extent == nullopt` is
    hit by a click far outside every other target and sorts at the bottom of the
    stack; the same target is **never** returned by `marquee`, including a
    marquee that covers the whole composition.
  - **Marquee overlap is exact (Constraint 7):** a rotated cell whose AABB
    overlaps the marquee but whose quad does not is **not** selected; touching
    (partial) overlap **is** selected; a marquee fully inside a large cell selects
    it; an empty/degenerate marquee rect selects nothing.
  - **Modifier policy (`click_selection` / `marquee_selection`):** plain click on
    a hit ⇒ `Replace{id}`; plain click on a miss ⇒ `Clear`; Shift-click on a hit
    ⇒ `Toggle{id}`; Shift-click on a miss ⇒ `None` (a Shift-miss must not wipe
    the selection); Cmd/Ctrl-click ⇒ `Replace{pick_behind(...)}`; marquee ⇒
    `Replace{ids}`, Shift-marquee ⇒ `Add{ids}`, empty plain marquee ⇒ `Clear`,
    empty Shift-marquee ⇒ `None`.
  - **Degenerate inputs return misses, never NaN:** a non-invertible placement, a
    zero-area extent, a non-positive camera resolution, and a non-finite pick
    point each yield a miss and leave the stack empty (the D-fit_bounds-3
    fallback discipline).
  - **`pick_targets` assembly over a real `Document`:** a document holding two
    cells (`scene::add_cell`) and one camera (`scene::add_camera`) yields exactly
    three targets — the two cells in layer order with `PickKind::Cell` and their
    live `Content::bounds()`, then the camera with `PickKind::Camera`, its frame
    as `placement`, and `Rect{0,0,res.width,res.height}` as extent. A cell of a
    kind whose token does not resolve is still a target (mirroring
    `cells()`'s empty-`kind_id` passthrough).
  - **`Selection::prune` (Constraint 10):** select three ids, prune against a
    live set missing one ⇒ that id is gone, order and `primary()` of the
    survivors are preserved; pruning away the current primary re-points
    `primary()` to a surviving member (or `ObjectId{}` when none survive);
    pruning against an empty live set is `clear()`.
  - **`Selection::replace_all` / `add_all` (the marquee verbs):** replacing sets
    the primary to the last id and drops previous members; adding is duplicate-safe
    and preserves existing order.
  - **Identity survives a rename (the D7/rename_stable_id payoff):** select a
    camera, `scene::rename_camera`, and assert the selection still `contains` it
    and `pick_targets` still reports it at the same index — the assertion that
    would fail if a rename ever went back to minting a new `ObjectId`.
  - **Selection is not a transaction (Constraint 2, at L1):** every mutation in
    this file runs against a live `Document` whose `journal().depth()` and
    `pin()->revision()` are captured before and after and asserted unchanged.

- **Rendered output — golden.** **No new golden file**; the assertion is
  byte-*in*variance, which is stronger here than a new baseline. Two Catch2 cases
  in `tests/selection_test.cpp` render a document holding two cells and a camera
  through `render::render_document_srgb8` at 64×64, capture the bytes, run a full
  click / Cmd-click-cycle / marquee / Select-All / Escape sequence against the
  same `AppState`, re-render, and require the byte vectors to be **identical** —
  pinning Constraint 13 (selection chrome never enters the composite) and
  Constraint 2 (no transaction, so nothing the compositor reads moved). The
  existing `tests/goldens/cells_insert_nested_64x64.rgba8` baseline is reused as
  the fixture's known-good starting image so a regression in the *render* path is
  distinguishable from a regression in *selection*.

- **UI e2e — ImGui Test Engine** (`tests/selection_e2e_test.cpp`, new file added
  to the `ace_shell_test` source list at `CMakeLists.txt:248-259`; modelled on
  `tests/camera_manip_e2e_test.cpp`, reusing its `ScratchDir`, `pump_until`, and
  the raw-position mouse-drive recipe at `:250-271`). Seeds a project with two
  overlapping cells and one camera framing them, then drives
  `"canvas#1/##canvas_nav"` by raw position and asserts against
  `state.selection()`:
  - click a cell body ⇒ `selection().primary()` is that cell, `size() == 1`;
  - click **inside the camera's frame but over a cell** ⇒ the **cell** is
    selected, not the camera (the D7 click-through assertion at the UI layer);
  - click the camera's **border** ⇒ the camera is selected **and** the existing
    frame grab still engages (drag + release still commits one
    `set_layer_transform`, proving Constraint 11 did not regress manip);
  - Cmd/Ctrl-click twice at a point where the two cells overlap ⇒ the selection
    walks front → back → front;
  - Shift-click a second cell ⇒ `size() == 2`; Shift-click it again ⇒ `size() == 1`;
  - drag from empty canvas across both cells ⇒ both selected; Shift-drag adds;
  - click empty canvas ⇒ `empty()`; `Cmd/Ctrl-A` selects all targets; `Escape`
    clears;
  - **Space held** during a press over a cell ⇒ the selection does **not** change
    and the view pans (Constraint 11);
  - across the whole sequence, `journal().depth()` and `pin()->revision()` are
    unchanged except for the one deliberate frame-grab commit (Constraint 2);
  - a screenshot baseline is captured after the two-cell marquee, where the
    outline chrome adds real signal.

- **Threading (ASan/TSan).** Selection mutates nothing, so there is no new writer
  path — but `pick_targets` now calls a `Content` virtual (`bounds()`) on the UI
  thread against a document a render thread is walking. One case appended to
  `tests/canvas_host_test.cpp` drives repeated `interact::pick_targets(document,
  registry)` + `interact::pick(...)` on the UI thread against a **live rendering**
  real-pool `CanvasHost` (`default_interactive_pool_config()`, the
  D-edit_render_sync-3 anchor) while inserts run through `apply_edit`, and asserts
  sanitizer-clean plus a stable final target count. This is the honest scope: it
  proves the lock-free `pin()` read seam covers the `bounds()` call, or surfaces
  it if it does not. No new lock and no new thread is introduced.

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed
  lines; clang-format + build clean. Tests ship with the task. The policy core is
  pure and fully unit-reachable; the L4 canvas wiring and the `views` chrome are
  covered by the e2e.

- **Deferred WBS work.** One follow-up, concrete and agent-implementable (the
  closer registers it as a real leaf with `effort`, `allocate team`, `depends`,
  and a `note` citing this refinement; it sits under `task cameras`
  (`tasks/00-editor.tji:257`), which milestone `m9_editor`
  (`tasks/99-milestones.tji:6-8`) already depends on via `editor.cameras`, so no
  milestone `depends` edit is needed):
  - **`editor.cameras.frame_selection`** — *1d*, `depends editor.cells.selection,
    editor.cameras.model`, under `task cameras`. Implement `docs/00-design.md:262-263`'s
    *"A **"frame selection"** command mints a camera fit to the current
    selection"*: compute the union of the selected targets' placed extents
    (`arbc::Affine::map_rect` over `interact::PickTarget`, a pure L1 helper beside
    `pick`), derive a frame + resolution from it, and dispatch
    `scene::add_camera` as one `commands::Command`, with a rail/inspector
    affordance and an e2e. It is a two-verb composition over seams this leaf and
    `editor.cameras.model` both ship, deliberately kept out of scope here because
    it *creates scene data* (a transaction) while everything in this leaf is
    transient.

  Nothing else is deferred: the transform handles, pivot, and snapping are
  `editor.cells.gizmo` (`:329`); the resolution readout and health badge are
  `editor.cells.resolution` (`:335`); Delete is `editor.cells.remove` (`:341`);
  the property sheet is `editor.panels.inspector` (`:351`); the list and overview
  surfaces over this same selection are `editor.panels.layers` (`:357`) and
  `editor.panels.overview` (`:363`); nested expand/enter + breadcrumb (D17) is
  `editor.panels.layers` (`:359`); zoom-to-selection and fit-to-cell are
  `editor.canvas.nav_aids` (`:213`); routing `ToolSelection` into pointer
  gestures is `editor.canvas.tool_dispatch` (`:200`) — all already-scheduled
  leaves.

## Decisions

- **D-selection-1 — Hit-testing lives in L1 `interact`, split into a
  primitive-only policy core and one `interact → scene` assembly adapter.**
  `ace/interact/pick.hpp` publishes `PickTarget{id, layer, kind, placement,
  extent}` and the pure functions `pick`, `pick_stack`, `pick_behind`, `marquee`,
  `click_selection`, `marquee_selection` over `std::span<const PickTarget>` —
  naming no `scene` type — plus exactly one adapter,
  `pick_targets(const arbc::Document&, const arbc::Registry&)`, which includes
  `<ace/scene/cell.hpp>` and `<ace/scene/camera.hpp>`.
  *Rationale:* the pick list must carry z-order, the cells/cameras split, and each
  target's extent — and no component below L3 other than `interact` can hold it.
  `commands` may not depend on `interact` (`check_levels.py:28`), `scene` may not
  depend on `interact` (`:26`), and `base` may not include `arbc/` at all, so no
  lower component can even name a type carrying an `arbc::Affine`. The edge is
  already in the §8 DAG (`docs/01-architecture.md:210`); it has simply never been
  exercised. Splitting policy from assembly keeps A16's real intent intact — the
  geometry helpers the Overview and the importer swap their own affines into stay
  primitive-only — while putting every rule that could be *wrong* (z-order,
  click-through, unbounded content, cycling) under headless Catch2.
  *Alternative rejected:* assemble the targets in L3 `views` or L4 `app`. Legal
  with zero doc delta, and it was the first choice — but it puts the merge rule
  in ImGui-linked code that `ace_tests` cannot link, and duplicates it once per
  surface (canvas, Layers list, Overview all need the same stack). Trading a
  narrow, already-declared edge for three copies of untestable logic is the wrong
  side of the trade.
  *Alternative rejected:* add a `commands → interact` edge and put picking beside
  `Selection`. That is a genuinely new DAG edge (this one is not) and drags
  interaction geometry into the transaction component, against A11's *"interact
  stays pure math."*
  *Alternative rejected:* extend `scene` with a `placed_objects()` accessor and
  keep `interact` scene-free. `scene` cannot name `interact::PickTarget` (no edge
  that direction), so the type would have to live in `scene` and `interact` would
  include it anyway — the same edge, with the geometry type in the wrong
  component. **Doc delta: A17.**

- **D-selection-2 — The selection state stays in `commands`; `interact` returns a
  `SelectionChange` value instead of mutating it.** `SelectionChange{SelectOp op;
  std::vector<arbc::ObjectId> ids;}` with `op ∈ {None, Replace, Add, Toggle,
  Clear}`; L4 applies it with a four-arm switch onto the shipped `Selection`
  verbs.
  *Rationale:* `interact` cannot see `commands` (`check_levels.py:27`) and should
  not — but the modifier policy (what Shift means on a miss, what Cmd/Ctrl means
  on a hit) is exactly the part most likely to be gotten wrong and most cheaply
  unit-tested. Returning it as a value gets full Catch2 coverage of the policy
  with no new edge, and gives the Layers list and the Overview the same policy for
  free when they land.
  *Alternative rejected:* pass a `Selection&` into `interact` — needs an
  `interact → commands` edge and inverts the dependency (`commands` already
  depends on `scene`, and `interact` depends on `scene`; adding
  `interact → commands` would make the L1 tier a near-complete graph for no gain).
  *Alternative rejected:* do the modifier policy in L4 alongside the ImGui read.
  That is where it naturally wants to live, but it is the highest-branch-count,
  lowest-visibility part of the feature and would be reachable only through the
  e2e. **No doc delta required.**

- **D-selection-3 — Cell body hits are tested in CONTENT space via the inverse
  placement, never against a placed AABB.** `placement.inverse()` (nullopt-guarded)
  maps the composition-space point into the content's own frame, where the test is
  a plain `Rect` containment.
  *Rationale:* placements are arbitrary affines (D3/D7 — rotation and shear are
  first-class, `docs/00-design.md:230-233`), so the placed shape is a
  parallelogram. An AABB test would let a click in the empty corner of a rotated
  cell's bounding box select it — visibly wrong, and wrong in exactly the way that
  makes a deep-zoom, arbitrary-placement editor feel broken. The inverse is one
  `Affine::inverse()` + one containment, already the cheapest correct option, and
  `Affine::inverse()` already returns `nullopt` for degenerate transforms so the
  fallback is free.
  *Alternative rejected:* `placement.map_rect(extent)` + AABB containment — one
  call cheaper, wrong for any rotation.
  *Alternative rejected:* rasterized/alpha hit-testing (pick only where the content
  is actually opaque). That is the "right" answer for a photo editor, but it needs
  a render of the cell at pick time, which is the render thread's job, off the UI
  thread — enormous machinery for a placement editor whose objects are rectangular
  by construction. Not deferred to a task; not wanted. **No doc delta required.**

- **D-selection-4 — Select-behind is derived from the current selection, with no
  hidden cycle state.** `pick_behind(targets, point, tol, selected)` returns the
  entry immediately *behind* the first stack member that is currently selected,
  wrapping to the front; with nothing selected (or nothing selected in this stack)
  it is `pick`.
  *Rationale:* a stateful cycle index would need an owner (which surface? reset on
  what — a pointer move, a zoom, a document edit?) and would drift out of sync the
  moment the Layers list or the Overview changes the selection. Deriving it makes
  the function pure, wrap-around trivially testable, and correct by construction
  when another surface changes the selection between clicks. The cost is that
  Cmd/Ctrl-clicking at a *different* point restarts from the front rather than
  continuing a cycle — which is the behaviour a user actually expects.
  *Alternative rejected:* a `{last_point, cycle_index}` pair on the `Presenter`.
  Per-canvas gesture state for a project-level selection, and it makes
  `pick_behind` untestable without a fixture. **No doc delta required.**

- **D-selection-5 — Marquee selects on TOUCH, uses exact quad-vs-rect overlap, and
  excludes unbounded content.** Any overlap between the marquee rect and a target's
  placed quad selects it (separating-axis test over four axes); a target with
  `extent == nullopt` is never marquee-selected.
  *Rationale:* touch-selection is the near-universal convention in placement
  editors and is the forgiving choice at deep zoom, where a fully-enclosing drag
  may be physically impossible on screen. `docs/00-design.md:224` says only
  *"Marquee to multi-select"*, and §11 (`:499-500`) leaves the input map open, so
  this is a refinement-level call. Exact overlap follows D-selection-3's reasoning
  for the same reason. Excluding unbounded content is the one place click and
  marquee deliberately disagree: a click over a solid is a real, informative hit
  (it *is* what you see there), while a marquee that always includes the solid
  conveys nothing and would make every rubber-band selection wrong.
  *Alternative rejected:* enclose-only (Illustrator's classic behaviour) — safer
  against over-selection, but at deep zoom the user often cannot get the whole
  object on screen to enclose it.
  *Alternative rejected:* a modifier to switch touch/enclose — a mode toggle for a
  gesture with no established second convention in this editor, and Shift and
  Cmd/Ctrl are already spoken for (`:257-258`).
  *Alternative rejected:* including unbounded content in the marquee — makes
  "select the two cells I dragged over" silently select three. **No doc delta
  required.**

- **D-selection-6 — Cameras sort above all cells in the pick stack.** `pick_targets`
  emits every cell in layer order, then every camera.
  *Rationale:* the true interleaved z-order is not recoverable from
  `scene::cells()` and `scene::cameras()`, which are independently filtered walks
  of the same layer list — and it does not matter, because a camera contributes
  **zero pixels** (A14, `camera.hpp:40-41`, `:46-48`): its outline is UI chrome drawn on top
  of the composite, so "above everything" is what the user sees. It is also what
  the shipped canvas already does (`canvas_view.cpp:299-312` draws frames after
  the image). Combined with D7's border-only grab, a camera can never steal a
  click from the art it frames, so the ordering costs nothing.
  *Alternative rejected:* add a merged z-ordered accessor to `scene` that walks
  the layer list once and tags each entry cell-or-camera. Strictly more faithful,
  and a reasonable future refactor — but it duplicates `cells()`/`cameras()` for a
  distinction with no observable consequence today, and both existing accessors
  have shipped consumers. Not deferred to a task; revisit only if a camera ever
  renders pixels. **No doc delta required.**

- **D-selection-7 — Stale selection ids are pruned every frame against the live
  target list.** `Selection::prune(std::span<const arbc::ObjectId> live)`.
  *Rationale:* undo, GC, and the imminent `editor.cells.remove` can all delete a
  selected object. Every consumer this leaf unblocks
  (`editor.panels.inspector` first) will resolve selected ids against the
  document, so a dangling id is a crash waiting on the next leaf. Pruning at the
  one place the live set is already computed makes it impossible to forget, costs
  a linear scan over a handful of ids, and needs no notification plumbing.
  *Alternative rejected:* subscribe the selection to document commits and prune on
  change. Needs a commit-sink hook the editor does not own (`Model::set_commit_sink`
  is already claimed, `docs/01-architecture.md:98-123`) and buys nothing over a
  per-frame scan at these sizes.
  *Alternative rejected:* validate lazily at each consumer. That is the same check
  written N times, and the first omission is a crash. **No doc delta required.**

- **D-selection-8 — Selection chrome in this leaf is an outline only; handles are
  `editor.cells.gizmo`'s.** A selected cell draws a stroked quad along its placed
  outline; a selected camera reuses the existing `draw_frame(..., active=true)`
  style; an in-progress marquee draws a rectangle.
  *Rationale:* `docs/00-design.md:230-235` puts the 8 scale handles, the rotate
  zone, the shear modifier, and the draggable pivot in the *cell gizmo*, which is
  a scheduled 3d leaf (`:329`). Drawing handles here that nothing can drag would be
  a lie in the UI, and drawing nothing would leave the feature invisible and the
  e2e screenshot signal-free. An outline is the exact amount of feedback the
  selection itself justifies.
  *Alternative rejected:* ship the handles inert now and wire them in
  `editor.cells.gizmo`. Inert affordances are worse than absent ones, and the
  gizmo leaf owns the hit zones anyway. **No doc delta required.**

- **D-selection-9 — Select-All is `Cmd/Ctrl-A` and Deselect is `Escape`, both
  scoped to the hovered canvas pane.** Both operate on the full `pick_targets`
  list (so they include cameras and unbounded cells).
  *Rationale:* `docs/00-design.md:499-500` explicitly leaves the full input map
  unwritten, and these two are the universally-expected companions to a selection
  model — two lines against verbs this leaf already adds. Scoping to the hovered
  pane matches how `F` (reset-to-fit) already works (`views.cpp:69-72`) and avoids
  stealing `Escape` from a modal. Select-All including cameras follows D7/D20:
  there is one select tool over one object shape, so "all" means all.
  *Alternative rejected:* defer the chords to `editor.canvas.tool_dispatch`. That
  leaf routes the *active tool* into pointer gestures; it has no natural claim on
  selection chords, and deferring would ship a selection model with no keyboard
  affordance at all. **No doc delta required.**

- **D-selection-10 — `views::CanvasInput` gains a press-anchor and a `super`
  modifier; nothing else about the input read changes.** Two `float`s
  (`press_x`, `press_y`, the pointer at the activation edge) and
  `bool super` (`io.KeySuper`), with the Cmd/Ctrl gate read as `in.ctrl ||
  in.super`.
  *Rationale:* a marquee needs its anchor, and `CanvasInput` reports only the live
  `focus_x/focus_y` (`views.hpp:52-53`). Capturing the anchor in L3 at
  `ImGui::IsItemActivated()` (`views.cpp:86`) keeps the L4 wiring stateless about
  where the drag began and matches how every other edge in that struct is read.
  `super` is needed because D7 says *"Cmd/Ctrl"* and the shipped `in.ctrl` is
  `io.KeyCtrl` only (`views.cpp:91`), which would make select-behind unreachable on
  macOS.
  *Alternative rejected:* track the anchor in the L4 `Presenter` on the `pressed`
  edge. It works — but it duplicates in L4 something L3 already has in hand, and
  every consumer of `CanvasInput` would have to repeat it.
  *Alternative rejected:* remap `in.ctrl` to `io.KeyCtrl || io.KeySuper`. It would
  silently change `editor.cameras.manip`'s bypass-snap gate, which is not this
  leaf's to change. **No doc delta required.**

- **D-selection-11 — `scene::Cell` gains a `content_bounds` field rather than the
  pick adapter reaching into `Document::resolve` itself.**
  `std::optional<arbc::Rect> content_bounds` is filled from
  `Content::bounds()` during the same pinned walk `scene::cells` already performs
  (`src/scene/cell.cpp:316-353`).
  *Rationale:* `cells()` already holds the pin, already resolves each `Content`
  for its kind token, and is already the single place cell facts are read — adding
  a field there is one line in the loop and zero extra resolves, whereas a second
  walk in `interact` would re-pin, re-resolve, and risk observing a different
  snapshot than the one that produced the placements. `editor.panels.inspector`
  ("placed size in composition units", `docs/00-design.md:234`),
  `editor.panels.layers`, `editor.panels.overview`, and `editor.canvas.nav_aids`
  (fit-to-cell) all need the same number, so it belongs on the shared struct. The
  field is additive; no existing caller changes.
  *Alternative rejected:* call `document.resolve(cell.id)->bounds()` from
  `pick_targets`. A second resolve per cell per frame against a possibly-different
  pin, for a value the first walk had in hand.
  *Alternative rejected:* a separate `scene::cell_bounds(document, id)` accessor —
  an N+1 read pattern for a list that is always consumed whole. **No doc delta
  required.**

## Open questions

(none — all decided.) Two items are recorded for human review rather than the
WBS (neither is an "audit" task; each is an observation a human decides whether
to act on), and go to `tasks/parking-lot.md`:

1. **`docs/01-architecture.md:167` still charters `scene/` as "cells · cameras ·
   **selection** · z-order", but the shipped `Selection` lives in `commands/`**
   (D-app_state-3, `src/commands/ace/commands/selection.hpp`). This leaf follows
   the shipped reality — moving a shipped, tested type across components to match
   a one-line charter would be churn with no behavioural change, and `commands` is
   the better home anyway (selection is app state that sits beside `AppState`, not
   a document projection). Whether to correct the §7 charter line is an editorial
   call for a human; the A17 row added here documents where hit-testing lives, and
   no code reads the charter.
2. **`arbc::Content::bounds()` is called from the UI thread while the render
   thread walks the same document.** The lock-free `pin()` seam covers the record
   walk, and `bounds()` is an immutable property for every kind shipped today —
   but the contract (`contract/content.hpp:487`) states no thread-safety
   guarantee, and a future kind whose bounds change under an `Editable` edit (a
   growing raster, per `editor.cells.resolution`'s "resample to crisp") would make
   it a real read/write pair. The TSan case under Acceptance criteria is the
   tripwire. If it ever fires, the fix is a libarbc-side contract statement, not
   an editor-side lock — an upstream-issue candidate for `ruoso/arbitrarycomposer`,
   not an editor WBS leaf.

The one deferred *implementation* follow-up
(`editor.cameras.frame_selection`) is named under Acceptance criteria for
mechanical registration.

## Status

**Done** — 2026-07-23.

- `src/interact/ace/interact/pick.hpp` + `src/interact/pick.cpp` — L1 pick policy core: `PickTarget`, `pick`, `pick_stack`, `pick_behind`, `marquee`, `click_selection`, `marquee_selection`, `SelectionChange`; all pure functions over `std::span<const PickTarget>`, no scene types.
- `src/interact/pick_targets.cpp` — sole `interact → scene` TU: `pick_targets(Document, Registry)` assembles cells (z-order, `content_bounds`) then cameras above them.
- `src/scene/ace/scene/cell.hpp` + `src/scene/cell.cpp` — `Cell::content_bounds` (`std::optional<arbc::Rect>`) filled during the pinned `scene::cells` walk.
- `src/commands/ace/commands/selection.hpp` + `src/commands/selection.cpp` — `replace_all`, `add_all`, `prune` verbs added to `commands::Selection`.
- `src/views/ace/views/views.hpp` + `src/views/views.cpp` — `press_x`, `press_y`, `super` fields added to `views::CanvasInput`.
- `src/app/ace/app/canvas_view.hpp` + `src/app/canvas_view.cpp` — `draw_selection` helper, marquee gesture state, `pick_targets`+`pick`/`marquee` wiring, selected-camera `active` frame style; Space-pan and existing frame-grab left intact.
- `tests/selection_test.cpp` — 15 Catch2 units covering body-hit under rotated+sheared affine, camera click-through/label, z-order, select-behind wrap, unbounded asymmetry, exact marquee SAT, modifier policy, degenerates, `pick_targets` assembly + unresolved-token passthrough, `prune`/`replace_all`/`add_all`, rename-preserves-identity, and two byte-invariance cases reusing `cells_insert_nested_64x64.rgba8`.
- `tests/canvas_host_test.cpp` — one TSan threading case: UI-thread `pick_targets`+`pick` vs. live real-pool render walk.
- `tests/selection_e2e_test.cpp` — ImGui Test Engine e2e `selection e2e: …` with screenshot baseline after two-cell marquee.
- `CMakeLists.txt` — new source files added to `ace_tests` and `ace_shell_test` targets.
- `docs/01-architecture.md` — A17 row (already present; confirmed untouched).
