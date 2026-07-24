#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/expected.hpp>
#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// editor.project.open_ui — the AppProjectGateway logic unit (Acceptance / the
// exec_new_argv_test precedent): the concrete gateway driven with a fake
// ProcessLauncher (records exe + args), a real temp FileSystem, a real
// RecentProjects, and a scriptable fake FolderDialog (the SDL wrapper is not
// exercised here). Validates open / new / recent + the deferred-pick lifecycle.

namespace {

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_app_project_gateway_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// Records the launch (exe, args) and reports success — the exec_new_test pattern.
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

// A launcher that reports a launch FAILURE — the tests/save_as_test.cpp:108-116 pattern,
// copied here to drive the `spawn_failed` half of the A24 outcome seam. The target was
// accepted and the sibling `exec` is what did not happen (a broken install, a missing
// executable, an exhausted process table).
class FailingLauncher final : public ace::platform::ProcessLauncher {
public:
  mutable bool invoked = false;
  std::error_code spawn_detached(const std::filesystem::path&,
                                 const std::vector<std::string>&) const override {
    invoked = true;
    return std::make_error_code(std::errc::no_such_file_or_directory);
  }
};

// Delegates to NativeFileSystem but can be told to fault `make_directories` (the
// tests/save_as_test.cpp:127 FaultyFileSystem pattern, narrowed to the one operation this
// suite needs). It is what lets Save As's PUBLISH half fail against a target that does not
// exist — the `publish_failed` outcome A25 added, which cannot be staged by pre-creating the
// target, because D27's existing-target guard would refuse before the publish is reached.
class FaultyFileSystem final : public ace::platform::FileSystem {
public:
  bool fail_make_directories = false;

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
    return native_.atomic_replace(path, contents);
  }

private:
  ace::platform::NativeFileSystem native_;
};

// Scripts a folder pick; delivers synchronously, or holds the callback for a
// manual (later-frame) delivery to model the async lifecycle.
class ScriptedFolderDialog final : public ace::app::FolderDialog {
public:
  std::optional<std::filesystem::path> next;
  bool defer = false;
  bool shown = false;
  Callback held;
  void show(Callback on_pick) override {
    shown = true;
    if (defer) {
      held = std::move(on_pick);
    } else {
      on_pick(next);
    }
  }
  void deliver() {
    if (held) {
      Callback cb = std::move(held);
      held = nullptr;
      cb(next);
    }
  }
};

std::filesystem::path make_project(const std::filesystem::path& parent, const std::string& name) {
  const std::filesystem::path dir = parent / name;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::ofstream(dir / "project.arbc") << "x";
  return dir;
}

const std::filesystem::path k_exe = "/usr/bin/arbitraryeditor";

// A minimal in-process session for the gateway to hold. The entry actions
// (open/new/recent) never touch it; only Save + dirty (A13) do.
ace::commands::AppState make_session(const ace::platform::FileSystem& fs,
                                     const std::filesystem::path& root) {
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  return ace::commands::AppState(std::move(*created));
}

// The same minimal session, but with the A19 reopen-degradation count scripted onto the
// `OpenedProject` before it is consumed — standing in for the never-saved lossy reopen,
// the one path `project::open_project` reports a non-zero count from. Producing that count
// is pinned in tests/project_open_test.cpp; what this file owns is the seam that carries it
// out to the rail.
ace::commands::AppState make_degraded_session(const ace::platform::FileSystem& fs,
                                              const std::filesystem::path& root,
                                              std::size_t unbindable) {
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  created.value().unbindable_content_records = unbindable;
  return ace::commands::AppState(std::move(*created));
}

// An entirely unwired gateway: every pure verb stubbed inert, nothing overridden beyond
// them. Its purpose is to pin that `reopen_unbindable_count()` has a `0` DEFAULT rather
// than being pure — the property that lets the gateway fakes of unrelated suites stay
// unchanged, and that makes an unwired session report no loss instead of failing to link.
class InertGateway final : public ace::dock::ProjectGateway {
public:
  // `refused_target` is the honest inert answer on the A24 seam: this gateway launches
  // nothing, so it can never report a failed spawn.
  ace::dock::ProjectEntryOutcome open_project(const std::filesystem::path&) override {
    return ace::dock::ProjectEntryOutcome::refused_target;
  }
  ace::dock::ProjectEntryOutcome new_project(const std::filesystem::path&,
                                             const std::string&) override {
    return ace::dock::ProjectEntryOutcome::refused_target;
  }
  ace::dock::ProjectEntryOutcome open_recent(const std::filesystem::path&) override {
    return ace::dock::ProjectEntryOutcome::refused_target;
  }
  void pick_folder(std::function<void(std::optional<std::filesystem::path>)>) override {}
  std::vector<std::filesystem::path> recent_projects() const override { return {}; }
  bool save() override { return false; }
  bool is_dirty() const override { return false; }
  ace::dock::ProjectEntryOutcome save_as(const std::filesystem::path&,
                                         const std::string&) override {
    return ace::dock::ProjectEntryOutcome::refused_target;
  }
  ace::dock::GcSummary clean_up(bool) override { return {}; }
  bool undo() override { return false; }
  bool redo() override { return false; }
  bool can_undo() const override { return false; }
  bool can_redo() const override { return false; }
};

} // namespace

using ace::app::AppProjectGateway;
using ace::app::ProjectEntryGateway;
using ace::dock::ProjectEntryOutcome;
using ace::dockmodel::RecentProjects;

// --- editor.project.welcome — the session-free base (A22 / D-welcome-6) -------------
// The pre-project launcher constructs THIS type, not AppProjectGateway: it holds no
// `commands::AppState`, so it structurally cannot reach a `Document`. These two cases
// are the type-level statement of A22's zero-`Document` invariant — the entry half
// behaves identically to the shipped derived class (same validate -> MRU-front -> spawn
// implementation, because the bodies MOVED rather than being copied), and the session
// half is inert.

