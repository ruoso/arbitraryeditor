#pragma once

#include <ace/render/render.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace arbc {
class Document;
class WorkerPool;
class Registry;
struct WorkerPoolConfig;
struct Affine;
} // namespace arbc

namespace ace::render {

// The N-canvas host (editor.canvas.multi_canvas; A5 "multiple canvases are multiple
// renderers sharing one WorkerPool"). It promotes editor.canvas.frame_sync's single
// off-UI-thread driver into a collection: ONE host-owned arbc::WorkerPool
// (runtime.shared_worker_pool), a canvas#N-keyed map of per-canvas entries — each a
// CanvasRenderer (its own HostViewport / TileCache / target Surface, borrowing the one
// shared pool) plus frame_sync's latest-frame double-buffer, reused verbatim — and ONE
// shared drive loop that round-robins a bounded step() across every live entry
// (D-multi_canvas-1/2/3).
//
//   - The RENDER thread is the sole owner/driver: it constructs, resizes, step()-drives,
//     and destroys every entry's cache, and is the single submitter/drainer of the
//     shared pool. add/remove are REQUESTS serviced at the top of a drive iteration, so
//     every cache stays render-thread-confined — no new locking beyond frame_sync's
//     mutex-guarded latest-frame slot, now one slot per entry (A4/A5, Constraint 3).
//   - The UI thread never blocks on the render thread and never touches a cache. It
//     add()s / remove()s entries, request_resize()s per id, poke()s ALL entries after an
//     edit (N observers, one writer — Constraint 4), consume()s each entry's latest
//     frame, and stop()s the loop (the owner then joins).
//
// std-only synchronization (<mutex>/<condition_variable>/<atomic>): NO render -> platform
// DAG edge (Constraint 5 / D-frame_sync-1). The one thread that runs run() is spawned by
// the L4 app through platform::NativeThreads. GL-FREE and ImGui-FREE: it produces CPU
// sRGB8 bytes; the per-canvas GL upload stays a UI-thread gl/app step. Every entry's
// Document (and its collaborators) MUST outlive the entry.
//
// Pool lifetime (Constraint 2 / interactive.hpp): the WorkerPool is declared before the
// entry map, so it OUTLIVES every borrowing renderer — ~InteractiveRenderer drains into
// the pool, so a pool that died first is a use-after-free. Entries tear down before it.
class CanvasHost {
public:
  // The shipped app config: the shared interactive WorkerPool (real workers, capped at
  // k_max_interactive_workers) + a bounded per-frame budget for fair N-canvas scheduling.
  CanvasHost();
  // Inject the pool config (WorkerPoolConfig{} = the deterministic inline degenerate,
  // for goldens/units) and the per-frame budget (an hour = settle-fully for byte-exact
  // goldens; D-multi_canvas-3/4).
  CanvasHost(arbc::WorkerPoolConfig pool_config, std::chrono::steady_clock::duration frame_budget);
  ~CanvasHost();

  CanvasHost(const CanvasHost&) = delete;
  CanvasHost& operator=(const CanvasHost&) = delete;

  // UI thread: ensure an entry rendering `document` exists for `id` (idempotent — a
  // second add with a live id is a no-op). The entry's CanvasRenderer is constructed on
  // the render thread at the top of the next drive iteration; also wakes the loop.
  //
  // `registry` (optional, default null) is the app's process-persistent kind Registry,
  // forwarded to the entry's CanvasRenderer so its HostViewport settles deferred external
  // nested children each frame (editor.canvas.nested_composition_binding). A libarbc type
  // crossing the seam (no commands->render edge); when null the entry uses the empty binding
  // — the ref-free path the headless unit fixtures rely on (D-nested_composition_binding-2).
  // It MUST outlive every entry over the document, like the document itself.
  void add(std::string id, arbc::Document& document, const arbc::Registry* registry = nullptr);

  // UI thread: remove the entry for `id` (idempotent). Its cache is freed on the render
  // thread; a removed id is no longer served (consume/published_sequence go quiet).
  void remove(std::string_view id);

  // UI thread: request a new framing size in device pixels for entry `id` (applied on
  // the render thread; a request for an unknown id is dropped). Also wakes the loop.
  void request_resize(std::string_view id, int width, int height);

  // UI thread: submit a new viewport camera for entry `id` (editor.canvas.nav,
  // D-nav-3). Per-entry, like request_resize: stashes a pending Affine under the host
  // lock, applied on the render thread before that entry's next step(); a submit for
  // an unknown id is dropped. Also wakes the loop (a camera change is device damage).
  void request_camera(std::string_view id, const arbc::Affine& camera);

