# editor.project.app_state — Project-level state; single Document per process; command dispatch

## TaskJuggler entry

`tasks/00-editor.tji:107-112` — `task app_state` under `editor.project`. Effort
`3d`, `allocate team`, `depends !open` (`:110`, i.e.
`editor.project.open`). The `note` (`:111`) cites **Design D19** and **arch
A4/A7** and names this refinement. (The `.tji` note back-links to the flat
`tasks/refinements/app_state.md`; the real landing path is
`tasks/refinements/editor/app_state.md`, matching the existing `editor/`
refinement set — the closer fixes the note back-link per the ritual in
`tasks/refinements/README.md:57-68`, exactly as `open.md` did.)

Downstream dependents (all `depends !app_state`): `editor.project.exec_new`
(`:113-117`, open-another = a new `exec`), `editor.project.save`
(`:119-123`, the re-dump publish step), `editor.project.undo` (`:125-129`,
journal-backed undo/redo + gesture coalescing); and `editor.canvas.view`
(`:142-145`, `depends editor.project.app_state`) which drives the owned
`Document` through a `HostViewport`. Every `editor.canvas.*` leaf and the whole
cell/camera/panel stack assume this leaf has established the process's single
owned `Document` and the action→transaction dispatch seam.

## Effort estimate

**3 days** (from the `.tji`). This leaf turns the transient `OpenedProject`
`editor.project.open` hands back into the process's **lifetime-owned session
state** and stands up the **action → libarbc transaction** dispatch seam every
later edit leaf plugs into. The cost is in three places: (1) getting the
ownership/lifetime object right so the workspace-backed `Document` (and its live
`HousekeepingThread`, A4) is held for the whole run and torn down cleanly under
the sanitizer lanes; (2) designing the `Command`/dispatch seam so the not-yet-
written edit leaves (cells, camera, brush) drop in without reshaping it, while
proving it end-to-end with the transactional `Document` wrappers that exist
today; and (3) the app-layer bootstrap that resolves a project path into the
session at launch. No canvas rendering, no off-UI-thread driver, no dump/undo
UI — those are `editor.canvas.*` / `save` / `undo`.

## Inherited dependencies

**Settled (from `editor.project.open`).** The `project` L1 component produces a
live workspace-backed `Document`: `open_project(fs, root)` /
`create_project(fs, root)` return a `platform::Result<OpenedProject>`, and
`struct OpenedProject { std::unique_ptr<arbc::Document> document; ProjectLayout
layout; bool rebuilt_from_canonical; }` (`src/project/ace/project/project.hpp:84-107`).
`open.md` explicitly deferred *this* leaf's scope: *"This leaf produces the
document; lifetime ownership (one per process, A7) is
`editor.project.app_state`'s, not this leaf's"*
(`src/project/ace/project/project.hpp:83-84`), and *"single-`Document`-per-
process ownership, selection, and command dispatch are
`editor.project.app_state`"* (`tasks/refinements/editor/open.md:99-103`).
`ProjectLayout` (`project.hpp:53-61`) carries the resolved bundle paths (incl.
`canonical` = `project.arbc`, the dump target `save` will consume). Errors are
values on `platform::Result<T>` (`src/platform/ace/platform/result.hpp:14`).

**Settled (D-open-7 — the deferral this leaf collects).** `open.md`'s
`register_builtin_kinds` Registry is *transient to the load*; the persistent,
lifetime-scoped kind `Registry` belongs to the single-`Document` owner: *"the
single-`Document`-per-process owner (A7, `editor.project.app_state`) is the
natural home for a lifetime-scoped registry once plugin kinds (A6 Registry seam)
and the save round-trip need one"* (`tasks/refinements/editor/open.md:422-430`).
This leaf owns that persistent `Registry`.

**Settled (from `editor.dock.tool_rail`).** The **active tool** already exists as
headless UI state — `ace::dockmodel::ToolSelection` (`ToolId` enum
Select/Brush/Eyedropper/Pan + a trivial holder,
`src/dockmodel/ace/dockmodel/tool_rail.hpp:18-52`), settled by A11/D20. This leaf
does **not** re-home it (see D-app_state-4); it composes with it at the app
layer.

