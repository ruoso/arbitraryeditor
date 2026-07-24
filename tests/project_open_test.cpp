// editor.project.open — L1 headless units for directory→Document open/create.
// Reuses the ScratchDir filesystem-round-trip pattern (tests/platform_test.cpp)
// and the render_probe golden (tests/goldens/render_probe_64x64.rgba8) for load
// fidelity. The workspace-backed cases mint libarbc's first checkpointable editor
// Document (its live HousekeepingThread) and run under ASan/TSan (A4/§9).

#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/content.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/model.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
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

  // A plain forwarder: `create_project`'s rollback is deferred to
  // editor.project.create_rollback, so this suite provokes no removal — the override exists
  // because the A26 primitive is pure (the compiler, not grep, re-signs every double).
  std::error_code remove_tree(const std::filesystem::path& path) const override {
    return native_.remove_tree(path);
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

// Scaffold a project whose CHECKPOINTED workspace holds one placed built-in content, then
// release it so the workspace file stays on disk with the content durable in it. The
// fixture the map-then-inspect guard is judged on (A19): `create_project` writes no
// `project.arbc`, so a caller decides separately whether a canonical floor exists
// (`write_probe_canonical`). Uses a built-in kind on purpose — the map path's inability to
// bind content is kind-agnostic, not a custom-kind quirk (D-slab_adopt-7).
void author_content_bearing_workspace(const ace::platform::FileSystem& fs,
                                      const std::filesystem::path& root,
                                      const arbc::Registry& registry) {
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  arbc::Document& doc = *created.value().document;
  doc.add_composition(static_cast<double>(ace::project::k_probe_width),
                      static_cast<double>(ace::project::k_probe_height));
  // Authored through the real insert path so the `ContentRecord`'s kind token is the
  // registry-seeded one (A16) — a hand-minted token of 0 is not what a project carries.
  REQUIRE(ace::scene::add_cell(doc, registry, "org.arbc.solid", "0,0.5,0,1",
                               arbc::Affine::translation(3.0, 4.0))
              .has_value());
  REQUIRE(doc.checkpoint().has_value());
} // released here: workspace unmapped, HousekeepingThread joined

// The built-in registry a document's cells are authored and read back through.
arbc::Registry builtin_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  return registry;
}

// How many of `document`'s layer-bound contents the binding table actually serves.
std::size_t bound_content_count(const arbc::Document& document) {
  std::size_t bound = 0;
  document.pin()->for_each_layer([&](const arbc::LayerRecord& layer) {
    if (layer.content.valid() && document.resolve(layer.content) != nullptr) {
      ++bound;
    }
  });
  return bound;
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

// --- editor.project.dir_is_project: creation means creation (D27 / D-dir_is_project-1) -----

TEST_CASE("create_project refuses a target that already exists, scaffolding nothing") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  std::error_code ec;

  // Each SECTION makes the target exist in a different way; the rule is exists-at-all, so
  // all four land on the same refusal and — the load-bearing half — leave the target
  // byte-for-byte as it was.
  std::filesystem::path root;
  SECTION("an EMPTY existing directory") {
    root = scratch.root / "empty_dir";
    REQUIRE(!fs.make_directories(root));
  }
  SECTION("a populated non-project directory") {
    root = scratch.root / "someones_documents";
    REQUIRE(!fs.make_directories(root));
    REQUIRE(!fs.write_file(root / "notes.txt", "unrelated work"));
  }
  SECTION("an existing project directory") {
    root = scratch.root / "already_a_project";
    REQUIRE(!fs.make_directories(root));
    write_probe_canonical(fs, ace::project::project_layout(root));
  }
  SECTION("a regular file") {
    root = scratch.root / "a_file";
    REQUIRE(!fs.write_file(root, "not a directory at all"));
  }

  // Snapshot the target's entire contents BEFORE, so "scaffolding nothing" is asserted
  // against what was there rather than against a hand-listed set of absences.
  std::vector<std::filesystem::path> before;
  if (std::filesystem::is_directory(root, ec)) {
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(root)) {
      before.push_back(entry.path());
    }
    std::sort(before.begin(), before.end());
  }
  const bool was_file = std::filesystem::is_regular_file(root, ec);
  std::string file_bytes;
  if (was_file) {
    const auto read = fs.read_file(root);
    REQUIRE(read.has_value());
    file_bytes = read.value();
  }

  const auto created = ace::project::create_project(fs, root);
  REQUIRE_FALSE(created.has_value());
  CHECK(created.error() == ace::project::make_error_code(OpenError::TargetExists));

  // Nothing was scaffolded: no assets/, no workspace/, no exports/, no .gitignore, and the
  // directory listing is exactly what it was.
  const ProjectLayout layout = ace::project::project_layout(root);
  CHECK_FALSE(fs.exists(layout.assets_dir));
  CHECK_FALSE(fs.exists(layout.workspace_dir));
  CHECK_FALSE(fs.exists(layout.exports_dir));
  CHECK_FALSE(fs.exists(layout.gitignore));
  if (was_file) {
    // A regular file at the target keeps its bytes — the refusal never truncated it.
    const auto after = fs.read_file(root);
    REQUIRE(after.has_value());
    CHECK(after.value() == file_bytes);
    CHECK(std::filesystem::is_regular_file(root, ec));
  } else {
    std::vector<std::filesystem::path> after;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(root)) {
      after.push_back(entry.path());
    }
    std::sort(after.begin(), after.end());
    CHECK(after == before);
  }
}

