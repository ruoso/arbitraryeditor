// editor.project.gc — Clean up (GC): reclaim orphaned owned asset bytes (D13 /
// docs 00 §8 / D-gc-1..5). These headless Catch2 units pin the L1 logic (the bulk
// of the DoD, docs 01-architecture §9): the dry-run/commit plan, the no-canonical
// guard (Constraint 2), the fail-safe mapping (Constraint 7), the `assets/`-only
// contract (Constraint 4), the `commands` orchestrator's not-a-transaction
// invariants (Constraint 3), and — reusing the probe golden — the
// GC-preserves-the-canonical round-trip. Because the editor cannot yet mint owned
// tiles (paint is not a dependency), the tile fixtures are HAND-AUTHORED on-disk state:
// a minimal `project.arbc` carrying a `params.blobs` hash list plus blob files
// written directly under `assets/tiles/` (the reaper reads the on-disk canonical's
// TEXT, asset_gc.cpp:83-116, so no live tile-bearing Document is needed).
//
// editor.project.gc_owned_images: the OWNED-IMAGE cases below pin the second key space
// arbc#30 delivered (v0.5.0) — a pasted image is content-addressed under `assets/images/`
// and rooted by the live cell's `params.source`, not `params.blobs`. Those blobs are NOT
// hand-authored: they are minted through the SHIPPED paste verb `project::mint_owned_asset`
// (Constraint 2 / D-gc_owned_images-2), reusing `paste_image_test`'s fixture bytes so the
// on-disk key scheme is the exact one paste emits. The canonical's `params.source` carries
// the URI that mint returned, never a hand-spelled scheme. The cases assert the safe
// direction and presence/absence, never exact reclaim counts on colliding basenames
// (Constraint 4 / D-gc_owned_images-3): the library roots an owned blob over-approximately
// (a shared basename roots it) because retaining an orphan leaks while missing a live
// reference is data loss.

#include <ace/commands/app_state.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/result.hpp>
#include <ace/project/gc.hpp>
#include <ace/project/import_asset.hpp>
#include <ace/project/project.hpp>
#include <ace/project/save.hpp>
#include <ace/render/render.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp> // arbc::KindBridge (the probe kind-token bridge)
#include <arbc/serialize/tile_blob.hpp>        // arbc::k_tile_hash_chars (a valid tile-hash width)

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "golden_support.hpp"

using ace::commands::AppState;
using ace::project::GcError;
using ace::project::GcOutcome;
using ace::project::ProjectLayout;