  // UI thread: run a document-mutating edit on the calling (writer) thread, then wake the
  // loop to re-render every live entry (one writer, N observers — Constraint 4). The edit
  // runs SYNCHRONOUSLY, under the host's writer-priority document lease, so it is mutually
  // exclusive with a driven render iteration. arbc v0.2.0's copy-on-write content bindings
  // (ruoso/arbitrarycomposer#10/#11) cover the render walk's binding read, but a commit also
  // flushes into the render thread's HostViewport::DamageAccumulator and races the commit
  // sink that step()'s settle_external_loads republishes (arbc#13) — both TSan-reported, so
  // the exclusion is required. It is WRITER-priority: run() yields to a queued edit before
  // starting an iteration, so a streamed burst of edits is never starved by the re-armed
  // render loop. Keeping every edit on the caller also holds arbc's single writer-*identity*
  // contract (SlotStore binds the writer thread on first write; editor.canvas.single_writer).
  // This is the edit-serializing seam the app's edit-runner binds to; it replaces
  // "mutate, then poke()".
  void apply_edit(const std::function<void()>& edit);

  // UI thread: install the writer-turn EPILOGUE — a callback invoked at the end of every
  // apply_edit, on the calling (writer) thread, still inside the document lease (arch
  // A18). This is the seam an L4 uses to republish anything the UI thread reads off
  // writer-owned document structure; `apply_edit` is the single point EVERY document
  // mutation passes through, including the bare `scene::` transactions that never go
  // near a `commands` verb, so hooking it here is structural rather than per-call-site
  // discipline. Deliberately an opaque `std::function<void()>`: `render` may not depend
  // on `commands` (§8), and this keeps the DAG intact. An empty function (the default)
  // means no epilogue. Set once at wiring time from the same thread that calls
  // apply_edit; the render thread never touches it.
  void set_post_edit_hook(std::function<void()> hook);

  // UI thread: wake the render loop to re-render EVERY live entry after an edit (the
  // fan-out poke — one writer, N observers; Constraint 4). Safe only when the edit
  // itself does not race the render thread's document read — prefer apply_edit() for a
  // document mutation submitted while the render loop is live.
  void poke();

  // UI thread: signal the render loop to stop and wake it so run() returns. The owner
  // then joins the render thread before destroying this host (Constraint 5). Idempotent.
  void stop();

  // RENDER thread: block until woken by an add/remove/resize/poke, self-signalled
  // settling work, stop(), or `should_stop()`; then drive one iteration. Returns when
  // stop() was called or `should_stop()` is observed true. No busy-spin.
  void run(const std::function<bool()>& should_stop);

  // RENDER thread: service pending add/remove, then drive one bounded step() per live
  // entry and publish each settled frame into that entry's double-buffer. Returns true
  // iff there is more work pending (a frame published, or an entry left unsettled under
  // its bounded budget and must be re-driven) so run() re-arms. Public for deterministic
  // single-threaded unit tests (inline executor); in the app only run() calls it.
  bool drive_once();

  // UI thread (or a test): take entry `id`'s latest published frame iff its sequence is
  // newer than `last_seq`. Non-blocking (a bounded MOVE under a short lock). Returns
  // false (out/last_seq untouched) for an unknown id or no new frame (Constraint 3).
  bool consume(std::string_view id, std::uint64_t& last_seq, Srgb8Image& out);

  // The latest published frame sequence for `id` (0 before the first publish or for an
  // unknown id). The presenter's per-pane liveness signal.
  std::uint64_t published_sequence(std::string_view id) const;

  // Entry `id`'s current deep-zoom anchor-path depth (D-nav-5 observability): 0 for an
  // unknown id, before the first frame, or while the camera is within the
  // well-conditioned band. Read lock-free from a render-thread-snapshotted atomic.
  std::size_t anchor_depth(std::string_view id) const;

  // The number of live entries currently served (post add/remove reconciliation).
  std::size_t live_count() const;

  // The number of drive iterations completed — the render-thread liveness signal.
  std::uint64_t iterations() const;

  // The one shared pool every entry borrows (Constraint 1 / acceptance f).
  const arbc::WorkerPool& worker_pool() const;

  // The pool entry `id` borrows, or nullptr for an unknown id — so a test can prove two
  // entries borrow the SAME single pool object (acceptance f).
  const arbc::WorkerPool* entry_pool(std::string_view id) const;

private:
  struct Entry;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ace::render
