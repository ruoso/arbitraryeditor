# editor.canvas.nav_aids — Deep-zoom navigation aids: fit-to-frame / fit-to-cell / zoom-to-selection

## TaskJuggler entry

`tasks/00-editor.tji:213-218` — `task nav_aids` under `editor.canvas`. Effort `1.5d`,
`allocate team`, `depends !fit_bounds` (i.e. `editor.canvas.fit_bounds`),
`editor.cells.selection`. The `note` (`:217`) frames the leaf: "The richer deep-zoom
orientation aids beyond reset-to-fit (`editor.canvas.nav`/`fit_bounds`), reusing
`interact::fit(content, pane)`: fit-to-frame (fit a chosen camera frame to the pane —
cameras only), fit-to-cell and zoom-to-selection (frame the selected cell / current
selection — need the selection model). Decided (pre-exec 2026-07-19, resolves the D2 §3
open marker + parking-lot 'Deep-zoom navigation aids'): **all three are in scope**. The
overview's in-panel viewport manipulation + zoom control is a SEPARATE surface owned by
`editor.panels.overview`, not this leaf." Source-of-debt:
`tasks/refinements/editor/nav.md`, `tasks/refinements/editor/fit_bounds.md`. Design:
**docs/00-design.md D2 §3**.

## Effort estimate

**1.5 days** (from the `.tji`). Every seam this leaf plugs into ships: the transient
per-`canvas#N` camera and its `request_camera` submit channel (`editor.canvas.nav`), the
origin-anchored `interact::fit` (`editor.canvas.fit_bounds` proved it against a real
document), and the kind-agnostic composition-space extent of the current selection
(`interact::selected_extent`, shipped by `editor.cells.selection` /
`editor.cameras.frame_selection`). The new code is small and concentrated:

1. **one L1 `interact` primitive** — `fit_region(const arbc::Rect&, pane_w, pane_h)`, the
   positioned generalization of `fit` (fit is its origin-anchored specialization);
2. **one canvas gesture** — a `views::CanvasInput` trigger read at the ImGui layer and a
   consumption branch in `app::CanvasView::draw_content` that computes the selection's
   extent and writes the transient camera through the unchanged `request_camera` path;
3. **the tests** — an L1 Catch2 unit (the bulk), an ImGui Test Engine e2e, and a D24 doc
   row.

No new component, no new DAG edge, no new render path, no new render-thread channel, no
transaction.

## Inherited dependencies

**Settled (from `editor.canvas.nav`, Done 2026-07-18).** The whole transient-camera
machinery this leaf drives:

- **The transient viewport camera.** `app::CanvasView::Presenter{ arbc::Affine camera; …
  }` (`src/app/ace/app/canvas_view.hpp:194`) is the per-`canvas#N` free-navigation camera
  (composition units → device pixels), never persisted, never a `transact` (D-nav-1 /
  D15). `framing_camera` (`:202`), `submitted` (`:210`) and `scale_bar_units` (`:203`) are
  its live-tracking siblings; `scale_bar_units(view_id)` (`canvas_view.hpp:77`) is the
  test-visible camera proxy.
- **The gesture-consumption site.** `app::CanvasView::draw_content`
  (`src/app/canvas_view.cpp:214-231`) already reads `views::CanvasInput` and applies
  `interact::fit` on `in.reset` (`:221`), `interact::zoom` (`:225`), `interact::pan`
  (`:229`), storing the result into `p.camera` (`:231`). It already holds the selection,
  document and registry (`state_.selection()`, `state_.document()`, `state_.registry()`)
  and already builds `interact::pick_targets(...)` for hit-testing (`:252`). **This leaf
  adds one consumption branch beside the `in.reset` branch.**
- **The camera submit channel.** On change, `p.camera` is submitted via
  `host_.request_camera(view_id, want)` (`canvas_view.cpp:264`) — dedup'd against
  `p.submitted` — reaching the render-thread-confined `render::CanvasHost::request_camera`
  (`src/render/ace/render/canvas_host.hpp:118`, applied on the render thread at
  `src/render/canvas_host.cpp:381` → `arbc::HostViewport::set_camera`, A4). **The fit-aid
  camera rides this channel verbatim.**
- **The input seam.** The `F`-while-hovered reset trigger is read at the ImGui/views layer
  into `views::CanvasInput{ bool reset }` (`src/views/ace/views/views.hpp:54`,
  `in.reset = ImGui::IsKeyPressed(ImGuiKey_F, false)` at `src/views/views.cpp:68`) and
  delivered via `views::draw_canvas_interactive` (`views.hpp:62`). **This leaf adds one
  parallel field + one key read.**
