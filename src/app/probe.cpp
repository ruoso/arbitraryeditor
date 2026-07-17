#include <ace/app/probe.hpp>
#include <ace/gl/gl.hpp>
#include <ace/project/project.hpp>
#include <ace/render/render.hpp>
#include <ace/views/views.hpp>

namespace ace::app {

ProbeView::~ProbeView() { destroy(); }

void ProbeView::upload() {
  // GL-free offline render (runs without a context), then the tile→GL upload.
  const render::Srgb8Image image =
      render::render_probe_srgb8(project::k_probe_width, project::k_probe_height);
  width_ = image.width;
  height_ = image.height;
  texture_ = gl::upload_rgba8(image.pixels.data(), width_, height_);
}

void ProbeView::draw() const { views::draw_probe_pane(texture_, width_, height_); }

void ProbeView::draw_content() const { views::draw_probe_image(texture_, width_, height_); }

void ProbeView::destroy() {
  if (texture_ != 0) {
    gl::destroy_texture(texture_);
    texture_ = 0;
  }
}

} // namespace ace::app
