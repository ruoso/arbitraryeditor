// editor.project.save_as — the publish-copy + sibling-exec verb (D-save_as-1..4).
// These headless Catch2 units pin the L1 logic (the bulk of the DoD, docs
// 01-architecture §9): `project::save_project_as` writes the portable core
// (`project.arbc` + `assets/` + a `workspace/`-excluding `.gitignore`) under a fresh
// root while leaving the source untouched, refuses to clobber an existing project,
// and surfaces I/O faults as `SaveError` values; `commands::save_project_as`
// canonicalizes the target once, publishes the copy, and hands it to an injected
// `ProcessLauncher` (the exec_new fake) — rejecting an empty target and never
// touching the launcher on a publish failure, and never marking the current session
// clean. The workspace-backed sessions exercise writer-thread capture racing the
// `HousekeepingThread` under the ASan/TSan legs of the gate, and — reusing the
// probe golden — the copy→reopen→render round-trip proves copy fidelity.

#include <ace/commands/app_state.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
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

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "golden_support.hpp"

using ace::commands::AppState;
using ace::project::ProjectLayout;
using ace::project::SaveError;

namespace {

// The platform_test ScratchDir pattern (not a shared header): a unique temp dir
// wiped on construction and destruction, named distinctly from the other suites
// sharing the ace_tests binary.
struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_save_as_test") {
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
// the built-in solid codec serializes it (mirrors project_save_test's
// build_saveable_probe: a bare add_content leaves the kind at 0, which renders but
// does not save).
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

// An in-test fake ProcessLauncher recording (exe, args) — the exec_new_test seam,
// proving `save_project_as` hands the copy to the sibling exec without a real spawn.
class RecordingLauncher final : public ace::platform::ProcessLauncher {
public:
  mutable bool invoked = false;
  mutable std::filesystem::path exe;
  mutable std::vector<std::string> args;

  std::error_code spawn_detached(const std::filesystem::path& executable,
                                 const std::vector<std::string>& a) const override {
    invoked = true;
    exe = executable;
    args = a;
    return {};
  }
};

// A launcher that reports a launch failure — the exec-half error path (Constraint 7:
// a failed exec leaves the published copy on disk and returns the error).
class FailingLauncher final : public ace::platform::ProcessLauncher {
public:
  mutable bool invoked = false;
  std::error_code spawn_detached(const std::filesystem::path&,
                                 const std::vector<std::string>&) const override {
    invoked = true;
    return std::make_error_code(std::errc::no_such_file_or_directory);
  }
};

const std::filesystem::path k_exe = "/usr/bin/arbitraryeditor";

} // namespace

TEST_CASE("save_project_as publishes the portable core under a fresh root, source untouched") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path source = scratch.root / "source";
  auto created = ace::project::create_project(fs, source);
  REQUIRE(created.has_value());
  ace::project::OpenedProject& opened = *created;
  build_saveable_probe(*opened.document);
  const arbc::Registry registry = builtin_registry();

  const std::filesystem::path target = scratch.root / "copy";
  const auto saved = ace::project::save_project_as(fs, target, *opened.document, registry);
  REQUIRE(saved.has_value());
  CHECK(saved.value().revision == opened.document->pin()->revision());

  const ProjectLayout target_layout = ace::project::project_layout(target);
  // The portable core lands: a non-empty project.arbc + assets/ + the gitignore.
  CHECK(fs.exists(target_layout.canonical));
  CHECK(fs.exists(target_layout.assets_dir));
  const auto bytes = fs.read_file(target_layout.canonical);
  REQUIRE(bytes.has_value());
  CHECK_FALSE(bytes.value().empty());
  const auto gitignore = fs.read_file(target_layout.gitignore);
  REQUIRE(gitignore.has_value());
  CHECK(gitignore.value() == "workspace/\n");

  // The copy is the portable core ONLY: no workspace/ (the sibling rebuilds it) and
  // no exports/.
  CHECK_FALSE(fs.exists(target_layout.workspace_dir));
  CHECK_FALSE(fs.exists(target_layout.exports_dir));

  // The parsed core is canonical: a fresh open over the target (no workspace present)
  // rebuilds from it without a corrupt-document error.
  auto reopened = ace::project::open_project(fs, target);
  REQUIRE(reopened.has_value());
  CHECK(reopened.value().rebuilt_from_canonical);

  // The SOURCE project is left untouched — Save As is "copy elsewhere," never a
  // publish-here (D-save_as-2 / Constraint 3): the source never gained a project.arbc.
  const ProjectLayout source_layout = ace::project::project_layout(source);
  CHECK_FALSE(fs.exists(source_layout.canonical));
}

TEST_CASE("save_project_as refuses to clobber a directory that already holds a project") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path target = scratch.root / "occupied";
  std::error_code ec;
  std::filesystem::create_directories(target, ec);
  // A pre-existing (foreign) project.arbc under the target — clobbering it would
  // destroy unrelated work (D-save_as-4).
  const ProjectLayout target_layout = ace::project::project_layout(target);
  std::ofstream(target_layout.canonical) << "existing-project";

  arbc::Document doc;
  build_saveable_probe(doc);
  const arbc::Registry registry = builtin_registry();

  const auto saved = ace::project::save_project_as(fs, target, doc, registry);
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error() == std::errc::file_exists); // a value, never a throw

  // The existing canonical is untouched — not atomically replaced by our snapshot.
  const auto bytes = fs.read_file(target_layout.canonical);
  REQUIRE(bytes.has_value());
  CHECK(bytes.value() == "existing-project");
}

