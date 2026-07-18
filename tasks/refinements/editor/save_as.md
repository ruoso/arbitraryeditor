# editor.project.save_as — Save As = publish a copy to a new directory + exec a sibling

## TaskJuggler entry

`tasks/00-editor.tji:129-133` — `task save_as "Save As = publish + copy project
dir + exec sibling"` under `editor.project`. Effort `1d`, `allocate team`,
`depends !save, !exec_new` (i.e. `editor.project.save` and
`editor.project.exec_new`). Unlike its shipped siblings the block has **no**
`complete 100` yet, and — unusually — its `note` (`:133`) already back-links the
correct landing path (`Refinement: tasks/refinements/editor/save_as.md`) and
records `Source-of-debt: tasks/refinements/editor/save.md`. Per the completion
ritual (`tasks/refinements/README.md:47-72`) the closer adds `complete 100`
immediately after `allocate team`, propagates to milestone **M9E** if this is its
last open dependency, then runs `tj3 project.tjp 2>&1 | grep -iE "error|warning"`
and confirms silence. This leaf was **registered by `save.md`** as its named
future task (`tasks/refinements/editor/save.md:362-372`).

## Effort estimate

**1 day** (from the `.tji`). Small, because every heavy piece already ships and
is trusted:

- The canonical dump — `project::save_project(fs, layout, doc, registry)` — already
  takes an **arbitrary** `ProjectLayout` (`src/project/ace/project/save.hpp:83`,
  impl `src/project/save.cpp:96-147`); pointing it at a target root *is* Save As's
  copy step (see Decisions).
- The sibling-`exec` primitive — `commands::open_another_project(launcher,
  executable, project_dir)` (`src/commands/ace/commands/exec_new.hpp:25`, impl
  `src/commands/exec_new.cpp:5-27`) — and `platform::current_executable_path()` /
  `platform::ProcessLauncher` already ship from `exec_new`.
- The `.gitignore` body (`k_gitignore_body = "workspace/\n"`,
  `src/project/project_open.cpp:56`) and the layout helper (`project_layout(root)`,
  `project_open.cpp:106-116`) already exist.
- The `ProjectGateway` seam + the native `FolderDialog`
  (`src/app/ace/app/folder_dialog.hpp:18`) already ship from `open_ui`; the
  `AppProjectGateway` already holds every collaborator Save As needs
  (`src/app/ace/app/project_gateway.hpp:34-37`).

The leaf is thin wiring in four small places (one new L1 `project` function, one
new L1 `commands` orchestrator, one `dock::ProjectGateway` virtual + rail button,
one L4 gateway override) plus the tests, which dominate the day. **No new
component, no new DAG edge, no new `FileSystem` primitive, no new library
machinery, no doc delta.**

## Inherited dependencies

**Settled (from `editor.project.save`).** The canonical publish exists and is
already trusted as the editor's persistence mechanism:

- `project::save_project(const FileSystem&, const ProjectLayout&, const
  arbc::Document&, const arbc::Registry&) -> Result<SaveOutcome>`
  (`save.hpp:83`, `save.cpp:96-147`): captures on the writer thread, serializes
  over `builtin_codecs(registry)`, routes owned bytes through the
  content-addressed `FilesystemAssetSink` (`save.hpp:58`, write-if-absent), and
  `atomic_replace`s the JSON to `layout.canonical`, `make_directories`-ing
  `assets/` first (`save.cpp:82-83`). Types: `SaveError { SerializeFailed,
  AssetWriteFailed, IoError }` (`save.hpp:31`), `SaveOutcome { revision,
  assets_written }` (`save.hpp:44`). **Plain Save already re-publishes over the
  live project's own `project.arbc` + `assets/` on every save** — so the editor
  already stakes fidelity on this exact re-emit path (D-save-1..3); Save As merely
  points it at a *different* root.
