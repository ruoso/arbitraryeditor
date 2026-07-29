// editor.canvas.grid (§6:260, D-grid-2/-4): headless units for the composition-grid line
// generator and its integration with the object-relative snap engine. `composition_grid_lines`
// is the SINGLE source both the L4 draw path and `snap_placement` consume, so these tests pin
// (a) that the generated lines are correct, origin-anchored, and self-limiting, and (b) that
// feeding them into `snap_placement`'s grid_x/grid_y spans snaps a moving edge flush to a line —
// the property the predecessor `editor.cells.gizmo` could not test without a grid to feed.

#include <ace/interact/pick.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/transform.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <vector>

using ace::interact::composition_grid_lines;
using ace::interact::GridLines;
using ace::interact::PickKind;
using ace::interact::PickTarget;
using ace::interact::snap_placement;
using ace::interact::SnapResult;
using Catch::Approx;

namespace {

constexpr int k_cap = 4096;

PickTarget cell_target(const arbc::Affine& placement, const arbc::Rect& extent) {
  PickTarget t;
  t.kind = PickKind::Cell;
  t.placement = placement;
  t.extent = extent;
  return t;
}

} // namespace

TEST_CASE("grid: generation is correct and origin-anchored") {
  // A region flush with the origin includes both endpoints; integer spacing is byte-exact.
  const GridLines g = composition_grid_lines(10.0, arbc::Rect{0.0, 0.0, 100.0, 50.0}, k_cap);
  CHECK(g.xs == std::vector<double>{0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100});
  CHECK(g.ys == std::vector<double>{0, 10, 20, 30, 40, 50});
}

TEST_CASE("grid: an offset region keeps only the multiples inside it") {
  const GridLines g = composition_grid_lines(10.0, arbc::Rect{15.0, 15.0, 35.0, 35.0}, k_cap);
  CHECK(g.xs == std::vector<double>{20, 30});
  CHECK(g.ys == std::vector<double>{20, 30});
}

TEST_CASE("grid: a region spanning the origin includes the negative multiples") {
  const GridLines g = composition_grid_lines(10.0, arbc::Rect{-15.0, -15.0, 15.0, 15.0}, k_cap);
  CHECK(g.xs == std::vector<double>{-10, 0, 10});
  CHECK(g.ys == std::vector<double>{-10, 0, 10});
}

TEST_CASE("grid: guards each return an empty grid, no crash") {
  const arbc::Rect ok{0.0, 0.0, 100.0, 100.0};
  const double inf = std::numeric_limits<double>::infinity();
  const double nan = std::numeric_limits<double>::quiet_NaN();

  SECTION("non-positive spacing") {
    CHECK(composition_grid_lines(0.0, ok, k_cap).xs.empty());
    CHECK(composition_grid_lines(-10.0, ok, k_cap).ys.empty());
  }
  SECTION("non-finite spacing") {
    CHECK(composition_grid_lines(inf, ok, k_cap).xs.empty());
    CHECK(composition_grid_lines(nan, ok, k_cap).ys.empty());
  }
  SECTION("non-finite region") {
    CHECK(composition_grid_lines(10.0, arbc::Rect{0.0, 0.0, inf, 100.0}, k_cap).xs.empty());
    CHECK(composition_grid_lines(10.0, arbc::Rect{0.0, nan, 100.0, 100.0}, k_cap).ys.empty());
  }
  SECTION("degenerate region (x1 < x0)") {
    CHECK(composition_grid_lines(10.0, arbc::Rect{50.0, 0.0, 10.0, 100.0}, k_cap).xs.empty());
  }
  SECTION("over-dense request (per-axis count exceeds max_lines) vanishes") {
    // 100 units / spacing 1 → 101 lines per axis; a cap of 10 makes the whole grid vanish.
    const GridLines g = composition_grid_lines(1.0, ok, 10);
    CHECK(g.xs.empty());
    CHECK(g.ys.empty());
  }
}

