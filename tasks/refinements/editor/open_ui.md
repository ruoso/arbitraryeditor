# editor.project.open_ui — In-app New/Open affordance; recent projects; native folder picker

## TaskJuggler entry

`tasks/00-editor.tji:101-105` — `task open_ui "In-app New/Open affordance;
recent projects; native folder picker"` under `editor.project`. `effort 2d`,
`allocate team`, `depends !exec_new, editor.dock.tool_rail` (`:104`). The `note`
(`:105`) cites **Design D16, D19** and names the source-of-debt refinement
`tasks/refinements/editor/open.md`. This task has **no** `complete 100` today;
per the completion ritual (`tasks/refinements/README.md:47-72`) the closer adds
`complete 100` after the `allocate team` line, ends the `note` with
`Refinement: tasks/refinements/editor/open_ui.md`, registers the two doc deltas
(D22 / A12) landing in the same commit, then runs
`tj3 project.tjp 2>&1 | grep -iE "error|warning"` and confirms silence.

Milestone: this leaf is inside `editor.project`, on which **M9E**
(`tasks/99-milestones.tji:6-10`) already depends — a new sibling under the
container needs no milestone edit.

## Effort estimate

**2d** (from the `.tji`). This is the editor's **first user-driven
project-lifecycle surface**, and the task title bundles three things — New,
Open, and recent projects behind a native folder picker. The bulk of the effort
is the L1-testable pieces (the `dockmodel::RecentProjects` prefs store modeled on
`WorkspaceStore`, the `project` validate/compose helpers) plus the one genuinely
new architectural seam (`dock::ProjectGateway` + its L4 SDL-backed impl). No
libarbc surface, no new component, no rendering. The New/Open *terminal* actions
were already built: the child-side open/create (`editor.project.open`), the
sibling-`exec` primitive (`editor.project.exec_new`), and the rail chrome
(`editor.dock.tool_rail`) all landed — this leaf **wires** them together and adds
the picker + recent list.

## Inherited dependencies

