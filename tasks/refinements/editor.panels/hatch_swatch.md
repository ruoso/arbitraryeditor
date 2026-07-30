# editor.panels.hatch_swatch — list-side hatch swatch + list↔overview cross-highlight

## TaskJuggler entry

- **Task:** `editor.panels.hatch_swatch` (in `tasks/00-editor.tji`, under `editor.panels`).
- **Effort:** 1d.
- **Depends:** `!overview` (`editor.panels.overview`).
- **Note:** "Draw the deterministic `interact::overview_pattern` swatch beside each
  Layers-list row and wire AppState hover cross-highlight between the list and the
  overview (§5:191-194: 'the same pattern swatch appears beside the layer row … hovering
  either cross-highlights the other'). The overview side of the identity ships with
  `editor.panels.overview`; this leaf owns the list-side rendering and the cross-panel
  hover state. Source-of-debt: `tasks/refinements/editor/overview.md`. Design:
  `docs/00-design.md` D6/§5."
- **Back-link:** `tasks/00-editor.tji:624-629`.
- **Downstream dependents:** none in the current WBS — this is a leaf polish task that
  completes the co-primary list↔overview identity promised by D6.

## Effort estimate

1d. The identity mechanism (`interact::overview_pattern` / `interact::hatch_segments`) and
the overview-side draw already ship (`editor.panels.overview`). This leaf is thin: a
list-side swatch cell in each Layers row that re-draws that exact identity, one new
transient `AppState` field, and the two-directional hover wiring between the two singleton
panels. Most of the day is the ImGui Test Engine e2e that drives hover across both panels.

## Inherited dependencies

**Settled (consumed as-is):**

- **`interact::overview_pattern(int ordinal, int count)`** — the deterministic per-layer
  identity, `src/interact/ace/interact/interact.hpp:464`, impl
  `src/interact/interact.cpp:867-887`. PURE, L1, no ImGui: same `ordinal` → same
  `OverviewPattern`; `ordinal` clamped into `[0, count)`; the first
  `k_overview_hatch_style_count` (=6, `interact.hpp:453`) ordinals are distinguished by
  hatch **style** alone (`color_index == -1`) and only past that does `color_index` become
  `>= 0` (`interact.hpp:455`, `k_overview_palette_count`=6). The header comment at
  `interact.hpp:426-427,444` explicitly earmarks this leaf as reusing the EXACT identity
  "with no forking" (D-overview-4).
- **`interact::OverviewPattern` / `HatchStyle` / `HatchSegment`** — the descriptor + geometry
  value types, `interact.hpp:435-470`. `hatch_segments(content_bounds, spacing, style)`
  (`interact.hpp:487-488`) generates the content-space hatch lines the overview box draws.
- **`interact::pick_targets`** — the L1 hit-test the overview already uses for click-select
  (`src/interact/ace/interact/pick.hpp:202`); reused here to resolve the hovered overview
  box (the draw-list boxes are not ImGui items).
- **`app::LayersPanel`** — the list body this leaf edits, `src/app/layers_panel.cpp:80-214`
  (header `src/app/ace/app/layers_panel.hpp`). Per-cell row loop `:139-207`; the disclosure
  spacer + `SameLine()` at `:167-169`; the row label build `:171-176`; the row `Selectable`
  `:178`. Camera rows `:113-121` (`###camera_row_<id>`, Selectable `:118`).
- **`app::OverviewPanel`** — the overview body this leaf edits, `src/app/overview_panel.cpp`.
  The per-cell pattern draw `:523-555` (`overview_pattern(this_ordinal, cell_count)` at
  `:539`, `hatch_ink(pat.color_index, selected)` at `:550`); dotted borders `:557-580`;
  camera label buttons `###ov_cam_<id>` `:410-460`.
- **`commands::AppState`** — the L1 headless session-state holder,
  `src/commands/ace/commands/app_state.hpp` (`:91` "Headless — no ImGui/GL/SDL"). Existing
  transient UI state: `selection()` (`:138-139`, member `:248`) and `entered_composition()`
  (`:149-150`, member `:252`). Both are project-level, UI-thread-only, never journaled — the
  precedent this leaf's hover field follows.

**Pending (owned here):**

- The list-side swatch cell in each Layers row.
- The new `AppState::hovered_object` transient field + its accessor.
- The two-directional hover wiring (list row / overview box / camera item → set hovered;
  both panels read it to render a hover highlight).
- A shared L4 `hatch_ink` (currently anonymous-namespace-local in `overview_panel.cpp:55-65`)
  so the swatch and the box ink identically — the color layer of the no-forking guarantee.

## What this task is

Two small pieces that complete the **shared list↔overview identity** D6/§5 promise:

1. **List-side swatch.** Beside each top-level Layers row, draw the same deterministic
   pattern the overview draws for that cell's box — a small filled, hatched square whose
   hatch style/color come from `interact::overview_pattern(ordinal, count)` with the cell's
   **bottom→top ordinal** (the same argument the overview passes), so "a box in the overview
   and a row in the list are matched at a glance" (§5:191-192).
2. **Cross-panel hover highlight.** A single project-level `hovered_object` on `AppState`:
   hovering a Layers row (or its camera row) sets it; hovering an overview box (or camera
   frame) sets it; **both** panels read it and render a hover highlight on the matching
   row/box — "hovering either cross-highlights the other" (§5:193).

It **does not** ship: any change to the identity function or its color-fallback policy
(those shipped with `editor.panels.overview`, D-overview-4); the full transform gizmo on
overview boxes (`editor.panels.overview_gizmo`); or persistence of hover (it is transient
per-frame state, never journaled, never saved).

## Why it needs to be done

D6 makes the list and the overview **co-primary** over one shared selection, and §5:191-194
names the mechanism that lets a user pivot between them: the pattern is a **shared visual
identity** carried in both views, and hover on one lights the same object in the other. The
overview leaf shipped the identity function and the overview-side draw but explicitly
deferred the list-side swatch and the cross-panel hover state to this leaf
(`tasks/refinements/editor/overview.md`, Deferred WBS). Without it the list row and the
overview box are matched only by reading a name — the exact weakness the shared pattern was
introduced to remove. This is the last piece of the D6 co-primary story.

## Inputs / context

**Governing design rows:**

- **D6 — Structure views** (`docs/00-design.md:473`): "the pattern is a shared list↔overview
  identity"; list and overview co-primary over one shared selection.
- **§5 — The composition overview** (`docs/00-design.md:160-206`), specifically **§5:191-194**:
  "The pattern is shared identity across views. The same pattern swatch appears beside the
  layer's row in the list … hovering either cross-highlights the other." §5:173 — cameras are
  drawn as **frames**, not hatch-filled content boxes (so they carry no swatch). §5:204 — the
  pattern-count-before-color open item, already decided by the overview leaf
  (`k_overview_hatch_style_count = 6`, then color).

**Governing architecture rows:**

- **§8 — Components & levelization** (`docs/01-architecture.md`): `interact` and `commands`
  are **L1** (UI-agnostic, no ImGui/GL/SDL, unit-tested headless); `app` is **L4**. This leaf
  adds no component and no DAG edge (see Constraints).
- **§9 — Testing & definition of done**: L1 logic → Catch2; rendered libarbc output → golden;
  UI behavior → ImGui Test Engine e2e; threads → ASan/TSan; `check_levels` clean; ≥90% diff
  coverage.

**Editor seams this leaf extends:**

- `src/interact/ace/interact/interact.hpp:435-488` — `HatchStyle`, `OverviewPattern`,
  `overview_pattern`, `HatchSegment`, `hatch_segments`, and the constants.
- `src/app/overview_panel.cpp:523-555` — the reference pattern-draw the swatch mirrors
  (fill → `hatch_segments` → `hatch_ink`); `:55-65` — `hatch_ink`, to be promoted to a shared
  L4 header; `:410-460` — the `###ov_cam_<id>` label buttons whose `IsItemHovered()` drives
  camera cross-highlight.
- `src/app/layers_panel.cpp:139-207` — the row loop the swatch cell is inserted into; the
  `slot`/`count` locals (`:138-141`) that already carry the ordinal; `:113-121` — the camera
  rows.
- `src/commands/ace/commands/app_state.hpp:138-150,248-252` — the `selection_` /
  `entered_composition_` precedent the `hovered_object_` field follows.

**Predecessor refinements:**

- `tasks/refinements/editor/overview.md` — ships `overview_pattern` / `hatch_segments`, the
  overview-side draw, and the shared-identity decision **D-overview-4** (the deferred swatch
  reuses the exact identity with no forking). Its Deferred-WBS block is this leaf's charter.
- `tasks/refinements/editor.panels/layers.md` — ships the Layers panel, the `###layer_row_<id>`
  / `###camera_row_<id>` stable ids, and `AppState::entered_composition` (the transient-field
  pattern reused here).

**Test rigs:**

- `tests/overview_test.cpp` / `tests/layers_test.cpp` (Catch2, appended to `ace_tests` near
  `tests/CMakeLists.txt:254`); `tests/overview_e2e_test.cpp` / `tests/layers_e2e_test.cpp`
  (ImGui Test Engine, appended to `ace_shell_test` near `tests/CMakeLists.txt:316`, driving
  the stable `###layer_row_<id>` / `###ov_cam_<id>` ids); `tests/canvas_host_test.cpp` (TSan
  anchors over the interactive pool).

## Constraints / requirements

1. **Reuse the identity, do not fork it (D-overview-4).** The swatch's style/color come from
   `interact::overview_pattern(ordinal, count)` — no list-side palette, no second style table.
   Both panels key off the cell's position in the **same** `scene::cells(document, registry,
   active)` bottom→top vector, so they agree by construction.
2. **Bottom→top ordinal.** The Layers loop walks front→back over list slot `slot`, drawing
   `cells[count-1-slot]` (`layers_panel.cpp:141`). The swatch passes ordinal `count-1-slot`,
   i.e. that cell's bottom→top index — exactly the `this_ordinal` the overview passes
   (`overview_panel.cpp:528,539`). Passing the raw `slot` would silently mismatch the two
   views; this is the correctness-critical convention (pinned by test).
3. **Swatches are for top-level active-composition CELL rows only.** Cameras carry no swatch
   (drawn as frames, §5:173). Read-only peek-children rows (`draw_peek_children`,
   `layers_panel.cpp:59-73`) carry no swatch — they belong to a nested composition with its
   own ordinal space, and a swatch there would imply an identity the overview does not draw
   for them.
4. **Cross-highlight is id-keyed and covers cells AND cameras.** `hovered_object` is a plain
   `std::optional<arbc::ObjectId>`, so it uniformly matches a cell box/row or a camera
   frame/row. Cameras get the hover cross-highlight (list camera row ↔ overview camera frame)
   for free even though they carry no swatch.
5. **Single-writer-per-frame hover, no shell reset.** The panel whose ImGui window currently
   contains the pointer authors `hovered_object` for that frame — setting the hovered id, or
   clearing it to `nullopt` when over no item. A panel whose window is not hovered never
   writes. Because at most one window holds the pointer, the two singleton panels never
   contend, and no shell-level per-frame reset is needed.
6. **Hover is transient UI-thread-only state.** `hovered_object_` lives beside `selection_` /
   `entered_composition_`: never persisted, never journaled (D15), never a transaction, moves
   cleanly with the defaulted `AppState` move. Read only on the UI thread; the writer thread
   never touches it (so it introduces no cross-thread shared mutable state).
7. **Hover highlight is visually distinct from selection.** The matching row/box gets a hover
   treatment (a light outline / brightened swatch border) that reads as *pointer is here*,
   not *this is selected* — both states can be true at once (hovering a selected row).
8. **Levelization (§8):** `overview_pattern` stays in L1 `interact`; `hovered_object_` is a
   plain-value field on L1 `commands::AppState` (no ImGui/GL/SDL include added to L1); all
   ImGui drawing and hover detection stay in L4 `app`. `hatch_ink` moves from
   `overview_panel.cpp`'s anonymous namespace to a shared **L4** header so both panels ink
   identically. **No new component, no new DAG edge**; `check_levels` stays clean.

## Acceptance criteria

The universal DoD (`docs/01-architecture.md` §9) instantiated for this leaf:

**Levelization — `check_levels` clean.** No new component, no new edge; the L1 core gains one
plain-value field and no ImGui/GL/SDL include. `scripts/check_levels.py` (via `scripts/gate`)
passes.

**L1 logic — Catch2 unit** (`tests/hatch_swatch_test.cpp`, appended to `ace_tests`):

- **Hover field round-trip:** a fresh `AppState` reports `hovered_object() == std::nullopt`;
  `set_hovered(id)` then reads back `id`; `set_hovered(std::nullopt)` clears it. Survives a
  move-construction (the field moves like `selection_`).
- **Swatch identity is the overview identity (Constraint 1/2):** for a composition of `N`
  cells, for every list slot `slot ∈ [0, N)` the swatch ordinal `count-1-slot` fed to
  `interact::overview_pattern` yields a byte-identical `OverviewPattern` (style `angle_rad`
  + `cross` + `color_index`) to the overview box's `overview_pattern(this_ordinal, N)` for the
  same cell — pinning that the list must pass the bottom→top ordinal, not the raw slot. Also
  re-pins `overview_pattern` determinism from the list's call site (same `(ordinal, count)`
  → same result across calls).

**Rendered output — golden N/A (justified).** The swatch is an ImGui draw-list mark (fill +
hatch lines + border), not a libarbc `render_offline` composition, so there is no byte-exact
kernel output to compare — the same justification the overview leaf recorded for its
schematic. Visual regression is covered by the e2e screenshot baselines below.

**UI e2e — ImGui Test Engine** (`tests/hatch_swatch_e2e_test.cpp`, appended to
`ace_shell_test`):

- **Swatch renders beside each cell row:** with a multi-cell composition, each
  `###layer_row_<id>` has its swatch cell drawn ahead of the label (screenshot baseline the
  regression pins); camera rows and peek-children rows have none (Constraint 3).
