// editor.cells.group_transform — L1 headless Catch2 units for applying ONE affine delta across a
// multi-object selection about a shared pivot (D7/D8/D15/D-group_transform-*). The load-bearing
// properties: the pure `interact::group_transform` helper composes the union-box gesture delta
// onto every member's start placement (order-independent, pivot = `selected_extent` center); a
// degenerate/identity gesture leaves every member untouched; the batch `commands::
// transform_cells_command` lands as EXACTLY ONE journal entry / one revision bump / one undo press
// and swaps in atomically; and the whole thing is PLACEMENT-only — no member's native resolution
// or `content_bounds` moves (D8), pinned by a group-scale golden.
//
// No ImGui/GL/SDL (Constraint 8); runs under the ASan/TSan legs (A4/§9).

#include <ace/commands/app_state.hpp>
#include <ace/commands/cells.hpp>
#include <ace/commands/selection.hpp>
#include <ace/interact/interact.hpp>
#include <ace/interact/pick.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
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
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "golden_support.hpp"

using ace::commands::AppState;
using ace::commands::Command;
using ace::commands::dispatch;
using ace::commands::DispatchOutcome;
using ace::commands::LayerTransform;
using ace::interact::cell_pivot;
using ace::interact::CellHandle;
using ace::interact::group_transform;
using ace::interact::move_cell;
using ace::interact::rotate_cell;
using ace::interact::scale_cell;
using ace::interact::shear_cell;
using ace::scene::Cell;
using Catch::Approx;

namespace {

constexpr double k_pi = 3.14159265358979323846;

// Byte-equality on the 6 affine components — the "matches the per-member expected affine" the
// pure helper promises, with no floating slop hidden behind it (the helper only `compose`s).
bool same_affine(const arbc::Affine& x, const arbc::Affine& y) { return x == y; }

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_group_transform_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// A fresh workspace-backed session with a 64x64 root composition (the mould of
// tests/cells_remove_test.cpp), so `dispatch` / undo run against a real journal.
AppState session_with_composition(const ScratchDir& scratch, const ace::platform::FileSystem& fs,
                                  const char* leaf) {
  auto created = ace::project::create_project(fs, scratch.root / leaf);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  dispatch(state, Command{"add_composition",
                          [](arbc::Document& doc) { doc.add_composition(64.0, 64.0); }});
  return state;
}

arbc::ObjectId insert(AppState& state, const char* kind_id, const char* config,
                      const arbc::Affine& placement) {
  const arbc::expected<arbc::ObjectId, std::string> added =
      ace::scene::add_cell(state.document(), state.registry(), kind_id, config, placement);
  REQUIRE(added.has_value());
  return *added;
}

std::vector<Cell> cells_of(const AppState& state) {
  return ace::scene::cells(state.document(), state.registry());
}

const Cell* cell_by_id(const std::vector<Cell>& cells, arbc::ObjectId id) {
  for (const Cell& cell : cells) {
    if (cell.id == id) {
      return &cell;
    }
  }
  return nullptr;
}

std::size_t depth(const AppState& state) { return state.document().journal().depth(); }
std::uint64_t revision(const AppState& state) { return state.document().pin()->revision(); }

arbc::Registry group_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  return registry;
}

// A 64x64 document with TWO 16x16 red rasters (at (8,8) and (32,32)) over a green full-frame
// background — painted content (a fresh raster is transparent, which would render an identical
// green frame and pin nothing), so a group scale shows the pair growing softer without a re-grid.
// Returns the two raster layer ids by out-param.
std::unique_ptr<arbc::Document> build_group_doc(arbc::ObjectId& layer_a, arbc::ObjectId& layer_b) {
  auto doc = std::make_unique<arbc::Document>();
  const arbc::ObjectId root = doc->add_composition(64.0, 64.0);
  const arbc::ObjectId bg =
      doc->add_content(std::make_shared<arbc::SolidContent>(arbc::Rgba{0.0F, 0.35F, 0.0F, 1.0F}));
  doc->attach_layer(root, doc->add_layer(bg, arbc::Affine::identity()));
  auto red_raster = [&]() {
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
    return doc->add_content(std::make_shared<arbc::RasterContent>(std::move(img)));
  };
  layer_a = doc->add_layer(red_raster(), arbc::Affine::translation(8.0, 8.0));
  doc->attach_layer(root, layer_a);
  layer_b = doc->add_layer(red_raster(), arbc::Affine::translation(32.0, 32.0));
  doc->attach_layer(root, layer_b);
  return doc;
}

} // namespace