**Settled (from `editor.project.exec_new`).** The whole "open a *different*
project" mechanism exists. `commands::open_another_project(const
platform::ProcessLauncher& launcher, const std::filesystem::path& executable,
const std::filesystem::path& project_dir)` (`src/commands/ace/commands/exec_new.hpp:25`,
impl `src/commands/exec_new.cpp`) rejects an empty `project_dir` with
`std::errc::invalid_argument` (launcher **not** called), else canonicalizes to
absolute (`std::filesystem::absolute` → `weakly_canonical`) and calls
`launcher.spawn_detached(executable, {resolved.string()})` — a **detached
sibling process, invoking editor left running** (D-exec_new-1). The launch
primitives: `platform::ProcessLauncher::spawn_detached`
(`src/platform/ace/platform/process_launcher.hpp:26`, native impl
`NativeProcessLauncher` `:35` via `posix_spawn` + `POSIX_SPAWN_SETSID` +
`SA_NOCLDWAIT`) and `platform::current_executable_path() ->
Result<std::filesystem::path>` (`:46`, reads `/proc/self/exe`). The **argv
contract** (`src/app/ace/app/args.hpp:22`, `parse_args`): zero positionals →
empty `project_dir` → scratch; exactly one → that project dir; `>1` → usage
error. exec_new's **Constraint 6** explicitly scoped to *this* leaf: "adding
`process_launcher()` to the injected `PlatformServices` the views receive, and
the actual 'Open…' widget that calls the action" — this refinement **refines**
that expectation (see A12 / Decision D-open_ui-2: the gateway supersedes bolting
faculties onto the shared services aggregate).

**Settled (from `editor.project.open`).** The child-side directory→`Document`
factories `project::open_project` / `create_project` → `platform::Result<OpenedProject>`
(`src/project/ace/project/project.hpp:99-107`) and the pure path resolver
`project::project_layout(root)` (`:64`, yields `canonical` = `<root>/project.arbc`,
`workspace_file` = `<root>/workspace/document.arbcws`, etc.). `OpenError`
(`:70`: `NotADirectory` / `NoProject` / `CorruptDocument` / `IoError`) is a value,
never thrown (D-open-6). **This leaf never calls these in-process** (that would
mint a second `Document`, violating A7); it hands a *path* to a sibling `exec`
whose bootstrap calls them — see Decision D-open_ui-1.

**Settled (from `editor.project.app_state`).** The app owns exactly **one**
`Document` for its lifetime (A7/D19); bootstrap resolves the project dir via
`commands::open_or_create_app_state(const platform::FileSystem&, const
std::filesystem::path& root)` (`src/commands/ace/commands/app_state.hpp:97`),
which **branches on `fs.exists(root)`**: existing → `open_project`, missing →
`create_project` (D-app_state-6). This is the branch the New flow relies on: a
sibling `exec`ed at a *non-existent* path scaffolds it. The app currently
constructs `NativeFileSystem` directly and calls this at
`src/app/shell.cpp:189-199`; the process's single session is fixed for the run.

**Settled (from `editor.dock.tool_rail`).** The fixed left "home base" rail is
built: `dock::draw_tool_rail(Dockspace&)` (`src/dock/ace/dock/dock.hpp:106`, impl
`src/dock/dock.cpp:57`) draws the rail's **Tools / Views / Workspaces** sections;
the Selectable-action pattern is `if (ImGui::Selectable(label)) { <action>; }`
(`src/dock/dock.cpp:79-84`). `tool_rail_title()` (`dock.hpp:98`) is the stable
window id e2e drives by. The dependency-injection precedent this leaf mirrors:
`Dockspace::set_workspace_store(WorkspaceStore*)` (`dock.hpp:69`), a bare pointer
set at bootstrap (`shell.cpp:223-224`). There is **no menu bar anywhere**
(`BeginMainMenuBar`/`MenuItem` do not appear in `src/`) — the rail *is* the
chrome, so the New/Open/Recent affordances live in the rail (D22).

**Settled (from `editor.dock.workspaces`).** The per-user prefs store pattern:
`dockmodel::WorkspaceStore(std::filesystem::path root, platform::FileSystem&
filesystem)` (`src/dockmodel/ace/dockmodel/workspaces.hpp:55`) persists a single
**versioned line-oriented text file** (`workspaces.cpp:16` `k_store_filename`,
header `"ace-workspace-store 1"`), publishing via `FileSystem::atomic_replace`
(D16/D21). The prefs **root** is resolved at L4 by `workspace_prefs_root()`
(`src/app/shell.cpp:41`): `$XDG_CONFIG_HOME` else `~/.config` else temp, then
`/ "arbitraryeditor" / "workspaces"`. `RecentProjects` (this leaf) is the second
consumer of exactly this pattern.

**Pending (this leaf owns them).**
1. `dockmodel::RecentProjects` — the MRU prefs store (add / list / prune /
   persist), modeled on `WorkspaceStore`.
2. `project::is_project_directory(fs, root)` — a pure L1 predicate (does
   `<root>/project.arbc` or `<root>/workspace/document.arbcws` exist), used to
   validate an Open target and prune the recent list; and a pure
   New-target composer.
3. `dock::ProjectGateway` — the L3-declared abstraction the rail calls, and its
   L4 `app` concrete (`AppProjectGateway`) wiring the SDL folder dialog +
   `ProcessLauncher` + `current_executable_path` + `RecentProjects` +
   `open_another_project`.
4. The rail's **Project** section (New / Open / Open Recent) + the small
   **New Project** modal, wired via `Dockspace::set_project_gateway(...)`.
5. The L4 wiring in `shell.cpp` (construct the gateway, thread it into the
   dockspace) + a `ShellOptions` test seam to inject a fake gateway.

## What this task is

Give the running editor an in-app way to start work on a project — **New**,
**Open**, and **Open Recent** — surfaced on the D18 tool rail, backed by the
OS-native folder picker, and honoring **process-per-project** (D19/A7): each
affordance **spawns a fresh sibling editor process** rather than swapping the
single `Document` the current process owns for life. **Open** picks an existing
project directory and hands it to a sibling `exec`; **New** composes a
not-yet-existing target (chosen parent + a name) so the sibling's bootstrap
create-vs-open branch scaffolds it; **Open Recent** replays a directory from an
MRU list persisted to the per-user prefs store (pruned of directories that have
vanished or are no longer projects). The three terminal mechanisms already
exist (`open`, `exec_new`, `tool_rail`); this leaf composes them behind one
`dock::ProjectGateway` seam, adds the `dockmodel::RecentProjects` store and the
`project` validation helpers, and lands the picker + rail widgets. It writes no
libarbc code and mints no second `Document`.

## Why it needs to be done

Today a project path can only enter the editor at process startup — the launch
argv (`parse_args`, `editor.project.exec_new`) or the scratch fallback
(`shell.cpp:189-191`). A user who launches the editor bare (scratch project) has
**no way to open or create a real project from inside the app**: no menu, no
picker, no recent list. `docs/00-design.md:410-412` promises exactly "New /
Open (a project folder) / Save / Save As; recent projects" — the interactive
front door to the whole project lifecycle. `editor.project.open` explicitly
deferred this front door to *this* leaf (`open.md:432-448`, "Named future
task"), shipping the headless open/create core complete but with UI e2e marked
N/A because "the in-app New/Open menu + native folder picker + recent-projects
list is deferred to `editor.project.open_ui`". `exec_new` likewise shipped the
sibling-`exec` primitive but built no widget (Constraint 6). This leaf is where
those seams become a thing a user can click.

## Inputs / context

**Design docs (normative — the constitution).**
- **D16** `docs/00-design.md:477` — project = a directory (`project.arbc` +
  `assets/` + `workspace/`); the layout `is_project_directory` witnesses.
- **D18** `docs/00-design.md:479` — the uniform dockspace; the tool rail is the
  home base that "hosts modal tools and the view launcher (reopen anything)" —
  the chrome the Project section extends.
- **D19** `docs/00-design.md:480` — *"One process = one project: opening a
  different project is a new `exec` … no in-process switching … WASM analog: a
  project is a tab/instance."* The load-bearing constraint: New/Open **must**
  spawn, never swap.
- **D21** `docs/00-design.md:482` — the per-user prefs store shape
  (`platform::FileSystem::atomic_replace`, a versioned line-oriented text file,
  rebuild-from-default on a missing/corrupt store) that `RecentProjects` reuses.
- **D22** (this task's doc delta, `docs/00-design.md`) — In-app New / Open /
  Recent: spawn-not-swap, New composes a non-existent target, the pruned MRU
  recent list, the app-level picker seam.
- **§9 prose** `docs/00-design.md:410-412` — the verb list this realizes ("New /
  Open (a project folder) … recent projects").
- **A3** `docs/01-architecture.md:44-52,253` — the platform/SDL seam philosophy
  (native impl vs. a later web impl; the WASM folder picker = File System Access
  API). The gateway's WASM swap point.
- **A7** `docs/01-architecture.md:257` — process-per-project; one `Document` per
  lifetime. Why the picker cannot open in-process.
- **A8** `docs/01-architecture.md:258` + **§8** `:162-179` — the levelization
  DAG; only L3 (`views`/`dock`) sees ImGui, only L4 `app` sees SDL
  (`scripts/check_levels.py:44` `EXTERNAL_ALLOWED["sdl"] = {"app"}`).
- **A11** `docs/01-architecture.md:261` — `dock` owns the whole rail within the
  `dock → {dockmodel, views}` edges. The Project section joins it.
- **A12** (this task's doc delta, `docs/01-architecture.md`) — the
  `dock::ProjectGateway` seam, its L4 impl, and its refinement of exec_new
  Constraint 6.
- **§9 / A9** `docs/01-architecture.md:182-208` — the four-lane DoD instantiated
  under Acceptance criteria.

**Library API surface.** None. This leaf touches no `<arbc/...>` type — the
`Document` open/create is the sibling process's job.

**Source seams this leaf extends.**
- `src/dock/ace/dock/dock.hpp` / `src/dock/dock.cpp` — `class Dockspace`
  (`dock.hpp:36`), `draw_tool_rail` (`dock.hpp:106` / `dock.cpp:57`),
  `set_workspace_store` (`dock.hpp:69`, the DI precedent). **Adds**
  `ProjectGateway` (declared here), `Dockspace::set_project_gateway`, the
  Project rail section + New modal.
- `src/dockmodel/ace/dockmodel/workspaces.hpp` / `workspaces.cpp` — the
  `WorkspaceStore` template `RecentProjects` mirrors (ctor `(root,
  FileSystem&)`, atomic text persistence).
- `src/project/ace/project/project.hpp` — `project_layout` (`:64`),
  `OpenedProject` (`:84`); **adds** `is_project_directory` + the New-target
  composer (pure, L1).
- `src/commands/ace/commands/exec_new.hpp:25` — `open_another_project`, the
  terminal action the gateway calls.
- `src/platform/ace/platform/process_launcher.hpp` — `ProcessLauncher` (`:19`),
  `current_executable_path()` (`:46`).
- `src/app/ace/app/shell.hpp` / `src/app/shell.cpp` — `ShellOptions`,
  `run_editor`; `workspace_prefs_root()` (`shell.cpp:41`), the
  `set_workspace_store` wiring (`:223-224`). **Adds** the `AppProjectGateway`
  concrete (SDL dialog lives here — L4 is the only SDL-permitted level), a
  `SdlFolderDialog` wrapper, the gateway wiring, and a `ShellOptions`
  gateway-injection seam.
- `CMakeLists.txt` — `ace_tests` (`:173-183`, headless Catch2) gains the store /
  predicate units; `ace_shell_test` (`:192-207`, offscreen-SDL + software-GL)
  gains the gateway logic unit + the rail e2e.

**Test rigs.** `tests/workspaces_test.cpp` (the `WorkspaceStore` unit pattern —
the template for `RecentProjects`), `tests/platform_test.cpp` (`ScratchDir`
temp-dir helper), `tests/project_open_test.cpp` (the `project` unit rig),
`tests/tool_rail_e2e_test.cpp` (the ImGui Test Engine rail e2e — offscreen SDL +
software GL, drives the rail by stable button id via `run_editor`;
`ace_shell_test`), `tests/exec_new_test.cpp` (the fake-`ProcessLauncher`
recording pattern reused by the gateway unit).

## Constraints / requirements

1. **Spawn a sibling, never swap in-process (D19/A7).** New, Open, and Open
   Recent all terminate in `commands::open_another_project(launcher, exe, dir)`
   — a detached sibling `exec`. The current process's one `Document` (A7) is
   **never** replaced and `open_project`/`create_project` are **never** called
   in-process. The current window (even an untouched scratch) stays up (D19 tab
   analog). No path exists in this leaf that mints a second `Document`.

2. **New composes a non-existent target; the child creates it.** New does **not**
   `create_project` in-process. It composes a target directory from a chosen
   parent + a project name, asserts it does not already exist, and `exec`s a
   sibling there; the sibling's `open_or_create_app_state` sees the missing path
   and scaffolds it (D-app_state-6). Name/parent composition is a **pure** L1
   helper (rejects empty/invalid names, no path traversal escapes), unit-tested.

3. **Open validates before spawning.** Open pre-checks the picked directory with
   `project::is_project_directory(fs, dir)` (a pure existence check of
   `project.arbc` or the workspace file via `project_layout`); a non-project
   selection surfaces an in-app error and does **not** spawn a doomed process.

4. **Levelization — no new component, no new DAG edge (A8/A12).** `ProjectGateway`
   is a `dock` type (L3); its concrete impl is L4 `app`, which already closes
   over everything. `dock`'s includes stay within its own header + std (it does
   **not** include `<ace/commands/...>` or `<ace/platform/...>` — the gateway
   inverts that dependency). The **SDL folder dialog lives only in L4 `app`**
   (`check_levels.py:44`, `EXTERNAL_ALLOWED["sdl"] = {"app"}`);
   `RecentProjects` is L1 `dockmodel` (deps `{base, platform}`,
   `check_levels.py:29` — `FileSystem`-only, no ImGui/GL/SDL); the `project`
   helpers add only std includes. `check_levels` stays green with **zero**
   `ALLOWED`/`EXTERNAL_ALLOWED` edits.

5. **Native folder dialog is async, behind the gateway.** SDL3's
   `SDL_ShowOpenFolderDialog` delivers its result via a callback pumped on the
   UI thread on a later frame. `ProjectGateway::pick_folder(on_pick)` therefore
   returns immediately and invokes `on_pick(std::optional<path>)` later (or with
   `nullopt` on cancel). The gateway must **not** dangle: a pending request's
   userdata must outlive — or be safely detached before — the callback if the
   window/gateway is torn down (shutdown-cancel).

6. **Recent list = pruned MRU prefs (D21/D22).** `RecentProjects` persists a
   capped (N=10), most-recent-first list of absolute project directories to a
   single versioned text file under the `workspace_prefs_root()`-style prefs
   root, via `FileSystem::atomic_replace`. On load it **prunes** entries whose
   directory no longer exists or is no longer a project
   (`is_project_directory`), and a missing/corrupt store degrades to empty
   (rebuild-from-default, D21). Absolute paths only (canonicalized on add).

7. **Errors are values; no throws across seams.** `is_project_directory` returns
   `bool`; the New composer returns a `Result`/`optional`; the gateway methods
   report success/failure the rail renders as inline feedback. `atomic_replace`
   / `read_file` failures degrade gracefully (D-platform_services-6).

8. **Scope boundary.** No Save / Save As / dirty indicator (that is
   `editor.project.save`). No last-active per-project layout persistence (D21
   defers it to a later `editor.project.*`). No WASM `pick_folder` impl (A3 —
   WASM not scoped; the gateway seam keeps it reachable by construction). No
   change to the single-`Document`-per-process invariant.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`);
