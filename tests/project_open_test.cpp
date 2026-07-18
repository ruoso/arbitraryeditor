// editor.project.open — L1 headless units for directory→Document open/create.
// Reuses the ScratchDir filesystem-round-trip pattern (tests/platform_test.cpp)
// and the render_probe golden (tests/goldens/render_probe_64x64.rgba8) for load
// fidelity. The workspace-backed cases mint libarbc's first checkpointable editor
// Document (its live HousekeepingThread) and run under ASan/TSan (A4/§9).

#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/render/render.hpp>

#include <arbc/base/transform.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/model.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "golden_support.hpp"

using ace::project::OpenError;
using ace::project::ProjectLayout;

namespace {

// A temp dir wiped on entry and exit (the platform_test pattern), with a name
// distinct from the platform suite's so the two never collide in one binary.
struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_project_open_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// Delegates to NativeFileSystem but can be told to fail one chosen operation, so
// open/create's IoError value-branches (which a healthy native filesystem never
// takes) are exercised. `noop_mkdir` reports success without creating anything —
// which makes libarbc's `Document::create` fail on the absent workspace subtree.
class FaultyFileSystem final : public ace::platform::FileSystem {
public:
  enum class Op { None, MakeDirectories, AtomicReplace, ReadFile };
  Op fail_op = Op::None;
  bool noop_mkdir = false;

  bool exists(const std::filesystem::path& path) const override { return native_.exists(path); }

  ace::platform::Result<std::vector<std::filesystem::path>>
  list_directory(const std::filesystem::path& dir) const override {
    return native_.list_directory(dir);
  }

  ace::platform::Result<std::string> read_file(const std::filesystem::path& path) const override {
    if (fail_op == Op::ReadFile) {
      return std::make_error_code(std::errc::io_error);
    }
    return native_.read_file(path);
  }

  std::error_code write_file(const std::filesystem::path& path,
                             std::string_view contents) const override {
    return native_.write_file(path, contents);
  }

  std::error_code make_directories(const std::filesystem::path& dir) const override {
    if (fail_op == Op::MakeDirectories) {
      return std::make_error_code(std::errc::io_error);
    }
    if (noop_mkdir) {
      return {};
    }
    return native_.make_directories(dir);
  }

  std::error_code atomic_replace(const std::filesystem::path& path,
                                 std::string_view contents) const override {
    if (fail_op == Op::AtomicReplace) {
      return std::make_error_code(std::errc::io_error);
    }
    return native_.atomic_replace(path, contents);
  }

private:
  ace::platform::NativeFileSystem native_;
};

// Write a canonical project.arbc synthesized from a probe-equivalent document (the
// save direction proper lives in editor.project.save; here it only stands up a
// fixture). The graph mirrors build_probe_document — one full-frame solid of
// k_probe_color at identity — so a rebuild renders byte-identically to the probe
// golden, but the content is tagged with its interned kind so the built-in solid
// codec serializes it (build_probe_document leaves the kind at 0, which is fine
// for rendering, which resolves content by pointer, but not for the save path).
void write_probe_canonical(const ace::platform::FileSystem& fs, const ProjectLayout& layout) {
  arbc::Document doc;
  const arbc::ObjectId composition =
      doc.add_composition(static_cast<double>(ace::project::k_probe_width),
                          static_cast<double>(ace::project::k_probe_height));
  arbc::KindBridge bridge;
  // A repeat intern of a pre-interned built-in returns its stable token and keeps
  // the pinned kind_version, so the empty version here is ignored.
  const std::uint64_t solid_kind = bridge.intern(arbc::SolidContent::kind_id, "");
  const arbc::ObjectId content = doc.add_content(
      std::make_shared<arbc::SolidContent>(ace::project::k_probe_color), solid_kind);
  const arbc::ObjectId layer = doc.add_layer(content, arbc::Affine::identity());
  doc.attach_layer(composition, layer);

  const auto bytes = arbc::save_document(doc, bridge);
  REQUIRE(bytes.has_value());
  REQUIRE(!fs.write_file(layout.canonical, *bytes));
}

} // namespace

