// editor.cameras.look_through — L1 headless Catch2 units for the inverse
// render-through-camera derivation (D-look_through-4) plus a render_offline golden that
// pins "look-through framing == export framing" (D18's exact-export-framing promise).
// Pure arbc::Affine math beside interact::new_shot_from_view — no ImGui/GL/SDL
// (Constraint 1); the golden runs GL-free through render_document_srgb8 (render_offline).
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

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "golden_support.hpp"

using ace::interact::look_through;
using ace::interact::LookThrough;
using ace::interact::new_shot_from_view;
using ace::interact::viewport_camera_for_shot;
using Catch::Approx;

namespace {

// A camera-DEPENDENT 64x64 document (mirrors canvas_host_test's build_raster_doc): a
// full-frame green background under a BOUNDED 16x16 red raster square at (8,8), so a shot
// that frames a sub-region renders genuinely different pixels than the whole-document view.
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

} // namespace

TEST_CASE("look_through: viewport_camera_for_shot inverts new_shot_from_view at native res") {
  // Representative viewport cameras + panes: uniform, translated, identity, and anisotropic.
  // For any shot minted from a view, rendering that shot at ITS OWN resolution reproduces the
  // view camera — the round-trip both look-through and export depend on (D-look_through-4).
  struct Case {
    arbc::Affine cam;
    int pw;
    int ph;
  };
  const std::vector<Case> cases = {
      {arbc::Affine{2.0, 0.0, 0.0, 2.0, 30.0, 40.0}, 1920, 1080},
      {arbc::Affine{1.5, 0.0, 0.0, 1.5, 3.0, 7.0}, 800, 600},
      {arbc::Affine::identity(), 64, 64},
      {arbc::Affine{0.5, 0.0, 0.0, 3.0, -10.0, 25.0}, 320, 240}, // anisotropic
  };
  for (const Case& c : cases) {
    const ace::interact::ShotFraming shot = new_shot_from_view(c.cam, c.pw, c.ph);
    const arbc::Affine round =
        viewport_camera_for_shot(shot.frame, shot.width, shot.height, shot.width, shot.height);
    // A numeric-inverse tolerance (inverse∘inverse), NOT a rendering tolerance.
    CHECK(round.a == Approx(c.cam.a));
    CHECK(round.b == Approx(c.cam.b));
    CHECK(round.c == Approx(c.cam.c));
    CHECK(round.d == Approx(c.cam.d));
    CHECK(round.tx == Approx(c.cam.tx));
    CHECK(round.ty == Approx(c.cam.ty));
  }
}

TEST_CASE("look_through: the shot's framed region fills the output at any resolution") {
  const arbc::Affine frame{2.0, 0.0, 0.0, 2.0, 10.0, 20.0}; // device -> composition
  const int nw = 100;
  const int nh = 50;
  for (int k = 1; k <= 3; ++k) {
    const int ow = nw * k;
    const int oh = nh * k;
    const arbc::Affine cam = viewport_camera_for_shot(frame, nw, nh, ow, oh);
    // The shot's native output rect [0,nw]x[0,nh] maps (via `frame`) to a composition region;
    // the render camera must map that region's corners exactly onto [0,ow]x[0,oh] — the crop
    // is byte-exact at any output resolution (Constraint 4, the pane-fit preview + export N x).
    const arbc::Vec2 p0 = cam.apply(frame.apply(arbc::Vec2{0.0, 0.0}));
    const arbc::Vec2 p1 =
        cam.apply(frame.apply(arbc::Vec2{static_cast<double>(nw), static_cast<double>(nh)}));
    CHECK(p0.x == Approx(0.0));
    CHECK(p0.y == Approx(0.0));
    CHECK(p1.x == Approx(static_cast<double>(ow)));
    CHECK(p1.y == Approx(static_cast<double>(oh)));
  }
  // Explicitly: at out == k*native the camera is the native camera pre-scaled by k.
  const arbc::Affine native = viewport_camera_for_shot(frame, nw, nh, nw, nh);
  const arbc::Affine scaled = viewport_camera_for_shot(frame, nw, nh, 2 * nw, 2 * nh);
  CHECK(scaled.a == Approx(native.a * 2.0));
  CHECK(scaled.d == Approx(native.d * 2.0));
  CHECK(scaled.tx == Approx(native.tx * 2.0));
  CHECK(scaled.ty == Approx(native.ty * 2.0));
}