TEST_CASE("save_project_as surfaces an unwritable target root as a SaveError value") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  // The target root is a regular file, so ensuring `assets/` beneath it faults.
  const std::filesystem::path target = scratch.root / "root-as-file";
  REQUIRE_FALSE(static_cast<bool>(fs.write_file(target, "x")));
  arbc::Document doc;
  build_saveable_probe(doc);
  const arbc::Registry registry = builtin_registry();

  const auto saved = ace::project::save_project_as(fs, target, doc, registry);
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error() == SaveError::IoError);
}

TEST_CASE("save_project_as surfaces a gitignore write fault after a clean publish") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path target = scratch.root / "gitignore-blocked";
  std::error_code ec;
  std::filesystem::create_directories(target, ec);
  // A DIRECTORY sitting at the .gitignore path: the publish (project.arbc + assets/)
  // succeeds, then the atomic rename-over of .gitignore fails (can't replace a dir).
  const ProjectLayout target_layout = ace::project::project_layout(target);
  std::filesystem::create_directories(target_layout.gitignore, ec);

  arbc::Document doc;
  build_saveable_probe(doc);
  const arbc::Registry registry = builtin_registry();

  const auto saved = ace::project::save_project_as(fs, target, doc, registry);
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error() == SaveError::IoError);
  // The publish half still landed the canonical (Constraint 7: a partial write is a
  // returned error value, not a throw); only the gitignore step faulted.
  CHECK(fs.exists(target_layout.canonical));
}

TEST_CASE("commands::save_project_as canonicalizes the target, publishes, and execs the sibling") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "session";
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  build_saveable_probe(state.document());

  RecordingLauncher launcher;
  // A RELATIVE target (resolved back into scratch so the publish is cleaned up):
  // save_project_as must canonicalize it to an absolute path used for BOTH the
  // publish and the spawn, so the child never depends on the caller's CWD.
  const std::filesystem::path abs_target = scratch.root / "relcopy";
  std::error_code rel_ec;
  const std::filesystem::path rel =
      std::filesystem::relative(abs_target, std::filesystem::current_path(), rel_ec);
  REQUIRE_FALSE(static_cast<bool>(rel_ec));
  REQUIRE_FALSE(rel.empty());

  const auto saved = ace::commands::save_project_as(state, fs, launcher, k_exe, rel);
  REQUIRE(saved.has_value());

  const std::filesystem::path resolved = std::filesystem::weakly_canonical(abs_target);
  CHECK(launcher.invoked);
  CHECK(launcher.exe == k_exe);
  REQUIRE(launcher.args.size() == 1);
  CHECK(launcher.args.front() == resolved.string()); // absolute — CWD-independent
  CHECK(fs.exists(ace::project::project_layout(resolved).canonical));

  // The CURRENT session is untouched (D-save_as-2 / Constraint 4): a fresh
  // create_project session is dirty, and Save As neither marks it clean nor
  // re-points its layout at the copy.
  CHECK(state.is_dirty());
  CHECK(state.layout().canonical == root / "project.arbc");
}

TEST_CASE("commands::save_project_as rejects an empty target without publishing or spawning") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "session";
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  build_saveable_probe(state.document());

  RecordingLauncher launcher;
  const auto saved = ace::commands::save_project_as(state, fs, launcher, k_exe, "");
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error() == std::errc::invalid_argument);
  CHECK_FALSE(launcher.invoked); // the launcher is never touched
}

TEST_CASE("commands::save_project_as short-circuits a publish failure and never execs") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "session";
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  build_saveable_probe(state.document());

  // A target that already holds a project.arbc makes the publish refuse-to-clobber:
  // the orchestrator returns that error and spawns nothing (Constraint 7).
  const std::filesystem::path target = scratch.root / "occupied";
  std::error_code ec;
  std::filesystem::create_directories(target, ec);
  std::ofstream(ace::project::project_layout(target).canonical) << "existing";

  RecordingLauncher launcher;
  const auto saved = ace::commands::save_project_as(state, fs, launcher, k_exe, target);
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error() == std::errc::file_exists);
  CHECK_FALSE(launcher.invoked); // a failed publish execs nothing
}

TEST_CASE("commands::save_project_as leaves the published copy on disk when the exec fails") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "session";
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  build_saveable_probe(state.document());

  FailingLauncher launcher;
  const std::filesystem::path target = scratch.root / "copy";
  const auto saved = ace::commands::save_project_as(state, fs, launcher, k_exe, target);
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error() == std::errc::no_such_file_or_directory);
  CHECK(launcher.invoked); // the exec was attempted (publish had succeeded)
  // The published copy survives the failed exec (Constraint 7).
  CHECK(fs.exists(ace::project::project_layout(target).canonical));
}

TEST_CASE("save_project_as's copy reloads and renders byte-exact against the probe golden") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path source = scratch.root / "source";
  auto created = ace::project::create_project(fs, source);
  REQUIRE(created.has_value());
  ace::project::OpenedProject& opened = *created;
  build_saveable_probe(*opened.document);
  const arbc::Registry registry = builtin_registry();

  const std::filesystem::path target = scratch.root / "copy";
  REQUIRE(ace::project::save_project_as(fs, target, *opened.document, registry).has_value());

  // Open the COPY forcing rebuild-from-canonical (no workspace/ present): the copy's
  // fidelity is proven by rendering it byte-exact against the existing probe golden.
  auto reopened = ace::project::open_project(fs, target);
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
