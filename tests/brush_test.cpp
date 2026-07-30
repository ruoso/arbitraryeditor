// editor.paint.brush — L1 headless Catch2 units for the brush stroke/size math (D5/§4) and the
// `scene::brush_dab` raster verb: the log-slider ↔ view-fraction mapping, the stroke-dab
// interpolation, the screen→composition→content-px footprint, the dab-into-raster verb, the
// coalesce-to-one-entry proof over a live Document, the D8 dual invariance (pixels move, placement
// does not), a render_offline golden of a committed dab, and the no-dab byte-identity. All pure
// arbc::Vec2/Rect/Affine + arbc types beside the gizmo/camera-frame verbs (Constraint 2). No
// ImGui/GL/SDL.
#include <ace/interact/interact.hpp>
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
#include <arbc/media/pixel_traits.hpp>
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

using ace::interact::brush_footprint;
using ace::interact::brush_fraction_from_slider;
using ace::interact::brush_slider_from_fraction;
using ace::interact::brush_units;
using ace::interact::BrushFootprint;
using ace::interact::k_brush_dab_spacing;
using ace::interact::k_brush_max_fraction;
using ace::interact::k_brush_min_fraction;
using ace::interact::stroke_dabs;
using Catch::Approx;

namespace {

arbc::Registry brush_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  return registry;
}

// A 64x64 document whose sole cell is a transparent `org.arbc.raster` at `placement`. Returns the
// raster's content id (the brush target).
std::unique_ptr<arbc::Document> build_raster_doc(const arbc::Registry& registry,
                                                 arbc::ObjectId& raster_cell,
                                                 const arbc::Affine& placement,
                                                 const char* config = "32x32") {
  auto doc = std::make_unique<arbc::Document>();
  doc->add_composition(64.0, 64.0);
  const arbc::expected<arbc::ObjectId, std::string> cell =
      ace::scene::add_cell(*doc, registry, "org.arbc.raster", config, placement);
  REQUIRE(cell.has_value());
  raster_cell = *cell;
  return doc;
}

arbc::RasterContent* resolve_raster(arbc::Document& doc, arbc::ObjectId cell) {
  return dynamic_cast<arbc::RasterContent*>(doc.resolve(cell));
}

// The flat level-0 pixel buffer of the raster's current live base version (post-commit /
// post-undo).
std::vector<float> raster_pixels(arbc::RasterContent& raster) {
  return raster.store().base_table()->level_pixels(0);
}

ace::scene::Cell find_cell(const arbc::Document& doc, const arbc::Registry& registry,
                           arbc::ObjectId id) {
  for (const ace::scene::Cell& c : ace::scene::cells(doc, registry)) {
    if (c.id == id) {
      return c;
    }
  }
  FAIL("cell vanished");
  return ace::scene::Cell{};
}

constexpr arbc::WorkingPixel k_black{0.0F, 0.0F, 0.0F, 1.0F}; // opaque black, premultiplied-linear

} // namespace

// --- Size mapping (D5, the log slider) ---------------------------------------