TEST_CASE("create_project still scaffolds a target that does not exist") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  // The guard must not be over-eager: a NOT-YET-EXISTING target under an existing parent is
  // the ordinary New/bootstrap case and still scaffolds. The parent exists (ScratchDir made
  // it) — only the project root itself must be absent.
  const std::filesystem::path root = scratch.root / "nested" / "still_fresh";
  REQUIRE_FALSE(fs.exists(root));

  const auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  const ProjectLayout layout = ace::project::project_layout(root);
  CHECK(fs.exists(layout.assets_dir));
  CHECK(fs.exists(layout.workspace_dir));
  CHECK(fs.exists(layout.exports_dir));
  CHECK(fs.exists(layout.gitignore));
  CHECK(created.value().document->workspace_backed());
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
  // Nothing was lost to the map: this workspace holds no content record at all, so the
  // guard's inspection finds nothing unbindable and the fast path survives (A19).
  CHECK(opened.value().unbindable_content_records == 0);

  // The checkpointed mutation (the composition) survived the reopen — proof the
  // crash-durable workspace, not an empty fresh document, was recovered.
  arbc::ObjectId composition{};
  const arbc::CompositionRecord* record = nullptr;
  CHECK(opened.value().document->pin()->find_first_composition(composition, record));
}

// --- editor.cameras.reopen_slab_adopt: what the workspace map actually recovers, and
// the map-then-inspect guard built on it (A19) ----------------------------------------

TEST_CASE("a workspace-mapped reopen restores the record graph but binds NO content") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "unbound";
  const ProjectLayout layout = ace::project::project_layout(root);

  const arbc::Registry registry = builtin_registry();
  arbc::ObjectId content{};
  arbc::ObjectId layer{};
  const arbc::Affine placement = arbc::Affine::translation(3.0, 4.0);
  {
    auto created = ace::project::create_project(fs, root);
    REQUIRE(created.has_value());
    arbc::Document& doc = *created.value().document;
    doc.add_composition(static_cast<double>(ace::project::k_probe_width),
                        static_cast<double>(ace::project::k_probe_height));
    REQUIRE(
        ace::scene::add_cell(doc, registry, "org.arbc.solid", "0,0.5,0,1", placement).has_value());
    const std::vector<ace::scene::Cell> live = ace::scene::cells(doc, registry);
    REQUIRE(live.size() == 1);
    content = live[0].id;
    layer = live[0].layer;
    REQUIRE(doc.resolve(content) != nullptr); // bound while the document is live
    REQUIRE(doc.checkpoint().has_value());
  } // released: workspace unmapped, HousekeepingThread joined

  // Straight through `arbc::Document::open`, deliberately BYPASSING `open_project`'s
  // guard: this case pins the LIBRARY behaviour A19 rests on, not the editor policy that
  // reacts to it. `org.arbc.solid` is a BUILT-IN, non-editable kind with an inert
  // `StateHandle` — so what follows is not a custom-kind or an editable-kind quirk
  // (D-slab_adopt-7; tests/camera_model_test.cpp asserts the same for `org.arbc.camera`).
  auto mapped = arbc::Document::open(layout.workspace_file.string());
  REQUIRE(mapped.has_value());
  const arbc::Document& doc = **mapped;
  const arbc::DocStatePtr pinned = doc.pin();
  REQUIRE(pinned != nullptr);

  // The record graph survives intact...
  arbc::ObjectId composition{};
  const arbc::CompositionRecord* comp = nullptr;
  CHECK(pinned->find_first_composition(composition, comp));
  const arbc::LayerRecord* record = pinned->find_layer(layer);
  REQUIRE(record != nullptr);
  CHECK(record->content == content);
  CHECK(record->transform.a == placement.a);
  CHECK(record->transform.b == placement.b);
  CHECK(record->transform.c == placement.c);
  CHECK(record->transform.d == placement.d);
  CHECK(record->transform.tx == placement.tx);
  CHECK(record->transform.ty == placement.ty);

  // ...and NOTHING is bound. `Document::open(path, housekeeping)` takes no `Registry`
  // and runs no factory (arbc `runtime/document.hpp:76-85`), so the id→`Content`
  // side-map starts empty and stays empty.
  CHECK(doc.resolve(content) == nullptr);
  std::size_t visited = 0;
  doc.for_each_content([&visited](arbc::Content*) { ++visited; });
  CHECK(visited == 0);

  // The consequence at the editor's read seam, and it differs by accessor — worth pinning
  // exactly, because "the map is unusable" is the claim A19 rests on. `scene::cells`
  // deliberately survives an unresolvable/unbound content (D-cells_model-8: "an
  // unknown-passthrough cell is still a cell"), so it still REPORTS the layer, off the
  // record graph, with the kind token intact — but with nothing behind it: no live
  // `Content`, hence no extent and nothing to render, edit, or hit-test.
  // `scene::cameras` has no such fallback (it needs a `dynamic_cast` on `resolve`) and
  // drops the camera outright — asserted in tests/camera_model_test.cpp.
  const std::vector<ace::scene::Cell> mapped_cells = ace::scene::cells(doc, registry);
  REQUIRE(mapped_cells.size() == 1);
  CHECK(mapped_cells[0].kind_id == "org.arbc.solid"); // the record's kind token survived
  CHECK(mapped_cells[0].id == content);
  CHECK(doc.resolve(mapped_cells[0].id) == nullptr); // ...and that is all that survived
  CHECK_FALSE(mapped_cells[0].content_bounds.has_value());
}

