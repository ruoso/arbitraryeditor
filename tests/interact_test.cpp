#include <ace/interact/interact.hpp>

#include <catch2/catch_test_macros.hpp>

// L1 logic unit-tested headless — the base of the test pyramid (docs §9).
TEST_CASE("brush_units maps a view fraction to composition units") {
  // 6% of a 200-unit short edge is 12 units (docs/00-design.md D5).
  REQUIRE(ace::interact::brush_units(0.06, 200.0) == 12.0);
  REQUIRE(ace::interact::brush_units(0.0, 200.0) == 0.0);
}
