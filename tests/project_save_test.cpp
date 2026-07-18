// editor.project.save — the canonical publish direction (D-save-1..5). These
// headless Catch2 units pin the L1 logic (the bulk of the DoD, docs
// 01-architecture §9): the `project.arbc` + `assets/` publish, the atomic replace,
// the content-addressed `FilesystemAssetSink` dedup / WriteFailed paths, the
// `SaveError` values, the `AppState` dirty model, and — reusing the load-fidelity
// golden — the save -> reload -> render round-trip. The workspace-backed sessions
// here exercise writer-thread capture racing the `HousekeepingThread`'s checkpoints
// under the ASan/TSan legs of the gate.

#include <ace/commands/app_state.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/project/save.hpp>
#include <ace/render/render.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/model.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "golden_support.hpp"

using ace::commands::AppState;
using ace::commands::Command;
using ace::commands::dispatch;
using ace::project::ProjectLayout;
using ace::project::SaveError;

namespace {

// The platform_test ScratchDir pattern (not a shared header): a unique temp dir
// wiped on construction and destruction.
struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_project_save_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// Build the probe graph into `doc` with the content TAGGED by its interned kind so
// the built-in solid codec serializes it (mirrors project_open_test's
// write_probe_canonical; a bare add_content leaves the kind at 0, which renders but
// does not save). A default KindBridge assigns the built-in solid a stable token, so
// save_project's own fresh bridge resolves it identically (D-save-3).
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

// A seeded built-in registry (what AppState holds; save builds its codec table off
// it, D-app_state-2).
arbc::Registry builtin_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  return registry;
}

} // namespace

TEST_CASE("save_project publishes project.arbc and ensures assets/ for a solid document") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "publish";
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  ace::project::OpenedProject& opened = *created;
  build_saveable_probe(*opened.document);
  const arbc::Registry registry = builtin_registry();

  const auto saved = ace::project::save_project(fs, opened.layout, *opened.document, registry);
  REQUIRE(saved.has_value());
  CHECK(saved.value().revision == opened.document->pin()->revision());
  CHECK(saved.value().assets_written == 0); // solid content hands no owned bytes to the sink

  CHECK(fs.exists(opened.layout.canonical));
  CHECK(fs.exists(opened.layout.assets_dir));
  const auto bytes = fs.read_file(opened.layout.canonical);
  REQUIRE(bytes.has_value());
  CHECK_FALSE(bytes.value().empty());

  // Parses as a canonical .arbc: a fresh open over the same root (workspace dropped)
  // rebuilds from it without a corrupt-document error.
  std::error_code ec;
  std::filesystem::remove_all(opened.layout.workspace_dir, ec);
  auto reopened = ace::project::open_project(fs, root);
  REQUIRE(reopened.has_value());
  CHECK(reopened.value().rebuilt_from_canonical);
}

TEST_CASE("a second save_project replaces project.arbc whole with no surviving temp sibling") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "atomic";
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  ace::project::OpenedProject& opened = *created;
  build_saveable_probe(*opened.document);
  const arbc::Registry registry = builtin_registry();

  REQUIRE(ace::project::save_project(fs, opened.layout, *opened.document, registry).has_value());
  const auto first = fs.read_file(opened.layout.canonical);
  REQUIRE(first.has_value());

  // Re-publishing replaces the whole file (atomic_replace: temp sibling + rename).
  REQUIRE(ace::project::save_project(fs, opened.layout, *opened.document, registry).has_value());
  const auto second = fs.read_file(opened.layout.canonical);
  REQUIRE(second.has_value());
  CHECK_FALSE(second.value().empty());

  // The temp sibling (`project.arbc.tmp`, native_platform.cpp) never survives a
  // successful publish — asserted through the FileSystem seam.
  CHECK_FALSE(fs.exists(opened.layout.canonical.string() + ".tmp"));
  const auto entries = fs.list_directory(root);
  REQUIRE(entries.has_value());
  for (const std::filesystem::path& entry : *entries) {
    CHECK(entry.extension() != ".tmp");
  }
}

TEST_CASE("FilesystemAssetSink writes content-addressed blobs write-if-absent") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  ace::project::FilesystemAssetSink sink(fs);
  const std::filesystem::path blob = scratch.root / "assets" / "3f" / "content-hash";
  const std::array<std::byte, 4> bytes{std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
                                       std::byte{0x04}};

  CHECK_FALSE(sink.contains(blob.string()));
  const auto first = sink.put(blob.string(), bytes);
  REQUIRE(first.has_value());
  CHECK(*first); // actually written
  CHECK(sink.blobs_written() == 1);
  CHECK(fs.exists(blob));
  CHECK(sink.contains(blob.string()));

  // A second put of the same name is a dedup no-op: not written, counter unchanged.
  const auto second = sink.put(blob.string(), bytes);
  REQUIRE(second.has_value());
  CHECK_FALSE(*second);
  CHECK(sink.blobs_written() == 1);
}

