#include <ace/app/canvas_view.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/gl/gl.hpp>
#include <ace/interact/interact.hpp>
#include <ace/project/project.hpp>
#include <ace/render/canvas_host.hpp>
#include <ace/render/render.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/transform.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace {
// The zoom factor per wheel notch (editor.canvas.nav): each notch multiplies the
// composition→device scale by this, so a wheel of `w` notches zooms by k_zoom_base^w.
constexpr double k_zoom_base = 1.2;
} // namespace

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
  // frames (the render thread only publishes on change), AND read the always-on
  // navigation gestures over the pane (editor.canvas.nav). Only meaningful once a frame
  // exists; the camera operates in the render target's device pixels (== the pane size).
  if (p.texture != 0 && p.tex_width > 0 && p.tex_height > 0) {
    const views::CanvasInput in =
        views::draw_canvas_interactive(p.texture, p.tex_width, p.tex_height);

    // Thread the raw gesture through the L1 interact math (D-nav-2) into a new transient
    // camera. reset-to-fit frames the root composition's authored canvas bounds into the
    // pane (D-fit_bounds-1) — the "don't get lost in unbounded space" recovery; a document
    // with no usable authored size keeps the identity framing ("nothing to fit",
    // D-fit_bounds-3). Wheel zooms about the cursor; a Space-drag pans (D9).
    arbc::Affine camera = p.camera;
    if (in.reset) {
      camera = arbc::Affine::identity();
      if (const std::optional<project::CompositionSize> size =
              project::root_composition_size(state_.document())) {
        camera = interact::fit(size->width, size->height, p.tex_width, p.tex_height);
      }
    }
    if (in.wheel != 0.0F) {
      camera = interact::zoom(camera, arbc::Vec2{in.focus_x, in.focus_y},
                              std::pow(k_zoom_base, static_cast<double>(in.wheel)));
    }
    if (in.panning && (in.pan_dx != 0.0F || in.pan_dy != 0.0F)) {
      camera = interact::pan(camera, in.pan_dx, in.pan_dy);
    }
    // Submit only on a real change — the transient camera is per-pane app state (D-nav-1),
    // never a transact; the submit rides the render-thread channel (Constraint 2).
    if (!(camera == p.camera)) {
      p.camera = camera;
      host_.request_camera(view_id, p.camera);
    }

    // The scale bar (composition units per screen span, never a "%"; D2 §3 / D-nav-6):
    // a bar targeting a quarter of the pane's short edge.
    const double target_px = 0.25 * std::min(p.tex_width, p.tex_height);
    const interact::ScaleBar bar = interact::scale_bar(p.camera, target_px);
    p.scale_bar_units = bar.units;
    views::draw_scale_bar(bar.units, bar.device_px);
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

std::size_t CanvasView::anchor_depth(std::string_view view_id) const {
  return host_.anchor_depth(view_id);
}

double CanvasView::scale_bar_units(std::string_view view_id) const {
  auto it = presenters_.find(view_id);
  return it == presenters_.end() ? 0.0 : it->second.scale_bar_units;
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
