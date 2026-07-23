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
class KindBridge;
struct WorkerPoolConfig;
struct Affine;
} // namespace arbc

namespace ace::writer {
class WriterThread;
} // namespace ace::writer

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
//   - The host does NOT own the document write path any more (D-writer_thread-11). The
//     writer-priority document lease and `apply_edit` are gone: edits are posted to the
//     document's own writer thread by the L4 edit seam, and the host's only remaining
//     writer-thread business is its OWN — the HostViewport / DamageRouter lifecycle, which
//     touches WRITER-THREAD-ONLY document slots and is posted through `set_writer`
//     (D-writer_thread-8). `poke()` stays as the render-wake.
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

  // Bind the DOCUMENT's writer thread — the one identity every WRITER-THREAD-ONLY libarbc call
  // this host makes must run on (D-writer_thread-8): the per-document DamageRouter's
  // construction/destruction (it occupies `Model::set_damage_sink`) and every HostViewport
  // ctor/dtor (they install and release `Document::set_external_load_settler` and (un)register
  // with that router). Set ONCE at wiring time, before the first `add()` and before `run()`. Null
  // (the default) means the caller IS the one writer identity — the deterministic headless
  // fixtures — and all of the above runs inline, exactly as it did before this leaf. The writer
  // MUST outlive this host (D-writer_thread-6: its lifetime strictly encloses the document's,
  // and the host's entries are gone before it stops).
  void set_writer(writer::WriterThread* writer);

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
  //
  // `bridge` (optional, default null) is the DOCUMENT-SCOPED KindBridge owned beside `document`
  // and shared by every canvas over it (D-writer_thread-9) — the document holds ONE settler slot,
  // so a per-renderer bridge is both a writer/render ownership split and wrong at N canvases.
  // Null falls back to a renderer-owned bridge (the single-canvas golden path). Same lifetime
  // contract as `registry`.
  void add(std::string id, arbc::Document& document, const arbc::Registry* registry = nullptr,
           arbc::KindBridge* bridge = nullptr);

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

  // ANY thread: wake the render loop to re-render EVERY live entry after an edit (the
  // fan-out poke — one writer, N observers; Constraint 4). This is the wake half of the edit
  // seam: the L4 posts the mutation to the writer thread and then pokes here. It does NOT
  // serialize anything, and does not need to — the render walk's document read is lock-free
  // (`pin()`, the copy-on-write binding table), the DamageAccumulator carries its own mutex
  // since v0.3.0, and `step()` no longer publishes off the writer thread (A4.1a as amended).
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

  // External arrivals the host's own async settle nudges INSTALLED (D-writer_thread-10) — a
  // behavioral counter, not a timing. Read together with `Document::external_loads_auto_settled()`
  // and whatever the writer's idle poll installed, it accounts for every install of an arrival, so
  // a test can pin "settled exactly once" across three idempotent paths rather than assuming only
  // one of them fired. Zero for a host with no writer bound or an empty DocumentBinding.
  std::uint64_t settles_installed() const;

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
