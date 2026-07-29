#include <ace/interact/pick.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

// The PICK POLICY CORE (A17 (a)): primitive-only, `scene`-free, ImGui/GL/SDL-free. The single
// `interact -> scene` include lives in pick_targets.cpp, deliberately not here.

namespace ace::interact {
namespace {

bool finite(arbc::Vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }

bool finite(const arbc::Rect& r) {
  return std::isfinite(r.x0) && std::isfinite(r.y0) && std::isfinite(r.x1) && std::isfinite(r.y1);
}

// A camera target's native output rectangle, recovered from its extent. `hit_frame` takes the
// resolution as primitive ints (the A16 convention), and `pick_targets` fills the extent from
// exactly those ints, so the round-trip is exact for any real camera.
bool camera_native(const PickTarget& target, int& native_w, int& native_h) {
  if (!target.extent || !finite(*target.extent)) {
    return false;
  }
  const double w = std::lround(target.extent->width());
  const double h = std::lround(target.extent->height());
  if (!(w > 0.0) || !(h > 0.0)) {
    return false; // a non-positive resolution is a miss, never a NaN (D-fit_bounds-3)
  }
  native_w = static_cast<int>(w);
  native_h = static_cast<int>(h);
  return true;
}

// Hit ONE target. `nullopt` is a miss; a value is the grabbed handle (`None` for a cell body,
// which is a filled region rather than an outline).
std::optional<FrameHandle> hit_target(const PickTarget& target, arbc::Vec2 point, double edge_tol,
                                      double corner_tol) {
  if (!finite(point)) {
    return std::nullopt;
  }
  if (target.kind == PickKind::Camera) {
    // The camera half of D7, delegated verbatim to the shipped function: border/label grab with
    // an interior that returns None — which IS the click-through.
    int native_w = 0;
    int native_h = 0;
    if (!camera_native(target, native_w, native_h)) {
      return std::nullopt;
    }
    const FrameHandle handle =
        hit_frame(target.placement, native_w, native_h, point, edge_tol, corner_tol);
    return handle == FrameHandle::None ? std::nullopt : std::optional<FrameHandle>(handle);
  }
  // A cell is grabbed by its BODY, tested in CONTENT space through the inverse placement
  // (D-selection-3): exact for an arbitrary affine, where an AABB test would let a click in the
  // empty corner of a rotated cell's bounding box select it.
  const std::optional<arbc::Affine> inv = target.placement.inverse();
  if (!inv) {
    return std::nullopt; // degenerate placement: nothing to grab
  }
  if (!target.extent) {
    return FrameHandle::None; // unbounded content genuinely covers the plane (Constraint 6)
  }
  if (target.extent->empty() || !finite(*target.extent)) {
    return std::nullopt;
  }
  const arbc::Vec2 local = inv->apply(point);
  if (!finite(local)) {
    return std::nullopt;
  }
  const arbc::Rect& e = *target.extent;
  if (local.x < e.x0 || local.x > e.x1 || local.y < e.y0 || local.y > e.y1) {
    return std::nullopt;
  }
  return FrameHandle::None;
}

// Project the four points onto `axis` and return the [min, max] interval.
std::pair<double, double> project(const arbc::Vec2* points, arbc::Vec2 axis) {
  double lo = points[0].x * axis.x + points[0].y * axis.y;
  double hi = lo;
  for (int i = 1; i < 4; ++i) {
    const double v = points[i].x * axis.x + points[i].y * axis.y;
    lo = std::min(lo, v);
    hi = std::max(hi, v);
  }
  return {lo, hi};
}

// Exact quad-vs-axis-aligned-rect overlap (Constraint 7 / D-selection-5): a separating-axis test
// over four axes — the marquee's two, and the quad's two edge normals. TOUCH counts as overlap
// (the separation test is strict), so a drag that grazes a cell selects it.
bool quad_overlaps_rect(const std::array<arbc::Vec2, 4>& quad, const arbc::Rect& rect) {
  const arbc::Vec2 rect_pts[4] = {
      {rect.x0, rect.y0}, {rect.x1, rect.y0}, {rect.x1, rect.y1}, {rect.x0, rect.y1}};
  const arbc::Vec2 e0{quad[1].x - quad[0].x, quad[1].y - quad[0].y};
  const arbc::Vec2 e1{quad[2].x - quad[1].x, quad[2].y - quad[1].y};
  const arbc::Vec2 axes[4] = {{1.0, 0.0}, {0.0, 1.0}, {-e0.y, e0.x}, {-e1.y, e1.x}};
  for (const arbc::Vec2 axis : axes) {
    if (!(std::abs(axis.x) > 0.0 || std::abs(axis.y) > 0.0)) {
      continue; // a collapsed quad edge yields no separating direction
    }
    const auto [qlo, qhi] = project(quad.data(), axis);
    const auto [rlo, rhi] = project(rect_pts, axis);
    if (qhi < rlo || rhi < qlo) {
      return false; // separated on this axis => no overlap
    }
  }
  return true;
}

SelectionChange miss_change(bool shift) {
  // A Shift-miss must NOT wipe the selection — the one asymmetry that makes additive
  // selection usable (`docs/00-design.md:224`).
  return shift ? SelectionChange{} : SelectionChange{SelectOp::Clear, {}};
}

} // namespace

std::optional<std::array<arbc::Vec2, 4>> placed_quad(const PickTarget& target) {
  if (!target.extent || target.extent->empty() || !finite(*target.extent)) {
    return std::nullopt; // unbounded / degenerate content has no outline to draw or overlap
  }
  if (!target.placement.inverse()) {
    return std::nullopt; // a collapsed placement is not a shape
  }
  const arbc::Rect& e = *target.extent;
  const std::array<arbc::Vec2, 4> quad = {
      target.placement.apply({e.x0, e.y0}), target.placement.apply({e.x1, e.y0}),
      target.placement.apply({e.x1, e.y1}), target.placement.apply({e.x0, e.y1})};
  for (const arbc::Vec2 corner : quad) {
    if (!finite(corner)) {
      return std::nullopt;
    }
  }
  return quad;
}

std::vector<PickHit> pick_stack(std::span<const PickTarget> targets, arbc::Vec2 point,
                                double edge_tol, double corner_tol) {
  std::vector<PickHit> stack;
  // `targets` arrives bottom-to-top (the composition's ordered layer list); the stack is
  // FRONT-to-back, so walk it in reverse (Constraint 5).
  for (std::size_t i = targets.size(); i-- > 0;) {
    const std::optional<FrameHandle> handle = hit_target(targets[i], point, edge_tol, corner_tol);
    if (!handle) {
      continue;
    }
    stack.push_back(PickHit{true, i, targets[i].id, targets[i].kind, *handle});
  }
  return stack;
}

PickHit pick(std::span<const PickTarget> targets, arbc::Vec2 point, double edge_tol,
             double corner_tol) {
  const std::vector<PickHit> stack = pick_stack(targets, point, edge_tol, corner_tol);
  return stack.empty() ? PickHit{} : stack.front();
}

PickHit pick_behind(std::span<const PickTarget> targets, arbc::Vec2 point, double edge_tol,
                    double corner_tol, arbc::ObjectId selected) {
  const std::vector<PickHit> stack = pick_stack(targets, point, edge_tol, corner_tol);
  if (stack.empty()) {
    return PickHit{};
  }
  if (selected.valid()) {
    for (std::size_t i = 0; i < stack.size(); ++i) {
      if (stack[i].id == selected) {
        return stack[(i + 1) % stack.size()]; // one step behind, wrapping to the front
      }
    }
  }
  return stack.front(); // nothing selected, or nothing of this stack: plain topmost
}

std::vector<arbc::ObjectId> marquee(std::span<const PickTarget> targets, const arbc::Rect& rect) {
  std::vector<arbc::ObjectId> ids;
  if (rect.empty() || !finite(rect)) {
    return ids; // a degenerate drag (including a plain click) selects nothing
  }
  for (const PickTarget& target : targets) {
    // Unbounded content is deliberately excluded: it would be caught by EVERY marquee, which
    // conveys nothing and would make "select the two cells I dragged over" select three
    // (D-selection-5). `placed_quad` returns nullopt for it, so the exclusion is structural.
    const std::optional<std::array<arbc::Vec2, 4>> quad = placed_quad(target);
    if (quad && quad_overlaps_rect(*quad, rect)) {
      ids.push_back(target.id);
    }
  }
  return ids;
}

std::vector<arbc::ObjectId> all_ids(std::span<const PickTarget> targets) {
  std::vector<arbc::ObjectId> ids;
  ids.reserve(targets.size());
  for (const PickTarget& target : targets) {
    ids.push_back(target.id);
  }
  return ids;
}

std::optional<arbc::Rect> selected_extent(std::span<const PickTarget> targets,
                                          std::span<const arbc::ObjectId> ids) {
  std::optional<arbc::Rect> united;
  for (const PickTarget& target : targets) {
    bool wanted = false;
    for (const arbc::ObjectId id : ids) {
      if (id == target.id) {
        wanted = true;
        break;
      }
    }
    if (!wanted) {
      continue;
    }
    // The same admission test `placed_quad` applies, so the two agree target-for-target:
    // unbounded content has no region, and a collapsed placement is not a shape.
    if (!target.extent || target.extent->empty() || !finite(*target.extent)) {
      continue;
    }
    if (!target.placement.inverse()) {
      continue;
    }
    const arbc::Rect placed = target.placement.map_rect(*target.extent);
    if (placed.empty() || !finite(placed)) {
      continue; // a degenerate/non-finite contribution is skipped, never propagated as a NaN
    }
    if (!united) {
      united = placed;
      continue;
    }
    united->x0 = std::min(united->x0, placed.x0);
    united->y0 = std::min(united->y0, placed.y0);
    united->x1 = std::max(united->x1, placed.x1);
    united->y1 = std::max(united->y1, placed.y1);
  }
  return united;
}

SelectionChange click_selection(std::span<const PickTarget> targets, arbc::Vec2 point,
                                double edge_tol, double corner_tol, PickModifiers mods,
                                arbc::ObjectId selected) {
  const PickHit hit = mods.behind ? pick_behind(targets, point, edge_tol, corner_tol, selected)
                                  : pick(targets, point, edge_tol, corner_tol);
  if (!hit.hit) {
    return miss_change(mods.shift);
  }
  if (mods.shift) {
    return SelectionChange{SelectOp::Toggle, {hit.id}};
  }
  return SelectionChange{SelectOp::Replace, {hit.id}};
}

SelectionChange marquee_selection(std::span<const PickTarget> targets, const arbc::Rect& rect,
                                  bool shift) {
  std::vector<arbc::ObjectId> ids = marquee(targets, rect);
  if (ids.empty()) {
    return miss_change(shift);
  }
  return SelectionChange{shift ? SelectOp::Add : SelectOp::Replace, std::move(ids)};
}

CellHandle hit_cell(const PickTarget& target, arbc::Vec2 pivot, arbc::Vec2 point, double edge_tol,
                    double corner_tol) {
  (void)edge_tol; // a cell body is a filled region (the `inside` test), not an outline band
  if (!finite(point) || !finite(pivot)) {
    return CellHandle::None;
  }
  const std::optional<std::array<arbc::Vec2, 4>> quad = placed_quad(target);
  if (!quad) {
    return CellHandle::None; // unbounded / degenerate content has no gizmo
  }
  const arbc::Vec2 tl = (*quad)[0];
  const arbc::Vec2 tr = (*quad)[1];
  const arbc::Vec2 br = (*quad)[2];
  const arbc::Vec2 bl = (*quad)[3];
  const auto d = [](arbc::Vec2 a, arbc::Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); };

