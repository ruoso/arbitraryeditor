// editor.panels.inspector — L1 headless Catch2 units for the dense per-object property sheet.
// Covers the pure transform decomposition (`interact::decompose_placement`, Constraint 1), the
// z-order readout (`scene::z_order_position`, Constraint 4), the two novel cell edit verbs
// (`scene::set_cell_opacity` / `set_cell_visible`, Constraint 3 — clamp, one-entry, undoable,
// same-object, non-cell no-op) and the provenance classification the Source block shows
// (`scene::describe_detail_source` over the generic-facet `DetailSource`, Constraint 6).
//
// No ImGui/GL/SDL (Constraint 1/§8); runs under the ASan/TSan legs (A4/§9).

#include <ace/commands/app_state.hpp>
#include <ace/interact/interact.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/expected.hpp>
#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/content.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/model/records.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace interact = ace::interact;

using ace::commands::AppState;
using ace::scene::Cell;
using ace::scene::DetailSource;
using Catch::Approx;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_inspector_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// A fresh workspace-backed session with a root composition to place cells in (the
// cell_model_test fixture shape).
AppState session_with_composition(const ScratchDir& scratch, const ace::platform::FileSystem& fs,
                                  const char* leaf) {
  auto created = ace::project::create_project(fs, scratch.root / leaf);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  state.document().add_composition(64.0, 64.0);
  return state;
}

double cell_opacity(AppState& state, arbc::ObjectId id) {
  for (const Cell& c : ace::scene::cells(state.document(), state.registry())) {
    if (c.id == id) {
      return c.opacity;
    }
  }
  return -1.0;
}

bool cell_visible(AppState& state, arbc::ObjectId id) {
  for (const Cell& c : ace::scene::cells(state.document(), state.registry())) {
    if (c.id == id) {
      return c.visible;
    }
  }
  return false;
}

// A referenced-image-shaped kind the EDITOR cannot know: `external_asset_ref()` non-empty,
// `editable()` null — it MUST classify ReferencedImage through the generic facet, never a kind
// string (A16 / D-resolution-2). Modelled on cell_model_test's ExternalRefProbe.
class ExternalRefProbe final : public arbc::Content {
public:
  std::optional<arbc::Rect> bounds() const override { return arbc::Rect{0.0, 0.0, 64.0, 48.0}; }
  arbc::Stability stability() const override { return arbc::Stability::Static; }
  std::optional<arbc::TimeRange> time_extent() const override { return std::nullopt; }
  std::optional<arbc::RenderResult> render(const arbc::RenderRequest&,
                                           std::shared_ptr<arbc::RenderCompletion>) override {
    return std::nullopt;
  }
  std::string_view external_asset_ref() const override { return d_uri; }

private:
  std::string d_uri = "file:///borrowed/photo.png";
};

constexpr const char* k_external_probe_kind = "org.example.external_probe";

arbc::Registry inspector_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  arbc::ContentFactory factory =
      [](arbc::ContentConfig) -> arbc::expected<std::unique_ptr<arbc::Content>, std::string> {
    return std::unique_ptr<arbc::Content>(std::make_unique<ExternalRefProbe>());
  };
  (void)registry.add(k_external_probe_kind, std::move(factory),
                     arbc::KindMetadata{"External Probe", "1"});
  return registry;
}

} // namespace

// --- Transform decomposition (Constraint 1) ----------------------------------

