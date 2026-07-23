// editor.cameras.frame_selection — L1 headless Catch2 units for "mint a camera fit to the
// current selection" (docs/00-design.md:262-263 §6, D23, D-frame_selection-1..10). The whole
// derivation is pure, which is the point: `selected_extent` unions placed extents over
// hand-built primitive `PickTarget`s, `shot_from_extent` turns a composition-space region into
// a `(frame, resolution)` pair with no pane / zoom / `ViewFraming` anywhere, and
// `commands::cameras` wraps `scene::add_camera` in the one dispatchable verb. Exercised here:
// the union's order-independence and kind-agnosticism, the rotated-placement AABB (pinned
// BYTE-EQUAL against the exact `placed_quad` corners, so D-frame_selection-4's "no tightness is
// lost" is a test rather than prose), the unbounded/degenerate skips, the 1-unit-per-pixel
// rounding + clamp, square pixels, expand-never-crop, the frame-orientation round-trip through
// the shipped `viewport_camera_for_shot`, the two-journal-entry create with a one-undo
// observable, the auto-name's first-free rule, dirty bookkeeping, and the byte-INVARIANCE case
// (a camera renders zero pixels, A14). No ImGui/GL/SDL; runs under the ASan/TSan legs.
#include <ace/commands/app_state.hpp>
#include <ace/commands/cameras.hpp>
#include <ace/commands/selection.hpp>
#include <ace/interact/interact.hpp>
#include <ace/interact/pick.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/expected.hpp>
#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "golden_support.hpp"

using ace::commands::AddCameraOutcome;
using ace::commands::AppState;
using ace::commands::Command;
using ace::commands::dispatch;
using ace::commands::DispatchOutcome;
using ace::interact::k_max_mint_resolution;
using ace::interact::PickKind;
using ace::interact::PickTarget;
using ace::interact::placed_quad;
using ace::interact::selected_extent;
using ace::interact::shot_from_extent;
using ace::interact::ShotFraming;
using ace::scene::Resolution;

namespace {

arbc::ObjectId oid(std::uint64_t value) { return arbc::ObjectId{value}; }

PickTarget cell_target(std::uint64_t id, const arbc::Affine& placement,
                       std::optional<arbc::Rect> extent) {
  return PickTarget{oid(id), oid(id + 1000), PickKind::Cell, placement, extent};
}

PickTarget camera_target(std::uint64_t id, const arbc::Affine& frame, int width, int height) {
  return PickTarget{oid(id), oid(id + 1000), PickKind::Camera, frame,
                    arbc::Rect{0.0, 0.0, static_cast<double>(width), static_cast<double>(height)}};
}

// The AABB of the EXACT placed parallelogram corners, assembled the long way through the
// shipped `placed_quad`. `selected_extent` must equal this byte-for-byte (D-frame_selection-4).
std::optional<arbc::Rect> aabb_of_quads(const std::vector<PickTarget>& targets,
                                        const std::vector<arbc::ObjectId>& ids) {
  std::optional<arbc::Rect> box;
  for (const PickTarget& target : targets) {
    bool wanted = false;
    for (const arbc::ObjectId id : ids) {
      if (id == target.id) {
        wanted = true;
      }
    }
    if (!wanted) {
      continue;
    }
    const std::optional<std::array<arbc::Vec2, 4>> quad = placed_quad(target);
    if (!quad) {
      continue;
    }
    for (const arbc::Vec2 corner : *quad) {
      if (!box) {
        box = arbc::Rect{corner.x, corner.y, corner.x, corner.y};
        continue;
      }
      box->x0 = std::min(box->x0, corner.x);
      box->y0 = std::min(box->y0, corner.y);
      box->x1 = std::max(box->x1, corner.x);
      box->y1 = std::max(box->y1, corner.y);
    }
  }
  return box;
}

bool near(double a, double b, double tol = 1e-9) { return std::abs(a - b) <= tol; }

bool rect_near(const arbc::Rect& a, const arbc::Rect& b, double tol = 1e-9) {
  return near(a.x0, b.x0, tol) && near(a.y0, b.y0, tol) && near(a.x1, b.x1, tol) &&
         near(a.y1, b.y1, tol);
}

// The composition region a `ShotFraming` covers: its output rectangle placed by the frame.
arbc::Rect covered(const ShotFraming& shot) {
  return shot.frame.map_rect(
      arbc::Rect::from_size(static_cast<double>(shot.width), static_cast<double>(shot.height)));
}

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_frame_selection_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// A fresh workspace-backed session with a root composition (the `cells_remove_test` mould).
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

std::size_t depth(const AppState& state) { return state.document().journal().depth(); }
std::uint64_t revision(const AppState& state) { return state.document().pin()->revision(); }

// --- the golden fixture (reused verbatim from tests/cells_remove_test.cpp) ------------------
constexpr int k_child_edge = 32;

arbc::ObjectId add_child_composition(arbc::Document& doc) {
  const arbc::ObjectId child =
      doc.add_composition(static_cast<double>(k_child_edge), static_cast<double>(k_child_edge));
  const arbc::ObjectId content = doc.add_content(std::make_shared<arbc::SolidContent>(
      arbc::Rgba{0.6F, 0.0F, 0.0F, 1.0F},
      arbc::Rect{0.0, 0.0, static_cast<double>(k_child_edge), static_cast<double>(k_child_edge)}));
  doc.attach_layer(child, doc.add_layer(content, arbc::Affine::identity()));
  return child;
}

} // namespace

