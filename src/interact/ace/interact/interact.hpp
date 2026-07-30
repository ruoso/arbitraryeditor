#pragma once

#include <arbc/base/geometry.hpp>  // arbc::Rect, arbc::Vec2
#include <arbc/base/transform.hpp> // arbc::Affine

#include <optional>
#include <span>
#include <vector>

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
// get lost in unbounded space" recovery. The ORIGIN-ANCHORED specialization of
// `fit_region` (`fit(w,h,pw,ph) == fit_region({0,0,w,h}, pw,ph)`, D-nav_aids-2), to
// which it delegates. A degenerate content/pane yields identity (nothing to fit).
arbc::Affine fit(double content_w, double content_h, double pane_w, double pane_h);

// The deep-zoom navigation-aid camera (editor.canvas.nav_aids; D24): the POSITIONED
// generalization of `fit` — a uniform-scale, centered `Affine` framing an arbitrary
// composition-space `region` into a `pane_w`×`pane_h` pane, preserving the pane's
// aspect and simply centering the region (no re-aspecting, no pixel rounding — it is a
// VIEW change, not a shot mint, D-nav_aids-2/Constraint 7). All three aids
// (fit-to-frame / fit-to-cell / zoom-to-selection) frame `interact::selected_extent`
// through this one primitive. A degenerate region (empty/inverted/non-finite) or a
// non-positive pane yields identity — no NaN escapes (D-fit_bounds-3).
arbc::Affine fit_region(const arbc::Rect& region, double pane_w, double pane_h);

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

// --- Resolution health (editor.cells.resolution; D8/§2) -----------------------
// The §2 "resolution health is computable, not a vibe" quantity, as a pure L1 helper
// over PRIMITIVE geometry beside the shot/frame math — no `scene`/`camera` type crosses
// the seam, so the `interact->scene` edge is untouched (D-resolution-3, the look_through/
// manip discipline). It answers D8 on the cell side: how densely a camera samples a
// cell's placed region against the cell's own native detail.

// How a camera samples a cell against its native grid. `NotApplicable` is the honest
// answer for a cell with no native floor (resolution-independent) or a degenerate input —
// never a false `Soft` (Constraint 4). `Crisp` when the camera samples at or below native
// (`ratio <= 1`), `Soft` when it magnifies above native (`ratio > 1`).
enum class ResolutionVerdict { NotApplicable, Crisp, Soft };

struct ResolutionHealth {
  // The sampling ratio: the camera's output pixels per composition unit over the cell's
  // placed region, divided by the cell's native pixels per composition unit (Constraint 4).
  // 0 when `verdict == NotApplicable`. The WORST (softest) axis is surfaced, so a
  // non-uniformly scaled cell reports the direction that magnifies most above native.
  double ratio = 0.0;
  ResolutionVerdict verdict = ResolutionVerdict::NotApplicable;
};

// The resolution-health verdict for a cell whose native pixel grid is `native_w`x`native_h`,
// placed by `placement` (content-px -> composition), sampled by a camera of output
// `cam_w`x`cam_h` framed by `cam_frame` (device-px -> composition). Both affines carry the
// same "1 native/device pixel spans this many composition units" scale the shot/frame
// helpers use, so the ratio is `(cam_w / shot_region_width) / (native_w / placed_region_width)`
// per axis (and its height twin), worst axis surfaced — the design's "hero camera samples
// the photo at 1.4x - slightly soft" (`docs/00-design.md:83-86`). PURE and byte-deterministic:
// no `Document`, no I/O, no frame state. A non-positive size, or a degenerate (non-invertible
// / zero-area) placement or frame, yields `NotApplicable` — never a div-by-zero or a false
// `Soft`. The caller supplies `native_w/native_h` from `scene::CellDetail::native_pixels`
// (so a `ResolutionIndependent` cell, whose native px is absent, never reaches here) and the
// camera's `Resolution` + frame from `scene::cameras`.
ResolutionHealth resolution_health(int native_w, int native_h, const arbc::Affine& placement,
                                   int cam_w, int cam_h, const arbc::Affine& cam_frame);

