// editor.import.image — L1 headless Catch2 units for importing a file as a BORROWED,
// read-only `org.arbc.image` cell placed native px -> composition units 1:1 (D11/D12/§8,
// A16/A29). Covers the two new L1 verbs (`interact::place_at_native_scale`, the
// `commands::image_config` frame + its decode), the A29 registration + the shipped generic
// classifier reading the minted cell (ReferencedImage + borrowed + native_pixels), the
// one-action-one-entry insert/undo, the present-file save->reopen round-trip, and one
// `render_offline` golden pinning that the borrowed photo renders at native scale where the
// drop point says. The L4 entry points (the drop verb, the FileDialog, `drop_device_point`)
// are the e2e's (tests/image_import_e2e_test.cpp) — `ace_tests` links no `ace::app`.
//
// No ImGui/GL/SDL (Constraint 1); runs under the ASan/TSan legs (A4/§9).

#include <ace/commands/app_state.hpp>
#include <ace/commands/cells.hpp>
#include <ace/commands/image_import.hpp>
#include <ace/interact/interact.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/import_asset.hpp>
#include <ace/project/project.hpp>
#include <ace/project/save.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/expected.hpp>
#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/content.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/model/model.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "golden_support.hpp"

using ace::commands::AppState;
using ace::commands::Command;
using ace::commands::dispatch;
using ace::scene::Cell;
using ace::scene::DetailSource;

namespace {

// The checked-in fixture: a 12x8 four-quadrant Netpbm PPM (the format the vendored `imdec`
// decodes) — top-left RED, top-right GREEN, bottom-left BLUE, bottom-right YELLOW, so a render
// pins orientation, scale and position, not just a bounding fill.
constexpr int k_fixture_w = 12;
constexpr int k_fixture_h = 8;
const std::filesystem::path k_fixture = std::filesystem::path(ACE_FIXTURE_DIR) / "photo_12x8.ppm";

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_image_import_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// A registry seeded exactly like `commands::register_editor_kinds` — builtins + camera + the
// A29 image kind.
arbc::Registry image_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  ace::commands::register_image_kind(registry);
  return registry;
}

// A fresh workspace-backed session (its registry already carries the image kind via
// register_editor_kinds) with a root composition to place the image in.
AppState session_with_composition(const ScratchDir& scratch, const ace::platform::FileSystem& fs,
                                  const char* leaf) {
  auto created = ace::project::create_project(fs, scratch.root / leaf);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  dispatch(state, Command{"add_composition",
                          [](arbc::Document& doc) { doc.add_composition(64.0, 64.0); }});
  return state;
}

} // namespace

// --- place_at_native_scale (Constraint 2) ------------------------------------

TEST_CASE("image_import: place_at_native_scale is unit-scale, centred, and camera-independent") {
  const std::optional<arbc::Rect> bounds =
      arbc::Rect{0.0, 0.0, static_cast<double>(k_fixture_w), static_cast<double>(k_fixture_h)};

  // A plain 2x zoom + offset camera (composition units -> device pixels).
  const arbc::Affine camera{2.0, 0.0, 0.0, 2.0, 10.0, 20.0};
  const arbc::Vec2 drop{50.0, 60.0};
  const arbc::Affine placement = ace::interact::place_at_native_scale(camera, drop, bounds);

  // UNIT scale, no shear/rotation (1 native px = 1 composition unit).
  CHECK(placement.a == 1.0);
  CHECK(placement.b == 0.0);
  CHECK(placement.c == 0.0);
  CHECK(placement.d == 1.0);
  CHECK(placement.max_scale() == 1.0);

  // The placed composition extent equals the native pixel dimensions, exactly.
  const arbc::Rect placed = placement.map_rect(*bounds);
  CHECK(placed.width() == static_cast<double>(k_fixture_w));
  CHECK(placed.height() == static_cast<double>(k_fixture_h));

  // Centred on the composition-space image of the drop point (device -> comp via the inverse):
  // comp = ((50-10)/2, (60-20)/2) = (20, 20).
  const arbc::Vec2 comp = camera.inverse()->apply(drop);
  CHECK((placed.x0 + placed.x1) * 0.5 == comp.x);
  CHECK((placed.y0 + placed.y1) * 0.5 == comp.y);

  // Camera-INDEPENDENCE: DOUBLING the zoom leaves the placed composition extent unchanged —
  // 1:1 is not fill-to-view. (The translation follows the drop point; the size does not.)
  const arbc::Affine zoomed{4.0, 0.0, 0.0, 4.0, 10.0, 20.0};
  const arbc::Rect placed2 =
      ace::interact::place_at_native_scale(zoomed, drop, bounds).map_rect(*bounds);
  CHECK(placed2.width() == static_cast<double>(k_fixture_w));
  CHECK(placed2.height() == static_cast<double>(k_fixture_h));
}

