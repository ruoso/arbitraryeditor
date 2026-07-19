# editor.cameras.workspace_reopen_slab — Fast-path reopen slab hook for editor-defined editable kinds

## TaskJuggler entry

- **Task:** `editor.cameras.workspace_reopen_slab` (`tasks/00-editor.tji:232-236`, under
  `task cameras "Cameras"` at `:210`).
- **Effort:** `2d` · `allocate team`.
- **Depends:** `!reopen_codec` — the sibling `editor.cameras.reopen_codec`
  (`:225-230`), already `complete 100` (`:228`).
- **Note (`.tji:236`):** "A checkpointed workspace containing an editor-defined editable
  Content (camera) aborts/corrupts on the Document::open fast-path remap (missing libarbc
  per-kind state-slab walk hook, model.cpp:771); a saved camera project crashes on
  fast-path reopen. Needs a libarbc bump exposing the slab hook, or an editor-side policy
  to force rebuild-from-canonical when custom editable kinds may be present.
  Source-of-debt: tasks/refinements/cameras/reopen_codec.md. Design:
  docs/01-architecture.md A14."
- **Back-link:** the closer appends `Refinement:
  tasks/refinements/cameras/workspace_reopen_slab.md` to the `.tji` note and adds
  `complete 100` after `allocate team` (`tasks/refinements/README.md:47-68`).
  **Do not** hand-edit the `.tji` here.

## Effort estimate

