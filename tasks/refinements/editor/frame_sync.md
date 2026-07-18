# editor.canvas.frame_sync — UI-thread ↔ driver frame handoff; edit submission

## TaskJuggler entry

`tasks/00-editor.tji:161-166` — `task frame_sync` under `editor.canvas`. Effort
`3d`, `allocate team`, `depends !view` (i.e. `editor.canvas.view`). The `note`
(`:165`) cites **Arch A4** and names this refinement. (The `.tji` note currently
back-links to the flat `tasks/refinements/frame_sync.md`; the real landing path
is `tasks/refinements/editor/frame_sync.md`, matching the `editor/` refinement
set — the closer fixes the note back-link per the ritual in
`tasks/refinements/README.md:57-68`, exactly as `open.md` / `app_state.md` /
`canvas_view.md` did.)

Downstream dependents: `editor.canvas.multi_canvas` (`:167-171`,
`depends !frame_sync` — N `HostViewport`/`InteractiveRenderer` over one
`Document` sharing one `WorkerPool`), which inherits this leaf's off-thread
driver shape and generalizes it to the shared-pool, N-canvas case. This leaf is
the first editor-owned **real-concurrency** surface: it is the designated
ASan/TSan target that `render_probe`, `canvas_view`, `app_shell`, `tool_rail`,
`dockspace`, `undo`, `save`, and `history` all explicitly deferred here.

## Effort estimate

**3 days** (from the `.tji`). This leaf promotes the synchronous, UI-thread
interactive driver `canvas_view` shipped into a **double-buffered, off-UI-thread
driver**: it moves the `HostViewport::step()` drive loop onto a dedicated
editor-owned render thread, publishes each settled frame into a latest-frame
double-buffer that the UI thread reads and uploads to GL, and wires the
edit-submission "poke" so a UI-thread edit promptly wakes the driver to
re-render. The cost is in three places: (1) the **double-buffer handoff** — a
single-producer/single-consumer latest-frame slot that is race-free under TSan
and never blocks the UI thread; (2) **thread lifecycle** — spawning the render
thread through the portable `platform` spawn seam (A3), a condition-variable
idle/wake loop honoring `step()`'s still-scene early-out, and a clean
stop→wake→join teardown; and (3) **confining the driver to the render thread** —
all `HostViewport` / `InteractiveRenderer` / `TileCache` access (construction,
resize, `step()`) happens on the render thread only, so the tile cache stays
render-thread-confined (A4). It deliberately does **not** introduce the shared
`WorkerPool` across N canvases (that is `multi_canvas`), drive camera pan/zoom
(`nav`), or move the *writer* off the UI thread (see D-frame_sync-2).

## Inherited dependencies

**Settled (from `editor.canvas.view`, `tasks/00-editor.tji:154-158`, Done
2026-07-18).** The synchronous interactive driver already exists as
`render::CanvasRenderer` — `src/render/ace/render/canvas_renderer.hpp` (`class
CanvasRenderer`, ctor `explicit CanvasRenderer(arbc::Document&)` at `:35`;
`resize(int,int)` `:44`, `step()` `:51`, `image()` `:55`, `frames_issued()`
`:60`) + `src/render/canvas_renderer.cpp` (the `Impl` composes `CpuBackend` +
`SurfacePool` + `TileCache` (64 MiB, `:27`) + persisted working-space target
`Surface` + `InteractiveRenderer` (inline `WorkerPoolConfig{}`, worker_count 0,
`:58`) + `HostViewport` via the `Document&` ctor, `rebuild()` at `:36-72`,
`step()` driving `viewport->step()` at `:118-128`, `convert()` → straight-alpha
sRGB8 `Srgb8Image` at `:77-88`). Its owner is `app::CanvasView` —
`src/app/ace/app/canvas_view.hpp` (`class CanvasView` `:22`, ctor `explicit
CanvasView(commands::AppState&)` `:24`, `draw_content(int,int)` `:34`,
`frames_issued()` `:38`, `destroy()` `:42`) + `src/app/canvas_view.cpp`
(`draw_content` `:14-51`: resize-on-change → `renderer_.step()` → upload-only-on-
`frames_issued()`-advance, fresh `gl::upload_rgba8` `:35-43` vs in-place
`gl::update_rgba8` `:44-47` → `views::draw_canvas_image` `:49`). Registered as
the Canvas `ViewBody` in `src/app/shell.cpp:215-221`. `canvas_view` explicitly
scoped **to this leaf**: "the off-UI-thread driver + latest-frame double-buffer +
edit submission" (`canvas_view.md:417-420`), and the honestly-uncovered
`update_rgba8` in-place branch at `canvas_view.cpp:44-47` (`canvas_view.md:540`).

