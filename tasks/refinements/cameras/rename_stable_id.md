# editor.cameras.rename_stable_id — In-place camera rename preserving ObjectId (Editable facet)

## TaskJuggler entry

- **Task:** `editor.cameras.rename_stable_id` (`tasks/00-editor.tji:218-223`, under
  `task cameras "Cameras"` at `:210`).
- **Effort:** `0.5d` · `allocate team`.
- **Depends:** `!model` — the sibling `editor.cameras.model` (`:221`), already
  `complete 100` (`:214`).
- **Note (`.tji:222`):** "rename_camera currently replaces the Content+Layer (new
  ObjectId), breaking future selection/manip identity for cells.selection/cameras.manip;
  implement in-place content-state rename via the Editable facet to preserve ObjectId
  across renames. Source-of-debt: tasks/refinements/cameras/model.md. Design:
  docs/00-design.md D7."
- **Back-link:** the closer appends `Refinement:
  tasks/refinements/cameras/rename_stable_id.md` to the `.tji` note and adds
  `complete 100` after `allocate team` (`tasks/refinements/README.md:47-68`). **Do not**
  hand-edit the `.tji` here.

## Effort estimate

**0.5 day.** A pure L1 `scene` change with no new file: give `CameraContent` the
`arbc::Editable` facet (a tiny versioned name/resolution store — the camera state is a
string + two ints, not raster tiles), and rewrite one function (`rename_camera`) to a
single in-place `set_content_state` transaction. `Document::add_content` already
auto-binds any editable content and captures its initial state, so there is **no host
wiring**. The coverage is a handful of Catch2 assertions extended onto the existing
`tests/camera_model_test.cpp`. No new component, no ImGui, no golden.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cameras.model`** (`tasks/refinements/cameras/model.md`, **Done**
  `model.md:381-391`) — shipped `scene::CameraContent` (`org.arbc.camera`), `add_camera`,
  `rename_camera`, `cameras()`, the codec, and the `project` roundtrip. This task
  **rewrites `rename_camera` only**; everything else it built is a fixed input. The model
  refinement **named this task as the follow-up** for the rename debt it knowingly shipped
  (`model.md:391`: "add_camera/rename_camera each produce two journal entries … D15
  observable contract holds"; header debt marker `src/scene/ace/scene/camera.hpp:100-106`,
  which points here by name).
- **`editor.project.app_state`** — the `commands::Command`/`dispatch` seam a rename is
  dispatched through (unchanged); `commands::Selection` (`src/commands/ace/commands/selection.hpp:17-43`)
  keys selection **by `arbc::ObjectId`** — the concrete consumer whose identity this task
  preserves.
- **libarbc `Editable` facet** — a fully-shipped seam in the pinned lib
  (`build/dev/_deps/arbc-src/src/contract/arbc/contract/content.hpp:462-476`;
  `Model::Transaction::set_content_state` at
  `.../src/model/arbc/model/model.hpp:489`; auto-bind + initial-capture in
  `Document::add_content` at `.../src/runtime/document.cpp:83-116`). Nothing new is
  fetched or forked.

**Pending (owned here):** the `Editable` implementation on `CameraContent` and the
in-place `rename_camera` body. No downstream leaf is blocked — `cameras.manip` and
`cells.selection` (the future consumers of stable camera identity) already exist as
scheduled leaves.

## What this task is

Today a camera **rename mints a new `ObjectId`.** `scene::rename_camera`
(`src/scene/camera.cpp:412-466`) locates the camera's binding layer and its order index,
then **replaces** the Content+Layer pair: `add_content` a freshly-named `CameraContent`,
then one transaction that `detach_layer` + `remove(old_layer)` + `remove(camera)` +
`add_layer(new_content)` + `attach_layer(…, index)` (`camera.cpp:456-465`). The old
camera's `ObjectId` is destroyed (`camera.cpp:461`). The self-documenting comment
(`camera.cpp:451-455`) explains the assumption that forced this: "a camera Content's state
is immutable and the leaf-kind param has no in-place transactional edit."

This task **overturns that assumption.** It gives `CameraContent` the `arbc::Editable`
facet — the libarbc seam for **mutable, undoable content state addressed by an opaque
`StateHandle`** — and rewrites `rename_camera` to **edit the name in place** via one
`Model::Transaction::set_content_state`, keeping the **same `ObjectId`, the same binding
layer, the same frame, and the same order.** A renamed camera is the *same object* with a
new name, so any consumer that remembers a camera by `ObjectId` (selection, and the future
manip/overview) keeps its handle across a rename.

## Why it needs to be done

D7 makes cells and cameras **one shape with one select tool** and **one shared selection**
across canvas/list/overview (`docs/00-design.md:205-223`). Selection is `ObjectId`-keyed
(`commands::Selection`, `selection.hpp:17-43`). A rename that changes the `ObjectId` silently
**drops the camera out of the selection** (and out of any manip gizmo bound to it) the
instant the user types a new name — the exact identity break the `.tji` note calls out.
`cameras.manip` (frame resize / resolution inspector) and `cells.selection` both `depend`
on the model and will address cameras by `ObjectId`; shipping them on top of an
identity-losing rename would bake a latent bug into the shared selection model. This 0.5d
task removes that hazard at its root and, as a bonus, collapses the rename from **two
journal entries to one** (a strictly cleaner undo unit, retiring the `model.md:391`
deviation for the rename path).

## Inputs / context

**Governing design docs (normative — the constitution):**

- **D7 — Manipulation model** (`docs/00-design.md:468`; §6 "Direct manipulation," prose
  `:205-223`). Cells and cameras are the *same* `ObjectId`-addressable placed-object shape
  under **one select tool**; selection is **shared** across canvas/list/overview
  (`:218`). The invariant this task protects: a camera's identity must survive an edit to
  its name so the shared selection does not lose it.
- **A14 — Cameras persist as an editor-defined `Content` kind** (`docs/01-architecture.md:264`).
  Frames cameras as one non-rendering `org.arbc.camera` `Content` + one `Layer`, mutated by
  `commands` transactions (D15). A14 says **nothing about the Content state being
  immutable** — using the `Editable` facet to make the name editable in place is an
  *elaboration within* A14, not an amendment (see Decision **D-rename-3**).
- **D15 — Undo boundary** (`docs/00-design.md:476`). A saved shot's data (name included) IS
  scene data and IS undoable. The in-place rename stays a `commands` transaction, so
  undo/redo continue to ride the journal — now restoring the **old name in place** rather
  than resurrecting a removed object.
- **§8 levelization** (`docs/01-architecture.md:162-175`) — `scene` is L1 and may depend on
  `base, project, libarbc`; ImGui/GL/SDL forbidden. **§9 DoD** (`:181-208`).

**libarbc API surface** (fetched under `build/dev/_deps/arbc-src/`):

- `arbc::Editable` — `capture() → StateHandle`, `restore(StateHandle)`,
  `state_cost(StateHandle) const`, `retain(StateHandle)`, `release(StateHandle)`
  (`src/contract/arbc/contract/content.hpp:462-476`). `Content::editable()` defaults to
  `nullptr` (`content.hpp:581`); a content opts in by inheriting `Editable` and returning
  `this`. Thread contract (`content.hpp:454-459`): `capture`/`restore`/`retain` on the
  **writer** thread, **`release` on the drain (housekeeping) thread** — the store must be
  mutex-guarded.
- `arbc::StateHandle { SlotIndex slot; has_state(); }` — an index-only slab handle,
  default `k_state_none` (`src/model/arbc/model/records.hpp:51-55`).
- `Model::Transaction::set_content_state(ObjectId content, StateHandle after)` — "assign a
  caller-captured, OPAQUE content-state handle to a content object (path-copies its record)
  and record the prior handle as the entry's *before*"; **no-op if `content` is absent or
  not a content object** (`src/model/arbc/model/model.hpp:489`). This is the in-place edit
  that preserves the `ObjectId`.
- `Document::add_content` already **binds** any editable content into the per-document
  `EditableBinding` and **captures its initial state** into the minted record, *in the same
  transaction* (`src/runtime/document.cpp:99-112`) — so a camera created by `add_camera`
  is editable-ready with **no additional wiring**. A document may hold any number of
  editable contents (`document.hpp:98-107`).
- `Document::resolve(ObjectId) const → Content*` — a **non-const** `Content*`
  (`src/runtime/arbc/runtime/document.hpp:262`); the writer can `dynamic_cast` it to
  `CameraContent*` and mutate it.
- `Document::editable_binding()` exposes the routing counters
  (`document.hpp:330`); `unrouted_state_calls()` is the "state call misrouted to the wrong
  owner" tripwire, **zero in any correct document**
  (`src/runtime/arbc/runtime/editable_binding.hpp:113-117, 271-274`).
- **Reference implementers of `Editable`** (the shape to copy, minus the pixels):
  - `arbc::FakeEditable` (`src/runtime/t/fake_editable.hpp:45-122`) — the *minimal*
    template: `editable()` returns `this` (`:59`), `capture()` returns the current base
    (`:62`), `restore(h)` adopts a handle (`:63-66`), `retain`/`release` bump a
    **mutex-guarded** refcount map (`:71-81, 120-121`), and `edit(txn, self)` mints a new
    slot, sets it base, and `txn.set_content_state(self, after)` (`:85-89`).
  - `arbc::RasterContent` (`src/kind_raster/arbc/kind_raster/raster_content.hpp:291-338`) —
    the real one; `paint(txn, self, …)` is the same discipline over a COW tile table
    (`:328, 330`).

**Editor seams this leaf extends:**

- `src/scene/ace/scene/camera.hpp` — `CameraContent` (`:35-57`), currently holding
  `std::string d_name` + `Resolution d_resolution` as **plain members** (`:54-56`) and
  **not** overriding `editable()`. `rename_camera` decl (`:107-108`) and its debt comment
  (`:100-106`). The `Camera` read struct (`:68-74`) — `id` (identity), `layer` (frame
  carrier) — unchanged by this task.
- `src/scene/camera.cpp` — `rename_camera` body to rewrite (`:412-466`); `add_camera`
  (`:383-410`), `cameras()` (`:353-381`), and the codec (`:291-325`) are **unchanged
  inputs**. The codec's `serialize` reads `camera_name()`/`resolution()`
  (`camera.cpp:294-300`), so those accessors must keep returning the **live** (current
  base) state after this change (see Constraint 4).
- `tests/camera_model_test.cpp` — the existing rename tests to extend: the happy-path
  rename (`:141-164`, which captures `first_id` at `:152` but **does not** assert it
  survives) and the unknown-id no-op (`:166-172`); the add/undo/redo test (`:216-240`) and
  the persistence roundtrip (`:365-402`) that must stay green.

**Predecessor refinement:** `tasks/refinements/cameras/model.md`.

## Constraints / requirements

1. **Levelization — no structural change.** All work is inside L1 `scene`.
   `CameraContent` already includes `arbc/contract/content.hpp` (`camera.hpp:6`), which
   declares `Editable`; `Model::Transaction`/`set_content_state` come from
   `arbc/model/model.hpp`, already reachable and used by `add_camera`. **No new component,
   no new DAG edge, no `check_levels` edit, no ImGui/GL/SDL include.**

2. **ObjectId (and layer) preserved across rename.** The renamed camera keeps its content
   `ObjectId`, its binding-layer `ObjectId`, its frame `Affine`, its resolution, and its
   position in layer order. No `add_content`, no `add_layer`, no `detach`/`remove`/`attach`
   in the rename path — the mutation is exactly one `set_content_state`.

3. **One transaction, undoable in place (D15).** `rename_camera` opens **one**
   `document.transact("rename_camera")`, calls `set_content_state(camera, after)`, and
   commits — **one** journal entry (down from two). Dispatched through the existing
   `commands::Command`/`dispatch`, so `undo` restores the **old name on the same object**
   and `redo` re-applies the new name (no bespoke undo). The unknown-id / non-camera path
   stays a **no-op returning `false`** (mirrors the current `:446-448` guard; also matches
   `set_content_state`'s own no-op-on-absent contract).

4. **`CameraContent` becomes editable; live state stays what serialize reads.** Give
   `CameraContent` the `arbc::Editable` facet backed by a **small versioned store** of
   `{name, resolution}` keyed by `StateHandle::slot` (a `std::vector`/`std::map` slot table
   + free list + a `d_base` handle — no tile machinery). `camera_name()`/`resolution()` and
   the codec's `serialize` must read the **current base** state, so a persisted or
   round-tripped camera always reflects the latest rename. `capture()` returns the base
   handle; `restore(h)` adopts a stored handle (the undo/redo path); `state_cost(h)` is the
   payload's byte size; `retain`/`release` manage slot lifetime. Mirror
   `FakeEditable`/`RasterContent`.

5. **Threading — mutex-guarded state store (A4/A5).** `release` arrives on the **drain
   thread** while the writer runs `capture`/`restore`/`retain`; guard the slot store with a
   `std::mutex` exactly as `FakeEditable` does (`fake_editable.hpp:120`). The camera stays
   **non-rendering** (empty `bounds()` culls it), so the render thread never reads camera
   state — the only cross-thread contact is the retain/release refcount, which the mutex
   covers.

6. **`unrouted_state_calls()` stays zero.** After any add→rename→undo→redo cycle, the
   document's `EditableBinding` must report **zero** misrouted state calls — the correctness
   tripwire that the camera's handles route to the camera and only the camera.

7. **Non-rendering & persistence invariants unchanged.** A camera still contributes zero
   pixels; `save_project → load_document` still round-trips name/resolution/frame/order,
   now including a post-rename name.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`); `scripts/gate` green
