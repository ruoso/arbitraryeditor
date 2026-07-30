# editor.canvas.isolation_scope — Canvas isolation-scope dim (entered composition bright, everything outside dimmed)

## TaskJuggler entry

- **Task:** `editor.canvas.isolation_scope` (`tasks/00-editor.tji:409-414`, under
  `task canvas "Canvas & rendering"` at `:216`, in `task editor` at `:26`).
- **Effort:** `2d` (`:410`) · `allocate team` (`:411`).
- **Depends:** `editor.panels.layers` (`:412`).
- **Note (`:413`):** "Present the entered composition in the canvas and dim
  everything outside (D17: 'canvas … show the child's cells, everything outside
  dims'), consuming `AppState::entered_composition` published by
  `editor.panels.layers`. The list's re-root, breadcrumb and scope model ship
  with layers; this leaf owns the canvas render reflection — the
  CanvasRenderer/DocumentBinding path that composites the child and dims the
  parent. Source-of-debt: `tasks/refinements/editor.panels/layers.md` (Deferred
  WBS). Design: `docs/00-design.md D17`, `docs/01-architecture.md A5/A6`."
- **Back-link:** **the closer** appends `Refinement:
  tasks/refinements/editor.canvas/isolation_scope.md` to the `.tji` note and
  adds `complete 100` after `allocate team` (`tasks/refinements/README.md:47-68`).
  **Do not** hand-edit the `.tji` here.
- **Milestone:** rolls up to the single top-level capability milestone via its
  `editor.canvas` parent (`tasks/99-milestones.tji:8`); no separate wiring.
- **Downstream:** nothing `depends` on this leaf directly. It is a consumer that
  turns the scope model `editor.panels.layers` published into the canvas's D17
  reflection, the sibling of the list re-root and breadcrumb that shipped there.

## Effort estimate

**Two days.** The scope model, the fail-safe resolution, the re-root and the
breadcrumb already shipped with `editor.panels.layers`; the render path
(`CanvasRenderer`/`CanvasHost`) and its per-canvas value channels already exist.
The work is: (1) one pure-L1 `scene` helper — the entered composition's placed
focus quad (~0.4d incl. Catch2); (2) a linear-working-space dim composite in L2
`render`, reusing the `to_srgb8_over` two-surface pattern, behind a new offline
entry point + a per-canvas scope channel that mirrors `set_camera` (~0.7d);
(3) wire `AppState::entered_composition` from `app::CanvasView` down that channel,
fail-safe (~0.4d); (4) the golden + ImGui Test Engine e2e + one sanitizer case
(~0.5d). **No new component, no new DAG edge, no new libarbc surface, no new
external dependency, no transaction** — the scope is transient session state, and
the render→scene edge it uses is already in the §8 DAG.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.panels.layers` (Done 2026-07-29,
  `tasks/refinements/editor.panels/layers.md`).** Ships the entire scope model
  this leaf reflects on the canvas:
  - **`AppState::entered_composition()`** — `std::optional<arbc::ObjectId>&`
    (`src/commands/ace/commands/app_state.hpp:149-150`, field `entered_composition_`
    at `:252`): `nullopt` = Root, an id = the entered nested composition. A
    **transient, non-transactional, project-level** session field on the one
    project-level `AppState` beside `AppState::selection()` (`app_state.hpp:138`).
    Re-resolved each frame; **fail-safe** — an id that no longer names a live
    composition (GC'd / undone-away / foreign) falls back to Root next frame
    (layers Constraint 8 / D-layers-3). Today it is read **only** by the Layers
    panel (`src/app/layers_panel.cpp:83`, via `scene::active_composition` `:88`);
    the canvas does **not** read it yet — plumbing it into the render path is the
    net-new work of this leaf.
  - **The scene walk helpers** it introduced/uses:
    `scene::cells(document, registry, composition)` (the composition-parameterised
    walk, `src/scene/ace/scene/cell.hpp:238`), `scene::nested_composition_of(document,
    cell)` (the child a nested cell wraps, read generically off
    `composition_ref()`, `:246`), `scene::active_composition(document, entered)`
    (the fail-safe resolve, `:254`), `scene::composition_path` (the breadcrumb
    walk over parent links, `:271`), and the `scene::Cell { …; arbc::Affine
    placement; std::optional<arbc::Rect> content_bounds; … }` shape
    (`cell.hpp:197-220`) — the `placement` × `content_bounds` this leaf turns into
    a placed focus quad.
  - **Decision continuity:** **Select ≠ expand ≠ enter** (D17; layers
    Constraint 6). Enter = double-click a nested cell → re-root; scope is
    transient, project-level, singleton (canvas, list, overview all reflect the
    one shared scope — D-layers-3, D-layers-6). The **canvas dimming is
    explicitly this leaf's** in the layers Deferred-WBS split (D-layers-6:
    scope model + list reflection + breadcrumb ship with layers; **canvas
    dimming is `editor.canvas.isolation_scope`**; scoped insert/delete is
    `editor.cells.scoped_edit`). The layers e2e "deliberately omits a canvas
    (pending `editor.canvas.isolation_scope`'s nested render reflection)."

- **The canvas render host (Done, consumed as a seam).** `render::CanvasRenderer`
  (`src/render/ace/render/canvas_renderer.hpp:55`) — GL-free, ImGui-free, produces
  the CPU sRGB8 frame; `resize(w,h)` (`:100`), `set_camera(affine)` (`:109`),
  building `config.viewport = arbc::Viewport{width, height, camera}`
  (`src/render/canvas_renderer.cpp:129`) and its `arbc::HostViewport::DocumentBinding`
  `{bridge, registry}` (`canvas_renderer.cpp:145-148`). `render::CanvasHost`
  (`src/render/ace/render/canvas_host.hpp:62`) is the one host over the one
  `Document` with `canvas#N`-keyed entries; `consume(id, last_seq, out)` (`:159`)
  is the non-blocking latest-frame handoff. The per-canvas **`request_camera` /
  `request_resize`** value channels (stash `src/render/canvas_host.cpp:97`, apply
  `:270-272`; `set_camera` at `canvas_renderer.cpp` behind them) are the exact
  pattern the new **scope channel** mirrors.

- **The offline render tail (Done, consumed + extended).**
  `render::render_document_srgb8(document, w, h, camera)`
  (`src/render/ace/render/render.hpp:47`) wraps `arbc::render_offline`, which
  "sources the ROOT composition as the anchor" (`src/render/render.cpp:90`). Its
  filled-background sibling `render_document_srgb8_over` (`render.hpp:82`) already
  demonstrates the **linear-working-space composite** this leaf reuses:
  `to_srgb8_over` (`render.cpp:50-74`) allocates a second surface in the **same
  linear working space**, `backend.clear` a premultiplied fill (`:68`),
  `backend.composite(**filled, **frame, identity, 1.0)` (`:72`), then
  `backend.convert` the sRGB8 tail (`:74`). The dim scrim composites through the
  identical discipline — **never over the sRGB8 bytes** (D10).

- **`editor.canvas.nav` (Done).** The transient per-canvas viewport camera and
  the composition↔device transform the canvas already computes for the grid
  chrome (`src/app/canvas_view.cpp:266-296`, `grid_to_screen` `:285`) — the same
  comp→device mapping the render uses to place the focus quad's dim mask.

**Pending (owned here):** the placed-focus-quad `scene` helper, the
linear-working-space dim composite in `render` + its offline entry point + the
per-canvas scope channel, and the `app` wiring that feeds
`AppState::entered_composition` down it. Nothing downstream is blocked on an
unwritten predecessor.

## What this task is

Make the canvas **reflect the entered composition scope**: when a nested
composition is entered (D17's isolation scope), the canvas keeps showing the
child's cells at full brightness and **dims everything outside** it, so the eye
lands on the art being edited while the surrounding parent stays visible as dim
context. When no composition is entered (`entered_composition == nullopt` =
Root), the canvas renders **exactly as today, byte-for-byte**.

Concretely:

1. A pure-L1 `scene` helper resolves the entered composition to its **placed
   focus quad** — the wrapping nested cell's `placement` applied to its
   `content_bounds`, in root/composition space. `nullopt` when the scope is Root
   or does not name a live nested composition (the fail-safe).
2. The **L2 `render`** path composites, in the linear working space, a neutral
   **dim scrim** over the **complement** of that focus quad (the "everything
   outside"), leaving the child region bright. A `nullopt` focus quad composites
   **no scrim** → the frame is byte-identical to the pre-scope render.
3. `app::CanvasView` reads the project-level `AppState::entered_composition` each
   frame and feeds it to the render through a **new per-canvas scope channel**
   that mirrors the settled `request_camera` channel — so all N canvases dim the
   one shared scope identically (D19), the interactive frame reflects a scope
   change on the next frame, and a scope that has gone invalid falls back to Root
   (no dim, no crash).

It ships **no scene mutation**, **no transaction** (D15), and **no change to the
export path**: export renders through a camera via the 2-arg
`render_document_srgb8` (root, no scope), so an exported PNG **never dims** (D10 /
D14). The dim is interactive-editing reflection, confined to the canvas view's
own frame.

## Why it needs to be done

D17 (`docs/00-design.md:484`; prose `:225-228`) makes entering a nested
composition an **isolation scope where "canvas, list, and overview show the
child's cells, everything outside dims, and a breadcrumb climbs back out."** The
scope model, the list re-root and the breadcrumb landed with
`editor.panels.layers`; **the canvas half of that sentence is unbuilt** — the
layers refinement deferred it here by name (D-layers-6), and the layers e2e
records that it "omits a canvas (pending `editor.canvas.isolation_scope`)." Until
this leaf lands, entering a composition re-roots the list and lights the
breadcrumb but the canvas gives no visual signal that you are scoped inside a
child — the isolation is half-communicated. This leaf closes D17's canvas
promise: the one surface where the art actually lives shows you what you are
editing and what you have stepped inside of.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **D17 — Nested scope** (`docs/00-design.md:484`; prose `:225-228`). "A
  nested-composition cell can be **expanded** … or **entered** (double-click →
  isolation scope: **canvas/list/overview show the child, outside dims**,
  breadcrumb climbs out). **Select ≠ expand ≠ enter.**" This is the exact
  behavior this leaf realizes on the canvas; the list/overview/breadcrumb are the
  siblings' (layers'/overview's).
- **D18 — Uniform dockspace** (`:485`; prose `:224-228`). "**Canvas is a
  view**" → the dim reflects on **every** open canvas, since each is a view over
  the one project-level scope (D19).
- **D19 — Project-scoped state** (`:486`). Selection and the shared panels
  belong to the **project**, not any canvas — so `entered_composition` is
  **project-level, singleton**, read the same by every canvas. The scope is not
  per-canvas (contrast `editor.cameras.look_through`'s per-canvas active camera).
- **D10 — the invisible translator** (working/display-space discipline;
  `docs/00-design.md` D10 row). Any compositing the editor does — including this
  dim — happens in the **linear working space**, never over gamma-encoded sRGB8
  bytes; a scrim alpha-blended over the packed display bytes is the classic
  dark-halo/again-gamma bug D10 forbids. This is why the dim lives in `render`
  (which owns the working-space surface via `to_srgb8_over`), not as an ImGui
  scrim over the presented texture.
- **D15 — Undo boundary** (`docs/00-design.md` D15 row). The scope is transient
  session state (like selection and the transient camera): **no transaction, no
  journal entry, not undoable** — entering/leaving a scope is not a document edit.
- **D7 — one select tool** and the cells/cameras shape: the entered scope is a
  **composition**, resolved from the nested cell's generic `composition_ref()`,
  never a `kind_id` switch (A16).

**Architecture (normative):**

- **A5** (`docs/01-architecture.md:227-240`, log `:420`) — "N observers, one
  document": a canvas is one `HostViewport`/`InteractiveRenderer`, panels are
  project-level. The scope value is project-level state fed identically to each
  per-canvas renderer; **no new locking** (the scope rides the existing
  per-canvas value-channel discipline, like `set_camera`).
- **A6** (`:242-269`, log `:421`) — the display path: `CpuBackend` tiles → the
  canvas view uploads them as GL textures → composites to the pane. The dim is
  baked into the **`CpuBackend` frame** (before the GL upload), so A6's
  texture-speaking display path is unchanged and a future GPU `Backend` inherits
  the dim for free.
- **§7 / §8 component map + levelization** (`:276-344`). `scene` = L1 (the focus
  quad); `render` = L2 (may depend on `base, project, scene, gl, writer,
  libarbc`; GL but **not** ImGui — the dim composite lives here); `app` = L4 (reads
  `AppState`, drives the channel). **`render → scene` is already a declared DAG
  edge** (`scripts/check_levels.py` `render: {base, project, scene, gl, writer}`),
  so the render's read of `scene::composition_focus_quad` introduces **no new
  edge**.
- **§9 / §9.1** — the universal DoD and the offscreen software-GL ASan/TSan lane.

**libarbc surface** (fetched under `build/*/_deps/arbc-src/`):

- `arbc::Viewport { int width; int height; Affine camera; ObjectId anchor; }`
  (`src/compositor/arbc/compositor/compositor.hpp:16-36`). The `anchor` field
  **is** composition-scoped ("draws exactly this composition's direct members"),
  but it is **driver-managed for zoom rebasing** — "Rebasing re-picks this as the
  user zooms … the persistent value lives in runtime"
  (`compositor.hpp:22-29`). So `anchor` is **not** a free handle for the user
  isolation scope; hijacking it would fight the rebasing that keeps transforms
  well-conditioned. This leaf therefore renders the full document once and **dims
  the complement of the focus quad in a post-composite**, rather than re-anchoring
  (see D-isolation_scope-2).
- `arbc::CpuBackend::{clear, composite, convert}` — the working-space primitives
  `to_srgb8_over` already uses (`render.cpp:64-74`); the dim scrim is one more
  `clear`/`composite` in the same working space.
- `arbc::Rect`, `arbc::Affine` (`apply`, `inverse`) — the geometry the focus quad
  and its device-space mask speak.

**Editor seams this leaf extends:**

- **The scope value:** `AppState::entered_composition()`
  (`app_state.hpp:149-150`), read each frame in `app::CanvasView`
  (`src/app/ace/app/canvas_view.hpp:62`; `state_` `:378`, `host_` `:381`).
- **The placed-quad source:** `scene::cells(document, registry, composition)`
  (`cell.hpp:238`), `scene::nested_composition_of` (`:246`),
  `scene::composition_path` (`:271`), `scene::Cell{placement, content_bounds}`
  (`cell.hpp:197-220`) — the new `scene::composition_focus_quad` walks these.
- **The render composite:** `render::render_document_srgb8` (`render.hpp:47`),
  `to_srgb8_over` (`render.cpp:50-74`) — the new dim entry point sits beside them;
  `render::CanvasRenderer` (`canvas_renderer.hpp:55`) applies it per interactive
  frame behind the new scope channel; `config.viewport` build at
  `canvas_renderer.cpp:129`.
- **The per-canvas channel pattern:** `CanvasHost::request_camera` (stash
  `canvas_host.cpp:97`, apply `:270-272`) → `CanvasRenderer::set_camera`
  (`canvas_renderer.hpp:109`); the new `set_scope` mirrors it exactly.
- **The A6 upload (unchanged):** `host_.consume(...)` +
  `gl::upload_rgba8`/`update_rgba8` (`src/app/canvas_view.cpp:190-204`).

**Predecessor / sibling refinements:** `tasks/refinements/editor.panels/layers.md`
(the scope model, fail-safe, D-layers-3/-6), `tasks/refinements/editor/look_through.md`
(the per-canvas value-channel pattern `request_camera`/`request_resize`, the
`CanvasHost`/`CanvasRenderer` seam, the golden/e2e/sanitizer rig shape),
`tasks/refinements/editor.canvas/tool_dispatch.md` and
`tasks/refinements/editor.canvas/render_loop_liveness_wake.md` (canvas-area style).

**Test rigs:** `ace_tests` (Catch2, headless, inline `arbc::WorkerPoolConfig{}` for
determinism); goldens under `tests/goldens/` via `ace_test::compare_golden`
(`tests/golden_support.hpp:36`); `ace_shell_test` (ImGui Test Engine, offscreen
software-GL) modeled on `tests/layers_e2e_test.cpp` / `tests/canvas_view_e2e_test.cpp`;
`tests/canvas_host_test.cpp` for host-level inline/real-pool + sanitizer cases
(`default_interactive_pool_config()`); `asan`/`tsan` presets, `tests/lsan.supp`;
coverage `diff-cover --fail-under=90`.

## Constraints / requirements

1. **Levelization (primary structural assertion).** The **focus quad** lands in
   **L1 `scene`** (pure geometry over the pinned snapshot: placement × bounds; no
   UI include). The **dim composite** lands in **L2 `render`**, which already
   depends on `scene` — **no new component, no new DAG edge, no `check_levels`
   edit**; nothing in the L1/L2 core gains an ImGui/GL/SDL-beyond-GL include
   (`render` may see GL, never ImGui). `app` (L4) reads `AppState` and drives the
   channel. `scripts/gate`'s level lint stays clean.

2. **`nullopt` scope ⇒ byte-identical to today (the regression guarantee).** With
   `entered_composition == nullopt`, or a scope that does not resolve to a live
   nested composition, the render composites **no scrim** and the produced sRGB8
   frame is **byte-for-byte** the pre-scope frame. This is asserted directly by a
   golden (Constraint met by test), and it is what keeps every existing canvas /
   export golden valid without re-baseline.

3. **Export never dims (D10/D14).** The dim is reachable only through the new
   scoped canvas entry point; the **export path is untouched** — `editor.cameras.export`
   renders through the 2-arg `render_document_srgb8` (root anchor, no scope
   argument), so an exported PNG carries no dim. No export golden re-baselines.

4. **The dim composites in the linear working space (D10).** The scrim is laid
   down with `CpuBackend::clear`/`composite` in the **same working-space surface**
   `to_srgb8_over` uses (`render.cpp:50-74`), and only then does
   `CpuBackend::convert` encode to sRGB8 — **never** an alpha-blend over the packed
   display bytes. The scrim is a neutral dim (a fixed dim colour + coverage; a
   named `render` constant in the `k_grid_line_cap` spirit, `canvas_view.cpp:51`),
   pinned by golden; a design tweak to the exact strength is a one-line constant
   change, not a re-refinement.

5. **The focus region is the entered composition's placed extent; dim its
   complement.** `scene::composition_focus_quad(document, entered)` returns the
   **wrapping nested cell's `placement` applied to its `content_bounds`** (a quad
   in root/composition space), found by locating the cell whose
   `nested_composition_of(cell) == entered` in `entered`'s parent composition
   (parent reached via `composition_path`). The render maps that quad to device
   space through the same `Viewport::camera` the frame used and dims the
   complement. A rotated/sheared placement yields a genuine quad, not an
   axis-aligned box; the axis-aligned case (the overwhelming common one) is the
   quad's fast path. `nullopt` (Root, or an unbounded / unresolvable composition)
   ⇒ no focus, no dim.

6. **Project-level, singleton, per-frame, fail-safe (D19; layers Constraint 8).**
   The scope is read from the one project-level `AppState` each frame in
   `app::CanvasView` and fed to **every** canvas's renderer, so N canvases dim the
   same child. It is **not** a per-canvas field and carries **no** project state
   (a canvas is only a camera, D19). A scope whose id is no longer a live nested
   composition **falls back to Root** (no dim) on the next frame — never a crash,
   never a stale dim over deleted geometry. This reuses the layers/`active_composition`
   fail-safe posture, not a new one.

7. **Transient, never a transaction (D15).** Entering/leaving a scope makes **no**
   `commands::dispatch` call, **no** `apply_edit`, **no** journal entry, and is
   **not** persisted in `project.arbc`. The scope channel carries a value on the
   UI thread exactly like `set_camera`; setting/clearing it is a pure channel
   write.

8. **No new locking (A5/A4).** The scope value crosses to the render thread
   through the **existing** per-canvas channel discipline (stash-under-the-host's
   lock, apply on the render/writer thread) that `request_camera` already uses —
   the new channel adds a value, not a new synchronization primitive. The focus
   quad is computed from the lock-free `pin()` snapshot. This leaf introduces **no
   new shared mutable state** beyond the one channel slot.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate`
green (check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.** No
  new component, no new DAG edge, no lint edit; `scene` gains no UI include, the
  `render` dim composite includes no ImGui, `render → scene` is the already-declared
  edge. Confirm `scripts/gate`'s level lint passes.

- **L1 logic — Catch2 unit** (`tests/isolation_scope_test.cpp`, in `ace_tests`,
  headless, inline `arbc::WorkerPoolConfig{}`):
  - `TEST_CASE("isolation_scope: focus quad is the wrapping cell's placement × content_bounds")`
    — build a doc with a nested composition placed by a known affine; assert
    `scene::composition_focus_quad(doc, nested_id)` returns the four corners of
    `placement.apply(content_bounds)`.
  - `TEST_CASE("isolation_scope: rotated/sheared placement yields a true quad")`
    — a non-axis-aligned placement returns a quad whose corners are the
    transformed content-bounds corners (not an AABB), so the mask can dim the true
    complement.
  - `TEST_CASE("isolation_scope: fail-safe — Root / unresolvable / non-nested id yields nullopt")`
    — `nullopt` scope, a GC'd/foreign id, and a plain (non-nested) cell each yield
    `nullopt` (no focus), the render's no-dim signal.
  - `TEST_CASE("isolation_scope: unbounded child (no content_bounds) yields nullopt")`
    — a nested cell whose content is unbounded has no finite focus region → no dim
    (never dims the whole plane's complement, which is empty).

- **Rendered output — golden (`render_offline`-byte-exact, the DoD test for canvas
  composition), no tolerance:**
  - `tests/goldens/isolation_scope_dim_64x64.rgba8` — build a cells doc with a
    nested composition, render the scoped offline entry point with `entered` = the
    nested id; assert the frame is byte-identical to the stored golden, which shows
    the **child region bright and the complement dimmed** (the observable D17
    reflection, dim strength pinned).
  - **The regression golden (Constraint 2):** assert the scoped entry with a
    `nullopt` / unresolvable scope is **byte-identical** to
    `render_document_srgb8(doc, w, h, camera)` for the same doc — the "scope off ⇒
    no pixel change" guarantee, which is what keeps every other canvas/export
    golden valid.
  - **A rotated-placement golden** (`isolation_scope_dim_rotated_64x64.rgba8`)
    pins that the dim complement follows the true quad, not an AABB.

- **UI e2e — ImGui Test Engine** (`tests/isolation_scope_e2e_test.cpp`, in
  `ace_shell_test`, offscreen software-GL, modeled on `tests/layers_e2e_test.cpp`
  / `tests/canvas_view_e2e_test.cpp`), driven by widget id:
  - Boot the real shell over a doc with a nested composition; open a canvas.
    Sample a pane pixel **outside** the child's placed extent and one **inside**
    it via `glReadPixels` at Root scope (baseline).
  - Enter the composition (set `AppState::entered_composition` via the layers
    enter gesture, `###layer_row_<id>` double-click, or the state setter as the
    layers e2e drives it); assert `frames_issued()` advanced and, on the next
    frame, the **outside** pixel is measurably **darker** than baseline while the
    **inside** pixel is **unchanged** — the "child bright, outside dims" property.
  - Climb out via the breadcrumb (`###crumb_<id>` → Root); assert the dim is gone
    (outside pixel returns to baseline).
  - **Multi-canvas (D19):** with two canvases open, entering the scope dims
    **both** panes' outside regions (the project-level singleton reflection).
  - A **screenshot baseline** is captured for signal (not a byte-exact golden —
    the byte assertion is the `render_offline` golden above).

- **Threading — ASan/TSan** (one case appended to `tests/canvas_host_test.cpp`'s
  real-pool sanitizer suite with `default_interactive_pool_config()`): two
  entries; the UI thread streams `set_scope(entered)` ⇄ `set_scope(nullopt)`
  toggles interleaved with `request_camera`/`apply_edit` while the render thread
  `drive_once`s both entries and each frame recomputes the focus quad from
  `pin()`. Must be data-race-clean: the new scope channel coexists with the
  settled camera/resize channels under the existing host-lock discipline (this
  leaf adds **no new shared mutable state** beyond the one channel slot; the focus
  quad read is the lock-free `pin()` snapshot). Residual Mesa leaks via
  `tests/lsan.supp`.

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`); clang-format +
  build clean across presets.

**No new WBS leaf is deferred.** The focus-quad helper, the dim composite, the
scope channel and the wiring all land here. Scoped **editing** inside the entered
composition (insert/delete honoring scope) is the already-scheduled sibling
`editor.cells.scoped_edit` (`tasks/00-editor.tji:591`, D-layers-6) — a consumer of
the same `AppState::entered_composition`, not new work here. The Overview's own
dim reflection (D17's "overview show the child") is `editor.panels.overview`'s
scope, not this leaf's. No parking-lot item surfaces — the one empirical value
(the exact dim strength) is a named constant pinned by golden, a call made here,
not deferred.

## Decisions

- **D-isolation_scope-1 — The dim is baked into the L2 `render` frame, not drawn
  as an ImGui scrim over the presented texture.** The scoped render composites the
  dim in the linear working space (reusing `to_srgb8_over`'s second-surface
  pattern, `render.cpp:50-74`) before the `CpuBackend::convert` sRGB8 tail; the A6
  GL upload and the ImGui chrome (grid/marquee/gizmo) are unchanged. *Rationale:*
  (i) the `.tji` charters exactly "the CanvasRenderer/DocumentBinding path that
  composites the child and dims the parent"; (ii) it yields a **byte-exact
  `render_offline` golden** — §9's preferred test for canvas composition — where
  an ImGui overlay could only get a screenshot baseline; (iii) D10 requires
  compositing in the working space, which `render` owns, not the packed sRGB8
  display bytes an ImGui scrim would darken; (iv) a render-baked dim rides the
  `CpuBackend` frame, so A6's texture-speaking display path and any future GPU
  `Backend` inherit it for free. *Alternative rejected — an ImGui dim scrim in
  `app::CanvasView::draw_content`:* cheaper, but it dims the editing chrome along
  with the art, darkens gamma-encoded display bytes (the D10 halo bug), and
  downgrades the test to a screenshot baseline; it also contradicts the `.tji`'s
  named render path. **No doc delta.**

- **D-isolation_scope-2 — Render the full document once and dim the complement of
  the focus quad; do NOT re-anchor the viewport to the entered composition.**
  Although `arbc::Viewport::anchor` is composition-scoped ("draws exactly this
  composition's direct members", `compositor.hpp:16-36`), the driver **re-picks
  the anchor per frame for zoom rebasing** (`compositor.hpp:22-29`), so it is not a
  free handle for the user isolation scope. This leaf keeps the existing
  single-pass root render (the child's cells already composite up through the
  nested cell, so they are shown) and applies a **post-composite masked dim** over
  everything outside the focus quad. *Rationale:* one render pass (interactive-cheap),
  the rebasing machinery is untouched, `nullopt` scope is trivially byte-identical
  (Constraint 2), and "show the child's cells, everything outside dims" is exactly
  a bright-inside / dim-outside mask — no second render, no placement re-composite
  bookkeeping. *Alternative rejected — two-pass isolation (dim the root backdrop,
  then composite the anchor-scoped child bright on top):* libarbc-native via
  `anchor`, but it fights the driver's rebasing, doubles per-frame render cost in
  the interactive loop, and needs the child's placement transform to re-seat the
  isolated render — all to reach the same picture the single-pass mask produces.
  *Alternative rejected — dim by per-cell membership (dim every cell not in the
  entered composition):* more work per frame and the entered composition's **placed
  extent** is the crisp "outside" boundary D17 names, so the quad mask is both
  simpler and more faithful. **No doc delta.**

- **D-isolation_scope-3 — The scope is project-level session state fed to every
  canvas through a new `set_scope` channel mirroring `set_camera`; it is not a
  per-canvas field and not a transaction.** `app::CanvasView` reads the one
  `AppState::entered_composition` each frame and calls
  `CanvasHost::request_scope(view_id, entered)` → `CanvasRenderer::set_scope`,
  stashed and applied on the render thread exactly as `request_camera` is
  (`canvas_host.cpp:97,270-272`). *Rationale:* D19 makes the scope project-level
  and singleton (canvas/list/overview reflect one child), so feeding the same
  value to each per-canvas renderer keeps N canvases coherent with **no new
  locking** (A5) and no per-canvas scope state; D15 keeps it off the journal and
  out of `project.arbc`. This is deliberately the **inverse** of
  `editor.cameras.look_through`'s **per-canvas** active camera — a shot is a
  canvas's own framing, a scope is the project's — and reuses that leaf's channel
  pattern without copying its per-canvas placement. *Alternative rejected — a
  per-canvas entered scope:* contradicts D-layers-3/D19 (one shared scope) and
  would let two canvases disagree about "where you are," which the list and
  breadcrumb cannot mirror. **No doc delta.**

- **D-isolation_scope-4 — The focus region is the wrapping nested cell's placed
  extent (a quad), resolved by a new pure-L1 `scene::composition_focus_quad`;
  `nullopt` ⇒ no dim.** The helper locates the cell whose
  `nested_composition_of(cell) == entered` in `entered`'s parent composition
  (parent via `composition_path`) and returns `placement.apply(content_bounds)`;
  it is registry-free (geometry + generic `composition_ref()`, never a `kind_id`
  switch, A16) so `render` calls it without threading a `Registry`. *Rationale:*
  the placed extent is the exact "everything outside" boundary D17 legislates, it
  is pure geometry over the pinned snapshot (Catch2-testable with hand-built
  docs), and returning a **quad** (not an AABB) keeps a rotated/sheared placement's
  dim faithful. Fail-safe to `nullopt` matches the layers `active_composition`
  posture (Constraint 6). *Alternative rejected — put the resolution in `render`
  or `app`:* pushes z-order/parent-walk geometry out of Catch2 reach and
  duplicates the walk the Layers list already owns in `scene`; `scene` is its
  natural, already-levelized home. **No doc delta.**

## Open questions

(none — all decided.) The single tunable — the exact dim strength — is a named
`render` constant pinned by the `isolation_scope_dim_64x64` golden (a call made
here per Constraint 4), not a deferred decision and not a parking-lot item. No new
WBS leaf is spawned: scoped editing is the scheduled `editor.cells.scoped_edit`
and the overview's dim is `editor.panels.overview`'s, both consumers of the same
`AppState::entered_composition`. No human-judgment item surfaces for
`tasks/parking-lot.md`.

## Status

**Done** — 2026-07-29.

- `src/scene/focus_quad.cpp` + `src/scene/ace/scene/cell.hpp`: pure-L1 `scene::composition_focus_quad` helper — walks the parent composition via `composition_path`, locates the wrapping nested cell, and returns `placement.apply(content_bounds)` as a quad; `nullopt` on Root / unresolvable / unbounded child.
- `src/render/dim_scrim.hpp` + `src/render/dim_scrim.cpp`: L2 dim-scrim composite in the linear working space using `CpuBackend::clear`/`composite`; named constant for dim strength, never over sRGB8 bytes (D10).
- `src/render/ace/render/render.hpp` + `src/render/render.cpp`: new `render_document_srgb8_scoped` offline entry point; `nullopt` scope is byte-identical to the 2-arg form (Constraint 2 regression guarantee).
- `src/render/ace/render/canvas_renderer.hpp` + `src/render/canvas_renderer.cpp`: `set_scope` / `request_scope` per-canvas value channel mirroring the settled `set_camera` / `request_camera` discipline; focus quad computed from the lock-free `pin()` snapshot.
- `src/render/ace/render/canvas_host.hpp` + `src/render/canvas_host.cpp`: `request_scope` stash-and-apply following the `request_camera` pattern (stash under host lock, apply on render thread); no new locking (A5).
- `src/app/ace/app/canvas_view.hpp` + `src/app/canvas_view.cpp`: reads `AppState::entered_composition` each frame and calls `CanvasHost::request_scope`, feeding the project-level singleton to every canvas (D19).
- `tests/isolation_scope_test.cpp`: Catch2 units — focus-quad geometry, multi-level nesting, rotated true-quad, fail-safe, unbounded-child, byte-identical no-scope regression.
- `tests/goldens/isolation_scope_dim_64x64.rgba8` + `tests/goldens/isolation_scope_dim_rotated_64x64.rgba8`: render_offline golden files — dim and rotated-dim cases, byte-exact.
- `tests/isolation_scope_e2e_test.cpp`: ImGui Test Engine e2e — single-canvas dim/restore cycle + multi-canvas D19 check.
- `tests/canvas_host_test.cpp`: two additions — isolation TSan anchor (streamed scope toggles) + deterministic scope-republish/no-op case.
- `CMakeLists.txt`: wired the new source files and test targets.
- `check_levels` clean: no new component, no new DAG edge (`render → scene` already declared); all 477 `ace_tests` + relevant e2es green; goldens cross-config-stable dev↔release.