TEST_CASE(
    "brush: the log-slider view-fraction mapping is monotonic, round-trips, and caps at 100%") {
  // Endpoints: the slider spans the [min, 100%] range exactly.
  CHECK(brush_fraction_from_slider(0.0) == Approx(k_brush_min_fraction));
  CHECK(brush_fraction_from_slider(1.0) == Approx(k_brush_max_fraction)); // 100% cap

  // Strictly increasing across the range (a log slider is still monotone).
  double prev = brush_fraction_from_slider(0.0);
  for (double t = 0.05; t <= 1.0; t += 0.05) {
    const double frac = brush_fraction_from_slider(t);
    CHECK(frac > prev);
    CHECK(frac <= k_brush_max_fraction + 1e-12); // never past the 100% cap
    prev = frac;
  }

  // Out-of-range slider clamps to the endpoints (never a NaN, never past 100%).
  CHECK(brush_fraction_from_slider(-1.0) == Approx(k_brush_min_fraction));
  CHECK(brush_fraction_from_slider(2.0) == Approx(k_brush_max_fraction));

  // Exact inverse across the working band.
  for (double t : {0.1, 0.3, 0.5, 0.7, 0.9}) {
    CHECK(brush_slider_from_fraction(brush_fraction_from_slider(t)) == Approx(t));
  }
  // A 100%-capped (or over-cap) value round-trips to t == 1.
  CHECK(brush_slider_from_fraction(k_brush_max_fraction) == Approx(1.0));
  CHECK(brush_slider_from_fraction(5.0) == Approx(1.0));
  CHECK(brush_slider_from_fraction(0.0) == Approx(0.0)); // below min clamps to t == 0

  // The ~0.3–15% working range sits inside the slider's interior (not jammed at an end).
  const double lo = brush_slider_from_fraction(0.003);
  const double hi = brush_slider_from_fraction(0.15);
  CHECK(lo > 0.0);
  CHECK(lo < hi);
  CHECK(hi < 1.0);
}

TEST_CASE("brush: brush_units scales the view-fraction to composition units linearly") {
  // A regression-pin on the shipped verb: linear in both the fraction and the edge.
  CHECK(brush_units(0.1, 200.0) == Approx(20.0));
  CHECK(brush_units(0.2, 200.0) == Approx(40.0)); // 2x fraction -> 2x units
  CHECK(brush_units(0.1, 400.0) == Approx(40.0)); // 2x edge -> 2x units
  CHECK(brush_units(0.0, 200.0) == Approx(0.0));
}

// --- Stroke interpolation ----------------------------------------------------

TEST_CASE("brush: stroke_dabs spaces dabs within the radius fraction and includes both endpoints") {
  const arbc::Vec2 from{0.0, 0.0};
  const arbc::Vec2 to{10.0, 0.0};
  const double radius = 2.0;
  const std::vector<arbc::Vec2> dabs = stroke_dabs(from, to, radius, k_brush_dab_spacing);

  REQUIRE(dabs.size() >= 2);
  // Endpoints included, in order.
  CHECK(dabs.front().x == Approx(from.x));
  CHECK(dabs.front().y == Approx(from.y));
  CHECK(dabs.back().x == Approx(to.x));
  CHECK(dabs.back().y == Approx(to.y));

  // Consecutive centers never exceed spacing_fraction * radius apart (no gaps).
  const double max_spacing = k_brush_dab_spacing * radius;
  for (std::size_t i = 1; i < dabs.size(); ++i) {
    const double d = std::hypot(dabs[i].x - dabs[i - 1].x, dabs[i].y - dabs[i - 1].y);
    CHECK(d <= max_spacing + 1e-9);
  }
}

TEST_CASE(
    "brush: a zero-length segment is exactly one dab; a non-finite/degenerate input is empty") {
  const arbc::Vec2 p{5.0, 7.0};
  const std::vector<arbc::Vec2> single = stroke_dabs(p, p, 3.0, k_brush_dab_spacing);
  REQUIRE(single.size() == 1); // a single click = one dab
  CHECK(single.front().x == Approx(p.x));
  CHECK(single.front().y == Approx(p.y));

  const double nan = std::numeric_limits<double>::quiet_NaN();
  CHECK(stroke_dabs({nan, 0.0}, {1.0, 0.0}, 2.0, k_brush_dab_spacing).empty());
  CHECK(stroke_dabs({0.0, 0.0}, {nan, 0.0}, 2.0, k_brush_dab_spacing).empty());
  CHECK(stroke_dabs({0.0, 0.0}, {1.0, 0.0}, 0.0, k_brush_dab_spacing).empty());  // radius 0
  CHECK(stroke_dabs({0.0, 0.0}, {1.0, 0.0}, -2.0, k_brush_dab_spacing).empty()); // radius < 0
  CHECK(stroke_dabs({0.0, 0.0}, {1.0, 0.0}, 2.0, 0.0).empty());                  // spacing 0
}

