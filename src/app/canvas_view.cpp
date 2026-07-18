#include <ace/app/canvas_view.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/gl/gl.hpp>
#include <ace/render/canvas_driver.hpp>
#include <ace/render/render.hpp>
#include <ace/views/views.hpp>

namespace ace::app {

CanvasView::CanvasView(commands::AppState& state) : driver_(state.document()) {
  // Spawn the dedicated render thread through the portable spawn seam (A3): it runs
  // the driver's blocking drive loop off the UI thread. The loop stops when
  // driver_.stop() is called at teardown (destroy()), so the predicate is trivially
  // false — no separate stop flag to keep in sync.
  render_thread_ = threads_.spawn([this] { driver_.run([] { return false; }); });
}

CanvasView::~CanvasView() { destroy(); }

void CanvasView::draw_content(int pane_width, int pane_height) {
  if (pane_width <= 0 || pane_height <= 0) {
    return; // degenerate pane — render nothing until it has area (Constraint 7).
  }
  // Pane-size drives the viewport: post a resize REQUEST the render thread services
  // only on a genuine size change (Constraint 1/7). The UI thread never touches the
  // driver/cache directly.
  if (pane_width != requested_width_ || pane_height != requested_height_) {
    requested_width_ = pane_width;
    requested_height_ = pane_height;
    driver_.request_resize(pane_width, pane_height);
  }

  // Consume the latest settled frame from the double-buffer (non-blocking); upload to
  // GL only when the sequence advanced (Constraint 3). A fresh upload on the first
  // frame or a size change (a new texture object), an in-place update thereafter.
  if (driver_.consume(consumed_seq_, image_) && image_.width > 0 && image_.height > 0) {
    const bool size_changed =
        texture_ == 0 || image_.width != tex_width_ || image_.height != tex_height_;
    if (size_changed) {
      if (texture_ != 0) {
        gl::destroy_texture(texture_);
        texture_ = 0;
      }
      texture_ = gl::upload_rgba8(image_.pixels.data(), image_.width, image_.height);
      tex_width_ = image_.width;
      tex_height_ = image_.height;
    } else {
      gl::update_rgba8(texture_, image_.pixels.data(), image_.width, image_.height);
    }
  }

  // Draw the last uploaded frame every tick so the canvas stays visible between new
  // frames (the render thread only publishes on change).
  if (texture_ != 0 && tex_width_ > 0 && tex_height_ > 0) {
    views::draw_canvas_image(texture_, tex_width_, tex_height_);
  }
}

void CanvasView::poke() { driver_.poke(); }

std::uint64_t CanvasView::frames_issued() const { return driver_.published_sequence(); }

void CanvasView::destroy() {
  // Deterministic teardown (Constraint 5): stop the loop, wake it, and join BEFORE
  // destroying the driver or the GL texture. The render thread touches no GL, so the
  // join needs no live context; no thread outlives this view.
  if (render_thread_) {
    driver_.stop();
    render_thread_->join();
    render_thread_.reset();
  }
  if (texture_ != 0) {
    gl::destroy_texture(texture_);
    texture_ = 0;
  }
}

} // namespace ace::app
