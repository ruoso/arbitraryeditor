#pragma once

#include <ace/platform/process_launcher.hpp>

#include <filesystem>
#include <system_error>

namespace ace::commands {

// The L1 session action behind the "open a *different* project" gesture
// (D19 / A7 process-per-project; refinement Decisions D-exec_new-2/4). Because a
// process owns exactly one Document for its whole life, opening another project
// launches a NEW, fully independent sibling editor rather than swapping the live
// session: this canonicalizes `project_dir` to an absolute path (so the child
// does not depend on the caller's working directory, D-exec_new-4) and hands
// (`executable`, {absolute project dir}) to `launcher`, which spawns a detached
// child and leaves THIS editor running. `executable` is the binary the relaunch
// targets — resolve it once via platform::current_executable_path()
// (D-exec_new-5); this action stays free of any "how do I find myself" detail.
//
// Returns an empty error_code on a successful spawn. An empty `project_dir` is
// rejected with an error value and the launcher is NOT invoked. Errors are
// values, never thrown (D-open-6). No UI here: the New/Open picker that calls
// this is editor.project.open_ui (`depends !exec_new`).
std::error_code open_another_project(const platform::ProcessLauncher& launcher,
                                     const std::filesystem::path& executable,
                                     const std::filesystem::path& project_dir);

} // namespace ace::commands
