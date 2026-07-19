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

} // namespace ace::interact
