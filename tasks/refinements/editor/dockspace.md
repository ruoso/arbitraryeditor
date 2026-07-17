# editor.dock.dockspace — Split-tree + tabs over ImGui docking

## TaskJuggler entry

`tasks/00-editor.tji:62-66` — `task dockspace` under `editor.dock`. Effort `3d`,
`allocate team`, `depends editor.foundation.app_shell`. The `note` (`:66`) cites
**Design D18** and names this refinement. (The `.tji` note currently points at
`tasks/refinements/dockspace.md`; the real path is
`tasks/refinements/editor/dockspace.md` — the closer fixes the back-link per the
ritual in `tasks/refinements/README.md:57-68`.) Downstream, every other `dock`
leaf hangs off this one: `editor.dock.view_registry` `depends !dockspace`
(`tasks/00-editor.tji:71`), `editor.dock.tool_rail` `depends !view_registry`
(`:77`), and `editor.dock.workspaces` `depends !dockspace,
editor.foundation.platform_services` (`:83`).

## Effort estimate

**3 days** (from the `.tji`). The surface is two components — the L1 `dockmodel`
layout value type and the L3 `dock` ImGui-docking shell — plus the app-layer
wiring and a new Test Engine e2e that drives docking interactively (tab-select,
drag-between-containers) rather than the presence-only check the probe e2e does
today. The estimate is dominated by getting the `DockBuilder` translation right
(programmatic layout with `io.IniFilename == nullptr`, deterministic across runs
so the e2e is stable) and by pinning the `DockLayout` invariants so `view_registry`
and `workspaces` inherit a solid L1 model — not by surface area. No threading, no
Document render, no real view types yet.

## Inherited dependencies

**Settled (from `editor.foundation.build`).** The 12-component levelized skeleton
exists and every edge this task needs is already declared. `dockmodel` is L1
(`CMakeLists.txt:145`, `ace_component(dockmodel DEPENDS base platform)`) and is
deliberately kept out of every external stack — `scripts/check_levels.py:42-48`
lists `dockmodel` in none of `EXTERNAL_ALLOWED`'s `imgui`/`sdl`/`gl_api`/`arbc`
sets, so the lint forbids it any ImGui/GL/SDL/libarbc include. `dock` is L3
(`CMakeLists.txt:151`, `ace_component(dock DEPENDS dockmodel views)`), links ImGui
transitively through `views` (`:150`, `views … LIBS imgui`), and is whitelisted
for ImGui directly (`scripts/check_levels.py:45`,
`EXTERNAL_ALLOWED["imgui"] = {"views", "dock", "app"}`). `app` already depends on
`ace::dock` (`CMakeLists.txt:163`). Both component bodies are near-empty stubs
today — `src/dockmodel/dockmodel.cpp:1-8` and `src/dock/dock.cpp:1-8` declare only
`name()`; `src/dock/dock.cpp:2` already includes `<ace/views/views.hpp>` (the
declared `dock`→`views` edge). **No lint edit and no new DAG edge is required.**

**Settled (from `editor.foundation.app_shell`).** The `Shell` surface exists
(`src/app/ace/app/shell.hpp:33`, lifecycle `init() → (new_frame(); draw_ui();
render())* → shutdown()`). ImGui is created with the **docking branch enabled** —
`src/app/shell.cpp:66`, `io.ConfigFlags |= ImGuiConfigFlags_DockingEnable`
(comment "docking branch (A2/D18)"), and the ImGui `docking` tag is pinned in
CMake — but **no `DockSpace`/`DockSpaceOverViewport`/`DockBuilder` call exists
anywhere** (this leaf is the first). Crucially, `src/app/shell.cpp:67` sets
`io.IniFilename = nullptr` ("reproducible headless runs"), so ImGui persists no
layout to disk: the dock tree must be **built programmatically in-code each run**.
`draw_ui()` owns no panes; it invokes an installed content callback
(`src/app/shell.hpp:45,51,64` — `set_draw_content(std::function<void()>)` /
`draw_content_`), so the dockspace draw plugs into the same seam the probe uses.
`imgui_context()` (`shell.hpp:60`) is exposed for the Test Engine.

