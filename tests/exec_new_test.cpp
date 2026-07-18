// editor.project.exec_new — L1/L0 headless units for the "open another project"
// primitive: the commands::open_another_project action (canonicalization + empty
// rejection + forwarding to an injected launcher) and the platform seam
// (NativeProcessLauncher::spawn_detached actually launches a detached, reaped
// child; current_executable_path resolves this binary). Reuses the ScratchDir
// temp-dir pattern (tests/platform_test.cpp). The native spawn smoke is the leaf's
// ASan/TSan target (Constraint 1 / Acceptance "Threading").

#include <ace/commands/exec_new.hpp>
#include <ace/platform/process_launcher.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace {

// A temp dir wiped on entry and exit, named distinctly so the exec_new suite never
// collides with the other suites sharing the ace_tests binary.
struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_exec_new_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// An in-test fake ProcessLauncher recording (exe, args) — the seam the future
// picker injects, proving open_another_project forwards without a real spawn.
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

} // namespace

TEST_CASE("open_another_project forwards the exe and the absolute project path") {
  ScratchDir scratch;
  RecordingLauncher launcher;
  const std::filesystem::path exe = "/opt/arbitraryeditor/arbitraryeditor";
  const std::filesystem::path target = scratch.root / "proj";
  std::error_code ec;
  std::filesystem::create_directories(target, ec);

  const auto err = ace::commands::open_another_project(launcher, exe, target);
  CHECK_FALSE(static_cast<bool>(err));
  CHECK(launcher.invoked);
  CHECK(launcher.exe == exe);
  REQUIRE(launcher.args.size() == 1);
  CHECK(launcher.args[0] == std::filesystem::weakly_canonical(target).string());
}

TEST_CASE("open_another_project canonicalizes a relative target to an absolute path") {
  RecordingLauncher launcher;
  const std::filesystem::path relative = "some/relative/proj";

  const auto err = ace::commands::open_another_project(launcher, "/bin/editor", relative);
  CHECK_FALSE(static_cast<bool>(err));
  REQUIRE(launcher.args.size() == 1);
  const std::filesystem::path passed = launcher.args[0];
  CHECK(passed.is_absolute()); // the child no longer depends on the caller's CWD
  CHECK(passed == std::filesystem::weakly_canonical(std::filesystem::absolute(relative)));
}

TEST_CASE("open_another_project rejects an empty target and does not spawn") {
  RecordingLauncher launcher;

  const auto err = ace::commands::open_another_project(launcher, "/bin/editor", "");
  CHECK(static_cast<bool>(err)); // an error value, never a throw
  CHECK_FALSE(launcher.invoked); // the launcher is not touched
}

TEST_CASE("NativeProcessLauncher::spawn_detached launches an independent, reaped child") {
  ScratchDir scratch;
  ace::platform::NativeProcessLauncher launcher;
  const std::filesystem::path sentinel = scratch.root / "sentinel";
  // A detached shell that writes the sentinel then exits — proof the child ran as
  // its own process, independent of this one.
  const std::string script = "printf done > '" + sentinel.string() + "'";

  const auto err = launcher.spawn_detached("/bin/sh", {"-c", script});
  REQUIRE_FALSE(static_cast<bool>(err));

  // The sentinel appears within a bounded timeout (the child really ran).
  bool appeared = false;
  for (int i = 0; i < 500 && !appeared; ++i) {
    std::error_code ec;
    appeared = std::filesystem::exists(sentinel, ec);
    if (!appeared) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  CHECK(appeared);

  // The child is reaped without a lingering zombie (SA_NOCLDWAIT): once it exits
  // there is no waitable child left, so waitpid reports ECHILD rather than a
  // zombie's exit status.
  bool reaped = false;
  for (int i = 0; i < 500 && !reaped; ++i) {
    int status = 0;
    const pid_t r = ::waitpid(-1, &status, WNOHANG);
    if (r == -1 && errno == ECHILD) {
      reaped = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  CHECK(reaped);
}

TEST_CASE("NativeProcessLauncher::spawn_detached surfaces a launch failure as an error value") {
  ace::platform::NativeProcessLauncher launcher;
  const auto err = launcher.spawn_detached("/definitely/not/a/real/binary/xyzzy", {});
  CHECK(static_cast<bool>(err)); // no such executable — a value, never a throw
}

TEST_CASE("current_executable_path returns an existing absolute path") {
  const auto exe = ace::platform::current_executable_path();
  REQUIRE(exe.has_value());
  CHECK(exe.value().is_absolute());
  std::error_code ec;
  CHECK(std::filesystem::exists(exe.value(), ec)); // the running test binary
}
