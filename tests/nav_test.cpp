// editor.canvas.nav — the L1 viewport-camera navigation math, headless + GL-free
// (docs/01-architecture.md §9, the base of the test pyramid). Exercises
// ace::interact::pan / zoom / scale_bar / fit — pure arbc::Affine geometry — with
// no HostViewport, no GL, no ImGui (D-nav-2). Mirrors tests/interact_test.cpp.
#include <ace/interact/interact.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/transform.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using ace::interact::fit;
using ace::interact::pan;
using ace::interact::scale_bar;
using ace::interact::zoom;
using Catch::Approx;

namespace {
// The composition point under a device pixel, for the invariance checks.
arbc::Vec2 unproject(const arbc::Affine& camera, arbc::Vec2 device) {
  const auto inv = camera.inverse();
  REQUIRE(inv.has_value());
  return inv->apply(device);
}
} // namespace

TEST_CASE("nav pan: a device drag translates the framing by drag / scale composition units") {
  // A 2x camera (2 device px per composition unit) with a translation.
  const arbc::Affine cam{2.0, 0.0, 0.0, 2.0, 10.0, -5.0};

  const arbc::Affine out = pan(cam, 8.0, -6.0);
  // Post-translate in device space: only the translation moves, the linear part is
  // preserved (so the zoom level is untouched).
  CHECK(out.a == cam.a);
  CHECK(out.d == cam.d);
  CHECK(out.tx == Approx(cam.tx + 8.0));
  CHECK(out.ty == Approx(cam.ty - 6.0));

  // The framing (composition point under a fixed device pixel) shifts by the drag
  // divided by the scale — drag right by d px at scale s moves the framing by d/s
  // composition units.
  const arbc::Vec2 focus{20.0, 20.0};
  const arbc::Vec2 before = unproject(cam, focus);
  const arbc::Vec2 after = unproject(out, focus);
  CHECK(after.x - before.x == Approx(-8.0 / 2.0));
  CHECK(after.y - before.y == Approx(6.0 / 2.0));
}

TEST_CASE(
    "nav zoom: the composition point under the cursor is invariant; scale tracks the factor") {
  const arbc::Affine cam{1.5, 0.0, 0.0, 1.5, 3.0, 7.0};
  const arbc::Vec2 focus{40.0, 24.0};
  const double factor = 2.0;

  const arbc::Affine out = zoom(cam, focus, factor);

  // The composition point under the focus pixel does not move (Constraint 4).
  const arbc::Vec2 before = unproject(cam, focus);
  const arbc::Vec2 after = unproject(out, focus);
  CHECK(after.x == Approx(before.x).epsilon(1e-9));
  CHECK(after.y == Approx(before.y).epsilon(1e-9));

  // The linear-part magnitude scaled by the wheel factor.
  CHECK(out.max_scale() == Approx(cam.max_scale() * factor));

  // Zooming out by the reciprocal returns to the original scale, focus still fixed.
  const arbc::Affine back = zoom(out, focus, 1.0 / factor);
  CHECK(back.max_scale() == Approx(cam.max_scale()));
  const arbc::Vec2 back_pt = unproject(back, focus);
  CHECK(back_pt.x == Approx(before.x).epsilon(1e-9));
  CHECK(back_pt.y == Approx(before.y).epsilon(1e-9));
}

TEST_CASE("nav zoom∘pan composes: zoom holds the focus, a following pan is purely additive") {
  const arbc::Affine cam = arbc::Affine::identity();
  const arbc::Vec2 focus{32.0, 32.0};

  const arbc::Affine zoomed = zoom(cam, focus, 4.0);
  // The zoom step alone keeps the focus composition point fixed.
  CHECK(unproject(zoomed, focus).x == Approx(unproject(cam, focus).x).epsilon(1e-9));

  // A pan after the zoom adds to the translation and leaves the (now 4x) scale intact.
  const arbc::Affine panned = pan(zoomed, 12.0, 4.0);
  CHECK(panned.max_scale() == Approx(zoomed.max_scale()));
  CHECK(panned.tx == Approx(zoomed.tx + 12.0));
  CHECK(panned.ty == Approx(zoomed.ty + 4.0));
}

TEST_CASE("nav scale_bar: nice-number length, device width in the target band, label matches") {
  // A non-round scale so the nice-number rounding is exercised.
  const arbc::Affine cam = arbc::Affine::scaling(3.0, 3.0); // 3 device px / comp unit
  const double target = 100.0;

  const ace::interact::ScaleBar bar = scale_bar(cam, target);

  // (a) composition-units-per-pixel == 1 / max_scale, and the label matches the width.
  CHECK(bar.units == Approx(bar.device_px / cam.max_scale()));
  CHECK(1.0 / cam.max_scale() == Approx(1.0 / 3.0));

  // (b) the chosen length is a 1/2/5·10ⁿ nice number.
  const double decade = std::pow(10.0, std::floor(std::log10(bar.units)));
  const double fraction = bar.units / decade;
  CHECK((fraction == Approx(1.0) || fraction == Approx(2.0) || fraction == Approx(5.0)));

  // (c) its device width lands in the target band (rounded down, so within (t/2.5, t]).
  CHECK(bar.device_px <= target + 1e-9);
  CHECK(bar.device_px > target / 2.5);

  // A degenerate camera scale draws nothing.
  const ace::interact::ScaleBar none = scale_bar(arbc::Affine::scaling(0.0, 0.0), target);
  CHECK(none.units == 0.0);
  CHECK(none.device_px == 0.0);
}

TEST_CASE("nav fit: frames the content in the pane with a uniform, centered scale") {
  // Content wider than the pane's aspect: scale is bounded by the width axis and the
  // content is centered vertically.
  const arbc::Affine f = fit(200.0, 100.0, 100.0, 100.0);
  CHECK(f.a == Approx(0.5)); // min(100/200, 100/100) == 0.5
  CHECK(f.d == Approx(0.5));
  CHECK(f.tx == Approx(0.0));
  CHECK(f.ty == Approx((100.0 - 100.0 * 0.5) * 0.5)); // centered leftover height

  // The four content corners map inside the pane, and the far corner lands on an edge.
  const arbc::Vec2 far = f.apply(arbc::Vec2{200.0, 100.0});
  CHECK(far.x == Approx(100.0));
  CHECK(far.y == Approx(75.0));

  // A pane matching the content is the identity fit (the default framing).
  const arbc::Affine same = fit(64.0, 64.0, 64.0, 64.0);
  CHECK(same == arbc::Affine::identity());

  // A degenerate content/pane falls back to identity (nothing to fit).
  CHECK(fit(0.0, 10.0, 10.0, 10.0) == arbc::Affine::identity());
}