namespace {

// The platform_test ScratchDir pattern (mirroring project_save_test): a unique temp
// dir wiped on construction and destruction.
struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_project_gc_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// A well-formed tile hash: `k_tile_hash_chars` (32) lowercase hex chars. Distinct
// `fill` chars produce distinct valid hashes — how a referenced vs. orphan blob is
// distinguished without the paint pipeline that would mint real ones.
std::string tile_hash(char fill) { return std::string(arbc::k_tile_hash_chars, fill); }

// Write a minimal canonical `project.arbc` referencing `blobs` in one raster-style
// layer's `params.blobs` and one image-style layer per owned-image URI in `sources`'
// `params.source` — the two key spaces the library mark walk harvests
// (asset_gc.cpp:83-116), hand-authored as raw JSON so the L1 test pulls no JSON lib.
// The `sources` URIs are the REAL URIs `mint_owned_asset` returned (Constraint 2 /
// D-gc_owned_images-2), never a hand-spelled `assets/images/` scheme.
void write_canonical(const std::filesystem::path& root, const std::vector<std::string>& blobs,
                     const std::vector<std::string>& sources = {}) {
  std::string arr;
  for (std::size_t i = 0; i < blobs.size(); ++i) {
    if (i != 0) {
      arr += ',';
    }
    arr += '"';
    arr += blobs[i];
    arr += '"';
  }
  std::string layers = "{\"params\":{\"blobs\":[" + arr + "]}}";
  for (const std::string& source : sources) {
    layers += ",{\"params\":{\"source\":\"" + source + "\"}}";
  }
  const std::string json = "{\"composition\":{\"layers\":[" + layers + "]}}";
  std::ofstream(root / "project.arbc", std::ios::binary) << json;
}

// Drop a blob into the two-hex fan-out slot the store derives, as if a prior save
// had written it (mirrors asset_gc.t.cpp's ProjectDir::write_blob).
void write_blob(const std::filesystem::path& root, const std::string& hash,
                const std::string& content) {
  const std::filesystem::path dir = root / "assets" / "tiles" / hash.substr(0, 2);
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::ofstream(dir / hash, std::ios::binary) << content;
}

bool blob_exists(const std::filesystem::path& root, const std::string& hash) {
  return std::filesystem::exists(root / "assets" / "tiles" / hash.substr(0, 2) / hash);
}

// The paste fixture reused as clipboard bytes (paste_image_test's `photo_12x8.ppm`): a small
// Netpbm PPM the vendored decoder handles. Distinct byte payloads content-address to distinct
// hashes — hence distinct owned-image basenames, so a referenced and an orphan owned image never
// collide under the over-approximate matching policy (D-gc_owned_images-3).
const std::filesystem::path k_image_fixture =
    std::filesystem::path(ACE_FIXTURE_DIR) / "photo_12x8.ppm";

std::string fixture_bytes(const ace::platform::FileSystem& fs) {
  const ace::platform::Result<std::string> read = fs.read_file(k_image_fixture);
  REQUIRE(read.has_value());
  return *read;
}

// Mint an OWNED image through the SHIPPED paste verb (Constraint 2 / D-gc_owned_images-2):
// content-address `bytes` and write them into `<root>/assets/images/<xx>/<hash>.ppm`, returning
// the project-relative `params.source` URI paste authors onto the cell — the exact key scheme,
// not a hand-authored synthetic blob.
ace::project::OwnedAsset mint_owned_image(const ace::platform::FileSystem& fs,
                                          const ProjectLayout& layout, std::string_view bytes) {
  return ace::project::mint_owned_asset(fs, layout, bytes, "ppm");
}

// Does the owned-image blob named by a minted project-relative URI still exist under `assets/`?
bool owned_blob_exists(const std::filesystem::path& root, const std::string& uri) {
  return std::filesystem::exists(root / uri);
}

// Every regular file under `<root>/assets/`, project-relative and sorted — a byte-level snapshot
// of the owned-asset store for "assets/ unchanged" assertions.
std::vector<std::string> asset_tree(const std::filesystem::path& root) {
  std::vector<std::string> files;
  const std::filesystem::path assets = root / "assets";
  std::error_code ec;
  if (!std::filesystem::exists(assets, ec)) {
    return files;
  }
  for (std::filesystem::recursive_directory_iterator it(assets, ec), end; it != end;
       it.increment(ec)) {
    if (it->is_regular_file(ec)) {
      files.push_back(it->path().lexically_relative(root).generic_string());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Build the probe graph into `doc` TAGGED by its interned kind so the built-in
// solid codec serializes it (mirrors project_save_test::build_saveable_probe).
void build_saveable_probe(arbc::Document& doc) {
  const arbc::ObjectId composition =
      doc.add_composition(static_cast<double>(ace::project::k_probe_width),
                          static_cast<double>(ace::project::k_probe_height));
  arbc::KindBridge bridge;
  const std::uint64_t solid_kind = bridge.intern(arbc::SolidContent::kind_id, "");
  const arbc::ObjectId content = doc.add_content(
      std::make_shared<arbc::SolidContent>(ace::project::k_probe_color), solid_kind);
  const arbc::ObjectId layer = doc.add_layer(content, arbc::Affine::identity());
  doc.attach_layer(composition, layer);
}

arbc::Registry builtin_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  return registry;
}

} // namespace

TEST_CASE("gc_project reclaims unreferenced blobs; dry-run and commit compute one plan") {
  ScratchDir scratch;
  const std::filesystem::path root = scratch.root / "reclaim";
  std::filesystem::create_directories(root);
  const std::string hash_a = tile_hash('a');
  const std::string hash_b = tile_hash('b');
  write_canonical(root, {hash_a});       // the canonical references only A
  write_blob(root, hash_a, "AAAA");      // referenced
  const std::string b_bytes = "BBBBBBB"; // 7 bytes
  write_blob(root, hash_b, b_bytes);     // orphaned

  const ProjectLayout layout = ace::project::project_layout(root);

  // Dry-run: reports the exact plan, deletes nothing.
  const auto preview = ace::project::gc_project(layout, /*dry_run=*/true);
  REQUIRE(preview.has_value());
  CHECK(preview.value() == GcOutcome{2, 1, 1, b_bytes.size()});
  CHECK(blob_exists(root, hash_a));
  CHECK(blob_exists(root, hash_b)); // previewed, not touched

  // Commit: the identical plan reclaims exactly the orphan, keeps the referenced.
  const auto swept = ace::project::gc_project(layout, /*dry_run=*/false);
  REQUIRE(swept.has_value());
  CHECK(swept.value() == preview.value());
  CHECK(blob_exists(root, hash_a));
  CHECK_FALSE(blob_exists(root, hash_b));
}

TEST_CASE("gc_project no-ops without a canonical project.arbc, sweeping nothing (Constraint 2)") {
  ScratchDir scratch;
  const std::filesystem::path root = scratch.root / "no_canonical";
  std::filesystem::create_directories(root);
  const std::string hash_a = tile_hash('a');
  write_blob(root, hash_a, "AAAA"); // with zero roots the library would reclaim THIS

  const ProjectLayout layout = ace::project::project_layout(root);
  REQUIRE_FALSE(std::filesystem::exists(layout.canonical));

  const auto out = ace::project::gc_project(layout, /*dry_run=*/false);
  REQUIRE(out.has_value());
  CHECK(out.value() == GcOutcome{}); // {0,0,0,0}: no sweep
  CHECK(blob_exists(root, hash_a));  // assets/ byte-unchanged (the guard, not the lib)
}

TEST_CASE("gc_project fails safe on an unparseable canonical, deleting nothing (Constraint 7)") {
  ScratchDir scratch;
  const std::filesystem::path root = scratch.root / "fail_safe";
  std::filesystem::create_directories(root);
  const std::string hash_a = tile_hash('a');
  write_blob(root, hash_a, "AAAA");
  std::ofstream(root / "project.arbc", std::ios::binary) << "{ not valid json ";

  const ProjectLayout layout = ace::project::project_layout(root);
  const auto out = ace::project::gc_project(layout, /*dry_run=*/false);
  REQUIRE_FALSE(out.has_value());
  CHECK(out.error() == GcError::MarkFailed); // mapped from arbc::GcError::MarkFailed
  CHECK(blob_exists(root, hash_a));          // fail-safe: nothing deleted
}

TEST_CASE("gc_project touches assets/ only, leaving workspace/ byte-unchanged (Constraint 4)") {
  ScratchDir scratch;
  const std::filesystem::path root = scratch.root / "assets_only";
  std::filesystem::create_directories(root);
  const std::string hash_a = tile_hash('a');
  const std::string hash_b = tile_hash('b');
  write_canonical(root, {hash_a});
  write_blob(root, hash_a, "AAAA");
  write_blob(root, hash_b, "BBBB"); // orphan

  // A workspace/ scratch file the sweep must not touch (never GC'd, D13/§9).
  std::filesystem::create_directories(root / "workspace");
  const std::filesystem::path ws = root / "workspace" / "document.arbcws";
  std::ofstream(ws, std::ios::binary) << "workspace-bytes";

  const ProjectLayout layout = ace::project::project_layout(root);
  REQUIRE(ace::project::gc_project(layout, /*dry_run=*/false).has_value());

  CHECK_FALSE(blob_exists(root, hash_b)); // the orphan under assets/ was reclaimed
  REQUIRE(std::filesystem::exists(ws));
  CHECK(read_text(ws) == "workspace-bytes"); // workspace/ byte-unchanged
}

TEST_CASE(
    "commands::gc_project leaves dirty state, revision, and layout unchanged (Constraint 3)") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "invariants";
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  build_saveable_probe(state.document());
  // Publish so a canonical exists for the sweep to root on (solid content writes no
  // blobs, so the sweep reclaims nothing — this is a not-a-transaction assertion).
  REQUIRE(ace::commands::save_project(state, fs).has_value());

  const bool dirty_before = state.is_dirty();
  const std::uint64_t rev_before = state.document().pin()->revision();
  const std::filesystem::path canonical_before = state.layout().canonical;

  const auto out = ace::commands::gc_project(state, /*dry_run=*/false);
  REQUIRE(out.has_value());
  // The orchestrator result matches the project-level sweep over the same layout.
  const auto direct = ace::project::gc_project(state.layout(), /*dry_run=*/false);
  REQUIRE(direct.has_value());
  CHECK(out.value() == direct.value());

  // GC is a maintenance op, never a document edit: no re-dirty, no revision bump,
  // no layout re-point (D13/D15).
  CHECK(state.is_dirty() == dirty_before);
  CHECK(state.document().pin()->revision() == rev_before);
  CHECK(state.layout().canonical == canonical_before);
}

TEST_CASE("gc_project preserves the canonical: save -> gc -> reopen renders byte-exact") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "golden";
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  ace::project::OpenedProject& opened = *created;
  build_saveable_probe(*opened.document);
  const arbc::Registry registry = builtin_registry();
  REQUIRE(ace::project::save_project(fs, opened.layout, *opened.document, registry).has_value());

