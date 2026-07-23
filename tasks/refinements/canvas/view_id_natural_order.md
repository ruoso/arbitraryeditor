# editor.canvas.view_id_natural_order — Order canvas view ids numerically, not lexicographically, in the framing fallback

## TaskJuggler entry

- **Task:** `editor.canvas.view_id_natural_order` — *"Order canvas view ids numerically, not
  lexicographically, in the framing fallback"*, `tasks/00-editor.tji:268-273`, under
  `task canvas "Canvas & rendering"` (`tasks/00-editor.tji:171`).
- **Effort:** `0.5d` · `allocate team`.
- **Depends:** `!focused_canvas_indicator` → `editor.canvas.focused_canvas_indicator`
  (**Done** — `complete 100`, `tasks/00-editor.tji:255-260`).
- **Note (`.tji:272`):** *"framing_for_focus/focus_target's 'lowest-id sized pane' fallback
  consumes panes_by_id in the order std::map<std::string, Presenter, std::less<>> yields
  (lexicographic), so with ten or more canvases open 'canvas#10' sorts before 'canvas#2' and the
  fallback picks the wrong pane. Fix by ordering the projection in CanvasView::framing_for
  (src/app/canvas_view.cpp:555-567) with a natural/numeric-suffix comparator, keeping focus_target
  itself order-agnostic and pure; Catch2 cases covering canvas#2 vs canvas#10 ordering, plus an
  e2e that opens ten panes. Source-of-debt: tasks/refinements/canvas/focused_canvas_indicator.md.
  Design: docs/00-design.md D23, D18."*
- **Note line-number correction.** The note's `src/app/canvas_view.cpp:555-567` citation was
  written before `editor.canvas.focused_canvas_indicator` extracted `pane_rows()` and before
  `editor.canvas.accent_palette` rewrote six literals in the same file. The projection now lives at
  **`src/app/canvas_view.cpp:636-650`** (`CanvasView::pane_rows()`), with `framing_for` at `:652-656`.
  Everything else in the note is current.
- **Back-link:** this refinement lands at `tasks/refinements/canvas/view_id_natural_order.md`.
  **The closer** appends `Refinement: tasks/refinements/canvas/view_id_natural_order.md` to the
  `.tji` note and adds `complete 100` after `allocate team`. **Do not** hand-edit the `.tji` here.
- **Source of debt:** `tasks/refinements/canvas/focused_canvas_indicator.md` — its
  *Acceptance criteria → Deferred WBS work* stanza, which registered this leaf as a
  *"pre-existing defect inherited from `mint_from_focused_canvas`, surfaced (not introduced) by
  this leaf."*
- **Downstream dependents:** none in the WBS today. `editor.panels.overview` and
  `editor.canvas.tool_dispatch` both read the focused/fallback pane through the same accessors and
  inherit the fix for free.
- **Milestone:** `m9_editor` (`tasks/99-milestones.tji:6-8`), reached through the `editor.canvas`
  container dependency.

## Effort estimate

**Half a day.** The production change is one new ~20-line pure function in an existing L1 component
plus one `std::sort` line at its single call site. The bulk of the work is test scaffolding — four
Catch2 cases and one new ten-pane e2e — and correcting the six comments in the tree that currently
assert the false invariant.

- **The defect has exactly one consumer.** A sweep for view-id-*ordered* iteration finds one site:
  `CanvasView::pane_rows()` (`src/app/canvas_view.cpp:636-650`) iterating `presenters_`
  (`src/app/ace/app/canvas_view.hpp:277`). Everything else that looks similar is order-independent
  by construction: `ViewRegistry::adopt` takes a **max**, not a first (`view_registry.cpp:130-140`);
  `ViewRegistry::open`'s default target is the **last leaf in structural pre-order**, not id order
  (`:117-121`, `dockmodel.cpp:174-180`); `tool_rail.cpp:35-47` is a membership test;
  `workspaces.cpp:317-318` sorts preset *names*, not view ids. There is no sweep to do.
- **The id grammar is already parsed, in one place.** `parse_view_id`
  (`src/dockmodel/view_registry.cpp:62-86`, contract `view_registry.hpp:55-60`) already turns
  `slug#N` into `{ViewType, int}` and already rejects the malformed forms. The comparator is a
  projection of a function that exists, not new parsing.
- **No new DAG edge and no new build file entry.** `app` may already include `<ace/dockmodel/...>`
  (`scripts/check_levels.py:21-39`, `ALLOWED["app"]` lists `dockmodel`) and already does
  (`src/app/shell.cpp:8-10`). `ace_component()` `file(GLOB)`s `src/dockmodel/*.cpp`
  (`CMakeLists.txt:153-167`), so adding to the existing `view_registry.cpp` needs no CMake edit,
  and `tests/view_registry_test.cpp` is already in `ace_tests` (`CMakeLists.txt:226`), which
  already links `ace::dockmodel` (`:246-248`).
- **The e2e is the real cost.** `tests/multi_canvas_mint_e2e_test.cpp`'s `settle()` (`:127-141`) is
  hardcoded to two pane ids and must be generalized; ten panes must each be brought to the front
  once before the assertion is non-vacuous (see Constraint 6). Budget most of the half-day here.

