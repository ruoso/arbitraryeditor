# editor.dock.workspaces — Saved layout presets (Paint / Compose / Review)

## TaskJuggler entry

`tasks/00-editor.tji:83-88` — `task workspaces "Saved layout presets (local UI
state)"` under `editor.dock`. Effort `2d`, `allocate team`,
`depends !dockspace, editor.foundation.platform_services`. The `note` cites
**Design: D18 / §9** and (now) the new **D21** row this task adds.

Back-link ritual: the `.tji` note at `tasks/00-editor.tji:87` currently points at
the flat `tasks/refinements/workspaces.md`; the sibling convention places
refinements under the first dot-segment of the id (`tasks/refinements/editor/…`),
while this driver brief wrote the file at `tasks/refinements/editor.dock/workspaces.md`.
The closer fixes the `note` back-link to the file's real landing path per the
ritual in `tasks/refinements/README.md:57-68` (and reconciles the directory with
the sibling `editor/` set if the orchestrator prefers it).

Downstream: `workspaces` is a **sibling shell feature**, not a predecessor of any
other leaf — no task lists it in `depends` beyond the `editor.integration.*`
smoke roll-up. It is the leaf that turns the `DockLayout` value type into
persisted, one-click-switchable presets, closing the D18 "Saved workspaces"
promise and the flagged-open "Paint / Compose / Review default arrangements"
item (`docs/00-design.md:488`, now resolved by D21).

## Effort estimate

**2 days** (from the `.tji`). What dominates: (1) the `DockLayout`/`DockNode`
serialization projection + its round-trip/malformed-input test matrix; (2) the
`WorkspaceStore` load/save/list/delete logic over `platform::FileSystem` with a
seed-built-ins-on-empty path; (3) drawing the three built-in arrangements; (4) the
dock-layer preset switcher + its ImGui Test Engine e2e. Explicitly **out of
scope**: persisting the *last-active* per-project layout into `workspace/` (an
`editor.project.*` concern per D21); resolving the OS-specific prefs directory
portably across XDG/AppData/OPFS (the store takes a root path — the app supplies
it); rename-in-place (expressible as save-as + delete); floating-window state
(deferred by D18).

## Inherited dependencies

**Settled (from `editor.dock.dockspace`).** The L1 split-tree is shipped:
`ace::dockmodel::DockNode` and `ace::dockmodel::DockLayout` are value-typed,
`operator==`-comparable, and serialize "as plain data — no ImGui, no handles"
(`src/dockmodel/ace/dockmodel/dockmodel.hpp:25-101`). The header comment at
`dockmodel.hpp:48-52` **names `editor.dock.workspaces` as the consumer that will
persist this tree**. `DockLayout::make_default(views)` (`dockmodel.hpp:62`) is the
deterministic "rebuild the default" arrangement; `valid()` (`:70`),
`view_ids()` pre-order (`:75`), and defaulted `operator==` (`:97`) are the
invariants a round-trip must preserve. The `dock`-layer host
`ace::dock::Dockspace` (`src/dock/ace/dock/dock.hpp`, `src/dock/dock.cpp`) already
translates a `DockLayout` to ImGui's live tree via a **once-guarded
`DockBuilder` rebuild** (`build_node`, `src/dock/dock.cpp:20-35`;
`DockBuilderDockWindow(id, node)` at `:23`) — the seam a "switch preset → replace
layout → raise the rebuild flag" path plugs into. `io.IniFilename` is pinned
`nullptr` (`src/app/shell.cpp:67`), so layout is ours to persist, never
imgui.ini (D-dockspace-3).

**Settled (from `editor.dock.view_registry`).** The set of open views IS
`DockLayout::view_ids()` — there is no second registry of "what's open"
(D-view-registry-2). Instance ids are stable strings: a singleton slug
(`"inspector"`) or `slug#N` for the one multi-instance type (`"canvas#2"`),
parsed by `parse_view_id()` (`src/dockmodel/ace/dockmodel/view_registry.hpp:60`).
`ViewRegistry` holds the **monotonic, non-recycling** per-type counter
`next_index_` (`view_registry.hpp:95`) whose D-view-registry-4 no-alias guarantee
this task must preserve when it **adopts** a deserialized layout.