TEST_CASE("inspector: decompose_placement recovers position/size/rotation/shear") {
  const std::optional<arbc::Rect> bounds = arbc::Rect{0.0, 0.0, 10.0, 20.0};

  SECTION("identity") {
    const interact::PlacementReadout t =
        interact::decompose_placement(arbc::Affine::identity(), bounds);
    REQUIRE(t.valid);
    CHECK(t.position.x == Approx(0.0));
    CHECK(t.position.y == Approx(0.0));
    CHECK(t.placed.width() == Approx(10.0));
    CHECK(t.placed.height() == Approx(20.0));
    CHECK(t.rotation_deg == Approx(0.0));
    CHECK(t.shear_deg == Approx(0.0));
    CHECK(t.scale_x == Approx(1.0));
    CHECK(t.scale_y == Approx(1.0));
  }

  SECTION("pure translation") {
    const interact::PlacementReadout t =
        interact::decompose_placement(arbc::Affine::translation(5.0, 3.0), bounds);
    REQUIRE(t.valid);
    CHECK(t.position.x == Approx(5.0));
    CHECK(t.position.y == Approx(3.0));
    CHECK(t.placed.width() == Approx(10.0));
    CHECK(t.placed.height() == Approx(20.0));
    CHECK(t.rotation_deg == Approx(0.0));
    CHECK(t.shear_deg == Approx(0.0));
  }

  SECTION("rotation by 90 degrees — shear stays 0 for a pure rotation") {
    const arbc::Affine rot{0.0, 1.0, -1.0, 0.0, 0.0, 0.0};
    const interact::PlacementReadout t = interact::decompose_placement(rot, bounds);
    REQUIRE(t.valid);
    CHECK(t.rotation_deg == Approx(90.0));
    CHECK(t.shear_deg == Approx(0.0));
    CHECK(t.scale_x == Approx(1.0));
    CHECK(t.scale_y == Approx(1.0));
    // The AABB of a 10x20 rect rotated 90 degrees is 20x10.
    CHECK(t.placed.width() == Approx(20.0));
    CHECK(t.placed.height() == Approx(10.0));
  }

  SECTION("anisotropic scale — shear stays 0 for a pure scale") {
    const arbc::Affine scl = arbc::Affine::scaling(2.0, 3.0);
    const interact::PlacementReadout t = interact::decompose_placement(scl, bounds);
    REQUIRE(t.valid);
    CHECK(t.rotation_deg == Approx(0.0));
    CHECK(t.shear_deg == Approx(0.0));
    CHECK(t.scale_x == Approx(2.0));
    CHECK(t.scale_y == Approx(3.0));
    CHECK(t.placed.width() == Approx(20.0));
    CHECK(t.placed.height() == Approx(60.0));
  }

  SECTION("pure shear — nonzero shear angle") {
    const arbc::Affine shear{1.0, 0.0, 1.0, 1.0, 0.0, 0.0};
    const interact::PlacementReadout t = interact::decompose_placement(shear, bounds);
    REQUIRE(t.valid);
    CHECK(t.rotation_deg == Approx(0.0));
    CHECK(t.shear_deg == Approx(45.0));
    CHECK(t.placed.width() == Approx(30.0));
    CHECK(t.placed.height() == Approx(20.0));
  }

  SECTION("rotated+sheared+translated composite — hand-computed") {
    const arbc::Affine m{1.0, 1.0, -1.0, 2.0, 5.0, 7.0};
    const interact::PlacementReadout t =
        interact::decompose_placement(m, arbc::Rect{0.0, 0.0, 4.0, 4.0});
    REQUIRE(t.valid);
    CHECK(t.position.x == Approx(5.0));
    CHECK(t.position.y == Approx(7.0));
    CHECK(t.rotation_deg == Approx(45.0));
    CHECK(t.shear_deg == Approx(18.434948822922));
    CHECK(t.scale_x == Approx(std::sqrt(2.0)));
    CHECK(t.scale_y == Approx(3.0 / std::sqrt(2.0)));
    // AABB of the four mapped corners {(5,7),(9,11),(1,15),(5,19)}: x in [1,9], y in [7,19].
    CHECK(t.placed.width() == Approx(8.0));
    CHECK(t.placed.height() == Approx(12.0));
  }
}

