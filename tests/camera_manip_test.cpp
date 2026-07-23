// editor.cameras.manip — L1 headless Catch2 units for the camera frame-manipulation math
// (D7/D8/D9): re-crop (holds resolution, aspect-locked), move, dutch rotation, and frame
// hit-test, all pure arbc::Affine beside interact's nav/shot helpers (Constraint 1, D-manip-3).
// Plus a render_offline golden pinning "resize re-crops, holds resolution" and the D8/D9
// resolution=resample distinction (byte-exact, no tolerance). No ImGui/GL/SDL.
#include <ace/interact/interact.hpp>
#include <ace/render/render.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/kind_raster/raster_content.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "golden_support.hpp"

using ace::interact::dutch_frame;
using ace::interact::FrameHandle;
using ace::interact::hit_frame;
using ace::interact::is_resize_handle;
using ace::interact::move_frame;
using ace::interact::recrop_frame;
using ace::interact::refit_frame_to_aspect;
using ace::interact::viewport_camera_for_shot;
using Catch::Approx;

namespace {

double vlen(arbc::Vec2 a, arbc::Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// The covered composition rectangle of `frame` over the native device rect [0,nw]x[0,nh].
struct Covered {
  arbc::Vec2 tl, tr, bl, br;
  double width() const { return vlen(tr, tl); }  // |U|, the horizontal extent
  double height() const { return vlen(bl, tl); } // |V|, the vertical extent
  double aspect() const { return width() / height(); }
};
Covered covered(const arbc::Affine& frame, int nw, int nh) {
  const double w = nw;
  const double h = nh;
  return {frame.apply({0.0, 0.0}), frame.apply({w, 0.0}), frame.apply({0.0, h}),
          frame.apply({w, h})};
}

// A camera-DEPENDENT 64x64 document (mirrors look_through_test's build_cells_doc): a
// full-frame green background under a BOUNDED 16x16 red raster square at (8,8), on integer
// composition bounds — so an integer-scale crop renders piecewise-constant pixels and a k*
// resolution render is the byte-exact k* upscale (the D8/D9 resample distinction).
std::unique_ptr<arbc::Document> build_cells_doc() {
  auto doc = std::make_unique<arbc::Document>();
  const arbc::ObjectId root = doc->add_composition(64.0, 64.0);
  const arbc::ObjectId bg =
      doc->add_content(std::make_shared<arbc::SolidContent>(arbc::Rgba{0.0F, 0.35F, 0.0F, 1.0F}));
  doc->attach_layer(root, doc->add_layer(bg, arbc::Affine::identity()));
  arbc::DecodedImage img;
  img.width = 16;
  img.height = 16;
  img.format = arbc::k_working_rgba32f;
  img.bytes.resize(static_cast<std::size_t>(16) * 16 * 4 * sizeof(float));
  auto* fp = reinterpret_cast<float*>(img.bytes.data());
  for (int i = 0; i < 16 * 16; ++i) { // opaque red, premultiplied linear
    fp[i * 4] = 0.6F;
    fp[i * 4 + 1] = 0.0F;
    fp[i * 4 + 2] = 0.0F;
    fp[i * 4 + 3] = 1.0F;
  }
  const arbc::ObjectId raster =
      doc->add_content(std::make_shared<arbc::RasterContent>(std::move(img)));
  doc->attach_layer(root, doc->add_layer(raster, arbc::Affine::translation(8.0, 8.0)));
  return doc;
}

// Nearest 2x upscale of a straight sRGB8 RGBA image (each source pixel -> a 2x2 block).
std::vector<std::uint8_t> upscale2(const std::vector<std::uint8_t>& src, int w, int h) {
  std::vector<std::uint8_t> dst(static_cast<std::size_t>(w) * 2 * h * 2 * 4);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const std::size_t s = (static_cast<std::size_t>(y) * w + x) * 4;
      for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
          const std::size_t d = (static_cast<std::size_t>(y * 2 + dy) * (w * 2) + (x * 2 + dx)) * 4;
          for (int ch = 0; ch < 4; ++ch) {
            dst[d + ch] = src[s + ch];
          }
        }
      }
    }
  }
  return dst;
}

} // namespace

