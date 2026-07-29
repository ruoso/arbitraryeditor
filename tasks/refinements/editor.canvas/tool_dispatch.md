# editor.canvas.tool_dispatch — Route the active modal tool into canvas pointer-gesture dispatch

## TaskJuggler entry

- **Task:** `editor.canvas.tool_dispatch` (`tasks/00-editor.tji:245-250`).
- **Effort:** `2d` · `allocate team`.
- **Depends:** `editor.dock.tool_rail`, `editor.cells.gizmo`, `editor.canvas.nav`
  (all Done).
- **Note (verbatim):** "Route `dockmodel::ToolSelection` active tool into the
  canvas interaction handler so a canvas pointer gesture dispatches per active
  tool (Select/Brush/Eyedropper/Pan); promote the tool→behavior seam into
  `interact` (hit-test/gizmo/brush math). Source of debt:
  `tasks/refinements/editor/tool_rail.md` (D20/A11). Design:
  `docs/00-design.md D20`, `docs/01-architecture.md A11`."
- **Back-link:** this refinement lands at
  `tasks/refinements/editor.canvas/tool_dispatch.md`. **The closer** appends
  `Refinement: …` to the `.tji` note and adds `complete 100` after
  `allocate team`. **Do not** hand-edit the `.tji` here.
- **Source of debt:** `tasks/refinements/editor/tool_rail.md` (D-tool_rail-4:
  `ToolSelection` shipped as observable state with *no canvas reader*; the
  tool→behavior wiring was named and deferred to exactly this leaf).
- **Milestone:** M9E (`m9_editor` → `editor.canvas`); this leaf is already a
  transitive dependency of the M9E rollup at `tasks/00-editor.tji:680`. It
  registers **no new WBS leaf** (the two inert arms are filled by existing
  leaves — see Acceptance criteria), so no new milestone edge.

## Effort estimate

**Two days.** The behaviors this task routes *already exist and are already
unit-tested* — nav pan/zoom (`editor.canvas.nav`), the cell/group/frame gizmos
(`editor.cells.gizmo` / `group_transform` / `editor.cameras.manip`), and the
pick/marquee selection policy (`editor.cells.selection`). Today they run as
**always-on defaults**, tool-agnostic, in one per-frame block of
`app::CanvasView::draw_content`. This leaf's work is therefore **gating and
wiring**, not new interaction math:

- **~0.5d wiring** — thread the active `dockmodel::ToolId` from
  `Dockspace::tools()` into `CanvasView::draw_content` (today unreachable — see
  Inputs §4), and factor the always-on block into a per-tool `switch`.
- **~0.5d the two live arms** — gate the gizmo/selection stack behind Select
  (D-gizmo-2's "narrow the default to under the Select tool"), and route a
  left-button drag under the Pan tool into the existing `interact::pan` math
  (D-nav-4).
- **~1d tests** — the e2e cases that pin "with tool X active, a canvas drag
  does Y" (the bulk of the coverage here is behavioral, so it lands in the
  Test Engine harness), plus the L1 unit pinning the closed-set arm mapping.

