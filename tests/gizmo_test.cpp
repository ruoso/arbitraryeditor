// editor.cells.gizmo — L1 headless Catch2 units for the cell transform-gizmo math (D7/D8/§6):
// handle hit-test under an arbitrary affine, move / scale (8 handles) / rotate / shear about a
// pivot, and the cross-object snap engine — all pure arbc::Affine beside interact's camera-frame
// verbs (Constraint 2, D-gizmo-1). Plus the D8 invariance (placement moves, pixels do not), the
// one-transaction-per-gesture proof over a live Document, and a render_offline golden pinning
// "a scaled cell is SOFTER, not cropped or re-gridded" (byte-exact). No ImGui/GL/SDL.
#include <ace/interact/interact.hpp>
#include <ace/interact/pick.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_raster/raster_content.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include "golden_support.hpp"

using ace::interact::cell_pivot;
using ace::interact::CellHandle;
using ace::interact::drag_angle;
using ace::interact::hit_cell;
using ace::interact::is_cell_scale_handle;
using ace::interact::k_cell_nudge;
using ace::interact::move_cell;
using ace::interact::PickKind;
using ace::interact::PickTarget;
using ace::interact::rotate_cell;
using ace::interact::scale_cell;
using ace::interact::shear_cell;
using ace::interact::snap_placement;
using ace::interact::SnapResult;
using Catch::Approx;

namespace {

constexpr double k_pi = 3.14159265358979323846;

PickTarget cell_target(const arbc::Affine& placement, const arbc::Rect& extent) {
  PickTarget t;
  t.kind = PickKind::Cell;
  t.placement = placement;
  t.extent = extent;
  return t;
}

arbc::Registry gizmo_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  return registry;
}

// A 64x64 document whose sole bounded cell is a 16x16 red raster placed at (8,8) over a green
// full-frame background (mirrors camera_manip_test's build_cells_doc) — integer bounds so an
// integer-scale placement renders piecewise-constant pixels. Returns the raster layer id.
std::unique_ptr<arbc::Document> build_gizmo_doc(arbc::ObjectId& raster_layer) {
  auto doc = std::make_unique<arbc::Document>();
  const arbc::ObjectId root = doc->add_composition(64.0, 64.0);
  const arbc::ObjectId bg =
      doc->add_content(std::make_shared<arbc::SolidContent>(arbc::Rgba{0.0F, 0.35F, 0.0F, 1.0F}));
  doc->attach_layer(root, doc->add_layer(bg, arbc::Affine::identity()));
  arbc::DecodedImage img;
  img.width = 16;
  img.height = 16;
  img.format = arbc::k_working_rgba32f;
  img.bytes.resize(static_cast<std::size_t>(16) * 16 * 4 * sizeof(float));
  auto* fp = reinterpret_cast<float*>(img.bytes.data());
  for (int i = 0; i < 16 * 16; ++i) { // opaque red, premultiplied linear
    fp[i * 4] = 0.6F;
    fp[i * 4 + 1] = 0.0F;
    fp[i * 4 + 2] = 0.0F;
    fp[i * 4 + 3] = 1.0F;
  }
  const arbc::ObjectId raster =
      doc->add_content(std::make_shared<arbc::RasterContent>(std::move(img)));
  raster_layer = doc->add_layer(raster, arbc::Affine::translation(8.0, 8.0));
  doc->attach_layer(root, raster_layer);
  return doc;
}

} // namespace

