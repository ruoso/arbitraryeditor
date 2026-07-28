// editor.project.open — L1 headless units for directory→Document open/create.
// Reuses the ScratchDir filesystem-round-trip pattern (tests/platform_test.cpp)
// and the render_probe golden (tests/goldens/render_probe_64x64.rgba8) for load
// fidelity. The workspace-backed cases mint libarbc's first checkpointable editor
// Document (its live HousekeepingThread) and run under ASan/TSan (A4/§9).

#include <ace/commands/app_state.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/project/save.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/content.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/model.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp>
#include <arbc/serialize/codec.hpp> // arbc::CodecTable (identity-capture fixture)

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
// takes) are exercised. `plant_dir_at_workspace_file` lets a fully-scaffolded
// `root` (real dirs + real `.gitignore`) still fault the workspace-document MINT,
// by squatting a directory where `Document::create` must open the workspace file.
class FaultyFileSystem final : public ace::platform::FileSystem {
public:
  enum class Op { None, MakeDirectories, AtomicReplace, ReadFile };
  Op fail_op = Op::None;
  // Land a real, fully-scaffolded `root` (dirs + `.gitignore`) but squat a DIRECTORY at the
  // workspace-file path, so libarbc's `O_RDWR|O_CREAT` mint open faults EISDIR — the only
  // portable double lever that steers past the earlier branches to the MINT step. (An
  // absent `root`, e.g. a no-op mkdir, would fault the `.gitignore` write first, landing on
  // the earlier gitignore branch rather than the mint one.)
  bool plant_dir_at_workspace_file = false;
  // editor.project.create_rollback made this observable: `create_project` now rolls back
  // its partial scaffold via `fs.remove_tree(root)` on every post-guard failure branch, so
  // the suite counts the calls and captures the path, and can force the rollback itself to
  // fail — the A26 injection that proves the ORIGINAL error survives a failed cleanup
  // (mirrors tests/save_as_test.cpp's `fail_remove_tree`).
  mutable int remove_tree_calls = 0;
  mutable std::filesystem::path last_remove_tree_path;
  bool fail_remove_tree = false;

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
    std::error_code ec = native_.make_directories(dir);
    if (!ec && plant_dir_at_workspace_file && dir.filename() == "workspace") {
      // The `workspace/` subdir just landed for real; plant a directory where the workspace
      // file (`workspace/document.arbcws`) must be minted, so `Document::create`'s
      // `O_RDWR|O_CREAT|O_TRUNC` open returns EISDIR and the mint branch is taken.
      ec = native_.make_directories(dir / "document.arbcws");
    }
    return ec;
  }

  // editor.project.reconstructing_reopen: fault the atomic_replace of one specific filename only,
  // so the published-revision sidecar write can be failed while the canonical publish that
  // precedes it succeeds — the crash-between-canonical-and-sidecar case D28's safety rests on.
  std::string fail_atomic_replace_filename;

  std::error_code atomic_replace(const std::filesystem::path& path,
                                 std::string_view contents) const override {
    if (fail_op == Op::AtomicReplace) {
      return std::make_error_code(std::errc::io_error);
    }
    if (!fail_atomic_replace_filename.empty() && path.filename() == fail_atomic_replace_filename) {
      return std::make_error_code(std::errc::io_error);
    }
    return native_.atomic_replace(path, contents);
  }

  std::error_code remove_tree(const std::filesystem::path& path) const override {
    ++remove_tree_calls;
    last_remove_tree_path = path;
    if (fail_remove_tree) {
      return std::make_error_code(std::errc::permission_denied);
    }
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
// Install the content-identity capture on `doc` so its records carry construction identity —
// the reconstructing reopen's precondition (editor.project.reconstructing_reopen). In the live
// editor this comes from `AppState`; these fixtures author directly on a `create_project`
// document (no `AppState`), so they install it themselves, exactly as `AppState` does, over a
// `codecs`/`bridge` pair that must OUTLIVE the `add_content` calls it captures for. Cleared
// before the fixture returns so no closure dangles into the freed locals.
struct IdentityCapture {
  arbc::KindBridge bridge;
  arbc::CodecTable codecs;
  IdentityCapture(arbc::Document& doc, const arbc::Registry& registry)
      : codecs(arbc::builtin_codecs(registry)) {
    ace::project::seed_kind_bridge(bridge, registry);
    doc.set_content_identity_capture(arbc::codec_identity_capture(codecs, bridge));
  }
};

void author_content_bearing_workspace(const ace::platform::FileSystem& fs,
                                      const std::filesystem::path& root,
                                      const arbc::Registry& registry) {
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  arbc::Document& doc = *created.value().document;
  const IdentityCapture capture(doc, registry);
  doc.add_composition(static_cast<double>(ace::project::k_probe_width),
                      static_cast<double>(ace::project::k_probe_height));
  // Authored through the real insert path so the `ContentRecord`'s kind token is the
  // registry-seeded one (A16) — a hand-minted token of 0 is not what a project carries.
  REQUIRE(ace::scene::add_cell(doc, registry, "org.arbc.solid", "0,0.5,0,1",
                               arbc::Affine::translation(3.0, 4.0))
              .has_value());
  REQUIRE(doc.checkpoint().has_value());
  doc.set_content_identity_capture({});
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
  // A FaultyFileSystem left at its defaults is a transparent native delegate, used here
  // ONLY so `remove_tree_calls` is observable: the single most important guarantee of
  // editor.project.create_rollback is that the delete never touches a directory the D27
  // guard rejected — the target is somebody else's, not this call's debris (A26).
  FaultyFileSystem fs;
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

  // The refusal removes NOTHING: the rollback runs only on the post-guard failure branches,
  // never on the D27 refusal, so a directory the guard rejected is never deleted (A26).
  CHECK(fs.remove_tree_calls == 0);

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

// --- editor.project.reconstructing_reopen: what the workspace map now RECONSTRUCTS (A19
// amendment) — the record-only guard is retired; the map path binds live content -----------

TEST_CASE("a workspace-mapped reopen reconstructs live content through open_document") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "reconstruct";
  const ProjectLayout layout = ace::project::project_layout(root);

  const arbc::Registry registry = builtin_registry();
  arbc::ObjectId content{};
  arbc::ObjectId layer{};
  const arbc::Affine placement = arbc::Affine::translation(3.0, 4.0);
  {
    auto created = ace::project::create_project(fs, root);
    REQUIRE(created.has_value());
    arbc::Document& doc = *created.value().document;
    const IdentityCapture capture(doc, registry); // as AppState installs (reconstructing_reopen)
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
    doc.set_content_identity_capture({});
  } // released: workspace unmapped, HousekeepingThread joined

  // The v0.4.0 pin makes the map path REGISTRY-AWARE: the editor's `open_project` maps the
  // workspace through `arbc::open_document`, which walks each recovered `ContentRecord` and
  // rebuilds it from the construction identity the record now carries (record-only
  // `arbc::Document::open` → registry-aware `arbc::open_document`, v0.4.0 changelog #19). The
  // previous version of this case pinned the RETIRED premise — that a mapped reopen bound no
  // `Content` and `for_each_content` visited none; the record-only behaviour still exists and
  // stays pinned in tests/arbc_pin_test.cpp, but it is no longer the editor's reopen path.
  const auto opened = ace::project::open_project(fs, root);
  REQUIRE(opened.has_value());
  CHECK_FALSE(opened.value().rebuilt_from_canonical); // mapped-and-reconstructed, not rebuilt
  CHECK(opened.value().unbindable_content_records == 0);
  const arbc::Document& doc = *opened.value().document;
  const arbc::DocStatePtr pinned = doc.pin();
  REQUIRE(pinned != nullptr);

  // The record graph survives intact — the layer and its placement transform...
  arbc::ObjectId composition{};
  const arbc::CompositionRecord* comp = nullptr;
  CHECK(pinned->find_first_composition(composition, comp));
  const arbc::LayerRecord* record = pinned->find_layer(layer);
  REQUIRE(record != nullptr);
  CHECK(record->content == content);
  CHECK(record->transform.tx == placement.tx);
  CHECK(record->transform.ty == placement.ty);

  // ...AND the content is now LIVE: `resolve()` answers a real object and `for_each_content`
  // visits the reconstructed content, where before both came back empty.
  CHECK(doc.resolve(content) != nullptr);
  std::size_t visited = 0;
  doc.for_each_content([&visited](arbc::Content*) { ++visited; });
  CHECK(visited == 1);

  // At the editor's read seam the cell is whole again: the kind token survived AND live content
  // sits behind it, so it has an extent and can render, edit, and hit-test.
  const std::vector<ace::scene::Cell> mapped_cells = ace::scene::cells(doc, registry);
  REQUIRE(mapped_cells.size() == 1);
  CHECK(mapped_cells[0].kind_id == "org.arbc.solid");
  CHECK(mapped_cells[0].id == content);
  CHECK(doc.resolve(mapped_cells[0].id) != nullptr);
  CHECK(bound_content_count(doc) == 1);
}

TEST_CASE("open_project reconstructs a content-bearing workspace with no extra-kinds callback") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "reconstruct_builtin";
  const arbc::Registry registry = builtin_registry();
  author_content_bearing_workspace(fs, root, registry);
  write_probe_canonical(fs, ace::project::project_layout(root));

  // Inverted from its predecessor (v0.4.0 changelog #19): with NO extra-kinds callback the map
  // path once handed back a null-resolving husk, so the old guard discarded it for a canonical
  // rebuild. Now the map path RECONSTRUCTS the built-in content directly, so the common reopen
  // stops paying a full canonical rebuild — `rebuilt_from_canonical == false`, nothing
  // unreconstructed, the cell live, all WITHOUT touching the canonical floor.
  const auto opened = ace::project::open_project(fs, root);
  REQUIRE(opened.has_value());
  CHECK_FALSE(opened.value().rebuilt_from_canonical);
  CHECK(opened.value().unbindable_content_records == 0);

  const std::vector<ace::scene::Cell> cells = ace::scene::cells(*opened.value().document, registry);
  REQUIRE(cells.size() == 1);
  CHECK(cells[0].kind_id == "org.arbc.solid");
  CHECK(opened.value().document->resolve(cells[0].id) != nullptr);
  CHECK(bound_content_count(*opened.value().document) == 1);
}

