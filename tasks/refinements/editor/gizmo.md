# editor.cells.gizmo — Transform gizmo: move / scale / rotate / shear; pivot; snapping

## TaskJuggler entry

- **Task:** `editor.cells.gizmo` (`tasks/00-editor.tji:527-532`, under
  `task cells "Cells & manipulation"` at `:512`).
- **Effort:** `3d` · `allocate team`.
- **Depends:** `!selection` (`editor.cells.selection`, `:530`).
- **Note (`.tji:531`):** "Move (body), scale (corners proportional-by-default,
  edges 1D), rotate (Shift 15deg), shear (modifier), draggable pivot; snapping to
  cell edges/centers, camera frames, grid. Handle-drag is PLACEMENT, never
  resample. Design: D7/D8."
- **Back-link:** the `.tji` note currently ends `Refinement:
  tasks/refinements/transform_gizmo.md` (an interim flat path that was never
  written). This refinement lands at **`tasks/refinements/editor/gizmo.md`**,
  beside its most recent sibling `editor/resolution.md`; the closer updates the
  note back-link to the real path and adds `complete 100` after `allocate team`
  (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji` here.
- **Downstream dependents:**
  - `editor.canvas.tool_dispatch` (`:245-250`, `depends … editor.cells.gizmo`) —
    routes `dockmodel::ToolSelection` into per-tool canvas behaviour; it consumes
    the gizmo as the Select tool's transform behaviour. The gizmo is therefore
    **upstream** of tool routing (see D-gizmo-2).
  - `editor.canvas.reset` / the `m9_editor` integration leaf (`:659`) lists
    `editor.cells.gizmo` among the pieces the editor milestone bundles.

## Effort estimate

**3 days.** One half has a shipped, end-to-end template; the other half is
greenfield math:

- **Templated (the camera gizmo):** `editor.cameras.manip` already shipped a
  full direct-manipulation gizmo — pure `arbc::Affine` handle math in `interact`
  (`hit_frame`/`recrop_frame`/`move_frame`/`dutch_frame`,
  `src/interact/ace/interact/interact.hpp:214-243`), preview-as-session-state on
  the `Presenter` (`src/app/ace/app/canvas_view.hpp:206-212`), a single
  `set_layer_transform` commit on release through `CanvasView::apply_edit`
  (`src/app/canvas_view.cpp:429-430`), and the whole Catch2 + golden + e2e + TSan
  test shape (`tests/camera_manip_test.cpp`, `camera_manip_e2e_test.cpp`,
  `tests/goldens/camera_manip_recrop_64x64.rgba8`). The cell gizmo parallels every
  one of these with a `draw_cell_gizmos` sibling and `gizmo_cell_*` Presenter
  fields.
- **Greenfield:** the cell handle set is **richer** than a camera frame's — 8
  scale handles (corners proportional-by-default, edges 1D), rotate zones, a
  **shear** modifier, and a **draggable pivot** — none of which the aspect-locked
  camera frame has; and the **cross-object snapping engine** (cell edges/centers,
  camera frames) that `editor.cameras.manip` explicitly deferred here
  (D-manip-6). No shear factory exists in `arbc::Affine` (agent-confirmed: shear
  is expressed directly through the `a,b,c,d` linear part,
  `transform.hpp:14-19`), so the scale/rotate/shear composition is written fresh.

No new component, no new DAG edge, no new external dependency, no libarbc change,
**no doc delta** (A11 already charters `interact` as "hit-test/gizmo/snapping/brush
math"). Coverage is Catch2-heavy (the transform + snap core is pure), one new
byte-exact `render_offline` golden, one e2e, one TSan case.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cells.selection`** (`tasks/refinements/editor.cells/selection.md`,
  Done 2026-07-23) — the direct predecessor: the thing there is to transform.
  - The one project-level `commands::Selection` on `AppState`
    (`src/commands/ace/commands/app_state.hpp:53-54`, `:94`), read **fresh each
    frame**; the gizmo reads `state_.selection().primary()` to find its target
    and holds no selection copy (Constraint 1).
  - `interact::PickTarget{id, layer, kind, placement, extent}`
    (`src/interact/ace/interact/pick.hpp:50-62`), where `layer` is documented as
    "what a transform edit targets" (`pick.hpp:53`) — the exact `ObjectId` the
    commit passes to `set_layer_transform`. `interact::placed_quad(const
    PickTarget&)` (`pick.hpp:80`) returns the placed parallelogram this leaf
    anchors handles to. `pick_targets(document, registry)` (`pick.hpp:202-203`)
    is the sole `interact→scene` adapter and supplies the snap-target list.
  - D-selection-8: selection ships an **outline only**; "the 8 scale handles, the
    rotate zone, the shear modifier, and the draggable pivot are in the *cell
    gizmo*." Constraint 11: a press over a cell body **selects and does nothing
    else — the move-drag is `editor.cells.gizmo`'s.**
  - `views::CanvasInput` already carries `press_x/press_y` (drag anchor),
    `focus_x/focus_y` (live pointer), and `shift/alt/ctrl/super` (Cmd/Ctrl read as
    `in.ctrl || in.super`) plus an `R` `rotate` gate (`src/views/ace/views/views.hpp:49-84`).
- **`editor.cameras.manip`** (`tasks/refinements/cameras/manip.md`, Done
  2026-07-22) — the load-bearing precedent.
  - **The commit discipline (D-manip-4), inherited verbatim:** a continuous drag
    **previews as session state** (the gizmo redraws at the dragged `Affine`, no
    journal churn); on release it commits a **single transaction = one undo step
    per gesture** through `CanvasView::apply_edit` (`canvas_view.cpp:429-430`,
    decl `canvas_view.hpp:92`, impl `:587-604` — posts to the one writer thread
    via `writer_.submit_sync`, then `host_.poke()`).
  - **Constraint 7:** the border-grab is the **always-on default** and does **not**
    wait on `editor.canvas.tool_dispatch`. This is the precedent for D-gizmo-2.
  - **D-manip-6 / Constraint 8** explicitly deferred to **this leaf**: "the
    **cross-object snapping engine** (cell edges/centers, camera frames, grid) is
    the shared engine `editor.cells.gizmo` owns," and "the visual transform-handle
    chrome cells and cameras share." Camera manip ships **no snapping**; it honours
    only the per-gesture modifier constraints.
  - The `interact` frame verbs (`recrop_frame`/`move_frame`/`dutch_frame`,
    `interact.hpp:231-243`) are the shape (primitive `Affine` + modifiers in, an
    `Affine` out, no `scene` type) the cell verbs sit beside.
- **`editor.cells.model`** (`tasks/refinements/editor.cells/model.md`, Done
  2026-07-22) — `scene::Cell{layer, placement, content_bounds}`
  (`src/scene/ace/scene/cell.hpp:189-200`), read back in z-order by
  `scene::cells(document, registry)` (`cell.hpp:214`). D-cells_model-4: placement
  affine and resolution are **independent inputs, never derived from one another**
  — the model-side statement of D8 the gizmo must never violate.
  `interact::place_in_view` (`interact.hpp:95-97`) is the "caller computes a
  finished `arbc::Affine`, `scene` never reads a viewport camera" purity rule the
  gizmo's transforms follow.
- **`editor.cells.resolution`** (`tasks/refinements/editor/resolution.md`, Done
  2026-07-28) — the **other half** of the placement-vs-resample rule. It owns the
  health badge (`interact::resolution_health`, `interact.hpp:287-288`) that flags a
  cell scaled soft by a gizmo drag; classification is off generic `Content`
  virtuals, never `kind_id`. The gizmo's job is strictly the **placement** side:
  scaling a cell up is non-destructive and merely gets soft, and the badge (owned
  there) is the fix affordance.
- **`editor.dock.tool_rail`** (`tasks/refinements/editor/tool_rail.md`, Done
  2026-07-17) — `dockmodel::ToolSelection` (active tool, default `Select`;
  `src/dockmodel/ace/dockmodel/tool_rail.hpp:49-56`) is **OBSERVABLE STATE ONLY —
  nothing on the canvas reads it yet** (`tool_rail.hpp:42-44`); wiring it to canvas
  behaviour is `editor.canvas.tool_dispatch`. The gizmo does not read it
  (D-gizmo-2).
- **`editor.cells.one_action_one_entry`** (Done 2026-07-28) — the current
  transaction/undo contract: **one user-visible action = one journal entry, one
  undo press** (D15). The proof obligation is **entry count + undo-wholeness**, not
  final state; `dispatch` diffs `doc.journal().depth()` and reads `pin()->revision()`
  (`src/commands/app_state.cpp:101-114`). A continuous transform gesture coalesces
  to one entry — already what D-manip-4 does. Also: `org.arbc.solid` is now
  **bounded by default** (arbc#22), so a factory-built solid is a normal,
  scalable, snappable gizmo target.
- **`editor.canvas.edit_render_sync` / `single_writer`** — A4.1/A4.1a
  (`docs/01-architecture.md:84-123`): every UI-thread `Document` **mutation** runs
  inside `CanvasView::apply_edit` (writer identity); reads are lock-free via
  `pin()`. The gizmo commit is a real writer-thread mutation (unlike selection),
  which is the threading surface it owes coverage for.

**Pending (owned here):** the L1 `interact` cell-gizmo verbs (handle hit-test,
move/scale/rotate/shear, pivot, and the snap engine), the L4 `draw_cell_gizmos`
overlay + `gizmo_cell_*` Presenter session state + the single-transaction commit,
the alignment-guide chrome, and the tests. Nothing downstream is blocked on an
unwritten predecessor.

## What this task is

Give a selected cell a **direct-manipulation transform gizmo** — the bounding box
and handles `docs/00-design.md:230-235` name — so a user drags the cell's
**placement** (the affine covering composition space) rather than typing numbers:

1. **Move** — drag the cell body; **arrow-nudge** by keyboard.
2. **Scale** — 8 handles: **corners proportional by default** (Shift = free
   distort), **edges = 1D** stretch, about the opposite handle (or the pivot with
   Alt).
3. **Rotate** — grab outside a corner; **Shift snaps to 15°**; about the pivot
   (default center).
4. **Shear** — modifier + edge (advanced); about the pivot.
5. **Pivot** — a draggable handle; **Alt** scales/rotates about it.
6. **Snapping** — the candidate placement snaps to **other cells' edges/centers**
   and **camera frames**, with on-canvas **alignment guides**; **Cmd/Ctrl bypasses
   snap**. Rotation snaps to 15° under Shift.

Every one of these changes **only the placing layer's `arbc::Affine`**, committed
as **one `Document::set_layer_transform` transaction on release** — one undo press.
It **never** touches the cell's stored pixels or its resolution (D8, the
load-bearing rule). During the drag the gizmo redraws at the previewed affine as
transient session state; nothing enters the journal until release.

The transform and snap math is **pure L1 `interact`** over `arbc::Affine` and
primitives (no `scene`/`commands`/ImGui type crosses the seam); the overlay chrome
and the pointer wiring are **L4 `app`** beside the shipped camera-frame gizmo.

**Not in scope, by prior decision:** the camera frame gizmo (`editor.cameras.manip`,
already shipped — this is the **cell** gizmo); the resolution readout / health
badge / resample control (`editor.cells.resolution`, `:533`); Delete
(`editor.cells.remove`); the property sheet (`editor.panels.inspector`); routing
`ToolSelection` into pointer gestures (`editor.canvas.tool_dispatch`, `:245`).
**Deferred here** (named under Acceptance criteria): **group transform of a
multi-selection** (`editor.cells.group_transform`) and **grid display + spacing +
snap-to-grid** (`editor.canvas.grid`).

## Why it needs to be done

Placement is the core verb of a resolution-independent editor: D7 reduces the
whole interaction to "**drag the extent, type the resolution — and the two are
always independent**" (`docs/00-design.md:216-217`). Selection made a cell
*pickable* but left it *immovable* on the canvas — the inspector's numeric
placement fields (`editor.panels.inspector`) and the overview's schematic drag
(`editor.panels.overview`) are the only ways to place a cell until this lands, and
neither is the on-canvas gesture D7/§6 promise.

It is also the second proof (after selection) of the D7 unification: the camera
gizmo already exists, so shipping the cell gizmo over the same `interact`/preview/
`set_layer_transform` machinery demonstrates that "cell and camera are the same
placed-object shape" is real in the manipulation path, not just the model.
`editor.canvas.tool_dispatch` cannot route "the Select tool transforms things"
until there is a transform behaviour to route.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **§6 "Cell gizmo"** (`docs/00-design.md:230-235`) — the normative handle spec,
  quoted because every acceptance criterion instantiates one clause: *"Bounding
  box with move (drag body; arrow-nudge), **scale** (8 handles — corners
  **proportional by default**, Shift = free distort; edges = 1D stretch),
  **rotate** (outside a corner; Shift snaps 15°), **shear** (modifier + edge;
  advanced), a draggable **pivot** (Alt = transform from pivot/center)."*
- **§6 "Placement is not resampling — the load-bearing rule"** (`:237-244`):
  *"Dragging handles changes the *affine* … it **never** touches stored pixels.
  Scaling a cell up is non-destructive … a handle-drag is never a resample."* The
  health-badge fix is `editor.cells.resolution`'s, not this leaf's.
- **§6 "Modifiers & snapping (shared)"** (`:257-263`): *"Shift constrains (aspect /
  15° / axis-lock); Alt from center; Cmd/Ctrl select-behind and bypass-snap; Space
  pans the *view***. Snap targets: cell↔cell edges/centers with alignment guides,
  **cell↔camera frame** … the composition grid, aspect presets, rotation angles."*