  // 1. The pivot dot wins (drawn on top, §6:233).
  if (d(point, pivot) <= corner_tol) {
    return CellHandle::Pivot;
  }
  // 2. Corners win over everything else within `corner_tol`.
  const arbc::Vec2 corners[4] = {tl, tr, bl, br};
  const CellHandle corner_ids[4] = {CellHandle::ScaleTopLeft, CellHandle::ScaleTopRight,
                                    CellHandle::ScaleBottomLeft, CellHandle::ScaleBottomRight};
  CellHandle best = CellHandle::None;
  double best_d = corner_tol;
  for (int i = 0; i < 4; ++i) {
    const double dd = d(point, corners[i]);
    if (dd <= best_d) {
      best_d = dd;
      best = corner_ids[i];
    }
  }
  if (best != CellHandle::None) {
    return best;
  }
  // Whether `point` is inside the placed box — content-space test, exact for any affine (as the
  // body pick is, D-selection-3), so a rotated cell's rotate ring and body are the rotated ones.
  bool inside = false;
  const std::optional<arbc::Affine> inv = target.placement.inverse();
  if (inv && target.extent) {
    const arbc::Vec2 local = inv->apply(point);
    const arbc::Rect& e = *target.extent;
    inside =
        finite(local) && local.x >= e.x0 && local.x <= e.x1 && local.y >= e.y0 && local.y <= e.y1;
  }
  // 3. The rotate ring just OUTSIDE a corner (beyond the corner handle, off the box, §6 rotate).
  if (!inside) {
    const double rotate_tol = corner_tol * 3.0;
    double rd = d(point, corners[0]);
    for (int i = 1; i < 4; ++i) {
      rd = std::min(rd, d(point, corners[i]));
    }
    if (rd <= rotate_tol) {
      return CellHandle::Rotate;
    }
  }
  // 4. Edge-midpoint scale handles.
  const arbc::Vec2 mids[4] = {{(tl.x + tr.x) * 0.5, (tl.y + tr.y) * 0.5},
                              {(bl.x + br.x) * 0.5, (bl.y + br.y) * 0.5},
                              {(tl.x + bl.x) * 0.5, (tl.y + bl.y) * 0.5},
                              {(tr.x + br.x) * 0.5, (tr.y + br.y) * 0.5}};
  const CellHandle edge_ids[4] = {CellHandle::ScaleTop, CellHandle::ScaleBottom,
                                  CellHandle::ScaleLeft, CellHandle::ScaleRight};
  best_d = corner_tol;
  for (int i = 0; i < 4; ++i) {
    const double dd = d(point, mids[i]);
    if (dd <= best_d) {
      best_d = dd;
      best = edge_ids[i];
    }
  }
  if (best != CellHandle::None) {
    return best;
  }
  // 5. The body interior is the move grab.
  return inside ? CellHandle::Body : CellHandle::None;
}

