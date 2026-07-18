# editor.canvas.multi_canvas — N canvases over one Document sharing one WorkerPool

## TaskJuggler entry

`tasks/00-editor.tji:168-172` — `task multi_canvas` under `editor.canvas`. Effort
`2d`, `allocate team`, `depends !frame_sync` (i.e. `editor.canvas.frame_sync`).
The `note` (`:172`) cites **Arch A5 / D18**, names `runtime.shared_worker_pool`,
and frames the leaf as "paint through Viewport ‖ look through Hero". (The `.tji`
note currently back-links to the flat `tasks/refinements/multi_canvas.md`; the
real landing path is `tasks/refinements/editor/multi_canvas.md`, matching the
`editor/` refinement set — the closer fixes the note back-link per the ritual in
`tasks/refinements/README.md:57-68`, exactly as `frame_sync.md` / `canvas_view.md`
/ `view_registry.md` did.)

Downstream dependents: `editor.cameras.look_through` (`tasks/00-editor.tji:202-206`,
`depends editor.canvas.multi_canvas` at `:205`) — "make a shot the active camera
of a canvas; a second canvas looks through a shot for a live export preview beside
the editing Viewport" (D18). That leaf adds a *second* canvas entry pointed at a
different camera; this leaf builds the machinery to have more than one canvas
render at all. This is the leaf that turns the dock model's already-minted
`canvas#N` slots into N independently-rendering viewports.

## Effort estimate

**2 days** (from the `.tji`). This leaf promotes the single off-UI-thread driver
`frame_sync` shipped into an **N-canvas host over one shared `WorkerPool`**: the
identity/lifecycle of "which canvases exist" already lives in L1 `dockmodel` (the
`canvas#N` multi-instance seam, `view_registry.md`), so the work is entirely the
render/app wiring the two predecessors deferred here. The cost is in three places:
(1) the **shared `WorkerPool` flip** — replacing each renderer's per-renderer
inline executor with one host-owned `arbc::WorkerPool` that every canvas's
`InteractiveRenderer` *borrows* (`InteractiveRenderer(WorkerPool&, Clock)`,
`runtime.shared_worker_pool`), the flip both `canvas_view` (D-canvas_view-2) and
`frame_sync` explicitly punted to this leaf; (2) the **N-entry host + one shared
render thread** — a `render::CanvasHost` owning a canvas-id → per-canvas driver map
and a single drive loop that round-robins `step()` over the live entries under a
bounded per-frame budget, so all N tile caches stay confined to that one render
thread (A5, "no new locking"); and (3) the **app-layer fan-out** — one host + one
render thread at the shell, a per-`canvas#N` GL-texture presenter keyed off the
dock's view id, and an edit poke that wakes every live canvas. It deliberately does
**not** give each canvas its own camera (each still frames the whole document
through a default viewport — per-canvas cameras are `editor.cameras.model` /
`look_through`), add pan/zoom (`nav`), or add a user-facing canvas rename (no such
seam exists in the dock model — see D-multi_canvas-5).

## Inherited dependencies

**Settled (from `editor.canvas.frame_sync`, `tasks/00-editor.tji:161-166`, Done
2026-07-18).** The off-UI-thread single-canvas driver exists as
`render::CanvasDriver` — `src/render/ace/render/canvas_driver.hpp` (`class
CanvasDriver` `:41`, ctor `explicit CanvasDriver(arbc::Document&)` `:43`,
`request_resize(int,int)` `:53`, `poke()` `:58`, `stop()` `:63`,
`run(const std::function<bool()>&)` `:69`, `drive_once()` `:76`,
`consume(std::uint64_t&, Srgb8Image&)` `:84`, `published_sequence()` `:89`,
`iterations()` `:94`; the single-producer/single-consumer latest-frame
double-buffer members at `:96-113`) + `src/render/canvas_driver.cpp`. Its per-frame
double-buffer protocol — full-frame publish under a short mutex, monotonic
sequence, non-blocking move-based `consume` (D-frame_sync-3/4) — is **settled and
reused verbatim per canvas**; this leaf does not re-open it. Its owner is
`app::CanvasView` — `src/app/ace/app/canvas_view.hpp` (`class CanvasView` `:30`,
owning `render::CanvasDriver driver_` `:58`, `platform::NativeThreads threads_`
`:59`, `std::unique_ptr<platform::JoinHandle> render_thread_` `:60`, `texture_`
`:72`) + `src/app/canvas_view.cpp` (spawns its own render thread `:10-16`,
`draw_content` resize→consume→upload `:20-57`, `poke()` `:59`, `destroy()`
stop→wake→join→release `:63-76`). **Note:** `CanvasView` today spawns *one render
thread per view* (`canvas_view.hpp:17-29`) — that single-canvas ownership is
superseded here (D-multi_canvas-2).