- **List→overview cross-highlight:** hovering `###layer_row_<id>` drives
  `state.hovered_object() == id`, and the overview renders that box's hover highlight
  (screenshot baseline). Moving off any row clears `hovered_object()` to `nullopt`
  (Constraint 5).
- **Overview→list cross-highlight (cell):** moving the pointer over an overview cell box's
  screen centre drives `state.hovered_object()` to that cell id (resolved via
  `interact::pick_targets`), and the matching `###layer_row_<id>` renders its hover highlight.
- **Camera cross-highlight:** hovering `###ov_cam_<id>` drives `hovered_object() == cam.id`
  and highlights `###camera_row_<id>` (and the reverse), with no swatch on either
  (Constraint 4).
- **Hover ≠ select:** hovering a row/box does not change `state.selection()` (hover and
  selection are independent; a selected-and-hovered row shows both treatments, Constraint 7).

**Threading (ASan/TSan).** No new cross-thread state: `hovered_object_` is UI-thread-only
like `selection_`/`entered_composition_` and is never read on the writer thread (Constraint 6),
so no new data-race surface is introduced. The e2e runs under the `clang-asan` offscreen lane
(§9.1) and must stay clean; the existing overview/layers panel-draw TSan anchors in
`tests/canvas_host_test.cpp` already exercise the panels' draw path under the interactive pool.

