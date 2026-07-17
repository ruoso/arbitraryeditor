# editor.dock.tool_rail — Fixed tool rail + view launcher (home base)

## TaskJuggler entry

`tasks/00-editor.tji:76-81` — `task tool_rail` under `editor.dock`. Effort
`1.5d`, `allocate team`, `depends !view_registry` (i.e.
`editor.dock.view_registry`, and transitively `editor.dock.dockspace` →
`editor.foundation.app_shell`). The `.tji` `note` cites Design **D18**.

The `note`'s back-link points at the flat path
`tasks/refinements/tool_rail.md`; the real path is
`tasks/refinements/editor/tool_rail.md` — the closer fixes the back-link on
completion per the ritual in `tasks/refinements/README.md:57-68`.

**Downstream dependents.** Only `editor.packaging.package`
(`tasks/00-editor.tji:294-298`, the feature-complete gather) lists
`editor.dock.tool_rail` in its `depends`. The sibling `editor.dock.workspaces`
depends `!dockspace` (`tasks/00-editor.tji:82-87`), **not** on tool_rail — so
this leaf is a self-contained shell feature, not a predecessor of workspaces.

## Effort estimate

**1.5 days** (from the `.tji`). The estimate is dominated by the ImGui rail
chrome (a fixed left column that coexists with `DockSpaceOverViewport`), the
launcher wiring onto the already-public `Dockspace` surface, and the e2e that
drives it. Explicitly **out of scope**: any canvas interaction behavior (the
active tool changes *state only*, not what a canvas drag does — no consumer
exists yet), layout/preset persistence (`editor.dock.workspaces` owns that),
import functionality (`editor.import.*`), and icon art (cosmetic, rides asset
bundling). No threading, no `Document` render, no libarbc call.

## Inherited dependencies

**Settled (from `editor.dock.view_registry`,
`tasks/refinements/editor/view_registry.md`).** The view catalog and registry
are done and public in `dockmodel` (L1, ImGui-free):
`src/dockmodel/ace/dockmodel/view_registry.hpp` gives
`enum class ViewType { Canvas, Layers, Inspector, Overview, Color, History,
Assets, Export }` (`k_view_type_count == 8`), `view_catalog()` →
`std::array<ViewDescriptor, 8>` (each `{type, slug, title, multi_instance}`,
`multi_instance` true only for Canvas), `view_title(type)`, and the
`ViewRegistry` (`mint_id`/`open`/`close`/`reopen`) with **singleton-idempotent**
and **multi-instance-mints-distinct** semantics. The launcher **calls this
surface, it does not re-implement it**. The open-set of views **is**
`DockLayout::view_ids()` (`src/dockmodel/ace/dockmodel/dockmodel.hpp`) — there
is no second registry of "what's open."

**Settled (from `editor.dock.dockspace`,
`tasks/refinements/editor/dockspace.md`).** The L3 host is public in `dock`:
`src/dock/ace/dock/dock.hpp` gives `Dockspace` with
`open(ViewType)`/`reopen(ViewType)`/`close(std::string_view)` (mint through the
owned `ViewRegistry` and set the rebuild flag so the new window docks on the
next `draw()`), `const DockLayout& layout()`, `unsigned int dockspace_id()`,
and the free `default_initial_views()` (`{Canvas, Inspector, Layers,
Overview}`). `Dockspace::draw()` (`src/dock/dock.cpp:64`) is the draw-dispatch
seam: it submits `DockSpaceOverViewport`, re-seeds the live tree from `layout_`
on rebuild, then wraps each `layout_.view_ids()` id in `ImGui::Begin(id, &open)`
+ `views::draw_view(id)`. `DockBuilderDockWindow` uses the instance id **as the
ImGui window id** (`src/dock/dock.cpp:23`) — so views are addressable by their
stable id (`"inspector"`, `"canvas#1"`).

**Settled (from `editor.foundation.app_shell`,
`tasks/refinements/editor/app_shell.md`).** The top-level composition point is
`ace::app::run_editor()` (`src/app/shell.cpp:145`): it constructs the
`Dockspace` and installs the per-frame draw via `shell.set_draw_content([&]{
dockspace.draw(); })`; `Shell::draw_ui()` (`src/app/shell.cpp:101`) invokes the
installed lambda. The ImGui Test Engine e2e harness (boot headless `Shell`,
`IM_REGISTER_TEST`, `UserData`, frame-pump-until-queue-drains) is stood up and
proven in `tests/shell_e2e_test.cpp` / `tests/view_registry_e2e_test.cpp` /
`tests/dockspace_e2e_test.cpp`.