TEST_CASE("open_project keeps the map fast path — durable-by-default for content too") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "durable_content";
  const arbc::Registry registry = builtin_registry();
  author_content_bearing_workspace(fs, root, registry);
  write_probe_canonical(fs, ace::project::project_layout(root));

  // The map fast path used to be kept ONLY for a content-free workspace; the retirement of the
  // content-bearing-map guard (A19 amendment) generalizes the rule: with a canonical floor
  // present the map is KEPT and its content RECONSTRUCTED for every kind, not rebuilt. This is
  // D-open-3's original durable-by-default policy restored for content-bearing projects.
  const auto opened = ace::project::open_project(fs, root);
  REQUIRE(opened.has_value());
  CHECK_FALSE(opened.value().rebuilt_from_canonical); // map kept, not rebuilt
  CHECK(opened.value().unbindable_content_records == 0);
  CHECK(bound_content_count(*opened.value().document) == 1); // content reconstructed
}

TEST_CASE("a never-saved content-bearing project reconstructs on the map with no loss") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "nocanon_content";
  const arbc::Registry registry = builtin_registry();
  author_content_bearing_workspace(fs, root, registry);
  const ProjectLayout layout = ace::project::project_layout(root);
  REQUIRE(fs.exists(layout.workspace_file));
  REQUIRE_FALSE(fs.exists(layout.canonical)); // create_project publishes nothing

  // Inverted (v0.4.0 changelog #19): the never-saved content-bearing project once KEPT the map
  // and REPORTED the whole of its content as lost (no canonical to rebuild from). Now the map
  // RECONSTRUCTS the reconstructable content — the workspace carries construction identity from
  // its first checkpoint — so `unbindable_content_records == 0` and the cell is live. The loss
  // report is exercised by a SEPARATE unreconstructable case (below), not by every content-
  // bearing project.
  const auto opened = ace::project::open_project(fs, root);
  REQUIRE(opened.has_value());
  CHECK_FALSE(opened.value().rebuilt_from_canonical);
  CHECK(opened.value().unbindable_content_records == 0);
  CHECK(bound_content_count(*opened.value().document) == 1);

  const std::vector<ace::scene::Cell> cells = ace::scene::cells(*opened.value().document, registry);
  REQUIRE(cells.size() == 1);
  CHECK(opened.value().document->resolve(cells[0].id) != nullptr);
}