GridLines composition_grid_lines(double spacing, const arbc::Rect& region, int max_lines) {
  GridLines out;
  // Total and self-limiting (D-grid-4 / Constraint 7): a bad spacing or a degenerate/non-finite
  // region yields no grid at all rather than a divide-by-zero or a NaN coordinate.
  if (!(spacing > 0.0) || !std::isfinite(spacing) || region.empty() || !finite(region) ||
      max_lines < 0) {
    return out;
  }
  // Origin-anchored: the lines within `region` are the integer multiples k*spacing for k running
  // from ceil(lo/spacing) to floor(hi/spacing) on each axis (negatives included when the region
  // spans the origin).
  const double kx0 = std::ceil(region.x0 / spacing);
  const double kx1 = std::floor(region.x1 / spacing);
  const double ky0 = std::ceil(region.y0 / spacing);
  const double ky1 = std::floor(region.y1 / spacing);
  // Vanish an over-dense grid (extreme zoom-out with a fine spacing) rather than smear the pane or
  // balloon the snap-candidate list: a per-axis count above `max_lines` returns an empty result.
  const double count_x = kx1 - kx0 + 1.0;
  const double count_y = ky1 - ky0 + 1.0;
  if (count_x > static_cast<double>(max_lines) || count_y > static_cast<double>(max_lines)) {
    return out;
  }
  for (double k = kx0; k <= kx1; k += 1.0) {
    out.xs.push_back(k * spacing);
  }
  for (double k = ky0; k <= ky1; k += 1.0) {
    out.ys.push_back(k * spacing);
  }
  return out;
}