// --- Transform readout (editor.panels.inspector; D7/§6) -----------------------
// The inspector's live decomposition of a cell's placing-layer `Affine` into the D7/§6 display
// quantities. PURE — takes only the placement `Affine` + the cell's content extent
// (`arbc::Rect`), names no `scene`/ImGui type (Constraint 8), so it sits beside the
// resolution-health helper and is unit-tested headless. A READOUT, not a numeric editor:
// placement editing is the gizmo's drag (D-inspector-1), this only displays.

// A decomposed placement readout in composition units + degrees. Every field is a defined,
// NaN-free zero when `valid` is false (a degenerate zero-`det` or non-finite placement) — the
// panel then shows an "n/a" transform rather than garbage (the refuse-rather-than-guess rule).
struct PlacementReadout {
  arbc::Vec2 position;       // the placement translation (tx, ty), composition units
  arbc::Rect placed;         // placement.map_rect(content_bounds); zero when unbounded
  double rotation_deg = 0.0; // rotation of the mapped x-axis, degrees in (-180, 180]
  double shear_deg = 0.0;    // shear of the mapped y-axis off orthogonal, degrees; 0 = square
  double scale_x = 0.0;      // length of the mapped x-axis basis (composition units / content px)
  double scale_y = 0.0;      // signed height of the mapped y-axis basis
  bool valid = false;        // false for a degenerate (zero-det) / non-finite placement
};

// Decompose `placement` (content-px -> composition) into its position / placed-size / rotation /
// shear readout. `content_bounds` (the cell's `Content::bounds()`) is mapped through `placement`
// (an AABB `map_rect`, matching the shipped "placed size" readout) to fill `placed`; a `nullopt`
// bounds leaves `placed` zero (an unbounded cell has no placed size). A degenerate (zero-`det`)
// or non-finite `placement` yields `valid == false` and all-zero fields — never a NaN. PURE.
PlacementReadout decompose_placement(const arbc::Affine& placement,
                                     const std::optional<arbc::Rect>& content_bounds);

// --- Cell transform gizmo (editor.cells.gizmo; D7/D8/§6) ----------------------
// The cell's placement is its placing layer's `Affine` (content-px -> composition): it maps
// the cell's CONTENT extent `arbc::Content::bounds()` into composition space. These verbs are
// the richer sibling of the camera-frame verbs above — move / scale (8 handles) / rotate /
// shear about a draggable pivot — reframed as PURE placement math (D8, the load-bearing rule):
// they take/return the placement `Affine` (+ the content extent + a composition-space pivot +
// modifier bools) and NEVER a resolution value or a pixel. A handle-drag is PLACEMENT, never a
// resample. `interact` names no `scene`/`commands`/ImGui type, so these sit beside
// `recrop_frame`/`move_frame`/`dutch_frame` and are unit-tested headless (the bulk of coverage).

// Which part of a selected cell's gizmo a composition-space point grabs. Corners/edges are the
// eight scale handles; Rotate is the ring just OUTSIDE a corner; Pivot is the draggable pivot
// dot; Body is the interior move grab; None is a miss. The `hit_cell` test lives in `pick.hpp`
// (it anchors handles to `placed_quad`); this enum is the shared vocabulary the transform verbs
// below select on.
enum class CellHandle {
  None,
  Body, // the interior move grab
  Pivot,
  ScaleTopLeft,
  ScaleTopRight,
  ScaleBottomLeft,
  ScaleBottomRight,
  ScaleLeft,
  ScaleRight,
  ScaleTop,
  ScaleBottom,
  Rotate,
};

// True for the four corner scale handles.
bool is_cell_corner(CellHandle handle);
// True for the four edge scale handles.
bool is_cell_edge(CellHandle handle);
// True for any of the eight scale handles (corner or edge).
bool is_cell_scale_handle(CellHandle handle);

// The default pivot: the center of the placed extent (composition space). The gizmo seeds the
// draggable pivot here (§6:233); scale/rotate/shear compose about whatever pivot the caller
// passes (Alt = from pivot/center).
arbc::Vec2 cell_pivot(const arbc::Affine& placement, const arbc::Rect& extent);

