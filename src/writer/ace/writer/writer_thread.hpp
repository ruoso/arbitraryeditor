#pragma once

#include <ace/platform/threads.hpp>

#include <cstdint>
#include <functional>
#include <memory>

namespace ace::writer {

// The document's ONE writer thread (editor.canvas.writer_thread, arch A4.1): a FIFO closure
// executor owning a single OS thread, and NOTHING else. It holds no `Document` and names no
// libarbc type — "one thread owns these closures" is the whole abstraction (D-writer_thread-1),
// which is what keeps it an L1 component over `base` + `platform` alone, headless-unit-testable
// with no library fixture, and leaves the document-shaped policy at the call sites.
//
// WHY a thread and not a lock. libarbc doc 15 § Thread rules: "single writer" means single
// *identity*, not serialized turns — the lock-free growth path (relaxed `high_water`,
// `SlabDirectory::publish`'s non-atomic load-check-new-store, the lock-step column publish, the
// writer-thread checkpoint seal) is written against ONE mutator, and `SlotStore::allocate`
// asserts that identity in debug builds. A consumer mutex only re-covers the accesses it wraps.
// The editor's writes originate on two threads (UI verbs + the render thread's `HostViewport`
// settler install), so the library's prescribed remedy — funnel both to one dedicated writer
// thread by posting the work — is what this class is.
//
// TWO ENTRIES, ONE QUEUE (D-writer_thread-2). `submit_sync` blocks until the closure has run;
// `submit` enqueues and returns. Both push onto the SAME FIFO, so submission order is total and
// cross-entry: an async gesture burst followed by a sync `undo` runs in submission order
// (Constraint 5). `submit_sync` carries a happens-before edge in BOTH directions — the caller's
// captures are visible to the writer, and the closure's writes are visible to the caller on
// return — which is what makes construct-on-writer / use-on-render legal (D-writer_thread-8).
//
// CLOSURE DISCIPLINE (D-writer_thread-13). A posted closure touches the `Document` and
// writer-owned state only. It must not take a UI lock, call ImGui, or block on the render
// thread — the last inverts the sync edge above and deadlocks. Enforced by review and by the
// TSan lane, not by a type.
//
// Spawned through `platform::Threads` (never `std::thread` directly): that is the seam A3
// reserves for the Emscripten port.
class WriterThread {
public:
  // The INLINE DEGENERATE mode (D-writer_thread-5): spawns nothing and runs every submission
  // inline on the calling thread. One mechanism, three masters — the headless Catch2/golden
  // fixtures stay deterministic and thread-free (mirroring libarbc's own `WorkerPoolConfig{}`
  // inline degenerate, which this repo already relies on); a single-threaded diagnostic build
  // needs no `#ifdef`; and a WASM build without pthreads/SharedArrayBuffer runs with the UI
  // thread as the writer identity — still ONE identity, still correct, and with no blocking
  // wait on the browser main thread (Constraint 7, "don't block WASM").
  WriterThread();

  // The shipped mode: spawn ONE thread through the portable seam and bind the identity to it.
  // The constructor returns only after the thread has bound its id, so `on_writer_thread()`
  // answers correctly from the very first submission. `threads` must outlive this object.
  explicit WriterThread(platform::Threads& threads);

  // Drains and joins if `stop()` has not already been called.
  ~WriterThread();

  WriterThread(const WriterThread&) = delete;
  WriterThread& operator=(const WriterThread&) = delete;

  // ASYNC: enqueue `work` and return. Use for a continuous gesture already coalescing into one
  // undo step and for the render thread's arrival nudge, which must never block a frame —
  // nothing else (D-writer_thread-3: sync is the default, async is the exception, because most
  // call sites are result-carrying and capture by reference, where async would be a silent
  // lifetime bug rather than a latency win). Re-entrant `submit` from inside a closure still
  // ENQUEUES: it cannot deadlock, and enqueueing is what preserves ordering (D-writer_thread-4).
  // Returns false iff the queue is stopped, in which case `work` is NOT run.
  bool submit(std::function<void()> work);

  // SYNC: enqueue `work` and return only after it has run. `work` is BORROWED for the call (no
  // copy), so a by-reference-capturing closure is safe and result-carrying call sites keep their
  // shape. Re-entrant `submit_sync` from inside a closure runs INLINE — enqueueing behind
  // ourselves would self-deadlock (D-writer_thread-4). Returns false iff the queue is stopped,
  // in which case `work` is NOT run: the caller reads that as "the document is going away" and
  // skips its work rather than falling back to running it inline, which would mint a second
  // writer identity at exactly the worst moment (D-writer_thread-6).
  bool submit_sync(const std::function<void()>& work);

  // Is the calling thread this document's writer identity? The tripwire that makes the design
  // testable rather than commented — the editor's mirror of `arbc::Document::on_writer_thread()`.
  // Always true in inline degenerate mode: there the caller IS the one identity by construction.
  bool on_writer_thread() const noexcept;

  // Writer-thread work with NO submitter (D-writer_thread-10): run whenever the queue drains,
  // BEFORE waiting. Returning true keeps the poll ARMED (wait boundedly, poll again); returning
  // false waits indefinitely for the next submission — so a document with nothing outstanding
  // costs one poll per drain and ZERO timer wakeups. Never interleaves with a submitted closure
  // (same thread, run between queue items). An empty function clears it. Settable from any
  // thread; arming an idle writer wakes it so the new poll runs without waiting for a submission.
  void set_idle_work(std::function<bool()> work);

  // Stop accepting submissions, DRAIN whatever is already queued, then join (D-writer_thread-6).
  // A queued save or checkpoint is never discarded at exit. Idempotent. Must NOT be called from
  // the writer thread itself (it joins). After it returns the thread is gone, so the document's
  // writer-owned slots are quiescent — which is what lets a destructor that finds a refused
  // submission fall back to running inline without minting a live second identity.
  void stop();

  // Has `stop()` been called? Any thread.
  bool stopped() const noexcept;

  // Closures this writer has RUN (behavioral counter, not a timing): the observability the unit
  // tests assert ordering and drain-on-stop against.
  std::uint64_t executed() const noexcept;

  // Times the idle work has been polled (D-writer_thread-10). The witness for "disarmed costs
  // zero wakeups": with the poll disarmed this stops advancing while the writer sleeps.
  std::uint64_t idle_polls() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ace::writer