New code: `bool view_id_less(std::string_view, std::string_view)` declared in
`src/dockmodel/ace/dockmodel/view_registry.hpp` (next to `parse_view_id`, `:60`) and defined in
`src/dockmodel/view_registry.cpp`; one `std::sort` call plus a corrected comment in
`CanvasView::pane_rows()` (`src/app/canvas_view.cpp:636-650`); contract-text corrections in
`src/app/ace/app/view_framing.hpp` and `src/app/ace/app/canvas_view.hpp`; four Catch2 cases
appended to `tests/view_registry_test.cpp`; one Catch2 case appended to
`tests/focused_framing_test.cpp`; one new `tests/view_id_natural_order_e2e_test.cpp` added to the
`ace_shell_test` source list (`CMakeLists.txt:259-284`). **No new component, no new DAG edge, no
new external dependency, no libarbc change, no `render` change, no `views` change, no `dock`
change, no change to `src/app/view_framing.cpp`'s executable code, no golden re-baseline.** One
doc delta: **D23 amended** (see **D-view_id_natural_order-2**).

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.canvas.focused_canvas_indicator`** (**Done**, `complete 100`) —
  `tasks/refinements/canvas/focused_canvas_indicator.md`. Consumed:
  - **D-focused_canvas_indicator-1** — *"`framing_for_focus` must be implemented in terms of
    `focus_target`, so a future change to the resolution rule cannot move the verb without moving
    the marker. Two parallel implementations of 'which pane wins' is the defect class this leaf
    exists to foreclose."* This leaf inherits that structure and must not weaken it: the ordering
    fix lands **outside** both functions, so there remains exactly one resolution rule and it stays
    id-format-agnostic. The consistency property test (`tests/focused_framing_test.cpp:261`)
    passes unmodified.
  - **D-focused_canvas_indicator-5** — `indicated_view_id()` re-projects `presenters_` through
    `pane_rows()` **per pane, per frame**, and caching was explicitly rejected on frame-staleness
    grounds. This leaf inherits that cost model and adds a `std::sort` to it; see
    **D-view_id_natural_order-4** for the accounting and for why caching is still rejected.
  - Its **Constraint 3** — `view_framing.{hpp,cpp}` must stay ImGui-free, GL-free and
    `CanvasView`-free. This leaf additionally keeps it **`dockmodel`-free** (Constraint 3 below).
- **`editor.cameras.mint_from_focused_canvas`** (**Done**) —
  `tasks/refinements/cameras/mint_from_focused_canvas.md`. It authored the lowest-id fallback that
  this leaf corrects (its **D-mint_from_focused_canvas-6**, quoted in the shipped comment at
  `src/app/canvas_view.cpp:658-661`: *"the first (lowest-id) live, sized pane — a deterministic
  choice, not the most-recently-drawn one"*). The **intent** — determinism over recency — is
  preserved exactly; only the order that realizes it changes.
- **`editor.dock.view_registry`** (**Done**) — `mint_id` (`src/dockmodel/view_registry.cpp:88-95`)
  and its **D-view-registry-4** monotonic, never-recycling per-type counter. Consumed as the
  guarantee that a canvas id is *always* `canvas#N` with N ≥ 1, decimal, no leading zeros
  (`parse_index`, `:27-41`), and that N genuinely reaches ≥ 10 in a long session or a restored
  workspace (`adopt`, `:130-140`).
- **`editor.canvas.accent_palette`** (**Done**) — not a dependency, but it shipped into the same
  file after this leaf's `.tji` note was written and moved the line numbers the note cites. Its
  only relevance is that citation correction, recorded above.

**Pending (owned here):** nothing. Every predecessor is `complete 100`.

## What this task is

1. **Give `dockmodel` a canonical total order over view ids** —
   `bool view_id_less(std::string_view, std::string_view)` beside `parse_view_id`, ordering by
   (view type in catalog order, then instance index ascending), with unparseable strings sorting
   after every well-formed id, by bytes (**D-view_id_natural_order-1**,
   **D-view_id_natural_order-3**).
2. **Apply it at the one site that needs it** — sort `CanvasView::pane_rows()`'s projection with
   `view_id_less` before handing it to the rule, and correct the comment that currently claims
   `presenters_`'s own order is sufficient (**D-view_id_natural_order-4**).
3. **Leave `focus_target` / `framing_for_focus` untouched, and pin that they are untouched** —
   the rule keeps consuming the caller's span order verbatim, and a new Catch2 case asserts it
   *does not* re-order, so a future implementer cannot install a second ordering authority inside
   the rule (**D-view_id_natural_order-5**).
4. **Correct the contract text.** Six comments in the tree assert the false invariant
   (`canvas_view.cpp:637-639`, `canvas_view.hpp:165`, `:257`, `:262`, `view_framing.hpp:41-45`,
   `:55-57`/`:63-70`). Each must now name `view_id_less` as the ordering obligation rather than
   claim the map comparator supplies it.
5. **Prove it end to end** — an ImGui Test Engine e2e that opens ten canvases, sizes all ten, and
   asserts the fallback lands on `canvas#2` rather than `canvas#10`
   (**D-view_id_natural_order-6**).
6. **Amend D23** so "lowest id" is normatively numeric rather than left ambiguous
   (**D-view_id_natural_order-2**).

Out of scope, by inheritance and by charter: the resolution *rule* itself (owned by
`focused_canvas_indicator`), focus *tracking* (owned by `mint_from_focused_canvas`), the id
*grammar* and the minting counter (owned by `editor.dock.view_registry`), and any change to
`presenters_`'s key type or map comparator (**D-view_id_natural_order-4**, alternative rejected).

## Why it needs to be done

D23 promises that a framing-derived verb — "new shot from view" (§3), and by A16 a cell insert's
provisional placement — lands on *"the focused one … falling back to the lowest-id live pane when
no canvas has yet held focus."* Since `editor.canvas.focused_canvas_indicator`, D23 additionally
promises that this pane is **shown**, with a hairline accent border *"derived from the same
resolution rule the verbs consume."*

Both promises are currently broken past nine canvases. `presenters_`
(`src/app/ace/app/canvas_view.hpp:277`) is a `std::map<std::string, Presenter, std::less<>>`, so it
yields byte-lexicographic order: `canvas#1, canvas#10, canvas#11, …, canvas#2`. `pane_rows()`
(`src/app/canvas_view.cpp:636-650`) hands that order straight to `focus_target`
(`src/app/view_framing.cpp:14-45`), whose fallback loop (`:29-34`) takes the **first sized pane in
span order**. So in the state where the hint is empty or stale and `canvas#1` is gone or unsized,
the verb acts on `canvas#10` and the accent border is drawn on `canvas#10` — while D23, the
header contract (`view_framing.hpp:44`, *"the lowest-id live pane"*), and the shipped comment at
`canvas_view.cpp:638` (*"so 'canvas#1' wins the fallback over 'canvas#2'"*) all say `canvas#2`.

