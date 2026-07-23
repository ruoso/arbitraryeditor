#include <ace/app/camera_inspector.hpp>
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
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

  // Keep windowed runs visible from creation time. Some Wayland/libdecor
  // combinations crash during explicit SDL_ShowWindow() on a hidden window.
  const SDL_WindowFlags flags =
      SDL_WINDOW_OPENGL | (opts_.headless ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE);
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
  // The Canvas subsystem is N LIVE interactive renders of the one owned Document over
  // ONE shared WorkerPool + ONE render thread (editor.canvas.multi_canvas; A5 "N
  // renderers share one WorkerPool", D18 "multiple canvases through different cameras
  // side by side"): the app owns the host + one render thread + a per-canvas#N GL
  // texture, drives every HostViewport off the UI thread (D-multi_canvas-2), and uploads
  // each settled frame — textures created once, updated in place, and released before
  // shutdown while the context is still valid (Constraint 5). Owned here in the app
  // layer, not in the shell. The body draws by its per-pane view_id, so every canvas#N
  // the dock mints renders independently. Every other view type draws a placeholder until
  // its downstream panel leaf lands (D-view-registry-5).
  CanvasView canvas(app_state);
  ace::views::register_view_body(
      ace::dockmodel::ViewType::Canvas, [&canvas](std::string_view view_id) {
        // The dockspace owns the canvas#N window; render at the
        // pane's pixel size (Constraint 7). A degenerate pane
        // draws nothing.
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        canvas.draw_content(view_id, static_cast<int>(avail.x), static_cast<int>(avail.y));
      });
  // The History view IS the undo journal made visible and click-navigable (D18
  // "History is a view"; editor.panels.history). It reads the one owned session's
  // journal and loops the shipped undo/redo verbs, so the body captures the same
  // AppState& the shell owns (D-history-3). Cleared on exit like the Canvas body —
  // the register_view_body seam is process-global.
  ace::views::register_view_body(
      ace::dockmodel::ViewType::History,
      [&app_state](std::string_view view_id) { ace::views::draw_history(app_state, view_id); });
  // The Inspector view body hosts the first-cut camera resolution editor (editor.cameras.manip,
  // D-manip-6): W×H + aspect presets driving set_camera_resolution through the same apply_edit
  // seam the History/undo edits ride. It captures the one AppState& and the CanvasView (for the
  // edit runner) and is cleared on exit like the other bodies (the seam is process-global).
  ace::app::CameraInspector camera_inspector(app_state, canvas);
  ace::views::register_view_body(
      ace::dockmodel::ViewType::Inspector,
      [&camera_inspector](std::string_view view_id) { camera_inspector.draw(view_id); });
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
    // Edit runner (editor.canvas.single_writer): the gateway hands each undo/redo Document
    // mutation to this runner, which runs it via CanvasHost::apply_edit on the UI/writer
    // thread, then wakes the off-thread canvas to re-render the damage. The render read takes
    // no lock — arbc v0.2.0 publishes content bindings copy-on-write (#10/#11), so the render
    // walk reads a stable snapshot while the edit rebinds. This replaces frame_sync's
    // fire-after poke, which mutated the Document before the wake (the v0.1.0 TSan race).
    // The gateway outlives the draw loop alongside `canvas`, so the capture is safe.
    app_gateway->set_edit_runner(
        [&canvas](const std::function<void()>& edit) { canvas.apply_edit(edit); });
    // The ONE framing source both framing-derived verbs read (D-mint_from_focused_canvas-4):
    // `insert_cell`'s provisional placement (editor.cells.model, Constraint 7) and
    // `new_shot_from_view`'s mint (D23) both go through this provider, so binding it to the
    // FOCUSED canvas moves both at once — an inserted cell lands where the canvas the user is
    // working in is looking, and a mint promotes that same pane rather than canvas#1. Read by
    // value at verb time. Same lifetime argument as the edit runner above.
    app_gateway->set_view_framing([&canvas] { return canvas.focused_framing(); });
    project_gateway = app_gateway.get();
  }
  dockspace.set_project_gateway(project_gateway);
  // After the dock draws every open view body (each canvas#N pane lazily registering its
  // host entry), reconcile the canvas subsystem against the authoritative layout: a
  // canvas#N that was closed leaves DockLayout::view_ids(), so its host entry + GL texture
  // are freed (D-multi_canvas-5 / Constraint 7). Runs on the UI thread with a live context.
  shell.set_draw_content([&dockspace, &canvas]() {
    dockspace.draw();
    canvas.reconcile(dockspace.layout().view_ids());
  });
  while (should_continue_loop(shell.frames_rendered(), opts.max_frames, shell.quit_requested())) {
    shell.new_frame();
    shell.draw_ui();
    shell.render();
  }
  // On-close GC (§8 "runs `gc_project_directory` … on close", D-gc-3 / Constraint 6):
  // a SILENT, best-effort sweep of the on-disk `assets/` orphans before teardown. It
  // runs while the Document's `HousekeepingThread` may still be checkpointing
  // `workspace/` — a DISJOINT directory, and the sweep roots on the on-disk canonical
  // and reads no live Document state, so there is no shared mutable state to race. Its
  // result is ignored: a mandated-automatic maintenance op must never nag or block
  // shutdown (no confirm modal here, unlike the rail). No-ops when no `project.arbc`
  // has been published yet (the no-canonical guard).
  (void)ace::commands::gc_project(app_state, /*dry_run=*/false);

  // Clear the body before the CanvasView it captures is destroyed — the seam is
  // process-global, so a later shell run must not call into a dangling capture.
  ace::views::register_view_body(ace::dockmodel::ViewType::Canvas, {});
  ace::views::register_view_body(ace::dockmodel::ViewType::History, {});
  ace::views::register_view_body(ace::dockmodel::ViewType::Inspector, {});
  canvas.destroy();
  shell.shutdown();
  return 0;
}

} // namespace ace::app
