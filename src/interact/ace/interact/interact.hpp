#pragma once

#include <arbc/base/geometry.hpp>  // arbc::Rect, arbc::Vec2
#include <arbc/base/transform.hpp> // arbc::Affine

#include <optional>

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

// --- Provisional cell placement (editor.cells.model; D3/D8/A16) ---------------

// The fraction of the visible region's SHORTER edge a provisionally-placed cell's
// longer edge occupies. Half — big enough to see and grab, small enough to leave the
// composition around it visible.
inline constexpr double k_default_placement_fill = 0.5;

// The provisional placement affine for a freshly-inserted cell (Constraint 6/7): the
// content's own extent, uniformly scaled so its LONGER edge is `fill_fraction` of the
// shorter edge of the region `view` currently shows over a `pane_w`x`pane_h` canvas,
// and centred in that region. `view` is the transient viewport camera (composition
// units -> device pixels, `Presenter::camera`); `content_bounds` is whatever
// `arbc::Content::bounds()` reported.
//
// Primitives only — no `scene` type crosses this signature (D-manip-2/D-cells_model-6),
// which is what lets `editor.panels.overview` later hand `scene::add_cell` a
// drag-derived affine and `editor.import.image` a native-px->units 1:1 affine with no
// change to `scene`.
//
// UNBOUNDED content (`nullopt`, e.g. a factory-built `org.arbc.solid`, whose config
// grammar admits no bounds) yields IDENTITY, because scaling an unbounded fill is
// meaningless (D-cells_model-3). So do all the degenerate ends — a non-invertible
// view, a non-positive pane, an empty content rect, a non-positive/non-finite fill
// fraction — rather than producing NaNs (the D-fit_bounds-3 fallback discipline).
// Resolution NEVER enters this computation and this affine never enters a resolution
// (D8's independence rule).
arbc::Affine place_in_view(const arbc::Affine& view, int pane_w, int pane_h,
                           const std::optional<arbc::Rect>& content_bounds,
                           double fill_fraction = k_default_placement_fill);

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

// --- Frame selection (editor.cameras.frame_selection; D23) --------------------

// The largest side a MINTED camera may be given. A composition-scale selection must not mint
// a terapixel camera whose export (D14) would allocate terabytes; the longer side is clamped
// to this with the aspect preserved, and the inspector's W x H fields are the documented
// escape hatch for anything the clamp changed (D9).
inline constexpr int k_max_mint_resolution = 8192;

// The (frame, resolution) a "frame selection" mints from the composition-space region
// `extent` — the SECOND producer of `ShotFraming`, sharing D23's one derivation rule with
// `new_shot_from_view` rather than inventing a second shape.
//
// The resolution is the region at 1 COMPOSITION UNIT = 1 PIXEL, rounded to whole pixels,
// clamped so the longer side never exceeds `k_max_mint_resolution` (aspect preserved) and so
// each side is at least 1 (D-frame_selection-2). That is the zoom-1:1 specialization of the
// rule `new_shot_from_view` already ships, which is what lets D23 state ONE rule for both: an
// unscaled cell framed exactly reproduces its own native pixel count (D8 read back through a
// camera), and the derivation is pure — no pane, no zoom, no `ViewFraming`.
//
// The frame is `extent` EXPANDED ABOUT ITS CENTER — never cropped — to the ROUNDED
// resolution's aspect, so pixels stay square (`a == d`, D9's aspect-lock) and nothing that
// was selected falls outside the minted frame (Constraint 3). Orientation is the SAME
// device -> composition convention every shipped consumer reads (`interact.hpp:143-145`): it
// places `[0,width]x[0,height]` onto the framed region, so `viewport_camera_for_shot` renders
// exactly that crop. The frame is axis-aligned — the minted camera has no dutch rotation (D9
// makes dutch modifier-gated and gizmo-driven), which is why an AABB is what "fit to the
// selection" means (D-frame_selection-4).
//
// A degenerate `extent` (empty, inverted, or non-finite) yields `{identity, 0, 0}` — the
// caller's "nothing to frame" sentinel, refused rather than guessed at (D-fit_bounds-3).
ShotFraming shot_from_extent(const arbc::Rect& extent);

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