TEST_CASE("camera_manip: re-crop holds the resolution aspect and never touches pixel count") {
  // A similarity frame (uniform scale 2, translated): its covered region is aspect nw:nh with
  // square pixels. A corner re-crop is a uniform scale about the opposite corner, so it stays
  // aspect-locked and only the covered SIZE changes — the pixel count (nw,nh) is not an input
  // the helper can mutate (D8 anti-resample; D9 holds resolution).
  const int nw = 32;
  const int nh = 18; // 16:9 aspect
  const arbc::Affine frame{2.0, 0.0, 0.0, 2.0, 10.0, 20.0};
  const Covered before = covered(frame, nw, nh);
  CHECK(before.aspect() == Approx(static_cast<double>(nw) / nh));

  // GROW: drag the bottom-right corner outward past its current position.
  const arbc::Affine grown =
      recrop_frame(frame, nw, nh, FrameHandle::CornerBottomRight, arbc::Vec2{200.0, 200.0});
  const Covered g = covered(grown, nw, nh);
  CHECK(g.aspect() == Approx(static_cast<double>(nw) / nh)); // aspect-locked (square pixels)
  CHECK(g.width() > before.width());                         // the covered region genuinely grew
  // A larger covered region at the SAME pixel count => lower PPI (D9). PPI == nw / covered width.
  CHECK(nw / g.width() < nw / before.width());

  // SHRINK: drag the same corner inward toward the pivot.
  const arbc::Affine tight =
      recrop_frame(frame, nw, nh, FrameHandle::CornerBottomRight, arbc::Vec2{30.0, 40.0});
  const Covered t = covered(tight, nw, nh);
  CHECK(t.aspect() == Approx(static_cast<double>(nw) / nh));
  CHECK(t.width() < before.width());           // tighter crop
  CHECK(nw / t.width() > nw / before.width()); // higher PPI

  // An edge re-crop stays aspect-locked too (grows/shrinks holding W:H).
  const arbc::Affine edged =
      recrop_frame(frame, nw, nh, FrameHandle::EdgeRight, arbc::Vec2{120.0, 60.0});
  CHECK(covered(edged, nw, nh).aspect() == Approx(static_cast<double>(nw) / nh));
}

TEST_CASE("camera_manip: every resize handle re-crops aspect-locked about its opposite pivot") {
  // The single-corner test above only exercises the CornerBottomRight/EdgeRight branches; drive
  // every handle so each pivot arm of recrop_frame's switch is covered. Each is a uniform
  // (aspect-locked, square-pixel) scale about the OPPOSITE corner / edge-midpoint, held fixed.
  const int nw = 32;
  const int nh = 18; // 16:9 aspect
  const arbc::Affine frame{2.0, 0.0, 0.0, 2.0, 10.0, 20.0};
  const double aspect = static_cast<double>(nw) / nh;
  const Covered b = covered(frame, nw, nh);
  const arbc::Vec2 left_mid{(b.tl.x + b.bl.x) * 0.5, (b.tl.y + b.bl.y) * 0.5};
  const arbc::Vec2 right_mid{(b.tr.x + b.br.x) * 0.5, (b.tr.y + b.br.y) * 0.5};
  const arbc::Vec2 top_mid{(b.tl.x + b.tr.x) * 0.5, (b.tl.y + b.tr.y) * 0.5};
  const arbc::Vec2 bot_mid{(b.bl.x + b.br.x) * 0.5, (b.bl.y + b.br.y) * 0.5};

  struct Case {
    FrameHandle handle;
    arbc::Vec2 dragged; // the reference point this handle moves
    arbc::Vec2 pivot;   // the opposite corner / edge-midpoint held fixed
  };
  const std::vector<Case> cases = {
      {FrameHandle::CornerTopLeft, b.tl, b.br},     {FrameHandle::CornerTopRight, b.tr, b.bl},
      {FrameHandle::CornerBottomLeft, b.bl, b.tr},  {FrameHandle::CornerBottomRight, b.br, b.tl},
      {FrameHandle::EdgeLeft, left_mid, right_mid}, {FrameHandle::EdgeRight, right_mid, left_mid},
      {FrameHandle::EdgeTop, top_mid, bot_mid},     {FrameHandle::EdgeBottom, bot_mid, top_mid},
  };
  for (const Case& c : cases) {
    // Push the pointer to twice the dragged->pivot offset beyond the dragged point: the projected
    // scale is exactly k=2 along the pivot->dragged axis, a genuine aspect-locked 2x grow about
    // `c.pivot` (which therefore stays a fixed point of the map).
    const arbc::Vec2 pointer{2.0 * c.dragged.x - c.pivot.x, 2.0 * c.dragged.y - c.pivot.y};
    const arbc::Affine grown = recrop_frame(frame, nw, nh, c.handle, pointer);
    const Covered g = covered(grown, nw, nh);
    CHECK(g.aspect() == Approx(aspect)); // aspect-locked (square pixels held)
    CHECK(g.width() > b.width());        // it genuinely grew about the pivot
    CHECK(grown.a == Approx(grown.d));   // isotropic uniform scale, no rotation introduced
    CHECK(grown.b == Approx(0.0));
    CHECK(grown.c == Approx(0.0));
    const double k = grown.a / frame.a;
    CHECK(k == Approx(2.0)); // the constructed drag is a clean 2x
    // The pivot is the fixed point: recrop = compose(scale_about(k, pivot), frame), so the
    // translation resolves to k*frame_t + (1-k)*pivot.
    CHECK(grown.tx == Approx(c.pivot.x * (1.0 - k) + frame.tx * k));
    CHECK(grown.ty == Approx(c.pivot.y * (1.0 - k) + frame.ty * k));
  }

  // Dragging the pointer PAST the pivot yields a non-positive projected scale, which is clamped
  // to a tiny positive minimum (k_min) so the covered region never collapses or flips.
  const arbc::Affine clamped =
      recrop_frame(frame, nw, nh, FrameHandle::CornerBottomRight, arbc::Vec2{-500.0, -500.0});
  const Covered cc = covered(clamped, nw, nh);
  CHECK(cc.aspect() == Approx(aspect)); // still aspect-locked
  CHECK(cc.width() > 0.0);              // positive (not collapsed / flipped)
  CHECK(cc.width() < b.width() * 0.01); // clamped to the k_min floor
}

