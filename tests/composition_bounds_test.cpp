#include <ace/interact/interact.hpp>
#include <ace/project/project.hpp>

#include <arbc/base/transform.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <optional>

// editor.canvas.fit_bounds: the L1 project accessor that reads the root
// composition's authored bounds, plus the reset-branch fit camera it feeds
// (D-fit_bounds-1/2/3/4). GL-free — runs on the plain headless ace_tests lane,
// mirroring tests/render_probe_test.cpp's probe-document use.

TEST_CASE("fit_bounds: root_composition_size returns the probe's authored 64x64") {
  const ace::project::ProbeDocument probe = ace::project::build_probe_document();
  const std::optional<ace::project::CompositionSize> size =
      ace::project::root_composition_size(*probe.document);
  REQUIRE(size.has_value());
  CHECK(size->width == static_cast<double>(ace::project::k_probe_width));
  CHECK(size->height == static_cast<double>(ace::project::k_probe_height));
}

TEST_CASE("fit_bounds: an empty document (no composition) yields nullopt") {
  const arbc::Document doc;
  CHECK_FALSE(ace::project::root_composition_size(doc).has_value());
}

TEST_CASE("fit_bounds: a degenerate authored size yields nullopt") {
  arbc::Document doc;
  doc.add_composition(0.0, 0.0); // canvas_w/canvas_h not > 0 (Constraint 2)
  CHECK_FALSE(ace::project::root_composition_size(doc).has_value());
}

TEST_CASE("fit_bounds: the reset branch's fit camera frames the probe into a non-square pane") {
  const ace::project::ProbeDocument probe = ace::project::build_probe_document();
  const std::optional<ace::project::CompositionSize> size =
      ace::project::root_composition_size(*probe.document);
  REQUIRE(size.has_value());

  // The exact Affine the reset branch submits: fit the 64x64 authored canvas into a
  // NON-SQUARE 128x64 pane. scale = min(128/64, 64/64) = 1, centered in the wide axis
  // (tx = (128 - 64*1)/2 = 32, ty = 0) — so it is NOT identity, pinning that F reaches
  // the authored-bounds fit, not device-pixel identity (D-fit_bounds-4).
  const arbc::Affine fit = ace::interact::fit(size->width, size->height, 128.0, 64.0);
  CHECK_FALSE(fit == arbc::Affine::identity());
  CHECK(fit == ace::interact::fit(64.0, 64.0, 128.0, 64.0));
  CHECK(fit.max_scale() == 1.0);
  CHECK(fit.tx == 32.0);
  CHECK(fit.ty == 0.0);
}