  // A real sweep: the probe hands no bytes to the sink, so GC reclaims nothing here
  // — the assertion pins that a sweep leaves the canonical + its render intact.
  const auto swept = ace::project::gc_project(opened.layout, /*dry_run=*/false);
  REQUIRE(swept.has_value());
  CHECK(swept.value().deleted == 0);

  // Force rebuild-from-canonical: drop the workspace so open reloads project.arbc.
  std::error_code ec;
  std::filesystem::remove_all(opened.layout.workspace_dir, ec);
  auto reopened = ace::project::open_project(fs, root);
  REQUIRE(reopened.has_value());
  REQUIRE(reopened.value().rebuilt_from_canonical);

  const ace::render::Srgb8Image image = ace::render::render_document_srgb8(
      *reopened.value().document, ace::project::k_probe_width, ace::project::k_probe_height);
  REQUIRE(image.width == ace::project::k_probe_width);
  REQUIRE(image.height == ace::project::k_probe_height);
  const std::string golden =
      "render_probe_" + std::to_string(image.width) + "x" + std::to_string(image.height) + ".rgba8";
  CHECK(ace_test::compare_golden(golden, image.pixels));
}

// --- editor.project.gc_owned_images: the arbc#30 owned-image key space (assets/images/ +
//     params.source), minted through the real paste verb (Constraint 2). ---------------------