TEST_CASE("gizmo: hit-test anchors every handle to the placed quad, never an AABB") {
  // A rotated + sheared + translated placement (a genuine parallelogram, not an AABB).
  const arbc::Affine placement{1.5, 0.9, -0.6, 1.2, 40.0, 40.0};
  const arbc::Rect extent{0.0, 0.0, 20.0, 20.0};
  const PickTarget target = cell_target(placement, extent);
  const arbc::Vec2 pivot = cell_pivot(placement, extent);
  const double edge_tol = 1.5;
  const double corner_tol = 3.0;
  const auto P = [&](double x, double y) { return placement.apply({x, y}); };

  // Each corner / edge-midpoint / pivot handle is hit at its placed anchor.
  CHECK(hit_cell(target, pivot, P(0.0, 0.0), edge_tol, corner_tol) == CellHandle::ScaleTopLeft);
  CHECK(hit_cell(target, pivot, P(20.0, 0.0), edge_tol, corner_tol) == CellHandle::ScaleTopRight);
  CHECK(hit_cell(target, pivot, P(0.0, 20.0), edge_tol, corner_tol) == CellHandle::ScaleBottomLeft);
  CHECK(hit_cell(target, pivot, P(20.0, 20.0), edge_tol, corner_tol) ==
        CellHandle::ScaleBottomRight);
  CHECK(hit_cell(target, pivot, P(10.0, 0.0), edge_tol, corner_tol) == CellHandle::ScaleTop);
  CHECK(hit_cell(target, pivot, P(10.0, 20.0), edge_tol, corner_tol) == CellHandle::ScaleBottom);
  CHECK(hit_cell(target, pivot, P(0.0, 10.0), edge_tol, corner_tol) == CellHandle::ScaleLeft);
  CHECK(hit_cell(target, pivot, P(20.0, 10.0), edge_tol, corner_tol) == CellHandle::ScaleRight);
  CHECK(hit_cell(target, pivot, pivot, edge_tol, corner_tol) == CellHandle::Pivot);

  // The body interior returns the Move grab.
  CHECK(hit_cell(target, pivot, P(5.0, 5.0), edge_tol, corner_tol) == CellHandle::Body);

  // The rotate ring just OUTSIDE a corner (beyond the corner handle, off the box).
  const arbc::Vec2 tl = P(0.0, 0.0);
  const double outx = tl.x - pivot.x;
  const double outy = tl.y - pivot.y;
  const double len = std::hypot(outx, outy);
  const arbc::Vec2 ring{tl.x + 5.0 * outx / len, tl.y + 5.0 * outy / len};
  CHECK(hit_cell(target, pivot, ring, edge_tol, corner_tol) == CellHandle::Rotate);

  // A point inside the placed box's AABB but OUTSIDE the parallelogram and off every handle is a
  // MISS — the assertion an AABB implementation (which would report a corner scale handle) fails.
  const arbc::Vec2 aabb_corner{70.0, 40.0}; // an AABB corner of this quad, in its empty region
  const CellHandle miss = hit_cell(target, pivot, aabb_corner, edge_tol, corner_tol);
  CHECK(miss == CellHandle::None);
  CHECK_FALSE(is_cell_scale_handle(miss));
}

TEST_CASE("gizmo: move translates the placement; arrow-nudge steps a fixed unit") {
  const arbc::Affine placement{2.0, 0.3, -0.1, 1.4, 12.0, -5.0};
  const arbc::Affine moved = move_cell(placement, 12.0, -8.0);
  CHECK(moved.a == placement.a); // linear part untouched — a pure translation
  CHECK(moved.b == placement.b);
  CHECK(moved.c == placement.c);
  CHECK(moved.d == placement.d);
  CHECK(moved.tx == Approx(24.0));
  CHECK(moved.ty == Approx(-13.0));

  // Arrow-nudge is one fixed step (the L4 commits one entry per press, Constraint 3).
  const arbc::Affine nudged = move_cell(placement, k_cell_nudge, 0.0);
  CHECK(nudged.tx == Approx(placement.tx + k_cell_nudge));
  CHECK(nudged.ty == Approx(placement.ty));
}

