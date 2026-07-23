#include <ace/interact/interact.hpp>
#include <ace/scene/scene.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace ace::interact {

const char* name() { return "interact"; }

double brush_units(double view_fraction, double view_short_edge_units) {
  return view_fraction * view_short_edge_units;
}

arbc::Affine pan(const arbc::Affine& camera, double device_dx, double device_dy) {
  // Post-translate in device space: compose(translation(dx,dy), camera).apply(p)
  // == camera.apply(p) + (dx,dy). The linear part is untouched, so only the
  // translation shifts — the content under the cursor follows the drag.
  arbc::Affine out = camera;
  out.tx += device_dx;
  out.ty += device_dy;
  return out;
}

arbc::Affine zoom(const arbc::Affine& camera, arbc::Vec2 device_focus, double factor) {
  // Scale-about-focus in device space: s(q) = focus + factor*(q - focus). Composed
  // OUTSIDE the camera, so s(camera.apply(focus_comp)) == focus and the linear part
  // scales by `factor` (max_scale *= factor). The composition point under the focus
  // pixel is therefore invariant (Constraint 4).
  const arbc::Affine scale_about_focus{
      factor, 0.0, 0.0, factor, device_focus.x * (1.0 - factor), device_focus.y * (1.0 - factor)};
  return compose(scale_about_focus, camera);
}

ScaleBar scale_bar(const arbc::Affine& camera, double target_px) {
  const double max_scale = camera.max_scale(); // device px per composition unit
  ScaleBar bar;
  if (!(max_scale > 0.0) || !std::isfinite(max_scale) || !(target_px > 0.0)) {
    return bar; // degenerate camera / target: nothing to draw
  }
  // The composition length that would render `target_px` wide, rounded DOWN to the
  // largest 1/2/5·10ⁿ nice number, so its device width fills the band without
  // overshooting.
  const double raw_units = target_px / max_scale;
  const double decade = std::pow(10.0, std::floor(std::log10(raw_units)));
  const double fraction = raw_units / decade; // in [1, 10)
  const double nice = fraction >= 5.0 ? 5.0 : (fraction >= 2.0 ? 2.0 : 1.0);
  bar.units = nice * decade;
  bar.device_px = bar.units * max_scale;
  return bar;
}

arbc::Affine fit(double content_w, double content_h, double pane_w, double pane_h) {
  if (!(content_w > 0.0) || !(content_h > 0.0) || !(pane_w > 0.0) || !(pane_h > 0.0)) {
    return arbc::Affine::identity(); // degenerate: nothing to fit
  }
  // Uniform scale so the whole content fits the pane, centered in the leftover axis.
  const double scale = std::min(pane_w / content_w, pane_h / content_h);
  const double tx = (pane_w - content_w * scale) * 0.5;
  const double ty = (pane_h - content_h * scale) * 0.5;
  return arbc::Affine{scale, 0.0, 0.0, scale, tx, ty};
}

ShotFraming new_shot_from_view(const arbc::Affine& camera, int pane_w, int pane_h) {
  ShotFraming shot;
  shot.width = pane_w;
  shot.height = pane_h;
  if (pane_w <= 0 || pane_h <= 0) {
    return shot; // degenerate pane: identity frame, nothing to promote
  }
  // The frame is the inverse camera (device -> composition): it places the shot's
  // output rectangle [0,pane_w]x[0,pane_h] into composition space exactly where the
  // viewport is looking, so rendering the shot at its own resolution reproduces the
  // framing. A non-invertible camera (zero scale) has no frame to promote.
  if (const std::optional<arbc::Affine> inverse = camera.inverse()) {
    shot.frame = *inverse;
  }
  return shot;
}