**Settled (from `editor.project.app_state`, Done).** The process owns exactly
one `arbc::Document` for its lifetime — `commands::AppState::document()`
(`src/commands/ace/commands/app_state.hpp:44-46`) — with its libarbc-owned
`HousekeepingThread` (A4, one per `Document`) already running concurrently. The
synchronous single-writer edit seam is `commands::dispatch(AppState&, const
Command&)` (`app_state.hpp:132`, impl `src/commands/app_state.cpp:29-39`) and
`undo`/`redo` (`app_state.hpp:146,151`, impl `app_state.cpp:41-58`, driving
`doc.journal().undo()/redo()`); gesture coalescing via `AppState::
next_gesture_key()` (`app_state.hpp:88`). `app_state.md` fixes this as
synchronous single-writer "here" and names **this leaf** as where the off-thread
handoff lands (`app_state.md:111-112,160-161,262-264,409-410`).

**Settled (from `foundation.platform_services`, Done).** The portable
thread-spawn seam — `platform::NativeThreads` / `Threads` / `JoinHandle` at
`src/platform/ace/platform/threads.hpp` — is the A3 faculty for **editor-owned
auxiliary threads** (`platform_services.md:128-134,273`). It is explicitly *not*
a `WorkerPool` and must not wrap or duplicate libarbc's concurrency; the render
pool and per-`Document` housekeeping stay the library's
(`platform_services.md:226-234`). platform_services also established the
sanitizer precedent this leaf inherits: `ace_tests` clean under `asan`/`tsan`
(`platform_services.md:178-188`), while "libarbc's `WorkerPool` concurrency
remains scoped to `editor.canvas.frame_sync`, not this leaf" (`:186-188`).

**Pending (owned here or downstream).**
- **The shared `WorkerPool` / borrow-a-pool flip** (`InteractiveRenderer(WorkerPool&,
  Clock)`, `interactive.hpp:244`) across N canvases is `editor.canvas.multi_canvas`
  (`:167-171`, `depends !frame_sync`), *not* this leaf — this leaf keeps the
  single driver's own-a-pool/inline construction (D-frame_sync-3).
- **Camera pan/zoom** driving the off-thread viewport is `editor.canvas.nav`
  (`:172-176`, `depends !view`).
- **UI-thread journal-read safety** under an off-thread writer — history.md /
  undo.md deferred a snapshot/guard here (`history.md:67,245,317`;
  `undo.md:83,194`). This leaf **resolves that concern by construction** by
  keeping the writer on the UI thread (D-frame_sync-2): the render thread never
  reads the journal, so History-panel reads stay same-thread with the writer and
  need no guard.

## What this task is

Move the interactive canvas driver **off the UI thread** and hand finished
frames back to the UI thread through a **latest-frame double-buffer**, so the UI
thread stays responsive while rendering runs concurrently — the concrete
realization of Arch A4's "the UI thread stays responsive because rendering is
never on it" (`docs/01-architecture.md:81-82`). Today `canvas_view` drives
`viewport->step()` synchronously from inside the ImGui frame
(`canvas_view.cpp:23` → `canvas_renderer.cpp:122`), so a slow frame stalls the
UI. This leaf spawns a dedicated **render thread** that owns the `HostViewport` /
`InteractiveRenderer` / `TileCache` and loops `step()`, publishing each settled
`Srgb8Image` into a double-buffer; the UI thread reads the latest published frame
each tick and uploads it to GL (reusing the `upload_rgba8`-first /
`update_rgba8`-on-change logic `canvas_view` already ships). Edit submission stays
on the UI thread as the single writer (`commands::dispatch` / `undo` / `redo`,
unchanged), but now **pokes** the render thread after each edit so the driver
wakes and re-renders the damage promptly. The tile cache stays single-writer /
render-thread-confined (A4): only the render thread touches it.

## Why it needs to be done