TEST_CASE(
    "gizmo: scale — proportional default, free under Shift, 1D on edges, about pivot on Alt") {
  const arbc::Affine placement = arbc::Affine::translation(8.0, 8.0);
  const arbc::Rect extent{0.0, 0.0, 16.0, 16.0};
  const arbc::Vec2 pivot = cell_pivot(placement, extent); // (16,16)

  // A corner drag, no modifier: uniform (aspect-preserving) about the OPPOSITE corner.
  const arbc::Affine prop =
      scale_cell(placement, extent, CellHandle::ScaleBottomRight, {40.0, 40.0}, pivot,
                 /*free_distort=*/false, /*about_pivot=*/false);
  CHECK(prop.a == Approx(2.0));
  CHECK(prop.d == Approx(2.0));
  CHECK(prop.b == Approx(0.0));
  CHECK(prop.c == Approx(0.0));

  // The SAME drag with Shift distorts freely (independent x/y): drag to (40,24) scales x by 2, y
  // by 1 about the opposite corner.
  const arbc::Affine dist =
      scale_cell(placement, extent, CellHandle::ScaleBottomRight, {40.0, 24.0}, pivot,
                 /*free_distort=*/true, /*about_pivot=*/false);
  CHECK(dist.a == Approx(2.0));
  CHECK(dist.d == Approx(1.0));

  // An edge drag scales ONE axis only (1D), the opposite axis held.
  const arbc::Affine edge = scale_cell(placement, extent, CellHandle::ScaleRight, {40.0, 16.0},
                                       pivot, /*free_distort=*/false, /*about_pivot=*/false);
  CHECK(edge.a == Approx(2.0));
  CHECK(edge.d == Approx(1.0));
  CHECK(edge.c == Approx(0.0)); // a plain edge does NOT shear

  // With Alt the transform is about the pivot (center), so the drag scales more to reach it.
  const arbc::Affine about = scale_cell(placement, extent, CellHandle::ScaleBottomRight,
                                        {40.0, 40.0}, pivot, /*free_distort=*/false,
                                        /*about_pivot=*/true);
  CHECK(about.a == Approx(3.0));
  CHECK(about.apply({8.0, 8.0}).x == Approx(pivot.x)); // the pivot's content preimage is fixed
  CHECK(about.apply({8.0, 8.0}).y == Approx(pivot.y));
}

TEST_CASE("gizmo: rotate about the pivot, with 15-degree snap under Shift") {
  const arbc::Affine placement = arbc::Affine::translation(8.0, 8.0);
  const arbc::Rect extent{0.0, 0.0, 16.0, 16.0};
  const arbc::Vec2 center = cell_pivot(placement, extent); // (16,16)

  const arbc::Affine rot = rotate_cell(placement, center, k_pi / 2.0, /*snap_15=*/false);
  CHECK(rot.a == Approx(0.0).margin(1e-9));
  CHECK(rot.b == Approx(1.0));
  CHECK(rot.apply({8.0, 8.0}).x == Approx(center.x)); // the center is the fixed point
  CHECK(rot.apply({8.0, 8.0}).y == Approx(center.y));

  // Shift snaps the swept angle to the nearest 15°: 0.2 rad (~11.5°) rounds to 15°.
  const arbc::Affine snapped = rotate_cell(placement, center, 0.2, /*snap_15=*/true);
  CHECK(snapped.a == Approx(std::cos(k_pi / 12.0)));
  CHECK(snapped.b == Approx(std::sin(k_pi / 12.0)));

  // An off-center pivot is the fixed point of the rotation.
  const arbc::Vec2 off{0.0, 0.0};
  const arbc::Affine rot_off = rotate_cell(placement, off, k_pi / 2.0, /*snap_15=*/false);
  CHECK(rot_off.apply({-8.0, -8.0}).x == Approx(0.0)); // content preimage of (0,0) stays at (0,0)
  CHECK(rot_off.apply({-8.0, -8.0}).y == Approx(0.0));
}

TEST_CASE("gizmo: shear on a modifier+edge drag; a plain edge does not shear") {
  const arbc::Affine placement = arbc::Affine::translation(8.0, 8.0);
  const arbc::Rect extent{0.0, 0.0, 16.0, 16.0};
  const arbc::Vec2 pivot = cell_pivot(placement, extent); // (16,16)

  // Dragging the TOP edge horizontally shears (off-diagonal linear part), about the pivot.
  const arbc::Affine sheared =
      shear_cell(placement, extent, CellHandle::ScaleTop, {24.0, 8.0}, pivot);
  CHECK(sheared.a == Approx(1.0)); // no scale
  CHECK(sheared.d == Approx(1.0));
  CHECK(sheared.c == Approx(-1.0)); // a real horizontal shear
  CHECK(sheared.c != Approx(0.0));

  // A plain edge drag through scale_cell is a 1D scale, not a shear (off-diagonal stays 0).
  const arbc::Affine scaled = scale_cell(placement, extent, CellHandle::ScaleTop, {16.0, 24.0},
                                         pivot, /*free_distort=*/false, /*about_pivot=*/false);
  CHECK(scaled.c == Approx(0.0));
  CHECK(scaled.b == Approx(0.0));
}

