# editor.project.exec_new — open-another-project = exec a new instance

## TaskJuggler entry

`tasks/00-editor.tji:114-118` — `task exec_new "Open-another-project = exec a
new instance"` under `editor.project`. `effort 1d`, `allocate team`,
`depends !app_state`. The `note` (`:118`) currently ends
`Refinement: tasks/refinements/exec_new.md` (a **flat** path); the real landing
path is `tasks/refinements/editor/exec_new.md`. Per the completion ritual
(`tasks/refinements/README.md:57-68`) the closer fixes that back-link, adds
`complete 100` immediately after the `allocate team` line (exec_new has **no**
`complete 100` today, unlike its shipped siblings `open`/`app_state`), then runs
`tj3 project.tjp 2>&1 | grep -iE "error|warning"` and confirms silence.

## Effort estimate

**1d.** Small. The child-side argv contract is a handful of lines in the
existing entry point; the parent-side "spawn a sibling editor" primitive is a
new L0 `platform` seam (`ProcessLauncher` + native `posix_spawn`) plus a thin L1
`commands` wrapper. No libarbc surface, no rendering, no new component, no doc
delta. The bulk of the effort is the tests.

## Inherited dependencies

**Settled (from editor.project.app_state).** The process owns exactly one
`arbc::Document` for its lifetime via `ace::commands::AppState`
(`src/commands/ace/commands/app_state.hpp:32-63`, move-only, one per process
per A7/D19). The bootstrap helper
`ace::commands::open_or_create_app_state(const platform::FileSystem&, const
std::filesystem::path&)` (declared `app_state.hpp:97-98`, defined
`src/commands/app_state.cpp:31-45`) branches on `fs.exists(root)` →
`open_project` vs `create_project` and is what `run_editor` already calls
(`src/app/shell.cpp:192`). The project-dir hand-off seam already exists:
`ShellOptions::project_dir` (`src/app/ace/app/shell.hpp:38`), whose comment
(`:33-37`) literally names *"a future `editor.project.exec_new`"* as the caller
that passes a concrete path. `run_editor`'s return-code contract
(`shell.hpp:90-95`) is 0 clean / 1 init failed / 2 project could not be
opened-or-created (logged, never thrown — D-app_state-6).

**Settled (from editor.project.open).** `project::open_project` /
`create_project` → `platform::Result<OpenedProject>`
(`src/project/ace/project/project.hpp:99-107`); errors are values on
`platform::Result<T>` (`src/platform/ace/platform/result.hpp:14`), never
thrown (D-open-6). The single-`Document`-per-process invariant is already
pinned by the boot-lifecycle e2e (`tests/app_state_e2e_test.cpp`).

**Pending (this leaf owns them).**
1. The **child-side argv contract**: `main()` accepts `argc/argv` and turns an
   optional positional project path into `ShellOptions::project_dir`.
2. The **parent-side spawn primitive**: a `platform::ProcessLauncher` seam with
   a native `posix_spawn`-based impl that launches a fully independent sibling
   editor process, plus `platform::current_executable_path()` so a relaunch
   targets *this* binary.
3. The **L1 session action** `commands::open_another_project(...)` — the seam
   `editor.project.open_ui`'s picker will call to realize "open another
   project" without any in-process switching.

## What this task is

Realize D19 / A7's **process-per-project** rule for the "open a *different*
project" gesture. Because one process owns exactly one `Document` for its whole
lifetime, opening another project must launch a **new, fully independent OS
process** (its own `Document`, workspace, threads, window) rather than swapping
the live session. This leaf supplies both halves of that: the **child side** —
teaching the `arbitraryeditor` binary to accept a project directory on the
command line so a freshly-launched process opens the right project — and the
**parent side** — a small platform seam that spawns a detached sibling editor
and an L1 `commands` action that invokes it with a target directory. It builds
no UI; the New/Open affordance and native folder picker that *call* this
primitive are `editor.project.open_ui`, which `depends !exec_new`.

## Why it needs to be done

