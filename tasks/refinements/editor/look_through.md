# editor.cameras.look_through — Activate a shot; multi-canvas look-through

## TaskJuggler entry

- **Task:** `editor.cameras.look_through` (`tasks/00-editor.tji:254-259`, under
  `task cameras "Cameras"` at `:219`).
- **Effort:** `1.5d` (`:255`) · `allocate team` (`:256`).
- **Depends:** `!model` (`editor.cameras.model`) and `editor.canvas.multi_canvas`
  (`:257`).
- **Note (`:258`):** "Make a shot the active camera of a canvas ('look through
  Hero'); a second canvas can look through a shot for a live export preview beside
  the editing Viewport. Design: D18."
- **Back-link:** the `.tji` note currently ends `Refinement:
  tasks/refinements/look_through.md` (the flat interim path). This refinement lands
  at **`tasks/refinements/editor/look_through.md`** per the orchestrator's
  area = first-dot-segment (`editor`) assignment; the closer updates the note
  back-link to the real path and adds `complete 100` after `allocate team`
  (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji` here.
- **Downstream:** none `depends` on this leaf directly. It is a consumer that
  makes the persisted camera model (`editor.cameras.model`) and N-canvas host
  (`editor.canvas.multi_canvas`) render the D18 payoff — "paint through Viewport ‖
  look through Hero." `editor.cameras.export` is a **sibling** (both `!model`,
  independent) that reuses the inverse render-through-camera derivation this leaf
  introduces (see D-look_through-4).

## Effort estimate

**1.5 days.** The keystone math and both dependencies already exist, so the work
is thin and mostly wiring: (1) one pure-L1 `interact` helper — the inverse of
`new_shot_from_view` (frame + resolution → the comp→device render camera) plus a
pane-fit wrapper (~0.4d incl. Catch2); (2) a per-canvas session-state selection
beside the transient viewport camera, resolved and re-submitted each frame through
the **existing** `request_resize` + `request_camera` seams (~0.4d); (3) the
in-canvas camera picker widget + aspect-fit ("letterbox") present (~0.4d); (4) the
golden + ImGui Test Engine e2e + one sanitizer case (~0.3d). No new component, no
new DAG edge, no new libarbc surface, no transaction — the active-camera selection
is session state (D-model-3), not scene data.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cameras.model`** (`tasks/refinements/cameras/model.md`, Done
  2026-07-18) — the persisted shot-camera model this leaf activates. It ships
  `scene::Camera { ObjectId id; ObjectId layer; std::string name; Resolution
  resolution; arbc::Affine frame; }` (`src/scene/ace/scene/camera.hpp:113-119`)
  and the read accessor `scene::cameras(const arbc::Document&) →
  std::vector<Camera>` over the lock-free `pin()` reader
  (`camera.hpp:121-124`). Crucially, `Camera.frame` is the binding layer's
  transform **device → composition** (`camera.hpp:118`; the "inverse camera" of
  `new_shot_from_view`, `interact.hpp:71-73`), so the comp→device render camera is
  `frame.inverse()` scaled to the output resolution — the exact derivation this
  leaf owns. **D-model-3** (`cameras/model.md:344-353`) explicitly charters the
  active-camera *selection* — "which shot a canvas looks through" — to **this
  leaf** as session state; and **Constraint 6 / interact.hpp:74-76** deferred the
  "frame+resolution → `arbc::Viewport`" inverse "to `editor.cameras.export`" —
  which this leaf provides first (D-look_through-4).
- **`editor.canvas.multi_canvas`** (`tasks/refinements/editor/multi_canvas.md`,
  Done 2026-07-18) — the N-canvas host. `render::CanvasHost` owns one
  `arbc::WorkerPool` + a `canvas#N → CanvasRenderer` entry map + one shared drive
  loop; `app::CanvasView` holds a per-pane presenter map `presenters_`
  (`src/app/ace/app/canvas_view.hpp:117`) keyed by dock `view_id`. It "deliberately
  does **not** give each canvas its own camera — each still frames the whole
  document through a default viewport … per-canvas cameras are
  `editor.cameras.model` / `look_through`" (`multi_canvas.md:41-44`). That is
  exactly the seam this leaf fills: a *second* canvas pointed at a *different*
  camera. The per-canvas camera submit path (`request_camera`) and per-entry resize
  (`request_resize`) are settled and reused verbatim.
- **`editor.canvas.nav`** (`tasks/refinements/editor/nav.md`) — the transient
  viewport camera `app::CanvasView::Presenter::camera`
  (`src/app/ace/app/canvas_view.hpp:109`; D-nav-1: "never a transact, never
  persisted; a per-pane value like Selection"), submitted via
  `host_.request_camera(view_id, p.camera)` (`src/app/canvas_view.cpp:114`). This
  leaf **overrides** the submitted camera when a shot is selected, leaving
  `Presenter::camera` untouched (D-look_through-6).
- **`editor.canvas.edit_render_sync`** (`tasks/refinements/editor/edit_render_sync.md`,
  Done 2026-07-19) — the UI-thread `Document`-edit serialization via
  `CanvasHost::apply_edit` under `doc_mu` (`src/render/canvas_host.cpp:150-165`).
  Manip edits to a shot's frame (the source of a *live* look-through preview
  update) flow through this seam; this leaf's per-frame **read** of
  `scene::cameras` rides the independent lock-free `pin()` snapshot, not `doc_mu`
  (D-look_through-7).
- **`editor.canvas.nested_composition_binding`** (Done) — threads the process
  `Registry` + per-renderer `KindBridge` into the render path so `org.arbc.camera`
  custom kinds resolve identically across the interactive render (consumed as-is;
  no change here).

**Pending (owned here):** the inverse render-through-camera derivation
(`interact`), the per-canvas active-camera selection + per-frame resolve, the
in-canvas camera picker, and the aspect-fit present. Nothing downstream is blocked
on an unwritten predecessor.

## What this task is

Make a **saved shot camera the active camera of a canvas** — "look through Hero" —
and let a **second canvas look through a shot** as a live export-framing preview
beside the free editing Viewport (D18's multi-camera payoff). Concretely:

1. Each `canvas#N` gains a **per-canvas active-camera selection** — session state
   (`std::optional<arbc::ObjectId>` beside `Presenter::camera`): `nullopt` = the
   free viewport camera (today's behavior); a shot's `ObjectId` = look through that
   shot. This is **not** persisted and **not** a transaction (D-model-3, D15).
2. When a canvas looks through a shot, it renders that shot's **exact crop**: the
   app derives the shot's own `arbc::Viewport` (resolution + comp→device camera)
   from `Camera.frame`/`Camera.resolution` and feeds it through the **existing**
   `request_resize` + `request_camera` seams, so the rendered image *is*
   `render_offline` through that shot (byte-convergent) — the "exact export
   framing" promise, live.
3. A small **camera picker** in the Canvas view body (`Viewport ▾ | Hero | Thumb …`,
   populated from `scene::cameras`) sets each canvas's selection independently, so
   two canvases render through two cameras side by side over one `Document` (A5).

It ships **no scene mutation** and **no new render channel** — it feeds the
multi-canvas submit seams different *values* (a shot's viewport instead of the
free viewport) and adds one pure-L1 derivation + one picker widget + an aspect-fit
present.

## Why it needs to be done

D18 (`docs/00-design.md:430-435`, `:479`) makes "Canvas is a view → multiple
canvases through different cameras side by side (paint-through-Viewport beside
look-through-Hero)" a headline of the uniform dockspace, and D2 (`:64-66`, `:90`)
defines "'Look through Hero' makes Hero the active camera." Today the machinery for
both halves exists but is not wired together: `editor.cameras.model` persists shot
cameras but nothing renders *through* one, and `editor.canvas.multi_canvas` renders
N canvases but each frames the whole document through the free default viewport
(`multi_canvas.md:41-44`). This leaf is the join that turns a persisted shot into a
canvas's live framing — the last piece of the "compose in the Viewport, preview the
export beside it" workflow, and the first consumer to render the frame+resolution →
`arbc::Viewport` inverse that `editor.cameras.export` also needs.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **D2 — Canvas model** (`docs/00-design.md:463`; prose `:47-74`). A camera **is**
  the library's `Viewport` (output resolution + region→device affine, `:49-51`);
  two roles, one primitive. "'**Look through Hero' makes Hero the active camera**"
  (`:64`); the canvas renders "**the active camera's view**" (`:90`). Export =
  render-through-camera at its own resolution (`:67-70`).
- **D9 — Camera frame ≠ resolution** (`:470`). A shot has its **own export
  resolution**, distinct from its frame; "**Space pans the active viewport
  camera** (transient) — distinct from **re-framing a saved shot camera** (scene
  edit)." This legislates D-look_through-3 (the shot's resolution is first-class,
  independent of pane size) and D-look_through-6 (free-nav is the viewport
  camera's; reframing a shot is `cameras.manip`'s scene edit, not this leaf).
- **D18 — Uniform dockspace** (`:479`; prose `:430-435`). "**Canvas is a view** →
  multiple canvases through different cameras side by side (paint-through-Viewport
  beside look-through-Hero) … shows its exact export framing live." The multi-camera
  side-by-side is the deliverable; `:249` names the "**look through** button [that]
  snaps the editing view to the camera."
- **D6 — Overview / shot map** (`:467`; prose `:170-176`). Every camera in the
  overview carries "a label and a '**look through**' affordance." That overview
  button is `editor.panels.overview`'s to draw; it *consumes* this leaf's per-canvas
  selection setter (D-look_through-5).
- **D14 — Export via cameras** (`:475`) and **D15 — Undo boundary** (`:476`,
  `:363-372`): the boundaries this leaf respects — it renders through a shot but
  does **not** own `render_offline` file output (export) and makes **no**
  transaction (the active-camera selection is session state, D15).
- **D19 — Project-scoped** (`:480`): canvases are *only* cameras and carry no
  selection/panel state; the shot list is project-level, read the same by every
  canvas.
- **Architecture:** **A5** (`docs/01-architecture.md:84-97`, log `:255`) — "N
  renderers … two cameras, one document"; **A4** (`:59-82`, `:254`) — single-writer
  / render-thread-confined caches, UI reads via `pin()`; **A7** (`:257`) —
  one process, one `Document`, project-level selection; **§7** component map
  (`:112-137`; `interact` = pure UI-agnostic math, L1, `:128`); **§8** levelization
  DAG (`:144-179`); **§9 / §9.1** the universal DoD + the offscreen software-GL
  ASan/TSan lane (`:181-245`).

**libarbc API surface** (fetched under `build/*/_deps/arbc-src/`):

- `arbc::Viewport { int width; int height; Affine camera; ObjectId anchor; }` —
  `arbc/compositor/compositor.hpp:16-36` (the transient render spec built per
  canvas). `arbc::Affine` (+ `inverse()`, `apply()`), the value the whole camera
  math speaks.
- `arbc::render_offline(document, backend, viewport)` — the byte-exact reference
  the interactive look-through converges to; wrapped by
  `render::render_document_srgb8(document, w, h, camera = identity)`
  (`src/render/ace/render/render.hpp:37`, `src/render/render.cpp:22-43`, building
  `arbc::Viewport{w,h,camera}` at `render.cpp:25`).
- `Document::pin()` — the lock-free immutable reader `scene::cameras` uses
  (concurrency-safe against the writer and the render thread's live read).

**Editor seams this leaf extends:**

- **The camera model:** `scene::cameras(const arbc::Document&)`
  (`src/scene/ace/scene/camera.hpp:124`); `scene::Camera` (`:113-119`); `Resolution
  {width,height}` (`camera.hpp:26-31`).
- **The inverse-derivation home:** `interact::new_shot_from_view(const arbc::Affine&
  camera, int pane_w, int pane_h) → ShotFraming{frame, width, height}`
  (`src/interact/ace/interact/interact.hpp:56-77`) — the **forward** view→shot
  snapshot; its comment (`:74-76`) defers the inverse "frame+resolution →
  `arbc::Viewport`" to export. This leaf adds the inverse **beside it** in
  `interact` (D-look_through-4).
- **The per-canvas camera submit path (reused verbatim):**
  `app::CanvasView::Presenter::camera` (`src/app/ace/app/canvas_view.hpp:109`) +
  `presenters_` (`:117`); `host_.request_camera(view_id, camera)`
  (`src/app/canvas_view.cpp:114`) → `CanvasHost::request_camera`
  (`src/render/canvas_host.cpp:141`, stash `:97`, apply `:270-272`) →
  `CanvasRenderer::set_camera` (`src/render/canvas_renderer.cpp:179-187`); the
  per-frame `draw_content(view_id, w, h)` entry point
  (`src/app/canvas_view.cpp:36`) and the per-pane resize call it forwards to
  `CanvasHost` resize. **The exact line a look-through changes** is
  `config.viewport = arbc::Viewport{width, height, camera}`
  (`src/render/canvas_renderer.cpp:83`) — width/height come from the resize
  channel, camera from `request_camera`; look-through feeds a shot's values into
  **both** existing channels, no new one.
- **The view body / picker home:** `views::register_view_body`
  (`src/views/ace/views/views.hpp:79`); the Canvas body registered at
  `src/app/shell.cpp:217-225`.
- **The live-edit seam:** `CanvasHost::apply_edit` under `doc_mu`
  (`src/render/canvas_host.cpp:150-165`, `:90`); `AppProjectGateway::run_edit`
  (`src/app/project_gateway.cpp:102`), bound at `src/app/shell.cpp:275`.

**Predecessor / sibling refinements:** `tasks/refinements/cameras/model.md`
(the Camera model, `frame` orientation, D-model-3), `tasks/refinements/editor/multi_canvas.md`
(the N-canvas host, `request_camera`/resize channels, the sanitizer lane),
`tasks/refinements/editor/nav.md` (the transient camera), `tasks/refinements/editor/edit_render_sync.md`
(the `doc_mu` edit seam).

**Test rigs:** `ace_tests` (Catch2, headless, inline `arbc::WorkerPoolConfig{}` for
determinism), goldens under `tests/goldens/` via `ace_test::compare_golden`
(`tests/golden_support.hpp:36`); `ace_shell_test` (ImGui Test Engine, offscreen
software-GL) modeled on `tests/multi_canvas_e2e_test.cpp` /
`tests/canvas_view_e2e_test.cpp`; `tests/canvas_host_test.cpp` for the host-level
inline/real-pool cases; `asan`/`tsan` presets, `tests/lsan.supp`; coverage
`diff-cover --fail-under=90`.

## Constraints / requirements

1. **Levelization (primary structural assertion).** The inverse derivation lands
   in **L1 `interact`** (pure `arbc::Affine` math beside `new_shot_from_view`; no
   `scene` include, no UI include). The per-canvas selection field and the
   per-frame resolve live in **L4 `app`** (`CanvasView::Presenter`); the picker is
   **L3 `views`** (or the L4 canvas body) — both already see ImGui. **No new
   component, no new DAG edge, no `check_levels` edit**; the L1 core never gains an
   ImGui/GL/SDL include. The resolve reads `scene::cameras` from `app`, which
   already sees both `scene` and `interact` — so no `scene→interact` /
   `interact→scene` edge is introduced (the `interact` helpers take *primitive*
   frame/resolution values, never a `scene::Camera`).

2. **Session state, never a transaction (D15, D-model-3).** The active-camera
   selection is per-canvas UI state (`std::optional<ObjectId>` beside
   `Presenter::camera`). It is not persisted in `project.arbc`, does not flow
   through `commands::dispatch` / `apply_edit`, and adds **no** journal entry.
   Selecting/clearing a shot is a pure presenter mutation on the UI thread.

3. **Render the shot's exact crop via its own `arbc::Viewport`, through the
   existing seams (D2/D18).** Looking through shot `S` submits
   `resolution = (out_w, out_h)` (via the resize channel) and
   `camera = interact::viewport_camera_for_shot(S.frame, S.resolution.w,
   S.resolution.h, out_w, out_h)` (via `request_camera`) so the rendered image
   contains **only** `S`'s framed region (no surrounding composition), converging
   byte-for-byte to `render_offline` through `S` at `(out_w, out_h)`. **No new
   render channel** on `CanvasHost`/`CanvasRenderer` — width/height flow through the
   settled resize path, the affine through `request_camera` (`canvas_renderer.cpp:83`
   consumes both unchanged).

4. **The inverse derivation is correct and shared (D-look_through-4).**
   `interact::viewport_camera_for_shot` is the exact inverse of
   `new_shot_from_view`: for any `shot = new_shot_from_view(C, pw, ph)`,
   `viewport_camera_for_shot(shot.frame, shot.width, shot.height, shot.width,
   shot.height) == C` (rendering the shot at its own resolution reproduces the
   view, `interact.hpp:71-73`); at other output resolutions it scales by
   `out/native`. `editor.cameras.export` reuses it (including D14's N× multiplier:
   `out = N·native`).

5. **Per-canvas independence (A5/D19).** Each `canvas#N`'s selection is its own;
   two canvases render through two cameras over one `Document` with no cross-talk
   (the existing per-entry `HostViewport`/cache confinement). Selection carries no
   project state (D19 — a canvas is only a camera).

6. **Free-nav is inert on a shot-locked canvas; `Presenter::camera` is preserved
   (D9, D-look_through-6).** While a shot is selected, the resolve **bypasses**
   `Presenter::camera` (uses the shot's affine), so `editor.canvas.nav`'s pan/zoom/
   Space gestures do not move the framing (re-framing a shot is `cameras.manip`'s
   scene edit, not this leaf) — with **no change to `nav`**. Returning to Viewport
   restores the prior free framing (the untouched `Presenter::camera`).

7. **Live preview via per-frame derivation (D18 "live"; A4).** `draw_content`
   re-reads `scene::cameras(doc)` via `pin()` each frame and re-submits the derived
   viewport, so a `cameras.manip` edit to the shot's frame/resolution (through
   `apply_edit`/`doc_mu`) reflects on the next frame. The `pin()` read is the
   lock-free immutable snapshot (A4); it touches no render cache and takes no
   `doc_mu` — **no new locking**. A selection whose `ObjectId` is no longer in
   `scene::cameras` (a GC'd/deleted shot) **falls back to the free viewport**
   (fail-safe, never a crash or a stale frame).

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate`
green (check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.** No
  new component, no new DAG edge, no lint edit; `interact` gains no `scene`/UI
  include; nothing in the L1 core includes ImGui/GL/SDL. Confirm `scripts/gate`'s
  level lint passes.
- **L1 logic — Catch2 unit** (`tests/look_through_test.cpp`, in `ace_tests`):
  - **Round-trip inverse:** for representative viewport cameras `C` + panes,
    `viewport_camera_for_shot(new_shot_from_view(C,pw,ph).frame, pw, ph, pw, ph)`
    reproduces `C` (float round-trip tolerance justified as numeric inverse, not a
    rendering tolerance) — the frame+resolution → render-camera correctness that
    both look-through and export depend on.
  - **Output scaling:** at `out = k·native` the derived camera equals the native
    camera pre-scaled by `k` (so a shot renders identically framed at any output
    resolution) — covering the pane-fit preview and export's N× multiplier.
  - **Pane-fit wrapper** (`interact::look_through(frame, shot_w, shot_h, pane_w,
    pane_h) → {out_w, out_h, camera}`): `(out_w,out_h)` fits the pane preserving the
    shot's aspect (letterbox dims) for pane-wider / pane-taller / equal-aspect
    cases; `camera == viewport_camera_for_shot(frame, shot_w, shot_h, out_w,
    out_h)`.
  - **Degenerate guards:** non-positive pane or shot resolution, or a
    non-invertible frame, yield a safe no-op (identity / skip), never a
    div-by-zero.
- **Rendered output — golden (`render_offline` byte-exact).** A new golden
  `tests/goldens/look_through_shot_*.rgba8`: build a cells doc + a shot `S`, then
  assert `render_document_srgb8(doc, out_w, out_h, look_through(S.frame,
  S.resolution.w, S.resolution.h, pane_w, pane_h).camera)` is **byte-identical** to
  the stored crop **and** to the affine `editor.cameras.export` would use for the
  same `S` at the same `(out_w,out_h)` — pinning "look-through framing == export
  framing," **no tolerance**. (This is the honest instantiation of D18's "exact
  export framing.")
- **UI e2e — ImGui Test Engine** (`tests/look_through_e2e_test.cpp`, in
  `ace_shell_test`, modeled on `tests/multi_canvas_e2e_test.cpp`), offscreen
  software-GL, driven by widget id:
  - open two canvases (`canvas#1`, `canvas#2`), seed a shot `Hero` (test helper, as
    the camera-model tests create cameras); drive `canvas#2`'s camera picker →
    `Hero`; assert `canvas#2`'s presenter selection == `Hero`'s `ObjectId`,
    `frames_issued() >= 1`, and its pane pixels (`glReadPixels`) differ from
    `canvas#1` (which stays **free** — per-canvas independence, A5/D19).
  - drive `canvas#2` picker → `Viewport`; assert selection clears and the prior free
    framing is restored (`Presenter::camera` preserved).
  - **live update:** with `canvas#2` looking through `Hero`, apply a frame edit to
    `Hero` through the gateway (`apply_edit`/`run_edit`); assert `canvas#2`'s
    sequence advances and its framing changes (the export-preview-is-live property).
  - **GC fallback:** delete `Hero` (a scene edit) while `canvas#2` looks through it;
    assert `canvas#2` falls back to free without crashing (Constraint 7).
- **Threading — ASan/TSan** (one case in the real-pool `CanvasHost` lane, joined to
  `tests/canvas_host_test.cpp`'s sanitizer suite with
  `default_interactive_pool_config()`): two entries, `canvas#2` looking through a
  shot; the UI thread streams `cameras.manip`-style frame edits to that shot via
  `apply_edit` while, each iteration, re-reading `scene::cameras(doc)` via `pin()`,
  re-deriving the look-through viewport, and submitting `request_resize` +
  `request_camera`; the render thread `drive_once`s both entries. Must be
  data-race-clean: the per-frame `pin()` derivation and the settled resize/camera
  channels coexist with the concurrent writer under the existing `doc_mu`
  discipline (this leaf adds **no new shared mutable state** — the selection is
  UI-thread-only). Residual Mesa leaks via the existing `tests/lsan.supp`.
- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`); clang-format +
  build clean across presets.

**No new WBS leaf is deferred.** The inverse derivation lands here (not a future
task — D-look_through-4); the Overview/Layers per-camera "look through" **button**
is `editor.panels.overview`'s own scope (an existing scheduled leaf that consumes
this leaf's selection setter, D-look_through-5), not new work; a full-export-
resolution preview is a rejected fidelity option (D-look_through-3), not a task.

## Decisions

- **D-look_through-1 — The active-camera selection is per-canvas session state, not
  scene data or a transaction.** A `std::optional<arbc::ObjectId>` beside
  `Presenter::camera` (`canvas_view.hpp:109`), per `canvas#N` in `presenters_`
  (`:117`): `nullopt` = free viewport, an `ObjectId` = look through that shot.
  *Rationale:* `cameras/model.md` D-model-3 chartered exactly this to this leaf as
  session state; D15/D19 keep it out of `project.arbc` and off the journal (like
  the transient camera and selection). *Alternative rejected:* persisting the active
  camera per canvas in the `Document` — contradicts D15 (transient framing is not
  scene data) and D18 (layout/view state is local UI state, never `project.arbc`),
  and would demand a canvas identity in the portable file the design refuses.
  **No doc delta required.**

- **D-look_through-2 — Render the shot's exact crop as its own `arbc::Viewport`
  through the existing `request_resize` + `request_camera` seams — no new render
  channel.** Looking through `S` feeds `(out_w,out_h)` and the derived comp→device
  affine into the two settled multi-canvas channels; `canvas_renderer.cpp:83`
  (`arbc::Viewport{width,height,camera}`) consumes both unchanged, so the rendered
  image is `S`'s crop and nothing else. *Rationale:* reuses the exact seam
  `multi_canvas` shipped (the "no new locking, per-entry viewport" contract, A5),
  and makes the preview byte-convergent to `render_offline` through `S` — the
  honest "exact export framing." *Alternative rejected:* submit only a *fit affine*
  at pane resolution (leave the viewport pane-sized, no resize) — cheaper still, but
  the InteractiveRenderer would then render the **whole composition** through that
  affine, so the letterbox margins show surrounding cells rather than clean bars:
  not an export preview. Sizing the viewport to the shot's crop is what clips it.
  **No doc delta required.**

- **D-look_through-3 — The preview renders at a pane-fit resolution (the shot's
  aspect scaled to the pane), letterboxed on present — not the shot's full export
  resolution.** `interact::look_through` picks `(out_w,out_h) ≤ pane` preserving the
  shot aspect; the canvas body draws that texture centered with neutral bars.
  *Rationale:* a shot's export resolution can be 4K; rendering it in full into a
  small live preview pane every frame would tile 4K of work for no visible gain —
  the *framing* (the region and aspect) is exact at any resolution (Constraint 4),
  and `editor.cameras.export` owns the full-resolution pixels for the file. This
  keeps a live preview cheap and honest. *Alternative rejected:* render at the
  shot's full export resolution and downscale on the GPU — heavier per frame with no
  framing benefit; a genuine "1:1 export-pixel inspector" is a possible future
  fidelity option, **not** encoded as a WBS task (it is not required by D18 and
  would be speculative). **No doc delta required.**

- **D-look_through-4 — The inverse render-through-camera derivation lands here, in
  `interact`, and `editor.cameras.export` reuses it.** `interact::viewport_camera_for_shot
  (const arbc::Affine& frame, int native_w, int native_h, int out_w, int out_h) →
  arbc::Affine` (the comp→device camera = `frame.inverse()` scaled by `out/native`),
  plus the `interact::look_through` pane-fit wrapper. *Rationale:*
  `cameras/model.md` Constraint 6 / `interact.hpp:74-76` deferred this inverse "to
  `editor.cameras.export`," but look-through needs it first and independently (no
  `depends` edge between the two sibling leaves), and `interact` — beside the
  forward `new_shot_from_view`, pure `arbc::Affine` math — is its natural home
  (no `scene` edge, since it takes primitive frame/resolution values). Whichever of
  the two sibling leaves lands first defines it; this refinement assigns the home
  and the signature so the other simply consumes it. *Alternative rejected:* let
  each of look-through and export carry its own copy — two derivations of the same
  matrix drift and double the golden surface. **No doc delta required** (the seam is
  an `interact` function, no new component/edge/dependency).

- **D-look_through-5 — The authoritative per-canvas control is a camera picker in
  the Canvas view body; the Overview/Layers "look through" button is
  `editor.panels.overview`'s to wire.** The Canvas body draws a compact picker
  (`Viewport ▾ | <shots from scene::cameras>`) that sets *that* `canvas#N`'s
  selection — unambiguous per-canvas targeting, directly e2e-testable by widget id.
  *Rationale:* with N canvases open, a picker *on the canvas* has an unambiguous
  target; the D6 overview per-camera button needs a "focused/active canvas" notion
  to decide *which* canvas to retarget, which `editor.panels.overview` (the leaf
  that owns overview UI) is the right place to define — it consumes this leaf's
  selection setter. *Alternative rejected:* ship only an overview button now — its
  target is ambiguous across N canvases and would force a focus concept into this
  leaf that overview should own. **No doc delta required** (no new view type — the
  picker is chrome within the existing Canvas view body, D-view-registry-3 intact).

- **D-look_through-6 — Selecting a shot bypasses (never clobbers) `Presenter::camera`;
  free-nav is inert on a shot-locked canvas.** The resolve uses the shot's affine
  while a shot is selected, leaving `Presenter::camera` at its last free value; nav
  gestures (pan/zoom/Space) still write `Presenter::camera` but it is not submitted,
  so the framing does not move; clearing the selection restores that free framing.
  *Rationale:* D9 draws the line — free-nav is the *viewport camera's* (transient),
  re-framing a *saved shot* is a scene edit owned by `editor.cameras.manip` (done
  from a Viewport canvas where the shot's frame is drawn and draggable). Preserving
  `Presenter::camera` gives a clean, testable Viewport↔shot toggle, and requires
  **no change to `editor.canvas.nav`**. *Alternative rejected:* have nav gestures
  re-frame the shot on a look-through canvas — turns casual panning into an undoable
  scene edit, violating D9 and duplicating `cameras.manip`. **No doc delta
  required.**

- **D-look_through-7 — Live preview via per-frame `pin()` re-derivation, with a
  fail-safe fallback for a vanished shot.** Each `draw_content` re-reads
  `scene::cameras(doc)` (lock-free `pin()`), re-derives the shot's viewport, and
  re-submits it; a selection whose `ObjectId` is gone falls back to the free
  viewport. *Rationale:* the per-frame `pin()` read is cheap and always current, so
  a `cameras.manip` frame edit (serialized through `apply_edit`/`doc_mu`) reflects
  next frame with **no invalidation bookkeeping and no new locking** (A4: `pin()` is
  the reader concurrent with the writer and the render thread's live read,
  `edit_render_sync.md`); the fallback makes a GC'd shot fail safe (D19 GC can
  reclaim an unreferenced camera). *Alternative rejected:* cache the derived viewport
  and invalidate on edit — extra cross-thread state for a read the pin already
  serves correctly every frame. **No doc delta required.**

## Open questions

(none — all decided.) The one empirical property — the exact `arbc::Affine`
composition for the inverse — is pinned by the round-trip Catch2 test
(D-look_through-4 / Acceptance), not deferred. No human-judgment item surfaces for
`tasks/parking-lot.md`; no new WBS leaf is spawned. The only cross-leaf boundary —
how the Overview's per-camera "look through" button chooses which of N canvases to
retarget (a focused-canvas notion) — is `editor.panels.overview`'s to resolve when
that leaf is refined (D-look_through-5), not this leaf's and not a parking-lot item.

## Status

**Done** — 2026-07-19.

- Added `interact::viewport_camera_for_shot` (inverse of `new_shot_from_view`, frame+resolution → comp→device affine) and `interact::look_through` pane-fit wrapper to `src/interact/ace/interact/interact.hpp` and `src/interact/interact.cpp`.
- Added `std::optional<arbc::ObjectId> active_camera` per-canvas session field to `app::CanvasView::Presenter` (`src/app/ace/app/canvas_view.hpp`), resolved per-frame via lock-free `scene::cameras(pin())` re-derivation in `src/app/canvas_view.cpp`.
- Wired an in-canvas camera picker widget (Viewport / shot list from `scene::cameras`) in `src/views/ace/views/views.hpp` and `src/views/views.cpp`.
- Applied aspect-fit letterbox present for look-through panes; GC-fallback to free viewport when a selected shot's `ObjectId` is no longer in `scene::cameras`.
- Registered `CMakeLists.txt` entries for new test targets; all existing seams (`request_camera`, `request_resize`) consumed unchanged.
- **L1 Catch2 units** (`tests/look_through_test.cpp`): round-trip inverse, output scaling, pane-fit letterbox (wider/taller/equal), degenerate guards.
- **Golden** (`tests/goldens/look_through_shot_64x64.rgba8`, 16 384 bytes): `render_document_srgb8` through the derived look-through camera byte-matches the stored crop, pinning "look-through framing == export framing."
- **ImGui Test Engine e2e** (`tests/look_through_e2e_test.cpp`): shot-pick / live-update / Viewport-toggle / GC-fallback + deterministic inline-`CanvasHost` per-canvas pixel-difference check.
- **ASan/TSan threading case** appended to `tests/canvas_host_test.cpp`: streamed frame edits + per-frame `pin()` re-derive under real worker pool — data-race-clean.
