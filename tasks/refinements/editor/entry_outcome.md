# editor.project.entry_outcome — Distinguish refused-target from failed-spawn at the ProjectGateway seam

## TaskJuggler entry

`tasks/00-editor.tji:157-162`:

```
    task entry_outcome "Distinguish refused-target from failed-spawn at the ProjectGateway seam" {
      effort 0.5d
      allocate team
      depends !welcome
      note "Replace the entry verbs' single bool return with a small dock-local outcome POD
            (e.g. ProjectEntryOutcome: succeeded / refused_target / spawn_failed) so
            dock::WelcomeWindow and dock::Dockspace can surface 'That folder is not a project.'
            vs 'Could not start the editor.' without inferring spawn failure from MRU state.
            Source-of-debt: tasks/refinements/editor/welcome.md (D-welcome deviation: Constraint 6
            inference). Design: D22, arch A12."
    }
```

Milestone: `milestones.m9_editor` depends on the `editor.project` container
(`tasks/99-milestones.tji:8`), so this leaf and any follow-up registered under
`editor.project` are wired by containment — no milestone `depends` edit is needed.

**Closer ritual** (`tasks/refinements/README.md:47-78`): add `complete 100`
immediately after the `allocate team` line, append
`Refinement: tasks/refinements/editor/entry_outcome.md` to the end of the `note`,
register the deferred follow-up named under Acceptance criteria as a real WBS leaf,
then run `tj3 project.tjp 2>&1 | grep -iE "error|warning"` and confirm silence.

## Effort estimate

**0.5d**, as estimated in the `.tji`. Breakdown:

- One scoped enum + one free mapping function declared in `src/dock/ace/dock/dock.hpp`
  and defined in `src/dock/dock.cpp` (~35 lines including the doc comments this
  codebase writes). **~0.1d**
- Four L3 call sites collapse from an `if/else` pair into one assignment
  (`dock.cpp:310-314`, `:326-330`; `welcome.cpp:86-91`, `:105-110`), and
  `welcome.cpp:22-43`'s `refusal_feedback` helper is **deleted** — a net line
  reduction. **~0.1d**
- L4: three verb bodies and the private `spawn` change return type in exactly one
  place (`src/app/project_gateway.cpp:44-89`), because `D-welcome-6` already made
  `ProjectEntryGateway` the single implementation. **~0.1d**
- Eleven test fake gateways re-sign three overrides each; eight of them are
  one-line `{ return true; }` / `{ return false; }` bodies. Mechanical, and the
  `override` keyword makes every one a hard compile error rather than a silent
  behaviour change. **~0.1d**
- Test work: one new pure-function Catch2 case, extending the L4 gateway units
  with a failing launcher, and re-pointing the two e2e suites whose feedback
  assertions this leaf makes honest. **~0.1d**

No library API changes, no new component, no new build target.

## Inherited dependencies

**Settled (from `editor.project.welcome`, `tasks/refinements/editor/welcome.md`)**

1. **`D-welcome-6` — the L4 gateway is split into a session-free base and a
   session-owning derived class.** `app::ProjectEntryGateway`
   (`src/app/ace/app/project_gateway.hpp:34-71`) owns the five entry verbs and the
   private `spawn` (`:70`); `AppProjectGateway final : ProjectEntryGateway`
   (`:91`) adds the `commands::AppState`. So there is exactly **one** L4
   implementation of open/new/recent to re-sign, and both process modes (A22)
   inherit the change.
2. **`D-welcome-7` — the compose modal is one shared value + one shared draw
   routine.** `NewProjectModal` (`dock.hpp:264-283`), `draw_compose_target_modal`
   (`dock.hpp:309-310`, `dock.cpp:425-461`) and the `draw_new_project_modal`
   forwarder (`dock.hpp:316`, `dock.cpp:463-474`). New's submit path changes in
   **one** place for both hosts.
3. **`D-welcome-8` — the `bool` means "validated AND spawned", and the launcher's
   exit latch fires only on it.** `WelcomeWindow::exit_requested()`
   (`src/dock/ace/dock/welcome.hpp:59`) is one-way and set only by a verb that
   actually spawned. This leaf preserves that predicate verbatim; it only changes
   what the *false* half can say.
4. **The debt itself.** `welcome.md`'s Status records the one deviation: the
   `"Could not start the editor."` string is *inferred* from MRU membership, in the
   file-local `refusal_feedback` helper at `src/dock/welcome.cpp:22-43`. That helper
   and its rationale comment are what this leaf retires.

**Settled (from `editor.project.open_ui`, `tasks/refinements/editor/open_ui.md`)**

5. **`D-open_ui-2` / A12 — the seam itself.** L3 `dock` declares
   `ProjectGateway` (`dock.hpp:66-254`); L4 `app` supplies the only concrete impl,
   holding SDL, the `ProcessLauncher`, the `RecentProjects` store and the L1
   `project` helpers. `dock` may include neither `<ace/project/…>` nor
   `<ace/platform/…>`, which is precisely why a refused L3 caller **cannot ask a
   second question** to find out why it was refused. The answer has to arrive with
   the refusal.

**Settled (from `editor.project.dir_is_project`, `tasks/refinements/editor/dir_is_project.md`)**