**Pending — none.** No predecessor is left unbuilt for this leaf. In
particular `interact` is **not** touched: active-tool state goes to `dockmodel`
(see A11 / Decision D-tool_rail-1), so `interact` stays the stateless math it is
today (`src/interact/ace/interact/interact.hpp` — only `name()`/`brush_units()`).

**No lint edit and no new DAG edge is required** (see Constraint 1).

## What this task is

The thin fixed **left tool rail** — the shell's *home base*. It has two halves,
both drawn in `dock` (L3, the design's rail home, `docs/01-architecture.md:133`):

1. **The view launcher.** One entry per view type from
   `dockmodel::view_catalog()`; clicking it opens (or, for a singleton already
   present, focuses) that view via `Dockspace::open`/`reopen`. Because the
   dockspace is uniform and *anything* can be closed — including every canvas —
   the launcher is what guarantees **nothing can be lost**: with the layout
   empty, one click restores a view (`docs/00-design.md` §10, `:446-450`).

2. **The modal tools.** The persistent pointer modes — **Select · Brush ·
   Eyedropper · Pan** (new **D20**) — rendered as selectable buttons over a
   single active-tool selection. At this leaf the selection is *observable state
   only*; nothing on the canvas reads it yet (no canvas interaction exists).

The rail is **chrome, not a view**: it is always present, cannot be closed or
docked, and is not registered in `view_catalog()`.

## Why it needs to be done

D18 makes the dockspace fully uniform with **no keep-a-canvas guardrail** — the
user can close every view, including all canvases, leaving an empty shell. The
tool rail is the *only* thing that makes that safe: it is the fixed surface that
"brings it back" (`docs/00-design.md:446-450`). Without it, the "anything can be
closed" contract that `view_registry` and `dockspace` deliver would be a trap.
Downstream, `editor.packaging.package` gathers the feature-complete editor and
depends on this leaf; the panel-content tasks (Inspector/Color/Overview) rely on
the launcher to reopen their views after a close.

## Inputs / context

**Design constitution (normative).**
- `docs/00-design.md` **D18** (`:479`) — the fully-uniform dockspace; "The fixed
  **tool rail** hosts modal tools **and the view launcher** (reopen anything)."
- `docs/00-design.md` **§10** home-base bullet (`:446-450`) — "the rail is also
  the **view launcher** … close it all, the rail brings it back."
- `docs/00-design.md` **D20** (`:480`, *this task's delta*) — the modal-tool set
  (Select/Brush/Eyedropper/Pan) and its reconciliation with D7/D9/D12/D14.
- `docs/00-design.md` **D7** (`:468`) — one select tool for cells+cameras;
  **D5** (`:466`) brush; **D9** (`:470`) Space-transient pan; **D10** (`:471`)
  eyedropper; **D12** (`:473`) import is drop/paste-driven (not a mode); **D14**
  (`:475`) crop = frame a camera (not a mode).

**Architecture constitution.**
- `docs/01-architecture.md` **§7** directory map (`:122-137`) — `dockmodel`
  (L1, `:130`), `dock` = "dockspace shell + tool rail" (`:133`), `app` (L4).
- `docs/01-architecture.md` **§8 / A8** (`:144-179`, `:258`) — the levelization
  DAG (`dock → {dockmodel, views, imgui}`, `:174`; `dockmodel → {base,
  platform}`, no ImGui/GL/SDL, `:171`).
- `docs/01-architecture.md` **§9 / A9** (`:181-208`, `:259`) — the layered DoD.
- `docs/01-architecture.md` **A11** (*this task's delta*) — `dockmodel` owns the
  active-tool selection so `dock` owns the whole rail without an `interact` edge.

**Source seams this leaf extends (extend, do not fork).**
- `src/dockmodel/ace/dockmodel/view_registry.hpp` — `view_catalog()`,
  `ViewType`, `view_title()`, `ViewDescriptor`. The launcher's menu source; the
  new `ToolId` / `tool_catalog()` / `ToolSelection` land here beside it.
- `src/dockmodel/ace/dockmodel/dockmodel.hpp` — `DockLayout::view_ids()`, the
  single source of truth for "what is open."
- `src/dock/ace/dock/dock.hpp` + `src/dock/dock.cpp` — `Dockspace` (the launcher
  drives `open`/`reopen`/`close`, reads `layout()`); `draw()` at `:64` is where
  the rail column is drawn before `DockSpaceOverViewport`.
- `src/app/shell.cpp:145` (`run_editor`) — the alternative install point if the
  rail is drawn in the content lambda rather than inside `Dockspace::draw()`.

**Test rigs.**
- `tests/shell_e2e_test.cpp` — the engine-boot + frame-pump pattern and the
  screenshot-capture rig; `tests/view_registry_e2e_test.cpp` /
  `tests/dockspace_e2e_test.cpp` — driving windows by stable id, asserting the
  live dock tree.
- `CMakeLists.txt` — `ace_tests` (Catch2 L1) and `ace_shell_test` (Test Engine
  e2e) targets; `tests/lsan.supp` (Mesa/EGL leak suppressions).
- `scripts/check_levels.py` — the `ALLOWED` / `EXTERNAL_ALLOWED` maps enforcing
  the DAG and the ImGui/SDL/GL/libarbc seam.

## Constraints / requirements

1. **No lint edit; no new component; no new DAG edge.** The active-tool state
   (`ToolId`, `tool_catalog()`, `ToolSelection`) lands in `dockmodel` (L1, deps
   `{base, platform}`); the rail chrome + launcher lands in `dock` (L3, deps
   `{dockmodel, views, imgui}`). Both fit the existing `scripts/check_levels.py`
   `ALLOWED` map unchanged.
2. **`dockmodel` stays ImGui/GL/SDL/libarbc-free (A8).** `ToolId` is a plain
   enum; `tool_catalog()` returns `string_view` descriptors; `ToolSelection`
   holds one `ToolId`. No `#include <imgui.h>` (or GL/SDL/arbc) may appear in
   `dockmodel`.
3. **The rail is chrome, not a view (D18 / §10).** Always present; cannot be
   closed or docked; not in `view_catalog()`. A fixed-width left column;
   `DockSpaceOverViewport` fills the remaining work area.
4. **The launcher drives the existing surface.** Enumerate `view_catalog()`,
   label with `view_title(type)`, open via `Dockspace::open`/`reopen`; reflect
   currently-open state from `Dockspace::layout().view_ids()` (e.g. highlight /
   toggle open singletons). **The home-base guarantee must hold**: with the
   layout empty, a launcher click restores the view.
5. **Do not re-implement registry semantics.** Singleton idempotence and
   multi-instance (`canvas#N`) minting are `ViewRegistry`'s job (settled in
   `view_registry`); the rail only calls `Dockspace::open`/`reopen`.
6. **Modal tools per D20.** Exactly Select/Brush/Eyedropper/Pan; **no import
   mode** (D12), **camera folded into Select** (D7). The active tool is
   `dockmodel::ToolSelection` (A11). **No canvas behavior is wired** — the
   selection is observable state with no reader at this leaf.
7. **Stable ImGui ids for every rail button.** Each launcher entry and tool
   button gets a deterministic id (label or `PushID`), and the rail window a
   stable id, so the Test Engine can `ItemClick` them by ref.
8. **Component homes follow §8.** state → `dockmodel` (L1); rail draw → `dock`
   (L3); the install seam is `Dockspace::draw()` (preferred) or the
   `run_editor` content lambda (L4). No UI logic leaks below L3.
9. **Rail/layout state is local UI state (D18), not `project.arbc`.** This leaf
   adds no persistence; preset persistence is `editor.dock.workspaces`.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate`
green (check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization.** `check_levels` clean: no new component, no new DAG edge;
  `dockmodel` gains `ToolId`/`tool_catalog()`/`ToolSelection` and still carries
  **no** ImGui/GL/SDL/libarbc include; `scripts/check_levels.py` `ALLOWED` map is
  unchanged (A8).
- **L1 logic — Catch2** (`tests/tool_rail_test.cpp`, headless, joined to
  `ace_tests`), mapping to the §9 "L1 logic" row (`docs/01-architecture.md:187`):
  - `tool_catalog()` — ordered `{Select, Brush, Eyedropper, Pan}`, correct
    slugs/titles, stable count.
  - `ToolSelection` — default is `Select`; `select(id)` then `active()` round-
    trips; selecting each catalog entry is observable.
  - launcher model helper — for a given `DockLayout`, produces one entry per
    `view_catalog()` type in catalog order, with `is_open` reflecting
    `layout.view_ids()` (empty layout → all closed; after an `open` → that
    type's entry flips to open).
- **Rendered output — N/A (justified).** The rail composes ImGui chrome, not a
  libarbc `Document`, so there is no byte-exact `render_offline` golden
  (`render_offline` goldens begin at `render_probe`). The "rendered output" DoD
  layer is instead instantiated by a **Test Engine screenshot baseline** of the
  rail via the capture rig (`tests/shell_e2e_test.cpp`) — a baseline, not a
  byte-exact assertion (software-GL pixels are flaky by construction).
- **UI behavior — ImGui Test Engine e2e** (`tests/tool_rail_e2e_test.cpp`, joined
  to `ace_shell_test`, offscreen software-GL), mapping to the §9 "end-to-end UI"
  row (`docs/01-architecture.md:189`), driving rail buttons by their stable ids:
  - **home base** — close every open view (tab ✕), assert `layout().view_ids()`
    empty; click a launcher entry; assert the view window reappears
    (`ctx->WindowInfo(id).ID != 0`) and the layout is non-empty.
  - **singleton idempotence** — click the Inspector launcher twice; assert a
    single `"inspector"` instance (focus, no duplicate).
  - **multi-instance** — click the Canvas launcher twice; assert `"canvas#1"`
    and `"canvas#2"` both exist.
  - **tool select** — click the Brush button; assert the active `ToolId` is
    `Brush` (via `UserData` → the `Dockspace`/`ToolSelection`); click Select;
    assert `Select`.
- **Threading — no new threading.** The rail/tool state is single-threaded
  UI-side state mutated only on the UI thread inside the draw loop; runs
  **ASan/LSan-clean** under the existing `asan`/`tsan` offscreen lanes
  (`tests/lsan.supp` covers the Mesa/EGL third-party leaks). Real concurrency
  TSan coverage is scoped to `editor.canvas.frame_sync` / `editor.canvas`.
- **Coverage.** ≥90% diff coverage on changed lines (`diff-cover
  --fail-under=90` under the `coverage` preset), shipping with the task.
- **Format/build.** `clang-format --dry-run --Werror` clean; `dev` and `release`
  presets build; `scripts/gate` green.
- **Deferred follow-up.** Wiring the active tool to canvas interaction behavior
  is deferred to `editor.canvas.tool_dispatch` (closer registers in WBS — see
  Decisions).

## Decisions

- **D-tool_rail-1 — The whole rail lives in `dock`; active-tool state lives in
  `dockmodel`.** `dock` is the design's documented rail home
  (`docs/01-architecture.md:133`) and already sees `dockmodel` (for
  `view_catalog()`) and drives its own `Dockspace`. Putting the active-tool
  state in `dockmodel` (L1) — not `interact` — lets `dock` own *both* halves of
  the rail within the existing `dock → {dockmodel, views}` edges, with **zero new
  DAG edges** (doc delta **A11**). *Alternative rejected:* tool state in
  `interact` (semantically the "interaction" home) — but no L3 component sees
  *both* `interact` and `dock`/`dockmodel`, so it would force the rail up into
  `app` (L4, the only layer that sees everything), moving UI chrome out of its
  documented `dock` home and inflating the bootstrap layer. The state is trivial
  value data, so `dockmodel` (already the headless UI-state model) is the natural
  fit, not a stretch.
- **D-tool_rail-2 — Modal tool set = Select/Brush/Eyedropper/Pan.** Reconciles
  the `.tji` note's casual six-item list (select/brush/eyedropper/camera/import/
  pan) with the design constitution (doc delta **D20**): "camera" is subsumed by
  the *one* select tool (D7), and "import" is drop/paste-gesture-driven, "not a
  mode" (D12) — it surfaces later as an action wired by `editor.import.*`, never
  a rail mode. Pan stays on the rail for discoverability even though Space is its
  transient shortcut (D9). *Alternative rejected:* rendering the literal six-item
  list — it duplicates Select (camera) and contradicts D12/D14.
- **D-tool_rail-3 — The rail is chrome, not a registered view.** It is not in
  `view_catalog()`, cannot be closed or docked. Per D18/§10 the rail is the home
  base that "can never leave you empty"; making it a closable/dockable view would
  reintroduce the exact empty-shell failure mode it exists to prevent.
  *Alternative rejected:* a pinned "ToolRail" view — self-defeating.
- **D-tool_rail-4 — No tool→canvas behavior at this leaf.** The rail sets and
  reflects the active tool as observable state; nothing reads it because canvas
  interaction is unbuilt. Per the "simpler abstraction with one or two call sites
  today" bias, a speculative tool-dispatch state machine with no consumer is not
  built now — but the `dockmodel::ToolSelection` seam is stable for downstream to
  read. *Alternative rejected:* building the interaction dispatch now — it would
  be dead, untestable-through-behavior code. Deferred to the named future task
  `editor.canvas.tool_dispatch` (closer registers in WBS).
- **D-tool_rail-5 — The launcher reflects open state from
  `Dockspace::layout().view_ids()`; it keeps no open-set of its own.**
  `view_registry` established that the set of open views *is*
  `DockLayout::view_ids()`; a rail-side mirror would drift. *Alternative
  rejected:* a launcher-owned open-set cache.

### Named future task (closer registers in WBS)

- **`editor.canvas.tool_dispatch`** — "Route the `dockmodel::ToolSelection`
  active tool into the canvas interaction handler so a plain canvas pointer
  gesture dispatches per active tool (Select/Brush/Eyedropper/Pan); promote the
  tool→behavior seam into `interact` (hit-test/gizmo/brush math)." Effort ~2d,
  `allocate team`. `depends editor.dock.tool_rail` + the canvas pointer-
  interaction leaf (`editor.cells.gizmo` and/or `editor.canvas.nav` — closer
  confirms the exact edge). Milestone: `editor` (via the `editor.canvas`
  subtree). `note` cites this refinement and D20/A11. This is concrete,
  agent-implementable work — a routing seam plus behavior tests — not an audit.

## Open questions

(none — all decided.)

The rail's button presentation (text/id labels now vs. icon art) is settled
minimally: buttons carry stable text/id labels this leaf; icon art is cosmetic
and rides asset bundling (`editor.packaging`), gating nothing. The History and
Assets view *bodies* remain parked (`tasks/parking-lot.md`) from `view_registry`
— the launcher still opens their registered placeholders, so no gap here.

## Status

**Done** — 2026-07-17.

- `src/dockmodel/ace/dockmodel/tool_rail.hpp`, `src/dockmodel/tool_rail.cpp` — L1 state: `ToolId` enum (Select/Brush/Eyedropper/Pan), `tool_catalog()` returning ordered descriptors, `ToolSelection` holding the active tool; ImGui-free per A11.
- `src/dock/ace/dock/dock.hpp`, `src/dock/dock.cpp` — `Dockspace::tools()` accessor, `tool_rail_title()`/`draw_tool_rail()` seam added; `Dockspace::draw()` now renders a fixed left rail window and a manual dockspace host filling the remainder (replaced `BeginViewportSideBar`+`DockSpaceOverViewport` to eliminate one-frame work-inset lag that jittered docked geometry).
- `tests/tool_rail_test.cpp` — Catch2 unit tests joined to `ace_tests`: catalog order/slugs/titles, `ToolSelection` default/round-trip, launcher model `is_open` reflection against `DockLayout`.
- `tests/tool_rail_e2e_test.cpp` — ImGui Test Engine e2e joined to `ace_shell_test`: home-base reopen from empty layout, singleton idempotence, multi-instance canvas minting, tool-select Brush→Select round-trip, screenshot baseline.
- `CMakeLists.txt` — both new test files joined to `ace_tests` / `ace_shell_test` targets.
- `docs/00-design.md`, `docs/01-architecture.md` — D20 (modal-tool set) and A11 (ToolSelection in dockmodel) recorded as settled.
