# editor.canvas.view — Canvas view = HostViewport + InteractiveRenderer; tile → GL

## TaskJuggler entry

`tasks/00-editor.tji:154-158` — `task view` under `editor.canvas`. Effort `3d`,
`allocate team`, `depends editor.project.app_state`. The `note` (`:158`) cites
**Arch A4/A6** and names this refinement. (The `.tji` note currently back-links
to the flat `tasks/refinements/canvas_view.md`; the real landing path is
`tasks/refinements/editor/canvas_view.md`, matching the existing `editor/`
refinement set — the closer fixes the note back-link per the ritual in
`tasks/refinements/README.md:57-68`, exactly as `open.md`/`app_state.md` did.)

Downstream dependents: `editor.canvas.frame_sync` (`:160-164`, `depends !view` —
the off-UI-thread driver + double-buffer + edit submission), `editor.canvas.nav`
(`:172-176`, `depends !view` — pan/zoom the viewport camera), `editor.cameras.model`
(`:188-192`, `depends editor.canvas.view`), and `editor.cells.model` (`:216-220`,
`depends editor.canvas.view`). This leaf is the first to put the shared
`Document` on screen *interactively*; the whole cell/camera stack binds to it.

## Effort estimate

**3 days** (from the `.tji`). This leaf promotes the static, offline
`render_probe` stand-in into a live **interactive canvas**: it stands up the
per-viewport libarbc driver (`HostViewport` + `InteractiveRenderer` + `CpuBackend`
+ `SurfacePool` + `TileCache` + a persisted target `Surface`) over the
`AppState`-owned `Document`, pumps one frame per UI tick, converts the settled
target to sRGB8, and uploads it as a GL texture that replaces the probe pane as
the Canvas view body. The cost is in three places: (1) composing the driver
bundle correctly against the `Document&` constructor (lifetimes, the derived
damage-sink install, the working-space target) and honoring `step()`'s
first-frame-composites / still-scene-early-out contract; (2) pane-size handling
(reallocate the target + rebind the viewport when the canvas pane resizes) without
churning GL objects; and (3) the **first interactive golden** — proving the
interactive compositor path is byte-identical to the offline `render_offline`
golden `render_probe` shipped. It deliberately does **not** move rendering off the
UI thread, introduce the shared `WorkerPool`, double-buffer the frame, route edit
submission, drive camera pan/zoom, or handle N canvases — those are
`frame_sync` / `multi_canvas` / `nav`.

## Inherited dependencies

**Settled (from `editor.project.app_state`).** The process owns exactly one
`arbc::Document` for its lifetime, reachable as `commands::AppState::document()`
(`src/commands/ace/commands/app_state.hpp:45-46`), alongside its live
`HousekeepingThread` (A4, one per `Document`), a persistent `arbc::Registry`
(`:50`), the `ProjectLayout` (`:48`), and the project-level `Selection` (`:53`).
The app-layer bootstrap (`open_or_create_app_state`, `run_editor`) already holds
that `AppState` for the whole run (`src/app/shell.cpp`, the `run_editor`
loop) and defaults to a fresh scratch project so there is *always* a `Document`
to bind — `app_state.md` D-app_state-6. The dispatch seam
(`commands::dispatch`, `app_state.hpp:126`) applies an action as one libarbc
transaction, **synchronously on the UI thread** (D-app_state-5); this leaf reads
the document through the driver but does not yet submit edits off-thread.

**Settled (from `editor.foundation.render_probe`,
`tasks/refinements/foundation/render_probe.md`).** The **tile → GL seam already
exists and is designed for exactly this reuse**:
- `gl::upload_rgba8(const void* pixels, int w, int h) → texture` +
  `gl::destroy_texture(handle)` (`src/gl/ace/gl/gl.hpp:14-19`,
  `src/gl/gl.cpp:17-33`) — the header comment says verbatim *"This is A6's tile→GL
  step — editor.canvas.view reuses the identical primitive for settled tiles."*
- The **working-space → straight-alpha sRGB8** conversion tail: `render`'s
  `render_document_srgb8` (`src/render/ace/render/render.hpp:30`,
  `src/render/render.cpp:21-40`) allocates a `k_fast_rgba8srgb` surface and calls
  `CpuBackend::convert` (never hand-rolled — D10, the "invisible translator"),
  reading tightly-packed bytes via `Surface::span<PixelFormat::Rgba8Srgb>()`.
  This leaf reuses the identical convert-and-pack tail; only the *frame source*
  changes (`HostViewport::step()` over a persistent target vs. one-shot
  `render_offline`).
- The `Srgb8Image { int width, height; std::vector<uint8_t> pixels; }` value
  (`render.hpp:17-21`) — directly GL-uploadable RGBA8.
- The **golden harness**: `ace_test::compare_golden(name, bytes)`
  (`tests/golden_support.hpp:36`), raw sRGB8 bytes under `tests/goldens/`
  (`ACE_GOLDEN_DIR`), `.actual` dumped on mismatch. render_probe's own golden
  (`tests/goldens/render_probe_64x64.rgba8`) is the offline reference this leaf's
  interactive golden is cross-checked against.