TEST_CASE("gizmo: every scale handle drags its own anchor about the opposite one") {
  // An axis-aligned unit box so each handle's dragged/opposite anchor is exact. Exercises all
  // eight handle_anchors branches (corners AND edges), not just the three the other cases touch.
  const arbc::Affine placement = arbc::Affine::identity();
  const arbc::Rect extent{0.0, 0.0, 16.0, 16.0};
  const arbc::Vec2 pivot = cell_pivot(placement, extent); // (8,8), unused (about_pivot=false)

  struct Corner {
    CellHandle handle;
    arbc::Vec2 dragged;
    arbc::Vec2 fixed; // the opposite corner, invariant under a proportional scale
  };
  const Corner corners[] = {
      {CellHandle::ScaleTopLeft, {0.0, 0.0}, {16.0, 16.0}},
      {CellHandle::ScaleTopRight, {16.0, 0.0}, {0.0, 16.0}},
      {CellHandle::ScaleBottomLeft, {0.0, 16.0}, {16.0, 0.0}},
      {CellHandle::ScaleBottomRight, {16.0, 16.0}, {0.0, 0.0}},
  };
  for (const Corner& c : corners) {
    // Drag to twice the fixed->dragged distance => a uniform k=2 about the opposite corner.
    const arbc::Vec2 pointer{2.0 * c.dragged.x - c.fixed.x, 2.0 * c.dragged.y - c.fixed.y};
    const arbc::Affine out = scale_cell(placement, extent, c.handle, pointer, pivot,
                                        /*free_distort=*/false, /*about_pivot=*/false);
    CHECK(out.a == Approx(2.0));
    CHECK(out.d == Approx(2.0));
    CHECK(out.apply(c.fixed).x == Approx(c.fixed.x)); // the opposite corner is the fixed point
    CHECK(out.apply(c.fixed).y == Approx(c.fixed.y));
  }

  struct Edge {
    CellHandle handle;
    arbc::Vec2 pointer;
    arbc::Vec2 fixed; // the opposite edge midpoint, invariant
    bool scales_x;    // which axis this edge drives (the other stays 1)
  };
  const Edge edges[] = {
      {CellHandle::ScaleLeft, {-16.0, 8.0}, {16.0, 8.0}, true},
      {CellHandle::ScaleRight, {32.0, 8.0}, {0.0, 8.0}, true},
      {CellHandle::ScaleTop, {8.0, -16.0}, {8.0, 16.0}, false},
      {CellHandle::ScaleBottom, {8.0, 32.0}, {8.0, 0.0}, false},
  };
  for (const Edge& e : edges) {
    const arbc::Affine out = scale_cell(placement, extent, e.handle, e.pointer, pivot,
                                        /*free_distort=*/false, /*about_pivot=*/false);
    CHECK(out.a == Approx(e.scales_x ? 2.0 : 1.0)); // the active axis doubles...
    CHECK(out.d == Approx(e.scales_x ? 1.0 : 2.0)); // ... the opposite axis is held at 1
    CHECK(out.b == Approx(0.0));                    // a plain edge never shears
    CHECK(out.c == Approx(0.0));
    CHECK(out.apply(e.fixed).x == Approx(e.fixed.x)); // the opposite midpoint is the fixed point
    CHECK(out.apply(e.fixed).y == Approx(e.fixed.y));
  }
}

TEST_CASE("gizmo: shear on a Left/Right edge is a VERTICAL shear (the b off-diagonal)") {
  // The existing shear case drives a Top edge (horizontal shear, the `c` term); this covers the
  // Left/Right branch (vertical shear, the `b` term), which no other case reaches.
  const arbc::Affine placement = arbc::Affine::identity();
  const arbc::Rect extent{0.0, 0.0, 16.0, 16.0};
  const arbc::Vec2 pivot = cell_pivot(placement, extent); // (8,8)

  const arbc::Affine right =
      shear_cell(placement, extent, CellHandle::ScaleRight, {16.0, 16.0}, pivot);
  CHECK(right.a == Approx(1.0)); // no scale
  CHECK(right.d == Approx(1.0));
  CHECK(right.c == Approx(0.0)); // NOT a horizontal shear
  CHECK(right.b != Approx(0.0)); // a real vertical shear (the b off-diagonal)

  const arbc::Affine left = shear_cell(placement, extent, CellHandle::ScaleLeft, {0.0, 0.0}, pivot);
  CHECK(left.a == Approx(1.0));
  CHECK(left.d == Approx(1.0));
  CHECK(left.b != Approx(0.0));
}