**Settled (from `editor.foundation.render_probe`).** The **ImGui Test Engine e2e
rig** is live and reusable (`tests/shell_e2e_test.cpp`): it boots the shell against
the SDL offscreen driver + software GL, registers a test via
`IM_REGISTER_TEST(engine, "<category>", "<name>")` (`:65`), drives widgets by their
stable exported id via `ctx->WindowInfo(...)` (`:68`), pumps the frame loop
`new_frame(); draw_ui(); render(); ImGuiTestEngine_PostSwap(...)` until the queue
drains (`:92-98`), and tears down ImGui-context-first (`:132-138`). It is built as
the `ace_shell_test` ctest under the pinned offscreen-GL env
(`CMakeLists.txt:185-197`). The reusable pane the dockspace can host as a
"canvas-is-a-view" stand-in exists: `ace::views::draw_probe_pane(texture, w, h)`
and its stable id `ace::views::probe_pane_title()` → `"Render Probe"`
(`src/views/views.cpp:16-25`), owned in the app layer by `ProbeView`
(`src/app/ace/app/probe.hpp`) and installed via `set_draw_content`
(`src/app/shell.cpp:151`).

**Pending (this leaf owns them).** The `DockLayout` split-tree value type and its
invariants/operations in `dockmodel`; the `DockBuilder`-based translation +
dockspace-host draw in `dock`; the app-layer wiring that installs the dockspace as
the shell's draw-content; a set of stably-id'd placeholder panes to exercise the
docking mechanics; the `dockmodel` Catch2 unit suite; and the interactive docking
e2e.

## What this task is