arbc::Affine viewport_camera_for_shot(const arbc::Affine& frame, int native_w, int native_h,
                                      int out_w, int out_h) {
  if (native_w <= 0 || native_h <= 0 || out_w <= 0 || out_h <= 0) {
    return arbc::Affine::identity(); // degenerate resolution: a safe no-op (no div-by-zero)
  }
  // `frame` maps device -> composition; its inverse is the comp -> device camera at the
  // shot's NATIVE resolution (the round-trip of new_shot_from_view). A non-invertible
  // frame has no render camera to derive.
  const std::optional<arbc::Affine> native = frame.inverse();
  if (!native) {
    return arbc::Affine::identity();
  }
  // Post-scale the native camera in device space by out/native: the SAME framed region
  // maps into [0,out_w]x[0,out_h] (device coords scale by k, the composition framing is
  // preserved), so the shot renders identically framed at any output resolution.
  const double kx = static_cast<double>(out_w) / static_cast<double>(native_w);
  const double ky = static_cast<double>(out_h) / static_cast<double>(native_h);
  return compose(arbc::Affine::scaling(kx, ky), *native);
}

LookThrough look_through(const arbc::Affine& frame, int shot_w, int shot_h, int pane_w,
                         int pane_h) {
  LookThrough lt;
  if (shot_w <= 0 || shot_h <= 0 || pane_w <= 0 || pane_h <= 0) {
    return lt; // degenerate: a zero-size no-op (the caller falls back to the free viewport)
  }
  // Fit the shot's aspect into the pane (letterbox): the constraining axis touches the
  // pane edge, the other is inset. Floor to an integer device size, clamped to >= 1.
  const double scale = std::min(static_cast<double>(pane_w) / static_cast<double>(shot_w),
                                static_cast<double>(pane_h) / static_cast<double>(shot_h));
  lt.out_w = std::max(1, static_cast<int>(static_cast<double>(shot_w) * scale));
  lt.out_h = std::max(1, static_cast<int>(static_cast<double>(shot_h) * scale));
  lt.camera = viewport_camera_for_shot(frame, shot_w, shot_h, lt.out_w, lt.out_h);
  return lt;
}

// --- Camera frame manipulation (editor.cameras.manip; D7/D8/D9) ---------------

namespace {

double vlen(double x, double y) { return std::sqrt(x * x + y * y); }
double dist(arbc::Vec2 p, arbc::Vec2 q) { return vlen(p.x - q.x, p.y - q.y); }
arbc::Vec2 mid(arbc::Vec2 a, arbc::Vec2 b) { return {(a.x + b.x) * 0.5, (a.y + b.y) * 0.5}; }

// Distance from composition-space point `p` to the segment [a,b].
double dist_to_segment(arbc::Vec2 p, arbc::Vec2 a, arbc::Vec2 b) {
  const double abx = b.x - a.x;
  const double aby = b.y - a.y;
  const double len2 = abx * abx + aby * aby;
  double t = 0.0;
  if (len2 > 0.0) {
    t = ((p.x - a.x) * abx + (p.y - a.y) * aby) / len2;
    t = std::clamp(t, 0.0, 1.0);
  }
  return vlen(p.x - (a.x + t * abx), p.y - (a.y + t * aby));
}

// A uniform scale by `k` about composition pivot `pv` (square-pixel-preserving), the
// same closed form as `zoom`'s scale-about-focus.
arbc::Affine scale_about(double k, arbc::Vec2 pv) {
  return arbc::Affine{k, 0.0, 0.0, k, pv.x * (1.0 - k), pv.y * (1.0 - k)};
}

// A rotation by `angle` radians about composition pivot `pv`.
arbc::Affine rotate_about(double angle, arbc::Vec2 pv) {
  const double cs = std::cos(angle);
  const double sn = std::sin(angle);
  return arbc::Affine{
      cs, sn, -sn, cs, pv.x - (cs * pv.x - sn * pv.y), pv.y - (sn * pv.x + cs * pv.y)};
}

} // namespace

