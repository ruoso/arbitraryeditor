#include <ace/app/probe.hpp>
#include <ace/app/shell.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/gl/gl.hpp>
#include <ace/views/views.hpp>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <cstdio>
#include <string_view>

namespace ace::app {
namespace {

// GLES3 GLSL for ImGui's OpenGL3 backend (matches IMGUI_IMPL_OPENGL_ES3).
constexpr const char* k_glsl_version = "#version 300 es";
constexpr const char* k_window_title = "Arbitrary Composer";

bool fail(const char* what) {
  std::fprintf(stderr, "shell: %s failed: %s\n", what, SDL_GetError());
  return false;
}

} // namespace

Shell::~Shell() { shutdown(); }

bool Shell::init(const ShellOptions& opts) {
  opts_ = opts;
  if (opts_.headless) {
    // Offscreen driver + software GL: the single headless code path — only this
    // hint differs from the windowed shell (D-app_shell-3).
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    return fail("SDL_Init");
  }
  sdl_inited_ = true;

  // Request an ES3 context so the same GL path runs native and (later) on WebGL2
  // (A2 :27, A3 :53).
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  // Created hidden; the windowed shell shows it after GL/ImGui are up (no flash).
  const SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN;
  window_ = SDL_CreateWindow(k_window_title, opts_.width, opts_.height, flags);
  if (!window_) {
    return fail("SDL_CreateWindow");
  }
  gl_ctx_ = SDL_GL_CreateContext(window_);
  if (!gl_ctx_) {
    return fail("SDL_GL_CreateContext");
  }
  SDL_GL_MakeCurrent(window_, static_cast<SDL_GLContext>(gl_ctx_));
  SDL_GL_SetSwapInterval(opts_.headless ? 0 : 1);
  if (!opts_.headless) {
    SDL_ShowWindow(window_);
  }

  IMGUI_CHECKVERSION();
  imgui_ctx_ = ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // docking branch (A2/D18)
  io.IniFilename = nullptr;                         // reproducible headless runs
  ImGui::StyleColorsDark();
  if (!ImGui_ImplSDL3_InitForOpenGL(window_, static_cast<SDL_GLContext>(gl_ctx_))) {
    return fail("ImGui_ImplSDL3_InitForOpenGL");
  }
  if (!ImGui_ImplOpenGL3_Init(k_glsl_version)) {
    return fail("ImGui_ImplOpenGL3_Init");
  }

  quit_requested_ = false;
  frames_ = 0;
  return true;
}

void Shell::new_frame() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL3_ProcessEvent(&event);
    if (event.type == SDL_EVENT_QUIT) {
      quit_requested_ = true;
    } else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
               event.window.windowID == SDL_GetWindowID(window_)) {
      quit_requested_ = true;
    }
  }
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
}

void Shell::draw_ui() {
  // The generic shell owns no panes: the render_probe pane (which replaces the
  // former placeholder) is installed by the app layer via set_draw_content, so
  // arbc/GL/view orchestration stays out of the shell (A8 / D-render_probe-1).
  if (draw_content_) {
    draw_content_();
  }
}

void Shell::render(const std::function<void()>& before_present) {
  ImGui::Render();
  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(window_, &w, &h);
  ace::gl::set_viewport(w, h);
  ace::gl::clear(0.10f, 0.10f, 0.12f, 1.0f);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  if (before_present) {
    before_present();
  }
  SDL_GL_SwapWindow(window_);
  ++frames_;
}

void Shell::shutdown() {
  if (imgui_ctx_) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(imgui_ctx_);
    imgui_ctx_ = nullptr;
  }
  if (gl_ctx_) {
    SDL_GL_DestroyContext(static_cast<SDL_GLContext>(gl_ctx_));
    gl_ctx_ = nullptr;
  }
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  if (sdl_inited_) {
    SDL_Quit();
    sdl_inited_ = false;
  }
}

int run_editor(const ShellOptions& opts) {
  Shell shell;
  if (!shell.init(opts)) {
    return 1;
  }
  // The probe texture is created once (GL context current after init), drawn
  // every frame, and destroyed before shutdown while the context is still valid
  // (refinement Constraint 8). Owned here in the app layer, not in the shell.
  ProbeView probe;
  probe.upload();
  // The Canvas view IS the render_probe pane (D18 "the canvas is a view"): the
  // app owns the GL texture, so it registers the Canvas body the dockspace draws
  // inside the canvas window it owns. Every other view type draws a placeholder
  // until its downstream panel leaf lands (D-view-registry-5).
  ace::views::register_view_body(ace::dockmodel::ViewType::Canvas,
                                 [&probe](std::string_view) { probe.draw_content(); });
  // The dockspace host (editor.dock.view_registry) owns the shell's whole draw:
  // it renders each open view by its instance id and syncs the tab ✕ back into
  // the authoritative DockLayout.
  ace::dock::Dockspace dockspace;
  shell.set_draw_content([&dockspace]() { dockspace.draw(); });
  while (should_continue_loop(shell.frames_rendered(), opts.max_frames, shell.quit_requested())) {
    shell.new_frame();
    shell.draw_ui();
    shell.render();
  }
  // Clear the body before the ProbeView it captures is destroyed — the seam is
  // process-global, so a later shell run must not call into a dangling capture.
  ace::views::register_view_body(ace::dockmodel::ViewType::Canvas, {});
  probe.destroy();
  shell.shutdown();
  return 0;
}

} // namespace ace::app