TEST_CASE("camera_manip: move translates the covered region with no scale/rotation change") {
  const int nw = 40;
  const int nh = 30;
  const arbc::Affine frame{1.5, 0.0, 0.0, 1.5, 5.0, 7.0};
  const Covered before = covered(frame, nw, nh);
  const arbc::Affine moved = move_frame(frame, 12.0, -8.0);
  const Covered after = covered(moved, nw, nh);
  // Every covered corner shifts by exactly the drag delta.
  CHECK(after.tl.x == Approx(before.tl.x + 12.0));
  CHECK(after.tl.y == Approx(before.tl.y - 8.0));
  CHECK(after.br.x == Approx(before.br.x + 12.0));
  CHECK(after.br.y == Approx(before.br.y - 8.0));
  // The linear part is untouched (no scale/rotation) — placement only (D7).
  CHECK(moved.a == frame.a);
  CHECK(moved.b == frame.b);
  CHECK(moved.c == frame.c);
  CHECK(moved.d == frame.d);
}

TEST_CASE("camera_manip: dutch rotates about the frame center, preserving covered size") {
  const int nw = 32;
  const int nh = 32;
  const arbc::Affine frame{2.0, 0.0, 0.0, 2.0, 8.0, 8.0};
  const Covered before = covered(frame, nw, nh);
  const arbc::Vec2 center = frame.apply({nw * 0.5, nh * 0.5});

  const arbc::Affine turned = dutch_frame(frame, nw, nh, 0.5, /*snap_15=*/false);
  const Covered after = covered(turned, nw, nh);
  // A pure rotation: the covered center is fixed and the covered SIZE is unchanged (only the
  // orientation turns) — resolution and covered extent are untouched (rotation only, D9).
  const arbc::Vec2 new_center = turned.apply({nw * 0.5, nh * 0.5});
  CHECK(new_center.x == Approx(center.x));
  CHECK(new_center.y == Approx(center.y));
  CHECK(after.width() == Approx(before.width()));
  CHECK(after.height() == Approx(before.height()));
  CHECK(after.tl.x != Approx(before.tl.x)); // it genuinely rotated

  // Shift snaps to the nearest 15deg: 0.2 rad (~11.5deg) snaps to 15deg.
  const arbc::Affine snapped = dutch_frame(frame, nw, nh, 0.2, /*snap_15=*/true);
  const arbc::Affine exact15 = dutch_frame(frame, nw, nh, 3.14159265358979323846 / 12.0,
                                           /*snap_15=*/false);
  CHECK(snapped.a == Approx(exact15.a));
  CHECK(snapped.b == Approx(exact15.b));
  CHECK(snapped.c == Approx(exact15.c));
  CHECK(snapped.d == Approx(exact15.d));
}