**Settled (from `editor.dock.view_registry`,
`tasks/refinements/editor/view_registry.md`).** **Canvas** is a registered view
type — the *only* `multi_instance` one (`dockmodel::ViewType::Canvas`,
`src/dockmodel/ace/dockmodel/view_registry.hpp:19`; ids `canvas#N`). The draw seam
is `views::register_view_body(ViewType::Canvas, ViewBody)` +
`views::draw_view(view_id)` (`src/views/ace/views/views.hpp:38-50`), and the
dockspace already walks `DockLayout::view_ids()` each frame, wrapping each in
`ImGui::Begin(id,&open)` → `views::draw_view(id)` → `End()` and routing tab-✕
back to the registry (`src/dock/dock.cpp:386-398`). **Today the Canvas body is
the offline probe stand-in**: `run_editor` registers `ViewType::Canvas` →
`ProbeView::draw_content()` (`src/app/shell.cpp:216`), which draws
`views::draw_probe_image(texture, w, h)` — the Image into the current window, no
Begin/End (`views.hpp:30-33`). `ProbeView` (`src/app/ace/app/probe.hpp`,
`src/app/probe.cpp`) owns the app-layer GL-texture lifecycle (create-once /
draw-each-frame / destroy-before-shutdown). **This leaf replaces that stand-in**
with a driver-backed Canvas body, reusing the same registration seam and lifecycle
shape.

**Pending (this leaf owns them).** The per-viewport interactive driver bundle in
`render` (L2); the working-space target + settled-frame → sRGB8 pull; a
same-size in-place GL texture update alongside the existing full upload; the
app-layer Canvas view owner (successor to `ProbeView`) that steps + uploads each
frame and registers the Canvas body over `AppState::document()`; the pane-resize
rebind; the first interactive golden + the ImGui Test Engine e2e.

## What this task is

Turn the Canvas view from a static picture into a **live interactive render of
the shared document**. Per A5 a canvas view is *"one `HostViewport` +
`InteractiveRenderer` over the shared `Document`"*; this leaf builds exactly that
for **one** canvas. Concretely, across the §8 level homes render_probe already
established:

1. **`render` (L2) — the interactive driver bundle + the settled-frame pull.** A
   `render`-owned canvas driver (working name `CanvasRenderer`) composes the
   libarbc pieces the `host-interactive` example wires by hand: a `CpuBackend`
   (behind the A6 `Backend` seam), a `SurfacePool`, a `TileCache`, one persistent
   target `Surface` in the document's working space
   (`backend.make_surface(w, h, document.pin()->working_space())`), one
   `InteractiveRenderer`, and a `HostViewport` bound via the **`Document&`
   constructor** (`host_viewport.hpp:176`) against `AppState::document()`. It
   exposes `resize(w,h)`, `step()` (drive one `HostViewport::step()`, then
   convert the settled target to straight-alpha sRGB8 via `CpuBackend::convert`),
   and an accessor to the current `Srgb8Image` bytes. **GL-free** — so the whole
   compositor path is headless-unit- and golden-testable without a GL context.
2. **`gl` (L0) — same-size texture update.** Reuse `upload_rgba8`/`destroy_texture`
   as-is; add a `gl::update_rgba8(texture, pixels, w, h)` (`glTexSubImage2D`,
   same dimensions) so a canvas that re-renders (a later camera nudge, an edit)
   reuses its texture object instead of churning `glGenTextures`/`glDeleteTextures`
   every changed frame — reallocating only on a genuine pane resize.
3. **`views` (L3) — the Canvas body.** Draw the driver's current texture as an
   `ImGui::Image` into the current window (reusing `draw_probe_image`, or a thin
   `draw_canvas_image` alias with a stable id) — the dockspace owns Begin/End and
   the tab ✕.
4. **`app` (L4) — the Canvas view owner + wiring.** A `CanvasView` (successor to
   `ProbeView`) owns the `render::CanvasRenderer` and the GL texture, and each
   frame: sizes the driver to the Canvas pane, `step()`s it, and uploads the
   settled bytes (fresh `upload_rgba8` on first frame / resize, `update_rgba8`
   otherwise) — but only when `HostViewport::frames_issued()` advanced (a still
   scene re-draws the same texture, zero GL traffic). It registers the Canvas
   body via `register_view_body(ViewType::Canvas, ...)` capturing the one
   `AppState&`, replacing the probe lambda at `shell.cpp:216`, and clears it on
   exit (the seam is process-global).

Not in scope: the off-UI-thread driver + latest-frame double-buffer + edit
submission (`frame_sync`), the shared `WorkerPool` and N-canvas per-instance
drivers (`multi_canvas`), interactive camera pan/zoom + scale bar + deep-zoom
(`nav`), the camera model (`editor.cameras.model`), and routing the active tool
into pointer gestures (`tool_dispatch`).

## Why it needs to be done