**Settled (from `editor.foundation.*`).** `commands` is an L1 component
(`CMakeLists.txt:144`, `ace_component(commands DEPENDS base project scene)`),
today a stub — `namespace ace::commands { const char* name(); }`
(`src/commands/ace/commands/commands.hpp:3-8`, `src/commands/commands.cpp`).
`check_levels` whitelists `<arbc/...>` includes for `commands`
(`scripts/check_levels.py:47`) and lists it in **none** of the imgui/sdl/gl sets,
so the arbc `Document`/`Transaction`/`Journal` includes this leaf adds need **no
lint edit**, and arbc links transitively through `project` (project links
`arbc::arbc` PUBLIC, `CMakeLists.txt:141`; `commands` DEPENDS `project`). The
`app` L4 shell (`src/app/ace/app/shell.hpp`, `run_editor`) and the headless
frame-loop harness (`src/app/ace/app/app_loop.hpp`) are the bootstrap seam.

**Pending (this leaf owns them).** The `commands` session aggregate (owns the
single `Document` + persistent `Registry` + `ProjectLayout`); the project-level
`Selection` model; the `Command`/action dispatch seam that applies an action to
the writer `Document` as one libarbc transaction; the app-layer bootstrap that
turns a project path into that session at launch; and the Catch2 L1 units +
sanitizer-scoped boot lifecycle.

## What this task is

Give the process its **one owned project session** and the **action → library
transaction** seam. Per D19/A7 the app owns exactly one `Document` for its whole
lifetime (one process = one project; GC root-set is trivially that document);
this leaf builds the object that holds it. Concretely, in the `commands` L1 core
it introduces an **`AppState`** (session) that takes ownership of the
`OpenedProject` (moves the `unique_ptr<Document>` in), holds the `ProjectLayout`
and a **persistent kind `Registry`** (D-open-7), and carries the **project-level
`Selection`** (D19/A5: selection is the project's, shared by every canvas, not
per-canvas). It exposes a **command dispatch**: a `Command`/action value applied
on the writer `Document`, each producing one libarbc transaction — one journal
entry, one revision bump — so the document's journal *is* undo/redo (doc 14), no
editor reimplementation. At the `app` L4 layer it wires launch: resolve a project
directory into the `AppState` via `project::open_project`/`create_project`, held
for the process lifetime beside the `dockmodel::ToolSelection`.

Everything stateful and logical lives in the L1 core (headless, ImGui/GL/SDL-
free, errors-as-values); the app layer is thin wiring. This leaf establishes the
**seam and the transaction boundary**; it does **not** ship concrete scene-edit
commands (those land with their own edit leaves — cells, camera, brush), does
**not** coalesce gestures (`editor.project.undo`), does **not** dump `project.arbc`
(`editor.project.save`), and does **not** move edits off the UI thread onto a
double-buffered driver (`editor.canvas.frame_sync`).

## Why it needs to be done

`open.md` produced a `Document` by value and pointed at exactly this gap: nothing
yet holds it for the process lifetime, and the shell still runs only the
anonymous probe document (`src/app/probe.cpp`, `src/app/shell.cpp:192` — the
dockspace draws chrome, no project). A7/D19 promise "one process = one project;
the app owns exactly one `Document` for its lifetime" — a promise no code yet
keeps. Every leaf below `app_state` — the canvas stack, cameras, cells, panels,
save, undo, exec-new — assumes (a) a real project session it can read the
`Document`, `Selection`, and `Registry` off of, and (b) a dispatch seam that
turns a user action into a journaled library transaction. `editor.canvas.view`
depends directly on `app_state` (`tasks/00-editor.tji:144`) precisely to drive
that owned `Document` through a `HostViewport`. Landing the session + the
dispatch seam now makes "actions → doc-14 transactions → free undo" (A4/D15) a
concrete pipe the edit leaves extend, rather than each reinventing document
ownership.

## Inputs / context

**Design docs (normative — the constitution).**

- `docs/00-design.md` **D19** (`:480`, *"Project-scoped state; process-per-
  project"*) — the governing row: *"Selection and the shared panels
  (Inspector/Layers/Overview) belong to the **project**, not any canvas … N of
  them share one project-level selection … **One process = one project** …
  single-project for its whole lifetime (GC root-set = this one document)."* The
  §10 prose (`:436-445`) restates it: *"Panels belong to the **project**, not a
  canvas … the editor is single-project for its whole lifetime, which makes the
  GC root-set trivially 'this one document' (D19)."*
- `docs/00-design.md` **§9 prose — scene edits are transactions** (`:366-372`):
  *"Every scene edit (place, move, scale, rotate, reorder, paint, import, delete,
  resolution change) is a **library transaction** (doc 14), so undo/redo is the
  document's journal, not an editor reimplementation. Continuous gestures
  **coalesce into one step**."* This leaf builds the dispatch that opens those
  transactions; the **coalescing** clause is `editor.project.undo`'s.
