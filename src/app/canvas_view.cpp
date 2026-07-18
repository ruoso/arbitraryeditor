#include <ace/app/canvas_view.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/gl/gl.hpp>
#include <ace/render/canvas_renderer.hpp>
#include <ace/render/render.hpp>
#include <ace/views/views.hpp>

namespace ace::app {

CanvasView::CanvasView(commands::AppState& state) : renderer_(state.document()) {}

CanvasView::~CanvasView() { destroy(); }

void CanvasView::draw_content(int pane_width, int pane_height) {
  if (pane_width <= 0 || pane_height <= 0) {
    return; // degenerate pane — render nothing until it has area (Constraint 7).
  }
  // Pane-size drives the viewport: reallocate + re-bind the driver only on a
  // genuine size change (Constraint 7), reusing the texture object otherwise.
  if (pane_width != renderer_.width() || pane_height != renderer_.height()) {
    renderer_.resize(pane_width, pane_height);
  }
  renderer_.step();

  const std::uint64_t frames = renderer_.frames_issued();
  const render::Srgb8Image& image = renderer_.image();
  // Guard the upload+draw positively: `frames == 0` / an empty image only on the
  // defensive no-frame path (a failed target allocation), which draws nothing.
  if (frames > 0 && image.width > 0 && image.height > 0) {
    // Re-upload only on change (Constraint 6): a fresh upload on the first frame
    // or a size change (a new texture object), an in-place update thereafter, and
    // no GL traffic when the still scene issued no new frame.
    const bool size_changed =
        texture_ == 0 || image.width != tex_width_ || image.height != tex_height_;
    if (size_changed) {
      if (texture_ != 0) {
        gl::destroy_texture(texture_);
        texture_ = 0;
      }
      texture_ = gl::upload_rgba8(image.pixels.data(), image.width, image.height);
      tex_width_ = image.width;
      tex_height_ = image.height;
      uploaded_frames_ = frames;
    } else if (frames != uploaded_frames_) {
      gl::update_rgba8(texture_, image.pixels.data(), image.width, image.height);
      uploaded_frames_ = frames;
    }

    views::draw_canvas_image(texture_, tex_width_, tex_height_);
  }
}

std::uint64_t CanvasView::frames_issued() const { return renderer_.frames_issued(); }

void CanvasView::destroy() {
  if (texture_ != 0) {
    gl::destroy_texture(texture_);
    texture_ = 0;
  }
}

} // namespace ace::app