namespace {

// One snap candidate line along an axis, carrying the ORTHOGONAL span of the object that owns it
// (so a vertical guide spans both boxes vertically).
struct SnapLine {
  double pos;
  double omin;
  double omax;
};

// The nearest `lines` entry to any of the moving object's three candidate positions `mvals`,
// within `tol`. Sets `delta` (the nudge), `owner` (the matched line), returns whether one snapped.
bool best_snap(const std::vector<SnapLine>& lines, const double (&mvals)[3], double tol,
               double& delta, SnapLine& owner) {
  double best_abs = tol;
  bool found = false;
  for (const SnapLine& line : lines) {
    for (const double mv : mvals) {
      const double diff = line.pos - mv;
      if (std::abs(diff) <= best_abs) {
        best_abs = std::abs(diff);
        delta = diff;
        owner = line;
        found = true;
      }
    }
  }
  return found;
}

} // namespace

SnapResult snap_placement(const arbc::Affine& candidate, const arbc::Rect& moving_extent,
                          std::span<const PickTarget> others, double tol, bool bypass,
                          std::span<const double> grid_x, std::span<const double> grid_y) {
  SnapResult out;
  out.placement = candidate;
  if (bypass || !(tol > 0.0) || !candidate.inverse() || moving_extent.empty() ||
      !finite(moving_extent)) {
    return out; // Cmd/Ctrl bypass or a degenerate candidate: unsnapped, no guides (§6:258)
  }
  const arbc::Rect m = candidate.map_rect(moving_extent);
  if (m.empty() || !finite(m)) {
    return out;
  }
  const double mxs[3] = {m.x0, (m.x0 + m.x1) * 0.5, m.x1};
  const double mys[3] = {m.y0, (m.y0 + m.y1) * 0.5, m.y1};

  std::vector<SnapLine> xlines;
  std::vector<SnapLine> ylines;
  for (const PickTarget& t : others) {
    if (!t.extent || t.extent->empty() || !finite(*t.extent) || !t.placement.inverse()) {
      continue;
    }
    const arbc::Rect r = t.placement.map_rect(*t.extent);
    if (r.empty() || !finite(r)) {
      continue;
    }
    const double xs[3] = {r.x0, (r.x0 + r.x1) * 0.5, r.x1};
    const double ys[3] = {r.y0, (r.y0 + r.y1) * 0.5, r.y1};
    for (const double x : xs) {
      xlines.push_back({x, r.y0, r.y1});
    }
    for (const double y : ys) {
      ylines.push_back({y, r.x0, r.x1});
    }
  }
  for (const double gx : grid_x) {
    xlines.push_back({gx, m.y0, m.y1});
  }
  for (const double gy : grid_y) {
    ylines.push_back({gy, m.x0, m.x1});
  }

  double dx = 0.0;
  double dy = 0.0;
  SnapLine xowner{};
  SnapLine yowner{};
  out.snapped_x = best_snap(xlines, mxs, tol, dx, xowner);
  out.snapped_y = best_snap(ylines, mys, tol, dy, yowner);

  arbc::Affine snapped = candidate;
  if (out.snapped_x) {
    snapped.tx += dx;
  }
  if (out.snapped_y) {
    snapped.ty += dy;
  }
  out.placement = snapped;
  if (out.snapped_x) {
    const double y0 = std::min(m.y0 + dy, xowner.omin);
    const double y1 = std::max(m.y1 + dy, xowner.omax);
    out.guides.push_back({{xowner.pos, y0}, {xowner.pos, y1}});
  }
  if (out.snapped_y) {
    const double x0 = std::min(m.x0 + dx, yowner.omin);
    const double x1 = std::max(m.x1 + dx, yowner.omax);
    out.guides.push_back({{x0, yowner.pos}, {x1, yowner.pos}});
  }
  return out;
}

} // namespace ace::interact
