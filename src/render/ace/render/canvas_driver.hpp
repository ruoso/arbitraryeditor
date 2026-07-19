#pragma once

#include <ace/render/canvas_renderer.hpp>
#include <ace/render/render.hpp>

#include <arbc/base/transform.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>

namespace arbc {
class Document;
} // namespace arbc

namespace ace::render {

// The off-UI-thread canvas driver (editor.canvas.frame_sync; A4 "the UI thread
// stays responsive because rendering is never on it"). It wraps the synchronous
// CanvasRenderer editor.canvas.view shipped with a render-thread drive loop and a
// single-producer/single-consumer latest-frame double-buffer:
//
//   - The RENDER thread owns the CanvasRenderer (HostViewport + InteractiveRenderer
//     + TileCache + target Surface). It ONLY touches it here — construction, resize,
//     and every step() are render-thread-confined (Constraint 1 / A4). It runs
//     run(should_stop), a condition-variable idle/wake loop that drives one step()
//     per wake and publishes each settled Srgb8Image into the double-buffer, tagged
//     with a monotonically increasing sequence.
//   - The UI thread NEVER blocks on the render thread and NEVER touches the cache.
//     It posts resize requests (request_resize), pokes the loop after an edit
//     (poke), consumes the latest published frame (consume — a bounded, non-blocking
//     buffer swap under a short mutex), and tears the loop down (stop → the owner
//     joins).
//
// This is std-only synchronization (<mutex>/<condition_variable>/<atomic>): it adds
// NO render → platform DAG edge (Constraint 6 / D-frame_sync-1). The thread that
// runs run() is spawned by the L4 app::CanvasView through platform::NativeThreads.
// GL-FREE and ImGui-FREE like CanvasRenderer: it produces CPU sRGB8 bytes; the GL
// upload stays a UI-thread gl/app step. `document` and its collaborators are held by
// reference (through CanvasRenderer) and MUST outlive this object.
class CanvasDriver {
public:
  explicit CanvasDriver(arbc::Document& document);
  ~CanvasDriver();

  CanvasDriver(const CanvasDriver&) = delete;
  CanvasDriver& operator=(const CanvasDriver&) = delete;

  // UI thread: request a new framing size in device pixels. The request is applied
  // on the render thread at the top of its next iteration (reconstructing the
  // non-movable HostViewport) — the UI thread never touches the driver (Constraint
  // 1). Also wakes the loop.
  void request_resize(int width, int height);

  // UI thread: submit a new viewport camera (editor.canvas.nav, D-nav-3). Stashes a
  // pending Affine under the SAME short lock as the pending resize; the render thread
  // applies it (renderer.set_camera) at the top of its next iteration, before step().
  // A submit both stores the pending camera and wakes the loop — a camera change is
  // device damage, so the next step repaints. Cloned from request_resize; adds no lock
  // and no thread (Constraint 2).
  void request_camera(const arbc::Affine& camera);

  // UI thread: wake the render loop to re-render after an edit (the poke, Constraint
  // 4a). A poke on an unchanged, settled scene still wakes the loop; step()'s
  // still-scene early-out then publishes no new frame.
  void poke();

  // UI thread: signal the render loop to stop and wake it so run() returns. The
  // owner then joins the render thread before destroying this driver (Constraint 5).
  // Idempotent.
  void stop();

  // RENDER thread: block on the condition variable until woken by a poke, a resize
  // request, self-signalled settling work, stop(), or `should_stop()`; then drive
  // one iteration. Returns when stop() was called or `should_stop()` is observed
  // true. No busy-spin: a settled scene with nothing pending blocks (Constraint 4).
  void run(const std::function<bool()>& should_stop);

  // RENDER thread: apply any pending resize, drive one HostViewport::step(), and
  // publish a fresh frame into the double-buffer iff a new frame was issued. Returns
  // true iff a frame was published this iteration. Public for deterministic
  // single-threaded unit tests (inline executor, D-frame_sync-3); in the app only
  // run() calls it, on the render thread.
  bool drive_once();

  // UI thread (or a test): take the latest published frame iff its sequence is newer
  // than `last_seq`. Non-blocking (a bounded MOVE under a short mutex, never a wait or a
  // copy-under-lock). On a new frame it MOVES the front buffer into `out` — the consumer
  // takes exclusive ownership so the render thread never reallocates the buffer it is
  // uploading — advances `last_seq` to the published sequence, and returns true;
  // otherwise leaves `out` and `last_seq` untouched and returns false (Constraint 3).
  bool consume(std::uint64_t& last_seq, Srgb8Image& out);

  // The latest published frame sequence (0 before the first publish). Monotonic:
  // advances only when a new frame is published, so a still scene holds it stable
  // (Constraint 3/4, D-frame_sync-4).
  std::uint64_t published_sequence() const;

  // The number of drive iterations completed — the render-thread liveness signal the
  // loop unit test asserts against (lock-free so a predicate may read it without
  // contending the render thread's lock).
  std::uint64_t iterations() const;

  // The viewport's current anchor-path depth (deep-zoom rebasing observability,
  // D-nav-5). Snapshotted into an atomic on the render thread after each step, so the
  // UI reads it lock-free with no torn cross-thread read of the render-confined
  // HostViewport. 0 within the well-conditioned band.
  std::size_t anchor_depth() const;

private:
  CanvasRenderer renderer_; // render-thread-confined: only drive_once() touches it

  mutable std::mutex mu_;
  std::condition_variable cv_;
  bool stop_ = false;           // stop requested (UI thread → render thread)
  bool dirty_ = false;          // work pending: a poke / resize / self-signal
  bool resize_pending_ = false; // a resize request awaits the render thread
  int pending_width_ = 0;       // the requested framing size (guarded by mu_)
  int pending_height_ = 0;
  bool camera_pending_ = false; // a camera submit awaits the render thread
  arbc::Affine pending_camera_ = arbc::Affine::identity(); // the submitted camera (guarded by mu_)

  Srgb8Image published_;               // the front buffer the consumer reads
  Srgb8Image back_;                    // the producer's scratch (render thread)
  std::uint64_t sequence_ = 0;         // the published-frame sequence (guarded)
  std::uint64_t published_frames_ = 0; // frames_issued() last published (render thread)
  bool content_published_ = false;     // has any content frame published? (render thread;
                                       // once-only content gate, editor.canvas.blank_first_frame)

  std::atomic<std::uint64_t> iterations_{0};
  std::atomic<std::size_t> anchor_depth_{0}; // render-thread snapshot, UI reads lock-free
};

} // namespace ace::render
