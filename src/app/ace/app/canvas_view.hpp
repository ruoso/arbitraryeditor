#pragma once

#include <ace/platform/threads.hpp>
#include <ace/render/canvas_driver.hpp>
#include <ace/render/render.hpp>

#include <cstdint>
#include <memory>

namespace ace::commands {
class AppState;
} // namespace ace::commands

namespace ace::app {

// Owns the interactive Canvas driver + its GL texture at the app layer (L4), the
// off-UI-thread successor to editor.canvas.view's synchronous driver
// (editor.canvas.frame_sync). At construction it spawns a dedicated RENDER THREAD
// (through the portable platform::NativeThreads seam, A3/D-frame_sync-1) that runs
// the render::CanvasDriver drive loop — so HostViewport::step() never runs on the UI
// thread (A4 "rendering is never on it"). Each UI frame it sizes the driver to the
// Canvas pane (a resize REQUEST the render thread services), consumes the latest
// settled frame from the double-buffer, and uploads it — a fresh gl::upload_rgba8 on
// the first frame / a resize, an in-place gl::update_rgba8 when a new sequence lands
// at the same size (Constraint 3), and no GL traffic when the sequence has not moved.
// An edit poke (poke()) wakes the render thread promptly; the UI thread stays the
// single writer (D-frame_sync-2). Teardown is deterministic: stop → join → release
// the texture (Constraint 5). Bind against the process's one owned Document via the
// AppState& the shell holds (Constraint 3); it must outlive this view.
class CanvasView {
public:
  explicit CanvasView(commands::AppState& state);
  ~CanvasView();

  CanvasView(const CanvasView&) = delete;
  CanvasView& operator=(const CanvasView&) = delete;

  // Draw the Canvas body into the CURRENT ImGui window at `pane_width`x
  // `pane_height` device pixels (the dockspace owns Begin/End + the tab ✕). A
  // zero/degenerate pane renders nothing (Constraint 7). Requires a current GL
  // context.
  void draw_content(int pane_width, int pane_height);

  // Wake the render thread to re-render after a UI-thread edit (the poke seam the
  // edit points — undo/redo through the gateway — drive; D-frame_sync-2). Thread-safe.
  void poke();

  // The driver's published-frame sequence — the test-visible signal the e2e asserts
  // (>= 1 for a live frame; an advance after an edit poke proves the off-thread
  // re-render reached the double-buffer).
  std::uint64_t frames_issued() const;

  // Stop + join the render thread and release the GL texture while the context is
  // still valid (before shutdown). Safe to call twice (the destructor also calls it).
  void destroy();

private:
  render::CanvasDriver driver_;
  platform::NativeThreads threads_;
  std::unique_ptr<platform::JoinHandle> render_thread_;

  // The latest frame consumed from the double-buffer, and the sequence it carried —
  // re-uploaded to GL only when the sequence advances (Constraint 3).
  render::Srgb8Image image_;
  std::uint64_t consumed_seq_ = 0;

  // The last framing size requested of the render thread; a genuine change posts a
  // new resize request (Constraint 7).
  int requested_width_ = 0;
  int requested_height_ = 0;

  unsigned int texture_ = 0;
  int tex_width_ = 0;
  int tex_height_ = 0;
};

} // namespace ace::app
