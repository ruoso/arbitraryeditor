// editor.cameras.new_shot_from_view — L1 headless Catch2 units for D23's SECOND mint verb,
// "promote the viewport's current framing into a saved shot" (docs/00-design.md:64-66 §2, D23,
// D-new_shot_from_view-1/-2/-7). `interact::new_shot_from_view` itself ships UNCHANGED here
// (D-new_shot_from_view-1) — this file is its first caller's contract, pinning the laws the L4
// join in `AppProjectGateway::new_shot_from_view` depends on and which nothing asserted before:
// the WYSIWYG round-trip through the shipped `viewport_camera_for_shot` (asserted against THIS
// verb's producer, with an anti-vacuity mismatch case), resolution == the pane in device pixels
// with `k_max_mint_resolution` deliberately NOT applied, square pixels + the frame's
// device -> composition orientation computed the long way, the two mint verbs agreeing at a 1:1
// viewport and only there, the degenerate-input sentinels, and the ONE `Camera <n>` sequence
// both verbs share. No ImGui/GL/SDL; runs under the ASan/TSan legs.
#include <ace/commands/app_state.hpp>
#include <ace/commands/cameras.hpp>
#include <ace/interact/interact.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/camera.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using ace::commands::AddCameraOutcome;
using ace::commands::AppState;
using ace::commands::Command;
using ace::commands::dispatch;
using ace::interact::k_max_mint_resolution;
using ace::interact::new_shot_from_view;
using ace::interact::shot_from_extent;
using ace::interact::ShotFraming;
using ace::interact::viewport_camera_for_shot;
using ace::scene::Resolution;