6. **`D-dir_is_project-5` — `ComposeModalSpec` carries a `std::function` submit**
   (`dock.hpp:291-295`), so the modal "knows NOTHING about which gateway verbs
   exist". Widening the seam's result type therefore means widening exactly that
   one `std::function` signature, not teaching the modal about verbs.
7. **`D-dir_is_project-6` — ONE refusal string for both of New's refusals**
   (an invalid name and a taken name call for the identical corrective act;
   `dock.cpp:444-451`). That decision **stands**, and this leaf's enum is shaped to
   respect it: `refused_target` deliberately does not split by cause.
8. **`D-dir_is_project-3` — the L4 existence pre-check on `new_project`**
   (`project_gateway.cpp:66-77`), which is a refusal that must not be confused with
   a launch failure.

**Settled (from `editor.project.reopen_degradation_notice`)**

9. **The two-channel rule** (`src/dock/dock.cpp:86-89`,
   `D-reopen_degradation_notice-3`): the inline `project_feedback_` string is for
   feedback on **an action the user just took**; the one-shot modal notice is for a
   **passive condition** of the session. Every string this leaf touches is the
   former, so nothing here goes near the modal channel.

**Pending (this leaf owns them)**

- The outcome type's shape, name, home and enumerator set.
- Where the outcome→string mapping lives so both hosts share it (the two hosts
  currently disagree — see "Why").
- What happens to `ComposeModalSpec::submit`, whose one `std::function` signature
  is shared by New (an entry verb) and Save As (a session verb, A13).
- Whether `save_as` widens too, or is adapted and deferred.

## What this task is

Replace `dock::ProjectGateway`'s three entry verbs' `bool` return with a small
scoped enum, `dock::ProjectEntryOutcome { succeeded, refused_target, spawn_failed }`,
declared in `dock`'s own header beside the seam it belongs to. Add one shared L3
mapping function that turns an outcome plus a caller-supplied refusal string into
the inline feedback both hosts render, point `dock::Dockspace`'s tool rail and
`dock::WelcomeWindow` at it, delete the MRU-inference helper in `welcome.cpp`, and
re-sign the three L4 verb bodies plus the eleven test fake gateways. The private
`ProjectEntryGateway::spawn` returns the outcome directly, so each verb body reads
"validate → record → spawn" with no ternary in between.

The scope boundary is the word **entry** in the task title: `open_project`,
`new_project` and `open_recent`. The session verbs (A13 — `save`, `save_as`,
`clean_up`, `undo`, `redo`) keep their current returns; `save_as` is adapted at its
one binding site and its residual ambiguity is deferred to a named follow-up.

## Why it needs to be done

**The seam lies, in two different directions, on two different surfaces.**

The welcome infers. `src/dock/welcome.cpp:22-43` reconstructs *which half failed*
by asking whether the directory is still in the MRU list, on the reasoning that
both `open_project` and `open_recent` record MRU-front **after** validating and
**before** spawning. The reasoning is sound about the code it cites and unsound as
a discriminator, for two reachable reasons:

- `RecentProjects::add` can fail (an empty path, or an I/O failure writing the
  per-user prefs file) and its return is discarded at `project_gateway.cpp:55` and
  `:87`. A prefs write that fails turns a genuine `spawn_failed` into
  `"That folder is not a project."` — the user is told their project is not a
  project.
- `recent_projects()` re-prunes through `project::is_project_directory` on every
  query (`project_gateway.cpp:96-102`), so a target that disappears between the
  `add` and the redraw flips the discriminator the other way.

The rail does not even try. `dock.cpp:310-314` and `:326-330` map `false`
unconditionally onto `"That folder is not a project."` / `"That project is no
longer available."`, so a failed `exec` on the tool rail is reported to the user as
a bad folder. This directly contradicts `welcome.cpp:15-17`'s claim that every
welcome verb mirrors `draw_project_section`'s "same feedback strings" — today the
two hosts of the same seam produce **different** messages for the same failure, and
the rail's is the wrong one. Fixing the rail is not incidental to this leaf; it is
half the user-visible payoff, and it is unreachable without the outcome value
because `dock` may not include `<ace/project/…>` to ask again (A12).

**The corrective acts are different, which is the whole point of splitting.** A
refused target means "pick a different folder" — the user acts. A failed spawn
means the editor could not launch a sibling process: a broken install, a missing
executable, an exhausted process table. Nothing the user does in the folder picker
helps, and the current message sends them to do exactly that.

**Downstream.** Every future consumer of this seam inherits the vocabulary. The
WASM port (A3/A12) has a File System Access API where "refused" and "could not
launch" are structurally different failures, and `editor.project.save_as_outcome`
(the follow-up named below) extends the same enum rather than inventing a second
result convention.

## Inputs / context

**Design docs (normative — the constitution)**

- **D22** (`docs/00-design.md:489`) — In-app New / Open / Recent. Names the three
  entry verbs, makes every one of them **spawn a sibling** and never swap the
  in-process session, and puts native folder picking behind the app-level seam
  (A12). The spawn is what makes "the spawn failed" a real, distinct outcome
  rather than a hypothetical.
- **D26** (`docs/00-design.md:493`) — the welcome launcher: three verbs only,
  dismissal exits. Realized by `editor.project.welcome`, whose implementation
  carries the deviation this leaf repays.
