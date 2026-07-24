# editor.project.save_as_outcome — widen Save As to the outcome seam

## TaskJuggler entry

[`tasks/00-editor.tji`](../../00-editor.tji) — `task save_as_outcome` under
`editor.project` (the block beginning at `tasks/00-editor.tji:164`):

```
    task save_as_outcome "Widen save_as to the outcome seam so a failed publish or exec stops reporting a name refusal" {
      effort 0.5d
      allocate team
      depends !entry_outcome
      note "AppProjectGateway::save_as (project_gateway.cpp:194-218) has four documented failure modes (dock.hpp:128-130: invalid name · target exists · failed publish · failed exec) collapsed into one bool; the entry_outcome leaf's adapter maps all four onto refused_target, preserving today's exact string. Resolving honestly needs a fourth enumerator (publish_failed) and a richer result from commands::save_project_as, which is L1 work outside the entry-verbs scope. Source-of-debt: tasks/refinements/editor/entry_outcome.md. Design: D22/D27, arch A12/A13."
    }
```

Two citations in that note are stale and are corrected here (the note was
written against the pre-`dir_is_project` layout):

- `AppProjectGateway::save_as` is at **`src/app/project_gateway.cpp:207-231`**,
  not `:194-218`. Lines 194-218 now straddle the tail of `clean_up` (`:191-205`)
  and the head of `save_as`'s comment block.
- The four documented failure modes live at **`src/dock/ace/dock/dock.hpp:158-172`**
  (the `save_as` doc comment), not `:128-130` — `:124-136` is now the three
  entry verbs.

## Effort estimate

**0.5d**, as registered. The change is wide but shallow: one enumerator, one
L1 result type, one total switch in L4, one mapper case plus one defaulted
parameter in L3, and a re-signing sweep across eight `save_as` overrides. The
bulk of the half-day is the test re-vocabularization (five L1 cases, three L4
cases, one table test, one e2e), not the production edit.

## Inherited dependencies

**Settled** (all `complete 100`):

- `editor.project.entry_outcome` — the direct dependency and the
  source-of-debt. Shipped `dock::ProjectEntryOutcome`, the `entry_feedback`
  mapper, the widened `ComposeModalSpec::submit`, and the `refused_target`
  adapter this leaf deletes. Refinement:
  [`entry_outcome.md`](entry_outcome.md); doc row **A24**.
- `editor.project.dir_is_project` — shipped the parent-pick + typed-name
  compose flow for Save As, the synchronous `bool save_as(parent, name)`
  signature, the shared `draw_compose_target_modal`, and the one refusal
  string (`D-dir_is_project-6`). Refinement:
  [`dir_is_project.md`](dir_is_project.md); doc row **D27**.
- `editor.project.save_as` — shipped the three-layer split this leaf
  re-plumbs (`project::save_project_as` / `commands::save_project_as` /
  the gateway verb). Refinement: [`save_as.md`](save_as.md).
- `editor.project.save`, `editor.project.exec_new`, `editor.project.welcome`
  — the `platform::Result<SaveOutcome>` publish core, `open_another_project`,
  and the `ProjectEntryGateway` / `AppProjectGateway` split respectively.

**Pending**: none. Every seam this leaf touches is shipped and tested.

## What this task is

`dock::ProjectGateway::save_as(parent, name)` returns a `bool`
(`src/dock/ace/dock/dock.hpp:172`) whose own doc comment enumerates four
distinct ways it can be `false` — *"an invalid name, a target that already
exists, a failed publish, or a failed exec"* (`dock.hpp:170-171`). The
`entry_outcome` leaf widened the three **entry** verbs to
`dock::ProjectEntryOutcome` but left Save As, a **session** verb (A13),
on its `bool`, adapting it at the single L3 binding with
`gw->save_as(parent, name) ? succeeded : refused_target`
(`src/dock/dock.cpp:352-361`). That adapter was observably a no-op — all four
modes already rendered one string — and it is the debt this leaf pays.

This task makes the four modes tell the truth. It adds a fourth enumerator
`publish_failed` to `ProjectEntryOutcome`, re-signs `save_as` to return that
enum, and gives `commands::save_project_as` a result that carries **which
stage stopped** rather than only a bare `std::error_code` — because the error
channel alone cannot distinguish a canonicalize fault from a spawn failure
(both are `std::generic_category()` / `std::system_category()` codes). L4
`app` then maps the L1 stage onto the L3 outcome, exactly as it already maps
`GcOutcome` onto `GcSummary` (`src/app/project_gateway.cpp:191-205`), and the
shared `entry_feedback` mapper grows one case and one defaulted parameter so
each outcome renders one honest string.

Out of scope: cleaning up a partially-written copy after a failed publish
(deferred, named below), the entry verbs' behaviour (byte-identical after
this leaf), and any change to what a *refused* Save As says — `D27` /
`D-dir_is_project-6` settled that and this leaf pins it as a regression.

## Why it needs to be done

The shipped UI is actively misleading in two reachable cases, and one of them
is a loop the user cannot escape by reading the message:

1. **A failed publish reports a name problem.** `project::save_project_as`
   returns `SaveError::SerializeFailed` / `AssetWriteFailed` / `IoError`
   (`src/project/ace/project/save.hpp:33-37`) for a full disk, an unwritable
   parent, or a non-finite scalar in the document. Every one of those arrives
   at the rail as *"Enter a project name that does not already exist here."*
   Retyping the name cannot fix any of them.
2. **A failed `exec` reports a name problem — and then contradicts itself.**
   `commands::save_project_as` publishes the copy first and execs second
   (`src/commands/app_state.cpp:223-233`), so a spawn failure leaves a
   *complete, valid copy on disk* and returns the launcher's error. The user
   is told to type a different name. If they do the obvious thing and retry
   the same name, D27's existing-target guard now fires (`src/project/save.cpp:205`)
   and they get the same message for an entirely different reason — with a
   good copy sitting in the target directory the whole time.

The information to answer both already exists in L1: `app_state.cpp:226`
knows the publish failed and `:232` knows the exec failed. It is erased by the
`.has_value()` at `src/app/project_gateway.cpp:230`. A24's own text names this
as its one out-of-scope residue and names this leaf as the payer.

**Downstream.** `entry_outcome.md:174-178` records the standing instruction:
the WASM port (A3/A12) has a File System Access API where "refused" and
"could not launch" are structurally different failures, and this leaf
*"extends the same enum rather than inventing a second result convention."*
Whatever vocabulary lands here is the one every future host binds against.

## Inputs / context

**Design docs (normative).**

- `docs/00-design.md:483` — **D16** (project = a directory; Save = re-dump
  `project.arbc`; D16 §9 gives Save As "copy the directory").
- `docs/00-design.md:486` — **D19** (process-per-project; opening a project is
  a new `exec`).
- `docs/00-design.md:489` — **D22** (in-app New / Open / Recent; every entry
  point spawns a sibling; the seam sits behind A12).
- `docs/00-design.md:494` — **D27** (creation targets a path that does not
  exist; New and Save As share one compose gesture and *"render the same
  single refusal message"*). Scoped to **refusals** — it does not constrain
  what a non-refusal failure says, which is the room this leaf works in.
- `docs/01-architecture.md:369` — **A7** (one process, one project).
- `docs/01-architecture.md:374` — **A12** (project-entry actions
  dependency-inverted behind the L3-declared `dock::ProjectGateway`; `dock`
  may include neither `<ace/project/…>` nor `<ace/commands/…>`).
- `docs/01-architecture.md:375` — **A13** (the same gateway carries the
  in-session Save + dirty query; *"Save As … is its own leaf"*).
- `docs/01-architecture.md:386` — **A24** (project-entry outcomes are values;
  the enum's shape rules; the pure-virtual forcing function; the one L3
  mapper; and the explicit deferral: *"The session verbs (A13) are out of
  scope: `save_as` … is adapted at its one L3 binding … with the honest
  widening deferred to `editor.project.save_as_outcome`, which needs a fourth
  enumerator and a richer result from `commands::save_project_as`."*).
- `docs/01-architecture.md:255-291` — **§8** levelization DAG.
  `project` L1 (base, platform, libarbc), `commands` L1 (base, project,
  scene), `dock` L3 (dockmodel, views, imgui), `app` L4 (everything).
  Mirrored in `scripts/check_levels.py:21-40`.
- `docs/01-architecture.md:293-357` — **§9** the definition of done, and
  §9.1's `clang-asan` lane.

**Source seams.**

- `src/dock/ace/dock/dock.hpp:66-85` — the `ProjectEntryOutcome` rationale
  comment and the three enumerators. `:75` says *"Exactly three
  enumerators"* — that sentence is edited by this leaf.
- `src/dock/ace/dock/dock.hpp:87-99` — `entry_feedback`'s contract and
  declaration.
- `src/dock/ace/dock/dock.hpp:158-172` — the `save_as` doc comment (the four
  failure modes) and the pure virtual.
- `src/dock/ace/dock/dock.hpp:332-343` — `ComposeModalSpec`, including
  `submit` (`:338`) and `refused_message` (`:342`).
- `src/dock/dock.cpp:429-442` — `entry_feedback`'s definition (a `switch` with
  no `default:`, falling out to the launch-failure literal).
- `src/dock/dock.cpp:339-361` — the Save As binding and the standing comment
  naming this leaf. Both are deleted/replaced here.
- `src/dock/dock.cpp:456-498` — `draw_compose_target_modal`; the submit branch
  at `:469-489` (`feedback = entry_feedback(outcome, spec.refused_message);`
  at `:480`, `outcome == succeeded` closes at `:481`).
- `src/dock/dock.cpp:276-284` — the rail's `###save_as` trigger (parent pick →
  `open_save_as_modal`). Untouched.
- `src/dock/dock.cpp:330-332` — where `project_feedback()` is rendered.
- `src/app/ace/app/project_gateway.hpp:57` — `ProjectEntryGateway`'s inert
  `save_as` override; `:110` — `AppProjectGateway::save_as`'s declaration.
- `src/app/project_gateway.cpp:207-231` — `AppProjectGateway::save_as`, ending
  in the `.has_value()` at `:230` that erases the discriminant.
- `src/app/project_gateway.cpp:44-102` — `spawn` and the three entry verbs,
  the shape this leaf mirrors.
