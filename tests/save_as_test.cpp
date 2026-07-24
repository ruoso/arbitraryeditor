// editor.project.save_as — the publish-copy + sibling-exec verb (D-save_as-1..4).
// These headless Catch2 units pin the L1 logic (the bulk of the DoD, docs
// 01-architecture §9): `project::save_project_as` writes the portable core
// (`project.arbc` + `assets/` + a `workspace/`-excluding `.gitignore`) under a fresh
// root while leaving the source untouched, refuses to clobber an existing project,
// and surfaces I/O faults as `SaveError` values; `commands::save_project_as`
// canonicalizes the target once, publishes the copy, and hands it to an injected
// `ProcessLauncher` (the exec_new fake) — rejecting an empty target and never
// touching the launcher on a publish failure, and never marking the current session
// clean. Since editor.project.save_as_outcome (A25) the orchestrator answers with a
// `SaveAsResult { stage, error, published }` rather than a bare
// `platform::Result<SaveOutcome>`, so these cases assert WHICH STAGE stopped — the
// discriminant the error channel alone cannot carry, because a refused target and a
// disk fault arrive as codes from two different categories and a failed `exec` leaves
// a complete copy on disk. The workspace-backed sessions exercise writer-thread capture racing the
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
#include <string_view>
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

// Delegates to NativeFileSystem but can be told to fault one chosen operation (the
// project_open_test FaultyFileSystem pattern). Once `save_project_as` refuses ANY existing
// target (D27 / D-dir_is_project-1), a publish-half fault can no longer be staged by
// pre-creating the target on disk — the guard would refuse before reaching it — so the fault
// has to be INJECTED through the seam instead, against a target that does not exist.
// `fail_atomic_replace_at` is path-scoped so the `project.arbc` publish can succeed while the
// trailing `.gitignore` write fails, which is the only way to reach the post-publish branch.
class FaultyFileSystem final : public ace::platform::FileSystem {
public:
  bool fail_make_directories = false;
  std::filesystem::path fail_atomic_replace_at;
  // A26: the rollback itself can fail (a vanishing mount, a revoked permission), and the
  // contract is that `save_project_as` still reports the SAVE's error. That branch is only
  // reachable through injection, which is the whole argument for putting the recursive remove
  // on the seam rather than calling `std::filesystem::remove_all` inline.
  bool fail_remove_tree = false;

  bool exists(const std::filesystem::path& path) const override { return native_.exists(path); }

  ace::platform::Result<std::vector<std::filesystem::path>>
  list_directory(const std::filesystem::path& dir) const override {
    return native_.list_directory(dir);
  }

  ace::platform::Result<std::string> read_file(const std::filesystem::path& path) const override {
    return native_.read_file(path);
  }

  std::error_code write_file(const std::filesystem::path& path,
                             std::string_view contents) const override {
    return native_.write_file(path, contents);
  }

  std::error_code make_directories(const std::filesystem::path& dir) const override {
    if (fail_make_directories) {
      return std::make_error_code(std::errc::io_error);
    }
    return native_.make_directories(dir);
  }

  std::error_code atomic_replace(const std::filesystem::path& path,
                                 std::string_view contents) const override {
    if (!fail_atomic_replace_at.empty() && path == fail_atomic_replace_at) {
      return std::make_error_code(std::errc::io_error);
    }
    return native_.atomic_replace(path, contents);
  }

  std::error_code remove_tree(const std::filesystem::path& path) const override {
    if (fail_remove_tree) {
      return std::make_error_code(std::errc::permission_denied);
    }
    return native_.remove_tree(path);
  }

private:
  ace::platform::NativeFileSystem native_;
};

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

// --- editor.project.dir_is_project: any existing target is refused (D27) -------------------