- **D27** (`docs/00-design.md:494`) — amends D22: Save As takes the same
  not-yet-existing parent-plus-name target New takes, which is why New and Save As
  share one compose modal and therefore one submit signature.
- **A12** (`docs/01-architecture.md:374`) — the `ProjectGateway` inversion:
  "L3 `dock` **declares and owns**" the interface, the impl is L4 `app`, and
  "**Errors are values**: the mutating actions return success/failure the rail
  renders as inline feedback." This leaf widens *what kind of value* — see the
  A24 doc delta below.
- **A13** (`docs/01-architecture.md:375`) — the session verbs on the same seam.
  The scope line this leaf does not cross.
- **A22** (`docs/01-architecture.md:384`) — two process modes; the gateway split
  along the entry/session seam. It is what makes "the entry verbs" a nameable,
  already-factored set: `ProjectEntryGateway` is literally the type holding them.
- **§8 levelization DAG** (`docs/01-architecture.md:255-291`) — `dock` is L3 and
  may depend only on `dockmodel`, `views` and imgui.
- **§9 testing / DoD** (`docs/01-architecture.md:293-321`).

**Library API surface**

None. This leaf touches no `arbc::` type and does not move the pin.

**Source seams this leaf extends**

- `src/dock/ace/dock/dock.hpp:66-104` — the seam's doc comment and the five entry
  verbs; `:86`, `:91`, `:95` are the three declarations that change type. The
  comment at `:78-79` ("Errors are values: the mutating actions return
  success/failure the rail renders as inline feedback") is the sentence this leaf
  makes more nearly true.
- `src/dock/ace/dock/dock.hpp:291-295` — `ComposeModalSpec`, whose `submit` field
  is `std::function<bool(const std::filesystem::path&, const std::string&)>`.
- `src/dock/ace/dock/dock.hpp:297-316` — `draw_compose_target_modal` and the
  `draw_new_project_modal` forwarder, plus the doc comment naming the one refusal
  string.
- `src/dock/dock.cpp:248-359` — `draw_project_section`; the two flat two-way
  branches at `:310-314` (Open) and `:326-330` (Recent), the feedback render at
  `:334-336`, the New binding at `:341-342` and the Save As binding at `:348-356`.
- `src/dock/dock.cpp:425-461` — `draw_compose_target_modal`; the submit branch at
  `:439-451` carries the `D-dir_is_project-6` comment that this leaf must keep
  accurate.
- `src/dock/welcome.cpp:22-43` — `refusal_feedback`, the MRU inference. **Deleted
  by this leaf**, along with the now-stale source citation at `:27`
  ("src/app/project_gateway.cpp:46-71", a range that no longer bounds what it
  claims to).
- `src/dock/welcome.cpp:80-93`, `:96-113`, `:114-116`, `:120-122` — the welcome's
  Open, Recent, feedback render and shared-modal call.
- `src/dock/ace/dock/welcome.hpp:54-68` — the `exit_requested()` latch contract
  ("A cancelled pick, a refused target and a failed spawn all leave it false") and
  the `feedback()` accessor. The latch's *documented* vocabulary already names the
  three outcomes this leaf makes real; only the comment knows them today.
- `src/app/ace/app/project_gateway.hpp:41-47` — the five entry-verb overrides;
  `:70` — the private `bool spawn(...)`.
- `src/app/project_gateway.cpp:44-49` (`spawn`), `:51-57` (`open_project`),
  `:59-81` (`new_project`, including the `D-dir_is_project-3` existence
  pre-check), `:83-89` (`open_recent`), `:96-102` (`recent_projects`'s pruning
  load).
- `src/app/project_gateway.cpp:194-218` — `AppProjectGateway::save_as`, whose
  four documented failure modes (`dock.hpp:128-130`) collapse into one `bool` and
  therefore into one string. The boundary of this leaf.

**Test rigs**

- `tests/welcome_e2e_test.cpp` — `FakeGateway` at `:47` with the
  `vanished` / `spawn_ok` pair at `:51-52` and the comment at `:41-46` explaining
  that the pair exists *only* to simulate the two halves the `bool` cannot
  distinguish; entry verbs at `:62-85`; the driven sequence at `:207-241`; the four
  verbatim string assertions at `:211`, `:221`, `:235`, `:251`. Also holds two
  non-e2e `TEST_CASE`s (`:375`, `:399`), so a pure-function unit belongs here
  without a CMake change.
- `tests/open_ui_e2e_test.cpp` — `FakeGateway` at `:34-82` (records, validates
  nothing, models no MRU), the rail e2e `TEST_CASE` at `:97` /
  `IM_REGISTER_TEST(engine, "open_ui", "rail_project_section")` at `:118`, and the
  non-project-Open feedback step at `:171-186`.
- `tests/app_project_gateway_test.cpp` — the L4 headless units, tag
  `[app_project_gateway]`; `ScratchDir` `:39`, `RecordingLauncher` `:53-65`
  (always succeeds), `ScriptedFolderDialog` `:69`, `make_project` `:92`,
  `InertGateway` `:129-152`, and the two session-free base cases at `:160` and
  `:224`.
- `tests/save_as_test.cpp:108-116` — the `FailingLauncher` pattern
  (`spawn_detached` returning `std::make_error_code(std::errc::no_such_file_or_directory)`)
  this leaf copies into the gateway suite to drive `spawn_failed`.
- `CMakeLists.txt:271-298` — the `ace_shell_test` target holding all three files
  (`:276` gateway units + `open_ui`, `:296` welcome); `:307-308` pins the
  displayless env.

**The eleven fake gateways whose entry verbs re-sign**

`tests/gc_ui_e2e_test.cpp:42-44`, `tests/reopen_degradation_notice_e2e_test.cpp:53-55`,
`tests/new_shot_from_view_e2e_test.cpp:400-402`, `tests/undo_ui_e2e_test.cpp:41-43`,
`tests/welcome_e2e_test.cpp:62-85`, `tests/save_ui_e2e_test.cpp:40-42`,
`tests/frame_selection_e2e_test.cpp:331-333`, `tests/save_as_ui_e2e_test.cpp:49-51`,
`tests/cells_remove_e2e_test.cpp:362-364`, `tests/app_project_gateway_test.cpp:131-133`,
`tests/open_ui_e2e_test.cpp:47-60`.

## Constraints / requirements

1. **The outcome is a `dock`-declared scoped enum, in `dock`'s own header, above
   the class that returns it.** `enum class ProjectEntryOutcome { succeeded,
   refused_target, spawn_failed };` in `src/dock/ace/dock/dock.hpp`, in the
   `GcSummary` / `InsertKindSpec` / `ComposeModalSpec` family of dock-local
   exchange types. It names no `project`, `platform`, `commands` or `arbc` type and
   adds no include, so `dock`'s include set is unchanged (§8 / A12).

2. **Exactly three enumerators, and `refused_target` does not split by cause.**
   `succeeded` = validated and spawned (the `D-welcome-8` meaning of today's
   `true`, unchanged). `refused_target` = the gateway declined before launching
   anything — not a project, no longer a project, an invalid name, a target that
   already exists. `spawn_failed` = the target was accepted and the sibling `exec`
   failed. The split is by **corrective act**, not by cause, which is what keeps
   `D-dir_is_project-6`'s one-string-for-both-New-refusals decision intact.

3. **The three entry verbs stay PURE virtuals.** `open_project`, `new_project` and
   `open_recent` return `ProjectEntryOutcome`. They are not demoted to non-pure
   with an inert default: every existing override carries `override`, so a changed
   return type is a hard compile error at all eleven fakes and at the L4 impl —
   which is the intended forcing function. A defaulted virtual would let a stale
   `bool` override silently stop overriding and flip every unrelated suite's fake
   from "succeeds" to "refuses".

4. **One shared L3 mapping function, and both hosts use it.** Declared beside the
   enum and defined in `src/dock/dock.cpp`:
   `const char* entry_feedback(ProjectEntryOutcome outcome, const char* refused_message);`
   returning `""` for `succeeded`, `refused_message` for `refused_target`, and the
   single `"Could not start the editor."` literal for `spawn_failed`. It takes no
   ImGui state, so it is unit-testable without a context. Each of the four call
   sites collapses to one assignment into the host's existing `std::string`
   (`Dockspace::project_feedback()` at `dock.hpp:475`, `WelcomeWindow::feedback()`
   at `welcome.hpp:64`), with `""` standing in for today's `.clear()`.

5. **`src/dock/welcome.cpp:22-43` is deleted, not amended.** The helper, its
   twenty-line rationale comment, its `<system_error>` / `weakly_canonical`
   dependency and its stale `project_gateway.cpp:46-71` citation all go. Whatever
   includes it alone justified go with it.

6. **The rail's spawn-failure message changes, and that is the point.**
   `dock.cpp:310-314` and `:326-330` must report `"Could not start the editor."`
   on `spawn_failed`, matching the welcome. The four refusal strings themselves are
   unchanged, verbatim: `"That folder is not a project."`,
   `"That project is no longer available."`, `"Could not start the editor."`,
   `"Enter a project name that does not already exist here."` (D22/D26 copy — this
   leaf changes *when* each is shown, never *what* it says).

7. **`ProjectEntryGateway::spawn` returns the outcome.**
   `ProjectEntryOutcome spawn(const std::filesystem::path& dir);`
   (`project_gateway.hpp:70`, `project_gateway.cpp:44-49`) yielding `succeeded` or
   `spawn_failed` from `commands::open_another_project`'s `error_code`. It is
   private and has no caller outside the three entry verbs — `AppProjectGateway::save_as`
   reaches `commands::save_project_as` directly (`project_gateway.cpp:215-217`) —
   so the change is local and each verb body ends in a bare `return spawn(...)`.

8. **The MRU record stays where it is, and its failure no longer matters.**
   `recent_.add(dir)` keeps running after validation and before the spawn
   (`project_gateway.cpp:55`, `:87`); this leaf does not start checking its return.
   The point is that the outcome no longer *depends* on it, so a failed prefs write
   degrades to "the entry is missing from the list" instead of "the user is told
   their project is not a project".

9. **The shared compose modal's submit widens; Save As is adapted at its binding,
   not at the seam.** `ComposeModalSpec::submit`
   (`dock.hpp:294`) becomes
   `std::function<ProjectEntryOutcome(const std::filesystem::path&, const std::string&)>`
   and the struct gains
   `const char* refused_message = "Enter a project name that does not already exist here.";`
   so `draw_compose_target_modal` maps through the same `entry_feedback` both flat
   verbs use. New binds `gw->new_project` directly. Save As binds an adapter
   returning `succeeded` / `refused_target`, which preserves today's observable
   behaviour for every one of its four failure modes exactly (they already all
   produce that one string) and introduces no new lie — the existing one is
   deferred, named, and called out in the code comment.

10. **Levelization: no new component, no new DAG edge, no new external
    dependency.** One enum, one free function and one field added to types `dock`
    already owns; L4 `app` already implements the seam. `scripts/check_levels.py`
    is unmodified and the L1 core gains no include.

11. **Comment deltas that keep the source honest.** `dock.hpp:78-79`'s
    "return success/failure" line becomes the three-outcome statement;
    `dock.hpp:84-95`'s three per-verb comments state which outcome each refusal
    path yields; `dock.cpp:444-451`'s "the seam is a `bool`, so there is nothing
    here to tell them apart" is rewritten to say what is now true — the modal
    *can* tell a refusal from a launch failure, and it still deliberately does not
    tell an invalid name from a taken one (`D-dir_is_project-6`);
    `welcome.cpp:15-17`'s "same feedback strings" claim becomes accurate for the
    first time and should say so.

12. **Scope boundary.** No session verb changes signature. No new gateway virtual.
    No change to the exit latch's semantics (`exit_requested()` still latches only
    on a verb that spawned — now spelled `== ProjectEntryOutcome::succeeded`). No
    change to the two-channel rule: every string here stays in the inline
    `project_feedback_` / `feedback_` channel.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate`
green (check_levels · clang-format · build · ctest · coverage) is the umbrella.
Specifically:

- **Levelization (`check_levels` clean).** `scripts/check_levels.py` is
  unmodified and passes. `src/dock/ace/dock/dock.hpp`'s include set is byte-identical
  (the enum and the `const char*` mapper need nothing new), and `src/dock/welcome.cpp`
  *loses* `<system_error>` with the deleted helper. No component gains a
  dependency; the L1 core is untouched.

- **L3 pure-function unit — Catch2.** A new `TEST_CASE` in
  `tests/welcome_e2e_test.cpp` (already in `ace_shell_test`, `CMakeLists.txt:296`;
  the file already carries two non-e2e cases at `:375` and `:399`), needing no
  ImGui context:
  `TEST_CASE("entry_outcome: entry_feedback maps each outcome to one inline string")`
  — asserts the full 3×2 table verbatim: `succeeded` → `std::string{}` for both
  refusal strings; `refused_target` → the caller's string, echoed unchanged, for
  `"That folder is not a project."` and `"That project is no longer available."`;
  `spawn_failed` → `"Could not start the editor."` **regardless** of the refusal
  string passed. The last row is the pin that the launch-failure message is a
  property of the outcome and not of the call site.

- **L4 headless units — extend `tests/app_project_gateway_test.cpp`**
  (`ace_shell_test`, `CMakeLists.txt:276`, tag `[app_project_gateway]`), adding a
  local `FailingLauncher` in the anonymous namespace on the
  `tests/save_as_test.cpp:108-116` pattern:
  - `TEST_CASE("ProjectEntryGateway reports refused_target for every pre-launch refusal")`
    — `SECTION`s for `open_project` on a directory with no `project.arbc`,
    `open_recent` on a directory pruned since the list was rendered, `new_project`
    with an empty / blank / traversing name, and `new_project` on a target that
    already exists (`D-dir_is_project-3`). Each asserts
    `== ProjectEntryOutcome::refused_target` **and** `launcher.invoked == false`
    — a refusal must still spawn nothing.
  - `TEST_CASE("ProjectEntryGateway reports spawn_failed when the sibling exec fails")`
    — with `FailingLauncher`, `SECTION`s for `open_project` and `open_recent` on a
    valid project and `new_project` on a valid not-yet-existing target, each
    asserting `== ProjectEntryOutcome::spawn_failed`. For the two recording verbs,
    additionally assert `recent_projects()` still lists the directory — pinning
    that the record-then-spawn ordering (`project_gateway.cpp:55`, `:87`) is
    unchanged while no longer being load-bearing for the message.
  - `TEST_CASE("ProjectEntryGateway reports succeeded only when it validated AND spawned")`
    — the `RecordingLauncher` path for all three verbs, `== succeeded`, preserving
    the assertions the two shipped session-free cases (`:160`, `:224`) already make
    about MRU recording and the composed target handed to the launcher.
  - The `InertGateway` at `:129-152` re-signs its three overrides to
    `refused_target`, which is the honest inert answer (it launches nothing) and
    keeps its existing purpose — pinning that `reopen_unbindable_count()` is
    defaulted — unaffected.

- **UI e2e — ImGui Test Engine.** Both hosts, driven headless by widget id:
  - `IM_REGISTER_TEST(engine, "welcome", "three_verbs_and_feedback")`
    (`tests/welcome_e2e_test.cpp:168`) keeps all four verbatim string assertions
    (`:211`, `:221`, `:235`, `:251`). Its `FakeGateway` (`:47`) drops the
    MRU-membership modelling that existed only to feed the inference (`:41-46`,
    `:51-52`) and returns outcomes directly. **The regression pin:** the
    `"Could not start the editor."` assertion at `:221` must now hold while
    `recent_projects()` returns an **empty** list — the exact configuration the
    deleted `refusal_feedback` reported as `"That folder is not a project."`.
    Ordering constraint from `:182-183` (the exit latch is one-way, so a successful
    verb comes last) is preserved.
  - `IM_REGISTER_TEST(engine, "open_ui", "rail_project_section")`
    (`tests/open_ui_e2e_test.cpp:118`) gains a step after the existing
    non-project-Open step (`:171-186`): with the fake returning `spawn_failed`,
    clicking `###open_project` (and, in a second step, a seeded `###recent0`)
    leaves `"Could not start the editor."` in the rail's feedback rather than the
    refusal string. This is the leaf's user-visible behaviour change on the rail and
    it must be asserted, not inferred; the existing refused-target steps stay
    unmodified.
  - The compose modal's three refs (`"New Project/Name"`, `"New Project/Create"`,
    `"New Project/Cancel"`) and the `"Save Project As"` refs are unchanged, so
    `tests/save_as_ui_e2e_test.cpp` and the New steps of both suites must pass
    **unmodified** apart from their fakes' three re-signed overrides.