- `src/app/project_gateway.cpp:191-205` — `clean_up`, the existing
  L1-vocabulary → dock-vocabulary re-mapping precedent (`GcOutcome` →
  `GcSummary`).
- `src/commands/ace/commands/app_state.hpp:319-341` — `save_project_as`'s doc
  comment and declaration.
- `src/commands/app_state.cpp:192-236` — `save_project_as`'s four failure
  exits: empty target `:199-201`, canonicalize fault `:211-213`, publish
  failure `:225-227`, exec failure `:231-233`.
- `src/commands/exec_new.cpp:5-28` — `open_another_project`; returns a bare
  `std::error_code`, empty == success.
- `src/project/ace/project/save.hpp:33-37` — `SaveError`; `:44-49` —
  `SaveOutcome`; `:140-144` — `save_project_as`'s declaration.
- `src/project/save.cpp:190-242` — `save_project_as`: the D27 guard at `:205`
  (`std::errc::file_exists`), the forwarded publish error at `:230-232`, the
  gitignore fault at `:237-239` (`SaveError::IoError`, **after** a successful
  publish).
- `src/project/save.cpp:30-50` — the `"ace.project.save"` error category.
- `src/platform/ace/platform/result.hpp:14-17` — `platform::Result<T>`, a
  hand-rolled `expected`-alike over `std::error_code`; it holds *either* a
  value or an error, which is the structural reason it cannot carry a stage
  alongside a failure.

**Predecessor decisions this leaf is bound by.**

- `D-entry_outcome-1` — the enum is dock-local, adds no include, splits by
  corrective act.
- `D-entry_outcome-2` — `refused_target` is one outcome for every pre-launch
  refusal; never split by cause.
- `D-entry_outcome-3` — the verbs stay **pure** virtuals; the compile break at
  every override *is* the deliverable.
- `D-entry_outcome-4` — one shared L3 mapper; host divergence must stay
  impossible, not merely fixed.
- `D-entry_outcome-7` — the Save As adapter is explicitly a placeholder for
  this leaf.
- `D-dir_is_project-2` — two error vocabularies over one `std::error_code`
  channel (`SaveError` + `std::errc`) is house style.
- `D-dir_is_project-6` — one refusal string for both compose failures; the
  settled answer, not a stopgap.
- `D-save_as-2` — Save As leaves the current session untouched; no
  `mark_saved`, no `layout_` rebind, on any path.
- `D-welcome-8` — `WelcomeWindow::exit_requested()` is spelled
  `== succeeded`; a fourth enumerator must not make that predicate wrong.

## Constraints / requirements

**1. `ProjectEntryOutcome` gains exactly one enumerator, appended.**

```cpp
enum class ProjectEntryOutcome {
  succeeded,      // validated AND spawned: the sibling editor exists
  refused_target, // declined before launching anything (not a project / no longer a project /
                  // an invalid name / a target that already exists) — nothing was spawned
  publish_failed, // the target was accepted and producing the copy failed — Save As only; the
                  // entry verbs publish nothing, so no entry verb can return this
  spawn_failed,   // the target was accepted (and, for Save As, the copy is on disk) and the
                  // sibling `exec` failed
};
```

Appended, not inserted, so no existing enumerator's meaning shifts. The enum
**keeps its name** and its home in `dock.hpp`'s dock-local-exchange family;
the `:66` header comment widens from *"What ONE project-entry verb did"* to
cover the one session verb that also creates a project directory, and the
*"Exactly three enumerators"* sentence at `:75` becomes four with the
corrective-act rule restated intact.

**2. `save_as` returns the outcome and stays a pure virtual.**

```cpp
virtual ProjectEntryOutcome save_as(const std::filesystem::path& parent,
                                    const std::string& name) = 0;
```

Per `D-entry_outcome-3`, no defaulted override: the return-type change must
be a hard compile error at `ProjectEntryGateway`'s inert override
(`src/app/ace/app/project_gateway.hpp:57`), at `AppProjectGateway` (`:110`),
and at every test fake that overrides `save_as`. The `dock.hpp:158-172` doc
comment's closing sentence (*"False means nothing was published and nothing
was spawned — an invalid name, a target that already exists, a failed
publish, or a failed exec"*) is replaced by a per-enumerator mapping,
including the fact that `spawn_failed` here means **the copy exists**.

**3. `commands::save_project_as` returns a stage, not a bare `Result`.**

New, in `src/commands/ace/commands/app_state.hpp` beside the declaration:

```cpp
// How far Save As got before it stopped. `platform::Result<SaveOutcome>` cannot answer this:
// it holds EITHER a value OR a `std::error_code`, and the codes overlap across stages — a
// canonicalize fault and a `spawn_detached` fault are both plain system/generic codes, so no
// caller can tell "nothing was written" from "the copy is on disk but nothing launched".
enum class SaveAsStage {
  refused,        // a bad target the user can retype: nothing written, nothing spawned
  publish_failed, // the target was not refused, but no usable copy was produced
  spawn_failed,   // the copy IS on disk; the sibling `exec` failed
  spawned,        // the copy is on disk AND the sibling editor is running
};

struct SaveAsResult {
  SaveAsStage stage = SaveAsStage::refused;
  std::error_code error;            // empty iff `stage == spawned`
  project::SaveOutcome published{}; // meaningful iff the copy reached disk (spawn_failed/spawned)
};

SaveAsResult save_project_as(AppState& state, const platform::FileSystem& fs,
                             const platform::ProcessLauncher& launcher,
                             const std::filesystem::path& executable,
                             const std::filesystem::path& target_root,
                             const project::WriterPost& post_writer = {});
```

