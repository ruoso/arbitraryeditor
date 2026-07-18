#include <ace/app/folder_dialog.hpp>
#include <ace/app/probe.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/dockmodel/workspaces.hpp>
#include <ace/gl/gl.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/views/views.hpp>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
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

// The per-user prefs root the WorkspaceStore persists named presets under (D21 —
// cross-project, so prefs not the per-project workspace/). The store takes a root
// path (A3: WASM swaps XDG for the File System Access API), so directory
// resolution is L4 app-wiring detail, not a design decision: prefer
// $XDG_CONFIG_HOME, else ~/.config, else the OS temp dir. No directory is created
// here — the store seeds parents lazily on the first save.
std::filesystem::path workspace_prefs_root() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  const char* home = std::getenv("HOME");
  std::filesystem::path base = (xdg != nullptr && *xdg != '\0') ? std::filesystem::path(xdg)
                               : (home != nullptr && *home != '\0')
                                   ? std::filesystem::path(home) / ".config"
                                   : std::filesystem::temp_directory_path();
  return base / "arbitraryeditor" / "workspaces";
}

// A unique scratch project directory for a no-path launch (D-app_state-6): the
// bootstrap create_projects one here so the single-`Document`-for-lifetime
// invariant always holds even before the in-app New/Open picker
// (editor.project.open_ui) exists. Under the OS temp dir with a random suffix so
// repeated headless runs in one process never collide on a live workspace file.
std::filesystem::path scratch_project_dir() {
  std::random_device rd;
  return std::filesystem::temp_directory_path() /
         ("arbitraryeditor-session-" + std::to_string(rd()));
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

int run_editor(const ShellOptions& opts, const std::function<void(commands::AppState&)>& on_ready) {
  Shell shell;
  if (!shell.init(opts)) {
    return 1;
  }
  // The process's ONE owned project session (A7/D19): resolve the project
  // directory (a fresh scratch project when none was given) into the `AppState`
  // and hold it for the whole run, beside the `dockmodel::ToolSelection` the
  // dockspace owns. Errors are values — a failed open/create logs and exits
  // non-zero, never a throw across the app boundary (D-app_state-6 / Constraint 6).
  ace::platform::NativeFileSystem filesystem;
  const std::filesystem::path project_dir =
      opts.project_dir.empty() ? scratch_project_dir() : opts.project_dir;
  auto session = ace::commands::open_or_create_app_state(filesystem, project_dir);
  if (!session) {
    std::fprintf(stderr, "shell: could not open project '%s': %s\n", project_dir.string().c_str(),
                 session.error().message().c_str());
    shell.shutdown();
    return 2;
  }
  ace::commands::AppState& app_state = *session;
  if (on_ready) {
    on_ready(app_state);
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
  // The saved-workspace preset store (editor.dock.workspaces): persisted through
  // the native FileSystem seam (the same one the session bootstrap used) under
  // the per-user prefs root. Wired into the dockspace so the rail's switcher can
  // list/apply/save/delete presets. Both outlive the draw loop below; no preset
  // file is touched until a user saves.
  ace::dockmodel::WorkspaceStore workspace_store(workspace_prefs_root(), filesystem);
  dockspace.set_workspace_store(&workspace_store);
  // The project-entry gateway (A12/D22): the rail's New / Open / Recent seam. A
  // test may inject a fake through ShellOptions; otherwise wire the L4 SDL-backed
  // AppProjectGateway (the sole native-folder-dialog holder) over the same
  // FileSystem, a ProcessLauncher, and the recent-projects prefs store (a sibling
  // of the workspaces prefs dir). Every action spawns a sibling editor — the one
  // owned Document is never swapped (D19/A7). All outlive the draw loop below.
  ace::platform::NativeProcessLauncher launcher;
  std::unique_ptr<ace::dockmodel::RecentProjects> recent_projects;
  std::unique_ptr<ace::app::SdlFolderDialog> folder_dialog;
  std::unique_ptr<ace::app::AppProjectGateway> app_gateway;
  ace::dock::ProjectGateway* project_gateway = opts.project_gateway;
  if (project_gateway == nullptr) {
    std::filesystem::path executable;
    if (const platform::Result<std::filesystem::path> exe =
            ace::platform::current_executable_path()) {
      executable = *exe;
    }
    recent_projects = std::make_unique<ace::dockmodel::RecentProjects>(
        workspace_prefs_root().parent_path(), filesystem);
    folder_dialog = std::make_unique<ace::app::SdlFolderDialog>();
    app_gateway = std::make_unique<ace::app::AppProjectGateway>(
        *recent_projects, filesystem, *folder_dialog, launcher, executable, app_state);
    project_gateway = app_gateway.get();
  }
  dockspace.set_project_gateway(project_gateway);
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