`scripts/gate` green (check_levels · clang-format · build · ctest · coverage) is
the umbrella. Specifically:

- **Levelization (`check_levels` clean).** `python3 scripts/check_levels.py`
  reports OK with **no** `ALLOWED`/`EXTERNAL_ALLOWED` edit: `ProjectGateway`
  and the rail additions are `dock` (L3, ImGui); the SDL folder dialog is L4
  `app` only; `RecentProjects` is L1 `dockmodel` (FileSystem-only); the
  `project` helpers add only std. No new component, no new DAG edge. This is the
  primary structural assertion, and it pins that `dock` did **not** gain a
  `<ace/commands/...>` or `<ace/platform/...>` include (A12 dependency
  inversion).
- **L1 logic — Catch2 units (the bulk), headless in `ace_tests`
  (`CMakeLists.txt:173-183`), reusing `ScratchDir`
  (`tests/platform_test.cpp`).**
  - `tests/recent_projects_test.cpp` (modeled on `tests/workspaces_test.cpp`):
    add pushes MRU-front and de-dups; the list caps at N=10 dropping the oldest;
    persistence round-trips through the versioned text file via a real temp
    `FileSystem`; load **prunes** a vanished / non-project directory; a missing
    store loads empty and a corrupt store degrades to empty (no throw); added
    paths are absolute.
  - `tests/project_entry_test.cpp` (joined to `ace_tests`, in `project`):
    `is_project_directory` is `true` for a scaffolded project (both the
    workspace-present and canonical-present cases), `false` for an empty dir, a
    non-existent path, and a non-directory; the New-target composer rejects an
    empty/invalid name and composes `parent / name` for a valid one.