`render_probe` proved the binding + display *thread* end-to-end but stopped at the
**offline** driver, explicitly deferring the interactive renderer, the tile cache,
and the frame handoff to `editor.canvas.view` (render_probe Constraint 2,
D-render_probe-2). `app_state` then established the one owned `Document` and
pointed here: *"`editor.canvas.view` depends directly on `app_state` precisely to
drive that owned `Document` through a `HostViewport`"* (`app_state.md:124-126`).
And `view_registry` registered Canvas as a real view type but wired its body to the
probe stand-in *"until `editor.canvas.view`"* ships the real one. Every leaf below
this — cameras (which *are* `Viewport`s the canvas looks through), cells (placed by
affine, seen through the canvas), selection/hit-test, brush — assumes a canvas that
shows the live document and re-renders when it changes. This leaf is where
"one shared Document" becomes "a picture that updates as you edit it," and where
A6's `Backend`-seam promise (CPU tiles now, GPU later with no editor change)
becomes a concrete display path rather than a one-shot probe.

## Inputs / context

**Design docs (normative — the constitution).**

- `docs/01-architecture.md` **A6 / §6** (`:99-105`, log row `:256`) — the exact
  display path this leaf implements: *"`CpuBackend` yields CPU tile surfaces; the
  canvas view **uploads them as GL textures** (GLES3/WebGL2) and composites to the
  pane. A **GPU `Backend`** later … is behind the `Backend` seam — **no editor
  change** (the display path already speaks textures)."* The driver constructs the
  `CpuBackend` behind an abstract `arbc::Backend&`, so a future GPU backend is a
  construction-site swap.
- `docs/01-architecture.md` **A5 / §5** (`:84-97`, log row `:255`) — *"A canvas
  view is **one `HostViewport` + `InteractiveRenderer` over the shared
  `Document`**."* Multi-canvas (*"multiple renderers sharing one `WorkerPool`"*,
  and *"K renderers over 1 pool"*) is scoped to `editor.canvas.multi_canvas`; this
  leaf builds the single-canvas shape. Selection/inspection are project-level, not
  per-canvas — *"a canvas is **only** a camera"* (`:91-92`) — so this leaf carries
  no selection state.
- `docs/01-architecture.md` **A4 / §4** (`:59-82`, log row `:254`) — the
  concurrency contract. The data-flow diagram (`:73-76`) is
  *"per-canvas driver — HostViewport + InteractiveRenderer (own cache, replans) …
  ─▶ frame ─▶ GL texture ─▶ screen."* **The line this leaf is careful about:**
  *"UI thread … SUBMIT edits (never touches the cache directly)"* and *"the UI
  thread stays responsive because rendering is never on it"* (`:66,81-82`) — that
  responsiveness (off-UI-thread driver + double-buffer) is the deliverable of
  `editor.canvas.frame_sync` (`:160-164`, `depends !view`), **not** this leaf.
  This leaf renders on the UI thread as the first cut (see D-canvas_view-2).
- `docs/00-design.md` **D18 / §10** (`:479`, `:414-456`) — *"**Canvas is a view**
  → multiple canvases through different cameras side by side."* This leaf makes the
  Canvas view real; "multiple … side by side" is `multi_canvas`.
- `docs/00-design.md` **D2 / §2** (`:463`, `:48-75`) — *"the editing viewport is
  itself a camera … the **viewport camera** (active, free-nav, transient
  framing)."* At this leaf there is no camera model yet; the canvas frames the
  document's root composition with a default identity/fit camera, exactly as the
  existing offline path does. Interactive framing (pan/zoom via
  `HostViewport::set_camera`) is `editor.canvas.nav`; the persisted camera model
  is `editor.cameras.model`.
- `docs/00-design.md` **D10** (the "invisible translator") — the library owns the
  premultiplied-linear → sRGB8 encode; the canvas calls `CpuBackend::convert`,
  never hand-rolled gamma/un-premultiply math.
- `docs/01-architecture.md` **§8 / A8** (`:144-179`, log `:258`) — the DAG. The
  §7 component table names **`render` (L2)** as
  *"HostViewport/InteractiveRenderer glue · frame-sync · tile→GL"* (`:131`); §8
  places it at L2, `arbc`/GL-allowed but **ImGui-free**. The L1 core stays
  untouched (`:177-179`). `views`/`dock` (L3) are the only ImGui layers.
- `docs/01-architecture.md` **§9 / A9** (`:181-208`, log `:259`) and **§9.1**
  (`:210-245`) — the layered DoD and the offscreen software-GL ASan lane. The
  *Rendered output* row (`:187`, *golden compare reusing `render_offline`*) is the
  one this leaf instantiates a second time (the first *interactive* golden).

**Library API surface (fetched under `build/dev/_deps/arbc-src/`, released
`v0.1.0`; `<arbc/...>` include roots).** The `host-interactive` example
(`examples/host-interactive/main.cpp:83-135`) is the canonical driving loop.