TEST_CASE("gc_project roots a referenced owned image, leaving its assets/images/ blob untouched") {
  // THE DATA-LOSS-CRITICAL DIRECTION: a Clean-Up over a live pasted image must not sweep its
  // blob, or the cell points at nothing after reopen. Unsatisfiable pre-v0.5.0 (the reaper never
  // looked under assets/images/); arbc#30 exposes the editor to the failure mode this guards.
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "owned_rooted";
  std::filesystem::create_directories(root);
  const ProjectLayout layout = ace::project::project_layout(root);

  const ace::project::OwnedAsset owned = mint_owned_image(fs, layout, fixture_bytes(fs));
  write_canonical(root, {}, {owned.uri}); // the live cell references the owned blob
  REQUIRE(owned_blob_exists(root, owned.uri));

  // Dry-run: the owned image is rooted (referenced set is no longer tiles-only), nothing deleted.
  const auto preview = ace::project::gc_project(layout, /*dry_run=*/true);
  REQUIRE(preview.has_value());
  CHECK(preview.value() == GcOutcome{1, 1, 0, 0}); // scanned 1 image, referenced 1, deleted 0
  CHECK(owned_blob_exists(root, owned.uri));

  // Commit: the identical plan, and the blob is STILL on disk (the safe direction).
  const auto swept = ace::project::gc_project(layout, /*dry_run=*/false);
  REQUIRE(swept.has_value());
  CHECK(swept.value() == preview.value());
  CHECK(swept.value().deleted == 0);
  CHECK(owned_blob_exists(root, owned.uri));
}

TEST_CASE("gc_project reclaims an orphaned owned image after paste->undo->save->Clean-Up") {
  // The blob is on disk (an at-paste-time mint the sink never deletes) but the saved canonical no
  // longer references it — the undo dropped the cell before the save (A23: the sink never deletes,
  // GC is the reaper). Assert presence/absence, not an exact byte count (Constraint 4).
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "owned_orphan";
  std::filesystem::create_directories(root);
  const ProjectLayout layout = ace::project::project_layout(root);

  const ace::project::OwnedAsset owned = mint_owned_image(fs, layout, fixture_bytes(fs));
  write_canonical(root, {}); // the undone cell is gone: the canonical references nothing
  REQUIRE(owned_blob_exists(root, owned.uri));

  // Dry-run and commit compute one plan; the orphan survives a preview, is gone after commit.
  const auto preview = ace::project::gc_project(layout, /*dry_run=*/true);
  REQUIRE(preview.has_value());
  CHECK(preview.value().deleted == 1);
  CHECK(owned_blob_exists(root, owned.uri)); // previewed, not touched

  const auto swept = ace::project::gc_project(layout, /*dry_run=*/false);
  REQUIRE(swept.has_value());
  CHECK(swept.value() == preview.value());
  CHECK_FALSE(owned_blob_exists(root, owned.uri)); // the orphan is reclaimed
}