TEST_CASE("save_project_as refuses ANY existing target directory") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  std::error_code ec;

  // Save As CREATES a project directory (D16/D27), so it takes exactly the targets New takes:
  // nothing that already exists. Each SECTION is one shape of "already exists"; the empty one
  // is the direct reversal witness — under the shipped narrow `project.arbc`-only guard
  // (D-save_as-4) it SUCCEEDED and half-adopted the directory.
  std::filesystem::path target;
  SECTION("an EMPTY existing directory") {
    target = scratch.root / "empty_dir";
    std::filesystem::create_directories(target, ec);
  }
  SECTION("a populated non-project directory") {
    target = scratch.root / "someones_documents";
    std::filesystem::create_directories(target, ec);
    std::ofstream(target / "notes.txt") << "unrelated work";
  }
  SECTION("a directory holding a foreign project.arbc") {
    target = scratch.root / "occupied";
    std::filesystem::create_directories(target, ec);
    std::ofstream(ace::project::project_layout(target).canonical) << "existing-project";
  }

  arbc::Document doc;
  build_saveable_probe(doc);
  const arbc::Registry registry = builtin_registry();

  const auto saved = ace::project::save_project_as(fs, target, doc, registry);
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error() == std::errc::file_exists); // a value, never a throw

  // NOTHING was written under the target: no assets/, no .gitignore, and no canonical of
  // ours. A foreign canonical that WAS there keeps its own bytes — never atomically
  // replaced by our snapshot.
  const ProjectLayout target_layout = ace::project::project_layout(target);
  CHECK_FALSE(fs.exists(target_layout.assets_dir));
  CHECK_FALSE(fs.exists(target_layout.gitignore));
  if (fs.exists(target_layout.canonical)) {
    const auto bytes = fs.read_file(target_layout.canonical);
    REQUIRE(bytes.has_value());
    CHECK(bytes.value() == "existing-project");
  }
}

TEST_CASE("save_project_as surfaces an unwritable target root as a SaveError value") {
  ScratchDir scratch;
  FaultyFileSystem fs;
  // A target that does NOT exist (D27 refuses one that does, before any publish), with the
  // fault injected through the seam instead: ensuring `assets/` beneath the root faults, so
  // the publish half's IoError branch keeps its coverage.
  const std::filesystem::path target = scratch.root / "unwritable-root";
  REQUIRE_FALSE(fs.exists(target));
  fs.fail_make_directories = true;

  arbc::Document doc;
  build_saveable_probe(doc);
  const arbc::Registry registry = builtin_registry();

  const auto saved = ace::project::save_project_as(fs, target, doc, registry);
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error() == SaveError::IoError);
  // With `make_directories` faulted the root is never created, so A26's rollback runs against
  // an absent path and is a silent no-op — the idempotence clause, documented as an assertion.
  CHECK_FALSE(fs.exists(target));
}

TEST_CASE("save_project_as surfaces a gitignore write fault after a clean publish") {
  ScratchDir scratch;
  FaultyFileSystem fs;
  // Again a NOT-YET-EXISTING target, with the fault scoped to the `.gitignore` path only: the
  // publish (project.arbc + assets/) succeeds, then the trailing atomic write of .gitignore
  // faults — the only route to the post-publish branch now that the target cannot pre-exist.
  const std::filesystem::path target = scratch.root / "gitignore-blocked";
  const ProjectLayout target_layout = ace::project::project_layout(target);
  REQUIRE_FALSE(fs.exists(target));
  fs.fail_atomic_replace_at = target_layout.gitignore;

  arbc::Document doc;
  build_saveable_probe(doc);
  const arbc::Registry registry = builtin_registry();

  const auto saved = ace::project::save_project_as(fs, target, doc, registry);
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error() == SaveError::IoError);
  // FLIPPED by editor.project.save_as_rollback (A26): the publish half really did land the
  // canonical, and that is exactly why the whole target must go. What the caller gets back is a
  // fault, not a copy — the bundle has no `.gitignore` and is not the portable project D16
  // describes — so leaving it on disk would strand a directory D27's guard then refuses
  // forever. The error-code assertion above is unchanged; only the state is.
  CHECK_FALSE(fs.exists(target_layout.canonical));
  CHECK_FALSE(fs.exists(target_layout.gitignore));
  CHECK_FALSE(fs.exists(target));
}

// --- editor.project.save_as_rollback: a failed Save As leaves nothing behind (A26) ----------

TEST_CASE("save_project_as removes the partial target when the publish fails") {
  ScratchDir scratch;
  FaultyFileSystem fs;
  // The canonical publish itself faults, AFTER `save_project` has `make_directories`-ed
  // `assets/` and thereby materialized the root — the case that used to strand a directory.
  const std::filesystem::path target = scratch.root / "publish-blocked";
  const ProjectLayout target_layout = ace::project::project_layout(target);
  REQUIRE_FALSE(fs.exists(target));
  fs.fail_atomic_replace_at = target_layout.canonical;

  arbc::Document doc;
  build_saveable_probe(doc);
  const arbc::Registry registry = builtin_registry();

  const auto saved = ace::project::save_project_as(fs, target, doc, registry);
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error() == SaveError::IoError); // the SAVE's error, unchanged by A26
  // Not just the canonical: the whole root, `assets/` included, is gone.
  CHECK_FALSE(fs.exists(target));
  CHECK_FALSE(fs.exists(target_layout.assets_dir));
}