// --- selected_extent: the union over the selection (D-frame_selection-3/4) ------------------

TEST_CASE("frame selection: selected_extent unions exactly the selected members, in any order") {
  const std::vector<PickTarget> targets = {
      cell_target(1, arbc::Affine::translation(0.0, 0.0), arbc::Rect{0.0, 0.0, 10.0, 10.0}),
      cell_target(2, arbc::Affine::translation(40.0, 20.0), arbc::Rect{0.0, 0.0, 10.0, 10.0}),
      cell_target(3, arbc::Affine::translation(500.0, 500.0), arbc::Rect{0.0, 0.0, 10.0, 10.0})};

  const std::vector<arbc::ObjectId> two = {oid(1), oid(2)};
  const std::optional<arbc::Rect> united = selected_extent(targets, two);
  REQUIRE(united.has_value());
  // Just the two selected, NOT the far-away third: the union is over the selection, not the
  // document.
  CHECK(*united == arbc::Rect{0.0, 0.0, 50.0, 30.0});

  // Order-independent — the ids arrive in whatever order the selection happens to hold them.
  const std::vector<arbc::ObjectId> reversed = {oid(2), oid(1)};
  CHECK(selected_extent(targets, reversed) == united);
  // A repeated id is idempotent (a set-shaped input, `Selection` is duplicate-free anyway).
  const std::vector<arbc::ObjectId> dupes = {oid(2), oid(1), oid(2)};
  CHECK(selected_extent(targets, dupes) == united);
}

TEST_CASE("frame selection: a rotated placement contributes its AABB, byte-equal to placed_quad") {
  // A 45-degree rotation about the origin of a 10x10 square centred on it, translated to
  // (50,50): the placed shape is a diamond whose AABB has corners nowhere near it.
  const double k = std::sqrt(0.5);
  const arbc::Affine rotate45{k, k, -k, k, 0.0, 0.0};
  const arbc::Affine placement = arbc::compose(arbc::Affine::translation(50.0, 50.0), rotate45);
  const std::vector<PickTarget> targets = {
      cell_target(1, placement, arbc::Rect{-5.0, -5.0, 5.0, 5.0}),
      cell_target(2, arbc::Affine::translation(0.0, 0.0), arbc::Rect{0.0, 0.0, 4.0, 4.0})};
  const std::vector<arbc::ObjectId> both = {oid(1), oid(2)};

  const std::optional<arbc::Rect> united = selected_extent(targets, both);
  REQUIRE(united.has_value());
  // The rotated square's half-diagonal is 5*sqrt(2); its AABB is [50±7.07]^2, unioned with the
  // axis-aligned [0,4]^2.
  const double half_diag = 5.0 * std::sqrt(2.0);
  CHECK(near(united->x0, 0.0));
  CHECK(near(united->y0, 0.0));
  CHECK(near(united->x1, 50.0 + half_diag));
  CHECK(near(united->y1, 50.0 + half_diag));

  // The claim D-frame_selection-4 makes in prose, pinned as a test: bounding each target with
  // `map_rect` BEFORE unioning is byte-identical to bounding the unioned exact corners, because
  // the AABB of a union of point sets is the coordinate-wise min/max of the per-set AABBs.
  const std::optional<arbc::Rect> exact = aabb_of_quads(targets, both);
  REQUIRE(exact.has_value());
  CHECK(*united == *exact);
}

