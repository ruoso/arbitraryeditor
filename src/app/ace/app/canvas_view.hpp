#pragma once

#include <ace/render/canvas_renderer.hpp>

#include <cstdint>

namespace ace::commands {
class AppState;
} // namespace ace::commands

namespace ace::app {

// Owns the interactive Canvas driver + its GL texture at the app layer (L4), the
// successor to ProbeView (editor.canvas.view). Each frame it sizes the driver to
// the Canvas pane, steps it on the UI thread (the deterministic inline executor,
// D-canvas_view-2), and uploads the settled sRGB8 bytes — a fresh gl::upload_rgba8
// on the first frame / a resize, an in-place gl::update_rgba8 otherwise, and no GL
// traffic at all when the still scene issued no new frame (Constraint 6). The
// generic Shell stays free of arbc/GL/view orchestration; this wiring lives here
// (D-canvas_view-1). Bind against the process's one owned Document via the
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

  // The driver's issued-frame counter — the test-visible signal the e2e asserts
  // (frames_issued() >= 1) to prove the canvas produced a live frame.
  std::uint64_t frames_issued() const;

  // Release the GL texture while the context is still valid (before shutdown).
  // Safe to call twice (the destructor also calls it).
  void destroy();

private:
  render::CanvasRenderer renderer_;
  unsigned int texture_ = 0;
  int tex_width_ = 0;
  int tex_height_ = 0;
  // The frame count last uploaded to the texture; skip GL when it has not moved.
  std::uint64_t uploaded_frames_ = 0;
};

} // namespace ace::app