TEST_CASE("open_project leaves an unreconstructable record UNBOUND and reports it") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "unreconstructable";
  const arbc::Registry registry = builtin_registry();

  // Author a content-bearing workspace WITH NO identity capture installed — the pre-v0.4.0 shape:
  // a workspace written before construction identity was captured, so its `ContentRecord` carries
  // no identity at all. (This is the reliably-unreconstructable case: an UNKNOWN-but-identified
  // kind reconstructs as a doc-05 placeholder, but a record with no identity has nothing to
  // rebuild from.)
  {
    auto created = ace::project::create_project(fs, root);
    REQUIRE(created.has_value());
    arbc::Document& doc = *created.value().document;
    doc.add_composition(static_cast<double>(ace::project::k_probe_width),
                        static_cast<double>(ace::project::k_probe_height));
    REQUIRE(ace::scene::add_cell(doc, registry, "org.arbc.solid", "0,0.5,0,1",
                                 arbc::Affine::translation(3.0, 4.0))
                .has_value());
    REQUIRE(doc.checkpoint().has_value());
  }

  // `open_document` cannot reconstruct the identity-less record, so it is listed in
  // `unreconstructed` and left UNBOUND — never filled with a default stand-in (Constraint 4 /
  // D-reconstructing_reopen-2). The document still opens; the loss is a reported VALUE feeding
  // the degradation notice (D25) unchanged.
  const auto opened = ace::project::open_project(fs, root);
  REQUIRE(opened.has_value());
  CHECK_FALSE(opened.value().rebuilt_from_canonical);
  CHECK(opened.value().unbindable_content_records == 1);
  CHECK(bound_content_count(*opened.value().document) == 0);

  // The record survives (the cell is still listed off the record graph, D-cells_model-8's
  // unknown-passthrough fallback) but nothing is bound behind it — no default stand-in.
  const std::vector<ace::scene::Cell> cells = ace::scene::cells(*opened.value().document, registry);
  REQUIRE(cells.size() == 1);
  CHECK(opened.value().document->resolve(cells[0].id) == nullptr);
}