- `docs/00-design.md` **D15** (`:476`) — undo/redo *is* the doc-14 journal;
  **transient vs. scene**: selection and viewport pan/zoom are transient app
  state, *not* journaled; a saved shot's framing *is* scene data. Fixes that
  `Selection` here is transient, never a transaction.
- `docs/00-design.md` **D20** (`:481`) / **D18** (`:479`) — the modal tool set is
  headless UI state (A11); layout is local UI state, never in `project.arbc`
  (scopes layout persistence *out* of this leaf — that is D21/D-workspaces-3).
- `docs/01-architecture.md` **A4** (`:61-82`, log row `:254`) — concurrency
  adopted verbatim: single-writer/render-thread-confined cache, **leaf-only
  dispatch**, one shared `WorkerPool`, **one `HousekeepingThread` per `Document`**.
  *"UI thread submits edits, never touches the cache"* (`:254`). This leaf's
  dispatch is the submit side; it stays synchronous single-writer here (the off-
  UI-thread driver + double-buffer is `editor.canvas.frame_sync`, `:148-151`).
- `docs/01-architecture.md` **A5** (`:86-97`, log row `:255`) — *"Selection and
  the shared panels are **project-level**, not per-canvas (D19): a canvas is
  **only** a camera and carries no selection or inspection state"* (`:91-92`);
  *"the app owns exactly one `Document` for its lifetime, and the GC root-set is
  trivially that document"* (`:96-97`).
- `docs/01-architecture.md` **A7** (log row `:257`) — *"Process-per-project: one
  process = one project … the app owns exactly one `Document` for its lifetime —
  no multi-doc management … Selection + shared panels are **project-level**."*
  The direct mandate for the `AppState` owner.
- `docs/01-architecture.md` **A11** (log row `:261`) — *"`dockmodel` owns headless
  UI state incl. active-tool"* (a `ToolId` enum + `ToolSelection`). Fixes that
  active-tool stays in `dockmodel`, not this leaf; the tool→interaction dispatch
  is `interact`'s later (`editor.canvas.tool_dispatch`, D20).
- `docs/01-architecture.md` **§7 / §8 / A8** (`:126`, `:162-179`, log `:258`) —
  §7 names `project` as *"libarbc Document, project-dir open/save/gc"* (`:126`);
  §8 places `commands` at **L1** (*"actions → libarbc transactions · undo"*,
  `:131`), may depend on `base`/`project`/`scene`, and *"All of L1 is the testable
  core and none of it may `#include <imgui.h>` (or GL/SDL)"* (`:176-179`).
- `docs/01-architecture.md` **§9 / A9** (`:181-208`, log `:259`) — the layered
  DoD; the *L1 logic* row explicitly lists *"app-state … selection"* as Catch2-
  headless coverage (`:187`).

**Library API surface (fetched under `build/dev/_deps/arbc-src/`).**

