#include <ace/commands/exec_new.hpp>

namespace ace::commands {

std::error_code open_another_project(const platform::ProcessLauncher& launcher,
                                     const std::filesystem::path& executable,
                                     const std::filesystem::path& project_dir) {
  if (project_dir.empty()) {
    // Rejecting empty keeps the "spawn a real project" contract honest and gives
    // the future picker a clear error instead of a mystery scratch process
    // (D-exec_new-4). The launcher is never touched.
    return std::make_error_code(std::errc::invalid_argument);
  }
  // Canonicalize to absolute before spawning: the child inherits the parent's
  // CWD, so a relative path would make "which project" CWD-dependent
  // (D-exec_new-4). `absolute` unconditionally roots the path at the CWD (lexical,
  // no existence needed — weakly_canonical alone leaves a fully-nonexistent
  // relative path relative); weakly_canonical then normalizes it. Neither throws.
  std::error_code ec;
  std::filesystem::path resolved = std::filesystem::absolute(project_dir, ec);
  if (!ec) {
    resolved = std::filesystem::weakly_canonical(resolved, ec);
  }
  if (ec) {
    return ec;
  }
  return launcher.spawn_detached(executable, {resolved.string()});
}

} // namespace ace::commands
