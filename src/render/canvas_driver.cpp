#include <ace/render/canvas_driver.hpp>
#include <ace/render/canvas_renderer.hpp>
#include <ace/render/render.hpp>

#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>

namespace ace::render {

CanvasDriver::CanvasDriver(arbc::Document& document) : renderer_(document) {}

CanvasDriver::~CanvasDriver() = default;

void CanvasDriver::request_resize(int width, int height) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    pending_width_ = width;
    pending_height_ = height;
    resize_pending_ = true;
    dirty_ = true;
  }
  cv_.notify_all();
}

void CanvasDriver::request_camera(const arbc::Affine& camera) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    pending_camera_ = camera;
    camera_pending_ = true;
    dirty_ = true;
  }
  cv_.notify_all();
}

void CanvasDriver::poke() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    dirty_ = true;
  }
  cv_.notify_all();
}

void CanvasDriver::stop() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    stop_ = true;
  }
  cv_.notify_all();
}

bool CanvasDriver::drive_once() {
  // Apply any pending resize on the render thread — the renderer is confined here,
  // so a UI-thread resize is a request the render thread services (Constraint 1).
  bool do_resize = false;
  int width = 0;
  int height = 0;
  bool do_camera = false;
  arbc::Affine camera;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (resize_pending_) {
      do_resize = true;
      width = pending_width_;
      height = pending_height_;
      resize_pending_ = false;
    }
    if (camera_pending_) {
      do_camera = true;
      camera = pending_camera_;
      camera_pending_ = false;
    }
  }
  if (do_resize && (width != renderer_.width() || height != renderer_.height())) {
    renderer_.resize(width, height);
    // The reconstructed viewport restarts its frame numbering at 0, so re-key the
    // publish gate to guarantee the first frame at the new size publishes even when
    // both sizes happen to settle at the same frames_issued() count.
    published_frames_ = 0;
  }
  // Apply the submitted camera AFTER any resize — the renderer holds it so the resize's
  // rebuild already reframed with the current camera; this seats a fresh submit
  // (editor.canvas.nav). A camera change is device damage, so the next step repaints.
  if (do_camera) {
    renderer_.set_camera(camera);
  }

  renderer_.step();
  iterations_.fetch_add(1, std::memory_order_relaxed);
  // Snapshot the deep-zoom anchor depth for the UI's lock-free observability read.
  anchor_depth_.store(renderer_.anchor_depth(), std::memory_order_relaxed);

  const std::uint64_t frames = renderer_.frames_issued();
  // A still scene issues zero further frames (frames_issued() stable); a zero-area
  // pane issues none at all — either way publish nothing (Constraint 4).
  if (frames == 0 || frames == published_frames_) {
    return false;
  }
  published_frames_ = frames;

  // Copy the settled image on the render thread (outside the lock), then publish it
  // under a short lock via a full-buffer swap — a torn frame is never observable
  // (Constraint 3 / D-frame_sync-4). The swap recycles the previous, still-UNCONSUMED
  // front buffer as the producer's next scratch; once the consumer has moved a front
  // out, the next publish fills a fresh buffer. The consumer never aliases the
  // producer's buffers.
  back_ = renderer_.image();
  {
    std::lock_guard<std::mutex> lock(mu_);
    std::swap(published_, back_);
    ++sequence_;
  }
  return true;
}

void CanvasDriver::run(const std::function<bool()>& should_stop) {
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait(lock, [&] { return stop_ || dirty_ || (should_stop && should_stop()); });
      if (stop_ || (should_stop && should_stop())) {
        return;
      }
      dirty_ = false;
    }
    if (drive_once()) {
      // A frame settled this step; async external loads may still be resolving, so
      // re-arm to re-check on the next iteration rather than sleeping through the
      // tail (Constraint 4c). A subsequent still step publishes nothing and the loop
      // then idles.
      std::lock_guard<std::mutex> lock(mu_);
      dirty_ = true;
    }
  }
}

bool CanvasDriver::consume(std::uint64_t& last_seq, Srgb8Image& out) {
  std::lock_guard<std::mutex> lock(mu_);
  if (sequence_ == last_seq) {
    return false; // no frame newer than the caller last observed
  }
  // MOVE the front buffer out under the short lock — a bounded pointer steal, never a
  // wait or a copy-under-lock (D-frame_sync-4). The consumer takes exclusive ownership,
  // so the buffer it uploads never re-enters the producer's back/front pool to be
  // reallocated under it (a cross-thread use-after-realloc the render thread would
  // otherwise race). `published_` is left empty; the next publish fills a fresh front
  // (the copy from the renderer runs off-lock on the render thread).
  out = std::move(published_);
  last_seq = sequence_;
  return true;
}

std::uint64_t CanvasDriver::published_sequence() const {
  std::lock_guard<std::mutex> lock(mu_);
  return sequence_;
}

std::uint64_t CanvasDriver::iterations() const {
  return iterations_.load(std::memory_order_relaxed);
}

std::size_t CanvasDriver::anchor_depth() const {
  return anchor_depth_.load(std::memory_order_relaxed);
}

} // namespace ace::render