- `<arbc/runtime/host_viewport.hpp>` — `class HostViewport` (non-copyable,
  **non-movable**). The **`Document&` constructor** (`:176`):
  `HostViewport(InteractiveRenderer&, Document&, DocumentBinding, Backend&,
  SurfacePool&, TileCache&, Surface& target, Clock, Config)` — derives the
  `ContentResolver` (`doc.resolve`), installs the damage sink on the document's
  model (so a dispatch commit reaches this frame with no host `set_damage_sink`
  call), and binds the content graph each frame. `doc` and every reference arg
  **must outlive** the viewport. `DocumentBinding{}` is the right shape for a
  document with no external references (`:130-133`). `Config`
  (`:76-107`): `Viewport viewport`, `Time transport_start`, per-frame
  `budget{16ms}`, optional `DamageRouter*`. `StepOutcome step()` (`:226`) drives
  one `InteractiveRenderer::render_frame`; **the first step always composites**,
  and after that it *"issues ZERO `render_frame` invocations when there is no
  pending damage, no owed follow-up, and the scene has not moved"* (`:222-225`).
  `set_camera(const Affine&)` (`:197`) — the seam `nav` drives. Counters:
  `frames_issued()` (`:229`), `last_frame_time()` (`:240`) — the change signal for
  "re-upload only when the frame moved."
- `<arbc/runtime/interactive.hpp>` — `class InteractiveRenderer` (non-copyable,
  non-movable). Two constructors: **own-a-pool**
  `InteractiveRenderer(WorkerPoolConfig = default_interactive_pool_config(), Clock)`
  (`:227`) — the default config ships **real worker threads**; passing an explicit
  `WorkerPoolConfig{}` (`worker_count == 0`) selects *"the deterministic
  thread-free inline executor … the spelling every deterministic unit test and
  golden in the tree uses"* (`:222-228`, `worker_pool.hpp` `worker_count = 0`). And
  **borrow-a-pool** `InteractiveRenderer(WorkerPool&, Clock)` (`:244`) — *"K
  viewports build ONE pool and hand it to every one"* — the shape
  `multi_canvas`/`frame_sync` adopt. A renderer must **not** be shared across
  viewports (per-viewport state) (`:230-243`).
- `<arbc/compositor/compositor.hpp>` — `struct Viewport { int width; int height;
  Affine camera; ObjectId anchor; }`; `using ContentResolver =
  std::function<Content*(ObjectId)>`.
- `<arbc/backend_cpu/cpu_backend.hpp>` — `CpuBackend final : Backend`:
  `make_surface(w, h, SurfaceFormat)`, `convert(Surface& dst, const Surface& src)`
  (un-premultiply + sRGB encode). `<arbc/surface/surface_pool.hpp>` —
  `SurfacePool(Backend&)`. `<arbc/compositor/counters.hpp>` — `TileCache`.
- `<arbc/surface/surface.hpp>` — `Surface::span<PixelFormat::Rgba8Srgb>()`
  (tightly-packed `w*h*4` sRGB8, GL-uploadable); `width()/height()/format()`.
- `<arbc/media/surface_format.hpp:49>` — `k_fast_rgba8srgb` (straight-alpha sRGB8,
  the display/golden format); the target is allocated in the document's working
  space via `document.pin()->working_space()` (premultiplied-linear
  `k_working_rgba32f`), matching `examples/host-interactive/main.cpp:115`.
- `<arbc/runtime/document.hpp>` — `DocStatePtr pin() const`, `Content* resolve(...)`;
  no public `Model&` accessor, so the `Document&` `HostViewport` ctor is the
  intended host path.

**Source seams this leaf extends.**

- `src/render/ace/render/render.hpp` / `src/render/render.cpp:21-40` — the
  offline `render_document_srgb8` + `Srgb8Image`; the convert-and-pack tail
  (`make_surface(k_fast_rgba8srgb)` → `convert` → `span<Rgba8Srgb>()`) the
  interactive driver reuses, and the existing `arbc::Viewport{w, h,
  Affine::identity()}` framing over an opened document (`render.cpp:23`).
- `src/gl/ace/gl/gl.hpp:14-19` / `src/gl/gl.cpp:17-33` — `upload_rgba8` /
  `destroy_texture`; the same seam grows `update_rgba8` (`glTexSubImage2D`).
- `src/views/ace/views/views.hpp:30-50` — `draw_probe_image`,
  `register_view_body`, `draw_view` — the Canvas draw seam.
- `src/dock/dock.cpp:386-398` — the per-open-view Begin/`draw_view`/End loop.
- `src/commands/ace/commands/app_state.hpp:45-46` — `AppState::document()`, how
  the view reaches the shared `Document`.
- `src/app/ace/app/probe.hpp` / `src/app/probe.cpp` — `ProbeView`, the app-layer
  texture-lifecycle pattern the Canvas view mirrors; `src/app/shell.cpp:208-282`
  (the `run_editor` create/register/loop/teardown) and `:216` (the Canvas body
  registration this leaf rewrites).
- `CMakeLists.txt` (the `ace_component(render DEPENDS base project scene gl)` and
  `views DEPENDS … render …` declarations, component block ~`:170-193`);
  `scripts/check_levels.py:42` (`EXTERNAL_ALLOWED` — `arbc` and `gl_api` already
  list `render`).

