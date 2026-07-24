# editor.project.welcome — Welcome dialog: interactive no-arg launch offers New / Open / Recent

## TaskJuggler entry

- **Task:** `editor.project.welcome`, `tasks/00-editor.tji:150-155`.
- **Effort:** `1.5d`, `allocate team`.
- **Depends:** `!open_ui` (`editor.project.open_ui`, `tasks/00-editor.tji:101-105`) — the
  leaf that built `dock::ProjectGateway`, the SDL folder dialog, the
  `dockmodel::RecentProjects` MRU store and the rail's New / Open / Recent section
  this leaf re-hosts.
- **Note (`tasks/00-editor.tji:154`):** cites **D16 / D19 / D22** and **A7 / A12**, names
  `tasks/refinements/editor/app_state.md` as the source of debt, and carries two
  pre-exec decisions (dismiss = clean exit; three verbs only, no scratch/skip verb and
  no remembered default in v1). Both are honoured verbatim below.
- **Milestone:** **M9E** (`m9_editor`, `tasks/99-milestones.tji:6-10`) via the
  `editor.project` subtree; already wired, no milestone edit needed.
- **Back-link fix for the closer.** The `.tji` note currently ends
  `Refinement: tasks/refinements/welcome.md` — a **flat** path. The refinement lives at
  `tasks/refinements/editor/welcome.md` (the `<area>/<task_name>.md` layout,
  `tasks/refinements/README.md:11-18`). The same slip was flagged and fixed for
  `editor.project.exec_new`; fix this one in the completion commit alongside
  `complete 100`, then run `tj3 project.tjp 2>&1 | grep -iE "error|warning"` and confirm
  silence (`tasks/refinements/README.md:57-68`).

## Effort estimate

**1.5 days.** The seams all exist: the gateway, the folder dialog, the MRU store, the
compose-a-new-project modal and the offscreen e2e rig were all built by
`editor.project.open_ui`. The work is one new `dock` chrome type, one L4 mode function
plus its selecting predicate, a mechanical base-class split of `AppProjectGateway`, an
extract-and-forward refactor of the New-Project modal, and the tests. The half-day over
a plain 1d is the two doc deltas (A7 is being refined) and the four-case e2e.

## Inherited dependencies

**Settled (from `editor.project.open_ui`, `tasks/refinements/editor/open_ui.md`).**

1. **`dock::ProjectGateway` is the one seam** for project-entry verbs (`D-open_ui-2`,
   **A12**): L3 `dock` declares it (`src/dock/ace/dock/dock.hpp:80-104`), L4 `app`
   implements it (`src/app/ace/app/project_gateway.hpp:34`), POD crosses, `dock` never
   includes `<ace/commands/…>` or `<ace/platform/…>`. New verbs are *added virtuals*, not
   new seams.
2. **Every entry verb spawns a detached sibling and never opens in-process**
   (`D-open_ui-1`, `D-exec_new-1`): `AppProjectGateway::spawn`
   (`src/app/project_gateway.cpp:39-44`) → `commands::open_another_project`
   (`src/commands/ace/commands/exec_new.hpp:25`) → `posix_spawn` + `POSIX_SPAWN_SETSID`.
3. **New composes a not-yet-existing target** (`D-open_ui-4`) from a picked parent plus a
   typed name; the *sibling's* bootstrap create-vs-open branch scaffolds it
   (`commands::open_or_create_app_state`, `src/commands/app_state.cpp:31-45`).
4. **`is_project_directory` pre-validates Open** (`D-open_ui-5`) and doubles as the
   `RecentProjects::load` validator, which is how `dockmodel` (L1, `{base, platform}`)
   reaches a `project` predicate without an edge.
5. **The MRU store** is `dockmodel::RecentProjects`
   (`src/dockmodel/ace/dockmodel/recent_projects.hpp:26-57`, `D-open_ui-3` / **D21**):
   cap 10, most-recent-first, pruned-and-self-healing on load, published via
   `FileSystem::atomic_replace`, rooted at `workspace_prefs_root().parent_path()`
   (`src/app/shell.cpp:321-322`).
6. **Widget-id house rule** (`src/dock/dock.cpp:280-290`): stable, slash-free `###` ids;
   the visible label carries the ellipsis or the path, the e2e drives the id.
7. **The offscreen e2e rig** (`tests/open_ui_e2e_test.cpp:98-219`): `Shell::init` with
   `headless = true`, a `FakeGateway` delivering `pick_folder` **synchronously**, a
   hand-rolled `new_frame`/`draw_ui`/`render`/`PostSwap` pump bounded by
   `k_max_frames = 400`, and `ScreenCaptureFunc = capture_pixels`.

**Pending (this leaf owns them).**

1. Deciding, once and before any filesystem work, whether the process is a **project
   process** or a **launcher process** — and proving the launcher creates no `Document`.
2. A `dock` chrome surface that hosts the three verbs **outside** a `Dockspace`.
3. Splitting `AppProjectGateway` so an entry-only gateway can exist without an
   `AppState`.
4. De-duplicating the New-Project compose modal now that it has two hosts.
5. The two doc deltas (**D26**, **A22**) that make the zero-`Document` process legal.

## What this task is