TEST_CASE("a session-free ProjectEntryGateway validates, records and spawns",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  // No `make_session` and no AppState& argument: the whole point of the split.
  ProjectEntryGateway gateway(recent, fs, dialog, launcher, k_exe);

  // (i) An existing project: spawns a sibling on the canonical path and MRU-fronts it.
  const std::filesystem::path project = make_project(scratch.root, "proj");
  REQUIRE(gateway.open_project(project) == ProjectEntryOutcome::succeeded);
  REQUIRE(launcher.invoked);
  CHECK(launcher.exe == k_exe);
  REQUIRE(launcher.args.size() == 1);
  CHECK(launcher.args.front() == std::filesystem::weakly_canonical(project).string());
  std::vector<std::filesystem::path> listed = gateway.recent_projects();
  REQUIRE(listed.size() == 1);
  CHECK(listed.front() == std::filesystem::weakly_canonical(project));

  // (ii) A non-project selection: refused as a value, nothing spawned, nothing recorded.
  launcher.invoked = false;
  const std::filesystem::path plain = scratch.root / "plain";
  std::error_code ec;
  std::filesystem::create_directories(plain, ec);
  CHECK(gateway.open_project(plain) == ProjectEntryOutcome::refused_target);
  CHECK_FALSE(launcher.invoked);
  CHECK(gateway.recent_projects().size() == 1); // still just the one real project

  // (iii) New composes an ABSOLUTE, not-yet-existing target from parent + name and
  //       spawns the sibling whose bootstrap create-branch scaffolds it (D-open_ui-4).
  REQUIRE(gateway.new_project(scratch.root, "Fresh") == ProjectEntryOutcome::succeeded);
  REQUIRE(launcher.invoked);
  const std::filesystem::path target = scratch.root / "Fresh";
  REQUIRE(launcher.args.size() == 1);
  CHECK(launcher.args.front() == std::filesystem::weakly_canonical(target).string());
  CHECK_FALSE(std::filesystem::exists(target)); // the create signal to the child
  CHECK(gateway.recent_projects().size() == 1); // New records nothing
  launcher.invoked = false;
  CHECK(gateway.new_project(scratch.root, "bad/name") == ProjectEntryOutcome::refused_target);
  CHECK_FALSE(launcher.invoked);

  // (iv) Replay re-orders MRU-front and spawns; a vanished entry is refused.
  const std::filesystem::path second = make_project(scratch.root, "two");
  REQUIRE(gateway.open_project(second) == ProjectEntryOutcome::succeeded); // two is MRU-front now
  // replay pulls the first one back to the front
  REQUIRE(gateway.open_recent(project) == ProjectEntryOutcome::succeeded);
  CHECK(launcher.args.front() == std::filesystem::weakly_canonical(project).string());
  listed = gateway.recent_projects();
  REQUIRE(listed.size() == 2);
  CHECK(listed.front() == std::filesystem::weakly_canonical(project));
  launcher.invoked = false;
  CHECK(gateway.open_recent(scratch.root / "never_a_project") ==
        ProjectEntryOutcome::refused_target);
  CHECK_FALSE(launcher.invoked);

  // (v) The async pick forwards to the injected dialog, exactly as the derived class's.
  dialog.next = scratch.root / "chosen";
  std::optional<std::filesystem::path> got;
  gateway.pick_folder([&got](std::optional<std::filesystem::path> p) { got = p; });
  CHECK(dialog.shown);
  REQUIRE(got.has_value());
  CHECK(*got == scratch.root / "chosen");
}

TEST_CASE("a session-free gateway answers every session verb inertly", "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  ProjectEntryGateway gateway(recent, fs, dialog, launcher, k_exe);

  // A launcher has no session to publish, navigate or sweep, so every session verb is
  // a value-returning no-op rather than a null-guarded branch (D-welcome-6). Driven
  // through the L3 seam, which is how a `dock` host would reach them.
  ace::dock::ProjectGateway& seam = gateway;
  CHECK_FALSE(seam.save());
  CHECK_FALSE(seam.is_dirty());
  CHECK_FALSE(seam.can_undo());
  CHECK_FALSE(seam.can_redo());
  CHECK_FALSE(seam.undo());
  CHECK_FALSE(seam.redo());
  for (const bool preview : {true, false}) {
    const ace::dock::GcSummary summary = seam.clean_up(preview);
    CHECK(summary.reclaimed_files == 0);
    CHECK(summary.reclaimed_bytes == 0);
    CHECK_FALSE(summary.ran); // no sweep ran — never a phantom reclaim
  }
  // Save As now answers on the outcome seam too (A25 / D-save_as_outcome-1), and is still
  // inert, because a launcher has no session to copy. `refused_target` is the honest inert
  // answer — this gateway publishes nothing and launches nothing, so it can report neither a
  // failed publish nor a failed spawn. No pick is opened and nothing is published.
  CHECK(seam.save_as(scratch.root, "Copy") == ProjectEntryOutcome::refused_target);
  CHECK_FALSE(dialog.shown);

  // The non-pure virtuals keep their inherited neutral defaults too, so a welcome
  // hosting this gateway offers nothing to insert, delete, frame or apologise for.
  CHECK(seam.insert_kinds().empty());
  CHECK(seam.insert_cell("org.arbc.raster", {}) == "Insert is unavailable.");
  CHECK_FALSE(seam.can_delete());
  CHECK(seam.delete_selected() == 0);
  CHECK_FALSE(seam.can_frame_selection());
  CHECK_FALSE(seam.frame_selection());
  CHECK_FALSE(seam.can_new_shot_from_view());
  CHECK_FALSE(seam.new_shot_from_view());
  CHECK(seam.reopen_unbindable_count() == 0);

  // Not one of them reached a process launch either (A13's verbs are in-process, and
  // there is no process here to be in).
  CHECK_FALSE(launcher.invoked);
}

