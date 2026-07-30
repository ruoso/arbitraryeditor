# editor.panels.color_eyedrop_cell — eyedropper modifier: the active cell's own straight color via an isolated single-cell render

## TaskJuggler entry

- **Task:** `editor.panels.color_eyedrop_cell` (`tasks/00-editor.tji:646-651`, under
  `task panels "Info panels"` at `:603`).
- **Effort:** `1d` · `allocate team`.
- **Depends:** `editor.panels.color` (`tasks/00-editor.tji:649`).
- **Note (`.tji:650`):** *"D10 eyedropper modifier that samples the active cell's
  own straight color (not the composite): render only the selected cell in
  isolation through the active camera at the sample point, unpremultiply →
  linear_to_srgb8 → set_active_color, bound to the eyedropper modifier (e.g.
  Alt-click). Separable from the base composited eyedropper (editor.panels.color)
  because it needs isolated single-cell rendering (siblings hidden). Design: D10
  (docs/00-design.md:477)."* The governing row is **D10** (`docs/00-design.md:477`),
  elaborated by **§7** (`:271-294`, the modifier variant pinned at `:290`).
- **Back-link:** the closer appends `Refinement:
  tasks/refinements/editor.panels/color_eyedrop_cell.md` to the `.tji` note (after
  the design-doc citation) and adds `complete 100` immediately after `allocate
  team` (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji` here.
- **Milestone:** `m9_editor` (`tasks/99-milestones.tji:6-10`) already carries the
  whole `editor.panels` area — no new milestone edge for this leaf.

## Effort estimate

**1 day.** This leaf is a near-twin of the composited sampler that `editor.panels.color`
already shipped — one new L1 `commands` sampler, one branch in the (now live)
eyedropper arm, and the tests:

- The **isolated sampler** mirrors `commands::sample_composited_color`
  (`src/commands/color.cpp:34-61`) almost line-for-line: build a 1×1 target,
  translate the camera so the sample point lands at pixel (0,0), render, decode the
  premul-linear pixel. The only structural difference is the render call — one
  **single layer** via the library's public `arbc::render_layer`
  (`<arbc/compositor/compositor.hpp>:58`) instead of the whole-document
  `arbc::render_offline` — and a leaf/operator gate.
- The **arm branch** reads the already-populated `in.alt` on the `CanvasInput`
  the arm receives (`src/views/views.cpp:158`, `in.alt = io.KeyAlt`) — no plumbing
  change — resolves the selected cell through the same `scene::cells` path the
  brush already walks (`src/app/canvas_view.cpp:562-576`), and calls the new
  sampler, falling back to the shipped composited sampler otherwise.
- The **decode back to sRGB** is the *identical* `commands::working_to_srgb`
  (`src/commands/color.cpp:26-32`, `unpremultiply` + `linear_to_srgb8` +
  `unorm8_encode`) the base arm already calls — no new conversion.

The estimate is dominated by the isolation golden (the byte-exact
own-color-under-an-occluder assertion) and the e2e that proves the modifier picks
the *back* cell's own color where a plain click picks the composite.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.panels.color`** (`tasks/refinements/panels/color.md`, **Done**
  2026-07-30) — the entire boundary this leaf extends. It shipped:
  - the **inert-no-more eyedropper arm** `CanvasView::dispatch_eyedropper`
    (`src/app/canvas_view.cpp:629-645`, declared `canvas_view.hpp:417`, dispatched
    `:390-393`), which today samples the composited result unconditionally;
  - the **composited sampler** `commands::sample_composited_color(const
    arbc::Document&, const arbc::Affine& camera, double device_x, double device_y)`
    (`src/commands/ace/commands/color.hpp:64-66`, impl `color.cpp:34-61`) — the
    1×1-`render_offline` pattern this leaf clones for a single layer;
  - the **sRGB↔working conversion pair** and `SrgbColor` type
    (`color.hpp:31-38,45,51`, `color.cpp:16-32`), including
    `working_to_srgb` (`color.cpp:26-32`) which *is* the D10 `unpremultiply →
    linear_to_srgb8` decode this task's note names — reused verbatim;
  - the **active-color home** on `AppState`: `active_color()`/`set_active_color()`
    (`app_state.hpp:177-178`), `active_working_color()` (`:184`, `app_state.cpp:101-105`),
    field `active_color_` (`app_state.hpp:294`) — the write target;
  - the **modifier availability**: `views::CanvasInput` carries `bool alt`/`shift`/`ctrl`
    (`src/views/ace/views/views.hpp:68-70,83`), populated every frame from ImGui
    (`src/views/views.cpp:156-159`); the eyedropper arm receives this `in` and can
    read `in.alt` today with no new plumbing (other arms already read
    `in.ctrl`/`in.shift`, e.g. `canvas_view.cpp:823,1024`);
  - the **test rigs** this leaf extends: `tests/color_test.cpp` (the L1 render
    golden-in-a-value pattern, `:164-193`) and `tests/color_e2e_test.cpp` (the
    boot-the-real-shell + Eyedropper-tool-drive rig, `:185-343`).
  - **D-color-4** (`panels/color.md:381-396`) explicitly deferred this modifier to
    the present leaf because it *"needs isolated single-cell rendering (siblings
    hidden) the composited path does not,"* naming it as *"a named WBS leaf, not an
    audit task."* This refinement discharges that debt.