- **Gateway logic — Catch2 unit in `ace_shell_test`** (the app-level suite, per
  the `exec_new_argv_test` precedent), `tests/app_project_gateway_test.cpp`,
  driving the concrete `AppProjectGateway` with a **fake `ProcessLauncher`**
  (records `(exe, args)`, as in `tests/exec_new_test.cpp`), a real temp
  `FileSystem`, a real `RecentProjects`, and a **scriptable fake folder
  dialog** — the SDL wrapper is *not* exercised here:
  - `open_project(existing_project_dir)` validates, records the dir MRU-front,
    and calls the launcher with `{weakly_canonical(dir)}`.
  - `open_project(non_project_dir)` returns failure and does **not** call the
    launcher.
  - `new_project(parent, "Name")` composes the (non-existent) target and calls
    the launcher with that absolute path; a launcher-recorded arg that does not
    yet exist is the create signal to the child.
  - `open_recent(dir)` re-orders MRU-front and spawns.
- **UI e2e — ImGui Test Engine, in `ace_shell_test`
  (`tests/open_ui_e2e_test.cpp`)**, offscreen SDL + software GL, mirroring
  `tests/tool_rail_e2e_test.cpp` and driving the rail **by stable button id**.
  A **fake `ProjectGateway`** (records calls; scripts a folder pick) is injected
  via the new `ShellOptions` gateway seam:
  - clicking **Open Project…** fires `pick_folder`; the fake returns a scripted
    project dir; assert the fake gateway recorded `open_project(dir)`.
  - clicking a **New Project** modal's Create (name entered by id) records
    `new_project(parent, name)`.
  - an **Open Recent ▸** entry (seeded via the injected recent list) records
    `open_recent(dir)`.
  - a **cancelled** pick (`nullopt`) spawns nothing.
  - The single-`Document`-per-process invariant is untouched — no e2e asserts a
    swap, because none happens (D19).