- `<arbc/runtime/document.hpp>` — the writer edit seam. Host-facing transactional
  wrappers, each *"commits its own version and bumps the revision"* and appends
  one journal entry: `add_composition(w, h)` (`:167`), `add_content(content,
  kind)` (`:106`), `add_layer(content, transform, opacity)` (`:132`),
  `set_layer_transform` (`:133`), `attach_layer` (`:149`), `remove_content`
  (`:130`). The general seam: `Model::Transaction transact(std::string name = {})`
  (`:178`, *"Open a transaction on the document's model … Commits publish one
  version and … append one journal entry … WRITER-THREAD ONLY"*). `Journal&
  journal()` (`:184`, *"the document-wide history … one journal across all
  objects"*). `checkpoint()` (`:219`), `workspace_backed()` (`:222`).
- `<arbc/model/journal.hpp>` — `bool undo()` / `bool redo()` (`:83-84`),
  `can_undo()`/`can_redo()` (`:86-87`), `depth()` (`:90`, stored entry count),
  `cursor()` (`:92`). Used here **only** to *assert* a dispatched command produced
  one entry; wiring undo/redo to UI + coalescing is `editor.project.undo`.
- `<arbc/contract/registry.hpp>` — `class Registry` (default-constructible) and
  `<arbc/builtin_kinds.hpp>` `register_builtin_kinds(Registry&)` — the persistent
  registry `AppState` owns (D-open-7), reused by `save`/export and the future A6
  plugin seam.

**Source seams this leaf extends.**

- `src/commands/ace/commands/commands.hpp` / `src/commands/commands.cpp` — the L1
  `commands` stub (`name()` only) this leaf fleshes out with `AppState`,
  `Selection`, `Command`, and `dispatch`. Adds `<ace/project/project.hpp>`
  (already reachable — `commands` DEPENDS `project`) and `<arbc/...>` includes
  (whitelisted, `scripts/check_levels.py:47`).
- `src/project/ace/project/project.hpp:84-107` — `OpenedProject`,
  `open_project`/`create_project` the `AppState` consumes (moves the `Document`
  in).
- `src/app/ace/app/shell.hpp` (`run_editor`, `Shell`) / `src/app/main.cpp:7` /
  `src/app/ace/app/app_loop.hpp` — the L4 bootstrap that constructs the `AppState`
  from a project path and holds it for the process lifetime (alongside the
  `dockmodel::ToolSelection`). `app` DEPENDS everything (`CMakeLists.txt:157-171`),
  so no new edge.
- `src/platform/ace/platform/result.hpp:14` (`Result<T>`) and
  `src/platform/ace/platform/filesystem.hpp` (`NativeFileSystem`) — the error
  channel + injected I/O the bootstrap passes to `open_project`/`create_project`.

**Test rigs.**

- `tests/platform_test.cpp` — the `ScratchDir` temp-dir helper; reused for the
  round-trip session units, exactly as `project_open_test.cpp` did.
- `ace_tests` (headless Catch2, `CMakeLists.txt:173-183`) — the new
  `tests/commands_test.cpp` joins here.
- `ace_shell_test` (`CMakeLists.txt:191-194`) — the ImGui Test Engine / headless-
  shell target; the boot-lifecycle e2e joins here.

## Constraints / requirements

1. **No new component, no new DAG edge, no lint edit.** `AppState`/`Selection`/
   `Command`/`dispatch` land in the existing `commands` L1 component; the
   bootstrap lands in `app` (L4). New includes are `<arbc/...>` (whitelisted for
   `commands`, `scripts/check_levels.py:47`), `<ace/project/...>` (existing
   `commands → project` edge), and std. `commands` stays ImGui/GL/SDL-free
   (`docs/01-architecture.md:176-179`). arbc links transitively via `project`
   (`CMakeLists.txt:141,144`) — no CMake dependency edit.

2. **`AppState` owns exactly one `Document` for its lifetime (A7/D19).** It takes
   an `OpenedProject` by value (moves the `unique_ptr<Document>` in), is move-only
   / non-copyable, and there is exactly one per process. It also owns the
   `ProjectLayout` (so `save` can find `project.arbc`) and a persistent
   `arbc::Registry` (`register_builtin_kinds` at construction — D-open-7). The
   `Document`'s live `HousekeepingThread` (A4) therefore spans the whole run and
   is joined by the `Document` destructor at `AppState` teardown.

3. **`Selection` is project-level, transient, never a transaction (D19/A5/D15).**
   A headless model in `commands` over `arbc::ObjectId` (empty by default) with a
   primary/active member; `select`/`add`/`toggle`/`clear` mutate it directly — no
   `Document` transaction, no journal entry (D15: selection is transient app
   state). It is the project's one selection, shared by every canvas/panel (A5),
   not per-canvas.

4. **Command dispatch = one action → one libarbc transaction.** A `Command`
   (action) applied on the writer `Document` produces exactly one journal entry /
   one revision bump by default, via the `Document` transactional wrappers /
   `transact()` (`document.hpp:167,178`). Dispatch runs on the writer (single-
   writer, A4) and is **synchronous** at this leaf (no canvas/driver exists yet).
   Gesture **coalescing** (a stroke/drag → one step) is `editor.project.undo`; the
   off-UI-thread submit-queue + double-buffer is `editor.canvas.frame_sync`. This
   leaf ships the seam + transaction boundary, proven with the `Document`
   wrappers that exist today — **not** the concrete scene-edit command taxonomy
   (that arrives per edit leaf: `editor.cells.*`, `editor.camera.*`).

5. **Active tool is not re-homed.** It remains `dockmodel::ToolSelection`
   (`tool_rail.hpp:45-52`, A11/D20). The app layer holds `AppState` (commands) and
   `ToolSelection` (dockmodel) side by side; the two are **not** merged into one
   object (levelization forbids it — see D-app_state-4).

6. **Errors are values, never throws.** The bootstrap surfaces
   `open_project`/`create_project` failures through `platform::Result<T>` /
   `OpenError` (`project.hpp:70-79`); a launch that cannot open/create its project
   fails cleanly (logged, non-zero exit), never a throw across the app boundary.

7. **Layout persistence is out of scope.** Per-project last-active layout into
   `workspace/` is D21/D-workspaces-3, a separate later `editor.project.*` concern
   (`tasks/refinements/editor/open.md:146-151`). This leaf touches only session
   state (Document/selection/registry/dispatch), never the dock layout tree.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella.

- **Levelization (`check_levels` clean).** `python3 scripts/check_levels.py`
  passes **with no edit**: new `commands` code includes only `<arbc/...>`,
  `<ace/project/...>`, `<ace/scene/...>`, and std — no ImGui/GL/SDL; the app-layer
  wiring stays in `app` (L4). No new component, no new DAG edge. Primary
  structural assertion.
- **L1 logic — Catch2 unit (the bulk).** A new `tests/commands_test.cpp`
  (headless, joined to `ace_tests`, `CMakeLists.txt:173-183`), reusing `ScratchDir`
  (`tests/platform_test.cpp`), asserting the observable contract:
  - **Ownership / lifetime:** `AppState` built from `create_project(...)` owns a
    `workspace_backed() == true` `Document` and a `register_builtin_kinds`-seeded
    `Registry`; the exposed `layout().canonical` names `<root>/project.arbc`.
    `AppState` is move-only and holds exactly one `Document` (single-owner
    invariant, A7).
  - **Selection (transient, project-level):** `select`/`add`/`toggle`/`clear`
    mutate the `Selection` observably; a selection change leaves `journal().depth()`
    **unchanged** (D15: not a transaction) and the document revision unchanged.
  - **Dispatch (action → one transaction):** dispatching a proving `Command` that
    adds a composition/content/layer via the `Document` wrappers bumps the
    revision and increments `journal().depth()` by **exactly one** per command
    (the one-command-one-entry boundary); `can_undo()` flips true. A no-op command
    appends no entry.
  - **Bootstrap resolution:** the app-layer `open_or_create_app_state(fs, path)`
    helper opens an existing project directory and creates a fresh one, surfacing
    `OpenError` values (`NotADirectory`, `NoProject`) through `Result` — none
    thrown.
- **Rendered output — golden N/A (justified).** `app_state` composes ownership +
  selection + dispatch state; it renders nothing new. The reopened-document
  load-fidelity golden already lives in `open.md`
  (`tests/goldens/render_probe_64x64.rgba8`), and the canvas render path is
  `editor.canvas.view` (which depends on this leaf and carries the first new
  golden). No golden committed here.
- **UI e2e — ImGui Test Engine (boot lifecycle).** A headless e2e joined to
  `ace_shell_test` (`CMakeLists.txt:191-194`) drives `run_editor`/`Shell` for a
  bounded frame count with a project path pointing at a `ScratchDir`, and asserts
  via a test-visible accessor that the process comes up owning **exactly one**
  `AppState`/`Document` for its lifetime and tears it down cleanly on shutdown
  (the A7 single-Document invariant end-to-end). No new user-driven widget lands
  here — the canvas that *displays* the `Document` is `editor.canvas.view`, whose
  e2e drives the rendered surface; this e2e pins the ownership/bootstrap seam
  headless.
- **Threading (ASan/TSan) — the process-lifetime workspace `Document`.** Where
  `open.md`'s sanitizer scope was a `Document` created and dropped **within one
  open call**, this leaf holds the workspace-backed `Document` — and its live
  `HousekeepingThread` (A4) — for the **whole app run**. The boot → dispatch a
  command → checkpoint → shutdown lifecycle runs under the ASan and TSan presets
  and must be clean, exercising writer-thread commits against the background
  checkpointer over the session's lifetime and a clean `AppState`/`Document`
  teardown (thread join). Non-N/A sanitizer scope.
- **Coverage.** ≥90% diff coverage on changed lines
  (`diff-cover --fail-under=90`, `coverage` preset), including the dispatch
  transaction-boundary branches, the selection-is-transient path, and the
  bootstrap open-vs-create + error branches.
- **Format/build.** `clang-format --dry-run --Werror` clean; `dev` and `release`
  presets build; `scripts/gate` green.

No new WBS leaf is registered: every downstream consumer already exists as a
scheduled leaf — concrete scene-edit `Command` types land with their edit leaves
(`editor.cells.cell_model`, `editor.camera.*`), gesture coalescing with
`editor.project.undo`, the `project.arbc` dump with `editor.project.save`, the
off-UI-thread submit path with `editor.canvas.frame_sync`, and open-another-execs
with `editor.project.exec_new`. This leaf ships the seam they all consume.

## Decisions

- **D-app_state-1 — The session aggregate `AppState` lives in `commands` (L1); the
  `app` (L4) holds it for the process lifetime.** `commands` is the one L1
  component that already depends on `project` (for `OpenedProject`/`Document`) and
  `scene`, is whitelisted for `<arbc/...>` (`scripts/check_levels.py:47`), and is
  designated *"actions → libarbc transactions"* (§8, `docs/01-architecture.md:131`).
  So the session state + dispatch logic is headless and Catch2-testable (§9
  `:187` lists "app-state … selection" as L1 Catch2 coverage), while "the app owns
  exactly one `Document`" (A7) is satisfied literally — the L4 app object holds the
  `AppState`. *Alternative rejected:* a new `session`/`app_state` **component**
  (a gratuitous DAG node for one aggregate with one call site), or putting the
  session in `project` (which §7 scopes to *I/O operations* — open/save/gc — not
  runtime session state, selection, and dispatch), or owning it directly in `app`
  L4 (would push the app-state logic out of the testable L1 core — the exact thing
  §8/§9 forbid).

- **D-app_state-2 — `AppState` owns the single `Document` + a persistent
  `Registry` + the `ProjectLayout`, moved in from `OpenedProject`.** Move-only,
  one per process; the persistent kind `Registry` (`register_builtin_kinds` at
  construction) collects the deferral `open.md` D-open-7 left here — `save`/export
  and the future A6 plugin seam reuse it, and re-registering per open is no longer
  needed. *Rationale:* A7's "no multi-doc management, GC root-set is that one
  document" wants a single move-only owner; folding layout + registry into it
  keeps the whole session reachable from one object. *Alternative rejected:* a
  shared/refcounted `Document` (contradicts single-writer/single-owner, A4/A7), or
  leaving the persistent registry unowned (forces `save` to rebuild one and breaks
  the plugin-seam lifetime).

- **D-app_state-3 — `Selection` is transient project-level state in `commands`,
  never a libarbc transaction.** A headless `ObjectId` set + primary, mutated
  directly, shared by every canvas/panel (D19/A5). *Rationale:* D15 fixes
  selection as transient app state *outside* the journal; A5 fixes it as the
  project's one selection, not per-canvas. *Alternative rejected:* per-canvas
  selection (D19/A5 forbid — a canvas is *only* a camera), or journaling selection
  changes (D15: selection is not scene data, so undoing a click is wrong).

- **D-app_state-4 — Active tool stays `dockmodel::ToolSelection`; `AppState` does
  not absorb it.** The app layer composes `AppState` (commands) and `ToolSelection`
  (dockmodel) side by side. *Rationale:* A11/D20 already home the active tool in
  `dockmodel` (L1, arbc-free); merging it into the arbc-owning `AppState` is
  structurally illegal — `commands` does not (and must not) depend on `dockmodel`,
  and `dockmodel` is arbc/ImGui/GL/SDL-free by construction. Keeping them separate
  respects the DAG and matches "selection is project state, active-tool is UI
  state" (D19 vs D20). *Alternative rejected:* one combined "editor state" object
  (would force a `commands ↔ dockmodel` edge and drag arbc into the arbc-free UI-
  state model, or vice versa — a levelization violation).

- **D-app_state-5 — Command dispatch is a synchronous, single-writer action →
  one-transaction seam; coalescing and off-thread submit are downstream.** A
  `Command` applied on the writer `Document` yields one journal entry / one
  revision bump (`document.hpp:178,184`); dispatch runs inline (single writer, A4).
  *Rationale:* the doc-14 journal *is* undo (§9 prose `:366-372`), so the dispatch
  need only open transactions correctly; there is no canvas/driver yet, so
  synchronous is correct and honors "UI submits, writer applies" trivially. The
  one-command-one-entry boundary is the invariant this leaf pins; gesture
  coalescing is `editor.project.undo` and the double-buffered off-UI-thread submit
  is `editor.canvas.frame_sync`. *Alternative rejected:* building the async
  submit-queue/driver now (that is `frame_sync`'s scope and needs a `HostViewport`
  that does not exist), or reimplementing an editor-side undo stack (D15/§9: undo
  *is* the library journal, not a reimplementation).

- **D-app_state-6 — App bootstrap resolves a project path into the `AppState` at
  launch, defaulting to a fresh scratch project when none is given.**
  `run_editor`/`main` take an optional project directory; the bootstrap calls
  `project::open_project` (existing dir) or `create_project` (new), moving the
  result into the process's one `AppState`. With no path, it `create_project`s a
  fresh scratch project so the invariant "one process owns exactly one `Document`,
  never empty" always holds and the app is drivable headless. *Rationale:* the
  in-app New/Open menu + recent-projects + native folder picker is
  `editor.project.open_ui` (already registered, depends `exec_new`), and open-
  another-execs is `editor.project.exec_new` — neither exists yet at this leaf, so
  the bootstrap must stand on its own; a path arg is exactly what `exec_new`'s new
  `exec` will pass (A7/D19). *Alternative rejected:* blocking launch on a picker
  (the picker is a later leaf; app_state cannot depend forward on it), or starting
  with no `Document` (violates the single-`Document`-for-lifetime invariant and
  leaves every downstream canvas/panel with nothing to bind).

## Open questions

- _None — all decided against the constitution._ D19/A5/A7 fix the single-
  `Document`-per-process owner and project-level (not per-canvas) selection;
  D15/§9-prose fix selection as transient and dispatch as action→doc-14
  transaction with coalescing owned by `undo`; A4 fixes single-writer/synchronous
  here and the off-thread driver as `frame_sync`; A11/D20 fix active-tool in
  `dockmodel`; §8/§9 fix the levelization (home in `commands`, no new edge) and the
  test model; D-open-7 fixes the persistent registry as this leaf's. The library
  API is concrete in the fetched surface (`Document::transact`/`journal`/`add_*`,
  `Journal::depth`, `register_builtin_kinds`). The one product-polish degree of
  freedom — what a **no-project first launch** should show once a picker exists —
  is settled defensibly here (create a scratch project) and revisited only when
  `editor.project.open_ui` lands; it is **not** a WBS task (surfaced to the
  parking lot for the human record). **No doc delta required:** no new dependency,
  no new component, no new DAG edge, no deviation from a decided behavior.

## Status

**Done** — 2026-07-17.

- `src/commands/ace/commands/app_state.hpp` + `src/commands/app_state.cpp`: `AppState` session aggregate (owns the one workspace-backed `Document` + persistent seeded `Registry` + `ProjectLayout` + project-level `Selection`), synchronous single-writer `dispatch` action→libarbc-transaction seam, and `open_or_create_app_state(fs, path)` bootstrap helper.
- `src/commands/ace/commands/selection.hpp` + `src/commands/selection.cpp`: transient project-level `Selection` model over `arbc::ObjectId` (select/add/toggle/clear, never a transaction per D15).
- `src/app/ace/app/shell.hpp` + `src/app/shell.cpp`: `run_editor` now resolves a project directory into the `AppState` (fresh scratch when none given) and holds it for the process lifetime beside `ToolSelection`; open-failure branch (NotADirectory / NoProject) returns non-zero cleanly.
- `CMakeLists.txt`: wired `commands_test.cpp` into `ace_tests` and `app_state_e2e_test.cpp` into `ace_shell_test`.
- `tests/commands_test.cpp`: 5 Catch2 units (AppState ownership/registry/layout, transient Selection, one-command-one-transaction dispatch, `open_or_create_app_state` create/open + NotADirectory/NoProject) — 53 assertions, all pass under ASan/TSan.
- `tests/app_state_e2e_test.cpp`: ImGui-shell e2e (boot→dispatch→checkpoint→shutdown, single-Document invariant) — passes headless.
- `tests/shell_smoke_test.cpp` (fixer patch): added smoke that points `project_dir` at a regular file so `run_editor` exercises the `NotADirectory` return-2 branch, closing the diff-coverage gap.