TEST_CASE("group transform: one delta composes onto every member about the union pivot") {
  // Three hand-built start placements — a plain translate, a rotate+scale, a translate+scale — so a
  // shared delta has visibly distinct outputs per member.
  const std::vector<arbc::Affine> starts = {
      arbc::Affine::translation(10.0, 20.0),
      arbc::Affine{1.5, 0.9, -0.6, 1.2, 40.0, 40.0},
      arbc::Affine{2.0, 0.0, 0.0, 2.0, 5.0, 60.0},
  };
  const arbc::Rect uni{8.0, 8.0, 48.0, 48.0};
  const arbc::Vec2 pivot = cell_pivot(arbc::Affine::identity(), uni);
  CHECK(pivot.x == Approx(28.0)); // the union CENTER is the shared pivot (Constraint 3)
  CHECK(pivot.y == Approx(28.0));

  auto expect_matches = [&](const arbc::Affine& new_union) {
    const std::vector<arbc::Affine> out =
        group_transform(arbc::Affine::identity(), new_union, starts);
    REQUIRE(out.size() == starts.size());
    for (std::size_t i = 0; i < starts.size(); ++i) {
      // old_union is identity, so D = new_union and each member is compose(new_union, start).
      CHECK(same_affine(out[i], arbc::compose(new_union, starts[i])));
    }
    return out;
  };

  SECTION("move / scale / rotate / shear each yield a delta that lands on every member") {
    // Move: drag the union body by (12, -7).
    expect_matches(move_cell(arbc::Affine::identity(), 12.0, -7.0));
    // Scale a corner about the pivot to exactly 2x (pointer = pivot + 2*(corner - pivot)).
    expect_matches(scale_cell(arbc::Affine::identity(), uni, CellHandle::ScaleBottomRight,
                              arbc::Vec2{68.0, 68.0}, pivot, /*free_distort=*/false,
                              /*about_pivot=*/true));
    // Rotate 30 degrees about the pivot.
    expect_matches(rotate_cell(arbc::Affine::identity(), pivot, k_pi / 6.0, /*snap_15=*/false));
    // Shear the top edge about the pivot.
    expect_matches(shear_cell(arbc::Affine::identity(), uni, CellHandle::ScaleTop,
                              arbc::Vec2{20.0, 8.0}, pivot));
  }

  SECTION("the result is order-independent — each member depends only on the shared delta") {
    const arbc::Affine new_union =
        scale_cell(arbc::Affine::identity(), uni, CellHandle::ScaleBottomRight,
                   arbc::Vec2{68.0, 68.0}, pivot, /*free_distort=*/false, /*about_pivot=*/true);
    const std::vector<arbc::Affine> forward =
        group_transform(arbc::Affine::identity(), new_union, starts);
    std::vector<arbc::Affine> reversed(starts.rbegin(), starts.rend());
    const std::vector<arbc::Affine> back =
        group_transform(arbc::Affine::identity(), new_union, reversed);
    REQUIRE(forward.size() == 3);
    REQUIRE(back.size() == 3);
    CHECK(same_affine(back[0], forward[2]));
    CHECK(same_affine(back[1], forward[1]));
    CHECK(same_affine(back[2], forward[0]));
  }
}

TEST_CASE("group transform: identity delta and a degenerate union leave every member unchanged") {
  const std::vector<arbc::Affine> starts = {
      arbc::Affine::translation(3.0, 4.0),
      arbc::Affine{1.0, 0.2, 0.0, 1.3, 9.0, 1.0},
  };

  SECTION("an identity gesture (new_union == old_union) is a no-op per member") {
    const std::vector<arbc::Affine> out =
        group_transform(arbc::Affine::identity(), arbc::Affine::identity(), starts);
    REQUIRE(out.size() == 2);
    CHECK(same_affine(out[0], starts[0]));
    CHECK(same_affine(out[1], starts[1]));
  }

  SECTION("a non-invertible old_union carries no derivable delta — members untouched") {
    const arbc::Affine singular{0.0, 0.0, 0.0, 0.0, 5.0, 5.0}; // det 0, no inverse
    const std::vector<arbc::Affine> out =
        group_transform(singular, arbc::Affine::translation(9.0, 9.0), starts);
    REQUIRE(out.size() == 2);
    CHECK(same_affine(out[0], starts[0]));
    CHECK(same_affine(out[1], starts[1]));
  }

  SECTION("an empty selection yields an empty batch") {
    CHECK(
        group_transform(arbc::Affine::identity(), arbc::Affine::translation(1.0, 1.0), {}).empty());
  }
}

