# editor.cameras.manip — Frame resize (hold resolution), aspect-lock, dutch, resolution inspector

## TaskJuggler entry

- **Task:** `editor.cameras.manip` (`tasks/00-editor.tji:248-253`, under
  `task cameras "Cameras"` at `:219`).
- **Effort:** `2d` (`:249`) · `allocate team` (`:250`).
- **Depends:** `!model` (`editor.cameras.model`) only (`:251`).
- **Note (`:252`):** "Resize = re-crop (aspect-locked to resolution, holds
  resolution); resolution (pixel count + aspect) edited separately; dutch rotation
  modifier-gated. Design: D9."
- **Back-link:** the `.tji` note currently ends `Refinement:
  tasks/refinements/camera_manip.md` (the flat interim path). This refinement lands
  at **`tasks/refinements/editor/manip.md`** per the orchestrator's
  area = first-dot-segment (`editor`) assignment; the closer updates the note
  back-link to the real path and adds `complete 100` after `allocate team`
  (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji` here.
- **Downstream:** `editor.cameras.manip` is a `depends` of the M-editor milestone
  (`tasks/00-editor.tji:393`). It is a sibling of `editor.cameras.export`,
  `editor.cameras.look_through`, `editor.cells.selection`, `editor.cells.gizmo`,
  and `editor.cells.resolution` — several of which *consume* the seams this leaf
  ships (the in-place resolution editor, the camera-frame gizmo math) without a
  hard `depends` edge. This leaf realizes the D9 "camera gizmo + resolution
  inspector" promise on top of the persisted shot-camera model.

## Effort estimate

**2 days.** The frame lives in the binding `Layer`'s `Affine` and the resolution
lives in the camera `Content`'s already-editable state, so both mutation channels
are pre-built for undo — the work is the pure-L1 manip math plus two thin seams and
their tests: (1) `interact` frame math — re-crop-holding-resolution, dutch, move,
and frame hit-test, pure `arbc::Affine` beside the existing nav/shot helpers
(~0.7d incl. Catch2); (2) `scene::set_camera_resolution` — a new in-place resolution
editor over the **existing** `arbc::Editable` store (mirror of `rename_camera`,
D-rename-2 chartered it "no new task"), ~0.3d incl. Catch2; (3) the interactive
camera-frame gizmo wired into the Canvas body — border-grab, drag → previewed frame
→ one committed `set_layer_transform` on release, through the settled
`apply_edit`/edit-runner seam (~0.5d); (4) a first-cut resolution inspector body
(W×H + aspect presets) driving `set_camera_resolution` (~0.2d); (5) golden + ImGui
Test Engine e2e + one sanitizer case (~0.3d). **No new component, no new DAG edge,
no new libarbc surface, no doc delta.**

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cameras.model`** (`tasks/refinements/cameras/model.md`, Done
  2026-07-18) — the persisted shot-camera model this leaf manipulates. It ships:
  - `scene::Camera { ObjectId id; ObjectId layer; std::string name; Resolution
    resolution; arbc::Affine frame; }` (`src/scene/ace/scene/camera.hpp:113-119`)
    and `scene::cameras(const arbc::Document&) → std::vector<Camera>`
    (`camera.hpp:124`, impl `src/scene/camera.cpp:452-480`), read over the
    lock-free `pin()` snapshot in layer order.
  - The **frame is the binding `Layer`'s `Affine transform`** (device→composition):
    `Camera.frame` is `layer->transform`, and the header comment at
    `camera.hpp:112` explicitly reserves it — *"`layer` is what `cameras.manip`
    reframes through `set_layer_transform`."* This leaf implements that named-but-
    unimplemented seam.
  - The **resolution + name are the `Content`'s editable state**:
    `class CameraContent final : public arbc::Content, public arbc::Editable`
    (`camera.hpp:42-102`) with a versioned `struct Version { std::string name;
    Resolution resolution; std::uint32_t refcount; }` store (`camera.hpp:88-92`,
    `:98-101`), `Resolution { int width; int height; }` (`camera.hpp:26-31`; no
    separate aspect field — aspect is `width/height` implicit), and the live-version
    accessors `camera_name()`/`resolution()` (`camera.hpp:80-81`).
  - The **in-place edit pattern to copy**: `CameraContent::set_name(Model::
    Transaction&, ObjectId self, std::string) ` (`camera.hpp:76`, impl
    `camera.cpp:97-110`) mints a fresh `Version`, adopts it as `d_base`, and journals
    it via `txn.set_content_state(self, after)` — one transaction, **ObjectId
    preserved**. Wrapped by `scene::rename_camera` (`camera.hpp:155-156`, impl
    `camera.cpp:511-532`). **D-rename-2** (`cameras/model.md`, via
    `tasks/refinements/cameras/rename_stable_id.md`) explicitly chartered this leaf's
    resolution edit as reuse-for-free: *"`cameras.manip`'s in-place resolution edit
    reuses this same facet (call `set_content_state` with a new-resolution handle) —
    no new task."*
- **`editor.cameras.rename_stable_id`** (`tasks/refinements/cameras/rename_stable_id.md`,
  Done) — established that camera content-state edits go through `arbc::Editable` +
  one `set_content_state` transaction preserving the `ObjectId` (so
  `commands::Selection`, keyed by `ObjectId`, survives). The resolution edit inherits
  that invariant verbatim.
- **`editor.cameras.reopen_codec`** (`tasks/refinements/cameras/reopen_codec.md`,
  Done) — threads `scene::register_camera_kind` into `project::open_project`'s load
  path so a reopened camera restores as live `CameraContent` (not `PlaceholderContent`);
  manip edits addressing a camera by `ObjectId` only work post-reopen because of it.
- **`editor.cameras.look_through`** (`tasks/refinements/editor/look_through.md`,
  Done 2026-07-19) — ships the inverse render-through-camera math this leaf reuses to
  pin the frame↔resolution independence: `interact::viewport_camera_for_shot(const
  arbc::Affine& frame, int native_w, int native_h, int out_w, int out_h) →
  arbc::Affine` (`src/interact/ace/interact/interact.hpp:93-94`, comp→device =
  `frame.inverse()` scaled by `out/native`) and its forward twin
  `interact::new_shot_from_view` (`interact.hpp:62-66`). A camera looked-through by a
  second canvas is the **live preview** of a manip edit (Constraint 6).
- **`editor.canvas.edit_render_sync`** (`tasks/refinements/editor/edit_render_sync.md`,
  Done 2026-07-19) — the UI-thread `Document`-edit serialization every manip edit
  rides: `CanvasHost::apply_edit(const std::function<void()>&)` under `doc_mu`
  (`src/render/canvas_host.cpp:150-165`, mutex `:90`, mutually excluded against the
  render thread's per-frame read held across `drive_once`, `:197-202`), forwarded by
  `CanvasView::apply_edit` (`src/app/canvas_view.cpp:210`) via the edit runner
  `AppProjectGateway::set_edit_runner`/`run_edit`, bound at `src/app/shell.cpp:275`.
  Its Constraint-1 note names *"cameras UI"* as a future edit-producer that **must**
  funnel its UI-thread mutations through this seam — this leaf is that consumer.

**Pending (owned here):** the `interact` frame-manip math (re-crop, dutch, move,
hit-test), the `scene::set_camera_resolution` in-place editor, the interactive
camera-frame gizmo, and the first-cut resolution inspector body. Nothing downstream
is blocked on an unwritten predecessor.

## What this task is

Make a **saved shot camera's framing and resolution directly manipulable** — the
D9 "camera gizmo + resolution inspector." Concretely:

1. **Frame resize = re-crop, holding resolution.** Dragging the camera frame's
   corner/edge changes the **region of the composition it covers** (the crop / field
   of view) — **never** its pixel count. The covered region stays **aspect-locked to
   the resolution** (W:H), so pixels stay square: a bigger frame captures more scene
   at the same pixel count (wider, lower effective PPI); smaller = tighter, higher
   PPI (D9, `docs/00-design.md:470`, `:241-250`).
2. **Move / pan the frame.** Dragging the frame's **border or label** pans what the
   camera frames (D7 — a camera is grabbed by border/label, interior click-through).
3. **Dutch rotation, modifier-gated.** A modifier-gated rotate of the frame about its
   center — advanced, not a default handle (D9 — "like cell shear").
4. **Resolution edited separately, in the inspector.** A resolution editor (W×H
   numeric + aspect presets) edits the camera's **pixel count and aspect
   independently** of which region the frame covers; the frame *follows* an aspect
   change (D9). This is a new in-place `Content`-state edit over the existing
   `arbc::Editable` store.

The frame edits mutate the **binding `Layer`'s `Affine`** (`set_layer_transform`);
the resolution edit mutates the **camera `Content`'s editable state**
(`set_content_state`). Both are undoable `Document` transactions routed through the
settled `apply_edit`/edit-runner seam; the frame and the resolution are **always
independent** (D7/D8 — placement is not resample).

## Why it needs to be done

D9 (`docs/00-design.md:470`, narrative `:241-250`) is the constitution for camera
manipulation, and it is entirely unimplemented: `editor.cameras.model` *persists* a
shot camera's frame (layer `Affine`) and resolution (editable content-state) and
`editor.cameras.look_through` *renders through* one, but nothing lets a user
**reframe or re-resolution** a saved shot. The seam is even reserved in the source
(`camera.hpp:112` — "`layer` is what `cameras.manip` reframes through
`set_layer_transform`"; `camera.hpp:152` names the same for resolution) but left
unimplemented. This leaf fills it — the last piece that makes a shot camera an
*editable* export spec (D14: a camera *is* the export spec, so its frame and
resolution are the crop and the pixel budget), and the D8/D9 payoff that a camera's
extent (drag) and resolution (type) are independent, non-destructive controls.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **D9 — Camera frame ≠ resolution** (`docs/00-design.md:470`, verbatim): *"Frame
  resize = **re-crop**, aspect-locked to resolution, **holds resolution**; resolution
  (pixel count + aspect) is edited separately in the inspector. Dutch rotation is
  **modifier-gated**. **Space pans the active viewport camera** (transient) — distinct
  from re-framing a saved shot camera (scene edit)."* The governing **Camera gizmo**
  narrative (`:241-250`): move (drag border/label = pan what's framed), **resize** =
  change the region it covers (re-crop / field of view), **aspect-locked to the
  camera's resolution** so pixels stay square, **holds resolution** (bigger frame =
  more scene at same pixel count, lower PPI); **modifier-gated dutch rotation** ("like
  cell shear — advanced, not a default handle"); **Inspector: output resolution
  (W×H + aspect presets)** — editing it changes pixel count and aspect (the frame
  follows) *independently* of the framed region, plus a "look through" button and a
  per-camera **resolution-health** read of the cells it captures. The **Modifiers &
  snapping** rule (`:252-254`): Shift constrains (aspect / 15° / axis-lock); Alt from
  center; Cmd/Ctrl select-behind and bypass-snap; **Space pans the *view*** — distinct
  from dragging a camera *object*.
- **D7 — Manipulation model** (`:468`): cells and cameras share **one shape** (affine
  placement + a resolution number) and **one select tool**, differing only in
  direction (cell *emits* / camera *samples*); "drag the extent, type the resolution;
  the two are always independent"; a camera is grabbed by **border/label** with
  **click-through interiors**.
- **D8 — Cell scale ≠ resample** (`:469`): handle-drag changes **placement (affine)**,
  never resolution — non-destructive; resampling is a separate explicit act. The
  camera analogue: dragging the frame re-crops (placement), never changes pixel count.
- **D2 — Canvas model** (`:463`) and **D14 — Export via cameras** (`:475`): a camera
  *is* the `Viewport` and the export spec (resolution + framing), so its frame and
  resolution are the crop and the pixel budget an editor must be able to set.
- **Architecture:** **A4** (`docs/01-architecture.md:254`, §4) — the UI thread
  *submits* edits as transactions to the single writer, never touches the cache;
  **A8** (`:258`) — the L1 core (project/scene/interact/commands/dockmodel) is
  ImGui/GL/SDL-free, the interact/commands seam joined only at L3 `views`; **A11**
  (`:261`) — `interact` stays pure math (hit-test/gizmo/snapping), tool→interaction
  dispatch is promoted into `interact` when a canvas consumer exists; **A14** (`:264`)
  — camera mutations (create/rename/reframe) are `commands` transactions (D15,
  undoable via the journal), camera `Content` lives in L1 `scene`, no new
  component/edge; **A15** (`:265`) — the camera `Content` is an `arbc::Editable`
  capturing live per-`Content` state into a non-inert `StateHandle`, written via
  `txn.set_content_state`. **§8** levelization DAG (`:144-179`; the edge table
  `:162-175`: `interact` → base/scene/libarbc, ImGui **no**; `commands` →
  base/project/scene; `views` → scene/interact/commands/render/dockmodel/imgui);
  **§9** the universal DoD (`:181-208`).

**Editor seams this leaf extends:**

- **The camera model (edit targets):** `scene::Camera`/`cameras`
  (`src/scene/ace/scene/camera.hpp:113-124`); `CameraContent` + its `arbc::Editable`
  store (`camera.hpp:42-102`, impls `src/scene/camera.cpp:48-110`); `Resolution`
  (`camera.hpp:26-31`); `scene::rename_camera` — the mutator to mirror
  (`camera.hpp:155-156`, impl `camera.cpp:511-532`); `register_camera_kind`
  (`camera.hpp:108`).
- **The `arbc::Editable` facet:** `capture/restore/state_cost/retain/release`
  (`arbc/contract/content.hpp:462-476`), discovered via `Content::editable()`
  (`content.hpp:581`); `CameraContent::editable()` returns `this` (`camera.hpp:56`);
  thread contract capture/restore/retain on the writer thread, release on the drain
  thread; tripwire `document.editable_binding().unrouted_state_calls() == 0`.
- **The frame edit primitive:** `Document::set_layer_transform(ObjectId layer, const
  arbc::Affine&)` (`arbc/runtime/document.hpp:133-142`) — transactional, one journal
  entry, undoable. Already exercised by "a cameras.manip-style frame edit through
  apply_edit" in `tests/canvas_host_test.cpp:855-860` and
  `tests/look_through_e2e_test.cpp:404`.
- **The interact math home:** `src/interact/ace/interact/interact.hpp` — pure
  `arbc::Affine` math (nav `pan`/`zoom`/`fit` `:28-54`; shot `new_shot_from_view`
  `:62-66`; inverse `viewport_camera_for_shot` `:93-94`; `look_through` `:103-108`);
  the header names "gizmo" math as its charter (`:8`). This leaf adds the frame-manip
  helpers **beside** them (D-manip-3).
- **The edit-commit seam:** `CanvasHost::apply_edit` (`src/render/canvas_host.cpp:150-165`)
  ← `CanvasView::apply_edit` (`src/app/canvas_view.cpp:210`) ← edit runner
  `AppProjectGateway::set_edit_runner`/`run_edit`, bound `src/app/shell.cpp:275`;
  gesture coalescing `commands::AppState::next_gesture_key()`
  (`src/commands/ace/commands/app_state.hpp:88`) + `doc.transact(name).coalesce(key)`
  (reference `tests/undo_test.cpp:159-232`); undo/redo are journal-cursor navigation
  `commands::undo/redo` (`app_state.hpp:147,152`).
- **The Canvas interactive body (gizmo home):** `views::draw_canvas_interactive`
  returning a `CanvasInput` (`src/views/ace/views/views.hpp:46-62`), consumed at
  `src/app/canvas_view.cpp:120-169` where nav gestures are turned into `interact`
  calls and submitted — the pattern the frame gizmo parallels, but committing to the
  `Document` instead of `request_camera`. The existing per-canvas camera picker
  (`CanvasView::draw_camera_picker`, `canvas_view.cpp:177-196`) is the nearest camera
  UI.
- **The inspector home:** `dockmodel::ViewType::Inspector`
  (`src/dockmodel/ace/dockmodel/view_registry.hpp:19`; the `:17-18` comment notes
  "cameras live within Overview / Layers" — there is no standalone Cameras view type,
  D-view-registry-3); a body is plugged via `views::register_view_body(ViewType,
  ViewBody)` (`views.hpp:79-88`), the reference registered body being
  `views::draw_history(commands::AppState&, std::string_view)` (`views.hpp:106`).

**libarbc API surface** (fetched under `build/*/_deps/arbc-src/`, staged headers at
`/tmp/arbc_stage/include`): `arbc::Affine` (+ `inverse()`, `apply()`, compose) — the
value all frame math speaks; `arbc::Model::Transaction` + `txn.set_content_state`
(`content.hpp`); `Document::set_layer_transform` (`document.hpp:133`); the fixed
record set has **no camera record** (a camera is a `Content` + `Layer`, A14) and
`arbc::Viewport` is a transient compositor value only.

**Predecessor / sibling refinements:** `tasks/refinements/cameras/model.md`,
`tasks/refinements/cameras/rename_stable_id.md`, `tasks/refinements/editor/look_through.md`,
`tasks/refinements/editor/edit_render_sync.md`.

**Test rigs:** `ace_tests` (Catch2, headless; `tests/camera_model_test.cpp` is the
sibling to extend — `CMakeLists.txt:230`); goldens under `tests/goldens/*.rgba8` via
`ace_test::compare_golden` (`tests/golden_support.hpp:36-46`) with the GL-free
offline path `render::render_document_srgb8(doc, w, h, camera)`
(`src/render/ace/render/render.hpp:37-38`, pattern `tests/look_through_test.cpp:170-203`);
`ace_shell_test` (ImGui Test Engine, offscreen software-GL, `CMakeLists.txt:246-258`;
patterns `tests/look_through_e2e_test.cpp`, `tests/multi_canvas_e2e_test.cpp` —
Document edits run on the main thread via the `E2EState` handshake,
`look_through_e2e_test.cpp:161-162,400-406`); `asan`/`tsan` presets, residual Mesa
leaks via `tests/lsan.supp`; coverage `diff-cover --fail-under=90`.

## Constraints / requirements

1. **Levelization (`check_levels` clean) — the primary structural assertion.** The
   frame-manip math lands in **L1 `interact`** (pure `arbc::Affine` math beside the
   nav/shot helpers; it takes **primitive** frame `Affine` + a resolution *aspect*,
   never a `scene::Camera`/`scene::Resolution`, so the existing `interact→scene` edge
   is not exercised and **no new edge** appears — consistent with look_through's
   primitive-values discipline). The resolution editor lands in **L1 `scene`**
   (`set_camera_resolution` over the existing `CameraContent` editable store — the
   camera `Content`'s home per A14). The gizmo wiring and the inspector body are
   **L3 `views`** / **L4 `app`** (both already see ImGui). **No new component, no new
   DAG edge, no `check_levels` edit**; the L1 core gains no ImGui/GL/SDL include.

2. **Frame resize is a re-crop that holds resolution and is aspect-locked to the
   resolution aspect (D9/D8).** The `interact` resize helper takes the current frame
   `Affine` and the resolution **aspect** (`W:H`) as inputs it **never mutates**, and
   returns a new frame `Affine` whose covered composition rectangle has aspect `W:H`
   (uniform scale of the covered region → square pixels). The camera `Content`'s
   `Resolution` is untouched by any frame edit. Corner-drag re-crops uniformly;
   edge-drag re-crops holding aspect (grows/shrinks the covered region while keeping
   `W:H`). This is the D8 anti-resample rule for cameras: dragging changes placement,
   never pixel count.

3. **Resolution is edited only in the inspector, in place, preserving `ObjectId`
   (A14/A15, D-rename-2).** `scene::set_camera_resolution` mints a new `Version` over
   the current name (mirror of `CameraContent::set_name`) and journals it via
   `txn.set_content_state` in one transaction — the content `ObjectId`, binding-layer
   `ObjectId`, name, frame, and layer order are all preserved (so
   `commands::Selection` and any active gizmo survive, per rename_stable_id). The
   correctness tripwire `editable_binding().unrouted_state_calls() == 0` must hold.

4. **The frame and the resolution are always independent (D7/D9).** Editing
   resolution while **holding aspect** re-samples the *same* framed region at more/
   fewer pixels (reusing look_through's `viewport_camera_for_shot` `out/native`
   scaling) — it does **not** re-crop. Editing resolution's **aspect** re-fits the
   frame to the new aspect (the frame "follows"): the covered region holds its
   position and its horizontal extent, adjusting the vertical extent to the new aspect
   (D-manip-7) — a resolution edit + a follow-frame `set_layer_transform` committed as
   **one** coalesced gesture (one undo step). Editing the frame never changes
   resolution.

5. **Every manip edit is an undoable `Document` transaction on the UI thread, routed
   through the settled `apply_edit`/edit-runner seam (A4, edit_render_sync
   Constraint 1) — one undo step per gesture.** A continuous frame drag **previews**
   as session state (the gizmo redraws at the dragged `Affine`; no journal churn) and
   commits a **single** `set_layer_transform` transaction on release; an aspect change
   commits one coalesced content+frame transaction. No manip path bypasses
   `apply_edit`, adds no new locking, and holds `doc_mu` only for the mutation itself.

6. **Live preview through a look-through canvas (D9, look_through).** Because a manip
   edit commits through `apply_edit`/`doc_mu` and `editor.cameras.look_through`
   re-reads `scene::cameras(pin())` each frame, a second canvas looking through the
   manipulated camera reflects the committed frame/resolution on the next frame — with
   **no new wiring** here (the look-through leaf already owns the per-frame re-derive).

7. **Direct-manipulation border-grab, not a modal tool (D7).** The camera-frame gizmo
   is the default select/transform behavior: hit-test the frame border/label/handles
   (interior click-through), drag to re-crop/move/dutch. It does **not** depend on
   `editor.canvas.tool_dispatch` (D20/A11) — the border-grab is direct manipulation in
   the Canvas interactive body (parallel to the existing nav gestures); when
   tool_dispatch lands it may route a dedicated transform tool, but the default path
   works standalone. Modifier constraints (Shift 15°/aspect/axis-lock; Alt from
   center; Cmd/Ctrl bypass-snap; Space pans the view, inert during a frame grab) are
   honored; **cross-object snapping** (cell edges/centers, camera frames, grid) is the
   shared engine `editor.cells.gizmo` owns and is **deferred to it** (Constraint 8),
   not shipped here.

8. **Self-contained under the `!model`-only dependency; defers to existing leaves, no
   new WBS work.** `editor.cells.selection` (unified `ObjectId`-keyed targeting),
   `editor.cells.gizmo` (the shared snapping engine + the visual transform-handle
   chrome cells and cameras share), `editor.cells.resolution` (the per-camera
   resolution-**health** readout — camera out-resolving a captured cell), and
   `editor.panels.inspector` (the dense property sheet that hosts this leaf's
   resolution control) are all **already-scheduled sibling leaves** that consume this
   leaf's seams. This leaf targets a camera by its own frame hit-test / a compact
   inspector chooser (no selection dependency) and ships the modifier constraints and a
   first-cut inspector body; the richer surfaces are those leaves' scope. **No new WBS
   leaf is spawned.**

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate`
green (check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.** No new
  component, no new DAG edge, no lint edit; `interact` gains no `scene`/UI include
  (its manip helpers take primitive `arbc::Affine` + aspect); `scene`'s resolution
  editor adds no UI include; nothing in the L1 core includes ImGui/GL/SDL. Confirm
  `scripts/gate`'s level lint passes.
- **L1 logic — Catch2 units:**
  - **Frame math** (`tests/camera_manip_test.cpp`, in `ace_tests`):
    - **Re-crop holds resolution + aspect-locks:** for representative frames and a
      resolution aspect `W:H`, a corner/edge resize returns a frame whose covered
      composition rectangle has aspect exactly `W:H` (square pixels) and whose scale
      changed by the drag; the input resolution value is unaffected (the helper never
      sees or returns a `Resolution`). Assert the D9 direction: a larger covered region
      ⇒ same pixel count ⇒ lower PPI, and the inverse.
    - **Move / pan:** dragging the frame translates the covered region by the drag
      delta with no scale/rotation change.
    - **Dutch:** a modifier-gated rotate composes a pure rotation about the frame
      center (or pivot); Shift snaps to 15° increments; resolution and covered-region
      size are unchanged (rotation only).
    - **Hit-test:** `hit_frame` returns the border/label/corner/edge handle for points
      on the frame outline and **None** for interior points (click-through, D7); a
      near-corner point resolves to the corner over the edge.
    - **Degenerate guards:** non-positive aspect or a non-invertible frame yield a safe
      no-op (identity / unchanged), never a div-by-zero.
  - **Resolution editor** (extend `tests/camera_model_test.cpp`, the sibling that
    already asserts rename ObjectId-stability at `:182-240`): `set_camera_resolution`
    changes `resolution()` while preserving the content `ObjectId`, binding-layer
    `ObjectId`, name, `frame`, and layer order; `commands::dispatch` advances the
    journal and `undo`/`redo` round-trips the resolution; `editable_binding().bound()`
    and `unrouted_state_calls() == 0` hold; free-list reuse across
    resize→undo→resize→resize; non-positive dims rejected as a no-op.
  - **Frame↔resolution independence:** editing resolution holding aspect leaves
    `frame` byte-identical; editing the frame leaves `resolution()` unchanged; an
    aspect change produces the deterministic follow-frame (D-manip-7) and commits as
    one journal entry (one undo step).
- **Rendered output — golden (`render_offline` byte-exact).** A new golden
  `tests/goldens/camera_manip_recrop_64x64.rgba8`: build a cells doc + a camera at
  resolution `R` framing region `A`; apply an `interact` re-crop to region `B`
  (holding `R`); render `render_document_srgb8(doc, R.w, R.h,
  viewport_camera_for_shot(new_frame, R.w, R.h, R.w, R.h))` and assert **byte-identical**
  to the stored crop of region `B` at resolution `R` — pinning "resize re-crops, holds
  resolution." A companion assertion (no separate golden needed) renders the **same**
  framed region at `k·R` and asserts it equals `R`'s output upscaled by `k` (resolution
  = resample, not re-crop; the D8/D9 distinction), **no tolerance**.
- **UI e2e — ImGui Test Engine** (`tests/camera_manip_e2e_test.cpp`, in
  `ace_shell_test`, modeled on `tests/look_through_e2e_test.cpp`), offscreen
  software-GL, driven by widget id, Document edits on the main thread via the
  `E2EState` handshake:
  - seed a camera `Hero` (test helper, as the camera-model tests create cameras);
    grab its frame border in the Canvas body and drag a corner; assert one journal
    entry was added (one undo step), the camera's `frame` changed, and its `resolution`
    is **unchanged** (re-crop holds resolution); `undo` restores the original frame.
  - drive the resolution inspector: set W×H (holding aspect); assert `resolution`
    changed, `frame` unchanged, one undo step; change an aspect preset and assert the
    frame follows (D-manip-7) as one undo step.
  - **modifier gating:** a dutch rotate only engages under the gating modifier; Shift
    snaps to 15°; Space during a frame grab pans the *view* (nav), not the frame.
  - **live preview:** open a second canvas looking through `Hero`
    (`editor.cameras.look_through`); apply a frame re-crop; assert the look-through
    canvas's sequence advances and its framing changes (the export-preview-is-live
    property, consumed unchanged).
- **Threading — ASan/TSan** (one case appended to `tests/canvas_host_test.cpp`'s
  real-pool sanitizer suite with `default_interactive_pool_config()`, joining the
  existing `:855-860` cameras.manip-style edit case): the UI thread streams frame
  re-crops (`set_layer_transform`) **and** resolution edits (`set_camera_resolution`)
  to a camera via `apply_edit` while the render thread `drive_once`s a canvas looking
  through it and re-reading `scene::cameras(pin())`. Must be data-race-clean: manip
  edits add **no new shared mutable state** (the drag preview is UI-thread-only; the
  `arbc::Editable` capture/restore/release run on their contracted writer/drain
  threads under the existing `doc_mu` discipline). Residual Mesa leaks via
  `tests/lsan.supp`.
- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`); clang-format +
  build clean across presets.

**No new WBS leaf is deferred.** The frame-manip math and the resolution editor land
here; cross-object snapping is `editor.cells.gizmo`'s (existing leaf), the resolution-
health readout is `editor.cells.resolution`'s (existing leaf), unified selection
targeting is `editor.cells.selection`'s (existing leaf), and the dense property sheet
that hosts this control is `editor.panels.inspector`'s (existing leaf) — none is new
work (Constraint 8).

## Decisions

- **D-manip-1 — Two mutation channels, both pre-built for undo: frame = binding-layer
  `Affine` via `Document::set_layer_transform`; resolution = camera-`Content` editable
  state via a new `set_resolution` mirroring `set_name`.** The frame is the crop
  (there is no separate crop object — D9 makes the frame *be* the crop, and the model
  stores it as the layer `Affine`); resolution+name are the `Content`'s editable
  `Version`. So a reframe drives `set_layer_transform(cam.layer, new_affine)` (one
  journal entry, `document.hpp:133-142`) and a resolution edit drives a new
  `CameraContent::set_resolution(txn, self, Resolution)` + `scene::set_camera_resolution`
  wrapper (mirror of `set_name`/`rename_camera`, `camera.cpp:97-110`/`:511-532`).
  *Rationale:* this is exactly the split the camera model shipped (A14/A15,
  `camera.hpp:112`), and D-rename-2 chartered the resolution editor as reuse-for-free —
  no new persistence, no new transaction machinery, `ObjectId` preserved.
  *Alternative rejected:* a separate crop-rectangle object edited independently of the
  frame — contradicts D9 (frame *is* the crop) and would add a record the fixed libarbc
  record set doesn't have (A14) and a second source of truth that drifts from the layer
  `Affine`. **No doc delta required.**

- **D-manip-2 — Frame resize is re-crop, holding resolution, aspect-locked to the
  resolution aspect; the interact helper never touches the `Resolution`.** The resize
  math takes the frame `Affine` + the aspect `W:H` and returns a new frame whose
  covered region has aspect `W:H` (uniform scale → square pixels); pixel count is out
  of scope of the frame op entirely. *Rationale:* D9/§6 verbatim ("resize = re-crop,
  aspect-locked to resolution, holds resolution"); D8's "handle-drag changes placement,
  never resolution — non-destructive." Making resolution an *input the helper cannot
  mutate* structurally guarantees the invariant. *Alternative rejected:* let a
  corner-drag change the resolution (resample-on-drag) — D8's named anti-pattern;
  dragging would become destructive and couple two controls D7 requires independent.
  **No doc delta required.**

- **D-manip-3 — The frame-manip math is pure `interact` L1, over primitive `Affine` +
  aspect, beside the nav/shot helpers; `views` wires it to committed edits.** New
  `interact` helpers — re-crop, move, dutch, and `hit_frame` — take/return
  `arbc::Affine` (+ an aspect and modifiers), never a `scene::Camera`. *Rationale:*
  A8/A11 make `interact` the pure-math home (hit-test/gizmo) and join the
  interact/commands seam at L3 `views`; taking primitive values (as look_through did)
  keeps `interact` decoupled from `scene` and unit-testable headless — the bulk of the
  coverage. *Alternative rejected:* implement the gizmo math in `views` (L3) — it would
  be untestable without ImGui/GL and violate the "L1 logic is the bulk" DoD;
  *or* thread a `scene::Camera` into `interact` — needless coupling when only the frame
  `Affine` and the aspect are math inputs. **No doc delta required** (a new function in
  an existing L1 component — no new component/edge/dependency).

- **D-manip-4 — Direct-manipulation border-grab, previewed as session state, committed
  as one transaction on release = one undo step per gesture.** The gizmo hit-tests the
  camera border/label/handles (D7 interior click-through), previews the dragged frame by
  redrawing the gizmo rectangle (no journal writes), and commits a single
  `set_layer_transform` through `apply_edit` on mouse-up. *Rationale:* mirrors the
  transient nav-camera pattern (`canvas_view.cpp:120-169`); one commit per gesture is a
  clean undo boundary (D15) with **no** coalesce bookkeeping for the common
  reframe/move/dutch drag, and honors edit_render_sync Constraint 1 (all UI-thread
  mutations via `apply_edit`). It needs no modal tool, so it does **not** depend on
  `editor.canvas.tool_dispatch` (D20/A11). *Alternative rejected:* commit every
  mouse-move under a `next_gesture_key()` coalesce — heavier (journal churn coalesced
  away) and more complex than preview-then-commit-once, with no user-visible benefit for
  a bounded drag. (Coalesce **is** used for the single aspect-change compound edit,
  D-manip-7 — the one case that legitimately spans two records.) **No doc delta
  required.**

- **D-manip-5 — Dutch rotation is modifier-gated, Shift-snapped to 15°, about the frame
  center/pivot.** A pure rotation composed onto the frame `Affine`, engaged only under
  the gating modifier — not a default handle. *Rationale:* D9/§6 ("modifier-gated dutch
  rotation, like cell shear — advanced, not a default handle"; Shift = 15° per the
  shared modifiers rule `:252-254`). *Alternative rejected:* a permanent visible rotate
  handle on the frame — D9 explicitly says dutch is not a default handle; a permanent
  handle would clutter the common reframe interaction. **No doc delta required.**

- **D-manip-6 — Resolution editing ships as a first-cut Inspector view body (W×H +
  aspect presets) driving `set_camera_resolution`; the dense property sheet, the
  resolution-health readout, cross-object snapping, and unified selection targeting are
  existing sibling leaves' scope.** This leaf registers a compact
  `dockmodel::ViewType::Inspector` body (`register_view_body`, following
  `views::draw_history`) that lists cameras and edits the targeted camera's output
  resolution; the D9 "look through" button belongs to `editor.cameras.look_through`/
  `editor.panels.overview`, the per-camera resolution-**health** read to
  `editor.cells.resolution` (it owns the health badge), the cross-object snapping engine
  to `editor.cells.gizmo`, the unified `ObjectId`-keyed selection to
  `editor.cells.selection`, and the dense multi-property Inspector sheet to
  `editor.panels.inspector` (which composes this control). *Rationale:* keeps manip
  self-contained under its `!model`-only dependency and testable now, while avoiding
  duplicating machinery those already-scheduled leaves own (each consumes this leaf's
  seams — no new WBS work, Constraint 8). *Alternative rejected:* ship the
  resolution-health readout here — its cell-resolution comparison logic is
  `editor.cells.resolution`'s; a second copy would drift; *or* block on
  `editor.cells.selection` for targeting — it is a sibling (`depends !model` +
  `editor.cameras.model`), strictly not-before manip, so manip must target standalone
  (frame hit-test / inspector chooser). **No doc delta required** (no new view type —
  a body on the existing `ViewType::Inspector`, D-view-registry-3 intact).

- **D-manip-7 — Editing resolution holds the frame when aspect is held (pure resample),
  and re-fits the frame deterministically when aspect changes (the frame follows) — one
  coalesced undo step.** A W×H change holding aspect is a single `set_camera_resolution`
  transaction; the framed region is unchanged and merely renders at more/fewer pixels
  (the `viewport_camera_for_shot` `out/native` scaling from look_through). An aspect
  change re-fits the covered region to the new aspect — **holding its position and
  horizontal extent, adjusting the vertical extent to the new aspect** — as a
  `set_camera_resolution` + follow-`set_layer_transform` committed under one coalesce
  key (one undo step). *Rationale:* D9 ("editing resolution changes pixel count and
  aspect *independently* of which region the frame covers; the frame follows"). The
  hold-horizontal-extent follow rule is the deterministic, testable reading of "the
  frame follows"; pinning one axis makes an aspect change reproducible in the golden and
  the Catch2 units. *Alternative rejected:* on an aspect change, leave the frame
  anamorphic (non-square pixels) — violates D9's square-pixels/aspect-lock; *or* re-fit
  by area rather than a held axis — under-determined and harder to test. **No doc delta
  required.**

## Open questions

(none — all decided.) The empirical properties — the exact `arbc::Affine` composition
for re-crop/dutch and the aspect-follow rule — are pinned by the Catch2 units and the
byte-exact golden (D-manip-2/-5/-7 / Acceptance), not deferred. No human-judgment item
surfaces for `tasks/parking-lot.md`; no new WBS leaf is spawned. The cross-leaf
boundaries (shared snapping, resolution-health, unified selection, the dense inspector
sheet) resolve to **already-scheduled** sibling leaves (D-manip-6, Constraint 8), each
of which consumes a seam this leaf ships — none is this leaf's to build and none is new
work.

## Status

**Done** — 2026-07-22.

- Frame gizmo + manip math in `src/interact/interact.{hpp,cpp}`: border-grab hit-test
  (`hit_frame`, interior click-through), aspect-locked re-crop about the opposite pivot
  (every resize handle), move (translate covered region), and modifier-gated dutch
  (rotate about frame center, preserving covered size) — pure `arbc::Affine` math over the
  `pin()` frame, committed on release as one `set_layer_transform` through `apply_edit`.
- In-place resolution edits in `src/scene/camera.{hpp,cpp}`: `set_camera_resolution` and
  `set_camera_resolution_and_frame` (aspect-change follow-frame in one coalesced transaction),
  preserving the camera's `ObjectId` (D-manip-1/7).
- Resolution inspector body in `src/app/camera_inspector.{hpp,cpp}` (aspect presets + swap),
  wired into the Canvas via `src/app/canvas_view.{hpp,cpp}`, `src/app/shell.cpp`, and
  `src/views/views.{hpp,cpp}`.
- Tests: `tests/camera_manip_test.cpp` (8 geometry cases), `tests/camera_manip_e2e_test.cpp`
  (gizmo + inspector + modifier gating + live preview), `tests/camera_model_test.cpp` +
  `tests/canvas_host_test.cpp` streamed-manip concurrency anchors; byte-exact golden
  `tests/goldens/camera_manip_recrop_64x64.rgba8`.
- The manip edit path surfaced the `doc_mu` render-contention that this commit also resolves
  by adopting libarbc v0.2.0 (`editor.canvas.arbc_v020` + `editor.canvas.single_writer`): the
  render read is now lock-free (COW content bindings), so streamed frame/resolution edits no
  longer contend a per-frame mutex.