- **§6 "The unifying shape"** (`:210-217`) and **§6 "(overview)"** (`:265-269`) —
  the same gizmos edit the same affines live on canvas and (later) in the overview.
- **D7 — Manipulation model** (`:474`) and **D8 — Cell scale ≠ resample** (`:475`):
  *"Handle-drag changes **placement (affine)**, never resolution —
  non-destructive. Corners proportional-by-default (Shift free), edges 1D.
  Resampling is a separate explicit act."*
- **D15** (`:482`) — scene edits are transactions; *"continuous gestures coalesce
  to one step."* A transform is a scene edit and **is** undoable (unlike the
  transient viewport framing).
- **D20** (`:487`) — Select is the *one* tool; wiring `ToolSelection` to canvas
  behaviour is `editor.canvas.tool_dispatch`, downstream of this leaf.

**Governing architecture rows:**

- **A8 / §8** (`docs/01-architecture.md:308-344`) — the levelization DAG. `interact`
  is **L1**, "hit-test · gizmo · snapping · brush math (L1, UI-agnostic)"
  (`:292`), deps `{base, scene}` (`scripts/check_levels.py:30`); `scene` is L1
  (`:29`); `views` is L3 (`:34`); only `views`/`dock`/`app` may include ImGui
  (`check_levels.py:46`).
