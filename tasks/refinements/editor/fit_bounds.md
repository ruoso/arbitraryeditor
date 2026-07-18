# editor.canvas.fit_bounds — Wire reset-to-fit to root composition authored bounds

## TaskJuggler entry

`tasks/00-editor.tji:188-192` — `task fit_bounds` under `editor.canvas`. Effort `1d`,
`allocate team`, `depends !nav` (i.e. `editor.canvas.nav`). The `note` (`:192`) frames
the leaf: "reset-to-fit currently restores identity/fit framing (D-canvas_view-5); wire
it to the root composition's authored bounds once a Document composition-bounds accessor
is available. The general `interact::fit(content,pane)` math ships in `editor.canvas.nav`
tested for this future wiring + downstream reuse." Source-of-debt:
`tasks/refinements/editor/nav.md`. Design: **docs/00-design.md D2 §3**.

This is tech-debt registered by the `nav` closer (`nav.md:503`): nav shipped the general
`interact::fit(content_w, content_h, pane_w, pane_h)` math and the reset-to-fit gesture,
but wired the reset *branch* to `arbc::Affine::identity()` because no path to the
document's authored canvas size was threaded yet. This leaf closes that gap — nothing
downstream depends on it; it completes an existing promise.

## Effort estimate

**1 day** (from the `.tji`). The math (`interact::fit`) and the trigger (the `F` key) and
the submit channel (`request_camera`) all already ship from `nav`. The cost is in exactly
two small places: (1) a **read accessor** — given the one owned `arbc::Document`, return
the root composition's authored canvas size — and (2) rewiring the **reset branch** of
`app::CanvasView::draw_content` (`src/app/canvas_view.cpp:87-88`) from a hard-coded
`identity()` to `interact::fit(size, pane)` with an identity fallback when the document
carries no bounds. No new render path, no new channel, no new UI seam, no new component.

## Inherited dependencies

**Settled (from `editor.canvas.nav`, `tasks/00-editor.tji:175-180`, Done 2026-07-18).**
The whole machinery this leaf plugs into shipped in `nav`:

- **The fit math.** `interact::fit(double content_w, double content_h, double pane_w,
  double pane_h) -> arbc::Affine` (`src/interact/ace/interact/interact.hpp:50-54`, impl
  `src/interact/interact.cpp:53-62`) — a uniform-scale, centered `Affine` framing a
  `content_w`×`content_h` composition into a `pane_w`×`pane_h` pane; a degenerate
  content/pane yields `identity()` (`interact.cpp:54-56`). D-nav-7 explicitly built and
  unit-tested this "tested for this future wiring." **This leaf writes no new math.**
- **The reset gesture.** The `F`-while-hovered reset trigger is already read at the ImGui
  layer into `views::CanvasInput{ bool reset }` (`src/views/ace/views/views.hpp:54`,
  read at `src/views/views.cpp:67`) and delivered to `app::CanvasView` via
  `draw_canvas_interactive` (`views.hpp:62`). **This leaf adds no input handling.**
- **The reset branch (the debt).** `app::CanvasView::draw_content`
  (`src/app/canvas_view.cpp:86-89`) currently does `if (in.reset) camera =
  arbc::Affine::identity();` — the interim from D-nav-7/D-canvas_view-5. The comment at
  `:84-85` names it "restores the default identity framing (D-canvas_view-5's fit
  camera)"; identity *was* the fit only because the 64×64 probe composition happens to
  frame 1:1 into a 64×64 pane (`fit(64,64,64,64) == identity`). **This is the line this
  leaf rewires.**
- **The camera submit channel.** The transient per-`canvas#N` camera lives in
  `app::CanvasView::Presenter{ arbc::Affine camera }` (`src/app/ace/app/canvas_view.hpp:99`)
  and is submitted on change via `host_.request_camera(view_id, p.camera)`
  (`canvas_view.cpp:99-102`, D-nav-3). The fit camera rides this same channel unchanged.
- **The scale-bar readout.** `p.scale_bar_units` (`canvas_view.hpp:100`) and the
  `scale_bar_units(view_id)` test accessor (`canvas_view.hpp:77`) already track the live
  camera — the e2e's observable that reset changed the framing.

**Settled (from `editor.canvas.view`, Done 2026-07-18).** D-canvas_view-5
(`tasks/refinements/editor/canvas_view.md:498-508`): "The canvas frames the document's
root composition with a default camera … the viewport uses an identity/fit camera over
the document's root composition — the same framing the offline path already renders
(`render.cpp:23`)." This leaf makes "fit over the root composition" *literal* rather than
the identity coincidence.