bool is_resize_handle(FrameHandle handle) {
  switch (handle) {
  case FrameHandle::EdgeLeft:
  case FrameHandle::EdgeRight:
  case FrameHandle::EdgeTop:
  case FrameHandle::EdgeBottom:
  case FrameHandle::CornerTopLeft:
  case FrameHandle::CornerTopRight:
  case FrameHandle::CornerBottomLeft:
  case FrameHandle::CornerBottomRight:
    return true;
  default:
    return false;
  }
}

FrameHandle hit_frame(const arbc::Affine& frame, int native_w, int native_h, arbc::Vec2 point,
                      double edge_tol, double corner_tol) {
  if (native_w <= 0 || native_h <= 0) {
    return FrameHandle::None;
  }
  const std::optional<arbc::Affine> inv = frame.inverse();
  if (!inv) {
    return FrameHandle::None; // degenerate frame: nothing to grab
  }
  const double nw = native_w;
  const double nh = native_h;
  const arbc::Vec2 tl = frame.apply({0.0, 0.0});
  const arbc::Vec2 tr = frame.apply({nw, 0.0});
  const arbc::Vec2 bl = frame.apply({0.0, nh});
  const arbc::Vec2 br = frame.apply({nw, nh});

  // Corner handles win over everything within `corner_tol` (D7 near-corner precedence).
  const arbc::Vec2 corners[4] = {tl, tr, bl, br};
  const FrameHandle corner_ids[4] = {FrameHandle::CornerTopLeft, FrameHandle::CornerTopRight,
                                     FrameHandle::CornerBottomLeft, FrameHandle::CornerBottomRight};
  FrameHandle best = FrameHandle::None;
  double best_d = corner_tol;
  for (int i = 0; i < 4; ++i) {
    const double d = dist(point, corners[i]);
    if (d <= best_d) {
      best_d = d;
      best = corner_ids[i];
    }
  }
  if (best != FrameHandle::None) {
    return best;
  }

  // Edge-midpoint resize handles next.
  const arbc::Vec2 mids[4] = {mid(tl, tr), mid(bl, br), mid(tl, bl), mid(tr, br)};
  const FrameHandle edge_ids[4] = {FrameHandle::EdgeTop, FrameHandle::EdgeBottom,
                                   FrameHandle::EdgeLeft, FrameHandle::EdgeRight};
  best_d = corner_tol;
  for (int i = 0; i < 4; ++i) {
    const double d = dist(point, mids[i]);
    if (d <= best_d) {
      best_d = d;
      best = edge_ids[i];
    }
  }
  if (best != FrameHandle::None) {
    return best;
  }

  // The move grab: the border line between the handles (within `edge_tol` of an edge, D7).
  const double edge_d = std::min({dist_to_segment(point, tl, tr), dist_to_segment(point, bl, br),
                                  dist_to_segment(point, tl, bl), dist_to_segment(point, tr, br)});
  if (edge_d <= edge_tol) {
    return FrameHandle::Move;
  }

  // The label tab just outside the top edge (also a move grab). Device-space local coords in
  // the frame's own basis; the tab is ~two handles tall (`corner_tol` converted to device px).
  const arbc::Vec2 local = inv->apply(point);
  const double s = vlen(frame.a, frame.b); // composition units per device pixel (uniform)
  const double label_h = s > 0.0 ? corner_tol / s * 2.0 : 0.0;
  if (local.x >= 0.0 && local.x <= nw && local.y < 0.0 && local.y >= -label_h) {
    return FrameHandle::Label;
  }

  return FrameHandle::None; // strictly interior (click-through, D7) or a miss
}