TEST_CASE("open_project rebuilds a content-bearing workspace even with no extra-kinds callback") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "unbindable";
  const arbc::Registry registry = builtin_registry();
  author_content_bearing_workspace(fs, root, registry);
  write_probe_canonical(fs, ace::project::project_layout(root));

  // NO extra-kinds callback, so the OLD guard's only condition was absent and this reopen
  // took the map fast path — handing back a document whose every `resolve()` was null.
  // That was the latent bug on the shared open path (D-slab_adopt-4): the caller has no
  // editor kinds, but the map binds no BUILT-IN content either. Map-then-inspect rejects
  // it and falls through to the canonical rebuild, which is the only route to live content.
  const auto opened = ace::project::open_project(fs, root);
  REQUIRE(opened.has_value());
  CHECK(opened.value().rebuilt_from_canonical);
  CHECK(opened.value().unbindable_content_records == 0); // nothing lost: it rebuilt

  // Asserted as LIVE CONTENT, not merely as the flag, so this case cannot regress into
  // pinning an empty document the way its predecessor did — a mapped reopen would report
  // this same cell off the record graph with a null `resolve()` behind it.
  const std::vector<ace::scene::Cell> cells = ace::scene::cells(*opened.value().document, registry);
  REQUIRE(cells.size() == 1);
  CHECK(cells[0].kind_id == "org.arbc.solid");
  CHECK(opened.value().document->resolve(cells[0].id) != nullptr);
  CHECK(bound_content_count(*opened.value().document) == 1);
}

TEST_CASE("open_project keeps the map fast path for a content-free workspace") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "contentfree";
  const ProjectLayout layout = ace::project::project_layout(root);
  {
    auto created = ace::project::create_project(fs, root);
    REQUIRE(created.has_value());
    arbc::Document& doc = *created.value().document;
    // A composition and no content: there is nothing the map could fail to bind, so
    // there is nothing to gain by rebuilding.
    doc.add_composition(static_cast<double>(ace::project::k_probe_width),
                        static_cast<double>(ace::project::k_probe_height));
    REQUIRE(doc.checkpoint().has_value());
  }
  write_probe_canonical(fs, layout);

  // A canonical floor EXISTS, so the guard could rebuild — and does not. The guard is
  // content-driven, not a blanket disable: A13's crash-recovery of unpublished
  // RECORD-level edits survives exactly where it is harmless.
  const auto opened = ace::project::open_project(fs, root);
  REQUIRE(opened.has_value());
  CHECK_FALSE(opened.value().rebuilt_from_canonical);
  CHECK(opened.value().unbindable_content_records == 0);
}

TEST_CASE("a never-saved content-bearing project keeps the map and REPORTS the loss") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "nocanon_content";
  const arbc::Registry registry = builtin_registry();
  author_content_bearing_workspace(fs, root, registry);
  const ProjectLayout layout = ace::project::project_layout(root);
  REQUIRE(fs.exists(layout.workspace_file));
  REQUIRE_FALSE(fs.exists(layout.canonical)); // create_project publishes nothing

  // No canonical to rebuild from: the mapped document is all the project has, and
  // `NoProject` would be strictly worse, so it is KEPT. What changes is that the loss
  // stops being silent — the count is a VALUE on a successful open, not an error
  // (D-slab_adopt-5). Without it a user reads this reopen as "my work was never saved."
  const auto opened = ace::project::open_project(fs, root);
  REQUIRE(opened.has_value());
  CHECK_FALSE(opened.value().rebuilt_from_canonical);
  CHECK(opened.value().unbindable_content_records == 1);
  CHECK(bound_content_count(*opened.value().document) == 0);

  // The count is exactly what the user lost: the cell is still listed off the record graph
  // (D-cells_model-8's unknown-passthrough fallback) but nothing is bound behind it, so it
  // renders nothing, hit-tests to nothing, and cannot be edited.
  const std::vector<ace::scene::Cell> cells = ace::scene::cells(*opened.value().document, registry);
  REQUIRE(cells.size() == 1);
  CHECK(opened.value().document->resolve(cells[0].id) == nullptr);
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
  CHECK_FALSE(ace::project::make_error_code(OpenError::TargetExists).message().empty());
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
