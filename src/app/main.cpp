#include <ace/app/shell.hpp>

// Editor entry point. A thin driver over ace::app::run_editor — the shell logic
// lives in the ace::app library so the ImGui Test Engine e2e and the offscreen
// smoke can drive it headless (D-app_shell-2). One window for the process
// lifetime (D19). See docs/01-architecture.md.
int main() { return ace::app::run_editor(ace::app::ShellOptions{}); }