arbc::Affine recrop_frame(const arbc::Affine& frame, int native_w, int native_h, FrameHandle handle,
                          arbc::Vec2 pointer) {
  if (native_w <= 0 || native_h <= 0 || !is_resize_handle(handle) || !frame.inverse()) {
    return frame; // nothing to re-crop / degenerate frame: a safe no-op
  }
  const double nw = native_w;
  const double nh = native_h;
  const arbc::Vec2 tl = frame.apply({0.0, 0.0});
  const arbc::Vec2 tr = frame.apply({nw, 0.0});
  const arbc::Vec2 bl = frame.apply({0.0, nh});
  const arbc::Vec2 br = frame.apply({nw, nh});

  // The dragged reference point and the fixed pivot (opposite corner / edge midpoint).
  arbc::Vec2 dragged{};
  arbc::Vec2 pivot{};
  switch (handle) {
  case FrameHandle::CornerTopLeft:
    dragged = tl;
    pivot = br;
    break;
  case FrameHandle::CornerTopRight:
    dragged = tr;
    pivot = bl;
    break;
  case FrameHandle::CornerBottomLeft:
    dragged = bl;
    pivot = tr;
    break;
  case FrameHandle::CornerBottomRight:
    dragged = br;
    pivot = tl;
    break;
  case FrameHandle::EdgeLeft:
    dragged = mid(tl, bl);
    pivot = mid(tr, br);
    break;
  case FrameHandle::EdgeRight:
    dragged = mid(tr, br);
    pivot = mid(tl, bl);
    break;
  case FrameHandle::EdgeTop:
    dragged = mid(tl, tr);
    pivot = mid(bl, br);
    break;
  case FrameHandle::EdgeBottom:
    dragged = mid(bl, br);
    pivot = mid(tl, tr);
    break;
  default:
    return frame;
  }

  const double rx = dragged.x - pivot.x;
  const double ry = dragged.y - pivot.y;
  const double ref_len2 = rx * rx + ry * ry;
  if (!(ref_len2 > 0.0)) {
    return frame; // collapsed reference axis: a safe no-op
  }
  // Uniform (aspect-locked, square-pixel-preserving) scale factor: the projection of the
  // pointer displacement onto the pivot->dragged axis, normalized. Clamped to a small
  // positive minimum so the covered region never collapses or flips (D8: re-crop, holds
  // resolution — the pixel count is not an input here and cannot change).
  double k = ((pointer.x - pivot.x) * rx + (pointer.y - pivot.y) * ry) / ref_len2;
  constexpr double k_min = 1e-3;
  if (!(k > k_min)) {
    k = k_min;
  }
  return compose(scale_about(k, pivot), frame);
}

arbc::Affine move_frame(const arbc::Affine& frame, double dx, double dy) {
  // Translate the covered composition region — the linear part is untouched, so the crop
  // pans with no scale/rotation change (D7 move).
  arbc::Affine out = frame;
  out.tx += dx;
  out.ty += dy;
  return out;
}

arbc::Affine dutch_frame(const arbc::Affine& frame, int native_w, int native_h, double angle_rad,
                         bool snap_15) {
  if (native_w <= 0 || native_h <= 0 || !frame.inverse()) {
    return frame; // degenerate frame/native: a safe no-op
  }
  double angle = angle_rad;
  if (snap_15) {
    constexpr double step = 3.14159265358979323846 / 12.0; // 15 degrees (Shift, D9)
    angle = std::round(angle / step) * step;
  }
  // A pure rotation about the covered region's center: lengths preserved, so the covered
  // size and the resolution are unchanged (rotation only).
  const arbc::Vec2 center = frame.apply({native_w * 0.5, native_h * 0.5});
  return compose(rotate_about(angle, center), frame);
}

arbc::Affine refit_frame_to_aspect(const arbc::Affine& frame, int old_native_w, int new_native_w) {
  if (old_native_w <= 0 || new_native_w <= 0) {
    return frame; // degenerate width: a safe no-op
  }
  // Hold the top-left corner and the horizontal extent; scale the linear part by
  // old_w/new_w so the SAME composition width now spans `new_native_w` device pixels at
  // square pixels, and the vertical extent follows the new aspect at render (D-manip-7).
  const double k = static_cast<double>(old_native_w) / static_cast<double>(new_native_w);
  const arbc::Vec2 top_left = frame.apply({0.0, 0.0});
  return compose(scale_about(k, top_left), frame);
}

} // namespace ace::interact