TEST_CASE("save_project_as removes the partial target when the gitignore write fails") {
  ScratchDir scratch;
  FaultyFileSystem fs;
  // The post-publish branch: `project.arbc` + `assets/` land cleanly and the trailing
  // `.gitignore` write is what faults. The bundle is not a portable project (D16), so the
  // rollback covers this branch exactly as it covers the publish branch.
  const std::filesystem::path target = scratch.root / "gitignore-rollback";
  const ProjectLayout target_layout = ace::project::project_layout(target);
  REQUIRE_FALSE(fs.exists(target));
  fs.fail_atomic_replace_at = target_layout.gitignore;

  arbc::Document doc;
  build_saveable_probe(doc);
  const arbc::Registry registry = builtin_registry();

  const auto saved = ace::project::save_project_as(fs, target, doc, registry);
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error() == SaveError::IoError);
  CHECK_FALSE(fs.exists(target));
}

TEST_CASE("save_project_as retried with the same name after a failed publish now succeeds") {
  // THE case this leaf exists for. Before A26 the sequence below ended in a refusal the user
  // could not escape without leaving the editor: the failed publish left `<parent>/<name>/`
  // behind, and D27's existing-target guard then answered every retry with
  // `file_exists` — reported to the rail as "Enter a project name that does not already exist
  // here.", a false statement produced by the tool's own debris.
  ScratchDir scratch;
  FaultyFileSystem fs;
  const std::filesystem::path target = scratch.root / "retry-me";
  const ProjectLayout target_layout = ace::project::project_layout(target);
  fs.fail_atomic_replace_at = target_layout.canonical;

  arbc::Document doc;
  build_saveable_probe(doc);
  const arbc::Registry registry = builtin_registry();

  const auto failed = ace::project::save_project_as(fs, target, doc, registry);
  REQUIRE_FALSE(failed.has_value());
  CHECK(failed.error() == SaveError::IoError);
  REQUIRE_FALSE(fs.exists(target)); // the precondition the retry depends on

  // The user frees some disk and presses Save Copy again with the IDENTICAL name.
  fs.fail_atomic_replace_at.clear();
  const auto retried = ace::project::save_project_as(fs, target, doc, registry);
  REQUIRE(retried.has_value()); // no longer `file_exists` — the loop is closed
  CHECK(retried.value().revision == doc.pin()->revision());
  CHECK(fs.exists(target_layout.canonical));
  CHECK(fs.exists(target_layout.assets_dir));
  const auto gitignore = fs.read_file(target_layout.gitignore);
  REQUIRE(gitignore.has_value());
  CHECK(gitignore.value() == "workspace/\n");

  // And the retry produced a COMPLETE project, not a repaired one: reopening the copy
  // (rebuild-from-canonical, no workspace/ present) renders byte-exact against the same probe
  // golden the first-time copy is held to — the strongest available statement that the
  // rollback left no poison behind.
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

TEST_CASE("save_project_as never removes the target it refuses") {
  // The single most important test in this leaf: the rollback's licence to delete is D27's
  // guard returning FALSE, so on the branch where the guard returns TRUE — the target is
  // somebody else's — nothing may be touched. A rollback wired one line too high would turn
  // Save As into a directory shredder for any name the user mistypes.
  ScratchDir scratch;
  FaultyFileSystem fs;
  const std::filesystem::path target = scratch.root / "someones_documents";
  std::error_code ec;
  std::filesystem::create_directories(target / "sub", ec);
  std::ofstream(target / "notes.txt") << "unrelated work";
  std::ofstream(target / "sub" / "deep.txt") << "also theirs";

  arbc::Document doc;
  build_saveable_probe(doc);
  const arbc::Registry registry = builtin_registry();

  const auto saved = ace::project::save_project_as(fs, target, doc, registry);
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error() == std::errc::file_exists);

  // Byte-unchanged, top to bottom.
  REQUIRE(fs.exists(target));
  const auto notes = fs.read_file(target / "notes.txt");
  REQUIRE(notes.has_value());
  CHECK(notes.value() == "unrelated work");
  const auto deep = fs.read_file(target / "sub" / "deep.txt");
  REQUIRE(deep.has_value());
  CHECK(deep.value() == "also theirs");
  // …and nothing of ours was added to it either (the pre-A26 refusal contract, still true).
  const ProjectLayout target_layout = ace::project::project_layout(target);
  CHECK_FALSE(fs.exists(target_layout.canonical));
  CHECK_FALSE(fs.exists(target_layout.assets_dir));
  CHECK_FALSE(fs.exists(target_layout.gitignore));
}

