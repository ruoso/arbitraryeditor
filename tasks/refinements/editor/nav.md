# editor.canvas.nav — Pan / zoom the viewport camera; scale bar; deep zoom

## TaskJuggler entry

`tasks/00-editor.tji:175-179` — `task nav` under `editor.canvas`. Effort `2d`,
`allocate team`, `depends !view` (i.e. `editor.canvas.view`). The `note` (`:179`)
cites **Design D2**, and frames the leaf as: "Pan/zoom drives the active viewport
camera (transient); a scale bar in composition units (no phantom %); deep-zoom
rebasing surfaced from HostViewport." (The `.tji` note currently back-links to the
flat `tasks/refinements/camera_nav.md`; the real landing path is
`tasks/refinements/editor/nav.md`, matching the `editor/` refinement set — the
closer fixes the note back-link per the ritual in
`tasks/refinements/README.md:57-68`, exactly as `multi_canvas.md` / `frame_sync.md`
/ `canvas_view.md` did.)

Downstream dependent: **`editor.canvas.tool_dispatch`** (`tasks/00-editor.tji:181-185`,
`depends editor.dock.tool_rail, editor.cells.gizmo, editor.canvas.nav` at `:184`) —
"Route `dockmodel::ToolSelection` active tool into the canvas interaction handler so
a canvas pointer gesture dispatches per active tool (Select/Brush/Eyedropper/Pan)".
That leaf routes the **left-button Pan tool** (D20) into the very camera-nav math
this leaf builds; this leaf builds the always-on navigation mechanism (the interact
math + the render-thread camera channel + the scale bar + the deep-zoom
pass-through) it will reuse. `editor.cameras.model` (`:191-195`, `depends
editor.canvas.view`) also consumes this leaf's transient camera — "new shot from
view" snapshots the live viewport camera into a persisted scene object.

## Effort estimate

**2 days** (from the `.tji`). The three canvas leaves before this one
(`canvas_view` → `frame_sync` → `multi_canvas`) built the render stack that frames
the document through a **hard-coded `arbc::Affine::identity()` camera**
(`src/render/canvas_renderer.cpp:69`) and plumbed exactly **one** UI→render state
channel — pixel resize (`request_resize`). This leaf adds the *second* channel — the
camera — and the input + math + chrome around it. The cost is in four places:
(1) the **interact pan/zoom/scale-bar math** (L1, headless, the bulk of the
coverage) — given a camera `Affine`, a device-pixel drag, a wheel-about-cursor
factor, and the pane size, produce the new camera, plus the composition-units
scale-bar computation; (2) the **render-thread camera channel** — a per-entry
`request_camera(id, Affine)` on `CanvasHost`/`CanvasDriver` applied via
`HostViewport::set_camera` before `step()`, analogous to `request_resize`, with
`CanvasRenderer` holding the current camera so a resize rebuild preserves framing;
(3) the **canvas pointer-input handler** — the Canvas pane is a bare `ImGui::Image`
today with **no** hover/drag/wheel handling, so this leaf adds the always-on
navigation gestures (wheel-zoom, Space-drag pan per D9) and feeds them through the
interact math; and (4) the **scale-bar overlay** (views, ImGui draw list) plus
surfacing the deep-zoom **anchor depth** for observability. It deliberately does
**not** persist the camera (transient session state, D15 — never a `transact`), add
the modal Pan *tool* routing (that is `tool_dispatch`), or implement the richer
navigation aids fit-to-cell / zoom-to-selection (D2 marks them *(open)*; see Open
questions).

## Inherited dependencies

**Settled (from `editor.canvas.view`, `tasks/00-editor.tji:154-158`, Done
2026-07-18).** The per-viewport render bundle is `render::CanvasRenderer` —
`src/render/ace/render/canvas_renderer.hpp` (ctors `:49`/`:55`, `resize(w,h)` `:65`,
`step()` `:74`, `image()` `:78`, `frames_issued()` `:83`, `width()`/`height()`
`:87`/`:88`, `borrowed_pool()` `:93`) + `src/render/canvas_renderer.cpp` — where the
camera is **hard-coded to identity**: `Impl::rebuild()` (`:40`) builds
`arbc::HostViewport::Config config; config.viewport = arbc::Viewport{width, height,
arbc::Affine::identity()};` (`:68-69`), constructs the `arbc::HostViewport` (`:79-81`),
and `step()` (`:144`) drives `viewport->step()` (`:148`). D-canvas_view-5 established
that the canvas frames the document's root composition with a **default identity/fit
camera** and **explicitly deferred interactive pan/zoom + scale bar + deep-zoom to
this leaf**. The byte-exact offline reference is `render::render_document_srgb8`
(`src/render/render.cpp:21-40`), which today also frames with
`arbc::Viewport{w, h, arbc::Affine::identity()}` (`:23`).