`canvas_view` put the shared `Document` on screen interactively but on the UI
thread as its "first cut" — it explicitly deferred responsiveness (the
off-UI-thread driver + double-buffer + edit submission) to this leaf
(`canvas_view.md:36-38,142-146,188-191,447-449,466-469`). Without it, every
canvas re-render (an edit, a settling async load, a future pan/zoom) blocks the
event loop for the duration of `step()`. It is also the gate for
`editor.canvas.multi_canvas` (`depends !frame_sync`): multi-canvas is "N drivers
over one `Document` sharing one `WorkerPool`" and needs the single off-thread
driver + double-buffer shape this leaf establishes before it can replicate it
across a per-view map on a shared pool (`canvas_view.md:466-481`). And it is the
editor's first genuine concurrency surface — the UI↔driver handoff that §9 names
as the ASan/TSan lane (`docs/01-architecture.md:190`) and that seven predecessor
refinements deferred here for that coverage.

## Inputs / context

**Governing design docs (normative — the constitution).**
- **Arch A4** — `docs/01-architecture.md:59-82` (§4) and the log restatement at
  `:254`. The threading contract this leaf realizes: tile cache
  **single-writer / render-thread-confined**, worker dispatch **leaf-only**, one
  shared `WorkerPool`, one `HousekeepingThread` per `Document`. The data-flow
  diagram (`:66-79`): "UI thread … SUBMIT edits │ (never touches the cache
  directly) ▼ Transaction ▶ Document (writer) ▶ damage … per-canvas driver
  HostViewport + InteractiveRenderer (own cache, replans) … cache ▶ frame ▶ GL
  texture ▶ screen". Closing rule (`:81-82`): "Edits flow UI → writer → damage →
  renderers; frames flow renderers → UI. The UI thread stays responsive because
  rendering is never on it."
- **Arch A5** — `docs/01-architecture.md:84-97` (§5): multi-canvas = N renderers
  sharing one `WorkerPool`, no new locking. Constrains this leaf's driver shape
  so `multi_canvas` can replicate it. Selection + shared panels are
  project-level, not per-canvas (D19) — a canvas carries no state but its camera.
- **Arch A3** — `docs/01-architecture.md:43-57` (§3): the `PlatformServices`
  thread-spawn seam (std threads native / Emscripten pthreads over Web Workers +
  `SharedArrayBuffer`, COOP/COEP). The render thread must be spawned through this
  seam, not raw `std::thread`, for WASM portability.