TEST_CASE("frame selection: a selected camera contributes its own output rectangle") {
  // D7 makes cells and cameras one shape under one select tool, and `selected_extent` reads
  // `kind` not at all — so framing a camera reproduces its crop (D-frame_selection-3).
  const arbc::Affine frame{2.0, 0.0, 0.0, 2.0, 12.0, 30.0};
  const std::vector<PickTarget> targets = {camera_target(7, frame, 320, 240)};
  const std::vector<arbc::ObjectId> just_camera = {oid(7)};

  const std::optional<arbc::Rect> united = selected_extent(targets, just_camera);
  REQUIRE(united.has_value());
  CHECK(*united == frame.map_rect(arbc::Rect{0.0, 0.0, 320.0, 240.0}));
  CHECK(*united == arbc::Rect{12.0, 30.0, 652.0, 510.0});
}

TEST_CASE("frame selection: unbounded members are skipped; all-unbounded returns nullopt") {
  // A factory-built `org.arbc.solid` has no bounds (D-cells_model-3): an unbounded fill has no
  // region to frame, so it contributes nothing — the same asymmetry `marquee` already ships.
  const std::vector<PickTarget> targets = {
      cell_target(1, arbc::Affine::identity(), std::nullopt),
      cell_target(2, arbc::Affine::translation(3.0, 4.0), arbc::Rect{0.0, 0.0, 6.0, 8.0}),
      cell_target(3, arbc::Affine::translation(9.0, 9.0), std::nullopt)};

  const std::vector<arbc::ObjectId> mixed = {oid(1), oid(2), oid(3)};
  const std::optional<arbc::Rect> united = selected_extent(targets, mixed);
  REQUIRE(united.has_value());
  CHECK(*united == arbc::Rect{3.0, 4.0, 9.0, 12.0}); // exactly target 2's placed extent

  const std::vector<arbc::ObjectId> only_unbounded = {oid(1), oid(3)};
  CHECK_FALSE(selected_extent(targets, only_unbounded).has_value());
}

