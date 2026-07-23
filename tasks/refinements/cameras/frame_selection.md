# editor.cameras.frame_selection — Mint a camera fit to the current selection's placed extent

## TaskJuggler entry

- **Task:** `editor.cameras.frame_selection` (`tasks/00-editor.tji:306-311`, under
  `task cameras "Cameras"` at `:257`).
- **Effort:** `1d` · `allocate team`.
- **Depends:** `editor.cells.selection` (`:329-335`, `complete 100` at `:332`) and
  `!model` = `editor.cameras.model` (`:258-264`, `complete 100` at `:261`) — both
  dependencies are satisfied.
- **Note (`.tji:310`):** "Implement docs/00-design.md:262-263 'frame selection'
  command: compute the union of the selected targets' placed extents
  (`arbc::Affine::map_rect` over `interact::PickTarget`, a pure L1 helper beside
  `pick`), derive a frame + resolution from it, and dispatch `scene::add_camera` as
  one `commands::Command`, with a rail/inspector affordance and an e2e.
  Source-of-debt: `tasks/refinements/editor.cells/selection.md`. Design:
  `docs/00-design.md:262-263` §6."
- **Back-link:** the `.tji` note currently ends `Source-of-debt:
  tasks/refinements/editor.cells/selection.md. Design: docs/00-design.md:262-263
  §6.` — it carries **no** `Refinement:` pointer yet (this leaf was registered as
  tech debt by `editor.cells.selection`, not written ahead as a flat interim path).
  This refinement lands at **`tasks/refinements/cameras/frame_selection.md`** per
  the area-subdir layout (`tasks/refinements/README.md:9-18`); the closer appends
  the back-link to the note and adds `complete 100` after `allocate team`
  (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji` here.
- **Source of debt:** registered by `editor.cells.selection` as a deferred WBS leaf
  (`tasks/refinements/editor.cells/selection.md:546-556`): *"compute the union of
  the selected targets' placed extents … and dispatch `scene::add_camera` as one
  `commands::Command`, with a rail/inspector affordance and an e2e. It is a
  two-verb composition over seams this leaf and `editor.cameras.model` both ship,
  deliberately kept out of scope here because it **creates scene data** (a
  transaction) while everything in this leaf is transient."*
- **Downstream dependents:** none in the `.tji` today — nothing `depends` on this
  leaf. It nevertheless opens the seam three later leaves consume: the
  `commands::add_camera_command` verb and the `ProjectGateway` mint pair are what
  `editor.cameras.new_shot_from_view` (registered by this refinement, see
  Acceptance criteria), `editor.panels.inspector` (`:359`) and
  `editor.panels.overview` (`:371`) reuse for their own camera-creation
  affordances.

## Effort estimate

**1 day.** Every seam this leaf needs is shipped; the new code is two pure L1
geometry functions, one L1 command verb, two gateway virtuals, one rail item, and
a ten-line L4 join.

- The **scene verb exists and is exactly right**: `scene::add_camera(Document&,
  const Registry&, const std::string& name, Resolution, const arbc::Affine& frame)`
  (`src/scene/ace/scene/camera.hpp:151-153`, impl `src/scene/camera.cpp:501-528`)
  already takes precisely the `(name, resolution, frame)` triple this leaf
  computes. No `scene` change at all.
- The **selection-side geometry is shipped**: `interact::PickTarget{id, layer,
  kind, placement, extent}` (`src/interact/ace/interact/pick.hpp:50-62`) already
  carries the placed-extent inputs for **both** cells and cameras, assembled by the
  single A17 adapter `interact::pick_targets(document, registry)` (`pick.hpp:182`),
  and `placed_quad` (`pick.hpp:80`, impl `src/interact/pick.cpp:122`) is the mould
  for the degenerate-input discipline the new helper copies.
- The **`(frame, resolution)` return shape already exists**:
  `interact::ShotFraming{frame, width, height}`
  (`src/interact/ace/interact/interact.hpp:94-97`) was minted by
  `editor.cameras.model` for `new_shot_from_view` (`:109`). This leaf adds a second
  producer of the same struct, not a second shape.
- The **command seam is shipped with a mould to mirror**: `commands::Command` /
  `dispatch` (`src/commands/ace/commands/app_state.hpp:110-126`) and
  `insert_cell_command(registry, kind_id, config, placement, outcome)`
  (`src/commands/ace/commands/cells.hpp:37`, impl `src/commands/cells.cpp:10-23`)
  — the errors-are-values, outcome-by-reference, caller-computes-the-affine shape
  this leaf copies verbatim for cameras.
- The **rail + gateway inversion is shipped and has an exact precedent**: the Edit
  section `draw_edit_section` (`src/dock/dock.cpp:200-215`, called `:395`) and the
  `can_delete()`/`delete_selected()` non-pure virtual pair
  (`src/dock/ace/dock/dock.hpp:187-188`) added by `editor.cells.remove`. This leaf
  adds a third item and a third virtual pair of the same shape.
- The **L4 join has a line-for-line template**:
  `AppProjectGateway::insert_cell` (`src/app/project_gateway.cpp:210-247`) already
  computes a placement affine at L4 from `interact::place_in_view`, hands it to a
  `commands` factory as a value, and runs the `dispatch` inside `run_edit`
  (`:112-122`).
- **`arbc::Affine::map_rect(const Rect&)`**
  (`build/dev/_deps/arbc-src/src/base/arbc/base/transform.hpp:38`) is the AABB of
  the four mapped corners — the exact primitive the `.tji` note names, already used
  at `src/interact/interact.cpp:79`.

New code: two functions in `interact`, one `commands/cameras.{hpp,cpp}` pair, two
`ProjectGateway` virtuals, one rail item, one gateway override — plus the tests. No
new component, no new DAG edge, no new external dependency, no libarbc change. One
doc delta (**D23**), because the design log has no row for camera *creation* at all.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cells.selection`** (`tasks/refinements/editor.cells/selection.md`) —
  the project-level `commands::Selection` keyed by content `ObjectId`
  (D-selection-1, `src/commands/ace/commands/selection.hpp:18-62`), the
  `interact::PickTarget` shape and its `extent`-is-content-space /
  `nullopt`-means-unbounded contract (`pick.hpp:57-61`, D-cells_model-3), the A17
  policy/assembly split, and **D-selection-6**: cameras are first-class selection
  members that sort above cells. This leaf reads all of that and mutates none of it.
- **`editor.cameras.model`** (`tasks/refinements/cameras/model.md`) —
  `scene::add_camera` and its documented **two-journal-entry** create with a
  one-undo observable contract (`camera.hpp:145-153`), `scene::Camera` /
  `scene::cameras` (`camera.hpp:123-134`), `scene::Resolution`
  (`camera.hpp:26-31`), `register_camera_kind` threaded through
  `commands::register_editor_kinds` (`src/commands/app_state.cpp:29`), and
  `interact::ShotFraming` / `new_shot_from_view` (`interact.hpp:94-109`) — the
  existing "mint a (frame, resolution) pair" precedent this leaf generalizes.
- **`editor.cameras.manip`** (`tasks/refinements/editor/manip.md`) — D9's
  frame≠resolution split as implemented: the frame is the binding layer's
  `arbc::Affine`, oriented **device → composition**, placing the output rectangle
  `[0,native_w]×[0,native_h]` into composition space (`interact.hpp:143-145`); the
  aspect-lock rule that keeps pixels square; and **D-manip-2/D-manip-3**: `interact`
  geometry helpers stay **primitive-only** and never see a `scene::Resolution`.
- **`editor.cells.model`** (`tasks/refinements/editor.cells/model.md`) — the
  `commands::Command`/`dispatch` shape for a scene edit (D-cells_model-7, which
  already accepted a two-entry create rather than inventing a bespoke atomic
  composition) and Constraint 5: **every UI-driven mutation runs inside
  `CanvasView::apply_edit`**.
- **`editor.cells.remove`** (`tasks/refinements/editor.cells/remove.md`) — the
  `ProjectGateway` gate+verb pair with **inert non-pure defaults** so existing
  gateway fakes need no churn (D-cells_remove-6), the retitled **Edit** rail
  section, and **D-cells_remove-3**: targets are resolved **inside** the edit
  closure on the writer thread against the live document, never from a UI-side
  snapshot taken frames earlier.
- **`editor.canvas.single_writer`** — `CanvasView::apply_edit`
  (`src/app/ace/app/canvas_view.hpp:78` → `src/render/ace/render/canvas_host.hpp:104`)
  is the writer-identity seam; `add_camera` opens transactions, so it must ride the
  shipped edit runner (`src/app/project_gateway.cpp:112-122`).
- **`editor.canvas.nav` / `editor.canvas.fit_bounds`** — **D-fit_bounds-3**, the
  fallback discipline every `interact` helper obeys: a degenerate input yields a
  safe identity/empty result, never a NaN.

**Pending (owned here):** nothing. Every predecessor is `complete 100`; this leaf
adds no dependency of its own.

## What this task is

Turn "the things I have selected" into "a camera that frames exactly those things".
`editor.cells.selection` shipped a project-level selection with exact placed
geometry; `editor.cameras.model` shipped a camera whose frame is an affine and
whose resolution is a pixel count. This leaf composes the two into the one command
the design doc names at `docs/00-design.md:262-263`, and ships the **first
camera-creation affordance in the editor** — `grep` finds no production caller of
`scene::add_camera` today, and the camera inspector currently advertises a
creation path that does not exist (`src/app/camera_inspector.cpp:41`, *"No
cameras — create a shot from the view."*).

1. **L1 `interact` (pure, beside `pick`)** — in
   `src/interact/ace/interact/pick.hpp` + `src/interact/pick.cpp`:
   `std::optional<arbc::Rect> selected_extent(std::span<const PickTarget> targets,
   std::span<const arbc::ObjectId> ids)` — the axis-aligned union of
   `target.placement.map_rect(*target.extent)` over the targets whose `id` is in
   `ids`. Unbounded members (`extent == nullopt`) are skipped; a degenerate or
   non-finite contribution is skipped; `nullopt` when nothing bounded remains.
2. **L1 `interact` (pure, beside `new_shot_from_view`)** — in
   `src/interact/ace/interact/interact.hpp` + `src/interact/interact.cpp`:
   `ShotFraming shot_from_extent(const arbc::Rect& extent)` — the D23 derivation:
   resolution = the extent at 1 composition unit per pixel, rounded and clamped to
   `k_max_mint_resolution` on the longer side; frame = the extent **expanded about
   its center** to the rounded resolution's aspect (never cropped), as the
   device→composition affine `{a = w'/W, b = 0, c = 0, d = h'/H, tx, ty}`. A
   degenerate extent yields `{identity, 0, 0}` — the caller's "nothing to frame"
   sentinel.
3. **L1 `commands`** — a new `src/commands/ace/commands/cameras.hpp` +
   `src/commands/cameras.cpp` pair, mirroring `cells.{hpp,cpp}`:
   - `struct AddCameraOutcome { arbc::ObjectId camera; std::string error; };`
   - `Command add_camera_command(const arbc::Registry&, std::string name,
     scene::Resolution, const arbc::Affine& frame, AddCameraOutcome&)` — exactly
     one `scene::add_camera`.
   - `std::string next_camera_name(const arbc::Document&)` — `"Camera <n>"` for the
     first `n ≥ 1` not already a camera name in `scene::cameras()`.
   - `bool can_frame_selection(const AppState&)` — the rail gate.
4. **L3 `dock`** — two `ProjectGateway` virtuals with inert non-pure defaults,
   mirroring `can_delete`/`delete_selected` (`dock.hpp:187-188`):
   `virtual bool can_frame_selection() const { return false; }` and
   `virtual bool frame_selection() { return false; }`. A rail item
   `Frame Selection###frame_selection` in the Edit section
   (`src/dock/dock.cpp:200-215`), disabled (not hidden) when the gate is false.
5. **L4 `app`** — `AppProjectGateway::can_frame_selection/frame_selection`
   (`src/app/ace/app/project_gateway.hpp` + `src/app/project_gateway.cpp`): the
   ten-line join that L1 structurally cannot host (`commands` may not include
   `interact`), performed **inside** `run_edit` on the writer thread —
   `pick_targets` → `selected_extent` → `shot_from_extent` → `add_camera_command`
   → `dispatch`.

It deliberately does **not** ship: an inspector "Frame Selection" button
(`editor.panels.inspector`, `.tji:359`, which builds the selection-keyed property
sheet and reuses these two virtuals — the same split `editor.cells.remove` made for
Delete); a keyboard chord (D-frame_selection-8); a "New Shot From View" rail action
(registered as a named follow-up, see Acceptance criteria); the `camera↔cell
bounds` **snap** target of `docs/00-design.md:261` (`editor.cells.gizmo`,
`.tji:336-341`, whose note already names "snapping to cell edges/centers, camera
frames, grid"); re-framing an *existing* camera to the selection (that is the
gizmo's re-crop, `editor.cameras.manip`, shipped); dutch rotation on the minted
frame (D9, modifier-gated, gizmo-only); and any change to which camera a canvas
looks through (`editor.cameras.look_through`, shipped).

## Why it needs to be done

The design doc promises a specific command in one sentence
(`docs/00-design.md:262-263`) and nothing in the tree implements it. More
structurally: the editor can create cells but **cannot create cameras** through the
UI at all. `scene::add_camera` has been shipped and tested since
`editor.cameras.model`, and every call site in the repository is a test
(`tests/camera_model_test.cpp`, `tests/selection_e2e_test.cpp:123`,
`tests/cells_remove_e2e_test.cpp:144`, `tests/look_through_e2e_test.cpp:186`,
`tests/camera_manip_e2e_test.cpp:183`) — the gizmo, the resolution inspector, the
look-through picker, and `editor.cameras.export` all operate on cameras a user
cannot make. That makes three shipped leaves reachable only from a test fixture.

It is also the piece that gives `editor.cells.selection` an output. The selection
model shipped exact placed geometry — `PickTarget::extent` in content space,
`placed_quad`'s exact parallelogram, the marquee's separating-axis test — and today
that geometry is consumed only to draw an outline and to decide what Delete
removes. Framing is the first verb that reads the selection's **shape** rather than
its membership, which is what turns "the union of the placed extents" from a
comment into a tested function that `editor.canvas.nav_aids`' zoom-to-selection
(`.tji:213`) and `editor.panels.overview` will both want.

Finally it closes D14's ad-hoc-crop story: *"ad-hoc crop = frame a camera"*
(`docs/00-design.md:481`). Export takes a camera as its spec, so "export exactly
this region" has no expression until a camera can be minted from a region.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **§6 Direct manipulation** (`docs/00-design.md:208`), the shared modifiers
  paragraph (`:257-263`), whose last sentence is this leaf: *"A **"frame
  selection"** command mints a camera fit to the current selection."*
  (`:262-263`). It sits immediately after the snap-target list naming
  **`camera↔cell bounds` ("frame this cell exactly")** (`:260-261`) — so the
  command is the one-shot form of an intent the gizmo will also offer as a snap.
- **D7 — Manipulation model** (`:474`): *"Cells and cameras share **one shape**
  (affine placement + a resolution number) and **one select tool** … Drag the
  extent, type the resolution; the two are always independent."* This is why a
  selected **camera** contributes its own placed extent to the union like any cell
  (D-frame_selection-3).
- **D8 — Cell scale ≠ resample** (`:475`): handle-drag changes placement, never
  resolution. Read back through a camera, this is what makes the 1-unit-per-pixel
  derivation the honest default (D-frame_selection-2).
- **D9 — Camera frame ≠ resolution** (`:476`): *"Frame resize = **re-crop**,
  aspect-locked to resolution, **holds resolution**; resolution … is edited
  separately in the inspector."* The aspect-lock is why the minted frame is
  expanded to the rounded resolution's aspect rather than left at the raw extent's
  (D-frame_selection-2); "edited separately in the inspector" is the escape hatch
  that makes a derived default acceptable rather than a lock-in.
- **§6 camera gizmo** (`:246-255`): *"a bigger frame captures more scene at the
  same pixel count (wider, lower effective PPI); smaller = tighter, higher PPI"* —
  the PPI semantics the derived resolution has to respect.
- **D14 — Export via cameras** (`:481`): *"a camera **is** the spec … ad-hoc crop =
  frame a camera."*
- **D15 — Undo = library transactions** (`:482`): *"a **saved shot's** framing is
  scene data and IS [undoable]"* — the mint is a transaction; the transient
  viewport camera is untouched.
- **D19 — Project-scoped state** (`:486`): one project-level selection shared by
  every canvas and panel, so Frame Selection is a **project** verb on the rail, not
  a canvas verb.
- **D20 — Tool rail modal set** (`:487`): *"likewise ad-hoc crop = frame a camera
  (D14), **not a mode**"* — the design log already rules, by name, that this verb
  is a rail **action** and never a fifth `ToolId`.
- **D23 — Minting a camera** (`:490`) — **added by this leaf** (see Decisions): the
  shared derivation rule for both mint verbs, the auto-name, the
  don't-select / don't-activate rule, and the refuse-on-no-bounded-extent rule.
- **§11 open item — the full input map** (`:499-500`): *"The complete
  keyboard/shortcut set … (the pieces are decided; the map isn't written)."* The
  standing licence under which D-undo-3, D-selection-9 and D-cells_remove-5 minted
  chords — and the reason this leaf deliberately mints none (D-frame_selection-8).

**Governing architecture rows:**

- **A8** (`docs/01-architecture.md:299`) + **§8** (`:185-221`, the edge table
  `:203-216`) — the DAG. The three edges that decide this leaf's shape:
  `interact → {base, scene}` (`:210`), `commands → {base, project, scene}` (`:211`,
  **no `interact`**), `dock → {dockmodel, views, imgui}` (`:215`). Enforced by
  `scripts/check_levels.py:21-37` (`"commands"` at `:28`, `"interact"` at `:27`).
- **A9** (`:300`) + **§9** (`:222-249`) — the layered DoD this leaf's Acceptance
  criteria instantiate.
- **A12/A13** (`:303-304`) + **A16** (`:307`) — the `ProjectGateway` dependency
  inversion. Frame Selection is the eighth session-acting verb to cross it, after
  Save / Save As / Clean up / Undo / Redo / Insert / Delete, and like Delete it
  needs no dock-local POD — two `bool`s carry everything.
- **A14** (`:305`) — a camera is one `Content` of kind `org.arbc.camera` plus one
  `Layer` whose `Affine transform` is the frame; the kind is **non-rendering**
  (`camera.hpp:47-48`), which is what makes the golden assertion byte-*invariance*
  rather than a new baseline. A14 also states the creation policy verbatim:
  *"Mutations (**create** · rename · 'new shot from view') are **`commands`
  transactions** (D15)."*
- **A17** (`:308`) — hit-testing is split into a primitive-only pick **policy** and
  exactly **one** `interact → scene` assembly TU (`src/interact/pick_targets.cpp`).
  Both new `interact` functions land on the **policy** side: they take spans of
  `PickTarget` / an `arbc::Rect` and return an `arbc::Rect` / a `ShotFraming`, so
  no second assembly TU appears.
- **A4/A4.1** (`:295`, `:84-123`) — writes are bound to one writer *identity*;
  `scene::add_camera` is WRITER-THREAD ONLY (`camera.hpp:145-150`).

**libarbc API surface** (fetched at tag `v0.2.0`, `CMakeLists.txt:25`; headers
under `build/dev/_deps/arbc-src/src/`):

- `arbc::Affine` (`base/arbc/base/transform.hpp:13-41`): fields `a,b,c,d,tx,ty`
  (`:14-19`); **`Rect map_rect(const Rect& r) const` `:38`** — the AABB of the four
  mapped corners, the primitive the `.tji` note names; `apply(Vec2)` `:25`,
  `det()` `:26`, `inverse()` `:30`, `max_scale()` `:35`; free `compose(outer,
  inner)` `:44`.
- `arbc::Rect` (`base/arbc/base/geometry.hpp:15-42`): `x0,y0,x1,y1` (`:16-19`),
  `from_size` `:21`, `width()` `:32`, `height()` `:33`, `empty()` `:34`,
  `intersect` `:36`. **There is no public `union`/`unite` member** — the union is
  min/max over the four coordinates, editor-side, which is exactly why the `.tji`
  note asks for "a pure L1 helper beside `pick`".
- `arbc::Document` (`runtime/arbc/runtime/document.hpp`): `add_content` `:107`
  (**self-commits** — this is why a create costs two journal entries), `transact`
  `:179`, `journal()` `:185-186`, `pin()` `:262`, `resolve` `:266`.
- `arbc::ObjectId` (`base/arbc/base/ids.hpp:11-17`): `valid()` is `value != 0`.

**Editor seams this leaf extends:**

- `interact::PickTarget` / `PickKind` — `src/interact/ace/interact/pick.hpp:42-62`;
  `placed_quad` `:80` (impl `src/interact/pick.cpp:122`); `marquee` `:113`;
  `all_ids` `:117`; the assembly adapter `pick_targets` `:182-183` (impl
  `src/interact/pick_targets.cpp`).
- `interact::ShotFraming` / `new_shot_from_view` —
  `src/interact/ace/interact/interact.hpp:94-97`, `:109` (impl
  `src/interact/interact.cpp:97-111`); the frame orientation contract `:143-145`;
  `place_in_view` `:84-86`; `fit` `:56`; `refit_frame_to_aspect` `:208`; the
  degenerate-fallback discipline `src/interact/interact.cpp:65-93`.
- `scene::add_camera` — `src/scene/ace/scene/camera.hpp:151-153` (contract
  `:145-150`), impl `src/scene/camera.cpp:501-528`; `scene::Camera` `:123-129`;
  `scene::cameras` `:134`; `scene::Resolution` `:26-31`; `rename_camera` `:165-166`
  (the post-mint rename escape hatch).
- `commands::Command` / `DispatchOutcome` / `dispatch` —
  `src/commands/ace/commands/app_state.hpp:110-126`, impl
  `src/commands/app_state.cpp:55-65`; `AppState::registry()` `:50`,
  `selection()` `:53-54`, `is_dirty()` `:69`. The header's own reservation at
  `:103-108` — *"those `Command`s land with their edit leaves (`editor.cells.*`,
  `editor.camera.*`)"* — is the slot `commands/cameras.hpp` fills.
- `commands::insert_cell_command` + `InsertCellOutcome` —
  `src/commands/ace/commands/cells.hpp:21-26,37`, impl `src/commands/cells.cpp:10-23`
  (the outcome-by-reference mould); `selected_removals` `:64` and `can_delete`
  `:105` (the resolve+gate mould).
- `commands::Selection` — `src/commands/ace/commands/selection.hpp:18-62`
  (`items()` `:24`, `empty()` `:20`, `contains()` `:30`), pruned per frame at
  `src/app/canvas_view.cpp:388-389`.
- `dock::ProjectGateway` — `src/dock/ace/dock/dock.hpp:80`; `insert_kinds` `:157`,
  `insert_cell` `:170`, `can_delete`/`delete_selected` `:187-188` (the inert-default
  precedent). Rail Edit section `src/dock/dock.cpp:200-215`, called `:395`;
  `tool_rail_title()` `:361`.
- `AppProjectGateway` — `src/app/ace/app/project_gateway.hpp`, impl
  `src/app/project_gateway.cpp:112-122` (`run_edit`), `:210-247` (`insert_cell`,
  with the single `dispatch` call site at `:245`), `:250-259`
  (`can_delete`/`delete_selected`).
- The camera inspector's lie this leaf makes true —
  `src/app/camera_inspector.cpp:41`; its per-camera rows `:45-49` and rename box are
  where the auto-name is edited.
- `register_editor_kinds` (which registers `org.arbc.camera` on every `AppState`) —
  `src/commands/app_state.cpp:29`.

**Predecessor refinements:** `tasks/refinements/cameras/model.md`,
`tasks/refinements/editor.cells/selection.md`,
`tasks/refinements/editor.cells/remove.md`, `tasks/refinements/editor/manip.md`,
`tasks/refinements/editor/look_through.md`.

**Test rigs:** `ace_tests` source list `CMakeLists.txt:219-235` (link line
`:236-238`, `ACE_GOLDEN_DIR` `:240-241`); `ace_shell_test` source list
`CMakeLists.txt:250-264`; goldens under `tests/goldens/` compared via
`ace_test::compare_golden` (`tests/golden_support.hpp:36-46`), rendered by
`render::render_document_srgb8`; the L4 gateway unit suite
`tests/app_project_gateway_test.cpp`; the real-pool threading suite
`tests/canvas_host_test.cpp`.

## Constraints / requirements

1. **Levelization (`check_levels` clean).** No new component and no new DAG edge.
   The two new `interact` functions are **primitive-only** (spans of `PickTarget`,
   `arbc::ObjectId`, `arbc::Rect`, `ShotFraming`) and land in the existing
   `pick.cpp` / `interact.cpp` TUs, so **no second `interact → scene` assembly TU**
   appears and A17's split holds. `src/commands/` gains an `ace/scene/camera.hpp`
   include (already legal) and **no** `ace/interact/` include. `src/dock/` gains no
   `ace/scene` or `ace/commands` include — the two new virtuals traffic in `bool`
   only. Nothing in `src/interact/`, `src/scene/`, or `src/commands/` gains an
   ImGui/GL/SDL include. No entry in `scripts/check_levels.py:21-37` changes.
   A new `.cpp` under `src/commands/` needs no CMake edit (the component sources are
   globbed, `CMakeLists.txt:155`); the two new **test** files do.
2. **The frame's orientation is device → composition, matching every shipped
   consumer.** `shot_from_extent` returns the same `ShotFraming::frame` convention
   `new_shot_from_view` does (`interact.hpp:143-145`): it maps `[0,W]×[0,H]` onto
   the framed composition region. Verified by round-tripping through the shipped
   `viewport_camera_for_shot` (`interact.hpp:126-127`) in the unit test — a frame
   with the wrong orientation renders the inverse crop, and that mistake must be
   caught by a test, not by looking at a canvas.
3. **The frame is aspect-locked to the derived resolution, and expands rather than
   crops.** Rounding the extent to whole pixels perturbs the aspect by up to one
   pixel; the frame is then fitted to the *rounded* `W:H` about the extent's center,
   growing the short axis. Square pixels (`w'/W == h'/H`) is a unit-test assertion,
   and *every selected object stays inside the minted frame* is another. Cropping
   would silently cut off part of what the user asked to frame.
4. **A rotated or sheared selection frames its axis-aligned bound, not a rotated
   frame.** `map_rect` is used deliberately: the minted camera has no dutch rotation
   (D9 makes dutch modifier-gated and gizmo-driven), so its frame is axis-aligned in
   composition space and the tightest axis-aligned region containing the selection
   is what "fit to the selection" means. Note this is also *exact*: the AABB of a
   union of point sets equals the union of their AABBs, so unioning `map_rect`
   results loses nothing relative to unioning the exact `placed_quad` parallelograms.
5. **Unbounded members are skipped; an all-unbounded selection refuses.** A
   `PickTarget` with `extent == nullopt` (a factory-built `org.arbc.solid`,
   D-cells_model-3) contributes nothing to the union — an unbounded fill has no
   region to frame. If nothing bounded remains, `selected_extent` returns `nullopt`,
   the gateway mutates nothing, `frame_selection()` returns `false`, and the journal
   depth and revision are unchanged. This is the exact asymmetry `marquee` already
   ships (`pick.hpp:106-112`) and D23 records it.
6. **Errors are values; the mint never throws and never half-mutates.** Every
   refusal path — empty selection, all-unbounded selection, degenerate extent,
   `add_camera` finding no root composition (`camera.cpp`, returns an invalid
   `ObjectId`) — leaves the `Document` untouched and surfaces as `false` /
   `AddCameraOutcome::error`, exactly as `insert_cell_command` does
   (`cells.hpp:21-26`).
7. **Targets are resolved inside the edit, on the writer thread, against the live
   document.** `interact::pick_targets` is called **inside** the `run_edit` closure,
   not from a UI-thread cache taken at click time — D-cells_remove-3's rule, and
   here it matters more, because the geometry (not just the ids) must match the
   document generation the transaction lands on.
8. **Every UI-driven mint runs inside `CanvasView::apply_edit`.** The gateway
   override wraps its work in `run_edit` (`project_gateway.cpp:112-122`), matching
   `insert_cell` (`:245`) and Constraint 5 of `model.md`. Headless gateway tests
   with no live canvas get the direct-invoke default runner.
9. **One `Command` performs one `scene::add_camera`.** The create's **two** journal
   entries are the library's shape (`add_content` self-commits, then one transact
   for add_layer+attach — `camera.hpp:145-150`), already accepted for the insert
   side by D-cells_model-7. The command does not loop, does not batch, and does not
   hand-compose the two transactions into one.
10. **The mint does not touch the selection and does not touch any canvas's active
    camera.** The selection stays exactly as it was (framing then re-framing the
    same set must be possible without re-selecting), and no canvas starts looking
    through the new camera — that is `editor.cameras.look_through`'s transient
    session state (D15/D-look_through-1). Both are named unit assertions, because
    both are the kind of "helpful" behaviour an implementer might add.
11. **The derived resolution is clamped and always ≥ 1×1.** A composition-scale
    selection (or a selection containing a camera placed at a huge frame) must not
    mint a terapixel camera: the longer side is clamped to
    `interact::k_max_mint_resolution` with the aspect preserved, and each side is at
    least 1. The inspector's W×H fields (`src/app/camera_inspector.cpp:79-84`) are
    the documented escape hatch for anything the clamp changed (D9).
12. **Auto-naming is deterministic and collision-free against `cameras()`, but
    duplicates elsewhere are tolerated.** `next_camera_name` picks the first free
    `"Camera <n>"`; it does not rename existing cameras and does not fail if the user
    later creates a duplicate by hand — the inspector already keys its rows by index,
    not name (`camera_inspector.cpp:45-49`).
13. **The rail item is disabled, not hidden, when the gate is false** — the fixed
    rail geometry is the "home base" guarantee (`docs/00-design.md:452-456`), the
    same rule `Delete Selected` follows (`dock.cpp:206-212`).

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate`
green (check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.**
  Asserted by inspection as well as by the lint: `src/interact/pick.cpp` and
  `src/interact/interact.cpp` gain **no** `ace/scene` include (the new helpers are
  primitive-only, so `src/interact/pick_targets.cpp` remains the sole A17 assembly
  TU); `src/commands/cameras.cpp` includes `ace/scene/camera.hpp` and **no**
  `ace/interact/`; `src/dock/dock.hpp`/`dock.cpp` gain no `ace/scene` or
  `ace/commands` include; nothing under `src/interact/`, `src/scene/`,
  `src/commands/` gains an ImGui/GL/SDL include. No entry in
  `scripts/check_levels.py:21-37` changes.

- **L1 logic — Catch2 unit** (`tests/frame_selection_test.cpp`, new file added to
  the `ace_tests` source list at `CMakeLists.txt:219-235`; naming follows
  `tests/selection_test.cpp`, i.e. `TEST_CASE("frame selection: …")`):
  - **`selected_extent` unions exactly the selected members, in any order:** three
    targets, two selected, returns the AABB of just those two; the result is
    independent of the order of `ids`.
  - **A rotated placement contributes its AABB (Constraint 4):** a cell rotated 45°
    yields the enclosing axis-aligned rect, and the union of `map_rect` results is
    **byte-equal** to the AABB computed from the exact `placed_quad` corners of the
    same targets — the test that pins the "no precision is lost" claim rather than
    asserting it in prose.
  - **A selected camera contributes its output rectangle (D-frame_selection-3):** a
    selection of one camera yields `frame.map_rect({0,0,res_w,res_h})`, so framing a
    camera reproduces its own crop.
  - **Unbounded members are skipped; all-unbounded returns `nullopt` (Constraint
    5):** an `extent == nullopt` target adds nothing to a mixed union, and a
    selection of only unbounded targets yields `nullopt`.
  - **Empty selection, ids naming no target, and degenerate/non-finite placements
    all yield `nullopt`** with no NaN escaping (D-fit_bounds-3 discipline).
  - **`shot_from_extent` derives 1 unit = 1 pixel (D23):** a 640×480-unit extent
    yields `{640, 480}`; a 100.4×50.7 extent yields `{100, 51}` and a frame whose
    aspect is exactly 100:51.
  - **Square pixels and expand-never-crop (Constraint 3):** for the rounded case,
    `frame.a * H == frame.d * W` within double tolerance, and the framed region
    **contains** the input extent (all four corners inside).
  - **Frame orientation round-trips (Constraint 2):** feeding
    `shot_from_extent(R).frame` and its resolution to the shipped
    `interact::viewport_camera_for_shot` (`interact.hpp:126-127`) maps `R`'s corners
    onto `[0,W]×[0,H]` — the assertion that catches an inverted frame.
  - **The clamp preserves aspect and floors at 1 (Constraint 11):** a
    100000×50000-unit extent yields a longer side of exactly
    `k_max_mint_resolution` with the 2:1 aspect intact; a 0.2×0.1-unit extent yields
    `{1, 1}` rather than `{0, 0}`.
  - **A degenerate extent yields the `{identity, 0, 0}` sentinel** (empty,
    inverted, or non-finite rect).
  - **`add_camera_command` adds exactly 2 journal entries and one undo removes the
    camera (Constraint 9, D15):** dispatch bumps `journal().depth()` by 2 and
    `pin()->revision()`; the camera appears in `scene::cameras()` with the exact
    `(name, resolution, frame)` it was given; one `commands::undo` removes it; one
    `commands::redo` restores it on the **same `ObjectId`**.
  - **`next_camera_name` picks the first free slot (Constraint 12):** empty document
    → `"Camera 1"`; with `"Camera 1"` and `"Camera 3"` present → `"Camera 2"`; with
    a hand-named `"Hero"` present → `"Camera 1"`.
  - **`can_frame_selection` is false on an empty selection and true otherwise.**
  - **Dirty bookkeeping (A13):** a mint on a freshly-saved session makes
    `is_dirty()` true; a subsequent `undo` leaves it true (undo never marks clean,
    D-undo-4).
  - **End-to-end derivation over a real document:** insert a cell at a known
    placement, select it, run the full `pick_targets → selected_extent →
    shot_from_extent → add_camera_command` chain headlessly, and assert the minted
    camera's frame maps its output rect onto exactly the cell's placed extent
    (expanded only by the sub-pixel aspect fit).

- **Rendered output — golden.** **No new golden file.** A camera is non-rendering
  (A14, `camera.hpp:47-48`), so the meaningful assertion is byte-*in*variance,
  which is stronger than a new baseline: in `tests/frame_selection_test.cpp`, render
  the probe document at 64×64 via `render::render_document_srgb8` → insert a cell →
  render again and assert the bytes **differ** (so the test cannot pass vacuously)
  and match the existing `tests/goldens/cells_insert_nested_64x64.rgba8` through
  `ace_test::compare_golden` (`tests/golden_support.hpp:36-46`) → frame-select it →
  assert the bytes are **byte-identical** to the second render. Nothing new lands in
  `tests/goldens/`. This mirrors `tests/selection_test.cpp:631-654` and
  `cameras/model.md`'s justified golden-N/A.

- **UI e2e — ImGui Test Engine** (`tests/frame_selection_e2e_test.cpp`, new file
  added to the `ace_shell_test` source list at `CMakeLists.txt:250-264`; modelled on
  `tests/cells_remove_e2e_test.cpp` and `tests/camera_manip_e2e_test.cpp`, reusing
  their `ScratchDir`, `pump_until`, `E2EState` main-thread edit handshake, and the
  `"canvas#1/##canvas_nav"` pane-rect probe recipe
  (`camera_manip_e2e_test.cpp:250-271`); registered as
  `IM_REGISTER_TEST(engine, "cameras", "frame_selection_rail")`). Assertions are on
  model state, never pixels:
  - the rail item `<tool_rail_title()>/###frame_selection` is **disabled** with an
    empty selection and **enabled** once a cell is selected (Constraint 13);
  - clicking it adds exactly one entry to `scene::cameras()`, named `"Camera 1"`,
    whose frame maps its output rect onto the selected cell's placed extent;
  - **the selection is unchanged and no canvas switched cameras (Constraint 10):**
    `selection().items()` is identical before and after, and the canvas is still on
    the free viewport (the `"Viewport"` picker button, `canvas_view.cpp:238`, is
    still the active one);
  - clicking it again with the **same** selection mints `"Camera 2"` — the
    auto-name advances and the geometry is identical;
  - selecting **two** cells and clicking mints one camera framing both (its output
    rect contains both placed extents);
  - `Ctrl+Z` removes the minted camera and the selection still stands;
  - the minted camera is immediately usable by the shipped surfaces: it appears in
    the canvas camera picker and in the Inspector's camera list, and its W×H fields
    (`"Inspector/Width"` / `"Inspector/Height"`,
    `camera_manip_e2e_test.cpp:288-301`) read back the derived resolution.

- **Threading (ASan/TSan).** One case appended to `tests/canvas_host_test.cpp` on
  the real-pool `CanvasHost` (`default_interactive_pool_config()`, the
  D-edit_render_sync-3 anchor): drive the full mint — `interact::pick_targets` read
  **plus** `scene::add_camera` write, both inside one `CanvasHost::apply_edit`
  (`src/render/ace/render/canvas_host.hpp:104`) — while the render thread walks the
  same document over the lock-free `pin()` seam, and assert frames keep arriving and
  the camera lands. This is a genuinely new threading surface: every prior edit
  performed a *write* inside `apply_edit`, and this is the first that performs a
  full document **read walk** there too (Constraint 7). Runs in the existing ASan and
  TSan lanes; no new lane, no new suppression.

- **L4 join coverage.** `tests/app_project_gateway_test.cpp` (existing, in
  `ace_shell_test`) gains cases for `can_frame_selection`/`frame_selection` over the
  headless direct-invoke edit runner: false-and-no-mutation on an empty selection,
  false-and-no-mutation on an all-unbounded selection (Constraint 5), and
  true-with-one-camera on a normal selection.

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed lines;
  clang-format + build clean. Tests ship with the task.

- **Doc delta (same commit).** `docs/00-design.md` gains **D23 — Minting a camera**
  (`:490`), the row the decisions log has never had for camera creation. See
  D-frame_selection-1.

- **Deferred WBS work.** One named follow-up, for the closer to register
  mechanically:
  - **`editor.cameras.new_shot_from_view`** — *"Rail action: promote the viewport's
    current framing into a saved shot"*, **0.5d**, `allocate team`,
    `depends !frame_selection`, under `task cameras` (`.tji:257`) so it is already
    wired into `m9_editor` through the `editor.cameras` container dependency
    (`tasks/99-milestones.tji`). Scope: a second Edit-section rail item
    `New Shot From View###new_shot_from_view` plus a `ProjectGateway` virtual pair,
    whose L4 impl reads `ViewFraming` (`src/app/ace/app/view_framing.hpp`, bound at
    `src/app/shell.cpp:288`), calls the **already-shipped, already-unit-tested**
    `interact::new_shot_from_view` (`src/interact/ace/interact/interact.hpp:109`),
    and dispatches the **`commands::add_camera_command` this leaf ships** — plus an
    e2e. It exists because `new_shot_from_view` has had no production caller since
    `editor.cameras.model` shipped it, and `src/app/camera_inspector.cpp:41` tells
    the user *"No cameras — create a shot from the view."*, which is currently
    false. Source-of-debt: `tasks/refinements/cameras/frame_selection.md`.
    Design: `docs/00-design.md` D23, `:64-66`.
  - Everything else out of scope already has a scheduled owner: the inspector
    affordance is `editor.panels.inspector` (`.tji:359`, a two-line reuse of the two
    virtuals this leaf ships); the `camera↔cell bounds` **snap** is
    `editor.cells.gizmo` (`.tji:336-341`, whose note names camera-frame snapping);
    zoom-to-selection over the same `selected_extent` helper is
    `editor.canvas.nav_aids` (`.tji:213`); an overview-driven mint is
    `editor.panels.overview` (`.tji:371`); rendering the minted camera to a file is
    `editor.cameras.export` (`.tji:312`).

## Decisions

- **D-frame_selection-1 — The mint rule is a design-doc row (D23), not refinement
  prose.**
  `docs/00-design.md` gains D23 "Minting a camera" (`:490`) covering: mint-is-an-
  action (restating D20's ruling), the shared derivation rule for both mint verbs,
  the auto-name, don't-select / don't-activate, and refuse-on-no-bounded-extent.
  *Rationale:* the decisions log runs D1–D22 and has **no row for camera creation at
  all** — creation appears only as prose at `:64-66` ("new shot from view") and
  `:262-263` (this command). The derivation rule is user-visible policy that at
  least three later leaves inherit — `editor.cameras.new_shot_from_view` (the
  sibling mint verb, registered here), `editor.cameras.export` (which renders at
  whatever resolution the mint chose, D14), and `editor.cells.resolution`'s
  resolution-health read (`.tji:342`). Per `tasks/refinements/README.md:80-96`, a
  rule that constrains future refinements belongs in the constitution, not in one
  leaf's work order.
  *Alternative rejected:* keep the rule in this refinement only. The next leaf to
  mint a camera would then either re-derive it (and drift) or cite a work-shaping
  document as if it were normative — the exact layering the README separates.
  *Alternative rejected:* an `A<n>` row instead. The rule is UI/UX behaviour (what
  resolution the user gets), not structure or build; A14 already covers the
  structural half (a camera is a `Content` + a `Layer`, created via `commands`).
  **Doc delta: D23 (`docs/00-design.md:490`).**

- **D-frame_selection-2 — The derived resolution is the framed region at 1
  composition unit = 1 pixel, rounded, clamped, with the frame then expanded about
  its center to the rounded aspect.**
  `shot_from_extent(R)`: `W = clamp(round(R.width()))`, `H = clamp(round(R.height()))`
  preserving aspect under the clamp; then the frame covers `R` grown about its
  center to aspect `W:H`.
  *Rationale:* four forces pick this rule. (i) **D8 read back:** a cell placed
  unscaled occupies as many composition units as it has content pixels, so framing
  it exactly returns *its own native pixel count* — "frame this cell exactly"
  (`docs/00-design.md:261`) produces a camera that exports the cell at 1:1, with no
  resample. (ii) **It is the zoom-1:1 specialization of the rule already shipped:**
  `new_shot_from_view` sets resolution = the pane size in device pixels
  (`interact.hpp:100-109`); at a 1:1 viewport 1 device pixel *is* 1 composition
  unit, so the two mint verbs agree, which is what lets D23 state one rule for both.
  (iii) **It is pure and deterministic** — no `ViewFraming`, no pane, no zoom — so
  the whole derivation is a headless Catch2 function rather than an L4 behaviour
  observable only through a shell. (iv) **D9's aspect-lock is preserved exactly:**
  fitting the frame to the *rounded* aspect makes `w'/W == h'/H` hold identically,
  so pixels are square; expanding rather than cropping guarantees nothing the user
  selected falls outside the frame.
  *Alternative rejected:* derive the resolution from the **pane**, i.e. the
  selection's extent measured in current device pixels (the literal
  `new_shot_from_view` rule). It makes the same command on the same selection
  produce a different camera depending on how far the user happened to be zoomed —
  surprising, and untestable without a live pane. The WYSIWYG argument that
  justifies it for *new shot from view* (where the pane **is** the thing being
  promoted) does not transfer to *frame selection* (where the selection is).
  *Alternative rejected:* a fixed default resolution (e.g. 1920×1080) with the frame
  letterboxed into it. It discards the selection's aspect — the one piece of
  information the command exists to capture — and forces an immediate inspector
  edit.
  *Alternative rejected:* leave the resolution unrounded/unclamped. `Resolution` is
  `{int, int}` (`camera.hpp:26-31`), so rounding is forced; the clamp is what stops
  a composition-scale selection from minting a camera whose export
  (`editor.cameras.export`, D14) would allocate terabytes.
  **Doc delta: D23.**

- **D-frame_selection-3 — The union is over the WHOLE selection — cells **and**
  cameras — with no kind filter, and unbounded members contribute nothing.**
  `selected_extent` reads `PickTarget::kind` not at all; a camera contributes
  `frame.map_rect({0,0,res_w,res_h})`, its own output rectangle.
  *Rationale:* D7 (`docs/00-design.md:474`) makes cells and cameras "one shape …
  one select tool", A14 makes a camera structurally a `Content`+`Layer`, and A17's
  `pick_targets` already supplies both with the same `{placement, extent}` fields —
  filtering would reintroduce exactly the kind discrimination A16, D-cells_model-8
  and D-cells_remove-1 all forbid. It is also *useful*: framing a selected camera
  reproduces its crop, which is how a user derives a second shot from an existing
  one. Unbounded content is skipped for the reason `marquee` already skips it
  (`pick.hpp:106-112`, D-selection-5): an unbounded fill has no region, so including
  it would mean every selection containing a solid frames the infinite plane.
  *Alternative rejected:* filter to `PickKind::Cell`. It makes the command's
  behaviour depend on something the user cannot see in the selection outline, and it
  would make "frame this camera's crop" — the obvious second use — impossible.
  *Alternative rejected:* treat an unbounded member as the root composition's
  authored bounds (`project::root_composition_size`, `project.hpp:67`). That is an
  invented fallback with no design-doc basis, and it would make the framed region
  depend on a value the user never selected.
  **Doc delta: D23 (the refuse-on-no-bounded-extent clause).**

- **D-frame_selection-4 — The union helper unions `map_rect` AABBs, and that is
  exact, not an approximation.**
  `selected_extent` accumulates `target.placement.map_rect(*target.extent)`
  (`transform.hpp:38`) rather than unioning the exact `placed_quad` parallelograms
  (`pick.hpp:80`).
  *Rationale:* the `.tji` note names `map_rect` and it is also the mathematically
  identical answer — the axis-aligned bound of a union of point sets is the
  coordinate-wise min/max of the per-set axis-aligned bounds, so no tightness is
  lost by bounding each target before unioning. Since the minted camera is
  axis-aligned in composition space (D9 makes dutch modifier-gated and
  gizmo-driven), an AABB is precisely the answer wanted. `map_rect` is one library
  call per target against `placed_quad`'s four-corner assembly plus an inverse, so it
  is also the cheaper and shorter code. The unit suite pins the equality against
  `placed_quad` so the claim is tested, not asserted.
  *Alternative rejected:* union the `placed_quad` corners. Identical result, more
  code, and it would suggest the two paths could disagree.
  *Alternative rejected:* fit a **rotated** (dutch) frame to the selection's
  principal axis. It would be tighter for a single rotated cell and strictly worse
  for everything else, and D9 makes dutch an explicitly modifier-gated advanced
  gesture — minting one by default contradicts a decided row.
  **No further doc delta (covered by D23).**

- **D-frame_selection-5 — The selection→geometry join lives at L4 `app`, inside
  `run_edit`; `commands` receives a finished `(name, Resolution, Affine)` triple.**
  `AppProjectGateway::frame_selection()` calls `interact::pick_targets` →
  `selected_extent` → `shot_from_extent` → `commands::add_camera_command` →
  `dispatch`, all inside the `run_edit` closure.
  *Rationale:* it is structurally forced and independently right. **Forced:**
  `commands` may not depend on `interact` (`scripts/check_levels.py:28`,
  `docs/01-architecture.md:211`) and `interact` may not depend on `commands`, so no
  L1 component can hold both halves; `views` (L3) could, but this is not a view.
  **Right:** it is exactly the shape `insert_cell` already has — L4 computes the
  placement affine from `interact::place_in_view` and passes it down as a value
  (`project_gateway.cpp:238-245`) — so `commands` stays a thin, purely-document
  layer and the geometry stays purely testable. Performing the join *inside*
  `run_edit` (rather than before it) is D-cells_remove-3's rule strengthened: the
  geometry, not just the ids, must come from the generation the transaction lands
  on.
  *Alternative rejected:* re-derive the extents in `commands` from `scene::cells` +
  `scene::cameras`, the `selected_removals` precedent (`cells.hpp:63-64`). It is
  legal, but it would fork the placed-extent computation into a second
  implementation that `pick`, `marquee` and the selection outline do not share — and
  the `.tji` note explicitly places the helper in `interact`, beside `pick`.
  *Alternative rejected:* pass a `std::vector<PickTarget>` down from the canvas's
  per-frame cache (`canvas_view.cpp:207`). Free at the call site, but it makes the
  correctness of a transaction depend on how fresh a UI-thread cache is.
  **No doc delta required.**

- **D-frame_selection-6 — The command verb lands in a new
  `src/commands/ace/commands/cameras.{hpp,cpp}` pair, and the mint is one
  `add_camera_command` costing two journal entries.**
  Modelled line-for-line on `insert_cell_command` + `InsertCellOutcome`
  (`cells.hpp:21-26,37`).
  *Rationale:* `app_state.hpp:103-108` explicitly reserves the slot — *"those
  `Command`s land with their edit leaves (`editor.cells.*`, `editor.camera.*`)"* —
  and there is no `commands/cameras.hpp` today, so every camera mutation in the tree
  is either a bare `scene::` call wrapped by an L4 caller
  (`src/app/camera_inspector.cpp:87-89`) or a hand-rolled `Command{"add_camera",
  …}` lambda inside a test (`tests/camera_model_test.cpp:86-92`, and four e2e
  fixtures). A named file pair gives the follow-up mint verb, the inspector, and the
  overview one factory instead of five copies. The two-entry cost is the library's
  (`add_content` self-commits, `camera.hpp:145-150`) and was already accepted for
  the insert side by D-cells_model-7; the D15 observable contract — one undo removes
  the camera — still holds, and is a named unit assertion.
  *Alternative rejected:* put `add_camera_command` in `cells.{hpp,cpp}`. The file is
  named for cells and already carries five cell verbs; a camera factory there would
  make the next reader look for a cell.
  *Alternative rejected:* hand-compose `add_content` + `add_layer` + `attach_layer`
  inside one editor-held `transact` to get a one-entry create. It re-implements
  `scene::add_camera`'s body outside `scene`, takes on the library's
  binding-lifetime invariants as an undocumented dependency, and is the same
  redesign D-cells_remove-2 rejected on the delete side. The missing library verb is
  already an upstream-issue candidate in `tasks/parking-lot.md`.
  **No doc delta required.**

- **D-frame_selection-7 — The rail gate is `!selection.empty()`, coarser than the
  actual refusal condition; an all-unbounded selection is an enabled item that
  mutates nothing.**
  `can_frame_selection(AppState&)` is `!state.selection().empty()`;
  `frame_selection()` returns `false` when no bounded extent survives.
  *Rationale:* an exact gate would have to run `interact::pick_targets` + the union
  **every frame** the rail draws, from a component (`commands`) that structurally
  cannot call `pick_targets` at all — so an exact gate would push a full
  document walk into the L4 gateway's `const` per-frame path for a corner case that
  requires selecting *only* factory-built unbounded solids
  (`org.arbc.solid` with no bounds, D-cells_model-3) and nothing else. The
  no-op-when-nothing-to-frame behaviour is documented in D23, returned as a value,
  and pinned by both a `commands` unit test and an
  `app_project_gateway_test.cpp` case. This is the same coarseness `can_delete`
  already ships: it is true for a selection whose members are all stale, and
  `delete_selection` then removes zero (D-cells_remove-5, Constraint 5).
  *Alternative rejected:* an exact gate. A per-frame document walk for a corner
  case, in a `const` method, to change a tooltip.
  *Alternative rejected:* fall back to framing the root composition when nothing is
  bounded, so the item is never a no-op. That is inventing a region the user did not
  select — see D-frame_selection-3.
  **No doc delta required.**

- **D-frame_selection-8 — Rail action only: no keyboard chord, no modal tool, no
  confirmation, and no inspector button in this leaf.**
  One item, `Frame Selection###frame_selection`, in the Edit section beside
  `Insert Cell…` and `Delete Selected` (`dock.cpp:200-215`), disabled when the gate
  is false, behind two `ProjectGateway` virtuals with inert non-pure defaults so the
  existing gateway fakes need no churn.
  *Rationale:* D20 (`docs/00-design.md:487`) rules on this verb **by name** —
  *"likewise ad-hoc crop = frame a camera (D14), not a mode"* — so a fifth `ToolId`
  is forbidden, and D-cells_remove-6 already established the action-in-the-Edit-
  section shape. No confirm: the mint is a journaled transaction, and D15 reserves
  confirmation for GC precisely because GC is *not* undoable. No chord: unlike
  `Ctrl+Z` and `Delete`, framing has no conventional binding, it is a low-frequency
  verb, and `docs/00-design.md:499-500` leaves the input map open — minting a chord
  now would pre-empt the leaf that writes the map, for no gain. No inspector button:
  `editor.cells.remove` made exactly this split (`remove.md:450-459`), and the
  inspector's selection-keyed property sheet is `editor.panels.inspector`'s
  (`.tji:359`), where adding this becomes a two-line reuse of the virtuals shipped
  here.
  *Alternative rejected:* add the button to the existing `CameraInspector`
  (`src/app/camera_inspector.cpp`) now, since it is L4 and could call the chain
  directly with no gateway. It would be a second, gateway-bypassing mutation path
  into the same verb, and `CameraInspector` today has no access to the selection —
  wiring one in is a wider change than the affordance is worth before
  `editor.panels.inspector` decides that panel's shape.
  **Covered by D20/D23 and A12/A13/A16 — no new architecture row.**

- **D-frame_selection-9 — The minted camera is auto-named `Camera <n>`; no naming
  prompt.**
  `next_camera_name(document)` returns the first `"Camera <n>"`, `n ≥ 1`, not
  already in `scene::cameras()`.
  *Rationale:* no auto-naming exists anywhere today — `add_camera` takes the name
  verbatim and every caller is a test passing `"Hero"` or `"shot"` — so this leaf
  has to mint the policy. `"Camera"` (not `"Shot"`) because that is the word the
  user sees: the inspector section is titled `"Cameras"`
  (`src/app/camera_inspector.cpp:38`) and the canvas picker lists camera names.
  First-free-`n` rather than a monotonic counter so that minting, undoing, and
  minting again reuses the name instead of leaking to `"Camera 3"` — deterministic,
  and therefore assertable in the e2e. Rename is already shipped
  (`scene::rename_camera`, `camera.hpp:165-166`, with `ObjectId` stability from
  `editor.cameras.rename_stable_id`), so the name is a starting point, not a
  commitment.
  *Alternative rejected:* a name-prompt modal before minting. It puts a dialog in
  front of a one-click action whose result is immediately visible and immediately
  undoable, and D-cells_remove-4's reasoning applies: an undoable op needs no
  ceremony.
  *Alternative rejected:* `"Shot <n>"`, matching the design doc's "saved shot"
  vocabulary (`:64-66`). The vocabulary is right for the *concept*; the *list the
  name appears in* is labelled Cameras, and consistency with what the user reads
  wins.
  **Doc delta: D23 (the auto-name clause).**

- **D-frame_selection-10 — The mint changes neither the selection nor any canvas's
  active camera.**
  *Rationale:* replacing the selection with the new camera would destroy the very
  thing that was framed, making "frame it, then adjust the selection and re-frame"
  impossible without re-selecting — and `insert_cell` already sets the precedent of
  not selecting what it creates (`project_gateway.cpp:238-247` touches no
  selection). Auto-activating look-through would be worse: D15 draws the line at
  transient-vs-scene, and which camera a canvas looks through is transient session
  state owned by `editor.cameras.look_through`; minting a scene object must not
  silently move the user's viewport. Both are named unit **and** e2e assertions
  because both are plausible "helpful" additions.
  *Alternative rejected:* select the minted camera so its frame gizmo is immediately
  draggable. Convenient once, destructive every other time; the user can click the
  new frame border, which D7 already makes grabbable.
  **Doc delta: D23 (the don't-select / don't-activate clause).**

## Open questions

(none — all decided.)

One item is routed to `tasks/parking-lot.md` for human review rather than the WBS
(it is not an "audit" task; it is an upstream-issue candidate a human decides
whether to file against `ruoso/arbitrarycomposer`):

1. **A camera create costs two journal entries because `Document::add_content`
   self-commits.** `scene::add_camera` (`camera.hpp:145-153`) must call
   `add_content` (which commits on its own, `document.hpp:107`) and then a second
   `transact` for add_layer+attach, so a mint adds 2 to `journal().depth()` while
   presenting one undo step. The already-parked `create_content_and_attach` ask
   (`tasks/parking-lot.md:92`, registered by `editor.cells.model`) would collapse
   both mint verbs and `insert_cell` to one entry each. This leaf adds a third
   consumer of that gap rather than a new one; no editor-side WBS task until the API
   exists.

## Status

**Done** — 2026-07-23.

- `src/commands/ace/commands/cameras.hpp` + `src/commands/cameras.cpp`: new `add_camera_command`, `next_camera_name`, `can_frame_selection` L1 verbs (D-frame_selection-6).
- `src/interact/ace/interact/pick.hpp` + `src/interact/pick.cpp`: `selected_extent` — AABB union over the selection's placed extents, unbounded members skipped (D-frame_selection-3/4).
- `src/interact/ace/interact/interact.hpp` + `src/interact/interact.cpp`: `shot_from_extent` + `k_max_mint_resolution` — D23 derivation (1 unit per pixel, rounded, clamped, aspect-expanded) (D-frame_selection-2).
- `src/dock/ace/dock/dock.hpp` + `src/dock/dock.cpp`: `can_frame_selection`/`frame_selection` inert-default virtuals; `Frame Selection###frame_selection` rail item in Edit section (D-frame_selection-8).
- `src/app/ace/app/project_gateway.hpp` + `src/app/project_gateway.cpp`: L4 join inside `run_edit` — `pick_targets` → `selected_extent` → `shot_from_extent` → `add_camera_command` → `dispatch` (D-frame_selection-5).
- `CMakeLists.txt`: new test files added to `ace_tests` and `ace_shell_test` source lists.
- `tests/frame_selection_test.cpp`: 18 Catch2 unit cases covering L1 geometry, command, golden byte-invariance, and full chain.
- `tests/frame_selection_e2e_test.cpp`: ImGui Test Engine e2e `"cameras/frame_selection_rail"`.
- `tests/app_project_gateway_test.cpp`: 3 gateway cases (empty/all-unbounded selection, normal mint).
- `tests/canvas_host_test.cpp`: 1 real-pool threading case (`pick_targets` read + `add_camera` write in one `apply_edit`).
- `docs/00-design.md`: D23 "Minting a camera" design-log row added (D-frame_selection-1).