// The editor-kind registrar the LOAD path applies (mirrors commands::register_editor_kinds,
// which is file-local there): threading it into open_project lets the reconstruction registry
// recognize `org.arbc.camera` and rebuild a persisted camera as live typed Content.
void register_editor_kinds(arbc::Registry& registry) { ace::scene::register_camera_kind(registry); }

TEST_CASE("open_project reconstructs cells and cameras live on a mapped reopen") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "cells_and_cameras";

  arbc::Registry authoring = builtin_registry();
  ace::scene::register_camera_kind(authoring);
  arbc::ObjectId cell_id{};
  arbc::ObjectId camera_id{};
  const arbc::Affine recrop = arbc::Affine::translation(10.0, 5.0);
  {
    auto created = ace::project::create_project(fs, root);
    REQUIRE(created.has_value());
    arbc::Document& doc = *created.value().document;
    const IdentityCapture capture(doc, authoring);
    doc.add_composition(static_cast<double>(ace::project::k_probe_width),
                        static_cast<double>(ace::project::k_probe_height));
    REQUIRE(ace::scene::add_cell(doc, authoring, "org.arbc.solid", "0,0.5,0,1",
                                 arbc::Affine::translation(3.0, 4.0))
                .has_value());
    camera_id = ace::scene::add_camera(doc, authoring, "Original", ace::scene::Resolution{32, 24},
                                       arbc::Affine::identity());
    REQUIRE(camera_id.valid());
    // Edit the camera AFTER creation but do NOT save: a re-crop (layer transform), a rename, and a
    // re-resolution. Durable-by-default requires the reconstructed state to be the
    // LAST-CHECKPOINTED state, not construction defaults (see the refinement's Open questions:
    // this test ENCODES that requirement).
    for (const ace::scene::Camera& cam : ace::scene::cameras(doc)) {
      doc.set_layer_transform(cam.layer, recrop);
    }
    REQUIRE(ace::scene::rename_camera(doc, authoring, camera_id, "Renamed"));
    REQUIRE(ace::scene::set_camera_resolution(doc, authoring, camera_id,
                                              ace::scene::Resolution{40, 30}));
    const std::vector<ace::scene::Cell> cells = ace::scene::cells(doc, authoring);
    REQUIRE(cells.size() == 1);
    cell_id = cells[0].id;
    REQUIRE(doc.checkpoint().has_value());
    doc.set_content_identity_capture({});
  }

  // Map-reconstruct with the camera codec registered.
  auto opened = ace::project::open_project(fs, root, register_editor_kinds);
  REQUIRE(opened.has_value());
  CHECK_FALSE(opened.value().rebuilt_from_canonical);
  CHECK(opened.value().unbindable_content_records == 0);
  const arbc::Document& doc = *opened.value().document;

  // Both the cell and the camera come back LIVE.
  CHECK(doc.resolve(cell_id) != nullptr);
  CHECK(doc.resolve(camera_id) != nullptr);
  CHECK(bound_content_count(doc) == 2);

  // The camera's LAST-CHECKPOINTED FRAMING survives — the frame is the binding layer's transform,
  // a record-graph fact the map restores verbatim, so a post-creation RE-CROP is durable.
  const std::vector<ace::scene::Camera> cams = ace::scene::cameras(doc);
  REQUIRE(cams.size() == 1);
  CHECK(cams[0].frame.tx == recrop.tx);
  CHECK(cams[0].frame.ty == recrop.ty);

  // KNOWN LIBRARY GAP (surfaced to tasks/parking-lot.md — trigger: "a mapped reconstruct reopen
  // observed reverting an unsaved camera edit"). The camera's post-creation MUTABLE STATE — its
  // name and resolution — does NOT round-trip through the mapped reopen: it reverts to the
  // CONSTRUCTION values, not the last-checkpointed ones. arbc#19 captures a content's
  // construction identity at `add_content` (name "Original", resolution 32×24 here) and
  // `open_document` rebuilds from that identity; a camera's name/resolution is `arbc::Editable`
  // content state that the codec persists into `project.arbc` (A19 consequence (1) — the editor
  // registers no `KindStateWalker`, and `open_document` does not replay recovered state), so the
  // workspace arena a mapped reopen reads never carries the post-creation edit. This is a
  // cross-repo LIBRARY property the editor cannot settle; it is pinned here as the CURRENT
  // contract so the day a libarbc release round-trips it, this case fails and the parked task is
  // picked up. A RE-CROP (above) is durable because it is a layer transform, not content state.
  CHECK(cams[0].name == "Original");
  CHECK(cams[0].resolution.width == 32);
  CHECK(cams[0].resolution.height == 24);
}

