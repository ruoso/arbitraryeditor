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