TEST_CASE("image_import: place_at_native_scale degrades to identity, never a NaN") {
  const std::optional<arbc::Rect> bounds = arbc::Rect{0.0, 0.0, 12.0, 8.0};
  // A zero-area / non-invertible camera -> identity placement.
  CHECK(ace::interact::place_at_native_scale(arbc::Affine::scaling(0.0, 0.0), arbc::Vec2{5.0, 5.0},
                                             bounds) == arbc::Affine::identity());
  // Unbounded content (nullopt) -> identity.
  CHECK(ace::interact::place_at_native_scale(arbc::Affine::identity(), arbc::Vec2{5.0, 5.0},
                                             std::nullopt) == arbc::Affine::identity());
  // An empty extent -> identity.
  const std::optional<arbc::Rect> empty = arbc::Rect{};
  CHECK(ace::interact::place_at_native_scale(arbc::Affine::identity(), arbc::Vec2{5.0, 5.0},
                                             empty) == arbc::Affine::identity());

  // A non-finite drop point -> identity (never propagate a NaN into the placement).
  const double nan = std::numeric_limits<double>::quiet_NaN();
  CHECK(ace::interact::place_at_native_scale(arbc::Affine::identity(), arbc::Vec2{nan, 0.0},
                                             bounds) == arbc::Affine::identity());

  // A finite drop under an invertible-but-near-singular camera whose inverse OVERFLOWS: the
  // determinant (1e-320) is a finite non-zero denormal so `inverse()` succeeds, but 1/det is
  // +inf, so the mapped composition point is non-finite -> identity, never a NaN translation.
  CHECK(ace::interact::place_at_native_scale(arbc::Affine::scaling(1e-160, 1e-160),
                                             arbc::Vec2{1.0, 1.0},
                                             bounds) == arbc::Affine::identity());

  // A finite drop + an invertible camera but an extent with a non-finite centre: the mapped
  // point is finite yet the centring translation is non-finite -> identity.
  const std::optional<arbc::Rect> unbounded_extent =
      arbc::Rect{0.0, 0.0, std::numeric_limits<double>::infinity(), 8.0};
  CHECK(ace::interact::place_at_native_scale(arbc::Affine::identity(), arbc::Vec2{5.0, 5.0},
                                             unbounded_extent) == arbc::Affine::identity());
}

// --- The image-config frame + its decode -------------------------------------

TEST_CASE("image_import: image_config builds a well-formed frame that decodes to native px") {
  ace::platform::NativeFileSystem fs;
  const ace::project::BorrowedAsset asset = ace::project::borrow_asset_file(fs, k_fixture);

  // The borrowed URI is the absolute path, un-relativized, and the bytes are embedded.
  CHECK(std::filesystem::path(asset.uri).is_absolute());
  CHECK_FALSE(asset.bytes.empty());

  const std::string config = ace::commands::image_config(asset.uri, asset.uri, asset.bytes);
  // Frame shape (D-image-5): "<authored>\n<resolved>\n<bytes>" — two newlines, the URI in both
  // fields, the fixture bytes after the second '\n'.
  const std::size_t first = config.find('\n');
  const std::size_t second = config.find('\n', first + 1);
  REQUIRE(first != std::string::npos);
  REQUIRE(second != std::string::npos);
  CHECK(config.substr(0, first) == asset.uri);
  CHECK(config.substr(first + 1, second - first - 1) == asset.uri);
  CHECK(config.substr(second + 1) == asset.bytes);

  // The registered factory decodes the embedded bytes to the fixture's native dimensions.
  const arbc::Registry registry = image_registry();
  const arbc::expected<std::optional<arbc::Rect>, std::string> bounds =
      ace::scene::probe_bounds(registry, ace::commands::image_kind_id, config);
  REQUIRE(bounds.has_value());
  REQUIRE(bounds->has_value());
  CHECK((*bounds)->width() == static_cast<double>(k_fixture_w));
  CHECK((*bounds)->height() == static_cast<double>(k_fixture_h));
}

