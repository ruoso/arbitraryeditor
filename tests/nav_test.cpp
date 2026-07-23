// editor.canvas.nav — the L1 viewport-camera navigation math, headless + GL-free
// (docs/01-architecture.md §9, the base of the test pyramid). Exercises
// ace::interact::pan / zoom / scale_bar / fit — pure arbc::Affine geometry — with
// no HostViewport, no GL, no ImGui (D-nav-2). Mirrors tests/interact_test.cpp.
#include <ace/commands/app_state.hpp>
#include <ace/interact/interact.hpp>
#include <ace/interact/pick.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/expected.hpp>
#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using ace::commands::AppState;
using ace::commands::Command;
using ace::commands::dispatch;
using ace::interact::fit;
using ace::interact::fit_region;
using ace::interact::pan;
using ace::interact::pick_targets;
using ace::interact::PickKind;
using ace::interact::PickTarget;
using ace::interact::scale_bar;
using ace::interact::selected_extent;
using ace::interact::zoom;
using Catch::Approx;

namespace {
// The composition point under a device pixel, for the invariance checks.
arbc::Vec2 unproject(const arbc::Affine& camera, arbc::Vec2 device) {
  const auto inv = camera.inverse();
  REQUIRE(inv.has_value());
  return inv->apply(device);
}

arbc::ObjectId oid(std::uint64_t value) { return arbc::ObjectId{value}; }

// A hand-built pick target, the frame_selection_test mould: the extent is content-space,
// nullopt for an unbounded fill.
PickTarget cell_target(std::uint64_t id, const arbc::Affine& placement,
                       std::optional<arbc::Rect> extent) {
  return PickTarget{oid(id), oid(id + 1000), PickKind::Cell, placement, extent};
}

// A fresh workspace-backed session with a root composition (the frame_selection_test mould),
// for the combined-wiring case that drives the aid over a real document.
struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_nav_aids_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

AppState session_with_composition(const ScratchDir& scratch, const ace::platform::FileSystem& fs,
                                  const char* leaf) {
  auto created = ace::project::create_project(fs, scratch.root / leaf);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  dispatch(state, Command{"add_composition",
                          [](arbc::Document& doc) { doc.add_composition(256.0, 256.0); }});
  return state;
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

// --- editor.canvas.nav_aids: the positioned fit (D24 / D-nav_aids-2) ------------------------

TEST_CASE("nav_aids: fit_region frames a positioned region centered, uniform-scaled") {
  // A region FAR from the origin (its position is exactly what `fit` cannot express): W=100,
  // H=50, framed into a non-square 200x200 pane.
  const arbc::Rect region{100.0, 100.0, 200.0, 150.0};
  const arbc::Affine f = fit_region(region, 200.0, 200.0);

  // Uniform scale = min(200/100, 200/50) = 2; the region center (150,125) lands on the pane
  // center (100,100), so tx = 100 - 2*150 = -200, ty = 100 - 2*125 = -150.
  CHECK(f.a == Approx(2.0));
  CHECK(f.d == Approx(2.0));
  CHECK(f.b == Approx(0.0));
  CHECK(f.c == Approx(0.0));
  CHECK(f.tx == Approx(-200.0));
  CHECK(f.ty == Approx(-150.0));

  const arbc::Vec2 center = f.apply(arbc::Vec2{150.0, 125.0});
  CHECK(center.x == Approx(100.0));
  CHECK(center.y == Approx(100.0));

  // All four region corners land within [0,200]x[0,200], and the TIGHT (width) axis touches
  // both pane edges — the region is centered with the leftover on the height axis.
  const arbc::Vec2 tl = f.apply(arbc::Vec2{100.0, 100.0});
  const arbc::Vec2 br = f.apply(arbc::Vec2{200.0, 150.0});
  CHECK(tl.x == Approx(0.0));   // width axis is tight: touches x=0
  CHECK(br.x == Approx(200.0)); // ... and x=200
  CHECK(tl.y == Approx(50.0));  // height axis centered: leftover 100px split
  CHECK(br.y == Approx(150.0));
  for (const arbc::Vec2 c : {tl, br}) {
    CHECK(c.x >= -1e-9);
    CHECK(c.x <= 200.0 + 1e-9);
    CHECK(c.y >= -1e-9);
    CHECK(c.y <= 200.0 + 1e-9);
  }
}

TEST_CASE("nav_aids: fit_region is the positioned generalization of fit; fit delegates") {
  // Reset-to-fit is exactly the origin-anchored specialization: for several shapes,
  // fit(w,h,pw,ph) equals fit_region({0,0,w,h}, pw,ph) field-for-field (D-nav_aids-2).
  struct Case {
    double w, h, pw, ph;
  };
  for (const Case k : {Case{200.0, 100.0, 100.0, 100.0}, Case{64.0, 64.0, 64.0, 64.0},
                       Case{30.0, 120.0, 400.0, 200.0}, Case{7.0, 3.0, 50.0, 90.0}}) {
    const arbc::Affine a = fit(k.w, k.h, k.pw, k.ph);
    const arbc::Affine b = fit_region(arbc::Rect{0.0, 0.0, k.w, k.h}, k.pw, k.ph);
    CHECK(a == b);
  }
}

TEST_CASE("nav_aids: a degenerate region or pane yields identity (no NaN escapes)") {
  const arbc::Affine id = arbc::Affine::identity();
  CHECK(fit_region(arbc::Rect{10.0, 10.0, 10.0, 60.0}, 200.0, 200.0) == id); // empty (zero width)
  CHECK(fit_region(arbc::Rect{50.0, 10.0, 10.0, 60.0}, 200.0, 200.0) == id); // inverted x
  CHECK(fit_region(arbc::Rect{10.0, 10.0, 60.0, 60.0}, 0.0, 200.0) == id);   // non-positive pane
  CHECK(fit_region(arbc::Rect{10.0, 10.0, 60.0, 60.0}, 200.0, -5.0) == id);
  const double inf = std::numeric_limits<double>::infinity();
  CHECK(fit_region(arbc::Rect{10.0, 10.0, inf, 60.0}, 200.0, 200.0) == id); // non-finite region
  const double nan = std::numeric_limits<double>::quiet_NaN();
  CHECK(fit_region(arbc::Rect{nan, 10.0, 60.0, 60.0}, 200.0, 200.0) == id);
}

TEST_CASE("nav_aids: the fit-to-cell chain frames a real cell's placed extent into the pane") {
  // The exact chain the Shift+F branch runs, headless: pick_targets -> selected_extent ->
  // fit_region over a real document (mirrors frame_selection_test's full-chain case).
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "fit_to_cell");

  // A 64x48 raster placed unscaled at a known off-center offset: placed extent [10,20]x[74,68].
  const arbc::expected<arbc::ObjectId, std::string> added =
      ace::scene::add_cell(state.document(), state.registry(), "org.arbc.raster", "64x48",
                           arbc::Affine::translation(10.0, 20.0));
  REQUIRE(added.has_value());
  state.selection().select(*added);

  const std::vector<PickTarget> targets = pick_targets(state.document(), state.registry());
  const std::optional<arbc::Rect> extent = selected_extent(targets, state.selection().items());
  REQUIRE(extent.has_value());
  CHECK(extent->x0 == Approx(10.0));
  CHECK(extent->y0 == Approx(20.0));
  CHECK(extent->x1 == Approx(74.0));
  CHECK(extent->y1 == Approx(68.0));

  // Frame it into a 128x128 pane: W=64,H=48 -> scale = min(128/64, 128/48) = 2; the cell
  // center (42,44) maps to the pane center (64,64).
  const arbc::Affine camera = fit_region(*extent, 128.0, 128.0);
  const arbc::Vec2 center = camera.apply(arbc::Vec2{42.0, 44.0});
  CHECK(center.x == Approx(64.0));
  CHECK(center.y == Approx(64.0));
  // The tight (width) axis touches both pane edges; the whole cell is inside the pane.
  const arbc::Vec2 tl = camera.apply(arbc::Vec2{10.0, 20.0});
  const arbc::Vec2 br = camera.apply(arbc::Vec2{74.0, 68.0});
  CHECK(tl.x == Approx(0.0));
  CHECK(br.x == Approx(128.0));
  CHECK(tl.y == Approx(16.0)); // (128 - 48*2)/2 = 16 leftover on the height axis
  CHECK(br.y == Approx(112.0));
}

TEST_CASE("nav_aids: selected_extent refusal preconditions are nullopt (the no-op trigger)") {
  // The two cases the app branch guards on: an empty selection and an all-unbounded selection
  // both yield nullopt, so Shift+F leaves the camera untouched (D-nav_aids-5 / Constraint 6).
  const std::vector<PickTarget> bounded = {
      cell_target(1, arbc::Affine::translation(0.0, 0.0), arbc::Rect{0.0, 0.0, 10.0, 10.0})};
  const std::vector<arbc::ObjectId> none{};
  CHECK_FALSE(selected_extent(bounded, none).has_value()); // empty selection

  const std::vector<PickTarget> unbounded = {
      cell_target(1, arbc::Affine::translation(0.0, 0.0), std::nullopt),
      cell_target(2, arbc::Affine::translation(40.0, 40.0), std::nullopt)};
  const std::vector<arbc::ObjectId> both = {oid(1), oid(2)};
  CHECK_FALSE(selected_extent(unbounded, both).has_value()); // only unbounded fills
}