D19 (`docs/00-design.md:436-445`, table row `:480`) and A7
(`docs/01-architecture.md:92-97`, log row `:257`) make process-per-project a
load-bearing simplification: the GC root-set is trivially "this one document,"
there is no multi-doc lifecycle to manage, and selection/shared panels are
unambiguously project-level (A5). That simplification only holds if "open
another project" genuinely forks a new process — otherwise in-process switching
sneaks multi-document state back in through the back door. Today the only way a
project path enters a process is `ShellOptions::project_dir` at startup
(`shell.cpp:190-192`) or the scratch fallback (`shell.cpp:56`); `main()` takes
no `argc/argv` (`src/app/main.cpp:7`) and there is no process-launch mechanism
anywhere in the tree (`Threads::spawn` at
`src/platform/ace/platform/threads.hpp:19-25` spawns *in-process threads* only).
`editor.project.open_ui` (`tasks/00-editor.tji:101-105`, `depends !exec_new`)
cannot wire its "Open…" entry to anything until this primitive exists.

## Inputs / context

**Design docs (normative — the constitution).**
- **D19** `docs/00-design.md:436-445` (table row `:480`) — *"One process = one
  project. Opening a different project is a new `exec` of the editor … its own
  `Document`, workspace, threads, and window. No multi-project-in-one-window, no
  in-process project switching … (WASM analog: a project is a tab / instance.)"*
  This is almost verbatim the exec_new task note.
- **A7** `docs/01-architecture.md:92-97` (log row `:257`) — *"opening another
  project execs a new instance, so there is no multi-document state to manage —
  the app owns exactly one `Document` for its lifetime, and the GC root-set is
  trivially that document."*
- **A5** `docs/01-architecture.md:255` — selection + shared panels are
  project-level; a canvas is only a camera (why a second project must be a
  second process, not a second panel-set).
- **D16** `docs/00-design.md` (project = a directory: `project.arbc` + `assets/`
  + `workspace/`) — the child re-opens this bundle through the already-built
  `open_or_create_app_state` path; the crash-durable `workspace/` is why the
  invoking editor need not shut down when it spawns a sibling.
- **§8 levelization DAG** `docs/01-architecture.md:144-179` and
  `scripts/check_levels.py:21-54` — the edges this leaf must respect (see
  Constraints).
- **§9 DoD** `docs/01-architecture.md:181-208` (the four verification layers and
  the universal definition of done, `:197-203`).

**Library API surface (fetched under `build/*/_deps/arbc-src/`).** None
directly. exec_new touches no libarbc type: the child's `Document` open runs
entirely through the already-shipped `project::open_project`/`create_project` →
`open_or_create_app_state` path; the parent side is pure OS-process plumbing.

**Source seams this leaf extends.**
- `src/app/main.cpp:7` — `int main()` today; gains `int main(int argc, char**
  argv)` + a pure `ace::app::parse_args`.
- `src/app/ace/app/shell.hpp:23-39` — `ShellOptions` (`project_dir` `:38`);
  `run_editor` `:96-97` (return-code contract `:90-95`). No signature change to
  `run_editor` — argv only fills the existing `project_dir`.
- `src/app/shell.cpp:56,179-199` — `scratch_project_dir()` fallback and the
  `open_or_create_app_state` call the child re-enters unchanged.
- `src/platform/ace/platform/platform_services.hpp:15-35` — `PlatformServices`
  aggregate (`filesystem()`/`clock()`/`threads()` faculties `:18-20`),
  `NativePlatformServices` `:25-35`; native faculty impls in
  `src/platform/native_platform.cpp:37`. The new `ProcessLauncher` seam and
  `current_executable_path()` land here, mirroring the `FileSystem`/`Threads`
  faculty pattern (`filesystem.hpp:18-61`, `threads.hpp:19-25`);
  `platform::Result<T>` from `result.hpp:14`.
- `src/commands/ace/commands/app_state.hpp:97-98` /
  `src/commands/app_state.cpp:31-45` — `open_or_create_app_state`, the sibling
  action that already takes a `platform::FileSystem&`; `open_another_project`
  lands beside it in the same `commands` component.
- CMake: `CMakeLists.txt:136` (`platform`), `:144`
  (`ace_component(commands DEPENDS base project scene)`), `:168-169` (the
  `arbitraryeditor` executable — the binary a relaunch re-spawns).

