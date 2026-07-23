# editor.canvas.focused_canvas_indicator — Show which canvas the framing-derived verbs act on

## TaskJuggler entry

- **Task:** `editor.canvas.focused_canvas_indicator` — `tasks/00-editor.tji:254-259`, inside
  `task canvas "Canvas & rendering"` (`tasks/00-editor.tji:171`), inside `task editor`.
- **Effort:** `0.5d` · `allocate team`
- **Depends:** `editor.cameras.mint_from_focused_canvas` (`complete 100`).
- **Note (`.tji:258`):** *"editor.cameras.mint_from_focused_canvas makes window focus
  semantically load-bearing — the focused canvas decides what 'New Shot From View' promotes and
  where an inserted cell lands — but leaves it invisible: ImGui's own focus chrome is a title-bar
  tint that is easy to miss in a two-pane dock and is absent while the rail holds focus. Draw a
  passive marker on the pane `CanvasView::focused_view_id()` names (a one-pixel accent border
  inside the pane rect, or a small badge beside the existing camera-picker overlay at
  src/app/canvas_view.cpp:236-245), driven from the sticky hint rather than from live ImGui focus
  so it persists across the rail click — plus an e2e phase asserting the marker follows
  WindowFocus and survives a rail interaction. Nothing in dockmodel, nothing in dock (D18's 'no
  privileged editor area' is about layout, not about indicating where the pointer verb applies).
  Source-of-debt: tasks/refinements/cameras/mint_from_focused_canvas.md. Design:
  docs/00-design.md D23, D18."*
- **Back-link:** this refinement lands at `tasks/refinements/canvas/focused_canvas_indicator.md`.
  **The closer** appends `Refinement: tasks/refinements/canvas/focused_canvas_indicator.md` to the
  `.tji` note and adds `complete 100` after `allocate team`. **Do not** hand-edit the `.tji` here.
- **Source of debt:** `tasks/refinements/cameras/mint_from_focused_canvas.md:482-499` (the
  deferred-WBS-work bullet that named this leaf).
- **Downstream dependents:** none registered today. Two follow-ups are registered *by* this leaf
  (see Acceptance criteria → Deferred WBS work).
- **Milestone:** `m9_editor` (`tasks/99-milestones.tji:6`), reached through the `editor.canvas`
  container dependency (`tasks/99-milestones.tji:8`).

## Effort estimate

**Half a day.** The seam already exists; this leaf spends its budget on making the marker
*provably agree with the verb* rather than on drawing a rectangle.

- **The hint is already tracked and already accessible.** `focused_view_id_`
  (`src/app/ace/app/canvas_view.hpp:232`) is stamped in `draw_content`
  (`src/app/canvas_view.cpp:93-95`), cleared in `reconcile` (`src/app/canvas_view.cpp:531-533`),
  and exposed as `focused_view_id()` (`src/app/ace/app/canvas_view.hpp:133`,
  `src/app/canvas_view.cpp:578`). Nothing about focus *tracking* is in scope — only reading it.
- **The pane rect is already in hand.** `pane_origin` is computed at
  `src/app/canvas_view.cpp:179`, outside the "a frame has landed" branch (`:180-244`), and
  `pane_width`/`pane_height` are `draw_content` parameters. The camera-picker overlay
  (`src/app/canvas_view.cpp:246-249`, body at `:252-271`) is already drawn at that origin,
  unconditionally, after the texture branch — the marker slots in beside it.
- **The draw-list idiom is established.** `ImGui::GetWindowDrawList()` is already used twice in
  this file (`src/app/canvas_view.cpp:281`, `:420`), and `IM_COL32(120, 200, 255, …)` is the
  de-facto accent at four sites (`:296`, `:469`, `:470`, `:497`).
- **The real work is the anti-divergence refactor** (D-focused_canvas_indicator-1): hoisting the
  winner-selection half of `framing_for_focus` (`src/app/view_framing.cpp:15-37`) into a named
  pure function so the marker and the verb read the *same* rule, plus the Catch2 matrix that pins
  it, plus a six-phase e2e.

New code: one new pure function + one refactored function in `src/app/view_framing.{hpp,cpp}`
(~20 lines), one public accessor + one private projection helper + a ~6-line draw block in
`src/app/{ace/app/,}canvas_view.{hpp,cpp}`, ~8 new Catch2 cases appended to
`tests/focused_framing_test.cpp`, one new `tests/focused_canvas_indicator_e2e_test.cpp` added to
the `ace_shell_test` source list (`CMakeLists.txt:252-271`).
**No new component, no new DAG edge, no new external dependency, no libarbc change, no `dock`
change, no `dockmodel` change, no `views` change.** One doc delta (**D23, amended**).

## Inherited dependencies

**Settled (consumed as-is):**

- `editor.cameras.mint_from_focused_canvas` — `tasks/refinements/cameras/mint_from_focused_canvas.md`
  (**Done**). Consumed:
  - **D-mint_from_focused_canvas-1** — the hint is **sticky**: set on any frame the pane's window
    is focused (`ImGuiFocusedFlags_RootAndChildWindows`), *never* cleared on an unfocused frame,
    because the rail item is an `ImGui::Selectable` in the `"Tool Rail"` window
    (`src/dock/dock.cpp:231-232`) that steals focus before the verb runs. This is precisely what
    makes a marker driven from the hint survive the rail click, and what makes a marker driven
    from live `ImGui::IsWindowFocused` wrong.
  - **D-mint_from_focused_canvas-2** — the resolution rule is **total**: focused-and-sized → else
    lowest-id sized → else the zero `ViewFraming` sentinel. A stale or unsized hint degrades to
    the fallback, never to the sentinel. Shipped as `framing_for_focus`
    (`src/app/ace/app/view_framing.hpp:51-52`, `src/app/view_framing.cpp:15-37`).
  - **D-mint_from_focused_canvas-7** — `focused_view_id_` is **UI-thread-owned** transient session
    state on the far side of D15: written only in `draw_content`, read only on the UI thread, never
    serialized, never a transaction, never read by `commands`/`scene`/`project`, and explicitly
    **not** promoted into `dockmodel` (A11) — ImGui owns window focus and mutates it on its own,
    so a `dockmodel::DockLayout::focused_view_id` field would be a second source of truth. This
    leaf inherits that ownership verbatim and widens nothing.
  - The `focused_view_id()` accessor itself, added "so the e2e can pin the *tracking* separately
    from the *rule*" — the same argument this leaf reuses for `indicated_view_id()`
    (D-focused_canvas_indicator-5).
- `editor.canvas.multi_canvas`, `editor.canvas.frame_sync`, `editor.canvas.single_writer` — all
  `complete 100`; N panes over one `Document` (A5) with a lock-free render read is the substrate
  the two-pane e2e runs on.

**Pending (owned here):** nothing. Every predecessor is `complete 100`.

## What this task is

1. **Name the target, don't just compute its framing** (D-focused_canvas_indicator-1). Split the
   *winner selection* out of `framing_for_focus` into a pure `focus_target(panes_by_id,
   focused_view_id) -> std::string_view`, and re-express `framing_for_focus` on top of it.
   `framing_for_focus`'s observable behaviour is unchanged; what is new is that a caller can ask
   **which pane** the framing-derived verbs will act on, not merely what framing they will get.
2. **Expose that answer on `CanvasView`** (D-focused_canvas_indicator-5). Add
   `std::string_view CanvasView::indicated_view_id() const`, which projects `presenters_` into
   `PaneFraming` rows exactly as `framing_for()` already does (`src/app/canvas_view.cpp:555-567`)
   and returns `focus_target(rows, focused_view_id_)`.
3. **Draw a passive hairline accent border** just inside the marked pane's content rect
   (D-focused_canvas_indicator-2, -3, -4), in `draw_content` immediately before the camera-picker
   call at `src/app/canvas_view.cpp:246-249` — i.e. **outside** the "a frame has landed" branch,
   so a cold pane that has not yet composited is still marked. Draw-list only: no `ImGui::Item`,
   no hit-test, no layout, no interference with `##canvas_nav`'s `SetNextItemAllowOverlap`
   arrangement (`src/views/views.cpp:60`).
4. **Amend D23** with the "that pane is shown, not inferred" clause (the doc delta below).
5. **Pin it with tests**: an extended Catch2 matrix over `focus_target` including a
   *consistency property* against `framing_for_focus`, and a six-phase ImGui Test Engine e2e that
   asserts the marker follows `WindowFocus`, survives a rail interaction, and falls back correctly
   when the marked canvas is closed.

Out of scope, by inheritance and by charter: any change to how focus is *tracked* (owned and
settled by `mint_from_focused_canvas`); anything in `dockmodel`, `dock`, or `views`; ImGui's own
title-bar focus tint (untouched — the marker is additive, not a replacement); a per-pane selection
or tool indicator (D19 makes selection project-scoped, not per-canvas — a different signal);
overview-panel chrome for the live viewport rect (`docs/00-design.md:179`, owned by
`editor.panels.overview`); and theming/palette work (see Deferred WBS work).

## Why it needs to be done

`mint_from_focused_canvas` made window focus **semantically load-bearing**: with two canvases
open, the focused pane decides what "New Shot From View" promotes (D23) and where an inserted
cell lands (A16). It shipped that rule with no visible expression of it. The user-facing failure
is concrete and inherited from the very mechanism that makes the rule correct:

- The rule is **sticky by design** — at the moment the verb fires, the rail `Selectable` holds
  ImGui focus and *no* canvas is focused. So ImGui's own focus chrome is showing the **rail**,
  not the canvas the verb is about to act on. The one moment the user most needs the answer is
  the one moment the built-in affordance actively misleads.
- The rule has a **fallback branch** (lowest-id sized pane) that has no chrome at all: on a fresh
  session where no canvas has yet held focus, the verb targets `canvas#1` while ImGui shows no
  canvas as focused.
- A background dock tab does not draw (`tests/multi_canvas_e2e_test.cpp:246-252`), so a
  focused-then-tabbed-behind pane keeps the hint while being entirely off-screen — a state the
  user cannot currently distinguish from "the visible pane is the target".

Downstream, every future framing-derived verb inherits the same ambiguity, so the marker is
built once here against the shared rule rather than re-derived per verb.

## Inputs / context

**Governing design docs (normative — the constitution):**

- `docs/00-design.md` **D23 "Minting a camera"** (`:490`) — in particular the *"Which viewport,
  when more than one canvas is open (D18)"* clause: *"the **focused** one — the canvas pane the
  user most recently worked in, remembered across the click that lands on the rail itself, falling
  back to the lowest-id live pane when no canvas has yet held focus."* This leaf **amends** D23
  (see Decisions → doc delta).
- `docs/00-design.md` **D18 "Uniform dockspace"** (`:485`), backed by §10 (`:420-462`) — governing
  but **unchanged**. "No privileged editor area" constrains *layout*: no pane gets a reserved
  slot, a bigger default size, or an un-closable guarantee. It does not forbid *indicating* which
  pane a pointer verb resolves to; the marker is per-pane chrome computed from a rule, not a
  layout privilege, and it moves freely with focus.
- `docs/00-design.md` **D15** (`:482`) — the transient/scene line. The marker reflects transient
  session state only; it is never persisted and never a transaction.
- `docs/00-design.md` **D19** (`:486`) — selection is **project-scoped, not per-canvas**. This is
  why the marker cannot be conflated with selection chrome: N panes share one selection, but
  exactly one pane is the framing target.
- `docs/00-design.md` **D2** (`:469`), **D20** (`:487`), **D22** (`:488`) — the canvas/camera
  model, the rail's modal set, and the rail-as-chrome framing that this marker complements.

**Governing architecture rows (`docs/01-architecture.md`):**

- **A8** (`:299`) / **§8** (`:185-221`) — the levelization DAG. `app` is **L4** (`:216`, "everything",
  ImGui yes); L1 (`project`/`scene`/`interact`/`commands`/`dockmodel`) may never include ImGui.
  `scripts/check_levels.py:45-51` whitelists `imgui` for `{views, dock, app}`.
- **A9** (`:300`) / **§9** (`:222-287`) — layered testing as per-leaf acceptance criteria.
- **A11** (`:302`) — `dockmodel` owns headless UI state. D-mint_from_focused_canvas-7 already
  forbids widening it with focus; this leaf honours that.
- **A5** (`:296`) — multi-canvas is N `HostViewport`/`InteractiveRenderer` over one `Document`.
- **A14** (`:305`) — the transient viewport camera lives at `app::CanvasView::Presenter::camera`;
  a camera renders zero pixels, so camera chrome is always-on-top overlay (cf. **A17**, `:308`).
- **A16** (`:307`) — the insert path that shares the focused pane with the mint verb.
- **§9.1** (`:251-286`) — the `clang-asan` lane (SDL3 `offscreen` + Mesa `llvmpipe`,
  `tests/lsan.supp`) the new e2e joins.

**Editor seams this leaf extends:**

- `src/app/ace/app/view_framing.hpp:30-33` — `PaneFraming` (`view_id` is a **non-owning** view into
  `CanvasView::presenters_`'s keys; a `PaneFraming` must not outlive it).
- `src/app/ace/app/view_framing.hpp:35-52` / `src/app/view_framing.cpp:15-37` — `framing_for_focus`
  and its three-branch rule; `sized()` at `:8-13`. The header already promises *"Pure: no ImGui,
  no GL, no `CanvasView` — the whole branch matrix is headless-testable."*
- `src/app/ace/app/canvas_view.hpp:133` / `src/app/canvas_view.cpp:578` — `focused_view_id()`.
- `src/app/ace/app/canvas_view.hpp:232` — `focused_view_id_`, with the ownership comment at
  `:228-231`.
- `src/app/canvas_view.cpp:77-250` — `draw_content`. Key anchors: degenerate-pane early-out
  (`:78-80`), the sticky stamp (`:81-95`), `pane_origin = ImGui::GetCursorScreenPos()` (`:179`),
  the frame-exists branch (`:180-244`), and the unconditional camera-picker tail (`:246-249`).
- `src/app/canvas_view.cpp:555-567` — `framing_for()`, the `presenters_` → `PaneFraming`
  projection; `primary_framing()` (`:569-574`) and `focused_framing()` (`:576`).
- `src/app/canvas_view.cpp:517-540` — `reconcile()`, which clears the hint when the named pane's
  presenter is dropped (`:531-533`).
- `src/dock/dock.cpp:577-590` — the per-pane `ImGui::Begin(id)`/`End` loop. **The view id is the
  window name** (`"canvas#1"`), which is why no id plumbing is needed. Untouched by this leaf.
- `src/app/shell.cpp:291` — `app_gateway->set_view_framing([&canvas]{ return canvas.focused_framing(); })`,
  the single provider both verbs read. Untouched.
- `src/views/views.cpp:111-122` — `draw_letterboxed` restores the cursor to `origin` so caller
  chrome lands predictably; `:129-143` `draw_scale_bar` is the existing "chrome anchored to the
  window rect" precedent.

**Predecessor refinements:**

- `tasks/refinements/cameras/mint_from_focused_canvas.md` — the source of debt; D-…-1, -2, -7
  above.
- `tasks/refinements/cameras/new_shot_from_view.md`, `tasks/refinements/cameras/frame_selection.md`
  — the two D23 amendments this one follows.
- `tasks/refinements/editor/multi_canvas.md`, `tasks/refinements/editor/canvas_view.md` — the
  pane/presenter lifecycle.

**Test rigs:**

- `tests/focused_framing_test.cpp` — the six existing `framing_for_focus` cases (`:34`, `:53`,
  `:69`, `:90`, `:105`, `:118`); its header comment (`:8-9`) records that it lives in
  `ace_shell_test` because only that target links `ace::app` (`CMakeLists.txt:252-271`;
  `ace_tests` at `:219-245` does not).
- `tests/multi_canvas_mint_e2e_test.cpp` — the direct rig ancestor. `ScratchDir` (`:80-91`),
  `NoopFolderDialog`/`NoopLauncher` (`:93-103`), the `E2EState` `UserData` POD (`:105-113`,
  necessary because `TestFunc` is a plain function pointer — `std::function` is disabled),
  `pump_until` (`:115-125`), `settle` (`:130-143`), engine boot (`:214-248`), the
  `WindowFocus` + `pump_until(focused_view_id() == …)` idiom (`:276-277`, `:379-380`, `:423-424`),
  the rail-ref construction `std::string rail = ace::dock::tool_rail_title();` (`:259`) and
  `ctx->ItemClick((rail + "/Canvas").c_str())` (`:267`), the
  `"Tool Rail/###new_shot_from_view"` click (`:310`), **the survives-a-rail-click assertion**
  (`:328-332`), and the drive/teardown loop (`:513-535`).
- `tests/multi_canvas_e2e_test.cpp` — the framebuffer-probe pattern: `snapshot_now` atomic set
  from `TestFunc` (`:266`), the `grab_frame` lambda handed to `shell.render()` (`:312-327`,
  `:334`), the `pane_is_lit` reader with the **glReadPixels y-flip** `fb_row = captured_h - 1 - cy`
  (`:359-372`), the close-a-canvas + reconcile drain (`:296-301`), and the header note (`:1-11`)
  that software-GL frames are **not byte-comparable**.
- `tests/camera_manip_e2e_test.cpp:250-271` — the `"canvas#1/##canvas_nav"` `ItemInfo(...).RectFull`
  pane-rect probe.
- `tests/golden_support.hpp:36-46` + `tests/goldens/*.rgba8` — the byte-exact golden rig (seven
  64×64 CPU renders), driven from `ace_tests`, **not** from e2e.
- `scripts/gate` (`:17-18`) — `check_levels` → clang-format → configure/build → `ctest`.

**Parking lot:** `tasks/parking-lot.md` — the human-judgment queue. Nothing in this leaf is routed
there except the product-taste note recorded by the closer (see Open questions).

## Constraints / requirements

1. **The marker must be computed from the same rule the verbs consume — structurally, not by
   convention.** After this leaf, `framing_for_focus` must be *implemented in terms of*
   `focus_target`, so a future change to the resolution rule cannot move the verb without moving
   the marker. Two parallel implementations of "which pane wins" is the defect class this leaf
   exists to foreclose.
2. **`framing_for_focus`'s observable behaviour must not change.** The six existing cases in
   `tests/focused_framing_test.cpp` must pass **unmodified**, including the empty-hint path that
   `primary_framing()` depends on for bit-identity.
3. **`src/app/ace/app/view_framing.{hpp,cpp}` must stay ImGui-free, GL-free, and
   `CanvasView`-free.** The header's purity promise (`:49-50`) is load-bearing for the Catch2
   matrix; `focus_target` inherits it.
4. **The marker is driven from `focused_view_id_` (the sticky hint), never from a live
   `ImGui::IsWindowFocused` poll at draw time.** A live poll shows the rail during exactly the
   interaction the marker exists to disambiguate (D-mint_from_focused_canvas-1).
5. **The marker must be drawn outside the "a frame has landed" branch**
   (`src/app/canvas_view.cpp:180-244`). A pane that has not yet composited is still a legitimate
   framing target once it is sized, and a marker that blinks on at first-frame would misreport the
   cold state. Placing it with the camera picker (`:246-249`) satisfies this by construction.
6. **The marker must be passive.** Draw-list only (`ImGui::GetWindowDrawList()`): no `ImGui::Item`,
   no `InvisibleButton`, no id, no hit-test, no cursor advance, no `SetNextItemAllowOverlap`
   interaction. It must not change the rect any existing item occupies, and must not perturb the
   `##canvas_nav` overlay arrangement (`src/views/views.cpp:60`).
7. **UI-thread only.** `focus_target`, `indicated_view_id()`, and the draw block all run on the UI
   thread inside `draw_content`/the rail's frame. No new shared state, no new lane, no new
   handoff — D-mint_from_focused_canvas-7's ownership is inherited unchanged.
8. **Nothing in `dockmodel`, nothing in `dock`, nothing in `views`, nothing in L1.** The marker is
   L4 `app` chrome reading L4 `app` state through an L4 `app` pure function.
9. **No effect on rendered output.** The marker is ImGui draw-list chrome composited into the live
   framebuffer; it must never reach a libarbc `render_offline` tile, an export, or the canvas
   texture. Every existing golden must pass unmodified.
10. **The marker applies uniformly to look-through panes.** D23 makes a look-through pane a
    first-class framing source (it offers its shot's framing at screen size), so it is markable on
    the same terms as a free-navigation pane. The draw site at `:246-249` runs for both branches.
11. **The projection must respect `PaneFraming`'s lifetime contract**
    (`src/app/ace/app/view_framing.hpp:26-29`): `focus_target` returns a `std::string_view` into
    the caller's own key storage (`presenters_`'s keys), so `indicated_view_id()`'s result has
    exactly the lifetime of `focused_view_id()`'s — valid until the next `reconcile`. Document it
    on the accessor.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate` green
(check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.** No new component and
  no new DAG edge. `src/app` is L4 and is already whitelisted for `imgui`/`gl_api`/`sdl`/`arbc`
  (`scripts/check_levels.py:37-40`, `:45-51`), so the ImGui draw block is legal where it lands.
  The *positive* claim: `src/app/ace/app/view_framing.hpp` and `src/app/view_framing.cpp` gain **no
  new include** — they must not acquire `<imgui.h>`, GL, or `<ace/app/canvas_view.hpp>`. A
  reviewer can confirm the whole leaf against the include diff.

- **Pure-logic unit — Catch2, headless.** Append to `tests/focused_framing_test.cpp` (already in
  the `ace_shell_test` source list, `CMakeLists.txt:252-271`), keeping its existing six
  `framing_for_focus` cases untouched:
  - `focus_target`: focused id names a **sized** pane → returns that id.
  - `focus_target`: focused id names a pane that **exists but is unsized** (`pane_w == 0` or
    `pane_h == 0`) → returns the first sized id, **not** the focused one and **not** empty.
  - `focus_target`: focused id names **no pane** (a closed canvas) → returns the first sized id.
  - `focus_target`: **empty** focused id → returns the first sized id (the historical
    `primary_framing()` path).
  - `focus_target`: panes exist but **none is sized** → returns an **empty** `string_view`.
  - `focus_target`: **no panes at all** → returns an empty `string_view`.
  - **Consistency property (the anti-divergence assertion).** Over the whole fixture matrix
    above, assert `framing_for_focus(rows, hint)` equals — field by field, camera included — the
    framing of the row `focus_target(rows, hint)` names, and equals the zero `ViewFraming` exactly
    when `focus_target` returns empty. This is the test that makes Constraint 1 mechanical rather
    than aspirational.
  - **Anti-vacuity:** at least one fixture must resolve to a **non-empty** target whose framing
    carries a **non-identity** camera and a non-zero pane size, and at least one must resolve to
    **empty** — so a `focus_target` stubbed to always return `""` (or `panes[0].view_id`) fails.
    At least one fixture must have **≥3 panes** with the focused one **not first**, so a stub that
    returns the first row fails the focused-wins case.
  - **Regression:** the six pre-existing cases pass **unmodified** (Constraint 2). Any edit to
    them in this diff is a failure signal, not an update.

- **Rendered output — golden: N/A, justified.** The marker is ImGui draw-list chrome in the live
  framebuffer; `render_offline` never sees ImGui, and A14 already establishes that camera-adjacent
  chrome renders zero composition pixels. The positive obligation instead: the seven existing
  goldens in `tests/goldens/` must pass **byte-identically and unmodified** — golden churn in this
  diff means the marker leaked into the canvas texture path, which is a **failure signal, not an
  update**.

- **UI e2e — ImGui Test Engine.** New `tests/focused_canvas_indicator_e2e_test.cpp`, added to the
  `ace_shell_test` source list (`CMakeLists.txt:252-271`), registering
  `IM_REGISTER_TEST(engine, "multi_canvas", "focused_canvas_indicator")`. It borrows the rig from
  `tests/multi_canvas_mint_e2e_test.cpp` (`ScratchDir`, `NoopFolderDialog`/`NoopLauncher`, the
  `E2EState` `UserData` POD, `pump_until`, `settle`, the `:513-535` drive/teardown loop) and the
  framebuffer probe from `tests/multi_canvas_e2e_test.cpp:308-372` (the `snapshot_now` atomic, the
  `grab_frame` lambda, the `fb_row = captured_h - 1 - cy` y-flip). **State assertions are on
  `canvas.indicated_view_id()`; pixel assertions are a corroborating second signal, never the
  only one.** Phases:
  1. **Single pane is marked.** Boot headless (`opts.headless = true`, 900×640), `settle`, assert
     `canvas.indicated_view_id() == "canvas#1"`.
  2. **Open `canvas#2`.** `ctx->ItemClick((rail + "/Canvas").c_str())` with
     `rail = ace::dock::tool_rail_title()`; `pump_until` two dock nodes exist; `settle`.
  3. **The marker follows `WindowFocus`.** `ctx->WindowFocus("canvas#2")`;
     `IM_CHECK(pump_until(ctx, [&]{ return canvas.focused_view_id() == "canvas#2"; }))`; then
     `IM_CHECK(canvas.indicated_view_id() == "canvas#2")`.
  4. **The border is on `canvas#2` and not on `canvas#1` (two-sided pixel probe).** Take each
     pane's rect from `ctx->ItemInfo("canvas#N/##canvas_nav").RectFull` (the shipped idiom,
     `tests/camera_manip_e2e_test.cpp:250-271`), capture a frame, and read the top-border row a
     few pixels in from the corner. Assert the `canvas#2` sample matches the accent
     `(120, 200, 255)` within **±8 per channel**, and that the same-offset `canvas#1` sample does
     **not**. The two-sided form is what makes the probe meaningful: a marker drawn on every pane
     fails, and a marker drawn on none fails.
  5. **It survives a rail interaction.** `ctx->ItemClick("Tool Rail/###new_shot_from_view")` — the
     rail `Selectable` steals ImGui focus, exactly as at
     `tests/multi_canvas_mint_e2e_test.cpp:328-332`. Assert `canvas.indicated_view_id()` is
     **still** `"canvas#2"`, and re-run the phase-4 probe to confirm the border did not move to
     `canvas#1` or vanish. This is the phase the whole leaf exists for.
  6. **The fallback pane is marked too.** Close `canvas#2` while it is the marked pane
     (`dockspace.close("canvas#2")` + reconcile drain, `tests/multi_canvas_e2e_test.cpp:296-301`),
     so `reconcile` clears the hint (`src/app/canvas_view.cpp:531-533`). Assert
     `canvas.focused_view_id()` is empty **and** `canvas.indicated_view_id() == "canvas#1"`, and
     probe that the border is now on `canvas#1`. This pins the "marks the fallback, not just the
     hint" half of D-focused_canvas_indicator-1 through the real UI.

  **Tolerance justification (the ±8, per §9's "tolerances are the justified exception").** The
  probed pixels are ImGui's own solid-colour vector output, drawn **opaque** (alpha 255,
  D-focused_canvas_indicator-4) so there is no blend against the varying canvas texture beneath.
  The "software-GL frames are not byte-comparable" caveat
  (`tests/multi_canvas_e2e_test.cpp:1-11`) applies to the composited *canvas* content, not to a
  flat ImGui triangle; ±8 absorbs llvmpipe's rasteriser rounding without admitting the ~(26,26,31)
  clear colour or any plausible canvas pixel into the accent band.

- **Regression — the existing suites must pass *unmodified*.**
  `tests/multi_canvas_mint_e2e_test.cpp`, `tests/multi_canvas_e2e_test.cpp`,
  `tests/new_shot_from_view_e2e_test.cpp`, `tests/frame_selection_e2e_test.cpp`,
  `tests/selection_e2e_test.cpp`, `tests/camera_manip_e2e_test.cpp`, and
  `tests/app_project_gateway_test.cpp:564-700` (the four headless framing cases) all pass with no
  edits. Any e2e that pixel-probes a canvas pane must survive the new border — if one does not,
  the marker is being drawn somewhere it does not belong, and the fix is the marker, not the test.

- **Threading (ASan/TSan).** **No new threading case and no new lane**, stated as a positive
  claim: `focus_target` is a pure function over a caller-owned span, `indicated_view_id()` reads
  only UI-thread-owned `presenters_`/`focused_view_id_`, and the draw block touches only the
  current ImGui draw list. No new shared state crosses the UI↔driver handoff. The new e2e joins
  `ace_shell_test`, which already runs in the `clang-asan` lane (§9.1, SDL3 `offscreen` + Mesa
  `llvmpipe`, suppressions via `tests/lsan.supp`) — it must be clean there with no new suppression
  entry.

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed lines; clang-format +
  build clean. The `view_framing` lines are covered by the Catch2 matrix, the `canvas_view` lines
  by the e2e. Tests ship with the task.

- **Doc delta (same commit).** `docs/00-design.md` D23 (`:490`) amended with the *"That pane is
  shown, not inferred"* clause — carried by D-focused_canvas_indicator-1, -2, -3 (see Decisions).
  No `docs/01-architecture.md` row changes: A8/A9/A11/A14/A16 already cover the level, the tests,
  the `dockmodel` boundary, and the chrome-is-overlay precedent.

- **Deferred WBS work.** Two named follow-ups, for the closer to register mechanically:
  - **`editor.canvas.accent_palette`** — *"Hoist the canvas accent colour into a shared
    alpha-parameterized constant"*, **0.5d**, `allocate team`,
    `depends editor.canvas.focused_canvas_indicator`, under `task canvas "Canvas & rendering"`
    (`tasks/00-editor.tji:171`), wired into `m9_editor` (`tasks/99-milestones.tji:8`) through the
    `editor.canvas` container dependency. Scope: `IM_COL32(120, 200, 255, α)` now appears at five
    sites in `src/app/canvas_view.cpp` (`:296` amber at α=200 is a *different* hue, `:469` α=40,
    `:470` α=220, `:497` α=255, plus this leaf's new border at α=255) with no shared definition —
    a blanket rename cannot consolidate them because the alphas differ, so this needs a small
    decomposed helper (`constexpr` RGB triplet + an `accent(std::uint8_t alpha)` `constexpr`
    function, or the equivalent) applied to every site, with the existing goldens and e2e pixel
    probes proving the output is byte-identical. Prerequisite for any future theming. Source-of-debt:
    `tasks/refinements/canvas/focused_canvas_indicator.md`. Design: `docs/00-design.md` D18.
  - **`editor.canvas.view_id_natural_order`** — *"Order canvas view ids numerically, not
    lexicographically, in the framing fallback"*, **0.5d**, `allocate team`,
    `depends editor.canvas.focused_canvas_indicator`, under `task canvas "Canvas & rendering"`
    (`tasks/00-editor.tji:171`), wired into `m9_editor` (`tasks/99-milestones.tji:8`) through the
    `editor.canvas` container dependency. Scope: `framing_for_focus`/`focus_target`'s "lowest-id
    sized pane" fallback consumes `panes_by_id` in the order
    `std::map<std::string, Presenter, std::less<>>` yields (`src/app/ace/app/canvas_view.hpp:227`),
    i.e. **lexicographic** — so with ten or more canvases open, `"canvas#10"` sorts before
    `"canvas#2"` and the fallback (and therefore the marker this leaf draws) picks the wrong pane.
    Fix by ordering the projection in `CanvasView::framing_for` (`src/app/canvas_view.cpp:555-567`)
    with a natural/numeric-suffix comparator, keeping `focus_target` itself order-agnostic and
    pure; Catch2 cases in `tests/focused_framing_test.cpp` covering the `canvas#2` vs `canvas#10`
    ordering, plus an e2e that opens ten panes. Pre-existing defect inherited from
    `mint_from_focused_canvas`, surfaced (not introduced) by this leaf. Source-of-debt:
    `tasks/refinements/canvas/focused_canvas_indicator.md`. Design: `docs/00-design.md` D23, D18.
  - Everything else out of scope already has an owner or a scheduled owner: focus *tracking* is
    owned by `editor.cameras.mint_from_focused_canvas` (shipped); the overview panel's live-viewport
    rect (`docs/00-design.md:179`) is owned by `editor.panels.overview`; tool→interaction dispatch
    chrome is owned by `editor.canvas.tool_dispatch` (A11); deep-zoom orientation aids are owned by
    `editor.canvas.nav_aids` (`tasks/00-editor.tji:217`).

## Decisions

- **D-focused_canvas_indicator-1 — The marker names the pane the framing rule *resolves to*, not
  the pane the raw sticky hint names; the two are made structurally inseparable.** Hoist the
  winner-selection half of `framing_for_focus` (`src/app/view_framing.cpp:15-37`) into
  `std::string_view focus_target(std::span<const PaneFraming>, std::string_view focused_view_id)`
  in the same header, and re-express `framing_for_focus` as "resolve the target, then return that
  row's framing (zero sentinel when the target is empty)".
  *Rationale:* (i) the hint and the target **genuinely differ** in three shipped states — an empty
  hint on a never-focused session, a stale hint naming a closed canvas, and a hint naming an
  unsized pane — and in all three the verb acts on the lowest-id sized pane while the hint names
  nothing or names the wrong pane. A marker driven from the raw hint would be *absent exactly
  where the fallback branch fires*, i.e. it would be silent in the most confusing case (fresh
  session, verb targets `canvas#1`, no pane marked). (ii) Deriving both from one function means a
  future change to the rule cannot desynchronise them; the consistency property test makes that a
  compile-and-run guarantee rather than a comment. (iii) The extraction costs ~10 lines and adds
  the only headless-testable surface this leaf has — without it, a 0.5d UI leaf ships with e2e
  coverage only.
  *Alternative rejected — read `focused_view_id()` directly and mark that pane.* This is what the
  `.tji` note literally suggests ("Draw a passive marker on the pane `CanvasView::focused_view_id()`
  names"), and it is simpler by one function. Rejected because it is **wrong in the fallback
  branch**, which is not an edge case: it is the state of every freshly-opened project until the
  user clicks a canvas, and the state immediately after the marked canvas is closed. The note's
  intent — "driven from the sticky hint rather than from live ImGui focus" — is fully honoured
  here; this decision only refines *which* derived answer the marker shows, in the direction the
  note's own purpose ("show which canvas the verbs act on") demands.
  *Alternative rejected — have the marker call `focused_framing()` and match on the returned
  framing.* Identifies the winner by value rather than by name; two panes with identical framing
  (trivially reachable: two same-sized panes at identity) would both match. A name is the correct
  key.
  **Doc delta: D23 amended** (the "derived from the **same** resolution rule the verbs consume —
  so the marker names the *fallback* pane too, and can never disagree" clause).

- **D-focused_canvas_indicator-2 — A hairline accent border just inside the pane's content rect,
  not a badge beside the camera picker.** The `.tji` note offered either; this picks the border and
  ships exactly one marker.
  *Rationale:* (i) the border reads **peripherally** — the question "which pane?" is spatial, and a
  rectangle answers it without the user reading anything, which is what makes it usable in the
  half-second before a rail click; (ii) it consumes **no pane area** and no layout, where a badge
  competes for the camera-picker row that already carries `"Camera:"` plus one `SmallButton` per
  shot (`src/app/canvas_view.cpp:252-271`) and would be pushed off-row as shots accumulate;
  (iii) it degrades gracefully on a look-through pane, where `draw_letterboxed` fills the pane with
  black (`src/views/views.cpp:111-115`) and a text badge would need its own contrast handling.
  *Alternative rejected — a text/icon badge.* Cheaper to probe in a test (`ItemInfo` by id rather
  than a pixel read) but worse as an affordance for the reasons above, and it would make the marker
  an ImGui item, weakening Constraint 6's passivity.
  *Alternative rejected — tinting the pane background or dimming the unfocused panes.* D17 already
  reserves "outside dims" for nested-composition isolation scope; reusing dimming for focus would
  overload an established visual language, and tinting the backdrop would perturb every existing
  canvas pixel probe.
  **Doc delta: D23 amended** (the "hairline accent border drawn just inside its rect" clause).

- **D-focused_canvas_indicator-3 — Drawn unconditionally on the marked pane: on a single-canvas
  dock, and on a pane that has not yet composited a frame.**
  *Rationale:* (i) **meaning must not be conditional.** If the border were suppressed below two
  panes, its absence would be ambiguous (no canvas focused? or only one open?) and the user would
  have to learn a second rule; drawing it always makes "bordered pane = where the verb lands" a
  single invariant, and on a one-canvas dock a constant hairline is harmless chrome that teaches
  the affordance before the second canvas exists. (ii) Suppression would require `draw_content` to
  reason about the *pane count*, i.e. more state and a flicker at the moment a second canvas opens
  or closes. (iii) Drawing outside the frame-exists branch (`src/app/canvas_view.cpp:180-244`,
  Constraint 5) means a sized-but-cold pane — which **is** a legitimate framing target the instant
  it is sized — is marked from its first frame, matching the camera picker's own unconditional
  placement at `:246-249`.
  *Alternative rejected — suppress when only one canvas pane is live.* Common in other editors and
  marginally quieter, but it trades a one-pixel line for a conditional invariant, and it is the
  single-canvas case where a new user first learns what the border means.
  **Doc delta: D23 amended** (the "it is drawn on a single-canvas dock too, so … never varies with
  how many canvases are open" clause).

- **D-focused_canvas_indicator-4 — `IM_COL32(120, 200, 255, 255)`, 1.0f thickness, no rounding,
  inset 0.5px inside the content rect, via `ImGui::GetWindowDrawList()->AddRect`.**
  *Rationale:* (i) `(120, 200, 255)` is the established accent in this file — the active camera
  frame (`:296`), the marquee (`:469-470`), and the selection outline (`:497`) all use it, so the
  marker joins an existing visual language rather than introducing a colour; (ii) **alpha 255, not
  a translucent tint**, is chosen deliberately so the pixel lands unblended over whatever the pane
  is showing, which is what makes the e2e's ±8 accent-dominance probe robust under llvmpipe — a
  translucent border would composite against the varying canvas texture and force either a much
  looser tolerance or a content-dependent expectation; (iii) 1.0f/no-rounding/0.5px-inset keeps the
  whole stroke inside the window clip rect so no half-pixel is shaved, and reads as chrome rather
  than as a drawn object. Distinguishability from the 2.0f selection outline at the same colour is
  positional (pane edge vs. object outline) and thickness-based, and D19 already establishes those
  as different signals (selection is project-scoped, the marker is per-pane).
  *Note on the rect:* `pane_origin` (`src/app/canvas_view.cpp:179`) is the **content** origin, so
  the border frames the canvas rather than the window frame — the correct target, since it is the
  canvas the verb acts on, not the tab.
  **No doc delta required** — pixel-level chrome specifics are implementation, not constitution;
  D23's amended clause says "hairline accent border" and stops there.

- **D-focused_canvas_indicator-5 — Add `std::string_view CanvasView::indicated_view_id() const`
  as public API, so the e2e pins the *rule* separately from the *pixels*.** It projects
  `presenters_` into `PaneFraming` rows through the same helper `framing_for()` uses
  (`src/app/canvas_view.cpp:555-567`, extracted into a private `pane_rows()`) and returns
  `focus_target(rows, focused_view_id_)`.
  *Rationale:* this mirrors the predecessor's reason for adding `focused_view_id()` — a pixel
  assertion alone cannot distinguish "the rule is wrong" from "the draw is wrong", and a
  framebuffer probe under software GL is the weaker of the two signals. With the accessor, phases
  3/5/6 assert deterministic state and the pixel probe corroborates. Cost is one `const` accessor
  over UI-thread state; lifetime is documented per Constraint 11.
  *Alternative rejected — infer the marked pane in the test from `focused_framing()`.* Value-keyed,
  ambiguous between identically-framed panes, and it would not exercise `focus_target`'s naming
  path at all.
  *Alternative rejected — cache the resolved target once per frame in a member.* Avoids the
  per-pane re-projection, but adds a frame-ordering hazard (panes are drawn before `reconcile`
  runs, so a cached value is one frame stale for a pane opened this frame) for a saving that does
  not exist: the pane count is single-digit, and `focused_framing()` already performs this exact
  projection once per frame from the rail's enable/disable check. Recomputing per pane is the
  simpler and fresher of the two.
  **No doc delta required.**

- **D-focused_canvas_indicator-6 — Nothing in `dockmodel`, nothing in `dock`, nothing in `views`;
  the marker is L4 `app` chrome end to end.**
  *Rationale:* D-mint_from_focused_canvas-7 already settled that window focus is an ImGui runtime
  fact ImGui owns and mutates on its own, so it is not promoted into L1 `dockmodel` (A11); a marker
  is a *consumer* of that same fact and inherits the same placement. `dock` may depend only on
  `{dockmodel, views, imgui}` (`docs/01-architecture.md:215`) and therefore cannot see
  `CanvasView` at all. Drawing inside `draw_content` — which already runs inside the pane's window
  begun at `src/dock/dock.cpp:577-590` — needs no plumbing, because the view id *is* the window
  name.
  *On D18:* "no privileged editor area" is a **layout** constraint (no reserved slot, no
  keep-a-canvas guardrail). A per-pane marker computed from a rule and free to move to any pane
  grants no pane a layout privilege; it reports where a pointer verb resolves. The `.tji` note
  makes this reading explicit and it is adopted here.
  **Covered by A8/A11/A16 — no new architecture row.**

- **D-focused_canvas_indicator-7 — The new draw site gets a file-local named accent constant; the
  four existing literals are *not* consolidated in this diff.**
  *Rationale:* the four shipped sites carry three different alphas (200, 40, 220, 255) and one is a
  different hue entirely (`:296` is amber `(255, 210, 80)`), so consolidation is not a rename — it
  needs a decomposed RGB triplet plus an alpha-taking helper applied at every site. That is a real
  (if small) refactor with its own golden and pixel-probe verification, disproportionate inside a
  0.5d leaf and orthogonal to it. Registered as `editor.canvas.accent_palette` (see Acceptance
  criteria → Deferred WBS work) rather than smuggled in.
  **No doc delta required.**

- **D-focused_canvas_indicator-8 — No new golden; the goldens' role here is *invariance*.** The
  marker is ImGui vector chrome in the live framebuffer and `render_offline` never sees ImGui
  (A14; §9's golden tier covers "export, canvas composition"). The obligation is therefore
  inverted: the seven existing `tests/goldens/*.rgba8` baselines must pass byte-identically and
  unmodified, which is the assertion that the marker stayed out of the composition path
  (Constraint 9). This follows the precedent set by `mint_from_focused_canvas` ("golden N/A,
  justified — any golden churn in this diff is a **failure signal**, not an update") and by
  `frame_selection`'s use of `cells_insert_nested_64x64` as a byte-invariance oracle.
  **No doc delta required.**

## Open questions

(none — all decided.)

One item is a **product-taste call rather than an engineering one** and is routed to
`tasks/parking-lot.md` (the human-review queue) rather than encoded as a WBS leaf: whether the
marker should be suppressed on a single-canvas dock, where it conveys no disambiguating
information and is arguably chrome noise. D-focused_canvas_indicator-3 makes the defensible call
(always draw, so the invariant never varies) with its rationale on the record; if a human later
prefers the quieter behaviour, it is a one-condition change to the draw block plus one e2e phase,
and the Catch2 rule matrix is unaffected. Nothing downstream is blocked either way.

## Status

**Done** — 2026-07-23.

- `src/app/ace/app/view_framing.hpp`, `src/app/view_framing.cpp` — new pure
  `focus_target()` (~10 lines); `framing_for_focus()` re-expressed on top of it
  (no new includes, purity promise preserved).
- `src/app/ace/app/canvas_view.hpp`, `src/app/canvas_view.cpp` — public
  `indicated_view_id()`, private `pane_rows()` extracted from `framing_for()`,
  file-local accent constants, and the passive hairline-border draw block placed
  before the camera picker (outside the frame-exists branch, per Constraint 5).
- `CMakeLists.txt` — new e2e `tests/focused_canvas_indicator_e2e_test.cpp` added
  to `ace_shell_test`.
- `docs/00-design.md` — D23 amended with the three "that pane is shown, not
  inferred" clauses (D-focused_canvas_indicator-1/2/3).
- `tests/focused_framing_test.cpp` — four new Catch2 cases appended: `focus_target`
  matrix (six inputs including anti-vacuity), consistency property against
  `framing_for_focus`, and matrix non-vacuity assertion; all six pre-existing
  cases unmodified.
- `tests/focused_canvas_indicator_e2e_test.cpp` (new) — six-phase ImGui Test
  Engine e2e `multi_canvas/focused_canvas_indicator` covering single-pane
  indicator, `WindowFocus` tracking, two-sided pixel probe (±8 per channel),
  rail-interaction survival, and fallback-to-canvas#1 after closure.
- `AddRect` draws pixel-grid-snapped corners (`ceil`/`floor` of content rect) so
  the 1px stroke is fully opaque, satisfying D-focused_canvas_indicator-4's
  opaque-pixel requirement and the e2e ±8 accent-dominance probe.
- Tech-debt registered: `editor.canvas.accent_palette` (0.5d) and
  `editor.canvas.view_id_natural_order` (0.5d); both wired into `m9_editor`
  through the `editor.canvas` container dependency.