- **Rendered output — golden N/A (justified).** The rail Project section
  composes ImGui chrome, not a `Document`; like `tool_rail` (D-tool_rail e2e,
  no `render_offline` golden — software-GL pixels are flaky), a screenshot
  baseline is captured for signal but no byte-exact golden is committed.
- **Threading (ASan/TSan).** The **async dialog lifecycle** is the scope: the
  `open_ui_e2e` + gateway unit run under the `asan`/`tsan` presets and stay
  clean, asserting the pending-request userdata does not dangle when the gateway
  is destroyed with a pick in flight (shutdown-cancel). No new thread is
  introduced (SDL delivers the dialog result on the event-pumping thread), so
  TSan scope is "no data race across the deferred callback."
- **Coverage.** ≥90% diff coverage on changed lines (`diff-cover
  --fail-under=90`, `coverage` preset), including the prune, cap, corrupt-store,
  non-project-reject, and cancel branches; tests ship with the task.
- **Format/build.** `clang-format --dry-run --Werror` clean; `dev` + `release`
  build clean; `scripts/gate` green.

No follow-up WBS task is deferred (see Decisions / Open questions): the WASM
folder-picker impl is out of scope by A3 (WASM not scoped) and is reachable at
the gateway seam by construction, not a WBS leaf; Save/dirty is the already-WBS
`editor.project.save`.