namespace {

bool near(double a, double b, double tol = 1e-9) { return std::abs(a - b) <= tol; }

bool affine_near(const arbc::Affine& a, const arbc::Affine& b, double tol = 1e-9) {
  return near(a.a, b.a, tol) && near(a.b, b.b, tol) && near(a.c, b.c, tol) && near(a.d, b.d, tol) &&
         near(a.tx, b.tx, tol) && near(a.ty, b.ty, tol);
}

// The composition region a `ShotFraming` covers: its output rectangle placed by the frame.
arbc::Rect covered(const ShotFraming& shot) {
  return shot.frame.map_rect(
      arbc::Rect::from_size(static_cast<double>(shot.width), static_cast<double>(shot.height)));
}

// The nav cameras `interact::pan`/`zoom`/`fit` actually produce: uniform scale + translation.
arbc::Affine nav_camera(double s, double tx, double ty) {
  return arbc::Affine{s, 0.0, 0.0, s, tx, ty};
}

struct ViewCase {
  double s;
  double tx;
  double ty;
  int pw;
  int ph;
};

// s < 1, s > 1, s == 1, and non-square panes in both orientations.
const std::vector<ViewCase> k_views = {
    {1.0, 0.0, 0.0, 64, 64},      {2.0, 30.0, 40.0, 1920, 1080},  {0.25, -12.5, 7.5, 800, 600},
    {1.75, 3.0, -9.0, 640, 1136}, {0.5, 100.0, -100.0, 1234, 97},
};

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_new_shot_from_view_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// A fresh workspace-backed session with a root composition (the `frame_selection_test` mould).
// `AppState`'s registry already carries `org.arbc.camera` via `register_editor_kinds`.
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

TEST_CASE("new shot from view: the promoted shot reproduces the view exactly") {
  // The WYSIWYG law (look_through.md:260-267), asserted against THIS verb's producer: rendering
  // the minted shot at its own resolution reproduces the viewport camera it was promoted from,
  // componentwise. This is what "promotes the viewport's current framing" means (§2, :64-66).
  for (const ViewCase& v : k_views) {
    const arbc::Affine camera = nav_camera(v.s, v.tx, v.ty);
    const ShotFraming shot = new_shot_from_view(camera, v.pw, v.ph);
    const arbc::Affine round =
        viewport_camera_for_shot(shot.frame, shot.width, shot.height, v.pw, v.ph);
    CHECK(affine_near(round, camera, 1e-9));
  }

  // Anti-vacuity: the law is resolution-SPECIFIC, so an implementation that returned the input
  // camera unconditionally could not pass. At twice the output size the round trip is the SAME
  // framing scaled by 2 (the export / pane-fit multiplier), not the viewport camera.
  const arbc::Affine camera = nav_camera(2.0, 30.0, 40.0);
  const ShotFraming shot = new_shot_from_view(camera, 1920, 1080);
  const arbc::Affine doubled =
      viewport_camera_for_shot(shot.frame, shot.width, shot.height, 2 * 1920, 2 * 1080);
  CHECK_FALSE(affine_near(doubled, camera, 1e-9));
  CHECK(near(doubled.a, 2.0 * camera.a));
  CHECK(near(doubled.d, 2.0 * camera.d));
}

TEST_CASE("new shot from view: the resolution is the pane in device pixels, unclamped") {
  // Amended D23's per-verb clause (D-new_shot_from_view-1): the pane is already a whole number
  // of pixels, so rounding is a no-op, and `k_max_mint_resolution` is deliberately NOT applied —
  // the clamp guards a composition-scale extent, which is unbounded above, whereas a pane is
  // bounded by the display. Keeping the pane's pixel count is what makes the promotion WYSIWYG.
  for (const ViewCase& v : k_views) {
    const ShotFraming shot = new_shot_from_view(nav_camera(v.s, v.tx, v.ty), v.pw, v.ph);
    CHECK(shot.width == v.pw);
    CHECK(shot.height == v.ph);
  }

  // A pane far past the clamp's threshold in BOTH dimensions still promotes verbatim — the
  // assertion that fails the moment someone "helpfully" adds `shot_from_extent`'s clamp here.
  const int huge_w = 4 * k_max_mint_resolution;
  const int huge_h = 3 * k_max_mint_resolution;
  const ShotFraming huge = new_shot_from_view(nav_camera(1.0, 0.0, 0.0), huge_w, huge_h);
  CHECK(huge.width == huge_w);
  CHECK(huge.height == huge_h);
  CHECK(huge.width > k_max_mint_resolution);
  CHECK(huge.height > k_max_mint_resolution);
  // The same region through the OTHER verb does clamp — the two rules differ by design.
  const ShotFraming clamped = shot_from_extent(
      arbc::Rect{0.0, 0.0, static_cast<double>(huge_w), static_cast<double>(huge_h)});
  CHECK(clamped.width == k_max_mint_resolution);
}

TEST_CASE("new shot from view: pixels are square and the frame is the inverted camera") {
  for (const ViewCase& v : k_views) {
    const ShotFraming shot = new_shot_from_view(nav_camera(v.s, v.tx, v.ty), v.pw, v.ph);
    // D9's aspect-lock holds identically: a uniform-scale nav camera inverts to a uniform-scale
    // frame, so no expand-about-center fix-up is needed (D-new_shot_from_view-1 (ii)).
    CHECK(shot.frame.a == shot.frame.d);
    CHECK(near(shot.frame.b, 0.0));
    CHECK(near(shot.frame.c, 0.0));

    // The frame places [0,pw]x[0,ph] onto the composition region the viewport shows, computed
    // the LONG way from the camera equation (device = s*comp + t => comp = (device - t)/s)
    // rather than by calling `inverse()` again.
    const arbc::Rect region = covered(shot);
    CHECK(near(region.x0, (0.0 - v.tx) / v.s, 1e-9));
    CHECK(near(region.y0, (0.0 - v.ty) / v.s, 1e-9));
    CHECK(near(region.x1, (static_cast<double>(v.pw) - v.tx) / v.s, 1e-9));
    CHECK(near(region.y1, (static_cast<double>(v.ph) - v.ty) / v.s, 1e-9));
  }
}

TEST_CASE("new shot from view: the two mint verbs agree at a 1:1 viewport, and only there") {
  // D23's "one derivation rule" made checkable, pinning D-frame_selection-2 (ii): at zoom 1 the
  // pane-derived rule IS the 1-composition-unit-per-pixel rule, so promoting the view and
  // framing the region the view shows produce the identical camera.
  const double tx = 37.0;
  const double ty = -21.0;
  const int pw = 320;
  const int ph = 200;

  const ShotFraming promoted = new_shot_from_view(nav_camera(1.0, tx, ty), pw, ph);
  const ShotFraming framed = shot_from_extent(
      arbc::Rect{-tx, -ty, static_cast<double>(pw) - tx, static_cast<double>(ph) - ty});
  CHECK(promoted.width == framed.width);
  CHECK(promoted.height == framed.height);
  CHECK(affine_near(promoted.frame, framed.frame, 1e-9));

  // At zoom 2 the resolutions diverge by exactly the zoom factor — the documented, INTENDED
  // difference (the pane shows half as many composition units at twice the magnification).
  const ShotFraming zoomed = new_shot_from_view(nav_camera(2.0, tx, ty), pw, ph);
  const arbc::Rect zoomed_region = covered(zoomed);
  const ShotFraming zoomed_framed = shot_from_extent(zoomed_region);
  CHECK(zoomed.width == pw);
  CHECK(zoomed_framed.width == pw / 2);
  CHECK(zoomed.height == ph);
  CHECK(zoomed_framed.height == ph / 2);
  // Same framed REGION either way, though — only the pixel count moved.
  const arbc::Rect framed_region = covered(zoomed_framed);
  CHECK(near(framed_region.x0, zoomed_region.x0, 1e-9));
  CHECK(near(framed_region.x1, zoomed_region.x1, 1e-9));
}

TEST_CASE("new shot from view: degenerate inputs refuse rather than guess") {
  // Constraint 4's sentinel, which is exactly what the L4 caller tests before dispatching.
  const arbc::Affine camera = nav_camera(1.5, 4.0, 5.0);
  const ShotFraming no_width = new_shot_from_view(camera, 0, 480);
  const ShotFraming no_height = new_shot_from_view(camera, 640, -1);
  CHECK(no_width.frame == arbc::Affine::identity());
  CHECK(no_height.frame == arbc::Affine::identity());
  // The caller's rejection test (`width <= 0 || height <= 0`) fires on both.
  CHECK((no_width.width <= 0 || no_width.height <= 0));
  CHECK((no_height.width <= 0 || no_height.height <= 0));

  // A non-invertible camera has no framing to promote either, so the frame stays identity.
  // Structurally unreachable from a live pane — `interact::pan`/`zoom`/`fit` only ever produce
  // a positive uniform scale — but the primitive refuses rather than emitting NaNs.
  const ShotFraming degenerate = new_shot_from_view(arbc::Affine::scaling(0.0, 0.0), 640, 480);
  CHECK(degenerate.frame == arbc::Affine::identity());
  CHECK(std::isfinite(degenerate.frame.a));
}

TEST_CASE("new shot from view: the Camera <n> sequence is shared with frame selection") {
  // D-new_shot_from_view-7: `next_camera_name` reads ALL existing cameras regardless of which
  // verb minted them, so "frame-select, then shot-from-view" reads Camera 1, Camera 2 rather
  // than two colliding namespaces. First-free-`n` (not a monotonic counter) is what makes
  // mint -> undo -> mint deterministic, and therefore what lets the e2e assert names at all.
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "naming");

  const ShotFraming promoted = new_shot_from_view(nav_camera(1.0, 0.0, 0.0), 200, 150);
  CHECK(ace::commands::next_camera_name(state.document()) == "Camera 1");

  AddCameraOutcome outcome;
  dispatch(state, ace::commands::add_camera_command(
                      state.registry(), ace::commands::next_camera_name(state.document()),
                      Resolution{promoted.width, promoted.height}, promoted.frame, outcome));
  REQUIRE(outcome.camera.valid());
  const std::vector<ace::scene::Camera> minted = ace::scene::cameras(state.document());
  REQUIRE(minted.size() == 1);
  CHECK(minted[0].name == "Camera 1");
  CHECK(minted[0].resolution.width == 200);
  CHECK(minted[0].resolution.height == 150);
  CHECK(ace::commands::next_camera_name(state.document()) == "Camera 2");

  // One observable undo takes the whole two-entry create back out, and the name frees again.
  CHECK(ace::commands::undo(state).moved);
  CHECK(ace::scene::cameras(state.document()).empty());
  CHECK(ace::commands::next_camera_name(state.document()) == "Camera 1");
}
