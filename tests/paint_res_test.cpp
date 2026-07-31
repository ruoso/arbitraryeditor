#include <ace/interact/interact.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_raster/raster_content.hpp>
#include <arbc/media/pixel_traits.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

// The brush detail-floor readout math (editor.paint.paint_res; D4/D5/§4), unit-tested headless —
// the base of the test pyramid (docs §9). `interact::brush_detail_floor` is pure over primitive
// geometry: no Document, no scene type, byte-deterministic (D-paint_res-2). The final case drives a
// real `scene::brush_dab` to pin D8 dual invariance (painting never re-grids the cell, §4 storage).

using ace::interact::brush_detail_floor;
using ace::interact::BrushDetailFloor;
using ace::interact::k_detail_floor_px;

namespace {

// --- helpers for the D8-invariance case (mirrors tests/brush_test.cpp) ---------------------------
arbc::Registry paint_res_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  return registry;
}

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

TEST_CASE("paint_res: px-on-cell is the native-px diameter, zoom/placement-relative (D5/D8)") {
  // A raster with a native grid, uniformly scaled placement. `N == 2 * comp_radius / max_scale`;
  // 1 content px = 1 native px (D-resolution-1). comp_radius 8, placement scale 2 -> radius 4,
  // N == 8. Integer scales keep N byte-exact.
  const arbc::Affine scale2 = arbc::Affine::scaling(2.0, 2.0);
  const BrushDetailFloor a = brush_detail_floor(8.0, scale2, /*has_native_pixels=*/true);
  REQUIRE(a.valid);
  CHECK(a.cell_px == 8.0); // 2 * (8 / 2)
  CHECK_FALSE(a.at_floor);

  // Doubling the placement scale HALVES N (finer real detail, D8) while the brush's composition-
  // unit radius is unchanged (screen-locked independence, D5) — the readout tracks the placement,
  // never re-grids the cell.
  const arbc::Affine scale4 = arbc::Affine::scaling(4.0, 4.0);
  const BrushDetailFloor b = brush_detail_floor(8.0, scale4, /*has_native_pixels=*/true);
  REQUIRE(b.valid);
  CHECK(b.cell_px == 4.0); // 2 * (8 / 4)
  CHECK(b.cell_px == a.cell_px / 2.0);

  // A translation-only placement (scale 1) reports the full 2*comp_radius diameter.
  const BrushDetailFloor c =
      brush_detail_floor(8.0, arbc::Affine::translation(10.0, 20.0), /*has_native_pixels=*/true);
  REQUIRE(c.valid);
  CHECK(c.cell_px == 16.0);
}

TEST_CASE("paint_res: worst-axis under a non-uniform placement (max_scale, floor-first)") {
  // An edge-dragged (non-uniform-scale) placement: the readout reports N from the LARGEST axis
  // scale (fewest native px, floor-first, Constraint 4), matching brush_footprint's `max_scale`
  // convention — never the roomier min-axis count. scale (2,4): max_scale 4, comp_radius 8 ->
  // radius 2, N == 4. Had it used the min axis (2) N would be 8, over-reporting headroom.
  const arbc::Affine skewed = arbc::Affine::scaling(2.0, 4.0);
  const BrushDetailFloor n = brush_detail_floor(8.0, skewed, /*has_native_pixels=*/true);
  REQUIRE(n.valid);
  CHECK(n.cell_px == 4.0); // 2 * (8 / max(2,4))
  // Orientation-independent: the same magnification on the other axis reports the same N.
  const BrushDetailFloor m =
      brush_detail_floor(8.0, arbc::Affine::scaling(4.0, 2.0), /*has_native_pixels=*/true);
  REQUIRE(m.valid);
  CHECK(m.cell_px == n.cell_px);
}

TEST_CASE("paint_res: detail-floor verdict at/below one native px, boundary from both sides") {
  // The threshold is one native pixel of DIAMETER (k_detail_floor_px). Exercised from both sides.
  CHECK(k_detail_floor_px == 1.0);

  // N well above 1 -> not at the floor.
  const BrushDetailFloor high =
      brush_detail_floor(8.0, arbc::Affine::identity(), /*has_native_pixels=*/true);
  REQUIRE(high.valid);
  CHECK(high.cell_px == 16.0);
  CHECK_FALSE(high.at_floor);

  // Just ABOVE the floor: comp_radius 0.75, scale 1 -> N 1.5 -> not at floor.
  const BrushDetailFloor above =
      brush_detail_floor(0.75, arbc::Affine::identity(), /*has_native_pixels=*/true);
  REQUIRE(above.valid);
  CHECK(above.cell_px == 1.5);
  CHECK_FALSE(above.at_floor);

  // Exactly AT the floor: comp_radius 0.5, scale 1 -> N 1.0 -> at floor (inclusive boundary).
  const BrushDetailFloor at =
      brush_detail_floor(0.5, arbc::Affine::identity(), /*has_native_pixels=*/true);
  REQUIRE(at.valid);
  CHECK(at.cell_px == 1.0);
  CHECK(at.at_floor);

  // BELOW the floor (sub-pixel dab): comp_radius 0.4, scale 1 -> N 0.8 -> at floor.
  const BrushDetailFloor below =
      brush_detail_floor(0.4, arbc::Affine::identity(), /*has_native_pixels=*/true);
  REQUIRE(below.valid);
  CHECK(below.cell_px == Catch::Approx(0.8));
  CHECK(below.at_floor);

  // The floor is N-relative (the helper reads only native-px PRESENCE, not the grid's total size):
  // a placement magnified enough hits the floor regardless of how many native px the cell has.
  const BrushDetailFloor tiny =
      brush_detail_floor(0.5, arbc::Affine::scaling(2.0, 2.0), /*has_native_pixels=*/true);
  REQUIRE(tiny.valid);
  CHECK(tiny.cell_px == 0.5); // 2 * (0.5 / 2)
  CHECK(tiny.at_floor);
}