**Test rigs.** `tests/commands_test.cpp` (L1 `commands` Catch2 units, joined to
`ace_tests` at `CMakeLists.txt:173-180`), `tests/platform_test.cpp` (the
`ScratchDir` temp-dir helper, reused across filesystem round-trips),
`tests/app_state_e2e_test.cpp` (boot-lifecycle e2e via the `on_ready` seam,
single-`Document` invariant; `ace_shell_test`, `CMakeLists.txt:192-207`,
offscreen-SDL + llvmpipe env), `tests/app_loop_test.cpp` (an app-level Catch2
unit already living in `ace_shell_test` — the precedent for testing L4 pure
helpers). Golden byte-compare harness `tests/golden_support.hpp` (not exercised
here).

## Constraints / requirements

1. **Spawn a detached sibling, never replace-self.** Opening another project
   launches an independent OS process and **leaves the invoking editor running**
   (D19's WASM analog is "a new tab," not "navigate away"; the current
   project's live `workspace/` and window must not be torn down). The native
   impl uses `posix_spawn` (not `execv`), placing the child in its own session
   so it is fully detached, and reaps it without leaving a zombie (e.g.
   `SA_NOCLDWAIT` / not-waited fire-and-forget). Spawn failure is a
   `platform::Result` error value, never a throw (D-open-6 house style).
2. **Levelization — no new component, no new DAG edge, no doc delta.** The
   relaunch action lives in `commands`, whose declared deps are
   `{base, project, scene}` (`check_levels.py:28`) but whose **transitive
   closure already includes `platform`** (because `project` depends on
   `platform`, `check_levels.py:25`) — which is exactly why
   `open_or_create_app_state` may already take a `platform::FileSystem&`. So
   `commands::open_another_project` taking a `platform::ProcessLauncher&` adds
   **no** edge. `open_ui`'s home component `views` transitively closes over
   `commands → project → platform`, so it too can call the action and resolve
   the launcher without a new `views → platform` edge. The native impl's
   `<spawn.h>`/`<unistd.h>` are system headers, unmatched by the
   imgui/sdl/gl seam regexes (`check_levels.py:49-53`), so they are legal in the
   L0 `platform` core. `check_levels` must stay green with **zero** edits to
   `ALLOWED`/`EXTERNAL_ALLOWED`.
3. **Argv contract (child side).** `arbitraryeditor` with no positional arg
   preserves today's behavior (empty `project_dir` → scratch project, so the
   "one process, one non-empty `Document`" invariant always holds and the app
   stays drivable headless). Exactly one positional arg is opened/created as the
   project directory. A malformed invocation (more than one positional) prints a
   one-line usage to stderr and returns a non-zero code; `run_editor`'s existing
   0/1/2 contract is unchanged (parse failure is handled in `main` before
   `run_editor` is called). The argv → `ShellOptions` mapping is a **pure**
   `ace::app::parse_args` so it is unit-testable without SDL/GL.
4. **Absolute-path hand-off.** `open_another_project` canonicalizes the target
   directory to an absolute path (e.g. `std::filesystem::weakly_canonical`)
   before spawning, so the child does not depend on the parent's working
   directory. It rejects an empty target with an error value and does not spawn.
5. **Self-location.** The relaunch must target *this* editor binary, resolved
   robustly (native: `readlink /proc/self/exe`, not the possibly-bare
   `argv[0]`), exposed as `platform::current_executable_path() ->
   platform::Result<path>`. The caller passes that path into
   `open_another_project`, keeping the L1 action itself free of any
   "how do I find myself" platform detail.
6. **Scope boundary — no UI, no view-injection wiring.** exec_new ships the
   standalone `ProcessLauncher`/`NativeProcessLauncher`,
   `current_executable_path()`, and `open_another_project`. Adding
   `process_launcher()` to the injected `PlatformServices` the views receive,
   and the actual "Open…" widget that calls the action, are
   `editor.project.open_ui`'s scope (it `depends !exec_new`) — **not** a new
   deferred task. `run_editor` is not modified beyond `main`'s argv plumbing.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`);
`scripts/gate` green (build + `check_levels` + tests + coverage + format) is the
umbrella. Specifically:

- **Levelization (`check_levels` clean).** No component added, no `ALLOWED` /
  `EXTERNAL_ALLOWED` edit. `commands::open_another_project` references
  `platform::ProcessLauncher` legally via `commands`'s existing closure over
  `platform`; the native launcher's `<spawn.h>` sits in L0 `platform` and is not
  matched by the imgui/sdl/gl regexes. `scripts/check_levels.py` reports OK.