TEST_CASE("gizmo: hit_cell rejects a non-finite point and unbounded content") {
  const arbc::Affine placement = arbc::Affine::identity();
  const arbc::Rect extent{0.0, 0.0, 16.0, 16.0};
  const PickTarget bounded = cell_target(placement, extent);
  const arbc::Vec2 pivot = cell_pivot(placement, extent);
  const double nan = std::numeric_limits<double>::quiet_NaN();

  // A non-finite pointer is never a hit (the finite guard).
  CHECK(hit_cell(bounded, pivot, {nan, 4.0}, 1.5, 3.0) == CellHandle::None);
  CHECK(hit_cell(bounded, {nan, nan}, {4.0, 4.0}, 1.5, 3.0) == CellHandle::None);

  // Unbounded content (no extent) has no placed quad, so no gizmo handles anywhere.
  PickTarget unbounded;
  unbounded.kind = PickKind::Cell;
  unbounded.placement = placement;
  unbounded.extent = std::nullopt;
  CHECK(hit_cell(unbounded, pivot, {8.0, 8.0}, 1.5, 3.0) == CellHandle::None);
}

TEST_CASE("gizmo: snap folds in composition-space grid lines (editor.canvas.grid seam)") {
  const arbc::Rect extent{0.0, 0.0, 20.0, 20.0};
  const arbc::Affine candidate = arbc::Affine::translation(10.0, 10.0); // placed AABB [10,30]^2
  const double tol = 2.0;
  const std::vector<PickTarget> no_others;

  // A vertical grid line at x=31 pulls the right edge (x=30) flush onto it.
  const std::vector<double> grid_x{31.0};
  const SnapResult gx = snap_placement(candidate, extent, no_others, tol, /*bypass=*/false, grid_x);
  CHECK(gx.snapped_x);
  CHECK(gx.placement.tx == Approx(11.0));

  // A horizontal grid line at y=31 pulls the bottom edge (y=30) onto it.
  const std::vector<double> grid_y{31.0};
  const SnapResult gy =
      snap_placement(candidate, extent, no_others, tol, /*bypass=*/false, {}, grid_y);
  CHECK(gy.snapped_y);
  CHECK(gy.placement.ty == Approx(11.0));
}

TEST_CASE("gizmo: snap engine snaps edges/centers to other objects; Cmd/Ctrl bypasses") {
  const arbc::Rect extent{0.0, 0.0, 20.0, 20.0};
  const arbc::Affine candidate = arbc::Affine::translation(10.0, 10.0); // placed AABB [10,30]^2
  const double tol = 2.0;

  // Another cell whose LEFT edge is at x=31 (1 unit past the moving cell's right edge at x=30).
  std::vector<PickTarget> others;
  others.push_back(cell_target(arbc::Affine::translation(31.0, 10.0), extent));

  const SnapResult snapped = snap_placement(candidate, extent, others, tol);
  CHECK(snapped.snapped_x);
  CHECK(snapped.placement.tx == Approx(11.0)); // right edge nudged flush onto x=31
  CHECK_FALSE(snapped.guides.empty());

  // Cmd/Ctrl bypass returns the candidate unsnapped, with no guides.
  const SnapResult bypassed = snap_placement(candidate, extent, others, tol, /*bypass=*/true);
  CHECK_FALSE(bypassed.snapped_x);
  CHECK(bypassed.placement.tx == Approx(10.0));
  CHECK(bypassed.guides.empty());

  // Two competing targets resolve to the NEARER: add a left edge at x=30.5 (0.5 away).
  others.push_back(cell_target(arbc::Affine::translation(30.5, 10.0), extent));
  const SnapResult nearer = snap_placement(candidate, extent, others, tol);
  CHECK(nearer.placement.tx == Approx(10.5)); // snapped to x=30.5, the closer edge

  // A camera frame is a snap target exactly like a cell (both come from pick_targets).
  std::vector<PickTarget> cam_only;
  PickTarget cam;
  cam.kind = PickKind::Camera;
  cam.placement = arbc::Affine::translation(31.0, 10.0);
  cam.extent = arbc::Rect{0.0, 0.0, 20.0, 20.0};
  cam_only.push_back(cam);
  CHECK(snap_placement(candidate, extent, cam_only, tol).snapped_x);

  // A candidate outside every tolerance is returned unchanged with no guides.
  std::vector<PickTarget> far;
  far.push_back(cell_target(arbc::Affine::translation(200.0, 200.0), extent));
  const SnapResult none = snap_placement(candidate, extent, far, tol);
  CHECK_FALSE(none.snapped_x);
  CHECK_FALSE(none.snapped_y);
  CHECK(none.placement.tx == Approx(10.0));
  CHECK(none.guides.empty());
}