**Settled (from `editor.canvas.view`, `tasks/00-editor.tji:154-158`, Done
2026-07-18).** The per-viewport render bundle is `render::CanvasRenderer` —
`src/render/ace/render/canvas_renderer.hpp` (ctor `:35`, `resize` `:44`, `step`
`:51`, `image` `:55`, `frames_issued` `:60`) + `src/render/canvas_renderer.cpp`
(the `Impl` `:29` composes `CpuBackend` + `SurfacePool` + `TileCache` (64 MiB
`:27`) + persisted working-space target `Surface` + `InteractiveRenderer` + a
`HostViewport` via the `Document&` ctor; `rebuild()` `:36-72`, `convert()` →
straight-alpha `Srgb8Image` `:77-88`, `step()` `:118-128`). Two hard-coded choices
this leaf parameterizes: the **inline executor** `renderer.emplace(arbc::WorkerPoolConfig{})`
(`:58`, `worker_count 0`) and the **settle-in-one-step budget**
`config.budget = std::chrono::hours(1)` (`:64`). The GL-uploadable value type
`render::Srgb8Image` is `src/render/ace/render/render.hpp:17-21`; the byte-exact
offline reference is `render::render_document_srgb8` (`render.hpp:30`,
`src/render/render.cpp:21-40`).

**Settled (from `editor.dock.view_registry`, and `dockspace`).** Canvas identity
and lifecycle are **already modeled** and tested in L1 `dockmodel` — Canvas is the
one `multi_instance` view type. `src/dockmodel/ace/dockmodel/view_registry.hpp`:
`enum class ViewType { Canvas, … }` (`:19`), `struct ViewDescriptor { …; bool
multi_instance; }` (`:29-34`) with only Canvas `true`
(`src/dockmodel/view_registry.cpp:14-23`), `struct ParsedViewId { ViewType; int
index; }` (`:49-53`), `parse_view_id` (`:60`), and `class ViewRegistry` (`:67-103`)
with `mint_id` (`:72`, `canvas#N`, monotonic non-recycling), `open` (`:81` — a
multi-instance `open` mints a *distinct* `canvas#N` each call), `close` (`:85`),
`reopen` (`:90`), `adopt` (`:97`, re-seeds counters on layout restore). The set of
open views IS `DockLayout::view_ids()` (`src/dockmodel/ace/dockmodel/dockmodel.hpp`,
`DockNode` `:25-46`, `DockLayout` `:53-101`). Layout — *which* canvases are open —
is **local UI state, never `project.arbc`** (D-dockspace-3, D18). Decisions
`D-view-registry-3/4` fix the 8-type catalog and the `slug#N` id scheme; the
view-registry e2e already proves "two distinct `canvas#N` windows coexisting" —
this leaf makes those two windows *render independently*.

**Settled (from `editor.project.app_state`).** `commands::AppState` owns the one
process-lifetime `arbc::Document` — `src/commands/ace/commands/app_state.hpp`
(`class AppState` `:36-101`, `document()` `:45-46`, `document_` `:91`), plus the
**project-level** shared `Selection& selection()` (`:53-54`) that all N canvases
share (D19: a canvas is *only* a camera, carries no selection). Edits go through
`dispatch(AppState&, const Command&)` (`:126`) / `undo` / `redo` (`:134-152`) on the
**UI thread only** (D-frame_sync-2, single writer). The edit-poke seam is wired via
`app_gateway->set_edit_listener([…]{ … })` (`src/app/shell.cpp:266`), today a
single-canvas poke.

**Pending (owned here).**
- The shared `arbc::WorkerPool` flip (`runtime.shared_worker_pool`) — the borrow
  ctor `InteractiveRenderer(WorkerPool&, Clock)`.
- The N-entry `render::CanvasHost` + its single shared drive loop.
- The bounded per-frame interactive budget for fair N-canvas scheduling.
- The app-layer refactor: one host + one render thread; per-`canvas#N` texture
  presenter keyed off the dock view id; edit-poke fan-out.

