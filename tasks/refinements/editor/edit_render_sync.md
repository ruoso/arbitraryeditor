# editor.canvas.edit_render_sync — Serialize UI-thread Document edits against the render-read path (TSAN)

## TaskJuggler entry

`tasks/00-editor.tji:195-200` — `task edit_render_sync` under `editor.canvas`.
Effort `2d`, `allocate team`, `depends !multi_canvas` (i.e.
`editor.canvas.multi_canvas`). The `note` (`:199`):

> Pre-existing TSAN race: render thread's `for_each_content` (CanvasHost
> render_frame) reads arbc's writer-thread-owned `d_contents` while an off-thread
> `damage()`/`add_content` mutates it; no synchronization. Surfaced under
> container CPU contention in CI. Serialize writer-thread Document edits against
> the render-read path (arbc dep or CanvasHost edit/render contract).
> Source-of-debt: `tasks/refinements/cameras/model.md`. Design:
> `docs/01-architecture.md A4/A5`.

This is a **debt closer** for a pre-existing race, not a feature leaf: nothing
downstream `depends` on it. It was chartered from three places — the
`cameras/model` iteration that surfaced it under CI contention (its Source-of-debt
line), the `1d05ff4` fixer commit that shipped the *partial* fix and named this
task as the owner of the residual (`shell.cpp:270` "retains the latent race … is
tracked under `editor.canvas.edit_render_sync`"), and `nested_composition_binding`,
whose Constraint 6 and D-nested_composition_binding-4 explicitly hand the
concurrent-edit-during-render race to this task as "one contract owned by one
task, not a per-call-site lock."

## Effort estimate

**2 days** (from the `.tji`). The serialization *primitive* already exists and is
unit-tested: `1d05ff4` added `CanvasHost::apply_edit(edit_fn)` — which runs the
mutation under a `doc_mu` mutex (`src/render/canvas_host.cpp:150-165`) — and made
`drive_once` hold that same `doc_mu` across its whole render iteration
(`:197-202`). What is missing is that **the shipped edit path does not use it**:
the app wires a post-hoc poke (`shell.cpp:270`
`set_edit_listener([&canvas]() { canvas.poke(); })`), so production `undo`/`redo`
mutate the `Document` *before* — and unserialized against — the render read. The
2 days are: (1) widen the gateway's edit seam from a fire-after poke into an
**edit-serializing runner** bound to `CanvasHost::apply_edit`, so every UI-thread
mutation runs inside `doc_mu`; (2) pin down *why* a lock is genuinely required
here (the interactive render read walks the **live** model via `bind_operators` /
`for_each_content`, not a `pin()` immutable snapshot — qualifying
D-frame_sync-2's "no lock on the `Document`" claim); and (3) the **TSan lifecycle
test** that reproduces the concurrent edit-during-render race on the real-pool
`CanvasHost` path and proves it gone. The bulk of the effort is the TSan
reproduction + the seam wiring, not new machinery.

## Inherited dependencies

**Settled (from `editor.canvas.multi_canvas`, `tasks/00-editor.tji:167-171`, Done
2026-07-18).** The production canvas path is `render::CanvasHost` (L2 `render`,
`src/render/ace/render/canvas_host.hpp:51`) — one shared `arbc::WorkerPool`, a
`canvas#N` → per-entry `CanvasRenderer` map, and **one** shared render thread
looping `drive_once` (`src/render/canvas_host.cpp:183-320`, loop at `:322-341`).
Critically, this path runs a **real** worker pool on a bounded per-frame budget
(`k_default_frame_budget{8}`ms), *not* the inline `hours(1)` settle-fully executor
`CanvasDriver` uses — so the render read genuinely overlaps a UI-thread edit in
time. **The race is live on the `CanvasHost` path** (`multi_canvas.md:259-260`
established single-writer / N-reader poke fan-out but relied on `pin()`; see
D-edit_render_sync-2).

**Settled (from `editor.canvas.frame_sync`, `:161-166`, Done 2026-07-18).** The
writer stays the UI thread (`commands::dispatch`/`undo`/`redo`), the render thread
is off-UI, and an edit **pokes** the driver to re-render (D-frame_sync-2). This
leaf **qualifies** frame_sync's claim that keeping the writer on the UI thread
"needs no lock on the `Document`… libarbc's versioned `pin()` gives lock-free
concurrent read" (`frame_sync.md:232-234`): that holds for the *journal* read and
for the composited *state snapshot*, but **not** for the operator-binding walk the
interactive `render_frame` performs over the live `d_contents` (see Inputs). The
poke seam is the exact wiring this leaf replaces with a serializing runner.

**Settled (the `1d05ff4` partial fix — `editor.cameras.workspace_reopen_slab`).**
A fixer sub-agent added the serialization primitive but wired it only for the
**test-local** `damage()` calls; the commit body names the residual explicitly:
"The shipped edit path in `shell.cpp:270` retains the latent race and is tracked
under `editor.canvas.edit_render_sync`." Artifacts already on disk:
`CanvasHost::apply_edit` (`src/render/canvas_host.cpp:150`), `std::mutex doc_mu`
(`:90`), the `drive_once` `doc_mu` hold (`:202`), and the header contract warning
that "mutate the document, then `poke()`" is racy
(`src/render/ace/render/canvas_host.hpp:92-105`). `apply_edit` is unit-tested in
`tests/canvas_host_test.cpp` but **never called by the app**.

**Settled (from `editor.canvas.nested_composition_binding`, `:208-214`, Done
2026-07-19).** Constraint 6 / D-nested_composition_binding-4 declined to add a
per-call-site lock around the settle, deferring to *this* task as the single owner
of "serialize writer-thread `Document` edits against the render-read path." It
also flagged that `HostViewport::settle_external_loads` is itself a
**render-thread** writer-publish — relevant to the writer-thread-identity note in
Open questions.

**Settled (from `editor.cameras.model`, `:219-224`, Done 2026-07-18).** The
Source-of-debt: it introduced camera create/rename as single-writer UI-thread
`transact` mutations (`src/scene/camera.cpp:502-506`, `:528-530`) and explicitly
delegated all real-concurrency coverage to `frame_sync`/`multi_canvas`
(`model.md:299-303`) — leaving the concurrent edit-vs-render read uncovered, which
CI contention then surfaced.

**Pending (owned here).** None downstream. This leaf closes the debt in full.

## What this task is

Route **every UI-thread `Document` mutation through `CanvasHost::apply_edit`'s
`doc_mu` window**, so a production edit is mutually excluded with the render
thread's per-frame document read. Today the render thread reads the live model on
every frame — `CanvasHost::drive_once` (`canvas_host.cpp:276`) →
`CanvasRenderer::step()` → arbc `HostViewport::step()` →
`InteractiveRenderer::render_frame()` → `bind_operators()` →
`Document::for_each_content()` walking the writer-owned `d_contents` side-map —
while the UI thread runs `commands::undo`/`redo` (and, as tools/panels land,
`commands::dispatch`) mutating that same `d_contents`, with **no lock between
them**. The concrete change replaces the fire-after poke seam
(`shell.cpp:270`, `AppProjectGateway::undo/redo` at `project_gateway.cpp:88,96`)
with an **edit-serializing runner** the shell binds to
`[&canvas](const auto& edit){ canvas.apply_edit(edit); }`: the gateway hands its
mutation closure to the runner, `apply_edit` takes `doc_mu`, runs the mutation,
and pokes — so the write happens *inside* the same lock `drive_once` holds for the
read. It retires the latent race at `shell.cpp:270` and establishes the contract
that **all** UI-thread edit sites funnel through `apply_edit` (one contract, one
task). No arbc change; no pixel change; no new component or DAG edge.

## Why it needs to be done

The editor ships a **known TSAN data race** on its production canvas path: the
render thread walks `d_contents` while the UI thread mutates it, unsynchronized.
It is not hypothetical — it surfaced under container CPU contention in CI during
the `cameras/model` iteration, was patched *only* for test-local edits in
`1d05ff4`, and was deferred here for the shipped path. A data race is undefined
behavior: under load it can crash the editor mid-edit (a torn `d_contents`
iteration in `for_each_content`), and it makes the `tsan` preset unsound as a gate
— every future concurrency leaf inherits a false-positive-or-real ambiguity until
this is closed. `frame_sync` explicitly did **not** claim the model-read-vs-edit
path clean (its TSan row covers "the double-buffer slot and the resize/poke
signals," `frame_sync.md:298-306`); `nested_composition_binding` and
`cameras/model` both routed the guarantee here. Closing it makes the single-writer
/ off-thread-render architecture (A4) actually race-free on the shipped path,
which is the precondition every downstream edit-producing leaf (`tool_rail`,
`cells`, panels, cameras UI, `export`) silently depends on.

## Inputs / context

**Governing design docs (normative — the constitution).**
- **Arch A4** — `docs/01-architecture.md:59-82` (§4), log restatement `:254`. The
  concurrency contract this leaf enforces on the shipped path: "the editor adopts
  `libarbc`'s concurrency rules **verbatim** … the tile cache is single-writer /
  render-thread-confined … one `HousekeepingThread` per `Document`." The data-flow
  rule (`:81-82`): "Edits flow UI → writer → damage → renderers; frames flow
  renderers → UI." A4 mandates adopting the library's contract *as given* — arbc
  is single-writer with **no internal lock** on `d_contents` by design
  (`canvas_host.hpp:92-93`), which is exactly why the serialization must be
  editor-side (D-edit_render_sync-1).
- **Arch A5** — `docs/01-architecture.md:84-97` (§5): N renderers over one
  `Document` sharing one `WorkerPool`, "no new locking." This leaf adds **no
  per-canvas lock** — it uses the single already-shipped `doc_mu` on the one
  `CanvasHost`, which is where the one writer meets the N readers, honoring A5's
  "no new locking" (one document lock, not one-per-canvas).
- **§8 levelization** — `docs/01-architecture.md:144-179`; `CMakeLists.txt:169-195`.
  `render` is **L2** ("HostViewport/InteractiveRenderer glue · frame-sync ·
  tile→GL", `:155`), may depend on base/project/scene/gl/libarbc, **GL not ImGui**.
  `app` is **L4** (everything). `doc_mu`/`apply_edit` already live in `render`
  (std `<mutex>`); the runner wiring lives in `app`. No L1 change, no new edge.
- **§9 DoD** — `docs/01-architecture.md:181-208`, §9.1 offscreen ASan lane
  `:210-245`. §9 line 190: "Threading & smoke — **UI↔driver handoff** … ASan/TSan."
  This leaf is squarely that lane — it is the task that finally makes the
  edit-vs-render read path TSan-clean.

**libarbc API surface (fetched `v0.1.0` under `build/*/_deps/arbc-src/`; consumed
via `FetchContent`, `CMakeLists.txt:12-37`, linked `arbc::arbc`). The read path is
entirely arbc-internal — `Document`/`for_each_content`/`d_contents`/`render_frame`
are library-owned, not editor code.**
- `<arbc/runtime/document.hpp>` — `Model::Transaction transact(std::string = {})`
  **WRITER-THREAD ONLY** (`:178`); `void for_each_content(...)` (`:268`, `:274`;
  defs `document.cpp:237,243`) — "the runtime operator binder walks these," a
  read-only walk over the writer-owned `d_contents` side-map; `add_content(...)`
  (`document.cpp:114`, `d_contents.emplace(...)`) the writer mutation;
  `DocStatePtr pin()` (`:261`) the immutable snapshot that covers the *state*, not
  the operator-binding walk.
- `<arbc/runtime/interactive.hpp>` / `interactive.cpp` — `render_frame(...)`
  (`interactive.cpp:216`) calls `bind_operators(*binding.document, …)`
  (`interactive.cpp:406`), which calls
  `document.for_each_content([&](Content* c){ walk(c); })`
  (`operator_binding.cpp:166`) — **the render-thread read that races the writer.**
- `<arbc/runtime/host_viewport.hpp>` — `HostViewport::step()` reaches the walk via
  `d_renderer.render_frame(*state, …)` (`host_viewport.cpp:213`);
  `settle_external_loads` is a **render-thread writer-publish** (see Open
  questions).

**Editor seams this leaf changes / relies on.**
- `src/render/canvas_host.cpp:150-165` — `CanvasHost::apply_edit(const
  std::function<void()>&)`: takes `doc_mu` (`:155`), runs the edit, sets `dirty`,
  notifies. **The race-free replacement for mutate-then-poke — already built,
  needs to be called by the app.**
- `src/render/canvas_host.cpp:90` — `std::mutex doc_mu`; `:197-202` — `drive_once`
  holds `doc_mu` across the whole render iteration (the read side, already done);
  `:167-173` — `poke()` (takes only `mu`, **not** `doc_mu` — the racy fast path).
- `src/render/ace/render/canvas_host.hpp:92-105` — the header contract that names
  "mutate the document, then `poke()`" as unsafe and `apply_edit` as the safe seam.
- `src/app/shell.cpp:270` — `app_gateway->set_edit_listener([&canvas]() {
  canvas.poke(); });` — **the shipped racy wiring to replace.**
- `src/app/project_gateway.cpp:82-105` — `AppProjectGateway::undo()`/`redo()`
  mutate via `commands::undo/redo(app_state_)` then fire `on_edit_`
  (`:88-91,96-100`); `set_edit_listener` (`:103-105`). This is where the mutation
  closure must be handed to the runner.
- `src/app/canvas_view.cpp:126` — `CanvasView::poke()` → `host_.poke()`; `:31` —
  the render thread spawn (`threads_.spawn([this]{ host_.run(...); })`). Adds a
  `CanvasView::apply_edit`/runner-forwarding method paralleling `poke()`.
- `src/dock/dock.cpp:215,221` — the Ctrl+Z / Ctrl+Y chord → `gateway.undo()`/
  `redo()`, the shipped production edit trigger.
- `src/commands/app_state.cpp:55` — `commands::dispatch(AppState&, Command)`, the
  general UI-thread mutation seam future edit leaves use — the contract requires it
  to funnel through the runner when a UI call site lands.

**Predecessor refinements** (style + decision continuity):
`tasks/refinements/editor/frame_sync.md` (writer-on-UI-thread + poke seam; the
"no lock via `pin()`" claim this leaf qualifies),
`tasks/refinements/editor/multi_canvas.md` (the `CanvasHost` real-shared-pool
bounded-budget path where the race is live),
`tasks/refinements/editor/nested_composition_binding.md` (Constraint 6 / D-4
charter this race here),
`tasks/refinements/cameras/model.md` (Source-of-debt; single-writer mutations,
concurrency delegated onward).

## Constraints / requirements

1. **Every UI-thread `Document` mutation runs inside `CanvasHost::apply_edit`
   (`doc_mu`).** No shipped code path may mutate the `Document` and then merely
   `poke()`. The shipped sites — `AppProjectGateway::undo()`/`redo()` (the dock
   chord `dock.cpp:215,221`) — must run their `commands::undo/redo` mutation as
   the closure passed to `apply_edit`. The general `commands::dispatch` seam
   inherits the same rule as UI call sites land (one contract, one task).
2. **Editor-side serialization only — no arbc change.** A4 mandates adopting
   libarbc's contract verbatim; arbc `v0.1.0` is single-writer with no internal
   `d_contents` lock by design, and the dependency is pinned via `FetchContent`.
   The lock lives in the editor's `CanvasHost` (the arbc "edit/render contract"
   option the `.tji` note names), never a fork of arbc.
3. **The read side is already correct; this leaf is the write side.** `drive_once`
   holds `doc_mu` across the whole iteration (including any render-thread settle
   write) — do not weaken that. The deliverable is bringing the *writers* under the
   same lock.
4. **Behavior- and pixel-preserving.** No golden changes: the serialization
   reorders nothing observable — an edit still re-renders and still pokes all live
   entries (`multi_canvas` fan-out). A no-op undo/redo (`moved == false`) must
   remain safe (an unconditional post-edit poke is a harmless wake; preserve the
   existing "poke only when moved" where cheap, but correctness must not depend on
   it).
5. **Levelization (§8): no new component, no new DAG edge.** `apply_edit`/`doc_mu`
   stay in L2 `render` (std `<mutex>` only, no `platform`/ImGui/GL added); the
   runner is bound in L4 `app` (which already depends on `render` and `commands`).
   L1 core gains nothing. `check_levels` stays clean.
6. **Deadlock-free lock order preserved.** `apply_edit` takes `doc_mu` only;
   `drive_once` takes `doc_mu` OUTER then re-acquires `mu` for the snapshot/publish
   (`canvas_host.cpp:197-202`). The runner must not introduce a second lock across
   the `apply_edit` call (e.g. no UI-side lock held while calling into
   `apply_edit`), so the established one-way order (`doc_mu` ⊃ `mu`) is never
   crossed.

## Acceptance criteria

Instantiating the universal DoD (`docs/01-architecture.md:199-203`) for this leaf;
`scripts/gate` green (check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean).** No new component, no new DAG edge; the
  lock stays in L2 `render`, the runner binds in L4 `app`; no ImGui/GL/SDL include
  added to L1 or to `render`. `scripts/check_levels.py` passes. Primary structural
  assertion.

- **Catch2 L2 unit (headless, GL-free; extending `tests/canvas_host_test.cpp` and
  `tests/app_project_gateway_test.cpp`, joined to `ace_tests`,
  `CMakeLists.txt:228`).** Inline pool config `WorkerPoolConfig{}` for determinism:
  - **`apply_edit` mutual-exclusion contract (`CanvasHost`):** an `apply_edit`
    whose closure sets a flag, asserted to run while no `drive_once` iteration is
    in flight — i.e. an instrumented edit + drive interleaving proves the mutation
    body and the render read never overlap (a shared "inside-critical-section"
    counter that must never exceed 1). This extends the existing `apply_edit` unit
    cases from `1d05ff4`.
  - **Gateway routes edits through the runner, not a bare poke:** with a **fake
    edit-runner** injected into `AppProjectGateway`, `undo()`/`redo()` invoke their
    `commands::undo/redo` mutation *inside* the runner (recorded ordering: runner
    entered → mutation ran → runner exited), and a no-op undo (`moved == false`)
    behaves identically to today (harmless, no crash). This is the L1/L4 proof that
    the shipped path now serializes.
  - Coverage ≥ 90% on changed lines (`diff-cover --fail-under=90`, `coverage`
    preset).

- **Golden — not added here (justified).** This leaf changes *only* synchronization,
  not the render path or any pixel: the existing byte-exact goldens
  (`tests/goldens/canvas_view_64x64.rgba8`, the `canvas_host` goldens, via
  `ace_test::compare_golden`, `tests/golden_support.hpp:36`) pass **unchanged** —
  that they still pass is the assertion that serialization perturbs no bytes. This
  is the justified exception to "rendered output gets a golden"
  (`docs/01-architecture.md §9`), not the default.

- **UI e2e — ImGui Test Engine (offscreen software-GL; `ace_shell_test`).** In
  `tests/canvas_view_e2e_test.cpp` (and/or `tests/multi_canvas_e2e_test.cpp`):
  drive an edit through the **real production trigger** — the Ctrl+Z dock chord
  (`dock.cpp:215`) → `AppProjectGateway::undo()` → runner → `apply_edit` →
  off-thread re-render — and assert the canvas advances to a new frame (published
  sequence advances / pixels change via `glReadPixels`), proving the serialized
  path is functionally identical to the pre-fix poke path end-to-end. Assert clean
  stop→join on teardown.

- **ASan/TSan — the designated deliverable for this leaf.** Under the `tsan`
  preset, a lifecycle **reproduction** test on the **real-pool `CanvasHost` path**
  (`default_interactive_pool_config()` — worker threads, bounded budget, *not* the
  inline executor): the UI thread submits a stream of edits (`undo`/`redo` and
  `add_content`/`transact`) through the gateway/`apply_edit` **concurrently with**
  the render thread looping `drive_once` → `for_each_content`, over many
  iterations to give TSan a real overlap window (this is the interleaving CI
  contention hit). The run is **data-race-clean** — specifically no race on
  `d_contents` between the writer mutation and the render read. A companion note in
  the test documents that the same interleaving *without* the `apply_edit` routing
  (the pre-fix `poke()`-only path) is what TSan flagged, so the test is a genuine
  regression guard, not a tautology. Also runs clean under `asan` (offscreen
  SDL + `llvmpipe`, §9.1; residual Mesa leaks via existing `tests/lsan.supp`). This
  is the "UI↔driver handoff" TSan scope of §9 (`:190`).

- **Format + build clean** across the standard presets; `scripts/gate` green.

**No new WBS leaf is deferred by this refinement.** The fix is self-contained —
wiring the shipped edit path through the already-built `apply_edit`/`doc_mu` seam.
The contract that *future* edit-producing leaves (`editor.dock.tool_rail`'s
per-tool dispatch, cells/selection, panels, cameras UI, `editor.cameras.export`)
funnel their UI-thread mutations through `apply_edit` is a **constraint those
leaves inherit**, not separable work minted here — encoding it as a task would be
an un-closeable "keep using the seam" leaf. One latent arbc-contract question is
**surfaced to the parking lot** (human/library judgment, not WBS work); see Open
questions.

## Decisions

- **D-edit_render_sync-1 — Serialize on the editor side via the existing `doc_mu`,
  never inside arbc.** The write side runs under `CanvasHost::apply_edit`'s
  `doc_mu`, the same lock `drive_once` already holds for the read — the "CanvasHost
  edit/render contract" the `.tji` note names. *Rationale:* the primitive is built,
  unit-tested, and correct on the read side (`1d05ff4`); production edits simply
  need to use it. A4 mandates adopting libarbc's concurrency rules **verbatim** —
  arbc `v0.1.0` is single-writer with no internal `d_contents` lock *by design*
  (`canvas_host.hpp:92-93`), and the dependency is pinned via `FetchContent`
  (`CMakeLists.txt:25`). *Alternative rejected:* push a lock into arbc / bump the
  arbc version (the "arbc dep" half of the note). Rejected — it forks a pinned
  dependency, contradicts A4's "adopt the library's contract verbatim," and
  duplicates a serialization seam the editor already owns and tests. **No doc delta
  required** — no new dependency, no new component, no new DAG edge; `doc_mu` is
  wholly within existing L2 `render`.

- **D-edit_render_sync-2 — Widen the gateway's edit seam from a post-hoc poke into
  an edit-serializing runner; qualify D-frame_sync-2's "no lock" claim.** Replace
  `set_edit_listener(std::function<void()>)` (fire *after* the mutation) with a
  runner `std::function<void(const std::function<void()>&)>` the shell binds to
  `[&canvas](const auto& edit){ canvas.apply_edit(edit); }`; the gateway hands its
  `commands::undo/redo` mutation *as the closure*, so the write happens inside
  `doc_mu`. *Rationale:* a *lock* — not `pin()` — is genuinely required here.
  frame_sync (D-frame_sync-2, `frame_sync.md:232-234`) argued the writer-on-UI-
  thread design "needs no lock on the `Document`" because `pin()` gives lock-free
  reads; that holds for the journal read and the composited *state snapshot*, but
  the interactive `render_frame` **binds operators over the live model** —
  `bind_operators` → `Document::for_each_content` walks the writer-owned
  `d_contents` (`operator_binding.cpp:166`, `interactive.cpp:406`), not an
  immutable pinned copy — so a concurrent `add_content`/`transact` races it. The
  runner is the minimal seam that funnels **all** UI-thread edits through the one
  lock (nested_composition_binding D-4's "one contract owned by one task"). When no
  canvas is present (headless gateway tests), the runner defaults to invoking the
  closure directly — behavior-identical, still single-threaded. *Alternative
  rejected:* keep mutate-then-poke and make the render thread read only `pin()`
  snapshots. Rejected — it would require re-architecting arbc's interactive
  operator binder to bind over an immutable snapshot rather than the live model (an
  arbc change, out of scope per D-1), and defeats the whole point of the
  already-shipped `doc_mu`. **No doc delta required** — reuses A4/A5 seams, crosses
  only existing L4→L2 / L4→L1 edges.

- **D-edit_render_sync-3 — TSan reproduction on the real-pool `CanvasHost` path is
  the acceptance anchor.** The regression guard runs the concurrent
  edit-‖-`for_each_content` interleaving with `default_interactive_pool_config()`
  (real worker threads, bounded budget), not the inline settle-fully executor.
  *Rationale:* the race is only live where the render read actually overlaps the
  writer in wall-clock time — the bounded-budget `CanvasHost` path
  (`multi_canvas.md`), never the inline `hours(1)` `CanvasDriver` path that settles
  a frame before returning. A TSan test on the inline path would pass vacuously and
  prove nothing. The test must stress many iterations to reliably present the
  overlap window CI contention hit. *Alternative rejected:* assert the fix via the
  deterministic inline unit test alone. Rejected — the mutual-exclusion *contract*
  is unit-testable deterministically (and is, above), but only a real-thread TSan
  run witnesses the *absence of the data race* the task exists to kill. Both ship.
  **No doc delta required.**

## Open questions

`(none — all decided.)`

One latent arbc-contract question is **surfaced for the parking lot** (human /
library judgment, not WBS work): arbc's `SlotStore` binds the writer thread on
first write, yet `HostViewport::settle_external_loads` performs a **render-thread**
writer-publish during `drive_once` (flagged by nested_composition_binding D-4).
`doc_mu` already serializes that render-thread settle against the UI-thread
`transact` (both run under the one lock), so the **data race** this task fixes is
closed regardless. But if arbc additionally requires a single *consistent writer
thread identity* (not merely serialized access), a UI-thread `transact` following
a render-thread settle could trip an arbc-internal writer-identity assertion under
sustained mixed load — a libarbc-contract question that only the arbc maintainers
(or an arbc version bump) can settle, and that does not gate this leaf's data-race
fix. It is **not** encoded as a WBS task (it would be an un-closeable "investigate
arbc's writer binding" leaf); the closer records it in `tasks/parking-lot.md` for
human/library review.

## Status

**Done** — 2026-07-19.

- Replaced `set_edit_listener`/`on_edit_` post-hoc poke with an edit-serializing runner (`set_edit_runner` + private `run_edit`) in `src/app/ace/app/project_gateway.hpp` and `src/app/project_gateway.cpp`; `undo()`/`redo()` now hand their `commands::undo/redo` mutation to the runner (direct-run when none installed).
- Added `CanvasView::apply_edit` forwarding to `host_.apply_edit` in `src/app/ace/app/canvas_view.hpp` and `src/app/canvas_view.cpp`.
- Wired the production binding in `src/app/shell.cpp`: `set_edit_runner([&canvas](const auto& e){ canvas.apply_edit(e); })`, retiring the racy `shell.cpp:270` poke path.
- Added Catch2 L2 mutual-exclusion contract tests in `tests/canvas_host_test.cpp`: `drive_once` iterations frozen while `apply_edit` holds `doc_mu` (non-tautological shared counter); real-pool streamed-edit TSan anchor over many iterations.
- Replaced old poke-count test in `tests/app_project_gateway_test.cpp` with fake-runner ordering proof (enter → mutation → exit, `can_redo` flips inside runner) + no-runner direct-path case.
- Re-pointed e2e undo-through-gateway cases in `tests/canvas_view_e2e_test.cpp` and `tests/multi_canvas_e2e_test.cpp` at the runner→`apply_edit` production wiring.
- Parking-lot entry added for arbc writer-thread-identity question (`settle_external_loads` render-thread publish vs UI-thread `transact` under sustained mixed load); data race is closed regardless — human/library judgment only.