**Settled (from `editor.foundation.platform_services`).** The injectable I/O seam
is `ace::platform::FileSystem` (`src/platform/ace/platform/filesystem.hpp:18-61`):
`read_file` (`:31`), `write_file` (`:35`), `make_directories` (`:40`), `exists`
(`:22`), `list_directory` (`:26`), and — for local-state writes — the D16 publish
`atomic_replace(path, contents)` (`:45`). The header comment (`:13-17`) already
names "read/write layout presets" as an intended use. `FileSystem` is reached via
`PlatformServices::filesystem()`
(`src/platform/ace/platform/platform_services.hpp:18`), wired at L4 bootstrap.

**Pending — none.** Both predecessors are `complete 100`. `dockmodel` already
`DEPENDS base platform` (`CMakeLists.txt:145`), so serializing through
`platform::FileSystem` needs **no lint edit and no new DAG edge**.

## What this task is

Turn the in-memory `DockLayout` into **named, persisted, one-click presets**.
Three levels:

1. **L1 (`dockmodel`)** — the headless core, ImGui-free:
   - a **serialization projection** of `DockLayout`/`DockNode` to/from a
     versioned line-oriented text string (`serialize_layout` /
     `parse_layout`);
   - a **`WorkspacePreset`** = `{name, DockLayout}` and a `workspace_builtins()`
     catalog returning the three immutable D21 arrangements over the D18 view
     slugs;
   - a **`WorkspaceStore`** constructed with a root `std::filesystem::path` +
     `platform::FileSystem&`: `presets()` (built-ins ∪ user), `apply(name)` →
     the `DockLayout` to seed, `save(name, layout)` (atomic publish), `remove(name)`
     (user only), with a missing/corrupt store falling back to built-ins;
   - `ViewRegistry::adopt(const DockLayout&)` — re-seed each per-type counter to
     `max(present index)+1` so a post-apply `mint_id` can never alias a restored
     pane (preserves D-view-registry-4).
2. **L3 (`dock`)** — the preset **switcher** UI (a menu/combo of preset names +
   "Save current as…" / "Delete"): a click sets the `Dockspace`'s layout to the
   applied preset, `adopt()`s the registry, and raises the existing rebuild flag.
3. **L4 (`app`)** — bootstrap wiring: construct the `WorkspaceStore` with the
   injected `FileSystem` + the resolved prefs root, hand it to the `Dockspace`
   draw-content in `run_editor` (`src/app/shell.cpp:145`).

## Why it needs to be done

D18 promises "Saved workspaces … one click, like VS Code / Blender," and
`docs/00-design.md:488` flags the actual Paint/Compose/Review arrangements as the
one still-open dockspace item. The `DockLayout` header, `dockspace.md`
(Constraint 2), and `view_registry.md` (Constraint 2) all defer serialization to
this leaf by name. Without it the uniform dockspace resets to `make_default` every
launch and offers no task-oriented layouts — the feature D18 sells as the reason
the shell is uniform in the first place. The `.tji` pulls in `platform_services`
precisely so this leaf can persist to disk through the WASM-safe seam.

## Inputs / context

**Design constitution (normative).**
- `docs/00-design.md:479` — **D18** (uniform dockspace; "Saved workspaces";
  "Layout is local UI state (`workspace/`/prefs), not `project.arbc`").
- `docs/00-design.md:451-455` — §10 prose: named presets Paint/Compose/Review;
  layout is machine-local, never scene data.