- **A11** (`docs/01-architecture.md:426`) — *"`interact` stays pure math
  (hit-test/gizmo/snapping/brush)"*; the tool→interaction dispatch is promoted
  into `interact` **when a canvas consumer exists** — `editor.canvas.tool_dispatch`,
  not this leaf. This charters the gizmo's home and is why **no doc delta** is
  needed.
- **A14** — the camera-as-placed-object shape, minted so the cell and camera reuse
  one transform machinery; this leaf is its second payoff after selection.
- **A17** (added by selection) — hit-testing lives in L1 `interact`, split
  primitive-only policy + one `interact→scene` adapter; the gizmo extends the
  primitive-only side (`placed_quad`, new handle/transform verbs).
- **A4 / A4.1 / A4.1a** (`:84-123`) — writer identity; the commit runs through
  `apply_edit`, reads stay lock-free via `pin()`.
- **§9** (`:346-410`) — the DoD table names cell math as L1-logic Catch2
  territory and **drag** in the e2e path.

**libarbc API surface** (fetched under `build/dev/_deps/arbc-src/`):

- `arbc::Affine` — `src/base/arbc/base/transform.hpp`: fields `a,b,c,d,tx,ty`
  (`:14-19`); `identity()` (`:21`), `translation`/`scaling` (`:22-23`),
  `apply(Vec2)` (`:25`), `inverse() -> std::optional<Affine>` (nullopt when
  degenerate, `:30`), `max_scale()` (`:35`), `map_rect(const Rect&)` (`:38`), free
  `compose(outer, inner)` (`:44`). **No `shear` factory** — a shear placement is
  the `a,b,c,d` linear part set directly.
- `arbc::Document::set_layer_transform(ObjectId layer, const Affine& transform)` —
  `src/runtime/arbc/runtime/document.hpp:244`. Host-facing wrapper over the model's
  transactional attach; **commits its own version, bumps the revision — one
  transaction = one journal entry = one undo press** (`:248-259`). WRITER-THREAD
  ONLY (reached through `apply_edit`).