TEST_CASE("frame selection: empty, unknown-id, and degenerate selections all yield nullopt") {
  const double inf = std::numeric_limits<double>::infinity();
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const std::vector<PickTarget> targets = {
      cell_target(1, arbc::Affine::identity(), arbc::Rect{0.0, 0.0, 0.0, 0.0}),    // zero-area
      cell_target(2, arbc::Affine::identity(), arbc::Rect{5.0, 5.0, 1.0, 1.0}),    // inverted
      cell_target(3, arbc::Affine::identity(), arbc::Rect{0.0, 0.0, inf, inf}),    // non-finite
      cell_target(4, arbc::Affine::identity(), arbc::Rect{0.0, 0.0, nan, 4.0}),    // NaN
      cell_target(5, arbc::Affine::scaling(0.0, 0.0), arbc::Rect{0, 0, 4.0, 4.0}), // collapsed
      // A FINITE extent under a FINITE, invertible (det == 1) placement whose mapped corner
      // still overflows to infinity — the only way past the input filters, and the reason the
      // MAPPED rect is re-checked rather than trusted (D-fit_bounds-3).
      cell_target(6, arbc::Affine{1e200, 0.0, 0.0, 1e-200, 0.0, 0.0},
                  arbc::Rect{0.0, 0.0, 1e200, 1e200})};

  // Nothing selected at all.
  CHECK_FALSE(selected_extent(targets, {}).has_value());
  // Ids naming no target.
  const std::vector<arbc::ObjectId> ghosts = {oid(99), oid(100)};
  CHECK_FALSE(selected_extent(targets, ghosts).has_value());
  // Every degenerate arm, individually — no NaN escapes as a "successful" union.
  for (std::uint64_t id = 1; id <= 6; ++id) {
    const std::vector<arbc::ObjectId> one = {oid(id)};
    CHECK_FALSE(selected_extent(targets, one).has_value());
  }
  // …and all of them together are still nothing to frame.
  const std::vector<arbc::ObjectId> all = {oid(1), oid(2), oid(3), oid(4), oid(5), oid(6)};
  CHECK_FALSE(selected_extent(targets, all).has_value());
  // An empty target list with a non-empty selection (everything went stale).
  CHECK_FALSE(selected_extent({}, all).has_value());
}

// --- shot_from_extent: the D23 derivation ---------------------------------------------------

TEST_CASE("frame selection: shot_from_extent derives 1 composition unit = 1 pixel") {
  // The honest default (D-frame_selection-2): a cell placed unscaled occupies as many
  // composition units as it has content pixels, so framing it exactly exports it at 1:1.
  const ShotFraming exact = shot_from_extent(arbc::Rect{10.0, 20.0, 650.0, 500.0});
  CHECK(exact.width == 640);
  CHECK(exact.height == 480);
  // No rounding was needed, so the frame covers the extent EXACTLY.
  CHECK(rect_near(covered(exact), arbc::Rect{10.0, 20.0, 650.0, 500.0}));

  // A fractional extent rounds to whole pixels — `Resolution` is `{int, int}`.
  const ShotFraming rounded = shot_from_extent(arbc::Rect{0.0, 0.0, 100.4, 50.7});
  CHECK(rounded.width == 100);
  CHECK(rounded.height == 51);
  // …and the frame's covered region carries exactly the ROUNDED aspect, 100:51.
  const arbc::Rect region = covered(rounded);
  CHECK(near(region.width() * 51.0, region.height() * 100.0, 1e-9));
}

TEST_CASE("frame selection: the minted frame has square pixels and never crops") {
  const arbc::Rect extent{0.0, 0.0, 100.4, 50.7};
  const ShotFraming shot = shot_from_extent(extent);
  REQUIRE(shot.width == 100);
  REQUIRE(shot.height == 51);

  // Square pixels (Constraint 3 / D9's aspect-lock): the two axis scales agree, i.e.
  // covered_w/W == covered_h/H. Axis-aligned, so there is no rotation/shear either.
  CHECK(near(shot.frame.a, shot.frame.d));
  CHECK(shot.frame.b == 0.0);
  CHECK(shot.frame.c == 0.0);

  // EXPAND, never crop: every corner of the input extent is inside the framed region, so
  // nothing the user selected falls outside the camera that was supposed to frame it.
  const arbc::Rect region = covered(shot);
  CHECK(region.x0 <= extent.x0 + 1e-9);
  CHECK(region.y0 <= extent.y0 + 1e-9);
  CHECK(region.x1 >= extent.x1 - 1e-9);
  CHECK(region.y1 >= extent.y1 - 1e-9);
  // The expansion is about the CENTRE (the short axis grows symmetrically) and is sub-pixel.
  CHECK(near((region.x0 + region.x1) * 0.5, (extent.x0 + extent.x1) * 0.5));
  CHECK(near((region.y0 + region.y1) * 0.5, (extent.y0 + extent.y1) * 0.5));
  CHECK(region.height() - extent.height() < 1.0);

  // The other branch: an extent TALLER than the rounded aspect grows in width instead.
  const arbc::Rect tall{0.0, 0.0, 50.7, 100.4};
  const ShotFraming portrait = shot_from_extent(tall);
  CHECK(portrait.width == 51);
  CHECK(portrait.height == 100);
  const arbc::Rect tall_region = covered(portrait);
  CHECK(near(portrait.frame.a, portrait.frame.d));
  CHECK(tall_region.x0 <= tall.x0 + 1e-9);
  CHECK(tall_region.x1 >= tall.x1 - 1e-9);
  CHECK(near(tall_region.height(), tall.height()));
  CHECK(tall_region.width() - tall.width() < 1.0);
}