**Test rigs.** `ace_tests` (headless, GL-free Catch2 + goldens, `ACE_GOLDEN_DIR`,
`CMakeLists.txt:219-235`) — the new `tests/canvas_view_test.cpp` joins here.
`ace_shell_test` (ImGui Test Engine + offscreen software-GL,
`CMakeLists.txt:242-262`) — the new `tests/canvas_view_e2e_test.cpp` joins here;
`tests/history_e2e_test.cpp` is the convention model (`ScratchDir`, real
`AppState`, `register_view_body` + clear-after-loop, drive by stable id,
`IM_CHECK` on model state, manual frame pump). `tests/golden_support.hpp:36` is
the byte-compare helper.

## Constraints / requirements

1. **No new component; no new DAG edge; no lint edit.** The driver bundle lands in
   the existing `render` (L2) — already `DEPENDS base project scene gl` and
   whitelisted for `arbc` + `gl_api` (`scripts/check_levels.py:42`); the new
   `<arbc/runtime/host_viewport.hpp>`, `<arbc/runtime/interactive.hpp>`,
   `<arbc/runtime/worker_pool.hpp>`, `<arbc/compositor/...>`,
   `<arbc/surface/...>` includes are all under the `arbc` external category, so
   **no `check_levels` edit and no CMake edge**. The `update_rgba8` primitive
   lands in `gl` (L0, `gl_api`). The Canvas body draws in `views` (L3, already
   `imgui` + depends `render`). Orchestration stays in `app` (L4). If the
   implementation ever needed a forbidden edge that would be an `A<n>`
   levelization delta — **not expected here**; §7 already homes the interactive
   glue in `render`.
2. **`render` stays ImGui/SDL-free (A8).** The driver bundle includes only
   `<arbc/...>`, `<ace/gl/...>`, `<ace/project/...>` (for the document type), and
   std — never `<imgui.h>` or SDL. The compositor path is GL-free too (it produces
   CPU sRGB8 bytes); the GL upload is a separate `gl`/`app` step. This keeps the
   whole render path headless-unit- and golden-testable without a GL context.
3. **Bind via the `Document&` constructor against the one owned document.** The
   `HostViewport` binds `AppState::document()` (`app_state.hpp:45`); the document
   and all reference collaborators (renderer, backend, pool, cache, target) are
   owned by the driver/app and **outlive** the viewport (the ctor's lifetime
   contract, `host_viewport.hpp:172-175`). `DocumentBinding{}` (no external refs)
   is the default; the derived damage-sink install means a future dispatch commit
   reaches this frame automatically — no host `set_damage_sink`.
4. **Single canvas, single driver, single writer, one document.** This leaf drives
   exactly one `HostViewport`/`InteractiveRenderer` pair. It adds **no editor-owned
   thread and no `WorkerPool`** (see D-canvas_view-2): `step()` runs on the UI
   thread with the deterministic inline executor. The only background thread in the
   process is the `Document`'s `HousekeepingThread` (owned by `AppState`, A4) — this
   leaf introduces no new shared mutable state. N canvases sharing one `WorkerPool`
   is `multi_canvas`; the off-thread driver is `frame_sync`.
5. **The editor is the invisible translator, via the library (D10).** The settled
   working-space target is converted to `k_fast_rgba8srgb` by `CpuBackend::convert`
   and read as straight-alpha sRGB8 through `Surface::span<Rgba8Srgb>()` — reusing
   render_probe's exact tail. No hand-rolled color math.
6. **Re-render / re-upload only on change.** `step()` is called every UI frame, but
   uploads GL only when `frames_issued()` advanced since the last upload (a still,
   undamaged scene issues zero frames and re-draws the existing texture) — `Image`
   the cached texture otherwise. Texture reuse: `upload_rgba8` on first frame and
   on a size change; `update_rgba8` (same dimensions) thereafter;
   `destroy_texture` before shutdown (GL context still valid), mirroring
   `ProbeView`'s explicit lifetime (render_probe D-render_probe-8).
7. **Pane-size drives the viewport; framing defaults to the document.** The canvas
   renders at the Canvas pane's current pixel size; on a size change the driver
   reallocates its target `Surface` and re-binds the `HostViewport` (non-movable →
   reconstruct the bundle) at the new dimensions. The camera is a default
   identity/fit that frames the document's root composition (the same framing
   `render_document_srgb8` uses at `render.cpp:23`); interactive pan/zoom
   (`set_camera`) is `editor.canvas.nav`. A degenerate/zero pane size renders
   nothing (no allocation) until it has area.
8. **Canvas body registration is process-global and reset on exit.** The app
   registers the Canvas `ViewBody` capturing the one `AppState&` and **clears it
   after the run loop** (`register_view_body(ViewType::Canvas, {})`), exactly as the
   History body does (`shell.cpp` History registration; `view_registry.md`
   app-wiring). The standalone `draw_probe_pane`/`ProbeView` path used by the
   pre-registry `shell_e2e` stays available and untouched.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella.