- **§8 levelization** — `docs/01-architecture.md:144-179`. `frame-sync` is named
  at **L2 `render`** (`:155`: "HostViewport/InteractiveRenderer glue · frame-sync
  · tile→GL"). `render` (L2) may depend on base, project, scene, gl, libarbc; GL
  but not ImGui (`:172`). L1 core stays ImGui/GL/SDL-free (`:177-179`).
- **§9 DoD** — `docs/01-architecture.md:181-208`. Universal DoD (`:199-203`):
  respects the DAG (`check_levels` clean); lands its tests (L1 logic → Catch2,
  renders → golden, UI → ImGui Test Engine e2e, threads → sanitizer-clean);
  clang-format + build clean. §9 line 190: "Threading & smoke — UI↔driver
  handoff … ASan/TSan". §9.1 (`:210-245`) the offscreen software-GL ASan lane.

**libarbc API surface (fetched `v0.1.0` under `build/*/_deps/arbc-src/`; local
checkout `/home/ruoso/devel/arbitrarycomposer`).**
- `<arbc/runtime/host_viewport.hpp>` — `class HostViewport` (non-copyable,
  non-movable; `:181-184`). Ctor `HostViewport(InteractiveRenderer&, Document&,
  DocumentBinding, Backend&, SurfacePool&, TileCache&, Surface&, Clock, Config)`
  (`:176-178`). `StepOutcome step()` (`:226`, drive one frame; deadline-bounded by
  `Config::budget`), `frames_issued()` (`:229`), `set_camera(const Affine&)`
  (`:197`, the seam `nav` drives). Non-movable ⇒ resize reconstructs (already the
  pattern in `canvas_renderer.cpp:36-72`).
- `<arbc/runtime/interactive.hpp>` — `class InteractiveRenderer`. Own-a-pool
  ctor `InteractiveRenderer(WorkerPoolConfig = default_interactive_pool_config(),
  Clock = {})` (`:227-228`); borrow-a-pool ctor `InteractiveRenderer(WorkerPool&,
  Clock = {})` (`:244`, the shape `multi_canvas` adopts). `k_max_interactive_workers
  = 2` (`:155`).
- `<arbc/runtime/document.hpp>` — `Model::Transaction transact(std::string = {})`
  **WRITER-THREAD ONLY** (`:178`); `Journal& journal()` (`:184`); `pin()` →
  immutable `DocStatePtr` (`:261`) whose `revision()` is the drift/display signal.
  The render thread must call **only** the read/render path (`step()` via the
  binding, `pin()`), never `transact`/`journal`.
- `<arbc/runtime/worker_pool.hpp>` — `WorkerPool`, `WorkerPoolConfig
  { worker_count = 0; … }` (`:131-147`), `submit`/`poke`/`wait_completions`/
  `drain_owner`/`request_stop` (`:170-253`). Relevant to `multi_canvas`'s shared
  pool; this leaf keeps `worker_count == 0` (inline on the render thread).

**Editor seams this leaf forks / extends.**
- `src/render/ace/render/canvas_renderer.hpp` + `src/render/canvas_renderer.cpp`
  — the driver; wrap it with the render-thread loop + double-buffer. `Srgb8Image
  { int width, height; std::vector<uint8_t> pixels; }` at
  `src/render/ace/render/render.hpp:17-21`; the offline byte-exact reference
  `render::render_document_srgb8` at `src/render/render.cpp:21-40`.
- `src/app/ace/app/canvas_view.hpp` + `src/app/canvas_view.cpp` — the GL-texture
  owner; add the render-thread lifecycle + poke, keep the upload logic.
- `src/gl/ace/gl/gl.hpp` — `upload_rgba8` (`:18`), `update_rgba8` (`:26`),
  `destroy_texture` (`:28`) — reused as-is.
- `src/platform/ace/platform/threads.hpp` — `platform::NativeThreads` spawn seam.
- `src/app/shell.cpp:215-221,267-271` — the `run_editor` loop and Canvas view
  registration; teardown wiring lands here.
- Edit-poke wiring: `dock::ProjectGateway` / `app::AppProjectGateway`
  (`src/app/ace/app/project_gateway.hpp`, `src/app/project_gateway.cpp`) and the
  Ctrl+Z chord in `src/dock/dock.cpp` — the app-level points that call
  `commands::dispatch`/`undo`/`redo` and must poke the driver afterward.

**Predecessor refinements** (style + decision continuity):
`tasks/refinements/editor/canvas_view.md` (the driver + the deferred scope),
`tasks/refinements/foundation/render_probe.md` (the display tail / the deferred
threaded head, `:112-118,201-206,286-299`),
`tasks/refinements/foundation/platform_services.md` (the spawn seam + sanitizer
precedent), `tasks/refinements/editor/app_state.md` (the synchronous submit
seam), `tasks/refinements/editor/{undo,history,save}.md` (writer-thread ordering
+ deferred journal-read safety).

## Constraints / requirements

1. **Render-thread-confined driver (A4).** All `HostViewport` /
   `InteractiveRenderer` / `TileCache` / target-`Surface` access — construction,
   `rebuild`/resize, and every `step()` — happens on the **render thread only**.
   The tile cache is never touched by the UI thread. Resize is a *request* the UI
   thread posts; the render thread applies it (reconstructing the non-movable
   `HostViewport`) at the top of its next iteration.
2. **Single writer stays the UI thread (A4).** `commands::dispatch` / `undo` /
   `redo` (and `transact` / `journal`) continue to run on the UI thread — it is
   the sole writer. The render thread is a pure **reader** of the `Document`
   (via `HostViewport`'s binding + `pin()`), never calling `transact` or
   `journal`. This keeps the single-writer and leaf-only-dispatch contracts and
   needs **no lock on the `Document`** (libarbc's versioned `pin()` gives
   lock-free concurrent read while the writer publishes new versions).
3. **Latest-frame double-buffer, non-blocking.** Exactly one producer (render
   thread) and one consumer (UI thread). The UI thread must **never block** on
   the render thread: reading the latest frame is a bounded, lock-guarded (or
   lock-free) swap, not a wait. Publish carries a monotonically increasing frame
   sequence so the UI re-uploads to GL **only when the sequence advances** (the
   existing `upload_rgba8`-first / `update_rgba8`-on-change logic, resolving the
   `canvas_view.cpp:44-47` uncovered branch). No partially-written frame is ever
   observable (full-frame publish under the swap).
4. **Poke on edit; idle when settled.** The render loop waits on a
   condition-variable (or equivalent) and is woken by (a) a UI-thread edit poke,
   (b) a resize request, or (c) its own signal that `step()` reported unsettled
   work (async loads still settling). When `step()` reports a still scene it
   issues zero frames (`frames_issued()` stable) and the loop returns to waiting
   — **no busy-spin**, honoring `canvas_view`'s still-scene early-out.
5. **Portable spawn + clean teardown (A3).** The render thread is spawned through
   `platform::NativeThreads` (not raw `std::thread`), so WASM rides A3. Teardown
   is deterministic: set stop → wake → `join()`, and only then destroy the driver
   and GL texture. No detached threads; no thread outlives its `CanvasView`.
6. **Levelization (§8).** No new component and **no new DAG edge**: the
   double-buffer + drive loop live in **L2 `render`** using only standard-library
   synchronization (`<mutex>`, `<condition_variable>`, `<atomic>`), and the render
   *thread* is spawned in **L4 `app`** (which already depends on `platform`),
   calling the render-side `run(stop_predicate)` loop. `render` gains no
   `platform` include; L1 core gains nothing; `check_levels` stays clean
   (D-frame_sync-1).

## Acceptance criteria

The universal DoD (`docs/01-architecture.md:199-203`) instantiated here:

- **Levelization — `check_levels` clean.** No new component, no new DAG edge, no
  ImGui/GL/SDL include added to L1 or to `render`. `scripts/check_levels.py`
  passes; verified by `scripts/gate`.
- **Catch2 L1/L2 unit tests** (GL-free, headless; new `tests/canvas_driver_test.cpp`
  or extending `tests/canvas_view_test.cpp`, joined to `ace_tests`), inline
  executor for determinism:
  - **Double-buffer publish/consume:** after one drive iteration, the consumer
    reads the settled frame with a fresh sequence; a second consume at the same
    sequence reports "no new frame"; after an edit + iteration the sequence
    advances and the new frame is observed.
  - **Drive loop with stop predicate:** `run()` given a predicate that stops
    after N iterations returns and joins cleanly, having published ≥1 frame.
  - **Resize request applied on the render side:** posting a new size makes the
    next iteration report the new dimensions in the published frame.
  - **Still-scene idle:** once settled, further iterations publish no new frame
    (sequence stable) — the early-out holds across the thread boundary.
  - **Byte-exactness preserved:** the settled double-buffered frame is
    byte-identical to offline `render::render_document_srgb8` and to the committed
    golden (below) — the off-thread handoff does not perturb pixels (inline
    executor makes this deterministic, per `canvas_view` D-canvas_view-2).
- **Golden** (`render_offline` byte-exact, reusing/extending
  `tests/goldens/canvas_view_64x64.rgba8` via `ace_test::compare_golden`): the
  frame that traverses the render thread + double-buffer, once settled, matches
  the offline reference byte-for-byte. No tolerance.
- **ImGui Test Engine e2e** (extending `tests/canvas_view_e2e_test.cpp`, joined
  to `ace_shell_test`, offscreen software-GL): boot the shell over a real
  `AppState` with a seeded solid fill; drive `canvas#1` by stable id; assert it
  docks and renders content distinct from the shell clear color
  (`0.10,0.10,0.12`) via offscreen `glReadPixels`; then **submit an edit through
  the gateway and assert the canvas updates to a new frame** (sequence advances /
  pixels change), exercising poke → off-thread re-render → double-buffer →
  `gl::update_rgba8` in-place upload end-to-end (this covers the branch
  `canvas_view.md:540` left uncovered). Assert clean stop→join on teardown.
- **ASan/TSan — the designated target for this leaf.** `ace_tests` /
  `ace_shell_test` run clean under the `asan` and `tsan` presets over the full
  lifecycle: boot → render thread loops `step()` ‖ UI thread submits edits
  (writer) + pokes ‖ libarbc `HousekeepingThread` checkpoints ‖ double-buffer
  handoff → shutdown with clean thread join. No data race (especially the
  double-buffer slot and the resize/poke signals), no leak, no
  use-after-free on teardown. This is the "UI↔driver handoff" TSan scope of §9
  (`:190`) and the first editor real-concurrency target (platform_services only
  exercised a toy atomic). Runs under the §9.1 offscreen ASan lane.
- **Coverage ≥90% diff** (`diff-cover --fail-under=90` under the `coverage`
  preset) and **clang-format** clean (`--dry-run --Werror`). The previously
  uncovered `update_rgba8` in-place branch is now exercised by the e2e edit case.

**No new WBS leaf is deferred by this refinement.** The two pieces this leaf
does not build — the shared `WorkerPool` / borrow-a-pool flip across N canvases,
and camera pan/zoom of the off-thread viewport — are already WBS leaves
(`editor.canvas.multi_canvas` `:167-171`, `editor.canvas.nav` `:172-176`); this
leaf's driver shape is exactly what `multi_canvas` replicates. (Whether to ever
move the *writer* off the UI thread — an async submit-queue — is intentionally
**not** a WBS task; see Open questions.)

## Decisions

- **D-frame_sync-1 — Thread in L4 `app`, drive-loop + double-buffer in L2
  `render`; no new DAG edge, no doc delta.** The render-side driver exposes a
  blocking `run(std::function<bool()> should_stop)` loop and a thread-safe
  latest-frame double-buffer built from `<mutex>`/`<condition_variable>`/
  `<atomic>` (standard headers, not a component include). `app::CanvasView` (L4,
  which already depends on `platform`) spawns the render thread via
  `platform::NativeThreads` and points it at `run()`. *Rationale:* keeps the §8
  DAG intact — `render` (L2) gains no `platform` edge, L1 gains nothing,
  `check_levels` stays clean. L4 is "wiring (SDL+GL+ImGui)"
  (`docs/01-architecture.md:152`); spawning the thread that runs an L2 loop is
  exactly that wiring. *Alternative rejected:* let `render` own the thread by
  adding a `render → platform` edge (a new A-number doc delta). Rejected because
  it introduces an architectural edge for **no functional gain** — the drive
  loop and double-buffer (the testable logic) live in `render` either way, and
  only the spawn/join primitive would move; the simpler abstraction with one call
  site today (§8 bias toward the minimal dependency surface) keeps threading
  policy in L4 and needs no constitution change. **No doc delta required.**

- **D-frame_sync-2 — Only the renderer moves off-thread; the writer stays on the
  UI thread (no submit-queue).** `commands::dispatch` / `undo` / `redo` continue
  to run synchronously on the UI thread as the single writer; the UI thread pokes
  the render thread after each edit. The render thread is a pure reader via
  libarbc's lock-free versioned `pin()`. *Rationale:* this is precisely what A4
  states — "UI thread … SUBMIT edits (never touches the **cache** directly) …
  Transaction ▶ Document (writer)" and "rendering is never on it" — the UI is the
  writer, the render thread owns the cache. `transact` is a cheap model mutation;
  the expensive work is rendering, which is what moves off-thread, so the UI stays
  responsive. It keeps single-writer trivially (one thread), needs **no lock on
  the `Document`**, scales to `multi_canvas` (single writer = the one UI thread
  across N readers, A5's "no new locking"), and **resolves by construction** the
  UI-thread journal-read safety that `history.md`/`undo.md` deferred here
  (`history.md:67,245,317`; `undo.md:83,194`): the render thread never reads the
  journal, so History-panel reads stay same-thread with the writer — no snapshot
  guard needed. *Alternative rejected:* the off-UI-thread submit-queue +
  dedicated writer thread that `app_state.md` anticipated (`:264,409-410`).
  Rejected as unnecessary complexity now — it would add a second thread, a
  cross-thread command queue, serialized undo-cursor navigation, **and** the
  journal-read guard, all to move a cheap operation off-thread. A4 does not
  mandate an off-thread writer; it mandates off-thread *rendering*, which this
  delivers. (If profiling ever shows UI-thread `transact` latency, that is a
  future call — see Open questions — not a prerequisite here.)

- **D-frame_sync-3 — Keep the single driver's own-a-pool / inline executor; the
  shared `WorkerPool` is `multi_canvas`.** This leaf retains
  `canvas_view`'s `InteractiveRenderer` construction (inline `WorkerPoolConfig{}`,
  `worker_count == 0`) and simply moves the `step()` loop onto the render thread.
  *Rationale:* the responsiveness deliverable is "rendering off the UI thread,"
  achieved by the dedicated render thread regardless of internal pool parallelism;
  keeping the inline executor preserves the **byte-exact golden path**
  `canvas_view` established (a threaded pool is deadline-bounded / non-deterministic,
  `canvas_view.md:452-460`). The shared/borrow-a-pool flip
  (`InteractiveRenderer(WorkerPool&, Clock)`, `interactive.hpp:244`) only earns
  its keep when there are ≥2 viewports to share one pool across — which is
  `editor.canvas.multi_canvas`'s explicit deliverable (`:167-171`,
  `canvas_view.md:466-481`). *Alternative rejected:* stand up the shared
  `WorkerPool` here. Rejected — one canvas has nothing to share, it would rewrite
  the drive loop into the `submit`/`poke`/`wait_completions`/`drain_owner` shape
  (`worker_pool.hpp:170-249`) that `multi_canvas` owns, and it would forfeit
  golden determinism for no single-canvas benefit.