TEST_CASE("open_project falls back to canonical rebuild when open_document faults") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "reconstruct_fault";
  const ProjectLayout layout = ace::project::project_layout(root);
  const arbc::Registry registry = builtin_registry();

  // A content-bearing workspace with a canonical floor, then the workspace file corrupted so
  // `arbc::open_document` faults with a WorkspaceFileError. The fault falls through to the
  // canonical rebuild exactly as the old map fault did — no error escapes, the content is live.
  author_content_bearing_workspace(fs, root, registry);
  write_probe_canonical(fs, layout);
  REQUIRE(!fs.write_file(layout.workspace_file, "not a mappable workspace at all"));

  const auto opened = ace::project::open_project(fs, root);
  REQUIRE(opened.has_value());
  CHECK(opened.value().rebuilt_from_canonical); // fell back to canonical, no error
  CHECK(opened.value().unbindable_content_records == 0);
  CHECK(bound_content_count(*opened.value().document) == 1);
}

// --- editor.project.reconstructing_reopen: cross-session dirty precision (D28) --------------

// Author a solid-bearing project on the create path, checkpoint it durable at the edited
// revision, and publish it (canonical + published-revision sidecar). Leaves the on-disk bundle
// as a cleanly-saved project whose workspace last-checkpointed revision equals its sidecar.
void author_and_publish(const ace::platform::FileSystem& fs, const std::filesystem::path& root) {
  auto state = ace::commands::open_or_create_app_state(fs, root); // root absent → create
  REQUIRE(state.has_value());
  arbc::Document& doc = state.value().document();
  doc.add_composition(static_cast<double>(ace::project::k_probe_width),
                      static_cast<double>(ace::project::k_probe_height));
  REQUIRE(ace::scene::add_cell(doc, state.value().registry(), "org.arbc.solid", "0,0.5,0,1",
                               arbc::Affine::identity())
              .has_value());
  REQUIRE(doc.checkpoint().has_value()); // workspace durable at this revision
  REQUIRE(ace::commands::save_project(state.value(), fs).has_value()); // canonical + sidecar
}