TEST_CASE("save_project_as reports the publish fault, not the rollback fault, when cleanup also "
          "fails") {
  // A26 mints no compound error and no `SaveError` enumerator: `SaveError` keeps meaning WHY
  // THE SAVE FAILED. When the cleanup also fails the caller therefore learns the same
  // actionable fact it always did, and the surviving debris is precisely the behaviour that
  // shipped before this leaf — a degradation, never a regression.
  ScratchDir scratch;
  FaultyFileSystem fs;
  const std::filesystem::path target = scratch.root / "double-fault";
  const ProjectLayout target_layout = ace::project::project_layout(target);
  fs.fail_atomic_replace_at = target_layout.canonical;
  fs.fail_remove_tree = true;

  arbc::Document doc;
  build_saveable_probe(doc);
  const arbc::Registry registry = builtin_registry();

  const auto saved = ace::project::save_project_as(fs, target, doc, registry);
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error() == SaveError::IoError);           // the SAVE's fault…
  CHECK(saved.error() != std::errc::permission_denied); // …never the cleanup's
  CHECK(fs.exists(target)); // the debris survives: the pre-A26 behaviour, degraded to
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
  // The success stage on the A25 result: the copy is on disk AND the sibling is running, with
  // no error and the publish's own revision carried through for the caller.
  REQUIRE(saved.stage == ace::commands::SaveAsStage::spawned);
  CHECK_FALSE(static_cast<bool>(saved.error));
  CHECK(saved.published.revision == state.document().pin()->revision());

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
  // A bad target the user can retype — `refused`, not a fault: nothing was written and nothing
  // was spawned.
  CHECK(saved.stage == ace::commands::SaveAsStage::refused);
  CHECK(saved.error == std::errc::invalid_argument);
  CHECK_FALSE(launcher.invoked); // the launcher is never touched
}

TEST_CASE("commands::save_project_as short-circuits a publish failure and never execs") {
  ScratchDir scratch;
  // A GENUINE publish fault, injected through the seam. Before A25 this case staged an
  // already-occupied target, which post-D27 is refused by the existing-target guard — i.e. it
  // had silently become a duplicate of the refused-target case below and no longer exercised
  // the branch its name claims. The `SaveError` route is what `publish_failed` is for: a full
  // disk / unwritable parent, which no amount of retyping fixes.
  FaultyFileSystem fs;
  const std::filesystem::path root = scratch.root / "session";
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  build_saveable_probe(state.document());

  // The fault is armed only AFTER the session exists, so the session's own bootstrap is real
  // and only the copy's publish faults.
  const std::filesystem::path target = scratch.root / "unwritable";
  fs.fail_make_directories = true;

  RecordingLauncher launcher;
  const auto saved = ace::commands::save_project_as(state, fs, launcher, k_exe, target);
  CHECK(saved.stage == ace::commands::SaveAsStage::publish_failed);
  CHECK(saved.error == SaveError::IoError);
  CHECK_FALSE(launcher.invoked); // a failed publish execs nothing (Constraint 7)
}

TEST_CASE("commands::save_project_as short-circuits a refused target and never execs") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "session";
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  build_saveable_probe(state.document());
  const bool dirty_before = state.is_dirty();

  // An EMPTY existing directory — the target D27 newly refuses and the shipped narrow guard
  // happily adopted. The orchestrator surfaces the refusal as a value and stops there.
  const std::filesystem::path target = scratch.root / "already_there";
  std::error_code ec;
  std::filesystem::create_directories(target, ec);

  RecordingLauncher launcher;
  const auto saved = ace::commands::save_project_as(state, fs, launcher, k_exe, target);
  // `refused`, not `publish_failed`: D27's existing-target guard is a target the user can
  // retype, and the split lives here in L1 `commands` — the level that owns both `std::errc`
  // and `SaveError` (D-save_as_outcome-3).
  CHECK(saved.stage == ace::commands::SaveAsStage::refused);
  CHECK(saved.error == std::errc::file_exists);
  CHECK_FALSE(launcher.invoked); // ZERO launches: a refused target execs nothing

  // Nothing was published into the target, and the CURRENT session is untouched
  // (D-save_as-2): same dirty state, same layout.
  const ProjectLayout target_layout = ace::project::project_layout(target);
  CHECK_FALSE(fs.exists(target_layout.canonical));
  CHECK_FALSE(fs.exists(target_layout.assets_dir));
  CHECK(state.is_dirty() == dirty_before);
  CHECK(state.layout().canonical == root / "project.arbc");
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
  // `spawn_failed` — the stage that carries the load-bearing fact the bare `error_code` erased:
  // the copy IS on disk. `published` is meaningful here precisely because of that, which is why
  // it rides alongside the error rather than instead of it (D-save_as_outcome-2).
  CHECK(saved.stage == ace::commands::SaveAsStage::spawn_failed);
  CHECK(saved.error == std::errc::no_such_file_or_directory);
  CHECK(saved.published.revision == state.document().pin()->revision());
  CHECK(launcher.invoked); // the exec was attempted (publish had succeeded)
  // The published copy survives the failed exec (Constraint 7).
  CHECK(fs.exists(ace::project::project_layout(target).canonical));
  // …and the CURRENT session is still its own (D-save_as-2): no `mark_saved`, no layout rebind,
  // on the failure path either.
  CHECK(state.layout().canonical == root / "project.arbc");
}