TEST_CASE("frame selection: the minted frame's orientation round-trips through the shipped "
          "viewport_camera_for_shot") {
  // Constraint 2 — the assertion that catches an INVERTED frame, which would render the
  // inverse crop. `shot_from_extent` must use the same device -> composition convention
  // `new_shot_from_view` does.
  const arbc::Rect extent{-30.0, 12.0, 290.0, 172.0}; // 320 x 160, integral aspect
  const ShotFraming shot = shot_from_extent(extent);
  REQUIRE(shot.width == 320);
  REQUIRE(shot.height == 160);

  const arbc::Affine camera = ace::interact::viewport_camera_for_shot(
      shot.frame, shot.width, shot.height, shot.width, shot.height);
  // The framed region's corners map onto the output rectangle [0,W]x[0,H], corner for corner.
  const arbc::Vec2 top_left = camera.apply(arbc::Vec2{extent.x0, extent.y0});
  const arbc::Vec2 bottom_right = camera.apply(arbc::Vec2{extent.x1, extent.y1});
  CHECK(near(top_left.x, 0.0, 1e-6));
  CHECK(near(top_left.y, 0.0, 1e-6));
  CHECK(near(bottom_right.x, static_cast<double>(shot.width), 1e-6));
  CHECK(near(bottom_right.y, static_cast<double>(shot.height), 1e-6));

  // And the same at a scaled output (the export / pane-fit path reuses this verbatim).
  const arbc::Affine doubled = ace::interact::viewport_camera_for_shot(
      shot.frame, shot.width, shot.height, shot.width * 2, shot.height * 2);
  const arbc::Vec2 scaled_br = doubled.apply(arbc::Vec2{extent.x1, extent.y1});
  CHECK(near(scaled_br.x, static_cast<double>(shot.width) * 2.0, 1e-6));
  CHECK(near(scaled_br.y, static_cast<double>(shot.height) * 2.0, 1e-6));
}

TEST_CASE("frame selection: the resolution clamp preserves aspect and floors at 1x1") {
  // A composition-scale selection must not mint a terapixel camera whose export would
  // allocate terabytes (Constraint 11); the aspect survives the clamp.
  const ShotFraming huge = shot_from_extent(arbc::Rect{0.0, 0.0, 100000.0, 50000.0});
  CHECK(huge.width == k_max_mint_resolution);
  CHECK(huge.height == k_max_mint_resolution / 2);
  // Still covers the whole selection — clamping shrinks the PIXEL COUNT, not the framing.
  const arbc::Rect huge_region = covered(huge);
  CHECK(near(huge_region.width(), 100000.0, 1e-6));
  CHECK(near(huge_region.height(), 50000.0, 1e-6));
  CHECK(near(huge.frame.a, huge.frame.d, 1e-9));

  // The tall orientation clamps the OTHER side.
  const ShotFraming tall = shot_from_extent(arbc::Rect{0.0, 0.0, 50000.0, 100000.0});
  CHECK(tall.height == k_max_mint_resolution);
  CHECK(tall.width == k_max_mint_resolution / 2);

  // A sub-pixel selection floors at 1x1 rather than rounding to a {0, 0} camera.
  const ShotFraming tiny = shot_from_extent(arbc::Rect{0.0, 0.0, 0.2, 0.1});
  CHECK(tiny.width == 1);
  CHECK(tiny.height == 1);
  const arbc::Rect tiny_region = covered(tiny);
  CHECK(tiny_region.x0 <= 0.0);
  CHECK(tiny_region.x1 >= 0.2);
  CHECK(tiny_region.y0 <= 0.0);
  CHECK(tiny_region.y1 >= 0.1);
}