// --- editor.project.entry_outcome — the three-outcome entry seam (A24) ---------------
// The verbs used to return one `bool` meaning "validated AND spawned", so `dock` could not
// tell a refused target from a failed launch and inferred it from MRU membership. These three
// cases pin the outcome the seam now reports directly — one case per enumerator, driven
// through the SESSION-FREE base so no `AppState` is involved, with `launcher.invoked` as the
// independent witness of whether anything was ever spawned.

TEST_CASE("ProjectEntryGateway reports refused_target for every pre-launch refusal",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  ProjectEntryGateway gateway(recent, fs, dialog, launcher, k_exe);

  // Four causes, ONE outcome (D-entry_outcome-2): the split is by corrective act — every one
  // of these is answered by the user choosing a different target — not by cause.
  SECTION("open_project on a directory with no project.arbc") {
    const std::filesystem::path plain = scratch.root / "plain";
    std::error_code ec;
    std::filesystem::create_directories(plain, ec);
    CHECK(gateway.open_project(plain) == ProjectEntryOutcome::refused_target);
  }

  SECTION("open_recent on a directory pruned since the list was rendered") {
    CHECK(gateway.open_recent(scratch.root / "never_a_project") ==
          ProjectEntryOutcome::refused_target);
  }

  SECTION("new_project with an empty, blank or traversing name") {
    for (const std::string& bad :
         {std::string(""), std::string("  "), std::string("bad/name"), std::string("..")}) {
      CHECK(gateway.new_project(scratch.root, bad) == ProjectEntryOutcome::refused_target);
    }
  }

  SECTION("new_project on a target that already exists (D-dir_is_project-3)") {
    const std::filesystem::path taken = scratch.root / "Taken";
    std::error_code ec;
    std::filesystem::create_directories(taken, ec);
    CHECK(gateway.new_project(scratch.root, "Taken") == ProjectEntryOutcome::refused_target);
  }

  // A refusal must still spawn NOTHING — the property that makes `refused_target` and
  // `spawn_failed` disjoint rather than merely differently worded.
  CHECK_FALSE(launcher.invoked);
  CHECK(gateway.recent_projects().empty()); // …and record nothing either
}

TEST_CASE("ProjectEntryGateway reports spawn_failed when the sibling exec fails",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  FailingLauncher launcher; // the target is accepted; the `exec` is what fails
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  ProjectEntryGateway gateway(recent, fs, dialog, launcher, k_exe);

  SECTION("open_project on a valid project") {
    const std::filesystem::path project = make_project(scratch.root, "proj");
    CHECK(gateway.open_project(project) == ProjectEntryOutcome::spawn_failed);
    CHECK(launcher.invoked);
    // The record-then-spawn ordering is UNCHANGED (Constraint 8) — it is simply no longer
    // load-bearing for the message. This is the exact configuration the deleted MRU inference
    // read as "still listed, so the spawn failed"; the outcome now says so on its own.
    const std::vector<std::filesystem::path> listed = gateway.recent_projects();
    REQUIRE(listed.size() == 1);
    CHECK(listed.front() == std::filesystem::weakly_canonical(project));
  }

  SECTION("open_recent on a valid project") {
    const std::filesystem::path project = make_project(scratch.root, "proj");
    CHECK(gateway.open_recent(project) == ProjectEntryOutcome::spawn_failed);
    CHECK(launcher.invoked);
    const std::vector<std::filesystem::path> listed = gateway.recent_projects();
    REQUIRE(listed.size() == 1);
    CHECK(listed.front() == std::filesystem::weakly_canonical(project));
  }

  SECTION("new_project on a valid not-yet-existing target") {
    CHECK(gateway.new_project(scratch.root, "Fresh") == ProjectEntryOutcome::spawn_failed);
    CHECK(launcher.invoked);
    CHECK_FALSE(std::filesystem::exists(scratch.root / "Fresh")); // the child never ran
    CHECK(gateway.recent_projects().empty());                     // New records nothing
  }
}

TEST_CASE("ProjectEntryGateway reports succeeded only when it validated AND spawned",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  ProjectEntryGateway gateway(recent, fs, dialog, launcher, k_exe);

  // `succeeded` keeps the D-welcome-8 meaning of the old `true` exactly: both halves happened.
  // Each verb is driven in ISOLATION here (the sequenced walk lives in the case above), so a
  // verb cannot borrow another's MRU state or launcher args.
  SECTION("open_project validates, records MRU-front and spawns the canonical path") {
    const std::filesystem::path project = make_project(scratch.root, "proj");
    CHECK(gateway.open_project(project) == ProjectEntryOutcome::succeeded);
    REQUIRE(launcher.invoked);
    CHECK(launcher.exe == k_exe);
    REQUIRE(launcher.args.size() == 1);
    CHECK(launcher.args.front() == std::filesystem::weakly_canonical(project).string());
    const std::vector<std::filesystem::path> listed = gateway.recent_projects();
    REQUIRE(listed.size() == 1);
    CHECK(listed.front() == std::filesystem::weakly_canonical(project));
  }

  SECTION("open_recent replays a still-valid entry MRU-front and spawns") {
    const std::filesystem::path project = make_project(scratch.root, "proj");
    REQUIRE(gateway.open_project(project) == ProjectEntryOutcome::succeeded);
    CHECK(gateway.open_recent(project) == ProjectEntryOutcome::succeeded);
    REQUIRE(launcher.args.size() == 1);
    CHECK(launcher.args.front() == std::filesystem::weakly_canonical(project).string());
    const std::vector<std::filesystem::path> listed = gateway.recent_projects();
    REQUIRE(listed.size() == 1);
    CHECK(listed.front() == std::filesystem::weakly_canonical(project));
  }

  SECTION("new_project spawns on the composed, not-yet-existing target and records nothing") {
    CHECK(gateway.new_project(scratch.root, "Fresh") == ProjectEntryOutcome::succeeded);
    REQUIRE(launcher.invoked);
    REQUIRE(launcher.args.size() == 1);
    const std::filesystem::path target = scratch.root / "Fresh";
    CHECK(launcher.args.front() == std::filesystem::weakly_canonical(target).string());
    CHECK_FALSE(std::filesystem::exists(target)); // the create signal to the child
    CHECK(gateway.recent_projects().empty());     // New records nothing
  }
}