It is the same weight class as its sibling routing leaves and deliberately
**adds no new component, no new DAG edge, and no libarbc surface**.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.dock.tool_rail` (Done).** Built the entire active-tool model:
  `dockmodel::ToolId { Select, Brush, Eyedropper, Pan }`
  (`src/dockmodel/ace/dockmodel/tool_rail.hpp:18`, `k_tool_count=4`, enumerator
  order = catalog order), the `ToolSelection` holder (`:45-52`, `active()` /
  `select()`, default `Select`), and the rail that authors it. The instance
  lives on `Dockspace` (`src/dock/ace/dock/dock.hpp:572`,
  `tools()` accessor `:466-467`); its only reader today is the rail draw
  (`src/dock/dock.cpp:539,543`). The header comment (`tool_rail.hpp:41-44`)
  names this leaf as the wiring point.
- **`editor.canvas.nav` (Done).** Shipped the transient-viewport camera math —
  `interact::pan` (`src/interact/ace/interact/interact.hpp:32`), `zoom` (`:38`),
  `scale_bar`, `fit` — and the always-on wheel-zoom + **Space-pan** gesture in
  `draw_content` (`src/app/canvas_view.cpp:232-238`). **D-nav-4** already states
  that the Pan *tool* "routes the left-button Pan tool into the same `interact`
  math this leaf builds," and that wheel-zoom + Space-pan stay always-on
  regardless of tool.
- **`editor.cells.gizmo` (Done).** Shipped the cell transform gizmo and the
  pick policy in `interact` (`pick.hpp`: `click_selection` `:175`,
  `marquee_selection` `:182`, `hit_cell`, `snap_placement`; `interact.hpp`
  `CellHandle`/`move`/`scale`/`rotate`/`shear_cell`). **D-gizmo-2** explicitly
  says the gizmo ships as the always-on default and "`tool_dispatch` later
  narrows the default to 'under the Select tool' by adding a branch it owns
  (`tool_rail.hpp:42-44`), touching no gizmo code." The gizmo is
  documented as **not** tool-gated today at `canvas_view.hpp:300`.

**Pending (out of scope, consumes this seam later):**

- **`editor.paint.brush`** (`tasks/00-editor.tji:621`) fills the **Brush** arm
  (dab into `org.arbc.raster`); it adds the missing brush stroke math to
  `interact` (only `brush_units`, `interact.hpp:21`, exists today).
- **`editor.color.color`** (`tasks/00-editor.tji:604`) fills the **Eyedropper**
  arm (sample sRGB through the active camera).
  Both arms ship here as **inert placeholders** (see Decisions D-tool_dispatch-3).
  *WBS-edge note for the orchestrator:* neither `editor.paint.brush` nor
  `editor.color.color` currently `depends` on `editor.canvas.tool_dispatch`;
  the closer/orchestrator should add that edge so each arm is filled after its
  seam exists. This is a dependency-edge decision the WBS owner makes, not a new
  leaf — surfaced in the return summary.

## What this task is

The tool rail already lets the user pick a modal tool, and the picked tool is
held as headless state (`ToolSelection`), but **nothing on the canvas reads it**
— every pointer gesture is dispatched tool-agnostically. This task closes that
loop: it threads the active `ToolId` into the canvas pointer handler and makes a
plain canvas drag **do what the active tool says it does** (D20). Concretely, it
wraps the existing per-frame interaction block of
`app::CanvasView::draw_content` in a `switch` over the four modal tools —
**Select** runs the current gizmo + pick/marquee stack (now gated to Select),
**Pan** routes a left-button drag into `interact::pan`, and **Brush** /
**Eyedropper** become inert seams their own leaves fill — while the always-on
transient-viewport gestures (wheel-zoom, Space-pan) keep running under *every*
tool. The "promote the tool→behavior seam into `interact`" clause is satisfied
by the math that already lives there; this leaf adds the L4 routing that selects
among it.

## Why it needs to be done

The modal tool set is the spine of the canvas interaction model (D20): Select,
Brush, Eyedropper, Pan are the persistent pointer modes that determine what a
drag means. Until the active tool reaches the pointer handler, the rail is a
control with no effect, and — more pressingly — the downstream **brush** and
**eyedropper** leaves have no place to plug their behavior in without each
re-inventing a tool-gating mechanism inside the canvas handler. This leaf builds
the single, closed, compile-checked routing seam those leaves consume, so the
brush task fills one named arm rather than guessing at a dispatch shape (the
explicit ask of this refinement). It also honors D-gizmo-2's promise that the
always-on gizmo becomes Select-only once a router exists.

## Inputs / context

**Design docs (normative):**

- `docs/00-design.md` **D20** (`:487`) — the modal tool set: exactly
  Select · Brush · Eyedropper · Pan; "Select" is the one select tool (cameras
  folded in, D7); Pan is a selectable mode *and* Space is its transient shortcut
  (D9); Import/crop are actions, **not** modes. The active tool is headless UI
  state (A11); wiring it to canvas behavior is "downstream (no consumer at
  rail-build time)" — i.e. this leaf.
- `docs/00-design.md` **D9** (`:476`) — "**Space pans the active viewport
  camera** (transient)"; the transient viewport pan is distinct from a scene
  edit. This is the always-on path that must survive under every tool.
- `docs/01-architecture.md` **A11** (`:426`) — `dockmodel` owns the active-tool
  state; `interact` stays pure math; the tool→interaction dispatch is promoted
  when a canvas consumer exists. **This leaf adds an amendment** to A11 (see
  Decisions / Doc delta) making the layer split explicit, because the literal
  "promoted into `interact`" is levelization-impossible.
- `docs/01-architecture.md` **§8** (`:308-344`) — the levelization DAG:
  `interact:{base,scene}`, `dockmodel:{base,platform}`, `app` sees everything.
  `interact` may not name `dockmodel::ToolId`; `app` sees both.
- `docs/01-architecture.md` **§9** (`:346-367`) — the universal DoD instantiated
  under Acceptance criteria.

**Source seams:**

1. **Tool state (L1 `dockmodel`).**
   `src/dockmodel/ace/dockmodel/tool_rail.hpp:18` (`ToolId`), `:45-52`
   (`ToolSelection`). Owned at `src/dock/ace/dock/dock.hpp:572`
   (`tools_`), accessor `:466-467`.
2. **Canvas pointer handler (L4 `app`).** `app::CanvasView::draw_content`
   (`src/app/ace/app/canvas_view.hpp:78`, impl `src/app/canvas_view.cpp:104`).
   The free-viewport gesture block is `canvas_view.cpp:217-334`; per-frame
   sub-handler order:
   1. always-on nav — wheel-zoom / reset / **Space-pan** (`:232-238`,
      `in.panning` → `interact::pan`);
   2. `draw_frame_gizmos` (`:304`, defn `:407`);
   3. `draw_cell_gizmos` (`:316`, defn `:539`);
   4. `draw_group_gizmo` (`:321`, defn `:744`);
   5. `draw_selection` (`:322`, defn `:971`).
   Space-for-pan is enforced per-handler via `ImGui::IsKeyDown(ImGuiKey_Space)`
   early-outs (`:1004`, `:1013`, `:1037`, and the gizmo handlers).
3. **The behavior math (L1 `interact`).** Pan/zoom
   (`interact.hpp:32`/`:38`); pick/marquee policy as values
   (`pick.hpp` `SelectOp`/`SelectionChange` `:147-158`, `click_selection`
   `:175`, `marquee_selection` `:182`); cell/frame/group transform verbs. The
   pick result is applied to `commands::Selection` by the L4 free function
   `apply_selection_change` (`canvas_view.cpp:57-77`), driving
   `state_.selection()`.
4. **The wiring gap.** In `src/app/shell.cpp`, `CanvasView` is built at `:327`
   and its body lambda registered at `:347-354` capturing only `[&canvas]`; the
   `Dockspace` that owns `tools_` is not declared until `:374`. So
   `draw_content` has **no** `ToolId` parameter and **no** path to the active
   tool today — closing that path (parameter + capture) is part of this leaf.

**Predecessor refinements:** `tasks/refinements/editor/tool_rail.md`,
`tasks/refinements/editor/gizmo.md`, `tasks/refinements/editor/nav.md`.

## Constraints / requirements

1. **Closed enum switch, no vtable.** Dispatch is a `switch` over the four
   `dockmodel::ToolId` values, **exhaustive with no `default`** so a future
   fifth tool fails to compile rather than silently no-oping. The D20 modal set
   is closed; do not introduce a polymorphic `Tool` hierarchy or a tool-object
   registry (D-tool_dispatch-2).
2. **Routing consumed at L4; math stays in `interact`.** The `switch(ToolId)`
   lives in `app` (the only layer seeing both `dockmodel` and `interact`).
   `interact` gains **no** `dockmodel` include and no `ToolId`; it may gain pure
   helpers only if useful. `dockmodel::ToolSelection` stays a plain value type.
   **No new component, no new DAG edge**; `check_levels` stays clean (A11
   amendment).
3. **Always-on viewport gestures survive every tool.** Wheel-zoom and Space-pan
   (D9/D-nav-4) run **outside** the switch — before it, unconditionally — for
   all four tools. Do **not** fold them into the Pan-tool arm; the Pan tool is
   an *additional* left-button pan path, not a replacement for Space.
4. **Select arm = today's behavior, gated.** Under Select, the frame/cell/group
   gizmos + pick/marquee selection run exactly as today (no behavior change,
   D-gizmo-2 "touching no gizmo code" beyond the added gate). Under any other
   tool they do not engage.
5. **Pan arm.** Under Pan, a left-button drag pans the transient viewport camera
   via `interact::pan` (D-nav-4) — the same math Space-pan uses — committed
   through `host_.request_camera`, **transient only** (no journal entry, no
   dirty; the D15 invariant asserted at `tests/commands_test.cpp:137-140` must
   hold).
6. **Brush / Eyedropper arms are inert seams.** Under Brush or Eyedropper a
   plain drag does nothing beyond the always-on viewport gestures (it does
   **not** fall back to selection — D20: the tool determines the drag). Each arm
   is a named private method (`dispatch_brush` / `dispatch_eyedropper`) the
   downstream leaf fills.
7. **Transient-state discipline preserved.** No routing change may make a nav or
   selection gesture journal, and every scene-mutating gesture still commits
   through the single-writer `CanvasView::apply_edit` seam (edit_render_sync /
   A4.1 writer identity) — routing changes *which* behavior runs, never *how*
   it commits, so no new thread and no new shared state.

## Acceptance criteria

The universal DoD (`docs/01-architecture.md` §9) instantiated for this leaf:

- **Levelization / `check_levels` clean.** `scripts/check_levels.py` stays green
  with no edit: the wire is `app`→`dockmodel`/`dock`/`interact`, all pre-existing
  edges; `interact` gains no `dockmodel`/ImGui include. Assert in the refinement
  record that no new component/edge is added (Constraint 2).
- **L1 Catch2 (logic).** The closed-set routing is pinned by a unit in
  `tests/canvas_view_test.cpp` (the existing app-layer, ImGui-free unit target):
  a pure arm-selection helper `app::dispatch_arm(dockmodel::ToolId)` (returning a
  small `DispatchArm` enum) is asserted to map Select→ObjectInteraction,
  Pan→ViewportPan, Brush→Brush, Eyedropper→Eyedropper — one case per tool, so the
  closed-set/exhaustiveness invariant is provable headless. `tests/tool_rail_test.cpp`
  (catalog + `ToolSelection` round-trip) is unchanged and remains the model-side
  witness.
- **ImGui Test Engine e2e (the behavioral bulk).** A new
  `tests/tool_dispatch_e2e_test.cpp` (rig cloned from
  `tests/selection_e2e_test.cpp:257-265` — `ScratchDir`, `pump_until`, raw-position
  drive of `"canvas#1/##canvas_nav"`, active tool set via `dockspace.tools().select(...)`
  as `tool_rail_e2e_test.cpp:83-89` does) asserts, over the *same* scene:
  - **Select** — a drag over a cell handle transforms it (selection/placement
    changes); a drag on empty canvas marquee-selects. (Regression-guards the
    predecessor behavior under the new gate.)
  - **Pan** — a left-button drag moves the transient viewport camera and
    changes **no** selection and **no** journal cursor (transient-only,
    Constraint 5).
  - **Brush / Eyedropper** — a left-button drag changes **no** selection, **no**
    placement, and **no** journal (inert seam, Constraint 6).
  - **Always-on** — with the **Brush** tool active, Space-drag still pans and
    wheel still zooms (Constraint 3).
  `tests/tool_rail_e2e_test.cpp` continues to assert the rail sets the active
  tool (unchanged).
- **Rendered output (golden).** **No new byte-golden is expected**: routing
  changes which gesture does what, not what any frame composites, so the
  existing `canvas_view_64x64` / gizmo / nav goldens stay byte-identical. Per the
  established chrome precedent (D-nav-6), any tool-active canvas capture is a
  Test Engine **screenshot baseline**, not a byte-exact golden, and only where it
  adds signal.
- **Threading (ASan/TSan).** Routing adds no thread and no new shared state
  (Constraint 7); the existing writer-identity coverage
  (`tests/canvas_host_test.cpp`) stands. If it adds signal, extend that fixture
  with one Select-tool transform + one Pan-tool drag driven against a live
  real-pool `CanvasHost`, asserting the ASan/TSan lanes stay clean and the Pan
  drag produces no journal entry.
- **Coverage.** ≥90% diff coverage on the changed lines — the arm-selection
  unit plus the four e2e arms ship with the task.
- **Deferred arms (no new WBS leaf).** The Brush arm is filled by
  `editor.paint.brush` and the Eyedropper arm by `editor.color.color` — both
  **existing** leaves; this task adds no successor. The only WBS action is the
  dependency-edge addition surfaced for the orchestrator (Inherited
  dependencies / return summary), which the WBS owner applies — not encoded
  here.

## Decisions

- **D-tool_dispatch-1 — Route the active tool at the L4 canvas consumer, not
  inside `interact`.** A11's "promoted into `interact`" cannot be taken
  literally: `interact:{base,scene}` (§8, `check_levels.py`) may not name
  `dockmodel::ToolId`, so no `switch(ToolId)` can compile there. The
  levelization-forced resolution is the layer split the A11 amendment records —
  the per-tool **math** is in `interact` (already shipped for Select and Pan;
  Brush later), and the **branch reading `ToolId`** is at
  `app::CanvasView::draw_content`, the sole site seeing both components with
  **zero new edges**. *Alternative rejected:* move/duplicate `ToolId` into
  `interact` or `base` to host the switch there — a genuine new coupling
  (`interact→dockmodel`, or a shared enum two components must agree on) for no
  benefit, since the branch also needs `CanvasInput` and the `Presenter`
  transient state, both L4. *Alternative rejected:* keep the branch in L3
  `views` (which already sees `dockmodel`) — but the transient camera, gizmo
  grab bases, and marquee anchor it must drive all live on `app::CanvasView::Presenter`
  (L4), so the branch belongs where that state is.
- **D-tool_dispatch-2 — Enum switch over the closed set, not a `Tool` vtable.**
  The D20 modal set is fixed at four; a compile-time-exhaustive `switch` (no
  `default`) makes adding a fifth tool a compile error (forcing a deliberate
  D-row change), and matches D-gizmo-2's "add a branch it owns" and the design
  bias toward the simpler abstraction with one call site. *Alternative
  rejected:* a polymorphic `Tool` base with per-tool subclasses — it would push
  the behaviors (tightly coupled to `Presenter` transient state and `CanvasInput`)
  behind an interface, add lifetime/ownership machinery, and buy extensibility a
  *closed, design-fixed* set does not want.
- **D-tool_dispatch-3 — Brush and Eyedropper ship as inert, named arms.** This
  leaf's shipped behavior is Select (gated) + Pan; Brush/Eyedropper are
  `dispatch_brush` / `dispatch_eyedropper` no-ops filled by `editor.paint.brush`
  / `editor.color.color`. A plain drag under those tools does nothing beyond the
  always-on viewport gestures — it does **not** fall back to selection, because
  D20 makes the tool decide the drag, and a silent select-fallback would make
  "Brush is active" observably indistinguishable from Select until the paint
  leaf lands. *Alternative rejected:* defer the whole switch until brush exists —
  it would leave the rail effectless through the intervening leaves and force
  the brush task to invent the routing this leaf is chartered to build.
- **D-tool_dispatch-4 — Always-on viewport gestures sit outside the switch.**
  Wheel-zoom and Space-pan (D9/D-nav-4) are transient-viewport navigation that
  D9 makes tool-independent; they run before the `switch` for every tool. The
  Pan *tool* adds a left-button pan **on top of** Space, never replacing it.
  *Alternative rejected:* model Space-pan as "temporarily the Pan tool" — it
  would entangle a transient modifier with the persistent modal state and break
  the D9 "Space pans regardless" guarantee.
- **Doc delta — A11 amendment (`docs/01-architecture.md:426`).** Appends the
  layer-split clarification above to the A11 row (same-commit), because the row's
  literal "promoted into `interact`" is levelization-impossible and would
  otherwise mislead the very next consumer (the brush task) about where its arm
  plugs in. No new external dependency, no new component, no new DAG edge — a
  clarification of an existing row, in the sibling `*(Amended by …)*` style
  (cf. A14/A16).

## Open questions

(none — all decided)

## Status

**Done** — 2026-07-29.

- Created `src/app/ace/app/tool_dispatch.hpp`: pure `DispatchArm` enum + inline exhaustive `dispatch_arm` free function (the L1-testable closed-set routing helper).
- Created `tests/tool_dispatch_e2e_test.cpp`: ImGui Test Engine e2e covering Select (transform + marquee), Pan (pans transient camera, no journal), Brush/Eyedropper (inert seams), and always-on Space-pan + wheel-zoom under Brush.
- Edited `src/app/ace/app/canvas_view.hpp`: `draw_content` gains defaulted `dockmodel::ToolId` parameter; `dispatch_pan`, `dispatch_brush`, `dispatch_eyedropper` private method declarations added.
- Edited `src/app/canvas_view.cpp`: compile-exhaustive `switch(dispatch_arm(tool))` wraps the gizmo/pick stack under Select, Pan routes left-button drag to `interact::pan` (transient only, no journal), Brush/Eyedropper are named inert arms; wheel-zoom and Space-pan remain outside the switch, unconditional.
- Edited `src/app/shell.cpp`: Canvas body lambda moved after `Dockspace` construction; threads `dockspace.tools().active()` into `draw_content`.
- Edited `CMakeLists.txt`: `ace_tests` target gains `src/app` include directory (no `ace::app` link); `tool_dispatch_e2e_test.cpp` registered.
- Edited `tests/canvas_view_test.cpp`: L1 Catch2 unit `tool_dispatch: each modal tool maps to its canvas dispatch arm` (5 assertions, one per `ToolId` + exhaustiveness probe).
- Amended `docs/01-architecture.md` A11: layer-split clarification (routing branch at L4 `app`, math in L1 `interact`; no new DAG edge).
