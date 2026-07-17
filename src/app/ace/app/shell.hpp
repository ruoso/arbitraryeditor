#pragma once

#include <ace/app/app_loop.hpp>

#include <functional>

// Forward declarations keep SDL/ImGui headers out of consumers of the shell API;
// the .cpp owns the real includes (the SDL/ImGui seam lives in `app`, A8).
struct SDL_Window;
struct ImGuiContext;

namespace ace::app {

struct ShellOptions {
  // Headless: SDL offscreen video driver + software GL — the CI path with no
  // display (D-app_shell-3). Only the SDL driver hint changes vs. windowed.
  bool headless = false;
  // Frame cap: <= 0 runs until quit (the windowed app); > 0 stops after N frames
  // (the offscreen smoke). The Test Engine e2e leaves this 0 and drives frames
  // until its test queue drains.
  int max_frames = 0;
  int width = 1280;
  int height = 720;
};

// The application shell: SDL3 window + GLES3 context + Dear ImGui (docking),
// factored out of main() so the ImGui Test Engine e2e and the offscreen smoke
// can link and drive it headless (D-app_shell-2). Lifecycle:
//   init() -> ( new_frame(); draw_ui(); render() )* -> shutdown()
// Today draw_ui() shows only ImGui chrome (a placeholder pane); the dockspace is
// editor.dock.dockspace and a real Document is editor.foundation.render_probe.
class Shell {
public:
  Shell() = default;
  ~Shell();
  Shell(const Shell&) = delete;
  Shell& operator=(const Shell&) = delete;

  // Creates the window, GLES3 context and ImGui backends. Returns false (logging
  // to stderr) on failure; the destructor / shutdown() releases partial state.
  bool init(const ShellOptions& opts);

  void new_frame(); // poll SDL events, then ImGui NewFrame
  void draw_ui();   // build the shell chrome (the placeholder pane)
  // ImGui::Render + GL clear + present. `before_present`, if set, runs after the
  // draw data is submitted and before the buffer swap — the seam the e2e uses to
  // read back the frame (screenshot baseline) while the back buffer is valid.
  void render(const std::function<void()>& before_present = {});
  void shutdown();

  bool quit_requested() const { return quit_requested_; }
  int frames_rendered() const { return frames_; }
  ImGuiContext* imgui_context() const { return imgui_ctx_; }

private:
  ShellOptions opts_{};
  SDL_Window* window_ = nullptr;
  void* gl_ctx_ = nullptr; // SDL_GLContext (an opaque pointer)
  ImGuiContext* imgui_ctx_ = nullptr;
  bool sdl_inited_ = false;
  bool quit_requested_ = false;
  int frames_ = 0;
};

// Convenience full run used by main() and the offscreen smoke: init, loop
// new_frame/draw_ui/render while should_continue_loop() holds, then shutdown.
// Returns 0 on a clean run, non-zero if init failed.
int run_editor(const ShellOptions& opts);

} // namespace ace::app