- **D-frame_sync-4 — Full-frame publish with a monotonic sequence; UI re-uploads
  only on advance.** The double-buffer publishes a complete `Srgb8Image` under
  the swap (never a torn frame) tagged with an incrementing sequence; the UI
  compares against the last-uploaded sequence and calls `upload_rgba8` on first
  frame / size change, `update_rgba8` otherwise, mirroring
  `canvas_view.cpp:35-47`. *Rationale:* single-producer/single-consumer
  latest-value handoff is the simplest race-free structure; the sequence avoids
  redundant GL uploads and gives the e2e a crisp "did it update" assertion.
  *Alternative rejected:* a lock-free triple-buffer with atomics only. Deferred
  as premature — a small mutex-guarded pointer swap is bounded, never contended
  (one producer, one consumer, sub-millisecond hold), TSan-trivial to prove, and
  can be swapped for a lock-free variant later if profiling demands it, without
  changing the seam.

## Open questions

`(none — all decided.)`

One item is **surfaced for the parking lot** (human judgment, not WBS work): if
future profiling shows UI-thread `transact`/`dispatch` latency stalling the event
loop on large edits (e.g. import), moving the *writer* off the UI thread behind
an async submit-queue (D-frame_sync-2's rejected alternative) may become
warranted. This is a monitor-and-decide call gated on real telemetry, not
implementable work today, so it is **not** encoded as a WBS task (it would be an
un-closeable "reconsider" leaf); the closer records it in
`tasks/parking-lot.md`.

## Status

**Done** — 2026-07-18.

- Shipped `render::CanvasDriver` off-UI-thread drive loop with move-based latest-frame double-buffer and std-sync only (`src/render/ace/render/canvas_driver.hpp`, `src/render/canvas_driver.cpp`).
- `app::CanvasView` spawns the render thread via `platform::NativeThreads`, reads the latest frame each UI tick, and calls `gl::upload_rgba8`/`gl::update_rgba8` only when the frame sequence advances (`src/app/ace/app/canvas_view.hpp`, `src/app/canvas_view.cpp`).
- Edit-poke wired through `AppProjectGateway` so UI-thread edits wake the driver promptly (`src/app/ace/app/project_gateway.hpp`, `src/app/project_gateway.cpp`).
- Shell teardown wired: stop → wake → join before GL texture destruction (`src/app/shell.cpp`).
- Fixed `GL_UNPACK_ROW_LENGTH` reset before every `glTexImage2D`/`glTexSubImage2D` in `gl::upload_rgba8`/`gl::update_rgba8` to prevent heap-buffer-overflow from inherited ImGui GL3 state (`src/gl/gl.cpp`).
- 5 new Catch2 unit cases: publish/consume, still-scene idle, edit-advances-sequence, render-side resize, and byte-exact golden against `tests/goldens/canvas_view_64x64.rgba8` (`tests/canvas_driver_test.cpp`).
- e2e rewritten for async off-thread model (3 cases: live-doc docked, edit-through-gateway poke → re-render → in-place `update_rgba8`, resize churn) and gateway edit-listener poke assertion added (`tests/canvas_view_e2e_test.cpp`, `tests/app_project_gateway_test.cpp`).
- `CMakeLists.txt` updated to build `canvas_driver` and wire `canvas_driver_test`.
- TSan clean; ASan clean at unit level; gate + full CI matrix green.