TEST_CASE("paint_res: N/A inputs yield no N and no false floor (no div-by-zero)") {
  // A ResolutionIndependent target (native px absent) has no floor at all: no number, no floor.
  const BrushDetailFloor no_native =
      brush_detail_floor(8.0, arbc::Affine::identity(), /*has_native_pixels=*/false);
  CHECK_FALSE(no_native.valid);
  CHECK(no_native.cell_px == 0.0);
  CHECK_FALSE(no_native.at_floor);

  // A zero-area placement (BOTH axes collapsed) has a zero worst-axis scale: N/A, no div-by-zero.
  const BrushDetailFloor collapsed =
      brush_detail_floor(8.0, arbc::Affine::scaling(0.0, 0.0), /*has_native_pixels=*/true);
  CHECK_FALSE(collapsed.valid);
  CHECK(collapsed.cell_px == 0.0);
  CHECK_FALSE(collapsed.at_floor);

  // A single collapsed axis (det == 0, non-invertible) still has a positive worst-axis scale but no
  // footprint: N/A via the invertibility guard (as brush_footprint), never a false number.
  const BrushDetailFloor half_collapsed =
      brush_detail_floor(8.0, arbc::Affine::scaling(2.0, 0.0), /*has_native_pixels=*/true);
  CHECK_FALSE(half_collapsed.valid);

  // An overflowing radius/scale pair (a finite but astronomically large radius over a tiny scale)
  // would produce an infinite diameter: caught by the residual finite guard, never a false floor.
  const BrushDetailFloor overflow =
      brush_detail_floor(1.0e308, arbc::Affine::scaling(0.5, 0.5), /*has_native_pixels=*/true);
  CHECK_FALSE(overflow.valid);
  CHECK_FALSE(overflow.at_floor);

  // A non-positive brush radius: safe no-op.
  CHECK_FALSE(brush_detail_floor(0.0, arbc::Affine::identity(), true).valid);
  CHECK_FALSE(brush_detail_floor(-4.0, arbc::Affine::identity(), true).valid);

  // A non-finite radius: no NaN escapes.
  const double inf = std::numeric_limits<double>::infinity();
  const double nan = std::numeric_limits<double>::quiet_NaN();
  CHECK_FALSE(brush_detail_floor(inf, arbc::Affine::identity(), true).valid);
  CHECK_FALSE(brush_detail_floor(nan, arbc::Affine::identity(), true).valid);
}

TEST_CASE("paint_res: a brush_dab stroke never re-grids the cell (D8 dual invariance, §4)") {
  // The readout leaf carries its own regression pin of §4's storage rule: driving the shipped
  // `scene::brush_dab` verb changes pixels only — the target Cell's placement, content_bounds, and
  // native resolution are byte-identical before and after (painting writes into the FIXED grid,
  // never re-grids it, Constraint 7). Reuses brush's verb; asserted here so the paint_res leaf is
  // self-contained.
  const arbc::Registry registry = paint_res_registry();
  arbc::ObjectId cell;
  auto doc = build_raster_doc(registry, cell, arbc::Affine::scaling(2.0, 2.0));
  arbc::RasterContent* raster = resolve_raster(*doc, cell);
  REQUIRE(raster != nullptr);

  const ace::scene::Cell seed = find_cell(*doc, registry, cell);
  REQUIRE(seed.detail.native_pixels.has_value());
  const std::vector<float> before = raster_pixels(*raster);
  const std::size_t cursor_before = doc->journal().cursor();

  const std::vector<arbc::Vec2> centers{arbc::Vec2{10.0, 10.0}, arbc::Vec2{20.0, 18.0}};
  CHECK(ace::scene::brush_dab(*doc, registry, cell, centers, 3.0, 6.0, k_black, 1));
  CHECK(doc->journal().cursor() == cursor_before + 1); // ONE coalesced transaction

  const std::vector<float> after = raster_pixels(*raster);
  CHECK(after != before); // pixels moved

  const ace::scene::Cell after_cell = find_cell(*doc, registry, cell);
  CHECK(after_cell.placement == seed.placement);
  CHECK(after_cell.content_bounds == seed.content_bounds);
  CHECK(after_cell.detail.native_pixels ==
        seed.detail.native_pixels); // native resolution unchanged

  // And the readout the leaf ships reads that stable native grid: a valid N before and after,
  // identical (nothing about the placement or native px changed).
  const BrushDetailFloor r_before = brush_detail_floor(8.0, seed.placement, true);
  const BrushDetailFloor r_after = brush_detail_floor(8.0, after_cell.placement, true);
  REQUIRE(r_before.valid);
  REQUIRE(r_after.valid);
  CHECK(r_before.cell_px == r_after.cell_px);
}