TEST_CASE("image_import: a missing borrow is a value (no pixels), never a throw") {
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path missing =
      std::filesystem::path(ACE_FIXTURE_DIR) / "does_not_exist.ppm";
  const ace::project::BorrowedAsset asset = ace::project::borrow_asset_file(fs, missing);
  // The URI is still recorded (the borrow is remembered) but the bytes are empty — the D13
  // unavailable case, a fact about the environment, not an error.
  CHECK_FALSE(asset.uri.empty());
  CHECK(asset.bytes.empty());

  // The frame is still well-formed (empty bytes), and the factory yields a content with the
  // URI and no pixels — a VALUE, never a throw.
  const std::string config = ace::commands::image_config(asset.uri, asset.uri, asset.bytes);
  const arbc::Registry registry = image_registry();
  const arbc::expected<std::optional<arbc::Rect>, std::string> bounds =
      ace::scene::probe_bounds(registry, ace::commands::image_kind_id, config);
  CHECK(bounds.has_value()); // resolved as a value: unavailable, not an error
}

// --- Registration + the shipped generic classifier (A29 / A16) ---------------

TEST_CASE("image_import: register_image_kind registers the factory and the kind id") {
  const arbc::Registry registry = image_registry();
  CHECK(registry.factory(ace::commands::image_kind_id) != nullptr);
  const std::vector<std::string_view> ids = registry.ids();
  CHECK(std::find(ids.begin(), ids.end(), std::string_view(ace::commands::image_kind_id)) !=
        ids.end());
  // Registered factory-only: the runtime serialize codec is provided by libarbc (gated on the
  // factory), so the registry carries no editor-supplied codec for the image kind.
  CHECK(registry.codec(ace::commands::image_kind_id) == nullptr);
}

TEST_CASE("image_import: a minted image cell classifies ReferencedImage + borrowed + native px") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "classify");

  const ace::project::BorrowedAsset asset = ace::project::borrow_asset_file(fs, k_fixture);
  const std::string config = ace::commands::image_config(asset.uri, asset.uri, asset.bytes);
  REQUIRE(ace::scene::add_cell(state.document(), state.registry(), ace::commands::image_kind_id,
                               config, arbc::Affine::translation(4.0, 5.0))
              .has_value());

  const std::vector<Cell> cells = ace::scene::cells(state.document(), state.registry());
  REQUIRE(cells.size() == 1);
  CHECK(cells[0].kind_id == std::string(ace::commands::image_kind_id));
  // The shipped classifier (cell.cpp) reads the generic facets, never a kind_id switch (A16):
  // no editable() -> ReferencedImage, a non-empty external_asset_ref() -> borrowed, and
  // native_pixels off bounds().
  CHECK(cells[0].detail.source == DetailSource::ReferencedImage);
  CHECK(cells[0].detail.borrowed);
  REQUIRE(cells[0].detail.native_pixels.has_value());
  CHECK(cells[0].detail.native_pixels->first == k_fixture_w);
  CHECK(cells[0].detail.native_pixels->second == k_fixture_h);
}

// --- One action, one entry (Constraint 4) ------------------------------------