TEST_CASE("gc_project roots owned images and tiles together in one pass") {
  // The union arbc#30 delivered: one live tile (params.blobs) and one live owned image
  // (params.source), plus one orphan of each kind. A regression that marked one store while
  // sweeping the other's live blob would fail here.
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "union";
  std::filesystem::create_directories(root);
  const ProjectLayout layout = ace::project::project_layout(root);

  const std::string tile_live = tile_hash('a');
  const std::string tile_orphan = tile_hash('b');
  write_blob(root, tile_live, "LIVE");
  write_blob(root, tile_orphan, "ORPHANBYTES");

  const std::string bytes = fixture_bytes(fs);
  const ace::project::OwnedAsset img_live = mint_owned_image(fs, layout, bytes);
  const ace::project::OwnedAsset img_orphan = mint_owned_image(fs, layout, bytes + "orphan");
  REQUIRE(img_live.uri != img_orphan.uri); // distinct bytes -> distinct basenames (no collision)

  write_canonical(root, {tile_live}, {img_live.uri}); // both live refs, in the two key spaces

  const auto swept = ace::project::gc_project(layout, /*dry_run=*/false);
  REQUIRE(swept.has_value());
  // Both live blobs survive; both orphans — one per store — are gone in the single pass.
  CHECK(blob_exists(root, tile_live));
  CHECK(owned_blob_exists(root, img_live.uri));
  CHECK_FALSE(blob_exists(root, tile_orphan));
  CHECK_FALSE(owned_blob_exists(root, img_orphan.uri));
}

TEST_CASE("gc_project never enumerates an image referenced outside the owned subtree") {
  // An external absolute URI (the borrowed-import shape) points OUTSIDE assets/images/, so the
  // reaper never enumerates it — it can be neither rooted nor reclaimed. The coexisting owned
  // blob (rooted) proves assets/ is byte-unchanged, and the external reference still resolves
  // on reopen (GC never rewrites the canonical). (Constraint 5.)
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "external_ref";
  std::filesystem::create_directories(root);
  const ProjectLayout layout = ace::project::project_layout(root);

  const ace::project::OwnedAsset owned = mint_owned_image(fs, layout, fixture_bytes(fs));
  const std::string external_uri = "file:///photos/holiday.png";
  write_canonical(root, {}, {owned.uri, external_uri});
  const std::string canonical_before = read_text(root / "project.arbc");
  const std::vector<std::string> assets_before = asset_tree(root);

  const auto swept = ace::project::gc_project(layout, /*dry_run=*/false);
  REQUIRE(swept.has_value());
  CHECK(swept.value().deleted == 0);                           // the external URI reclaims nothing
  CHECK(asset_tree(root) == assets_before);                    // assets/ byte-unchanged
  CHECK(owned_blob_exists(root, owned.uri));                   // the owned blob survived
  CHECK(read_text(root / "project.arbc") == canonical_before); // the reference still resolves
}

TEST_CASE("gc_project fails safe on a malformed params.source, deleting nothing (Constraint 7)") {
  // A v0.5.0 consequence: a `params.source` that is present but not a string is a MARK failure
  // (over-preservation on any doubt) where v0.4.1 would have swept. Surfaces project::GcError
  // (MarkFailed) with nothing deleted; the gateway maps this to ran=false / nothing reclaimed
  // (project_gateway.cpp:197-211). Assert the safe direction only, not the message.
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "malformed_source";
  std::filesystem::create_directories(root);
  const ProjectLayout layout = ace::project::project_layout(root);

  const ace::project::OwnedAsset owned = mint_owned_image(fs, layout, fixture_bytes(fs));
  // A non-string `params.source` — the mark walk rejects it (asset_gc.cpp:109) before any sweep.
  std::ofstream(root / "project.arbc", std::ios::binary)
      << "{\"composition\":{\"layers\":[{\"params\":{\"source\":123}}]}}";

  const auto out = ace::project::gc_project(layout, /*dry_run=*/false);
  REQUIRE_FALSE(out.has_value());
  CHECK(out.error() == GcError::MarkFailed);
  CHECK(owned_blob_exists(root, owned.uri)); // fail-safe: nothing deleted
}