## Decisions

**D-open_ui-1 — New/Open/Recent all spawn a sibling `exec`; the picker never
opens a project in-process.** Every affordance terminates in
`commands::open_another_project`; `project::open_project`/`create_project` are
never called from this process.
*Rationale:* D19/A7 fix one `Document` per process for its whole lifetime; the
current process already owns a project (bootstrap always opens/creates one,
D-app_state-6). Opening in-process would either mint a second `Document`
(forbidden) or tear down the live session mid-run (the invariant is
lifetime-fixed). Spawning a sibling is the *only* constitution-legal realization,
and it is exactly D19's "new `exec` … a project is a tab/instance." The
consequence — an untouched scratch window remains after the user opens a real
project — is honest per the tab analog; the user closes it.
*Alternative rejected:* replace the scratch `Document` in-process when the
current project is untouched. A "nicer" UX, but it reintroduces in-process
switching and a mutable-`Document`-slot that A7 deliberately eliminated —
the whole GC-root-set-is-one-document simplification would leak.

**D-open_ui-2 — Project actions are dependency-inverted behind a
`dock`-declared `ProjectGateway`; the SDL dialog + `ProcessLauncher` live only in
the L4 impl (doc delta A12).** The rail calls an abstract gateway
(`open`/`new`/`pick_folder`/`recent`); `AppProjectGateway` in L4 `app` holds the
SDL folder dialog, the launcher, `current_executable_path`, `RecentProjects`, and
the `project` helpers.
*Rationale:* SDL is legal **only** in `app` (`check_levels.py:44`), so a native
folder dialog cannot live in `platform` (L0) or `dock` (L3) — it must be L4.
Inverting the dependency (dock declares the interface; app implements) keeps
`dock`'s includes to its own header + std, keeps the interactive SDL dialog out
of the shared `platform::PlatformServices` aggregate (whose native impl must stay
L0-constructible for the WASM swap), and gives the WASM port one File System
Access API seam. This **refines exec_new's Constraint 6**, which anticipated
bolting `process_launcher()` (and a dialog faculty) onto `PlatformServices`: the
gateway is the better swap point and adds no DAG edge.
*Alternative rejected:* add `process_launcher()` + `folder_dialog()` faculties to
`PlatformServices` and have `dock` reach through the injected services (the
literal exec_new Constraint 6 wording). It forces the SDL-backed dialog into the
L0 services aggregate's construction (SDL is L4-only, so the native aggregate
would need an injected dialog anyway — the same inversion, but leaking a
UI-dialog concept into a file/threads/clock seam), and makes `dock` include
`<ace/platform/...>` + `<ace/commands/...>` directly. The gateway is strictly
cleaner.