// The signed angle (radians) swept about `pivot` going from composition-space `from` to `to` —
// the rotate gesture's angle input (`atan2` difference), the cell analog of the camera dutch's
// inline `a1 - a0` (`canvas_view.cpp` D-manip-5).
double drag_angle(arbc::Vec2 pivot, arbc::Vec2 from, arbc::Vec2 to);

// One arrow-nudge step, in composition units (Constraint 3: one entry per discrete nudge).
inline constexpr double k_cell_nudge = 1.0;

// Move: translate the placement by `(dx, dy)` composition units — the linear part is untouched,
// so the cell slides with no scale/rotation change (§6 move = drag body / arrow-nudge). The
// content resolution and `content_bounds` cannot change (D8).
arbc::Affine move_cell(const arbc::Affine& placement, double dx, double dy);

// Scale by dragging `handle` (a corner or edge) to composition-space `pointer`, about the
// opposite corner/edge midpoint by default or about `pivot` when `about_pivot` (Alt). A CORNER
// scales PROPORTIONALLY by default (uniform, aspect preserved) and FREELY (independent x/y) when
// `free_distort` (Shift — the one place Shift relaxes, §6/D8); an EDGE scales 1D along its own
// axis. The scale is in the placed box's OWN (possibly rotated/sheared) axes, so a rotated cell
// scales along its edges, not the global axes. Only the linear part changes — never the pixel
// count (D8). A non-scale handle, a degenerate extent/placement, a non-finite pointer, or a drag
// that would collapse/flip the box returns the placement UNCHANGED (Constraint 4).
arbc::Affine scale_cell(const arbc::Affine& placement, const arbc::Rect& extent, CellHandle handle,
                        arbc::Vec2 pointer, arbc::Vec2 pivot, bool free_distort, bool about_pivot);

// Rotate: compose a pure rotation of `angle_rad` about composition-space `pivot` onto the
// placement (§6 rotate; grabbed outside a corner). `snap_15` rounds to the nearest 15° (Shift),
// reusing the `dutch_frame` idiom. Lengths preserved, so no resample (D8). A degenerate
// placement or non-finite input returns it unchanged.
arbc::Affine rotate_cell(const arbc::Affine& placement, arbc::Vec2 pivot, double angle_rad,
                         bool snap_15);

// Shear: the advanced modifier+edge drag (§6). Dragging edge `edge` to `pointer` produces an
// off-diagonal linear part about `pivot` — a Top/Bottom edge shears horizontally (along the
// box's width axis as a function of its height coordinate), a Left/Right edge shears vertically.
// Only the linear part changes — never the pixel count (D8). A non-edge handle, a degenerate
// extent/placement, or a non-finite input returns the placement unchanged.
arbc::Affine shear_cell(const arbc::Affine& placement, const arbc::Rect& extent, CellHandle edge,
                        arbc::Vec2 pointer, arbc::Vec2 pivot);

// --- Group transform (editor.cells.group_transform; D-group_transform-3) --------------------

// Compose ONE affine delta across a multi-object selection: given the union box treated as one
// virtual cell — its placement BEFORE the gesture (`old_union`) and AFTER (`new_union`, produced
// by the shipped `move`/`scale`/`rotate`/`shear_cell` verbs on that box about the shared pivot) —
// the group delta is `D = new_union ∘ old_union⁻¹`, and every member's new placement is
// `D ∘ start`. Returns the recomposed placements in the SAME ORDER as `start_placements`.
//
// This is the ONLY new transform surface the group gizmo needs (D-group_transform-3): it reuses
// the exact single-object math on the union box and only composes the resulting delta onto each
// member, so it stays a pure `arbc::Affine` verb (no `scene`/UI include, headless-testable, A17).
// Kind-agnostic — a member is just a start placement, so cells and cameras compose identically
// (D-group_transform-4).
//
// A non-invertible `old_union` or a non-finite `new_union`/delta is an IDENTITY gesture: every
// member is returned UNCHANGED (refuse-rather-than-guess, D23/D24). An empty span yields an empty
// batch. Each member depends only on the shared delta and its own start, so the result is
// order-independent.
std::vector<arbc::Affine> group_transform(const arbc::Affine& old_union,
                                          const arbc::Affine& new_union,
                                          std::span<const arbc::Affine> start_placements);