- **The reset-to-fit math.** `interact::fit(double content_w, double content_h, double
  pane_w, double pane_h) -> arbc::Affine` (`src/interact/ace/interact/interact.hpp:56`,
  impl `src/interact/interact.cpp:54`) — a uniform-scale, centered camera framing an
  **origin-anchored** `content_w`×`content_h` region into the pane; degenerate input →
  `identity()`. **This leaf generalizes it to a positioned region (D-nav_aids-2), not
  rewrites it.**

**Settled (from `editor.canvas.fit_bounds`, Done 2026-07-18).** The precedent that the fit
camera rides the already-golden `request_camera` / `render_document_srgb8` channel
(`src/render/render.cpp:20`, golden `tests/goldens/canvas_nav_zoom_64x64.rgba8`) so a
camera-only change needs **no new golden** (D-fit_bounds-4); and the degenerate-fallback
discipline every `interact` helper obeys — a degenerate input yields a safe identity/empty
result, never a NaN (D-fit_bounds-3).

**Settled (from `editor.cells.selection`, Done 2026-07-23).** The project-level selection
and the kind-agnostic extent read this leaf consumes:

- `commands::Selection` (`src/commands/ace/commands/selection.hpp:18-64`) — the one
  project-level `ObjectId` set (D19), read fresh each frame as `state_.selection()`
  (`src/app/canvas_view.cpp:463`); `items()` (`:23`, selection order) and `primary()`
  (`:27`) are the id sources. It holds **no geometry**.
- `interact::PickTarget{ id, layer, kind, placement, extent }`
  (`src/interact/ace/interact/pick.hpp:47-59`) with `extent` the **content-space** rect
  (`nullopt` = unbounded), assembled for cells *and* cameras by the single A17 adapter
  `interact::pick_targets(document, registry)` (`pick.hpp:202`, impl
  `src/interact/pick_targets.cpp:16`). A camera's `extent` is its output rectangle
  `{0,0,res_w,res_h}` under `placement == frame` (`pick_targets.cpp:36`).
- **`std::optional<arbc::Rect> interact::selected_extent(std::span<const PickTarget>
  targets, std::span<const arbc::ObjectId> ids)`** (`pick.hpp:136`, impl
  `src/interact/pick.cpp:204`) — the axis-aligned **composition-space** union of
  `placement.map_rect(*extent)` over the targets whose `id` is in `ids`; **kind-agnostic**
  (a camera contributes its output rect), unbounded members skipped, `nullopt` when nothing
  bounded remains. **This is the region source for all three aids — shipped, unit-tested,
  reused unchanged.**

**Settled (from `editor.cameras.frame_selection`, Done 2026-07-23).** D23 (`Minting a
camera`, `docs/00-design.md:490`) claims the phrase **"frame selection"** for the camera
*mint* verb (a rail action, a scene **transaction**) that fits a *saved shot* to
`selected_extent`. This leaf's aids share that geometry but write the **transient** viewport
camera, never a transaction — the disambiguation D24 records (D-nav_aids-3). The
`selected_extent` helper itself was minted there (`src/interact/pick.cpp:204`).

**Settled (from `editor.cameras.model`, Done).** `scene::Camera{ id, layer, name,
resolution, frame }` (`src/scene/ace/scene/camera.hpp:123-129`), `scene::cameras(document)`
(`:134`), `scene::Resolution{ int width, height }` (`:26`) — the persisted shot camera whose
`frame.map_rect({0,0,res_w,res_h})` is its covered region (already what `pick_targets`
feeds `selected_extent` for a camera).

**Pending (owned here).**
- `interact::fit_region(const arbc::Rect& region, double pane_w, double pane_h) ->
  arbc::Affine` (L1 `interact`, beside `fit`).
- One `views::CanvasInput` trigger + its key read, and the consumption branch in
  `app::CanvasView::draw_content`.

## What this task is

**D2 §3** (`docs/00-design.md:88-102`) is normative: the canvas is an "infinite, pannable,
deep-zoomable surface," and its deep-zoom navigation aids — **"fit-to-frame, fit-to-cell,
and zoom-to-selection are all in scope"** (`:98-100`) — exist "so users don't get lost in
unbounded space." `editor.canvas.nav`/`fit_bounds` shipped the first aid, **reset-to-fit**
(`F` frames the whole document); this leaf ships the selection-aware trio the doc marks
in-scope, all of which frame a *region other than the whole document* into the pane.

The three named aids reduce to **one operation over one primitive**: take a composition-
space `arbc::Rect`, and set the transient viewport camera to the uniform-scale, centered
framing of that rect in the current pane — the positioned generalization of reset-to-fit.
They differ only in **where the rect comes from**, and because the selection is
kind-agnostic (D7: cells and cameras are one shape, one select tool), a single
selection-driven gesture delivers all three:

1. **`interact::fit_region(region, pane_w, pane_h)`** (L1, new) — the positioned fit: a
   uniform scale `min(pane_w/W, pane_h/H)` centering an arbitrary composition-space region
   in the pane. `fit(w,h,pw,ph)` is exactly `fit_region({0,0,w,h}, pw, ph)` — reset-to-fit
   is the origin-anchored specialization, so this leaf adds the general primitive and lets
   `fit` delegate to it (D-nav_aids-2).
2. **The region is the current selection's extent** — `interact::selected_extent(
   pick_targets(state_.document(), state_.registry()), state_.selection().items())`, the
   already-shipped kind-agnostic union. One selected cell yields **fit-to-cell**; one
   selected camera yields **fit-to-frame** ("cameras only" falls out — only a camera
   contributes a camera-frame rect); a multi-selection yields **zoom-to-selection**
   (D-nav_aids-1).
3. **The camera is transient** — `p.camera = fit_region(*extent, p.tex_width,
   p.tex_height)`, submitted through the unchanged `host_.request_camera` channel exactly
   like every pan/zoom/reset. **No transaction, no journal entry, no dirty** (D15 /
   D-nav-1). This is the load-bearing difference from D23's like-named *mint* verb
   (D-nav_aids-3).

When the selection is empty or contains only unbounded fills (`selected_extent` →
`nullopt`), the aid is **refused — the camera is left unchanged** (D-nav_aids-5).

It deliberately does **not** ship: any *scene* edit (that is D23's mint, already shipped as
`editor.cameras.frame_selection`); a list/overview-driven "fit to *this* camera" affordance
that frames a camera you point at without selecting it (that is the overview navigator's,
`editor.panels.overview`, `.tji:432`, reusing this primitive); the overview's in-panel
zoom control (same owner, explicitly excluded by the `.tji:217` note); and the finalized
keyboard map (the open §11 input map, `docs/00-design.md:500`).

## Why it needs to be done

Reset-to-fit answers "get me back to the whole document," but the deep-zoom model makes the
*opposite* move just as necessary: once a user has selected a cell or a camera in unbounded
space, "take me to *that*" is the primary orientation gesture — and today it does not exist.
The selection model (`editor.cells.selection`) shipped exact placed geometry —
`PickTarget::extent`, `selected_extent`'s union — but the only consumers so far are the
selection outline, Delete (`editor.cells.remove`), and D23's camera *mint*. This leaf is the
first that reads the selection's **shape** to move the user's *view* rather than to edit the
scene, closing the "so users don't get lost in unbounded space" promise (D2 §3) for the
selection-aware half of the trio. It also completes the aid set the pre-exec 2026-07-19
decision put in scope and that `editor.canvas.nav`'s D-nav-7 explicitly parked pending
`editor.cells.selection` (`nav.md:468-490`) — the dependency is now satisfied.

## Inputs / context

**Governing design docs (normative — the constitution).**
- **D2 §3** `docs/00-design.md:88-102` — the canvas: "infinite, pannable, deep-zoomable";
  the navigation-aids sentence, "**fit-to-frame, fit-to-cell, and zoom-to-selection** are
  all in scope … so users don't get lost in unbounded space" (`:98-100`). No `(open)`
  marker remains in §3 — the pre-exec truth-up already resolved it (`.tji:217`).
- **D2 row** `docs/00-design.md:469` — the **viewport camera** is the active, free-nav,
  **transient** framing; export = render-through-camera (a shot, not this).
- **D7** `docs/00-design.md:474` — cells and cameras share **one shape** and **one select
  tool**; this is why one kind-agnostic gesture serves fit-to-cell, fit-to-frame and
  zoom-to-selection (D-nav_aids-1).
- **D15** `docs/00-design.md:482` — **transient vs scene**: "the **viewport camera's** live
  framing (pan/zoom) is transient session state, NOT undoable." The fit-aid camera is
  transient; it is never a transact, never journaled, never dirties (D-nav_aids-3).
- **D19** `docs/00-design.md:486` — one **project-level** selection shared by every canvas;
  the aid reads `state_.selection()`, a project verb.
- **D23** `docs/00-design.md:490` — **Minting a camera**: the like-named "frame selection"
  *mint* verb (a rail action, a **transaction**) over the same `selected_extent` geometry.
  The disambiguation target for D24 / D-nav_aids-3.
- **§11 open input map** `docs/00-design.md:500` — "The complete keyboard/shortcut set …
  (the pieces are decided; the map isn't written)." The standing licence under which the
  predecessors minted provisional chords, and the reason this leaf's binding is provisional
  (D-nav_aids-6).
- **D24 — Deep-zoom navigation aids** `docs/00-design.md` (**added by this leaf**, see
  Decisions) — the transient-viewport aid set, its shared geometry with D23, its
  refuse-when-nothing-bounded rule, and its distinction from the mint verb.

**Governing architecture rows.**
- **§8** `docs/01-architecture.md:255-291` (edge table `:273-287`) — the DAG. The edges this
  leaf relies on, all existing: `interact → {base, scene, libarbc}` (`:281`, ImGui/GL
  **no**), `app → {everything}` (`:287`), and A17's single `interact → scene` assembly TU
  (`src/interact/pick_targets.cpp`). `fit_region` is primitive-only (`arbc::Rect` in,
  `arbc::Affine` out) so it lands in the existing `interact.cpp` TU and adds **no** second
  assembly TU. Enforced by `scripts/check_levels.py`.
- **§9** `docs/01-architecture.md:293-320` — the layered DoD this leaf's Acceptance criteria
  instantiate.
- **A4/A5** `docs/01-architecture.md` — the camera reaches the render-thread-confined
  `HostViewport` as a *submit* (`request_camera`), applied on the render thread. This leaf
  reuses that channel verbatim; the only UI-thread work is the already-existing per-frame
  `pick_targets` read (D-nav_aids-4).

**libarbc API surface** (fetched under `build/*/_deps/arbc-src/`).
- `arbc::Affine` (`src/base/arbc/base/transform.hpp:13`): `identity()`; `map_rect(const
  Rect&)` (`:38`, used by `selected_extent` per target); free `compose(outer, inner)`
  (`:44`). `fit_region` returns an `Affine` in the same composition→device orientation
  `fit`/`request_camera`/`HostViewport::set_camera` consume.
- `arbc::Rect` (`src/base/arbc/base/geometry.hpp:15`): `x0,y0,x1,y1`, `width()`, `height()`,
  `empty()` — the region `fit_region` frames; positioned, so its `x0,y0` is what `fit`
  cannot express.

**Editor seams this leaf extends.**
- `interact` (L1) — add `arbc::Affine fit_region(const arbc::Rect&, double pane_w, double
  pane_h)` to `src/interact/ace/interact/interact.hpp` (impl `src/interact/interact.cpp`,
  beside `fit` at `:54`); refactor `fit` to delegate. Consumes `interact::selected_extent`
  (`pick.hpp:136`) and `pick_targets` (`pick.hpp:202`) — both shipped, unchanged. No
  ImGui/GL/SDL, no new include beyond the `arbc/base/*` it already has.
- `views` (L3) — add a trigger field to `views::CanvasInput`
  (`src/views/ace/views/views.hpp:54`) and its key read in `draw_canvas_interactive`
  (`src/views/views.cpp:68`, beside the `F`/reset read). ImGui-only, no new dependency.
- `app::CanvasView::draw_content` (L4) — one consumption branch beside `in.reset`
  (`src/app/canvas_view.cpp:214-231`): `selected_extent(pick_targets(state_.document(),
  state_.registry()), state_.selection().items())` → on a bounded rect,
  `p.camera = interact::fit_region(*extent, p.tex_width, p.tex_height)`; submitted through
  the unchanged `request_camera` (`:264`). `app` already depends on `interact`, `commands`
  and `render`; no new edge.

**Predecessor / sibling refinements** (style + decision continuity):
`tasks/refinements/editor/nav.md` (D-nav-1 transient camera, D-nav-2 framing math lives in
L1 `interact`, D-nav-7 parked these aids pending selection),
`tasks/refinements/editor/fit_bounds.md` (the fit camera rides the golden channel → no new
golden; degenerate-fallback discipline),
`tasks/refinements/editor.cells/selection.md` (the project-level selection + `selected_extent`),
`tasks/refinements/cameras/frame_selection.md` (D23; the mint verb this leaf is disambiguated
from; `selected_extent`'s definition and the kind-agnostic-union rationale).

## Constraints / requirements

1. **The aids frame the selection's extent into the current pane.** The region is
   `interact::selected_extent(pick_targets(state_.document(), state_.registry()),
   state_.selection().items())`; on a bounded rect the transient camera becomes
   `interact::fit_region(region, p.tex_width, p.tex_height)` for the current pane device
   size. No new region math beyond `fit_region`; the extent read is the shipped
   `selected_extent`.

2. **All three named aids are the one kind-agnostic gesture.** A single selected cell →
   fit-to-cell; a single selected camera → fit-to-frame ("cameras only" is emergent, not a
   filter); a multi-selection → zoom-to-selection. No per-aid mode, `ToolId`, or separate
   key (D-nav_aids-1); no kind filter in the region computation (D7 / reuse of the
   kind-agnostic `selected_extent`).

3. **`fit_region` is the positioned generalization of `fit`; `fit` delegates.**
   `fit_region({0,0,w,h}, pw, ph)` is byte-identical to `fit(w,h,pw,ph)`; a degenerate
   region (empty/inverted/non-finite) or non-positive pane yields `identity()`
   (D-fit_bounds-3 discipline). The framing math stays in **L1 `interact`**, unit-tested
   headless — never composed in L4 (D-nav-2, D-nav_aids-2).

4. **The camera is transient — never a transaction (D15 / D-nav-1).** It is written to
   `p.camera` and submitted via `host_.request_camera(view_id, want)` exactly like pan/zoom/
   reset; it is never persisted, never journaled, and **never marks the document dirty**.
   This leaf touches no `dispatch`/`transact`/`commands::Command` path — the load-bearing
   distinction from D23's mint (D-nav_aids-3).

5. **The geometry is read on the UI thread from the per-frame `pick_targets`.** Because
   there is no transaction, the region may come from the same UI-thread `pick_targets`
   `draw_content` already builds for hit-testing (`canvas_view.cpp:252`) — the
   inside-`run_edit`, resolve-against-the-live-generation discipline
   (D-frame_selection-5 / D-cells_remove-3) does **not** apply, precisely because nothing is
   mutated (D-nav_aids-4). No writer-thread work, no new lock, no cross-thread state.

6. **Refuse when there is nothing bounded to frame; leave the camera unchanged.** An empty
   selection or a selection of only unbounded fills (`selected_extent` → `nullopt`) is a
   **no-op** — the camera is not touched, not snapped to identity, not fallen back to the
   document (D-nav_aids-5), mirroring D23's refuse-rather-than-guess discipline.

7. **The region fits at the pane's own aspect, centered — not re-aspected.** Unlike a *mint*
   (D23, which expands the region to a rounded-resolution aspect), a nav aid preserves the
   pane's aspect and simply centers the region with a uniform scale — it is a *view*
   change, so nothing is rounded to pixels and no resolution is derived.

8. **Levelization stays clean (§8).** `fit_region` extends existing L1 `interact` (pure,
   primitive-only, lands in `interact.cpp`, adds no A17 assembly TU); the trigger extends
   existing L3 `views`; the wiring extends existing L4 `app`. **No new component, no new DAG
   edge**; nothing in `interact` gains an ImGui/GL/SDL include. `check_levels` stays clean.

## Acceptance criteria

Instantiating the universal DoD (`docs/01-architecture.md` §9); `scripts/gate` green
(check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean).** Primary structural assertion. `fit_region` lands
  in `src/interact/interact.cpp` (primitive-only — `arbc::Rect` in, `arbc::Affine` out — so
  `src/interact/pick_targets.cpp` remains the sole A17 `interact → scene` TU); the trigger
  lands in `src/views/`; the wiring in `src/app/`. **No new component, no new DAG edge**;
  `interact` stays free of ImGui/GL/SDL. No entry in `scripts/check_levels.py` changes.

- **Catch2 unit (L1 `interact` — the bulk, headless, GL-free).** New cases in
  `tests/nav_test.cpp` (the `interact` nav-math family home, `interact::fit` at
  `nav_test.cpp:118`; joined to `ace_tests` in `CMakeLists.txt`), naming
  `TEST_CASE("nav_aids: …")`:
  - **`fit_region` frames a positioned region centered, uniform-scaled:** a region far from
    the origin (e.g. `{100,100,200,150}`, W=100 H=50) into a non-square pane (e.g. 200×200)
    → scale `= min(200/100, 200/50) = 2`, the region center `(150,125)` maps to the pane
    center `(100,100)` (so `tx = 100 - 2·150 = -200`, `ty = 100 - 2·125 = -150`), and all
    four region corners land within `[0,200]×[0,200]` with the tight axis touching a pane
    edge. Numbers pinned via `Approx`.
  - **`fit_region` is the general form of `fit`:** for several `(w,h,pw,ph)`,
    `fit_region({0,0,w,h}, pw, ph)` equals `fit(w,h,pw,ph)` field-for-field — pins that
    reset-to-fit is the origin-anchored specialization and the delegation is correct.
  - **Degenerate → identity:** an empty, inverted, or non-finite region, or a non-positive
    pane, yields `arbc::Affine::identity()` — no NaN escapes (D-fit_bounds-3).
  - **Combined wiring over a real document (the aid end-to-end, headless):** build the probe
    document, insert a cell at a known off-center placement, then
    `selected_extent(pick_targets(doc, registry), {cell_id})` → a rect, and
    `fit_region(rect, pane)` → a camera that maps the cell's placed extent centered into the
    pane (the exact framing the `Shift+F` branch will submit) — mirrors
    `composition_bounds_test.cpp`'s combined assertion and `frame_selection_test.cpp`'s
    end-to-end derivation, proving the fit-to-cell chain without a shell.
  - **`selected_extent` refusal preconditions:** an empty id list and an all-unbounded
    selection both yield `nullopt` (the no-op trigger, asserted at the seam the app branch
    guards on).

- **UI e2e — ImGui Test Engine (the gesture frames the selection).** A new test in
  `tests/canvas_nav_e2e_test.cpp` (`ace_shell_test`, `CMakeLists.txt`; offscreen software-GL,
  modeled on the reset-to-fit e2e at `canvas_nav_e2e_test.cpp:229` and the selection setup
  in `tests/frame_selection_e2e_test.cpp`), registered
  `IM_REGISTER_TEST(engine, "canvas_nav", "frame_selection_view")`, driving the stable
  `canvas#1` view id and asserting on model state (the scale-bar proxy), never pixels:
  - insert a cell placed **off the initial framing**, select it, press the aid key
    (`Shift+F`, D-nav_aids-6); assert `scale_bar_units("canvas#1")` becomes the value implied
    by `fit_region(selected_extent(...), pane)` (i.e. the camera now frames the cell, not the
    document) and a published frame advances — proving the fit-to-cell / zoom-to-selection
    reach end-to-end;
  - with an **empty** selection, the same key is a **no-op**: `scale_bar_units("canvas#1")`
    is unchanged (Constraint 6);
  - the document stays **not dirty** across the gesture (Constraint 4 — a nav aid never
    transacts), asserted via the shipped dirty read.

- **Golden — none added (justified).** This leaf introduces **no new render path**: the
  fit-aid camera is one more `arbc::Affine` on the already-golden `request_camera` /
  `render_document_srgb8` channel, pinned byte-exact by `nav`'s
  `tests/goldens/canvas_nav_zoom_64x64.rgba8`. The framing `Affine` itself is pinned
  numerically by the L1 unit (combined-wiring case). A fit-aid-specific golden would add no
  signal — the justified exception to "rendered output gets a golden," identical in shape to
  D-fit_bounds-4.

- **Threading (ASan/TSan) — no new scope.** This leaf adds no channel, no transaction, and
  no cross-thread state; the only UI-thread work is the existing per-frame `pick_targets`
  read (Constraint 5) feeding the existing `request_camera` submit. The `nav`/`multi_canvas`
  lifecycle test (add → resize → camera-submit → render → teardown) already covers the
  camera channel under `asan`/`tsan`; running it green with the fit-aid camera flowing is
  sufficient. Unlike `editor.cameras.frame_selection` (which added a writer-thread
  read-walk + write inside `apply_edit`), **this leaf performs no edit-closure work at all**,
  so no new TSan target is warranted and the refinement claims none.

- **Coverage / format / build.** ≥90% diff coverage (`diff-cover --fail-under=90`,
  `coverage` preset) on changed lines; clang-format + build clean across the standard
  presets; `scripts/gate` green. Tests ship with the task.

- **Doc delta (same commit).** `docs/00-design.md` gains **D24 — Deep-zoom navigation
  aids** (see D-nav_aids-3). No `A<n>` change (no structural/build change).

**No new WBS leaf is deferred.** This leaf ships all three in-scope aids in full. The two
remaining threads both have scheduled owners and need no new task:
- a list/overview-driven "fit to *this* camera" (framing a camera you point at without
  selecting it) and the overview's in-panel zoom control are **`editor.panels.overview`**'s
  (`.tji:432`, explicitly excluded here by the `.tji:217` note), reusing this leaf's
  `fit_region` primitive;
- the finalized keyboard binding is the standing open **§11 input map**
  (`docs/00-design.md:500`) — a design-open item already tracked there, not new WBS scope;
  this leaf mints a provisional chord under §11's licence (D-nav_aids-6) and adds no
  parking-lot item.

## Decisions

- **D-nav_aids-1 — The three named aids are delivered by one kind-agnostic
  selection-driven gesture, not three modes/keys.** "Frame the current selection in the
  view" computes `selected_extent` over `state_.selection().items()` and fits it; a single
  selected cell *is* fit-to-cell, a single selected camera *is* fit-to-frame ("cameras
  only" emerges because only a camera contributes a camera-frame rect), a multi-selection
  *is* zoom-to-selection. *Rationale:* `selected_extent` is already kind-agnostic
  (`pick.hpp:136`, D-frame_selection-3) and D7 (`docs/00-design.md:474`) makes cells and
  cameras "one shape … one select tool" — so fit-to-cell and fit-to-frame are literally the
  single-member case of zoom-to-selection, and splitting them into three separate gestures
  would fork one primitive into three near-identical call sites and over-commit the open
  input map (§11) for no user-visible gain. All three doc-named behaviors are reachable and
  tested. *Alternative rejected:* three distinct keys/rail items (frame-cell, frame-camera,
  frame-selection). More surface, more input-map commitment, and the kind branch is exactly
  the discrimination A16/D-cells_model-8/D7 push against. *Alternative rejected:* a separate
  "frame the primary member only" gesture beside "frame the whole selection." The primary is
  reachable by reducing the selection; a second key for a marginal single-vs-union
  distinction is not worth pre-empting the input-map leaf. **Doc delta: D24.**

- **D-nav_aids-2 — The positioned fit is a new L1 primitive `interact::fit_region(rect,
  pane_w, pane_h)`; `fit` delegates to it as the origin-anchored specialization.** *Rationale:*
  all three aids frame a rect at an *arbitrary composition-space position*, but `fit`
  (`interact.hpp:56`) is origin-anchored — it takes only `content_w`/`content_h`, so it
  cannot express the region's `x0,y0`. The framing math must live in **L1 `interact`**,
  unit-tested headless (D-nav-2's charter that all nav framing math is L1), so it is a named
  primitive there, not a `compose(fit(...), translate(-x0,-y0))` assembled in L4. Making
  `fit` delegate to `fit_region({0,0,w,h}, …)` keeps one implementation and one
  degenerate-guard, and the unit test pins their equality so the two can never drift.
  *Alternative rejected:* compose the pre-translation in `app::CanvasView` (L4). It buries
  framing geometry in the ImGui-adjacent layer where it cannot be unit-tested headless — the
  exact anti-pattern D-nav-2 established `interact` to avoid. *Alternative rejected:* route
  through the shipped `shot_from_extent` → `viewport_camera_for_shot` (`interact.hpp:141`,
  `:157`). That derives and rounds a *resolution* and re-aspects the region to it (a mint's
  job), producing a camera at the *selection's* aspect with pixel-rounding artifacts, not a
  clean fit into the *pane's* aspect — wrong shape for a view change (Constraint 7). **No
  doc delta (mechanism); the primitive is covered by D24.**

- **D-nav_aids-3 — The aid writes the transient viewport camera, never a transaction; it is
  distinct from D23's like-named "frame selection" mint, and D24 records the distinction.**
  The camera goes to `p.camera` and the `request_camera` channel (`canvas_view.cpp:264`) —
  no `commands::Command`, no `dispatch`, no journal entry, no dirty. *Rationale:* D15
  (`docs/00-design.md:482`) draws the line at transient-vs-scene: the viewport camera's live
  framing is transient session state (like scroll position), explicitly NOT undoable —
  identical to reset-to-fit/pan/zoom (D-nav-1). This is the load-bearing contrast with D23
  (`docs/00-design.md:490`), whose "frame selection" *mints a saved shot camera* (a scene
  transaction) over the **same** `selected_extent` geometry — two verbs, confusingly close
  in name, that a future implementer could wire into each other. Because D23 is recent and
  claims the phrase, the constitution needs a row that names the transient counterpart and
  states they share geometry but differ in effect, or the collision becomes a bug. Per
  `tasks/refinements/README.md:80-96`, a user-visible verb-set rule that constrains future
  refinements belongs in the decisions log. *Alternative rejected:* keep the distinction in
  this refinement only. The next reader of D23 would have no constitutional pointer to the
  transient sibling and could conflate them. *Alternative rejected:* an `A<n>` row. The rule
  is UI/UX behavior (which verb moves the view vs. which creates scene data), not structure;
  A-rows carry the structural half (a camera is a `Content`+`Layer`, A14). **Doc delta:
  D24 (`docs/00-design.md`).**

- **D-nav_aids-4 — The selection geometry is read on the UI thread from the per-frame
  `pick_targets`, not inside an edit closure.** `draw_content` reuses (or re-runs) the
  `pick_targets(state_.document(), state_.registry())` it already builds for hit-testing
  (`canvas_view.cpp:252`) and passes it to `selected_extent`. *Rationale:* the
  inside-`run_edit`, resolve-against-the-generation-the-transaction-lands-on rule
  (D-frame_selection-5, D-cells_remove-3) exists to keep a *mutation* consistent with the
  document version it commits to. This aid mutates nothing — it computes a camera for display
  — so a UI-thread frame-cache read is exactly right, no different from the hit-test read
  that already drives the selection outline, and it adds no writer-thread work, no lock, no
  cross-thread state (Constraint 5). *Alternative rejected:* route the read through
  `CanvasView::apply_edit` / the writer thread for "consistency." It would add a
  writer-thread hop and a threading surface for a read that has no transaction to be
  consistent with — cost with no correctness gain. **No doc delta required.**

- **D-nav_aids-5 — A selection with no bounded extent is refused: the camera is left
  unchanged, not snapped to the document.** When `selected_extent` returns `nullopt` (empty
  selection, or only unbounded fills such as a factory `org.arbc.solid`), `draw_content`
  takes no camera action. *Rationale:* "frame the selection" with nothing framable is a no-op
  by construction; leaving the camera where it is preserves the user's current view (the
  least-surprising outcome), and it mirrors D23's refuse-rather-than-guess clause and
  `marquee`'s unbounded-skip (`pick.hpp`, D-selection-5). *Alternative rejected:* fall back
  to reset-to-fit (frame the whole document) when nothing is bounded. It invents a target the
  user did not select and would make an accidental `Shift+F` on an empty selection yank the
  view to the document — the same "inventing a region the user never selected" D-frame_selection-3/-7
  reject; reset-to-fit already has its own key (`F`). *Alternative rejected:* snap to identity.
  A jarring jump to device-origin for a gesture that found nothing to frame. **Doc delta: D24
  (the refuse clause).**

- **D-nav_aids-6 — The gesture ships a provisional keyboard chord (`Shift+F`); the input-map
  leaf owns the final binding.** `Shift+F` sits in the reset-to-fit family (`F` frames the
  document, `Shift+F` frames the selection), read beside the `F`/reset read in
  `views.cpp:68`, and does not collide with the shipped drag modifiers (Shift=constrain,
  Alt=from-center, Cmd/Ctrl=select-behind, Space=pan — all *drag* modifiers, whereas
  `Shift+F` is a discrete keypress when not dragging). *Rationale:* `docs/00-design.md:500`
  leaves the full input map explicitly open ("the pieces are decided; the map isn't
  written"), and the predecessors minted provisional chords under that licence (`F` in
  D-nav-7, and D-undo-3/D-selection-9/D-cells_remove-5) with **no doc delta** for the binding.
  A discoverable, mnemonic, non-colliding chord now, rebindable by the input-map leaf later,
  is the right minimal call; it is **not** encoded as a WBS task (the input map is a
  design-open item already tracked at §11, not implementer-closable scope). *Alternative
  rejected:* an Edit-rail item like D23's "Frame Selection". The Edit section is for scene
  *mutations* (Insert/Delete/Frame-Selection-mint); a transient nav verb there would read as
  an edit and sit beside the like-named mint — exactly the confusion D-nav_aids-3 guards
  against. Reset-to-fit set the precedent that nav aids are canvas keyboard gestures with no
  rail item. *Alternative rejected:* repurpose the shipped `F` to be selection-aware (frame
  selection if any, else document). It silently changes a shipped, decided behavior
  (reset-to-fit as the unconditional "escape to the whole document" guarantee, D-nav-7);
  keeping `F` unconditional and adding `Shift+F` is purely additive with no regression.
  **No doc delta required for the binding (§11 governs); the aid set is D24.**

## Open questions

(none — all decided.) The two remaining threads in this area are both routed to existing
owners, not left open here: the **final keyboard binding** is the standing §11 input-map
item (`docs/00-design.md:500`), where the provisional `Shift+F` will be confirmed or
rebound; the **list/overview-driven "fit to *this* camera"** affordance and the overview's
in-panel zoom control are `editor.panels.overview`'s (`.tji:432`), reusing this leaf's
`fit_region` primitive. Neither is a new WBS leaf and neither is a new parking-lot item.

## Status

**Done** — 2026-07-23.

- `src/interact/ace/interact/interact.hpp`, `src/interact/interact.cpp` — new L1 `fit_region(const arbc::Rect&, pane_w, pane_h) -> arbc::Affine`; `fit` now delegates to it as the origin-anchored specialization (D-nav_aids-2).
- `src/views/ace/views/views.hpp`, `src/views/views.cpp` — `CanvasInput::frame_selection` field + `Shift+F` key read; split from plain `F`/reset so `Shift+F` is never simultaneously a reset (D-nav_aids-6).
- `src/app/canvas_view.cpp` — consumption branch: `selected_extent(pick_targets(…), selection.items())` → `fit_region` into the transient `p.camera`; refused (no-op) when `selected_extent` returns `nullopt` (D-nav_aids-5).
- `tests/nav_test.cpp` — 5 new `nav_aids:` Catch2 cases: positioned framing, `fit`-equivalence delegation, degenerate→identity, real-document fit-to-cell chain, `selected_extent` refusal preconditions.
- `tests/canvas_nav_e2e_test.cpp` — ImGui Test Engine e2e `canvas_nav`/`frame_selection_view`: `Shift+F` frames the selection, empty-selection no-op, gesture never dirties.
- `docs/00-design.md` — D24 row added (deep-zoom navigation aids, transient camera, distinction from D23 mint verb).