This is reachable, not theoretical. The counter is monotonic and never recycles
(`view_registry.hpp:95-100`), and `adopt` (`view_registry.cpp:130-140`) re-seeds it from a restored
workspace — so a session that has opened and closed canvases across a few project loads arrives at
`canvas#10` with a handful of panes actually open. And the failure is silent: the border is drawn
on a real pane, so the user sees a confident, wrong answer rather than an error.

The existing coverage cannot catch it. `tests/focused_framing_test.cpp`'s fixture matrix
(`:158-227`) tops out at three panes, `canvas#1/#2/#3`, whose lexicographic and numeric orders
agree — including the fixture at `:180-185` named *"an empty hint takes the lowest-id SIZED pane,
not the first row"*, which is exactly the assertion the defect violates and exactly the fixture too
small to see it.

## Inputs / context

**Design docs (normative — the constitution).**

- **`docs/00-design.md` D23** (`:490`) — the framing-derived-verb rule: *"the focused one … falling
  back to the lowest-id live pane when no canvas has yet held focus"* and *"That pane is shown, not
  inferred."* **Amended by this leaf** (**D-view_id_natural_order-2**).
- **`docs/00-design.md` D18** (`:485`) — the uniform dockspace: *"Canvas is a view → multiple
  canvases through different cameras side by side"*, with **no keep-a-canvas guardrail**. D18 is
  why more than nine canvases is a state the design permits and why the fallback may find zero
  sized panes; it states no ordering, so it is context here, not a constraint.
- **`docs/01-architecture.md` §8** (`:255-291`) — the levelization DAG. `dockmodel` is **L1** (*may
  depend on `base`, `platform`; ImGui/GL: **no***); `app` is **L4** (*may depend on everything*).
  §8's closing rule: all of L1 is the testable core and none of it may include ImGui/GL/SDL.
- **`docs/01-architecture.md` §9** (`:293-320`) and **§9.1** (`:322-357`) — the DoD and the
  `clang-asan` lane (SDL3 `offscreen` + Mesa `llvmpipe`, suppressions in `tests/lsan.supp`).
- **`docs/01-architecture.md` A8** — the testability seam that makes the levelization a
  compile-time invariant; this leaf's whole placement argument is an A8 argument.

**Sources to change.**

- `src/dockmodel/ace/dockmodel/view_registry.hpp:47-60` — `ParsedViewId` and `parse_view_id`'s
  contract; the new declaration goes immediately after `:60`.
- `src/dockmodel/view_registry.cpp:27-41` (`parse_index`), `:62-86` (`parse_view_id`),
  `:88-95` (`mint_id`) — the id grammar the comparator projects. Definition goes after `:86`.
- `src/app/canvas_view.cpp:636-650` (`CanvasView::pane_rows()`) — the one call site. Its comment at
  `:637-640` currently asserts the false invariant and must be corrected.
- `src/app/ace/app/canvas_view.hpp:277` (`presenters_`), `:165` (accessor comment), `:257`, `:262`
  (`pane_rows()` / `framing_for` declarations) — contract text.
- `src/app/ace/app/view_framing.hpp:41-45`, `:55-57`, `:63-70` — the "caller passes them view-id
  ordered, so that is the lowest-id live pane" contract, which must now name `view_id_less`.

**Read-only context (must not change).**

- `src/app/view_framing.cpp:8-12` (`sized()`), `:14-45` (`focus_target`), `:47-56`
  (`framing_for_focus`) — executable code untouched by this leaf.
- `src/app/canvas_view.cpp:95-97` — the degenerate-pane early-out; with `sized()` above it is why
  a pane that has never been fronted is invisible to the fallback (Constraint 6).
- `src/app/canvas_view.cpp:110-113` (sticky-hint write), `:117-119` (lazy presenter creation),
  `:606-614` (hint clear on close) — the mechanics the e2e drives.
- `scripts/check_levels.py:21-39` (`ALLOWED`), `:44-49` (`EXTERNAL_ALLOWED`), `:56`
  (`ACE_INCLUDE_RE`), `:59-67` (`closure`) — the lint this leaf must stay clean under.

**Tests that must keep passing, unmodified.**

- `tests/focused_framing_test.cpp` — all ten existing cases (`:36`, `:55`, `:71`, `:92`, `:107`,
  `:120`, `:240`, `:247`, `:261`, `:287`), including the consistency property and the
  matrix-non-vacuity assertion. This leaf **appends** one case; it edits none.
- `tests/view_registry_test.cpp` — all seven existing cases (`:27`, `:75`, `:88`, `:114`, `:139`,
  `:163`, `:184`). Appends four; edits none.
- `tests/focused_canvas_indicator_e2e_test.cpp`, `tests/multi_canvas_mint_e2e_test.cpp`,
  `tests/multi_canvas_e2e_test.cpp`, `tests/new_shot_from_view_e2e_test.cpp`,
  `tests/canvas_nav_e2e_test.cpp`, `tests/cells_insert_e2e_test.cpp`,
  `tests/app_project_gateway_test.cpp:674` — all pass with no edits. Every one of them uses
  single-digit canvas ids, where the old and new orders agree, so a green run there is a
  no-behaviour-change proof.
- The seven goldens in `tests/goldens/` — byte-identical.

**Test rigs to borrow.**

