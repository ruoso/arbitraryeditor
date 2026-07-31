# editor.panels.color_eyedrop_nested — eyedropper modifier: nested-composition cells' own colour via an anchored full-pipeline render

## TaskJuggler entry

- **Task:** `editor.panels.color_eyedrop_nested` (`tasks/00-editor.tji:653-657`, under
  `task panels "Info panels"` at `:603`).
- **Effort:** `1.5d` · `allocate team`.
- **Depends:** `editor.panels.color_eyedrop_cell` (`tasks/00-editor.tji:656`).
- **Note (`.tji:657`):** *"Extend the active-cell eyedropper modifier to nested
  compositions and operator/procedural cells, which sample_cell_color currently
  returns std::nullopt for because bare render_layer cannot stand up the pull
  service they need. Render the one cell in isolation through an ephemeral
  single-object composition (rebuild the content via the registry factory into an
  anonymous Document, anchor a Viewport at it, and arbc::render_offline through the
  full pull-service pipeline — src/runtime/offline.cpp:44-67), then decode as the
  shipped leaf does; the modifier stops falling back to the composite for those
  cells. Design: D10 (docs/00-design.md:477). Source-of-debt:
  tasks/refinements/editor.panels/color_eyedrop_cell.md."* The governing row is
  **D10** (`docs/00-design.md:477`), elaborated by **§7** (`:287-290`, the modifier
  variant). This refinement realises the note's intent with a **simpler and
  strictly more robust mechanism than the ephemeral rebuild it sketches** — see
  D-eyedrop_nested-1.