TEST_CASE("AppProjectGateway::open_project validates, records MRU-front, spawns",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);

  const std::filesystem::path project = make_project(scratch.root, "proj");
  REQUIRE(gateway.open_project(project) == ProjectEntryOutcome::succeeded);
  REQUIRE(launcher.invoked);
  REQUIRE(launcher.exe == k_exe);
  REQUIRE(launcher.args.size() == 1);
  REQUIRE(launcher.args.front() == std::filesystem::weakly_canonical(project).string());

  const std::vector<std::filesystem::path> listed = gateway.recent_projects();
  REQUIRE(listed.size() == 1);
  REQUIRE(listed.front() == std::filesystem::weakly_canonical(project));
}

TEST_CASE("AppProjectGateway::open_project rejects a non-project, spawning nothing",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);

  const std::filesystem::path plain = scratch.root / "plain";
  std::error_code ec;
  std::filesystem::create_directories(plain, ec);
  REQUIRE(gateway.open_project(plain) == ProjectEntryOutcome::refused_target);
  REQUIRE_FALSE(launcher.invoked);
  REQUIRE(gateway.recent_projects().empty());
}

TEST_CASE("AppProjectGateway::new_project composes a non-existent target and spawns",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);

  REQUIRE(gateway.new_project(scratch.root, "Fresh") == ProjectEntryOutcome::succeeded);
  REQUIRE(launcher.invoked);
  REQUIRE(launcher.args.size() == 1);
  const std::filesystem::path target = scratch.root / "Fresh";
  REQUIRE(launcher.args.front() == std::filesystem::weakly_canonical(target).string());
  // The create signal to the child: the composed target does not yet exist.
  REQUIRE_FALSE(std::filesystem::exists(target));
  // New records nothing (the directory is not yet a project).
  REQUIRE(gateway.recent_projects().empty());

  // An invalid name spawns nothing.
  launcher.invoked = false;
  REQUIRE(gateway.new_project(scratch.root, "bad/name") == ProjectEntryOutcome::refused_target);
  REQUIRE_FALSE(launcher.invoked);
}

TEST_CASE("AppProjectGateway::new_project refuses an existing target without spawning",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);

  // New spawns a DETACHED sibling (D19/A7), so a refusal raised only inside the child would be
  // a window that never appears. The L4 pre-check (D-dir_is_project-3) is therefore the entire
  // user-visible half of D27's rule on this path: composed target exists -> `false`, before
  // the spawn.
  const std::filesystem::path taken = scratch.root / "Taken";
  std::error_code ec;
  std::filesystem::create_directories(taken, ec);
  std::ofstream(taken / "someones_file.txt") << "unrelated";

  REQUIRE(gateway.new_project(scratch.root, "Taken") == ProjectEntryOutcome::refused_target);
  CHECK_FALSE(launcher.invoked);            // zero launches: no process burned on a mistake
  CHECK(gateway.recent_projects().empty()); // the MRU is not touched on a refusal
  CHECK(std::filesystem::exists(taken / "someones_file.txt")); // the directory is untouched
  CHECK_FALSE(fs.exists(ace::project::project_layout(taken).assets_dir));

  // …and an EMPTY existing directory is refused just the same — exists-at-all, no "empty is
  // fine" exception (D-dir_is_project-1).
  const std::filesystem::path empty_dir = scratch.root / "Empty";
  std::filesystem::create_directories(empty_dir, ec);
  REQUIRE(gateway.new_project(scratch.root, "Empty") == ProjectEntryOutcome::refused_target);
  CHECK_FALSE(launcher.invoked);
}

TEST_CASE("AppProjectGateway::open_recent re-orders MRU-front and spawns",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);

  const std::filesystem::path one = make_project(scratch.root, "one");
  const std::filesystem::path two = make_project(scratch.root, "two");
  REQUIRE(gateway.open_project(one) == ProjectEntryOutcome::succeeded);
  REQUIRE(gateway.open_project(two) == ProjectEntryOutcome::succeeded); // two is MRU-front now

  // replay re-orders one to the front
  REQUIRE(gateway.open_recent(one) == ProjectEntryOutcome::succeeded);
  REQUIRE(launcher.args.front() == std::filesystem::weakly_canonical(one).string());
  const std::vector<std::filesystem::path> listed = gateway.recent_projects();
  REQUIRE(listed.size() == 2);
  REQUIRE(listed.front() == std::filesystem::weakly_canonical(one));

  // A recent entry that is no longer a project is rejected (no spawn).
  launcher.invoked = false;
  REQUIRE(gateway.open_recent(scratch.root / "never_a_project") ==
          ProjectEntryOutcome::refused_target);
  REQUIRE_FALSE(launcher.invoked);
}

TEST_CASE("AppProjectGateway::save publishes the in-process session; is_dirty tracks it",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);

  // Save acts on the in-process session (A13), not a sibling exec: no launch.
  CHECK(gateway.is_dirty()); // a fresh create_project session
  REQUIRE(gateway.save());
  CHECK_FALSE(gateway.is_dirty());
  CHECK(fs.exists(session.layout().canonical));
  REQUIRE_FALSE(launcher.invoked);
}

