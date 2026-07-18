// editor.project.gc — Clean up (GC): reclaim orphaned owned asset bytes (D13 /
// docs 00 §8 / D-gc-1..5). These headless Catch2 units pin the L1 logic (the bulk
// of the DoD, docs 01-architecture §9): the dry-run/commit plan, the no-canonical
// guard (Constraint 2), the fail-safe mapping (Constraint 7), the `assets/`-only
// contract (Constraint 4), the `commands` orchestrator's not-a-transaction
// invariants (Constraint 3), and — reusing the probe golden — the
// GC-preserves-the-canonical round-trip. Because the editor cannot yet mint owned
// tiles (paint is not a dependency), the fixtures are HAND-AUTHORED on-disk state:
// a minimal `project.arbc` carrying a `params.blobs` hash list plus blob files
// written directly under `assets/tiles/` (the reaper reads the on-disk canonical's
// TEXT, asset_gc.hpp:84-90, so no live tile-bearing Document is needed).

#include <ace/commands/app_state.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/gc.hpp>
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

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
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

// Write a minimal canonical `project.arbc` whose one content body references
// `blobs` in `params.blobs` — the exact SHAPE the library mark walk keys on
// (asset_gc.hpp:84-90), hand-authored as raw JSON so the L1 test pulls no JSON lib.
void write_canonical(const std::filesystem::path& root, const std::vector<std::string>& blobs) {
  std::string arr;
  for (std::size_t i = 0; i < blobs.size(); ++i) {
    if (i != 0) {
      arr += ',';
    }
    arr += '"';
    arr += blobs[i];
    arr += '"';
  }
  const std::string json =
      "{\"composition\":{\"layers\":[{\"params\":{\"blobs\":[" + arr + "]}}]}}";
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
