// editor.import.paste — L1 headless Catch2 units for pasting a clipboard bitmap as an OWNED,
// read-only `org.arbc.image` cell placed native px -> composition units 1:1 (D11/D12/§8, A16/A30).
// Covers the ONE genuinely new L1 verb (`project::mint_owned_asset` — content-address + write-if-
// absent + dedup + project-relative owned URI), the `scene::classify_detail` owned/borrowed
// URI-shape split, the one-action-one-entry insert/undo of a minted owned cell, the in-place
// save->reopen round-trip, and the golden REUSE proving owning-the-bytes does not perturb the
// render. The L4 entry points (the Clipboard seam, `paste_image`, the Ctrl+V chord, the "Paste
// image" affordance) are the e2e's (tests/paste_image_e2e_test.cpp) — `ace_tests` links no
// `ace::app`.
//
// No ImGui/GL/SDL (Constraint 1); runs under the ASan/TSan legs (A4/§9).

#include <ace/commands/app_state.hpp>
#include <ace/commands/cells.hpp>
#include <ace/commands/image_import.hpp>
#include <ace/interact/interact.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/result.hpp>
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
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
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

// The SAME checked-in fixture `editor.import.image` added, reused here as the "clipboard bytes": a
// 12x8 four-quadrant Netpbm PPM the vendored `imdec` decodes (top-left RED, top-right GREEN,
// bottom-left BLUE, bottom-right YELLOW), so a render pins orientation, scale and position.
constexpr int k_fixture_w = 12;
constexpr int k_fixture_h = 8;
const std::filesystem::path k_fixture = std::filesystem::path(ACE_FIXTURE_DIR) / "photo_12x8.ppm";

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_paste_image_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// A registry seeded exactly like `commands::register_editor_kinds` — builtins + camera + image.
arbc::Registry image_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  ace::commands::register_image_kind(registry);
  return registry;
}

// A fresh workspace-backed session with a root composition to place the pasted image in.
AppState session_with_composition(const ScratchDir& scratch, const ace::platform::FileSystem& fs,
                                  const char* leaf) {
  auto created = ace::project::create_project(fs, scratch.root / leaf);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  dispatch(state, Command{"add_composition",
                          [](arbc::Document& doc) { doc.add_composition(64.0, 64.0); }});
  return state;
}

// Read the fixture bytes off disk (the stand-in for encoded clipboard bytes).
std::string fixture_bytes(const ace::platform::FileSystem& fs) {
  ace::platform::Result<std::string> read = fs.read_file(k_fixture);
  REQUIRE(read.has_value());
  return std::move(*read);
}

} // namespace

// --- mint_owned_asset: content-address + dedup + resolve (Constraints 3-4) ----

TEST_CASE("paste_image: mint_owned_asset writes a project-relative owned blob under assets/") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const ace::project::ProjectLayout layout = ace::project::project_layout(scratch.root / "mint");
  const std::string bytes = fixture_bytes(fs);

  const ace::project::OwnedAsset owned = ace::project::mint_owned_asset(fs, layout, bytes, "ppm");

  // Project-relative owned URI (NOT absolute, NOT `file://`, under `assets/images/`).
  CHECK(owned.uri.rfind("assets/images/", 0) == 0);
  CHECK_FALSE(std::filesystem::path(owned.uri).is_absolute());
  CHECK(owned.uri.rfind("file://", 0) == std::string::npos);
  // The extension is appended for legibility only.
  CHECK(owned.uri.substr(owned.uri.size() - 4) == ".ppm");
  // The bytes are echoed back verbatim for the immediate embed.
  CHECK(owned.bytes == bytes);

  // The blob is on disk, resolved against the project root the same way save/reopen resolve it
  // (`<root>/` + `assets/...`), and equals the minted bytes.
  const std::filesystem::path blob = layout.root / owned.uri;
  CHECK(std::filesystem::exists(blob));
  const ace::platform::Result<std::string> read_back = fs.read_file(blob);
  REQUIRE(read_back.has_value());
  CHECK(*read_back == bytes);
}

