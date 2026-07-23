# editor.canvas.writer_thread — A dedicated writer thread with sync + async closure submission

## TaskJuggler entry

`tasks/00-editor.tji` — `task writer_thread` under `editor.canvas`, `depends
!arbc_v030`. Supersedes `editor.canvas.single_writer`'s "keep every edit on the
caller" seam and retires `A4.1a`'s writer-priority document lease. Promotes the
parking-lot item *"Async submit-queue (off-UI-thread writer) for large-edit latency"*
(`tasks/parking-lot.md`, from `editor.canvas.frame_sync` D-frame_sync-2) into the WBS —
with its framing corrected: it is not a latency optimization gated on profiling, it is
the shape libarbc's memory model **requires** of a consumer that writes from more than
one thread.

## Effort

**4d.** New L1 component + tests (1d), seam rebind + straggler migration (1d), bootstrap /
open / save lifetime restructuring (1d), lease retirement + TSan/e2e reconciliation (1d).

## Inherited dependencies

- **Settled:** `editor.canvas.single_writer` (retired `doc_mu`, established `apply_edit`
  as the edit seam), `editor.canvas.multi_canvas` (`CanvasHost`, the one render thread),
  `editor.canvas.nested_composition_binding` (the `DocumentBinding` / per-renderer
  `KindBridge`), `editor.foundation.platform_services` (`platform::Threads` spawn seam).
- **Pending:** `editor.canvas.arbc_v030` — the pin bump. This task cannot land before it:
  without `ruoso/arbitrarycomposer#13`'s fix, `HostViewport::step()` still publishes from
  the render thread and **no host-side design can fix it**; without `#15`, the UI cannot
  read `can_undo()`/`can_redo()` off the writer thread at all.

## What this task is

Give the `Document` **one owned writer thread** and funnel every writer-thread-only
libarbc call to it through a FIFO closure queue with two entries — `submit_sync` (blocks
until the closure has run) and `submit` (enqueue and return). Retire the writer-priority
document lease; the UI thread and the render thread become pure submitters and readers.

Today the writer identity is "whatever thread called the verb", which happens to be the
UI thread (`CanvasHost::apply_edit` runs the closure inline on its caller,
`src/render/canvas_host.cpp:201-221`). That is not a design — it is a coincidence that
holds only while every write originates on the UI thread, and ours do not.

## Scope — the whole migration lands here, because there is no correct half

This task moves **every** writer-thread-only call onto the queue. A partial migration is
not a shippable intermediate state: the moment the writer thread exists and *any* write
still runs on the UI thread, the document has two identities — strictly worse than today,
where the single UI identity is at least consistent for everything except the render
thread's settler install. "Single writer" has no 90% version.

The work is not uniform, though, and it is worth knowing which parts are cheap:

- **Seam-bound already — a rebind, not a migration.** Every verb funnels through
  `AppProjectGateway::run_edit` or `CanvasView::apply_edit` today
  (`project_gateway.cpp:106,112,258,270,284,323` — including the `commands::dispatch`
  calls at `:300,:343`, which are *inside* those closures — plus
  `camera_inspector.cpp:92,118` and `canvas_view.cpp:403`). They migrate by rebinding one
  lambda at `shell.cpp:283` and pointing `CanvasView::apply_edit` at the injected runner
  instead of at `CanvasHost`. `editor.canvas.single_writer` built that funnel; this is
  what it bought.