(check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — primary structural assertion.** No new component,
  no new DAG edge, no lint edit; `scene` gains no ImGui/GL/SDL include (Constraint 1).
- **L1 logic — Catch2 unit** (extend `tests/camera_model_test.cpp`, in `ace_tests`):
  - **ObjectId stability (the headline).** In the existing rename test (`:141-164`), add
    `CHECK(cams[0].id == first_id)` — the assertion that **fails against today's code and
    passes after**. Also assert the **binding-layer id** is unchanged (`cams[0].layer`
    captured before == after) and the second camera is untouched.
  - **Selection survives rename (the D7 payoff).** Build a `commands::Selection`,
    `select(first_id)`, dispatch the rename, and `CHECK(selection.contains(first_id))` — the
    concrete break this task fixes.
  - **One journal entry, in-place undo/redo.** The dispatched rename advances the journal by
    **exactly one** undo unit; `undo` restores the **old name** with the camera **still
    present at the same `ObjectId`** (not removed), `redo` re-applies the new name. (Kept
    distinct from the add/undo test at `:216-240`, which asserts add-undo *removes* the
    camera — that behavior is unchanged.)
  - **Editable wiring + tripwire.** After `add_camera`,
    `document.editable_binding().bound(camera_id)` is `true`; after an
    add→rename→undo→redo cycle, `document.editable_binding().unrouted_state_calls() == 0`.
  - **Preserved fields + no-op.** Keep the existing resolution/frame/order asserts; keep
    `rename_camera(..., ObjectId{999}, …) == false` (`:166-172`), and add a
    rename-a-non-camera-content id → `false`.
  - **Persistence after rename.** Extend the roundtrip (`:365-402`): rename a camera, then
    `save_project → load_document`, and assert the reloaded camera reports the **new** name
    (same resolution/frame/order).
- **Rendered output — golden N/A (justified).** Cameras are non-rendering; the non-rendering
  invariant is already pinned by the model task's byte-equality unit and is unaffected by an
  in-place name edit. No new golden.
- **UI e2e — ImGui Test Engine N/A (justified).** No widget ships here — there is **no
  Cameras view** (`model.md:291-298`); the rename affordance (an editable label) lands with
  the first consuming UI leaf, **`editor.panels.layers`** (`.tji:286-291`), which carries the
  e2e budget. No e2e is deferred as a new task — the surface does not exist until that leaf.
- **Threading (ASan/TSan).** No new concurrency seam. The camera's state store is
  mutex-guarded (Constraint 5); ASan/TSan run over add→rename→undo→redo→save→load through
  the existing lanes and assert `unrouted_state_calls() == 0`. The real-concurrency
  render/edit coverage stays owned by `frame_sync`/`multi_canvas`.
- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) — the units above ship with
  the task; clang-format + build clean.