TEST_CASE("look_through: the pane-fit wrapper letterboxes preserving the shot aspect") {
  const arbc::Affine frame = arbc::Affine::identity();

  // Pane WIDER than the shot's aspect -> height-bound: out_h == pane_h, out_w inset.
  {
    const LookThrough lt = look_through(frame, 1920, 1080, 1000, 500);
    CHECK(lt.out_h == 500);
    CHECK(lt.out_w < 1000);
    CHECK(lt.out_w == static_cast<int>(1920.0 * (500.0 / 1080.0)));
    CHECK(lt.camera == viewport_camera_for_shot(frame, 1920, 1080, lt.out_w, lt.out_h));
  }
  // Pane TALLER than the shot's aspect -> width-bound: out_w == pane_w, out_h inset.
  {
    const LookThrough lt = look_through(frame, 1920, 1080, 640, 2000);
    CHECK(lt.out_w == 640);
    CHECK(lt.out_h < 2000);
    CHECK(lt.out_h == static_cast<int>(1080.0 * (640.0 / 1920.0)));
    CHECK(lt.camera == viewport_camera_for_shot(frame, 1920, 1080, lt.out_w, lt.out_h));
  }
  // EQUAL aspect -> fills the pane exactly.
  {
    const LookThrough lt = look_through(frame, 100, 100, 64, 64);
    CHECK(lt.out_w == 64);
    CHECK(lt.out_h == 64);
    CHECK(lt.camera == viewport_camera_for_shot(frame, 100, 100, 64, 64));
  }
}

TEST_CASE("look_through: degenerate resolutions and non-invertible frames are safe no-ops") {
  const arbc::Affine id = arbc::Affine::identity();
  // viewport_camera_for_shot: any non-positive native/out dimension yields identity.
  CHECK(viewport_camera_for_shot(id, 0, 100, 64, 64) == id);
  CHECK(viewport_camera_for_shot(id, 100, 0, 64, 64) == id);
  CHECK(viewport_camera_for_shot(id, 100, 100, 0, 64) == id);
  CHECK(viewport_camera_for_shot(id, 100, 100, 64, -3) == id);
  // A collapsed (non-invertible) frame has no render camera -> identity, never a div-by-zero.
  CHECK(viewport_camera_for_shot(arbc::Affine::scaling(0.0, 0.0), 100, 100, 64, 64) == id);

  // The pane-fit wrapper: a degenerate shot/pane is a zero-size, identity-camera no-op (the
  // caller falls back to the free viewport).
  const LookThrough a = look_through(id, 0, 100, 64, 64);
  CHECK(a.out_w == 0);
  CHECK(a.out_h == 0);
  CHECK(a.camera == id);
  const LookThrough b = look_through(id, 100, 100, 64, 0);
  CHECK(b.out_w == 0);
  CHECK(b.out_h == 0);
  CHECK(b.camera == id);
}

TEST_CASE(
    "look_through: a shot's pane-fit crop is byte-exact vs the golden and the export affine") {
  const std::unique_ptr<arbc::Document> doc = build_cells_doc();

  // A shot S framing a 32x32 device region placed at composition (16,16): its frame is the
  // device -> composition placement (the binding layer's Affine, scene::Camera.frame).
  const arbc::Affine s_frame = arbc::Affine::translation(16.0, 16.0);
  const int s_res_w = 32;
  const int s_res_h = 32;

  // Look through S in a 64x64 pane: aspect-fit picks a 64x64 crop (scale 2), and the derived
  // camera renders EXACTLY S's framed region — the live preview's pane-fit resolution.
  const LookThrough lt = look_through(s_frame, s_res_w, s_res_h, 64, 64);
  REQUIRE(lt.out_w == 64);
  REQUIRE(lt.out_h == 64);

  const ace::render::Srgb8Image crop =
      ace::render::render_document_srgb8(*doc, lt.out_w, lt.out_h, lt.camera);
  REQUIRE(crop.pixels.size() == static_cast<std::size_t>(64) * 64 * 4);
  CHECK(ace::render::frame_has_content(crop)); // the crop actually covered content

  // 1. Byte-exact against the committed golden (no tolerance — D18's honest exact framing).
  CHECK(ace_test::compare_golden("look_through_shot_64x64.rgba8", crop.pixels));

  // 2. look-through framing == export framing: the derived camera IS the exact affine
  //    editor.cameras.export would use for the same S at the same (out_w,out_h)
  //    (viewport_camera_for_shot is the single shared derivation, D-look_through-4).
  CHECK(lt.camera == viewport_camera_for_shot(s_frame, s_res_w, s_res_h, lt.out_w, lt.out_h));

  // 3. The framing is genuinely applied: the shot's crop is NOT the whole-document identity
  //    render (it clips to S's region), so the derivation is not a silent no-op.
  const ace::render::Srgb8Image whole = ace::render::render_document_srgb8(*doc, 64, 64);
  CHECK(crop.pixels != whole.pixels);
}