Stand up the **fully-uniform dockspace shell** (D18): a recursive split-tree of
containers, each a tab-group of relocatable views, hosted over ImGui's docking
branch, with **drag-to-dock, split, resize, and tab-grouping** all working. The
work splits across its two §8 level homes: `dockmodel` (L1, UI-agnostic) owns a
declarative **`DockLayout`** — a recursive tree of split nodes (orientation +
ratio) and leaf tab-groups (an ordered list of view ids + the active tab) — with
pure operations and invariants, unit-tested headless; `dock` (L3, ImGui) draws the
main-viewport dockspace host each frame, translates a `DockLayout` into ImGui via
`DockBuilder` on first use (because `io.IniFilename == nullptr`, layout is rebuilt
in-code, not restored from `imgui.ini`), and lets ImGui's native docking own the
interactive drag/split/resize/tab mechanics. Because there is **no real view
registry yet**, the dockspace is populated with a small set of stably-id'd
**placeholder panes** — one reusing the render_probe pane as a canvas stand-in
(instantiating D18's "canvas is a view"), plus a couple of plain panels — purely
to exercise and assert the docking mechanics.

It does **not** build the real view catalog or open/close/reopen semantics (that
is `editor.dock.view_registry`, `tasks/00-editor.tji:68-72`), the fixed tool rail
/ view launcher (`editor.dock.tool_rail`, `:74-78`), or layout persistence /
saved workspace presets (`editor.dock.workspaces`, `:80-84`) — all already in the
WBS and all depending, directly or transitively, on this leaf.

## Why it needs to be done

The entire `dock` sub-tree hangs off this leaf: `view_registry`, `tool_rail`, and
`workspaces` all depend on it (`tasks/00-editor.tji:71,77,83`). D18 makes the
dockspace the editor's whole shell paradigm — "there is no special editor area,"
every view (including every canvas) is relocatable content, and "anything can be
closed." None of that is expressible until the split-tree-over-ImGui-docking
mechanism exists and the L1 `DockLayout` model that the registry and the workspace
presets will extend is laid down. app_shell deliberately stopped at a dockspace-less
window (`src/app/ace/app/shell.hpp:32` names "the dockspace is editor.dock.dockspace");
this leaf turns the enabled-but-unused docking config flag into the actual uniform
dockspace, and gives `view_registry`/`workspaces` a concrete `DockLayout` seam
instead of inventing one.

## Inputs / context

**Design docs (normative — the constitution).**

- `docs/00-design.md` **D18** (`:479`, log row) — the governing decision: *"The
  shell is a fully-uniform dockspace (Blender/ImGui-style — no privileged editor
  area, no keep-a-canvas guardrail …): a recursive split-tree of containers, each
  a tab-group of relocatable views … Any view → any container; drag-to-dock,
  split, resize. Canvas is a view … The fixed tool rail hosts modal tools and the
  view launcher … Saved workspaces. Layout is local UI state (`workspace/`/prefs),
  not `project.arbc`. Floating windows deferred."* Its normative prose is **§10**
  (`docs/00-design.md:414-456`): the fully-uniform / no-guardrail thesis
  (`:414-423`); **views are content, decoupled from place — drag-to-dock drop
  zones (center = add-as-tab, edge = split), splitters resize, tree splits H/V
  recursively "the same model all the way down"** (`:424-429`); **the canvas is a
  view too** (`:430-435`); the tool rail = home base + view launcher (`:446-450`);
  saved workspaces (`:451-452`); **layout is local UI state, never scene data,
  living in machine-local `workspace/`/prefs, not portable `project.arbc`**
  (`:453-455`); and **floating / tear-off windows deferred past v1** (`:456`). The
  "Not yet designed" open item (`:487-489`) records that *"the dockspace paradigm
  is set (§10 / D18); the actual 'Paint / Compose / Review' default arrangements
  still need drawing"* — that drawing is `workspaces`' scope, not this leaf's.
- `docs/00-design.md` **D19** (`:480`, log row; prose `§10:436-445`) — the panel /
  process binding this leaf respects but does not implement: shared views and
  selection belong to the **project**, not any canvas (N canvases share one
  project-level selection); one process = one project. The dockspace hosts views;
  it owns no project/selection state.
- `docs/01-architecture.md` **A2** (`:252`, log row) — *"Self-rendered UI on Dear
  ImGui (docking) + SDL3 + OpenGL (GLES3/WebGL2 subset)."* The commitment to
  ImGui's docking branch is exactly what lets this leaf delegate the interactive
  split-tree to ImGui rather than hand-rolling one (see D-dockspace-1).
- `docs/01-architecture.md` **§7 / §8 / A8** (`:117-179`, log row `:258`) — the
  component skeleton and levelization DAG. §7 (`:126,130`) names `dockmodel` as L1
  *"view registry + layout data"* and `dock` as L3 *"dockspace shell + tool rail
  (ImGui docking)."* The §8 level stack (`:152-160`) puts `dockmodel` in the
  **UI-agnostic L1 core** ("no ImGui/GL/SDL, unit-tested headless") and `dock` at
  L3 ("ImGui draw code — the ONLY layer that sees ImGui"). The edge table
  (`:162-175`) fixes the legal edges: `dockmodel`(L1) → `base, platform` (ImGui/GL
  = **no**); `dock`(L3) → `dockmodel, views, imgui`. Machine-enforced by
  `scripts/check_levels.py:21-48` (`ALLOWED["dockmodel"] = {"base","platform"}`
  `:29`, `ALLOWED["dock"] = {"dockmodel","views"}` `:32`, `imgui`→`{views,dock,app}`
  `:45`).
- `docs/01-architecture.md` **§9 / A9** (`:181-208`, log row `:259`) — the layered
  DoD. The *L1 logic* row (`:187`) names *"dock model"* explicitly as Catch2
  headless coverage; the *End-to-end UI* row (`:189`, *"open→dock→select→… Dear
  ImGui Test Engine, headless, drives widgets by ID + screenshot capture"*) is the
  layer the dock e2e instantiates. §9.1 (`:210-245`) is the offscreen software-GL
  lane the e2e runs under.

**Source seams this leaf extends.**

- `src/dockmodel/dockmodel.cpp:1-8` / `src/dockmodel/ace/dockmodel/dockmodel.hpp` —
  the L1 stub (only `name()`) to grow the `DockLayout` value type + operations into.
  It may include only `<ace/base/...>` / `<ace/platform/...>` + std — never ImGui.
- `src/dock/dock.cpp:1-8` / `src/dock/ace/dock/dock.hpp` — the L3 stub (only
  `name()`, already including `<ace/views/views.hpp>`) to grow the `DockBuilder`
  translation + dockspace-host draw into.
- `src/views/views.cpp:16-25` — `probe_pane_title()` / `draw_probe_pane(...)`, the
  reusable pane the dockspace hosts as its canvas-is-a-view stand-in.
- `src/app/ace/app/shell.hpp:45,51,60,64` / `src/app/shell.cpp:97-104,151` — the
  `draw_ui()` → `draw_content_` seam (`set_draw_content`) the dockspace install
  hooks into (as `ProbeView` does today), and `imgui_context()` for the e2e.
- `src/app/shell.cpp:66-67` — the `ImGuiConfigFlags_DockingEnable` flag (already
  set) and `io.IniFilename = nullptr` (the reason layout is programmatic).
- `CMakeLists.txt:170-178` (`ace_tests`, headless Catch2 — the `dockmodel` unit
  joins here) and `:185-197` (`ace_shell_test`, offscreen-GL e2e — the dock e2e
  joins here).

## Constraints / requirements

1. **No lint edit; no new component; no new DAG edge.** Every include this leaf
   adds is already whitelisted: `dock` may include `<imgui.h>` +
   `<ace/dockmodel/...>` + `<ace/views/...>` (`scripts/check_levels.py:32,45`);
   `dockmodel` includes only `<ace/base/...>`/`<ace/platform/...>` + std.
   `scripts/check_levels.py` and the `CMakeLists.txt` component graph must stay
   **unedited**. If the implementation ever needs a forbidden edge, that is a
   levelization change requiring an explicit `A<n>` delta — **not expected here**.

2. **`dockmodel` is ImGui/GL/SDL/libarbc-free (A8).** The `DockLayout` value type
   and its operations are pure data over `base`/`platform`/std — no `<imgui.h>`,
   no ImGui `ImGuiID`/`ImVec2`, no `<arbc/...>`. It references views by **stable
   string id**, not by any ImGui or document handle, so it stays headless-testable
   and is the seam `workspaces` will serialize.

3. **Layout is built programmatically each run — `io.IniFilename` stays `nullptr`.**
   Because the shell disables the imgui.ini for reproducible headless runs
   (`src/app/shell.cpp:67`), `dock` must build the initial dock tree from the
   `DockLayout` via `DockBuilder` (guarded so it runs once — e.g. on first frame /
   when the root node is absent), then let ImGui own the live tree. This leaf must
   **not** re-enable `io.IniFilename`; persistence of layout is `workspaces`'
   scope, and it targets machine-local `workspace/`/prefs (D18/§9), not imgui.ini.

4. **Lean on ImGui's native docking for interaction (D-dockspace-1).** Drag-to-dock
   drop zones, splitter resize, and tab-grouping are ImGui's docking-branch
   behavior; `dock` wires up the host (`DockSpaceOverViewport` over the main
   viewport) and draws the panes, but does **not** reimplement hit-testing, drop
   zones, or a custom splitter. `dockmodel` is the declarative seed + serialization
   projection, not a live docking engine.

5. **Fully uniform — no privileged central editor area (D18).** The initial
   `DockLayout` tiles the whole dockspace with the placeholder nodes; the host must
   not reserve a passthru central node dedicated to canvases (that would recreate
   the VS-Code-style privileged center D18 explicitly rejects). Every node is a
   peer; a container may legitimately hold zero canvases.

6. **Placeholder panes only — no real view types.** The dockspace is populated with
   a small fixed set of stably-id'd placeholder panes (the render_probe pane as a
   canvas stand-in + ≥2 plain panels) to exercise split/tab/drag/resize and anchor
   the e2e. The real view catalog and open/close/reopen live in `view_registry`;
   this leaf must not anticipate them. The tool rail / view launcher chrome
   (`tool_rail`) and the top/status bars are likewise out of scope.

7. **One sensible default layout only.** This leaf ships a single bootstrap
   `DockLayout` (enough to run the app and the e2e). The named "Paint / Compose /
   Review" presets and switching between them are `workspaces`' scope (the D18
   open item, `docs/00-design.md:487-489`); this leaf must not build a preset
   registry.