## Decisions

- **D-rename-1 — Rename in place via `Editable` + `set_content_state`, not
  detach/remove/re-add.** The current replace path (`camera.cpp:456-465`) is the *only*
  reason a rename changes the `ObjectId`; libarbc offers a first-class in-place editor
  (`set_content_state`, `model.hpp:489`) that path-copies the content record and journals
  the prior handle as the undo `before`, preserving the object. *Rationale:* it is the
  seam's designed use (the reference implementers are `RasterContent` and `FakeEditable`),
  it fixes the identity break D7's shared selection depends on, and it makes the rename a
  single, cleaner journal entry. *Alternative rejected:* keep the replace path and instead
  make downstream consumers (`cells.selection`, `cameras.manip`) *re-key* their remembered
  `ObjectId` on every rename — pushes fragile identity-tracking into every consumer and
  contradicts D7's "one shape, stable object"; the library already solves this at the model
  layer. *Alternative rejected:* store the name outside the Content (a side map keyed by
  layer id) so the Content never changes — the name would then not travel in the
  `project.arbc` snapshot (A14/D16 require it to) and would desync from `serialize`.

- **D-rename-2 — `CameraContent` carries a small versioned `{name, resolution}` store; no
  host wiring.** The state is a string + two ints, so the `Editable` store is a trivial slot
  table + free list + `d_base` handle guarded by one mutex — the `FakeEditable` shape
  (`fake_editable.hpp:45-122`), not raster's COW tile machinery. Because `Document::add_content`
  auto-binds editable content and captures its initial state (`document.cpp:99-112`), a
  camera minted by `add_camera` is editable-ready with **zero** changes to `add_camera` or
  any host code. *Rationale:* smallest change that satisfies the facet contract; reuses the
  exact seam the runtime already drives for raster. *Alternative rejected:* a bespoke
  editor-side rename journal — reinvents the library's transactional undo the whole editor
  is built on (also the model task's D-model-4). *Note — scope:* the store carries
  **resolution** too (it is part of the state), so **`cameras.manip`'s in-place resolution
  edit reuses this same facet** (call `set_content_state` with a new-resolution handle) — no
  new task; this leaf edits **name only** and leaves resolution unchanged.

- **D-rename-3 — No doc delta.** The `Editable` facet is an existing libarbc seam, and A14
  already models a camera as a `Content` without asserting its state is immutable — the
  "immutable" claim lived only in the model *refinement* and a code comment
  (`camera.hpp:100-106`), which explicitly named *this* task as the fix. Using the facet
  adds **no component, no DAG edge, no external dependency, and no deviation from a `D`/`A`
  row** — it *removes* the `model.md:391` deviation for the rename path. So this is an
  elaboration within A14, not an amendment; no `A<n>`/`D<n>` row changes. *Alternative
  considered:* sharpen A14 to state cameras are editable content — deferred as unnecessary
  noise; A14's "Content kind + commands transactions" framing already admits it.

- **D-rename-4 — `add_camera`'s two-entry create is out of scope.** `add_camera` spans two
  journal entries because placing a *new* object is inherently `add_content` (bind the
  vtable) + `add_layer`/`attach_layer` (place it) — unrelated to the state-handle mechanism
  and **not** fixable by the `Editable` facet. *Rationale:* this task is scoped to *rename*;
  the create-entry count is the model task's recorded, D15-sound behavior
  (`model.md:391`, `camera.cpp:398-408`) and changing it is neither required nor enabled
  here. No follow-up task is spawned for it — the observable D15 contract already holds.

## Open questions

(none — all decided.) The one mechanism question — whether the pinned `arbc` admits an
editor-authored editable content — is answered by the fetched headers: `Editable` is a
public contract facet, `set_content_state` a public transaction method, and
`add_content` already auto-binds arbitrary editable kinds (`document.cpp:99-112`). No
human-judgment item surfaces for `tasks/parking-lot.md`; no new WBS leaf is spawned (the
rename affordance's UI e2e rides `editor.panels.layers`, and resolution-in-place rides
`editor.cameras.manip` — both already scheduled).

## Status

**Done** — 2026-07-18.

- `CameraContent` now implements the `arbc::Editable` facet — a small mutex-guarded versioned `{name, resolution}` slot store with `capture/restore/state_cost/retain/release` + a `set_name` editor (`src/scene/ace/scene/camera.hpp`).
- `rename_camera` rewritten to a single in-place `set_content_state` transaction (resolve → dynamic_cast → edit), preserving the camera's `ObjectId`, binding layer, frame, resolution, and order; unknown-id / non-camera → `false` no-op (`src/scene/camera.cpp`).
- Accessors (`camera_name()`, `resolution()`) and the codec now read the live base version, so persisted/round-tripped cameras always reflect the latest rename.
- Extended the rename test (ObjectId + binding-layer stability, sibling untouched); new cases: selection-survives-rename, one-journal-entry in-place undo/redo, editable-binding `bound()` + `unrouted_state_calls()==0` tripwire, rename-a-non-camera → false; persistence roundtrip extended to rename-then-save/load asserting the post-rename name (`tests/camera_model_test.cpp`).
- Coverage gap remediated: additional Catch2 cases drive the facet's defensive guard branches (no-state/out-of-range/underflow handles + base-invalidated accessors) and the free-list-reuse path via rename→undo→rename→rename (`tests/camera_model_test.cpp`).
- 155 test cases pass (including 6 new/extended camera cases); full CI matrix (gcc/clang × debug/release/asan/tsan) + diff-coverage gate (≥90%) green.