// editor.project.reopen_degradation_notice — the A19 count crossing the L3→L4 inversion
// (A12/A13), asserted headless exactly as `is_dirty()` above is. `dock` may include neither
// `ace/commands` nor `ace/scene`, so the rail can only learn the session's degradation by
// asking this seam; these cases pin that the number it gets back is the session's own.
TEST_CASE("AppProjectGateway::reopen_unbindable_count reports the session's carried count",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);

  SECTION("a degraded session's count round-trips through the seam") {
    auto session = make_degraded_session(fs, scratch.root / "degraded", 5);
    AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);
    CHECK(gateway.reopen_unbindable_count() == 5);
    // A pure REPORTER (D-reopen_degradation_notice-5): re-querying never consumes the fact,
    // because the one-shot latch lives on the Dockspace, not here.
    CHECK(gateway.reopen_unbindable_count() == 5);
    REQUIRE_FALSE(launcher.invoked); // a session query, never a sibling exec
  }

  SECTION("an undegraded session reports zero") {
    auto session = make_session(fs, scratch.root / "clean");
    AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);
    CHECK(gateway.reopen_unbindable_count() == 0);
  }

  SECTION("the base-class default is an inert zero for an unwired gateway") {
    InertGateway inert;
    const ace::dock::ProjectGateway& seam = inert;
    CHECK(seam.reopen_unbindable_count() == 0);
  }
}

TEST_CASE("AppProjectGateway::save_as composes parent + name, publishes a copy, and execs a "
          "sibling",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);

  // Save As now takes a parent + a typed name, exactly as New does (D27 / D-dir_is_project-4),
  // and is SYNCHRONOUS: the rail owns the `pick_folder` half, so this verb opens no dialog of
  // its own and its outcome is its return value.
  REQUIRE(gateway.save_as(scratch.root, "Copy") == ProjectEntryOutcome::succeeded);
  CHECK_FALSE(dialog.shown); // save_as itself opens no folder dialog any more

  // The composed target now exists and holds the portable core — project.arbc + assets/ +
  // the workspace/-excluding .gitignore — and the sibling exec was fired on that same
  // absolute path.
  const std::filesystem::path target = scratch.root / "Copy";
  const auto target_layout = ace::project::project_layout(target);
  CHECK(fs.exists(target_layout.canonical));
  CHECK(fs.exists(target_layout.assets_dir));
  CHECK(fs.exists(target_layout.gitignore));
  REQUIRE(launcher.invoked);
  REQUIRE(launcher.args.size() == 1);
  CHECK(launcher.args.front() == std::filesystem::weakly_canonical(target).string());

  // The current session is untouched (D-save_as-2): still dirty, still its own root.
  CHECK(gateway.is_dirty());
  CHECK(session.layout().canonical == (scratch.root / "session" / "project.arbc"));
}

TEST_CASE("AppProjectGateway::save_as refuses an existing target and an invalid name without "
          "publishing or spawning",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);

  // This case is also the regression that A25 did NOT change what a refusal means: both halves
  // still answer `refused_target`, and D27 / D-dir_is_project-6's one refusal string is
  // byte-identical after the widening.

  // (i) A composed target that already exists — an EMPTY directory, the case the shipped
  //     narrow clobber guard let through (D27's direct reversal). Refused as a value the
  //     rail renders on the same frame; nothing published, nothing spawned. The refusal is
  //     produced deep in L1 (`project`'s existing-target guard, classified `refused` by
  //     `commands`) and translated back into the dock's vocabulary here.
  const std::filesystem::path taken = scratch.root / "Taken";
  std::error_code ec;
  std::filesystem::create_directories(taken, ec);
  CHECK(gateway.save_as(scratch.root, "Taken") == ProjectEntryOutcome::refused_target);
  CHECK_FALSE(launcher.invoked);
  CHECK_FALSE(fs.exists(ace::project::project_layout(taken).canonical));
  CHECK_FALSE(fs.exists(ace::project::project_layout(taken).assets_dir));

  // (ii) A name `compose_new_project_target` refuses — empty, and one that would traverse out
  //      of the parent. Same `refused_target`, still zero writes and zero launches, and this
  //      half never reaches `commands` at all (the L4 compose guard answers directly).
  for (const std::string& bad :
       {std::string(""), std::string("  "), std::string("bad/name"), std::string("..")}) {
    CHECK(gateway.save_as(scratch.root, bad) == ProjectEntryOutcome::refused_target);
    CHECK_FALSE(launcher.invoked);
  }

  // The session is untouched throughout (D-save_as-2).
  CHECK(gateway.is_dirty());
  CHECK(session.layout().canonical == (scratch.root / "session" / "project.arbc"));
}

TEST_CASE("AppProjectGateway::save_as reports publish_failed when the copy cannot be written",
          "[app_project_gateway]") {
  // The stage L4 could not previously name (A25): the target was ACCEPTED — it is a valid,
  // not-yet-existing name — and producing the copy is what failed. The shipped `bool` collapsed
  // this onto the rail's "Enter a project name that does not already exist here.", telling a
  // user with a full disk to retype a name that was never the problem.
  ScratchDir scratch;
  FaultyFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);

  // Armed only AFTER the session's own bootstrap, so exactly the copy's publish faults.
  fs.fail_make_directories = true;

  CHECK(gateway.save_as(scratch.root, "Copy") == ProjectEntryOutcome::publish_failed);
  CHECK_FALSE(launcher.invoked); // a failed publish execs nothing (no sibling on a non-bundle)
  CHECK_FALSE(fs.exists(ace::project::project_layout(scratch.root / "Copy").canonical));

  // The current session is untouched on the fault path too (D-save_as-2).
  CHECK(gateway.is_dirty());
  CHECK(session.layout().canonical == (scratch.root / "session" / "project.arbc"));
}

TEST_CASE("AppProjectGateway::save_as reports spawn_failed and leaves the copy on disk",
          "[app_project_gateway]") {
  // The bug this leaf exists to kill, at the level that produces it: the publish SUCCEEDED and
  // the sibling `exec` failed, so a complete, valid copy is sitting in the target directory
  // while the shipped UI told the user to type a different name — and the obvious retry with the
  // same name then hit D27's existing-target guard and produced that same message for an
  // entirely different reason.
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  FailingLauncher launcher; // the target is accepted and the copy lands; the `exec` is what fails
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);

  CHECK(gateway.save_as(scratch.root, "Copy") == ProjectEntryOutcome::spawn_failed);
  CHECK(launcher.invoked); // the exec was attempted, which is only possible after a publish

  // …and the copy really is on disk — the fact `spawn_failed` exists to let the rail state.
  const std::filesystem::path target = scratch.root / "Copy";
  const auto target_layout = ace::project::project_layout(target);
  CHECK(fs.exists(target_layout.canonical));
  CHECK(fs.exists(target_layout.assets_dir));

  // The current session is STILL its own, on this path as on every other (D-save_as-2): no
  // `mark_saved`, no `layout_` rebind.
  CHECK(gateway.is_dirty());
  CHECK(session.layout().canonical == (scratch.root / "session" / "project.arbc"));
}