8. **Component homes follow §8.** The `DockLayout` model + invariants live in
   `dockmodel` (L1, headless); the ImGui host draw + `DockBuilder` translation live
   in `dock` (L3); the one-shot install of the dockspace as the shell's draw-content
   lives in `app` (L4). The shell issues no `DockBuilder` calls; `dockmodel`
   touches no ImGui.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the umbrella.
This leaf is the **first** to add real `dockmodel`/`dock` code, so each layer is
named concretely.

- **Levelization (`check_levels` clean).** `python3 scripts/check_levels.py` passes
  **with no edit**: `dockmodel` includes only `<ace/base/...>`/`<ace/platform/...>`
  + std (no ImGui/GL/SDL/arbc); `dock` includes `<imgui.h>` +
  `<ace/dockmodel/...>` + `<ace/views/...>`. No new component and no new DAG edge —
  the primary structural assertion.
- **L1 logic — Catch2 unit (`dockmodel`).** A new `tests/dockmodel_test.cpp`
  (headless, joined to `ace_tests`, `CMakeLists.txt:170-178`) builds `DockLayout`
  trees and asserts: **invariants** — split ratios in `(0,1)`, every leaf tab-group
  is non-empty, view ids are unique across the tree, and a full traversal visits
  every node; **operations** — split a leaf H/V (parent replaces leaf, ratio set),
  insert a view as a tab into a leaf (order + active-tab tracked), remove a view
  (a leaf emptied by removal collapses, its sibling promoted), and the
  "close-everything → rebuild the default" round-trip yields the seed layout. This
  is the *"dock model"* row §9 (`:187`) names.