TEST_CASE("create_project scaffolds the bundle and mints a workspace-backed document") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "fresh";
  const ProjectLayout layout = ace::project::project_layout(root);

  const auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());

  CHECK(fs.exists(layout.assets_dir));
  CHECK(fs.exists(layout.workspace_dir));
  CHECK(fs.exists(layout.exports_dir));
  CHECK(fs.exists(layout.gitignore));

  const auto gitignore = fs.read_file(layout.gitignore);
  REQUIRE(gitignore.has_value());
  CHECK(gitignore.value().find("workspace/") != std::string::npos);

  CHECK(created.value().document->workspace_backed());
  CHECK_FALSE(created.value().rebuilt_from_canonical);
  // No project.arbc is written — that is editor.project.save's publish step.
  CHECK_FALSE(fs.exists(layout.canonical));
}

TEST_CASE("open_project maps the crash-durable workspace on reopen") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "roundtrip";

  {
    auto created = ace::project::create_project(fs, root);
    REQUIRE(created.has_value());
    arbc::Document& doc = *created.value().document;
    // A fresh project has no composition; add one and make it durable.
    doc.add_composition(static_cast<double>(ace::project::k_probe_width),
                        static_cast<double>(ace::project::k_probe_height));
    REQUIRE(doc.checkpoint().has_value());
  } // the first document is destroyed (workspace unmapped) before reopening

  const auto opened = ace::project::open_project(fs, root);
  REQUIRE(opened.has_value());
  CHECK_FALSE(opened.value().rebuilt_from_canonical); // mapped the workspace, not rebuilt

  // The checkpointed mutation (the composition) survived the reopen — proof the
  // crash-durable workspace, not an empty fresh document, was recovered.
  arbc::ObjectId composition{};
  const arbc::CompositionRecord* record = nullptr;
  CHECK(opened.value().document->pin()->find_first_composition(composition, record));
}

TEST_CASE("open_project rebuilds from the canonical project.arbc when the workspace is absent") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "rebuild";
  const ProjectLayout layout = ace::project::project_layout(root);

  REQUIRE(!fs.make_directories(root));
  write_probe_canonical(fs, layout);

  const auto opened = ace::project::open_project(fs, root);
  REQUIRE(opened.has_value());
  CHECK(opened.value().rebuilt_from_canonical);

  // The canonical bytes reconstructed the expected structure (the composition).
  arbc::ObjectId composition{};
  const arbc::CompositionRecord* record = nullptr;
  CHECK(opened.value().document->pin()->find_first_composition(composition, record));
}

TEST_CASE("open_project falls back to rebuild on an unusable workspace file") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "cross_machine";
  const ProjectLayout layout = ace::project::project_layout(root);

  REQUIRE(!fs.make_directories(root));
  write_probe_canonical(fs, layout);
  // A truncated/garbage workspace file, as if opened on another machine: present,
  // but not a mappable workspace.
  REQUIRE(!fs.make_directories(layout.workspace_dir));
  REQUIRE(!fs.write_file(layout.workspace_file, "not a workspace file"));

  const auto opened = ace::project::open_project(fs, root);
  REQUIRE(opened.has_value());
  CHECK(opened.value().rebuilt_from_canonical); // fell back, no error
}

