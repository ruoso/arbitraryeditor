#include <ace/app/canvas_view.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/gl/gl.hpp>
#include <ace/interact/interact.hpp>
#include <ace/project/project.hpp>
#include <ace/render/canvas_host.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>

#include <imgui.h>

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
    // Thread the app's process-persistent kind Registry into the render path so this canvas's
    // HostViewport settles deferred external nested children each frame — the same Registry the
    // save/load paths use, so custom kinds resolve identically across save/load/render (A14,
    // editor.canvas.nested_composition_binding). The L4 app owns both AppState and CanvasHost;
    // the Registry crosses as a libarbc const arbc::Registry* (no commands->render edge).
    host_.add(std::string(view_id), state_.document(), &state_.registry());
  }
  Presenter& p = it->second;

  // Read the persisted shot cameras ONCE per frame over the lock-free pin() snapshot (A4;
  // no doc_mu, no render-cache touch — D-look_through-7). Reused by the look-through resolve
  // below and the picker overlay at the end.
  const std::vector<scene::Camera> cameras = scene::cameras(state_.document());

  // Resolve this frame's target framing (D-look_through-2). Free viewport (nullopt) = a
  // pane-sized render through the transient camera (today's behavior). A selected shot = its
  // EXACT crop: the shot's pane-fit resolution + the derived comp->device camera, both fed
  // through the SETTLED resize + request_camera channels — no new render channel. A selection
  // whose shot is gone (GC'd/deleted, cameras() no longer lists it) or degenerate falls back
  // to the free viewport (fail-safe, never a crash or a stale frame — Constraint 7).
  int target_w = pane_width;
  int target_h = pane_height;
  std::optional<arbc::Affine> shot_camera;
  if (p.look_through) {
    for (const scene::Camera& cam : cameras) {
      if (cam.id == *p.look_through) {
        const interact::LookThrough lt = interact::look_through(
            cam.frame, cam.resolution.width, cam.resolution.height, pane_width, pane_height);
        if (lt.out_w > 0 && lt.out_h > 0) {
          target_w = lt.out_w;
          target_h = lt.out_h;
          shot_camera = lt.camera;
        }
        break;
      }
    }
  }

  // The target size drives this entry's viewport: post a resize REQUEST only on a genuine
  // change (Constraint 1/7). Sizing the viewport to the shot's crop is what clips it, so the
  // letterbox margins are clean bars, not surrounding composition (D-look_through-2).
  if (target_w != p.requested_width || target_h != p.requested_height) {
    p.requested_width = target_w;
    p.requested_height = target_h;
    host_.request_resize(view_id, target_w, target_h);
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

  // Draw the last uploaded frame every tick so the canvas stays visible between new frames
  // (the render thread only publishes on change). The camera picker overlay is drawn AFTER,
  // regardless of whether a frame exists yet, so a canvas can be retargeted before its first
  // frame lands.
  const ImVec2 pane_origin = ImGui::GetCursorScreenPos();
  if (p.texture != 0 && p.tex_width > 0 && p.tex_height > 0) {
    if (shot_camera) {
      // Look through a shot: present its crop letterboxed. Nav is INERT here — the gesture
      // pipeline is skipped, so Presenter::camera is preserved at its last free value and
      // clearing the selection restores that framing (D-look_through-6). The submitted camera
      // is the shot's, below.
      views::draw_letterboxed(p.texture, p.tex_width, p.tex_height, pane_width, pane_height);
    } else {
      // Free viewport (editor.canvas.nav): draw + read the always-on navigation gestures over
      // the pane. Thread the raw gesture through the L1 interact math (D-nav-2) into the
      // transient camera. reset-to-fit frames the root composition's authored canvas bounds
      // (D-fit_bounds-1); wheel zooms about the cursor; a Space-drag pans (D9).
      const views::CanvasInput in =
          views::draw_canvas_interactive(p.texture, p.tex_width, p.tex_height);
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
      p.camera = camera;

      // The scale bar (composition units per screen span, never a "%"; D2 §3 / D-nav-6):
      // a bar targeting a quarter of the pane's short edge.
      const double target_px = 0.25 * std::min(p.tex_width, p.tex_height);
      const interact::ScaleBar bar = interact::scale_bar(p.camera, target_px);
      p.scale_bar_units = bar.units;
      views::draw_scale_bar(bar.units, bar.device_px);
    }

    // Submit the resolved camera — the shot's affine while looking through, else the free
    // transient camera — on a real change (D-nav-1: never a transact; the submit rides the
    // per-entry render-thread channel, Constraint 2). Dedup against the last submitted value,
    // which diverges from p.camera while a shot is selected.
    const arbc::Affine want = shot_camera ? *shot_camera : p.camera;
    if (!(want == p.submitted)) {
      p.submitted = want;
      host_.request_camera(view_id, want);
    }
  }

  // The per-canvas camera picker (D-look_through-5), drawn LAST so it overlays the pane's
  // top-left and stays clickable (the nav button yields via SetNextItemAllowOverlap).
  ImGui::SetCursorScreenPos(pane_origin);
  draw_camera_picker(view_id, p, cameras);
}

void CanvasView::draw_camera_picker(std::string_view view_id, Presenter& p,
                                    const std::vector<scene::Camera>& cameras) {
  // A compact per-canvas picker: "Viewport" (the free camera) plus one entry per shot from
  // scene::cameras. Clicking sets THIS canvas's selection (session state, no transaction).
  // ImGui ids are seeded per window, and each canvas#N draws into its own window, so the two
  // panes carry independent selections (A5/D19) and the e2e drives the buttons by their
  // labels within this canvas#N's window ref. `view_id` is unused today but names the target.
  (void)view_id;
  ImGui::TextUnformatted("Camera:");
  ImGui::SameLine();
  if (ImGui::SmallButton("Viewport")) {
    p.look_through.reset();
  }
  for (const scene::Camera& cam : cameras) {
    ImGui::SameLine();
    if (ImGui::SmallButton(cam.name.c_str())) {
      p.look_through = cam.id;
    }
  }
}

void CanvasView::set_look_through(std::string_view view_id, std::optional<arbc::ObjectId> shot) {
  auto it = presenters_.find(view_id);
  if (it != presenters_.end()) {
    it->second.look_through = shot;
  }
}

std::optional<arbc::ObjectId> CanvasView::look_through(std::string_view view_id) const {
  auto it = presenters_.find(view_id);
  return it == presenters_.end() ? std::nullopt : it->second.look_through;
}

void CanvasView::apply_edit(const std::function<void()>& edit) { host_.apply_edit(edit); }

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