TEST_CASE("group transform: the batch is one journal entry and undo restores every member") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "one_entry");

  const arbc::ObjectId a =
      insert(state, "org.arbc.raster", "16x16", arbc::Affine::translation(8.0, 8.0));
  const arbc::ObjectId b =
      insert(state, "org.arbc.raster", "16x16", arbc::Affine::translation(32.0, 32.0));

  // The union of the two placed rasters, its center pivot, and a 2x scale-about-pivot delta.
  const std::optional<arbc::Rect> uni = ace::interact::selected_extent(
      ace::interact::pick_targets(state.document(), state.registry()), std::vector{a, b});
  REQUIRE(uni.has_value());
  const arbc::Vec2 pivot = cell_pivot(arbc::Affine::identity(), *uni);
  const arbc::Vec2 br{uni->x1, uni->y1};
  const arbc::Affine new_union =
      scale_cell(arbc::Affine::identity(), *uni, CellHandle::ScaleBottomRight,
                 arbc::Vec2{pivot.x + 2.0 * (br.x - pivot.x), pivot.y + 2.0 * (br.y - pivot.y)},
                 pivot, /*free_distort=*/false, /*about_pivot=*/true);

  const std::vector<Cell> before = cells_of(state);
  const Cell* ca0 = cell_by_id(before, a);
  const Cell* cb0 = cell_by_id(before, b);
  REQUIRE(ca0 != nullptr);
  REQUIRE(cb0 != nullptr);
  const arbc::Affine a_start = ca0->placement;
  const arbc::Affine b_start = cb0->placement;
  const std::vector<arbc::Affine> previews =
      group_transform(arbc::Affine::identity(), new_union, std::vector{a_start, b_start});
  REQUIRE(previews.size() == 2);

  SECTION("a well-formed batch is exactly one entry, one revision bump, atomically applied") {
    const std::size_t depth0 = depth(state);
    const std::uint64_t rev0 = revision(state);
    const std::vector<LayerTransform> batch = {LayerTransform{ca0->layer, previews[0]},
                                               LayerTransform{cb0->layer, previews[1]}};
    const DispatchOutcome out = dispatch(state, ace::commands::transform_cells_command(batch));

    CHECK(out.journal_entries_added == 1); // the whole batch is ONE entry (D-group_transform-1)
    CHECK(depth(state) == depth0 + 1);
    CHECK(revision(state) > rev0);

    // BOTH layers carry the new transform — no partial group (atomic publish).
    const std::vector<Cell> after = cells_of(state);
    CHECK(same_affine(cell_by_id(after, a)->placement, previews[0]));
    CHECK(same_affine(cell_by_id(after, b)->placement, previews[1]));

    // One undo press restores EVERY member's prior placement (D15 one step).
    REQUIRE(state.document().journal().undo());
    const std::vector<Cell> undone = cells_of(state);
    CHECK(same_affine(cell_by_id(undone, a)->placement, a_start));
    CHECK(same_affine(cell_by_id(undone, b)->placement, b_start));
  }

  SECTION("a stale-layer member adds nothing; the live member still commits as one entry") {
    const std::size_t depth0 = depth(state);
    // A bogus layer id (never minted) rides alongside a live member: it is skipped, the live one
    // commits, and it is still exactly ONE entry.
    const std::vector<LayerTransform> batch = {LayerTransform{arbc::ObjectId{}, previews[0]},
                                               LayerTransform{cb0->layer, previews[1]}};
    const DispatchOutcome out = dispatch(state, ace::commands::transform_cells_command(batch));
    CHECK(out.journal_entries_added == 1);
    CHECK(depth(state) == depth0 + 1);
    const std::vector<Cell> after = cells_of(state);
    CHECK(same_affine(cell_by_id(after, a)->placement,
                      a_start)); // untouched — its layer wasn't named
    CHECK(same_affine(cell_by_id(after, b)->placement, previews[1]));
  }

  SECTION("an empty batch commits nothing") {
    const std::size_t depth0 = depth(state);
    const DispatchOutcome out =
        dispatch(state, ace::commands::transform_cells_command(std::vector<LayerTransform>{}));
    CHECK(out.journal_entries_added == 0);
    CHECK(depth(state) == depth0);
  }
}