TEST_CASE("frame selection: a degenerate extent yields the {identity, 0, 0} sentinel") {
  const double inf = std::numeric_limits<double>::infinity();
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const arbc::Rect degenerate[] = {arbc::Rect{},                      // empty
                                   arbc::Rect{5.0, 5.0, 1.0, 1.0},    // inverted
                                   arbc::Rect{0.0, 0.0, 10.0, 0.0},   // zero height
                                   arbc::Rect{0.0, 0.0, inf, 10.0},   // non-finite width
                                   arbc::Rect{-inf, -inf, inf, inf},  // the whole plane
                                   arbc::Rect{nan, 0.0, 10.0, 10.0}}; // NaN

  for (const arbc::Rect& rect : degenerate) {
    const ShotFraming shot = shot_from_extent(rect);
    CHECK(shot.width == 0);
    CHECK(shot.height == 0);
    CHECK(shot.frame == arbc::Affine::identity()); // never a NaN (D-fit_bounds-3)
  }
}

// --- commands::cameras: the dispatchable mint (Constraint 9 / D15) --------------------------

TEST_CASE("frame selection: add_camera_command adds 2 journal entries; one undo removes it") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "mint");

  const Resolution resolution{640, 480};
  const arbc::Affine frame{1.0, 0.0, 0.0, 1.0, 12.0, 34.0};
  const std::size_t depth_before = depth(state);
  const std::uint64_t revision_before = revision(state);

  AddCameraOutcome outcome;
  const Command command =
      ace::commands::add_camera_command(state.registry(), "Camera 1", resolution, frame, outcome);
  const DispatchOutcome dispatched = dispatch(state, command);

  // TWO entries: `Document::add_content` self-commits and the binding layer is a second
  // transaction (the library's shape, already accepted for the insert side by D-cells_model-7).
  CHECK(dispatched.journal_entries_added == 2);
  CHECK(depth(state) == depth_before + 2);
  CHECK(revision(state) > revision_before);
  REQUIRE(outcome.camera.valid());
  CHECK(outcome.error.empty());

  const std::vector<ace::scene::Camera> minted = ace::scene::cameras(state.document());
  REQUIRE(minted.size() == 1);
  CHECK(minted[0].id == outcome.camera);
  CHECK(minted[0].name == "Camera 1");
  CHECK(minted[0].resolution == resolution);
  CHECK(minted[0].frame == frame);

  // The D15 observable contract: ONE undo removes the camera even though the create cost two
  // entries, because `cameras()` keys off composition membership.
  REQUIRE(ace::commands::undo(state).moved);
  CHECK(ace::scene::cameras(state.document()).empty());
  // …and one redo restores it on the SAME ObjectId.
  REQUIRE(ace::commands::redo(state).moved);
  const std::vector<ace::scene::Camera> restored = ace::scene::cameras(state.document());
  REQUIRE(restored.size() == 1);
  CHECK(restored[0].id == outcome.camera);
  CHECK(restored[0].resolution == resolution);
  CHECK(restored[0].frame == frame);
}

TEST_CASE("frame selection: a document with no root composition refuses the mint as a value") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "no_composition");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));

  AddCameraOutcome outcome;
  const Command command = ace::commands::add_camera_command(
      state.registry(), "Camera 1", Resolution{16, 16}, arbc::Affine::identity(), outcome);
  dispatch(state, command);

  // Errors are values (Constraint 6): the mint never throws and never half-mutates.
  CHECK_FALSE(outcome.camera.valid());
  CHECK_FALSE(outcome.error.empty());
  CHECK(ace::scene::cameras(state.document()).empty());
}