- **Rendered output — N/A (Test Engine screenshot baseline, not a `render_offline`
  golden).** The dockspace composes ImGui chrome, not a `Document`, so there is no
  byte-exact CPU golden here (that path stays on `editor.canvas.*` / export leaves,
  matching app_shell's convention). Where it adds signal, the dock e2e may capture
  a **screenshot baseline** of the default docked layout via the existing
  `capture_pixels` rig (`tests/shell_e2e_test.cpp:36-40`) — a baseline, not a
  byte-exact assertion (software-GL pixels are flaky by construction).
- **UI e2e — ImGui Test Engine (dockspace mechanics).** A new case in
  `ace_shell_test` (`CMakeLists.txt:185-197`, offscreen-GL env — a case added to
  `tests/shell_e2e_test.cpp` or a sibling `tests/dockspace_e2e_test.cpp` linked the
  same way) boots the shell with the dockspace draw-content installed and, driving
  **by stable widget id**: asserts the dockspace host and each placeholder pane
  exist and are addressable (`ctx->WindowInfo(...)`, `info.ID != 0`); drives a
  **tab-select** on a tab-group (`ctx->ItemClick`) and asserts the activated view
  is focused; drives a **drag-to-dock** moving one pane into another container
  (`ctx->DockInto` / `ctx->WindowMove`) and asserts the moved pane now shares (or
  splits) the target node — exercising the D18 drag/split/tab mechanics. The frame
  pump + teardown reuse the existing rig (`tests/shell_e2e_test.cpp:92-98,132-138`).
- **Threading (ASan/TSan) — N/A as a new target; stays clean.** This leaf adds no
  editor thread and no `WorkerPool`; the dock e2e simply stays clean under the
  existing `asan`/`tsan` offscreen lanes (§9.1). Real concurrency TSan coverage is
  scoped to `editor.canvas.frame_sync` / `editor.canvas.multi_canvas`.
- **Coverage.** ≥90% diff coverage on changed lines (`diff-cover --fail-under=90`
  under the `coverage` preset), including the `DockLayout` operation/collapse
  branches and the `DockBuilder`-once guard.
- **Format/build.** `clang-format --dry-run --Werror` clean; `dev` and `release`
  presets build; `scripts/gate` green.

**No follow-up WBS task is deferred.** The dockspace's successors already exist as
WBS leaves — `editor.dock.view_registry` (real view catalog + open/close/reopen,
extending `dockmodel`), `editor.dock.tool_rail` (fixed rail + view launcher), and
`editor.dock.workspaces` (saved presets + `DockLayout` persistence to
`workspace/`/prefs) — so nothing new is registered. The one design gap D18 flags
(the actual "Paint / Compose / Review" default arrangements, `docs/00-design.md:487-489`)
is `workspaces`' scope, not a new task. Floating / tear-off windows remain deferred
past v1 by D18 (`:456`) and are not registered.

## Decisions

- **D-dockspace-1 — Delegate the interactive split-tree to ImGui's native docking
  branch; `dockmodel` keeps only a declarative layout projection, not a docking
  engine.** ImGui's docking branch already owns the recursive dock-node tree,
  drag-to-dock drop zones (center = tab, edge = split), splitter resize, and tab
  groups — exactly D18's mechanics (`docs/00-design.md:424-429`). `dock` wires the
  host + draws panes and lets ImGui own the *live* tree; `dockmodel`'s `DockLayout`
  is the *declarative* seed (for `DockBuilder`) and the serialization projection
  (for `workspaces`). *Rationale:* A2 already commits the UI to ImGui's docking
  branch, and reusing it is the "reuse existing seams" bias; the branch is
  enabled-but-unused today (`src/app/shell.cpp:66`). *Alternative rejected:* build
  a custom split-tree widget with our own hit-testing / drop zones / splitters —
  a large, ImGui-duplicating effort, harder to test, and a deviation from A2 for
  no benefit.

- **D-dockspace-2 — Split the work across `dockmodel` (L1, `DockLayout` value +
  invariants, Catch2) and `dock` (L3, ImGui host + `DockBuilder`, Test Engine
  e2e).** *Rationale:* this is exactly the §7/§8 decomposition ("view registry +
  layout data" at L1, `:126`; "dockspace shell" at L3, `:130`) and it lands each
  DoD layer in its natural test home — the layout tree is pure data with real
  invariants → headless Catch2 (the §9 "dock model" row, `:187`), the ImGui
  rendering → Test Engine e2e (`:189`). It keeps `dockmodel` ImGui-free (A8) and
  gives `view_registry`/`workspaces` an L1 model to extend rather than reinvent.
  *Alternative rejected:* put all dockspace code in `dock` (L3) — legal
  levelization-wise but leaves the layout logic untestable headless (violating A9's
  "L1 logic → Catch2"), and leaves the successor leaves with no L1 seam.

- **D-dockspace-3 — Build the initial layout programmatically via `DockBuilder`
  each run; do not re-enable `io.IniFilename`.** The shell sets
  `io.IniFilename = nullptr` for reproducible headless runs
  (`src/app/shell.cpp:67`), so there is no persisted layout to restore; `dock`
  seeds the tree from the `DockLayout` once (guarded on the root node's absence),
  then ImGui owns it. *Rationale:* a deterministic in-code layout is essential for
  a stable e2e, and it is precisely the seam `workspaces` later swaps a saved
  `DockLayout` into. *Alternative rejected:* re-enable `imgui.ini` persistence —
  reintroduces run-to-run nondeterminism into headless CI (defeating app_shell's
  reproducibility decision), and layout persistence belongs to `workspaces` as
  machine-local `workspace/`/prefs state (D18/§9), not an ImGui-owned ini.

- **D-dockspace-4 — Placeholder panes stand in for real views until
  `view_registry`.** The dockspace hosts a small fixed set of stably-id'd
  placeholder panes — the render_probe pane as a canvas stand-in
  (`ace::views::probe_pane_title()`, instantiating D18's "canvas is a view")
  plus ≥2 plain panels — to exercise split/tab/drag/resize and anchor the e2e.
  *Rationale:* the real view catalog + open/close/reopen is `editor.dock.view_registry`
  (`tasks/00-editor.tji:68-72`), which depends on this leaf; building it here would
  collapse two tasks and blow the 3d estimate. Reusing the probe pane proves the
  multi-container mechanism with zero new view code. *Alternative rejected:* build
  the real Canvas/Layers/Inspector view types now — over-scopes the leaf and
  duplicates `view_registry`.

- **D-dockspace-5 — Host a full-window uniform dockspace over the main viewport,
  with no reserved central node.** `dock` calls `DockSpaceOverViewport` on the main
  viewport and the seed `DockLayout` tiles it entirely with the placeholder nodes,
  so there is no empty privileged central region. *Rationale:* D18 is explicit —
  "no special editor area," "no keep-a-canvas guardrail" (`docs/00-design.md:414-423`);
  a reserved passthru central node dedicated to canvases is exactly the VS-Code
  privileged center D18 rejects. *Alternative rejected:* a dockspace with a
  passthru central node reserved for the canvas — recreates the privileged editor
  area and the keep-a-canvas guardrail D18 forbids.

## Open questions

- _None._ D18 (`docs/00-design.md:479`, prose §10 `:414-456`) fixes the uniform
  dockspace paradigm this leaf implements; A2 commits the UI to ImGui's docking
  branch (so the interactive tree is ImGui's, not ours); §7/§8 already declare
  `dockmodel`(L1) and `dock`(L3) with every edge this task needs; §9 fixes the test
  model (dock model → Catch2, dockspace UI → Test Engine e2e); and the successor
  scope (registry, tool rail, workspaces) is already carved into separate WBS
  leaves. The one D18-flagged gap — the default "Paint / Compose / Review"
  arrangements (`:487-489`) — is `workspaces`' design scope, not a dockspace
  decision. Every choice above is settled against the constitution with **no doc
  delta required** — no new dependency, no new component, no new DAG edge, no
  deviation from a decided behavior.

## Status

**Done** — 2026-07-17.

- `src/dockmodel/ace/dockmodel/dockmodel.hpp`, `src/dockmodel/dockmodel.cpp` — L1 `DockLayout`/`DockNode` value type and pure operations (split, insert, remove, collapse, activate), with invariants (ratio ∈ (0,1), non-empty leaf tab-groups, unique view ids).
- `src/dock/ace/dock/dock.hpp`, `src/dock/dock.cpp` — L3 `Dockspace` host: `DockSpaceOverViewport` over the main viewport, once-guarded `DockBuilder` translation of the `DockLayout` seed, placeholder panes (render_probe + plain panels), `default_layout()`.
- `src/app/shell.cpp` — `run_editor` installs the `Dockspace` as draw-content (probe drawn after, as canvas stand-in per D-dockspace-4).
- `tests/dockmodel_test.cpp` (new) — Catch2 unit suite: invariants, split/insert/remove/collapse/activate operations, close-everything→rebuild round-trip; wired into `ace_tests` via `CMakeLists.txt`.
- `tests/dockspace_e2e_test.cpp` (new) — ImGui Test Engine e2e: pane presence by id, host-node addressability, tab-select via tab id, drag-to-dock via `DockInto`; wired into `ace_shell_test` via `CMakeLists.txt`.
- `CMakeLists.txt` — wires both new test files into their respective test targets.
- Fully-uniform D18 dockspace landed: declarative split-tree seeds ImGui native docking via `DockBuilder` (`io.IniFilename` stays `nullptr`), full-viewport host with no reserved central node, no lint/DAG/CMake-graph edits required.
</content>
</invoke>
