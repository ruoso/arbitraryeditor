#include <ace/app/canvas_view.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/gl/gl.hpp>
#include <ace/render/canvas_host.hpp>
#include <ace/render/render.hpp>
#include <ace/views/views.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace ace::app {

CanvasView::CanvasView(commands::AppState& state) : state_(state) {
  // Spawn the ONE render thread through the portable spawn seam (A3): it runs the host's
  // blocking drive loop off the UI thread for ALL canvases. The loop stops when
  // host_.stop() is called at teardown (destroy()), so the predicate is trivially false.
  render_thread_ = threads_.spawn([this] { host_.run([] { return false; }); });
}

CanvasView::~CanvasView() { destroy(); }

void CanvasView::draw_content(std::string_view view_id, int pane_width, int pane_height) {
  if (pane_width <= 0 || pane_height <= 0) {
    return; // degenerate pane — render nothing until it has area (Constraint 7).
  }
  // Lazily register this canvas#N on its first appearance: add the host entry (idempotent)
  // and its presenter (D-multi_canvas-5). The entry's cache is constructed on the render
  // thread; the first settled frame arrives asynchronously through the double-buffer.
  auto it = presenters_.find(view_id);
  if (it == presenters_.end()) {
    it = presenters_.emplace(std::string(view_id), Presenter{}).first;
    host_.add(std::string(view_id), state_.document());
  }
  Presenter& p = it->second;

  // Pane-size drives this entry's viewport: post a resize REQUEST only on a genuine size
  // change (Constraint 1/7). The UI thread never touches the driver/cache directly.
  if (pane_width != p.requested_width || pane_height != p.requested_height) {
    p.requested_width = pane_width;
    p.requested_height = pane_height;
    host_.request_resize(view_id, pane_width, pane_height);
  }

  // Consume this entry's latest settled frame from its double-buffer (non-blocking);
  // upload to GL only when the sequence advanced (Constraint 3). A fresh upload on the
  // first frame or a size change (a new texture object), an in-place update thereafter.
  if (host_.consume(view_id, p.consumed_seq, p.image) && p.image.width > 0 && p.image.height > 0) {
    const bool size_changed =
        p.texture == 0 || p.image.width != p.tex_width || p.image.height != p.tex_height;
    if (size_changed) {
      if (p.texture != 0) {
        gl::destroy_texture(p.texture);
        p.texture = 0;
      }
      p.texture = gl::upload_rgba8(p.image.pixels.data(), p.image.width, p.image.height);
      p.tex_width = p.image.width;
      p.tex_height = p.image.height;
    } else {
      gl::update_rgba8(p.texture, p.image.pixels.data(), p.image.width, p.image.height);
    }
  }

  // Draw the last uploaded frame every tick so the canvas stays visible between new
  // frames (the render thread only publishes on change).
  if (p.texture != 0 && p.tex_width > 0 && p.tex_height > 0) {
    views::draw_canvas_image(p.texture, p.tex_width, p.tex_height);
  }
}

void CanvasView::poke() { host_.poke(); }

void CanvasView::reconcile(const std::vector<std::string>& live_view_ids) {
  // Drop any presenter whose canvas#N left the dock layout: free its host entry (the
  // render thread frees the cache) and its GL texture (here, on the UI thread with a live
  // context) — Constraint 7 / D-multi_canvas-5.
  for (auto it = presenters_.begin(); it != presenters_.end();) {
    if (std::find(live_view_ids.begin(), live_view_ids.end(), it->first) == live_view_ids.end()) {
      if (it->second.texture != 0) {
        gl::destroy_texture(it->second.texture);
      }
      host_.remove(it->first);
      it = presenters_.erase(it);
    } else {
      ++it;
    }
  }
}

std::uint64_t CanvasView::frames_issued(std::string_view view_id) const {
  return host_.published_sequence(view_id);
}

void CanvasView::destroy() {
  // Deterministic teardown (Constraint 5): stop the loop, wake it, and join BEFORE
  // releasing any GL texture or destroying the host. The render thread touches no GL, so
  // the join needs no live context; no thread outlives this view. The host's entries then
  // tear down before its shared pool (Constraint 2).
  if (render_thread_) {
    host_.stop();
    render_thread_->join();
    render_thread_.reset();
  }
  for (auto& [id, p] : presenters_) {
    if (p.texture != 0) {
      gl::destroy_texture(p.texture);
      p.texture = 0;
    }
  }
  presenters_.clear();
}

} // namespace ace::app