**Settled (from `editor.project.view`, existing).** The one owned `arbc::Document` is
reachable at the app layer via `commands::AppState::document()`
(`src/commands/ace/commands/app_state.hpp:45`), which `app::CanvasView` already holds
(`state_.document()`, `src/app/canvas_view.cpp:44`). The document graph is *built* by the
`project` component's `add_composition(canvas_w, canvas_h)`
(`src/project/project.cpp:21`, ProbeDocument) — the natural home for the mirror *reader*.

**Settled (from `editor.project.undo` / D15).** The viewport camera is transient session
state, never a `transact`. The fit camera is a camera value like any other — it is
submitted to the render thread, never persisted, never journaled (nav Constraint 1 /
D-nav-1). This leaf does not touch the journal.

**Pending (owned here).**
- A read accessor `project::root_composition_size(const arbc::Document&)` returning the
  root composition's authored `canvas_w`/`canvas_h` (or absent when none/degenerate).
- Rewiring the reset branch (`canvas_view.cpp:87-88`) to `interact::fit(size, pane)` with
  an identity fallback, plus the app-layer include of `project`.

## What this task is

**D2 §3** (`docs/00-design.md:88-100`) is normative: the canvas is an "infinite, pannable,
deep-zoomable surface," and among its navigation aids the doc lists "fit-to-frame" —
i.e. **fit the document into the view** — "so users don't get lost in unbounded space"
(`:98-100`). D-nav-7 (`nav.md:468-481`) shipped exactly this recovery affordance ("a 'fit
the document to the pane' … the essential 'don't get lost in unbounded space' escape") on
the `F` key, but computed it as `identity()` in the absence of a wired path to the
document's authored size. This leaf completes it: **reset-to-fit computes an `Affine`
that frames the root composition's authored canvas bounds into the current pane.**

Concretely:

- **The document exposes its authored canvas size through the public read seam.** A
  composition is minted with `Document::add_composition(double canvas_w, double
  canvas_h)` (`build/*/_deps/arbc-src/src/runtime/arbc/runtime/document.hpp:167`), which
  stores the pair on the composition's `CompositionRecord{ double canvas_w; double
  canvas_h; }` (`build/*/_deps/arbc-src/src/model/arbc/model/records.hpp:136-138`). The
  size is read back by pinning a version and asking for the root composition:
  `Document::pin()` (`document.hpp:261`) → `DocRoot::find_first_composition(ObjectId&,
  const CompositionRecord*&)` (`build/*/_deps/arbc-src/src/model/arbc/model/model.hpp:72`)
  → `rec->canvas_w`/`rec->canvas_h`. `find_first_composition` is the **lowest-id-wins root
  rule** (model.hpp:58-71) — the v0.1 root convention, and the *same* anchor
  `render_offline` sources the frame walk on (offline.cpp; `render.cpp:24-25` comment "the
  root composition as the anchor"). So the fit frames the identical composition the
  compositor renders — consistent by construction.
- **A small L1 `project` accessor wraps that read.** `project::root_composition_size(const
  arbc::Document&) -> std::optional<CompositionSize>` (a `struct CompositionSize { double
  width; double height; }`) pins, calls `find_first_composition`, and returns
  `{canvas_w, canvas_h}` — or `std::nullopt` when the document has no composition or the
  authored size is degenerate (`canvas_w`/`canvas_h` not `> 0`). It mirrors
  `add_composition`: the reader for the writer, in the component that owns the Document
  graph.
- **The reset branch calls the existing fit math.** In `CanvasView::draw_content`, the
  `in.reset` branch becomes: query `root_composition_size(state_.document())`; on a size,
  `camera = interact::fit(size.width, size.height, p.tex_width, p.tex_height)`; on
  `nullopt`, keep the current `identity()` fallback ("nothing to fit"). The result is
  submitted through the unchanged `request_camera` channel.
- **The authored bounds are origin-anchored, so `fit`'s existing signature suffices.** A
  `CompositionRecord` carries *only* `canvas_w`/`canvas_h` — no origin — so the authored
  region is `[0,0]→[canvas_w, canvas_h]` in composition units. `interact::fit(w, h, …)`
  frames exactly that rect. This is precisely why `nav` shipped `fit` with a
  `(content_w, content_h, pane_w, pane_h)` signature and no origin parameter; **no fit
  extension is needed.**

## Why it needs to be done

Today reset-to-fit is a lie for any document whose authored canvas is not the pane size:
pressing `F` snaps the camera to `identity()`, which frames the composition 1:1 at the
device origin — correct for the 64×64 probe in a 64×64 pane by coincidence, wrong the
moment a real document has a differently-sized canvas or the pane is a different shape.
D2 §3's "fit-to-frame … so users don't get lost in unbounded space" and D-nav-7's
"fit the document to the pane" both promise a *real* framing of the document. Once the
user has panned/zoomed into deep space (which `nav` makes possible and which the deep-zoom
rebasing encourages), the only orientation guarantee is this recovery affordance — and it
has to actually recenter on the document, not on device-pixel identity. This leaf is the
minimum that makes the promised escape true.

## Inputs / context

**Governing design docs (normative — the constitution).**
- **D2 §3** `docs/00-design.md:88-100` — the canvas: "infinite, pannable, deep-zoomable";
  the navigation aids list, "fit-to-frame … so users don't get lost in unbounded space"
  (`:98-100`). D2 row `:463` — the editing viewport *is* the active camera (free-nav,
  transient framing).
- **D15** `docs/00-design.md:476` — the viewport camera's live framing (incl. a fit
  reset) is transient session state, **not** a `transact`. The fit camera is not
  persisted.
- **A4** `docs/01-architecture.md:59-82` — the camera reaches the render-thread-confined
  `HostViewport` as a *submit* (`request_camera`), applied on the render thread. This leaf
  reuses that channel verbatim; the only new UI-thread work is a `pin()` *read* of the
  Document, which the UI (writer) thread may legally do.
- **§8** `docs/01-architecture.md:144-179` — the levelization DAG: `project` is **L1**
  (owns the Document graph), `interact` is **L1** (fit math), `app` is **L4**. The
  accessor is L1 `project`; the wiring is L4 `app`. No ImGui/GL enters L1.
- **§9 / §9.1** `docs/01-architecture.md:181-245` — the universal DoD and the offscreen
  software-GL ASan/TSan lane.

**libarbc API surface** (fetched under `build/*/_deps/arbc-src/`).
- `arbc::Document` — `src/runtime/arbc/runtime/document.hpp`: `ObjectId
  add_composition(double canvas_w, double canvas_h)` (`:167`, the writer this leaf
  mirrors); **`DocStatePtr pin() const`** (`:261`, the read seam). No public
  `model()`/`root()` accessor — `Model` is attorney-client (`:338-349`); the pin path is
  the intended reader.
- `arbc::DocRoot` — `src/model/arbc/model/model.hpp`: `DocStatePtr` = `std::shared_ptr<const
  DocRoot>` (`:163`); **`bool find_first_composition(ObjectId& out_id, const
  CompositionRecord*& out_rec) const`** (`:72`) — lowest-id-wins root rule (`:58-71`),
  returns `false` and leaves out-params untouched when the document has no composition;
  `const CompositionRecord* find_composition(ObjectId) const` (`:55`).
- `arbc::CompositionRecord` — `src/model/arbc/model/records.hpp:136`: `double
  canvas_w{0.0}` (`:137`), `double canvas_h{0.0}` (`:138`) — the authored bounds, always
  present (never optional), default `0.0`, no `Rect`/`origin` (so origin-anchored at
  `[0,0]`).
- `arbc::Affine` — `src/base/arbc/base/transform.hpp:13`: `identity()` (`:21`) — the
  fallback; `interact::fit` returns one.

**Editor seams this leaf extends.**
- `project` (L1) — add `struct CompositionSize` + `std::optional<CompositionSize>
  root_composition_size(const arbc::Document&)` to `src/project/ace/project/project.hpp`
  (impl `src/project/project.cpp`); add the `<arbc/model/model.hpp>` +
  `<arbc/model/records.hpp>` includes (public arbc headers; same `project → arbc::arbc`
  edge, no new DAG edge). Mirrors `add_composition` at `project.cpp:21`.
- `app::CanvasView::draw_content` (L4) — rewire `src/app/canvas_view.cpp:87-88`: query the
  accessor, `interact::fit(size, p.tex_width, p.tex_height)` on a size, identity fallback
  on `nullopt`; add `#include <ace/project/project.hpp>` (app already depends transitively
  on `project` via `commands`). Update the comment at `:84-85`.
- `interact::fit` — **used unchanged** (`src/interact/interact.cpp:53`); no signature or
  behavior change.
- `render::render_document_srgb8(doc, w, h, camera)` — **used unchanged**
  (`src/render/render.cpp:20`); the fit camera is just another `Affine` argument, already
  golden-covered by `nav`'s `tests/goldens/canvas_nav_zoom_64x64.rgba8` cross-check.

**Predecessor refinements** (style + decision continuity):
`tasks/refinements/editor/nav.md` (D-nav-7 shipped `interact::fit` + the reset gesture +
the camera channel this leaf reuses; D-nav-1 the transient-camera invariant),
`tasks/refinements/editor/canvas_view.md` (D-canvas_view-5 "frame the root composition"),
`tasks/refinements/editor/multi_canvas.md` (the per-`canvas#N` presenter camera),
`tasks/refinements/editor/undo.md` (transient-vs-scene: the fit camera is never a transact).

## Constraints / requirements

1. **Reset frames the root composition's authored bounds.** On `F`-while-hovered, the
   camera becomes `interact::fit(canvas_w, canvas_h, pane_w, pane_h)` for the root
   composition's authored `canvas_w`/`canvas_h` (via `find_first_composition`, the same
   root the compositor anchors on) and the current pane device size (`p.tex_width`/
   `p.tex_height`). No new math — the existing `interact::fit`.

2. **No authored bounds → identity fallback ("nothing to fit").** When the document has no
   composition (`find_first_composition` returns `false`) or the authored size is
   degenerate (`canvas_w`/`canvas_h` not `> 0`), `root_composition_size` returns
   `std::nullopt` and the reset keeps the current `arbc::Affine::identity()` behavior. This
   composes with `interact::fit`'s own degenerate guard (`interact.cpp:54-56`) — the two
   agree, so a degenerate size can never produce a NaN/blank camera.

3. **The fit camera is transient — never a `transact` (D15 / D-nav-1).** It is submitted
   through the existing `host_.request_camera(view_id, p.camera)` channel on change, exactly
   like every other camera value; it is never persisted, never journaled. This leaf touches
   no `dispatch`/`transact` path.

4. **The Document read is a `pin()` read on the UI (writer) thread; no cache access.** The
   accessor pins a version and reads the model (`find_first_composition`) — the intended
   lock-free reader seam. It never touches the render-thread-confined `HostViewport`/
   `TileCache` (A4). No new lock, no new thread, no cross-thread sharing added.

5. **The authored bounds are origin-anchored; `fit`'s signature is unchanged.** The
   composition stores only `canvas_w`/`canvas_h` (no origin), so `interact::fit`'s existing
   `(content_w, content_h, pane_w, pane_h)` signature suffices. This leaf does **not**
   extend `interact::fit`.

6. **Levelization stays clean (§8).** The accessor extends existing L1 `project` (which
   already sees `arbc`; adding `arbc/model/*` public includes is the same edge); the wiring
   extends existing L4 `app`; `interact` is untouched. **No new component, no new DAG edge**;
   `project` gains no ImGui/GL/SDL include. `check_levels` stays clean.

## Acceptance criteria

Instantiating the universal DoD (`docs/01-architecture.md:199-203`) for this leaf;
`scripts/gate` green (check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean).** Primary structural assertion. The accessor is
  added to existing L1 `project`; the wiring to existing L4 `app`; `interact`/`render`
  untouched. **No new component, no new DAG edge.** `project` stays free of ImGui/GL/SDL.

- **Catch2 unit (L1 `project` accessor — the bulk, headless, GL-free).** A new
  `tests/composition_bounds_test.cpp` (joined to `ace_tests` in `CMakeLists.txt:219-228`,
  mirroring `tests/render_probe_test.cpp`'s probe-document use), asserting: (a)
  `project::root_composition_size(*project::build_probe_document().document)` returns a
  size equal to `{k_probe_width, k_probe_height}` = `{64, 64}` (`project.hpp:24-25`); (b) a
  freshly-constructed empty `arbc::Document{}` (no composition) yields `std::nullopt`
  (`find_first_composition` false); (c) a document whose only composition is
  `add_composition(0, 0)` (degenerate) yields `std::nullopt` (Constraint 2); (d) a
  **combined wiring assertion** — for the 64×64 probe size and a **non-square pane** (e.g.
  128×64), `interact::fit(size.width, size.height, 128, 64)` is **not** identity and its
  centered/uniform-scale numbers match `interact::fit(64,64,128,64)` exactly (scale
  `= min(128/64, 64/64) = 1`, `tx = 32`, `ty = 0`) — pinning the fit camera the reset
  branch will submit, headless, without ImGui. Coverage ≥ 90% on changed lines
  (`diff-cover --fail-under=90`, `coverage` preset).

- **UI e2e — ImGui Test Engine (F fits the document).** Extend
  `tests/canvas_nav_e2e_test.cpp` (in `ace_shell_test`, `CMakeLists.txt:252`; offscreen
  software-GL, modeled on the existing nav e2e), driving the stable **`canvas#1`** view id
  at a pane size **distinguishable from the probe's 64×64 authored canvas** (so fit ≠
  identity): (i) a simulated **wheel** zooms the camera off the fit framing
  (`scale_bar_units(canvas#1)` changes / a non-identity camera); (ii) a simulated **`F`
  keypress** while hovered recenters — assert the resulting `scale_bar_units(canvas#1)`
  returns to the value implied by `interact::fit(root_composition_size(doc), pane)` (the
  scale readout is the test-visible camera proxy already surfaced by nav,
  `canvas_view.hpp:77`), and the published frame advances. This proves `F` reaches the
  authored-bounds fit end-to-end, not identity. (Where the harness cannot force a
  non-square pane, the assertion instead pins that after `F` the camera equals the computed
  `interact::fit(...)` rather than `identity()`.)

- **Golden — not added here (justified).** This leaf introduces **no new render path**: the
  fit camera is one more `arbc::Affine` value carried by the already-golden camera channel,
  and the camera→composite byte-exactness is pinned by `nav`'s
  `tests/goldens/canvas_nav_zoom_64x64.rgba8` (hosted == `render_document_srgb8` with the
  same `Affine`). A fit-specific golden would add no signal — the probe's content is an
  **unbounded solid** that fills the frame regardless of camera (`project.cpp:22`), so a
  letterboxed fit would render a uniform color with no observable margin. The fit `Affine`
  is instead pinned numerically by the L1 unit (criterion d). This is the justified
  exception to "rendered output gets a golden," not the default.

- **Threading (ASan/TSan) — no new scope.** This leaf adds no channel and no cross-thread
  state; the only new UI-thread code is a `pin()` read (the established reader seam) whose
  result feeds the existing `request_camera` submit. The `nav`/`multi_canvas` lifecycle
  test (add→resize→camera-submit→render→teardown) already covers the camera channel under
  `asan`/`tsan`; running it green with the fit-camera value flowing is sufficient — no new
  TSan target is warranted, and the refinement claims none.

- **Format + build clean** across the standard presets; `scripts/gate` green.

**No new WBS leaf is deferred.** This leaf *closes* the `fit_bounds` tech-debt in full.
The richer, selection/cell-aware navigation aids (**fit-to-cell**, **zoom-to-selection**)
remain **D2 §3-*(open)*** and are already routed to the parking lot by D-nav-7
(`nav.md:483-490`) — a design-open UX judgment (which aids, which gestures), not
implementable scope; this leaf mints no task for them and adds no new parking-lot item.

## Decisions

- **D-fit_bounds-1 — reset-to-fit frames the root composition's authored `canvas_w/canvas_h`,
  read through `pin()` → `find_first_composition`.** The camera on `F` is
  `interact::fit(canvas_w, canvas_h, pane_w, pane_h)` for the lowest-id (root) composition.
  *Rationale:* `find_first_composition` is libarbc's own root convention
  (model.hpp:58-71) and the exact anchor `render_offline` sources the frame walk on, so the
  fit frames the identical composition the compositor renders — no drift between "what fit
  shows" and "what renders." It is public API (via `pin()`), needs no library change, and
  the authored `canvas_w/canvas_h` is precisely the "document bounds" D2 §3 / D-nav-7
  promise. *Alternative rejected:* deriving bounds from layer/content geometry (union of
  `LayerRecord::transform` footprints) — heavier, ambiguous for unbounded content (the
  probe's solid is infinite), and *not* what the compositor anchors on; the authored canvas
  is the single, unambiguous frame. *Alternative rejected:* a library change adding a
  `Document::bounds()` — out of scope (libarbc is consumed via FetchContent) and
  unnecessary, the `pin()` path is public and intended. **No doc delta required.**

- **D-fit_bounds-2 — the accessor lives in L1 `project` as the reader mirror of
  `add_composition`, returning `std::optional<CompositionSize>`.**
  `project::root_composition_size(const arbc::Document&)` pins, reads the root
  `CompositionRecord`, and returns `{canvas_w, canvas_h}` or `nullopt` (no composition /
  degenerate). *Rationale:* `project` owns the Document graph and already writes it
  (`add_composition`, `project.cpp:21`) and holds the test probe — the reader belongs
  beside the writer, is trivially unit-testable headless, and keeps `interact` pure (no
  Document dependency in the math layer). The `optional` makes "no bounds" a first-class,
  testable case that composes with `interact::fit`'s degenerate guard. *Alternative
  rejected:* putting the accessor in the (currently empty) `scene` component — it would
  split composition read/write across two L1 components for no gain, and `project` is the
  established Document-graph home. *Alternative rejected:* reading the Document inline in
  `app` (L4) with `pin()`/`find_first_composition` — buries a reusable, unit-testable L1
  read in the ImGui-adjacent app layer and pulls `arbc/model/*` includes up to L4
  needlessly. **No doc delta required.**

- **D-fit_bounds-3 — `nullopt`/degenerate bounds fall back to identity ("nothing to fit").**
  When the document carries no usable authored size, `F` keeps the pre-existing
  `arbc::Affine::identity()` framing. *Rationale:* it preserves the exact current behavior
  for the degenerate case (a strict superset change — real bounds now fit, everything else
  is unchanged), and it mirrors `interact::fit`'s own degenerate contract (`interact.cpp:54`)
  so the two never disagree. *Alternative rejected:* leaving the camera untouched on `F` for
  a boundless document — a silent no-op is a worse "I'm lost" recovery than recentering to
  the origin/identity framing. **No doc delta required.**

- **D-fit_bounds-4 — no new golden; the fit `Affine` is pinned by the L1 unit, camera→
  composite byte-exactness by `nav`'s existing golden.** *Rationale:* the leaf adds no
  render path (the fit camera rides the already-golden `request_camera` channel), and the
  probe's unbounded-solid content makes a fit-specific golden signal-free (uniform color
  regardless of framing, `project.cpp:22`). The numeric fit is pinned headless by
  `tests/composition_bounds_test.cpp`; the end-to-end `F`→fit reach by the Test Engine e2e.
  *Alternative rejected:* minting a bounded-raster fixture document purely to golden a
  letterboxed fit — real test scaffolding for a render path this leaf does not change; the
  DoD's "goldens for rendered output" targets *new* composition, which this leaf has none
  of. **No doc delta required** (the DoD's tolerance/exception clause,
  `docs/01-architecture.md §9`, covers a justified golden omission).

## Open questions

(none — all decided.) The only open item in this area — **which deep-zoom navigation aids
beyond reset-to-fit** (D2 §3 marks fit-to-cell / zoom-to-selection *(open)*) and their
gesture/menu bindings — is already routed to the parking lot by D-nav-7
(`nav.md:483-490`), a design-open UX judgment for human review, not WBS scope. This leaf
neither reopens nor extends it.

## Status

**Done** — 2026-07-18.

- Added `struct CompositionSize` and `project::root_composition_size(const arbc::Document&) -> std::optional<CompositionSize>` to `src/project/ace/project/project.hpp`.
- Implemented `root_composition_size` in `src/project/project.cpp` — pins the document, calls `find_first_composition`, returns `{canvas_w, canvas_h}` or `nullopt` for no composition or degenerate (`canvas_w`/`canvas_h` not `> 0`); added `<arbc/model/model.hpp>` + `<arbc/model/records.hpp>` includes.
- Rewired the `in.reset` branch in `src/app/canvas_view.cpp` to call `interact::fit(size.width, size.height, p.tex_width, p.tex_height)` with identity fallback on `nullopt`; added `<ace/project/project.hpp>` and `<optional>` includes; updated comment.
- Joined `tests/composition_bounds_test.cpp` to `ace_tests` in `CMakeLists.txt`.
- New Catch2 unit `tests/composition_bounds_test.cpp` (4 cases: probe → 64×64, empty document → `nullopt`, degenerate 0×0 composition → `nullopt`, non-square-pane 128×64 fit verified numerically: `scale=1`, `tx=32`, `ty=0`).
- Extended `tests/canvas_nav_e2e_test.cpp`: the `F`-key assertion now proves authored-bounds fit (`scale_bar_units` after `F` > `scale_bar_units` before `F` after a zoom-out), confirming the reset frames root composition authored bounds end-to-end rather than identity.