TEST_CASE("open_project returns typed error values, never throws") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;

  SECTION("a non-directory path is NotADirectory") {
    const std::filesystem::path file = scratch.root / "a_file";
    REQUIRE(!fs.write_file(file, "hi"));
    const auto opened = ace::project::open_project(fs, file);
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error() == OpenError::NotADirectory);
  }

  SECTION("an empty directory with no workspace and no project.arbc is NoProject") {
    const std::filesystem::path root = scratch.root / "empty";
    REQUIRE(!fs.make_directories(root));
    const auto opened = ace::project::open_project(fs, root);
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error() == OpenError::NoProject);
  }

  SECTION("a corrupt project.arbc with the workspace removed is CorruptDocument") {
    const std::filesystem::path root = scratch.root / "corrupt";
    const ProjectLayout layout = ace::project::project_layout(root);
    REQUIRE(!fs.make_directories(root));
    REQUIRE(!fs.write_file(layout.canonical, "this is not a valid arbc document"));
    const auto opened = ace::project::open_project(fs, root);
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error() == OpenError::CorruptDocument);
  }
}

TEST_CASE("open/create surface filesystem faults as IoError values") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem native;

  SECTION("create_project: a directory-scaffold failure") {
    FaultyFileSystem fs;
    fs.fail_op = FaultyFileSystem::Op::MakeDirectories;
    const auto created = ace::project::create_project(fs, scratch.root / "p1");
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error() == OpenError::IoError);
  }

  SECTION("create_project: a .gitignore write failure") {
    FaultyFileSystem fs;
    fs.fail_op = FaultyFileSystem::Op::AtomicReplace;
    const auto created = ace::project::create_project(fs, scratch.root / "p2");
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error() == OpenError::IoError);
  }

  SECTION("create_project: a workspace-file mint failure") {
    FaultyFileSystem fs;
    fs.noop_mkdir = true; // dirs report success but nothing is created on disk
    const auto created = ace::project::create_project(fs, scratch.root / "p3");
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error() == OpenError::IoError);
  }

  SECTION("open_project: a canonical-read failure") {
    const std::filesystem::path root = scratch.root / "p4";
    const ProjectLayout layout = ace::project::project_layout(root);
    REQUIRE(!native.make_directories(root));
    REQUIRE(!native.write_file(layout.canonical, "irrelevant"));
    FaultyFileSystem fs;
    fs.fail_op = FaultyFileSystem::Op::ReadFile;
    const auto opened = ace::project::open_project(fs, root);
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error() == OpenError::IoError);
  }

  SECTION("open_project: a workspace-directory scaffold failure on rebuild") {
    const std::filesystem::path root = scratch.root / "p5";
    const ProjectLayout layout = ace::project::project_layout(root);
    REQUIRE(!native.make_directories(root));
    REQUIRE(!native.write_file(layout.canonical, "irrelevant"));
    FaultyFileSystem fs;
    fs.fail_op = FaultyFileSystem::Op::MakeDirectories;
    const auto opened = ace::project::open_project(fs, root);
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error() == OpenError::IoError);
  }
}

TEST_CASE("OpenError messages are populated for every value") {
  CHECK_FALSE(ace::project::make_error_code(OpenError::NotADirectory).message().empty());
  CHECK_FALSE(ace::project::make_error_code(OpenError::NoProject).message().empty());
  CHECK_FALSE(ace::project::make_error_code(OpenError::CorruptDocument).message().empty());
  CHECK_FALSE(ace::project::make_error_code(OpenError::IoError).message().empty());
}

TEST_CASE("a canonical-rebuilt document reconstructs pixel-identically (load fidelity)") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "fidelity";
  const ProjectLayout layout = ace::project::project_layout(root);

  REQUIRE(!fs.make_directories(root));
  write_probe_canonical(fs, layout);

  const auto opened = ace::project::open_project(fs, root);
  REQUIRE(opened.has_value());
  REQUIRE(opened.value().rebuilt_from_canonical);

  const ace::render::Srgb8Image image = ace::render::render_document_srgb8(
      *opened.value().document, ace::project::k_probe_width, ace::project::k_probe_height);
  REQUIRE(image.width == ace::project::k_probe_width);
  REQUIRE(image.height == ace::project::k_probe_height);
  const std::string golden =
      "render_probe_" + std::to_string(image.width) + "x" + std::to_string(image.height) + ".rgba8";
  CHECK(ace_test::compare_golden(golden, image.pixels));
}
