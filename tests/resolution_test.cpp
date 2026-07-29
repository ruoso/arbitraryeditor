#include <ace/interact/interact.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/transform.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// L1 resolution-health math (editor.cells.resolution; D8/§2), unit-tested headless — the
// base of the test pyramid (docs §9). The helper is pure over primitive geometry: no
// Document, no scene type, byte-deterministic (D-resolution-3).

using ace::interact::resolution_health;
using ace::interact::ResolutionVerdict;

namespace {
// A camera whose output maps device pixels 1:1 into composition (frame identity): one
// device pixel spans one composition unit, so it samples at 1 px per composition unit.
const arbc::Affine k_identity_frame = arbc::Affine::identity();
} // namespace

TEST_CASE("resolution: a camera sampling at exactly native density is Crisp (r == 1)") {
  // A 100x100 native raster placed 1:1 into composition (each native pixel = 1 comp unit),
  // captured by a 100x100 camera framed 1:1: the camera lays exactly one output pixel over
  // each native pixel. r == 1, Crisp (docs/00-design.md:83-86).
  const ace::interact::ResolutionHealth health =
      resolution_health(100, 100, arbc::Affine::identity(), 100, 100, k_identity_frame);
  CHECK(health.verdict == ResolutionVerdict::Crisp);
  CHECK(health.ratio == 1.0);
}

TEST_CASE("resolution: scaling a cell up above native is Soft (r == 2)") {
  // Handle-drag scales the PLACEMENT 2x (D8: placement changes, resolution does not): the
  // 100 native pixels now span 200 composition units, so native density halves to 0.5 px/unit
  // while the 1:1 camera still samples 1 px/unit. r == 2 — "scaling a cell up is soft."
  const ace::interact::ResolutionHealth health =
      resolution_health(100, 100, arbc::Affine::scaling(2.0, 2.0), 100, 100, k_identity_frame);
  CHECK(health.verdict == ResolutionVerdict::Soft);
  CHECK(health.ratio == 2.0);
}

TEST_CASE("resolution: a camera out-resolving a fixed cell is Soft (r == 2)") {
  // The mirror path (the health badge's headline case, .tji:537): the cell is placed 1:1 but
  // the camera frame samples at 2 px/comp-unit (frame scaled 0.5, so one device pixel spans
  // half a composition unit). The camera out-resolves the cell -> Soft, r == 2.
  const ace::interact::ResolutionHealth health = resolution_health(
      100, 100, arbc::Affine::identity(), 100, 100, arbc::Affine::scaling(0.5, 0.5));
  CHECK(health.verdict == ResolutionVerdict::Soft);
  CHECK(health.ratio == 2.0);
}

TEST_CASE("resolution: the design's 1.4x example lands Soft") {
  // "hero camera samples the photo at 1.4x - slightly soft" (docs/00-design.md:85): the
  // placement is scaled 1.4x, so the camera samples 1.4x above native. A 1.4x scale is not an
  // integer ratio, so a small tolerance is documented here (Constraint / criterion).
  const ace::interact::ResolutionHealth health =
      resolution_health(100, 100, arbc::Affine::scaling(1.4, 1.4), 100, 100, k_identity_frame);
  CHECK(health.verdict == ResolutionVerdict::Soft);
  CHECK(health.ratio == Catch::Approx(1.4));
}

TEST_CASE("resolution: a camera sampling below native (downsampling) is Crisp") {
  // The camera covers a large region coarsely (frame scaled 2x: one device pixel spans two
  // composition units), so it samples 0.5 px/unit against the cell's 1 px/unit native density.
  // r == 0.5 <= 1: Crisp — a downsample is never soft.
  const ace::interact::ResolutionHealth health = resolution_health(
      100, 100, arbc::Affine::identity(), 100, 100, arbc::Affine::scaling(2.0, 2.0));
  CHECK(health.verdict == ResolutionVerdict::Crisp);
  CHECK(health.ratio == 0.5);
}