TEST_CASE("camera_manip: hit_frame grabs the outline, click-through interior, corner over edge") {
  const int nw = 64;
  const int nh = 64;
  const arbc::Affine frame = arbc::Affine::identity(); // covered [0,64]^2
  const double edge_tol = 3.0;
  const double corner_tol = 6.0;

  // Corners.
  CHECK(hit_frame(frame, nw, nh, {0.0, 0.0}, edge_tol, corner_tol) == FrameHandle::CornerTopLeft);
  CHECK(hit_frame(frame, nw, nh, {64.0, 0.0}, edge_tol, corner_tol) == FrameHandle::CornerTopRight);
  CHECK(hit_frame(frame, nw, nh, {0.0, 64.0}, edge_tol, corner_tol) ==
        FrameHandle::CornerBottomLeft);
  CHECK(hit_frame(frame, nw, nh, {64.0, 64.0}, edge_tol, corner_tol) ==
        FrameHandle::CornerBottomRight);
  // Edge midpoints (resize handles).
  CHECK(hit_frame(frame, nw, nh, {32.0, 0.0}, edge_tol, corner_tol) == FrameHandle::EdgeTop);
  CHECK(hit_frame(frame, nw, nh, {32.0, 64.0}, edge_tol, corner_tol) == FrameHandle::EdgeBottom);
  CHECK(hit_frame(frame, nw, nh, {0.0, 32.0}, edge_tol, corner_tol) == FrameHandle::EdgeLeft);
  CHECK(hit_frame(frame, nw, nh, {64.0, 32.0}, edge_tol, corner_tol) == FrameHandle::EdgeRight);
  // A point on the border line away from a handle grabs Move (drag border, D7).
  CHECK(hit_frame(frame, nw, nh, {14.0, 0.0}, edge_tol, corner_tol) == FrameHandle::Move);
  // The label tab just outside the top edge (clear of the corner/mid handles and the border
  // band) is a move grab too.
  CHECK(hit_frame(frame, nw, nh, {12.0, -5.0}, edge_tol, corner_tol) == FrameHandle::Label);
  // Interior is click-through (None, D7).
  CHECK(hit_frame(frame, nw, nh, {32.0, 32.0}, edge_tol, corner_tol) == FrameHandle::None);
  // Near a corner resolves to the corner over the edge (near-corner precedence, D7).
  CHECK(hit_frame(frame, nw, nh, {2.0, 2.0}, edge_tol, corner_tol) == FrameHandle::CornerTopLeft);
  // A far-outside point misses.
  CHECK(hit_frame(frame, nw, nh, {200.0, 200.0}, edge_tol, corner_tol) == FrameHandle::None);
}

TEST_CASE("camera_manip: degenerate frame / native are safe no-ops (never a div-by-zero)") {
  const arbc::Affine frame = arbc::Affine::identity();
  const arbc::Affine singular{0.0, 0.0, 0.0, 0.0, 3.0, 4.0}; // non-invertible

  // Non-positive native: re-crop / dutch return the frame unchanged.
  CHECK(recrop_frame(frame, 0, 64, FrameHandle::CornerTopLeft, {1.0, 1.0}) == frame);
  CHECK(recrop_frame(frame, 64, -1, FrameHandle::CornerTopLeft, {1.0, 1.0}) == frame);
  CHECK(dutch_frame(frame, 0, 64, 0.5, false) == frame);
  // A non-invertible frame: every op is a no-op, and hit_frame is None.
  CHECK(recrop_frame(singular, 64, 64, FrameHandle::CornerTopLeft, {1.0, 1.0}) == singular);
  CHECK(dutch_frame(singular, 64, 64, 0.5, false) == singular);
  CHECK(hit_frame(singular, 64, 64, {1.0, 1.0}, 3.0, 6.0) == FrameHandle::None);
  CHECK(hit_frame(frame, 0, 0, {1.0, 1.0}, 3.0, 6.0) == FrameHandle::None);
  // A non-resize handle passed to re-crop is a no-op (only handles re-crop).
  CHECK(recrop_frame(frame, 64, 64, FrameHandle::Move, {10.0, 10.0}) == frame);
  CHECK(!is_resize_handle(FrameHandle::Move));
  CHECK(is_resize_handle(FrameHandle::CornerTopLeft));
  // refit rejects a non-positive width.
  CHECK(refit_frame_to_aspect(frame, 0, 64) == frame);
  CHECK(refit_frame_to_aspect(frame, 64, 0) == frame);
}