- `docs/00-design.md:481` — **D21** (this task's doc delta): the three built-in
  arrangements, per-user prefs storage, versioned text format, save/delete,
  atomic publish, corrupt→built-ins fallback.
- `docs/00-design.md:477` — **D16** (`workspace/` = machine-local scratch,
  rebuilt from core, excluded from VCS; Save = a publish step).
- `docs/00-design.md:467` — **D6** (Overview = the multi-camera/shot map; grounds
  the Compose arrangement).

**Architecture constitution.**
- `docs/01-architecture.md:152-179` — §8 levelization DAG. `dockmodel` is **L1**,
  `May depend on {base, platform}`, **no ImGui/GL/SDL** (A8, `:177-179`); `dock`
  is **L3** `{dockmodel, views, imgui}`.
- `docs/01-architecture.md:261` — **A11** (`dockmodel` owns headless UI state) —
  the WorkspaceStore's home.
- `docs/01-architecture.md:253` — **A3** (PlatformServices file seam; WASM swaps
  it for File System Access API / OPFS).
- `docs/01-architecture.md:181-208` — §9 layered DoD (rows `:187`/`:189`/`:190`).

**Source seams this leaf extends (extend, do not fork).**
- `src/dockmodel/ace/dockmodel/dockmodel.hpp:17,25-101` — `SplitOrientation`,
  `DockNode`, `DockLayout` (the tree to serialize; `make_default:62`, `valid:70`,
  `view_ids:75`, `operator==:97`).
- `src/dockmodel/ace/dockmodel/view_registry.hpp:19,60,67-96` — `ViewType`,
  `parse_view_id`, `ViewRegistry`/`next_index_` (extended with `adopt`).
- `src/dock/ace/dock/dock.hpp`, `src/dock/dock.cpp:20-35,64` — `Dockspace`,
  `build_node`, the once-guarded `DockBuilder` rebuild + rebuild flag.
- `src/platform/ace/platform/filesystem.hpp:18-61` — `FileSystem`/`NativeFileSystem`
  (`atomic_replace:45`, `read_file:31`, `make_directories:40`, `list_directory:26`).
- `src/app/shell.cpp:66-67,145` — `run_editor` wiring point; docking enabled,
  `io.IniFilename = nullptr`.
- `scripts/check_levels.py` — `ALLOWED["dockmodel"] = {"base","platform"}`;
  `dockmodel` absent from every `EXTERNAL_ALLOWED` set (imgui/gl/sdl/arbc all
  forbidden).

**Test rigs.**
- `tests/dockmodel_test.cpp`, `tests/view_registry_test.cpp` — Catch2 L1 pattern,
  joined to `ace_tests` (`CMakeLists.txt:170-179`).
- `tests/platform_test.cpp` — the `ScratchDir` (temp-dir, wiped on entry/exit)
  filesystem-round-trip pattern to reuse for the `WorkspaceStore` cases.
- `tests/dockspace_e2e_test.cpp`, `tests/view_registry_e2e_test.cpp` — ImGui Test
  Engine rig (offscreen SDL + llvmpipe), joined to `ace_shell_test`
  (`CMakeLists.txt:186-199`); drive by stable view id, ImGui-context-first
  teardown, `tests/lsan.supp` for Mesa/EGL leaks.

## Constraints / requirements

1. **No lint edit; no new component; no new DAG edge.** All new L1 code lands in
   the existing `dockmodel` component; all new UI in `dock`; wiring in `app`.
   `python3 scripts/check_levels.py` passes **unchanged**.
2. **`dockmodel` stays ImGui/GL/SDL/libarbc-free (A8).** New `dockmodel` headers
   include only `<ace/dockmodel/…>`, `<ace/platform/…>`, and std. The
   serialization + store logic is pure data over `base`/`platform` — headless and
   Catch2-testable with no graphics context.
3. **Round-trip fidelity.** `parse_layout(serialize_layout(L)) == L` for every
   valid `DockLayout` (empty, lone leaf, deep asymmetric splits, multi-tab leaves
   with a non-zero `active`, multi-instance ids like `canvas#3`). A parsed layout
   must satisfy `valid()`; the projection never invents or drops a view id.
4. **Malformed input is total and non-throwing.** `parse_layout` returns
   `std::optional<DockLayout>` = `nullopt` on any defect — bad version tag,
   truncation, a ratio outside (0,1), a duplicate view id, an unparsable/unknown
   token, a leaf with an out-of-range `active`. It never throws and never yields
   an invalid layout (mirrors `parse_view_id`'s total style).
5. **Local scratch, atomic publish (D16/D21).** The user store is written via
   `FileSystem::atomic_replace` under a per-user prefs root the app supplies;
   `make_directories` seeds the parent. A missing store yields the built-ins; a
   corrupt store logs-and-falls-back to built-ins (rebuild-from-default), never
   aborts. The portable `project.arbc` is never touched, and `io.IniFilename`
   stays `nullptr`.
6. **Built-ins are immutable and deterministic.** `workspace_builtins()` returns
   exactly Paint/Compose/Review (D21) as `valid()` layouts over D18 view slugs,
   identical every call; `save`/`remove` operate on user presets only and reject a
   name that collides with a built-in.
7. **Registry no-alias preserved (D-view-registry-4).** Applying a persisted
   layout calls `ViewRegistry::adopt(layout)` before any subsequent `mint_id`, so
   restored `slug#N` ids can never be re-minted.
8. **Component homes follow §8.** Serialization/store/`adopt` → `dockmodel` (L1);
   the switcher + "Save as…/Delete" chrome → `dock` (L3); store construction +
   prefs-root resolution + load-at-startup → `app` (L4).

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`); `scripts/gate`
green (check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization.** `python3 scripts/check_levels.py` clean and unchanged; no new
  `dockmodel` include of imgui/gl/sdl/arbc; no new edge in `ALLOWED`/`EXTERNAL_ALLOWED`.
- **L1 logic — Catch2 (`tests/workspaces_test.cpp`, joined to `ace_tests`).**
  - serialize→parse round-trip equality (`operator==`) over the Constraint 3
    tree matrix, including empty and multi-instance-id cases;
  - malformed-input matrix (Constraint 4) each → `nullopt`, no throw;
  - `workspace_builtins()` = {Paint, Compose, Review}, each `valid()` with the
    expected `view_ids()`, deterministic across calls;
  - `WorkspaceStore` over `NativeFileSystem` + a `ScratchDir` (per
    `tests/platform_test.cpp`): fresh root → built-ins; `save` then reopen returns
    the user preset (via `atomic_replace`); `remove` deletes a user preset and
    refuses a built-in; a hand-written corrupt store file → falls back to
    built-ins without throwing;
  - `ViewRegistry::adopt`: after adopting a layout containing `canvas#3`, the next
    `mint_id(Canvas)` yields `canvas#4` (no alias).
- **Rendered output — golden N/A (justified).** The switcher composes ImGui
  chrome, not a libarbc `Document`, so there is no byte-exact `render_offline`
  golden (goldens begin at `render_probe`, `tests/render_probe_test.cpp`).
- **UI behavior — ImGui Test Engine e2e (`tests/workspaces_e2e_test.cpp`, joined
  to `ace_shell_test`).** Boot the shell + `Dockspace` + switcher; click the
  **Compose** preset by stable widget id and assert the live layout's
  `view_ids()`/arrangement matches the Compose built-in; switch to **Paint** and
  assert it changes; drive **Save current as "X"** then re-select "X" and assert
  round-trip. Reuse the `shell_e2e_test.cpp` rig (drive by id, ImGui-context-first
  teardown); no byte-exact screenshot assertion (software-GL pixels flaky) — a
  screenshot baseline is optional signal only.
- **Threading — no new surface.** All state is UI-side, mutated only on the UI
  thread inside the draw loop; preset file I/O is **synchronous on the UI thread**
  (small files, a click cadence), adding no background thread. Runs clean under the
  existing offscreen `asan` lane with `tests/lsan.supp`; no new TSan target
  (real-concurrency TSan stays scoped to `editor.canvas.frame_sync`). If a future
  need pushes preset I/O off-thread, that is separate work — not this leaf.
- **Coverage.** ≥90% diff coverage on changed lines (`diff-cover --fail-under=90`
  under the coverage preset); tests ship with the task.
- **Format/build.** `clang-format --dry-run --Werror` clean; `dev` + `release`
  presets build; both new test files registered in `CMakeLists.txt`.
- **No deferred WBS leaf.** Everything in scope ships here. The only deferral —
  persisting the *last-active* per-project layout into `workspace/` — is an
  existing `editor.project.*` concern (D21), not a new leaf this task spawns.

## Decisions

- **D-workspaces-1 — Serialization lives in `dockmodel` (L1) as a pure text
  projection; the store takes a `FileSystem&`, not the OS.** `serialize_layout` /
  `parse_layout` + `WorkspaceStore` are headless, tested with a `ScratchDir` over
  `NativeFileSystem`.
  *Rationale:* A11 makes `dockmodel` the home of headless UI state; the
  `base`/`platform` edge already exists, so no DAG change. Injecting `FileSystem`
  keeps the store fully unit-testable and WASM-swappable (A3).
  *Alternative rejected:* putting load/save in the `dock` (L3) layer — it would
  bury persistence logic behind an ImGui context, forcing an e2e for what is
  plain-data logic and violating the "L1 is the testable core" seam.

- **D-workspaces-2 — A hand-rolled versioned line-oriented text format, no JSON
  dependency.** The tree is tiny and value-typed; a `ace-workspace <version>`
  header followed by a pre-order encoding of splits (`orientation` + `ratio`) and
  leaves (`active` + ordered ids) is enough.
  *Rationale:* keeps `dockmodel`'s dependency set exactly `{base, platform}` — a
  JSON lib would be a new L1 dependency needing an A-row; the format is local
  scratch with no portability promise (§9), so ergonomics beat interchange.
  *Alternative rejected:* reusing libarbc's JSON — `dockmodel` may **not** depend
  on libarbc (§8 table), and pulling it in would break levelization.

- **D-workspaces-3 — Named presets persist to the per-user prefs store, not the
  per-project `workspace/` (D21).** Paint/Compose/Review span every project;
  binding them to one project's rebuilt-from-core scratch dir would lose them on a
  fresh checkout and duplicate them per project.
  *Rationale:* resolves D18's "`workspace/` or prefs" ambiguity for *named* presets
  toward prefs, matching VS Code/Blender where workspaces are user-global.
  *Alternative rejected:* storing presets in `workspace/` — that dir is
  machine-local scratch "rebuilt from the core, excluded from VCS" (D16), the wrong
  lifetime for cross-project presets. (The *last-active* layout, which IS
  per-project, stays a separate `editor.project.*` concern.)

- **D-workspaces-4 — The open-view set is the single source of truth; do not
  serialize `ViewRegistry` counters.** The file stores only the layout (its view
  ids); on apply, `ViewRegistry::adopt` re-derives each counter from the ids
  present.
  *Rationale:* mirrors D-view-registry-2 ("no second registry of what's open") and
  keeps the format minimal — a serialized counter could drift from the layout and
  reintroduce the alias bug it exists to prevent.
  *Alternative rejected:* persisting `next_index_` alongside the tree — redundant
  state that can disagree with the ids it guards.

- **D-workspaces-5 — Applying a preset replaces the layout and raises the existing
  `Dockspace` rebuild flag; it does not re-enable `io.IniFilename`.** The switch
  reuses the once-guarded `DockBuilder` translation path
  (`src/dock/dock.cpp:20-35`).
  *Rationale:* the dockspace already owns "seed ImGui from a `DockLayout`"; a
  preset switch is just a new seed. Persisting via imgui.ini (D-dockspace-3) would
  fork layout ownership away from `dockmodel`.
  *Alternative rejected:* turning `io.IniFilename` back on to let ImGui persist —
  it would bypass our versioned format, the prefs location, and the named-preset
  model entirely.

## Open questions

_None — all decided._ The one genuinely design-level gap (named-preset storage
location + the concrete arrangements + format) is resolved by the **D21** doc
delta this task adds. The per-user prefs **directory resolution** (XDG /
`%APPDATA%` / OPFS) is intentionally left to L4 bootstrap — the `WorkspaceStore`
takes a root path, so no design decision is pending; if the orchestrator wants
that resolution tracked, it is app-wiring detail, not a WBS leaf. No re-audit task
is spawned.

## Status

**Done** — 2026-07-17.

- `src/dockmodel/ace/dockmodel/workspaces.hpp` + `src/dockmodel/workspaces.cpp`: L1 headless core — `serialize_layout`/`parse_layout` (versioned line-oriented text, D-workspaces-2), `WorkspacePreset`, `workspace_builtins()` (Paint/Compose/Review, D21), `WorkspaceStore` over `platform::FileSystem` with atomic publish + corrupt-fallback.
- `src/dockmodel/ace/dockmodel/view_registry.hpp` + `src/dockmodel/view_registry.cpp`: added `ViewRegistry::adopt(const DockLayout&)` — re-seeds per-type counters from deserialized ids, preserving D-view-registry-4 no-alias guarantee.
- `src/dock/ace/dock/dock.hpp` + `src/dock/dock.cpp`: L3 preset switcher in `Dockspace::draw_content` — combo of preset names, "Save current as…", "Delete"; click sets layout, calls `adopt()`, raises rebuild flag (D-workspaces-5).
- `src/app/shell.cpp`: L4 bootstrap — constructs `WorkspaceStore` with injected `FileSystem` + resolved prefs root, hands it to `run_editor` draw loop.
- `CMakeLists.txt`: linked `ace::platform` into `ace_app`; registered `tests/workspaces_test.cpp` and `tests/workspaces_e2e_test.cpp`.
- `docs/00-design.md`: added D21 row (three built-in arrangements, per-user prefs storage, versioned text format, save/delete, atomic publish, corrupt→built-ins fallback).
- `tests/workspaces_test.cpp`: Catch2 unit — round-trip matrix, malformed→nullopt matrix, built-ins, `WorkspaceStore` over `ScratchDir`/`NativeFileSystem` incl. corrupt-fallback, `adopt` no-alias (6 test cases, 87 assertions).
- `tests/workspaces_e2e_test.cpp`: ImGui Test Engine e2e — apply Compose→Paint, Save-as "X", re-apply X round-trip (4 assertions). No golden (switcher is chrome, not a `Document` render).