TEST_CASE("AppProjectGateway::undo/redo run their mutation inside the edit runner (A13, "
          "edit_render_sync)",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);

  // The edit runner (editor.canvas.single_writer): the shell binds this to CanvasHost::apply_edit
  // so the undo/redo mutation runs on the writer thread (the render read is lock-free via arbc
  // v0.2.0 COW content bindings). Here a FAKE runner records the ordering — runner
  // entered -> mutation ran -> runner exited — proving the gateway funnels the Document
  // mutation THROUGH the runner (not a bare fire-after poke). `can_redo()` is sampled just
  // before and just after `edit()` inside the runner: a moved undo flips it false->true
  // WHILE the runner is on the stack, witnessing that the mutation ran between enter and exit.
  int entered = 0;
  int exited = 0;
  bool redo_before_mutation = true;
  bool redo_after_mutation = false;
  gateway.set_edit_runner([&](const std::function<void()>& edit) {
    ++entered;
    redo_before_mutation = gateway.can_redo();
    edit();
    redo_after_mutation = gateway.can_redo();
    ++exited;
  });

  // A fresh session has an empty journal cursor: nothing to navigate. A no-op undo/redo runs
  // its (inert) mutation through the runner and reports false — harmless, no crash (Constraint 4).
  CHECK_FALSE(gateway.can_undo());
  CHECK_FALSE(gateway.can_redo());
  CHECK_FALSE(gateway.undo()); // an empty journal is inert (D-undo)
  CHECK_FALSE(gateway.redo());
  CHECK(entered == exited); // every entry matched an exit — the runner never dangled

  // One edit through the seam pushes exactly one journal entry.
  ace::commands::dispatch(session,
                          ace::commands::Command{"add_composition", [](arbc::Document& doc) {
                                                   doc.add_composition(64.0, 64.0);
                                                 }});
  CHECK(gateway.can_undo());
  CHECK_FALSE(gateway.can_redo());

  // Undo navigates the cursor back and reports the move; the mutation ran INSIDE the runner
  // (redo went false->true across `edit()`). No sibling exec is fired for either (A13).
  const int entered_before_undo = entered;
  REQUIRE(gateway.undo());
  CHECK(entered == entered_before_undo + 1);
  CHECK(exited == entered);          // entered -> mutation -> exited, balanced
  CHECK_FALSE(redo_before_mutation); // pre-mutation: nothing to redo yet
  CHECK(redo_after_mutation);        // post-mutation (still inside the runner): redo now available
  CHECK(gateway.can_redo());

  // Redo navigates forward again, likewise through the runner.
  const int entered_before_redo = entered;
  REQUIRE(gateway.redo());
  CHECK(entered == entered_before_redo + 1);
  CHECK(exited == entered);
  CHECK(gateway.can_undo());
  REQUIRE_FALSE(launcher.invoked);
}

TEST_CASE("AppProjectGateway::undo/redo run the mutation directly when no runner is installed",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);

  // No runner installed (a headless session with no live canvas): the mutation runs directly
  // on the calling thread — behaviour-identical to a wired runner, still single-threaded, so
  // undo/redo navigate the journal exactly as before (D-edit_render_sync-2 default path).
  ace::commands::dispatch(session,
                          ace::commands::Command{"add_composition", [](arbc::Document& doc) {
                                                   doc.add_composition(64.0, 64.0);
                                                 }});
  REQUIRE(gateway.can_undo());
  REQUIRE(gateway.undo());
  CHECK(gateway.can_redo());
  REQUIRE(gateway.redo());
  CHECK(gateway.can_undo());
  REQUIRE_FALSE(launcher.invoked);
}

TEST_CASE("AppProjectGateway::clean_up previews then reclaims the session's orphan blobs (A13)",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);

  // Publish a canonical to root the sweep on, then seed an orphan blob under the
  // session's assets/tiles/ (nothing in the canonical references it).
  REQUIRE(gateway.save());
  const std::filesystem::path tiles = session.layout().assets_dir / "tiles" / "aa";
  std::error_code ec;
  std::filesystem::create_directories(tiles, ec);
  const std::string orphan(32, 'a');        // a well-formed 32-hex tile hash
  const std::string bytes = "orphan-bytes"; // 12 bytes
  std::ofstream(tiles / orphan, std::ios::binary) << bytes;

  // Preview (dry-run) reports the orphan — files + bytes — and deletes nothing.
  const ace::dock::GcSummary preview = gateway.clean_up(/*preview=*/true);
  CHECK(preview.ran);
  CHECK(preview.reclaimed_files == 1);
  CHECK(preview.reclaimed_bytes == bytes.size());
  CHECK(std::filesystem::exists(tiles / orphan));

  // Commit reclaims it; the arbc -> project -> dock mapping carries the counts.
  const ace::dock::GcSummary swept = gateway.clean_up(/*preview=*/false);
  CHECK(swept.ran);
  CHECK(swept.reclaimed_files == 1);
  CHECK(swept.reclaimed_bytes == bytes.size());
  CHECK_FALSE(std::filesystem::exists(tiles / orphan));

  // Clean up is an in-process verb (A13), not a sibling exec.
  REQUIRE_FALSE(launcher.invoked);
}