TEST_CASE("a cleanly-saved workspace reopens CLEAN through the map path") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "clean_reopen";
  author_and_publish(fs, root);

  auto opened = ace::project::open_project(fs, root, register_editor_kinds);
  REQUIRE(opened.has_value());
  CHECK_FALSE(opened.value().rebuilt_from_canonical); // mapped, not rebuilt
  CHECK(opened.value().mapped_in_sync);               // sidecar matched the reconstructed revision
  ace::commands::AppState state(std::move(opened.value()));
  CHECK_FALSE(state.is_dirty()); // seeded clean off mapped_in_sync
}

TEST_CASE("a workspace edited after its last save reopens DIRTY") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "edited_dirty";
  author_and_publish(fs, root);

  // Reopen the live session, edit past the published revision, checkpoint the workspace, close.
  {
    auto state = ace::commands::open_or_create_app_state(fs, root);
    REQUIRE(state.has_value());
    REQUIRE(ace::scene::add_cell(state.value().document(), state.value().registry(),
                                 "org.arbc.solid", "0,0.5,0,1", arbc::Affine::translation(1.0, 1.0))
                .has_value());
    REQUIRE(state.value().document().checkpoint().has_value());
  }

  auto opened = ace::project::open_project(fs, root, register_editor_kinds);
  REQUIRE(opened.has_value());
  CHECK_FALSE(opened.value().rebuilt_from_canonical);
  CHECK_FALSE(opened.value().mapped_in_sync); // workspace revision moved past the sidecar
  ace::commands::AppState state(std::move(opened.value()));
  CHECK(state.is_dirty());
}

TEST_CASE("a pre-v0.4.0 / sidecar-absent workspace reopens DIRTY") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "no_sidecar";
  const ProjectLayout layout = ace::project::project_layout(root);
  author_and_publish(fs, root);
  // Simulate a workspace written before the sidecar existed: remove it. The canonical stays.
  REQUIRE(fs.exists(layout.published_rev));
  REQUIRE(!fs.remove_tree(layout.published_rev));
  REQUIRE_FALSE(fs.exists(layout.published_rev));

  auto opened = ace::project::open_project(fs, root, register_editor_kinds);
  REQUIRE(opened.has_value());
  CHECK_FALSE(opened.value().rebuilt_from_canonical); // still maps-and-reconstructs
  CHECK_FALSE(opened.value().mapped_in_sync);         // no sidecar → dirty (safe)
  ace::commands::AppState state(std::move(opened.value()));
  CHECK(state.is_dirty());
}

TEST_CASE("a never-saved created project reopens DIRTY") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "never_saved";
  const ProjectLayout layout = ace::project::project_layout(root);
  const arbc::Registry registry = builtin_registry();
  author_content_bearing_workspace(fs, root, registry); // create + content + checkpoint, NO save
  REQUIRE_FALSE(fs.exists(layout.canonical));           // publishes nothing
  REQUIRE_FALSE(fs.exists(layout.published_rev));       // and writes no sidecar

  auto opened = ace::project::open_project(fs, root, register_editor_kinds);
  REQUIRE(opened.has_value());
  CHECK_FALSE(opened.value().mapped_in_sync);
  ace::commands::AppState state(std::move(opened.value()));
  CHECK(state.is_dirty());
}

TEST_CASE("a crash between canonical publish and sidecar write never reads clean") {
  ScratchDir scratch;
  const std::filesystem::path root = scratch.root / "crash_before_sidecar";
  const ProjectLayout layout = ace::project::project_layout(root);

  // The single most important safety case (D-save-4's never-a-false-clean asymmetry through the
  // D28 sidecar): the canonical `atomic_replace` succeeds but the sidecar write does not — a
  // crash in the window between them. Save still succeeds (the canonical is durable; the sidecar
  // is best-effort), but the next reopen must be DIRTY, never clean.
  {
    FaultyFileSystem fs;
    fs.fail_atomic_replace_filename = layout.published_rev.filename().string();
    auto state = ace::commands::open_or_create_app_state(fs, root); // create
    REQUIRE(state.has_value());
    arbc::Document& doc = state.value().document();
    doc.add_composition(static_cast<double>(ace::project::k_probe_width),
                        static_cast<double>(ace::project::k_probe_height));
    REQUIRE(ace::scene::add_cell(doc, state.value().registry(), "org.arbc.solid", "0,0.5,0,1",
                                 arbc::Affine::identity())
                .has_value());
    REQUIRE(doc.checkpoint().has_value());
    // The publish succeeds even though the sidecar write is faulted (best-effort).
    REQUIRE(ace::commands::save_project(state.value(), fs).has_value());
    REQUIRE(fs.exists(layout.canonical));           // canonical landed
    REQUIRE_FALSE(fs.exists(layout.published_rev)); // sidecar did not
  }

  ace::platform::NativeFileSystem fs;
  auto opened = ace::project::open_project(fs, root, register_editor_kinds);
  REQUIRE(opened.has_value());
  CHECK_FALSE(opened.value().mapped_in_sync); // absent sidecar → never clean
  ace::commands::AppState state(std::move(opened.value()));
  CHECK(state.is_dirty());
}