- **Rendered output — golden N/A (justified).** This leaf renders no pixels; it
  changes a return type and which of four existing strings is shown. The e2e
  screenshot baselines are unaffected because no widget, id or layout changes.

- **Threading (ASan/TSan) — no new surface, existing lane covers it.** The entry
  verbs run entirely on the UI thread, spawn a detached process and touch no
  `Document`, no writer thread and no worker pool. The `clang-asan` lane
  (`docs/01-architecture.md` §9.1) runs the three affected suites under the
  offscreen/software-GL env (`CMakeLists.txt:307-308`) and must stay clean —
  notably `welcome.cpp`'s deleted helper removes a `weakly_canonical` filesystem
  call from a per-click path.

- **Coverage.** ≥90% diff coverage on changed lines (`diff-cover --fail-under=90`).
  The changed surface is small and every branch is directly driven: the three
  enumerators are each asserted in the pure-function unit, each of the three L4
  verbs is driven to all three outcomes by the units, and both hosts' rendering of
  each outcome is driven by the two e2e tests.

- **Format / build.** `clang-format` clean; `cmake --build` clean with no new
  warnings. The compile errors at the eleven fakes are expected and are the
  mechanism by which the change is proved exhaustive — the build is not green
  until every implementor of the seam has been visited.

**One follow-up WBS task is deferred**, and only one:

- `editor.project.save_as_outcome` — **0.5d**, `depends !entry_outcome` — *"Widen
  `save_as` to the `ProjectEntryOutcome` seam so a failed publish or a failed exec
  stops reporting a name refusal."* `AppProjectGateway::save_as`
  (`project_gateway.cpp:194-218`) has four documented failure modes
  (`dock.hpp:128-130`: invalid name · target exists · failed publish · failed exec)
  collapsed into one `bool`, and this leaf's adapter maps all four onto
  `refused_target`, preserving today's exact string. Resolving it honestly needs a
  fourth enumerator (`publish_failed`) and a richer result out of
  `commands::save_project_as`, which is real work in L1 `commands` and outside the
  "entry verbs" scope this leaf's `.tji` note draws. Deferred to
  `editor.project.save_as_outcome` (closer registers in WBS, under `editor.project`,
  which `milestones.m9_editor` already depends on).

## Decisions

**`D-entry_outcome-1` — A scoped enum with three enumerators, not a struct, not an
error code, not a message string.** `enum class ProjectEntryOutcome { succeeded,
refused_target, spawn_failed; }` declared in `src/dock/ace/dock/dock.hpp` beside
`GcSummary`, `InsertKindSpec` and `ComposeModalSpec`.

*Rationale:* there is nothing to carry besides the discriminant — the two hosts
already own their feedback string, and the refusal wording is caller context (Open
says one thing, Recent another) rather than implementation knowledge. A scoped enum
also makes every existing `if (gateway.open_project(dir))` a compile error rather
than a silently-true expression, which is exactly what a seam-wide vocabulary change
should do.