**Pending (owned here):** the L1 `commands::sample_cell_color` isolated sampler
(returning `std::optional<arbc::WorkingPixel>`); the Alt-modifier branch in
`dispatch_eyedropper` with graceful fallback to the composited sampler; and the
Catch2 unit, the `render_offline`/`render_layer` byte-exact golden-in-a-value, the
ImGui Test Engine e2e phase, and the TSan case. Nothing downstream is blocked — the
sole dependency is Done.

## What this task is

One gesture: **Alt-click with the Eyedropper tool samples the selected cell's own
straight color, rendered in isolation, instead of the composited result.** D10
(`:477`) and §7 (`:290`) promise exactly two eyedropper variants — *"Default: the
composited result … modifier: the active cell's own straight color"* — and
`editor.panels.color` shipped only the default. This leaf adds the modifier.

Concretely:

1. **An L1 isolated single-cell sampler** — `commands::sample_cell_color`,
   twin of `sample_composited_color`, that renders **one layer** (the selected
   cell's placing layer) through the active camera at the click point via the
   library's public `arbc::render_layer`, siblings absent, and returns the raw
   premul-linear `WorkingPixel`. It returns **`std::nullopt`** when the selected
   object is not an isolable leaf cell (empty selection, a camera, or a content
   that needs the pull service — see Decisions), so the caller degrades gracefully.
2. **The Alt branch in the eyedropper arm** — on an Eyedropper click with `in.alt`
   held, resolve the active cell (`state_.selection().primary()` matched through
   `scene::cells`), call `sample_cell_color`, and on a value decode it with the
   existing `working_to_srgb` and `set_active_color`. On `std::nullopt` (or Alt
   not held), fall through to the shipped composited sample — so the gesture always
   yields a meaningful color, never a garbage or empty one.

The decode, the write target, and the plain-click default are all unchanged; this
leaf adds only the *isolated render* and the *modifier branch that chooses it*.

## Why it needs to be done

The composited eyedropper answers *"what color is on screen here?"*. The active-cell
modifier answers a different, equally-designed question — *"what color is **this
cell**, unmixed with whatever sits over or under it?"* — which is the one a painter
needs to grab a partially-occluded cell's true color, or to lift a solid's exact
value out from behind a semi-transparent sibling. §7 makes this a first-class D10
promise (`:290`), and `editor.panels.color`'s D-color-4 parked it precisely because
it requires a rendering capability — **single-cell isolation** — that the whole-document
`render_offline` path cannot express (`panels/color.md:388-393`). This leaf builds
that capability once, in L1, byte-exact and headless, so the modifier is real rather
than an approximation of the composite.

## Inputs / context

**Governing design rows (normative — the constitution):**

- `docs/00-design.md:477` **D10** — the color boundary; eyedropper samples
  *"displayed sRGB through the active camera (composited result by default,
  **active-cell via modifier**)."* This leaf is the modifier half.
- `docs/00-design.md:287-290` **§7** — *"with no fixed pixel buffer, 'the color
  here' is a render through a camera, not a lookup. Default: the composited result
  …; **modifier: the active cell's own straight color**."* The "render through a
  camera" clause is why isolation is a *render*, not a buffer mask.
