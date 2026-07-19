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

// --- New shot from view (editor.cameras.model; D2 §"new shot from view") ------

// The (frame placement, output resolution) a "new shot from view" mints from the
// transient viewport framing (Constraint 6). The frame is a composition-space `Affine`
// — the placement of the shot's output rectangle, the same shape a cell's layer
// transform takes (D7) — and the resolution is device pixels (D9: frame != resolution).
struct ShotFraming {
  arbc::Affine frame; // composition-space placement of the output frame (device -> comp)
  int width = 0;
  int height = 0;
};

// Snapshot the transient viewport framing into a shot camera's (frame, resolution),
// keeping the operation pure L1 (Constraint 6). The viewport `camera` maps composition
// units -> device pixels over a `pane_w`x`pane_h` on-screen canvas (`Presenter::camera`,
// D-nav-1); the shot reproduces that framing as its FRAME = the inverse camera
// (device -> composition, so rendering the shot at its own resolution reproduces the
// view) and its RESOLUTION = the pane size. A degenerate (non-invertible) camera or a
// non-positive pane yields an identity frame (nothing to promote). The inverse
// render-through-camera derivation (frame+resolution -> `arbc::Viewport`) is
// `editor.cameras.export`'s, deliberately out of scope here.
ShotFraming new_shot_from_view(const arbc::Affine& camera, int pane_w, int pane_h);

// --- Look through a shot (editor.cameras.look_through; D2/D9/D18) --------------

// The inverse of `new_shot_from_view` (D-look_through-4): given a shot's FRAME
// (device -> composition, the binding layer's `Affine`) and its NATIVE output
// resolution, the comp -> device render camera that renders the shot's exact crop at
// an arbitrary `(out_w, out_h)`. It is `frame.inverse()` (the comp -> device camera at
// the shot's own resolution — the round-trip of `new_shot_from_view`,
// `interact.hpp:71-73`) post-scaled by `out/native` in device space, so the SAME
// framed composition region fills the output at any resolution: at `out == native` it
// reproduces the viewport camera the shot was minted from; at `out == k*native` it is
// that native camera scaled by `k` (the pane-fit preview and export's D14 N x
// multiplier, Constraint 4). A non-positive native/out resolution or a non-invertible
// frame yields identity — a safe no-op, never a div-by-zero. `editor.cameras.export`
// reuses this verbatim (the sibling leaf simply consumes it).
arbc::Affine viewport_camera_for_shot(const arbc::Affine& frame, int native_w, int native_h,
                                      int out_w, int out_h);

// The pane-fit "look through" wrapper (D-look_through-3): the output size + comp ->
// device camera to preview shot `frame` / `(shot_w, shot_h)` inside a `pane_w`x`pane_h`
// canvas — the shot's aspect scaled to fit the pane (the letterbox dimensions), NOT the
// shot's full export resolution. `camera == viewport_camera_for_shot(frame, shot_w,
// shot_h, out_w, out_h)`, so the preview is byte-convergent to `render_offline` through
// the shot. A non-positive shot/pane resolution yields a zero-size, identity-camera
// no-op (the caller falls back to the free viewport).
struct LookThrough {
  int out_w = 0;
  int out_h = 0;
  arbc::Affine camera = arbc::Affine::identity();
};
LookThrough look_through(const arbc::Affine& frame, int shot_w, int shot_h, int pane_w, int pane_h);

} // namespace ace::interact