The four exits of `src/commands/app_state.cpp:192-236` map as:

| exit | line | stage | error |
|---|---|---|---|
| `target_root.empty()` | `:199-201` | `refused` | `std::errc::invalid_argument` |
| canonicalize fault | `:211-213` | `publish_failed` | the `ec` from `absolute`/`weakly_canonical` |
| publish failed, `error() == std::errc::file_exists` | `:225-227` | `refused` | `std::errc::file_exists` |
| publish failed, any other error | `:225-227` | `publish_failed` | the forwarded `SaveError` code |
| exec failed | `:231-233` | `spawn_failed` | the launcher's `exec_ec` |
| success | `:235` | `spawned` | `{}` |

The `file_exists`-versus-`SaveError` split happens **in L1 `commands`**,
because that is the level that owns both vocabularies (`D-dir_is_project-2`)
and can name them without an illegal include. A canonicalize fault is
`publish_failed`, not `refused`: nothing the user retypes resolves it, and
"could not save a copy there" is truthful (nothing was saved there). This is
the corrective-act rule of `D-entry_outcome-2` applied one level down.

`platform::Result<SaveOutcome>` remains the return type of
`project::save_project_as` and of `commands::save_project` — this constraint
changes exactly one function.

**4. L4 maps the stage to the outcome with a total switch.**

`AppProjectGateway::save_as` keeps its `compose_new_project_target` guard
(`src/app/project_gateway.cpp:223-227`) returning `refused_target` — an
empty/blank/traversing name is a retype, and it still never touches
`commands` — then switches over `result.stage` with a case per enumerator and
no `default:`, so a future stage is a compiler diagnostic. `refused →
refused_target`, `publish_failed → publish_failed`, `spawn_failed →
spawn_failed`, `spawned → succeeded`. This is the `clean_up` re-mapping
pattern (`project_gateway.cpp:191-205`), where an L1 POD is translated into
the dock's own vocabulary at the only level allowed to see both.

`ProjectEntryGateway`'s inert override answers `refused_target` — the
established honest inert answer (`D-entry_outcome-3`'s `InertGateway`
precedent): a session-free launcher has no session to copy.

**5. `entry_feedback` gains one case and one defaulted parameter.**

```cpp
const char* entry_feedback(ProjectEntryOutcome outcome, const char* refused_message,
                           const char* spawn_failed_message = "Could not start the editor.");
```

- `succeeded` → `""` (unchanged).
- `refused_target` → `refused_message` (unchanged).
- `publish_failed` → the mapper's own literal **`"Could not save a copy there."`**,
  regardless of either caller string. Save-As-only in practice, so it is not
  a caller parameter — one literal, one place, per `D-entry_outcome-4`.
- `spawn_failed` → `spawn_failed_message`, whose **default is the unchanged
  `"Could not start the editor."` literal**.

The defaulted parameter is what keeps `D-entry_outcome-4` intact rather than
reopening it. That decision made host divergence for *one verb* impossible;
it did not say every verb must say the same thing. "What a failed launch
means" is per-verb context in exactly the way `refused_message` already is
(`dock.hpp:91-92`: *"Open says one thing, Recent another"*) — and for Save As
it differs in a load-bearing way, because the copy is on disk. Every call
site that does not opt in gets the one literal, so divergence-by-accident
stays impossible.

**6. `ComposeModalSpec` carries the spawn string; the adapter is deleted.**