- `docs/00-design.md:271-279` **§7 boundary principle** — decode on the way out
  (*"display, eyedropper"*); *"Premultiplied and linear values are never shown"*
  (`:279`). The isolated sample is decoded to sRGB before it ever reaches the user.
- `docs/01-architecture.md` **§8** the levelization DAG (`:308-346`), **§9** the
  per-leaf DoD (`:348-375`).

**Predecessor decisions (`tasks/refinements/panels/color.md`):**

- **D-color-4** (`:381-396`) — the composited eyedropper renders through a camera
  (not a GL readback), the sampler is **L1 `commands` over the library render**
  (byte-exact, headless), and the active-cell modifier is a separable named leaf.
  This refinement realizes that separation with the *same* L1-render discipline.
- **D-color-2** (`:357-373`) — sRGB 8-bit straight-alpha is the stored truth;
  premul-linear is derived at the boundary via the library primitives, never a
  hand-rolled transfer function. The isolated decode obeys the same rule (it reuses
  `working_to_srgb`).

**Source seams (editor):**

- Eyedropper arm (now live, composited-only): `src/app/canvas_view.cpp:629-645`;
  the sample point is `in.focus_x, in.focus_y` (`:643`), the active camera is
  `Presenter::camera` (`canvas_view.hpp:231`, `arbc::Affine`). The comment at
  `:637-638` names this task as the modifier's home.
- Modifier state on the input: `views::CanvasInput` `bool alt`
  (`src/views/ace/views/views.hpp:69`), set from `io.KeyAlt`
  (`src/views/views.cpp:158`). Available to the arm with no plumbing change.
- Selection → cell: `commands::AppState::selection()` (`app_state.hpp:139-140`) →
  `commands::Selection::primary()` returns the primary `arbc::ObjectId` (the
  **Content** id) (`src/commands/ace/commands/selection.hpp:28`). The placing layer
  + placement come from `scene::cells(document, registry, active_composition)`
  (`src/scene/ace/scene/cell.hpp:249,257`), whose `struct Cell` carries
  `id`/`layer`/`kind_id`/`placement`/`content_bounds`/`visible`
  (`cell.hpp:216-239`); the brush already walks exactly this to capture
  `cell.placement` (`src/app/canvas_view.cpp:562-576`). Entered-scope resolution is
  `scene::active_composition` (`cell.hpp:273`) — the same fail-safe the brush uses.
- The composed content→device transform: the brush composes `p.camera` with
  `cell.placement` for its footprint (`canvas_view.cpp:594-595`); the isolated
  sampler composes the identical `(camera, placement)` pair, so it inherits the
  brush's proven handling of the active/entered composition frame.
- The reused decode + write: `commands::working_to_srgb`
  (`src/commands/color.cpp:26-32`) and `AppState::set_active_color`
  (`app_state.hpp:178`).

**Source seams (vendored libarbc, fetched via CMake FetchContent under
`build/*/_deps/arbc-src/`; cited by logical include path):**

- **`arbc::render_layer`** — `<arbc/compositor/compositor.hpp>:58`:
  `void render_layer(const ContentResolver& resolve, const LayerRecord& layer,
  const Affine& composed, const Rect& device_rect, Backend& backend,
  SurfacePool& pool, Surface& target);` — renders **exactly one layer** into a
  caller-provided target, with the full cull/compose/region path, and is
  `ARBC_API` *"declared here so the anchored walk reuses it verbatim"*
  (`compositor.hpp:53-57`). This is the library's single-layer primitive.
- **`arbc::render_offline`** — `<arbc/runtime/offline.hpp>:20,42`, whose 1×1-viewport
  setup (`src/runtime/offline.cpp:44-67`: `ContentResolver`, `SurfacePool`,
  `CpuBackend`) is the template for how the isolated sampler stands up its
  render substrate; it **cannot** anchor at a single cell — `Viewport::anchor`
  (`<arbc/compositor/compositor.hpp>:16`) scopes to a whole composition.
- **The leaf/operator gate** — `arbc::is_operator(content)`
  (`<arbc/compositor/operator_graph.hpp>:80-85`, *"the sole leaf/operator test"*)
  and `arbc::Content::inputs()` / `composition_ref()`
  (`<arbc/contract/content.hpp>`): a content that **is an operator or references a
  child composition** pulls inputs through the pull service that a bare
  `render_layer` does not stand up (`src/runtime/offline.cpp:44-52`;
  `src/runtime/interactive.cpp:127-152`). A self-contained leaf (solid/raster/image)
  needs neither.