- **Levelization (`check_levels` clean).** `python3 scripts/check_levels.py`
  passes **with no edit**: `render` adds only `<arbc/...>` + `<ace/gl/...>` +
  `<ace/project/...>` + std (no ImGui/SDL); `gl` adds only the `glTexSubImage2D`
  update (GL only); `views` uses `<imgui.h>` + the texture handle; `app` wires. No
  new component, no new DAG edge. Primary structural assertion.
- **L1/L2 logic — Catch2 unit (`render`, GL-free, the bulk).** A new
  `tests/canvas_view_test.cpp` (headless, joined to `ace_tests`,
  `CMakeLists.txt:219-235`) exercises the `render` driver without a GL context:
  - **Drives a frame:** built over a known small `Document` (reuse
    `project::build_probe_document`, or a small multi-layer doc), the driver's
    first `step()` composites (`frames_issued() == 1`), a second `step()` on the
    unchanged, playhead-pinned scene issues **zero** further frames
    (`frames_issued()` unchanged — the still-scene early-out, `host_viewport.hpp:222`).
  - **Produces display bytes:** the settled sRGB8 image is the pane size, tightly
    packed (`w*h*4`), straight-alpha `k_fast_rgba8srgb`.
  - **Resize:** `resize(w,h)` to new dimensions re-frames and the next settled
    image reports the new size.
- **Rendered output — the first *interactive* golden.** A Catch2 case (same
  `tests/canvas_view_test.cpp`, GL-free) renders a known static document through
  the driver (inline executor, playhead pinned) and **byte-compares** the settled
  sRGB8 bytes against a committed `tests/goldens/canvas_view_<W>x<H>.rgba8` via
  `ace_test::compare_golden` (`tests/golden_support.hpp:36`). **Cross-check:** for
  the *same* document and framing, assert the interactive settled bytes are
  **byte-identical** to the offline `render::render_document_srgb8` output — proving
  the interactive compositor path (`HostViewport` + `InteractiveRenderer`) composes
  pixel-for-pixel like the byte-exact `render_offline` reference render_probe shipped.
  Runs on the plain headless lane (no GL context).
- **UI e2e — ImGui Test Engine (Canvas view displays the live document).** A new
  `tests/canvas_view_e2e_test.cpp` (joined to `ace_shell_test`,
  `CMakeLists.txt:242-262`, offscreen software-GL), modeled on
  `tests/history_e2e_test.cpp`: build a real `AppState` over a `ScratchDir`
  project, register the Canvas view body, drive by the stable **`canvas#1`** view
  id — assert `ctx->WindowInfo("canvas#1").ID != 0` and its window has a
  `DockNode`; assert via a test-visible accessor that the driver produced a frame
  (`frames_issued() >= 1`); and use the offscreen `glReadPixels` capture to assert
  the canvas pane shows rendered content (**distinct from** the shell clear color
  `0.10,0.10,0.12` — `shell.cpp`), not a byte-exact golden (software-GL pixels are
  flaky by construction — same rationale as render_probe; the byte-exactness lives
  in the CPU golden above). Clears the Canvas body after the loop.
- **Threading (ASan/TSan) — the UI-thread step loop vs. the background
  checkpointer.** This leaf adds **no editor thread and no `WorkerPool`** (inline
  executor, Constraint 4), but it repeatedly `pin()`s / reads the `Document` and
  writes the driver's `TileCache` **each frame** while the `Document`'s
  `HousekeepingThread` (A4, held by `AppState`) checkpoints the workspace
  concurrently over the whole run — a genuinely new pattern beyond `app_state`'s
  boot/dispatch/teardown. The boot → step N frames → shutdown lifecycle runs under
  the existing `asan` offscreen lane (§9.1) and the TSan preset and must be clean,
  including a clean driver/`HostViewport`/`Document` teardown (thread join). The
  heavy TSan surface — the shared `WorkerPool`, the off-UI-thread driver, and the
  double-buffered handoff — is scoped explicitly to `editor.canvas.frame_sync`.
- **Coverage.** ≥90% diff coverage on changed lines (`diff-cover --fail-under=90`,
  `coverage` preset), including the convert/pull path, the frames-issued upload
  gate, the resize/reallocate branch, and the zero-area no-op.
- **Format/build.** `clang-format --dry-run --Werror` clean; `dev` and `release`
  presets build; `scripts/gate` green.

**No follow-up WBS task is deferred.** Every successor already exists as a
scheduled leaf: the off-UI-thread driver + latest-frame double-buffer + edit
submission is `editor.canvas.frame_sync` (`tasks/00-editor.tji:160-164`), the
shared `WorkerPool` + N per-instance canvases is `editor.canvas.multi_canvas`
(`:166-170`), interactive pan/zoom + scale bar + deep-zoom is
`editor.canvas.nav` (`:172-176`), the persisted camera model is
`editor.cameras.model` (`:188-192`), and the active-tool → pointer-gesture routing
is `editor.canvas.tool_dispatch` (`:178-182`). The `gl::update_rgba8` primitive and
the interactive golden ship as part of this leaf's tests/seam, not as standalone
tasks.