*Alternative rejected:* a struct POD `{ bool spawned; bool refused; }` or
`{ bool ok; const char* message; }` — the `.tji` note's "outcome POD" phrasing
admits either, but two bools encode a fourth state that cannot occur, and a message
field would move D22/D26 UI copy into L4 `app`, where the rail's and the welcome's
different refusal wordings would have to be re-derived from context the impl does
not have.

*Alternative rejected:* `std::expected<void, std::error_code>` — the codebase's
house idiom below L3 (`commands::open_another_project` returns exactly that,
`project_gateway.cpp:48`) and it would carry the underlying OS error for free. But
`dock` would then have to map an open-ended `error_code` space onto two strings, and
the mapping table would have to live somewhere that knows what
`commands::open_another_project` can return — which is the `<ace/commands/…>`
include A12 forbids. Three closed outcomes is the smaller abstraction with the two
call sites it actually has.

*Alternative rejected:* keep `bool` and add a `virtual ProjectEntryOutcome
last_entry_outcome() const` query — no signature churn at any fake, but it turns a
value-returning seam into a stateful two-call protocol, and nothing forces a fake to
keep the two in sync.

**`D-entry_outcome-2` — `refused_target` is one outcome for every pre-launch
refusal; the split is by corrective act, not by cause.** Not-a-project, no-longer-a-
project, invalid name and target-exists all yield `refused_target`.