TEST_CASE("FilesystemAssetSink surfaces an unwritable target as WriteFailed") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  // A regular file where a directory is needed: creating a child dir under it faults
  // (ENOTDIR), so the blob's parent cannot be made.
  const std::filesystem::path file_as_dir = scratch.root / "occupied";
  REQUIRE_FALSE(static_cast<bool>(fs.write_file(file_as_dir, "x")));
  ace::project::FilesystemAssetSink sink(fs);
  const std::array<std::byte, 1> bytes{std::byte{0x00}};

  const auto put = sink.put((file_as_dir / "child" / "blob").string(), bytes);
  REQUIRE_FALSE(put.has_value());
  CHECK(put.error().kind == arbc::AssetSinkError::Kind::WriteFailed);
}

TEST_CASE("save_project surfaces an unwritable root as an error value") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  // The root is a regular file, so ensuring `assets/` beneath it faults.
  const std::filesystem::path root = scratch.root / "root-as-file";
  REQUIRE_FALSE(static_cast<bool>(fs.write_file(root, "x")));
  const ProjectLayout layout = ace::project::project_layout(root);
  arbc::Document doc;
  const arbc::Registry registry = builtin_registry();

  const auto saved = ace::project::save_project(fs, layout, doc, registry);
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error() == SaveError::IoError);
}

TEST_CASE("save_project surfaces a serialize failure as SerializeFailed") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "serfail";
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  ace::project::OpenedProject& opened = *created;
  arbc::Document& doc = *opened.document;

  // A non-finite canvas extent faults the writer (SerializeError::NonFiniteValue),
  // which maps to SaveError::SerializeFailed — asserted as a value, never thrown.
  const arbc::ObjectId comp = doc.add_composition(std::nan(""), std::nan(""));
  arbc::KindBridge bridge;
  const std::uint64_t solid_kind = bridge.intern(arbc::SolidContent::kind_id, "");
  const arbc::ObjectId content = doc.add_content(
      std::make_shared<arbc::SolidContent>(ace::project::k_probe_color), solid_kind);
  doc.attach_layer(comp, doc.add_layer(content, arbc::Affine::identity()));
  const arbc::Registry registry = builtin_registry();

  const auto saved = ace::project::save_project(fs, opened.layout, doc, registry);
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error() == SaveError::SerializeFailed);
}

TEST_CASE("commands::save_project surfaces a publish failure and leaves the session dirty") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "pubfail";
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  build_saveable_probe(state.document());

  // A directory sitting at the canonical path makes the atomic rename-over fail
  // AFTER a clean serialize — exercising the publish IoError path.
  std::error_code ec;
  std::filesystem::create_directories(state.layout().canonical, ec);

  const auto saved = ace::commands::save_project(state, fs);
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error() == SaveError::IoError);
  CHECK(state.is_dirty()); // a failed publish never marks the session clean
}

TEST_CASE("a fresh create_project session is dirty until the first save") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "dirty";
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));

  CHECK(state.is_dirty()); // no known-published snapshot this session

  const auto saved = ace::commands::save_project(state, fs);
  REQUIRE(saved.has_value());
  CHECK_FALSE(state.is_dirty());
  CHECK(fs.exists(state.layout().canonical));

  // A revision-bumping command re-dirties (the published baseline is now stale).
  dispatch(state, Command{"add_content", [](arbc::Document& doc) {
                            doc.add_content(std::make_shared<arbc::SolidContent>(
                                arbc::Rgba{0.0F, 0.5F, 0.0F, 1.0F}));
                          }});
  CHECK(state.is_dirty());
}

TEST_CASE("a session rebuilt from project.arbc is clean at construction") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "rebuilt";

  // Stand up a canonical to rebuild from: create, save, then release the workspace.
  {
    auto created = ace::project::create_project(fs, root);
    REQUIRE(created.has_value());
    AppState seed(std::move(*created));
    REQUIRE(ace::commands::save_project(seed, fs).has_value());
  } // seed destructs → workspace file released, its HousekeepingThread joined

  std::error_code ec;
  std::filesystem::remove_all(ace::project::project_layout(root).workspace_dir, ec);
  auto reopened = ace::project::open_project(fs, root);
  REQUIRE(reopened.has_value());
  REQUIRE(reopened.value().rebuilt_from_canonical);

  AppState state(std::move(*reopened));
  CHECK_FALSE(state.is_dirty()); // the workspace was just built from project.arbc
}

TEST_CASE("save_project's dump reloads and renders byte-exact against the probe golden") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "roundtrip";
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  ace::project::OpenedProject& opened = *created;
  build_saveable_probe(*opened.document);
  const arbc::Registry registry = builtin_registry();

  REQUIRE(ace::project::save_project(fs, opened.layout, *opened.document, registry).has_value());

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
