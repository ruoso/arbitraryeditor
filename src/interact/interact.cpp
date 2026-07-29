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
  // Reset-to-fit is the ORIGIN-ANCHORED specialization of the positioned fit: framing
  // the content's own extent from the origin (D-nav_aids-2). One implementation, one
  // degenerate guard — the unit pins their field-for-field equality so they never drift.
  return fit_region(arbc::Rect{0.0, 0.0, content_w, content_h}, pane_w, pane_h);
}

arbc::Affine fit_region(const arbc::Rect& region, double pane_w, double pane_h) {
  const double region_w = region.width();
  const double region_h = region.height();
  if (region.empty() || !(region_w > 0.0) || !(region_h > 0.0) || !std::isfinite(region_w) ||
      !std::isfinite(region_h) || !std::isfinite(region.x0) || !std::isfinite(region.y0) ||
      !(pane_w > 0.0) || !(pane_h > 0.0)) {
    return arbc::Affine::identity(); // degenerate region/pane: nothing to fit
  }
  // Uniform scale so the whole region fits the pane at the pane's OWN aspect, centered in
  // the leftover axis (Constraint 7 — a view change, never re-aspected). The extra
  // `-scale * x0/y0` over `fit` is exactly what carries the region's position (its x0,y0),
  // which the origin-anchored `fit` cannot express.
  const double scale = std::min(pane_w / region_w, pane_h / region_h);
  const double tx = (pane_w - region_w * scale) * 0.5 - scale * region.x0;
  const double ty = (pane_h - region_h * scale) * 0.5 - scale * region.y0;
  return arbc::Affine{scale, 0.0, 0.0, scale, tx, ty};
}