- `tests/multi_canvas_mint_e2e_test.cpp` — `ScratchDir` (`:80-91`), `E2EState` `UserData`
  (`:105-111`), `pump_until` (`:113-123`), `settle` (`:127-141`, **two-pane hardcoded**),
  `layout_contains` (`:153-156`), the boot block (`:195-267`, `opts.headless`, 900×640,
  `register_view_body`, `gateway.set_view_framing` at `:246`, `set_draw_content` at `:249-252`),
  the rail-open idiom (`:277-291`), and the drive/teardown loop (`:523-548`,
  `k_max_frames = 200000`).
- `tests/multi_canvas_e2e_test.cpp:296-301` — the `dockspace.close(id)` + reconcile-drain idiom.
- `tests/focused_canvas_indicator_e2e_test.cpp:197`, `:259` — the
  `IM_REGISTER_TEST(engine, "multi_canvas", …)` registration shape.

**Predecessor refinements.** `tasks/refinements/canvas/focused_canvas_indicator.md`,
`tasks/refinements/cameras/mint_from_focused_canvas.md`.

## Constraints / requirements

1. **`focus_target` and `framing_for_focus` keep their current bodies, byte for byte.** The
   ordering obligation belongs to the caller; the rule stays order-agnostic and id-format-agnostic.
   Inherited from D-focused_canvas_indicator-1: one resolution rule, not two.
2. **The observable behaviour for one-through-nine canvases must not change.** Lexicographic and
   numeric order agree there, so every existing e2e and every existing Catch2 case must pass
   unmodified. Any edit to an existing test in this diff is a failure signal, not an update.
3. **`view_framing.{hpp,cpp}` gains no include.** It must not acquire
   `<ace/dockmodel/view_registry.hpp>`, `<imgui.h>`, GL, or `<ace/app/canvas_view.hpp>` — the
   predecessor's Constraint 3 extended by one entry. A reviewer can confirm this half of the leaf
   against the include diff.
4. **`src/dockmodel/` gains no `#include <ace/...>` beyond `base`/`platform`/its own** and no
   external include. `view_id_less` needs only `<optional>`, `<string_view>` and `<tuple>`;
   `dockmodel` appears in no `EXTERNAL_ALLOWED` set (`scripts/check_levels.py:44-49`) and must
   stay that way.
5. **`view_id_less` must be a strict weak ordering, by construction rather than by inspection.**
   Implement it as a lexicographic comparison of a key tuple (see
   **D-view_id_natural_order-3**), so its SWO-ness follows from `std::tuple`'s, and pin that with
   a test. A hand-rolled branch cascade that is subtly not an SWO is undefined behaviour inside
   `std::sort`, i.e. strictly worse than the bug being fixed.
6. **The ten-pane e2e must size every pane, and must be shown to fail before the fix.** Nine of
   ten tabs share one dock leaf and only the active tab runs `draw_content`
   (`src/app/canvas_view.cpp:95-97`), so a pane never brought to the front keeps
   `requested_width/height == 0` and is invisible to `sized()` (`src/app/view_framing.cpp:8-12`).
   A ten-pane test that skips the fronting pass passes on the **unfixed** tree and proves nothing.
7. **`presenters_`'s declared type and comparator do not change.** `std::less<>` is load-bearing
   for the four heterogeneous `find(std::string_view)` lookups (`src/app/canvas_view.cpp:118`,
   `:552`, `:558`, `:632`); see **D-view_id_natural_order-4** for why the ordering is not moved
   into the container.
8. **Minted ids are not re-formatted.** No zero padding, no id rewrite, no counter change — ids
   are persisted in workspace files (`src/dockmodel/workspaces.cpp:249-253` seeds `"canvas#1"`)
   and `parse_index` explicitly rejects leading zeros (`src/dockmodel/view_registry.cpp:30-32`).
9. **The comparator is defined for *every* `std::string_view`, including strings that are not view
   ids.** It is a `Compare` handed to `std::sort`; it may not have a precondition that a caller
   could violate. Unparseable inputs get a defined, tested position rather than an assert.
10. **No new threading surface.** `view_id_less` is pure; `pane_rows()` runs on the UI thread over
    UI-owned state. Nothing new crosses the UI↔writer handoff.