TEST_CASE(
    "paste_image: mint_owned_asset is content-addressed — same bytes dedup, different differ") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const ace::project::ProjectLayout layout = ace::project::project_layout(scratch.root / "dedup");
  const std::string bytes = fixture_bytes(fs);

  const ace::project::OwnedAsset first = ace::project::mint_owned_asset(fs, layout, bytes, "ppm");

  // Overwrite the on-disk blob with a SENTINEL, then re-mint the same bytes: write-if-absent must
  // find the content name already present and NOT re-write — the sentinel survives (dedup, no-op).
  const std::filesystem::path blob = layout.root / first.uri;
  REQUIRE_FALSE(fs.atomic_replace(blob, "SENTINEL"));
  const ace::project::OwnedAsset again = ace::project::mint_owned_asset(fs, layout, bytes, "ppm");
  CHECK(again.uri == first.uri); // same bytes -> same URI
  const ace::platform::Result<std::string> after = fs.read_file(blob);
  REQUIRE(after.has_value());
  CHECK(*after == "SENTINEL"); // NOT re-written — the mint no-op'd on the present blob

  // Different bytes content-address to a DIFFERENT URI.
  const ace::project::OwnedAsset other =
      ace::project::mint_owned_asset(fs, layout, bytes + "x", "ppm");
  CHECK(other.uri != first.uri);

  // An empty extension yields the bare `tile_blob_uri` shape (no trailing dot).
  const ace::project::OwnedAsset no_ext = ace::project::mint_owned_asset(fs, layout, bytes);
  CHECK(no_ext.uri.rfind("assets/images/", 0) == 0);
  CHECK(no_ext.uri.find('.') == std::string::npos);
}

// --- classify_detail: owned vs borrowed by URI shape (Constraint 3 / D-paste-2) ---

TEST_CASE("paste_image: classify reports owned for assets/ URIs, borrowed for external") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "classify");
  const std::string bytes = fixture_bytes(fs);

  // Table-driven over a few URI shapes, added bottom->top: the SAME decodable bytes, only the
  // authored URI differs. The shipped generic classifier (`scene::classify_detail`, exercised
  // through the public `cells()` walk) reads the D11 "bytes where?" axis off the URI SHAPE.
  struct Shape {
    std::string uri;
    bool borrowed;
  };
  const std::vector<Shape> shapes = {
      {"assets/images/ab/0123456789abcdef0123456789abcdef.ppm", false}, // minted owned
      {"assets/tiles/cd/fedcba9876543210fedcba9876543210", false},      // owned (tile scheme)
      {k_fixture.string(), true},           // external absolute (import)
      {"file:///photos/holiday.png", true}, // external file:// (import)
  };
  for (const Shape& shape : shapes) {
    const std::string config = ace::commands::image_config(shape.uri, shape.uri, bytes);
    REQUIRE(ace::scene::add_cell(state.document(), state.registry(), ace::commands::image_kind_id,
                                 config, arbc::Affine::identity())
                .has_value());
  }

  // `cells()` returns membership order (bottom->top), so `cells[i]` is `shapes[i]`.
  const std::vector<Cell> cells = ace::scene::cells(state.document(), state.registry());
  REQUIRE(cells.size() == shapes.size());
  for (std::size_t i = 0; i < shapes.size(); ++i) {
    // A referenced image either way; the OWNED/BORROWED split is the URI-shape verdict, and the
    // native pixel grid is filled from the decoded bounds regardless of provenance.
    CHECK(cells[i].detail.source == DetailSource::ReferencedImage);
    CHECK(cells[i].detail.borrowed == shapes[i].borrowed);
    REQUIRE(cells[i].detail.native_pixels.has_value());
    CHECK(cells[i].detail.native_pixels->first == k_fixture_w);
    CHECK(cells[i].detail.native_pixels->second == k_fixture_h);
  }
}

// --- One action, one entry; owned cell selected + undone whole (Constraint 5) -

TEST_CASE(
    "paste_image: inserting a minted owned cell is ONE journal entry undone on its ObjectId") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "journal");
  const std::string bytes = fixture_bytes(fs);

  const ace::project::OwnedAsset owned =
      ace::project::mint_owned_asset(fs, state.layout(), bytes, "ppm");
  const std::string config = ace::commands::image_config(owned.uri, owned.uri, owned.bytes);

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

  // The minted cell classifies OWNED (the pasted-provenance readout, D11).
  const std::vector<Cell> cells = ace::scene::cells(state.document(), state.registry());
  REQUIRE(cells.size() == 1);
  CHECK(cells[0].detail.source == DetailSource::ReferencedImage);
  CHECK_FALSE(cells[0].detail.borrowed);
  // Read-only by construction: `org.arbc.image` overrides no `editable()`.
  arbc::Content* content = state.document().resolve(cells[0].id);
  REQUIRE(content != nullptr);
  CHECK(content->editable() == nullptr);

  // A single undo removes the cell whole, on its same ObjectId (D15).
  REQUIRE(ace::commands::undo(state).moved);
  CHECK(ace::scene::cells(state.document(), state.registry()).empty());
  CHECK(state.document().pin()->find_content(outcome.content) == nullptr);
}