// --- Footprint mapping -------------------------------------------------------

TEST_CASE("brush: the footprint maps device -> content px through the composed affine") {
  // Uniform scale-2 + translate(10,20) placement, identity camera.
  const arbc::Affine placement{2.0, 0.0, 0.0, 2.0, 10.0, 20.0};
  const BrushFootprint fp =
      brush_footprint(arbc::Affine::identity(), placement, arbc::Vec2{30.0, 60.0}, 8.0);
  REQUIRE(fp.valid);
  CHECK(fp.center.x == Approx(10.0)); // (30-10)/2
  CHECK(fp.center.y == Approx(20.0)); // (60-20)/2
  CHECK(fp.radius == Approx(4.0));    // comp_radius 8 / placement scale 2

  // A scaled camera pulls the device point back to composition first.
  const arbc::Affine camera{2.0, 0.0, 0.0, 2.0, 0.0, 0.0}; // 2x device per comp unit
  const arbc::Affine translate{1.0, 0.0, 0.0, 1.0, 4.0, 4.0};
  const BrushFootprint fp2 = brush_footprint(camera, translate, arbc::Vec2{24.0, 24.0}, 6.0);
  REQUIRE(fp2.valid);
  CHECK(fp2.center.x == Approx(8.0)); // (24/2) - 4
  CHECK(fp2.center.y == Approx(8.0));
  CHECK(fp2.radius == Approx(6.0)); // placement scale 1
}

TEST_CASE("brush: a degenerate camera/placement/cursor yields an invalid footprint, never a NaN") {
  const arbc::Affine ok_place{2.0, 0.0, 0.0, 2.0, 10.0, 20.0};
  const arbc::Affine singular{0.0, 0.0, 0.0, 0.0, 3.0, 4.0}; // det 0 -> non-invertible
  const double nan = std::numeric_limits<double>::quiet_NaN();

  CHECK_FALSE(brush_footprint(singular, ok_place, arbc::Vec2{1.0, 1.0}, 4.0).valid);
  CHECK_FALSE(brush_footprint(arbc::Affine::identity(), singular, arbc::Vec2{1.0, 1.0}, 4.0).valid);
  CHECK_FALSE(brush_footprint(arbc::Affine::identity(), ok_place, arbc::Vec2{nan, 1.0}, 4.0).valid);
  CHECK_FALSE(brush_footprint(arbc::Affine::identity(), ok_place, arbc::Vec2{1.0, 1.0}, 0.0).valid);
}

// --- scene::brush_dab verb ---------------------------------------------------

TEST_CASE("brush: brush_dab composites into a resolved raster in ONE transaction") {
  const arbc::Registry registry = brush_registry();
  arbc::ObjectId cell;
  auto doc = build_raster_doc(registry, cell, arbc::Affine::identity());
  arbc::RasterContent* raster = resolve_raster(*doc, cell);
  REQUIRE(raster != nullptr);

  const std::vector<float> before = raster_pixels(*raster);
  const std::size_t cursor_before = doc->journal().cursor();

  // A two-dab call: one transaction, one journal entry.
  const std::vector<arbc::Vec2> centers{arbc::Vec2{8.0, 16.0}, arbc::Vec2{20.0, 16.0}};
  CHECK(ace::scene::brush_dab(*doc, registry, cell, centers, 3.0, 6.0, k_black, 1));
  CHECK(doc->journal().cursor() == cursor_before + 1); // ONE transaction

  const std::vector<float> after = raster_pixels(*raster);
  CHECK(after != before); // touched tiles changed
}

