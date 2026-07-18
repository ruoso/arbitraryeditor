#pragma once

#include <arbc/base/geometry.hpp>  // arbc::Vec2
#include <arbc/base/transform.hpp> // arbc::Affine

namespace ace::interact {

// The interact component (L1): pure interaction math — hit-test, gizmo,
// snapping, brush footprint. UI-agnostic; unit-tested headless (docs §8/§9).
const char* name();

// Brush size mapping (docs/00-design.md D5): the screen-space brush is measured
// as a fraction of the active camera's view; this maps that fraction to a size
// in composition units. `view_fraction` is the brush diameter as a fraction of
// the view's shorter edge; `view_short_edge_units` is that edge in composition
// units.
double brush_units(double view_fraction, double view_short_edge_units);

// --- Viewport camera navigation (editor.canvas.nav; D2/D9) -------------------
// The camera is an `arbc::Affine` mapping composition units -> device pixels (the
// same seam `HostViewport::set_camera` drives). All three take/return a camera by
// value: pure, UI-agnostic geometry, unit-tested headless (D-nav-2).

// Pan by a device-pixel drag: post-translates the camera in DEVICE space so the
// composition point under the cursor follows the drag exactly. The visible
// framing shifts by (device_dx, device_dy) / max_scale composition units — drag
// right by `d` px at scale `s` moves the framing by `d/s` composition units.
arbc::Affine pan(const arbc::Affine& camera, double device_dx, double device_dy);

// Zoom about a device-pixel focus by `factor` (>1 zooms in, <1 out): scales the
// camera's linear part by `factor` while holding the composition point under
// `device_focus` invariant (Constraint 4) —
//   zoom(cam, f, k).inverse().apply(f) == cam.inverse().apply(f).
arbc::Affine zoom(const arbc::Affine& camera, arbc::Vec2 device_focus, double factor);

// A scale-bar reading (D2 §3 / D-nav-6): a nice-number (1/2/5·10ⁿ) length of the
// composition, drawn `device_px` device pixels wide. `units` is composition units;
// composition-units-per-device-pixel is `1 / camera.max_scale()`, so
// `units == device_px / camera.max_scale()`. Never a "%".
struct ScaleBar {
  double units = 0.0;     // the nice-number length in composition units
  double device_px = 0.0; // its rendered width in device pixels
};

// The largest 1/2/5·10ⁿ composition length whose device width does not exceed
// `target_px` (so `device_px` lands in `(target_px/2.5, target_px]`). A
// degenerate/non-finite camera scale yields a zero bar (nothing to draw).
ScaleBar scale_bar(const arbc::Affine& camera, double target_px);

// The reset-to-fit camera (D-nav-7): a uniform-scale, centered `Affine` framing a
// `content_w`×`content_h` composition into a `pane_w`×`pane_h` pane — the "don't
// get lost in unbounded space" recovery. A degenerate content/pane yields
// identity (nothing to fit).
arbc::Affine fit(double content_w, double content_h, double pane_w, double pane_h);

} // namespace ace::interact