TEST_CASE("frame selection: next_camera_name picks the first free slot") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "naming");

  const auto mint = [&state](const char* name) {
    AddCameraOutcome outcome;
    dispatch(state, ace::commands::add_camera_command(state.registry(), name, Resolution{16, 16},
                                                      arbc::Affine::identity(), outcome));
    REQUIRE(outcome.camera.valid());
  };

  CHECK(ace::commands::next_camera_name(state.document()) == "Camera 1");
  mint("Camera 1");
  CHECK(ace::commands::next_camera_name(state.document()) == "Camera 2");
  mint("Camera 3");
  // FIRST-FREE, not a monotonic counter: minting, undoing and minting again reuses the name.
  CHECK(ace::commands::next_camera_name(state.document()) == "Camera 2");
  mint("Hero");
  CHECK(ace::commands::next_camera_name(state.document()) == "Camera 2");
  mint("Camera 2");
  CHECK(ace::commands::next_camera_name(state.document()) == "Camera 4");
}

TEST_CASE("frame selection: a hand-named camera leaves Camera 1 free") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "hand_named");

  AddCameraOutcome outcome;
  dispatch(state, ace::commands::add_camera_command(state.registry(), "Hero", Resolution{16, 16},
                                                    arbc::Affine::identity(), outcome));
  REQUIRE(outcome.camera.valid());
  CHECK(ace::commands::next_camera_name(state.document()) == "Camera 1");
}

TEST_CASE("frame selection: can_frame_selection is false when nothing is selected") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "gate");

  CHECK_FALSE(ace::commands::can_frame_selection(state));
  state.selection().select(oid(4242)); // even a STALE id enables it (D-frame_selection-7)
  CHECK(ace::commands::can_frame_selection(state));
  state.selection().clear();
  CHECK_FALSE(ace::commands::can_frame_selection(state));
}

TEST_CASE("frame selection: a mint re-dirties a saved session and undo never marks it clean") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "dirty");

  state.mark_saved(revision(state));
  REQUIRE_FALSE(state.is_dirty());

  AddCameraOutcome outcome;
  dispatch(state,
           ace::commands::add_camera_command(state.registry(), "Camera 1", Resolution{32, 32},
                                             arbc::Affine::identity(), outcome));
  REQUIRE(outcome.camera.valid());
  CHECK(state.is_dirty()); // a mint advances the revision past the saved baseline (A13)

  // Undo is a FORWARD publish, so it never returns the revision to a prior value (D-undo-4).
  REQUIRE(ace::commands::undo(state).moved);
  CHECK(state.is_dirty());
}

// --- The whole derivation over a REAL document (the headless L4 chain) ----------------------

TEST_CASE("frame selection: the full chain frames a real cell's placed extent exactly") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "chain");

  // A 64x48 raster placed unscaled at a known offset: its placed extent is [10,74]x[20,68].
  const arbc::Affine placement = arbc::Affine::translation(10.0, 20.0);
  const arbc::expected<arbc::ObjectId, std::string> added = ace::scene::add_cell(
      state.document(), state.registry(), "org.arbc.raster", "64x48", placement);
  REQUIRE(added.has_value());
  state.selection().select(*added);

  // The exact chain `AppProjectGateway::frame_selection` runs inside `run_edit`.
  const std::vector<PickTarget> targets =
      ace::interact::pick_targets(state.document(), state.registry());
  const std::optional<arbc::Rect> extent = selected_extent(targets, state.selection().items());
  REQUIRE(extent.has_value());
  CHECK(rect_near(*extent, arbc::Rect{10.0, 20.0, 74.0, 68.0}));

  const ShotFraming shot = shot_from_extent(*extent);
  // 1 unit = 1 pixel: framing an unscaled cell exactly reproduces its NATIVE pixel count (D8
  // read back through a camera) — the whole point of the derivation rule.
  CHECK(shot.width == 64);
  CHECK(shot.height == 48);

  AddCameraOutcome outcome;
  dispatch(state, ace::commands::add_camera_command(
                      state.registry(), ace::commands::next_camera_name(state.document()),
                      Resolution{shot.width, shot.height}, shot.frame, outcome));
  REQUIRE(outcome.camera.valid());

  const std::vector<ace::scene::Camera> minted = ace::scene::cameras(state.document());
  REQUIRE(minted.size() == 1);
  CHECK(minted[0].name == "Camera 1");
  CHECK((minted[0].resolution == Resolution{64, 48}));
  // The minted camera's output rectangle lands on exactly the cell's placed extent.
  CHECK(rect_near(minted[0].frame.map_rect(arbc::Rect{0.0, 0.0, 64.0, 48.0}),
                  arbc::Rect{10.0, 20.0, 74.0, 68.0}));

  // The mint touched neither the selection (D-frame_selection-10) nor the cell.
  CHECK(state.selection().size() == 1);
  CHECK(state.selection().contains(*added));
  CHECK(ace::scene::cells(state.document(), state.registry()).size() == 1);

  // Framing the MINTED camera reproduces its own crop — the second use D-frame_selection-3
  // exists for, and the reason the union carries no kind filter.
  state.selection().select(outcome.camera);
  const std::vector<PickTarget> after =
      ace::interact::pick_targets(state.document(), state.registry());
  const std::optional<arbc::Rect> camera_extent = selected_extent(after, state.selection().items());
  REQUIRE(camera_extent.has_value());
  CHECK(rect_near(*camera_extent, arbc::Rect{10.0, 20.0, 74.0, 68.0}));
}