// --- editor.project.reconstructing_reopen: reconstruct == canonical-rebuild (golden) --------

TEST_CASE("a mapped-reconstruct reopen renders byte-identically to a canonical rebuild") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const arbc::Registry registry = builtin_registry();

  // One project authored two ways at two roots, then reopened by the two routes.
  const std::filesystem::path map_root = scratch.root / "golden_map";
  author_content_bearing_workspace(fs, map_root, registry);          // workspace present
  write_probe_canonical(fs, ace::project::project_layout(map_root)); // + a canonical floor

  const std::filesystem::path rebuild_root = scratch.root / "golden_rebuild";
  REQUIRE(!fs.make_directories(rebuild_root));
  write_probe_canonical(fs, ace::project::project_layout(rebuild_root)); // canonical only

  const auto mapped = ace::project::open_project(fs, map_root);
  REQUIRE(mapped.has_value());
  REQUIRE_FALSE(mapped.value().rebuilt_from_canonical); // map-reconstruct route
  const auto rebuilt = ace::project::open_project(fs, rebuild_root);
  REQUIRE(rebuilt.has_value());
  REQUIRE(rebuilt.value().rebuilt_from_canonical); // forced canonical-rebuild route

  const ace::render::Srgb8Image from_map = ace::render::render_document_srgb8(
      *mapped.value().document, ace::project::k_probe_width, ace::project::k_probe_height);
  const ace::render::Srgb8Image from_rebuild = ace::render::render_document_srgb8(
      *rebuilt.value().document, ace::project::k_probe_width, ace::project::k_probe_height);

  // The differential the golden anchors: reconstruction yields pixel-LIVE content, not husks.
  CHECK(from_map.pixels == from_rebuild.pixels);
  CHECK(ace_test::compare_golden("reconstructed_reopen_64x64.rgba8", from_map.pixels));
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

  // editor.project.create_rollback: each create_project fault now ALSO rolls the partial
  // scaffold back, so the directory does not survive to poison the D27-guarded retry (A26).
  SECTION("create_project: a directory-scaffold failure") {
    FaultyFileSystem fs;
    fs.fail_op = FaultyFileSystem::Op::MakeDirectories;
    const std::filesystem::path root = scratch.root / "p1";
    const auto created = ace::project::create_project(fs, root);
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error() == OpenError::IoError);
    CHECK_FALSE(fs.exists(root));
  }

  SECTION("create_project: a .gitignore write failure") {
    FaultyFileSystem fs;
    fs.fail_op = FaultyFileSystem::Op::AtomicReplace;
    const std::filesystem::path root = scratch.root / "p2";
    const auto created = ace::project::create_project(fs, root);
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error() == OpenError::IoError);
    CHECK_FALSE(fs.exists(root));
  }

  SECTION("create_project: a workspace-file mint failure") {
    FaultyFileSystem fs;
    fs.plant_dir_at_workspace_file = true; // a dir squats the workspace file, so the mint faults
    const std::filesystem::path root = scratch.root / "p3";
    const auto created = ace::project::create_project(fs, root);
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error() == OpenError::IoError);
    CHECK_FALSE(fs.exists(root));
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

// --- editor.project.create_rollback: every post-guard failure rolls the scaffold back ------

