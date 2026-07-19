# editor.cameras.reopen_codec — Thread camera-kind registration into open_project (no PlaceholderContent on reopen)

## TaskJuggler entry

- **Task:** `editor.cameras.reopen_codec` (`tasks/00-editor.tji:225-230`, under
  `task cameras "Cameras"` at `:210`).
- **Effort:** `1d` · `allocate team`.
- **Depends:** `!model` — the sibling `editor.cameras.model` (`:216`), already
  `complete 100` (`:214`).
- **Note (`.tji:229`):** "scene::register_camera_kind is not threaded into
  project::open_project's rebuild-from-canonical registry; a project reopened from
  project.arbc degrades cameras to PlaceholderContent. Wire via a registration
  callback so open_project restores camera kinds with no project->scene edge.
  Source-of-debt: tasks/refinements/cameras/model.md. Design:
  docs/01-architecture.md A14."
- **Back-link:** the closer appends `Refinement:
  tasks/refinements/cameras/reopen_codec.md` to the `.tji` note and adds
  `complete 100` after `allocate team` (`tasks/refinements/README.md:47-68`).
  **Do not** hand-edit the `.tji` here.

## Effort estimate

**1 day.** A pure L1 change split across two components with no new file: give
`project::open_project` (and its `rebuild_from_canonical` helper) an **extra-kinds
registration callback** parameter (a `std::function<void(arbc::Registry&)>`,
`#include <functional>` only — no new dependency), applied to the transient load
registry right after `arbc::register_builtin_kinds`; then have the `commands` open
path supply `scene::register_camera_kind` as that callback (the one component that
already links both `project` and `scene`). The coverage is a handful of Catch2
assertions on the existing `tests/project_open_test.cpp` / `tests/camera_model_test.cpp`
in `ace_tests`. No new component, no new DAG edge, no ImGui, no golden.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cameras.model`** (`tasks/refinements/cameras/model.md`, **Done**
  `model.md:381-391`) — shipped `scene::CameraContent` (`org.arbc.camera`,
  `src/scene/ace/scene/camera.hpp:83-84`), `scene::register_camera_kind`
  (decl `camera.hpp:108`, def `src/scene/camera.cpp:390-424`), the codec, and the
  `project` **save-side** roundtrip (`project::seed_kind_bridge`,
  `src/project/project.cpp:19-25`; `arbc::builtin_codecs(registry)`,
  `src/project/save.cpp:116-117`). This task **rewrites nothing** the model built —
  it threads the *already-shipped* `register_camera_kind` into the one path the model
  task left uncovered: the **load** side. The model refinement anticipated exactly
  this fix in its Constraint 1 (`model.md:207-210`): *"thread registration through a
  `Registry`/codec-table callback — never add a `project→scene` edge."*
- **`commands::AppState`** — already the wiring point for camera-kind registration on
  the **persistent** save registry (`register_builtin_kinds` + `register_camera_kind`,
  `src/commands/app_state.cpp:18-26`), and already the caller of `open_project`
  (`src/commands/app_state.cpp:70-74`). `commands` is the lowest component that legally
  links both `project` and `scene` (`CMakeLists.txt:184,186`), so it is the correct
  origin for the load-side callback.
- **libarbc `Registry` / `load_document` seam** — a fully-shipped seam in the pinned
  lib. `arbc::load_document` reconstructs each `Content` through the registry's
  `CodecTable`; a `find`-miss degrades the record to `arbc::PlaceholderContent`
  (`build/dev/_deps/arbc-src/src/serialize/codec.cpp:106,138-161`;
  `.../src/serialize/arbc/serialize/placeholder_content.hpp:37`). Nothing new is
  fetched or forked — the fix uses the codec table exactly as the save path already does.

**Pending (owned here):** the `open_project` callback parameter, its application inside
`rebuild_from_canonical`, and the `commands` call site that passes
`scene::register_camera_kind`. No downstream leaf is blocked — `cameras.manip`,
`cells.selection`, and `panels.overview` (the future consumers of a typed, live
`CameraContent` after reopen) already exist as scheduled leaves.

## What this task is

Today a camera **survives a save but degrades on cold reopen.**
`scene::register_camera_kind` (`camera.cpp:390-424`) registers the `org.arbc.camera`
codec + factory on an `arbc::Registry`, and `commands::AppState`'s constructor calls it
on the **persistent** `registry_` used for **save** (`app_state.cpp:26`). But reopen runs
through a *different, transient* registry: `project::open_project`
(`src/project/project_open.cpp:114-165`) delegates to `rebuild_from_canonical`
(`:72-94`) when the crash-durable workspace file is absent or stale (the normal case
after a fresh clone or a discarded workspace — and, per **A7**, every reopen is a fresh
`exec`). That helper builds its **own** registry seeded with **built-ins only**:

```
81  arbc::Registry registry;
82  arbc::register_builtin_kinds(registry);      // <- org.arbc.camera never registered here
83  arbc::KindBridge bridge;
...
85  const auto loaded = arbc::load_document(canonical_bytes, *document, bridge, registry, ...);
```

So `load_document` finds no `org.arbc.camera` codec, hits the `CodecTable::find`-miss
path, and reconstructs each persisted camera as a **non-typed `PlaceholderContent`**
(`codec.cpp:138-161`). The `register_camera_kind` call in the `AppState` constructor
(`app_state.cpp:26`) runs **too late** to help — `open_project` has already rebuilt the
`Document` (`app_state.cpp:70-74`) before `AppState` exists.

This task **threads the registration into the load path.** `open_project` /
`rebuild_from_canonical` gain an **extra-kinds registration callback**; `commands`
supplies `scene::register_camera_kind` as that callback, so the transient load registry
recognizes `org.arbc.camera` and reconstructs each persisted camera as a **live
`scene::CameraContent`** — typed, `ObjectId`-addressable, editable, and returned by
`scene::cameras(const Document&)`. A reopened project is therefore identical to the
in-session document: cameras are cameras, not placeholders.

## Why it needs to be done

**A14** promises cameras "persist in the document (`project.arbc`) as scene objects"
(`docs/01-architecture.md:264`, citing `docs/00-design.md:519`). The save side honors
that; the load side silently breaks it. A `PlaceholderContent` reopen is *observably*
transform-preserving and non-rendering (the intended fallback, `model.md:332-342`), so a
casual roundtrip looks fine — but the camera has **lost its kind identity**:
`scene::cameras()` no longer returns it, so it can't be renamed (`rename_camera` resolves
and `dynamic_cast`s to `CameraContent`, `camera.cpp` — a placeholder fails the cast),
can't be manipulated (`cameras.manip`), can't be looked-through (`cameras.look_through`),
and can't be exported (`cameras.export`, "a camera IS the export spec"). Every downstream
camera consumer that `depends !model` would work in-session and then break after the user
closes and reopens the project — the worst kind of latent bug, invisible until the second
launch. This 1d task closes the save/load asymmetry at its root so a persisted camera is a
first-class camera on every open, and it does so via the exact callback-threading seam A14
and the model refinement's Constraint 1 already mandate — **no `project→scene` edge**.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **A14 — Cameras persist as an editor-defined `Content` kind**
  (`docs/01-architecture.md:264`). The camera `Content` + codec + read accessor
  (`scene::cameras(const Document&)`) live in **L1 `scene`**; **"kind registration is
  wired at a level that already sees `scene` (`commands`/`app`), so `project`'s generic
  snapshot save serializes cameras with no new `project→scene` edge, no new component, no
  new DAG edge, no new external dependency."** A14 states this for *save*; this task
  extends the **same principle to load** — the callback is the load-side realization of
  "wired at a level that already sees `scene`" (see Decision **D-reopen-4**).
- **A13 — the canonical-dump / rebuild-from-canonical seam** (`:263`). "The canonical dump
  itself is L1 `project::save_project`" over the `Registry` codec table, and the **dirty
  baseline is set at a `rebuilt_from_canonical` open** — i.e. the same load path this task
  fixes runs on every cold open, making the degradation reproducible from any fresh clone.
- **A7 — process-per-project** (`:257`). "Opening a project is a new `exec`," so a reopen
  always runs `open_project` in a fresh process against `project.arbc` — the transient
  rebuild registry is rebuilt every launch, so the bug is not a one-off cache miss but the
  default reopen behavior once the workspace file is gone.
- **A8 — levelization enforces the testability seam** (`:258`); **§8 levelization table**
  (`:162-175`) — `project` (L1) may depend on `base, platform, libarbc` — **not** `scene`;
  `scene` depends on `project`; `commands` depends on **both**. **§9 DoD** (`:181-208`).

**libarbc API surface** (fetched under `build/dev/_deps/arbc-src/`):

- `arbc::Registry` — the codec/factory table `load_document` consults. `register_camera_kind`
  calls `registry.add(...)` with a `KindCodec` + `ContentFactory` + `KindMetadata`
  (`src/scene/camera.cpp:391-423`); registration is **first-wins / idempotent** (a duplicate
  id is not an error, `camera.cpp:420-423`).
- `arbc::load_document(bytes, doc, bridge, registry, path, assets)` — reconstructs content
  through `registry`; the `CodecTable::find`-miss branch builds `PlaceholderContent`
  (`serialize/codec.cpp:106,138-161`). `register_builtin_kinds(registry)` pre-seeds only the
  library's leaf kinds.
- `arbc::PlaceholderContent` (`serialize/arbc/serialize/placeholder_content.hpp:37`) — the
  degraded, non-typed fallback the fix prevents on reopen.

**Editor seams this leaf extends:**

- `src/project/ace/project/project.hpp:137-138` — `open_project` declaration (gains the
  callback parameter). `project.hpp:96` — `seed_kind_bridge`, the existing precedent for
  bridging editor kinds into a `project` codec path without a `scene` edge.
- `src/project/project_open.cpp` — `open_project` body (`:114-165`), the workspace-map fast
  path (`:126-137`, no codecs — see Constraint 3), and `rebuild_from_canonical` (`:72-94`),
  whose transient registry (`:81-83`) is where the callback must be applied, **after** the
  built-in seeding (`:82`) and **before** `load_document` (`:85`). `create_project`
  (`:167-202`) mints an empty workspace and runs no load-side codec — **out of scope**.
- `src/commands/app_state.cpp` — the persistent-registry seeding
  (`register_builtin_kinds` + `register_camera_kind`, `:18-26`) and the `open_project`
  call site (`:70-74`) that must pass the callback. `state.registry()` (`:88`) is the
  save-side registry, unchanged.
- `src/scene/ace/scene/camera.hpp:108` / `src/scene/camera.cpp:390-424` —
  `register_camera_kind` itself, **unchanged**: this task only routes it to a second call
  site, it does not touch the function.
- Tests: `tests/project_open_test.cpp` (the rebuild-from-canonical fidelity cases,
  `:178,197,306`; `SECTION` idiom `:274,286`), `tests/camera_model_test.cpp` (the save→load
  roundtrip `:538-583` and the `camera_registry()` load-side helper `:567`),
  `tests/project_gc_test.cpp:242,262` (reopen roundtrip), and
  `tests/project_save_test.cpp:269` (clean-at-construction after a rebuild) — all must stay
  green, with new cases added for the reopen-as-live-camera guarantee.

**Predecessor refinement:** `tasks/refinements/cameras/model.md`.

## Constraints / requirements

1. **Levelization — no new `project→scene` edge, no new component.** The callback's type
   references only `arbc::Registry` (already a `project` dependency via libarbc) and
   `<functional>` — **not** any `scene` symbol. `project` stays ignorant of what the
   callback registers; the concrete `scene::register_camera_kind` is named only in
   `commands` (which already links `scene`, `CMakeLists.txt:186`). No new DAG edge, no
   `check_levels` edit, no ImGui/GL/SDL include. `grep scene src/project/` must remain empty.

2. **Reopen yields a live `CameraContent`, not `PlaceholderContent`.** After
   `save_project` then a cold `open_project` that takes the **rebuild-from-canonical** path
   (workspace absent), `scene::cameras(reopened)` returns the persisted cameras as typed
   `scene::CameraContent` with their `ObjectId`, name, resolution, frame `Affine`, and layer
   order intact — byte-identical to the pre-save document. No record decodes to
   `PlaceholderContent`.

3. **Scope is the rebuild-from-canonical registry; the fast path is pinned, not changed.**
   The workspace-map fast path (`project_open.cpp:126-137`) maps a live checkpointed
   `Document` via `arbc::Document::open` and runs **no** codecs, so cameras already survive it
   — it needs no callback. The callback is threaded into `rebuild_from_canonical` **only**.
   A regression-pinning test (Constraint-2's assertion, run through the fast path with a
   present workspace) documents that both cold-open routes yield live cameras, so a future
   change to the map path cannot silently reintroduce the degradation.

4. **The extra-kinds callback is the single editor-kind registrar, reused on both sides.**
   To keep "which editor kinds exist" in one place, `commands` supplies the **same**
   registration routine to both its persistent save registry (`app_state.cpp:26`) and the
   `open_project` callback — today that is exactly `scene::register_camera_kind`. When a
   second editor kind ships, adding it in that one routine restores it on reopen *for free*.
   The callback is applied **idempotently** (safe if the load registry ever already carries
   the kind, matching `register_camera_kind`'s first-wins contract, `camera.cpp:420-423`).

5. **`create_project` and the save path are untouched.** A new project mints an empty
   workspace with no cameras and runs no load-side codec (`project_open.cpp:167-202`), so it
   takes no callback. The save path (`save.cpp:116-117`, `seed_kind_bridge`) already
   serializes cameras and is not modified — this task is load-side only.

6. **Optional-callback shape keeps existing callers compiling.** The parameter defaults to
   an empty/absent callback (a null `std::function`, skipped when unset), so any current
   `open_project` caller (tests, `save_as`, gc) that does not need editor kinds is unaffected
   and no unrelated call site churns.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`); `scripts/gate` green