- **L1 logic — Catch2 unit (the bulk), in `tests/exec_new_test.cpp` joined to
  `ace_tests`.**
  - `open_another_project` forwards the executable path and the **absolute**
    project path to an injected fake `ProcessLauncher` (records `(exe, args)`);
    assert `args == {weakly_canonical(project_dir)}`.
  - a **relative** target directory is canonicalized to absolute before spawn.
  - an **empty** target directory returns an error value and the launcher is
    **not** invoked.
  - `NativeProcessLauncher::spawn_detached` actually launches an independent
    process: spawn a sentinel-writing command into a `ScratchDir`, poll for the
    sentinel within a bounded timeout, assert it appears and that the child was
    reaped without a lingering zombie.
  - `current_executable_path()` returns an existing absolute path (the running
    test binary) as a `Result` success.
- **Argv contract — Catch2 unit, in `tests/exec_new_argv_test.cpp` joined to
  `ace_shell_test`** (the app-level suite, per the `app_loop_test` precedent):
  `parse_args` maps `{exe}` → empty `project_dir` (scratch), `{exe, dir}` →
  `project_dir == dir`, and `{exe, a, b}` → a signalled usage error.
- **Rendered output — golden N/A (justified).** exec_new composites nothing; the
  child's first frame is the already-covered shell/render_probe path. No new
  golden.
- **UI e2e — ImGui Test Engine, in `ace_shell_test`.** exec_new adds no widget
  (the New/Open affordance is `open_ui`), so there is no button to drive. What
  it *does* add — "a parsed project-dir argument boots into that project" — is
  pinned by an `ace_shell_test` case (extend `tests/app_state_e2e_test.cpp` or
  add `tests/exec_new_e2e_test.cpp`) that feeds a `parse_args({exe, scratch})`
  result into `run_editor` and, via the `on_ready` seam, asserts the resulting
  `AppState` opened *that* directory. The single-`Document`-per-process
  invariant remains pinned by the existing `app_state_e2e` case.
- **Threading (ASan/TSan).** The `spawn_detached` smoke runs under the `asan`
  and `tsan` presets and stays clean: the primitive introduces no shared mutable
  state (fire-and-forget from the calling thread), and the child is reaped
  without a zombie. Scope note: `posix_spawn` is the sanitizer-safe launch path
  (no `fork`-without-`exec` in a multithreaded, ASan-instrumented process).
- **Coverage.** ≥90% diff coverage on changed lines (`diff-cover
  --fail-under=90`, `coverage` preset); tests ship with the task.
- **Format/build.** `clang-format --dry-run --Werror` clean; `dev` + `release`
  build clean.

No follow-up WBS task is deferred: the UI/injection consumer is the already-WBS
`editor.project.open_ui` (`depends !exec_new`).

## Decisions

**D-exec_new-1 — Spawn a detached sibling process, not a replace-self `execv`.**
D19's "new `exec`" is realized as `posix_spawn` of an independent, session-
detached child while the invoking editor keeps running.
*Rationale:* D19's own WASM analog — "a project is a tab / instance" — means
opening another project *adds* an instance; tabs coexist. The current project's
`workspace/` is live and its window is up; a replace-self `execv` would destroy
that window and any in-flight UI state for no benefit.
*Alternative rejected:* replace-self `execv` (the process becomes the new
project). Simpler by one concept but it silently closes the current project,
contradicting "fully independent" and the tab analog, and turns "open another"
into "switch," which is precisely what D19 forbids.