TEST_CASE("create_project removes the partial scaffold when the mkdir loop fails") {
  ScratchDir scratch;
  const std::filesystem::path root = scratch.root / "mkdir_fault";
  FaultyFileSystem fs;
  fs.fail_op = FaultyFileSystem::Op::MakeDirectories;
  const auto created = ace::project::create_project(fs, root);
  REQUIRE_FALSE(created.has_value());
  CHECK(created.error() == OpenError::IoError);
  // Idempotence is relied on: the mkdir loop failed before `root` could materialize, and the
  // unconditional rollback on an absent path is still success (D-create_rollback-5).
  CHECK_FALSE(fs.exists(root));
  CHECK(fs.remove_tree_calls == 1);
  CHECK(fs.last_remove_tree_path == root);
}

TEST_CASE("create_project removes the partial scaffold when the gitignore write fails") {
  ScratchDir scratch;
  const std::filesystem::path root = scratch.root / "gitignore_fault";
  FaultyFileSystem fs;
  fs.fail_op = FaultyFileSystem::Op::AtomicReplace;
  const auto created = ace::project::create_project(fs, root);
  REQUIRE_FALSE(created.has_value());
  CHECK(created.error() == OpenError::IoError);
  // Here the mkdir loop DID materialize `root` (assets/workspace/exports) before the
  // gitignore fault; the rollback removes the real directory it left behind.
  CHECK_FALSE(fs.exists(root));
  CHECK(fs.remove_tree_calls == 1);
  CHECK(fs.last_remove_tree_path == root);
}

TEST_CASE("create_project removes the partial scaffold when the workspace-document mint fails") {
  ScratchDir scratch;
  const std::filesystem::path root = scratch.root / "mint_fault";
  FaultyFileSystem fs;
  // Dirs and `.gitignore` land for real, but a directory squats the workspace-file path, so
  // libarbc's `O_RDWR` mint open faults EISDIR — reaching the mint branch specifically (an
  // absent `root` would fault the earlier `.gitignore` write and land on the gitignore branch).
  fs.plant_dir_at_workspace_file = true;
  const auto created = ace::project::create_project(fs, root);
  REQUIRE_FALSE(created.has_value());
  CHECK(created.error() == OpenError::IoError);
  CHECK_FALSE(fs.exists(root));
  // `minted` held an error, so nothing escaped into scope — a bare rollback with `root`.
  CHECK(fs.remove_tree_calls == 1);
  CHECK(fs.last_remove_tree_path == root);
}

TEST_CASE("create_project retried with the same name after a failed create now succeeds") {
  ScratchDir scratch;
  const std::filesystem::path root = scratch.root / "retry";

  // A first attempt faults after the mkdir loop has materialized `root`. Before this leaf
  // that stranded directory poisoned the D27 guard, refusing every retry of the same name
  // forever after (A26); the rollback clears it.
  FaultyFileSystem faulty;
  faulty.fail_op = FaultyFileSystem::Op::AtomicReplace;
  const auto first = ace::project::create_project(faulty, root);
  REQUIRE_FALSE(first.has_value());
  CHECK(first.error() == OpenError::IoError);
  REQUIRE_FALSE(faulty.exists(root));

  // The same name on a healthy filesystem now creates cleanly — the loop is closed. The
  // result is structural: a live `Document` and a fresh (cold) `RasterTileStore`, pinned by
  // create/checkpoint success rather than by pixels (D-create_rollback-5).
  ace::platform::NativeFileSystem fs;
  const auto second = ace::project::create_project(fs, root);
  REQUIRE(second.has_value());
  CHECK(second.value().document != nullptr);
  CHECK(second.value().tiles != nullptr);
  CHECK_FALSE(second.value().rebuilt_from_canonical);
}

TEST_CASE(
    "create_project reports the create fault, not the rollback fault, when cleanup also fails") {
  ScratchDir scratch;
  const std::filesystem::path root = scratch.root / "cleanup_fault";
  FaultyFileSystem fs;
  fs.fail_op = FaultyFileSystem::Op::AtomicReplace; // the scaffold fault
  fs.fail_remove_tree = true;                       // and the rollback itself fails
  const auto created = ace::project::create_project(fs, root);
  REQUIRE_FALSE(created.has_value());
  // The ORIGINAL create error survives; `remove_tree`'s error is discarded best-effort, and
  // no `OpenError::RollbackFailed` exists (D-create_rollback-3).
  CHECK(created.error() == OpenError::IoError);
  CHECK(fs.remove_tree_calls == 1);
  CHECK(fs.last_remove_tree_path == root);
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
