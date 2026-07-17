#pragma once

namespace ace::app {

// Owns the render_probe's offline-rendered texture at the app layer (L4): the
// build→render→upload happens once after the GL context is current, the pane is
// drawn every frame, and the texture is destroyed before shutdown while the
// context is still valid (refinement Constraint 8). The generic Shell stays free
// of arbc/GL/view orchestration — the probe wiring lives here (D-render_probe-1).
class ProbeView {
public:
  ProbeView() = default;
  ~ProbeView();
  ProbeView(const ProbeView&) = delete;
  ProbeView& operator=(const ProbeView&) = delete;

  // Offline-render the probe (ace::render) and upload it (ace::gl). Requires a
  // current GL context. Idempotent replace is out of scope — call once.
  void upload();
  // Draw the standalone probe pane (ace::views): its own Begin/Image/End window.
  // No-op-safe with a zero texture handle.
  void draw() const;
  // Draw just the probe image into the CURRENT ImGui window — the Canvas view
  // body the dockspace draws inside the canvas window it owns (D18 "canvas is a
  // view"). No Begin/End.
  void draw_content() const;
  // Release the GL texture. Safe to call twice (the destructor also calls it).
  void destroy();

private:
  unsigned int texture_ = 0;
  int width_ = 0;
  int height_ = 0;
};

} // namespace ace::app