TEST_CASE("inspector: decompose_placement refuses degenerate/non-finite placements, NaN-free") {
  const std::optional<arbc::Rect> bounds = arbc::Rect{0.0, 0.0, 10.0, 10.0};

  SECTION("zero determinant (collapsed to a line)") {
    const arbc::Affine deg{1.0, 1.0, 1.0, 1.0, 2.0, 3.0}; // det == 0
    const interact::PlacementReadout t = interact::decompose_placement(deg, bounds);
    CHECK_FALSE(t.valid);
    CHECK(t.rotation_deg == 0.0);
    CHECK(t.shear_deg == 0.0);
    CHECK_FALSE(std::isnan(t.rotation_deg));
    CHECK_FALSE(std::isnan(t.shear_deg));
  }

  SECTION("all-zero linear part") {
    const arbc::Affine zero{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    const interact::PlacementReadout t = interact::decompose_placement(zero, bounds);
    CHECK_FALSE(t.valid);
  }

  SECTION("non-finite entry") {
    const double inf = std::numeric_limits<double>::infinity();
    const arbc::Affine nf{inf, 0.0, 0.0, 1.0, 0.0, 0.0};
    const interact::PlacementReadout t = interact::decompose_placement(nf, bounds);
    CHECK_FALSE(t.valid);
    CHECK_FALSE(std::isnan(t.rotation_deg));
  }
}

// --- Z-order readout (Constraint 4) ------------------------------------------

TEST_CASE("inspector: z_order_position reports membership order, stable across a layer edit") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "zorder");
  arbc::Registry& registry = state.registry();

  const auto a = ace::scene::add_cell(state.document(), registry, "org.arbc.raster", "8x8",
                                      arbc::Affine::identity());
  const auto b = ace::scene::add_cell(state.document(), registry, "org.arbc.raster", "8x8",
                                      arbc::Affine::translation(4.0, 4.0));
  const auto c = ace::scene::add_cell(state.document(), registry, "org.arbc.raster", "8x8",
                                      arbc::Affine::translation(8.0, 8.0));
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  REQUIRE(c.has_value());

  std::vector<Cell> cells = ace::scene::cells(state.document(), registry);
  REQUIRE(cells.size() == 3);
  // z-order == insertion order (back->front): a, b, c.
  CHECK(ace::scene::z_order_position(cells, *a).index == 0);
  CHECK(ace::scene::z_order_position(cells, *b).index == 1);
  CHECK(ace::scene::z_order_position(cells, *c).index == 2);
  CHECK(ace::scene::z_order_position(cells, *a).count == 3);
  CHECK(ace::scene::z_order_position(cells, arbc::ObjectId{}).index == -1);

  // An appearance edit path-copies a LayerRecord; z-order tracks MEMBERSHIP, not attributes, so
  // the (index, count) and the object identities are preserved (no cell-rename verb exists —
  // this exercises the same identity-preserved property the refinement frames as a rename).
  REQUIRE(ace::scene::set_cell_opacity(state.document(), registry, *b, 0.5));
  cells = ace::scene::cells(state.document(), registry);
  REQUIRE(cells.size() == 3);
  CHECK(ace::scene::z_order_position(cells, *a).index == 0);
  CHECK(ace::scene::z_order_position(cells, *b).index == 1);
  CHECK(ace::scene::z_order_position(cells, *c).index == 2);
}

// --- Opacity / visibility edit verbs (Constraint 3) --------------------------

TEST_CASE("inspector: set_cell_opacity clamps straight-alpha, one entry, undoable, same object") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "opacity");
  arbc::Registry& registry = state.registry();
  const auto raster = ace::scene::add_cell(state.document(), registry, "org.arbc.raster", "16x16",
                                           arbc::Affine::identity());
  REQUIRE(raster.has_value());
  const arbc::ObjectId cell = *raster;
  CHECK(cell_opacity(state, cell) == Approx(1.0)); // library default

  const arbc::Journal& journal = state.document().journal();
  const std::size_t depth_before = journal.depth();
  REQUIRE(ace::scene::set_cell_opacity(state.document(), registry, cell, 0.4));
  CHECK(journal.depth() == depth_before + 1); // ONE journal entry per edit
  CHECK(cell_opacity(state, cell) == Approx(0.4));
  // The object keeps its ObjectId across the edit (the shared selection holds).
  CHECK(ace::scene::cells(state.document(), registry).front().id == cell);

  // Straight-alpha, clamped to [0,1].
  REQUIRE(ace::scene::set_cell_opacity(state.document(), registry, cell, 5.0));
  CHECK(cell_opacity(state, cell) == Approx(1.0));
  REQUIRE(ace::scene::set_cell_opacity(state.document(), registry, cell, -2.0));
  CHECK(cell_opacity(state, cell) == Approx(0.0));

  // Undo restores the prior opacity (1.0 from the clamped-high edit) on the same object.
  const ace::commands::UndoOutcome u = ace::commands::undo(state);
  CHECK(u.moved);
  CHECK(cell_opacity(state, cell) == Approx(1.0));
  CHECK(ace::scene::cells(state.document(), registry).front().id == cell);
}

TEST_CASE("inspector: set_cell_visible toggles k_layer_visible, one entry, undoable") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "visible");
  arbc::Registry& registry = state.registry();
  const auto raster = ace::scene::add_cell(state.document(), registry, "org.arbc.raster", "16x16",
                                           arbc::Affine::identity());
  REQUIRE(raster.has_value());
  const arbc::ObjectId cell = *raster;
  CHECK(cell_visible(state, cell)); // library default: visible

  const arbc::Journal& journal = state.document().journal();
  const std::size_t depth_before = journal.depth();
  REQUIRE(ace::scene::set_cell_visible(state.document(), registry, cell, false));
  CHECK(journal.depth() == depth_before + 1);
  CHECK_FALSE(cell_visible(state, cell));

  const ace::commands::UndoOutcome u = ace::commands::undo(state);
  CHECK(u.moved);
  CHECK(cell_visible(state, cell)); // restored on the same object
  CHECK(ace::scene::cells(state.document(), registry).front().id == cell);
}