TEST_CASE("AppProjectGateway::pick_folder forwards and survives teardown mid-pick",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");

  // Immediate delivery: pick_folder forwards to the dialog and the callback runs.
  {
    AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);
    dialog.next = scratch.root / "chosen";
    std::optional<std::filesystem::path> got;
    gateway.pick_folder([&got](std::optional<std::filesystem::path> p) { got = p; });
    REQUIRE(dialog.shown);
    REQUIRE(got.has_value());
    REQUIRE(*got == scratch.root / "chosen");
  }

  // Deferred delivery across gateway teardown (shutdown-cancel): destroying the
  // gateway with a pick in flight must not dangle. The callback captures only
  // test-owned state, so a later delivery is safe and sanitizer-clean.
  bool delivered = false;
  dialog.defer = true;
  {
    AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);
    gateway.pick_folder([&delivered](std::optional<std::filesystem::path>) { delivered = true; });
  }
  dialog.deliver();
  REQUIRE(delivered);
}

// --- Frame Selection (editor.cameras.frame_selection / D23) -------------------------------
// The L4 join over the headless direct-invoke edit runner (no shell, no canvas): the branch
// that proves the two new virtuals are honest without any ImGui.

TEST_CASE("AppProjectGateway::frame_selection mints one camera fit to the selection",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  session.document().add_composition(256.0, 256.0);
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);
  // Deliberately no set_edit_runner: the direct-invoke default (Constraint 8's headless arm).

  // Two 32x32 rasters, placed apart: the framed region is the union of BOTH placed extents.
  const arbc::expected<arbc::ObjectId, std::string> left =
      ace::scene::add_cell(session.document(), session.registry(), "org.arbc.raster", "32x32",
                           arbc::Affine::translation(10.0, 10.0));
  const arbc::expected<arbc::ObjectId, std::string> right =
      ace::scene::add_cell(session.document(), session.registry(), "org.arbc.raster", "32x32",
                           arbc::Affine::translation(90.0, 42.0));
  REQUIRE(left.has_value());
  REQUIRE(right.has_value());

  CHECK_FALSE(gateway.can_frame_selection());
  session.selection().select(*left);
  session.selection().add(*right);
  CHECK(gateway.can_frame_selection());

  REQUIRE(gateway.frame_selection());
  const std::vector<ace::scene::Camera> minted = ace::scene::cameras(session.document());
  REQUIRE(minted.size() == 1);
  CHECK(minted[0].name == "Camera 1");
  // The union spans [10,122]x[10,74] — 112 x 64 composition units at 1 unit per pixel.
  CHECK(minted[0].resolution.width == 112);
  CHECK(minted[0].resolution.height == 64);
  const arbc::Rect covered = minted[0].frame.map_rect(arbc::Rect{0.0, 0.0, 112.0, 64.0});
  CHECK(covered.x0 <= 10.0);
  CHECK(covered.y0 <= 10.0);
  CHECK(covered.x1 >= 122.0);
  CHECK(covered.y1 >= 74.0);

  // The mint touches NEITHER the selection nor any look-through state (D-frame_selection-10).
  CHECK(session.selection().size() == 2);
  CHECK(session.selection().contains(*left));
  CHECK(session.selection().contains(*right));

  // A second click on the SAME selection advances the auto-name and repeats the geometry.
  REQUIRE(gateway.frame_selection());
  const std::vector<ace::scene::Camera> again = ace::scene::cameras(session.document());
  REQUIRE(again.size() == 2);
  CHECK(again[1].name == "Camera 2");
  CHECK(again[1].frame == minted[0].frame);
  CHECK(again[1].resolution == minted[0].resolution);
}

TEST_CASE("AppProjectGateway::frame_selection refuses an empty or all-unbounded selection",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  session.document().add_composition(64.0, 64.0);
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);

  // A factory-built `org.arbc.solid` is UNBOUNDED (D-cells_model-3): no region to frame.
  const arbc::expected<arbc::ObjectId, std::string> fill =
      ace::scene::add_cell(session.document(), session.registry(), "org.arbc.solid", "0.6,0,0,1",
                           arbc::Affine::identity());
  REQUIRE(fill.has_value());

  const std::size_t depth_before = session.document().journal().depth();
  const std::uint64_t revision_before = session.document().pin()->revision();

  // (i) Nothing selected: the gate is false and the verb mutates nothing.
  CHECK_FALSE(gateway.can_frame_selection());
  CHECK_FALSE(gateway.frame_selection());

  // (ii) Only unbounded content selected: the COARSE gate is true (D-frame_selection-7) but
  // the verb still refuses — as a value, with the document untouched (Constraint 5).
  session.selection().select(*fill);
  CHECK(gateway.can_frame_selection());
  CHECK_FALSE(gateway.frame_selection());

  CHECK(ace::scene::cameras(session.document()).empty());
  CHECK(session.document().journal().depth() == depth_before);
  CHECK(session.document().pin()->revision() == revision_before);
  CHECK(session.selection().size() == 1); // a refusal does not clear the selection either
}

TEST_CASE("AppProjectGateway::frame_selection runs its mutation inside the edit runner",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  session.document().add_composition(64.0, 64.0);
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);

  const arbc::expected<arbc::ObjectId, std::string> cell = ace::scene::add_cell(
      session.document(), session.registry(), "org.arbc.raster", "16x16", arbc::Affine::identity());
  REQUIRE(cell.has_value());
  session.selection().select(*cell);

  // Every UI-driven mint runs inside `CanvasView::apply_edit` (Constraint 8): the whole join —
  // the pick_targets READ as well as the add_camera WRITE — happens inside the closure.
  int runs = 0;
  bool camera_inside = false;
  gateway.set_edit_runner([&](const std::function<void()>& edit) {
    ++runs;
    edit();
    camera_inside = !ace::scene::cameras(session.document()).empty();
  });

  REQUIRE(gateway.frame_selection());
  CHECK(runs == 1);
  CHECK(camera_inside);
  CHECK(ace::scene::cameras(session.document()).size() == 1);
}