TEST_CASE("grid: snap integration — a moving edge snaps flush to a generated grid line") {
  // Grid lines at {0,31,62}; a cell placed at [10,30] whose right edge (x=30) sits within tol
  // of the x=31 line is pulled flush onto it, emitting a guide.
  const GridLines g = composition_grid_lines(31.0, arbc::Rect{0.0, 0.0, 70.0, 70.0}, k_cap);
  REQUIRE(g.xs == std::vector<double>{0, 31, 62});

  const arbc::Rect extent{0.0, 0.0, 20.0, 20.0};
  const arbc::Affine candidate = arbc::Affine::translation(10.0, 10.0); // placed AABB [10,30]^2
  const double tol = 2.0;
  const std::vector<PickTarget> no_others;

  const SnapResult snapped =
      snap_placement(candidate, extent, no_others, tol, /*bypass=*/false, g.xs, g.ys);
  CHECK(snapped.snapped_x);
  CHECK(snapped.snapped_y);
  CHECK(snapped.placement.tx == Approx(11.0)); // right edge 30 nudged flush onto x=31
  CHECK(snapped.placement.ty == Approx(11.0));
  CHECK_FALSE(snapped.guides.empty());
}

TEST_CASE("grid: an edge outside tolerance is returned unchanged") {
  const GridLines g = composition_grid_lines(31.0, arbc::Rect{0.0, 0.0, 70.0, 70.0}, k_cap);
  const arbc::Rect extent{0.0, 0.0, 20.0, 20.0};
  const arbc::Affine candidate =
      arbc::Affine::translation(10.0, 10.0); // right edge x=30, line x=31
  const double tol = 0.5;                    // 30→31 is 1.0 away > tol

  const SnapResult r = snap_placement(candidate, extent, {}, tol, /*bypass=*/false, g.xs, g.ys);
  CHECK_FALSE(r.snapped_x);
  CHECK(r.placement.tx == Approx(10.0));
  CHECK(r.guides.empty());
}

TEST_CASE("grid: bypass (Cmd/Ctrl) returns the candidate unsnapped even with grid lines") {
  const GridLines g = composition_grid_lines(31.0, arbc::Rect{0.0, 0.0, 70.0, 70.0}, k_cap);
  const arbc::Rect extent{0.0, 0.0, 20.0, 20.0};
  const arbc::Affine candidate = arbc::Affine::translation(10.0, 10.0);

  const SnapResult r = snap_placement(candidate, extent, {}, 2.0, /*bypass=*/true, g.xs, g.ys);
  CHECK_FALSE(r.snapped_x);
  CHECK_FALSE(r.snapped_y);
  CHECK(r.placement.tx == Approx(10.0));
}

TEST_CASE("grid: a grid line and an object edge both in tolerance resolve to the nearer") {
  // Grid line at x=31 (right edge 30 is +1 away). An object cell's right edge at x=29.5 is only
  // -0.5 away, so the object edge wins and the placement lands on 29.5, not 31.
  const GridLines g = composition_grid_lines(31.0, arbc::Rect{0.0, 0.0, 70.0, 70.0}, k_cap);
  const arbc::Rect extent{0.0, 0.0, 20.0, 20.0};
  const arbc::Affine candidate = arbc::Affine::translation(10.0, 10.0); // right edge x=30

  std::vector<PickTarget> others;
  others.push_back(cell_target(arbc::Affine::translation(9.5, 40.0), extent)); // right edge x=29.5

  const SnapResult r = snap_placement(candidate, extent, others, 2.0, /*bypass=*/false, g.xs, {});
  CHECK(r.snapped_x);
  CHECK(r.placement.tx == Approx(9.5)); // object edge (nearer) beats the grid line
}

TEST_CASE("grid: empty grid spans reproduce the pre-grid object-only snap exactly") {
  const arbc::Rect extent{0.0, 0.0, 20.0, 20.0};
  const arbc::Affine candidate = arbc::Affine::translation(10.0, 10.0);
  std::vector<PickTarget> others;
  others.push_back(cell_target(arbc::Affine::translation(11.0, 10.0), extent)); // right edge x=31

  const SnapResult object_only = snap_placement(candidate, extent, others, 2.0);
  const SnapResult with_empty_grid =
      snap_placement(candidate, extent, others, 2.0, /*bypass=*/false, {}, {});
  CHECK(object_only.snapped_x == with_empty_grid.snapped_x);
  CHECK(object_only.placement.tx == Approx(with_empty_grid.placement.tx));
  CHECK(with_empty_grid.snapped_x);
}
