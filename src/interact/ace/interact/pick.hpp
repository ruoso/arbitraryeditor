#pragma once

#include <ace/interact/interact.hpp> // FrameHandle / hit_frame (the camera half of D7)

#include <arbc/base/geometry.hpp>  // arbc::Rect, arbc::Vec2
#include <arbc/base/ids.hpp>       // arbc::ObjectId
#include <arbc/base/transform.hpp> // arbc::Affine

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace arbc {
class Document;
class Registry;
} // namespace arbc

namespace ace::interact {

// --- Picking: pointer -> the project-level selection (editor.cells.selection; D7/D19/A17) --
//
// A17 splits this header in two:
//
//   (a) a PRIMITIVE-ONLY POLICY CORE — `PickTarget` plus `pick` / `pick_stack` /
//       `pick_behind` / `marquee` / `click_selection` / `marquee_selection` over
//       `std::span<const PickTarget>`. It names NO `scene` type, so the Overview can feed it
//       schematic boxes and a headless Catch2 test can feed it hand-built targets. Every rule
//       that could be WRONG (z-order, camera click-through, unbounded content, select-behind
//       cycling, the modifier policy) lives here, under unit test.
//
//   (b) exactly ONE ASSEMBLY ADAPTER — `pick_targets(document, registry)`, the sole
//       `interact -> scene` include in the component (src/interact/pick_targets.cpp), where the
//       cells list and the cameras list are merged into one z-ordered stack.
//
// Nothing here mutates `commands::Selection` (there is no `interact -> commands` edge and
// should not be): the policy functions return a `SelectionChange` VALUE and L4 applies it onto
// the shipped `Selection` verbs (D-selection-2).

// Which side of D7's one shape a target is. A CELL is grabbed by its BODY; a CAMERA only by
// its BORDER or LABEL, its interior staying click-through to the art it frames.
enum class PickKind {
  Cell,
  Camera,
};

// One pickable placed object: the same `{content id, placing layer, affine, extent}` shape a
// `scene::Cell` and a `scene::Camera` both have (A14/D7), reduced to primitives.
struct PickTarget {
  arbc::ObjectId id;    // the Content object — the selection's key
  arbc::ObjectId layer; // the placing layer (what a transform edit targets)
  PickKind kind = PickKind::Cell;
  // Composition-space placement: a cell's layer transform, or a camera's frame.
  arbc::Affine placement = arbc::Affine::identity();
  // The target's own extent in CONTENT space, i.e. BEFORE `placement`. For a cell that is
  // `arbc::Content::bounds()`; for a camera it is its output rectangle `{0, 0, res_w, res_h}`
  // (a camera contributes zero pixels, so `bounds()` is NOT its pickable extent — A14).
  // `nullopt` means UNBOUNDED (a factory-built `org.arbc.solid`, D-cells_model-3): clicking
  // anywhere over it is a real hit, but a marquee never selects it (D-selection-5).
  std::optional<arbc::Rect> extent;
};

// One entry of the pick stack. `handle` is `FrameHandle::None` for a cell (a body is not an
// outline); for a camera it is `hit_frame`'s verdict — the corner/edge/label the point grabbed,
// which is what lets the canvas start the shipped frame-gizmo drag from the same press.
struct PickHit {
  bool hit = false;
  std::size_t index = 0; // index into the `targets` span the hit came from
  arbc::ObjectId id;
  PickKind kind = PickKind::Cell;
  FrameHandle handle = FrameHandle::None;
};

// The four composition-space corners of a target's placed extent, in `{tl, tr, br, bl}` order
// of the extent's own corners — a PARALLELOGRAM for a rotated/sheared placement, not an AABB.
// `nullopt` for unbounded content, an empty/degenerate extent, a non-invertible placement, or a
// non-finite result (the D-fit_bounds-3 fallback discipline). The canvas draws the selection
// outline from this; `marquee` overlaps against it.
std::optional<std::array<arbc::Vec2, 4>> placed_quad(const PickTarget& target);

// The full stack under `point`, ordered FRONT-TO-BACK — i.e. the reverse of `targets`, which
// arrives in layer (z) order, bottom-to-top (`arbc::CompositionRecord`'s ordered layer list).
// A cell is hit when `placement.inverse().apply(point)` lands inside `extent` — exact for any
// rotation/shear (D-selection-3), and always for unbounded content. A camera is hit only when
// `hit_frame(...) != FrameHandle::None`, so its interior is CLICK-THROUGH (D7). Tolerances are
// COMPOSITION units (the caller converts screen px through the view scale, as
// `canvas_view.cpp` already does for the frame gizmo); a cell body takes no tolerance — it is a
// filled region, not an outline. A degenerate target (non-invertible placement, zero-area
// extent, non-positive camera resolution) or a non-finite `point` contributes no hit.
std::vector<PickHit> pick_stack(std::span<const PickTarget> targets, arbc::Vec2 point,
                                double edge_tol, double corner_tol);

// The TOPMOST hit under `point` (the front of `pick_stack`), or a `hit == false` miss.
PickHit pick(std::span<const PickTarget> targets, arbc::Vec2 point, double edge_tol,
             double corner_tol);

// Select-behind (D7 "Cmd/Ctrl-click cycles down"), derived from the CURRENT selection with no
// hidden cycle state (D-selection-4): the stack entry immediately BEHIND the first stack member
// whose id is `selected`, wrapping past the back to the front. With nothing selected — or with
// a `selected` that is not in this stack — it is exactly `pick`. Pure, so the wrap is trivially
// testable and another surface changing the selection between clicks can never desync it.
PickHit pick_behind(std::span<const PickTarget> targets, arbc::Vec2 point, double edge_tol,
                    double corner_tol, arbc::ObjectId selected);

// Every target whose placed quad OVERLAPS the axis-aligned composition-space `rect`, in z order
// (D-selection-5). Overlap is exact — a separating-axis test over the marquee's two axes and
// the quad's two edge normals — so a rotated cell whose AABB overlaps but whose quad does not
// is NOT selected. TOUCH selects; full enclosure is not required. UNBOUNDED content is never
// marquee-selected (it would be caught by every marquee, which carries no information — the one
// place click and marquee deliberately disagree). An empty/degenerate/non-finite `rect` selects
// nothing.
std::vector<arbc::ObjectId> marquee(std::span<const PickTarget> targets, const arbc::Rect& rect);

// Every target's id in z order — what Select-All (Cmd/Ctrl-A) selects. "All" means all: cells
// AND cameras AND unbounded content, because D7/D20 make them one shape under one select tool.
std::vector<arbc::ObjectId> all_ids(std::span<const PickTarget> targets);

// The axis-aligned composition-space union of the placed extents of the targets whose `id` is
// in `ids` — "the region the selection occupies" (editor.cameras.frame_selection, D23). Each
// contribution is `target.placement.map_rect(*target.extent)` (`transform.hpp:38`), and that
// loses NO tightness: the AABB of a union of point sets IS the coordinate-wise min/max of the
// per-set AABBs, so unioning bounded quads is byte-identical to bounding the unioned
// `placed_quad` corners (D-frame_selection-4). Order-independent, and `ids` may name targets
// that are not present (they contribute nothing).
//
// KIND-AGNOSTIC (D-frame_selection-3): a selected CAMERA contributes its own output rectangle
// exactly like a cell contributes its bounds, because `pick_targets` already fills both with
// the same `{placement, extent}` pair — so framing a camera reproduces its crop.
//
// UNBOUNDED members (`extent == nullopt`) are SKIPPED, the same asymmetry `marquee` ships
// (D-selection-5): an unbounded fill has no region, so including it would frame the infinite
// plane. So are degenerate contributions — an empty/non-finite extent, a non-invertible
// placement, a non-finite mapped rect (the D-fit_bounds-3 fallback discipline). `nullopt` when
// nothing bounded remains, which the caller reads as "there is nothing to frame" and refuses.
std::optional<arbc::Rect> selected_extent(std::span<const PickTarget> targets,
                                          std::span<const arbc::ObjectId> ids);

// --- The modifier policy, returned as a VALUE (D-selection-2) -----------------------------

// The intended mutation of the project-level selection. `interact` cannot see `commands`
// (`check_levels.py`) and should not; L4 applies this with a four-arm switch onto the shipped
// `Selection::replace_all` / `add_all` / `toggle` / `clear` verbs. Returning it as a value is
// what puts the whole modifier policy — the highest-branch-count, lowest-visibility part of the
// feature — under headless Catch2, and gives the Layers list and the Overview the same policy
// for free when they land.
enum class SelectOp {
  None,    // leave the selection exactly as it is (a Shift-miss must NOT wipe it)
  Replace, // selection := ids
  Add,     // selection += ids (duplicate-safe)
  Toggle,  // flip each id's membership
  Clear,   // selection := {}
};

struct SelectionChange {
  SelectOp op = SelectOp::None;
  std::vector<arbc::ObjectId> ids;
};

// The pointer modifiers the selection policy reads (`docs/00-design.md:257-263`). `shift` adds
// to / toggles the selection; `behind` is the Cmd/Ctrl select-behind gate — the CALLER ORs
// Ctrl and Super, because D7 says "Cmd/Ctrl" and a Ctrl-only read makes select-behind
// unreachable on macOS (D-selection-10).
struct PickModifiers {
  bool shift = false;
  bool behind = false;
};

// The selection change one CLICK at `point` intends, given the current primary id:
//   plain hit    -> Replace{id}        plain miss    -> Clear
//   Shift  hit   -> Toggle{id}         Shift  miss   -> None
//   Cmd/Ctrl hit -> Replace{pick_behind(...)}
// A miss NEVER returns a hit-shaped change, so the caller can read `op == Clear || op == None`
// as "the press landed on empty canvas" and arm a marquee from it.
SelectionChange click_selection(std::span<const PickTarget> targets, arbc::Vec2 point,
                                double edge_tol, double corner_tol, PickModifiers mods,
                                arbc::ObjectId selected);

// The selection change one MARQUEE over `rect` intends:
//   plain non-empty -> Replace{ids}    plain empty -> Clear
//   Shift non-empty -> Add{ids}        Shift empty -> None
SelectionChange marquee_selection(std::span<const PickTarget> targets, const arbc::Rect& rect,
                                  bool shift);

// --- The one assembly adapter (A17 (b); src/interact/pick_targets.cpp) ---------------------

// The document's z-ordered stack of placed objects: every cell from `scene::cells` in layer
// order (bottom-to-top) carrying its live `content_bounds`, then every camera from
// `scene::cameras` appended ABOVE them, each with its frame as `placement` and
// `{0, 0, res_w, res_h}` as `extent`.
//
// Cameras sort above all cells (D-selection-6) because the true interleaved order is not
// recoverable from two independently-filtered walks of the same layer list — and it does not
// matter: a camera renders ZERO pixels (A14), so its outline is always-on-top chrome, which is
// what the shipped canvas already draws. Combined with D7's border-only grab, a camera still
// can never steal a click from the art it frames.
//
// A plain UI-thread read over the lock-free `pin()` seam (Constraint 12): no transaction, no
// lease, no lock — the same per-frame read the canvas already performs, extended to cells. A
// cell whose kind token does not resolve is still a target (mirroring `cells()`'s
// empty-`kind_id` passthrough), because an unknown-passthrough cell is still selectable.
std::vector<PickTarget> pick_targets(const arbc::Document& document,
                                     const arbc::Registry& registry);

} // namespace ace::interact