**2 days.** A pure L1 change confined to `project::open_project`, plus one **doc delta**
(new `A15`). The code is a single guarded branch in `project_open.cpp` — the workspace-map
fast path becomes conditional on *not* (editor kinds may be present **and** a canonical
baseline exists) — forwarding to the already-shipped `rebuild_from_canonical` (which
`reopen_codec` made restore live cameras). No new file, no new component, no new DAG edge,
no libarbc fork, no `commands`/`scene` change. The bulk of the 2d is the **test matrix**:
this task lands the fast-path-present-with-a-camera case that `reopen_codec` explicitly
**deferred** here (`reopen_codec.md:344`, `camera_model_test.cpp:679-693`) — that case could
not be written before because it *aborts* against the pinned lib; the fix makes it a live,
passing Catch2 case. The doc delta (A15) records the open-path policy deviation from D-open's
durable-by-default fast path and the libarbc v0.1.0 limitation that forces it.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cameras.reopen_codec`** (`tasks/refinements/cameras/reopen_codec.md`, **Done**
  `reopen_codec.md:336-345`) — threaded an **extra-kinds registration callback**
  (`const std::function<void(arbc::Registry&)>&`, default empty) into `open_project` and
  `rebuild_from_canonical` (`src/project/project_open.cpp:126-128`, `:77-106`), and had
  `commands` supply a file-local `register_editor_kinds()` (the single editor-kind
  registrar, D-reopen-2) as that callback. **This task rewrites none of that.** It reuses two
  of its products verbatim: (1) the callback parameter is the exact **signal** this task
  branches on — a non-empty `register_extra_kinds` means "this session may hold
  editor-defined editable Content"; (2) the `rebuild_from_canonical` path it wired up is the
  path this task **routes camera-bearing workspaces to**, which `reopen_codec` already proved
  restores live typed `CameraContent` (`camera_model_test.cpp:609-649`).
- **`reopen_codec`'s deferred fast-path-parity case is *this* task's headline.**
  `reopen_codec.md:344` and the in-place `NOTE` at `camera_model_test.cpp:679-693` record
  that the workspace-map fast path *cannot* be exercised with a camera against the pinned lib
  — `arbc::Document::open` → `Model::rebuild_counts` **asserts** on the first `ContentRecord`
  with a non-inert `StateHandle` (`model.cpp:771`). `reopen_codec`'s **D-reopen-3** assumed
  "cameras already survive the map path"; the pinned lib **falsifies** that assumption. This
  task **supersedes D-reopen-3's premise**: rather than rely on the map path handling cameras
  (it can't), it **avoids** the map path for camera-bearing sessions.
- **libarbc `Document::open` / `Model::rebuild_counts`** — a fully-shipped, *unchangeable*
  seam in the pinned lib (`v0.1.0`, `CMakeLists.txt:23,25`). `Document::open`
  (`build/dev/_deps/arbc-src/src/runtime/document.cpp:60-66`) delegates to `Model::open`
  (`src/model/model.cpp:618-680`), which at `:667` calls `rebuild_counts`; that walk asserts
  every recovered `ContentRecord` carries an **inert** `StateHandle`
  (`model.cpp:760-772`, assert at `:771`: *"a persisted non-inert StateHandle needs a
  per-kind state-slab walk hook"*). The library reserves the hook location but **does not
  expose it** in `v0.1.0` (`Registry::add` accepts only factory/metadata/codec/binder —
  `contract/registry.hpp:125-128` — none an open-time state walk).

**Pending (owned here):** the single guarded branch in `open_project` that skips the
workspace-map fast path when editor kinds may be present and a canonical exists; the doc
delta (A15); and the Catch2 test matrix (headline crash-avoidance case + policy-gate +
canonical-absent fallback). No downstream leaf is blocked — `cameras.manip`,
`cells.selection`, and the panels leaves consume a live `CameraContent` regardless of which
open route materialized it, and all are already scheduled.

## What this task is

Today a saved camera project **survives a cold rebuild-from-canonical reopen** (fixed by
`reopen_codec`) **but crashes on the workspace-map fast-path reopen.** `open_project`
(`src/project/project_open.cpp:126-178`) prefers the **fast path** whenever the crash-durable
workspace file exists (`:139` `fs.exists(layout.workspace_file)`): it maps the live
checkpoint via `arbc::Document::open` (`:140`) and returns with `rebuilt_from_canonical =
false` (`:145`). That map runs **no codec** — it re-binds the persisted `Model` and rebuilds
refcounts. But `scene::CameraContent` is an `arbc::Editable` that holds a **non-inert
`arbc::StateHandle d_base`** (`src/scene/ace/scene/camera.hpp:101`, minted at construction
`camera.cpp:26-33`, written into the record via `txn.set_content_state` on rename
`camera.cpp:97-110`). So a checkpointed workspace containing a camera carries a
`ContentRecord` with a **non-inert** `StateHandle`, and `Model::rebuild_counts`'s assertion
(`model.cpp:771`) fires — **abort in debug, silent handle-corruption in release.** The
built-in cell kinds dodge this only because their durable state rides the serialize codec
path, never the workspace map.

This task **fixes the crash with an editor-side open-path policy**: `open_project` **skips the
workspace-map fast path and rebuilds from canonical** whenever (a) the caller supplied a
non-empty extra-kinds callback — i.e. **editor-defined editable kinds may be present** — and
(b) a canonical baseline (`project.arbc`) exists to rebuild from. Camera-bearing workspaces
therefore reopen through the codec path `reopen_codec` already made restore live typed
`CameraContent`, never touching the map path that would abort. The fast path is **preserved
untouched** for callers that register no editor kinds (tools/tests) and for the narrow
never-saved case where no canonical exists.

## Why it needs to be done

**A14** promises cameras "persist in the document (`project.arbc`) as scene objects"
(`docs/01-architecture.md:264`). `reopen_codec` closed the save/load asymmetry for the
*rebuild-from-canonical* open — but that is only the open route taken when the workspace file
is **absent or stale** (`project_open.cpp:148-149`). On the **common** reopen — the workspace
file present and mappable — the editor prefers the fast path (D-open-3's durable-by-default
promise, `project_open.cpp:137-138`), and a saved camera project **aborts there**. This is
strictly worse than the `PlaceholderContent` degradation `reopen_codec` fixed: a placeholder
was a *quiet* fallback; this is a *hard crash* (or silent corruption) on the exact projects
the editor is built to author. Every camera user hits it the first time they reopen a saved
project through the normal fast path. The bug is latent — invisible until the second launch —
and it sits on the highest-traffic open route. Closing it is a prerequisite for a shippable
camera feature: `cameras.manip`, `cameras.look_through`, and `cameras.export` all assume a
reopened camera is a live editable object, not a process that never finishes `open`.

The proper long-term fix — a libarbc per-kind **state-slab walk hook** that lets the map path
carry a custom kind's persisted state across reopen — lives in the pinned lib and does not
exist in `v0.1.0` (the library author reserves the seam, `model.cpp:768-770`, but ships it
inert). Exposing and consuming it is **cross-repo** work (a new `arbitrarycomposer` release +
an editor pin bump), out of scope for this editor-repo leaf. The editor-side policy is the
correct, self-contained interim: it is **fail-safe** (never maps a workspace that *might*
hold a custom kind, so it cannot abort), reuses the already-tested rebuild path, and adds no
DAG edge. The `.tji` note offers exactly these two options ("a libarbc bump … **or** an
editor-side policy to force rebuild-from-canonical when custom editable kinds may be
present"); this task takes the in-repo one.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **A14 — Cameras persist as an editor-defined `Content` kind** (`docs/01-architecture.md:264`).
  Cameras are a custom `org.arbc.camera` kind persisted through the `Registry` codec seam;
  A14 explicitly hedged that persistence rides "the existing snapshot seam" and left the
  *reopen* mechanics to the implementing leaves. This task handles the **workspace-map**
  reopen interaction A14 did not spell out.
- **A13 — canonical dump / rebuild-from-canonical + the dirty/durability model**
  (`:263`). "The dirty baseline is set at a `rebuilt_from_canonical` open" — the same path
  this task routes camera sessions to. A13 also establishes that a **workspace-mapped open**
  recovers unpublished edits (hence it is treated as *dirty*): the durability trade-off this
  task's policy makes (see Decision **D-slab-2**) is measured against A13's model.
- **A7 — process-per-project** (`:257`). "Opening a project is a new `exec`," so every reopen
  runs `open_project` fresh against the on-disk workspace/canonical — the crash is the default
  reopen behavior for a saved camera project, not a one-off.
- **D-open-3 — durable-by-default fast path** (`project_open.cpp:137-138`). The decision this
  task **conditionally deviates from** for editor-kind sessions; the deviation is recorded as
  the new **A15** doc delta (this refinement's doc delta).
- **A8 / §8 levelization table** (`:162-175`): `project` (L1) may depend on `base, platform,
  libarbc` — **not** `scene`. **§9 DoD** (`:181-208`).

**libarbc API surface** (pinned `v0.1.0`, fetched under `build/dev/_deps/arbc-src/`):

- `arbc::Document::open(path)` (`src/runtime/document.cpp:60-66`) → `Model::open`
  (`src/model/model.cpp:618-680`, `rebuild_counts` at `:667`) → `Model::rebuild_counts`
  (`:729`), whose leaf branch **asserts** `record->kind != RecordKind::Content ||
  !record->as.content.state.has_state()` (`model.cpp:760-772`, assert `:771`). This is the
  map-path abort.
- `arbc::StateHandle` (`src/model/arbc/model/records.hpp:47-56`): an index-only slab handle;
  **inert** = `slot == k_state_none` (`records.hpp:36`), **non-inert** otherwise. A
  `ContentRecord` (`records.hpp:59-63`) holds `{kind, StateHandle state}`; the state bytes
  live in a **kind-owned** store the model cannot interpret — hence the walk hook it lacks.
- `arbc::Registry::add` (`src/contract/arbc/contract/registry.hpp:125-128`): factory +
  metadata + optional codec + optional binder — **no** open-time state-walk registration.
- Pin: `CMakeLists.txt:23` (repo) / `:25` (`ARBC_GIT_TAG "v0.1.0"`); fetched HEAD is tag
  `v0.1.0`. **Unchangeable here** — a bump is a separate cross-repo action.

**Editor seams this leaf extends:**

- `src/project/project_open.cpp:126-178` — `open_project`; the **fast-path branch** at
  `:137-150` (present-workspace test `:139`, `Document::open` `:140`,
  `rebuilt_from_canonical = false` `:145`); the **canonical-existence** guard `:153`
  (`NoProject` if absent) and read `:156`; the forward to `rebuild_from_canonical` `:167-168`
  (`rebuilt_from_canonical = true` `:176`). The **single new guard** goes on the `:139`
  branch. `rebuild_from_canonical` (`:77-106`) and `create_project` (`:180-208`, writes **no**
  canonical — comment `:199`) are otherwise unchanged.
- `src/project/ace/project/project.hpp:150-152` — `open_project` declaration (the
  `register_extra_kinds` parameter, default `{}`). The branch condition reads it as a bool;
  no signature change.
- `src/scene/ace/scene/camera.hpp:42,101` / `src/scene/camera.cpp:26-33,97-110,390-424` —
  `CameraContent`'s non-inert `StateHandle` and `set_content_state` write (the crash trigger);
  `register_camera_kind` (the callback `commands` passes). **Unchanged** — this task touches
  neither `scene` nor `commands`.
- `src/commands/app_state.cpp` — the `register_editor_kinds()` registrar and the
  `open_project` call site (`reopen_codec.md:342`, `.tji`-cited `:70-74`). **Unchanged**: it
  already passes the callback; this task only changes how `project` *reacts* to a present
  callback.
- Tests: `tests/camera_model_test.cpp` — the `persist_and_shed_workspace` helper (test lines
  `100-113`), the 4 shed-workspace reopen units (`609-649`, `651-677`, `695-724`, `726-739`),
  and the **deferred fast-path `NOTE`** (`679-693`) that this task **converts into a live
  case**. `tests/project_open_test.cpp` — the open-route fidelity cases and `SECTION` idiom.

**Predecessor refinement:** `tasks/refinements/cameras/reopen_codec.md`.

## Constraints / requirements

1. **Fail-safe: never map a workspace that *may* hold a custom editable kind.** The crash
   trigger (a non-inert-`StateHandle` `ContentRecord`) can be checkpointed into the workspace
   map from an **unpublished** camera — one present in the map but not yet in `project.arbc`.
   So **canonical-based detection is unsound** (it would take the fast path on an unpublished
   camera and abort). The only crash-safe signal available *without* calling the aborting
   `Document::open` is the **presence of the extra-kinds callback** — "this session registers
   editor kinds, so a custom editable Content may be in the map." The policy branches on that
   signal, accepting conservative false-positives (a camera-free session still rebuilds) in
   exchange for never faulting.

2. **Reopen of a saved camera project yields a live `CameraContent`, not a crash.** After
   `save_project` (publishes canonical) with the workspace file **present** on disk,
   `open_project(fs, root, register_camera_kind)` completes without abort, reports
   `rebuilt_from_canonical == true` (it took the rebuild route, not the map), and
   `scene::cameras(reopened)` returns the persisted cameras as typed `scene::CameraContent`
   with `ObjectId`, name, resolution, frame `Affine`, and layer order intact — identical to the
   `reopen_codec` shed-workspace guarantee, now also holding when the workspace is present.

3. **The policy is gated on the callback; the fast path is untouched for non-editor callers.**
   A caller that registers **no** editor kinds (an empty/absent `register_extra_kinds` — tools,
   internal fixtures) keeps the workspace-map fast path verbatim (`rebuilt_from_canonical ==
   false`). The new branch adds exactly one conjunct to the existing `:139` condition; it does
   not perturb the map-path body, `rebuild_from_canonical`, or `create_project`.

4. **Canonical-existence fallback for never-saved projects.** `create_project` writes **no**
   `project.arbc` (`project_open.cpp:199`), so a created-but-never-saved project has only a
   workspace map. When editor kinds may be present but **no canonical exists**, there is
   nothing to rebuild from, so the policy **falls back to the fast path** (rebuild would return
   `NoProject`, `:153`). A **camera-free** never-saved project maps cleanly. A **camera-bearing**
   never-saved project is the one **residual** the pinned lib cannot serve (no canonical floor
   *and* no safe map) — documented, not crash-fixed here (see Decision **D-slab-3** / Open
   questions); the durability model (A13/D16 "dirty until first Save") already flags such a
   project as unsaved.

5. **Levelization — no new edge, no new component, no libarbc change.** The branch reads only
   `register_extra_kinds` (already a `project` parameter) and `fs.exists` (already used at
   `:139`,`:153`). `project` gains no `scene` include (`grep scene src/project/` stays empty),
   no ImGui/GL/SDL include, no new dependency. The pinned libarbc is **not** forked or bumped.
   `check_levels` stays clean with no lint edit.

6. **Doc delta (A15) records the open-path deviation.** Conditionally skipping D-open-3's
   durable-by-default fast path — and the libarbc v0.1.0 limitation that forces it — is an
   architectural policy on the open path, so it lands as a new **A15** row in
   `docs/01-architecture.md` in the **same commit** (`tasks/refinements/README.md:49`,
   `docs/00-design.md`/`01-architecture.md` same-commit rule). See Decision **D-slab-4**.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`); `scripts/gate` green