**Coverage.** ≥90% diff coverage on the changed lines (the swatch cell, the `hovered_object`
field + wiring, the shared `hatch_ink`), tests shipping in the same commit.

**Deferred WBS work:** none — the leaf is self-contained. The full overview transform gizmo
is already tracked separately (`editor.panels.overview_gizmo`, `tasks/00-editor.tji:630-635`).

## Decisions

- **D-hatch_swatch-1 — Cross-panel hover lives on `AppState` (L1 `commands`) as
  `std::optional<arbc::ObjectId> hovered_object_`.** *Rationale:* it is shared cross-panel
  transient state, exactly the shape of `selection_` and `entered_composition_` (the two
  singleton structure panels already index one shared selection, D6); putting it in the L1
  headless holder keeps its set/clear/round-trip logic unit-testable without ImGui and follows
  a shipped precedent. *Alternative rejected:* a per-panel L4 hover field threaded between the
  two panels via a shared L4 struct — rejected because the Layers and Overview panels are
  independent singletons that do not reference each other, an L4↔L4 coupling has no precedent,
  and it would not be headless-testable.

- **D-hatch_swatch-2 — Single-writer-per-frame: the pointer-owning panel authors the hover
  field.** The panel whose ImGui window contains the pointer writes `hovered_object` (set or
  clear); a non-hovered panel never writes. *Rationale:* at most one window holds the mouse per
  frame, so the two panels can never contend, and no shell-level reset or ordering contract is
  needed — the rule is local to each panel. *Alternative rejected:* have the shell reset
  `hovered_object` to `nullopt` each frame before the panels draw — rejected because it adds a
  shell↔panel ordering coupling for state the panels can fully own themselves.