## Decisions

- **D-canvas_view-1 — Decompose across the same §8 homes render_probe used: the
  interactive driver in `render` (L2), the GL update in `gl` (L0), the body in
  `views` (L3), ownership/orchestration in `app` (L4).** §7's component table
  (`docs/01-architecture.md:131`) assigns *"HostViewport/InteractiveRenderer glue ·
  tile→GL"* to `render`, which already links `arbc` + `gl`. Putting the driver
  there keeps the whole compositor path GL-free and ImGui-free (so it unit-tests
  and goldens headless), reuses render_probe's convert tail, and mirrors the exact
  seam split (`project`/`render`/`gl`/`views`/`app`) that leaf established.
  *Alternative rejected:* build the driver inside `app` (L4, may include
  everything) — legal, but untestable per-layer, leaks arbc/compositor
  orchestration into the generic shell, and would fork the render seam instead of
  extending it. *Alternative rejected:* a **new L2 component** for the canvas
  driver — a gratuitous DAG node + `check_levels`/CMake edit for one class with one
  call site, when `render` is already exactly its home.

- **D-canvas_view-2 — Drive `step()` on the UI thread with the deterministic
  inline executor (`WorkerPoolConfig{}`); the off-UI-thread driver + shared pool are
  `frame_sync`/`multi_canvas`.** This is the crux scope call. `frame_sync`
  (`depends !view`) *owns* "the driver renders off the UI thread; the UI displays
  the latest frame (double-buffer) and SUBMITS edits" — so `view` is the first cut
  where rendering is synchronous on the UI thread. Choosing the inline executor
  (rather than the shipped threaded pool) has three payoffs: (1) it lets this leaf
  carry a **byte-exact interactive golden** — the threaded pool is deadline-bounded
  and non-deterministic by construction (partial settles, `deadline_expiries`), so
  a byte-exact frame is only reachable inline (the mode *"every deterministic unit
  test and golden in the tree uses"*, `interactive.hpp:222-228`); (2) it makes
  production and the golden exercise the **same** path (no threaded-vs-inline
  divergence); (3) it keeps this leaf's threading obligation modest and honest — no
  editor thread, no `WorkerPool` — so the heavy TSan surface lands with the leaf
  that actually introduces it. *Alternative rejected:* use the shipped threaded
  pool on the UI thread now — pulls the deadline/park machinery and its
  non-determinism into `view`, makes the mandated golden impossible, and still
  blocks the UI thread (the very thing `frame_sync` exists to fix), for no gain
  before `frame_sync` moves the driver off-thread anyway. *Alternative rejected:*
  do the off-thread double-buffer here — that *is* `frame_sync`'s whole
  deliverable, needs the shared-pool shape, and would leave `frame_sync` with
  nothing. The `InteractiveRenderer` construction is a single seam:
  `frame_sync`/`multi_canvas` flip it to the borrow-a-pool constructor
  (`interactive.hpp:244`) over one app-owned `WorkerPool` without touching the
  compositor/convert/upload code this leaf writes.

- **D-canvas_view-3 — One driver serves the primary Canvas instance; per-instance
  drivers keyed by view id are `multi_canvas`.** Canvas is registered
  `multi_instance` (`canvas#N`), but N canvases sharing one `WorkerPool` is A5's
  `multi_canvas` scope. This leaf's app-layer owner holds a single
  `render::CanvasRenderer` bound to the primary canvas; the registered Canvas body
  draws that driver's texture. *Rationale:* a single driver is the correct
  single-canvas shape and the exact object `multi_canvas` will replicate into a
  per-view-id map over a shared pool; building the map now would front-load
  `multi_canvas`'s scope (and its shared-pool threading) into a 3d leaf.
  *Alternative rejected:* a per-instance driver map here — that is
  `multi_canvas`, and needs the shared `WorkerPool` this leaf deliberately omits.

- **D-canvas_view-4 — Reuse render_probe's `CpuBackend::convert` → sRGB8 →
  `gl::upload_rgba8` tail; add only a same-size `gl::update_rgba8`.** The settled
  working-space target is converted and packed exactly as `render_document_srgb8`
  does (D10 — the library owns the encode), and uploaded through the primitive
  render_probe built *for this reuse*. The only new GL is `glTexSubImage2D` for
  in-place same-size updates, so a re-rendering canvas (a later edit, a `nav`
  camera nudge) reuses its texture object instead of churning GL handles.
  *Rationale:* maximal reuse of the proven, golden-backed color path; the update
  primitive is the natural completion of the tile→GL seam and belongs with it.
  *Alternative rejected:* golden/upload the working-space float32 surface directly
  — not display-ready, 4× larger, and re-implements the sRGB encode the editor must
  not own. *Alternative rejected:* fresh `upload_rgba8` + `destroy_texture` every
  changed frame — churns GL objects under continuous `nav` re-renders for no
  benefit over `glTexSubImage2D`.

- **D-canvas_view-5 — The canvas frames the document's root composition with a
  default camera; interactive framing is `nav`, the camera model is
  `editor.cameras.model`.** There is no camera model at this leaf, so the viewport
  uses an identity/fit camera over the document's root composition — the same
  framing the offline path already renders (`render.cpp:23`). *Rationale:* D2 makes
  the viewport camera *transient* free-nav state (`set_camera`, driven by `nav`),
  and the persisted camera is a separate registered leaf; a canvas must show
  *something* correct before either exists, and the document's own framing is that
  something. *Alternative rejected:* block on a camera concept — `cameras.model`
  `depends editor.canvas.view`, so the canvas must stand on its own first;
  inventing a camera here would duplicate that leaf's scope.

## Open questions

- _None — all decided against the constitution._ A6 fixes the CPU-tiles → GL
  display path and the `Backend` seam; A5 fixes the single-canvas
  `HostViewport` + `InteractiveRenderer` shape (multi-canvas + shared pool =
  `multi_canvas`); A4 fixes that the off-UI-thread driver + double-buffer + edit
  submission belong to `frame_sync` (so `view` renders synchronously on the UI
  thread with the deterministic inline executor); D2 fixes the transient viewport
  camera (framing/nav = `nav`, model = `cameras.model`); §7/§8 home the interactive
  glue in `render` (L2) with no new component or edge; §9 fixes the golden model
  (and the inline executor makes the first interactive golden byte-exact and
  cross-checkable against the offline reference); D10 fixes the library-owned sRGB
  encode. The library API is concrete in the fetched `v0.1.0` surface (`HostViewport`
  `Document&` ctor + `step()`, `InteractiveRenderer` inline/borrow ctors,
  `CpuBackend::convert`, `Surface::span<Rgba8Srgb>`) and demonstrated end-to-end by
  `examples/host-interactive/main.cpp`. **No doc delta required** — no new
  dependency, no new component, no new DAG edge, no deviation from a decided
  behavior.

## Status

**Done** — 2026-07-18.

- **`render` (L2) — `CanvasRenderer`:** New `src/render/ace/render/canvas_renderer.hpp` + `src/render/canvas_renderer.cpp` — composes `CpuBackend` + `SurfacePool` + `TileCache` + persistent working-space `Surface` + `InteractiveRenderer` (inline executor, `WorkerPoolConfig{}`) + `HostViewport` via the `Document&` constructor; exposes `resize(w,h)`, `step()` (drive one frame, convert settled target → sRGB8 via `CpuBackend::convert`), `current_image()`, and `frames_issued()`. GL-free, headless-testable.
- **`gl` (L0) — `update_rgba8`:** Added `gl::update_rgba8(texture, pixels, w, h)` (`glTexSubImage2D`) to `src/gl/ace/gl/gl.hpp` + `src/gl/gl.cpp` — same-size in-place texture update so a re-rendering canvas reuses its GL texture object instead of churning `glGenTextures`/`glDeleteTextures`.
- **`views` (L3) — `draw_canvas_image`:** Added `views::draw_canvas_image(texture, w, h)` to `src/views/ace/views/views.hpp` + `src/views/views.cpp` — thin stable-id alias for drawing the driver's current texture as an `ImGui::Image` into the current window.
- **`app` (L4) — `CanvasView`:** New `src/app/ace/app/canvas_view.hpp` + `src/app/canvas_view.cpp` — owns `render::CanvasRenderer` + GL texture; each frame sizes the driver to the Canvas pane, `step()`s it, and uploads (fresh `upload_rgba8` on first frame / resize, `update_rgba8` otherwise) only when `frames_issued()` advanced. Registered as the Canvas `ViewBody` in `src/app/shell.cpp`, replacing the `ProbeView` probe stand-in.
- **`CMakeLists.txt`:** Added two test targets — `canvas_view_test` (headless Catch2, `ace_tests`) and `canvas_view_e2e_test` (ImGui Test Engine + offscreen GL, `ace_shell_test`).
- **Tests — unit (`tests/canvas_view_test.cpp`):** Frame-drive/still-scene early-out (`frames_issued()` does not advance on unchanged scene), display bytes (sRGB8, tightly packed `w*h*4`), resize rebind (new dimensions reported after `resize()`), zero-area no-op; interactive golden `tests/goldens/canvas_view_64x64.rgba8` byte-compared + cross-checked byte-identical to offline `render_document_srgb8`; `gl::update_rgba8` primitive test.
- **Tests — e2e (`tests/canvas_view_e2e_test.cpp`):** `canvas#1` docked + live-render distinct-from-clear-color; pane-resize case. Interactive golden committed at `tests/goldens/canvas_view_64x64.rgba8`.
- Diff-coverage 98%; the `update_rgba8` call-site branch (same-size in-place path, `canvas_view.cpp:45-46`) is honestly uncovered — triggerable only by a future edit/nav re-render, already owned by `editor.canvas.frame_sync` / `editor.canvas.nav`.