**D-exec_new-2 — The launch primitive is a `platform` (L0) seam; the action is
in `commands` (L1); no doc delta.** `ProcessLauncher` +
`NativeProcessLauncher` + `current_executable_path()` join the existing
`platform` faculties; `open_another_project` joins `open_or_create_app_state` in
`commands`.
*Rationale:* `commands` already sits above `platform` in the DAG closure (via
`project`, `check_levels.py:25`) and already consumes a `platform::FileSystem&`,
so no new edge or `ALLOWED` change is needed; `views`/`dock` (open_ui's home)
close over `commands`, so the future picker reaches the action with no
`views → platform` edge either. Mirroring the `FileSystem`/`Threads` faculty
pattern keeps the native-vs-WASM swap point in the one place that already owns
platform divergence (the WASM `ProcessLauncher` opens a new tab, D19).
*Alternative rejected:* `exec` directly from L4 `app`. It would keep everything
in the entry point, but the only runtime caller is a UI action in L3
`views`/`dock`, and L3 cannot call up into L4 — the primitive must live at
`commands` (L1) or below to be reachable. A standalone new component or a
declared `commands → platform` edge were both considered and rejected as
unnecessary: the closure already permits the include.

**D-exec_new-3 — Positional project-path argv; no-arg preserves scratch;
malformed → usage + non-zero, via a pure `parse_args`.** `arbitraryeditor
[<project-dir>]`.
*Rationale:* the child needs the target directory and nothing else; a single
positional is the minimal contract and matches the `ShellOptions::project_dir`
seam already anticipated in `shell.hpp:37`. No-arg → scratch keeps every headless
test and the bare-launch dev loop working unchanged and preserves the "never an
empty `Document`" invariant. Keeping the argv→`ShellOptions` mapping a pure
function makes it a plain Catch2 unit despite living in L4 `app`.
*Alternative rejected:* named flags (`--project=…`) or a subcommand grammar.
Over-built for one argument at 1d; a positional is unambiguous and trivially
extended later if a real CLI surface is ever wanted.

**D-exec_new-4 — `open_another_project` canonicalizes to an absolute path and
rejects empty.** The action normalizes the target before handing it to the
launcher.
*Rationale:* the child inherits the parent's CWD; passing a relative path would
make "which project" CWD-dependent and non-reproducible. Rejecting empty keeps
the "spawn a real project" contract honest and gives the future picker a clear
error value instead of a mystery scratch process.
*Alternative rejected:* pass the path through verbatim and let the child's
`FileSystem` resolve it. Works for absolute paths but is a latent footgun for
relative ones and hides the failure inside the freshly-spawned process where it
is hardest to report.

**D-exec_new-5 — Self-location via `/proc/self/exe`, exposed as
`platform::current_executable_path()`.** The relaunch targets the resolved
running binary, not `argv[0]`.
*Rationale:* `argv[0]` can be a bare name (PATH launch) or a symlink; a
relaunch must hit *this* exact binary. Resolving it once in the platform layer
keeps the L1 action free of the detail and gives the future picker a single,
tested way to find the editor.
*Alternative rejected:* thread `argv[0]` through `ShellOptions` and every
injection layer. More plumbing across levels for a strictly less reliable path
string.

## Open questions

_None — all decided against the constitution._ D19/A7 fix the process-per-
project model and the "new exec" mechanism; the only genuinely open choices —
spawn-vs-replace, where the seam levels, the argv shape, path normalization, and
self-location — are settled above under Decisions, each reusing an existing seam
and the existing levelization closure. **No doc delta required:** no new
dependency, no new component, no new DAG edge, no deviation from a decided
behavior.

## Status

**Done** — 2026-07-18.

- `src/platform/ace/platform/process_launcher.hpp` — `ProcessLauncher` seam + `current_executable_path()` declaration (L0 platform)
- `src/platform/native_process_launcher.cpp` — `NativeProcessLauncher` via `posix_spawn` + `POSIX_SPAWN_SETSID` + `SA_NOCLDWAIT`; `/proc/self/exe` self-location
- `src/commands/ace/commands/exec_new.hpp` + `src/commands/exec_new.cpp` — `open_another_project()` L1 action: empty-reject, `weakly_canonical` absolute-path, launcher delegation
- `src/app/ace/app/args.hpp` + `src/app/args.cpp` — pure `parse_args(argc, argv)` mapping positional arg → `ShellOptions::project_dir`; malformed → usage + non-zero
- `src/app/main.cpp` — `main(argc, argv)` → `run_editor_argv`; entry-point line marked `GCOVR_EXCL_LINE` (untestable forwarder)
- `CMakeLists.txt` — wired `exec_new_test`, `exec_new_argv_test`, `exec_new_e2e_test` into `ace_tests` / `ace_shell_test`
- Tests: `tests/exec_new_test.cpp` (forward/canonicalize/empty-reject; `NativeProcessLauncher` detached-spawn smoke + reap-no-zombie + launch-failure; `current_executable_path`), `tests/exec_new_argv_test.cpp` (`parse_args` + `run_editor_argv` usage), `tests/exec_new_e2e_test.cpp` (parsed project-dir boots into that project via `on_ready`)