- `arbc::Rect` / `arbc::Vec2` — `src/base/arbc/base/geometry.hpp`.

**Editor seams this leaf extends:**

- `interact::FrameHandle` / `is_resize_handle` / `hit_frame` / `recrop_frame` /
  `move_frame` / `dutch_frame` / `refit_frame_to_aspect` —
  `src/interact/ace/interact/interact.hpp:199-251` (the camera verbs the cell verbs
  sit beside).
- `interact::PickTarget` / `placed_quad` / `pick_targets` / `selected_extent` —
  `src/interact/ace/interact/pick.hpp:50-62`, `:80`, `:136-137`, `:202-203`.
- `scene::Cell{layer, placement, content_bounds}` / `scene::cells` —
  `src/scene/ace/scene/cell.hpp:189-200`, `:214`.
- `dockmodel::ToolSelection` — `src/dockmodel/ace/dockmodel/tool_rail.hpp:49-56`
  (read only later, by `tool_dispatch`).
- `views::CanvasInput` / `views::draw_canvas_interactive` —
  `src/views/ace/views/views.hpp:49-91`; impl `src/views/views.cpp:110-154`. The
  `##canvas_nav` InvisibleButton is the widget id the e2e drives (`canvas#N/##canvas_nav`).
- `app::CanvasView` — `draw_frame_gizmos` decl `src/app/ace/app/canvas_view.hpp:234-236`,
  impl `src/app/canvas_view.cpp:204-438`; the `gizmo_camera`/`gizmo_layer`/
  `gizmo_handle`/`gizmo_start_frame`/`gizmo_grab_comp`/`gizmo_res_w/h` session
  fields at `canvas_view.hpp:206-212`; the input read at `canvas_view.cpp:214`, the
  per-frame dispatch at `:261` (`draw_frame_gizmos`) and `:270` (`draw_selection`);
  the commit at `:429-430`; `apply_edit` decl `canvas_view.hpp:92`, impl `:587-604`.
- `scripts/check_levels.py:29-34` — the `ALLOWED` DAG; the `interact→scene` edge is
  already declared (`:30`), external-include gates at `:45-50`.

**Predecessor refinements:** `tasks/refinements/editor.cells/selection.md`,
`tasks/refinements/cameras/manip.md`, `tasks/refinements/editor.cells/model.md`,
`tasks/refinements/editor/resolution.md`, `tasks/refinements/editor/tool_rail.md`,
`tasks/refinements/editor.cells/one_action_one_entry.md`.

**Test rigs:** Catch2 units join `ace_tests` (`CMakeLists.txt:232`); ImGui Test
Engine e2e joins `ace_shell_test` (`:279`). Goldens are raw sRGB8 bytes under
`tests/goldens/` compared via `ace_test::compare_golden`. The hit-test unit mould
is `tests/camera_manip_test.cpp`; the raw-position mouse-drive recipe is
`tests/camera_manip_e2e_test.cpp` / `tests/selection_e2e_test.cpp:257-265`; the
real-pool threading suite is `tests/canvas_host_test.cpp`.

## Constraints / requirements

1. **Handle-drag is PLACEMENT only — never a resample (D8, the load-bearing
   rule).** Every gesture commits exactly `Document::set_layer_transform(cell.layer,
   affine)` and touches **nothing** else: not the content's resolution, not its
   stored pixels, not `content_bounds`. Asserted at L1 (the `Content`'s native
   resolution and `content_bounds` are byte-identical before and after every
   transform) and pinned by a golden that shows the scaled cell rendered **softer,
   never cropped or re-gridded**.

2. **Transform and snap math is pure L1 `interact`; only L4 touches ImGui and
   commits.** New verbs in `src/interact/ace/interact/interact.hpp` take/return
   `arbc::Affine` + primitive geometry + modifier bools, name **no** `scene`,
   `commands`, ImGui, GL, or SDL type, and live beside `recrop_frame`/`move_frame`/
   `dutch_frame`. `interact` gains no new include; `check_levels` needs **no
   edit** (the `interact→scene` edge for `pick_targets` already exists at
   `check_levels.py:30`). No new component, no new DAG edge, no doc delta (A11).

3. **A continuous gesture previews as session state and commits ONE transaction on
   release (D15/D-manip-4/one_action_one_entry).** During the drag the gizmo
   redraws at the previewed `Affine` held on the `Presenter` (mirroring
   `gizmo_start_frame`, `canvas_view.hpp:206-212`); **no** journal entry, **no**
   revision bump until release. On release, one `apply_edit(... set_layer_transform
   ...)` (`canvas_view.cpp:429-430`) = **one journal entry, one undo press**. The
   proof obligation is entry **count** (`journal().depth()` delta == 1) and undo
   restoring the exact prior affine, not final state. An **arrow-nudge** is one
   entry per discrete nudge.

4. **Exact affine geometry, never an AABB.** Handle hit-zones are anchored to the
   placed parallelogram (`interact::placed_quad`, `pick.hpp:80`); the pointer is
   mapped through `placement.inverse()` (nullopt-guarded) into content space for a
   handle test, exactly as selection's body hit-test does (D-selection-3). A
   rotated or sheared cell's handles sit on its rotated box, not an axis-aligned
   one. A non-invertible placement, a zero-area extent, and a non-finite pointer
   each yield a safe no-op — **no NaN written to the document**.

5. **Scale semantics per §6/D8.** Corner drag is **proportional by default**
   (uniform, about the opposite corner or the pivot with Alt); **Shift = free
   distort**. Edge drag is **1D** stretch along that edge's axis. This is the one
   place Shift **relaxes** rather than constrains, and it is exactly what
   `docs/00-design.md:231-232` specifies.