TEST_CASE("commands::save_project_as separates a refused target from a publish fault") {
  // THE case this leaf exists for (A25). Both of these exits leave `published.error()` set at
  // the one branch in `commands::save_project_as`, and the bare `platform::Result` erased which
  // one it was — so a full disk was reported to the user as "Enter a project name that does not
  // already exist here.", which no retype could ever satisfy. The stage tells them apart, and
  // the split happens HERE, in the only level holding both vocabularies (D-save_as_outcome-3).
  ScratchDir scratch;
  FaultyFileSystem fs;
  const std::filesystem::path root = scratch.root / "session";
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  build_saveable_probe(state.document());
  RecordingLauncher launcher;

  SECTION("an existing target is a REFUSAL the user can retype away") {
    const std::filesystem::path target = scratch.root / "already_there";
    std::error_code ec;
    std::filesystem::create_directories(target, ec);

    const auto saved = ace::commands::save_project_as(state, fs, launcher, k_exe, target);
    CHECK(saved.stage == ace::commands::SaveAsStage::refused);
    CHECK(saved.error == std::errc::file_exists);
  }

  SECTION("an unwritable target root is a FAULT no retype fixes") {
    const std::filesystem::path target = scratch.root / "unwritable_root";
    fs.fail_make_directories = true;

    const auto saved = ace::commands::save_project_as(state, fs, launcher, k_exe, target);
    CHECK(saved.stage == ace::commands::SaveAsStage::publish_failed);
    CHECK(saved.error == SaveError::IoError);
    // Two different error CATEGORIES over the one channel (D-dir_is_project-2) — which is
    // precisely why the level that owns both is the one that classifies them.
    CHECK(saved.error != std::errc::file_exists);
  }

  // Neither exit touches the launcher: a refusal and a publish fault both exec nothing.
  CHECK_FALSE(launcher.invoked);
}

TEST_CASE("commands::save_project_as reports a publish fault after a clean publish") {
  ScratchDir scratch;
  FaultyFileSystem fs;
  const std::filesystem::path root = scratch.root / "session";
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  build_saveable_probe(state.document());

  // The POST-publish fault (`project::save_project_as`'s trailing `.gitignore` write): the
  // canonical and `assets/` land, then the gitignore step faults, so a `SaveError` comes back
  // even though bytes reached the target. That is still a fault and not a refusal — the bundle
  // is not a usable project and retyping the name does not make it one — and it must still exec
  // nothing. The fault path is armed on the CANONICALIZED target, mirroring the one
  // canonicalization `commands::save_project_as` performs.
  const std::filesystem::path target = scratch.root / "gitignore_blocked";
  const std::filesystem::path resolved =
      std::filesystem::weakly_canonical(std::filesystem::absolute(target));
  const ProjectLayout target_layout = ace::project::project_layout(resolved);
  fs.fail_atomic_replace_at = target_layout.gitignore;

  RecordingLauncher launcher;
  const auto saved = ace::commands::save_project_as(state, fs, launcher, k_exe, target);
  CHECK(saved.stage == ace::commands::SaveAsStage::publish_failed);
  CHECK(saved.error == SaveError::IoError);
  CHECK_FALSE(launcher.invoked);
  // FLIPPED by editor.project.save_as_rollback, for the same reason as the L1 gitignore case
  // above and with the STAGE assertion untouched: the publish half did land, which is precisely
  // what L1 now rolls back, so `publish_failed` at this level means "nothing is on disk" rather
  // than "an unusable half-bundle is". A26 changes state, never the outcome vocabulary.
  CHECK_FALSE(fs.exists(target_layout.canonical));
  CHECK_FALSE(fs.exists(target_layout.gitignore));
  CHECK_FALSE(fs.exists(resolved));
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