**Settled (from `editor.canvas.frame_sync`, `tasks/00-editor.tji:161-166`, Done
2026-07-18).** The off-UI-thread driver is `render::CanvasDriver` —
`src/render/ace/render/canvas_driver.hpp` (`request_resize(int,int)` `:53`, `poke()`
`:58`, `stop()` `:63`, `run(...)` `:69`, `drive_once()` `:76`, `consume(seq&, Srgb8Image&)`
`:84`, `published_sequence()` `:89`) + `src/render/canvas_driver.cpp`. Its **only**
UI→render per-frame state today is `pending_width_`/`pending_height_` guarded by
`resize_pending_` under the lock (`:103-105`) — the exact submit-channel shape this
leaf clones for the camera. D-frame_sync-2 keeps the writer (and all transient
session state) on the UI thread; the render thread is a pure `pin()` reader that
never reads the journal.

**Settled (from `editor.canvas.multi_canvas`, `tasks/00-editor.tji:168-172`, Done
2026-07-18).** The shipped render path is `render::CanvasHost` —
`src/render/ace/render/canvas_host.hpp` (`add(id, Document&)` `:66`, `remove(id)`
`:70`, `request_resize(id, w, h)` `:74` — again the **only** per-entry state channel,
`poke()` `:78`, `stop()` `:82`, `run(...)` `:87`, `drive_once()` `:94`, `consume(id,
seq, out)` `:99`, `published_sequence(id)` `:103`, `worker_pool()` `:112`,
`entry_pool(id)` `:116`) + `src/render/canvas_host.cpp` — one shared `arbc::WorkerPool`
+ a `canvas#N` → `CanvasRenderer`/`CanvasDriver` entry map + one bounded round-robin
drive loop. The app-layer presenter is `app::CanvasView` — `src/app/ace/app/canvas_view.hpp`
(`draw_content(view_id, pane_width, pane_height)` `:53`, `poke()` `:57`, `reconcile(...)`
`:62`, per-pane `struct Presenter` `:76` with `requested_width/height` `:80-81`,
`texture` `:82`) + `src/app/canvas_view.cpp` (`draw_content` `:23`: lazily
adds the host entry `:30-34`, posts a resize **request** only on a genuine size
change `:39-43`, consumes + uploads `:48-62`, blits via `views::draw_canvas_image`
`:67`). **The Presenter carries no camera or interaction state** — this is where the
transient per-canvas camera lands. D19/D-multi_canvas: a canvas *is only a camera*
and carries no selection/panel state.

**Settled (from `editor.dock.tool_rail`, Done 2026-07-17).** The modal-tool set
(D20) is exactly **Select · Brush · Eyedropper · Pan** in L1 `dockmodel`
(`src/dockmodel/ace/dockmodel/tool_rail.hpp`), with **no canvas behavior wired** —
Pan-on-the-rail and Space-as-its-transient-shortcut (D9) are observable state only,
their routing deferred to `editor.canvas.tool_dispatch`. This leaf therefore owns the
Space/wheel *always-on* navigation gestures; the left-button *Pan tool* is
`tool_dispatch`'s job (which reuses this leaf's interact math).

**Settled (from `editor.project.undo`, Done 2026-07-18).** Undo Constraint 5 /
D15: "Viewport-camera navigation (pan/zoom) is transient session state and must
**never** be a `transact` — it stays off the journal, like `Selection`." The undo
model **assumes** nav stays off the journal (guarded by the transient-state-inert
unit test at `tests/commands_test.cpp:137-140`). This leaf must honor that: the
camera is never persisted, never a transaction.

**Settled (existing seam).** The lone editor-side composition-unit helper is in L1
`interact` — `src/interact/ace/interact/interact.hpp:14` `double brush_units(double
view_fraction, double view_short_edge_units)` (impl `src/interact/interact.cpp:8`,
test `tests/interact_test.cpp:6`); the header comment (`:9-13`) explicitly frames it
as "the active camera's view … in composition units". This is the natural home for
the pan/zoom and scale-bar math.

**Pending (owned here).**
- The `interact` pan / zoom-about-cursor / scale-bar math (L1, `arbc::Affine` in/out).
- The per-entry `request_camera(id, Affine)` channel on `CanvasHost`/`CanvasDriver`,
  applied via `HostViewport::set_camera`; `CanvasRenderer` holding the current camera
  so a resize preserves framing.
- The canvas pointer-input handler (wheel-zoom, Space-drag pan) at the ImGui layer.
- The scale-bar overlay (views) + surfaced deep-zoom `anchor_depth` observability.