6. **Rotate and shear are modifier/zone-gated and pivot-relative.** Rotate is
   grabbed **outside** a corner (a zone beyond the corner handle); **Shift snaps to
   the nearest 15°** (reusing the `dutch_frame` `snap_15` idiom,
   `interact.hpp:242-243`). Shear is a **modifier + edge** drag (advanced). Both
   compose about the **pivot** (default = the placed box's center), which is
   **draggable**; **Alt** makes scale/rotate operate about the pivot/center per
   §6:233.

7. **Snapping is a pure engine over the other placed objects; Cmd/Ctrl bypasses
   it.** `interact::snap_placement(candidate, moving_extent, std::span<const
   PickTarget> others, tol) -> {Affine snapped, guides}` snaps the moving cell's
   edges/centers to **other cells' edges/centers and camera frames** (both drawn
   from `pick_targets`, the selected object excluded), emitting the **alignment
   guides** §6:260 names. Tolerance is in composition units, converted from a
   screen-pixel threshold by the L4 caller (the `edge_tol = px/scale` recipe,
   `canvas_view.cpp:208-224`). **Cmd/Ctrl held ⇒ no snap** (`in.ctrl || in.super`,
   §6:258). Grid lines and aspect-preset snapping are **out of scope** here
   (D-gizmo-5): the engine accepts an optional grid-line set that no caller
   populates yet.

8. **The gizmo is the always-on default when a single cell is the selection
   primary — NOT gated on `ToolSelection` (D-gizmo-2).** Like the camera frame grab
   (manip Constraint 7, D-tool_rail-4), the cell gizmo engages whenever exactly one
   **cell** is the selection `primary()`, independent of the active tool.
   `editor.canvas.tool_dispatch` (downstream, `depends editor.cells.gizmo`) later
   gates it under the Select tool; this leaf must **not** read
   `dockmodel::ToolSelection`.

9. **Single-object scope; the camera gizmo and multi-select are untouched
   (D-gizmo-6).** The gizmo transforms the selection **primary** when it is a
   **cell** (`scene::cells` kind, not a camera). A selected **camera** keeps the
   shipped `editor.cameras.manip` frame gizmo unchanged. A **multi-object**
   selection shows the selection outline (from `editor.cells.selection`) but **no
   transform gizmo** — group transform is deferred to `editor.cells.group_transform`.

10. **Space-pan and the camera frame grab stay exactly as they are.** Space held ⇒
    nav pan, inert on objects (manip Constraint 7, `canvas_view.cpp` Space gate). A
    press over a selected camera border still starts the existing frame grab. The
    cell gizmo adds hit-zones over the selected **cell** only.