- `commands::save_project(AppState&, const FileSystem&) -> Result<SaveOutcome>`
  (`app_state.hpp:127`, `app_state.cpp:54-65`) — the L1 orchestrator Save As
  parallels: it calls `project::save_project(fs, state.layout(), state.document(),
  state.registry())`, then `state.mark_saved(...)`. `AppState`
  (`src/commands/ace/commands/app_state.hpp:34`) exposes `document()`, `layout()`,
  `registry()`, `is_dirty()` (`:67`), `mark_saved()` (`:74`).
- The dirty model, and the `dock::ProjectGateway` seam Save As extends: `save()`
  (`dock.hpp:69`) / `is_dirty()` (`:73`) join `open_project`/`new_project`/
  `open_recent`/`pick_folder`/`recent_projects` (`dock.hpp:44-62`), impl in L4
  `AppProjectGateway` (`src/app/project_gateway.cpp:72-78`). Doc delta **A13**
  (`docs/01-architecture.md:263`) rode `save`; A13 *already names this leaf* —
  *"Save As … is its own leaf (`editor.project.save_as`), not this seam."*

**Settled (from `editor.project.exec_new`).** The sibling-`exec` half:

- `commands::open_another_project(const platform::ProcessLauncher&, const
  std::filesystem::path& executable, const std::filesystem::path& project_dir) ->
  std::error_code` (`exec_new.hpp:25`, `exec_new.cpp:5-27`): empty `project_dir`
  → `std::errc::invalid_argument`, launcher **not** invoked; otherwise
  `absolute` + `weakly_canonical` (`exec_new.cpp:20-22`) then
  `launcher.spawn_detached(executable, {resolved.string()})`. The child re-opens
  the target through `open_or_create_app_state` (`app_state.hpp:118`,
  `app_state.cpp:38`) → `open_project` (a bundle with `project.arbc`) and
  **regenerates `workspace/` itself** from the canonical core — which is exactly
  why Save As need not copy `workspace/`.
- `platform::ProcessLauncher` + `platform::current_executable_path()`
  (`src/platform/ace/platform/process_launcher.hpp`) — native `posix_spawn`
  detached-sibling launch, `/proc/self/exe` self-location; the invoking editor
  keeps running (D-exec_new-1, the D19 tab analog).

**Settled (from `editor.project.open` / `open_ui`).** `project_layout(root)`
(`project_open.cpp:106-116`) fills `ProjectLayout`
(`src/project/ace/project/project.hpp:55-63`: `canonical`=`<root>/project.arbc`,
`assets_dir`=`<root>/assets`, `workspace_dir`, `exports_dir`, `gitignore`);
`create_project` scaffolds `assets/`/`workspace/`/`exports/` and writes the
`workspace/`-excluding `.gitignore` (`project_open.cpp:171-206`, `:185`,
`k_gitignore_body` `:56`, D-open-4/5). The native folder picker seam
`FolderDialog::show(Callback)` (`folder_dialog.hpp:18-26`, SDL impl
`folder_dialog.cpp:35`) and its `ProjectGateway::pick_folder` exposure
(`project_gateway.cpp:59-62`) exist. `AppProjectGateway`'s constructor already
takes `RecentProjects&, const FileSystem&, FolderDialog&, const ProcessLauncher&,
std::filesystem::path executable, AppState&` (`project_gateway.hpp:34-37`) — **every
collaborator Save As needs is already held**; `spawn()`
(`project_gateway.cpp:25-57`) already routes New/Open/Recent to
`open_another_project`.

**Pending (this leaf owns them).** The L1 `project::save_project_as` (publish the
live document into a fresh target root + write its `.gitignore`); the L1
`commands::save_project_as` orchestrator (canonicalize + publish-copy + exec
sibling); the `dock::ProjectGateway::save_as()` virtual + rail **Save As…**
button; the L4 `AppProjectGateway::save_as()` (pick folder → orchestrator).

## What this task is

Realize the design's **Save As** verb: *"Save As (copy the directory)"*
(`docs/00-design.md:410-411`) under **process-per-project** (D19/A7). Save As
produces an **independent copy of the current project at a new location** and
opens **that copy in a new, fully independent sibling editor process** (its own
`Document`, `workspace/`, threads, window), leaving the invoking editor running
on its original project. Concretely:

- In **L1 `project`**, add `save_project_as(fs, target_root, doc, registry) ->
  Result<SaveOutcome>`: compute `project_layout(target_root)`, publish the live
  document into it by reusing the trusted `save_project` core (so `project.arbc` +
  the content-addressed `assets/` land under the new root), and write the
  `workspace/`-excluding `.gitignore` (reusing `k_gitignore_body`). It does **not**
  create `workspace/` (the sibling regenerates it) and does **not** touch the
  source project.
- In **L1 `commands`**, add `save_project_as(state, fs, launcher, executable,
  target_root) -> Result<...>`: reject an empty target, canonicalize it to an
  absolute path, call `project::save_project_as(...)`, then hand the new directory
  to `commands::open_another_project(launcher, executable, target_root)` to exec
  the sibling on the copy.
- On the **rail**, a **Save As…** action beside **Save** (`dock.cpp:95`), wired
  through a new `dock::ProjectGateway::save_as()`; the L4 `AppProjectGateway`
  override opens the native `FolderDialog` to choose the target, then calls the
  `commands` orchestrator with its already-held collaborators.

This leaf ships **no new library machinery, no new filesystem primitive, and no
new component or DAG edge** — the "copy" is a re-publish of the canonical core
(the mechanism plain Save already uses), and the exec is `exec_new`'s primitive.

## Why it needs to be done

D16 §9 lists Save As alongside New/Open/Save as a **front-door verb**
(`docs/00-design.md:410-412`), and A13 (`docs/01-architecture.md:263`) explicitly
carved it out of the `save` leaf as *"its own leaf (`editor.project.save_as`)…
publish + copy the directory + exec a sibling on the copy, A7/D19."* `save` shipped
Save + the dirty indicator but deliberately *"never mints a second `Document`,
never copies the bundle, never execs"* (`save.md:294-296`), registering this leaf
as its named debt (`save.md:362-372`). Until it lands, the editor can publish and
re-open a project but cannot **fork** one — the "Save a copy elsewhere and keep
working" gesture D16 promises has no home, and D22's *"every entry point spawns a
new sibling exec"* (`docs/00-design.md:483`) is unrealized for the Save-As entry.
Both halves this leaf composes — `save_project` and `open_another_project` — are
shipped and idle for this exact use.

## Inputs / context

**Design docs (normative — the constitution).**

- `docs/00-design.md` **D16** (`:477`) — *"A project is a **directory** … Save =
  re-dump `project.arbc`. Portable core = `.arbc` + `assets/`; `workspace/` is
  machine-local scratch, rebuilt from the core, excluded from sharing/VCS."*
