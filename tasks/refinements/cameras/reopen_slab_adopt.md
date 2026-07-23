# editor.cameras.reopen_slab_adopt — Adopt libarbc v0.2.0 state-slab walk hook; retire A15 rebuild-from-canonical

## TaskJuggler entry

- **Task:** `editor.cameras.reopen_slab_adopt` (`tasks/00-editor.tji:329-334`, under
  `task cameras "Cameras"` at `:300`).
- **Effort:** `2d` · `allocate team`.
- **Depends:** `editor.canvas.arbc_v020` (`.tji:241-247`, `complete 100`) and
  `!workspace_reopen_slab` — the sibling `editor.cameras.workspace_reopen_slab`
  (`:322-328`), already `complete 100`.
- **Note (`.tji:333`):** "ruoso/arbitrarycomposer#5 shipped in v0.2.0:
  `Registry::KindStateWalker` (registered with the factory, looked up lock-free via
  `Registry::state_walker`) + `Model::recovered_content_state()` (the fast-reopen walk
  collects each reachable non-inert content StateHandle the model cannot descend) +
  `arbc/runtime/recovered_state_replay.hpp`'s `replay_recovered_content_state()`.
  Register a camera-kind KindStateWalker, call `replay_recovered_content_state` on the
  `Document::open` map path, and retire A15's force-rebuild-from-canonical policy
  (reverses D-slab-2's durability cost). Also closes the never-saved-camera residual
  (D-slab-3): the fast path carries the camera's persisted state directly, no canonical
  floor needed. Source-of-debt: `tasks/refinements/cameras/workspace_reopen_slab.md`.
  Design: `docs/01-architecture.md` A14."
- **Back-link:** the `.tji` note already ends with
  `Refinement: tasks/refinements/cameras/reopen_slab_adopt.md`; the closer adds
  `complete 100` after `allocate team` (`tasks/refinements/README.md:47-68`).
  **Do not** hand-edit the `.tji` here.

> **Scope correction, up front.** The `.tji` note prescribes a plan that **cannot be
> implemented at the pinned libarbc v0.3.0**, for two independent, verified reasons (see
> **Inputs / context → Verification** and **D-slab_adopt-1/2**). The note's *goal* —
> "make the workspace-map reopen serve a camera, and stop paying A15's durability cost" —
> is unreachable in this repo. What this leaf delivers instead is the **verified finding
> that settles it**, a **corrected and broadened open-path guard** derived from the true
> cause, and an **honest signal** where the previous policy silently lost data. A15's
> policy is *confirmed and kept*, not retired; its stale premise is replaced.

## Effort estimate

**2 days**, unchanged from the `.tji`, but reallocated. Roughly: 0.5d to author the
evidence suite that pins what the v0.3.0 map path actually does (the part that converts an
assumed architectural claim into a tested one); 0.75d for the `project_open.cpp` guard
rewrite — the branch condition, the 22-line justification comment that currently cites a
libarbc assert that no longer exists, the new `OpenedProject` degradation field, and the
knock-on updates to the four existing policy tests plus the two `project_open_test.cpp`
cases that pin the map path; 0.5d for the doc delta (a new `A19` row plus a one-line `A15`
amendment); 0.25d for the sanitizer lanes and coverage. No new component, no new DAG edge,
no new external dependency, no libarbc fork, no pin bump.

The original 2d was budgeted for "register a walker, call the replay, delete the policy."
That work does not exist. The replacement work is comparable in size because it is mostly
test authorship and a behavior change on the shared open path, which the existing suite
pins from four directions.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cameras.workspace_reopen_slab`** (`tasks/refinements/cameras/workspace_reopen_slab.md`,
  **Done** 2026-07-19) — shipped the force-rebuild-from-canonical guard at
  `src/project/project_open.cpp:162-163` under a 15-line rationale comment (`:147-161`),
  the `persist_keeping_workspace` test helper (`tests/camera_model_test.cpp:115-128`), the
  four policy units (`camera_model_test.cpp:858, 903, 918, 933`), and the **A15** row
  (`docs/01-architecture.md:377`). This leaf **rewrites the guard's condition and its
  entire justification**, keeps its effect for the editor, and amends A15. Its
  **D-slab-1** explicitly anticipated this leaf: *"the interim policy is losslessly
  superseded by the hook when it lands."* The finding here is that the hook does not
  supersede it — see D-slab_adopt-1.
- **`editor.cameras.reopen_codec`** (`tasks/refinements/cameras/reopen_codec.md`, **Done**)
  — the extra-kinds registration callback threaded into `open_project` /
  `rebuild_from_canonical` (`project_open.cpp:77-116`, `project.hpp:150-152`), and
  `commands::register_editor_kinds` (`src/commands/app_state.cpp:29`) as its single
  editor-side registrar. **Unchanged here**, but the callback stops being the *reason* for
  the guard and becomes only a cheap short-circuit (D-slab_adopt-4).
- **`editor.cameras.rename_stable_id`** — created the state slab that this whole thread is
  about: `CameraContent`'s per-instance versioned `{name, resolution}` store
  (`src/scene/ace/scene/camera.hpp:97-111`) and the non-inert `arbc::StateHandle d_base`
  minted in the constructor (`src/scene/camera.cpp:165-172`). Its shape is what makes the
  `KindStateWalker` inapplicable (D-slab_adopt-2). **Unchanged.**
- **`editor.canvas.arbc_v020` / `editor.canvas.arbc_v030`** — the pin is at **v0.3.0**
  (`CMakeLists.txt:25`), so the entire arbc#5 surface is present and compilable.
  `arbc_v030.md`'s **D-arbc_v030-6** amended A15 to say the premise no longer holds and
  deliberately declined to assert that the exposed hook serves the camera kind: *"Asserting
  here that it does would be an unverified claim in the constitution — the failure mode this
  decision exists to prevent."* **That verification is this leaf's job, and its answer is
  no.**

**Pending (owned here):** the evidence suite; the `project_open.cpp` guard condition +
comment rewrite; the `OpenedProject` degradation field; the doc delta (`A19` + `A15`
amendment); the follow-up task registration. No downstream leaf is blocked.

## What this task is

The `.tji` note asks for three things: register a camera `KindStateWalker`, call
`replay_recovered_content_state` on the `Document::open` map path, and retire A15's
force-rebuild-from-canonical policy. **All three were verified against the pinned v0.3.0
sources and a live probe, and none of them is achievable.** The reasons are recorded in
full under **Inputs / context → Verification**; in summary:

1. **The replay has no reachable input.** `replay_recovered_content_state` consumes
   `Model::recovered_content_state()`. The editor opens through `arbc::Document::open`
   (`project_open.cpp:164`), and `arbc::Document` publishes **no** accessor for the
   underlying `Model` — deliberately (`document.hpp:398-410`: granted only through the
   attorney-client `HostViewportDocumentAccess`, "rather than a public `Model& model()`,
   which would hand every host the unguarded model"), and that accessor lives in
   `src/runtime/document_access.hpp`, a header the library marks *"PRIVATE to the
   component — it is not a public header, so no host can reach it."*
2. **The walker has nothing to walk.** `KindStateWalker::reach(void* store, ObjectId,
   StateHandle)` rebuilds slab refcounts inside a kind's **document-level** state store
   (`registry.hpp:105-120`). `CameraContent`'s store is a **per-instance** `std::vector<Version>`
   of heap-allocated `std::string`s (`camera.hpp:97-111`) that does not survive process
   exit and has no document-level identity to hand back from `RecoveredStateHooks::store_of`.
3. **The deeper reason, which subsumes both:** the workspace-map reopen **binds no
   `Content` object for any kind at all.** `arbc::Document::open(path, housekeeping)` takes
   no `Registry` and runs no factory (`document.hpp:76-85`), so the id→`Content` side-map
   starts empty and `resolve()` returns null for every recovered record. `scene::cameras()`
   requires `dynamic_cast<const CameraContent*>(document.resolve(...))`
   (`src/scene/camera.cpp:471-499`), so a mapped reopen yields **zero cameras** — and,
   identically, `scene::cells()` yields zero cells. Retiring A15 would not restore the
   camera; it would hand the editor a document with a live record graph and nothing in it.

So this leaf **keeps the guard and re-founds it on the true cause**, and delivers the three
things that are genuinely available:

- **The evidence.** Catch2 units that pin, at the pinned version, that (a) the v0.1.0 abort
  is *gone* — a checkpointed non-inert `StateHandle` now reopens cleanly, which is the one
  real thing arbc#5 gave the editor — and (b) the mapped document restores the record graph
  **only**, with every content unbound, for a built-in kind and the camera kind alike.
- **A correct, broader guard.** The current condition (`register_extra_kinds && canonical
  exists`) protects the editor but leaves a **latent bug for every other caller**: a
  no-callback caller with a content-bearing workspace still takes the map path today and
  silently receives a document whose every `resolve()` is null. Because mapping is now
  provably non-aborting, the guard can *inspect what it mapped* instead of guessing:
  keep the mapped document only when it holds no content records the map could not bind.
- **An honest signal instead of silent loss.** When there is no canonical to rebuild from
  (a never-saved project — D-slab-3's residual), the mapped document is all the editor has,
  and its cameras and cells are gone without a word. `OpenedProject` gains a count of
  unbindable content records so the caller can say so.

## Why it needs to be done

**A15 is currently wrong in the constitution, and its own amendment says so.** The row's
premise — *"`Model::rebuild_counts` asserts every recovered `ContentRecord` carries an
inert `StateHandle`"* — describes libarbc v0.1.0. At the pinned v0.3.0 that assert is gone;
`model.cpp:768-783` **collects** the handle instead. `arbc_v030.md`'s D-arbc_v030-6 flagged
the drift, retained the v0.1.0 text as history, and wrote: *"until that leaf lands, the
rebuild-from-canonical policy below remains in force, because it is what ships today."*
This is that leaf. Leaving it undone means the constitution keeps justifying a shipped
policy with a fact that is no longer true, and the 22-line comment at
`project_open.cpp:147-161` keeps telling the next reader that `Document::open` will abort —
which it will not. A future engineer who checks that claim, finds it false, and deletes the
guard would ship a reopen that silently empties every project.

**The latent bug is real and unowned.** `project_open_test.cpp:152` pins that
`open_project` "maps the crash-durable workspace on reopen" for a caller that registers no
editor kinds. Nothing asserts that the mapped document's content is *usable*, and the probe
below shows it is not. D-open-3's "durable-by-default fast path" has therefore never
delivered a usable document for a project with content — for **any** kind, built-in cells
included. That is a strictly larger problem than cameras, it sits on the shared open path,
and this leaf is the first one with the evidence in hand to name it.

**The never-saved case is now silent data loss rather than a crash.** At v0.1.0 a
never-saved camera-bearing project *aborted* on reopen (D-slab-3's documented residual).
At v0.3.0 it opens successfully with the camera simply absent. A crash is at least
self-announcing; a silently empty project reads as "my work was never saved," and D16's
promise that `workspace/` makes the project "crash-durable by default" is exactly what the
user will have believed. Converting that into a reported degradation is the honest in-repo
close, and it is a prerequisite for any UI that tells the user what happened.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **A14 — Cameras persist as an editor-defined libarbc `Content` kind**
  (`docs/01-architecture.md:376`). The `.tji`-named governing row. A14 settles that camera
  state is persisted **through the `Registry` codec seam** into `project.arbc` — *"the
  camera codec registers on the same `Registry`/codec seam as the builtin cell kinds"* —
  which is precisely why the camera is a **codec-persisted** kind and not a
  **workspace-slab-persisted** one. That distinction is the whole answer to this leaf
  (D-slab_adopt-2).
- **A15 — An editor-defined *editable* kind forces rebuild-from-canonical on reopen**
  (`:377`). The row this leaf amends. Its "Future fix" clause — *"a libarbc release
  exposing the per-kind state-slab walk hook restores the fast path for custom editable
  kinds… then A15's policy is superseded"* — is the claim this leaf tests and refutes.
- **A13 — canonical dump / rebuild-from-canonical + the dirty model** (`:375`). Establishes
  `rebuilt_from_canonical` as the dirty baseline signal, consumed at
  `src/commands/app_state.cpp:53,70-76`. Broadening the guard changes which reopens report
  it (Constraint 6).
- **A16** (`:378`) — enumerates the `Registry` surface *including* `state_walker`, and notes
  cell insert *"registers no new kind, so A15's rebuild-from-canonical policy is
  untouched."* Still true after this leaf.
- **A18** (`:380`) — the highest existing row; the doc delta lands **A19** after it.
- **§8 levelization** (`:256-291`) and the lint's `ALLOWED` / `EXTERNAL_ALLOWED` dicts
  (`scripts/check_levels.py:21-42`, `:47-53`): `project` is L1 and may depend on
  `base`, `platform`, and `arbc` — **not** `scene`. **§9 DoD** (`:293-357`).
- **D16** (`docs/00-design.md:483`) — *"`workspace/` makes the project crash-durable by
  default; Save = re-dump `project.arbc`"*. The promise the map path does not keep for
  content; the reason the degradation deserves a signal rather than silence.

**Verification performed while writing this refinement (reproducible).** A standalone probe
was compiled against the pinned tree (`build/dev/_deps/arbc-build/src/libarbc.a`,
`git describe` → `v0.3.0`) and run. It creates a workspace-backed `arbc::Document`, mints
one content via `registry.factory(kind)`, attaches it to a composition by one layer,
`checkpoint()`s, closes, and reopens with `arbc::Document::open`. Observed, verbatim:

```
--- org.arbc.solid (non-editable: inert StateHandle) ---
pre-close: state_handle_slot=4294967295 has_state=0
pre-close: resolve=0x55f6b00f4910 revision=4
reopened: pin=0x55f6b00f6a90 revision=5
reopened: resolve(content)=(nil)
reopened: state_handle_slot=4294967295 has_state=0
reopened: for_each_content count=0
reopened: find_first_composition=1
  layer 3 content=1 resolve=(nil)
reopened: layer count=1

--- org.arbc.raster (Editable: NON-INERT StateHandle, the camera's shape) ---
pre-close: state_handle_slot=0 has_state=1
pre-close: resolve=0x55db56a258c0 revision=4
reopened: pin=0x55db56a28460 revision=5
reopened: resolve(content)=(nil)
reopened: state_handle_slot=0 has_state=1
reopened: for_each_content count=0
reopened: find_first_composition=1
  layer 3 content=1 resolve=(nil)
reopened: layer count=1
```

Three facts, each load-bearing:

1. **The v0.1.0 abort is gone.** A checkpointed **non-inert** `StateHandle` (`slot=0`,
   `has_state=1` — the raster case, structurally identical to `CameraContent`'s
   constructor-minted handle at `camera.cpp:165-172`) reopens with no abort and no
   diagnostic. A15's premise is dead, empirically, not just by reading `model.cpp:768-783`.
2. **The record graph survives intact** — composition found, layer present, layer→content
   id preserved, and the persisted `StateHandle` slot preserved verbatim across the reopen.
3. **No `Content` is bound, for either kind.** `resolve(content) == nullptr` and
   `for_each_content` visits **zero** contents. The limitation is **kind-agnostic**: it is
   not about editable kinds, not about custom kinds, and not about state slabs. It is that
   `Document::open` runs no factory.

**libarbc API surface (pinned v0.3.0, `build/dev/_deps/arbc-src/`, mirrored at
`/home/ruoso/devel/arbitrarycomposer`, tag `v0.3.0`):**

- `arbc::KindStateWalker` (`src/contract/arbc/contract/registry.hpp:105-120`):
  `struct KindStateWalker { void (*reach)(void* store, ObjectId content, StateHandle handle); };`
  The doc comment is explicit that `store` is *"the kind's own **document-level** state
  store, type-erased across the registry boundary"* and that the walker exists *"so the
  kind's DOCUMENT-LEVEL state store rebuilds the slab refcounts a persisted handle keeps
  reachable"*. Registered as the 6th argument to `Registry::add` (`:141-146`); looked up via
  `Registry::state_walker(id)` (`:160-164`), whose comment notes *"every kind today"*
  registers none.
- `Model::RecoveredContentState` + `Model::recovered_content_state()`
  (`src/model/arbc/model/model.hpp:309-331`); populated at `src/model/model.cpp:731`,
  collected at `:768-783`.
- `arbc::replay_recovered_content_state` (`src/runtime/arbc/runtime/recovered_state_replay.hpp:44-53`)
  with `RecoveredStateHooks{kind_id_of, store_of}` (`:25-35`) and
  `RecoveredStateReplayStats{dispatched, skipped}` (`:37-42`). **The library never calls it
  itself** — the only call sites in the whole arbc tree are its own three tests
  (`tests/recovered_state_replay.t.cpp:157,187,214`), each of which drives it from
  **`Model::open`**, not `Document::open`.
- `arbc::Document::open` (`src/runtime/arbc/runtime/document.hpp:76-85`) — no `Registry`
  parameter, no `recovered` accessor. `resolve` / `for_each_content` (`:264-284`) read the
  COW binding table, which `open` leaves empty. The `Model&` is reachable only through
  `friend struct HostViewportDocumentAccess` (`:398-410`), defined in the **private** header
  `src/runtime/document_access.hpp`.
- `arbc::DocRoot` walk surface the guard will use: `for_each_layer(const
  std::function<void(const LayerRecord&)>&)` (`model.hpp:96`), `find_layer` / `find_content`
  (`:54-55`), `find_first_composition` (`:73`), `content_state(ObjectId)` (`:153`).

**Editor seams this leaf extends:**

- `src/project/project_open.cpp:136-202` — `open_project`. The **rationale comment**
  `:147-161` (rewritten in full), the **guard** `:162` (`force_rebuild_for_editor_kinds`)
  and its branch `:163-174`, the canonical-absent `NoProject` at `:176-178`, the forward to
  `rebuild_from_canonical` `:194-197`, and `rebuilt_from_canonical = true` at `:200`.
  `rebuild_from_canonical` itself (`:77-116`) is **unchanged**.
- `src/project/ace/project/project.hpp:123-131` — `OpenedProject` (gains one field);
  `:150-152` — `open_project`'s declaration and its `register_extra_kinds` doc block
  (`:134-149`), whose text describes the callback's role in the guard and must be updated.
  `create_project` (`:155-159`, defined `project_open.cpp:204-239`) writes **no**
  `project.arbc` — the source of the never-saved case.
- `src/scene/ace/scene/camera.hpp:93-94,97-111,118` and `src/scene/camera.cpp:165-172,
  187-238,390-399,409-437,471-499` — the camera kind, its per-instance slab, its `Editable`
  hooks, its codec, and `scene::cameras()`. **All unchanged**; cited as the evidence that
  the walker does not fit and as the read-back the tests assert through.
- `src/commands/app_state.cpp:29` (`register_editor_kinds`), `:53,70-76` (the
  `rebuilt_from_canonical` → clean-baseline branch), `:145-165`
  (`open_or_create_app_state`). **Unchanged code**, but `:70-76` changes *which* reopens
  take the clean branch (Constraint 6).
- Tests: `tests/camera_model_test.cpp` — helpers `persist_and_shed_workspace` (`:96-113`)
  and `persist_keeping_workspace` (`:115-128`), the four A15 policy units (`:858`, `:903`,
  `:918`, `:933`), the canonical-rebuild units (`:779`, `:821`, `:954`, `:985`).
  `tests/project_open_test.cpp:152,178,197,306`. `tests/project_save_test.cpp:247,270`.
  `tests/cell_model_test.cpp:446`. `tests/arbc_pin_test.cpp:91` — the pin-witness file.

**Predecessor refinements:** `tasks/refinements/cameras/workspace_reopen_slab.md`,
`tasks/refinements/cameras/reopen_codec.md`, `tasks/refinements/canvas/arbc_v030.md`.

## Constraints / requirements

1. **The evidence suite is authored first and is the leaf's primary artifact.** Before any
   guard change, land the Catch2 units that pin the two facts the probe established: a
   checkpointed **non-inert** `StateHandle` reopens through `arbc::Document::open` without
   abort, and the mapped document binds **no `Content`** (`resolve` null, `for_each_content`
   zero) while the record graph survives. Assert both for a **built-in** kind and for the
   **camera** kind, so the record shows the limitation is kind-agnostic rather than a camera
   quirk. If these tests contradict the probe, the rest of this refinement does not apply
   and the leaf stops and reports (Open questions).

2. **No `KindStateWalker` is registered.** `register_camera_kind`
   (`src/scene/camera.cpp:409-437`) keeps its three-argument `registry.add(id, factory,
   metadata, codec)` shape. Registering a walker whose input is unreachable and whose
   `store` has no document-level referent would be unreachable code asserting a capability
   the editor does not have (D-slab_adopt-2).

3. **The guard's *effect* for the editor is unchanged; its *condition* and *justification*
   are rewritten.** An editor session (non-empty `register_extra_kinds`) with a canonical
   present still rebuilds from canonical. The comment at `project_open.cpp:147-161` must
   stop citing `model.cpp:771`'s assert — which no longer exists — and state the real
   reason: the map path binds no `Content`, so a content-bearing project is only reopenable
   through the canonical rebuild.

4. **The guard is extended to callers that register no editor kinds, via map-then-inspect,
   with no extra `Document::open` on the editor's common path.** Shape: keep the existing
   cheap proxy as a short-circuit (an editor-kind session with a canonical skips the map
   entirely, exactly as today — no wasted open of a large workspace); otherwise map as
   today, then **inspect the recovered root** and keep the mapped document only when it
   holds no content record the map could not bind. When a content-bearing map is rejected
   and a canonical exists, fall through to `rebuild_from_canonical`. Detection walks
   `DocRoot::for_each_layer` (`model.hpp:96`) and counts layers whose `content.valid()` but
   `document.resolve(content) == nullptr` — the global-order walk, so contents inside nested
   compositions are counted too. `project` names no `scene` type to do this
   (Constraint 7).

5. **Canonical-absent fallback is preserved, and its loss is reported.** With no
   `project.arbc` there is nothing to rebuild from, so the mapped document is kept even when
   its content is unbindable — the alternative is `NoProject`, which is worse. `OpenedProject`
   (`project.hpp:123-131`) gains a count of unbindable content records, defaulting to zero,
   set only on the kept-mapped path. It is a **value, not an error** (`platform::Result`'s
   error channel stays reserved for `OpenError`): the document still opens.

6. **The dirty-baseline knock-on is deliberate and pinned.** More reopens now report
   `rebuilt_from_canonical == true`, so `commands::AppState` takes its clean-baseline branch
   (`app_state.cpp:53,70-76`) where it previously started dirty. That is a correctness
   improvement — the document really was just built from the canonical — but it is a
   behavior change on a shipped path and must be asserted, not discovered
   (`tests/project_save_test.cpp:247,270` are the incumbents).

7. **Levelization — no new component, no new edge, no new dependency, no pin bump.** The
   change lives in L1 `project`, which already depends on `arbc`
   (`scripts/check_levels.py:47-53`). `grep -r scene src/project/` stays empty;
   `CMakeLists.txt:25` stays at `v0.3.0`; no ImGui/GL/SDL include enters L1.
   `check_levels` stays clean with no lint edit.

8. **Doc delta (`A19` + an `A15` amendment) rides the same commit.** The newly-established
   normative fact — the workspace-map reopen restores the record graph only, for every kind
   — is general, is not what A15 is titled about, and is the thing every future editor kind
   needs to read. It lands as **A19**; A15 gains a one-line amendment pointing at it
   (D-slab_adopt-6). Same-commit rule, `tasks/refinements/README.md:49`.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate` green
(check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean).** No new component, no new DAG edge, no
  `scripts/check_levels.py` edit, no new `project` include beyond `arbc/`;
  `grep -r scene src/project/` empty; `CMakeLists.txt:25` unchanged at `v0.3.0`
  (Constraint 7).

- **L1 logic — Catch2 units** (`ace_tests`, headless):

  - **Evidence 1 — the v0.1.0 abort is gone (`tests/arbc_pin_test.cpp`, beside the existing
    v0.3.0 witnesses at `:91`).** Create a workspace-backed `arbc::Document`, add an
    `Editable` built-in content so its `ContentRecord` carries a **non-inert**
    `StateHandle` (assert `pin()->content_state(id).has_state()`), `checkpoint()`, close,
    and `arbc::Document::open` the file. Assert the open **returns a value** (this is the
    call that aborted at v0.1.0) and that the recovered `content_state(id)` slot is
    **preserved**. This is the single fact arbc#5 actually delivered to the editor, and it
    is what stops A15's dead premise from being re-asserted. Compile-time witnesses that
    `arbc::KindStateWalker`, `arbc::Registry::state_walker`,
    `arbc::Model::recovered_content_state`, and `arbc::replay_recovered_content_state` exist
    at the pin ride here too — named, then documented as unreachable from `arbc::Document`
    (D-slab_adopt-1), so the next reader does not re-derive it.

  - **Evidence 2 — the mapped document binds no content, for a built-in kind
    (`tests/project_open_test.cpp`).** Reopen a checkpointed workspace holding one built-in
    cell through the map path and assert: `pin()` is non-null and the record graph survives
    (`find_first_composition` true, the layer present, `layer->content` the original id,
    `layer->transform` byte-equal), while `document.resolve(layer->content) == nullptr`,
    `for_each_content` visits zero contents, and `scene::cells(mapped)` is **empty**.

  - **Evidence 3 — the same for the camera kind (`tests/camera_model_test.cpp`).** Via
    `persist_keeping_workspace` (`:115-128`), reopen a camera-bearing workspace through the
    map path directly (`arbc::Document::open`, bypassing `open_project`'s guard) and assert
    `scene::cameras(mapped)` is **empty** while the camera's layer record and its non-inert
    `content_state` slot survive. Together with Evidence 2 this pins that the limitation is
    kind-agnostic — the assertion A15 will now rest on.

  - **Guard — an editor-kind session with a canonical still rebuilds.** The incumbent
    headline (`camera_model_test.cpp:858`) stays green **unchanged**: a saved camera
    project with the workspace present reopens with `rebuilt_from_canonical == true` and
    `scene::cameras()` round-tripping name, resolution, frame `Affine`, `ObjectId`, and
    layer order as live `CameraContent`. Effect preserved, justification replaced
    (Constraint 3).

  - **Guard — a no-callback caller with a content-bearing workspace now rebuilds too.**
    The behavior change of Constraint 4. A content-bearing project with the workspace
    present and **no** callback reopens with `rebuilt_from_canonical == true` and live
    content. This **updates** `camera_model_test.cpp:903` ("without the callback a
    present-workspace project keeps the map fast path") and, if its fixture carries content,
    `project_open_test.cpp:152` — both currently pin behavior Evidence 2 proves returns an
    unusable document. The updated cases must assert *live content*, not just the flag, so
    they cannot regress to pinning emptiness again.

  - **Guard — a content-free workspace still takes the map fast path.** A no-callback
    caller whose workspace holds no content records keeps the map
    (`rebuilt_from_canonical == false`), so the fast path and A13's recovery of unpublished
    record-level edits survive where they are actually harmless. Pins that the guard is
    content-driven, not a blanket disable.

  - **Never-saved fallback + the degradation signal.** A `create_project` project (no
    canonical) with a camera checkpointed into the workspace, reopened **with** the
    callback: the call **returns** (no abort — the case that faulted at v0.1.0 and is
    asserted here for the first time), takes the map (`rebuilt_from_canonical == false`),
    and reports a **non-zero** unbindable-content count on `OpenedProject`. The camera-free
    never-saved case (`camera_model_test.cpp:933`) stays green with the count **zero**.
    This is D-slab-3's residual, closed as a reported degradation (D-slab_adopt-5).

  - **Dirty-baseline knock-on.** `tests/project_save_test.cpp:247,270` stay green, plus a
    case asserting that a no-callback content-bearing reopen — which now rebuilds — starts
    **clean** rather than dirty (Constraint 6).

  - **Regression.** The canonical-rebuild units (`camera_model_test.cpp:779, 821, 954, 985`),
    the shed-workspace units, `project_open_test.cpp:178,197,306`, and
    `cell_model_test.cpp:446` stay green unchanged.

- **Rendered output — golden N/A (justified).** Cameras are non-rendering (A14, zero
  pixels), and no code path here materializes pixels: the change is a branch in
  `open_project` plus test authorship. The six committed goldens must come back
  **byte-identical**; per `arbc_v030.md`'s **D-arbc_v030-7**, a mismatch is a finding that
  blocks the leaf, never a baseline to regenerate. No new golden.

- **UI e2e — ImGui Test Engine N/A in this leaf (justified, with the follow-up named).** No
  widget ships; this is a headless L1 open-path change. The degradation signal is a value on
  `OpenedProject` with no UI consumer yet — surfacing it is deferred to
  `editor.project.reopen_degradation_notice` (**0.5d**, under `editor.project`, `depends
  editor.cameras.reopen_slab_adopt`; milestone `m9_editor` via the `editor.project`
  container): read `OpenedProject`'s unbindable-content count through the existing
  `dock::ProjectGateway` POD seam and show a one-shot notice on open ("this project's
  unsaved workspace held N objects that could not be recovered"), with an ImGui Test Engine
  e2e driving it headless. **Deferred to `editor.project.reopen_degradation_notice` (closer
  registers in WBS).**

- **Threading (ASan/TSan).** The change adds no concurrency seam: the guard and the
  inspection run on the open (writer) thread inside `open_project`, before the document is
  handed to any renderer, and `resolve()` is documented lock-free-safe regardless
  (`document.hpp:264-269`). The new units run in the existing `ace_tests` ASan and TSan
  lanes; Evidence 1's reopen-with-non-inert-state case runs under ASan specifically, since
  the v0.1.0 failure mode in release builds was *silent handle corruption* rather than an
  abort, and ASan is what would catch its return.

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed lines; tests
  ship with the task. clang-format + build clean.

## Decisions

- **D-slab_adopt-1 — The `.tji`'s literal plan is not implementable at v0.3.0; the leaf
  delivers the verified finding and a corrected guard instead of the prescribed change.**
  Calling `replay_recovered_content_state` requires `Model::recovered_content_state()`
  (`recovered_state_replay.hpp:44-53`, `model.hpp:329-331`). The editor holds an
  `arbc::Document`, which exposes no accessor for its `Model`; access is granted only to
  `HostViewportDocumentAccess` in `src/runtime/document_access.hpp`, a header the library
  marks *"PRIVATE to the component — it is not a public header, so no host can reach it."*
  `document.hpp:398-410` states the intent directly: no public `Model& model()`, because it
  *"would hand every host the unguarded model."* The library's own three tests all drive the
  replay from `Model::open`, never from a `Document`. There is no in-repo route to the
  input, and manufacturing one would mean forking libarbc.
  *Rationale:* the refinement's job is to turn a design promise into a testable work order;
  when the promise is unreachable, the honest work order is the verification that settles it
  plus whatever real improvement the evidence unlocks. Shipping a no-op that *looks* like
  adoption would leave A15 wrong and the latent map-path bug unowned — strictly worse than
  recording the finding.
  *Alternative rejected — open with `arbc::Model::open` instead of `arbc::Document::open`
  to reach the accessor.* `Model` is the unguarded layer beneath `Document`; a host-built
  `Document` over a host-opened `Model` is not constructible through any public API (the
  `Document(std::unique_ptr<Model>, …)` constructor is private, `document.hpp:411-413`), and
  the editor would lose `add_content`'s editable-facet registration, journal wiring, and
  captured-initial-state record. It is the exact bypass the library's attorney-client
  comment exists to prevent.
  *Alternative rejected — fork or vendor libarbc to add the accessor.* `CMakeLists.txt:25`
  pins a released tag by design (A1); a fork is cross-repo work with no owner in this leaf,
  and it would not help anyway, because of D-slab_adopt-2 and the deeper binding gap.

- **D-slab_adopt-2 — Do not register a camera `KindStateWalker`.** Two independent
  mismatches. (i) **Wrong store shape.** The walker's contract is *"the kind's own
  **document-level** state store"* (`registry.hpp:105-117`), passed as one `void*` per kind
  id via `RecoveredStateHooks::store_of` (`recovered_state_replay.hpp:31-34`).
  `CameraContent`'s slab is a **per-instance** `std::vector<Version>` + free list + `d_base`
  (`camera.hpp:97-111`), one table per camera object, holding heap `std::string`s. There is
  no per-kind, document-level object to hand back, and no way to route a per-`ObjectId`
  store through a per-kind hook. (ii) **Nothing durable to rebuild.** The walker's purpose is
  to *rebuild refcounts* in a slab whose bytes are themselves recovered from the workspace
  arena. The camera's bytes are not in the arena at all — A14 persists them through the
  **codec** into `project.arbc` (`camera.cpp:390-399`), and the runtime table is ordinary
  heap memory that ends with the process. A recovered `StateHandle{slot}` would index a
  freshly-constructed, empty version table.
  *Rationale:* the camera is a codec-persisted kind, not a workspace-slab-persisted one.
  That was A14's deliberate choice, and the walk hook is simply the wrong seam for it. A
  registered-but-unreachable walker would be dead code that reads, to the next maintainer,
  as a working capability.
  *Alternative rejected — promote `CameraContent`'s slab to a document-level store to fit
  the hook.* It would be a substantial redesign of `rename_stable_id`'s shipped
  `Editable` implementation, in service of a hook whose input the editor still could not
  reach (D-slab_adopt-1), for a reopen route that still would not bind the `Content`
  (D-slab_adopt-3). Three blockers deep, zero user-visible gain.

- **D-slab_adopt-3 — Keep A15's force-rebuild policy; replace its premise with the verified
  cause.** The policy is not merely still *needed* — it is needed for a **larger** reason
  than A15 states. The map path binds no `Content` for **any** kind
  (verified: `resolve` null and `for_each_content` zero for a non-editable `org.arbc.solid`
  and an editable `org.arbc.raster` alike), because `Document::open` takes no `Registry` and
  runs no factory (`document.hpp:76-85`). So the canonical rebuild is not a fallback for
  custom editable kinds; it is the **only** route that produces a usable document for a
  project with content.
  *Rationale:* pinning the real cause is what makes the guard safe to maintain. The current
  comment (`project_open.cpp:147-161`) tells the next reader the map path *aborts*; a reader
  who checks that against v0.3.0 finds it false and has every reason to delete the guard —
  which would silently empty every reopened project. Replacing a falsified justification is
  the highest-value line of code in this leaf.
  *Alternative rejected — retire the guard as the `.tji` asks.* It would return a document
  with a live record graph and zero live content: no cameras, no cells, nothing rendering,
  and `AppState` reporting it clean. Strictly worse than today, and it would present as data
  loss.

- **D-slab_adopt-4 — Extend the guard to no-callback callers by inspecting the mapped
  document, with the existing proxy kept as a short-circuit.** Today's condition
  (`register_extra_kinds && canonical exists`, `project_open.cpp:162`) leaves a real hole:
  a caller registering no editor kinds still maps a content-bearing workspace and receives a
  document whose every `resolve()` is null. That hole was invisible while the map was
  believed to abort. Because mapping is now provably non-aborting (Evidence 1), the guard
  can stop guessing: map, count the content records the binding table could not serve, and
  keep the mapped document only when that count is zero.
  *Rationale:* the callback was only ever a **proxy** for "custom editable content may be
  present" (D-slab-2's fail-safe reasoning under an aborting library). With inspection
  available the proxy is no longer the safety mechanism, and the guard becomes *correct*
  rather than *conservative* — it protects every caller and stops rejecting the fast path
  for workspaces that have nothing to lose.
  *Rationale for keeping the proxy as a short-circuit:* an editor-kind session with a
  canonical present will always reject the map, so mapping it first would open a
  potentially large workspace file only to discard it. Ordering the cheap predicate first
  costs nothing and preserves today's I/O profile on the common editor reopen.
  *Cost, accepted and pinned:* two incumbent tests (`camera_model_test.cpp:903`, and
  `project_open_test.cpp:152` if its fixture carries content) currently assert the map path
  is taken there. They are updated, and the replacements assert **live content** rather than
  only the `rebuilt_from_canonical` flag, so the suite cannot drift back to pinning an empty
  document.
  *Alternative rejected — leave the no-callback path alone as "not my scope".* It is one
  conjunct in the same branch this leaf is already rewriting, the evidence proving it broken
  is authored here, and no other scheduled leaf owns `open_project`. Deferring it means
  knowingly shipping a path this leaf proved returns an unusable document — the same
  "not my scope" choice `arbc_v030.md:594` identifies as why A15 was wrong for two releases.
  *Alternative rejected — always rebuild whenever a canonical exists, dropping the map path
  entirely.* Simpler, but it discards A13's crash-recovery of unpublished **record-level**
  edits (layer transforms, z-order, composition size) in the content-free case where the map
  is perfectly serviceable, and it would make `rebuilt_from_canonical` constant — collapsing
  a signal `AppState` and two tests depend on.

- **D-slab_adopt-5 — Close D-slab-3's never-saved residual as a reported degradation, not a
  silent loss and not an error.** With no canonical there is nothing to rebuild from, so the
  mapped document is kept and its cameras and cells are unrecoverable. At v0.1.0 this
  aborted; at v0.3.0 it opens looking empty. `OpenedProject` gains a count of content records
  the map could not bind, set only on the kept-mapped path, defaulting to zero.
  *Rationale:* the count is the honest close available in-repo — it cannot restore the
  camera (that needs a library seam, see Open questions), but it converts silent data loss
  into a fact the caller can act on, and it is L1-testable today. It rides `OpenedProject`
  because that is already the open path's result value, needs no new type, and no new edge.
  *Rationale for a value, not an `OpenError`:* the document **does** open, and every other
  route through `open_project` returns a usable session; failing the open would turn a
  degraded reopen into no reopen at all. Errors are values, and this is a value.
  *Alternative rejected — publish a canonical baseline at `create_project` so a rebuild
  always has a floor.* Beyond deviating from D-open's "no `project.arbc` until Save is the
  publish step" (`project.hpp:155-157`), it does not work: the camera is added *after*
  create, so an empty canonical floor loses it just as thoroughly. Making it work means
  re-dumping on every mutation — autosave — which is a product decision against D16's
  explicit "Save = re-dump" model. Surfaced to the parking lot, as D-slab-3 already did.
  *Alternative rejected — count only camera records.* `project` may not name a `scene` type
  (§8, Constraint 7), and the evidence shows the limitation is kind-agnostic; a
  camera-specific count would under-report exactly the cells the user also lost.

- **D-slab_adopt-6 — Doc delta: add `A19`, and amend `A15` with a one-line pointer.** The
  fact this leaf establishes is new and general: *the libarbc workspace-map reopen restores
  the record graph only and binds no `Content` for any kind, so a content-bearing project is
  reopenable only through the canonical rebuild; `KindStateWalker` does not change this and
  the editor registers none.* It lands as **A19** (`docs/01-architecture.md:380` is the
  current highest row, A18).
  *Rationale:* A15 is titled *"An editor-defined **editable** kind forces
  rebuild-from-canonical"* — filing a kind-agnostic, all-content fact under it would
  mis-shelve the very thing a future editor kind (or a future reader of the cell path) needs
  to find. This is the same argument **D-slab-4** used to keep A15 out of A14. A15 keeps its
  v0.1.0 text as the historical record of why the guard was first written, gaining one
  amendment line: its premise is retired, the "Future fix" clause is answered in the
  negative, and A19 now carries the operative rule.
  *Alternative rejected — amend A15 in place only, as `D-arbc_v030-6` did.* That amendment
  was a truth-up of stale version strings inside a row whose subject was unchanged. Here the
  *subject* changes — from "editable kinds" to "all content" — and A15's title would become
  a misnomer for its own content.
  *Alternative rejected — a `D<n>` row in `docs/00-design.md`.* Nothing user-visible
  changes in this leaf; the open-path policy has always lived in the architecture doc
  (A15/D-open-3), and `docs/00-design.md` D1-D24 own no open-route decision. The UI-visible
  half is the deferred notice task, which carries its own D-row question if it needs one.

- **D-slab_adopt-7 — Prove the limitation with a built-in kind alongside the camera.** Every
  Evidence criterion is asserted twice: once for a built-in cell kind and once for
  `org.arbc.camera`.
  *Rationale:* the single most likely misreading of this leaf is "cameras are special."
  They are not — the probe shows a non-editable `org.arbc.solid` reopens just as unbound as
  an editable raster. Pinning the built-in case is what makes A19's kind-agnostic claim a
  tested fact rather than a generalization from one sample, and it is what will catch a
  future libarbc release that fixes the binding for built-ins only.
  *Alternative rejected — test only the camera, as the leaf's area suggests.* It would leave
  A19 asserting more than the suite proves, which is the failure mode `arbc_v030.md`'s
  D-arbc_v030-6 explicitly refused to repeat.

- **D-slab_adopt-8 — No new golden, and golden invariance is the rendered-output
  assertion.** The change is a branch in `open_project` plus tests; no code path here
  produces pixels, and cameras render zero pixels by construction (A14). The six committed
  goldens must return byte-identical; a mismatch blocks the leaf rather than rebaselining it
  (inherited from `D-arbc_v030-7` / `D-focused_canvas_indicator-8`).
  *Rationale:* a new golden would assert nothing the existing six do not, while the
  invariance of the existing six is a real assertion that the open-path change did not
  perturb what a rebuilt document composites — which matters precisely because more reopens
  now take the rebuild route.

## Open questions

(none — all decided.) Three items are surfaced for the **parking lot** as cross-repo or
human-judgment work, **not** WBS leaves — none can be closed by an in-repo implementer, so
encoding them as tasks would spawn the self-perpetuating loop `tasks/refinements/README.md`
warns against:

1. **The library seam that would actually restore the workspace fast path (cross-repo,
   `ruoso/arbitrarycomposer`).** The map path binds no `Content` because
   `arbc::Document::open` takes no `Registry` and runs no factory. Restoring it needs either
   a registry-aware open (`Document::open(path, registry, bridge)` reconstructing each
   recovered `ContentRecord` through `registry.factory(kind_id)` plus a state codec) or a
   public rebind seam (`Document::rebind_content(ObjectId, std::shared_ptr<Content>)`) — and
   the library states there is deliberately no rebind API
   (`src/pool/arbc/pool/slot_store.hpp:119`: *"it never rebinds and there is no rebind
   API"*). A secondary, smaller ask: expose `recovered_content_state()` on `Document`, since
   `replay_recovered_content_state` is currently unreachable by any host that opens through
   `Document`. Until one of these lands, D-open-3's "durable-by-default fast path" cannot
   deliver a usable document for any project with content, and A15/A19's policy is
   permanent. Issue candidates for the library repo; no editor-side WBS task until the API
   exists (the same disposition the parking lot already gives
   `create_content_and_attach`).

2. **Whether the workspace-map fast path should remain in `open_project` at all.** After
   this leaf it survives only for content-free workspaces and the never-saved fallback —
   a narrow slice. Deleting it would simplify the open path and remove a branch that has
   never delivered its advertised benefit; keeping it preserves A13's recovery of
   unpublished record-level edits where that is harmless, and keeps the seam ready for item
   (1). This is a judgment about how much dead-ish machinery to carry against a possible
   library fix — a human call, not an implementer's.

3. **The never-saved camera-bearing project remains lossy (D-slab-3's residual, now
   *reported* rather than silent).** D-slab_adopt-5 closes the silence; it does not close
   the loss, and no in-repo change can (the create-time-canonical alternative is refuted
   above, and autosave contradicts D16). Whether the product should autosave, or should warn
   the user at camera-creation time in a never-saved project, is a design judgment.

**Named future task for the closer to register** (concrete, agent-implementable, wired
into `m9_editor` via its `editor.project` container):

- **`editor.project.reopen_degradation_notice`** — *0.5d* — surface `OpenedProject`'s
  unbindable-content count in the UI: read it through the existing `dock::ProjectGateway`
  POD seam and show a one-shot notice on open naming how many objects the unsaved workspace
  could not recover, with an ImGui Test Engine e2e driving it headless by widget id.
  `depends editor.cameras.reopen_slab_adopt`. Source-of-debt: this refinement.

## Status

**Done** — 2026-07-23.

- `src/project/project_open.cpp` — guard rewritten: condition broadened from `register_extra_kinds && canonical exists` to map-then-inspect (count unbound content records), with `register_extra_kinds` kept as a cheap short-circuit; 22-line comment at `:147-161` replaced with the verified cause (no `Content` binding because `Document::open` runs no factory); `OpenedProject::unbindable_content_records` field set on the kept-mapped path.
- `src/project/ace/project/project.hpp` — `OpenedProject` gains `size_t unbindable_content_records{0}`; `open_project` declaration doc-block updated to describe the new inspection logic.
- `docs/01-architecture.md` — A19 added (kind-agnostic workspace-map reopen restores record graph only, binds no `Content`; `KindStateWalker` does not change this); A15 amended with a one-line pointer to A19 and retirement of its v0.1.0 premise.
- `tests/arbc_pin_test.cpp` — four compile-time witnesses for the arbc#5 trio (`KindStateWalker`, `state_walker`, `recovered_content_state`, `replay_recovered_content_state`) + "a checkpointed NON-INERT StateHandle reopens through Document::open" (Evidence 1).
- `tests/project_open_test.cpp` — four new units: "restores the record graph but binds NO content", "rebuilds a content-bearing workspace even with no extra-kinds callback", "keeps the map fast path for a content-free workspace", "a never-saved content-bearing project keeps the map and REPORTS the loss".
- `tests/camera_model_test.cpp` — three Evidence 3 units: "binds no CAMERA content either", "without the callback a CAMERA-bearing project now rebuilds", "a never-saved CAMERA project reports the lost camera"; incumbents at `:903/:918/:933` retitled/re-founded.
- `tests/project_save_test.cpp` — "a content-bearing reopen with the workspace PRESENT is clean" (dirty-baseline knock-on, Constraint 6).
- `tests/canvas_nav_e2e_test.cpp` — `settle()` bounded to 10 s wall clock; prevents nav-aids' magnified-raster infinite-render loop from hanging the ImGui Test Engine's 60 s watchdog.
- `tasks/parking-lot.md` — three human-judgment entries: library seam for workspace fast path, whether the fast path should remain, magnified-raster idle defect.