TEST_CASE("inspector: opacity/visible verbs no-op on a non-cell or unresolvable id") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "noop");
  arbc::Registry& registry = state.registry();
  const auto raster = ace::scene::add_cell(state.document(), registry, "org.arbc.raster", "16x16",
                                           arbc::Affine::identity());
  REQUIRE(raster.has_value());
  const arbc::ObjectId camera = ace::scene::add_camera(
      state.document(), registry, "Cam", ace::scene::Resolution{16, 16}, arbc::Affine::identity());
  REQUIRE(camera.valid());

  const arbc::Journal& journal = state.document().journal();
  const std::size_t depth_before = journal.depth();
  // A camera's content id is not a cell (A14): rejected, mutates nothing.
  CHECK_FALSE(ace::scene::set_cell_opacity(state.document(), registry, camera, 0.5));
  CHECK_FALSE(ace::scene::set_cell_visible(state.document(), registry, camera, false));
  // An invalid / unresolvable id: rejected.
  CHECK_FALSE(ace::scene::set_cell_opacity(state.document(), registry, arbc::ObjectId{}, 0.5));
  CHECK_FALSE(ace::scene::set_cell_visible(state.document(), registry, arbc::ObjectId{}, false));
  CHECK(journal.depth() == depth_before); // nothing journaled by any no-op
}

// --- Provenance classification (Constraint 6, D11) ---------------------------

TEST_CASE("inspector: describe_detail_source labels the generic-facet provenance classes") {
  CHECK(std::string(ace::scene::describe_detail_source(DetailSource::PaintedRaster)) ==
        "Painted raster - editable, owned");
  CHECK(std::string(ace::scene::describe_detail_source(DetailSource::ReferencedImage)) ==
        "Referenced image - source-limited");
  CHECK(std::string(ace::scene::describe_detail_source(DetailSource::ResolutionIndependent)) ==
        "Resolution-independent - no native detail floor");
}

TEST_CASE(
    "inspector: Source block reads provenance + health from classify_detail, no kind switch") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "provenance");
  arbc::Registry registry = inspector_registry();

  const arbc::Affine at = arbc::Affine::identity();
  REQUIRE(ace::scene::add_cell(state.document(), registry, "org.arbc.raster", "16x24", at)
              .has_value()); // painted
  REQUIRE(ace::scene::add_cell(state.document(), registry, k_external_probe_kind, "x", at)
              .has_value()); // referenced
  REQUIRE(ace::scene::add_cell(state.document(), registry, "org.arbc.solid", "0.1,0.2,0.3,1", at)
              .has_value()); // independent

  const std::vector<Cell> cells = ace::scene::cells(state.document(), registry);
  REQUIRE(cells.size() == 3);

  CHECK(cells[0].detail.source == DetailSource::PaintedRaster);
  CHECK(std::string(ace::scene::describe_detail_source(cells[0].detail.source)) ==
        "Painted raster - editable, owned");
  REQUIRE(cells[0].detail.native_pixels.has_value());

  CHECK(cells[1].detail.source == DetailSource::ReferencedImage);
  CHECK(std::string(ace::scene::describe_detail_source(cells[1].detail.source)) ==
        "Referenced image - source-limited");

  CHECK(cells[2].detail.source == DetailSource::ResolutionIndependent);
  CHECK_FALSE(cells[2].detail.native_pixels.has_value());

  // Health is READ through the shipped resolution_health path (Constraint 6): a camera framing a
  // small region at a high pixel count samples the painted raster well above native, so the
  // Source block shows a Soft verdict. frame scaling(0.25) => 256 output px over 64 comp units
  // (4 px/unit) vs the raster's 1 native px/unit => ratio 4.
  const arbc::ObjectId camera =
      ace::scene::add_camera(state.document(), registry, "Hero", ace::scene::Resolution{256, 256},
                             arbc::Affine::scaling(0.25, 0.25));
  REQUIRE(camera.valid());
  const std::vector<Cell> after = ace::scene::cells(state.document(), registry);
  const std::vector<ace::scene::Camera> cams = ace::scene::cameras(state.document());
  REQUIRE(after[0].detail.native_pixels.has_value());
  const interact::ResolutionHealth h = interact::resolution_health(
      after[0].detail.native_pixels->first, after[0].detail.native_pixels->second,
      after[0].placement, cams.front().resolution.width, cams.front().resolution.height,
      cams.front().frame);
  CHECK(h.verdict == interact::ResolutionVerdict::Soft);
}