- `docs/00-design.md` **§9 "Files & the project directory"** (`:374-412`): the
  data-file-vs-dump framing (`:393-400`, *"`project.arbc` is the **dump** — the
  portable, canonical, content-addressed snapshot"*); portable-core-vs-scratch +
  the `.gitignore` (`:402-408`, *"`workspace/` … **rebuilt from the canonical
  core** … excluded from sharing / version control (the editor writes a
  `.gitignore` for it), and moving a project between machines carries `.arbc` +
  `assets/` and regenerates `workspace/`"*); and the verb list (`:410-411`) —
  ***"Save As (copy the directory)."*** This is the governing sentence: the
  observable result is a copy of the portable core at a new location.
- `docs/00-design.md` **D19** (`:480`) / **A7** (`docs/01-architecture.md:257`) —
  **process-per-project**: *"opening a different project is a new `exec` … its own
  `Document`, workspace, threads, window … WASM analog: a project is a
  tab/instance."* Save As opens its copy as a sibling, not in this window.
- `docs/00-design.md` **D22** (`:483`) — in-app New/Open/Recent on the rail; every
  entry point spawns a sibling `exec`; *"The dirty indicator and Save / Save As
  stay with `editor.project.save`"* (Save As now split into this leaf).
- `docs/01-architecture.md` **§8 levelization** (`:144-179`, table `:167`
  `project`/`:170` `commands`) — `project` is L1 (may depend on `base`, `platform`,
  **libarbc**; ImGui/GL/SDL-free); `commands` is L1; the copy-as-republish stays in
  L1, the folder dialog + exec wiring stay in L4. **§9 DoD** (`:181-208`, `:200-203`).
- `docs/01-architecture.md` **A12** (`:262`, `ProjectGateway` dependency-inversion)
  / **A13** (`:263`, Save + dirty extend the seam; A13 already names *this* leaf).

**Library API surface (fetched under `build/dev/_deps/arbc-src/`).** None
directly. Save As adds no libarbc call: it composes the already-wired
`project::save_project` (which owns the `capture_snapshot`/`serialize_snapshot`/
`SaveContext`/`AssetSink` path) and the OS-process plumbing of `open_another_project`.

**Source seams this leaf extends.**

- `src/project/ace/project/save.hpp:83` / `src/project/save.cpp:96-147` —
  `save_project(fs, layout, doc, registry)`; `save_project_as` lands beside it in
  the same `project` component and reuses its publish core, `project_layout(root)`
  (`project_open.cpp:106-116`, exposed from the `project` header if currently
  file-local — a mechanical intra-component change, no DAG impact) and
  `k_gitignore_body` (`project_open.cpp:56`).
- `src/commands/ace/commands/exec_new.hpp:25` / `src/commands/exec_new.cpp:5-27` —
  `open_another_project`; `commands::save_project_as` joins `commands` beside it and
  `commands::save_project` (`app_state.hpp:127`), reusing both.
- `src/dock/ace/dock/dock.hpp:38-73` — `ProjectGateway` gains
  `virtual bool save_as() = 0;`; `src/dock/dock.cpp:87-152` (`draw_project_section`)
  gains a **Save As…** `Selectable` next to `Save###save_project` (`:95`).
- `src/app/ace/app/project_gateway.hpp:32-37` / `src/app/project_gateway.cpp` —
  `AppProjectGateway::save_as()` reuses the held `dialog_`, `filesystem_`,
  `launcher_`, `executable_`, `app_state_` (ctor `:34-37`); `save()` at
  `project_gateway.cpp:72-78` and `spawn()` at `:25-57` are the patterns it mirrors.
  Fake-gateway injection point `ShellOptions::project_gateway`
  (`src/app/ace/app/shell.hpp:50`).

**Test rigs.** `tests/platform_test.cpp` `ScratchDir`; `ace_tests` (headless
Catch2, `CMakeLists.txt:184-190`, `add_test` `:198`) for the new
`tests/save_as_test.cpp`; `ace_shell_test` (ImGui Test Engine,
`CMakeLists.txt:205-211`, `add_test` `:213`) for the new
`tests/save_as_ui_e2e_test.cpp`, mirroring `tests/save_ui_e2e_test.cpp` /
`tests/exec_new_test.cpp` / `tests/exec_new_e2e_test.cpp`; the golden
`tests/goldens/render_probe_64x64.rgba8` + `tests/golden_support.hpp` for the
copy→reopen→render round-trip.

## Constraints / requirements

1. **The "copy" is a re-publish of the live document into the target root, not a
   raw byte copy (see Decisions).** `project::save_project_as(fs, target_root, doc,
   registry)` computes `project_layout(target_root)`, reuses the `save_project`
   publish core to emit `project.arbc` + the content-addressed `assets/` under the
   new root, and writes the `workspace/`-excluding `.gitignore`. This produces the
   D16 §9 result (a copy of the portable core at a new location) using the exact
   mechanism plain Save already trusts, and adds **no** new `FileSystem` primitive.

2. **`workspace/` and `exports/` are excluded; the sibling regenerates
   `workspace/`.** Save As writes only the portable core (`project.arbc` +
   `assets/`) + `.gitignore`. It creates no `workspace/` (D16: machine-local
   scratch, *"rebuilt from the canonical core"*, `:405-408`); the exec'd sibling's
   `open_or_create_app_state` → `open_project` path regenerates it (exec_new
   inherited dependency). `exports/` (rendered outputs, not portable core) is not
   copied.

3. **Publish before copy captures the *current* state (D16 §9 "dump the current
   state").** Because the copy is a re-publish of the *live* `Document`, the target
   reflects the current in-memory edits directly — no separate "publish to source
   first" step, and the source project is **left untouched** (its `project.arbc`,
   `assets/`, and dirty flag are unchanged). Save As is "save a copy elsewhere,"
   not "save here then copy."

4. **The current session is not re-pointed or marked clean (D19/A7).**
   Process-per-project means the invoking process stays bound to its own project
   and keeps running; Save As does **not** call `mark_saved` on the current
   `AppState`, does not rebind `AppState::layout_`, and does not tear down the
   window (that would be the forbidden in-process switch). A fresh sibling process
   owns the copy.

5. **Absolute-path hand-off + empty-target rejection (mirror `open_another_project`).**
   `commands::save_project_as` rejects an empty `target_root` with an error value
   (`std::errc::invalid_argument`) and neither publishes nor execs; otherwise it
   canonicalizes the target to an absolute path (`absolute` + `weakly_canonical`)
   **once** and uses that same path for both the publish and
   `open_another_project`, so the child never depends on the parent's CWD.

6. **Refuse to clobber an existing project (data-loss guard).**
   `project::save_project_as` returns an error value if `target_root` already
   contains a `project.arbc` (it does not silently `atomic_replace` a *different*
   project's canonical). Overwrite-with-confirmation is a UI concern surfaced to
   the parking lot (see Open questions), not part of this leaf.

7. **Errors are values, never throws (house style).** The publish maps
   `project::SaveError` onto the returned `Result`; the exec maps
   `open_another_project`'s `std::error_code`. A failed publish returns cleanly and
   **does not** exec a sibling on a half-written or absent bundle; a failed exec
   leaves the (successfully published) copy on disk and returns the error. Neither
   throws.

8. **Levelization — no new component, no new DAG edge, no lint edit, no doc delta.**
   `save_project_as` sits in `project` (L1; uses `FileSystem` + `<arbc/...>` + std,
   all already whitelisted for `project`, `scripts/check_levels.py:21-48`);
   `commands::save_project_as` sits in `commands` (L1), reaching `project` directly
   and `platform::FileSystem`/`platform::ProcessLauncher` through `commands`'s
   existing closure over `platform` (via `project`) — the very closure that already
   lets `commands` host both `save_project` and `open_another_project`, so **no new
   edge**; the `dock` virtual adds no include; the impl + folder dialog stay in L4
   `app`. `project`/`commands` stay ImGui/GL/SDL-free. `check_levels` stays green
   with **zero** `ALLOWED`/`EXTERNAL_ALLOWED` edits.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`, `:200-203`);
`scripts/gate` green (check_levels · clang-format · build · ctest · coverage) is
the umbrella. Specifically:

- **Levelization (`check_levels` clean).** `python3 scripts/check_levels.py`
  passes with **no edit**: `save_project_as` in `project` includes only
  `<arbc/...>`, `<ace/platform/...>`, and std; `commands::save_project_as` adds no
  non-whitelisted include and references `project`/`platform` types via the
  existing closure; `dock` gains one pure-virtual signature (no new include); the
  override + `FolderDialog` use stay in `app` (L4). No new component, no new DAG
  edge. Primary structural assertion.

- **L1 logic — Catch2 unit (the bulk), in `tests/save_as_test.cpp` joined to
  `ace_tests`** (`CMakeLists.txt:190`), reusing `ScratchDir`, header-comment
  `editor.project.save_as`, sentence-style `TEST_CASE`s:
  - **`project::save_project_as` publish-copy:** a `create_project`'d probe/solid
    `AppState` → `save_project_as(fs, target, doc, registry)` writes
    `<target>/project.arbc` (`fs.exists`, non-empty, parses as canonical `.arbc`),
    `<target>/assets/` exists, `<target>/.gitignore` == `"workspace/\n"`;
    `<target>/workspace/` is **not** created and `<target>/exports/` is **not**
    created; the **source** project dir is byte-unchanged. Returns a `SaveOutcome`.
  - **Refuse-to-clobber:** a target that already contains `project.arbc` returns an
    error value; the existing file is untouched.
  - **Error values:** an unwritable target surfaces `SaveError::IoError` /
    `AssetWriteFailed` via `Result`; none thrown.
  - **`commands::save_project_as` orchestration** (with an injected fake
    `ProcessLauncher` recording `(exe, args)`, per `tests/exec_new_test.cpp`): a
    **relative** target is canonicalized to absolute for both publish and spawn;
    `args == {weakly_canonical(target)}`; an **empty** target returns an error and
    the launcher is **not** invoked and nothing is written; a **publish failure**
    (unwritable/clobbered target) returns an error and the launcher is **not**
    invoked; the current `AppState::is_dirty()` and `layout()` are **unchanged** by
    a successful Save As (Constraint 4).

- **Rendered output — golden (copy fidelity), in `tests/save_as_test.cpp`.**
  Reuse `tests/goldens/render_probe_64x64.rgba8`: `create_project` → build the probe
  content → `save_project_as` into a fresh `ScratchDir` target → `open_project` the
  **target** forcing rebuild-from-canonical (no `workspace/` present) →
  `render_document_srgb8` → **byte-exact** compare against the existing golden. No
  new golden committed (the probe is the only rendered content this leaf can
  represent; a painted-raster copy golden rides `editor.paint.*`).

- **UI e2e — ImGui Test Engine (Save As…), in `tests/save_as_ui_e2e_test.cpp`
  joined to `ace_shell_test`** (`CMakeLists.txt:211`). Inject a **fake
  `ProjectGateway`** (recording `save_as()`) via `ShellOptions::project_gateway`
  (mirroring `tests/save_ui_e2e_test.cpp`), drive the rail's **Save As…** button by
  stable widget id (e.g. `Save As…###save_as`), and assert `save_as()` was invoked.
  (The folder-dialog → orchestrator wiring in the real `AppProjectGateway` is a
  headless L4 unit, extending `tests/app_project_gateway_test.cpp` with a scripted
  `FolderDialog` returning a target and a fake launcher, asserting
  `project.arbc` appears at the target and the launcher is invoked with it.)

- **Threading (ASan/TSan).** A save_as scenario under the `asan` and `tsan`
  presets: boot → dispatch a command → **Save As** (writer-thread
  `capture_snapshot` racing the `HousekeepingThread` over the source `workspace/`,
  then the fire-and-forget detached spawn via a sentinel/fake launcher so no real
  child races the harness) → shutdown, sanitizer-clean. Scope note: the publish
  half reuses `save`'s already-scoped writer-vs-housekeeping race and the exec half
  reuses `exec_new`'s `posix_spawn` sanitizer-safe path; Save As introduces no new
  shared mutable state.

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`, `coverage`
  preset), including the publish-copy success + `SaveError`/refuse-to-clobber
  branches, the empty-target and publish-failure short-circuits in the
  orchestrator, and the canonicalization path. Tests ship with the task.

- **Format/build.** `clang-format --dry-run --Werror` clean; `dev` and `release`
  presets build; `scripts/gate` green.

**No follow-up WBS task is deferred.** The Save-As entry point is fully realized
here; the UI/injection collaborators (`FolderDialog`, `ProcessLauncher`,
`RecentProjects`) all ship from `open_ui`/`exec_new`. The one human-judgment item —
overwrite-with-confirmation UX for a target that already holds a project — is
**not** an agent-implementable WBS leaf; it is surfaced to the parking lot (see
Open questions).

## Decisions

**D-save_as-1 — "Copy the directory" is a re-publish of the live document into the
target root, not a raw recursive byte copy.** `project::save_project_as` reuses the
`save_project` publish core against `project_layout(target_root)`, emitting
`project.arbc` + the content-addressed `assets/` under the new root.
*Rationale:* `save_project` already accepts an arbitrary `ProjectLayout`
(`save.hpp:83`) and **plain Save already re-publishes over the live project's own
`project.arbc` + `assets/` on every save** — the editor has therefore *already*
staked its persistence fidelity on this exact re-emit path. Re-publishing to a new
root is the *same* trusted mechanism pointed at a different layout; it needs no new
code beyond selecting the target. It also produces a strictly *cleaner* copy — only
the owned bytes the live document actually references are written (pre-GC orphans in
the source `assets/` are naturally dropped, consistent with the content-addressed
portable-core model, D8/D13). The observable D16 §9 result — *"copy the
directory"*, i.e. a new directory holding the portable core — is delivered exactly.
*Alternative rejected:* a literal recursive `project.arbc` + `assets/` copy. The
codebase has **no** directory-copy primitive (survey: zero `std::filesystem::copy`
uses), so this would mean either a **new `FileSystem::copy_tree` virtual** (a new
L0 seam → an A-row doc delta, plus a WASM impl) or hand-rolled
`list_directory`+`read_file`+`write_file` recursion — a *second*, differently-tested
persistence mechanism that nothing else exercises, and one that would also require
"publish to source first" (mutating the source's `project.arbc` and clearing its
dirty flag as a surprising side effect) to capture current edits. Re-publish is
fewer moving parts, no new seam, no doc delta, and reuses the path the whole editor
already trusts. *This is why no doc delta is required:* re-publish is an
implementation mechanism *inside* L1 `project` that yields the promised D16 §9
result — not a new dependency, seam, or behavioral deviation.

**D-save_as-2 — The current session is untouched; the copy opens in a sibling
`exec` (D19/A7).** Save As publishes to the target, then calls
`open_another_project` to launch a detached sibling on it; the invoking editor keeps
running on its original project, and its `AppState` is neither re-pointed nor marked
clean.
*Rationale:* process-per-project (D19/A7, the tab analog) forbids in-process
switching — one process owns one `Document` for its lifetime. "Save As" here is
therefore "save a copy elsewhere and open it in a new window," which is precisely
`open_another_project`'s contract; reusing it keeps Save As's exec identical to
New/Open/Recent (`project_gateway.cpp:25-57`). Leaving the source untouched is the
honest, least-surprising behavior: the user asked to copy *elsewhere*, not to save
*here*.
*Alternative rejected:* re-point the running session's `layout_` at the new root
(classic single-process "Save As" re-association). It would smuggle exactly the
in-process project switch D19 forbids back in, mutate the shipped `AppState`
invariants, and diverge Save As's exec model from every other entry point.

**D-save_as-3 — One L1 `project` primitive (`save_project_as`) + one L1 `commands`
orchestrator (`save_project_as`), mirroring the `save` split; the L4 gateway drives
the folder dialog.** `project::save_project_as` is pure publish-to-a-new-root
(headless Catch2-testable); `commands::save_project_as` composes
empty-reject/canonicalize + the project publish + the exec; `AppProjectGateway::
save_as()` picks the folder and calls the orchestrator.
*Rationale:* this is the exact shape `save` established (`project::save_project` +
`commands::save_project` + `ProjectGateway::save()`, D-save-1/5) and that `open_ui`
established for folder-picked entry points (`pick_folder` → gateway action). Every
collaborator the L4 override needs is already constructor-injected into
`AppProjectGateway` (`project_gateway.hpp:34-37`), so no new wiring at the shell.
Keeping the copy in `project` (L1) keeps the bulk of the logic headless and off the
ImGui/SDL surface (§8).
*Alternative rejected:* a single fat gateway method that inlines publish + copy +
exec at L4. It would push testable I/O logic up into the SDL-only level (harder to
unit-test, per §9's "L1 logic is the bulk"), and duplicate the `project`/`commands`
layering `save` already proved out.

**D-save_as-4 — Refuse to clobber an existing project; defer overwrite-confirm to
the UI/parking lot.** `save_project_as` errors if the target already holds a
`project.arbc`.
*Rationale:* the folder picker can land on a populated directory; silently
`atomic_replace`-ing another project's canonical is the one Save-As error that
destroys unrelated work. An error value (house style) is the safe, testable default
and gives the future picker a clear signal. The "target exists — replace?" prompt is
a UI/UX judgment (which layer confirms, what copy) that is not agent-mechanical, so
it is surfaced to the parking lot rather than encoded as a WBS "revisit" task.
*Alternative rejected:* silent overwrite (a data-loss footgun) or a mandatory
"empty directory only" rule (over-restrictive — a target that exists but has no
`project.arbc` is a fine destination).

## Open questions

_None blocking — all decided against the constitution._ D16 §9 fixes Save As as
producing a copy of the portable core at a new location; D19/A7 fix that copy
opening in a sibling `exec`; A13 fixes the `ProjectGateway` seam and already names
this leaf; §8 fixes the L1 homes and the no-new-edge levelization. The one genuine
degree of freedom — copy-by-republish vs copy-by-byte-copy — is settled defensibly
(D-save_as-1: reuse the mechanism Save already trusts, no new seam) and pinned by
the copy→reopen→render golden. **Doc delta:** none — no new dependency, no new
component, no new DAG edge, no new seam (Save As extends the A13 `ProjectGateway`
already established), and no deviation from a decided *behavior* (the D16 §9 result
is delivered; only the internal mechanism is chosen). **Parking-lot item (human
judgment, not a WBS task):** overwrite-with-confirmation UX when the chosen target
already contains a project — which layer confirms and how (this leaf refuses safely
by default, D-save_as-4).

## Status

**Done** — 2026-07-18.

- `src/project/ace/project/project.hpp`: exposed `k_gitignore_body` as a project-level constant (previously file-local in `src/project/project_open.cpp`).
- `src/project/ace/project/save.hpp` + `src/project/save.cpp`: added `project::save_project_as(fs, target_root, doc, registry) -> Result<SaveOutcome>` — publishes the live document as a portable copy (`project.arbc` + `assets/` + `.gitignore`), refuses to clobber an existing `project.arbc`, writes no `workspace/` or `exports/`.
- `src/commands/ace/commands/app_state.hpp` + `src/commands/app_state.cpp`: added `commands::save_project_as` orchestrator — canonicalizes target, calls `project::save_project_as`, then `commands::open_another_project`; empty-target rejection and publish-short-circuit on failure; current session untouched (`AppState::layout_` and dirty flag unchanged).
- `src/dock/ace/dock/dock.hpp` + `src/dock/dock.cpp`: added `ProjectGateway::save_as()` pure virtual and wired a **Save As…** rail button beside **Save**.
- `src/app/ace/app/project_gateway.hpp` + `src/app/project_gateway.cpp`: added `AppProjectGateway::save_as()` — opens `FolderDialog`, calls `commands::save_project_as` with already-held collaborators.
- `CMakeLists.txt`: wired new `save_as_test.cpp` and `save_as_ui_e2e_test.cpp` into the build targets.
- `tests/save_as_test.cpp` (new): Catch2 units — publish-copy success, refuse-to-clobber, `SaveError`/gitignore-fault, orchestrator canonicalize/empty-reject/publish-short-circuit/exec-fail, current-session-untouched; copy→reopen→render golden round-trip against `tests/goldens/render_probe_64x64.rgba8`.
- `tests/save_as_ui_e2e_test.cpp` (new): ImGui Test Engine e2e asserting **Save As…** rail button invokes `save_as()` via fake `ProjectGateway`.
- `tests/app_project_gateway_test.cpp`: added two headless L4 gateway units for `AppProjectGateway::save_as()`.
- `tests/open_ui_e2e_test.cpp` + `tests/save_ui_e2e_test.cpp`: added inert `save_as()` override to existing fake gateways.