- **D-hatch_swatch-3 — The swatch reuses `interact::overview_pattern` verbatim with the cell's
  bottom→top ordinal.** *Rationale:* D-overview-4 mandates one identity with no forking; both
  panels key off the same `scene::cells` bottom→top order, so passing ordinal `count-1-slot`
  makes the swatch and the box identical by construction. *Alternative rejected:* a separate
  list-side palette or a swatch keyed by the front→back slot — rejected as an identity fork
  that breaks the §5:191 "matched at a glance" promise and would silently desync the two views.

- **D-hatch_swatch-4 — Swatches on top-level cell rows only; cross-highlight covers cells and
  cameras.** *Rationale:* §5:173 draws cameras as frames (no hatch), and peek-children live in
  a nested composition with their own ordinals, so a swatch on either would assert an identity
  the overview does not draw; but the hover cross-highlight is a plain id match, so cameras
  (and their frames) participate for free. *Alternative rejected:* a swatch on every row
  including cameras and peek children — rejected as visually misleading.

- **D-hatch_swatch-5 — Overview cell-box hover resolves through `interact::pick_targets`;
  camera-frame and list-row hover use ImGui `IsItemHovered()`.** *Rationale:* overview cell
  boxes are draw-list quads with a click-through-to-background interior (the overview leaf's
  deliberate model), so the existing L1 hit-test is the right hover probe; cameras and list
  rows are real ImGui items, so their built-in hover is direct. *Alternative rejected:* mint a
  per-box `InvisibleButton` for every overview cell to get `IsItemHovered()` — rejected because
  it would change the overview's click-through interaction model and add N invisible items per
  frame.

