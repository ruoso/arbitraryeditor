#include <ace/interact/interact.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/transform.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <optional>

// L1 logic unit-tested headless — the base of the test pyramid (docs §9).
TEST_CASE("brush_units maps a view fraction to composition units") {
  // 6% of a 200-unit short edge is 12 units (docs/00-design.md D5).
  REQUIRE(ace::interact::brush_units(0.06, 200.0) == 12.0);
  REQUIRE(ace::interact::brush_units(0.0, 200.0) == 0.0);
}

// --- place_in_view (editor.cells.model; Constraint 6/7) ----------------------

TEST_CASE("place_in_view centres the content's extent in the visible region") {
  // Identity view over a 100x200 pane: the visible composition region IS [0,100]x
  // [0,200]. Its shorter edge is 100, so a half fill targets 50 composition units;
  // the content's longer edge (10) scales by 5 to reach it, and the result is
  // centred on the region's centre (50,100).
  const arbc::Affine placement = ace::interact::place_in_view(
      arbc::Affine::identity(), 100, 200, arbc::Rect{0.0, 0.0, 10.0, 10.0}, 0.5);
  CHECK(placement.a == 5.0);
  CHECK(placement.d == 5.0);
  CHECK(placement.b == 0.0);
  CHECK(placement.c == 0.0);
  CHECK(placement.tx == 25.0);
  CHECK(placement.ty == 75.0);
  // The placed extent really is centred: [25,75] x [75,125] inside [0,100]x[0,200].
  const arbc::Rect placed = placement.map_rect(arbc::Rect{0.0, 0.0, 10.0, 10.0});
  CHECK(placed.x0 == 25.0);
  CHECK(placed.x1 == 75.0);
  CHECK(placed.y0 == 75.0);
  CHECK(placed.y1 == 125.0);
}

TEST_CASE("place_in_view honours the fill fraction and a non-identity view camera") {
  // A smaller fill fraction over the same framing shrinks the placement uniformly
  // and keeps it centred (D8: the fraction is placement, never resolution).
  const arbc::Affine quarter = ace::interact::place_in_view(arbc::Affine::identity(), 100, 200,
                                                            arbc::Rect{0.0, 0.0, 10.0, 10.0}, 0.25);
  CHECK(quarter.a == 2.5);
  CHECK(quarter.tx == 37.5);
  CHECK(quarter.ty == 87.5);

  // A 2x-zoomed, panned viewport camera over a 100x100 pane shows composition
  // [-15,35]x[-20,30] (the pane pulled back through the camera): a 50x50 region, so
  // a half fill targets 25 units. A 10x20 content's longer edge (20) scales by 1.25,
  // centred on (10,5).
  const arbc::Affine view{2.0, 0.0, 0.0, 2.0, 30.0, 40.0};
  const arbc::Affine placement =
      ace::interact::place_in_view(view, 100, 100, arbc::Rect{0.0, 0.0, 10.0, 20.0}, 0.5);
  CHECK(placement.a == 1.25);
  CHECK(placement.d == 1.25);
  CHECK(placement.tx == 3.75);
  CHECK(placement.ty == -7.5);
}

TEST_CASE("place_in_view yields identity for unbounded content") {
  // A factory-built org.arbc.solid reports no bounds (its config grammar admits
  // none, D-cells_model-3): scaling an unbounded fill is meaningless, so the
  // placement is a no-op affine rather than an arbitrary one.
  const arbc::Affine placement =
      ace::interact::place_in_view(arbc::Affine::identity(), 640, 480, std::nullopt, 0.5);
  CHECK(placement == arbc::Affine::identity());
}

TEST_CASE("place_in_view falls back to identity on every degenerate input, never a NaN") {
  const arbc::Rect content{0.0, 0.0, 10.0, 10.0};
  const arbc::Affine identity = arbc::Affine::identity();

  // A non-invertible (collapsed) view camera has no visible region to centre in.
  const arbc::Affine collapsed =
      ace::interact::place_in_view(arbc::Affine::scaling(0.0, 0.0), 640, 480, content, 0.5);
  CHECK(collapsed == identity);
  // A zero / negative pane.
  CHECK(ace::interact::place_in_view(identity, 0, 480, content, 0.5) == identity);
  CHECK(ace::interact::place_in_view(identity, 640, -1, content, 0.5) == identity);
  // An empty content rect (x0 == x1) — nothing to scale.
  CHECK(ace::interact::place_in_view(identity, 640, 480, arbc::Rect{5.0, 5.0, 5.0, 9.0}, 0.5) ==
        identity);
  // A non-positive / non-finite fill fraction.
  CHECK(ace::interact::place_in_view(identity, 640, 480, content, 0.0) == identity);
  CHECK(ace::interact::place_in_view(identity, 640, 480, content,
                                     std::numeric_limits<double>::quiet_NaN()) == identity);
  // An infinite content extent would divide to a zero scale — identity, no NaN.
  const arbc::Affine infinite =
      ace::interact::place_in_view(identity, 640, 480, arbc::Rect::infinite(), 0.5);
  CHECK(infinite == identity);
  CHECK(std::isfinite(infinite.a));
  CHECK(std::isfinite(infinite.tx));
}