*Rationale:* `D-dir_is_project-6` already settled that an invalid name and a taken
name deserve one string because they call for the identical corrective act, and the
same reasoning generalizes: everything the gateway refuses before launching is
answered by the user choosing a different target. What is *not* answered that way is
a failed `exec`, and that is precisely the boundary the enum draws. Splitting
further would multiply enumerators the UI would immediately re-merge.

*Alternative rejected:* four or five enumerators (`not_a_project`, `invalid_name`,
`target_exists`, …) — strictly more information, but every host would collapse them
back into the two strings D22/D26 specify, and `D-dir_is_project-6` would have to be
re-argued to keep New's two refusals showing one message.

**`D-entry_outcome-3` — The entry verbs stay pure virtuals; the compile break at
all eleven fakes is the deliverable, not the cost.**

*Rationale:* the header's own precedent (`dock.hpp:161-163`) makes a verb non-pure
with an inert default *specifically so unrelated suites' fakes need no churn* — but
that reasoning applies to **added** verbs, where an un-updated fake correctly means
"this surface is unwired". Here the verbs already exist and every override says
`override`, so a changed return type produces a hard error at each of the eleven
sites and the build cannot go green until every implementor of the seam has been
visited. Making them defaulted would let a stale `bool open_project(...) override`
fail to override and silently flip eight suites' fakes from "succeeds" to
"refuses" — a behaviour change disguised as a compile success.

*Alternative rejected:* introduce the outcome on three *new* verb names and leave
the `bool` trio deprecated-but-present — zero churn, but two parallel vocabularies
on one seam is the exact ambiguity this leaf exists to remove.

**`D-entry_outcome-4` — One shared L3 mapper, `entry_feedback(outcome,
refused_message) -> const char*`, and both hosts route through it.**

*Rationale:* the bug is that two hosts of one seam disagree about what `false`
means. A shared pure function is the smallest construct that makes disagreement
impossible: `"Could not start the editor."` exists in exactly one place, the
per-verb refusal string stays a parameter at the call site where the verb's context
lives, and `succeeded` mapping to `""` lets all four call sites collapse from an
`if/else` into one assignment. Returning `const char*` rather than taking
`std::string&` keeps it a pure function that Catch2 can table-test with no ImGui
context and no host object.

*Alternative rejected:* a `switch` at each of the four call sites — no new symbol,
but it reintroduces the per-host duplication that produced the divergence, and it
puts the launch-failure literal in four places.

*Alternative rejected:* a member on `Dockspace`/`WelcomeWindow` — the two hosts have
no common base and no reason to grow one for a pure mapping.

**`D-entry_outcome-5` — `welcome.cpp`'s `refusal_feedback` is deleted, and the
rail's spawn-failure message changes to match the welcome's.**

*Rationale:* the inference is not merely redundant once the outcome exists, it is
unsound — `RecentProjects::add`'s discarded failure (`project_gateway.cpp:55`,
`:87`) and `recent_projects()`'s re-pruning (`:96-102`) each flip the discriminator
in a reachable case. And the rail never had it at all, so `welcome.cpp:15-17`'s
"same feedback strings" claim is currently false; repairing the welcome without
repairing the rail would leave it false in the other direction. Both hosts routing
through `D-entry_outcome-4`'s mapper is what makes the claim true by construction.