- **Back-link:** the closer appends `Refinement:
  tasks/refinements/editor.panels/color_eyedrop_nested.md` to the `.tji` note (after
  the design-doc citation) and adds `complete 100` immediately after `allocate team`
  (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji` here.
- **Milestone:** `m9_editor` (`tasks/99-milestones.tji:6-10`) **already lists
  `editor.panels.color_eyedrop_nested` directly in its `depends`** — no new
  milestone edge is required on completion (unusual for a leaf; the WBS author
  wired this one in ahead of time).

## Effort estimate

**1.5 days.** Half a day more than the `color_eyedrop_cell` twin, and the extra
half-day is entirely fixtures and test inversion, not new production code:

- The **production change is a three-way branch** inside the already-shipped
  `commands::sample_cell_color` (`src/commands/color.cpp:72-129`): keep the
  leaf → `render_layer` path, keep the operator → `std::nullopt` gate, and add a
  **nested → anchored `render_offline`** arm (~15 lines) that reuses the same pin,
  the same `compose(shift, camera, placement)` transform, and the same
  `visit_surface`/`PixelTraits::decode` read-back. **The eyedropper arm does not
  change at all** (its call site `canvas_view.cpp:657-658` is untouched; only the
  inline comment at `:644` that lists nested as non-isolable is corrected).
- The **cost is the fixtures**: every test tier must now build a **two-level
  document** (a child composition with a known-colour leaf member + a nested cell
  placing it in the root), where the leaf twin only ever built flat cells. The
  construction pattern is established (`tests/overview_gizmo_test.cpp:262-269`;
  `tests/color_test.cpp:314-319`), so this is mechanical, but it recurs in the L1
  unit, the e2e, and the TSan case.
- The estimate is dominated by **inverting the two predecessor assertions that used
  a nested cell as the *non-isolable* example** — `color_test.cpp:296-320` and the
  `color_e2e_test.cpp` fallback phase — into the new *own-colour* assertions, each
  re-justified against this leaf's decision rather than silently rewritten.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.panels.color_eyedrop_cell`** (`tasks/refinements/editor.panels/color_eyedrop_cell.md`,
  **Done** 2026-07-30) — the entire boundary this leaf extends. It shipped:
  - the **isolated single-cell sampler** `commands::sample_cell_color(const
    arbc::Document&, const arbc::Affine& camera, arbc::ObjectId layer, double
    device_x, double device_y) → std::optional<arbc::WorkingPixel>`
    (`src/commands/ace/commands/color.hpp:86-89`, impl `color.cpp:72-129`) — a pure
    pinned read that builds a 1×1 target, composes `compose(shift, camera,
    placement)`, and decodes premul-linear. **This leaf edits exactly this
    function.**
  - the **structural gate** at `color.cpp:95`: `content == nullptr ||
    arbc::is_operator(content) || content->composition_ref().valid()` →
    `std::nullopt`. This leaf **removes the `composition_ref().valid()` clause from
    the nullopt gate** and routes it to a new nested branch; the `nullptr` and
    `is_operator` clauses are retained verbatim.
  - the **eyedropper arm** `CanvasView::dispatch_eyedropper` with its Alt branch
    (`src/app/canvas_view.cpp:629-670`, Alt branch `:650-666`), which resolves the
    selection-primary through `scene::cells` to `cell.layer` and calls
    `sample_cell_color`, decoding a value with `working_to_srgb`/`set_active_color`
    and **falling through to `sample_composited_color` on `std::nullopt`**
    (`:667-669`). Because the arm keys off the sampler's return, extending the
    sampler is sufficient — **no arm logic change**.
  - the **decode + write pair** `commands::working_to_srgb` (`color.cpp:34-40`) and
    `AppState::set_active_color` (`app_state.hpp:178`) — reused verbatim.
  - the **test rigs** this leaf extends: `tests/color_test.cpp` (the
    golden-in-a-value pattern, helpers `full_render_pixel` `:76-89` and
    `hand_layer_pixel` `:95-116`; the nested-cell construction already appears at
    `:314-319`); `tests/color_e2e_test.cpp` (the boot-the-real-shell +
    Eyedropper-drive rig); `tests/canvas_host_test.cpp` (the color TSan case over
    the inline `WorkerPoolConfig{}` pool).
  - **D-eyedrop_cell-1**'s rejected-alternative and **Deferred WBS work**
    (`color_eyedrop_cell.md:329-343,368-372`) explicitly scheduled *this* leaf to
    lift the `composition_ref` gate. This refinement discharges that debt.
- **The v0.4.0 arbc pin** (`editor.canvas.arbc_v040`, `tasks/00-editor.tji:355`,
  Done) — **one render path**: `arbc::render_offline` now renders through the same
  tiled driver the interactive loop uses and **stands up the full pull service +
  operator binding** (`register_builtin_operator_binders()` + `bind_operators(...)`,
  vendored `src/runtime/offline.cpp:50-52`). This is the capability the note names
  and the reason a nested composition renders through `render_offline` at all.

**Pending (owned here):** the nested branch of `sample_cell_color` (anchored
`render_offline`), the one-line comment correction in `dispatch_eyedropper`, and
the tests — the Catch2 nested golden-in-a-value + occluder-isolation + operator/id
`nullopt` retention, the e2e nested-own-colour phase (+ inverted fallback phase),
and the extended TSan case. Nothing downstream is blocked — the sole dependency is
Done.

## What this task is

One extension: **Alt-click with the Eyedropper tool on a *nested-composition* cell
now samples that cell's own composited straight colour — the result of its child
composition rendered in isolation from the parent's siblings — instead of
silently falling back to the on-screen composite.** D10 (`:477`) and §7 (`:287-290`)
promise the modifier samples *"the active cell's own straight colour"* for the
active cell; `editor.panels.color_eyedrop_cell` delivered this for self-contained
**leaf** cells (solid/raster/image) and deliberately returned `std::nullopt` for
cells whose content needs the pull service — nested compositions and operators —
so the modifier degraded to the composite there. This leaf lifts that restriction
for the case with a real v1 authoring surface: **nested compositions**
(`org.arbc.nested`, D12 *"place `.arbc` → nested composition"*).

Concretely, inside `commands::sample_cell_color`:

1. **The `nullopt` gate loses its `composition_ref` clause.** It becomes `content
   == nullptr || arbc::is_operator(content)` → `std::nullopt`; a cell whose content
   answers a valid `composition_ref()` no longer returns `nullopt`.
2. **A nested branch renders the child composition in isolation.** When
   `content->composition_ref()` is valid, the sampler renders **through the full
   pull-service pipeline** by calling `arbc::render_offline` with a 1×1 `Viewport`
   whose **`anchor` is set to that child composition id** and whose `camera` is the
   same `compose(shift, camera, placement)` the leaf path builds — then decodes
   pixel (0,0) exactly as before. Because the viewport is anchored at the child
   composition, the frame walk draws **exactly that composition's own members**
   (`compositor.hpp:20-27`), unmixed with the parent's siblings — the cell's own
   colour — through the same driver export uses, so nested content (including
   nested-within-nested) resolves.

Operators (non-empty `inputs()`) continue to return `std::nullopt` — they have no
still-image-editor authoring surface (see D-eyedrop_nested-3). The decode, the
write target, the arm, and the plain-click default are all unchanged.

## Why it needs to be done

The leaf modifier answered *"what colour is **this leaf cell**, unmixed with what
sits over it?"* but stopped at the document's structural boundary: a nested
composition — the first-class way the editor composes reusable sub-scenes and
places `.arbc` files (D12) — fell through to the composite, so Alt-clicking a
partly-occluded nested cell gave the occluder's colour, not the sub-scene's own.
That is precisely the bounded degradation `color_eyedrop_cell`'s D-eyedrop_cell-3
named and scheduled this leaf to remove (`color_eyedrop_cell.md:396-399`). The
capability the nested case needs — a **single cell rendered through the pull
service in isolation from its siblings** — is exactly what the v0.4.0 unified
`render_offline` provides when anchored at a composition; this leaf spends that
capability so the modifier is real for nested cells rather than an approximation of
the composite.

## Inputs / context

**Governing design rows (normative — the constitution):**

- `docs/00-design.md:477` **D10** — the colour boundary; the eyedropper samples the
  displayed sRGB through the active camera, *"composited result by default,
  active-cell via modifier."* This leaf extends the modifier half to nested cells.
- `docs/00-design.md:287-290` **§7** — *"the colour here is a render through a
  camera, not a lookup … modifier: the active cell's own straight colour."* The
  "render through a camera" clause is why the nested sample is an **anchored render
  through the pull service**, not a buffer mask.
- `docs/00-design.md:271-279` **§7 boundary principle** — decode on the way out;
  *"Premultiplied and linear values are never shown"* (`:279`). The isolated nested
  sample is decoded to sRGB before it reaches the user.
- `docs/00-design.md` **D12** (`:479`) — *"place `.arbc` → nested composition"*: the
  nested-composition cell is a first-class v1 authoring surface, which is why this
  leaf's case (unlike the operator case) has real consumers.
- `docs/01-architecture.md` **§8** the levelization DAG (`:310-346`), **§9** the
  per-leaf DoD (`:348-375`).

**Predecessor decisions (`tasks/refinements/editor.panels/color_eyedrop_cell.md`):**

- **D-eyedrop_cell-1** (`:347-373`) — the isolated sample is an **L1 `commands`**
  sampler over the library render, a **pure pinned read**, byte-exact and headless;
  the ephemeral single-object composition was named as *this* leaf and rejected for
  the 1d twin only on code size, not correctness. This refinement chooses an even
  simpler realisation (D-eyedrop_nested-1).
- **D-eyedrop_cell-2** (`:374-387`) — the leaf/operator gate is the library's
  **structural** `is_operator || composition_ref` predicate, never a kind
  allow-list (respects D-cells_model-8). This leaf keeps `is_operator` as the
  `nullopt` gate and reuses `composition_ref` as the **nested-branch selector** —
  still no kind allow-list.
- **D-eyedrop_cell-3** (`:388-403`) — the modifier degrades to the composite rather
  than refusing; the nested own-colour case was named as the bounded degradation
  this leaf removes.
- **D-color-2** (`panels/color.md:357-373`) — sRGB straight-alpha is the stored
  truth; premul-linear is derived at the boundary via the library primitives. The
  nested decode reuses `working_to_srgb` unchanged.

**Source seams (editor):**

- The sampler this leaf edits: `commands::sample_cell_color`
  (`src/commands/color.cpp:72-129`), decl `src/commands/ace/commands/color.hpp:86-89`.
  The gate to split is `color.cpp:95`; the leaf render path is `color.cpp:104-128`
  (`compose(shift, camera, placement)` at `:102-104`; 1×1 target `:106-110`;
  `render_layer` `:119`; `visit_surface`/`decode` `:122-128`). The pin at
  `color.cpp:82` is the same generation the nested branch will render against.
- The composited sampler `commands::sample_composited_color` (`color.cpp:42-70`) —
  the nested branch mirrors its `render_offline` call, differing only by supplying a
  valid `Viewport::anchor` and the placement-composed camera.
- The eyedropper arm `CanvasView::dispatch_eyedropper` (`src/app/canvas_view.cpp:629-670`):
  the Alt branch (`:650-666`) resolves `state_.selection().primary()` through
  `scene::cells(state_.document(), state_.registry(), scene::active_composition(...))`
  and calls `sample_cell_color(state_.document(), p.camera, cell.layer,
  in.focus_x, in.focus_y)` (`:657-658`). **Only the inline comment `:644`** ("an
  operator / nested cell") needs correcting to name only operators; the logic is
  untouched.
- The decode + write: `commands::working_to_srgb` (`color.cpp:34-40`) and
  `AppState::set_active_color` (`app_state.hpp:178`).
- Nested-cell authoring helper: `scene::add_cell(arbc::Document&, const
  arbc::Registry&, std::string_view kind_id, std::string_view config, const
  arbc::Affine& placement, std::optional<arbc::ObjectId> entered)`
  (`src/scene/ace/scene/cell.hpp:135`, impl `cell.cpp:253-287`) — `kind_id =
  "org.arbc.nested"`, `config = std::to_string(child.value)`. The identity read-back
  the editor uses everywhere is registry-free off `content->composition_ref()`
  (`cell.cpp:377-382`), the same predicate the gate uses.

**Source seams (vendored libarbc, `build/*/_deps/arbc-src/`; cited by logical
include path):**

- **`arbc::render_offline`** — `<arbc/runtime/offline.hpp>:20-21` (pins per call)
  and `:42-45` (caller-pinned overload, issue #27: *"`state` must be a pin of
  `document`"*). The impl (`src/runtime/offline.cpp:18-86`) stands up the **full
  pull substrate** the nested case needs: `SurfacePool` (`:39`), `ContentResolver`
  over `document.resolve` (`:40`), `TileCache` (`:43`), the pull identity/stamp maps
  and `PullServiceImpl` with `direct_dispatch()` (`:44-50`), and
  **`register_builtin_operator_binders()` + `bind_operators(document, pull, backend,
  state)`** (`:51-52`). The **anchor logic** (`:60-67`) only sources the root
  composition *"if `!anchored.anchor.valid()`"* — *"A caller that already pinned a
  specific composition keeps it"* (`:59`), which is exactly how the nested branch
  pins the child composition.
- **`arbc::Viewport`** — `<arbc/compositor/compositor.hpp>:16-36`: `{int width; int
  height; Affine camera; ObjectId anchor{};}`. The `anchor` field (`:30`,
  documented `:20-29`) is *"the node the camera is pinned to … the frame walk is
  composition-scoped: it draws exactly this composition's direct members, reaching a
  nested child only through the enclosing layer's content."* Setting `anchor` to the
  child composition id renders the child's members in the child's local space, which
  `compose(shift, camera, placement)` maps to device.
- **The leaf/operator gate** — `arbc::is_operator(content)`
  (`<arbc/compositor/operator_graph.hpp>:84-86`, `content != nullptr &&
  !content->inputs().empty()`) and `arbc::Content::composition_ref()`
  (`<arbc/contract/content.hpp>:636`, default `ObjectId{}` = "not a composition
  reference"; a nested content answers its in-document child composition id).
- **`arbc::NestedContent`** — `<arbc/kind_nested/nested_content.hpp>:66`
  (`NestedContent(ObjectId child)`); already used in the shipped color test
  (`tests/color_test.cpp:20,316-318`). This is the L1 test's direct construction of
  a nested cell (the `add_cell(..., "org.arbc.nested", ...)` path is equivalent, used
  by the e2e/overview fixtures).
- **The decode primitives** — `<arbc/media/pixel_traits.hpp>` via `working_to_srgb`;
  `arbc::visit_surface` + `PixelTraits<…>::decode` for the 1×1 read-back
  (`color.cpp:122-128`).

**A reconciliation the implementer must know.** The sibling refinement
`tasks/refinements/editor/nested_composition_binding.md` (written pre-v0.4.0)
states — and parks — that *"`render_offline` never calls `bind_operators`, so it
composites no nested-composition operator."* That is **superseded by the v0.4.0
render-path unification** (`editor.canvas.arbc_v040`): the currently pinned
`src/runtime/offline.cpp:50-52` **does** call `register_builtin_operator_binders()`
+ `bind_operators(...)`. Do not trust the stale claim; the pinned source is
authoritative. Crucially, **this leaf does not depend on nested operators binding
at the *root* anyway** — it anchors the isolated render at the *child composition*,
whose direct members are leaves, so it composites non-blank irrespective of the
operator-binding question (see D-eyedrop_nested-1 and the test design). That
staleness in the other refinement's parking-lot entry is surfaced to the closer in
the return summary, not fixed here (out of this leaf's file scope).

**Levelization lint:** `scripts/check_levels.py` — `commands` is on the `arbc`
external allow-list (`:49`) and already includes `<arbc/runtime/offline.hpp>`,
`<arbc/compositor/compositor.hpp>`, `<arbc/compositor/operator_graph.hpp>` and
`<arbc/contract/content.hpp>` (`src/commands/color.cpp:7-9,14`). The nested branch
adds **no new include** — it reuses `render_offline` and `Viewport`, both already
in scope. `dockmodel` stays `{base, platform}`-only and untouched.

## Constraints / requirements

1. **Isolation is a render through a camera, via the pull service** (D10, §7:288).
   The nested sample is `arbc::render_offline` anchored at the cell's child
   composition — never a GL readback, never masking the composited buffer, never a
   visibility toggle. It stays **L1 `commands`** (headless, byte-exact),
   twinning `sample_composited_color`.
2. **The nested branch is a pure pinned read against the *same* generation.** Use
   the **caller-pinned `render_offline` overload** (`offline.hpp:42-45`), passing the
   `state` already pinned at `color.cpp:82`, so the nested sample reads exactly the
   generation the gate inspected — no second pin, no document mutation, no
   transaction, no writer thread, and **no ephemeral `Document`** (which would spin
   up a background housekeeping thread per sample — deliberately avoided,
   D-eyedrop_nested-1). `render_offline` uses `direct_dispatch()` (single-threaded,
   `offline.cpp:50`): the sample never enters the interactive worker pool, so it does
   not touch the worker-detach path the `arbc-nested-render-worker-detach-race`
   memory warns about.
3. **The gate keeps `is_operator` as `nullopt`; only `composition_ref` moves.** The
   split is: `content == nullptr || arbc::is_operator(content)` → `std::nullopt`
   (retained verbatim from `color.cpp:95`); `content->composition_ref().valid()` →
   the nested branch. Still the library's structural predicate, still **no kind
   allow-list** (D-eyedrop_cell-2 / D-cells_model-8).
4. **The nested branch anchors at `composition_ref()`, camera `compose(shift,
   camera, placement)`.** Build `arbc::Viewport{1, 1, compose(compose(translation
   (-device_x,-device_y), camera), record->transform),
   content->composition_ref()}` and call `render_offline(document, state, viewport,
   backend)`; on the error path return `std::nullopt` (so the arm degrades
   gracefully, as for an unstorable working space today, `color.cpp:108-109`);
   decode pixel (0,0) through the identical `visit_surface`/`PixelTraits::decode`
   read-back (`color.cpp:122-128`). The isolated result is the child composition's
   composited straight colour — the cell's own colour — not attenuated by the parent
   layer's placement opacity (that is a placement attribute, not the cell's own
   colour; D-eyedrop_nested-4).
5. **The arm and the composited default are unchanged** (D-eyedrop_cell-3). The
   arm's `sample_cell_color` call (`canvas_view.cpp:657-658`) now returns a value for
   nested cells, so they stop falling through; the `std::nullopt` fall-through to
   `sample_composited_color` (`:667-669`) is unchanged and still catches operators,
   empty selections, cameras, and deferred/unresolved nested children (invalid
   `composition_ref`). Correct the inline comment at `:644` to name only operators.
6. **The decode and write target are the shipped ones** (D-color-2). `working_to_srgb`
   + `set_active_color`; premultiplied/linear values are never surfaced (§7:279).
7. **Levelization (§8) holds with no new edge.** The branch lives in L1 `commands`
   beside the shipped sampler, adds no include, no new component, no new DAG edge;
   `dockmodel` untouched; the L1 core gains no ImGui/GL/SDL include.
   `scripts/check_levels.py` is not edited.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`); `scripts/gate`
green (check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.** No new
  component, no `check_levels.py` edit, no new include, `dockmodel` untouched. The
  change is confined to `src/commands/color.cpp` (the sampler branch) and the
  one-line comment in `src/app/canvas_view.cpp`. Asserted by inspection and the lint.
- **L1 logic — Catch2 units** (extend `tests/color_test.cpp`; naming `TEST_CASE
  ("color: …")`):
  - **Nested own-colour byte-exact golden-in-a-value (Constraints 1, 2, 4).** Build a
    two-level document — a **child** composition holding one solid of a known
    premul-linear colour (`add_composition` + `add_content(SolidContent)` +
    `attach_layer`), and a **nested** cell placing it in the root (`NestedContent
    (child)` per `color_test.cpp:314-319`, or the equivalent `add_cell(...,
    "org.arbc.nested", std::to_string(child.value), ...)` of
    `overview_gizmo_test.cpp:262-269`). At a point inside the nested cell,
    `sample_cell_color` of the nested cell's layer returns the child solid's own
    straight colour **byte-exactly** — matched against a new hand-driven reference
    that pins `render_offline(doc, doc.pin(), Viewport{1,1, compose(shift, camera,
    placement), child}, backend)` and decodes pixel 0 (the nested twin of
    `hand_layer_pixel`, `:95-116`). Working space `rgba32f`, so exact, not
    toleranced.
  - **Own colour under an occluder — the isolation proof (Constraint 1).** Add an
    opaque **leaf** solid sibling in the **root**, of a distinct colour, over the
    nested cell's placed region. At a point in the overlap, `sample_cell_color` of
    the nested cell returns the **child's** own colour while `sample_composited_color`
    at the same point returns the **occluder's** colour — the two samplers disagree
    exactly where a sibling occludes. (The proof rests only on the leaf occluder and
    the child's leaf members compositing — robust regardless of nested-at-root
    operator binding.)
  - **Nested-within-nested resolves (Constraint 1 / v0.4.0 pull service).** With a
    child composition that itself contains a nested cell wrapping an innermost solid
    (a 2-deep chain, cf. `canvas_host_test.cpp:974-989`), `sample_cell_color` of the
    outer nested cell returns the innermost solid's own colour — pinning that the
    anchored `render_offline` stands up the full pull service (`offline.cpp:50-52`)
    and the sample is not limited to one level. *(If, contrary to the pinned
    `offline.cpp:50-52`, the anchored render composites a nested child member blank,
    the implementer restricts this assertion to the single-level case and records the
    finding in the return summary — the single-level nested own-colour golden above
    remains the leaf's binding deliverable.)*
  - **Retained `nullopt` gate (Constraint 3) — the inverted predecessor test.** Split
    `color_test.cpp:296-320`: the non-cell-id branches (`ObjectId{}`, a valid-shaped
    id naming nothing) keep asserting `std::nullopt`; the **nested branch (`:311-320`)
    moves out** — a nested cell now returns a value (asserted by the golden above),
    so it is no longer an example of `nullopt`. Add an **operator** cell (a content
    with non-empty `inputs()` — e.g. a library operator kind, or the codec
    `deserialize` path of the arbc operator tests) and assert it still returns
    `std::nullopt`, so the `is_operator` clause stays covered. Re-justify the
    inversion in the test comment against this leaf, not silently.
  - **Every working-space storage format still decoded.** Extend or mirror
    `color_test.cpp:323-345` so the nested branch's `visit_surface` read-back is
    exercised under `rgba32f`/`rgba16f`/`fast_rgba8srgb`, warming every instantiation
    on the new path (round-trip within each format's precision).
- **Rendered output — golden.** The nested own-colour byte-exact case above **is**
  the `render_offline`-anchored golden for this leaf (a single isolated pixel pinned
  as an exact `WorkingPixel`, the golden-in-a-value form the base samplers use,
  `color_test.cpp:164-193`, because the observable is one pixel). No new image-file
  golden.
- **UI e2e — ImGui Test Engine** (extend `tests/color_e2e_test.cpp`, reusing its
  boot-the-real-shell + Eyedropper-drive rig; offscreen software-GL; Alt via
  `io.KeyAlt`):
  - **Modifier → nested own colour (the isolation proof).** Seed a nested cell whose
    child holds a known colour, partly occluded by a known leaf sibling. Select the
    **nested** cell, hold Alt, and Eyedropper-click inside the overlap:
    `active_color()` equals the **child's** own sRGB. A **plain** (no-Alt) click at
    the same point yields the **occluder/composite** colour — modifier and default
    visibly diverge on a nested cell.
  - **Fallback still holds — the inverted predecessor phase.** The predecessor's
    fallback phase drove Alt on a **nested** cell to prove `std::nullopt` →
    composite; that behaviour is now *inverted* (nested yields its own colour). Move
    the fallback proof to a still-non-isolable selection — **Alt-click with no cell
    selected** (empty selection) sets `active_color()` to the composited sample, not
    empty/black. Re-justify the change in the test comment.
- **Threading (ASan/TSan).** Extend the color TSan case
  (`tests/canvas_host_test.cpp`, over the inline `WorkerPoolConfig{}` pool per the
  `arbc-nested-render-worker-detach-race` memory rule): the UI thread runs the
  Alt-branch `sample_cell_color` on a **nested** cell → `set_active_color` concurrent
  with a live render walk, TSan-clean. The argument is the shipped one: the nested
  sample is a **pinned read** via `direct_dispatch()` (`offline.cpp:50`, no worker
  pool), and the resulting `WorkingPixel` crosses to the writer only as a copied
  value — no shared mutable colour/render state crosses threads, no new lane, no new
  suppression. (This is the same UI-thread `render_offline`-with-operator-binding the
  shipped `sample_composited_color` already performs over a document that contains a
  nested cell, so it introduces no new concurrency class.)
- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed lines;
  clang-format + build clean. Tests ship with the task.
- **Deferred WBS work.** **None as a WBS leaf.** The one loose end — the modifier's
  own colour for **operator** cells (`is_operator`, non-empty `inputs()`: the
  library's `org.arbc.fade`/`crossfade`/`tone`) — has **no still-image-editor
  authoring surface in v1** (the editor authors solid/raster/image leaves, cameras,
  and nested compositions only; `commands/app_state.cpp:57`,
  `scene/camera.hpp:93`), so an operator-isolation leaf would be picked up and built
  with **zero consumers to validate it against**. It is therefore surfaced to
  **`tasks/parking-lot.md` (Waiting on evidence)** with the trigger "the still-image
  editor authors an operator cell," not encoded as a WBS leaf (closer records it; see
  return summary). If/when that trigger fires, the ephemeral single-object
  composition the `.tji` note sketches (rebuild the operator + its input closure via
  the registry codec `deserialize`, `registry.hpp:76-78`, into an anonymous
  `Document`, anchor, `render_offline`) is the ready mechanism.

## Decisions

- **D-eyedrop_nested-1 — Nested isolation is `render_offline` anchored at the cell's
  `composition_ref()` on the *real pinned document* — not an ephemeral rebuild.**
  When `content->composition_ref()` is valid, the sampler renders the child
  composition in place via `render_offline(document, state, Viewport{1,1,
  compose(shift, camera, placement), /*anchor=*/composition_ref()}, backend)`,
  reusing the pin already taken at `color.cpp:82`.
  *Rationale:* the child composition is **already an ordinary composition in the
  document's model** (`content.hpp:640` — a nested child, whether document-local or
  loaded from an external `.arbc`, *"is by then an ordinary composition in this
  document's model"*), and `Viewport::anchor` scopes the frame walk to exactly that
  composition's members (`compositor.hpp:20-27`). So isolation is a **one-field
  change to the viewport** over a seam the composited sampler already uses — zero
  rebuild, zero mutation, all bindings live, and it stays a pure pinned read. It is
  also **robust against the operator-binding question**: anchoring at the child means
  the isolated render's direct members are the child's own contents (leaves in the
  common case), so it composites non-blank whether or not `render_offline` binds the
  *nested operator* at the root — sidestepping the exact concern
  `nested_composition_binding` parked.
  *Alternative rejected — the `.tji` note's ephemeral single-object composition:*
  rebuild the content via the registry factory into an anonymous `Document`, anchor,
  render. For a **nested** cell this is strictly worse: `org.arbc.nested` is
  **host-bound, not codec-carried**, so "rebuild the content" would mean
  reconstructing the entire child-composition subtree into the anonymous document —
  far more code than a branch — and a fresh `arbc::Document` per sample spins up a
  **background housekeeping thread** (`document.hpp` ctor), turning a passive pinned
  read into a thread-creating operation. The ephemeral approach's only advantage —
  it also covers operators — buys nothing in v1 (operators have no authoring surface,
  D-eyedrop_nested-3). Rejected for the nested case; retained as the future
  mechanism if operator isolation is ever scheduled. **No doc delta required**
  (realises D10/§7:290 over an existing library seam).
  *Alternative rejected — visibility-toggle around a root render:* hide every
  sibling via a transaction, `render_offline` at root, roll back. It **mutates the
  shared model on a writer transaction** for what must be a passive UI-thread sample
  — the same reason D-eyedrop_cell-1 rejected it. Rejected.
- **D-eyedrop_nested-2 — Extend the shipped `sample_cell_color` in place (a
  three-way branch) rather than add a second sampler.** `sample_cell_color` becomes:
  `nullptr || is_operator` → `std::nullopt`; `composition_ref().valid()` → anchored
  `render_offline`; else (leaf) → the shipped `render_layer` path.
  *Rationale:* the pin, the layer-record lookup, the `compose(shift, camera,
  placement)` transform, and the `visit_surface`/`decode` read-back are **identical**
  across the leaf and nested paths — a second sampler would duplicate all of them for
  a three-line render difference. Extending in place also means **the eyedropper arm
  does not change**: it already calls `sample_cell_color` and already falls through on
  `std::nullopt`, so nested cells start yielding their own colour with no L4 edit
  (only the stale comment at `canvas_view.cpp:644`). The predecessor's deferred-work
  text said "a new L1 sampler over an ephemeral document," but that phrasing assumed
  the ephemeral mechanism D-eyedrop_nested-1 rejects; over the anchored mechanism, an
  in-place branch is the smaller, single-call-site change.
  *Alternative rejected — a separate `sample_nested_cell_color` the arm tries before
  falling back:* needs a new arm branch and duplicates the pinned-read scaffolding;
  no benefit. **No doc delta required.**
- **D-eyedrop_nested-3 — Operators keep returning `std::nullopt` → graceful
  composited fallback; operator-cell isolation is a parking-lot item, not a WBS
  leaf.** The `is_operator` clause of the gate is retained verbatim.
  *Rationale:* the still-image editor authors only leaves (solid/raster/image),
  cameras (excluded from `cells()`), and nested compositions — it exposes **no
  operator-cell authoring path** (the library's `fade`/`crossfade`/`tone` are
  time/audio-domain operators with no still-image surface). An operator-isolation
  leaf would therefore be scheduled and implemented against **zero consumers**, with
  no authorable fixture to validate it and nothing to close it cleanly — the
  self-perpetuating shape the refinement policy forbids. Graceful fallback keeps the
  gesture always-meaningful for operator cells if one ever arrives via import, and is
  a strict superset of shipped behaviour. The genuine open question — *should* the
  editor ever author operator cells, and thus want their own-colour sample — is a
  product/evidence call, so it goes to `tasks/parking-lot.md` under "Waiting on
  evidence" with the trigger named, per the policy for decisions blocked on something
  that does not yet exist. **No doc delta required.**
- **D-eyedrop_nested-4 — The nested sample is the child composition's composited
  straight colour; the parent layer's placement opacity is excluded.** Anchoring at
  `composition_ref()` renders the child at its own composited alpha, not multiplied
  by the nested cell's layer opacity in the parent.
  *Rationale:* D10/§7:290 defines the modifier as *"the active cell's own straight
  colour"* — the cell's intrinsic content, unmixed with *placement*, of which
  opacity-in-parent is one facet (the geometric placement is still honoured, via the
  `compose(…, placement)` camera, so the sampled point maps correctly). Sampling the
  child's own composite is the answer a painter wants ("what colour is this
  sub-scene?"), and it keeps the nested path a clean anchored render with no
  post-multiply. The minor difference from the leaf path — where `render_layer`
  applies the layer opacity — is deliberate and bounded: for a leaf, opacity is the
  cell's *only* alpha; for a nested cell, the child's own composite is the meaningful
  "own colour." **No doc delta required.**

## Open questions

(none — all decided.) One item is surfaced to the parking lot in the return summary
rather than encoded as a WBS leaf, because it is blocked on evidence that does not
yet exist: **own-colour sampling for operator cells** (D-eyedrop_nested-3) becomes
decidable only if the still-image editor ever gains an operator-cell authoring
surface — the named trigger. Nested-within-nested is **not** an open question — it
is a required assertion grounded in the pinned `offline.cpp:50-52`, with a written
fallback if that pinned behaviour proves otherwise at implementation time.

## Status

**Done** — 2026-07-31.

- **The nested arm landed as designed** (`src/commands/color.cpp:111-138`):
  `sample_cell_color` carves a content answering a valid `composition_ref()` out of
  the `is_operator` gate and renders it through `render_offline` anchored at the
  child composition on the **real pinned document** — D-eyedrop_nested-1's frozen
  choice, no ephemeral rebuild — returning the child's own composited straight
  colour. `src/app/canvas_view.cpp:660` routes the Alt-branch through it unchanged.
- **Tests ship with the task**: L1 goldens-in-a-value for the nested colour, the
  nested-within-nested case, and the storage-format sweep
  (`tests/color_test.cpp`); the modifier-vs-default divergence and the inverted
  empty-selection fallback (`tests/color_e2e_test.cpp`); the concurrent nested
  sample against a live render walk (`tests/canvas_host_test.cpp:3069`).
- **The Threading criterion turned out to be unsatisfiable on the pinned library,
  and the fix was upstream, not here.** The criterion's own argument — that the
  nested sample "introduces no new concurrency class" because
  `sample_composited_color` already does the same UI-thread `render_offline` — was
  correct, but backwards in its conclusion: that shipped sampler carried the SAME
  latent defect, so the new TSan case did not create a race, it exposed one. arbc
  v0.4.0's `NestedContent::inputs()` returned a span into memo-owned storage past
  its own lock, and a second binder's re-key freed it under the compositor's damage
  walk — a **heap-use-after-free**, not a stale read. Fixed upstream in
  **libarbc v0.4.1** (`Content::visit_inputs`, a locked-visit contract);
  incorporated by `editor.canvas.arbc_v041`. The criterion now passes as written.
- **No tech debt registered.** Ten successive fixers proposed registering
  `arbc.nested_render.concurrent_bind_race`; it is *resolved* upstream rather than
  deferred, so recording it would describe a debt that no longer exists. The two
  memory notes asserting the criterion is unsatisfiable
  (`arbc-nested-concurrent-bind-conflict`, `arbc-nested-render-worker-detach-race`)
  are superseded for the concurrent-bind half.
- **Error-path coverage added** (`tests/color_test.cpp`): the nested arm's
  "degrades to `std::nullopt`" promise is now asserted through the library's own
  capability gate — a working space whose tag triple no CPU codec honors makes
  `render_offline` return an error value — with the same scene passing at a
  storable format, so the `nullopt` is provably the unstorable path and not an
  empty scene.
- **Deferred as designed**: own-colour sampling for true operator cells
  (D-eyedrop_nested-3) stays in `tasks/parking-lot.md` under its stated trigger,
  not encoded as a WBS leaf.