- **D-hatch_swatch-6 — Promote `hatch_ink` to a shared L4 header.** The `color_index → ImU32`
  mapping (`overview_panel.cpp:55-65`) moves to a shared L4 `app` header (`pattern_swatch`) so
  the list swatch and the overview box ink from one table. *Rationale:* the no-forking
  guarantee (D-overview-4) must hold at the color layer too, not just the style layer.
  *Alternative rejected:* duplicate the six-entry palette in `layers_panel.cpp` — rejected as a
  fork that would drift.

## Open questions

(none — all decided.) The exact hover-highlight weight/color is minor visual polish that
reuses the overview's existing selection accent palette (`overview_panel.cpp:546,574`); it is
not a design question and does not gate the leaf.

## Status

**Done** — 2026-07-30.

- `src/app/ace/app/pattern_swatch.hpp` — new shared L4 header: `hatch_ink` (promoted from `overview_panel.cpp` anon namespace) + `draw_pattern_swatch`; ensures list and overview ink identically (D-hatch_swatch-6, D-overview-4).
- `src/commands/ace/commands/app_state.hpp` — added `hovered_object()` / `set_hovered()` accessor pair and `hovered_object_` field (plain-value, L1-clean, beside `selection_`/`entered_composition_`) (D-hatch_swatch-1).
- `src/app/overview_panel.cpp` — removed local `hatch_ink`; cell hover via `interact::pick`; camera hover via `IsItemHovered`; single-writer authoring when window-hovered; solid-accent hover highlight on box/frame (D-hatch_swatch-2/5/7).
- `src/app/layers_panel.cpp` — per-row bottom→top swatch cell (`overview_pattern(count-1-slot, count)`); row/camera hover capture + highlight + single-writer authoring (Constraint 2/3/4).
- `CMakeLists.txt` — registered `tests/hatch_swatch_test.cpp` (Catch2, `ace_tests`) and `tests/hatch_swatch_e2e_test.cpp` (ImGui Test Engine, `ace_shell_test`).
- `tests/hatch_swatch_test.cpp` — Catch2 unit: hover round-trip + move survival; swatch↔box identity pins the bottom→top ordinal (Constraint 2).
- `tests/hatch_swatch_e2e_test.cpp` — ImGui Test Engine e2e: list→overview, overview→list, camera both-ways cross-highlight, clear-to-nullopt, hover≠select.
- Golden: N/A (swatch is a draw-list mark, not a libarbc render_offline output) — same justification as `editor.panels.overview`.