## What this task is

**D2** (`docs/00-design.md:463`) is normative: cameras are the only observer
primitive; "the **editing viewport is itself a camera** — the active one" with two
roles, "the **viewport camera** (active, free-nav, transient framing)" and shot
cameras. Section 3, "The canvas" (`docs/00-design.md:88-100`), is the concrete
promise this leaf realizes: an "**infinite, pannable, deep-zoomable** surface
rendering the active camera's view via `InteractiveRenderer` + `HostViewport`";
"**Progressive refinement is a feature, not a glitch:** fast pan/zoom shows a coarse
scale-rung first, then sharpens. The UI must *not* fight this"; "**Zoom is shown as a
scale bar** (composition units per screen pixel), never as a '%' against a nonexistent
native grid." D9 (`docs/00-design.md:470`): "Space pans the active viewport camera"
(transient). D15 (`:476`): the viewport camera's live framing is transient, **not**
undoable.

Concretely, the camera is an `arbc::Affine` (composition → device pixels;
`build/*/_deps/arbc-src/src/base/arbc/base/transform.hpp:13`). The seam that mutates
it is `arbc::HostViewport::set_camera(const Affine&)`
(`build/*/_deps/arbc-src/src/runtime/arbc/runtime/host_viewport.hpp:197`) — flagged
in both `canvas_view.md` and `frame_sync.md` as "the seam `nav` drives". This leaf
threads a real, mutable camera through the render stack and gives the user the
gestures to drive it:

- **Pan / zoom math lives in L1 `interact`** (headless, unit-tested): `pan(camera,
  device_dx, device_dy)` translates the camera by the drag expressed in composition
  units; `zoom(camera, device_focus, factor)` scales about the cursor so the
  composition point under the cursor stays fixed; `scale_bar(camera, target_px)`
  computes the composition-units length of a screen span with nice-number (1/2/5·10ⁿ)
  rounding. All take/return `arbc::Affine` — legal at L1 (`interact` is in the
  arbc-allowed set; `interact` → `scene` → `project` → `arbc::arbc`).
- **The camera reaches the render thread through a new per-entry channel.** The
  `HostViewport` (and its `TileCache`) is render-thread-confined (A4), so
  `set_camera` must be called on the render thread. `CanvasHost::request_camera(id,
  Affine)` / `CanvasDriver::request_camera(Affine)` stashes a pending `Affine` under
  the existing lock (beside the pending resize) and applies it via
  `renderer.set_camera(...)` → `viewport->set_camera(...)` at the top of the next
  drive iteration. `CanvasRenderer` holds the current camera and `rebuild()` uses it
  in place of the hard-coded identity at `canvas_renderer.cpp:69`, so a resize
  preserves framing.
- **The pointer handler lives at the ImGui layer** (views/app — the only levels that
  may see ImGui). The Canvas pane is a bare `ImGui::Image` today (no
  hover/drag/wheel), so this leaf adds an `InvisibleButton`/hover region around it
  that yields the always-on navigation gestures: **mouse wheel = zoom about the
  cursor**, **Space-held drag = pan** (D9). `app::CanvasView::draw_content` reads
  those gestures, updates its per-`canvas#N` transient camera through the `interact`
  math, and submits the result via `host_.request_camera(view_id, camera)` — exactly
  the shape of the existing resize submit at `canvas_view.cpp:39-43`.
- **Deep zoom is the library's, surfaced not re-implemented.** `HostViewport::step()`
  rebases internally when the camera leaves the well-conditioned band (comment
  `host_viewport.hpp:190-196`), surfacing `StepOutcome{RebaseNeed need, Reanchor
  reanchor}` (`:113`) and an `anchor_depth()` (`:236`, "zoom-in pushes, zoom-out
  pops"). The rebase re-anchors internally while **preserving the visible
  composition→device mapping**, so the editor's transient `Affine` stays
  authoritative and no UI↔render read-back loop is needed; this leaf surfaces
  `anchor_depth(id)` through `CanvasHost` purely as an observability signal.
- **The scale bar** (views, ImGui draw list overlay) renders the `scale_bar` result —
  a bar N device pixels wide labelled with its composition-unit length — never a "%".

## Why it needs to be done

The canvas today can *display* the document but is a fixed window onto it: the camera
is nailed to identity (`canvas_renderer.cpp:69`), the only UI→render channel is pixel
resize, and the pane has no pointer interaction at all. D2's canvas — "infinite,
pannable, deep-zoomable" — does not exist yet. Every downstream interaction leaf
needs the camera to be *live*: `editor.canvas.tool_dispatch` routes the Pan tool into
this leaf's camera-nav math; `editor.cameras.model`'s "new shot from view" snapshots
this leaf's transient viewport camera into a persisted shot; `editor.cameras.manip`
and the overview navigator (§5) all assume a mutable viewport camera. This is also
where the "no phantom %" scale-bar promise (the reframing in `docs/00-design.md:72-73`
— "zoom is the editing camera's scale, there is no phantom [100%]") becomes real UI.

## Inputs / context

**Governing design docs (normative — the constitution).**
- **D2** `docs/00-design.md:463` — the canvas model; the editing viewport *is* the
  active camera; viewport camera = free-nav transient framing. Nearby: the "no
  phantom %" reframing `:72-73`; the canvas prose (pannable/deep-zoomable/progressive
  refinement/scale-bar-in-composition-units) `:88-100`.
