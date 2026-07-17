#include <ace/dock/dock.hpp>

#include <cstdio>

// Editor entry point. The SDL3 + GL + ImGui window is a later foundation leaf
// (app_shell); this bootstrap proves the component graph links and the gate is
// green. See docs/01-architecture.md.
int main() {
  std::printf("arbitraryeditor: %s stack linked; app_shell pending\n", ace::dock::name());
  return 0;
}