// --- In-place save -> canonical-rebuild reopen round-trip (Constraint 6) ------

TEST_CASE("paste_image: an owned image round-trips through save + canonical-rebuild reopen") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "roundtrip";
  const std::string bytes = fixture_bytes(fs);
  const arbc::Affine placement = arbc::Affine::translation(3.0, 7.0);

  std::string owned_uri;
  {
    AppState state = session_with_composition(scratch, fs, "roundtrip");
    // Mint into the LIVE project's assets/ (the eager, at-paste-time mint) then insert + save.
    const ace::project::OwnedAsset owned =
        ace::project::mint_owned_asset(fs, state.layout(), bytes, "ppm");
    owned_uri = owned.uri;
    const std::string config = ace::commands::image_config(owned.uri, owned.uri, owned.bytes);
    REQUIRE(ace::scene::add_cell(state.document(), state.registry(), ace::commands::image_kind_id,
                                 config, placement)
                .has_value());
    REQUIRE(ace::commands::save_project(state, fs).has_value());
  } // released: workspace checkpointed + unmapped

  // Shed the workspace so the reopen MUST rebuild from canonical — the runtime image codec path
  // (gated on the factory), which proves the OWNED image serializes with `params.source` = the URI.
  std::error_code ec;
  std::filesystem::remove_all(ace::project::project_layout(root).workspace_dir, ec);

  // Reopen with the image kind registered so the codec reconstructs it live; the installed
  // FilesystemAssetSource re-resolves the OWNED project-relative URI to `<root>/assets/...`.
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
  CHECK_FALSE(
      cells[0].detail.borrowed); // still OWNED after the round-trip (URI is project-relative)

  // The owned reference survives as the authored project-relative URI, and the blob re-resolves
  // from `assets/` to native px.
  arbc::Content* content = reopened.value().document->resolve(cells[0].id);
  REQUIRE(content != nullptr);
  CHECK(content->external_asset_ref() == owned_uri);
  const std::optional<arbc::Rect> bounds = content->bounds();
  REQUIRE(bounds.has_value());
  CHECK(bounds->width() == static_cast<double>(k_fixture_w));
  CHECK(bounds->height() == static_cast<double>(k_fixture_h));
}

// --- Rendered output: the golden REUSE (§9) ----------------------------------

TEST_CASE(
    "paste_image: an owned pasted image composites to the SAME golden as the borrowed import") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const ace::project::ProjectLayout layout = ace::project::project_layout(scratch.root / "golden");
  const std::string bytes = fixture_bytes(fs);

  const ace::project::ProbeDocument probe = ace::project::build_probe_document();
  const arbc::Registry registry = image_registry();

  // Mint the fixture as an OWNED asset, then insert through the SAME native-scale placement the
  // borrowed golden test uses (a paste at composition centre (32,32) under identity: tx=26, ty=28).
  const ace::project::OwnedAsset owned = ace::project::mint_owned_asset(fs, layout, bytes, "ppm");
  const std::string config = ace::commands::image_config(owned.uri, owned.uri, owned.bytes);
  const arbc::expected<std::optional<arbc::Rect>, std::string> img_bounds =
      ace::scene::probe_bounds(registry, ace::commands::image_kind_id, config);
  REQUIRE(img_bounds.has_value());
  REQUIRE(img_bounds->has_value());
  const arbc::Affine placement = ace::interact::place_at_native_scale(
      arbc::Affine::identity(), arbc::Vec2{32.0, 32.0}, *img_bounds);
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

  // Byte-exact against the golden `editor.import.image` committed: owning the bytes does not
  // perturb the render (provenance differs, pixels do not) — so NO new golden is committed (§9).
  CHECK(ace_test::compare_golden("import_image_64x64.rgba8", image.pixels));
}