// --- New Shot From View (editor.cameras.new_shot_from_view / D23) --------------------------
// The sibling join, whose source region is L4 SESSION state (the installed ViewFraming
// provider) rather than the document — so these are the cases no L1 suite can host.

TEST_CASE("AppProjectGateway::new_shot_from_view promotes the live framing into a shot",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  session.document().add_composition(256.0, 256.0);
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);

  // A nav-produced viewport camera (uniform scale + translation) over an 800x600 pane.
  const arbc::Affine viewport{2.0, 0.0, 0.0, 2.0, 30.0, -40.0};
  const int pane_w = 800;
  const int pane_h = 600;
  gateway.set_view_framing([&] { return ace::app::ViewFraming{viewport, pane_w, pane_h}; });

  CHECK(gateway.can_new_shot_from_view());
  REQUIRE(gateway.new_shot_from_view());

  const std::vector<ace::scene::Camera> minted = ace::scene::cameras(session.document());
  REQUIRE(minted.size() == 1);
  CHECK(minted[0].name == "Camera 1");
  // Resolution is the pane in DEVICE PIXELS, verbatim (amended D23 / D-new_shot_from_view-1).
  CHECK(minted[0].resolution.width == pane_w);
  CHECK(minted[0].resolution.height == pane_h);
  // …and the frame is the viewport camera inverted, so the shot rendered at its own
  // resolution reproduces the view.
  const std::optional<arbc::Affine> inverse = viewport.inverse();
  REQUIRE(inverse.has_value());
  CHECK(minted[0].frame == *inverse);

  // The mint touches neither the selection nor the naming of the OTHER verb: one shared
  // `Camera <n>` sequence (D-new_shot_from_view-7).
  CHECK(session.selection().empty());
  REQUIRE(gateway.new_shot_from_view());
  const std::vector<ace::scene::Camera> again = ace::scene::cameras(session.document());
  REQUIRE(again.size() == 2);
  CHECK(again[1].name == "Camera 2");
}

TEST_CASE("AppProjectGateway::new_shot_from_view refuses when no canvas pane is live",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  session.document().add_composition(64.0, 64.0);
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);

  const std::size_t depth_before = session.document().journal().depth();

  // (i) NO provider installed at all (the headless arm, and the state a session reaches once
  // every canvas is closed — D18 has no keep-a-canvas guardrail).
  CHECK_FALSE(gateway.can_new_shot_from_view());
  CHECK_FALSE(gateway.new_shot_from_view());

  // (ii) A provider reporting a ZERO pane — "no live canvas" (view_framing.hpp:15).
  gateway.set_view_framing([] { return ace::app::ViewFraming{arbc::Affine::identity(), 0, 0}; });
  CHECK_FALSE(gateway.can_new_shot_from_view());
  CHECK_FALSE(gateway.new_shot_from_view());

  // The root-composition fallback is NOT substituted (Constraint 4 / D-new_shot_from_view-2):
  // with no pane there is nothing the user could be promoting, so the mint refuses as a value
  // with the document untouched.
  CHECK(ace::scene::cameras(session.document()).empty());
  CHECK(session.document().journal().depth() == depth_before);

  // …but `insert_cell`'s provisional placement still HAS that fallback in the very same state,
  // which is what the `view_framing()` split preserves: the refactor must not regress the one
  // consumer the fallback exists for.
  const std::string error = gateway.insert_cell("org.arbc.raster", {{"size", "16x16"}});
  CHECK(error.empty());
  CHECK(session.document().journal().depth() > depth_before);
}

TEST_CASE("AppProjectGateway::new_shot_from_view runs its mutation inside the edit runner",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  session.document().add_composition(64.0, 64.0);
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);
  gateway.set_view_framing(
      [] { return ace::app::ViewFraming{arbc::Affine::identity(), 320, 240}; });

  // Constraint 5: the ViewFraming READ as well as the add_camera WRITE happen inside the
  // closure, so the pane size the transaction records is the one live when it lands.
  int runs = 0;
  bool empty_before_run = false;
  bool camera_inside = false;
  gateway.set_edit_runner([&](const std::function<void()>& edit) {
    ++runs;
    empty_before_run = ace::scene::cameras(session.document()).empty();
    edit();
    camera_inside = !ace::scene::cameras(session.document()).empty();
  });

  REQUIRE(gateway.new_shot_from_view());
  CHECK(runs == 1);
  CHECK(empty_before_run);
  CHECK(camera_inside);
  CHECK(ace::scene::cameras(session.document()).size() == 1);
}

TEST_CASE("AppProjectGateway::new_shot_from_view mints undo one-for-one", "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  session.document().add_composition(64.0, 64.0);
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);
  gateway.set_view_framing([] { return ace::app::ViewFraming{arbc::Affine::identity(), 128, 96}; });

  const std::size_t depth_before = session.document().journal().depth();
  REQUIRE(gateway.new_shot_from_view());
  REQUIRE(gateway.new_shot_from_view());
  REQUIRE(ace::scene::cameras(session.document()).size() == 2);
  // `scene::add_camera`'s documented shape (model.md:391): TWO journal entries per create —
  // `add_content` self-commits, the binding layer is a second transaction — so two mints cost
  // exactly four entries.
  CHECK(session.document().journal().depth() == depth_before + 4);

  // …of which only the BINDING entry is observable through `scene::cameras`, which keys off
  // composition membership: the press that pops a mint's attach removes that camera, and the
  // press that pops its add_content changes nothing visible. Undoing back to zero cameras
  // therefore takes three presses, not two — the accounting `frame_selection`'s e2e sees as
  // "one Ctrl+Z removes the camera I just minted".
  CHECK(gateway.undo());
  CHECK(ace::scene::cameras(session.document()).size() == 1);
  CHECK(gateway.undo());
  CHECK(ace::scene::cameras(session.document()).size() == 1);
  CHECK(gateway.undo());
  CHECK(ace::scene::cameras(session.document()).empty());
}