## What this task is

A5 (`docs/01-architecture.md:84-97`) is normative: "A canvas view is **one
`HostViewport` + `InteractiveRenderer` over the shared `Document`**; multiple
canvases are **multiple renderers sharing one `WorkerPool`** (`runtime.shared_worker_pool`
is exactly this). Each cache stays render-thread-confined, so multi-canvas needs
**no new locking** — 'paint through Viewport ‖ look through Hero' (D18) is two
renderers, two cameras, one document." This leaf realizes exactly that sentence.

Concretely it introduces `render::CanvasHost` (L2), which owns:
- **one `arbc::WorkerPool`**, declared *before* the entries that borrow it
  (lifetime: the pool must outlive every borrowing renderer,
  `interactive.hpp:239-243`);
- **a `canvas#N` → per-canvas driver map** — each entry reuses `frame_sync`'s
  per-canvas `CanvasRenderer` + double-buffer unchanged, except its
  `InteractiveRenderer` now *borrows* the host pool via
  `InteractiveRenderer(WorkerPool&, Clock)` (`interactive.hpp:244`) instead of
  owning an inline executor;
- **one shared drive loop** (`run(should_stop)`) that, on each wake, drives one
  bounded `step()` per live entry and publishes each settled frame into that
  entry's double-buffer.

libarbc mandates this exact shape: "K renderers over 1 pool is the only correct
multi-viewport shape … not interchangeable with K viewports over 1 RENDERER: the
wanted-tile set, the carried damage, the previous time and camera scale … are all
PER-VIEWPORT state, so sharing a renderer cross-contaminates one viewport's frame
into another's — silently, with no crash" (`interactive.hpp:230-237`). So each
canvas keeps its own `CanvasRenderer` (own `HostViewport`, own `TileCache`, own
target `Surface`); only the `WorkerPool` — the leaf-miss executor + async park/wake
substrate — is shared.

At L4 the shell owns **one** `CanvasHost` and spawns **one** render thread (through
`platform::NativeThreads`, A3, exactly as `frame_sync` — the L2 host stays
platform-free, std-sync only, so `render` gains no `platform` edge). The Canvas
view body, registered once for `ViewType::Canvas`, now uses its per-pane `view_id`:
a per-`canvas#N` presenter consumes that entry's latest frame and uploads it to a
per-id GL texture. As `canvas#N` slots open and close in the dock, the host's entry
map is reconciled (add on first appearance, remove when the id leaves
`view_ids()`). One edit poke wakes every live entry.

## Why it needs to be done