11. **`scripts/gate` green** (check_levels · clang-format · build · ctest) is the umbrella; the
    doc delta rides in the same commit as the code (README ritual step 1).

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate` green is the
umbrella.

- **Levelization (`check_levels` clean) — a positive claim, not just a silent pass.** No new
  component and no new DAG edge: `app → dockmodel` is already in `ALLOWED["app"]`
  (`scripts/check_levels.py:21-39`) and already exercised at `src/app/shell.cpp:8-10`. The
  reviewable assertions are the *negative* ones from Constraints 3 and 4 — `src/dockmodel/*`
  acquires no include outside `base`/`platform`/itself and no external include, and
  `src/app/ace/app/view_framing.hpp` / `src/app/view_framing.cpp` acquire **no new include at
  all**. The whole levelization story is confirmable from the include diff.

- **L1 logic — Catch2, headless, in `tests/view_registry_test.cpp`** (already in `ace_tests`,
  `CMakeLists.txt:226`; no CMake edit). Four cases appended, matching the file's
  `"view_registry: <lowercase behavioural sentence>"` naming and its plain `CHECK`/`REQUIRE` style
  (no `SECTION`s):
  - `"view_registry: view_id_less orders instance indices numerically, not lexicographically"` —
    the headline. `view_id_less("canvas#2", "canvas#10")` is **true** and
    `view_id_less("canvas#10", "canvas#2")` is **false**; likewise `#9` before `#10`, `#1` before
    `#2`, `#99` before `#100`. Then the whole-set form: `std::sort` over
    `{canvas#10, canvas#3, canvas#1, canvas#21, canvas#2, canvas#11}` yields exactly
    `{canvas#1, canvas#2, canvas#3, canvas#10, canvas#11, canvas#21}`.
  - `"view_registry: view_id_less groups by view type in catalog order"` — every `canvas#N`
    precedes every singleton slug; singletons follow `view_catalog()`'s order
    (`src/dockmodel/view_registry.cpp:14-23`), asserted by sorting the eight slugs from
    `view_catalog()` and checking the result equals the catalog sequence. Pins the order against
    the catalog rather than against a hand-copied list.
  - `"view_registry: view_id_less puts unparseable ids after every parseable one, ordered by
    bytes"` — the four rejection classes `parse_view_id` already defines
    (`src/dockmodel/view_registry.cpp:62-86`): a bare multi-instance slug (`"canvas"`), a `#`
    suffix on a singleton (`"layers#2"`), a zero / leading-zero / non-numeric index (`"canvas#0"`,
    `"canvas#01"`, `"canvas#x"`), and an unknown slug (`"bogus#1"`, `""`). Each sorts **after**
    `"export"` (the last catalog slug), and among themselves by `std::string_view::operator<`.
  - `"view_registry: view_id_less is a strict weak ordering"` — over a fixed ~16-element corpus
    spanning both parseable and unparseable ids plus a duplicate, an exhaustive check of
    irreflexivity (`!less(a,a)`), asymmetry (`less(a,b)` ⇒ `!less(b,a)`), transitivity of `<`, and
    transitivity of incomparability, by triple loop. Plus a determinism assertion: sorting two
    different input permutations of the corpus yields the identical sequence. This is Constraint 5
    made mechanical.
  - **Anti-vacuity.** The first case must fail on the pre-fix tree by construction (there is no
    pre-fix `view_id_less`); the implementer confirms the *e2e* fails pre-fix per the bullet below.

- **The rule stays order-agnostic — one case appended to `tests/focused_framing_test.cpp`**
  (already in `ace_shell_test`, `CMakeLists.txt:276`; no CMake edit), matching its untagged
  `TEST_CASE("<rule_name>: <assertion in prose>")` style and reusing its `cam()` helper (`:26-33`):
  - `"focus_target: consumes the caller's span order verbatim and never re-orders it"` — pass
    `{{"canvas#10", sized}, {"canvas#2", sized}}` **in that span order** with an empty hint and
    assert the result is `"canvas#10"`, and that `framing_for_focus` returns `canvas#10`'s framing.
    This case deliberately asserts the *un*-sorted answer: it is the anti-regression that keeps the
    ordering authority in exactly one place (Constraint 1, inherited D-focused_canvas_indicator-1).
    A future implementer who "fixes the bug again" inside `focus_target` breaks this case, which is
    the point. Its comment must say so, so it is not mistaken for a stale expectation.
  - **Regression:** the ten pre-existing cases pass **unmodified**.

- **Rendered output — golden: N/A, justified, with a positive obligation.** This leaf changes which
  *pane* a verb targets in a ten-canvas fallback state; it changes no pixel any composition
  renders and touches no code `render_offline` executes. The obligation instead: the seven goldens
  in `tests/goldens/` pass **byte-identically and unmodified**. Golden churn in this diff means
  something reached the composition path that should not have — a failure signal, not an update.

- **UI e2e — ImGui Test Engine.** New `tests/view_id_natural_order_e2e_test.cpp`, added to the
  `ace_shell_test` source list (`CMakeLists.txt:259-284`), registering
  `IM_REGISTER_TEST(engine, "multi_canvas", "view_id_natural_order")`. It borrows the
  `tests/multi_canvas_mint_e2e_test.cpp` rig wholesale (`ScratchDir` `:80-91`, `E2EState`
  `:105-111`, `pump_until` `:113-123`, `layout_contains` `:153-156`, boot `:195-267`, drive
  loop `:523-548`) with `settle()` generalized to sum `frames_issued` over a caller-supplied id
  list rather than the hardcoded pair at `:127-141`. **State assertions are on
  `canvas.indicated_view_id()` and `canvas.focused_framing()`; there is no pixel probe** — the
  ordering is a naming question, and the predecessor already owns the marker's pixels. Phases:
  1. **Boot with one canvas.** `opts.headless = true`; `pump_until(frames_issued("canvas#1") >= 1)`.
     The shell size may be raised above the rig's 900×640 if tab-bar overflow makes `WindowFocus`
     unreliable at ten tabs — nothing in this leaf depends on the window size.
  2. **Open nine more.** Nine `ctx->ItemClick((rail + "/Canvas").c_str())` with
     `rail = ace::dock::tool_rail_title()`, `ctx->Yield(3)` between
     (`tests/multi_canvas_mint_e2e_test.cpp:277-285`). Assert
     `layout_contains(dockspace, "canvas#N")` for N = 1…10 — this also pins that the monotonic
     counter really reaches 10.
  3. **Size all ten (the anti-vacuity phase, Constraint 6).** For N = 2…10:
     `ctx->WindowFocus(("canvas#" + std::to_string(N)).c_str())` then
     `IM_CHECK(pump_until(ctx, [&]{ return canvas.frames_issued(id) >= 1; }))`. Assert afterwards
     that `canvas.focused_view_id() == "canvas#10"` — proof that every pane in 2…10 actually drew
     and stamped the hint, so all ten presenters are sized.
  4. **Arm the fallback.** `ctx->WindowFocus("canvas#1")`;
     `pump_until(canvas.focused_view_id() == "canvas#1")`; then `dockspace.close("canvas#1")` plus
     the reconcile drain (`tests/multi_canvas_e2e_test.cpp:296-301`), so `reconcile` clears the
     hint (`src/app/canvas_view.cpp:606-614`) and erases `canvas#1`'s presenter.
     `IM_CHECK(canvas.focused_view_id().empty())` — the fallback branch is now the live branch,
     over nine sized panes `canvas#2 … canvas#10`.
  5. **The assertion this leaf exists for.** `IM_CHECK(canvas.indicated_view_id() == "canvas#2")`.
     On the pre-fix tree this is `"canvas#10"`. Corroborate through the verb-facing accessor too:
     `canvas.focused_framing()` must equal `canvas#2`'s pane size, not `canvas#10`'s — which is
     what proves `framing_for_focus` moved with `focus_target` (Constraint 1).
  6. **The order is total, not one lucky comparison.** Close `canvas#2` the same way and assert
     `canvas.indicated_view_id() == "canvas#3"` — not `"canvas#10"`. A comparator that special-cases
     only the `#1`/`#10` pair, or a `min_element` that happened to fire once, fails here.
  - **Pre-fix failure check (required, and recorded in the Status block).** The implementer runs
    the new e2e and the new `view_id_less` ordering case against the tree with the `std::sort` line
    removed and confirms phase 5 reports `canvas#10`. An e2e that passes both before and after is
    not evidence.

- **Threading (ASan/TSan) — no new case and no new lane, stated positively.** `view_id_less` is a
  pure function over two `string_view`s with no state; `pane_rows()` and its new `std::sort` run
  on the UI thread over UI-thread-owned `presenters_`. Nothing new crosses the UI↔writer handoff
  (`docs/01-architecture.md` A4.1). Both new test files land in targets that already run in the
  `clang-asan` lane (§9.1, SDL3 `offscreen` + Mesa `llvmpipe`) and must be clean there with **no
  new `tests/lsan.supp` entry**. The ten-pane e2e must also complete within the rig's
  `k_max_frames = 200000` budget (`tests/multi_canvas_mint_e2e_test.cpp:523-548`) under ASan — if
  it does not, the fix is fewer settle iterations, not a raised cap.

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed lines; clang-format +
  build clean. `view_id_less`'s every branch — both-parse, one-parses, neither-parses — is covered
  by the four Catch2 cases; the single `std::sort` line in `pane_rows()` is covered by the new e2e
  and by every existing canvas e2e. The remaining diff is comments and one header declaration.
  Tests ship with the task.

- **Doc delta (same commit).** `docs/00-design.md` D23 (`:490`) amended with the *"'Lowest id'
  means numerically lowest instance index"* clause — carried by **D-view_id_natural_order-2**. No
  `docs/01-architecture.md` row changes: §8 already grants `app → dockmodel`, and §9 already
  governs the test tiers.

- **Deferred WBS work: none.** This leaf is self-contained and closes its own debt. Explicitly
  *not* registered, with reasons: (i) **consolidating the duplicated e2e helpers** (`pump_until`,
  `settle`, `ScratchDir`, `E2EState` are copied across six e2e files) is test hygiene with no
  behavioural risk and no owner asking for it — speculative structure, per the predecessor's
  no-"revisit"-task rule; (ii) **applying `view_id_less` anywhere else** has no second consumer —
  the sweep in *Effort estimate* found exactly one view-id-ordered iteration in the tree, and
  every other "first"/"lowest" site is order-independent by construction or ordered on something
  that is not a view id; (iii) **the `presenters_` container change** is a rejected alternative
  (**D-view_id_natural_order-4**), not deferred work. Everything else out of scope already has an
  owner: the resolution rule is `editor.canvas.focused_canvas_indicator` (shipped), focus tracking
  is `editor.cameras.mint_from_focused_canvas` (shipped), the id grammar is
  `editor.dock.view_registry` (shipped), and tool→interaction dispatch chrome is
  `editor.canvas.tool_dispatch` (A11).

## Decisions

- **D-view_id_natural_order-1 — The comparator lands in `ace::dockmodel` (L1) as a free
  `bool view_id_less(std::string_view, std::string_view)` beside `parse_view_id`, not as a lambda
  in `canvas_view.cpp`.**
  *Rationale:* (i) `dockmodel` already owns the view-id grammar in one place — `mint_id`
  (`src/dockmodel/view_registry.cpp:88-95`) writes it, `parse_view_id` (`:62-86`) reads it, and a
  comparator is a third projection of the same grammar; putting the "which id is lower" knowledge
  anywhere else creates a second authority on `slug#N` that can drift from the first. (ii) It is
  the placement that buys **L1 Catch2 coverage**, which is what §9 calls "the bulk". `dockmodel` is
  L1 and `tests/view_registry_test.cpp` is already in `ace_tests` (`CMakeLists.txt:226`, linking
  `ace::dockmodel` at `:246-248`) — headless, no ImGui, no GL, no shell boot. (iii) `app` already
  depends on `dockmodel` (`scripts/check_levels.py:21-39`; `src/app/shell.cpp:8-10`), so this is a
  zero-cost placement: no new edge, no new component, no CMake edit (`ace_component()` globs
  `src/dockmodel/*.cpp`, `CMakeLists.txt:153-167`).
  *Alternative rejected — a file-local lambda or anonymous-namespace comparator in
  `src/app/canvas_view.cpp`.* This is the literal reading of the `.tji` note ("ordering the
  projection in `CanvasView::framing_for` with a natural/numeric-suffix comparator"), and it is one
  file smaller. Rejected because an anonymous-namespace symbol in an L4 TU **cannot be linked by
  any test**: the only available proof would be the ten-pane e2e, so a comparator with three
  branches and an SWO obligation (Constraint 5) would ship with a single end-to-end assertion and
  no unit coverage, against a 90% diff-coverage gate. The note's *intent* — "keep `focus_target`
  order-agnostic and pure, fix it at the projection" — is honoured exactly; this decision only
  chooses where the comparator's **definition** lives, and the call site is still `pane_rows()`.
  *Alternative rejected — put it in `src/app/ace/app/view_framing.hpp` next to the rule it serves.*
  Closest to the code that needs it, but it would pull `<ace/dockmodel/view_registry.hpp>` into a
  header the predecessor deliberately kept dependency-free (its Constraint 3), and it would make
  the *rule's* header depend on the *id grammar* — the exact coupling Constraint 1 exists to
  prevent. It would also strand the tests in `ace_shell_test`, which boots a shell, for logic that
  needs none.
  **Covered by §8's existing `app → dockmodel` edge — no new architecture row.**

- **D-view_id_natural_order-2 — D23 is amended to make "lowest id" normatively numeric, rather
  than treating the fix as a silent bug repair.**
  *Rationale:* (i) D23's *"falling back to the lowest-id live pane"* is genuinely ambiguous
  between byte order and index order, and the implementation picked one reading while every comment
  in the tree asserted the other (`src/app/canvas_view.cpp:638`, `view_framing.hpp:44`) — an
  ambiguity that produced a shipped defect is exactly what the constitution exists to remove.
  (ii) The clause now defines a **total order over view ids**, including the unparseable tail, so
  any future "first live pane" rule inherits a decided answer instead of re-deriving one.
  (iii) The predecessor set the precedent of amending D23 in place for a clarification of its own
  fallback clause rather than minting a new row.
  *Alternative rejected — a new `D24` row, "canonical view-id order".* A new row for a
  one-sentence clarification of an existing row's own rule fragments the fallback story across two
  places, so a reader of D23 could still take the byte reading. Amendment keeps one row answering
  one question.
  *Alternative rejected — no doc delta; treat it purely as a bug.* Defensible, since D23 arguably
  always meant the numeric reading. Rejected because this leaf does not merely fix a comparison —
  it **chooses** the type-then-index grouping and the position of unparseable ids
  (**D-view_id_natural_order-3**), and those are normative choices a future leaf would otherwise be
  free to contradict.
  **Doc delta: D23 amended** (`docs/00-design.md:490`, the *"'Lowest id' means numerically lowest
  instance index, not byte-lexicographically lowest"* clause).

- **D-view_id_natural_order-3 — The order is (view type in catalog order, then instance index
  ascending), with unparseable strings last by bytes, expressed as a lexicographic comparison of a
  key tuple.**
  *Rationale:* (i) The key is
  `parse_view_id(id) ? std::tuple{0, int(type), index, std::string_view{}} : std::tuple{1, 0, 0, id}`,
  compared with `std::tuple::operator<`. SWO-ness then follows from `std::tuple`'s rather than from
  reviewing a branch cascade — Constraint 5 satisfied by construction, which matters because a
  non-SWO `Compare` is undefined behaviour inside `std::sort`, i.e. a worse failure than the bug
  being fixed. (ii) **Catalog order** (the `ViewType` enumerator, `view_registry.hpp:19`, whose
  header states *"the enumerator order is the catalog order"*) is chosen over slug bytes for the
  type key because the catalog is already the shell's one canonical view ordering — the launcher
  reads it — so a second, alphabetical notion of "view order" would be a divergence with no
  consumer. For `presenters_`, which holds only `Canvas` ids (Canvas is the sole multi-instance
  type, `view_registry.cpp:15`), the two are indistinguishable anyway; the choice only matters for
  future callers. (iii) **Unparseable last** rather than first: a well-formed id names a view the
  shell can actually reason about, so a "first live pane" rule biased toward well-formed ids
  degrades more gracefully against a hand-edited or corrupt workspace file. The discriminator being
  the tuple's leading element makes that a one-line, testable property.
  *Alternative rejected — parse the trailing digit run generically (a `strverscmp`-style natural
  sort) without consulting `parse_view_id`.* More general and independent of the catalog, but it
  invents a **second** definition of what a view id is, sitting next to `parse_view_id`'s and free
  to disagree with it — for instance on `"canvas#01"`, which `parse_index` (`:27-41`) deliberately
  rejects but a generic natural sort would happily read as 1 and tie with `"canvas#1"`. Reusing the
  existing parser means there is exactly one answer to "is this a view id, and what index is it".
  *Alternative rejected — assert / UB on unparseable input.* Violates Constraint 9: a `Compare`
  handed to `std::sort` must be total over its domain, and `pane_rows()` gets its keys from the
  dock layout, which can be restored from a file on disk.
  **No doc delta beyond D-view_id_natural_order-2's** (the amended D23 clause states this order).

- **D-view_id_natural_order-4 — Sort the projection inside `pane_rows()`; leave `presenters_` a
  `std::map<std::string, Presenter, std::less<>>`.**
  *Rationale:* (i) `std::less<>` is load-bearing: four heterogeneous `find(std::string_view)`
  lookups depend on transparent comparison (`src/app/canvas_view.cpp:118`, `:552`, `:558`, `:632`).
  (ii) Changing the container's comparator changes its ordering invariant for **every** iteration
  of `presenters_`, not just the projection — a blast radius far wider than the one consumer this
  defect has, for no additional benefit. (iii) A comparator embedded in a `std::map`'s type is the
  worst place for an SWO bug: it corrupts the container silently, whereas a `std::sort` on a
  freshly built vector fails loudly and locally. (iv) The cost is negligible and bounded:
  `pane_rows()` builds a vector of at most a handful of rows, and `indicated_view_id()` calls it
  once per pane per frame (inherited D-focused_canvas_indicator-5) — ten panes is ~10 sorts of ten
  elements per frame, a few hundred `string_view` comparisons, against a frame that composites and
  uploads ten canvas textures.
  *Alternative rejected — a transparent natural-order comparator as the map's `Compare`.* It would
  make the ordering an invariant of the container rather than a step a future caller could forget,
  which is genuinely attractive. Rejected on (i)–(iii): it must be written to preserve heterogeneous
  lookup, it silently re-orders every other iteration site, and it converts a local sort bug into
  container UB. The forgettable-step risk is instead answered by the contract text (What this task
  is, item 4) and by the `focus_target` order-agnosticism case, which documents whose job the
  ordering is.
  *Alternative rejected — replace `presenters_` with an insertion-ordered vector.* Attractive
  because "mint order" sounds like what D23 wants. Rejected because it is **not** mint order:
  presenters are created lazily on a pane's first `draw_content` (`src/app/canvas_view.cpp:117-119`),
  so the sequence is *first-draw* order — after a workspace restore, tab-activation order. That is
  precisely the arbitrary, session-dependent choice D23's deterministic lowest-id rule
  (D-mint_from_focused_canvas-6) exists to avoid.
  *Alternative rejected — cache the sorted projection once per frame.* Rejected for the same reason
  the predecessor rejected caching the resolved target: panes are drawn before `reconcile` runs, so
  a cached projection is one frame stale for a pane opened this frame — a correctness hazard traded
  for a saving that does not exist at single- to low-double-digit pane counts.
  **No doc delta required.**

- **D-view_id_natural_order-5 — `tests/focused_framing_test.cpp` gains a case asserting
  `focus_target` returns `canvas#10` for the span order `{canvas#10, canvas#2}` — i.e. it asserts
  the "wrong" pane on purpose.**
  *Rationale:* (i) The `.tji` note asks for *"Catch2 cases in `tests/focused_framing_test.cpp`
  covering the `canvas#2` vs `canvas#10` ordering"*, but that file constructs `PaneFraming` vectors
  directly, so a case there asserting `canvas#2` **could only pass if `focus_target` itself
  sorted** — which contradicts the same note's *"keeping `focus_target` itself order-agnostic and
  pure"* and the inherited D-focused_canvas_indicator-1. The note's two clauses are satisfiable
  only by splitting them: the *ordering* cases go to `tests/view_registry_test.cpp` where the
  comparator lives, and this file gets the *order-agnosticism contract*. (ii) It is a real
  anti-regression, not a formality: the single most likely future defect is a well-meaning
  implementer "fixing" this bug a second time inside the rule, producing two ordering authorities —
  exactly the divergence class the predecessor's whole design exists to foreclose. This case turns
  that into a red test. (iii) It costs six lines and needs no new fixture machinery.
  *Alternative rejected — omit the case and rely on prose in the header contract.* The tree already
  had prose asserting the correct invariant (`view_framing.hpp:44`) while the code did the opposite;
  prose is what failed here.
  *Alternative rejected — put an ordering case in `focused_framing_test.cpp` that calls
  `view_id_less` and pre-sorts the fixture.* It would duplicate the `view_registry_test.cpp`
  coverage in a target that must boot `ace::app` to link, while testing nothing about
  `focus_target`.
  **No doc delta required.**

- **D-view_id_natural_order-6 — The e2e asserts on `indicated_view_id()` / `focused_framing()` and
  carries no pixel probe, and it reaches the fallback branch by closing the hinted pane.**
  *Rationale:* (i) The defect is about **which name** the rule resolves to; the predecessor already
  owns the proof that the marker's pixels follow that name
  (`tests/focused_canvas_indicator_e2e_test.cpp`, its two-sided ±8 probe), so re-probing here would
  re-test the predecessor at ten times the pane count and add a second llvmpipe-tolerance surface
  for no new signal. (ii) `dockspace.close()` + reconcile is the only shipped path that empties the
  sticky hint (`src/app/canvas_view.cpp:606-614`); the hint is deliberately sticky, so there is no
  "unfocus" verb to drive. It is also the exact idiom the predecessor's phase 6 uses
  (`tests/multi_canvas_e2e_test.cpp:296-301`), so the rig is proven. (iii) Asserting
  `focused_framing()` alongside `indicated_view_id()` covers both halves of the inherited
  single-rule structure in one phase — the verb-facing answer and the marker-facing answer must
  move together.
  *Alternative rejected — construct ten panes headlessly without the shell, by calling
  `canvas.draw_content` directly.* Cheaper and faster, but `draw_content` requires a live ImGui
  frame and a GL context, so "headless" here still means the full rig; and it would bypass
  `ViewRegistry::open`, which is what actually mints `canvas#10` and is half of what makes the
  test's premise real.
  *Alternative rejected — assert only that the target is not `canvas#10`.* Passes for a comparator
  that returns any of the other eight panes. Phases 5 and 6 name the exact expected pane twice, in
  two different fallback states, which a partial or accidental ordering cannot satisfy.
  **No doc delta required.**

## Open questions

(none — all decided.)

## Status

**Done** — 2026-07-23.

- `src/dockmodel/ace/dockmodel/view_registry.hpp` — declared `bool view_id_less(std::string_view, std::string_view)` beside `parse_view_id`.
- `src/dockmodel/view_registry.cpp` — defined `view_id_less` as a tuple-keyed strict weak ordering (view type in catalog order, then instance index ascending; unparseable inputs sorted last by bytes).
- `src/app/canvas_view.cpp` — added `std::sort` with `view_id_less` in `pane_rows()`; added dockmodel include; corrected the false-invariant comment.
- `src/app/ace/app/canvas_view.hpp` and `src/app/ace/app/view_framing.hpp` — contract-text corrections naming `view_id_less` as the ordering obligation; no executable-code change.
- `CMakeLists.txt` — added `tests/view_id_natural_order_e2e_test.cpp` to the `ace_shell_test` source list.
- `tests/view_registry_test.cpp` — 4 new Catch2 units: numeric ordering, catalog-order grouping, unparseable-ids-last, and strict-weak-ordering exhaustive check.
- `tests/focused_framing_test.cpp` — 1 new Catch2 unit: `focus_target` consumes span order verbatim and never re-orders it (the anti-regression that keeps ordering authority in exactly one place).
- `tests/view_id_natural_order_e2e_test.cpp` — new ImGui Test Engine e2e (`multi_canvas / view_id_natural_order`): opens ten canvases, sizes all ten, arms the fallback by closing `canvas#1`, asserts `indicated_view_id() == "canvas#2"`, then closes `canvas#2` and asserts `"canvas#3"`.
- `docs/00-design.md` — D23 amended: "Lowest id" now normatively means numerically lowest instance index, not byte-lexicographically lowest.
- Pre-fix check done: with the `std::sort` line removed, phase 5 reported exactly `"canvas#10"` (verified by the implementer per constraint).