TEST_CASE(
    "camera_manip: refit_frame_to_aspect holds top-left + horizontal extent (frame follows)") {
  // The aspect-follow (D-manip-7): the resolution's pixel width changes 64 -> 128; the frame
  // re-fits to hold its top-left corner and its horizontal composition extent while the
  // vertical extent follows the new aspect at the new pixel dims.
  const int old_w = 64;
  const int old_h = 64;
  const int new_w = 128;
  const int new_h = 64; // new aspect 2:1
  const arbc::Affine frame{1.5, 0.0, 0.0, 1.5, 12.0, 9.0};
  const Covered before = covered(frame, old_w, old_h);

  const arbc::Affine follow = refit_frame_to_aspect(frame, old_w, new_w);
  const Covered after = covered(follow, new_w, new_h);
  // Top-left held.
  CHECK(after.tl.x == Approx(before.tl.x));
  CHECK(after.tl.y == Approx(before.tl.y));
  // Horizontal extent held (same composition width, now spanning new_w pixels).
  CHECK(after.width() == Approx(before.width()));
  // The covered region now has the NEW aspect, with square pixels preserved.
  CHECK(after.aspect() == Approx(static_cast<double>(new_w) / new_h));
}

TEST_CASE("camera_manip: re-crop holds resolution vs resolution=resample (render_offline golden)") {
  const std::unique_ptr<arbc::Document> doc = build_cells_doc();
  const int rw = 64;
  const int rh = 64;

  // A camera framing region A = [16,80]^2, re-cropped by dragging its bottom-right corner in
  // to region B = [16,48]^2 (a tighter crop) — HOLDING resolution R (the helper never sees a
  // resolution, only the native aspect basis 64:64).
  const arbc::Affine frame_a{1.0, 0.0, 0.0, 1.0, 16.0, 16.0};
  const arbc::Affine frame_b =
      recrop_frame(frame_a, rw, rh, FrameHandle::CornerBottomRight, arbc::Vec2{48.0, 48.0});
  // The re-crop is analytically the uniform 0.5 scale about the fixed top-left (16,16).
  CHECK(frame_b.a == Approx(0.5));
  CHECK(frame_b.d == Approx(0.5));
  CHECK(frame_b.tx == Approx(16.0));
  CHECK(frame_b.ty == Approx(16.0));

  // Render region B at R through the shared derivation, and pin it byte-exact to the golden.
  const ace::render::Srgb8Image crop = ace::render::render_document_srgb8(
      *doc, rw, rh, viewport_camera_for_shot(frame_b, rw, rh, rw, rh));
  REQUIRE(crop.pixels.size() == static_cast<std::size_t>(rw) * rh * 4);
  CHECK(ace_test::compare_golden("camera_manip_recrop_64x64.rgba8", crop.pixels));

  // The re-crop is a genuine crop, NOT the whole-document identity render.
  const ace::render::Srgb8Image whole = ace::render::render_document_srgb8(*doc, rw, rh);
  CHECK(crop.pixels != whole.pixels);

  // Resolution = RESAMPLE, not re-crop (D8/D9): rendering the SAME framed region at 2xR is the
  // 2x upscale of the R render — more pixels over the same crop. This is exact (no tolerance)
  // over a flat framed region F = [40,56]^2 (pure background, no sub-pixel content edge — the
  // renderer anti-aliases content boundaries, which no integer upscale can reproduce, so the
  // exact-upscale invariant is pinned on a flat crop; the k* camera-derivation equivalence over
  // detailed content is byte-pinned separately by look_through_test). Same crop, more pixels —
  // NEVER a re-crop: the framed region F is identical, only the pixel budget grew.
  const arbc::Affine flat{0.25, 0.0, 0.0, 0.25, 40.0, 40.0}; // covered [40,56]^2
  const ace::render::Srgb8Image flat_r = ace::render::render_document_srgb8(
      *doc, rw, rh, viewport_camera_for_shot(flat, rw, rh, rw, rh));
  const ace::render::Srgb8Image flat_2r = ace::render::render_document_srgb8(
      *doc, rw * 2, rh * 2, viewport_camera_for_shot(flat, rw, rh, rw * 2, rh * 2));
  const bool resample_is_upscale = flat_2r.pixels == upscale2(flat_r.pixels, rw, rh);
  CHECK(resample_is_upscale);
}