**D-open_ui-3 — `RecentProjects` is an L1 `dockmodel` prefs store modeled on
`WorkspaceStore`; pruned MRU (doc delta D22).** A capped (10), most-recent-first
list of absolute project dirs in a versioned line-oriented text file under the
prefs root, published via `atomic_replace`, pruned of vanished/non-project dirs
on load.
*Rationale:* recent projects is cross-project local UI state — exactly D21's
per-user prefs category, and `dockmodel` (deps `{base, platform}`,
`check_levels.py:29`) already hosts `WorkspaceStore` with the identical
`(root, FileSystem&)` + atomic-text shape. Reusing the pattern keeps prefs
persistence in one component, is fully L1-testable, and adds no edge. Pruning on
load keeps the list honest when a project is moved/deleted (D22).
*Alternative rejected:* a new `recents` component or storing the list in
`project` (L1, but libarbc-linked and not a UI-state home) — a gratuitous
component / mis-homed state for a single small store; or an unpruned list that
offers dead entries.

**D-open_ui-4 — New composes a non-existent target and lets the child scaffold;
no in-process `create_project`.** New = pick a parent + type a name → assert the
composed path is absent → `exec` a sibling there → the child's
`open_or_create_app_state` create-branch scaffolds it.
*Rationale:* the child's bootstrap already branches create-vs-open on
`fs.exists(root)` (D-app_state-6), so a sibling `exec`ed at a missing path
creates it — reusing the exact seam with **zero** core change and never minting
a second `Document` in the current process. The name+parent modal is the honest
minimal UI, since the OS folder dialog can only select an *existing* directory
and cannot express "a new folder here."
*Alternative rejected:* (a) teach the child "existing empty dir → create" so New
could reuse the folder dialog alone — a deviation from the settled D-app_state-6
create-vs-open branch, needing a doc delta, for a worse UX (an empty existing dir
is ambiguous with a corrupt project). (b) `create_project` in-process then
`exec` — mints a transient second `Document`/`HousekeepingThread`, against A7's
spirit. (c) an argv `--new` flag on the child so the picker signals create —
changes exec_new's settled positional-only argv contract (D-exec_new-3) for no
gain over composing a non-existent path.

