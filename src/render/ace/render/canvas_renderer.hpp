#pragma once

#include <ace/render/render.hpp>

#include <cstdint>
#include <memory>

namespace arbc {
class Document;
} // namespace arbc

namespace ace::render {

// The single-canvas interactive driver (editor.canvas.view; A5 "a canvas view is
// one HostViewport + InteractiveRenderer over the shared Document"). It composes
// the libarbc pieces the host-interactive example wires by hand — a CpuBackend
// (behind the A6 Backend seam), a SurfacePool, a TileCache, one persistent target
// Surface in the document's working space, one InteractiveRenderer, and a
// HostViewport bound via the Document& constructor against the app's one owned
// Document — and drives one frame per UI tick, converting the settled target to
// straight-alpha sRGB8 (D10 — via CpuBackend::convert, never hand-rolled).
//
// GL-FREE and ImGui-FREE (Constraint 2): it produces CPU sRGB8 bytes; the GL
// upload is a separate gl/app step, so the whole compositor path is
// headless-unit- and golden-testable without a GL context.
//
// This leaf drives the renderer synchronously on the UI thread with the
// deterministic thread-free inline executor (WorkerPoolConfig{}) — D-canvas_view-2.
// The off-UI-thread driver + latest-frame double-buffer is editor.canvas.frame_sync;
// N canvases sharing one WorkerPool is editor.canvas.multi_canvas. `document` and
// its collaborators are held by reference and MUST outlive this object (the
// HostViewport ctor's lifetime contract).
class CanvasRenderer {
public:
  explicit CanvasRenderer(arbc::Document& document);
  ~CanvasRenderer();

  CanvasRenderer(const CanvasRenderer&) = delete;
  CanvasRenderer& operator=(const CanvasRenderer&) = delete;

  // Frame the canvas at `width`x`height` device pixels: reallocate the target
  // Surface and reconstruct the (non-movable) HostViewport bundle at the new
  // size. A degenerate/zero size renders nothing (no allocation) — Constraint 7.
  void resize(int width, int height);

  // Drive one HostViewport::step() on the UI thread (inline executor). The first
  // step always composites (frames_issued() == 1); a subsequent step on an
  // unchanged, playhead-pinned scene issues zero further frames (the still-scene
  // early-out). Re-converts the settled target to sRGB8 only when a new frame was
  // issued.
  void step();

  // The current settled straight-alpha sRGB8 image (empty before the first frame
  // or at a zero size). Tightly packed w*h*4, GL-uploadable — Constraint 5.
  const Srgb8Image& image() const;

  // The HostViewport's issued-frame counter — the change signal the app uses to
  // upload GL only when the frame moved (Constraint 6). Zero before the first
  // step or at a zero size.
  std::uint64_t frames_issued() const;

  // The current framing size in device pixels (0 until the first non-degenerate
  // resize).
  int width() const;
  int height() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ace::render
