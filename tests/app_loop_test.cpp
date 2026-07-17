#include <ace/app/app_loop.hpp>

#include <catch2/catch_test_macros.hpp>

// The one pure, UI-agnostic helper the shell factors out — the frame-loop
// lifecycle predicate — unit-tested headless (docs §9; refinement §"L1 Catch2
// units"). The rest of this leaf's coverage is the offscreen smoke + e2e.
using ace::app::should_continue_loop;

TEST_CASE("frame-loop predicate: a quit request always stops the loop") {
  CHECK_FALSE(should_continue_loop(0, 0, true));
  CHECK_FALSE(should_continue_loop(0, 100, true));
  CHECK_FALSE(should_continue_loop(7, 3, true));
}

TEST_CASE("frame-loop predicate: max_frames <= 0 runs until quit") {
  CHECK(should_continue_loop(0, 0, false));
  CHECK(should_continue_loop(1000000, 0, false));
  CHECK(should_continue_loop(5, -1, false));
}

TEST_CASE("frame-loop predicate: a positive cap stops at N frames") {
  CHECK(should_continue_loop(0, 3, false));
  CHECK(should_continue_loop(2, 3, false));
  CHECK_FALSE(should_continue_loop(3, 3, false));
  CHECK_FALSE(should_continue_loop(4, 3, false));
}