- **The decode primitives** — `<arbc/media/pixel_traits.hpp>`: `unpremultiply`
  (`:30`), `linear_to_srgb8` (`:48`), `unorm8_encode` (`:56`) — all already wrapped
  by `working_to_srgb`; `arbc::visit_surface` + `PixelTraits<…>::decode` for the
  1×1 read-back (the pattern at `color.cpp:55-61`).

**Levelization lint:** `scripts/check_levels.py` — `commands` is in the `arbc`
external allow-list (so it may include `<arbc/compositor/…>`, `<arbc/contract/…>`
just as it already includes `<arbc/runtime/offline.hpp>`); `views` already depends
on `commands`; `dockmodel` remains `{base, platform}`-only and untouched.

## Constraints / requirements

1. **Isolation is a *render*, not a mask or a buffer lookup** (D10, §7:288). The
   isolated sample renders **one layer** via `arbc::render_layer` through the active
   camera at the click point — never a GL framebuffer readback, never masking the
   composited buffer. The sampler is **L1 `commands`** (headless, byte-exact),
   twinning `sample_composited_color`.
2. **The sampler renders the selected cell's placing layer only, siblings absent.**
   `commands::sample_cell_color` builds a 1×1 target, computes the composed
   content→device transform as `compose(camera, placement)` translated so the
   sample point lands at output pixel (0,0) (the exact
   `compose(translation(-x,-y), …)` trick of `color.cpp:43-46`), reads the layer's
   `LayerRecord` (placement, content ref, visibility) off the **pin**, calls
   `render_layer` into the target, and decodes pixel (0,0) via `visit_surface` +
   `PixelTraits::decode`. No document mutation, no transaction, no writer thread —
   it is a pure pinned read, exactly like `sample_composited_color`.
