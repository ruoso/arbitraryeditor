#include <ace/app/args.hpp>

// Editor entry point. A thin driver over ace::app::run_editor_argv — the shell
// logic lives in the ace::app library so the ImGui Test Engine e2e and the
// offscreen smoke can drive it headless (D-app_shell-2). One window for the
// process lifetime (D19). A single optional positional argument selects the
// project directory (editor.project.exec_new / D-exec_new-3):
// `arbitraryeditor [<project-dir>]`; no arg opens a fresh scratch project. See
// docs/01-architecture.md.
//
// Excluded from coverage: a test binary links its own Catch2/ImGui `main`, so
// this thin forwarding entry point is unreachable under instrumentation. The
// shell logic it delegates to (ace::app::run_editor_argv) is covered directly.
int main(int argc, char** argv) {               // GCOVR_EXCL_LINE
  return ace::app::run_editor_argv(argc, argv); // GCOVR_EXCL_LINE
}
