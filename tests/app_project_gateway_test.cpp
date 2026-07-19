#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/project/project.hpp>

#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
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

} // namespace

using ace::app::AppProjectGateway;
using ace::dockmodel::RecentProjects;

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
  REQUIRE(gateway.open_project(project));
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
  REQUIRE_FALSE(gateway.open_project(plain));
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

  REQUIRE(gateway.new_project(scratch.root, "Fresh"));
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
  REQUIRE_FALSE(gateway.new_project(scratch.root, "bad/name"));
  REQUIRE_FALSE(launcher.invoked);
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
  REQUIRE(gateway.open_project(one));
  REQUIRE(gateway.open_project(two)); // two is MRU-front now

  REQUIRE(gateway.open_recent(one)); // replay re-orders one to the front
  REQUIRE(launcher.args.front() == std::filesystem::weakly_canonical(one).string());
  const std::vector<std::filesystem::path> listed = gateway.recent_projects();
  REQUIRE(listed.size() == 2);
  REQUIRE(listed.front() == std::filesystem::weakly_canonical(one));

  // A recent entry that is no longer a project is rejected (no spawn).
  launcher.invoked = false;
  REQUIRE_FALSE(gateway.open_recent(scratch.root / "never_a_project"));
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

TEST_CASE("AppProjectGateway::save_as picks a target, publishes a copy, and execs a sibling",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);

  // The folder pick resolves to a fresh, not-yet-existing target: save_as publishes
  // the in-process session's copy there (D-save_as-1) and execs a sibling on it.
  const std::filesystem::path target = scratch.root / "copy";
  dialog.next = target;
  gateway.save_as();

  REQUIRE(dialog.shown);
  // project.arbc appears at the target — the copy was published (not a sibling on the
  // source), and the sibling exec was fired with that same absolute path.
  CHECK(fs.exists(ace::project::project_layout(target).canonical));
  REQUIRE(launcher.invoked);
  REQUIRE(launcher.args.size() == 1);
  CHECK(launcher.args.front() == std::filesystem::weakly_canonical(target).string());

  // The current session is untouched (D-save_as-2): still dirty, still its own root.
  CHECK(gateway.is_dirty());
  CHECK(session.layout().canonical == (scratch.root / "session" / "project.arbc"));
}

TEST_CASE("AppProjectGateway::save_as on a cancelled pick publishes nothing and execs nothing",
          "[app_project_gateway]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecordingLauncher launcher;
  ScriptedFolderDialog dialog;
  RecentProjects recent(scratch.root / "prefs", fs);
  auto session = make_session(fs, scratch.root / "session");
  AppProjectGateway gateway(recent, fs, dialog, launcher, k_exe, session);

  dialog.next = std::nullopt; // the user cancelled the folder picker
  gateway.save_as();

  REQUIRE(dialog.shown);
  REQUIRE_FALSE(launcher.invoked); // a cancelled pick spawns nothing (D-save_as)
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

  // The edit-serializing runner (editor.canvas.edit_render_sync, D-edit_render_sync-2): the
  // shell binds this to CanvasHost::apply_edit so the undo/redo mutation runs inside the
  // render thread's `doc_mu` window. Here a FAKE runner records the ordering — runner
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
