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

} // namespace ace::interact