(check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — primary structural assertion.** No new component,
  no new DAG edge, no lint edit; `project` gains no `scene` include (`grep scene src/project/`
  empty) and no ImGui/GL/SDL include; the only new `project` include is `<functional>`
  (Constraint 1).
- **L1 logic — Catch2 unit** (extend `tests/camera_model_test.cpp` and/or
  `tests/project_open_test.cpp`, in `ace_tests`):
  - **Reopen restores a live camera (the headline).** Author a project with a camera, save
    it, discard/omit the workspace file, and `open_project` through the **rebuild-from-canonical**
    path **with** the camera-kind callback; assert `scene::cameras(reopened).size()` matches
    and the reopened content `dynamic_cast`s to `scene::CameraContent` (not
    `PlaceholderContent`) — the assertion that **fails against today's code and passes after**.
    Assert name, resolution, frame `Affine`, `ObjectId`, and layer order round-trip.
  - **Reopen without the callback still degrades (the guard's contract).** The same reopen
    with an **absent** callback still produces a non-typed placeholder (no `scene::CameraContent`)
    — pins that the callback, not some incidental change, is what restores the kind, and that
    the default-empty parameter (Constraint 6) is a true no-op.
  - **Fast-path parity (Constraint 3).** With the workspace file **present**, reopen takes the
    map path and `scene::cameras(reopened)` returns live cameras too — regression-pins that
    both cold-open routes yield typed cameras.
  - **Reopened camera is fully operable.** After a rebuild-path reopen, `rename_camera` on a
    reopened camera succeeds (resolves + `dynamic_cast`s), preserving its `ObjectId` — proving
    the reopened object is the real editable kind, not a placeholder, and that
    `cameras.manip` / `cells.selection` will address it correctly post-reopen.
  - **Idempotence + no-camera project.** Applying the callback to a registry that already
    carries the kind is a no-op (Constraint 4); a project with **no** camera reopens cleanly
    through the callback path (the callback runs harmlessly, zero cameras).
- **Rendered output — golden N/A (justified).** Cameras are non-rendering (zero pixels); a
  reopened camera contributes nothing to composition just as an in-session one does. The
  non-rendering invariant is already pinned by the model task's byte-equality unit and is
  unaffected by *which* code path materialized the content. No new golden.
- **UI e2e — ImGui Test Engine N/A (justified).** No widget ships here — there is **no
  Cameras view** (`model.md:291-298`); this leaf is a headless L1 open-path fix. The reopen
  behavior surfaces to the user through the first consuming camera UI (`editor.panels.layers`,
  `.tji:286-291`, and `editor.panels.overview`, `.tji:293-297`), which carry the e2e budget.
  No e2e is deferred as a new task — the surface does not exist until those leaves.
- **Threading (ASan/TSan).** No new concurrency seam — the callback runs synchronously on the
  open (writer) thread inside `rebuild_from_canonical`, before the document is handed to any
  renderer. The reopen unit runs under the existing ASan/TSan lanes; the pre-existing
  render/edit race is separately owned by `editor.canvas.edit_render_sync` (`.tji:195-199`)
  and is untouched here.
- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) — the units above ship with
  the task; clang-format + build clean.

## Decisions

- **D-reopen-1 — Thread an extra-kinds registration callback into `open_project`, supplied
  by `commands`; never a `project→scene` edge.** `project` (L1) may not depend on `scene`
  (§8, `:167-168`), yet the load path must register a `scene`-owned codec. A
  `std::function<void(arbc::Registry&)>` parameter on `open_project` (applied inside
  `rebuild_from_canonical` after `register_builtin_kinds`, `project_open.cpp:82`) inverts the
  dependency: `project` calls an abstract hook typed only on `arbc::Registry`; the concrete
  `scene::register_camera_kind` is named by the caller in `commands` (which already links
  both, `CMakeLists.txt:186` / `app_state.cpp:26`). *Rationale:* it is the seam A14 and the
  model refinement's Constraint 1 (`model.md:207-210`) explicitly prescribed for the save side
  — this is the identical move on the load side; it adds no component, no DAG edge, no
  external dependency. *Alternative rejected:* add a direct `project→scene` include so
  `open_project` calls `scene::register_camera_kind` — a forbidden DAG edge that
  `check_levels` fails and that A8/A14 rule out. *Alternative rejected:* move the camera
  codec down into `project`/`base` so `open_project` sees it natively — inverts A14's home
  assignment (the camera kind lives in `scene`) and drags scene concepts into the generic
  project layer. *Alternative rejected:* register on a process-global/static registry
  consulted by `load_document` — hidden global state, breaks the "transient to this one load"
  discipline (D-open-7, `project_open.cpp:70-71`) and the multi-open/testability story.

- **D-reopen-2 — One `commands`-level editor-kind registrar reused by both the save registry
  and the reopen callback.** Rather than name `scene::register_camera_kind` at two unrelated
  call sites, `commands` keeps a single routine (the existing `app_state.cpp:26` seeding,
  extracted or referenced) that both the persistent save registry and the `open_project`
  callback invoke — one list of "editor kinds." *Rationale:* DRY and future-proof — when a
  second custom kind ships (a plugin, an annotation), adding it once restores it on reopen
  automatically, so the save/load asymmetry this task fixes cannot silently re-open for the
  next kind. *Alternative rejected:* an inline lambda naming `scene::register_camera_kind`
  only at the `open_project` call site — works today, but drifts from the save-registry list
  the moment a second kind is added, reintroducing exactly this class of bug.

- **D-reopen-3 — Scope is the rebuild-from-canonical registry only; the fast path is pinned,
  not modified.** The workspace-map fast path (`project_open.cpp:126-137`) maps a live
  checkpointed `Document` and runs no codecs, so cameras already survive it; the `.tji` note
  names precisely "rebuild-from-canonical registry" as the gap. *Rationale:* fix the one path
  that reconstructs from serialized bytes; don't perturb a path that already works. *Guard:*
  a fast-path parity test (Acceptance) pins that a workspace-present reopen also yields live
  cameras, so a future refactor of the map path can't silently regress. *Alternative rejected:*
  also thread the callback into the map path defensively — no codec runs there, so the
  callback would be dead code; the pinning test is the right amount of insurance.

- **D-reopen-4 — No doc delta.** A14 already states kind registration is "wired at a level
  that already sees `scene` (`commands`/`app`)" with "no new `project→scene` edge," and the
  model refinement's Constraint 1 pre-authorized threading registration "through a
  `Registry`/codec-table callback." This task is the **load-side realization** of that exact
  decision — an elaboration within A14, not an amendment; it adds no component, no DAG edge,
  no external dependency, and deviates from no `D`/`A` row. So no `A<n>`/`D<n>` row changes.
  *Alternative considered:* sharpen A14 to state that reopen restores editor kinds via the
  same callback — deferred as unnecessary; A14's "wired at a level that sees `scene`" framing
  already covers both save and load, and the specificity belongs in this refinement.

## Open questions

(none — all decided.) The one mechanism question — whether the pinned `arbc`'s
`open_project`/`load_document` reconstruction can be made to recognize an editor-authored
kind — is answered by the fetched source: `load_document` consults the passed `arbc::Registry`
and only degrades to `PlaceholderContent` on a `find`-miss (`serialize/codec.cpp:106,138-161`),
and `register_camera_kind` already produces a registry entry the save path proves works
(`camera_model_test.cpp:538-583`); registering the same kind on the transient load registry is
the whole fix. No human-judgment item surfaces for `tasks/parking-lot.md`; no new WBS leaf is
spawned — the reopen behavior's UI e2e rides `editor.panels.layers`/`editor.panels.overview`
(already scheduled), and the render/edit thread-safety of the reopened document is already
owned by `editor.canvas.edit_render_sync` (`.tji:195-199`).

## Status

**Done** — 2026-07-19.

- `src/project/ace/project/project.hpp` — `open_project` gains an optional `const std::function<void(arbc::Registry&)>&` extra-kinds callback (default empty); `#include <functional>` added; no new `project→scene` edge.
- `src/project/project_open.cpp` — callback threaded through `rebuild_from_canonical`, applied to the transient load registry after `register_builtin_kinds` and before `load_document`; forwarded from `open_project`.
- `src/commands/app_state.cpp` — new file-local `register_editor_kinds()` (D-reopen-2) reused by both the persistent save-registry seeding and the `open_project` load callback in `open_or_create_app_state`.
- `tests/camera_model_test.cpp` — `persist_and_shed_workspace` helper + 4 Catch2 reopen units: restores live `CameraContent` with callback; degrades to placeholder without callback; reopened camera is fully operable (rename preserves `ObjectId`); no-camera project reopens cleanly.
- Fast-path parity test (Constraint-3) deferred: `arbc::Document::open` asserts on any `Content` with a non-inert `StateHandle` at `model.cpp:771`, so cameras cannot survive the workspace-map path with the pinned libarbc; a `NOTE` block documents this in-place. Tech-debt filed as `editor.cameras.workspace_reopen_slab`.
- All 1419 Catch2 assertions pass; `check_levels` clean; no `scene` include in `project`; diff coverage gate met.
