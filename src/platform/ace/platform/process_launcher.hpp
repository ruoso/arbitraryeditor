#pragma once

#include <ace/platform/result.hpp>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace ace::platform {

// Process-spawn faculty (refinement Decision D-exec_new-2): launch a NEW, fully
// independent, session-detached sibling process and leave the caller running.
// This is the one place the WASM port maps D19's "a project is a tab / instance"
// (opening another project opens a new tab, not a new OS process). It is NOT a
// replace-self `execv` (D-exec_new-1) — the caller keeps its live window and
// workspace. Fire-and-forget: no handle is returned and the child is reaped
// without a lingering zombie. Errors are values, never thrown (D-open-6).
class ProcessLauncher {
public:
  virtual ~ProcessLauncher() = default;

  // Launch `executable` with `args` as argv[1..] (argv[0] is the executable
  // itself) as a detached child in its own session, leaving THIS process alive.
  // Returns an empty error_code on a successful launch, else the typed error.
  virtual std::error_code spawn_detached(const std::filesystem::path& executable,
                                         const std::vector<std::string>& args) const = 0;
};

// Native impl over posix_spawn (D-exec_new-1): the child is placed in its own
// session (POSIX_SPAWN_SETSID) so tearing down the caller's session never reaches
// it, and it is reaped without a zombie (SA_NOCLDWAIT). posix_spawn — not a hand
// fork+exec — is the sanitizer-safe launch path in a multithreaded, ASan/TSan
// process.
class NativeProcessLauncher final : public ProcessLauncher {
public:
  std::error_code spawn_detached(const std::filesystem::path& executable,
                                 const std::vector<std::string>& args) const override;
};

// The absolute path of the currently-running executable (refinement Decision
// D-exec_new-5): native reads /proc/self/exe, never the possibly-bare argv[0], so
// a relaunch targets THIS exact binary. A Result error on a path it cannot
// resolve. The caller passes this into commands::open_another_project, keeping
// the L1 action free of any "how do I find myself" platform detail.
Result<std::filesystem::path> current_executable_path();

} // namespace ace::platform