TEST_CASE("gizmo: degenerate inputs are safe no-ops, never a NaN") {
  const arbc::Rect extent{0.0, 0.0, 16.0, 16.0};
  const arbc::Affine placement = arbc::Affine::translation(8.0, 8.0);
  const arbc::Vec2 pivot = cell_pivot(placement, extent);
  const double inf = std::numeric_limits<double>::infinity();

  // A non-invertible placement.
  const arbc::Affine singular{0.0, 0.0, 0.0, 0.0, 3.0, 4.0};
  CHECK(scale_cell(singular, extent, CellHandle::ScaleBottomRight, {40.0, 40.0}, pivot, false,
                   false) == singular);
  CHECK(rotate_cell(singular, pivot, 0.5, false) == singular);
  CHECK(shear_cell(singular, extent, CellHandle::ScaleTop, {24.0, 8.0}, pivot) == singular);

  // A zero-area extent.
  const arbc::Rect empty{5.0, 5.0, 5.0, 5.0};
  CHECK(scale_cell(placement, empty, CellHandle::ScaleBottomRight, {40.0, 40.0}, pivot, false,
                   false) == placement);

  // A non-positive scale drag that would collapse/flip the box: unchanged.
  CHECK(scale_cell(placement, extent, CellHandle::ScaleBottomRight, {8.0, 8.0}, pivot, false,
                   false) == placement);

  // A non-finite pointer.
  const arbc::Affine nan_scale =
      scale_cell(placement, extent, CellHandle::ScaleBottomRight, {inf, inf}, pivot, false, false);
  CHECK(nan_scale == placement);
  CHECK(std::isfinite(nan_scale.a));
  CHECK(std::isfinite(nan_scale.tx));
}

TEST_CASE("gizmo: D8 invariance — placement moves, content resolution and bounds do not") {
  arbc::Registry registry = gizmo_registry();
  arbc::Document doc;
  doc.add_composition(64.0, 64.0);
  const auto id = ace::scene::add_cell(doc, registry, "org.arbc.raster", "16x16",
                                       arbc::Affine::translation(8.0, 8.0));
  REQUIRE(id.has_value());

  const auto find_cell = [&]() {
    for (const ace::scene::Cell& c : ace::scene::cells(doc, registry)) {
      if (c.id == *id) {
        return c;
      }
    }
    FAIL("cell vanished");
    return ace::scene::Cell{};
  };

  const ace::scene::Cell before = find_cell();
  REQUIRE(before.content_bounds.has_value());
  REQUIRE(before.detail.native_pixels.has_value());
  const arbc::Rect extent = *before.content_bounds;
  const arbc::Vec2 pivot = cell_pivot(before.placement, extent);

  const std::vector<arbc::Affine> results = {
      move_cell(before.placement, 9.0, -4.0),
      scale_cell(before.placement, extent, CellHandle::ScaleBottomRight, {40.0, 40.0}, pivot, false,
                 false),
      rotate_cell(before.placement, pivot, 0.7, false),
      shear_cell(before.placement, extent, CellHandle::ScaleTop, {30.0, 8.0}, pivot),
  };
  for (const arbc::Affine& next : results) {
    doc.set_layer_transform(before.layer, next);
    const ace::scene::Cell after = find_cell();
    CHECK(after.placement == next);                                   // placement DID change
    CHECK(after.content_bounds == before.content_bounds);             // ... but the extent did NOT
    CHECK(after.detail.native_pixels == before.detail.native_pixels); // ... nor the native grid
  }
}