11. **Reads are lock-free `pin()`; the one write goes through `apply_edit`.**
    Assembling the gizmo (target placement, snap targets via `pick_targets`) is the
    same per-frame `pin()` read selection already performs; the commit is the sole
    mutation and takes the writer identity (A4.1). This is a **real writer path**
    (unlike selection's read-only gestures), which the TSan case below scopes.

12. **Rendered pixels change only by the committed placement.** Gizmo handles,
    guides, and the pivot are ImGui draw-list overlay over the pane; they never
    enter the composited image. Between an *un*committed preview and the pre-gesture
    state the composite is byte-identical (preview is chrome only); after a
    committed transform the composite reflects exactly the new affine and nothing
    else.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella. `diff-cover --fail-under=90` on changed lines; tests ship with the task.

- **Levelization (`check_levels` clean) — the primary structural assertion.** No
  new component and **no `scripts/check_levels.py` edit**: the transform/snap verbs
  are added to L1 `interact` over `arbc::Affine`/primitives, and the sole
  `interact→scene` reach (`pick_targets` for snap targets) uses the edge already
  declared (`check_levels.py:30`). Asserted by inspection and by the lint:
  `src/interact/` gains no ImGui/GL/SDL and no `ace/commands`/`ace/dockmodel`
  include; `src/scene/`, `src/commands/`, `src/dockmodel/` gain no `ace/interact`
  include; only `src/app/` (L4) reads `views::CanvasInput` and commits
  `set_layer_transform`. **No doc delta** (A11 already charters this).

- **L1 logic — Catch2 unit** (`tests/gizmo_test.cpp`, new file added to the
  `ace_tests` source list at `CMakeLists.txt:232`; naming follows
  `tests/camera_manip_test.cpp`, i.e. `TEST_CASE("gizmo: …")`):
  - **Handle hit-test under an arbitrary affine:** for a cell with a rotated +
    sheared + translated placement, each corner/edge/rotate-zone/pivot handle is
    hit at its placed-quad anchor and missed for a point inside the placed box's
    AABB but off the handle — the assertion an AABB implementation fails; the body
    interior returns the Move grab.
  - **Move:** a body drag by `(dx,dy)` in composition units translates the
    placement by exactly `(dx,dy)`; arrow-nudge steps by the fixed unit; content
    `content_bounds` and native resolution are unchanged.
  - **Scale — proportional default, free under Shift, 1D on edges:** a corner drag
    with no modifier scales uniformly about the opposite corner (aspect preserved);
    the same drag with Shift distorts freely (independent x/y); an edge drag scales
    one axis only; with Alt the transform is about the pivot. In every case the
    *linear part changes and the content resolution does not* (D8).
  - **Rotate — 15° snap:** a rotate-zone drag rotates about the pivot by the swept
    angle; with Shift the result is the nearest multiple of 15°; the pivot, when
    dragged off-center, is the fixed point of the rotation.
  - **Shear:** a modifier+edge drag produces the expected off-diagonal linear part
    about the pivot; a plain edge drag does **not** shear (it 1D-scales).
  - **Snap engine (`snap_placement`):** a moving cell whose edge lands within
    tolerance of another cell's edge snaps flush and reports that guide; within
    tolerance of a camera frame edge snaps to it; **Cmd/Ctrl bypass** returns the
    unsnapped candidate with no guides; two competing snap targets resolve to the
    nearer; a candidate outside every tolerance is returned unchanged. The selected
    object is never its own snap target.
  - **D8 invariance (Constraint 1, at L1):** across every verb above, the resolved
    `Content`'s resolution and `scene::Cell::content_bounds` are captured before and
    after and asserted **identical** — placement moved, pixels did not.
  - **Degenerate inputs return safe no-ops, never NaN (Constraint 4):** a
    non-invertible placement, a zero-area extent, a non-positive scale drag that
    would collapse the box, and a non-finite pointer each leave the placement
    finite and unchanged (the D-fit_bounds fallback discipline).
  - **One transaction per gesture (Constraint 3, at L1):** driving a preview →
    commit sequence against a live `Document` leaves `journal().depth()` **+1** and
    `undo()` restores the exact prior `Affine` (byte-equal), for move, scale,
    rotate, and shear.

- **Rendered output — golden.** One new byte-exact `render_offline` golden
  `tests/goldens/gizmo_scale_64x64.rgba8` (modelled on
  `camera_manip_recrop_64x64.rgba8`): a document with one bounded raster cell is
  rendered at 64×64 through a fixed camera, the cell is scaled up ~2× via a corner
  handle drag and committed, and the re-render is compared byte-exact — proving the
  placement composes to exact pixels and the cell is **softer, not cropped or
  re-gridded** (D8). A second Catch2 case asserts **byte-invariance** of the
  composite between the pre-gesture state and an *un*committed preview frame
  (Constraint 12), reusing an existing baseline as the fixture image so a render
  regression is distinguishable from a gizmo regression.

- **UI e2e — ImGui Test Engine** (`tests/gizmo_e2e_test.cpp`, new file added to the
  `ace_shell_test` source list at `CMakeLists.txt:279`; modelled on
  `tests/camera_manip_e2e_test.cpp` / `tests/selection_e2e_test.cpp`, reusing their
  `ScratchDir`, `pump_until`, and the raw-position mouse-drive recipe
  `selection_e2e_test.cpp:257-265`). Seeds a project with a bounded cell and drives
  `"canvas#1/##canvas_nav"` by raw position:
  - select the cell, drag a corner handle, release ⇒ the placed size changed, the
    content resolution is unchanged, and `journal().depth()` rose by exactly 1;
    `undo` restores the exact prior placement (one press);
  - drag the cell body ⇒ it moves; drag it near a second cell's edge ⇒ it snaps
    (asserted on the committed affine) and an alignment guide was drawn (screenshot
    baseline where the guide adds signal); **Cmd/Ctrl-drag** does not snap;
  - a rotate-zone drag with **Shift** commits a 15°-multiple rotation;
  - **Space held** during a press over the cell ⇒ the placement does **not** change
    and the view pans (Constraint 10);
  - with **two** cells selected, no transform gizmo appears and a body drag does
    **not** transform (Constraint 9 / deferral to `group_transform`);
  - a selected **camera** still shows and drives the shipped frame gizmo unchanged
    (Constraint 9);
  - across the sequence, `journal().depth()` and `pin()->revision()` move by
    exactly the number of committed transforms.

- **Threading (ASan/TSan).** The gizmo commit is a **real writer-thread
  mutation**. One case appended to `tests/canvas_host_test.cpp` drives repeated
  `apply_edit(... set_layer_transform ...)` commits on the UI thread against a
  **live rendering** real-pool `CanvasHost` (`default_interactive_pool_config()`,
  the D-edit_render_sync-3 anchor) while `pick_targets`/gizmo assembly reads run
  each frame, and asserts sanitizer-clean plus a stable final placement — proving
  the writer-identity + `pin()` seam covers a rapid transform gesture stream. No
  new lock and no new thread is introduced.

- **Deferred WBS work.** Two follow-ups, each concrete and agent-implementable (the
  closer registers each as a real leaf with `effort`, `allocate team`, `depends`,
  and a `note` citing this refinement; both sit under milestones `m9_editor`
  already depends on via `editor.cells` / `editor.canvas`, so **no milestone
  `depends` edit is needed**):
  - **`editor.cells.group_transform`** — *1.5d*, `depends editor.cells.gizmo`,
    under `task cells` (`tasks/00-editor.tji:512`). Transform a **multi-object**
    selection: apply one affine delta about a shared pivot (the union of the
    selected placed extents, `interact::selected_extent`, `pick.hpp:136-137`)
    across **every** selected cell's placing layer, coalesced into **one** journal
    entry via `doc.transact(name).coalesce(key)` and a **batch** `commands::Command`
    (the natural home for the `transform_cells_command` this leaf's single-object
    path did not need — D-gizmo-3). Kept out of scope here because the single-object
    gizmo is the D7/§6 primitive and the batch-coalesce is a distinct concern.
  - **`editor.canvas.grid`** — *2d*, `depends editor.cells.gizmo`, under
    `task canvas` (`:216`). The **composition grid** §6:260 names: a display toggle
    + spacing setting (local UI state), and feeding the grid lines into the gizmo's
    already-general `snap_placement` engine (Constraint 7) so a placement snaps to
    grid. Deferred because snap-to-an-invisible-grid with no spacing control is a
    non-affordance; the engine is built to consume grid lines the moment a grid
    exists.

  Nothing else is deferred: the camera frame gizmo is `editor.cameras.manip`
  (shipped); the resolution readout / health badge / resample is
  `editor.cells.resolution` (`:533`); Delete is `editor.cells.remove`; the property
  sheet is `editor.panels.inspector`; the overview's schematic drag over the same
  affines is `editor.panels.overview`; routing `ToolSelection` into pointer
  gestures is `editor.canvas.tool_dispatch` (`:245`) — all already-scheduled
  leaves.

## Decisions

- **D-gizmo-1 — The transform and snap math lives in L1 `interact` as pure
  `arbc::Affine` verbs, beside the shipped camera-frame verbs.** New functions
  (cell-handle hit-test, `move`/`scale`/`rotate`/`shear` placement composers, a
  draggable-pivot helper, and `snap_placement`) take/return `arbc::Affine` +
  primitive geometry + modifier bools, name no `scene`/`commands`/`dockmodel`/ImGui
  type, and sit next to `recrop_frame`/`move_frame`/`dutch_frame`
  (`interact.hpp:231-243`).
  *Rationale:* A11 charters `interact` as exactly "hit-test/gizmo/snapping/brush
  math," and the camera gizmo already proved the shape; the whole transform/snap
  core is then reachable in headless Catch2 (the bulk of the coverage), and `views`
  cannot even link `ace_tests`. This is the levelization rule manip (D-manip-3) and
  selection (Constraint 8) both followed.
  *Alternative rejected:* do the affine composition in L4 `app` beside the ImGui
  input read — where it naturally wants to live, but it puts the highest-value,
  most error-prone geometry (shear, pivot-relative rotation, snap resolution) in
  ImGui-linked code reachable only through the e2e. **No doc delta** (A11 already
  covers it — this is the row's intended consumer).

- **D-gizmo-2 — The gizmo is the always-on default for a single selected cell, not
  gated on `ToolSelection`; `editor.canvas.tool_dispatch` (downstream) routes it
  later.** This leaf does not read `dockmodel::ToolSelection`; the cell gizmo
  engages whenever exactly one cell is the selection `primary()`.
  *Rationale:* the WBS DAG makes `editor.cells.gizmo` an **upstream** dependency of
  `editor.canvas.tool_dispatch` (`tasks/00-editor.tji:248`), so the gizmo cannot
  depend on tool routing — and the camera frame gizmo already set exactly this
  precedent (manip Constraint 7, D-tool_rail-4: "direct-manipulation transform is
  the always-on default … does NOT depend on tool_dispatch"). It is also what makes
  the gizmo **e2e-testable now**: selection exists, so a drag on a selected cell's
  handle is drivable without any tool wiring. `tool_dispatch` later narrows the
  default to "under the Select tool" by adding a branch it owns
  (`tool_rail.hpp:42-44`), touching no gizmo code.
  *Alternative rejected:* ship the gizmo inert and wire it on in `tool_dispatch`.
  It inverts the DAG (`tool_dispatch` depends on this leaf, not the reverse) and
  would ship a 3d feature with no way to exercise it. **No doc delta.**

- **D-gizmo-3 — The commit is a direct `Document::set_layer_transform` through
  `apply_edit`, not a new `commands::Command` or `scene` wrapper.** On release the
  gizmo runs `apply_edit([...]{ state_.document().set_layer_transform(cell.layer,
  preview); })` — the exact line the camera gizmo already ships
  (`canvas_view.cpp:429-430`).
  *Rationale:* `set_layer_transform` is already a single atomic transaction / one
  journal entry / one undo press (`document.hpp:244-259`), so the one-action-one-
  entry contract holds with no wrapper, and the proof (journal-depth delta == 1,
  undo restores the prior affine) uses the same instrument `dispatch` uses without
  a `Command`. There is no read-back or batching to encapsulate for a single object
  — a `scene::set_cell_placement` wrapper would be a pure passthrough (unlike
  `scene::cells`, which encapsulates the pinned read), and a `transform_cell_command`
  would add an L1 type and a call site for a verb the shipped camera gizmo commits
  directly. This is the reuse-the-existing-seam bias.
  *Alternative rejected:* a `commands::transform_cells_command`. Genuinely useful
  for the **batch** case, so it is named as the home for
  `editor.cells.group_transform`'s coalesced multi-object edit — but minting it here
  for one object is machinery ahead of a consumer. **No doc delta.**

- **D-gizmo-4 — Preview is transient `Presenter` session state; the journal sees
  only the release.** The gizmo redraws at the dragged `Affine` held on the
  `Presenter` (a `gizmo_cell` / `gizmo_start_placement` set mirroring the camera
  gizmo's `gizmo_start_frame` at `canvas_view.hpp:206-212`); nothing enters the
  journal until release, when one transaction commits.
  *Rationale:* D15's "continuous gestures coalesce to one step" and D-manip-4's
  shipped pattern — a per-frame journal write would flood undo and thrash the writer
  thread. The proof obligation is entry **count**, not intermediate state
  (one_action_one_entry D-…-5), so a preview that never touches the document is
  correct by construction.
  *Alternative rejected:* commit every frame and coalesce in the journal. The
  library supports coalescing (`transact(name).coalesce(key)`), but it is reserved
  for genuinely compound edits (manip's aspect-change case); a pure move/scale is
  one final affine, and previewing off-journal is simpler and cheaper. **No doc
  delta.**

- **D-gizmo-5 — Snapping ships the object-relative + rotation-angle targets; the
  composition grid is deferred with its display.** `snap_placement` snaps to other
  cells' edges/centers and camera frames (from `pick_targets`) and Shift snaps
  rotation to 15°; it accepts an **optional grid-line set** that no caller populates
  yet. Grid **display + spacing + snap wiring** is `editor.canvas.grid`.
  *Rationale:* §6:260 lists the grid among many snap targets (several of which —
  aspect presets, camera↔cell "frame this cell exactly" — are camera-side, not cell
  gizmo), but a grid has **no referent** in the editor yet: snapping to an invisible
  grid with no spacing control is a confusing non-affordance and cannot be
  meaningfully tested. The object-relative targets have live referents (the very
  cells and cameras on screen) and deliver the alignment guides §6 promises now;
  building the engine grid-line-ready makes the grid task pure wiring. manip
  (D-manip-6) grouped "grid" with the snapping engine it deferred here — this leaf
  honours that by shipping the engine and the object snaps, and splitting only the
  grid's missing UI to a named leaf.
  *Alternative rejected:* a provisional fixed-spacing grid snap with no display. It
  snaps to something the user cannot see and cannot configure — worse than absent,
  and it would need retracting when the real grid lands. **No doc delta.**

- **D-gizmo-6 — Single-object scope; multi-object group transform is a named
  follow-up; the camera gizmo is untouched.** The gizmo transforms the selection
  **primary** when it is a cell; a multi-selection shows the selection outline but
  no transform gizmo; a selected camera keeps the shipped
  `editor.cameras.manip` frame gizmo.
  *Rationale:* §6:230-235 describes the gizmo in the singular ("Bounding box"), and
  the single-object transform is the D7/§6 primitive. Group transform is a distinct
  concern — one delta about a shared pivot distributed across N placing layers and
  coalesced to one entry — with its own natural home (a batch command, D-gizmo-3),
  and is registered as `editor.cells.group_transform`. Retrofitting the camera
  frame gizmo onto the cell handle chrome (manip's deferred "shared visual chrome")
  is deliberately **not** pursued: the camera frame is an aspect-locked resize +
  dutch affordance with a different handle set that already works, so unifying the
  *draw code* would be churn with no behavioural change. The reusable anchor
  primitive both share already exists (`interact::placed_quad`). This is an
  observation for the parking lot, not a WBS leaf (there is no feature to ship, only
  a possible refactor).
  *Alternative rejected:* ship group transform now. It roughly doubles the gesture
  matrix (per-member vs. bounding-box handles, shared-pivot distribution) for a 3d
  leaf already carrying shear + pivot + a new snap engine; splitting it keeps each
  leaf closeable. **No doc delta.**

- **D-gizmo-7 — Handle hit-zones and all transforms use exact placed-quad
  geometry, never an AABB, and are nullopt/finite-guarded.** Handles anchor to
  `interact::placed_quad`; the pointer maps through `placement.inverse()` for a
  handle test; degenerate transforms and non-finite pointers are safe no-ops.
  *Rationale:* placements are arbitrary affines (rotation and shear are first-class,
  §6:230-233), so an AABB handle box would let a user grab a "corner" in the empty
  region of a rotated cell's bounding box — visibly wrong in exactly the way that
  makes an arbitrary-placement editor feel broken. This is selection's D-selection-3
  reasoning applied to handles, and `Affine::inverse()` already returns `nullopt`
  for degenerate transforms so the fallback is free.
  *Alternative rejected:* AABB handles with a rotation-aware pointer remap only for
  the drag. One cheaper containment test, but the *grab affordance* would be wrong
  before the drag even starts. **No doc delta.**

## Open questions

(none — all decided.) One item is recorded for human review rather than the WBS
(it is an observation, not an "audit" task), and goes to `tasks/parking-lot.md`:

1. **Unifying the camera frame gizmo onto the cell handle chrome (manip D-manip-6's
   deferred "shared visual chrome").** This leaf ships the richer cell handle set
   and the reusable placed-quad anchor both gizmos already share, but leaves the
   shipped, working camera frame chrome as-is (D-gizmo-6). Whether to later refactor
   the camera frame to draw over the same handle helpers is an aesthetic/consistency
   call a human should weigh against the churn of touching a shipped, tested gizmo
   with no behavioural change — it is not a feature and cannot be closed by an
   implementer, so it is not a WBS leaf.

The two deferred *implementation* follow-ups (`editor.cells.group_transform`,
`editor.canvas.grid`) are named under Acceptance criteria for mechanical
registration.

## Status

**Done** — 2026-07-29.

- Shipped an always-on direct-manipulation transform gizmo for the single selected cell: move/scale (8 handles)/rotate (15° snap)/shear about a draggable pivot with cross-object snapping + alignment guides (`src/interact/ace/interact/interact.hpp`, `src/interact/interact.cpp`).
- Added `hit_cell`, `snap_placement` + guide emission to the L1 pick layer (`src/interact/ace/interact/pick.hpp`, `src/interact/pick.cpp`).
- Added `gizmo_cell_*` Presenter session state, `draw_cell_gizmos`, per-frame dispatch, and grab-guard in `draw_selection` to the L4 canvas view (`src/app/ace/app/canvas_view.hpp`, `src/app/canvas_view.cpp`).
- Fixed click/drag disambiguation: a no-movement body release forwards to `click_selection` (restoring Ctrl-click select-behind); a real drag commits one `set_layer_transform` (`src/app/canvas_view.cpp`).
- New test files: `tests/gizmo_test.cpp` (15 Catch2 units covering hit-test/move/scale/rotate/shear/snap/D8-invariance/degenerate/one-transaction/preview-invariance and all 8 scale handles + Left/Right shear), `tests/gizmo_e2e_test.cpp` (ImGui Test Engine e2e with opaque backdrop fixture), `tests/goldens/gizmo_scale_64x64.rgba8` (byte-exact scale golden — 32×32 red over green, softer not cropped).
- Appended one TSan case to `tests/canvas_host_test.cpp` (cells.gizmo anchor: repeated `set_layer_transform` commits against a live rendering real-pool `CanvasHost`).
- Wired two new test files into `CMakeLists.txt`.
- Tech-debt follow-ups registered: `editor.cells.group_transform` (1.5d) and `editor.canvas.grid` (2d).