arbc::Affine place_in_view(const arbc::Affine& view, int pane_w, int pane_h,
                           const std::optional<arbc::Rect>& content_bounds, double fill_fraction) {
  if (pane_w <= 0 || pane_h <= 0 || !content_bounds.has_value() || content_bounds->empty()) {
    return arbc::Affine::identity(); // unbounded content / no pane: nothing to centre
  }
  if (!(fill_fraction > 0.0) || !std::isfinite(fill_fraction)) {
    return arbc::Affine::identity();
  }
  // The composition region the pane currently shows: the pane rectangle pulled back
  // through the viewport camera. A collapsed camera has no inverse — nothing visible.
  const std::optional<arbc::Affine> inverse = view.inverse();
  if (!inverse.has_value()) {
    return arbc::Affine::identity();
  }
  const arbc::Rect visible = inverse->map_rect(
      arbc::Rect::from_size(static_cast<double>(pane_w), static_cast<double>(pane_h)));
  if (visible.empty()) {
    return arbc::Affine::identity();
  }
  const arbc::Rect& extent = *content_bounds;
  const double longest = std::max(extent.width(), extent.height());
  const double target = std::min(visible.width(), visible.height()) * fill_fraction;
  const double scale = target / longest;
  // Centre the content's own extent on the visible region's centre.
  const double tx = (visible.x0 + visible.x1) * 0.5 - scale * (extent.x0 + extent.x1) * 0.5;
  const double ty = (visible.y0 + visible.y1) * 0.5 - scale * (extent.y0 + extent.y1) * 0.5;
  if (!(scale > 0.0) || !std::isfinite(scale) || !std::isfinite(tx) || !std::isfinite(ty)) {
    return arbc::Affine::identity(); // an infinite visible region etc. — never a NaN
  }
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

ShotFraming shot_from_extent(const arbc::Rect& extent) {
  const double region_w = extent.width();
  const double region_h = extent.height();
  if (extent.empty() || !std::isfinite(region_w) || !std::isfinite(region_h) ||
      !std::isfinite(extent.x0) || !std::isfinite(extent.y0)) {
    return ShotFraming{}; // {identity, 0, 0}: the caller's "nothing to frame" sentinel
  }
  // 1 composition unit = 1 pixel (D23), scaled down uniformly when the longer side would
  // exceed the mint clamp — so the aspect survives the clamp instead of being letterboxed.
  const double longest = std::max(region_w, region_h);
  const double k_max = static_cast<double>(k_max_mint_resolution);
  const double clamp_scale = longest > k_max ? k_max / longest : 1.0;
  ShotFraming shot;
  shot.width =
      std::clamp(static_cast<int>(std::lround(region_w * clamp_scale)), 1, k_max_mint_resolution);
  shot.height =
      std::clamp(static_cast<int>(std::lround(region_h * clamp_scale)), 1, k_max_mint_resolution);

  // Rounding to whole pixels perturbs the aspect by up to one pixel, so re-fit the covered
  // region to the ROUNDED aspect: EXPAND the short axis about the center (never crop, so
  // nothing the user selected falls outside) and pixels come out exactly square (D9).
  const double res_w = static_cast<double>(shot.width);
  const double res_h = static_cast<double>(shot.height);
  double covered_w = region_w;
  double covered_h = region_h;
  if (region_w * res_h >= region_h * res_w) {
    covered_h = region_w * res_h / res_w; // the region is wider than the aspect: grow height
  } else {
    covered_w = region_h * res_w / res_h; // taller than the aspect: grow width
  }
  const double center_x = (extent.x0 + extent.x1) * 0.5;
  const double center_y = (extent.y0 + extent.y1) * 0.5;
  // device -> composition, exactly as `new_shot_from_view` orients it: [0,W]x[0,H] onto the
  // covered region. Square pixels means the two scales agree: covered_w/W == covered_h/H.
  shot.frame = arbc::Affine{
      covered_w / res_w,         0.0, 0.0, covered_h / res_h, center_x - covered_w * 0.5,
      center_y - covered_h * 0.5};
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

ResolutionHealth resolution_health(int native_w, int native_h, const arbc::Affine& placement,
                                   int cam_w, int cam_h, const arbc::Affine& cam_frame) {
  ResolutionHealth health;
  if (native_w <= 0 || native_h <= 0 || cam_w <= 0 || cam_h <= 0) {
    return health; // no native floor / degenerate resolution: N/A, never a false Soft
  }
  // The placed region (composition) of the cell's native extent, and the camera's covered
  // region of its output extent — each affine's own map of its pixel rectangle. A collapsed
  // placement or frame maps its rectangle to a zero-area sliver (`empty()`): N/A, never a
  // div-by-zero (Constraint 4). AABB-based, so an axis-aligned scale is byte-exact and a
  // rotation carries the documented tolerance the caller's tests pin.
  const arbc::Rect placed = placement.map_rect(
      arbc::Rect::from_size(static_cast<double>(native_w), static_cast<double>(native_h)));
  const arbc::Rect shot = cam_frame.map_rect(
      arbc::Rect::from_size(static_cast<double>(cam_w), static_cast<double>(cam_h)));
  if (placed.empty() || shot.empty()) {
    return health;
  }
  // Per axis: (camera output px per composition unit) / (cell native px per composition
  // unit). The worst (largest) axis is the softness the readout leads with — a cell stretched
  // more on one axis is judged by that axis (D-resolution-4).
  const double rx = (static_cast<double>(cam_w) / shot.width()) /
                    (static_cast<double>(native_w) / placed.width());
  const double ry = (static_cast<double>(cam_h) / shot.height()) /
                    (static_cast<double>(native_h) / placed.height());
  const double ratio = std::max(rx, ry);
  if (!std::isfinite(ratio) || !(ratio > 0.0)) {
    return health; // an infinite / non-finite region etc. — never a NaN verdict
  }
  health.ratio = ratio;
  // Softness is any magnification above native (D-resolution-4): no invented tolerance
  // band, the raw ratio is surfaced so the user judges severity.
  health.verdict = ratio > 1.0 ? ResolutionVerdict::Soft : ResolutionVerdict::Crisp;
  return health;
}

PlacementReadout decompose_placement(const arbc::Affine& placement,
                                     const std::optional<arbc::Rect>& content_bounds) {
  PlacementReadout out;
  // Refuse a non-finite or degenerate (zero-`det`) placement: no defined rotation/shear, so the
  // readout stays all-zero and invalid rather than emitting a NaN (Constraint 1).
  const bool finite = std::isfinite(placement.a) && std::isfinite(placement.b) &&
                      std::isfinite(placement.c) && std::isfinite(placement.d) &&
                      std::isfinite(placement.tx) && std::isfinite(placement.ty);
  const double sx = std::sqrt(placement.a * placement.a + placement.b * placement.b);
  const double det = placement.det();
  if (!finite || !(sx > 0.0) || det == 0.0) {
    return out;
  }
  // QR-style decomposition of the linear part [[a, c], [b, d]] (its columns are the mapped x/y
  // axes): rotation is the angle of the mapped x-axis; removing it leaves the y-axis as
  // (shear_component, sy) in the rotated frame, with sy = det/sx (signed) and the shear
  // component (a*c + b*d)/sx. A pure rotation+scale has shear_component == 0, so `shear_deg` is
  // exactly 0 there (the property the unit test pins).
  const double k_rad_to_deg = 180.0 / std::acos(-1.0);
  out.position = arbc::Vec2{placement.tx, placement.ty};
  out.scale_x = sx;
  out.scale_y = det / sx;
  const double shear_component = (placement.a * placement.c + placement.b * placement.d) / sx;
  out.rotation_deg = std::atan2(placement.b, placement.a) * k_rad_to_deg;
  out.shear_deg = std::atan2(shear_component, out.scale_y) * k_rad_to_deg;
  if (content_bounds.has_value()) {
    out.placed = placement.map_rect(*content_bounds);
  }
  out.valid = true;
  return out;
}

// --- Cell transform gizmo (editor.cells.gizmo; D7/D8/§6) ----------------------

namespace {

constexpr double k_cell_min_scale = 1e-3; // below this a scale drag collapses/flips: no-op
constexpr double k_basis_eps = 1e-9;      // a box axis at/below this is degenerate

bool finite_rect(const arbc::Rect& r) {
  return std::isfinite(r.x0) && std::isfinite(r.y0) && std::isfinite(r.x1) && std::isfinite(r.y1);
}
bool finite_pt(arbc::Vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
bool finite_affine(const arbc::Affine& m) {
  return std::isfinite(m.a) && std::isfinite(m.b) && std::isfinite(m.c) && std::isfinite(m.d) &&
         std::isfinite(m.tx) && std::isfinite(m.ty);
}

// The (U, V) coordinates of composition-space vector `w` in the placed box's own (possibly
// non-orthogonal, sheared) edge basis B = [U V]; `det` is B's determinant (nonzero).
arbc::Vec2 basis_coords(arbc::Vec2 w, arbc::Vec2 u, arbc::Vec2 v, double det) {
  return {(w.x * v.y - w.y * v.x) / det, (-w.x * u.y + w.y * u.x) / det};
}

// The affine that applies the 2x2 `S = [[s00,s01],[s10,s11]]` in the box basis B = [U V] about
// composition pivot `pv`: M = B S B^-1, translated so `pv` is fixed. Scale is S = diag(sx,sy);
// a shear is an off-diagonal S. The caller guards `det != 0`.
arbc::Affine basis_delta(arbc::Vec2 u, arbc::Vec2 v, double s00, double s01, double s10, double s11,
                         arbc::Vec2 pv) {
  const double det = u.x * v.y - v.x * u.y;
  const double bs00 = s00 * u.x + s10 * v.x;
  const double bs01 = s01 * u.x + s11 * v.x;
  const double bs10 = s00 * u.y + s10 * v.y;
  const double bs11 = s01 * u.y + s11 * v.y;
  const double a = (bs00 * v.y - bs01 * u.y) / det;
  const double c = (-bs00 * v.x + bs01 * u.x) / det;
  const double b = (bs10 * v.y - bs11 * u.y) / det;
  const double d = (-bs10 * v.x + bs11 * u.x) / det;
  return arbc::Affine{a, b, c, d, pv.x - (a * pv.x + c * pv.y), pv.y - (b * pv.x + d * pv.y)};
}

// The four placed corners + the two edge basis vectors of `placement` over `extent`.
struct PlacedBox {
  arbc::Vec2 tl, tr, bl, br, u, v;
};
PlacedBox placed_box(const arbc::Affine& placement, const arbc::Rect& extent) {
  PlacedBox box;
  box.tl = placement.apply({extent.x0, extent.y0});
  box.tr = placement.apply({extent.x1, extent.y0});
  box.bl = placement.apply({extent.x0, extent.y1});
  box.br = placement.apply({extent.x1, extent.y1});
  box.u = {box.tr.x - box.tl.x, box.tr.y - box.tl.y};
  box.v = {box.bl.x - box.tl.x, box.bl.y - box.tl.y};
  return box;
}

// The dragged reference point and the default (opposite) fixed point for a scale/shear handle.
bool handle_anchors(const PlacedBox& box, CellHandle handle, arbc::Vec2& dragged,
                    arbc::Vec2& opposite) {
  switch (handle) {
  case CellHandle::ScaleTopLeft:
    dragged = box.tl;
    opposite = box.br;
    return true;
  case CellHandle::ScaleTopRight:
    dragged = box.tr;
    opposite = box.bl;
    return true;
  case CellHandle::ScaleBottomLeft:
    dragged = box.bl;
    opposite = box.tr;
    return true;
  case CellHandle::ScaleBottomRight:
    dragged = box.br;
    opposite = box.tl;
    return true;
  case CellHandle::ScaleLeft:
    dragged = mid(box.tl, box.bl);
    opposite = mid(box.tr, box.br);
    return true;
  case CellHandle::ScaleRight:
    dragged = mid(box.tr, box.br);
    opposite = mid(box.tl, box.bl);
    return true;
  case CellHandle::ScaleTop:
    dragged = mid(box.tl, box.tr);
    opposite = mid(box.bl, box.br);
    return true;
  case CellHandle::ScaleBottom:
    dragged = mid(box.bl, box.br);
    opposite = mid(box.tl, box.tr);
    return true;
  default:
    return false;
  }
}

} // namespace

bool is_cell_corner(CellHandle handle) {
  switch (handle) {
  case CellHandle::ScaleTopLeft:
  case CellHandle::ScaleTopRight:
  case CellHandle::ScaleBottomLeft:
  case CellHandle::ScaleBottomRight:
    return true;
  default:
    return false;
  }
}

bool is_cell_edge(CellHandle handle) {
  switch (handle) {
  case CellHandle::ScaleLeft:
  case CellHandle::ScaleRight:
  case CellHandle::ScaleTop:
  case CellHandle::ScaleBottom:
    return true;
  default:
    return false;
  }
}

bool is_cell_scale_handle(CellHandle handle) {
  return is_cell_corner(handle) || is_cell_edge(handle);
}

arbc::Vec2 cell_pivot(const arbc::Affine& placement, const arbc::Rect& extent) {
  return placement.apply({(extent.x0 + extent.x1) * 0.5, (extent.y0 + extent.y1) * 0.5});
}

double drag_angle(arbc::Vec2 pivot, arbc::Vec2 from, arbc::Vec2 to) {
  return std::atan2(to.y - pivot.y, to.x - pivot.x) -
         std::atan2(from.y - pivot.y, from.x - pivot.x);
}

arbc::Affine move_cell(const arbc::Affine& placement, double dx, double dy) {
  arbc::Affine out = placement;
  out.tx += dx;
  out.ty += dy;
  return out;
}

arbc::Affine scale_cell(const arbc::Affine& placement, const arbc::Rect& extent, CellHandle handle,
                        arbc::Vec2 pointer, arbc::Vec2 pivot, bool free_distort, bool about_pivot) {
  if (!is_cell_scale_handle(handle) || extent.empty() || !finite_rect(extent) ||
      !placement.inverse() || !finite_pt(pointer) || (about_pivot && !finite_pt(pivot))) {
    return placement; // a non-scale handle / degenerate input: a safe no-op (Constraint 4)
  }
  const PlacedBox box = placed_box(placement, extent);
  arbc::Vec2 dragged{};
  arbc::Vec2 opposite{};
  if (!handle_anchors(box, handle, dragged, opposite)) {
    return placement;
  }
  const arbc::Vec2 fixed = about_pivot ? pivot : opposite;

  if (is_cell_corner(handle) && !free_distort) {
    // Proportional (uniform, aspect-preserving) about `fixed` — the recrop_frame idiom.
    const double rx = dragged.x - fixed.x;
    const double ry = dragged.y - fixed.y;
    const double ref = rx * rx + ry * ry;
    if (!(ref > 0.0)) {
      return placement;
    }
    const double k = ((pointer.x - fixed.x) * rx + (pointer.y - fixed.y) * ry) / ref;
    if (!(k > k_cell_min_scale)) {
      return placement; // a collapse/flip drag: unchanged, never a degenerate placement
    }
    return compose(scale_about(k, fixed), placement);
  }

  // Anisotropic scale in the box's OWN axes about `fixed` — 1D for an edge (the opposite-axis
  // coordinate of the dragged point is ~0, so that axis stays at 1), free x/y for a Shift corner.
  const double det = box.u.x * box.v.y - box.v.x * box.u.y;
  if (!(std::abs(det) > 0.0)) {
    return placement;
  }
  const arbc::Vec2 cd = basis_coords({dragged.x - fixed.x, dragged.y - fixed.y}, box.u, box.v, det);
  const arbc::Vec2 cp = basis_coords({pointer.x - fixed.x, pointer.y - fixed.y}, box.u, box.v, det);
  const double sx = std::abs(cd.x) > k_basis_eps ? cp.x / cd.x : 1.0;
  const double sy = std::abs(cd.y) > k_basis_eps ? cp.y / cd.y : 1.0;
  if (!std::isfinite(sx) || !std::isfinite(sy) || !(sx > k_cell_min_scale) ||
      !(sy > k_cell_min_scale)) {
    return placement; // a collapse/flip drag on either active axis: unchanged
  }
  return compose(basis_delta(box.u, box.v, sx, 0.0, 0.0, sy, fixed), placement);
}

arbc::Affine rotate_cell(const arbc::Affine& placement, arbc::Vec2 pivot, double angle_rad,
                         bool snap_15) {
  if (!placement.inverse() || !finite_pt(pivot) || !std::isfinite(angle_rad)) {
    return placement;
  }
  double angle = angle_rad;
  if (snap_15) {
    constexpr double step = 3.14159265358979323846 / 12.0; // 15 degrees (Shift, §6)
    angle = std::round(angle / step) * step;
  }
  return compose(rotate_about(angle, pivot), placement);
}

arbc::Affine shear_cell(const arbc::Affine& placement, const arbc::Rect& extent, CellHandle edge,
                        arbc::Vec2 pointer, arbc::Vec2 pivot) {
  if (!is_cell_edge(edge) || extent.empty() || !finite_rect(extent) || !placement.inverse() ||
      !finite_pt(pointer) || !finite_pt(pivot)) {
    return placement;
  }
  const PlacedBox box = placed_box(placement, extent);
  arbc::Vec2 dragged{};
  arbc::Vec2 opposite{};
  if (!handle_anchors(box, edge, dragged, opposite)) {
    return placement;
  }
  const double det = box.u.x * box.v.y - box.v.x * box.u.y;
  if (!(std::abs(det) > 0.0)) {
    return placement;
  }
  const arbc::Vec2 cd = basis_coords({dragged.x - pivot.x, dragged.y - pivot.y}, box.u, box.v, det);
  const arbc::Vec2 cp = basis_coords({pointer.x - pivot.x, pointer.y - pivot.y}, box.u, box.v, det);
  if (edge == CellHandle::ScaleTop || edge == CellHandle::ScaleBottom) {
    // Horizontal shear: slide the box's width coordinate as a function of its height coordinate.
    if (!(std::abs(cd.y) > k_basis_eps)) {
      return placement;
    }
    const double sh = (cp.x - cd.x) / cd.y;
    if (!std::isfinite(sh)) {
      return placement;
    }
    return compose(basis_delta(box.u, box.v, 1.0, sh, 0.0, 1.0, pivot), placement);
  }
  // Vertical shear on a Left/Right edge: slide height as a function of width.
  if (!(std::abs(cd.x) > k_basis_eps)) {
    return placement;
  }
  const double sh = (cp.y - cd.y) / cd.x;
  if (!std::isfinite(sh)) {
    return placement;
  }
  return compose(basis_delta(box.u, box.v, 1.0, 0.0, sh, 1.0, pivot), placement);
}

std::vector<arbc::Affine> group_transform(const arbc::Affine& old_union,
                                          const arbc::Affine& new_union,
                                          std::span<const arbc::Affine> start_placements) {
  std::vector<arbc::Affine> out;
  out.reserve(start_placements.size());
  const std::optional<arbc::Affine> old_inv = old_union.inverse();
  if (!old_inv || !finite_affine(new_union)) {
    // A degenerate union carries no derivable delta: leave every member untouched (an identity
    // gesture, refuse-rather-than-guess). The caller reads "nothing changed" and commits nothing.
    for (const arbc::Affine& start : start_placements) {
      out.push_back(start);
    }
    return out;
  }
  // D = new_union ∘ old_union⁻¹ — the composition-space change the gesture made to the union box.
  const arbc::Affine delta = compose(new_union, *old_inv);
  if (!finite_affine(delta)) {
    for (const arbc::Affine& start : start_placements) {
      out.push_back(start);
    }
    return out;
  }
  // Each member's new placement is D applied on top of its own start (D-group_transform-3): one
  // shared delta, composed independently, so the result is order-independent.
  for (const arbc::Affine& start : start_placements) {
    out.push_back(compose(delta, start));
  }
  return out;
}

} // namespace ace::interact
