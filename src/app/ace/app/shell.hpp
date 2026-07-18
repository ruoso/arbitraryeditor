#pragma once

#include <ace/app/app_loop.hpp>

#include <filesystem>
#include <functional>
#include <utility>

// Forward declarations keep SDL/ImGui headers out of consumers of the shell API;
// the .cpp owns the real includes (the SDL/ImGui seam lives in `app`, A8).
struct SDL_Window;
struct ImGuiContext;

// The session aggregate (editor.project.app_state) lives in the L1 `commands`
// core; forward-declared here so the bootstrap accessor signature does not drag
// the arbc-owning session header into every shell consumer.
namespace ace::commands {
class AppState;
}

// The project-entry seam (editor.project.open_ui / A12) is declared in L3 `dock`;
// forward-declared here so a test can inject a fake gateway through ShellOptions
// without dragging the dock header into every shell consumer.
namespace ace::dock {
class ProjectGateway;
}

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
  // The project directory the process resolves into its one owned session
  // (editor.project.app_state / D-app_state-6). Empty means "no path given": the
  // bootstrap creates a fresh scratch project so the "one process owns exactly
  // one Document, never empty" invariant always holds and the app is drivable
  // headless. A future `editor.project.exec_new` passes a concrete path here.
  std::filesystem::path project_dir;
  // Test seam (editor.project.open_ui): a fake project-entry gateway the rail's
  // New / Open / Recent affordances drive, injected in place of the SDL-backed
  // AppProjectGateway (A12). Null in a real run — run_editor then constructs the
  // native gateway. Not owned; must outlive the run.
  ace::dock::ProjectGateway* project_gateway = nullptr;
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
  void draw_ui();   // build the shell chrome (invokes the installed content, if any)

  // Install the per-frame content the shell draws inside draw_ui(). The app
  // layer installs the render_probe pane here (editor.foundation.render_probe),
  // keeping the arbc/GL/view orchestration out of the generic shell (A8). Empty
  // by default — the piecewise smoke drives a bare shell with no content.
  void set_draw_content(std::function<void()> content) { draw_content_ = std::move(content); }
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
  std::function<void()> draw_content_;
  SDL_Window* window_ = nullptr;
  void* gl_ctx_ = nullptr; // SDL_GLContext (an opaque pointer)
  ImGuiContext* imgui_ctx_ = nullptr;
  bool sdl_inited_ = false;
  bool quit_requested_ = false;
  int frames_ = 0;
};

// Convenience full run used by main() and the offscreen smoke: init, resolve the
// project directory into the process's one owned `AppState` (held for the whole
// run beside the `dockmodel::ToolSelection`), loop new_frame/draw_ui/render while
// should_continue_loop() holds, then shutdown (the session torn down cleanly,
// its `HousekeepingThread` joined). Returns 0 on a clean run, 1 if init failed,
// 2 if the project could not be opened/created (logged, never thrown —
// D-app_state-6). `on_ready`, if set, is invoked exactly once with the live
// session just after it comes up — the test-visible seam the boot-lifecycle e2e
// asserts the single-`Document` ownership through.
int run_editor(const ShellOptions& opts,
               const std::function<void(commands::AppState&)>& on_ready = {});

} // namespace ace::app