TEST_CASE("brush: brush_dab is a no-op sentinel for a bad target, empty set, or bad radius") {
  const arbc::Registry registry = brush_registry();
  arbc::ObjectId cell;
  auto doc = build_raster_doc(registry, cell, arbc::Affine::identity());
  arbc::RasterContent* raster = resolve_raster(*doc, cell);
  REQUIRE(raster != nullptr);

  // A camera and a non-raster (unbounded solid) cell in the same doc — neither is paintable.
  const arbc::ObjectId camera = ace::scene::add_camera(
      *doc, registry, "Hero", ace::scene::Resolution{64, 64}, arbc::Affine::identity());
  const arbc::expected<arbc::ObjectId, std::string> solid = ace::scene::add_cell(
      *doc, registry, "org.arbc.solid", "0.2,0.2,0.2,1", arbc::Affine::identity());
  REQUIRE(solid.has_value());

  const std::vector<float> before = raster_pixels(*raster);
  const std::size_t cursor_before = doc->journal().cursor();
  const std::vector<arbc::Vec2> centers{arbc::Vec2{16.0, 16.0}};

  CHECK_FALSE(
      ace::scene::brush_dab(*doc, registry, arbc::ObjectId{}, centers, 3.0, 6.0, k_black, 1));
  CHECK_FALSE(ace::scene::brush_dab(*doc, registry, camera, centers, 3.0, 6.0, k_black, 1));
  CHECK_FALSE(ace::scene::brush_dab(*doc, registry, *solid, centers, 3.0, 6.0, k_black, 1));
  CHECK_FALSE(ace::scene::brush_dab(*doc, registry, cell, {}, 3.0, 6.0, k_black, 1)); // empty set
  CHECK_FALSE(
      ace::scene::brush_dab(*doc, registry, cell, centers, 3.0, 0.0, k_black, 1)); // radius 0

  // Nothing opened a transaction and nothing mutated the raster.
  CHECK(doc->journal().cursor() == cursor_before);
  CHECK(raster_pixels(*raster) == before);
}

TEST_CASE("brush: brush_dab skips a non-finite center but still paints the finite ones (no NaN)") {
  const arbc::Registry registry = brush_registry();
  arbc::ObjectId cell;
  auto doc = build_raster_doc(registry, cell, arbc::Affine::identity());
  arbc::RasterContent* raster = resolve_raster(*doc, cell);
  REQUIRE(raster != nullptr);

  const double nan = std::numeric_limits<double>::quiet_NaN();
  const std::vector<arbc::Vec2> centers{arbc::Vec2{nan, nan}, arbc::Vec2{16.0, 16.0}};
  CHECK(ace::scene::brush_dab(*doc, registry, cell, centers, 3.0, 6.0, k_black, 1));

  // The finite dab landed; no pixel is NaN (the non-finite center was skipped, not rasterized).
  const std::vector<float> after = raster_pixels(*raster);
  bool any_finite_nonzero = false;
  for (const float ch : after) {
    CHECK(std::isfinite(ch));
    any_finite_nonzero = any_finite_nonzero || ch != 0.0F;
  }
  CHECK(any_finite_nonzero);
}

// --- Coalescing -> one entry, undo whole stroke (Constraint 6) ---------------