(check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — primary structural assertion.** No new component, no
  new DAG edge, no lint edit, no new `project` include; `grep scene src/project/` empty; the
  libarbc pin (`CMakeLists.txt:25`) is unchanged (Constraint 5).
- **L1 logic — Catch2 units** (extend `tests/camera_model_test.cpp`; `ace_tests`, headless):
  - **Headline — a saved camera project reopens *without aborting* via forced rebuild (the case
    `reopen_codec` deferred).** Author a project with a camera, `save_project` (publishes
    canonical), and leave the **workspace file present** (do **not** `remove_all` the
    workspace dir — the opposite of `persist_and_shed_workspace`, `camera_model_test.cpp:100-113`).
    Reopen via `open_project(fs, root, register_camera_kind)`. Assert the call **returns**
    (no abort — this is the case that faults against today's fast path, `model.cpp:771`),
    `opened.rebuilt_from_canonical == true` (policy skipped the map), and
    `scene::cameras(reopened)` round-trips name, resolution, frame `Affine`, `ObjectId`, and
    layer order as live `CameraContent` (`dynamic_cast` succeeds). This **replaces** the
    deferred `NOTE` at `camera_model_test.cpp:679-693`.
  - **Policy gate — no callback keeps the fast path.** The same present-workspace project
    reopened with an **absent** callback still takes the map path (`rebuilt_from_canonical ==
    false`) — pins that the rebuild is driven by the callback signal (Constraint 3), not an
    unconditional change. (Use a **camera-free** project here so the map path does not fault.)
  - **Camera-free editor session still rebuilds (documents the conservative cost).** A
    camera-free project with the workspace present, reopened **with** the callback, reports
    `rebuilt_from_canonical == true` — pins the deliberate false-positive of the fail-safe
    policy (Constraint 1 / D-slab-2) so the trade-off is visible and regression-locked.
  - **Canonical-absent fallback — a never-saved camera-free project maps cleanly.** A
    `create_project` project (no canonical) with the workspace present, reopened **with** the
    callback, falls back to the fast path (`rebuilt_from_canonical == false`) and opens
    successfully (Constraint 4). *(The never-saved **camera-bearing** case is the documented
    residual — it is **not** asserted here, because it aborts against the pinned lib; see Open
    questions.)*
  - **Regression — the shed-workspace units still pass.** The 4 existing `reopen_codec` units
    (`camera_model_test.cpp:609-649,651-677,695-724,726-739`) shed the workspace and hit
    canonical-rebuild regardless of the new branch; they stay green unchanged.
- **Rendered output — golden N/A (justified).** Cameras are non-rendering (A14, zero pixels);
  no code path here materializes pixels. The reopened camera's non-rendering invariant is
  already pinned by the model task's byte-equality unit and is independent of the open route.
  No new golden.
- **UI e2e — ImGui Test Engine N/A (justified).** No widget ships — this is a headless L1
  open-path fix (there is no Cameras view, `model.md:291-298`). The reopen behavior surfaces to
  the user through the first consuming camera UI (`editor.panels.layers`, `.tji:286-291`;
  `editor.panels.overview`, `.tji:293-297`), which carry the e2e budget. No e2e is deferred as a
  new task.
- **Threading (ASan/TSan).** The change **removes** a code path (it declines the map for
  editor-kind sessions); it adds no concurrency seam. The forced-rebuild route already
  synchronously checkpoints on the open (writer) thread before the document is handed to any
  renderer (`rebuild_from_canonical`, `project_open.cpp:102`). The units run under the existing
  ASan/TSan lanes; the render/edit race is separately owned by `editor.canvas.edit_render_sync`
  (`.tji:195-199`), untouched.
- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) — the units above ship with
  the task; clang-format + build clean.

## Decisions

- **D-slab-1 — Fix with an editor-side open-path policy (force rebuild-from-canonical for
  editor-kind sessions), not a libarbc bump.** The `.tji` note names both options. The libarbc
  per-kind state-slab walk hook does **not** exist in the pinned `v0.1.0` and cannot be added
  from this repo — it is cross-repo work (an `arbitrarycomposer` release exposing the hook +
  an editor pin bump + an editor-side walk implementation). The editor-side policy is
  self-contained, ships now, reuses the already-tested `rebuild_from_canonical` route
  (`reopen_codec`), adds no DAG edge, and is **fail-safe**. *Rationale:* correctness now over a
  library feature that requires external coordination; the interim policy is losslessly
  superseded by the hook when it lands. *Alternative rejected:* bump/fork libarbc to add the
  hook here — outside this repo's control (`CMakeLists.txt:25` pins a released tag), a large
  cross-repo change, and unnecessary for a correct reopen today; surfaced to the parking lot as
  the long-term fix (Open questions). *Alternative rejected:* teach the map path to skip/relocate
  a non-inert `StateHandle` locally — the state bytes live in a **kind-owned store the model
  cannot interpret** (`records.hpp:47-56`), which is *why* the lib defers the hook; the editor
  cannot re-implement it above libarbc.

- **D-slab-2 — Gate on the callback's presence (fail-safe), not on canonical content
  (fail-open).** The crash trigger can be a checkpointed **unpublished** camera — present in
  the workspace map, absent from `project.arbc`. So inspecting the canonical to decide "does
  this project use a custom kind" would take the fast path on an unpublished camera and
  **abort**. The only signal available without calling the aborting `Document::open` is
  whether the caller registered editor kinds (a non-empty `register_extra_kinds`). Branching on
  that is crash-safe by construction: a session that *might* hold a custom editable Content
  never maps. *Rationale:* fail-safe beats surgical — the conservative false-positive (a
  camera-free editor session rebuilds instead of mapping) costs the fast-path optimization, but
  never faults; every more-surgical detector fails open on the unpublished-custom-kind case.
  *Cost, documented:* an editor session forfeits the map path's crash-recovery of *unpublished*
  edits (A13's dirty-recovery), reopening at the last **saved** canonical instead. For a
  camera-**bearing** workspace this is strictly better than today's abort; for a
  camera-**free** editor session it is a durability regression that the libarbc hook (D-slab-1's
  long-term fix) reverses. A camera-free-session test (Acceptance) pins the cost so it is
  visible, not silent. *Alternative rejected:* a `commands`-maintained sidecar marker
  ("this workspace holds a custom kind") to preserve the fast path for camera-free sessions —
  it introduces editor-owned durable state parallel to the libarbc-owned workspace, and can
  **fail open** (a lost/late marker write races the async housekeeping checkpoint that puts the
  camera in the map → the fast path is taken → abort). More machinery, weaker safety;
  disproportionate for 2d when the libarbc hook is the real fix.

- **D-slab-3 — Guard the policy on canonical existence; leave the never-saved camera case as a
  documented residual.** `create_project` writes no `project.arbc` (`project_open.cpp:199`), so
  forcing rebuild unconditionally would break reopen of a created-but-never-saved project
  (`NoProject`, `:153`). The policy therefore skips the fast path only when editor kinds may be
  present **and** a canonical exists; otherwise it falls back to the map. This leaves one
  narrow case the pinned lib genuinely cannot serve — a **never-saved** project that **has** a
  camera (no canonical floor to rebuild from, no safe way to map). *Rationale:* every **saved**
  camera project — the durable, cross-session artifact A14 is about — is fully fixed; the
  residual is an unsaved session the dirty model (A13/D16, "dirty until first Save") already
  frames as not-yet-durable. Closing it needs either the libarbc hook (D-slab-1) or a
  create-time canonical baseline. *Alternative rejected:* publish an empty `project.arbc`
  baseline at `create_project` (or on first editor-kind mutation) so rebuild always has a floor
  — it deviates from D-open's "no `project.arbc` until save is the publish step"
  (`project_open.cpp:199`), adds VCS/publish semantics, and would be mooted by the libarbc hook;
  a design judgment surfaced to the parking lot, not auto-spawned as a WBS leaf (per
  `tasks/refinements/README.md`: no self-perpetuating "revisit" tasks).

- **D-slab-4 — Doc delta: add `A15`.** Conditionally declining D-open-3's durable-by-default
  fast path is a deviation from a stated decision, and the libarbc v0.1.0 slab-hook absence is a
  normative architectural fact future editor kinds inherit — so it is recorded as a new **A15**
  row in `docs/01-architecture.md`, not left implicit in this refinement. A15 states: (1)
  libarbc v0.1.0 exposes no per-kind state-slab walk hook, so `Document::open` aborts on a
  checkpointed non-inert custom-kind `StateHandle` (`model.cpp:771`); (2) the editor therefore
  forces rebuild-from-canonical for any reopen that may hold editor-defined editable kinds,
  trading the fast path's speed and unpublished-edit recovery for a correct, live reopen; (3)
  the fallback and residual of D-slab-3; (4) the future libarbc hook restores the fast path.
  *Rationale:* the same-commit constitution rule (`README.md:85-87`) — a change that alters
  decided open-path behavior amends the governing doc. *Alternative rejected:* fold it into
  A14 — A14 is about *persistence-as-a-custom-kind*; the open-path policy + libarbc limitation
  is a distinct architectural fact cleaner as its own row that any second editable kind reads.

## Open questions

(none — all decided.) Two items are surfaced to the parking lot as **human-judgment /
cross-repo** work, **not** WBS leaves (they cannot be closed by an in-repo implementer, so
encoding them as tasks would spawn the self-perpetuating loop `tasks/refinements/README.md`
warns against):

1. **The long-term fix — a libarbc per-kind state-slab walk hook restoring the fast path for
   custom editable kinds.** It is cross-repo: `arbitrarycomposer` must expose the hook
   (`model.cpp:768-770` reserves the location) in a new release, then the editor bumps the pin
   (`CMakeLists.txt:25`) and consumes it. When it lands it *supersedes* D-slab-1's policy and
   reverses D-slab-2's durability cost — an editor kind would then survive the map path
   directly. Requires an external library release, so it is a parking-lot item.

2. **The never-saved camera residual (D-slab-3).** Closing it in-repo would mean a create-time
   canonical baseline — a design judgment that deviates from D-open's publish model and is
   mooted by item (1). Surfaced for human decision, not spawned as a WBS leaf.

No new WBS leaf is spawned by this task. The reopen behavior's UI e2e rides
`editor.panels.layers` / `editor.panels.overview` (already scheduled); the render/edit
thread-safety of the reopened document is owned by `editor.canvas.edit_render_sync`
(`.tji:195-199`).

## Status

**Done** — 2026-07-19.

- `src/project/project_open.cpp` — added one guarded conjunct on the `open_project` fast-path branch: skip the workspace-map fast path and force rebuild-from-canonical when `register_extra_kinds` is non-empty *and* a canonical exists (`A15 / D-slab-1/2/3`).
- `tests/camera_model_test.cpp` — added `persist_keeping_workspace` helper; replaced the deferred NOTE at lines 679-693 with four live Catch2 cases: headline crash-avoidance ("saved camera project reopens via forced rebuild with the workspace present"), policy gate ("without the callback keeps the map fast path"), fail-safe cost ("camera-free editor session still forces rebuild"), and canonical-absent fallback ("never-saved camera-free project falls back to the map path").
- `docs/01-architecture.md` — new `A15` row recording the open-path deviation from D-open-3 for editor-defined editable kinds and the libarbc v0.1.0 slab-hook absence.
- `src/render/ace/render/canvas_host.hpp`, `src/render/canvas_host.cpp`, `tests/canvas_host_test.cpp` — fixer: added `CanvasHost::apply_edit` with `doc_mu` mutex to guard document mutations against render-thread reads; routed two `damage()` edits in the test through it (resolves TSAN race surfaced by CI on this iteration; remaining shipped-path race tracked under `editor.canvas.edit_render_sync`).
- Tech-debt registered: none (libarbc slab-hook and never-saved-camera residual are parking-lot items per Open questions; fixer's `canvas-host-shipped-edit-race` maps to existing `editor.canvas.edit_render_sync`).