`ComposeModalSpec` gains `const char* spawn_failed_message = "Could not start the editor.";`
after `refused_message`, and `draw_compose_target_modal` passes it:
`feedback = entry_feedback(outcome, spec.refused_message, spec.spawn_failed_message);`
(`src/dock/dock.cpp:480`). The Save As binding at `dock.cpp:352-361` drops the
ternary lambda and binds the seam directly, using **designated initializers**
(the spec is aggregate-initialized positionally today, and Save As relies on
`refused_message`'s default):

```cpp
  {
    ProjectGateway* gw = &gateway;
    (void)draw_compose_target_modal(
        dockspace.save_as_modal(), dockspace.project_feedback(),
        ComposeModalSpec{
            .popup_id = "Save Project As",
            .submit_label = "Save Copy",
            .submit = [gw](const std::filesystem::path& parent,
                           const std::string& name) { return gw->save_as(parent, name); },
            .spawn_failed_message = "Saved the copy, but could not start the editor.",
        });
  }
```

The standing comment at `dock.cpp:345-351` (which names this leaf as the
payer) is deleted, not amended — its whole subject was the adapter. New's
binding is untouched and takes the default for both strings.

**7. The modal-close policy is unchanged: `succeeded` closes, everything else
stays open.** `dock.cpp:481`'s `outcome == ProjectEntryOutcome::succeeded`
check is not touched, and the comment at `:485-488` (which already reasons
that closing on a non-success *"would throw away the parent the async pick
resolved"*) extends to the two new cases without edit. For `publish_failed`
the retry may genuinely succeed once the disk problem is fixed; for
`spawn_failed` the message now tells the user the copy is safe, so Cancel is
an informed choice rather than a guess. Forking the close policy per outcome
would put New's and Save As's needs in conflict inside one shared modal.

**8. The entry verbs are byte-identical after this leaf.** New, Open and
Recent produce the same four strings in the same situations. `publish_failed`
is unreachable from `ProjectEntryGateway::spawn` and the three entry verbs;
`spawn_failed_message` defaults to the shipped literal at all four of their
call sites (`src/dock/dock.cpp` Open/Recent, `src/dock/welcome.cpp:69,:87`).
`WelcomeWindow::exit_requested()`'s `== succeeded` predicate
(`src/dock/ace/dock/welcome.hpp:53-61`) is correct unchanged — a fourth
non-success enumerator only adds another way for it to stay false.

**9. Levelization: no new include, no new edge, `check_levels` unmodified.**
`SaveAsStage`/`SaveAsResult` are `commands`-local and name only
`std::error_code` and `project::SaveOutcome` — both already visible there
(`commands` → `project` is a legal L1 edge, `scripts/check_levels.py:31`).
`publish_failed` and the `const char*` parameter add nothing to `dock`'s
include set (`scripts/check_levels.py:35` keeps `dock` at
`{dockmodel, views}` + imgui). `app` (L4) already includes both sides. No
component is added and no external dependency is introduced.

**10. Every `save_as` override is re-signed.** The inert L4 override
(`src/app/ace/app/project_gateway.hpp:57`), `AppProjectGateway` (`:110`), and
the e2e fakes in `tests/save_as_ui_e2e_test.cpp:68-71`,
`tests/open_ui_e2e_test.cpp`, `tests/save_ui_e2e_test.cpp`,
`tests/welcome_e2e_test.cpp`, `tests/reopen_degradation_notice_e2e_test.cpp`
and the remaining `ProjectGateway` fakes. Per `D-entry_outcome-3` the sweep is
the deliverable: a stale `bool … override` must not compile.

## Acceptance criteria

**Levelization.** `scripts/check_levels.py` clean and **unmodified**. No new
component, no new DAG edge, no new external dependency. The L1 core gains no
ImGui/GL/SDL include, and `dock` still includes neither `<ace/project/…>` nor
`<ace/commands/…>` — the discrimination happens in `commands` (L1) and is
translated in `app` (L4), which is the only level allowed to see both
vocabularies.

**L1 Catch2 units — `tests/save_as_test.cpp` (target `ace_tests`).** This is
the bulk of the coverage and where the leaf's substance lives.

Re-vocabularized (behaviour pinned unchanged, assertions restated on
`SaveAsResult`):

- `:303` `"commands::save_project_as canonicalizes the target, publishes, and execs the sibling"`
  → `stage == SaveAsStage::spawned`, `!error`, and `published.revision`
  carrying the publish's revision.
- `:340` `"commands::save_project_as rejects an empty target without publishing or spawning"`
  → `stage == refused`, `error == std::errc::invalid_argument`,
  `launcher.invoked == false`.
- `:356` `"commands::save_project_as short-circuits a publish failure and never execs"`
  → `stage == publish_failed`, `launcher.invoked == false`.
- `:379` `"commands::save_project_as short-circuits a refused target and never execs"`
  → `stage == refused`, `error == std::errc::file_exists`,
  `launcher.invoked == false`, target still bare.
- `:410` `"commands::save_project_as leaves the published copy on disk when the exec fails"`
  → `stage == spawn_failed`, the copy's `project.arbc` present, `published`
  carrying the revision, and `state.layout().canonical` still the original
  root (`D-save_as-2`).

New — **the case that is this leaf**:

- `TEST_CASE("commands::save_project_as separates a refused target from a publish fault")`,
  two SECTIONs over the one `published.error()` exit at `app_state.cpp:225-227`:
  an existing target ⇒ `refused` (+ `std::errc::file_exists`), and an
  unwritable target root (the `save_as_test.cpp:260` pattern, yielding a
  `SaveError` in the `"ace.project.save"` category) ⇒ `publish_failed`. Both
  assert `launcher.invoked == false`. This pins the discrimination the bare
  `std::error_code` erased.
- `TEST_CASE("commands::save_project_as reports a publish fault after a clean publish")`
  — the gitignore-write fault (`src/project/save.cpp:237-239`, the
  `save_as_test.cpp:279` pattern) ⇒ `publish_failed` and no exec, pinning that
  a post-publish write fault is a fault and not a refusal.

Unchanged and re-run as regressions: `:169`, `:214`, `:260`, `:279`, `:429`.

**Rendered output — golden.** No new golden and none needed: this leaf
changes no pixels, no widget id and no layout. The existing byte-exact
`render_offline` golden at `tests/save_as_test.cpp:429`
(`"save_project_as's copy reloads and renders byte-exact against the probe
golden"`, against `tests/goldens/render_probe_64x64.rgba8`) re-runs unmodified
as the regression that the success path still publishes a faithful copy. This
is the justified N/A, not a skipped criterion.

**L3 pure-function unit — `tests/welcome_e2e_test.cpp:385` (target
`ace_shell_test`).** `TEST_CASE("entry_outcome: entry_feedback maps each
outcome to one inline string")` grows from a 3×2 table to cover four
enumerators and the new parameter:

- `succeeded` → `""` and `refused_target` → the refusal string, each asserted
  independent of **both** other strings (unchanged rows, now also pinning that
  `spawn_failed_message` does not leak into them).
- `publish_failed` → `"Could not save a copy there."` regardless of the
  refusal string *and* regardless of an overridden `spawn_failed_message` —
  the row that pins the mapper, not the caller, as its owner.
- `spawn_failed` with the parameter omitted → `"Could not start the editor."`
  (the regression that every entry-verb call site is byte-identical).
- `spawn_failed` with the parameter supplied → that string verbatim.

**L4 headless units — `tests/app_project_gateway_test.cpp`, tag
`[app_project_gateway]` (target `ace_shell_test`).**

- `:611` `"AppProjectGateway::save_as composes parent + name, publishes a copy, and execs a sibling"`
  → `== ProjectEntryOutcome::succeeded`.
- `:645` `"AppProjectGateway::save_as refuses an existing target and an invalid name without publishing or spawning"`
  → `== refused_target` for the taken name and for each of
  `{"", "  ", "bad/name", ".."}`, still asserting nothing published and
  nothing spawned. The regression that this leaf does **not** change what a
  refusal means.
- `:250` `"a session-free gateway answers every session verb inertly"` →
  the inert `save_as` now asserts `== refused_target`.
- New `TEST_CASE("AppProjectGateway::save_as reports publish_failed when the copy cannot be written")`
  — an unwritable parent, asserting `publish_failed` and
  `launcher.invoked == false`.
- New `TEST_CASE("AppProjectGateway::save_as reports spawn_failed and leaves the copy on disk")`
  — the `FailingLauncher` pattern (`tests/save_as_test.cpp:108-116`, reused at
  `app_project_gateway_test.cpp:346`), asserting `spawn_failed`, that the
  copy's `project.arbc` exists, and that `state.layout()` is untouched
  (`D-save_as-2`).

**UI e2e — `tests/save_as_ui_e2e_test.cpp`, `IM_REGISTER_TEST(engine,
"save_as", "rail_save_as")` (target `ace_shell_test`).** `FakeGateway`'s
`bool save_as_ok` (`:45`) becomes
`ProjectEntryOutcome save_as_result = ProjectEntryOutcome::succeeded;` and the
override at `:68-71` returns it. Driven headless by widget id
(`"Save Project As/Name"`, `"Save Project As/Save Copy"`):

- Step 3 (`:144-157`) keeps its exact assertion —
  `project_feedback() == "Enter a project name that does not already exist here."`,
  modal still open — as the regression pin on the refused case.
- New step: `publish_failed` ⇒
  `project_feedback() == "Could not save a copy there."`, modal still open
  (`save_as_modal_open()`), and explicitly **not** the refusal string.
- New step: `spawn_failed` ⇒
  `project_feedback() == "Saved the copy, but could not start the editor."`,
  modal still open, and explicitly not the refusal string — the exact bug this
  leaf exists to kill.

No new screenshot baseline: the rail and modal geometry are unchanged and the
signal here is the string, which the assertions carry directly.

**UI e2e regressions (unmodified, must stay green).**
`IM_REGISTER_TEST(engine, "welcome", "three_verbs_and_feedback")`
(`tests/welcome_e2e_test.cpp:150`) and
`IM_REGISTER_TEST(engine, "open_ui", "rail_project_section")`
(`tests/open_ui_e2e_test.cpp:118`) — including their
`"Could not start the editor."` steps — pass with only their fakes' `save_as`
overrides re-signed. Any change to their asserted strings means Constraint 8
was violated.

**Threading.** No new threading surface: `save_as` is synchronous on the UI
thread (`dock.hpp:163`) and reaches the writer only through the existing
`writer_post_` seam, unchanged. The `clang-asan` lane (§9.1) covers
`ace_shell_test` including the new e2e steps; no TSan-specific work is added
or removed.

**Build & coverage.** `scripts/gate` clean (levels · format · build · ctest),
clang-format clean, and ≥90% diff coverage on the changed lines — every new
branch above has a named test, so the gate is met by the tests shipping in
this commit, not a follow-up.

**Doc delta.** A new **A25** row in `docs/01-architecture.md` (see Decisions),
landing in the same commit.

**One follow-up WBS task is deferred**, and only one:

- `editor.project.save_as_rollback` — **1d**, `depends !save_as_outcome` —
  *"Remove a partially published Save As target when the publish fails, so a
  retry with the same name is not blocked by D27's existing-target
  refusal."* `project::save_project_as` creates the target tree before it can
  fail (`src/project/save.cpp:195-239`): a `SaveError` from the publish
  (`:230`) or from the gitignore write (`:237`) leaves a directory behind, and
  D27's guard (`:205`) then refuses the same name forever. This leaf makes the
  *message* honest (`"Could not save a copy there."`); making the *state*
  recoverable needs a recursive-remove faculty on `platform::FileSystem`
  (native impl + test double + the A3 WASM seam), which is a new L0 method and
  outside a 0.5d reporting change. Deferred to `editor.project.save_as_rollback`
  (closer registers in WBS, under `editor.project`, which
  `milestones.m9_editor` already depends on).

## Decisions

**D-save_as_outcome-1 — Extend `ProjectEntryOutcome` with a fourth
enumerator; do not mint a second result type for session verbs.**
`publish_failed` is appended to the existing dock-local enum and `save_as`
joins the three entry verbs on it. *Rationale:* `entry_outcome.md:174-178`
made this the standing instruction (*"extends the same enum rather than
inventing a second result convention"*), the compose modal already carries one
`std::function<ProjectEntryOutcome(...)>` that both New and Save As bind, and
a second enum would force `draw_compose_target_modal` to be templated or
duplicated over two vocabularies that agree on three of four cases. The enum's
*name* is kept despite now covering one session verb, because A24 cites it by
name and renaming would churn eleven fakes for a comment-level gain; the `:66`
comment is widened instead. *Rejected:* a `SessionVerbOutcome` beside it —
symmetric names, but the shared modal would need both, and `succeeded` /
`refused_target` / `spawn_failed` would mean exactly the same thing in each.
*Rejected:* reusing `spawn_failed` for a failed publish — the corrective acts
differ (fix the disk vs. open the copy yourself) and the copy's existence
differs, which is the whole point of `D-entry_outcome-2`'s corrective-act
rule.

**D-save_as_outcome-2 — `commands::save_project_as` returns an explicit
`SaveAsResult { stage, error, published }`, not a `platform::Result` the
caller sniffs.** *Rationale:* the discriminant is genuinely absent from the
error channel, not merely inconvenient to read. `platform::Result<T>`
(`src/platform/ace/platform/result.hpp:14-17`) holds *either* a value or an
error, so a failed call structurally cannot also report that the copy reached
disk. And sniffing does not work even where it seems to: the canonicalize
fault at `app_state.cpp:211-213` and the `spawn_detached` fault at `:231-233`
are both plain system/generic `error_code`s, so "nothing was written" would be
reported as *"Could not start the editor."* — a new wrong message replacing
the old one. An explicit stage also makes L4's mapping a total `switch` that a
future stage breaks at compile time. *Rejected:* keep `platform::Result` and
add a `commands::SaveAsError::SpawnFailed` code so L4 discriminates by error
value — smaller, and it stays on the house `SaveError` + `std::errc` idiom
(`D-dir_is_project-2`), but the mapping degenerates to an `else` fallthrough
into `publish_failed` that silently swallows any error added later, and it
still cannot express "spawned" as anything but the absence of an error.
*Rejected:* a `SaveAsStage*` out-parameter alongside the existing return —
zero churn at the four L1 test call sites, but it contradicts *"errors are
values"* (A12) and makes the stage optional to read, which is precisely the
failure mode this leaf is paying off. *Rejected:* have L4 sniff
`error.category().name() == "ace.project.save"` — couples L4 to an L1 category
*string* and still leaves the canonicalize/spawn ambiguity unresolved.

**D-save_as_outcome-3 — The refused-versus-fault split happens in L1
`commands`; L4 only translates vocabularies.** `app_state.cpp` classifies
`published.error() == std::errc::file_exists` as `refused` and everything else
as `publish_failed`. *Rationale:* `commands` (L1) is the level that owns both
error vocabularies — `std::errc` from its own guards and `SaveError` from
`project` — and `D-dir_is_project-2` already blessed exactly that pairing on
one channel. Pushing the split up to L4 would mean `app` re-deriving
knowledge L1 already has; pushing it down into `project::save_project_as`
would mean an L1 primitive knowing about the exec stage it does not perform.
L4's job is then the one thing only L4 can do — naming an L1 type and a dock
type in the same function — which is precisely the `clean_up` /
`GcOutcome`→`GcSummary` precedent at `project_gateway.cpp:191-205`.
*Rejected:* a single unified error enum spanning `project` and `commands` —
one vocabulary, but it makes every `project` caller link against a `commands`
concept and reverses the DAG edge.

**D-save_as_outcome-4 — A canonicalize fault is `publish_failed`, not
`refused`.** *Rationale:* `D-entry_outcome-2`'s rule is split by corrective
act. `refused` means "retype the name and it may work"; a path that cannot be
resolved is not fixed by retyping, and *"Could not save a copy there."* is
literally true (nothing was saved there). The branch is defensively
unreachable in practice — `compose_new_project_target` yields a non-empty
path and `weakly_canonical` succeeds on non-existent paths — so this decision
is about which bucket is *honest*, not which is hot. *Rejected:* `refused`,
on the grounds that nothing was written — but "was anything written" is not
the question the message answers; "what should the user do next" is.

**D-save_as_outcome-5 — `publish_failed`'s string is owned by the mapper;
`spawn_failed`'s becomes a defaulted caller parameter.** *Rationale:* the two
new strings have different natures. *"Could not save a copy there."* is
Save-As-only and context-free, so it lives beside *"Could not start the
editor."* as a literal the mapper owns — one place, per `D-entry_outcome-4`.
The launch-failure string is different: after a Save As the copy **is on
disk**, and telling the user only *"Could not start the editor."* leaves them
one obvious retry away from a `file_exists` refusal that re-reports the very
name-problem this leaf exists to stop reporting. That makes "what a failed
launch means here" per-verb context in exactly the sense `dock.hpp:91-92`
already grants `refused_message` (*"Open says one thing, Recent another"*).
`D-entry_outcome-4` made divergence between two **hosts of one verb**
impossible; a default-valued parameter keeps that intact — every call site
that does not opt in gets the single literal, and exactly one call site opts
in — while letting one verb say the one additional true thing it knows.
*Rejected:* leave `spawn_failed` alone and accept the shared literal — smaller
by one field, but it ships the retry loop described under "Why it needs to be
done" with a message that is incomplete rather than wrong. *Rejected:* make
both strings caller parameters for symmetry — three `const char*`s on
`ComposeModalSpec` and three arguments at every call site, reintroducing the
per-host string duplication A24 removed, to configure a string only one verb
can ever produce.

**D-save_as_outcome-6 — The modal-close policy is untouched: `succeeded`
closes, every other outcome leaves the modal open.** *Rationale:*
`draw_compose_target_modal`'s existing reasoning (`dock.cpp:485-488`) — that
closing on a non-success throws away the parent the async pick resolved —
holds for both new outcomes, and the honest strings make staying open
informative rather than confusing. Forking the policy per outcome would put
New (which wants `spawn_failed` to keep the typed name) and Save As in
conflict inside one shared modal, reopening the coupling
`D-dir_is_project-5` closed. *Rejected:* close on `spawn_failed` for Save As
because retrying the same name can only fail — true, but the fix for that is
the message (D-save_as_outcome-5) and, structurally,
`editor.project.save_as_rollback`; a per-outcome close policy on a shared
modal is a worse price.

**D-save_as_outcome-7 — Doc delta is a new `A25` row refining `A24`, not an
edit to `A24` and not a `D` row.** *Rationale:* the house pattern in
`docs/01-architecture.md` is that a later row **refines** an earlier one in
place of rewriting it — A22 *"A7 is refined, not broken"*, A23 *"reverses
`D-save-2`'s projection"*, A24 *"refines A12's 'errors are values' clause"*.
A24's text is the historical record of what `entry_outcome` shipped, including
its own explicit deferral to this leaf; editing that clause away would erase
why the debt existed. It is an `A` row rather than a `D` row because the
change is structural — a fourth enumerator, a session verb joining an L3-declared
seam, and a new L1 result type — and because D27's user-visible rule is about
the **refusal** message, which this leaf leaves byte-identical; the two new
strings are the mechanical consequence of the new enumerator, not a new UX
policy. *Rejected:* amend A24 in place — one row instead of two, but it
rewrites shipped history and breaks the refine-don't-rewrite pattern every
other row follows. *Rejected:* add a `D28` for the two new failure strings —
defensible if the wording were the decision, but the decision recorded here is
*which outcomes exist*; the strings follow from it, and D27's refusal rule is
untouched.

## Open questions

(none — all decided)

## Status

**Done** — 2026-07-24.

- Added `publish_failed` enumerator to `ProjectEntryOutcome`; updated rationale comment (`src/dock/ace/dock/dock.hpp`)
- Re-signed `ProjectGateway::save_as` to return `ProjectEntryOutcome`; added `spawn_failed_message` defaulted parameter to `entry_feedback` and `ComposeModalSpec` (`src/dock/ace/dock/dock.hpp`, `src/dock/dock.cpp`)
- Save As binding in `src/dock/dock.cpp` uses designated initializers with `spawn_failed_message = "Saved the copy, but could not start the editor."`; standing adapter comment deleted
- New `SaveAsStage`/`SaveAsResult` in `src/commands/ace/commands/app_state.hpp`; `commands::save_project_as` returns the stage with `file_exists`-vs-`SaveError` split in L1 (`src/commands/app_state.cpp`)
- `ProjectEntryGateway::save_as` inert override answers `refused_target`; `AppProjectGateway::save_as` maps stage with total default-free switch (`src/app/ace/app/project_gateway.hpp`, `src/app/project_gateway.cpp`)
- New Catch2 L1 tests in `tests/save_as_test.cpp`: separates refused target from publish fault (2 SECTIONs); reports publish fault after clean publish
- New Catch2 L4 tests in `tests/app_project_gateway_test.cpp`: `publish_failed` and `spawn_failed` cases; L3 table in `tests/welcome_e2e_test.cpp` grown to 4 enumerators × new parameter
- UI e2e in `tests/save_as_ui_e2e_test.cpp`: two new steps for `publish_failed` and `spawn_failed` outcome strings
- Fake re-signing sweep: `tests/cells_remove_e2e_test.cpp`, `tests/frame_selection_e2e_test.cpp`, `tests/gc_ui_e2e_test.cpp`, `tests/new_shot_from_view_e2e_test.cpp`, `tests/open_ui_e2e_test.cpp`, `tests/reopen_degradation_notice_e2e_test.cpp`, `tests/save_ui_e2e_test.cpp`, `tests/undo_ui_e2e_test.cpp`
- `docs/01-architecture.md` A25 row (pre-existing uncommitted delta, left as-is)
- Deferred: `editor.project.save_as_rollback` (1d) — remove partially published Save As target on publish failure so a retry is not blocked by D27