*Alternative rejected:* fix only the welcome (the literal reading of the `.tji`
note's "Source-of-debt") — leaves the rail telling a user with a broken install that
their project folder is not a project, which is the same defect on the surface more
users see.

**`D-entry_outcome-6` — `ProjectEntryGateway::spawn` returns the outcome directly.**

*Rationale:* all three verb bodies end in `return spawn(...)`, so pushing the
`bool → outcome` conversion into the one private helper removes three ternaries and
keeps `spawn_failed` produced in exactly one place — the place that owns the
`error_code` from `commands::open_another_project`. `spawn` is private and
`AppProjectGateway::save_as` does not use it, so the change is contained.

*Alternative rejected:* keep `spawn` returning `bool` and ternary at each call site
— three copies of the same mapping, one of which will eventually be written the
other way round.

**`D-entry_outcome-7` — The compose modal's `submit` widens to the outcome and
gains a `refused_message` field; Save As binds an adapter, and its residue is
deferred.** `ComposeModalSpec::submit` becomes
`std::function<ProjectEntryOutcome(const std::filesystem::path&, const std::string&)>`;
`ComposeModalSpec` gains
`const char* refused_message = "Enter a project name that does not already exist here.";`;
`draw_compose_target_modal` maps through the same `entry_feedback`. New binds
`new_project` directly; Save As binds
`[gw](parent, name) { return gw->save_as(parent, name) ? succeeded : refused_target; }`.

*Rationale:* New is an entry verb and must carry the new vocabulary; the modal is
shared with Save As by `D-welcome-7`/`D-dir_is_project-5`, so exactly one of the two
sides has to adapt. Adapting at the *binding* (an L3 lambda, three tokens long)
rather than at the *seam* keeps `save_as`'s signature, its L4 body, and the seven
fakes that override it untouched, and it is observably a no-op: all four of
`save_as`'s failure modes already produce the one refusal string, so mapping them
all to `refused_target` reproduces today's UI exactly. Widening `save_as` honestly
needs a fourth enumerator and a richer result from `commands::save_project_as`,
which is L1 work outside "the entry verbs" — hence
`editor.project.save_as_outcome`, named under Acceptance criteria.

*Alternative rejected:* widen `save_as` in this leaf — one consistent seam
immediately, but it pulls `commands::save_project_as`'s error taxonomy into a 0.5d
task whose `.tji` note explicitly scopes it to the entry verbs, and it would ship a
`publish_failed` enumerator with no tested producer.

*Alternative rejected:* keep `submit` returning `bool` and have
`draw_new_project_modal`'s lambda write the feedback itself — no signature change,
but then the modal's `else` branch would overwrite the string the lambda just set,
and untangling that means a "did the submit already write feedback?" convention that
is strictly worse than one widened `std::function`.

### Doc delta (rides in the closer's commit)

**A24** in `docs/01-architecture.md` (next free id — A23 is the last row, at
`:385`): *Project-entry outcomes are values, not inferences.* A12 established the
gateway and stated "Errors are values: the mutating actions return success/failure";
A13 and A22 each extended the same seam with a row. This leaf changes the seam's
error **vocabulary** — the three entry verbs return `dock::ProjectEntryOutcome`
rather than `bool` — and retires a shipped host-side inference, so it belongs in the
same log. It is structure, not UI/UX, so it is an `A` row rather than a `D` row: the
four user-visible strings are unchanged D22/D26 copy.

## Open questions

_None — all decided against the constitution._

D22 (`docs/00-design.md:489`) settles that every entry verb spawns, which is what
makes "the spawn failed" a distinct outcome. D26 (`:493`) settles the welcome's
three verbs and its dismissal rule, so the exit latch's predicate is fixed and this
leaf only renames the condition it tests. `D-dir_is_project-6` settles that New's
two refusals share one string, so the enum does not split refusals by cause. A12
(`docs/01-architecture.md:374`) settles that `dock` cannot ask a second question
about a refusal, so the answer must ride on the return value. A13 (`:375`) settles
that the session verbs are a separate family, which is what makes deferring
`save_as` a scope decision rather than an omission. §8 (`:255-291`) settles that a
`dock`-local type is the only legal exchange shape.

**Doc delta (same-commit rule):** A24 as described above.

**Parking-lot item (human judgment, not a WBS task):** none. The one judgment call
this leaf makes — that `"Could not start the editor."` is the right words for a
failed `exec` — is D26/`D-welcome`'s shipped copy, reused verbatim rather than
re-opened.

## Status

**Done** — 2026-07-24.

- Introduced `dock::ProjectEntryOutcome { succeeded, refused_target, spawn_failed }` in `src/dock/ace/dock/dock.hpp` beside the existing dock-local exchange types; added `entry_feedback(outcome, refused_message) -> const char*` mapper declared there and defined in `src/dock/dock.cpp`.
- Re-signed the three entry verb declarations in `src/dock/ace/dock/dock.hpp` and the L4 overrides in `src/app/ace/app/project_gateway.hpp`; `ProjectEntryGateway::spawn` now returns the outcome directly (`src/app/project_gateway.cpp`).
- Deleted `welcome.cpp:refusal_feedback` (MRU-inference helper) and updated both hosts (`src/dock/dock.cpp`, `src/dock/welcome.cpp`) to route through the shared `entry_feedback` mapper; the rail now correctly reports `"Could not start the editor."` on `spawn_failed` instead of the refusal string.
- `ComposeModalSpec::submit` widened to `std::function<ProjectEntryOutcome(...)>` with a `refused_message` field; Save As adapted at its binding site with a `refused_target` adapter (deferred to `editor.project.save_as_outcome`).
- Eight mechanically re-signed fake gateways in the e2e suites (`gc_ui`, `reopen_degradation_notice`, `undo_ui`, `save_ui`, `save_as_ui`, `new_shot_from_view`, `frame_selection`, `cells_remove`) — `tests/*.cpp`.
- Added Catch2 unit `entry_outcome: entry_feedback maps each outcome to one inline string` (full 3×2 table) in `tests/welcome_e2e_test.cpp`; three new L4 cases in `tests/app_project_gateway_test.cpp` (`refused_target` for every pre-launch refusal, `spawn_failed` via `FailingLauncher`, `succeeded` for the happy path); e2e step pinning `"Could not start the editor."` on both flat rail verbs in `tests/open_ui_e2e_test.cpp`.
- A24 doc row (`docs/01-architecture.md`) — "Project-entry outcomes are values, not inferences" — was already present in the working tree and is committed here.
- Tech-debt follow-up `editor.project.save_as_outcome` registered in `tasks/00-editor.tji` (0.5d, `depends !entry_outcome`).