// --- Overview schematic: hatch identity + content-space hatch (editor.panels.overview; D6/§5) --
// Two pure helpers the render-free composition overview draws with, kept in L1 so the shared
// list↔overview identity (`overview_pattern`) and the content-space hatch geometry
// (`hatch_segments`) are unit-tested headless and the deferred list-side swatch
// (`editor.panels.hatch_swatch`) reuses the EXACT identity with no forking (D-overview-4).
// Neither names a `scene`/ImGui type — `arbc` geometry in, plain values out.

// A hatch line style in the cell's CONTENT space (§5:191, §5:201): `angle_rad` is the primary
// hatch direction and `cross` adds a second set perpendicular to it. Because the direction is
// content-space, the hatch TILTS with the cell once its segments are mapped through `placement`
// (the "orientation reads for free" claim), and its density SCALES with the cell for the same
// reason.
struct HatchStyle {
  double angle_rad = 0.0; // primary hatch direction, radians, in content space
  bool cross = false;     // draw a second set of lines perpendicular to `angle_rad`
};

// The per-layer overview identity (§5:191): a distinct hatch `style` plus, once the fixed set of
// distinct styles is exhausted, a `color_index` that carries the distinction instead — the
// §5:204 "how many auto-distinct patterns before color must carry the load" open item, decided
// here with a defensible default (`k_overview_hatch_style_count` styles first, then color). The
// swatch a future Layers row draws is `overview_pattern(row_ordinal, layer_count)`.
struct OverviewPattern {
  HatchStyle style;
  // -1 while distinct hatch styles still separate every layer; >=0 (a palette index in
  // [0, k_overview_palette_count)) once the styles have wrapped and color must distinguish.
  int color_index = -1;
};

// The number of distinct hatch styles assigned before the color fallback engages (§5:204).
inline constexpr int k_overview_hatch_style_count = 6;
// The number of fallback colors cycled once the hatch styles wrap.
inline constexpr int k_overview_palette_count = 6;

// The deterministic overview identity for the layer at bottom→top `ordinal` in a composition of
// `count` layers (§5:191, Constraint 4). PURE: the same `ordinal` always yields the same pattern,
// the first `k_overview_hatch_style_count` ordinals are distinguished by hatch style alone
// (`color_index == -1`), and only past that does `color_index` become >=0. `ordinal` is clamped
// into `[0, count)` so an out-of-range index is defined (a degenerate `count <= 0` yields the
// style-0 default). Style and color depend only on `ordinal`, so the list-side swatch and the
// overview box agree by construction.
OverviewPattern overview_pattern(int ordinal, int count);

// One hatch line segment in CONTENT space (before the cell's `placement`).
struct HatchSegment {
  arbc::Vec2 a;
  arbc::Vec2 b;
};

// The largest number of hatch lines PER DIRECTION `hatch_segments` will emit before it declines
// (returns empty) — the over-dense guard, the `composition_grid_lines` `max_lines` discipline.
inline constexpr int k_max_hatch_lines = 4096;

// The CONTENT-space hatch fill of `content_bounds`: a set of parallel line segments at
// `style.angle_rad`, spaced `spacing` content units apart (perpendicular distance), ORIGIN-
// anchored (lines sit at integer multiples of `spacing` from the content origin, so two cells
// sharing content align), each clipped to `content_bounds`; `style.cross` appends a second set
// perpendicular to the first (§5:201). Generated in content space and returned BEFORE
// `placement`, so the count and spacing are INVARIANT to the cell's placement rotation and SCALE
// with the content extent (the load-bearing "scales/tilts with the cell" claim, Constraint 4) —
// the L4 draw maps each endpoint through `placement` and the overview transform. Total and
// self-limiting: a `spacing <= 0`/non-finite, a degenerate/non-finite `content_bounds`, or a
// per-direction line count exceeding `k_max_hatch_lines` (an over-fine hatch) all yield an EMPTY
// result — a defined no-op that draws nothing (the D-fit_bounds-3 fallback discipline).
std::vector<HatchSegment> hatch_segments(const arbc::Rect& content_bounds, double spacing,
                                         const HatchStyle& style);

} // namespace ace::interact