- **The stragglers, and they are the load-bearing ones.** Project open/create
  (`project_open.cpp:107`'s `load_document` + the open-time checkpoint, reached from
  `commands/app_state.cpp:93,99` and the `shell.cpp:58` bootstrap) and save
  (`save.cpp:128`'s `capture_snapshot`, then the checkpoint) are **not** on the seam and
  never were. Open is precisely the call that *binds* the identity, so deferring it to a
  follow-up task would leave the writer thread as the document's second identity from the
  first frame — the exact bug, relocated. This is also where the real restructuring sits:
  the writer must be started before `AppState` is built and stopped after the document is
  released (D-6).
- **Legitimately deferrable, and deliberately deferred.** *Which* gestures switch from
  sync to async submission is a latency refinement, not a correctness one — everything
  posted synchronously is correct, just no faster than today. Shipping all-sync and tuning
  afterwards under measurement is Open question 3; if it warrants work it becomes its own
  leaf then, with data behind it.

## Why it needs to be done

libarbc doc 15 § Thread rules states the requirement and prescribes this exact remedy
(`15-memory-model.md:185-202`):

> Single writer means single *identity*, not serialized turns … the whole lock-free growth
> path is *written against a single mutator* — relaxed `high_water`,
> `SlabDirectory::publish`'s non-atomic load-check-new-store, the lock-step column publish,
> the writer-thread checkpoint seal. A consumer mutex only re-covers the accesses it wraps
> … **A consumer whose writes originate on two threads must funnel them to one dedicated
> writer thread** (post the work as a task), not take turns under a mutex.

Our writes originate on two threads:

- **UI thread** — every verb, `undo`/`redo`, project open's `arbc::load_document`
  (`src/project/project_open.cpp:107`), save's `capture_snapshot`
  (`src/project/save.cpp:126`).
- **Render thread** — `HostViewport`'s constructor and destructor call the
  writer-thread-only `Document::set_external_load_settler`
  (`host_viewport.cpp:115,130`), and every `CanvasRenderer::Impl::rebuild()` on resize
  reconstructs that viewport (`src/render/canvas_renderer.cpp:52-108`). Before v0.3.0 the
  render thread additionally published the external-arrival settle from inside `step()`.

`A4.1a`'s lease makes those *mutually exclusive*, which is precisely the "take turns under
a mutex" the rule rejects — it re-covers accesses, not identity. The two data races the
lease was reintroduced for are both fixed in v0.3.0 (the `DamageAccumulator` handoff now
carries its own mutex; `step()` no longer publishes off the writer thread), so after the
pin bump the lease guards nothing and the *identity* problem is the only one left.

## Inputs / context

**Design docs.** `docs/01-architecture.md` §4 — **A4** (threads: UI submits, writer
writes, renderers read), **A4.1** (single writer *identity*, which already names
"funnel both to one writer thread via task-posting" as the consequence), **A4.1a** (the
lease, which this task deletes); §8 levelization; §9 DoD. `docs/00-design.md` D15 (undo =
library transactions).

**libarbc v0.3.0 surface this design consumes.**

- `Document::on_writer_thread()` (`document.hpp:351`) — the identity, queryable from any
  thread in every build. This is what makes the design *testable*: the tripwire is an
  assertion, not a comment.
- `HostViewport::StepOutcome::external_loads_ready` (`host_viewport.hpp:168`) and
  `Document::external_loads_ready()` (`document.hpp:341`) — the per-frame report and the
  any-thread poll of arrivals owed a writer-thread install.
- `Document::set_external_load_settler()` / `external_loads_auto_settled()`
  (`document.hpp:370,375`) — the writer-thread settler a `Document`-bound viewport installs
  automatically, run at the next `Document::begin()`. This is why ignoring the report costs
  latency, never correctness.
- `Journal::can_undo()/can_redo()` become any-thread atomic reads
  (`ruoso/arbitrarycomposer#15`, landing in v0.3.0).

**Editor call sites.** `src/render/canvas_host.cpp` (the lease: `Impl::doc_mu`,
`doc_cv`, `doc_busy`, `doc_waiting_writers`, `Impl::Lease`, the `run()` hold at `:390`,
the `apply_edit` wrap at `:211`); `src/render/canvas_renderer.cpp:33-108,140-142` (the
per-renderer `KindBridge`, the viewport rebuild); `src/app/shell.cpp:283` (the edit-runner
binding); `src/app/project_gateway.cpp:96-131` (`undo`/`redo`/`run_edit`);
`src/app/canvas_view.cpp:561` (`CanvasView::apply_edit`);
`src/commands/ace/commands/app_state.hpp:69` (`is_dirty`);
`src/project/project_open.cpp:107`; `src/project/save.cpp:126`;
`src/platform/ace/platform/threads.hpp` (the spawn seam);
`src/dock/dock.cpp:258,337,343` (the per-frame UI reads).

## Constraints / requirements

1. **One identity for the document's lifetime.** Every libarbc call documented
   WRITER-THREAD ONLY runs on the writer thread, including the *first* one — project
   open's `load_document` binds the identity, so if it is not posted the UI thread becomes
   the writer and every posted edit is the second identity, i.e. the same bug relocated.
2. **Levelization (§8).** The new component depends on `base` + `platform` only — no
   libarbc, no ImGui/GL/SDL. `check_levels` clean.
3. **The render thread never blocks per frame.** It may block on a `submit_sync` at
   entry add/remove/resize only; the per-frame arrival nudge is async.
4. **No lock re-introduced around the document.** The lease is deleted, not renamed.
   Reads stay lock-free (`pin()`, the COW binding table, `revision()`); writes are posted.
5. **Total submission order.** A queued gesture burst, then an undo, must execute in that
   order. One FIFO for both entries.
6. **Deterministic headless behaviour.** The Catch2 units and `render_offline` goldens
   must stay thread-free and byte-identical.
7. **Don't-block-WASM (A3).** The design must degrade to a single-threaded build without a
   rewrite — a browser main thread may not block on `Atomics.wait`, so a blocking
   `submit_sync` from the UI thread is not portable as-is.
8. **Teardown order (Constraint 5 shape).** The writer thread's lifetime strictly encloses
   the document's, and stopping it drains rather than discards — a queued save or
   checkpoint must not be dropped at exit.

## Decisions

**D-writer_thread-1 — a new L1 component `writer`, a pure closure executor.**
`ace::writer::WriterThread` in `src/writer/`, depending on `base` + `platform` only. It
holds no `Document`, no libarbc type: it is "one thread owns these closures", which makes
it headless-unit-testable with no library fixture and keeps the document-shaped policy at
the call sites. Spawned through `platform::Threads` (`threads.hpp:24`), never
`std::thread` directly — that is the seam A3 reserves for the Emscripten port.
*Rejected:* putting it in `commands` (forces a `platform` edge onto the command layer),
in `platform` (policy inside the portability seam), or on `CanvasHost` (L2 `render` would
own the document write path; rendering is not writing, and the writer must outlive every
canvas).

**D-writer_thread-2 — two entries, one FIFO.**

```cpp
class WriterThread {
public:
  bool submit(std::function<void()> work);        // async: enqueue, return
  bool submit_sync(const std::function<void()>&); // sync: return after it ran
  bool on_writer_thread() const noexcept;
  // Writer-thread work with no submitter: run whenever the queue drains, before
  // waiting. Returns true to stay ARMED (wait boundedly, poll again), false to
  // wait indefinitely for the next submission. See D-10.
  void set_idle_work(std::function<bool()> work);
};
```

Both push onto the same queue, so submission order is total and cross-entry: an async
gesture burst followed by a sync `undo` runs in submission order. Both return `false`
iff the queue is stopped (see D-6). Sync carries a happens-before edge in both directions
(the caller's captures are visible to the writer; the closure's writes are visible to the
caller on return), which is what makes construct-on-writer / use-on-render legal in D-8.

**D-writer_thread-3 — sync is the default; async is the exception.** Most existing call
sites are result-carrying and capture by reference:
`run_edit([&]{ moved = commands::undo(...).moved; })` (`project_gateway.cpp:106-108`), and
insert returns the new id the UI uses to set selection. Making those async would be a
silent lifetime bug, not a latency win. Async is for exactly two shapes: a continuous
gesture already coalescing into one undo step (manip drag, brush stroke), and the render
thread's arrival nudge (D-10), which must never block a frame.

**D-writer_thread-4 — re-entrant `submit_sync` runs inline.** A closure already on the
writer that calls `submit_sync` (an edit whose tail saves, say) would self-deadlock;
detect by thread id and run it inline. Re-entrant `submit` still enqueues — it cannot
deadlock and enqueueing preserves ordering.

**D-writer_thread-5 — an inline degenerate mode, and it is also the WASM fallback.**
A `WriterThread` constructed without a `platform::Threads&` spawns nothing and runs every
submission inline on the caller. This serves three masters with one mechanism: the
headless Catch2/golden fixtures stay deterministic and thread-free (mirroring libarbc's
own `WorkerPoolConfig{}` inline degenerate, which this repo already relies on); a
single-threaded diagnostic build needs no `#ifdef`; and a WASM build without
pthreads/SharedArrayBuffer runs with the UI thread as the writer identity — still **one**
identity, still correct, no blocking wait on the browser main thread (Constraint 7).

**D-writer_thread-6 — lifetime encloses the document; stop drains; post-stop is refused.**
`start()` before the document is created or loaded, `stop()` after the last canvas entry
is gone and before the document is destroyed. `stop()` drains the queue, then joins.
`submit*` after `stop()` returns `false` without running the closure; the render thread
reads a `false` as "the document is going away" and skips its work rather than falling
back to running it inline (which would mint a second identity at exactly the worst moment).

**D-writer_thread-7 — the posting inventory is "everything arbc marks WRITER-THREAD
ONLY", which is wider than "writes".** Three distinct categories, all posted:

| What | Why | Where |
|---|---|---|
| `transact`/commit (every `scene::` mutator, `commands::dispatch`) | structural write — binds/holds identity | `scene/cell.cpp:310`, `scene/camera.cpp:523,547,569,588` |
| `journal().undo()/redo()` | structural write via `Model::navigate` | `commands/app_state.cpp:74,81` |
| `arbc::load_document` | the **first** write — binds the identity | `project/project_open.cpp:107` |
| `Document::checkpoint()` | `Checkpointer::commit` racing a writer transaction is memory-unsafe (`document.hpp:213`) | save/quit paths |
| `capture_snapshot` | *a read*, but it copies the writer-owned content side-map and unknown-field stash (`document_serialize.hpp:144-147`) | `project/save.cpp:126` |
| `set_damage_sink` / `set_external_load_settler` | plain writer-owned member slots read by `Document::begin()` | `HostViewport` ctor/dtor — see D-8 |

Explicitly **not** posted, because libarbc documents them any-thread: `pin()`,
`resolve()`, `for_each_content()` (`document.hpp:263-273,480`), `serialize_snapshot` over
an already-captured `ContentSnapshot`, `external_loads_ready()`, `on_writer_thread()`,
`drain()`, and (post-#15) `can_undo()`/`can_redo()`. Save therefore keeps its cheap half
on the writer and its expensive half off it: **post the capture, serialize off-thread** —
which is what `document_serialize.hpp:74-81` designed the split for.

**D-writer_thread-8 — post the `HostViewport`, and only the `HostViewport`.** Its ctor
and dtor install/release the writer-thread-only settler slot (`host_viewport.cpp:115,130`),
so both are wrapped in a `submit_sync` issued from the render thread — including inside
`CanvasRenderer::Impl::rebuild()`, which reconstructs the viewport on every resize. The
`InteractiveRenderer`, `TileCache`, `SurfacePool` and target `Surface` stay
render-thread-confined exactly as Constraint 3 has them: `~InteractiveRenderer` drains into
the shared `WorkerPool`, and blocking the writer on a worker drain would stall every
pending edit. When the `DocumentBinding` is empty (null registry — the headless fixtures)
no settler is installed and nothing is posted at all, so the golden path stays thread-free.
*Alternative (not a dependency):* ask upstream to split the settler install out of the
constructor into an explicit writer-thread `attach` call, which would retire this posting
entirely — see Open questions.

**D-writer_thread-9 — the `KindBridge` moves from per-renderer/render-confined to
document-scoped/writer-owned.** Today each `CanvasRenderer` owns a `KindBridge` seeded
once and "mutated only by step()'s settle" (`canvas_renderer.cpp:40,140-142`) — sound
while the settle ran on the render thread. In v0.3.0 the settle runs on the **writer**
thread (via `step()` only when writer == driver, otherwise via the document's auto-settler
at `begin()`), so that bridge would be writer-mutated render-owned state: a new race, and
one the lease's removal exposes. It is also wrong per-canvas: the document holds **one**
settler slot, so with N canvases the arrival interns into whichever viewport installed
last. One bridge owned beside the document, handed to every viewport's `DocumentBinding`,
fixes both and is canvas-count-independent. The `Registry` is already process-persistent
and read-only after seeding, so it stays shared as-is.

**D-writer_thread-10 — the writer consumes the deferred settle proactively; it does not
wait to be told.** The work `step()` declines to do is the writer's work, so the writer
polls for it itself rather than depending on another thread to notice. `set_idle_work`
(D-2) is bound once, at the same place the document and the D-9 bridge are wired:

```cpp
writer.set_idle_work([&] {
  if (doc.external_loads_ready() > 0) {   // any-thread, lock-free, one relaxed load
    arbc::settle_external_loads(doc, bridge, registry);
    canvas.poke();                        // the install is damage; re-render it
  }
  return doc.pending_external_loads() > 0; // fetches still in flight => stay armed
});
```

The writer runs this whenever its queue drains, **before** waiting. The return value is
what keeps the poll honest: armed (a fetch is outstanding) waits boundedly and re-polls;
disarmed waits indefinitely on the queue, so a document with no external references costs
exactly one relaxed atomic load per drain and **zero** timer wakeups. An arrival on a
completely idle app — nobody editing, nothing rendering — is therefore consumed without
any submission from anywhere.

Two other paths reach the same settle and all three are idempotent (the settle drains the
ready queue, and re-entry is suppressed by `InstallScope`):
`StepOutcome::external_loads_ready > 0` on the render thread issues **one** deduped async
wake — an in-flight flag cleared when the closure runs, or the still-unsettled arrival
re-nudges every frame — which only shortens latency to the current frame; and arbc's own
auto-settler runs at the next `Document::begin()` (`document.hpp:360-365`) if a UI edit
gets there first. None of the three is load-bearing alone, which is the point: the
placeholder cannot sit forever because *no one* was watching. The closure captures only
the document, the D-9 bridge and the registry — no render-thread state crosses.

**D-writer_thread-11 — the lease is deleted and the edit seam leaves `render`.** Remove
`doc_mu`, `doc_cv`, `doc_busy`, `doc_waiting_writers`, `Impl::Lease`, the `run()` hold
(`canvas_host.cpp:390`) and `CanvasHost::apply_edit` outright; `poke()` stays as the
render-wake. The shell binds the gateway's edit runner
(`shell.cpp:283`) to `writer.submit_sync(edit)` followed by `canvas.poke()`, and
`CanvasView::apply_edit` (`canvas_view.cpp:561`) forwards to that same injected runner
instead of to the host. `render` (L2) stops owning the document write path, which is what
it should never have owned.

**D-writer_thread-12 — the UI's per-frame reads.** `can_undo()`/`can_redo()`
(`dock.cpp:337,343`) become any-thread atomic loads once `#15` lands — no editor-side
snapshot needed. `is_dirty()` (`app_state.hpp:69`) is already race-free: it is
`saved_revision_ != document_->pin()->revision()`, a pinned read of the immutable
`DocRoot`. The one editor-owned scalar to fix is `saved_revision_` itself, written by
`mark_saved` inside the posted save closure and read by the UI — make it atomic.

**D-writer_thread-13 — closure discipline.** A posted closure touches the `Document` and
writer-owned state only. It must not take a UI lock, call ImGui, or block on the render
thread — the last inverts the D-8 sync edge and deadlocks. Enforced by review and by the
TSan lane, not by a type.

## Acceptance criteria

**L1 Catch2 units** (`tests/writer_thread_test.cpp`, new — headless, no libarbc):

- Mixed `submit`/`submit_sync` execute in submission order (one FIFO, D-2).
- `submit_sync` returns only after its closure completed, and the closure's writes are
  visible to the caller on return.
- Every closure observes the **same** `std::this_thread::get_id()`, and it differs from
  every submitting thread (D-1).
- Re-entrant `submit_sync` from inside a closure runs inline and does not deadlock;
  re-entrant `submit` enqueues and preserves order (D-4).
- `stop()` drains queued work before joining; `submit`/`submit_sync` after `stop()` return
  `false` and do not run (D-6).
- Inline degenerate mode runs on the calling thread and spawns nothing (D-5).
- Idle work runs when the queue drains and never interleaves with a closure; returning
  `true` re-polls on a bounded wait with no submission, `false` waits indefinitely and
  costs zero wakeups; `stop()` does not leave a poll armed (D-10).

**Proactive-settle integration** (`tests/canvas_host_test.cpp`, real pool): a document with
a deferred external child, an idle writer, **no** edits and **no** render loop running —
the arrival still installs, witnessed by `external_loads_ready()` returning to zero and the
child compositing on the next driven frame. Then the same case with the render loop live,
witnessing the D-10 wake path shortens it to the observing frame without double-settling
(`external_loads_auto_settled()` + the settle's own return account for exactly one install).

**Identity tripwire** (`tests/canvas_host_test.cpp` / `tests/app_project_gateway_test.cpp`):
`doc.on_writer_thread()` is **true** inside every posted closure and **false** on the UI
and render threads — the v0.3.0 predicate asserted directly, plus a debug assert at the
top of the edit seam.

**TSan** (hermetic `gcc-tsan` lane, both bundles): streamed UI edits ‖ a live render loop
‖ a canvas added and removed mid-stream ‖ a deferred external child arriving — race-free,
and the arrival composites (`external_loads_auto_settled()` or the D-10 nudge accounts for
it). This replaces `canvas_host_test.cpp`'s current streamed-edit anchor, which proved the
lock-free COW read under the lease.

**e2e** (ImGui Test Engine): the existing suites stay green with edits posted — the
`E2EState` handshake keeps determinism because verbs go through `submit_sync`.
`multi_canvas_e2e_test.cpp` and `multi_canvas_mint_e2e_test.cpp` exercise the D-8 posted
viewport construction across N canvases.

**Goldens** byte-identical; **`check_levels`** clean with the new `writer` edge;
`scripts/gate` green.

## Open questions

1. **Does `~Document` need the writer thread?** doc 15 is explicit that the *drainer* is
   not the writer and that the checkpointer is, but the destructor runs a final drain
   through the housekeeping seam (`document.hpp:533-535`). D-6 posts creation and load;
   whether destruction must also be posted needs a read of `~Model`/`~Document` at
   implementation time, or a question upstream. Posting it is harmless if unnecessary.
2. **Split the settler install out of `HostViewport`'s constructor?** If upstream adds an
   explicit writer-thread `attach`/`detach` for the settler, D-8's posted construction
   retires entirely and the render thread goes back to owning its whole cache lifecycle.
   Worth an issue after v0.3.0 ships; not a dependency of this task.
3. **Sync-submit latency behind a deep async burst.** A streamed gesture's queue could
   make a subsequent sync verb wait. The coalescing key already bounds the *commit* cost,
   not the queue depth. Measure on the real pool; if it bites, add a bounded depth or a
   gesture-drop policy — deliberately not designed ahead of data.

## Status

**Done** — 2026-07-23.

- New L1 component `src/writer/ace/writer/writer_thread.hpp` + `src/writer/writer_thread.cpp`: `ace::writer::WriterThread` with sync FIFO (`submit_sync`), async FIFO (`submit`), idle-work hook, re-entrant inline path, inline-degenerate mode, and drain-then-join teardown — depending on `base` + `platform` only.
- `tests/writer_thread_test.cpp` (9 Catch2 units): FIFO order, sync edge, one identity, re-entrancy, drain-on-stop/refusal, inline degenerate, idle arm/disarm.
- `tests/canvas_host_test.cpp` +4 cases: idle-writer proactive settle, live-loop nudge settling exactly once, `on_writer_thread()` tripwire, TSan anchor (streamed edits ‖ live render loop ‖ canvas add/remove mid-stream ‖ deferred arrival).
- `tests/project_save_test.cpp` +2 cases: capture posted once, refused capture publishes nothing.
- `tests/writer_session.hpp`: shared e2e fixture wiring `WriterThread` into existing test suites (11 `*_e2e_test.cpp` files migrated).
- `src/render/canvas_host.{cpp,hpp}`, `src/render/canvas_renderer.{cpp,hpp}`: writer-priority lease (`doc_mu`/`doc_cv`/`doc_busy`/`doc_waiting_writers`/`Impl::Lease`/`apply_edit`) deleted; `KindBridge` moved from per-renderer/render-confined to document-scoped/writer-owned; `HostViewport` ctor/dtor wrapped in `submit_sync` from the render thread (D-8); `poke()` retained as render-wake.
- `src/app/shell.cpp`, `src/app/canvas_view.{cpp,hpp}`, `src/app/project_gateway.{cpp,hpp}`: edit-runner rebound to `writer.submit_sync` + `canvas.poke()`; all `run_edit`/`apply_edit` call sites posting through the writer.
- `src/commands/{app_state.cpp,ace/commands/app_state.hpp}`: `saved_revision_` made atomic; all verb dispatch funnelled through the writer.
- `src/project/{save.cpp,ace/project/save.hpp}`: `capture_snapshot` and `checkpoint` posted to the writer thread.
- `CMakeLists.txt`, `scripts/check_levels.py`, `docs/01-architecture.md`: `writer` registered as a new L1 component; `check_levels` DAG updated; A4/A4.1/A4.1a documentation updated to reflect the retired lease and the new single-identity design.
- TSan-clean under gcc-tsan (31 warnings in the shell bundle are all pre-existing libdbus lock-order-inversion inside `SDL_Init`/`SDL_Quit`, no editor mutex in the cycle).