TEST_CASE("image_import: an insert is ONE journal entry undone whole on its own ObjectId") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "journal");

  const ace::project::BorrowedAsset asset = ace::project::borrow_asset_file(fs, k_fixture);
  const std::string config = ace::commands::image_config(asset.uri, asset.uri, asset.bytes);

  const std::size_t depth_baseline = state.document().journal().depth();
  ace::commands::InsertCellOutcome outcome;
  const Command command =
      ace::commands::insert_cell_command(state.registry(), ace::commands::image_kind_id, config,
                                         arbc::Affine::translation(1.0, 2.0), outcome);
  const ace::commands::DispatchOutcome dispatched = dispatch(state, command);

  CHECK(dispatched.journal_entries_added == 1);
  CHECK(state.document().journal().depth() == depth_baseline + 1);
  CHECK(outcome.error.empty());
  REQUIRE(outcome.content.valid());

  // A single undo removes the cell whole, on its same ObjectId (D15).
  REQUIRE(ace::commands::undo(state).moved);
  CHECK(ace::scene::cells(state.document(), state.registry()).empty());
  CHECK(state.document().pin()->find_content(outcome.content) == nullptr);
}

TEST_CASE("image_import: a malformed frame opens no transaction and mutates nothing") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "journal_fail");

  const std::size_t depth_baseline = state.document().journal().depth();
  ace::commands::InsertCellOutcome outcome;
  // A single-line frame has no resolved-URI delimiter -> the kind's own malformed-frame error.
  const Command command =
      ace::commands::insert_cell_command(state.registry(), ace::commands::image_kind_id,
                                         "no-delimiters", arbc::Affine::identity(), outcome);
  const ace::commands::DispatchOutcome dispatched = dispatch(state, command);
  CHECK(dispatched.journal_entries_added == 0);
  CHECK_FALSE(outcome.content.valid());
  CHECK_FALSE(outcome.error.empty());
  CHECK(state.document().journal().depth() == depth_baseline);
  CHECK(ace::scene::cells(state.document(), state.registry()).empty());
}

// --- Present-file round-trip (Constraint 5) ----------------------------------

TEST_CASE("image_import: a borrowed image round-trips through save + canonical-rebuild reopen") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "roundtrip";

  const ace::project::BorrowedAsset asset = ace::project::borrow_asset_file(fs, k_fixture);
  const arbc::Affine placement = arbc::Affine::translation(3.0, 7.0);
  {
    AppState state = session_with_composition(scratch, fs, "roundtrip");
    const std::string config = ace::commands::image_config(asset.uri, asset.uri, asset.bytes);
    REQUIRE(ace::scene::add_cell(state.document(), state.registry(), ace::commands::image_kind_id,
                                 config, placement)
                .has_value());
    REQUIRE(ace::commands::save_project(state, fs).has_value());
  } // released: workspace checkpointed + unmapped

  // Shed the workspace so the reopen MUST rebuild from canonical — the runtime image codec
  // path (gated on the factory), which is what proves the borrowed image serializes.
  std::error_code ec;
  std::filesystem::remove_all(ace::project::project_layout(root).workspace_dir, ec);

  // Reopen with the image kind registered so the codec reconstructs it live; the installed
  // FilesystemAssetSource re-resolves the borrowed URI and re-fetches the (present) pixels.
  // The reopen callback mirrors `commands::register_editor_kinds` EXACTLY (camera THEN image):
  // the KindBridge token ordering must match the save side, or a reconstructed cell's kind token
  // resolves to the wrong kind. This is the real app's `open_project(fs, root,
  // register_editor_kinds)`.
  auto reopened = ace::project::open_project(fs, root, [](arbc::Registry& r) {
    ace::scene::register_camera_kind(r);
    ace::commands::register_image_kind(r);
  });
  REQUIRE(reopened.has_value());
  CHECK(reopened.value().rebuilt_from_canonical);

  const arbc::Registry load_registry = image_registry();
  const std::vector<Cell> cells = ace::scene::cells(*reopened.value().document, load_registry);
  REQUIRE(cells.size() == 1);
  CHECK(cells[0].kind_id == std::string(ace::commands::image_kind_id));
  CHECK(cells[0].placement == placement);
  CHECK(cells[0].detail.source == DetailSource::ReferencedImage);
  CHECK(cells[0].detail.borrowed);

  // The borrowed reference survives as the authored URI, and the pixels re-resolve to native px.
  arbc::Content* content = reopened.value().document->resolve(cells[0].id);
  REQUIRE(content != nullptr);
  CHECK(content->external_asset_ref() == asset.uri);
  const std::optional<arbc::Rect> bounds = content->bounds();
  REQUIRE(bounds.has_value());
  CHECK(bounds->width() == static_cast<double>(k_fixture_w));
  CHECK(bounds->height() == static_cast<double>(k_fixture_h));
}