TEST_CASE("gizmo: one transaction per gesture — journal +1, undo restores the prior affine") {
  arbc::Registry registry = gizmo_registry();
  arbc::Document doc;
  doc.add_composition(64.0, 64.0);
  const auto id = ace::scene::add_cell(doc, registry, "org.arbc.raster", "16x16",
                                       arbc::Affine::translation(8.0, 8.0));
  REQUIRE(id.has_value());

  const auto find_cell = [&]() {
    for (const ace::scene::Cell& c : ace::scene::cells(doc, registry)) {
      if (c.id == *id) {
        return c;
      }
    }
    FAIL("cell vanished");
    return ace::scene::Cell{};
  };

  const ace::scene::Cell seed = find_cell();
  const arbc::Rect extent = *seed.content_bounds;
  const arbc::Vec2 pivot = cell_pivot(seed.placement, extent);
  const std::vector<arbc::Affine> gestures = {
      move_cell(seed.placement, 6.0, 6.0),
      scale_cell(seed.placement, extent, CellHandle::ScaleBottomRight, {40.0, 40.0}, pivot, false,
                 false),
      rotate_cell(seed.placement, pivot, 0.5, false),
      shear_cell(seed.placement, extent, CellHandle::ScaleTop, {30.0, 8.0}, pivot),
  };

  for (const arbc::Affine& gesture : gestures) {
    const ace::scene::Cell prior = find_cell();
    const std::size_t cursor_before = doc.journal().cursor();

    // The commit is exactly what the L4 gizmo runs on release: one set_layer_transform. The
    // applied-entry count (cursor) is the "one undo press" measure the shipped suite uses.
    doc.set_layer_transform(prior.layer, gesture);
    CHECK(doc.journal().cursor() == cursor_before + 1); // ONE journal entry per gesture
    CHECK(find_cell().placement == gesture);

    // A single undo restores the exact prior placement (byte-equal Affine).
    REQUIRE(doc.journal().undo());
    CHECK(find_cell().placement == prior.placement);
    CHECK(doc.journal().cursor() == cursor_before);
    REQUIRE(doc.journal().redo()); // re-apply so the next gesture commits without a redo tail
  }
}

TEST_CASE(
    "gizmo: a scaled cell renders SOFTER, not cropped or re-gridded (render_offline golden)") {
  arbc::ObjectId layer{};
  const std::unique_ptr<arbc::Document> doc = build_gizmo_doc(layer);
  const int rw = 64;
  const int rh = 64;

  // Scale the 16x16 cell up ~2x via a bottom-right corner drag about the opposite (top-left)
  // corner — a PLACEMENT change (D8), committed as one set_layer_transform.
  const arbc::Affine start = arbc::Affine::translation(8.0, 8.0);
  const arbc::Rect extent{0.0, 0.0, 16.0, 16.0};
  const arbc::Affine scaled = scale_cell(start, extent, CellHandle::ScaleBottomRight, {40.0, 40.0},
                                         cell_pivot(start, extent), /*free_distort=*/false,
                                         /*about_pivot=*/false);
  CHECK(scaled.a == Approx(2.0));
  CHECK(scaled.d == Approx(2.0));
  doc->set_layer_transform(layer, scaled);

  const ace::render::Srgb8Image image = ace::render::render_document_srgb8(*doc, rw, rh);
  REQUIRE(image.pixels.size() == static_cast<std::size_t>(rw) * rh * 4);
  CHECK(ace_test::compare_golden("gizmo_scale_64x64.rgba8", image.pixels));

  // The scaled cell covers MORE of the frame than the un-scaled one (placement, not a crop).
  arbc::ObjectId layer0{};
  const std::unique_ptr<arbc::Document> doc0 = build_gizmo_doc(layer0);
  const ace::render::Srgb8Image base = ace::render::render_document_srgb8(*doc0, rw, rh);
  CHECK(image.pixels != base.pixels);
}

TEST_CASE("gizmo: an uncommitted preview leaves the composite byte-identical (Constraint 12)") {
  arbc::ObjectId layer{};
  const std::unique_ptr<arbc::Document> doc = build_gizmo_doc(layer);
  const int rw = 64;
  const int rh = 64;
  const arbc::Rect extent{0.0, 0.0, 16.0, 16.0};
  const arbc::Affine start = arbc::Affine::translation(8.0, 8.0);

  const std::vector<std::uint8_t> before = ace::render::render_document_srgb8(*doc, rw, rh).pixels;

  // Computing a drag preview is pure session state — it never touches the Document (D-gizmo-4).
  const arbc::Affine preview = scale_cell(start, extent, CellHandle::ScaleBottomRight, {40.0, 40.0},
                                          cell_pivot(start, extent), false, false);
  CHECK_FALSE(preview == start); // the preview really is a different placement

  const std::vector<std::uint8_t> during = ace::render::render_document_srgb8(*doc, rw, rh).pixels;
  CHECK(before == during); // the composite is unchanged until the release commits
}
