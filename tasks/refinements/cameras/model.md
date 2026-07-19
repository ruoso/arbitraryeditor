# editor.cameras.model — Viewport + shot cameras in the document; create

## TaskJuggler entry

- **Task:** `editor.cameras.model` (`tasks/00-editor.tji:199-204`, under `task cameras "Cameras"` at `:198`).
- **Effort:** `2d` · `allocate team`.
- **Depends:** `editor.canvas.view` (`:202`).
- **Note (`.tji:203`):** "Cameras are libarbc Viewports persisted as scene
  objects: the active viewport camera (screen-sized) plus saved shot cameras
  (own export resolution). Create / name / 'new shot from view'. Design: D2."
- **Back-link:** the `.tji` note currently ends `Refinement:
  tasks/refinements/camera_model.md` (the flat interim path). This refinement
  lands at **`tasks/refinements/cameras/model.md`** per the area-subdir layout
  (`tasks/refinements/README.md:9-18`); the closer updates the note back-link to
  the real path and adds `complete 100` after `allocate team`
  (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji` here.
- **Downstream dependents** (why this is the cameras keystone): `editor.cameras.manip`
  (`!model`, `:208`), `editor.cameras.look_through` (`!model`, `:214`),
  `editor.cameras.export` (`!model`, `:220`), `editor.cells.selection`
  (`editor.cameras.model`, `:236`), `editor.panels.overview`
  (`editor.cameras.model`, `:270`).

## Effort estimate

**2 days.** The bulk is L1: a `scene` camera scene-object type + read accessor,
the editor's **first custom libarbc `Content` kind** (`org.arbc.camera`,
non-rendering) + its codec, three `commands` transactions (create / rename /
"new shot from view"), and the persistence roundtrip. No ImGui, no GL, no
threading model change — the coverage is Catch2-heavy, which keeps 2d realistic.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.canvas.view`** (`tasks/refinements/editor/canvas_view.md`) — the
  live canvas over the one shared `Document`. D-canvas_view-5 (`:498-508`)
  established that "the canvas frames the document's root composition with a
  default identity/fit camera; the **persisted camera model is
  `editor.cameras.model`**" — i.e. this task is the named successor that turns
  the transient default camera into persisted scene objects.
- **`editor.canvas.nav`** (`tasks/refinements/editor/nav.md`) — the transient
  viewport camera. D-nav-1 (`:396-406`): "the viewport camera is transient
  app-layer state, never a `transact`," living in
  `app::CanvasView::Presenter::camera` (`src/app/ace/app/canvas_view.hpp:99`).
  nav explicitly hands off (`nav.md:22-25`, `:193-195`): "`editor.cameras.model`'s
  'new shot from view' snapshots this leaf's transient viewport camera into a
  persisted scene object." **This is the primary seam handoff.**
- **`editor.project.app_state`** (`tasks/refinements/editor/app_state.md`) — the
  `commands::Command`/`dispatch` seam and the single owned `Document` +
  `Registry`. The doc **reserves `editor.camera.*`** as where concrete
  `Command`s land (`src/commands/ace/commands/app_state.hpp:103-108`).
- **`editor.canvas.fit_bounds`** (`tasks/refinements/editor/fit_bounds.md`) — the
  template for a small **L1 read accessor over `pin()`/`find_first_composition`**
  (`project::root_composition_size`, `src/project/project.cpp:35-53`); the camera
  read accessor mirrors it.
- **`editor.dock.view_registry`** (`tasks/refinements/editor/view_registry.md`) —
  D-view-registry-3: **there is no "Cameras" view type**; cameras surface within
  Overview/Layers (D6). This task adds **no view** and therefore **no ImGui**.
- **`editor.canvas.multi_canvas`** (`tasks/refinements/editor/multi_canvas.md`) —
  restates D19: a canvas is *only* a camera and carries no selection/panel state;
  the persisted camera list is project-level, not per-canvas.

**Pending (owned here):** the persisted camera scene-object model itself, its
libarbc `Content` kind + codec, the three mutating commands, and the
project.arbc roundtrip. Nothing downstream is blocked on an unwritten
predecessor — all consumers listed above already exist as scheduled leaves.

## What this task is

Introduce **shot cameras as persisted scene objects** in the one shared
`Document`, plus the three operations the note names — **create**, **name**, and
**"new shot from view."** A shot camera is exactly the design's "first-class
placed object that *looks at* the composition" (`docs/00-design.md:49-51`):
an **affine frame placement** (a rectangle you drag) plus an **output
resolution** (device pixels), persisted in `project.arbc` so it travels and
versions with the cells (`docs/00-design.md:519-520`). Because libarbc has **no
camera record** (its record set is fixed; `arbc::Viewport` is a transient
compositor value), a camera is stored as the editor's **first custom `Content`
kind** (`org.arbc.camera`) attached by one non-rendering `Layer` — see **A14**
(doc delta this task adds). Mutations flow through the `commands`
transaction/undo seam (D15: a saved shot's framing IS scene data). The
**transient viewport camera** (the free-nav one you paint through) is *not*
persisted by this task — it already exists as `Presenter::camera` (D-nav-1);
this task only *reads* it to implement "new shot from view."

This is a pure L1 model task. It ships **no UI** (no Cameras view exists,
D-view-registry-3) and **renders nothing** (cameras are observers) — the
create/name affordances and the render-through-camera path land in the
already-scheduled consumer leaves (see Acceptance criteria).

## Why it needs to be done

Cameras are the editor's export spec (D14), its look-through targets (D18), and
half of the shared selection model (D7/D19) — but today **no camera object is
persisted anywhere.** `src/scene/` (the architecturally-assigned home for
"cells · cameras · selection · z-order," `docs/01-architecture.md:127`) is an
empty stub (`src/scene/scene.cpp` is just `name()`); the only camera in the
codebase is the transient per-pane `arbc::Affine` (`Presenter::camera`). Five
downstream leaves (`cameras.manip`, `cameras.look_through`, `cameras.export`,
`cells.selection`, `panels.overview`) `depend` on this model existing. It is the
keystone that makes a camera a persisted, selectable, exportable object.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **D2 — Canvas model** (`docs/00-design.md:463`; prose §2 "Cameras — the
  observers," `:47-74`). A camera **is** the library's `Viewport`: output
  resolution + region→device affine (`:49-51`). Two *roles*, one primitive: the
  **viewport camera** (active, free-nav, resolution = the on-screen canvas, live
  framing is **transient session state**, `:58-61`) and **shot cameras** (saved
  framings with their own **export resolution** — scene data that travels and
  versions with the document, `:62-63`). "**'new shot from view' promotes the
  viewport's current framing into a saved shot**" (`:64-66`). Cameras **persist
  in the document as scene objects** (`:519-520`).
- **D7 — Manipulation model** (`:468`; §6 "The unifying shape," `:205-216`).
  Cells and cameras share **one shape** (affine placement + a resolution number)
  and **one select tool**, differing only in direction (cell *emits* / camera
  *samples*). "Manipulated identically (D7)" — the camera model must expose the
  cell object-shape so `cells.selection`/`cameras.manip` reuse it.
- **D15 — Undo boundary** (`:476`; §9 "Undo/redo," `:363-372`). "The **viewport
  camera's** live framing (pan/zoom) is transient session state, NOT undoable …
  a **saved shot's** framing is scene data and IS." This legislates: create /
  rename / reframe-a-shot → transactions; the viewport camera → never a
  transaction.
- **D9 — Camera frame ≠ resolution** (`:470`), **D14 — Export via cameras**
  (`:475`), **D18 — Look-through** (`:479`), **D6 — cameras in the overview**
  (`:467`), **D19 — project-scoped, canvases are only cameras** (`:480`): the
  boundaries of the *downstream* leaves — cited so this task does not
  over-reach into frame-resize math (manip), render_offline (export), or
  active-camera session state (look_through).
- **libarbc mapping table** (`docs/00-design.md:501-520`): Camera (export) =
  `Viewport` + `render_offline`; Camera (editing) = `HostViewport` +
  `InteractiveRenderer`; Undo = data-model transactions. "Nothing here needs new
  library machinery."
- **Architecture:** §7 component map (`docs/01-architecture.md:112-137`; `scene`
  = "cells · cameras · selection · z-order (L1)," `:127`); §8 levelization DAG
  (`:162-175` — `scene` L1 may depend on `base, project, libarbc`, ImGui/GL/SDL
  forbidden); §9 testing/DoD (`:181-208`); A5/A7 (`:255,:257`), A8 (`:258`), A13
  canonical save (`:263`), and **A14** (this task's doc delta — the camera
  persistence seam).

**libarbc API surface** (fetched under `build/<preset>/_deps/arbc-src/`; canonical
co-dev checkout `/home/ruoso/devel/arbitrarycomposer/src/`):

- `arbc::Viewport { int width; int height; Affine camera; ObjectId anchor; }` —
  `arbc/compositor/compositor.hpp:16-36`. A **transient** compositor value, not a
  record.
- `RecordKind { Composition, Layer, Content, LayerOrderChunk }` — fixed, **no
  camera record** — `arbc/model/records.hpp:28-33`; `LayerRecord{content,
  Affine transform, opacity, …}` (`:68-92`); `ContentRecord{kind, state}`
  (`:60-63`).
- `Document`: `add_composition` (`document.hpp:167`), `add_content(shared_ptr
  <Content>, kind)` (`:106`), `add_layer(content, transform, opacity)` (`:132`),
  `set_layer_transform` (`:133`), `attach_layer` (`:149`), `transact(name)`
  (`:178`), `pin()` (`:261`), `for_each_content` (`:268,:274`); runtime content
  side-map `d_contents` (`:404`).
- Serialize: `capture_snapshot` (`document_serialize.hpp:147`), `save_document`
  (`:176`), `load_document` (`:218`); unknown kinds round-trip via
  `ContentSnapshot::unknown` → `PlaceholderContent` (`:96-102`).
- Kind/codec registration: `register_builtin_kinds`, `Registry`, `CodecTable`,
  `KindBridge`, `builtin_codecs(registry)`.

**Editor seams this leaf extends:**

- `src/scene/ace/scene/scene.hpp` — the **empty stub** that becomes the camera
  model's home.
- `commands::Command { std::string name; std::function<void(arbc::Document&)>
  apply; }` + `dispatch(AppState&, const Command&)`
  (`src/commands/ace/commands/app_state.hpp:110-126`); `undo`/`redo` (`:134-152`);
  `next_gesture_key()` (`:88`); the owned `Document` (`:91`) + `Registry`
  (`:93`); the reserved `editor.camera.*` note (`:103-108`).
- `project::root_composition_size` (`src/project/project.cpp:35-53`) — the
  `pin()`→`DocRoot::find_first_composition` read pattern to mirror; the
  `add_composition→add_content→add_layer→attach_layer` build pattern
  (`:19-33`).
- `project::save_project` (`src/project/ace/project/save.hpp:84-86`) over
  `builtin_codecs(registry)` — the snapshot seam the camera codec joins.
- The transient viewport camera: `Presenter::camera`
  (`src/app/ace/app/canvas_view.hpp:99`), submitted via
  `host_.request_camera(view_id, camera)` (`src/app/canvas_view.cpp:108`); built
  into `arbc::Viewport{width,height,camera}` at
  `src/render/canvas_renderer.cpp:72`.
- `interact` pure Affine math — `pan`/`zoom`/`fit`/`scale_bar`
  (`src/interact/ace/interact/interact.hpp:19-54`) — reused for the
  view→frame conversion in "new shot from view."

**Predecessor refinements:** `canvas_view.md`, `nav.md`, `app_state.md`,
`fit_bounds.md`, `view_registry.md`, `multi_canvas.md` (all under
`tasks/refinements/editor/`).

**Test rigs:** `ace_tests` (Catch2, headless — `CMakeLists.txt:225-237`,
`ACE_GOLDEN_DIR = tests/goldens`); golden harness
`ace_test::compare_golden` (`tests/golden_support.hpp`); offline render entry
`render::render_document_srgb8` (`src/render/ace/render/render.hpp:37`); `Command`
construction examples `tests/commands_test.cpp:56`, `tests/undo_test.cpp:57`.

## Constraints / requirements

1. **Levelization (primary structural assertion).** All work lands in L1
   (`scene`, `commands`, `interact`) and the existing L1 `project` snapshot seam.
   No `#include <imgui.h>`/GL/SDL anywhere. The camera `Content` type + codec +
   read accessor live in **`scene`** (which may depend on `base, project,
   libarbc`, `docs/01-architecture.md:168`). Kind/codec registration is wired at
   a level that already sees `scene` (`commands` or `app`) so `project`'s generic
   snapshot save serializes cameras with **no `project→scene` edge**. **No new
   component, no new DAG edge, no `check_levels` edit.** If `project::builtin_codecs`
   proves to be a static hardcoded list that cannot reach the `scene` codec
   without a new edge, thread registration through a `Registry`/codec-table
   callback — **never add a `project→scene` edge.**

2. **Persistence = A14, in the `Document`.** A shot camera is one `org.arbc.camera`
   `Content` + one `Layer` in the root composition: frame placement = the
   `Layer`'s `Affine transform`; output resolution (W×H) + name = the `Content`'s
   serialized state; the kind is **non-rendering**. It must serialize into
   `project.arbc` through `project::save_project` and reload identically via
   `load_document`. **Decision rule (no successor task):** register a proper
   `org.arbc.camera` codec on the editor `Registry`/codec seam; **if** pinned
   `arbc` v0.1.0 does not admit an editor-authored codec, persist via the
   unknown-field passthrough (`ContentSnapshot::unknown` → `PlaceholderContent`)
   — same observable roundtrip, no libarbc fork. The acceptance roundtrip test is
   identical either way, so the implementer picks the mechanism the pinned lib
   supports without changing the contract.

3. **Transactions, not session state (D15).** create / rename / "new shot from
   view" are each **one** `commands::Command` whose `apply` runs **one** libarbc
   `transact`, dispatched through `dispatch(AppState&, Command)`, and thus
   undoable via the existing `undo`/`redo` (no bespoke undo). The **transient
   viewport camera** (`Presenter::camera`) is **not** persisted and **not**
   moved into the `Document` by this task (D-nav-1).

4. **The D7 uniform object shape.** A camera must be an `ObjectId`-addressable
   placed object with an `Affine` frame + a resolution number — the *same* shape
   as a cell — so `cells.selection` and `cameras.manip` reuse the cell
   object/transform machinery (`set_layer_transform`). Camera layers must be
   **distinguishable by kind** so downstream consumers (the layers list, the
   compositor-output expectation) can filter them.

5. **Non-rendering invariant.** A camera contributes **zero pixels** to the
   composited output: `render_document_srgb8` of a cells-only document must be
   **byte-identical** with and without a camera present. A fresh scratch project
   has **zero** persisted cameras (shots are user-created; the viewport camera is
   transient), so existing goldens (`render_probe_64x64`, `canvas_view_64x64`,
   `canvas_nav_zoom_64x64`) are unaffected.

6. **"New shot from view" contract.** The command takes the current viewport
   framing as **values** — the viewport `arbc::Affine` + the pane W×H captured by
   the caller (`app`/`views`) — and produces a shot camera whose frame + resolution
   reproduce that framing, keeping the command pure L1. The
   viewport-`Affine`+size → (frame `Affine`, resolution W×H) conversion is an
   `interact` helper reusing existing Affine math (`interact.hpp:19-54`); the
   inverse render-through-camera derivation (frame+resolution → `arbc::Viewport`)
   is **out of scope** — it lands with `editor.cameras.export`.

7. **Read accessor.** `scene::cameras(const arbc::Document&) →
   std::vector<Camera>` (ordered, each `{ObjectId, name, resolution, frame
   Affine}`) built over `pin()` in the mould of `project::root_composition_size`.
   This is what `panels.overview`/`layers` and `cells.selection` read.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`); `scripts/gate`
green (check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.** No
  new component, no new DAG edge, no lint edit; nothing in `scene`/`commands`/
  `interact` includes ImGui/GL/SDL; the L1 core never gains a UI include.
- **L1 logic — Catch2 unit** (`tests/camera_model_test.cpp`, in `ace_tests`):
  - `scene::add_camera` / `rename_camera` mutate the `Document`; `scene::cameras`
    reads them back in order with identical name/resolution/frame.
  - `new_shot_from_view`: a known viewport `Affine` + pane W×H yields the
    expected frame `Affine` + resolution (the `interact` conversion).
  - each command dispatched via `dispatch` adds **exactly one** journal entry;
    `undo` removes the camera and `redo` restores it (the `Document` journal, not
    a reimplementation).
  - **non-rendering invariant** (Constraint 5): `render_document_srgb8` bytes are
    identical for a cells-only doc vs. the same doc with a camera added — asserted
    as byte-equality (reusing the offline path), so **no new golden file** is
    needed.
  - a fresh scratch project reports **zero** cameras.
- **Persistence roundtrip — Catch2** (in `ace_tests`): create N cameras →
  `save_project` → `load_document` → `scene::cameras` returns identical
  name/resolution/frame/order. This exercises the A14 codec (or the unknown-field
  passthrough) end-to-end.
- **Rendered output — golden N/A (justified).** Cameras are non-rendering
  observers, so this leaf produces no new rendered output; the non-rendering
  invariant is pinned by the Catch2 byte-equality above. The
  **render-through-camera-at-its-own-resolution** golden lands with
  **`editor.cameras.export`** (already scheduled, `:217-221`), which owns the
  `render_offline` path.
- **UI e2e — ImGui Test Engine N/A (justified).** This leaf ships **no widget**
  — there is no Cameras view (D-view-registry-3) and the create/name affordances
  belong to `editor.panels.layers`/`editor.panels.overview` and
  `editor.cameras.manip`. The first camera e2e (drive "new shot from view" by
  widget id, assert the persisted camera) lands with the **first consuming UI
  leaf** (`editor.panels.layers`, `:261-265`), which already carries an
  `ace_shell_test` e2e budget. No e2e is deferred as a new task — the surface
  does not exist until that leaf.
- **Threading (ASan/TSan).** No new concurrency seam: mutations are single-writer
  on the UI thread; the render thread pins the `Document` (camera layers are
  read-only pinned data, rendered as nothing) under the existing arbc contract
  (A4/A5). ASan/TSan run over create→save→load→undo through the existing lanes;
  the **real-concurrency** coverage stays owned by `frame_sync`/`multi_canvas`.
- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) — the L1 tests
  above ship with the task, clang-format + build clean.

## Decisions

- **D-model-1 — A shot camera is one editor `Content` of a new kind
  `org.arbc.camera` + one `Layer`, persisted in the `Document` (A14).** libarbc's
  record set is fixed and has no camera record (`arbc/model/records.hpp:28-33`);
  `arbc::Viewport` is a transient compositor value. To honour D2's "cameras
  persist in the document as scene objects" (`docs/00-design.md:519`), a camera
  is stored as the editor's **first custom `Content` kind** — frame = the
  `Layer`'s `Affine transform`, resolution + name = the `Content` state,
  non-rendering. *Rationale:* it is the only way to get editor data *inside* the
  `project.arbc` snapshot (which is a `Document` capture), and it makes a camera
  an `ObjectId`-addressable object identical in shape to a cell (D7), so
  `cells.selection`/`cameras.manip` reuse the cell machinery.
  *Alternative rejected:* a single `CamerasContent` list object holding all
  cameras — cleaner z-order separation, but a camera would no longer be an
  individually addressable placed object, breaking the D7 one-shape/one-select
  model that `cells.selection` (which selects "a camera BORDER/label" through the
  *same* selection, `:214-216`) depends on. *Alternative rejected:* an
  editor-side side-structure serialized to a sidecar file — violates D2/D16
  (cameras must travel and version *in* `project.arbc`, not beside it).
  *Alternative rejected:* petition libarbc for a camera record — arbc is a pinned
  external dep and the design is explicit that "nothing here needs new library
  machinery" (`:503`). **Doc delta: A14** (`docs/01-architecture.md`), added in
  this task's commit.

- **D-model-2 — Persist via a registered `org.arbc.camera` codec, with the
  unknown-field passthrough as the tested fallback — not an audit.** *Rationale:*
  whether pinned `arbc` v0.1.0 exposes an editor-authorable `Content`-kind codec
  is an empirical property of the fetched headers; rather than defer that to a
  "decide later" task (which would loop), the contract is fixed by a **testable
  decision rule** (Constraint 2): register a real codec if the surface admits it,
  else persist through `ContentSnapshot::unknown` → `PlaceholderContent` (which is
  non-rendering and transform-preserving for free). Both satisfy the identical
  roundtrip test, so the mechanism is an implementation detail the closer never
  has to re-open. *Alternative rejected:* a `camera.persistence_mechanism` WBS
  task — its only deliverable would be "decide," which no implementer can close.

- **D-model-3 — The transient viewport camera stays session state; this task
  persists only shot cameras.** *Rationale:* D2/D15 and D-nav-1 are explicit that
  the viewport camera's live framing is transient (like scroll position), living
  in `Presenter::camera`. Persisting it would contradict the constitution and the
  nav refinement. "New shot from view" *reads* it to mint a shot; the active-camera
  *selection* (which shot a canvas looks through) is session state owned by
  `editor.cameras.look_through`. *Alternative rejected:* storing the active
  viewport camera as a document object now — that is precisely the transient/scene
  line D15 draws; it belongs nowhere in the persisted set. **No doc delta beyond
  A14.**

- **D-model-4 — Mutations reuse the `commands` transaction/undo seam; no bespoke
  undo.** *Rationale:* `app_state.md` reserves `editor.camera.*` for exactly this
  (`app_state.hpp:103-108`); a `Command{name, apply}` running one `transact`
  gives undo/redo for free via the existing journal cursor (D15). *Alternative
  rejected:* a separate camera edit log — reinvents the library's transactional
  undo the whole editor is built on. **No doc delta required.**

- **D-model-5 — The camera model + read accessor live in `scene`; registration is
  wired above it.** *Rationale:* §7 assigns "cameras" to `scene` (`:127`); `scene`
  may reach `arbc` and `project`, so the `Content` type, codec, mutation helpers,
  and `cameras()` accessor all sit there, and `commands`/`app` (which see `scene`)
  register the kind — keeping `project`'s save generic and the DAG unchanged.
  *Alternative rejected:* putting the camera `Content`+codec in `project` to sit
  next to `save_project` — it would misplace cameras (against §7) and is
  unnecessary once registration is threaded through the `Registry`/codec seam.
  **No new DAG edge.**

## Open questions

(none — all decided.) The one empirical unknown — whether pinned `arbc` v0.1.0
admits an editor-authored `Content` codec — is resolved mechanically by
D-model-2's tested fallback, not deferred. No human-judgment item surfaces for
`tasks/parking-lot.md`; no new WBS leaf is spawned (every downstream consumer —
`cameras.manip`, `cameras.look_through`, `cameras.export`, `cells.selection`,
`panels.overview`, `panels.layers` — already exists as a scheduled leaf).

## Status

**Done** — 2026-07-18.

- Introduced `scene::CameraContent` (`org.arbc.camera`) as the editor's first custom libarbc `Content` kind — non-rendering, persisted in the `Document` via a proper codec registered on the `Registry`/codec seam (`src/scene/ace/scene/camera.hpp`, `src/scene/camera.cpp`).
- Shipped `scene::add_camera`, `scene::rename_camera`, and `scene::cameras(const arbc::Document&)` — the full L1 camera model; `interact::new_shot_from_view` wired as a pure Affine conversion (`src/interact/ace/interact/interact.hpp`, `src/interact/interact.cpp`).
- Wired codec registration via `project::seed_kind_bridge` called from `commands::AppState` constructor — no `project→scene` edge added; `project::save_project` serializes cameras generically (`src/project/ace/project/project.hpp`, `src/project/project.cpp`, `src/project/save.cpp`, `src/commands/app_state.cpp`).
- 22 Catch2 L1 units in `tests/camera_model_test.cpp`: add/rename/`cameras()` order+roundtrip; `new_shot_from_view` (incl. degenerate/non-invertible); dispatch→journal-advances + undo/redo; non-rendering byte-equality; fresh-project-zero; no-op paths; codec escape + UTF-8 roundtrip; `save_project`→`load_document` persistence roundtrip.
- Fixed a pre-existing deterministic test-observation bug in `tests/canvas_view_e2e_test.cpp`: probe-color assertion now drives frames until the composited pixel is visible (bounded deadline) instead of the unsound frame-quiet heuristic that failed under sanitizer slowdown.
- `docs/01-architecture.md` A14 delta: documents the `org.arbc.camera` Content persistence seam.
- One deviation from spec: `add_camera`/`rename_camera` each produce two journal entries (libarbc has no atomic content+layer+attach API); D15 observable contract holds — one undo detaches the camera layer, one redo restores it.