TEST_CASE("resolution: placement scale is the sampling density (D8 independence)") {
  // Changing the cell's PLACEMENT scale changes r while the native px input is untouched:
  // placement is the sampling density (D8 "the two are always independent"). Same native
  // (100x100) and camera, three placements -> three verdicts.
  CHECK(resolution_health(100, 100, arbc::Affine::identity(), 100, 100, k_identity_frame).ratio ==
        1.0);
  CHECK(resolution_health(100, 100, arbc::Affine::scaling(2.0, 2.0), 100, 100, k_identity_frame)
            .ratio == 2.0);
  CHECK(resolution_health(100, 100, arbc::Affine::scaling(0.5, 0.5), 100, 100, k_identity_frame)
            .ratio == 0.5);
}

TEST_CASE("resolution: at a fixed placed size more native detail is crisper (D8, not conflated)") {
  // The native grid is not conflated with placement: two cells filling the SAME 200x200
  // composition region, one with 100x100 native pixels and one with 200x200, judged by the
  // same 1:1 200x200 camera. The higher-resolution cell holds detail the camera does not
  // out-resolve (Crisp); the lower-resolution one is sampled 2x above native (Soft). This is
  // the health quantity resample-to-crisp would target (D8) — more native px, same placement
  // extent, crisper. The placement necessarily co-varies because it encodes composition units
  // per native pixel (D-resolution-1); the native px input is what distinguishes the two.
  const ace::interact::ResolutionHealth low =
      resolution_health(100, 100, arbc::Affine::scaling(2.0, 2.0), 200, 200, k_identity_frame);
  const ace::interact::ResolutionHealth high =
      resolution_health(200, 200, arbc::Affine::identity(), 200, 200, k_identity_frame);
  CHECK(low.verdict == ResolutionVerdict::Soft);
  CHECK(low.ratio == 2.0);
  CHECK(high.verdict == ResolutionVerdict::Crisp);
  CHECK(high.ratio == 1.0);
}

TEST_CASE("resolution: N/A inputs yield no verdict, never a div-by-zero or false Soft") {
  // A resolution-independent cell reaches here with native px == 0 (its native grid is absent,
  // Constraint 4): NotApplicable, not Soft.
  CHECK(resolution_health(0, 0, arbc::Affine::identity(), 100, 100, k_identity_frame).verdict ==
        ResolutionVerdict::NotApplicable);
  // A degenerate (non-invertible, zero-area) placement maps the native extent to a sliver:
  // NotApplicable, no div-by-zero.
  const ace::interact::ResolutionHealth collapsed =
      resolution_health(100, 100, arbc::Affine::scaling(0.0, 0.0), 100, 100, k_identity_frame);
  CHECK(collapsed.verdict == ResolutionVerdict::NotApplicable);
  CHECK(collapsed.ratio == 0.0);
  // A non-positive camera resolution is equally N/A.
  CHECK(resolution_health(100, 100, arbc::Affine::identity(), 0, 100, k_identity_frame).verdict ==
        ResolutionVerdict::NotApplicable);
  // A degenerate camera frame too.
  CHECK(resolution_health(100, 100, arbc::Affine::identity(), 100, 100,
                          arbc::Affine::scaling(0.0, 0.0))
            .verdict == ResolutionVerdict::NotApplicable);
}

TEST_CASE("resolution: placed size is placement-mapped content_bounds, distinct from native px") {
  // The readout shows the placed size (composition units) as `placement`-mapped content_bounds
  // (a pure Affine::map_rect) SEPARATELY from the native pixel grid (D7/D8 independence): the
  // two are never derived from each other. A 100x100 native raster placed at scale 3 has a
  // 300x300 placed size but its native resolution is still 100x100.
  const arbc::Rect content_bounds = arbc::Rect::from_size(100.0, 100.0);
  const arbc::Affine placement = arbc::Affine::scaling(3.0, 3.0);
  const arbc::Rect placed = placement.map_rect(content_bounds);
  CHECK(placed.width() == 300.0);
  CHECK(placed.height() == 300.0);
  // The native px the inspector shows is unchanged by the placement — it is the content_bounds
  // dimensions, not the placed size.
  CHECK(content_bounds.width() == 100.0);
  CHECK(content_bounds.height() == 100.0);
}