// --- Rendered output: byte-INVARIANCE, stronger here than a new baseline (A14) --------------

TEST_CASE("frame selection: minting a camera leaves the rendered bytes byte-identical") {
  const ace::project::ProbeDocument probe = ace::project::build_probe_document();
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);

  const ace::render::Srgb8Image before = ace::render::render_document_srgb8(
      *probe.document, ace::project::k_probe_width, ace::project::k_probe_height);

  // Insert the nested cell that anchors the committed golden — so the middle state is a
  // KNOWN-GOOD image rather than one this leaf invents, and the invariance below cannot pass
  // vacuously.
  const arbc::ObjectId child = add_child_composition(*probe.document);
  const arbc::Rect child_extent{0.0, 0.0, static_cast<double>(k_child_edge),
                                static_cast<double>(k_child_edge)};
  const arbc::Affine placement =
      ace::interact::place_in_view(arbc::Affine::identity(), ace::project::k_probe_width,
                                   ace::project::k_probe_height, child_extent);
  const arbc::expected<arbc::ObjectId, std::string> added = ace::scene::add_cell(
      *probe.document, registry, "org.arbc.nested", std::to_string(child.value), placement);
  REQUIRE(added.has_value());

  const ace::render::Srgb8Image middle = ace::render::render_document_srgb8(
      *probe.document, ace::project::k_probe_width, ace::project::k_probe_height);
  CHECK(middle.pixels != before.pixels);
  CHECK(ace_test::compare_golden("cells_insert_nested_64x64.rgba8", middle.pixels));

  // Frame-select the nested cell through the full chain.
  const std::vector<PickTarget> targets = ace::interact::pick_targets(*probe.document, registry);
  const std::vector<arbc::ObjectId> selected = {*added};
  const std::optional<arbc::Rect> extent = selected_extent(targets, selected);
  REQUIRE(extent.has_value());
  const ShotFraming shot = shot_from_extent(*extent);
  REQUIRE(shot.width > 0);
  REQUIRE(ace::scene::add_camera(*probe.document, registry, "Camera 1",
                                 Resolution{shot.width, shot.height}, shot.frame)
              .valid());

  const ace::render::Srgb8Image after = ace::render::render_document_srgb8(
      *probe.document, ace::project::k_probe_width, ace::project::k_probe_height);
  // A camera is NON-RENDERING (A14, `camera.hpp:47-48`): `bounds()` is empty, so the
  // compositor culls its layer and the minted camera adds no pixel. Byte-invariance is the
  // meaningful assertion here, and it is stronger than a new baseline.
  CHECK(after.pixels == middle.pixels);
}