`frame_sync` moved rendering off the UI thread but is still single-canvas — the
shell holds exactly one `CanvasView` and registers it as the body for the whole
Canvas *type*, ignoring the per-pane `view_id` (`shell.cpp:215-221`), so every
`canvas#N` pane the dock can already mint would draw the *same* driver. D18's
uniform dockspace ("Canvas is a view → multiple canvases through different cameras
side by side", `docs/00-design.md:479`) and the `look_through` leaf that depends on
this one (a live export preview *beside* the editing viewport) are impossible until
each `canvas#N` renders independently. This leaf is also where A5's shared
`WorkerPool` — deferred through every prior canvas leaf to keep goldens
inline-deterministic — finally lands, so N viewports cost N threads, not N×N
(`interactive.hpp:139-155`).

## Inputs / context

**Governing design docs (normative — the constitution).**
- **A5** `docs/01-architecture.md:84-97` — N observers, one document; N renderers
  share one `WorkerPool`; render-thread-confined caches ⇒ no new locking;
  selection/panels are project-level, not per-canvas. Decisions-log row A5 at
  `:255`.
- **A4** `docs/01-architecture.md:59-82` — the library concurrency contract this
  inherits: single-writer/render-thread-confined cache, leaf-only dispatch, one
  shared `WorkerPool`, one `HousekeepingThread` per `Document`; UI thread submits
  edits, never touches the cache. The data-flow diagram (`:66-79`) already shows
  "shared WorkerPool" feeding "(that canvas's) cache".
- **A6** `docs/01-architecture.md:99-105` — `CpuBackend` tiles → GL textures behind
  the `Backend` seam (each entry's presenter uploads independently).
- **§7 / §8** `docs/01-architecture.md:112-179` — component homes and the
  levelization DAG: `render` is **L2** ("HostViewport/InteractiveRenderer glue ·
  frame-sync · tile→GL", `:155`; may depend on `base project scene gl libarbc`,
  GL-but-not-ImGui, `:172`); L1 core stays ImGui/GL/SDL-free (`:177-179`).
- **§9 / §9.1** `docs/01-architecture.md:181-245` — the universal DoD and the
  offscreen software-GL ASan/TSan lane.
- **D18** `docs/00-design.md:479` — uniform dockspace; Canvas is a multi-instance
  view; paint-through-Viewport beside look-through-Hero. **D19**
  `docs/00-design.md:480` — project-scoped selection/panels; canvases are *only*
  cameras; one process = one project = one `Document`.

**libarbc API surface** (fetched under `build/*/_deps/arbc-src/`).
- `arbc::InteractiveRenderer` — `src/runtime/arbc/runtime/interactive.hpp`: the
  **borrow-a-pool ctor** `explicit InteractiveRenderer(WorkerPool& pool, Clock clock = {})`
  (`:244`); the multi-viewport contract and lifetime rule (`:230-243`); the OWN-pool
  ctor `InteractiveRenderer(WorkerPoolConfig, Clock)` (`:227`, `WorkerPoolConfig{}`
  = inline degenerate, byte-identical); `default_interactive_pool_config()` (`:185`),
  `default_interactive_worker_count()` (`:171`), `k_max_interactive_workers = 2`
  (`:155`); `~InteractiveRenderer` drains its own submissions via
  `d_pool.drain_owner` (`:246-249`); `render_frame(…, budget, …) -> FrameOutcome`
  with `FrameOutcome{ bool schedule_follow_up }` (`:213-215`, `:271-276`) — the
  deadline-bounded pass and the follow-up signal the shared loop round-robins on.
- `arbc::WorkerPool` / `WorkerPoolConfig` — `<arbc/runtime/worker_pool.hpp>`
  (`submit`/`wait_completions`/`drain_owner`/`request_stop` `:170-253`,
  `WorkerPoolConfig` `:131-147`).

**Editor seams this leaf forks / extends.**
- `render::CanvasRenderer` — parameterize the pool (borrow vs inline) and the
  budget (bounded vs settle-fully): `src/render/canvas_renderer.cpp:58,64`.
- `render::CanvasDriver` — reuse the per-canvas double-buffer + `drive_once` +
  `consume` verbatim (`src/render/ace/render/canvas_driver.hpp:76,84`); the single
  drive loop / wake moves up into `CanvasHost` (D-multi_canvas-1).
- `app::CanvasView` — refactor from "one driver + one render thread per view" to a
  thin per-`canvas#N` presenter over a host entry: `src/app/canvas_view.cpp:10-76`.
- `app::shell.cpp` — own one `CanvasHost` + one render thread; make the Canvas body
  use its `view_id` (`:215-221`); fan the edit-poke listener out to the host
  (`:266`); reconcile the entry map against `DockLayout::view_ids()`.
- `dockmodel::ViewRegistry` (`canvas#N` ids) and the dock per-view Begin/End loop
  (`src/dock/dock.cpp:385-398`, tool-rail Views launcher `:258-263`) — consumed
  unchanged; they already mint/close the ids this leaf keys on.

**Predecessor refinements** (style + decision continuity):
`tasks/refinements/editor/frame_sync.md` (double-buffer protocol, off-thread
driver, ASan/TSan target — the shape generalized here),
`tasks/refinements/editor/canvas_view.md` (D-canvas_view-2 inline-executor
determinism, the golden reference),
`tasks/refinements/editor/view_registry.md` (D-view-registry-3/4 `canvas#N`
identity), `tasks/refinements/editor/app_state.md` (D19 project-level selection).

## Constraints / requirements

1. **N renderers, one pool — never one renderer for N viewports (A5).** Each
   `canvas#N` keeps its own `CanvasRenderer` (own `HostViewport` / `TileCache` /
   target `Surface`); only one host-owned `arbc::WorkerPool` is shared, borrowed via
   `InteractiveRenderer(WorkerPool&, Clock)`. Sharing a renderer is a silent
   cross-contamination bug (`interactive.hpp:230-237`) and is forbidden.

2. **Pool lifetime — declare before borrowers (`interactive.hpp:239-243`).** The
   `CanvasHost`'s `WorkerPool` member must be declared (and destroyed) so it
   outlives every entry that borrows it; `~InteractiveRenderer` drains into the
   pool. A pool that dies first is a use-after-free. The entry map is torn down
   before the pool.

3. **No new locking; caches render-thread-confined (A4/A5).** Every entry's
   `HostViewport` / `InteractiveRenderer` / `TileCache` / target `Surface` is
   constructed, resized, and `step()`-driven **only** on the single shared render
   thread. The UI thread never touches any cache — it only `request_resize`s,
   `poke`s, and `consume`s per entry (the settled double-buffer protocol).

4. **Single writer, poked across N readers (D-frame_sync-2).** `dispatch` / `undo`
   / `redo` stay UI-thread-only; the render thread is a pure `pin()` reader. One
   edit pokes *all* live entries so every canvas re-renders the new revision.

5. **One shared render thread (D-multi_canvas-2), spawned at L4 (A3).** The host's
   `run(should_stop)` loop is spawned once through `platform::NativeThreads` at the
   shell; `render` stays platform-free (std `<mutex>`/`<condition_variable>`/`<atomic>`
   only), so **no `render → platform` DAG edge** (D-frame_sync-1). Teardown is
   deterministic: stop → wake → join the one thread, then tear down entries, then
   the pool, then release all GL textures (on the UI thread, live context).

6. **Bounded per-frame budget for fair scheduling (D-multi_canvas-3).** The live
   path drives each entry with a *bounded* interactive budget (replacing
   `canvas_renderer.cpp:64`'s settle-in-one-step hour budget) so one heavy canvas
   cannot starve another on the shared thread; unsettled entries
   (`FrameOutcome::schedule_follow_up`) are re-driven next cycle. Deterministic
   goldens keep the inline executor + settle-fully drive (D-multi_canvas-4).

7. **Lifecycle keyed off the dock (D18/D19, D-multi_canvas-5).** Add an entry when a
   `canvas#N` first appears; remove it (freeing its cache on the render thread, its
   texture on the UI thread) when the id leaves `DockLayout::view_ids()`. No canvas
   state is persisted in `project.arbc`. There is no rename operation.

8. **Levelization stays clean (§8).** No new component and no new DAG edge:
   `CanvasHost` lives in existing L2 `render`; `<arbc/runtime/worker_pool.hpp>` is
   under the already-whitelisted `arbc` external category
   (`scripts/check_levels.py`). L1 core gains no ImGui/GL/SDL include.

## Acceptance criteria

Instantiating the universal DoD (`docs/01-architecture.md:199-203`) for this leaf:

- **Levelization** — `check_levels` clean. `render::CanvasHost` is added to the
  existing L2 `render` component (no new component, no new edge); the thread spawn
  stays in L4 `app` through `platform::NativeThreads`. Confirm `scripts/gate`'s
  level lint passes and the L1 core still holds no ImGui/GL/SDL include.

- **Catch2 unit (L2 logic, headless, the bulk)** — a new `tests/canvas_host_test.cpp`
  (joined to `ace_tests`, mirroring `tests/canvas_driver_test.cpp`), driven with an
  **inline** pool config (`WorkerPoolConfig{}`) for determinism, asserting: (a) two
  entries `canvas#1`/`canvas#2` over one `Document` each publish an independent
  frame with an **independent** monotonic sequence; (b) `consume(id, …)` returns each
  entry's own latest frame and is a no-op at the same sequence; (c) `request_resize`
  is per-entry (resizing one leaves the other's size untouched); (d) one edit +
  `poke()` advances **both** entries' sequences (N observers, one writer); (e)
  `add`/`remove` mutate the live set — a removed id is no longer served and leaks
  nothing; (f) the two entries **borrow one pool** (a single `WorkerPool` object,
  not two). Coverage ≥ 90% on changed lines (`diff-cover --fail-under=90`).

- **Golden (rendered output, byte-exact)** — a hosted canvas entry, driven with the
  inline config + settle-fully drive, is **byte-identical** to
  `render::render_document_srgb8`, reusing `tests/goldens/canvas_view_64x64.rgba8`
  via `ace_test::compare_golden` (`tests/golden_support.hpp:36`) with **no
  tolerance** — proving the shared-pool wiring (in its inline-degenerate form)
  preserves the D10/D-canvas_view-2 byte-exactness. A second entry at the same size
  yields the identical bytes (two canvases, one document → one image).

- **ImGui Test Engine e2e (UI, offscreen software-GL)** — a new
  `tests/multi_canvas_e2e_test.cpp` (joined to `ace_shell_test`, modeled on
  `tests/canvas_view_e2e_test.cpp` / `tests/history_e2e_test.cpp`): open a second
  canvas through the tool-rail Views launcher (mints `canvas#2`) and assert **two**
  distinct canvas DockNodes coexist, **both** with `frames_issued() >= 1` and both
  panes distinct from the shell clear color (`glReadPixels`); an edit through the
  gateway advances **both** panes' sequences (fan-out poke); closing `canvas#2`
  removes its host entry, leaves `canvas#1` live, and does not crash.

- **ASan/TSan (threading — the escalated concurrency target)** — this leaf is the
  first to exercise a **real** shared `WorkerPool` (worker threads), not the inline
  degenerate. At least one sanitizer test constructs the host with
  `default_interactive_pool_config()` and runs the full add→render→edit→remove→
  teardown lifecycle: N renderers ‖ one shared `WorkerPool` ‖ one shared render
  thread ‖ UI writer+poke ‖ `HousekeepingThread` ‖ N double-buffer handoffs → clean
  under the `asan` (offscreen SDL + `llvmpipe`, §9.1) and `tsan` presets, with a
  clean stop→join and no leaked entry/texture. Any residual Mesa-driver leak is
  covered by the existing scoped `tests/lsan.supp`, not by new suppressions.

- **Format + build clean** across the standard presets; `scripts/gate` green.

**No new WBS leaf is deferred.** Per-canvas cameras (`editor.cameras.model`,
`look_through`) and pan/zoom (`editor.canvas.nav`) are already independent WBS
leaves that build on this one; nothing here spawns a new task. (A possible future
optimization — moving from one shared render thread to N — is *not* encoded as a
task; it is a profile-gated judgment call surfaced to the parking lot, per
D-multi_canvas-2.)

## Decisions

- **D-multi_canvas-1 — `render::CanvasHost` (L2) owns the pool + a `canvas#N` →
  driver map + one drive loop; the per-canvas double-buffer is `frame_sync`'s,
  reused verbatim.** *Rationale:* A5 names exactly this shape; keeping the settled
  publish/consume/sequence protocol per entry honors the orchestrator's "don't
  re-open the double-buffer protocol" while lifting only the *loop + wake* to the
  collection level. *Alternative rejected:* one renderer driving N viewports —
  forbidden by libarbc (`interactive.hpp:230-237`): per-viewport state
  cross-contaminates silently. *Alternative rejected:* a bespoke lock-free
  multi-frame ring — the mutex-guarded latest-frame slot is already TSan-clean and
  never blocks the UI thread (D-frame_sync-4); N independent slots inherit that for
  free. **No doc delta required** — A5 already legislates N-renderers/one-pool.

- **D-multi_canvas-2 — one shared render thread for all canvases, superseding
  `frame_sync`'s render-thread-per-`CanvasView`.** *Rationale:* (a) the shared
  `WorkerPool` is then driven and drained by a *single* owner thread, sidestepping
  any question of concurrent multi-thread `submit`/`drain_owner` into one pool — the
  conservative reading of the borrowed-pool contract; (b) one spawn/join is a
  smaller, simpler TSan surface than N threads; (c) it matches the orchestrator's
  steer. A5 mandates a shared *WorkerPool* but is **silent on render-thread count**,
  so this does not deviate from the constitution; `frame_sync`'s per-view thread was
  a single-canvas implementation detail (a task-level decision, not a `D`/`A` row),
  which a follow-on task is free to supersede. *Alternative rejected:* N render
  threads (the natural `frame_sync` replication) — more threads, N concurrent pool
  submitters (a contract libarbc does not explicitly promise), and larger
  sanitizer/lifecycle surface, for a parallelism win that the bounded round-robin
  budget (D-multi_canvas-3) plus the shared worker pool already largely deliver at
  the realistic N (two or three canvases). The head-of-line risk is bounded by
  giving each entry a deadline-limited slice per cycle, not a run-to-settle pass.
  **No doc delta required.**

- **D-multi_canvas-3 — the live path drives a bounded interactive budget; the
  golden path settles fully.** *Rationale:* `canvas_renderer.cpp:64`'s
  `hours(1)` budget settles a canvas in one `step()` — correct for one off-thread
  canvas, but on a shared thread it lets a heavy canvas starve its neighbor. The
  deadline-bounded interactive loop libarbc is built for (`interactive.hpp:256-276`,
  `FrameOutcome::schedule_follow_up`) gives each entry a bounded slice per cycle and
  re-drives the unsettled ones next cycle. Byte-exact goldens keep the settle-fully
  drive (they compare a fully-settled frame). *Alternative rejected:* keep the hour
  budget live — reintroduces head-of-line blocking the moment a second canvas is
  heavy. **No doc delta required.**

- **D-multi_canvas-4 — the pool config is injectable; the app uses real workers,
  tests use the inline degenerate.** *Rationale:* `WorkerPoolConfig{}` (inline,
  `worker_count 0`) is byte-identical to the parallel path (`interactive.hpp:222-226`),
  so a test can borrow an inline pool for a deterministic golden while the app
  borrows a `default_interactive_pool_config()` pool (real workers, capped at 2,
  `interactive.hpp:155`) — **one code path**, determinism as a config choice, not a
  code fork. This is how this leaf discharges D-canvas_view-2's "no `WorkerPool` yet,
  for golden determinism" without abandoning determinism. *Alternative rejected:* a
  separate inline-only test renderer distinct from the shipped path — would leave the
  real shared-pool wiring golden-untested. **No doc delta required.**

- **D-multi_canvas-5 — lifecycle is add/remove keyed off `canvas#N`; there is no
  rename.** *Rationale:* the dock model already mints/closes `canvas#N` ids
  (`view_registry.md` D-view-registry-4) as **local UI state** (D-dockspace-3), and
  the id is the stable identity; the app reconciles host entries against
  `DockLayout::view_ids()` each frame (add on first appearance, remove when the id
  leaves). No user-facing canvas title/rename field exists anywhere in `dockmodel`,
  and D19 makes a canvas *only* a camera — so "rename" (named in the `.tji` note's
  generic "add/remove/rename") is a **non-operation** here, not deferred work: no
  seam needs it until (if ever) canvas titling is designed. *Alternative rejected:*
  minting a UI-visible canvas name/rename now — invents a `dockmodel` seam no design
  row calls for, and persists nothing (layout is local UI state). **No doc delta
  required.**

## Open questions

(none — all decided. The one-shared-thread-vs-N-threads question is settled here as
one shared thread per D-multi_canvas-2; revisiting it is a profile-gated judgment
call for the parking lot, not a WBS task.)

## Status

**Done** — 2026-07-18.

- Introduced `render::CanvasHost` (L2) in `src/render/ace/render/canvas_host.hpp` and `src/render/canvas_host.cpp`: owns one `arbc::WorkerPool` + a `canvas#N` → `CanvasRenderer`/`CanvasDriver` entry map + one bounded round-robin drive loop (`run(should_stop)`).
- Parameterized `render::CanvasRenderer` with an injectable borrow-pool ctor and a bounded-budget mode (`src/render/ace/render/canvas_renderer.hpp`, `src/render/canvas_renderer.cpp`); `step()` now returns `schedule_follow_up` for the host's re-drive loop.
- Refactored `app::CanvasView` (`src/app/ace/app/canvas_view.hpp`, `src/app/canvas_view.cpp`) from one-driver-per-view to a thin per-`canvas#N` presenter over the host; reconcile loop adds/removes entries as dock view ids change.
- Updated `src/app/shell.cpp` to own one `CanvasHost` + one render thread (via `platform::NativeThreads`), use view-id-keyed canvas bodies, and fan the edit-poke listener across all live host entries.
- Added Catch2 unit tests in `tests/canvas_host_test.cpp` (8 cases: independent sequences, per-entry consume/resize, one-edit-pokes-both, add/remove live-set, two-entries-one-pool, byte-exact golden reusing `tests/goldens/canvas_view_64x64.rgba8`, real-shared-pool ASan/TSan lifecycle).
- Added ImGui Test Engine e2e in `tests/multi_canvas_e2e_test.cpp`: two docked canvases both live + lit, fan-out edit advances both, close-one-leaves-other.
- Adapted `tests/canvas_view_e2e_test.cpp` to the view-id-keyed `CanvasView` API with async-settle wait.
- `CMakeLists.txt` updated to register `canvas_host.cpp` and the two new test targets.