3. **Only self-contained leaf cells are sampled in isolation; everything else
   returns `std::nullopt`** (D-eyedrop_cell-2). `sample_cell_color` returns
   `std::nullopt` when the selected object is not a placed cell (empty selection or
   a camera → not found in `scene::cells`) **or** when its content is an operator /
   references a child composition (`is_operator(content) || content.composition_ref()`
   valid), because a bare `render_layer` cannot stand up the pull service those
   contents need. The gate is the library's own structural predicate — **no editor
   kind allow-list** (respects D-cells_model-8's no-allowlist stance).
4. **The arm degrades gracefully; the gesture always yields a color** (D-eyedrop_cell-3).
   In `dispatch_eyedropper`: if `in.alt` is held **and** `sample_cell_color`
   returns a value → decode with `working_to_srgb` and `set_active_color`.
   Otherwise (Alt not held, or `std::nullopt`) → the shipped composited sample,
   unchanged. Alt-clicking a cell the isolated path cannot represent therefore
   behaves like a plain composited pick rather than doing nothing or writing empty.
5. **The decode and write target are the shipped ones** (D-color-2). The premul-linear
   sample is turned into sRGB by the *same* `working_to_srgb` (`unpremultiply` +
   `linear_to_srgb8` + `unorm8_encode`, `color.cpp:26-32`) and written through the
   *same* `set_active_color` the base arm uses. No new conversion, no new state.
   Premultiplied/linear values are never surfaced (§7:279).
6. **The modifier is Alt, provisionally** (D-eyedrop_cell-4). Read via `in.alt`
   inside the arm (no plumbing change). Alt is free in the Eyedropper context (the
   arm reads no other modifier). The binding is **provisional** pending the full
   input map (`docs/00-design.md:507` "Full input map", the same provisional status
   D24's keyboard bindings carry) — recorded here, not encoded as a WBS leaf.
7. **Levelization (§8) holds with no new edge.** `sample_cell_color` lives in L1
   `commands` beside `sample_composited_color`, including only libarbc +
   existing commands headers (it gains `<arbc/compositor/…>` /
   `<arbc/contract/content.hpp>` — legal, `commands` is arbc-allow-listed and
   already pulls `<arbc/runtime/offline.hpp>`). The arm branch is L4 `app`. No new
   component, no new DAG edge, `dockmodel` untouched, the L1 core gains no
   ImGui/GL/SDL include. `scripts/check_levels.py` is not edited.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`); `scripts/gate`
green (check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.** No
  new component, no `check_levels.py` edit, `dockmodel` untouched.
  `src/commands/color.{hpp,cpp}` gains `sample_cell_color` (libarbc-only deps);
  `src/app/canvas_view.cpp` gains the Alt branch in `dispatch_eyedropper` (the only
  ImGui/render-facing change). Asserted by inspection and by the lint.
- **L1 logic — Catch2 unit** (extend `tests/color_test.cpp`; naming
  `TEST_CASE("color: …")` per the file's convention):
  - **Own-color-under-an-occluder (Constraints 1, 2) — `render_layer` byte-exact
    golden-in-a-value.** Over a known small document — a **back** solid cell of a
    distinct premul-linear color, partly covered by an opaque **front** solid cell
    (the front over the back in z-order, extending the two-cell construction at
    `color_test.cpp:169-177`) — at a point inside the overlap: `sample_cell_color`
    of the **back** cell's layer returns the back cell's own straight color
    **byte-exactly** (matched against a hand-driven `render_layer` of that one layer,
    the golden-in-a-value form), while `sample_composited_color` at the same point
    returns the **front/composite** color. This is the isolation proof: the two
    samplers disagree exactly where a sibling occludes.
  - **Leaf sample = own straight color (Constraint 5).** For a single semi-transparent
    solid cell, `working_to_srgb(*sample_cell_color(...))` equals the cell's authored
    sRGB straight color (round-trip within the 8-bit codec), confirming the
    `unpremultiply` recovers straight color, not premultiplied.
  - **Gate returns `std::nullopt` (Constraint 3).** `sample_cell_color` returns
    `std::nullopt` for an empty selection / a layer id that names no cell; and — over
    a document holding a nested-composition (or operator) cell — for that cell's
    layer, because its content is not an isolable leaf. (If constructing a nested
    cell headlessly is disproportionate for the unit, the operator/nested branch of
    the gate is exercised in the e2e's fallback phase below; the not-a-cell branch
    is unit-tested unconditionally.)
- **Rendered output — golden.** The own-color-under-an-occluder case above **is**
  the `render_layer` byte-exact check for this leaf (a single-pixel isolated composite
  pinned as an exact `WorkingPixel`, the same golden-in-a-value form the base
  `sample_composited_color` test uses, `color_test.cpp:164-193`, because the
  observable is one pixel, not a raster image). No new image-file golden.
- **UI e2e — ImGui Test Engine** (extend `tests/color_e2e_test.cpp`, reusing its
  boot-the-real-shell + Eyedropper-drive rig, `:185-343`; offscreen software-GL):
  - **Modifier → own color (the isolation proof).** Seed a **front** leaf cell of a
    known color partly occluding a **back** leaf cell of a distinct known color.
    Select the **back** cell, hold Alt (drive `io.KeyAlt` via the test engine key
    state), and Eyedropper-click a point inside the overlap: `active_color()`
    equals the **back** cell's own sRGB. Then a **plain** (no-Alt) Eyedropper click
    at the same point yields the **front/composite** color — the modifier and the
    default visibly diverge.
  - **Graceful fallback (Constraint 4).** Alt-Eyedropper-click with **no cell
    selected** (or a selection the isolated path cannot represent) sets
    `active_color()` to the **composited** sample, not empty/black — proving the
    `std::nullopt` fallback.
  - A non-byte-exact ColorPicker4 screenshot baseline already exists from
    `editor.panels.color`; no new screenshot baseline is required (the observable
    here is `active_color()` state, not panel chrome).
- **Threading (ASan/TSan).** Extend the color TSan case
  (`tests/canvas_host_test.cpp`, over the inline `WorkerPoolConfig{}` pool per the
  `arbc-nested-render-worker-detach-race` memory / e2e rule): the UI thread runs the
  Alt-branch `sample_cell_color` → `set_active_color` concurrent with a live render
  walk, TSan-clean. Like the base sampler, `sample_cell_color` is a **pinned read**
  and the resulting `WorkingPixel` crosses to the writer only as a copied value —
  no shared mutable color/render state crosses threads. No new lane, no new
  suppression.
- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed lines;
  clang-format + build clean. Tests ship with the task.
- **Deferred WBS work.** One follow-up, named crisply for mechanical registration
  (closer wires it into `m9_editor` via the `editor.panels` area, beside this leaf):
  - **`editor.panels.color_eyedrop_nested`** (effort **1.5d**, `depends
    editor.panels.color_eyedrop_cell`) — extend the active-cell modifier to the
    contents `sample_cell_color` currently returns `std::nullopt` for: **nested
    compositions and operator/procedural cells**, whose own color requires the pull
    service a bare `render_layer` cannot stand up. Render the one cell in isolation
    through an **ephemeral single-object composition** (rebuild the content via the
    registry factory into an anonymous `Document`, anchor a `Viewport` at it, and
    `arbc::render_offline` through the full pull-service pipeline —
    `src/runtime/offline.cpp:44-67`), then decode as here; the modifier stops
    falling back to the composite for those cells. Concrete, agent-implementable
    (a new L1 sampler over an ephemeral document, no design judgment). Note cites
    this refinement. *(Deferred to `editor.panels.color_eyedrop_nested`; closer
    registers in WBS.)*

## Decisions

- **D-eyedrop_cell-1 — The isolated single-cell sample is an L1 `commands`
  sampler over the library's public `arbc::render_layer`, twinning
  `sample_composited_color`.**
  `commands::sample_cell_color(const arbc::Document&, const arbc::Affine& camera,
  arbc::ObjectId layer, double device_x, double device_y) →
  std::optional<arbc::WorkingPixel>` builds a 1×1 target, composes
  `camera ∘ layer.placement` shifted so the sample point lands at pixel (0,0),
  calls `render_layer` for that one layer, and decodes premul-linear via
  `visit_surface`/`PixelTraits::decode`.
  *Rationale:* §7:288 makes isolation *"a render through a camera, not a lookup,"*
  and D-color-4 already established L1-render-over-libarbc as the sampler discipline
  (byte-exact, headless-goldenable). `render_layer` is the library's **public**
  single-layer primitive (`compositor.hpp:53-58`), so single-cell isolation needs
  **no document mutation and no writer thread** — the sampler stays a pure pinned
  read, structurally identical to the shipped composited one.
  *Alternative rejected — visibility-toggle around `render_offline`:* hide every
  sibling layer via `Model::Transaction::set_visible`, render the whole composition,
  roll back. It reuses the full pipeline (incl. the pull service) but **mutates the
  shared `Model` on a transaction** — a writer-thread, racy-against-live-edits
  operation for what must be a passive UI-thread sample; the exact opposite of the
  pinned-read seam the base sampler set. Rejected for v1.
  *Alternative rejected — ephemeral single-object composition:* correct for **all**
  cell kinds (full pull service, no mutation) but requires reconstructing/rebinding
  the `Content` into an anonymous `Document` via the registry factory — materially
  more code than a 1d leaf, and unnecessary for the leaf-cell common case. Deferred
  (it is exactly `editor.panels.color_eyedrop_nested`), not rejected. **No doc delta
  required** (realizes D10/§7:290).
- **D-eyedrop_cell-2 — The leaf/operator gate is the library's own
  `is_operator || composition_ref` predicate, not an editor kind allow-list.**
  `sample_cell_color` returns `std::nullopt` when the selected content
  `is_operator(content)` or has a valid `composition_ref()` (a nested composition),
  because those pull inputs through the pull service (`offline.cpp:44-52`;
  `interactive.cpp:127-152`) that a bare `render_layer` does not stand up; a
  self-contained leaf (solid/raster/image, and any future leaf kind) needs neither
  and renders correctly.
  *Rationale:* the distinction that matters is **structural** (does this content
  pull inputs?), not a fixed list of kind strings — and the library exposes the
  authoritative test (`operator_graph.hpp:80-85`, *"the sole leaf/operator test"*).
  A hardcoded `{solid,raster,image}` allow-list would re-introduce exactly the kind
  allowlist the editor deliberately avoids (D-cells_model-8, `scene/cell.hpp:34-42`)
  and would silently mis-handle any future leaf kind. **No doc delta required.**
- **D-eyedrop_cell-3 — On a non-isolable selection the modifier degrades to the
  composited sample; it never refuses.**
  Alt-click over an empty selection, a camera, or a pull-service content falls
  through to the shipped `sample_composited_color`.
  *Rationale:* the eyedropper's job is to set a color; a modifier that silently does
  nothing (or writes transparent black) over some cells is a worse surprise than one
  that lands the on-screen color. Graceful fallback keeps the gesture
  always-meaningful and is a strict superset of the shipped behavior, so it cannot
  regress the base eyedropper. The **UX nuance** — Alt over a nested cell reads the
  composite rather than that cell's own composite-in-isolation — is a bounded,
  documented degradation that `editor.panels.color_eyedrop_nested` removes; it is
  **not** silent wrongness (the fallback is a real, correct composited color).
  *Alternative rejected — refuse (leave the color unchanged) for non-leaf cells:*
  defensible but leaves the user wondering why the click did nothing; fallback is
  the lesser evil and matches the "the color here is always a render through a
  camera" framing. **No doc delta required.**
- **D-eyedrop_cell-4 — The modifier key is Alt, read from the existing
  `CanvasInput.alt`; the binding is provisional.**
  *Rationale:* Alt is the conventional "sample this specific thing" modifier, it is
  free in the Eyedropper context (the arm reads no other modifier), and it is
  already plumbed to the arm (`views.cpp:158` → `in.alt`), so binding to it adds no
  input surface. The binding is **provisional** pending the full input map
  (`docs/00-design.md:507`), the same status D24's keyboard bindings carry —
  recorded here and surfaced to the parking lot, **not** encoded as a WBS leaf.
  **No doc delta required.**

## Open questions

(none — all decided.) One item is surfaced to the parking lot in the return summary
rather than encoded as a WBS leaf, because it is a UX/input-map call with no
current test to pin: the **final chord** for the active-cell modifier (Alt is the
provisional v1 binding, D-eyedrop_cell-4) resolves when the full input map
(`docs/00-design.md:507`, §11) is written. The nested/operator own-color case is
**not** an open question — it is a concrete, scheduled WBS leaf
(`editor.panels.color_eyedrop_nested`).

## Status

**Done** — 2026-07-30.

- `src/commands/ace/commands/color.hpp` — declared `sample_cell_color(const arbc::Document&, const arbc::Affine& camera, arbc::ObjectId layer, double device_x, double device_y) → std::optional<arbc::WorkingPixel>`.
- `src/commands/color.cpp` — implemented the L1 isolated single-cell sampler: pinned read, `is_operator || composition_ref` gate returns `std::nullopt`, `render_layer` into a 1×1 target, `visit_surface`/`PixelTraits::decode` readback, all three working-space storage formats covered.
- `src/app/canvas_view.cpp` — added the Alt branch in `dispatch_eyedropper`: resolve selection-primary → `cell.layer` via `scene::cells`, call `sample_cell_color`, decode with `working_to_srgb` and `set_active_color`; graceful fall-through to the composited sample on `std::nullopt` or no Alt held.
- `tests/color_test.cpp` — Catch2 units: own-color-under-an-occluder isolation proof (byte-exact `render_layer` golden-in-a-value vs hand-driven render and authored constant); recovers own straight color (round-trip within 8-bit codec); `std::nullopt` for non-cell id and nested cell; all three working-space storage formats (`rgba32f`/`rgba16f`/`fast_rgba8srgb`) exercised to warm every `visit_surface` instantiation (798 assertions, 7 cases).
- `tests/color_e2e_test.cpp` — two new ImGui Test Engine phases: (vi) modifier→own-color isolation proof (Alt-click on back cell yields back cell's own sRGB vs. composited front color on plain click); (vii) graceful fallback with no isolable cell selected (Alt-eyedrop on nested cell drives `std::nullopt` → composited sample); total 9 assertions.
- `tests/canvas_host_test.cpp` — extended the `panels.color` TSan anchor: `sample_cell_color`→`set_active_color` concurrent with live render walk, TSan-clean; inline `WorkerPoolConfig{}` pool per the arbc-nested-render-worker-detach-race memory rule.
- Diff-coverage: 37/40 = 92.5% on changed lines (gate ≥ 90%); 3 remaining uncovered lines are genuinely-defensive and unreachable, mirroring the shipped composited sampler.