TEST_CASE("group transform: the batch is placement-only — resolution and bounds invariant (D8)") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "d8");

  const arbc::ObjectId a =
      insert(state, "org.arbc.raster", "16x16", arbc::Affine::translation(8.0, 8.0));
  const arbc::ObjectId b =
      insert(state, "org.arbc.raster", "16x16", arbc::Affine::translation(32.0, 32.0));

  const std::vector<Cell> before = cells_of(state);
  const Cell* ca0 = cell_by_id(before, a);
  const Cell* cb0 = cell_by_id(before, b);
  REQUIRE(ca0 != nullptr);
  REQUIRE(cb0 != nullptr);

  const std::optional<arbc::Rect> uni = ace::interact::selected_extent(
      ace::interact::pick_targets(state.document(), state.registry()), std::vector{a, b});
  REQUIRE(uni.has_value());
  const arbc::Vec2 pivot = cell_pivot(arbc::Affine::identity(), *uni);
  const arbc::Affine new_union =
      scale_cell(arbc::Affine::identity(), *uni, CellHandle::ScaleBottomRight,
                 arbc::Vec2{uni->x1 + 20.0, uni->y1 + 20.0}, pivot, /*free_distort=*/false,
                 /*about_pivot=*/true);
  const std::vector<arbc::Affine> previews = group_transform(
      arbc::Affine::identity(), new_union, std::vector{ca0->placement, cb0->placement});
  const std::vector<LayerTransform> batch = {LayerTransform{ca0->layer, previews[0]},
                                             LayerTransform{cb0->layer, previews[1]}};
  dispatch(state, ace::commands::transform_cells_command(batch));

  const std::vector<Cell> after = cells_of(state);
  const Cell* ca1 = cell_by_id(after, a);
  const Cell* cb1 = cell_by_id(after, b);
  REQUIRE(ca1 != nullptr);
  REQUIRE(cb1 != nullptr);

  // Placement moved for both — but their native pixel grid and content bounds are byte-identical
  // (a handle-drag is placement, never a resample; D8).
  CHECK_FALSE(same_affine(ca1->placement, ca0->placement));
  CHECK_FALSE(same_affine(cb1->placement, cb0->placement));
  CHECK(ca1->content_bounds == ca0->content_bounds);
  CHECK(cb1->content_bounds == cb0->content_bounds);
  CHECK(ca1->detail.native_pixels == ca0->detail.native_pixels);
  CHECK(cb1->detail.native_pixels == cb0->detail.native_pixels);
}

TEST_CASE("group transform: a group scale renders softer, never re-gridded (golden)") {
  arbc::ObjectId layer_a;
  arbc::ObjectId layer_b;
  const std::unique_ptr<arbc::Document> doc = build_group_doc(layer_a, layer_b);
  const arbc::Registry registry = group_registry();

  constexpr int rw = 64;
  constexpr int rh = 64;
  const ace::render::Srgb8Image base = ace::render::render_document_srgb8(*doc, rw, rh);
  REQUIRE(base.pixels.size() == static_cast<std::size_t>(rw) * rh * 4);

  // The two painted rasters are the only bounded targets; union them for the shared pivot.
  const std::vector<ace::interact::PickTarget> targets =
      ace::interact::pick_targets(*doc, registry);
  std::vector<arbc::ObjectId> bounded;
  std::vector<arbc::Affine> starts;
  std::vector<arbc::ObjectId> layers;
  for (const ace::interact::PickTarget& t : targets) {
    if (t.extent && (t.layer == layer_a || t.layer == layer_b)) {
      bounded.push_back(t.id);
      starts.push_back(t.placement);
      layers.push_back(t.layer);
    }
  }
  REQUIRE(bounded.size() == 2);
  const std::optional<arbc::Rect> uni = ace::interact::selected_extent(targets, bounded);
  REQUIRE(uni.has_value());
  const arbc::Vec2 pivot = cell_pivot(arbc::Affine::identity(), *uni);
  // Exactly 2x about the union center: pointer = pivot + 2*(corner - pivot).
  const arbc::Affine new_union = scale_cell(
      arbc::Affine::identity(), *uni, CellHandle::ScaleBottomRight,
      arbc::Vec2{pivot.x + 2.0 * (uni->x1 - pivot.x), pivot.y + 2.0 * (uni->y1 - pivot.y)}, pivot,
      /*free_distort=*/false, /*about_pivot=*/true);
  const std::vector<arbc::Affine> previews =
      group_transform(arbc::Affine::identity(), new_union, starts);
  std::vector<LayerTransform> batch;
  for (std::size_t i = 0; i < layers.size(); ++i) {
    batch.push_back(LayerTransform{layers[i], previews[i]});
  }
  ace::commands::transform_cells_command(batch).apply(*doc);

  const ace::render::Srgb8Image image = ace::render::render_document_srgb8(*doc, rw, rh);
  REQUIRE(image.pixels.size() == static_cast<std::size_t>(rw) * rh * 4);
  CHECK(image.pixels != base.pixels); // the group scale visibly changed the frame
  CHECK(ace_test::compare_golden("group_transform_scale_64x64.rgba8", image.pixels));
}