Today a bare `arbitraryeditor` with no arguments always ends up owning a project: `main`
parses zero positionals into an empty `ShellOptions::project_dir`
(`src/app/args.cpp:7-19`), and `run_editor` turns that into a randomly-named scratch
project under the OS temp directory (`src/app/shell.cpp:70-74`, `:203-204`) so the
"one process owns exactly one `Document`, never empty" invariant holds
(`D-app_state-6`). That default was decided when no picker existed
(`tasks/refinements/editor/app_state.md`, whose Open questions parked "what a no-project
first launch should show once a picker exists" for exactly this leaf).

This task replaces that default **for interactive launches only**. A no-arg,
non-headless launch now runs the editor in a second process mode — a **pre-project
launcher** that owns **zero** `Document`s and whose window contains one thing: a welcome
offering the same three verbs the tool rail carries (**New Project…**, **Open
Project…**, **Open Recent**), driven through the same `dock::ProjectGateway`. Choosing
any verb spawns a detached sibling editor on the chosen (or newly composed) project
directory and the launcher exits; dismissing the welcome without choosing exits too.
Headless no-arg is untouched and keeps the scratch bootstrap, so every driven path still
holds a `Document`.

## Why it needs to be done

Three chains converge here.

**The litter.** Every interactive launch currently scaffolds a real project directory
with a live `workspace/` mmap arena under `/tmp`, and the user's first act — New or Open
— *spawns a sibling* (D22/D19) rather than reusing it, orphaning the scratch process and
its directory. The scratch default was a bootstrap convenience, not a UX decision;
`open_ui` shipping the picker is what makes it removable.

**The empty home base.** D22 puts the entry affordances on the tool rail, and D18 makes
the rail home base *of a project window*. A process with no project has no rail, no
dockspace and no canvas — so without this leaf there is no surface on which a
project-less process could offer anything, which is precisely why the scratch project
had to exist.

**Downstream.** `editor.project.dir_is_project` (`tasks/00-editor.tji:157-161`) will
tighten New and Save As to refuse any existing target. The welcome's New must therefore
go through the *same* `ProjectGateway::new_project` virtual and the *same* compose modal
the rail uses, so that leaf tightens one implementation and both hosts inherit it.

## Inputs / context

**Design docs (normative — the constitution).**

- **D16** `docs/00-design.md:483` — "A project is a **directory** with the library's
  canonical layout"; §9 prose `:416-418` lists the verbs "New / Open (a project folder) /
  Save … recent projects".
- **D19** `docs/00-design.md:486` — "**One process = one project**: opening a different
  project is a new **`exec`** … no in-process switching; single-project for its whole
  lifetime."
- **D22** `docs/00-design.md:489` — the three verbs, their semantics (Open picks an
  *existing* directory; New composes a *not-yet-existing* one from a parent + a name),
  and the MRU-in-prefs rule. This leaf re-hosts that set unchanged.
- **D26** `docs/00-design.md:493` — **this leaf's doc delta**: welcome on a project-less
  launch; three verbs only; dismiss = clean exit; headless keeps scratch.
- **A7** `docs/01-architecture.md:369` — "the app owns exactly one `Document` for its
  lifetime". Refined, not broken, by A22.
- **A12** `docs/01-architecture.md:374` — the `dock`-declared `ProjectGateway`, its L4
  `app` impl, and `EXTERNAL_ALLOWED["sdl"] = {app}`.
- **A22** `docs/01-architecture.md:384` — **this leaf's doc delta**: the two process
  modes, the pure selecting predicate, the `dock::WelcomeWindow` host, and the
  `ProjectEntryGateway` / `AppProjectGateway` split.
- **§8** `docs/01-architecture.md:256-291` — the DAG. `dock` may depend only on
  `dockmodel`, `views`, `imgui`; `app` on everything; `sdl` is `app`-only
  (`scripts/check_levels.py:21-40`, `:45-56`).
- **§9** `docs/01-architecture.md:293-320` — the layered DoD this leaf's Acceptance
  criteria instantiate.

**Library API surface.** None. This leaf touches no libarbc type: the launcher's whole
point is that it never constructs a `Document`.

**Source seams this leaf extends.**

- `src/app/ace/app/shell.hpp:30-51` — `ShellOptions` (`headless`, `max_frames`,
  `project_dir`, `project_gateway`). `:108-109` — `run_editor`'s signature and its
  documented 0/1/2 return contract (`:99-107`).
- `src/app/shell.cpp:70-74` — `scratch_project_dir()`; `:55-63` —
  `workspace_prefs_root()`; both file-local, both reachable from a new mode function in
  the same TU.
- `src/app/shell.cpp:80-130` — `Shell::init`: SDL hint → `SDL_Init` → GL attrs → window →
  GL context → ImGui context/backends. **Every step is `Document`-free**, which is what
  makes launcher mode a subtraction rather than a parallel bring-up.
- `src/app/shell.cpp:192-232` — `run_editor`'s head: the project-dir resolution
  (`:203-204`) and the writer-thread-then-`AppState` sequence (`:213-226`) the launcher
  must branch **before**.
- `src/app/shell.cpp:294-325` — where `Dockspace`, `WorkspaceStore`, `RecentProjects`,
  `SdlFolderDialog` and `AppProjectGateway` are built, and the
  `opts.project_gateway == nullptr` injection branch the launcher mirrors.
- `src/app/shell.cpp:412-420` — the `set_draw_content` + `should_continue_loop` pump the
  launcher reuses (`src/app/ace/app/app_loop.hpp:13`).
- `src/app/ace/app/project_gateway.hpp:34-166`, `src/app/project_gateway.cpp:39-84` — the
  five session-free entry verbs plus the private `spawn`; everything else takes
  `app_state_` (`hpp:157`).
- `src/dock/ace/dock/dock.hpp:80-104` — the five entry virtuals, verbatim; `:111-146` the
  pure session virtuals; `:157-246` the non-pure ones with inert defaults.
- `src/dock/dock.cpp:56-84` — `draw_new_project_modal`, popup id `"New Project"`, refs
  `New Project/Name`, `New Project/Create`, `New Project/Cancel`.
- `src/dock/dock.cpp:284-367` — `draw_project_section`: `###new_project` (`:323`),
  `###open_project` (`:331`), `###recent<i>` (`:349-352`), the two feedback strings
  (`:340`, `:355`), and `recent_projects()` queried every frame (`:344`).
- `src/dock/dock.cpp:425` — `tool_rail_title()`, the precedent for a stable
  window-title accessor an e2e composes refs from.
- `src/app/folder_dialog.cpp:24-30` — the `SdlFolderDialog` destructor detaching
  in-flight requests by nulling `owner`.

**Test rigs.**

- `tests/open_ui_e2e_test.cpp` — the rig (`:98-219`), the `FakeGateway`
  (`:34-82`), `rail_ref` (`:123-124`), and at `:222-235` the sole precedent for a Catch2
  `TEST_CASE` in the same file that drives a **real** shell entry point with an injected
  gateway.
- `tests/reopen_degradation_notice_e2e_test.cpp:111,185` — the precedent for two
  `IM_REGISTER_TEST`s in one file, each in its own `TEST_CASE` with its own rig.
- `tests/app_project_gateway_test.cpp` (`CMakeLists.txt:275`) — headless L4 gateway
  units with a fake `ProcessLauncher`, a real temp `FileSystem`, a real `RecentProjects`
  and a scriptable fake dialog; `save_as` appended to it rather than forking a file.
- `tests/project_entry_test.cpp`, `tests/recent_projects_test.cpp`
  (`CMakeLists.txt:238`), `tests/exec_new_test.cpp` (`:237`) — the already-shipped L1
  coverage of every predicate/store/spawn primitive this leaf consumes.
- `CMakeLists.txt:270-294` — `ace_shell_test`'s source list; `:304-306` — its offscreen
  SDL/Mesa environment.

## Constraints / requirements

1. **The mode is decided by a pure predicate, before any work (A22).** Add
   `bool wants_welcome_launcher(const ShellOptions& opts)` to `src/app/ace/app/shell.hpp`,
   defined as `opts.project_dir.empty() && !opts.headless` — exactly the two facts the
   `.tji` note names. `run_editor` dispatches on it as its **first** statement after
   entry, ahead of `Shell::init`, the `NativeFileSystem`, `scratch_project_dir()` and the
   `WriterThread`. Keeping it a free predicate (rather than an `if` buried in
   `run_editor`) is what makes the mode selection unit-testable without standing up SDL.
2. **Launcher mode owns zero `Document`s, structurally.** `int run_welcome_launcher(const
   ShellOptions&)` (public in `shell.hpp`, defined in `shell.cpp`) constructs **no**
   `commands::AppState`, no `writer::WriterThread`, no `render`/`CanvasView`, no
   `dock::Dockspace`, no `commands::ExportService`, registers no view bodies and runs no
   on-close `gc_project`. It reuses `Shell::init`/`new_frame`/`draw_ui`/`render`/
   `shutdown` and `should_continue_loop` unchanged. `run_editor`'s existing 0/1/2 return
   contract is not widened: the launcher returns **1** if `Shell::init` fails and **0**
   otherwise — spawn failures surface as in-window feedback, not as an exit code, and
   there is no project to fail to open so **2** is unreachable.
3. **Headless no-arg is unchanged.** `opts.headless = true` with an empty `project_dir`
   keeps `D-app_state-6`'s scratch bootstrap verbatim. This is load-bearing for the
   shipped suite: `tests/open_ui_e2e_test.cpp:222-235` and the offscreen smoke both call
   `run_editor` with `headless = true` and no `project_dir` and must keep creating a real
   session. No shipped test may change behaviour as a result of this leaf.
4. **The welcome is `dock` chrome, not a `Dockspace` feature (§8/A12).** Add
   `src/dock/ace/dock/welcome.hpp` + `src/dock/welcome.cpp` in the **existing** `dock`
   component. `WelcomeWindow` holds a non-owning `ProjectGateway*` (`set_project_gateway`
   / `project_gateway()`, mirroring `Dockspace`'s injection style,
   `dock.hpp:315-316`), draws once per frame, and exposes
   `bool exit_requested() const`. It includes only its own component's headers, `imgui`
   and std — no `commands`, no `platform`, no SDL.
5. **Modal in the UX sense, a full-viewport window in the implementation (D26).** Draw
   `Begin(welcome_window_title(), nullptr, NoDecoration | NoMove | NoResize |
   NoSavedSettings | NoBringToFrontOnFocus)` positioned and sized to
   `GetMainViewport()`. Do **not** use `BeginPopupModal` for the welcome itself: New's
   compose popup is a real `BeginPopupModal` and must open above it, and stacking modals
   is the fiddly route to an identical picture. Expose
   `const char* welcome_window_title()` returning `"Welcome"` — the `tool_rail_title()`
   precedent (`dock.cpp:425`) — so the e2e composes refs from one symbol.
6. **Exactly D22's three verbs, with the established id discipline.**
   `New Project…###welcome_new`, `Open Project…###welcome_open`, and one
   `<path>###welcome_recent<i>` per entry under a plain `"Open Recent"` header drawn only
   when the list is non-empty. No scratch verb, no skip verb, no Quit button, no
   remembered default (D26). Feedback is a `std::string&` accessor rendered with
   `TextWrapped`, reusing the rail's exact strings — `"That folder is not a project."`
   (`dock.cpp:340`) and `"That project is no longer available."` (`dock.cpp:355`) — plus
   `"Could not start the editor."` when a validated verb's `spawn` returns false.
7. **Verb semantics mirror `draw_project_section` exactly** (`dock.cpp:284-367`): New →
   `pick_folder` → on a pick, open the compose modal on that parent; Open →
   `pick_folder` → on a pick, `open_project(dir)`, feedback on false; Recent →
   `open_recent(dir)`, feedback on false. A **cancelled** pick (`std::nullopt`) does
   nothing at all: no feedback, no spawn, and **no exit** — the user is still choosing.
   `recent_projects()` is queried each frame, as the rail does.
8. **Exit is a latch, set only by a verb that actually spawned, or by an explicit
   dismissal (D26).** `exit_requested()` flips true when `open_project`, `open_recent` or
   `new_project` returns true, or when the user dismisses: `shell.quit_requested()` (the
   OS window close) or `ImGuiKey_Escape` pressed while the compose modal is **not** open
   (a `p_open == nullptr` modal does not consume `Esc`, so an unguarded check would exit
   the launcher out from under the New dialog). The frame loop runs while
   `should_continue_loop(frames, opts.max_frames, shell.quit_requested() ||
   welcome.exit_requested())`.
9. **Split the L4 gateway rather than nulling its `AppState` (A22).** Introduce
   `ace::app::ProjectEntryGateway : public dock::ProjectGateway` in
   `src/app/ace/app/project_gateway.hpp`, holding `RecentProjects&`,
   `const FileSystem&`, `FolderDialog&`, `const ProcessLauncher&` and the executable
   path; implementing `open_project`, `new_project`, `open_recent`, `pick_folder`,
   `recent_projects` and the private `spawn` (bodies **moved**, not copied, from
   `project_gateway.cpp:39-84`); and answering the nine pure session virtuals
   (`save`, `is_dirty`, `save_as`, `clean_up`, `undo`, `redo`, `can_undo`, `can_redo`)
   inertly — `false` / `{}` / no-op. `AppProjectGateway final : public
   ProjectEntryGateway` keeps its current constructor signature and overrides every
   session verb, so **no existing call site changes**. Launcher mode constructs the base.
10. **De-duplicate the compose modal by extraction, not by copy.** Move
    `Dockspace`'s New-Project state (`dock.hpp:321-330`) into a dock-local
    `NewProjectModal` value and refactor `draw_new_project_modal`
    (`dock.cpp:56-84`) to `bool draw_new_project_modal(NewProjectModal&,
    ProjectGateway&, std::string& feedback)` returning whether a project was created
    this frame. `Dockspace` keeps its six existing accessors as forwarders to a
    `NewProjectModal` member — **zero public-API change**, so no shipped test churns —
    and `WelcomeWindow` holds a second instance and latches exit on a `true` return.
    The popup id, its three refs and its behaviour (empty name → `"Enter a valid project
    name."`, modal stays open) are unchanged.
11. **Levelization (§8).** No new component and no new DAG edge: `welcome.{hpp,cpp}` is
    `dock`, the gateway base is `app`. `scripts/check_levels.py` is not edited — neither
    `ALLOWED` nor `EXTERNAL_ALLOWED`.
12. **Teardown ordering.** Scope the launcher's `RecentProjects`, `SdlFolderDialog` and
    gateway inside a block that closes **before** `shell.shutdown()`, so a folder-dialog
    request still in flight when the user closes the launcher window is detached by
    `~SdlFolderDialog` (`folder_dialog.cpp:24-30`) while SDL is still up. This is a local
    choice for the new function; `run_editor`'s existing ordering is not touched.
13. **Comment deltas that keep the source honest.** `ShellOptions::project_dir`'s comment
    (`shell.hpp:45`) and `run_editor`'s contract comment (`:99-107`) both currently state
    the scratch-when-empty rule unconditionally; both must be amended to name the
    headless qualifier and point at A22/D26. Likewise `scratch_project_dir()`'s comment
    (`shell.cpp:68-69`) already says "until (editor.project.open_ui) exists" — update it
    to the shipped rule.
14. **Scope boundary.** No change to `parse_args`/`main` (a bare `arbitraryeditor`
    already yields an empty `project_dir` and `headless = false`); no new argv flag; no
    change to `commands::open_another_project`, `RecentProjects`, `is_project_directory`
    or `open_or_create_app_state`; no change to the rail; no branding, splash art, window
    sizing/persistence or animation; no WASM launcher (A3 — reachable at the same
    gateway seam by construction, not a WBS leaf); and no relaxation of
    `editor.project.dir_is_project`'s pending target rules, which this leaf inherits
    through the shared `new_project` virtual.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate` green
(check_levels · clang-format · build · ctest · coverage) is the umbrella. Specifically:

- **Levelization (`check_levels` clean).** `src/dock/welcome.{hpp,cpp}` includes only
  `ace/dock/…`, `ace/dockmodel/…`, `imgui` and std — no `ace/commands/`,
  `ace/platform/`, `ace/project/`, `arbc/` or SDL. The `ProjectEntryGateway` base is
  `app`, the only level permitted SDL. `scripts/check_levels.py` is byte-unchanged: no
  `ALLOWED` edit, no `EXTERNAL_ALLOWED` edit, no new component directory.

- **L1 logic — Catch2: N/A (justified).** This leaf adds no L1 code. Every L1 primitive
  it drives is already pinned: `project::is_project_directory` and the New-target
  composer by `tests/project_entry_test.cpp`; the MRU cap/prune/round-trip/corrupt-store
  matrix by `tests/recent_projects_test.cpp`; `commands::open_another_project`'s
  canonicalize/empty-reject/spawn contract by `tests/exec_new_test.cpp` (all
  `ace_tests`, `CMakeLists.txt:237-238`). Stated explicitly so the omission reads as
  scoped, not skipped — the new logic is L3 chrome and L4 bootstrap, tested below.

- **L4 headless units — extend `tests/app_project_gateway_test.cpp`** (`ace_shell_test`,
  `CMakeLists.txt:275`), two new `TEST_CASE`s over the existing fake-`ProcessLauncher` +
  temp-`FileSystem` + real-`RecentProjects` + fake-dialog rig:
  1. *"a session-free `ProjectEntryGateway` validates, records and spawns"* — construct
     the **base** with no `AppState` and assert all four entry paths behave identically
     to the shipped `AppProjectGateway` cases: open an existing project spawns and
     MRU-fronts; open a non-project returns false and spawns nothing; `new_project`
     composes an absolute target from parent + name and spawns; `open_recent` re-orders
     and spawns.
  2. *"a session-free gateway answers every session verb inertly"* — `save()`,
     `is_dirty()`, `can_undo()`, `can_redo()`, `undo()`, `redo()` are `false`,
     `clean_up(true)`/`clean_up(false)` return a zeroed `GcSummary`, `save_as()` is a
     no-op, and the inherited defaults (`insert_kinds()`, `can_delete()`,
     `reopen_unbindable_count()`, …) stay neutral. This is the type-level statement of
     A22's invariant and it covers every new line of the base's session block.

- **New file `tests/welcome_e2e_test.cpp`** (`ace_shell_test`; one source line appended
  inside `CMakeLists.txt:270-294`), four `TEST_CASE`s:
  1. **ImGui Test Engine e2e** — `IM_REGISTER_TEST(engine, "welcome",
     "three_verbs_and_feedback")` on the `open_ui` rig (`headless = true`, 640×480,
     `FakeGateway` delivering `pick_folder` synchronously, hand-rolled pump bounded by
     `k_max_frames`), driving a standalone `dock::WelcomeWindow` — **no `AppState`, no
     `Dockspace`, no `WriterThread`** — via a `welcome_ref` helper composed from
     `welcome_window_title()`. Ordered so the non-latching cases run first: recents are
     listed (`recent_queries > 0` and `###welcome_recent0` exists); a **cancelled** pick
     records a pick, spawns nothing and leaves `exit_requested()` false; a non-project
     pick sets `"That folder is not a project."` and does not exit; a vanished
     `###welcome_recent0` sets `"That project is no longer available."` and does not
     exit; `###welcome_new` → pick → empty name → `New Project/Create` is refused, the
     modal stays open and nothing exits; then a valid name → `Create` records the
     created target **and** `exit_requested()` is true. A screenshot baseline is captured
     through the rig's `capture_pixels`.
  2. **ImGui Test Engine e2e** — `IM_REGISTER_TEST(engine, "welcome", "dismiss_exits")`
     in its own rig with its own fresh `WelcomeWindow` (the two-test-two-rig pattern of
     `tests/reopen_degradation_notice_e2e_test.cpp:111,185`, needed because the exit
     latch is one-way): pressing `Esc` with no popup open sets `exit_requested()` while
     `opened`/`created`/`replayed` all stay empty — dismissal exits and spawns nothing
     (D26). A second phase opens the compose modal, presses `Esc`, and asserts the
     launcher did **not** latch (Constraint 8's guard).
  3. **Launcher mode runs with zero `Document`s** —
     `TEST_CASE("welcome: run_welcome_launcher drives a no-arg launch without a
     Document")`, the `tests/open_ui_e2e_test.cpp:222-235` precedent: call
     `run_welcome_launcher` directly with `headless = true`, `max_frames = 3`,
     `project_gateway = &fake`; assert it returns `0`, that the fake saw
     `recent_queries > 0` (the welcome really drew), and that **no new
     `arbitraryeditor-session-*` directory appeared** under
     `std::filesystem::temp_directory_path()` across the call — a prefix-scoped
     before/after listing, which is the one externally observable proof that
     `open_or_create_app_state` was never reached.
  4. **Mode selection** — `TEST_CASE("welcome: wants_welcome_launcher picks launcher mode
     only for an interactive no-arg launch")`: the full truth table — empty +
     `!headless` → true; empty + `headless` → false; a `project_dir` + `!headless` →
     false; a `project_dir` + `headless` → false. Pure, no SDL.

- **Regression guard — the shipped headless path is untouched.**
  `tests/open_ui_e2e_test.cpp:222-235` (`run_editor` with `headless = true` and an empty
  `project_dir`) and `tests/shell_smoke_test.cpp` must pass **unmodified**, which is the
  executable form of Constraint 3.

- **Rendered output — golden: N/A (justified).** The launcher composes chrome and owns no
  `Document`; there is nothing for `render_offline` to compare against. Software-GL
  pixels are flaky, so the visual assertion is the Test Engine screenshot baseline
  (`capture_pixels`), never a byte-exact golden — the same justification `open_ui` and
  `tool_rail` recorded.

- **Threading (ASan/TSan).** **No new thread**: launcher mode starts neither the
  `WriterThread` nor a `HousekeepingThread` nor `ExportService`, so it strictly reduces
  the concurrent surface. *Scope note:* the one hazard this leaf makes newly reachable is
  **quitting with a folder-dialog request in flight** — a launcher can be dismissed
  seconds after it starts, where the long-lived project window rarely is. Constraint 12's
  scoping plus `~SdlFolderDialog`'s detach (`folder_dialog.cpp:24-30`) is the fix, and
  the `clang-asan` lane (`docs/01-architecture.md` §9.1) running `ace_shell_test` —
  including the new `run_welcome_launcher` case — is where it is exercised. No new
  `tests/lsan.supp` entry is expected.

- **Coverage.** ≥90% diff coverage on changed lines (`coverage` preset,
  `diff-cover --fail-under=90`). The branches that must be reached by name:
  `wants_welcome_launcher`'s four input combinations; `run_welcome_launcher`'s
  init-failure return and its clean return; the injected-vs-real gateway branch; every
  verb outcome in `WelcomeWindow::draw` (pick-cancelled, open-succeeded,
  open-refused, recent-succeeded, recent-vanished, spawn-failed, create-succeeded,
  create-refused); the `Esc`-guard's both sides; and every inert session verb on
  `ProjectEntryGateway`.

- **Format / build.** `clang-format --dry-run --Werror` clean on all touched files; `dev`
  and `release` presets build clean; `scripts/gate` green.

**No follow-up WBS task is deferred.** The two consumers that could look like debt are
already scheduled or already excluded: tightening New's target rules is
`editor.project.dir_is_project` (`tasks/00-editor.tji:157-161`), which this leaf inherits
for free through the shared `new_project` virtual and the single extracted compose modal;
and the WASM launcher is out of scope by **A3**, reachable at the same `ProjectGateway`
seam by construction rather than as a leaf (the `open_ui` precedent). The welcome's
visual treatment — branding, splash art, remembered window geometry — is a design
judgement call, not agent-implementable work, and goes to `tasks/parking-lot.md`.

## Decisions

**D-welcome-1 — A process is in exactly one of two modes, chosen once by a pure
predicate, and never transitions.** `wants_welcome_launcher(opts)` is
`project_dir.empty() && !headless`; `run_editor` dispatches on it before any filesystem,
thread or libarbc work, and launcher mode never mints an `AppState`. A7 keeps governing
every project process verbatim; the launcher is simply the one process shape that holds
no project (doc delta **A22**, `docs/01-architecture.md:384`).

*Rationale:* it makes "zero `Document`s" structural rather than a runtime check — the
code that would create one is not on the path — and it keeps the invariant statable in
one sentence per mode. Evaluating a *pure predicate* rather than an inline `if` lets the
mode rule be unit-tested without SDL, which is the only cheap way to pin the headless
qualifier that Constraint 3 depends on.

*Alternative rejected:* keep `D-app_state-6`'s scratch default for interactive launches
and put the welcome inside that scratch project's window. It needs no new mode and no doc
delta, but it mints a project directory and a live `workspace/` mmap arena on every
curiosity launch, and the very first verb spawns a sibling and orphans that process —
which is the litter this leaf exists to remove. *Alternative rejected:* let the launcher
open the chosen project **in-process** and become the project window. One process instead
of two, but it is a direct D19/A7 violation and would make `Document` lifetime lazy for
every consumer that today may assume it is bound for the process's life.

**D-welcome-2 — This SUPERSEDES `D-app_state-6` for interactive launches only; headless
no-arg keeps the scratch bootstrap verbatim.** `opts.headless` is the qualifier, exactly
as the `.tji` note specifies.

*Rationale:* a driven run has no user to answer a welcome, and every e2e and the
offscreen smoke depend on "no path given ⇒ a real session comes up" — including
`tests/open_ui_e2e_test.cpp:222-235`, which asserts `run_editor` threads an injected
gateway into the rail with an empty `project_dir`. Gating on `headless` means **zero
shipped tests change**, which is the strongest available evidence that the new mode is
additive.

*Alternative rejected:* introduce an explicit `--welcome` / `--no-welcome` argv flag and
key on that. It is more discoverable, but `parse_args` (`src/app/args.cpp:7-19`)
deliberately exposes only a positional project directory, and a flag would let a
headless run select a mode that has no user to drive it — a way to hang CI, in exchange
for an option nobody asked for. *Alternative rejected:* key on "is a display attached"
instead of `opts.headless`. It probes the environment rather than reading the caller's
stated intent, and `Shell::init` already treats `headless` as *the* windowed-vs-offscreen
switch (`shell.cpp:82-86`, `:101-102`, `:112`).

**D-welcome-3 — The welcome is a `dock` chrome type hosting the same
`ProjectGateway`, not a `Dockspace` mode and not a new component.**
`dock::WelcomeWindow` (`src/dock/ace/dock/welcome.hpp`) takes a `ProjectGateway*` through
the same non-owning `set_*` injection `Dockspace` uses, and the launcher wires it to a
`ProjectEntryGateway` built in L4.

*Rationale:* A12's inversion already exists precisely so L3 chrome can reach process
launch and SDL without an edge; a second host for the same abstraction costs one header
and one TU and inherits the whole seam — validation, MRU, spawn, the async pick — for
free. `check_levels` stays untouched.

*Alternative rejected:* a "no project" state inside `Dockspace`. It would put the
launcher's lifetime inside a type whose entire job (a recursive split-tree of view tabs
plus a rail) is meaningless without a project, and every `Dockspace` consumer would grow
a null-project branch. *Alternative rejected:* a new L3 component for the launcher — the
DAG edge is identical to `dock`'s, so a new component buys a `check_levels` table edit
and nothing else; `writer` earned its component by being structurally different, this is
not.

**D-welcome-4 — The welcome is modal in the UX sense but a full-viewport window in the
implementation.** A viewport-sized, undecorated `Begin`/`End` — not `BeginPopupModal` —
titled `"Welcome"`, exposed as `welcome_window_title()`.

*Rationale:* the launcher window contains nothing else — no dockspace, no rail, no canvas
— so it is already unreachable-behind, which is what "modal" buys. Meanwhile New's
compose dialog **is** a genuine `BeginPopupModal` that must open above the welcome, and
ImGui's popup stack makes modal-over-modal the fiddly path to a picture identical to the
one a plain window gives. Exposing the title as a function mirrors `tool_rail_title()`
(`dock.cpp:425`), so the e2e composes every ref from one symbol instead of a literal.

*Alternative rejected:* `BeginPopupModal` for the welcome itself, matching the note's
word "modal" literally — it adds a popup-stack ordering hazard for New's dialog and a
dim-background overlay over an empty window, buying nothing a user can perceive.

**D-welcome-5 — Exactly D22's three verbs; dismissal exits; nothing else.** No scratch
verb, no skip, no Quit button, no remembered default or auto-open-last-project in v1.
Dismissal is the OS window close or `Esc` (guarded on the compose modal not being open),
and it exits the launcher cleanly.

*Rationale:* both halves are the `.tji`'s pre-exec decisions (2026-07-19), and both hold
up against the constitution. A scratch/skip verb re-mints the throwaway project D-welcome-1
removes, and a remembered default takes the choice away at the one moment the user is
being asked to make it — while D22's MRU already puts the last project one click away. A
`Quit` button would be a fourth verb on a surface the note caps at three; closing the
window is the platform-native dismissal and is what `shell.quit_requested()` already
reports. The `Esc` guard is not decoration: a `BeginPopupModal` opened with
`p_open == nullptr` (`dock.cpp:63`) does not consume `Esc`, so an unguarded check would
exit the launcher out from under a half-typed project name.

*Alternative rejected:* fall back to the scratch project on dismissal, so the user always
lands somewhere. It sounds friendlier but re-introduces the stray project on the *most*
common accidental path (launch, look, close), and it makes the zero-`Document` invariant
conditional on user input rather than on the mode.

**D-welcome-6 — The L4 gateway splits into a session-free base and a session-owning
derived class, rather than gaining a nullable `AppState`.**
`app::ProjectEntryGateway` implements the five entry verbs plus `spawn` and answers the
session virtuals inertly; `AppProjectGateway final : ProjectEntryGateway` adds
`commands::AppState&` and overrides them. Existing constructor signature and call sites
are unchanged (doc delta **A22**).

*Rationale:* exactly five of `ProjectGateway`'s virtuals never touch `app_state_`
(`project_gateway.cpp:46-84`) and every other one does, so the split is a seam the code
already has — this only makes it a type. And a type is what keeps A22 absolute: a
launcher's gateway *cannot* reach a `Document` because it does not hold one. Moving the
bodies (not copying them) keeps one implementation of validate → MRU-front → `spawn`.

*Alternative rejected:* a nullable `commands::AppState*` with a guard on each session
verb — no new type, but roughly seventeen null branches that are unreachable in project
mode and unreached in launcher mode, i.e. dead lines the coverage gate would have to be
argued around. *Alternative rejected:* a standalone `LauncherProjectGateway` beside
`AppProjectGateway` — about twenty lines and zero churn to the existing class, but two
copies of the validate/MRU/spawn sequence, which is the one piece of this seam a silent
divergence would corrupt (a launcher that skips MRU-fronting, or validates differently
from the rail). *Alternative rejected:* demote the nine pure session virtuals in
`dock::ProjectGateway` to non-pure inert defaults, matching the newer virtuals' style
(`dock.hpp:157-246`). It removes the need for the base to implement anything, but it also
removes the compile-time obligation on the *real* session gateway, where a forgotten
override would silently no-op Save.

**D-welcome-7 — The New-Project compose modal is extracted to a shared dock-local value +
one draw function; `Dockspace` keeps its accessors as forwarders.**
`bool draw_new_project_modal(NewProjectModal&, ProjectGateway&, std::string& feedback)`
returns whether a project was created this frame; `Dockspace` ignores the return,
`WelcomeWindow` latches its exit on it.

*Rationale:* the welcome and the rail must compose New identically — same parent-plus-name
flow, same refusal string, same widget refs — and `editor.project.dir_is_project` is
about to tighten that flow; one implementation means it tightens once. Keeping
`Dockspace`'s six existing accessors as forwarders makes the refactor invisible to every
current caller and test, so the diff is additive.

*Alternative rejected:* copy the modal into `welcome.cpp`. It is the smaller diff today
and the two copies drift on the first behavioural change — which is already scheduled.
*Alternative rejected:* give `WelcomeWindow` a `Dockspace` just to borrow its modal state
— a whole dockspace, its layout tree and its rail, instantiated for one text buffer.

**D-welcome-8 — The launcher exits after a verb *succeeds*, and a cancelled pick is not a
choice.** `exit_requested()` latches only on a `true` from `open_project`,
`open_recent` or `new_project`; a `std::nullopt` pick, a refused target and a failed
spawn all leave the welcome up with feedback.

*Rationale:* the gateway's `bool` return already means "validated and spawned"
(`project_gateway.cpp:46-71`), so latching on it is precise: the sibling exists before the
launcher goes away. Treating a cancelled folder dialog as a dismissal would make `Esc` in
the OS picker close the whole application, which is the opposite of what cancel means
everywhere else. Leaving the window up on failure is what gives the two shipped feedback
strings somewhere to be read.

*Alternative rejected:* exit as soon as a verb is *invoked*, before its result is known.
Simpler control flow, but a user who picks a non-project folder would watch the editor
vanish with no explanation and no sibling.

## Open questions

_None — all decided against the constitution._

The mode split and the gateway shape are fixed by **A22** (`docs/01-architecture.md:384`),
itself a refinement of **A7** (`:369`) and constrained by **A12** (`:374`) and §8's DAG
(`:256-291`). The verb set, the dismissal semantics, the headless carve-out and the
"no remembered default" cap are fixed by **D26** (`docs/00-design.md:493`), which inherits
D22's verb semantics (`:489`) and D19's spawn-not-swap rule (`:486`) unchanged. The
in-process consequences — recents pruned on load, New composing a not-yet-existing
target, Open validating first — are `open_ui`'s settled decisions
(`D-open_ui-1`…`D-open_ui-5`) and are re-used, not re-decided.

**Doc delta (same-commit rule):** two rows, both written with this refinement and riding
the closer's commit — **D26** `docs/00-design.md:493` (the welcome's UX: three verbs,
dismiss = exit, headless keeps scratch) and **A22** `docs/01-architecture.md:384` (the
two process modes, the selecting predicate, the `dock::WelcomeWindow` host and the
`ProjectEntryGateway` split). Beyond those rows: no new dependency, no new component, no
new DAG edge, no new external, no libarbc pin bump.