// --- Rendered output: the golden (§9) ----------------------------------------

TEST_CASE("image_import: the borrowed image composites at native scale to the sRGB8 golden") {
  const ace::project::ProbeDocument probe = ace::project::build_probe_document();
  const arbc::Registry registry = image_registry();

  ace::platform::NativeFileSystem fs;
  const ace::project::BorrowedAsset asset = ace::project::borrow_asset_file(fs, k_fixture);
  const std::string config = ace::commands::image_config(asset.uri, asset.uri, asset.bytes);
  const arbc::expected<std::optional<arbc::Rect>, std::string> bounds =
      ace::scene::probe_bounds(registry, ace::commands::image_kind_id, config);
  REQUIRE(bounds.has_value());
  REQUIRE(bounds->has_value());

  // The placement the shipped drop would compute for a drop at the composition centre (32,32)
  // under an identity camera: unit scale, centred -> the 12x8 image spans comp x[26,38), y[28,36).
  const arbc::Affine placement = ace::interact::place_at_native_scale(
      arbc::Affine::identity(), arbc::Vec2{32.0, 32.0}, *bounds);
  CHECK(placement.a == 1.0);
  CHECK(placement.tx == 26.0);
  CHECK(placement.ty == 28.0);
  REQUIRE(ace::scene::add_cell(*probe.document, registry, ace::commands::image_kind_id, config,
                               placement)
              .has_value());

  const ace::render::Srgb8Image image = ace::render::render_document_srgb8(
      *probe.document, ace::project::k_probe_width, ace::project::k_probe_height);
  REQUIRE(image.pixels.size() == static_cast<std::size_t>(ace::project::k_probe_width) *
                                     static_cast<std::size_t>(ace::project::k_probe_height) * 4);

  // Spot-check the geometry the golden encodes so a regenerated golden can never silently bless
  // a wrong placement/scale/orientation: green probe outside the image, and the four quadrants
  // inside it (top-left red, top-right green, bottom-left blue, bottom-right yellow). The image
  // occupies comp/device x[26,38), y[28,36); the quadrant split is at x=32, y=32.
  const auto px = [&image](int x, int y) {
    const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                            static_cast<std::size_t>(x)) *
                           4;
    return std::vector<std::uint8_t>{image.pixels[at], image.pixels[at + 1], image.pixels[at + 2],
                                     image.pixels[at + 3]};
  };
  CHECK(px(2, 2)[1] > px(2, 2)[0]);                // green probe well outside the image
  CHECK(px(20, 32)[1] > px(20, 32)[0]);            // just LEFT of the image: still green background
  const std::vector<std::uint8_t> tl = px(28, 29); // top-left quadrant: RED
  CHECK((tl[0] > tl[1] && tl[0] > tl[2]));
  const std::vector<std::uint8_t> tr = px(35, 29); // top-right quadrant: GREEN
  CHECK((tr[1] > tr[0] && tr[1] > tr[2]));
  const std::vector<std::uint8_t> bl = px(28, 34); // bottom-left quadrant: BLUE
  CHECK((bl[2] > bl[0] && bl[2] > bl[1]));
  const std::vector<std::uint8_t> br = px(35, 34); // bottom-right quadrant: YELLOW (r+g high)
  CHECK((br[0] > br[2] && br[1] > br[2]));

  CHECK(ace_test::compare_golden("import_image_64x64.rgba8", image.pixels));
}