- **D9** `docs/00-design.md:470` — **Space pans the active viewport camera** (transient).
- **D15** `docs/00-design.md:476` (restated `:370`) — transient-vs-scene: the viewport
  camera's live framing (pan/zoom) is transient session state, **NOT undoable**; a
  saved shot's framing is scene data and IS. The line this leaf must not cross.
- **A4** `docs/01-architecture.md:59-82` — the concurrency contract: single-writer /
  render-thread-confined cache, leaf-only dispatch, UI thread submits, never touches
  the cache. The camera mutation is therefore a *submit*, applied on the render thread.
- **§8** `docs/01-architecture.md:144-179` — the levelization DAG: `interact` is
  **L1** ("hit-test · gizmo · snapping · brush math", UI-agnostic, **no** ImGui/GL,
  `:169`); `render` is **L2** ("HostViewport/InteractiveRenderer glue · frame-sync ·
  tile→GL", `:172`); `views`/`dock` are **L3** (the only layers that see ImGui);
  `app` is L4. Enforcement: L1 holds no ImGui/GL/SDL include (`:177-179`,
  `scripts/check_levels.py`).
- **§9 / §9.1** `docs/01-architecture.md:181-245` — the universal DoD and the
  offscreen software-GL ASan/TSan lane.

**libarbc API surface** (fetched under `build/*/_deps/arbc-src/`).
- `arbc::HostViewport` — `src/runtime/arbc/runtime/host_viewport.hpp`: `const Affine&
  camera() const` (`:188`), **`void set_camera(const Affine&) noexcept`** (`:197`) —
  the mutation seam; `struct StepOutcome{ bool schedule_follow_up; Reanchor reanchor;
  RebaseNeed need; }` (`:113`); `StepOutcome step()` (`:226`); the deep-zoom
  observability `frames_issued()` (`:229`), `reanchor_events()` (`:231`),
  `std::size_t anchor_depth()` (`:236`).
- `arbc::Affine` — `src/base/arbc/base/transform.hpp:13`: fields `a,b,c,d,tx,ty`
  (`:14-19`), `identity()` (`:21`), `translation(dx,dy)` (`:22`), `scaling(sx,sy)`
  (`:23`), `apply(Vec2)` (`:25`), `inverse()` (`:30`), **`double max_scale()`
  (`:35`)** — "the larger singular value of the linear part" (the scale-bar magnitude
  / deep-zoom scale), `map_rect(Rect)` (`:38`); free `compose(outer, inner)` (`:44`).
- `arbc::Viewport` — `src/compositor/arbc/compositor/compositor.hpp:16`: `int width`
  (`:17`), `int height` (`:18`), `Affine camera` (`:19`), `ObjectId anchor` (`:30`).
- Deep-zoom rebase types — `src/compositor/arbc/compositor/anchored_viewports.hpp`:
  `enum class RebaseNeed { none, zoom_in, zoom_out }` (`:62`), `struct Reanchor`
  (`:72`) — the rebase is applied *inside* `HostViewport::step()`; the editor observes,
  it does not drive it.

**Editor seams this leaf extends.**
- `render::CanvasRenderer` — hold a current `arbc::Affine camera_` (default identity),
  add `set_camera(const Affine&)`, and use it at `src/render/canvas_renderer.cpp:69`
  in place of the hard-coded identity (+ re-apply on `rebuild()`).
- `render::CanvasDriver` / `render::CanvasHost` — add `request_camera(...)` beside
  `request_resize` (`canvas_driver.hpp:53`, `canvas_host.hpp:74`), stashing a pending
  `Affine` under the existing lock and applying it on the render thread; surface
  `anchor_depth(id)`.
- `app::CanvasView::Presenter` — add the transient `arbc::Affine camera_` (per
  `canvas#N`) + drag/space gesture bookkeeping; read gestures and submit in
  `draw_content` (`src/app/canvas_view.cpp:23`).
- `views` — a scale-bar overlay draw helper beside `draw_canvas_image`
  (`src/views/ace/views/views.hpp:40`, `src/views/views.cpp:42`); the pane's
  pointer-gesture read (`InvisibleButton`/wheel) at the ImGui layer.
- `render::render_document_srgb8` — extend to accept an `arbc::Affine camera`
  (default `identity()`) at `src/render/render.cpp:23`, so the interactive-camera
  golden cross-checks byte-identically.
- `app::shell.cpp:217-225` — the Canvas body lambda already forwards the pane size to
  `draw_content`; no structural change, the gesture read is inside `draw_content`.

**Predecessor refinements** (style + decision continuity):
`tasks/refinements/editor/canvas_view.md` (D-canvas_view-2 inline-executor golden
determinism, the identity-camera default, the deferral *to here*),
`tasks/refinements/editor/frame_sync.md` (the `request_resize` submit-channel shape
cloned for the camera; the ASan/TSan target),
`tasks/refinements/editor/multi_canvas.md` (the `CanvasHost` per-entry API, the
per-`canvas#N` presenter), `tasks/refinements/editor/undo.md` (the transient-vs-scene
invariant this leaf must honor), `tasks/refinements/editor/tool_rail.md` (the Pan
tool / Space shortcut the downstream `tool_dispatch` routes into this leaf's math).

## Constraints / requirements

1. **The camera is transient session state — never a `transact` (D15 / undo
   Constraint 5).** Pan/zoom stays entirely off `Document`/the journal: the
   per-`canvas#N` `Affine` lives in the app-layer `Presenter` (like `Selection`, a
   project/UI-side value), submitted to the render thread, never persisted to
   `project.arbc`, never routed through `dispatch`/`transact`. A "new shot from view"
   (the persisted case) is `editor.cameras.model`'s job — this leaf only exposes the
   current transient camera for it to snapshot.

2. **The camera reaches the render thread as a submit, applied on that thread
   (A4).** The UI thread never calls `set_camera` or touches the `HostViewport`/cache
   directly. `request_camera(id, Affine)` stashes a pending `Affine` under the same
   short lock as the pending resize; the render thread applies it (`set_camera`)
   before `step()`. No new lock, no new thread — the existing single shared render
   thread and double-buffer are reused verbatim.

3. **Resize preserves framing.** `CanvasRenderer::rebuild()` (triggered by
   `request_resize`) must construct the `HostViewport` with the *current* camera, not
   identity — so a pane resize does not silently reset the user's pan/zoom. The camera
   channel and resize channel commute.

4. **Zoom is about the cursor.** `zoom(camera, device_focus, factor)` must keep the
   composition point under the cursor fixed: `new_camera.apply⁻¹(device_focus) ==
   old_camera.apply⁻¹(device_focus)` (the crisp, testable invariant). Wheel-zoom and,
   later, tool-driven zoom both use it.

5. **Do not fight progressive refinement or the library's rebasing (D2 §3).** Deep
   zoom is `HostViewport::step()`'s internal re-anchoring; the editor keeps its
   double-precision `Affine` authoritative and submits it — it does **not** read the
   rebased `camera()` back into the UI each frame (a cross-thread feedback loop and a
   threading hazard, and unnecessary since the rebase preserves the visible mapping).
   `anchor_depth(id)` is surfaced read-only for observability. The UI shows at most a
   subtle "refining…" affordance; it never blocks on settle.

6. **The scale bar is composition units, never a "%".** It reads
   `Affine::max_scale()` for composition-units-per-device-pixel (`1 / max_scale`) and
   renders a nice-number bar. No percentage, no phantom native-grid reference (D2 §3,
   `:72-73`).

7. **Interaction math is UI-agnostic and headless (§8).** All pan/zoom/scale-bar
   computation is L1 `interact` (`arbc::Affine` in/out); the pointer-gesture *read*
   (ImGui wheel/drag/Space) is L3 `views` / L4 `app`; the camera *submit* is L2
   `render`. No math in the ImGui layer.

8. **Levelization stays clean (§8).** No new component and no new DAG edge: the math
   extends existing L1 `interact`, the channel extends existing L2 `render`, the
   overlay/handler extend existing L3 `views` / L4 `app`. `interact` gains no
   ImGui/GL/SDL include (it already transitively sees `arbc` via `scene`→`project`).
   `check_levels` stays clean.

## Acceptance criteria

Instantiating the universal DoD (`docs/01-architecture.md:199-203`) for this leaf;
`scripts/gate` green (check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean).** Primary structural assertion. The interact
  math is added to existing L1 `interact` (still no ImGui/GL/SDL include); the camera
  channel to existing L2 `render`; the overlay + pointer read to existing L3 `views` /
  L4 `app`. **No new component, no new DAG edge.**

- **Catch2 unit (L1 `interact` math — the bulk, headless, GL-free).** A new
  `tests/nav_test.cpp` (joined to `ace_tests`, mirroring `tests/interact_test.cpp`),
  asserting: (a) **pan** — a device-pixel drag translates the camera by that drag in
  composition units (drag right by `d` px at scale `s` moves the framing by `d/s`
  composition units), verified via `apply`/`inverse`; (b) **zoom-about-cursor** — the
  composition point under the focus pixel is invariant across the zoom
  (`new.inverse().apply(focus) == old.inverse().apply(focus)` within a tight epsilon)
  and the linear-part magnitude scales by the wheel factor; (c) **scale bar** —
  composition-units-per-pixel `== 1 / camera.max_scale()`; the chosen bar length is a
  1/2/5·10ⁿ nice number, its device width lands in the target band, and its label
  matches its composition-unit length; (d) pan∘zoom composes correctly (zoom then pan
  keeps the focus invariant only for the zoom step, pan is additive). Coverage ≥ 90%
  on changed lines (`diff-cover --fail-under=90`, `coverage` preset).

- **Catch2 unit (L2 `render` camera channel — headless, inline executor).** Extend
  `tests/canvas_host_test.cpp` (and/or `tests/canvas_driver_test.cpp`), inline
  `WorkerPoolConfig{}` for determinism, asserting: (e) `request_camera(id, A)` applied
  on drive changes the published frame for a non-identity `A` (the camera reaches
  `HostViewport::set_camera`); (f) a `request_resize` after a `request_camera`
  **preserves** the camera (rebuild re-applies it, Constraint 3); (g) `request_camera`
  is per-entry (one canvas's camera does not disturb another's); (h) `anchor_depth(id)`
  is surfaced and non-zero after a deep zoom-in, back to 0 after zoom-out (deep-zoom
  rebasing observably engaged).

- **Golden (rendered output, byte-exact — the interactive camera composites).** A new
  `tests/goldens/canvas_nav_zoom_64x64.rgba8`: render the document through a **known
  non-identity camera** (e.g. a 2× zoom + translation `Affine`) on a hosted entry,
  inline config + settle-fully drive, **byte-compared** via `ace_test::compare_golden`
  (`tests/golden_support.hpp:36`) with **no tolerance**. **Cross-check:**
  byte-identical to `render::render_document_srgb8` invoked with the *same* `Affine`
  (extended offline signature) — proving the camera threads to the composite exactly,
  not just approximately.

- **UI e2e — ImGui Test Engine (canvas navigates; scale bar tracks).** A new
  `tests/canvas_nav_e2e_test.cpp` (joined to `ace_shell_test`, offscreen software-GL,
  modeled on `tests/canvas_view_e2e_test.cpp` / `tests/multi_canvas_e2e_test.cpp`),
  driving the stable **`canvas#1`** view id: (i) a simulated **wheel** over the pane
  advances the published frame **and** raises `anchor_depth(canvas#1)` (zoom
  engaged); (ii) a simulated **Space-held drag** pans (frame advances; the interact
  math is exercised end-to-end); (iii) the **scale-bar** overlay renders and its
  labelled length changes after a zoom — captured as a **Test Engine screenshot
  baseline** (not a byte-exact golden — ImGui chrome + software-GL pixels are flaky by
  construction, the tool_rail precedent). Drive by widget/view id, assert the resulting
  state.

- **Threading (ASan/TSan).** The camera adds a second UI→render submit channel; the
  shared-pool lifecycle test from `multi_canvas` is extended to include
  `request_camera` in the drive loop, so add→resize→**camera-submit**→render→teardown
  is clean under the `asan` (offscreen SDL + `llvmpipe`, §9.1) and `tsan` presets,
  with a clean stop→join and no torn camera read/write (the pending-`Affine` slot is
  lock-guarded exactly like the pending resize). Residual Mesa-driver leaks stay
  covered by the existing scoped `tests/lsan.supp`.

- **Format + build clean** across the standard presets; `scripts/gate` green.

**No new WBS leaf is deferred.** The left-button **Pan tool** routing is already the
independent leaf `editor.canvas.tool_dispatch` (which depends on this one and reuses
its interact math) — nothing here spawns a new task for it. The richer navigation
aids (fit-to-cell, zoom-to-selection) are **design-open** (D2 §3 marks them *(open)*)
and go to the parking lot, not the WBS (see Open questions) — a minimal **reset-to-fit
(fit document to pane)** recovery affordance ships in this leaf.

## Decisions

- **D-nav-1 — the viewport camera is transient app-layer state, never a `transact`.**
  The per-`canvas#N` `arbc::Affine` lives in `app::CanvasView::Presenter` and is
  submitted to the render thread; it is never written to `Document`/the journal.
  *Rationale:* D15 and undo Constraint 5 legislate exactly this — pan/zoom is
  transient like scroll position; making it a transaction would pollute undo and
  violate the invariant the undo model already assumes
  (`tests/commands_test.cpp:137-140`). A canvas "is only a camera" (D19), so per-pane
  camera state is legitimate UI state. *Alternative rejected:* storing the viewport
  camera in the scene as the active-camera object now — that is `editor.cameras.model`
  (persisted shot cameras / "new shot from view"); the *live* viewport framing is
  explicitly the transient half of D15. **No doc delta required.**

- **D-nav-2 — pan/zoom/scale-bar math lives in L1 `interact`, `arbc::Affine` in/out.**
  *Rationale:* it is pure, UI-agnostic geometry — exactly `interact`'s charter
  ("hit-test · gizmo · snapping · brush math", §8) — so it is unit-tested headless and
  becomes the bulk of the leaf's coverage; `interact` already owns the one
  composition-unit helper (`brush_units`) and transitively sees `arbc` (legal at L1).
  *Alternative rejected:* computing the camera in `app`/`views` next to the ImGui input
  — buries the testable geometry in the ImGui layer, un-unit-testable and off-charter.
  **No doc delta required.**

- **D-nav-3 — the camera is a per-entry render-thread submit channel, cloned from
  `request_resize`; `CanvasRenderer` holds the current camera.** `CanvasHost`/
  `CanvasDriver` gain `request_camera(id, Affine)` stashing a pending `Affine` under
  the existing lock; the render thread applies it via `set_camera` before `step()`;
  `CanvasRenderer::rebuild()` uses the held camera at `canvas_renderer.cpp:69` instead
  of identity. *Rationale:* `HostViewport`/its cache are render-thread-confined (A4),
  so `set_camera` must run there; the resize channel is the proven, TSan-clean shape
  for a UI→render state hand-off, and holding the camera in the renderer makes resize
  and camera commute (Constraint 3). *Alternative rejected:* mutating the
  `HostViewport` from the UI thread — a direct A4 violation (UI thread touching the
  render-confined viewport/cache). *Alternative rejected:* a separate camera lock /
  atomic — the pending-resize slot already demonstrates a lock-guarded pending value
  is TSan-clean and never blocks the UI; the camera rides the same lock for free.
  **No doc delta required.**

- **D-nav-4 — the always-on navigation gestures are wheel-zoom + Space-drag pan,
  independent of the active modal tool; the left-button Pan tool is `tool_dispatch`.**
  *Rationale:* D9 states Space pans the viewport camera (transient), and "deep-zoomable"
  (D2 §3) implies wheel-zoom as the universal complement; these must work regardless of
  which modal tool is active (you navigate while brushing). `editor.canvas.tool_dispatch`
  — which *depends on this leaf* — routes the left-button **Pan tool** (D20) into the
  same `interact` math this leaf builds. *Alternative rejected:* gating all pan/zoom
  behind `tool_dispatch`'s active-tool routing — inverts the dependency (tool_dispatch
  depends on nav, not vice-versa) and would make basic navigation modal, contra D9's
  always-available Space-pan. **No doc delta required** — wheel-zoom + Space-pan are
  refinement-level realizations of D2 §3 / D9, not new UX seams.

- **D-nav-5 — deep zoom is the library's internal rebasing, surfaced not driven; the
  editor's `Affine` stays authoritative, no read-back loop.** `HostViewport::step()`
  re-anchors when the camera leaves the well-conditioned band, preserving the visible
  mapping; the editor keeps submitting its double-precision `Affine` and surfaces
  `anchor_depth(id)` read-only. *Rationale:* the rebase changes only the internal
  anchor, not what the user sees, so the transient `Affine` remains the single source
  of truth for input; reading the rebased `camera()` back into the UI each frame would
  create a cross-thread camera feedback loop (a threading hazard) for no visible gain.
  *Alternative rejected:* the UI reads `HostViewport::camera()` back each frame and
  re-bases its own transient camera — a UI↔render feedback loop across the double-buffer
  boundary, racy and unnecessary. **No doc delta required** — D2 §3's "the UI must not
  fight [progressive refinement/rebasing]" already legislates this posture.

- **D-nav-6 — the scale bar is composition-units-per-pixel from `Affine::max_scale()`
  with 1/2/5·10ⁿ nice-number rounding; drawn as an ImGui overlay in `views`; screenshot
  baseline, not a byte-golden.** *Rationale:* D2 §3 mandates "composition units per
  screen pixel, never … '%'"; `max_scale()` is precisely "the resolution a layer must
  render at" (transform.hpp:32-35), i.e. device-px-per-composition-unit; the nice-number
  math is L1 `interact` (testable), only the bar/label draw is ImGui (L3). Per the
  `tool_rail` precedent, ImGui chrome gets a Test Engine screenshot baseline, not a
  byte-exact `render_offline` golden (it composes no `Document`). *Alternative rejected:*
  a "%" zoom readout — the exact phantom-native-grid metric D2 §1/§3 abolishes.
  **No doc delta required.**

- **D-nav-7 — a minimal reset-to-fit ships; fit-to-cell / zoom-to-selection do not.**
  A "fit the document to the pane" recovery affordance (a key/menu action computing an
  `Affine` that frames the document bounds in the pane) ships in this leaf as the
  essential "don't get lost in unbounded space" escape. The richer, selection/cell-aware
  aids (fit-to-cell, zoom-to-selection) are **D2 §3-*(open)*** and depend on
  selection/cell state; they are not minted as a WBS task here. *Rationale:* reset-to-fit
  needs no selection dependency and is the minimum orientation guarantee; the other aids'
  *set and invocation* are an unsettled UX judgment (which aids, which gestures), and
  spatial orientation is otherwise served by the overview navigator (§5,
  `editor.panels.overview`), so pinning them now would be premature. Per the refinement
  rules, an unsettled UX judgment goes to the parking lot, not the WBS (and never an
  "audit" task). *Alternative rejected:* minting `editor.canvas.nav_aids` now — the aid
  set is a design-open UX call, not settled implementable scope. **No doc delta
  required** (D2 §3 already marks the aids *(open)*).

## Open questions

(none blocking — all implementation decisions settled above.) One item is routed to
the parking lot rather than the WBS: **which deep-zoom navigation aids beyond
reset-to-fit** (D2 §3 lists fit-to-frame / fit-to-cell / zoom-to-selection as
*(open)*) and their exact gesture/menu bindings — a design-open UX judgment that
cannot be closed by an implementer, so it is surfaced for human review, not encoded as
a task (per D-nav-7).

## Status

**Done** — 2026-07-18.

- L1 `interact` pan/zoom/scale-bar/fit math (`src/interact/ace/interact/interact.hpp`, `src/interact/interact.cpp`); unit-tested in new `tests/nav_test.cpp` (pan, zoom-about-cursor, scale bar, fit-to-pane).
- Per-entry render-thread `request_camera(id, Affine)` channel cloned from `request_resize` on `CanvasHost`/`CanvasDriver` (`src/render/ace/render/canvas_host.hpp`, `src/render/canvas_host.cpp`, `src/render/ace/render/canvas_driver.hpp`, `src/render/canvas_driver.cpp`); `CanvasRenderer` holds the camera so resize preserves framing (`src/render/ace/render/canvas_renderer.hpp`, `src/render/canvas_renderer.cpp`); `anchor_depth` observability surfaced through `CanvasHost`.
- `render::render_document_srgb8` extended to accept an optional `arbc::Affine` camera (`src/render/ace/render/render.hpp`, `src/render/render.cpp`) enabling byte-exact interactive-vs-offline cross-check.
- Canvas pointer-input handler (wheel-zoom, Space-drag pan, reset-to-fit) and scale-bar overlay (composition-units, never %): `src/views/ace/views/views.hpp`, `src/views/views.cpp`, `src/app/ace/app/canvas_view.hpp`, `src/app/canvas_view.cpp`.
- `CMakeLists.txt` updated for new test targets.
- New tests: `tests/nav_test.cpp` (Catch2 L1 unit), `tests/canvas_nav_e2e_test.cpp` (ImGui Test Engine e2e: wheel-zoom, Space-drag pan, scale-bar tracks), `tests/goldens/canvas_nav_zoom_64x64.rgba8` (byte-exact golden — native-scale pan on bounded-raster document so hosted == offline cross-check holds).
- Extended: `tests/canvas_host_test.cpp` (camera channel, resize-preserves-camera, per-entry isolation, `anchor_depth` deep-zoom rise/fall, lifecycle under ASan/TSan), `tests/canvas_driver_test.cpp` (driver camera channel + `anchor_depth`).
- Tech-debt registered: `editor.canvas.fit_bounds` — wire reset-to-fit to root composition's authored bounds (the general `interact::fit(content, pane)` math ships here tested for that future wiring).