TEST_CASE("brush: a multi-dab stroke under one key is ONE entry and undo restores the pixels") {
  const arbc::Registry registry = brush_registry();
  arbc::ObjectId cell;
  auto doc = build_raster_doc(registry, cell, arbc::Affine::identity());
  arbc::RasterContent* raster = resolve_raster(*doc, cell);
  REQUIRE(raster != nullptr);

  // Capture the D8 invariants + pixels before the stroke.
  const ace::scene::Cell seed = find_cell(*doc, registry, cell);
  const std::vector<float> before = raster_pixels(*raster);
  const std::size_t cursor_before = doc->journal().cursor();

  // Stroke A: five per-frame commits, ALL stamped with one gesture key -> one entry.
  const std::uint64_t key_a = 7;
  for (int i = 0; i < 5; ++i) {
    const std::vector<arbc::Vec2> centers{arbc::Vec2{4.0 + i * 4.0, 16.0}};
    CHECK(ace::scene::brush_dab(*doc, registry, cell, centers, 2.0, 4.0, k_black, key_a));
  }
  CHECK(doc->journal().cursor() == cursor_before + 1); // whole stroke folded to ONE entry
  const std::vector<float> painted = raster_pixels(*raster);
  CHECK(painted != before);

  // D8 dual invariance: pixels moved, placement/bounds/native resolution did not.
  const ace::scene::Cell after_cell = find_cell(*doc, registry, cell);
  CHECK(after_cell.placement == seed.placement);
  CHECK(after_cell.content_bounds == seed.content_bounds);
  CHECK(after_cell.detail.native_pixels == seed.detail.native_pixels);

  // One undo press reverses the whole stroke, byte-equal.
  REQUIRE(doc->journal().undo());
  CHECK(doc->journal().cursor() == cursor_before);
  CHECK(raster_pixels(*raster) == before);
  REQUIRE(doc->journal().redo()); // re-apply so the tip is clean for the next stroke

  // Stroke B under a DIFFERENT key is a second, separate entry / undo press.
  const std::uint64_t key_b = 8;
  const std::vector<arbc::Vec2> centers_b{arbc::Vec2{16.0, 16.0}};
  CHECK(ace::scene::brush_dab(*doc, registry, cell, centers_b, 2.0, 4.0, k_black, key_b));
  CHECK(doc->journal().cursor() == cursor_before + 2); // two strokes = two entries
  REQUIRE(doc->journal().undo());
  CHECK(raster_pixels(*raster) == painted); // back to the end of stroke A
}

// --- Rendered output: golden + no-dab byte-identity (Constraint 7) -----------

namespace {

// A 64x64 composition: a green full-frame background + a bounded 40x40 raster at (12,12).
std::unique_ptr<arbc::Document> build_golden_doc(const arbc::Registry& registry,
                                                 arbc::ObjectId& raster_cell) {
  auto doc = std::make_unique<arbc::Document>();
  const arbc::ObjectId root = doc->add_composition(64.0, 64.0);
  const arbc::ObjectId bg =
      doc->add_content(std::make_shared<arbc::SolidContent>(arbc::Rgba{0.0F, 0.35F, 0.0F, 1.0F}));
  doc->attach_layer(root, doc->add_layer(bg, arbc::Affine::identity()));
  const arbc::expected<arbc::ObjectId, std::string> cell = ace::scene::add_cell(
      *doc, registry, "org.arbc.raster", "40x40", arbc::Affine::translation(12.0, 12.0));
  REQUIRE(cell.has_value());
  raster_cell = *cell;
  return doc;
}

} // namespace

TEST_CASE("brush: a committed soft dab composites to exact pixels (render_offline golden)") {
  const arbc::Registry registry = brush_registry();
  arbc::ObjectId cell;
  const std::unique_ptr<arbc::Document> doc = build_golden_doc(registry, cell);

  // A single soft round dab (inner 6, outer 12) of a fixed premultiplied-linear color at the
  // raster's center. byte-exact because round_dab is libm-free (raster_content.hpp:104).
  const std::vector<arbc::Vec2> centers{arbc::Vec2{20.0, 20.0}};
  const arbc::WorkingPixel paint{0.6F, 0.0F, 0.0F, 1.0F};
  REQUIRE(ace::scene::brush_dab(*doc, registry, cell, centers, 6.0, 12.0, paint, 1));

  const ace::render::Srgb8Image image = ace::render::render_document_srgb8(*doc, 64, 64);
  REQUIRE(image.pixels.size() == static_cast<std::size_t>(64) * 64 * 4);
  CHECK(ace_test::compare_golden("brush_dab_64x64.rgba8", image.pixels));
}

TEST_CASE("brush: two no-dab frames are byte-identical (a render regression is not a brush one)") {
  const arbc::Registry registry = brush_registry();
  arbc::ObjectId cell;
  const std::unique_ptr<arbc::Document> doc = build_golden_doc(registry, cell);

  const ace::render::Srgb8Image a = ace::render::render_document_srgb8(*doc, 64, 64);
  const ace::render::Srgb8Image b = ace::render::render_document_srgb8(*doc, 64, 64);
  CHECK(a.pixels == b.pixels); // no committed dab between frames -> byte-identical composite
}
