// POSIX_SPAWN_SETSID (and readlink over /proc/self/exe) are POSIX.1-2008 / glibc
// extensions gated behind a feature-test macro; define it before any system
// header so the native launcher compiles under strict -std=c++20 too.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <ace/platform/process_launcher.hpp>

#include <string>
#include <vector>

#include <signal.h>
#include <spawn.h>
#include <unistd.h>

// The child's environment: inherited verbatim from this process (declared by
// <unistd.h> under POSIX, but spelt out here so the reference is unambiguous).
extern char** environ;

namespace ace::platform {

std::error_code NativeProcessLauncher::spawn_detached(const std::filesystem::path& executable,
                                                      const std::vector<std::string>& args) const {
  // argv: the executable as argv[0], then the caller's args, nullptr-terminated.
  // `storage` owns the bytes; `argv` points into it (posix_spawn wants char*).
  std::vector<std::string> storage;
  storage.reserve(args.size() + 1);
  storage.push_back(executable.string());
  for (const auto& arg : args) {
    storage.push_back(arg);
  }
  std::vector<char*> argv;
  argv.reserve(storage.size() + 1);
  for (auto& piece : storage) {
    argv.push_back(piece.data());
  }
  argv.push_back(nullptr);

  // Reap detached children without a lingering zombie, without blocking the
  // calling thread: SA_NOCLDWAIT tells the kernel to discard child exit status,
  // so a fire-and-forget spawn never accumulates zombies (Constraint 1). This is
  // a one-shot disposition change, no shared mutable state to synchronize.
  struct sigaction sa{};
  sa.sa_handler = SIG_DFL;
  sa.sa_flags = SA_NOCLDWAIT;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGCHLD, &sa, nullptr);

  // Detach the child into its own session so the caller and child share no
  // controlling terminal or process group — a fully independent sibling editor.
  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);

  pid_t pid = 0;
  const int rc = posix_spawn(&pid, executable.c_str(), nullptr, &attr, argv.data(), environ);
  posix_spawnattr_destroy(&attr);
  if (rc != 0) {
    return std::error_code(rc, std::generic_category()); // launch failed — a value, never a throw
  }
  return {};
}

Result<std::filesystem::path> current_executable_path() {
  std::error_code ec;
  // /proc/self/exe resolves to THIS binary's real absolute path (D-exec_new-5),
  // robust against a bare or symlinked argv[0].
  std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
  if (ec) {
    return ec;
  }
  return exe;
}

} // namespace ace::platform
