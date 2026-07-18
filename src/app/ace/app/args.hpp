#pragma once

#include <ace/app/shell.hpp>

namespace ace::app {

// The parsed result of the editor's command line (refinement Decision
// D-exec_new-3): the ShellOptions to run, plus whether the invocation was
// well-formed. Kept a plain value so the argv -> ShellOptions mapping is a pure,
// unit-testable function despite living in L4 `app` (the app_loop precedent).
struct ParsedArgs {
  ShellOptions options;
  bool ok = true; // false => malformed (more than one positional) -> usage + non-zero
};

// Map `arbitraryeditor [<project-dir>]` to ShellOptions (D-exec_new-3). No
// positional arg leaves `project_dir` empty (the bootstrap then creates a fresh
// scratch project, so the "one process owns exactly one non-empty Document"
// invariant always holds and the app stays drivable headless). Exactly one
// positional sets `project_dir`. More than one positional is a usage error
// (`ok == false`). Pure: no SDL/GL, no side effects.
ParsedArgs parse_args(int argc, const char* const* argv);

// The argv entry point behind main(): parse, print a one-line usage to stderr and
// return non-zero on a malformed invocation, else run the editor (D-exec_new-3;
// the parse-failure path is handled here, before run_editor, so run_editor's
// 0/1/2 contract is unchanged). Factored out of main() so the malformed-invocation
// branch is unit-testable headless.
int run_editor_argv(int argc, const char* const* argv);

} // namespace ace::app