**D-open_ui-5 — `is_project_directory` pre-validates Open in L1 `project`.** A
pure predicate (canonical or workspace file present via `project_layout`) gates
Open and prunes the recent list.
*Rationale:* without it, Open on a non-project folder silently `exec`s a process
that dies with return-2 (`NoProject`) — a confusing dead window. A cheap
path-existence predicate (no `Document` open, no second-`Document` risk) gives
the rail immediate inline feedback and doubles as the recent-list pruner. It is
pure L1 logic, Catch2-tested.
*Alternative rejected:* let the child fail and show nothing (bad UX), or open the
candidate in-process to test it (mints a `Document` — forbidden by D-open_ui-1).

## Open questions

_None — all decided against the constitution._ D19/A7 fix spawn-not-swap;
D-app_state-6 fixes the child create-vs-open branch New rides; D21 fixes the
prefs-store shape `RecentProjects` reuses; §8/`check_levels.py:44` fix that SDL
is L4-only, forcing the gateway inversion. The two genuinely design-level calls —
the `dock::ProjectGateway` seam (structure) and the New/Open/Recent UX incl. the
pruned recent list (UI/UX) — are recorded as the same-commit deltas **A12**
(`docs/01-architecture.md`) and **D22** (`docs/00-design.md`), each reusing an
existing seam and adding no DAG edge. No further doc delta is required, and no
follow-up WBS task is deferred (the WASM `pick_folder` impl is out of scope by A3
and reachable at the gateway seam by construction).

## Status

**Done** — 2026-07-18.

- Added `dockmodel::RecentProjects` MRU store (`src/dockmodel/ace/dockmodel/recent_projects.hpp`, `src/dockmodel/recent_projects.cpp`) — capped N=10 most-recent-first, versioned text file via `FileSystem::atomic_replace`, prunes vanished/non-project dirs on load; modeled on `WorkspaceStore`.
- Added `project::is_project_directory` predicate and `compose_new_project_target` helper (`src/project/ace/project/project.hpp`, `src/project/project_open.cpp`) — pure L1, validates Open targets and prunes the recent list.
- Added `dock::ProjectGateway` L3 abstraction + `Dockspace::set_project_gateway` DI seam and rail **Project** section (New / Open / Open Recent) + New modal (`src/dock/ace/dock/dock.hpp`, `src/dock/dock.cpp`); dependency-inverted per A12.
- Added `app::AppProjectGateway` L4 concrete (`src/app/ace/app/project_gateway.hpp`, `src/app/project_gateway.cpp`) and `SdlFolderDialog` wrapper (`src/app/ace/app/folder_dialog.hpp`, `src/app/folder_dialog.cpp`) — SDL lives only in `app`, satisfying `check_levels`/A8.
- Wired gateway into `ShellOptions` + `run_editor` (`src/app/ace/app/shell.hpp`, `src/app/shell.cpp`) with test-injection seam.
- Updated `CMakeLists.txt` to build all new sources; updated `docs/00-design.md` (D22) and `docs/01-architecture.md` (A12) with same-commit doc deltas.
- Tests: `tests/recent_projects_test.cpp` (Catch2 unit, `ace_tests`), `tests/project_entry_test.cpp` (Catch2 unit, `ace_tests`), `tests/app_project_gateway_test.cpp` (Catch2 unit, `ace_shell_test`), `tests/open_ui_e2e_test.cpp` (ImGui Test Engine e2e, `ace_shell_test`); 71 unit cases + 26 shell cases + 2 new e2e cases — all green.