// --- Camera frame manipulation (editor.cameras.manip; D7/D8/D9) ---------------
// The frame is the binding layer's `Affine` (device -> composition): it places the
// camera's output rectangle [0,native_w]x[0,native_h] into composition space. These
// helpers reframe it as PURE placement math — they take/return the frame `Affine`
// (+ the native output rectangle, whose ASPECT `native_w:native_h` the covered region
// is aspect-locked to for square pixels) and NEVER a resolution value: a frame edit is
// a re-crop / move / dutch and can never change the camera's pixel count (D8 anti-
// resample). `native_w`/`native_h` are the aspect basis — primitive ints exactly as
// `viewport_camera_for_shot` takes them, so no `scene` dependency is introduced
// (D-manip-3). All are unit-tested headless (the bulk of the coverage).

// Which part of the frame outline a composition-space point grabs (D7: border/label
// grab, interior click-through). Corners/edges are resize handles; Label is the move
// grab; None is an interior click-through (or a miss).
enum class FrameHandle {
  None,
  Move,
  Label,
  EdgeLeft,
  EdgeRight,
  EdgeTop,
  EdgeBottom,
  CornerTopLeft,
  CornerTopRight,
  CornerBottomLeft,
  CornerBottomRight,
};

// True for the eight corner/edge resize handles (not None/Label).
bool is_resize_handle(FrameHandle handle);

// Hit-test a composition-space `point` against the frame outline. Corners win over
// edges within `corner_tol` (D7 near-corner precedence); a point within `edge_tol` of
// an edge segment (but not a corner) grabs that edge; a point in the label band just
// outside the top edge grabs Label (the move grab); a strictly-interior or far-outside
// point is None (interior click-through, D7). Tolerances are composition units (the
// caller converts screen px through the view scale). A non-invertible / degenerate
// frame or non-positive native is None.
FrameHandle hit_frame(const arbc::Affine& frame, int native_w, int native_h, arbc::Vec2 point,
                      double edge_tol, double corner_tol);

// Re-crop the frame by dragging `handle` to composition-space `pointer`: a uniform
// (aspect-locked, square-pixel-preserving) scale of the covered region about the
// opposite corner / edge midpoint (D9: resize = re-crop, HOLDS resolution). The pixel
// count is untouched — only the covered region changes (D8 anti-resample). A degenerate
// frame / native or a non-resize handle returns the frame unchanged.
arbc::Affine recrop_frame(const arbc::Affine& frame, int native_w, int native_h, FrameHandle handle,
                          arbc::Vec2 pointer);

// Pan the frame: translate the covered composition region by `(dx, dy)` composition
// units — no scale / rotation change (D7 move = drag border/label).
arbc::Affine move_frame(const arbc::Affine& frame, double dx, double dy);

// Dutch rotation (D9, modifier-gated): compose a pure rotation of `angle_rad` about the
// covered region's center onto the frame — lengths preserved, so the resolution and the
// covered-region size are unchanged. `snap_15` rounds to the nearest 15° (Shift). A
// degenerate frame / non-positive native returns it unchanged.
arbc::Affine dutch_frame(const arbc::Affine& frame, int native_w, int native_h, double angle_rad,
                         bool snap_15);

// The follow-frame for an aspect change (D-manip-7): when the resolution's pixel width
// changes from `old_native_w` to `new_native_w` under an aspect edit, re-fit the covered
// region to the new aspect HOLDING its top-left position and its horizontal extent while
// adjusting the vertical extent — the deterministic "frame follows" rule, committed as
// one coalesced step with the resolution edit. A non-positive width returns the frame
// unchanged.
arbc::Affine refit_frame_to_aspect(const arbc::Affine& frame, int old_native_w, int new_native_w);

} // namespace ace::interact