**Parking-lot item (human judgment, not a WBS task):** the welcome's *visual* treatment —
product branding, splash art, whether the launcher window remembers its geometry, and
whether a first-run experience differs from a returning user's. All are design calls with
no correct answer derivable from the constitution, and none block the leaf.

## Status

**Done** — 2026-07-24.

- `src/dock/ace/dock/welcome.hpp` + `src/dock/welcome.cpp` — new `dock::WelcomeWindow` chrome type; full-viewport `"Welcome"` window hosting New / Open / Open Recent verbs via a `ProjectGateway*`; `exit_requested()` latch; `welcome_window_title()` accessor.
- `src/app/ace/app/project_gateway.hpp` + `src/app/project_gateway.cpp` — `app::ProjectEntryGateway` base class (session-free: five entry verbs + inert session overrides); `AppProjectGateway final : ProjectEntryGateway` keeps existing constructor and call sites unchanged.
- `src/app/ace/app/shell.hpp` + `src/app/shell.cpp` — `wants_welcome_launcher()` pure predicate (`project_dir.empty() && !headless`); `run_welcome_launcher()` zero-`Document` mode function; `run_editor` dispatches on the predicate as its first statement, before SDL / `NativeFileSystem` / `scratch_project_dir` / `WriterThread`.
- `src/dock/ace/dock/dock.hpp` + `src/dock/dock.cpp` — `NewProjectModal` extracted to dock-local struct; `draw_new_project_modal(NewProjectModal&, ProjectGateway&, std::string&)` free function returning `bool`; `Dockspace` accessors kept as forwarders (zero public-API change).
- `CMakeLists.txt` — `tests/welcome_e2e_test.cpp` appended to `ace_shell_test` source list.
- `tests/app_project_gateway_test.cpp` — two new `TEST_CASE`s: `"a session-free ProjectEntryGateway validates, records and spawns"` and `"a session-free gateway answers every session verb inertly"`.
- `tests/welcome_e2e_test.cpp` — four `TEST_CASE`s: `welcome/three_verbs_and_feedback` ImGui e2e; `welcome/dismiss_exits` ImGui e2e; `run_welcome_launcher` zero-`Document` drive; `wants_welcome_launcher` truth table.
- Doc deltas D26 (`docs/00-design.md:493`) and A22 (`docs/01-architecture.md:384`) written in-tree.
- One deviation: Constraint 6's `"Could not start the editor."` feedback is inferred from MRU state (a validated entry still present after a refusal = spawn failure) rather than a distinct outcome type at the seam; `editor.project.entry_outcome` registered as follow-up tech-debt.
