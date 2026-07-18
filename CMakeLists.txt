cmake_minimum_required(VERSION 3.24)
project(arbitraryeditor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# ---- libarbc (external dependency, docs/01-architecture.md A1) --------------
# Consumed as a CMake subproject. Default to a local checkout for co-development
# (arch A2: "a local checkout during co-development, a released tag once the
# editor-facing API stabilizes"); override ARBC_SOURCE_DIR / ARBC_GIT_* to point
# elsewhere. libarbc fetches Catch2 itself (a shipped artifact), so we reuse ITS
# Catch2 rather than declaring a second one; and we keep the library's own test
# suite out of our build (its tests/ is gated on BUILD_TESTING).
include(FetchContent)
# Default: fetch the released library from GitHub (works on CI + the act runner).
# For co-development against a local checkout, set -DARBC_SOURCE_DIR=/path/to/lib
# (overrides the git fetch) or point ARBC_GIT_TAG at a branch.
set(ARBC_GIT_REPOSITORY "https://github.com/ruoso/arbitrarycomposer.git"
    CACHE STRING "libarbc git remote")
set(ARBC_GIT_TAG "v0.1.0" CACHE STRING "libarbc git ref")
set(ARBC_SOURCE_DIR "" CACHE STRING "Local libarbc checkout (overrides the git fetch)")
set(_editor_build_testing "${BUILD_TESTING}")
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
if(ARBC_SOURCE_DIR)
  FetchContent_Declare(arbc SOURCE_DIR "${ARBC_SOURCE_DIR}")
else()
  FetchContent_Declare(arbc
    GIT_REPOSITORY "${ARBC_GIT_REPOSITORY}"
    GIT_TAG "${ARBC_GIT_TAG}"
    GIT_SHALLOW TRUE)
endif()
FetchContent_MakeAvailable(arbc)
set(BUILD_TESTING "${_editor_build_testing}" CACHE BOOL "" FORCE)

# ---- SDL3 + Dear ImGui (docking) + ImGui Test Engine (docs A2/A3, A9/§9) ----
# The self-rendered UI stack (editor.foundation.app_shell). SDL3 owns the
# window / GL context / input / main loop (A3); Dear ImGui (docking branch) is
# the self-rendered UI (A2); the ImGui Test Engine is the headless e2e rig every
# later view leaf inherits (A9/§9). Neither ImGui repo ships a CMakeLists, so we
# build them as plain static libraries below. Includes are seam-gated by
# scripts/check_levels.py: SDL only in app; ImGui in views/dock/app; GL in the
# GL-facing components — no edit to check_levels is required.
find_package(Threads REQUIRED)
# ImGui's OpenGL3 backend targets the GLES3 / WebGL2 common subset (A2 :27,
# A3 :53) and links GLES directly (no loader), so libGLESv2 must be present.
find_library(ACE_GLES_LIBRARY NAMES GLESv2)
if(NOT ACE_GLES_LIBRARY)
  message(FATAL_ERROR
    "libGLESv2 not found — install the GLES3 runtime "
    "(e.g. libgles-dev / libgles2-mesa-dev) for the GLES3/WebGL2 render subset.")
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
# The shell inits SDL_INIT_VIDEO only (src/app/shell.cpp) — audio and camera are
# unused. Disable both subsystems: it trims the build and, crucially, drops SDL's
# PipeWire backends (audio + camera both build SDL_pipewire.c), which fail to
# compile against newer system PipeWire-0.3 headers (pw_node_enum_params now
# takes pw_node* not pw_proxy*) once -Wincompatible-pointer-types is an error
# (GCC 14+). No display/audio surface is needed for a self-rendered editor.
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.2.0
  GIT_SHALLOW TRUE)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG docking)
FetchContent_Declare(imgui_test_engine
  GIT_REPOSITORY https://github.com/ocornut/imgui_test_engine.git
  GIT_TAG main)
FetchContent_MakeAvailable(SDL3)
# imgui / imgui_test_engine have no CMakeLists — MakeAvailable only populates.
FetchContent_MakeAvailable(imgui imgui_test_engine)

# Dear ImGui core + the SDL3 and OpenGL3 (ES3) backends. IMGUI_ENABLE_TEST_ENGINE
# arms the item hooks the Test Engine drives by; the hooks are no-ops until an
# engine context is created, so the shipped shell is unaffected (D-app_shell-5).
add_library(imgui STATIC
  "${imgui_SOURCE_DIR}/imgui.cpp"
  "${imgui_SOURCE_DIR}/imgui_draw.cpp"
  "${imgui_SOURCE_DIR}/imgui_tables.cpp"
  "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
  "${imgui_SOURCE_DIR}/imgui_demo.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp")
target_include_directories(imgui PUBLIC
  "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3 IMGUI_ENABLE_TEST_ENGINE)
target_link_libraries(imgui PUBLIC SDL3::SDL3 ${ACE_GLES_LIBRARY})

file(GLOB _ite_srcs CONFIGURE_DEPENDS
  "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/*.cpp")
add_library(imgui_test_engine STATIC ${_ite_srcs})
target_include_directories(imgui_test_engine PUBLIC
  "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine")
# The engine drives tests on a coroutine; imgui_te_imconfig.h defaults the
# std::thread coroutine impl OFF, so without this ImGuiTestEngine_Start() asserts
# ("Missing CoroutineFuncs!"). Enable the shipped std::thread implementation.
target_compile_definitions(imgui_test_engine PUBLIC
  IMGUI_ENABLE_TEST_ENGINE IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1)
target_link_libraries(imgui_test_engine PUBLIC imgui Threads::Threads)
# imgui.cpp emits Test Engine hook calls; the engine calls back into imgui. Both
# are STATIC, so CMake repeats the archives on the link line to resolve the cycle.
target_link_libraries(imgui PUBLIC imgui_test_engine)

# ---- levelized components (docs/01-architecture.md §8) ----------------------
# One static library per component; the level DAG is enforced by
# scripts/check_levels.py, the CMake link edges below follow it. SDL3 + Dear
# ImGui arrive with the window/dockspace work below.
function(ace_component name)
  cmake_parse_arguments(C "" "" "DEPENDS;LIBS" ${ARGN})
  file(GLOB _srcs CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/${name}/*.cpp")
  add_library(ace_${name} STATIC ${_srcs})
  add_library(ace::${name} ALIAS ace_${name})
  target_include_directories(ace_${name} PUBLIC "${CMAKE_SOURCE_DIR}/src/${name}")
  target_compile_features(ace_${name} PUBLIC cxx_std_20)
  if(C_DEPENDS)
    list(TRANSFORM C_DEPENDS PREPEND ace_)
    target_link_libraries(ace_${name} PUBLIC ${C_DEPENDS})
  endif()
  if(C_LIBS)
    target_link_libraries(ace_${name} PUBLIC ${C_LIBS})
  endif()
endfunction()

# L0
ace_component(base)
ace_component(platform DEPENDS base LIBS Threads::Threads)
# gl issues the shell's raw GLES3/WebGL2-subset draw calls (viewport/clear), so
# it links libGLESv2; ImGui owns its own backend GL (docs A2/A3, refinement §2).
ace_component(gl DEPENDS base LIBS ${ACE_GLES_LIBRARY})
# L1 — the UI-agnostic core (project owns the libarbc Document)
ace_component(project DEPENDS base platform LIBS arbc::arbc)
# editor.project.save holds an `arbc::CodecTable` (builtin_codecs) to drive the
# asset-sink save path; its definition (arbc/serialize/codec.hpp) names nlohmann::json,
# which arbc keeps PRIVATE to arbc_serialize. Link nlohmann header-only + PRIVATE to
# `ace_project` alone (the exact idiom arbc's own CMake documents: header-only,
# invisible to check_levels.py, "no levelization edge, no new dependency") so only
# src/project/save.cpp sees it — no downstream component inherits JSON.
target_link_libraries(ace_project PRIVATE nlohmann_json::nlohmann_json)
ace_component(scene DEPENDS base project)
ace_component(interact DEPENDS base scene)
ace_component(commands DEPENDS base project scene)
ace_component(dockmodel DEPENDS base platform)
# L2
ace_component(render DEPENDS base project scene gl)
# L3 — the ImGui layer. views draws the render_probe pane, so it links Dear ImGui
# (the A8 seam already whitelists imgui in views, scripts/check_levels.py).
ace_component(views DEPENDS scene interact commands render dockmodel LIBS imgui)
ace_component(dock DEPENDS dockmodel views)

# L4 — the app. The shell logic lives in a static library (ace::app) so the
# ImGui Test Engine e2e and the offscreen smoke can link and drive it headless
# (D-app_shell-2); main.cpp stays a thin driver. app is L4 and already permitted
# SDL/GL/ImGui by the A8 seam, so this split changes no level.
file(GLOB _app_srcs CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/app/*.cpp")
list(REMOVE_ITEM _app_srcs "${CMAKE_SOURCE_DIR}/src/app/main.cpp")
add_library(ace_app STATIC ${_app_srcs})
add_library(ace::app ALIAS ace_app)
target_include_directories(ace_app PUBLIC "${CMAKE_SOURCE_DIR}/src/app")
target_compile_features(ace_app PUBLIC cxx_std_20)
# ace::platform: run_editor constructs a NativeFileSystem + WorkspaceStore for the
# saved-workspace presets (editor.dock.workspaces); app is L4 so platform is in its
# closure (scripts/check_levels.py). ace::commands/ace::project are named directly
# now that AppProjectGateway (editor.project.open_ui) calls open_another_project +
# the project validate/compose helpers; dockmodel via ace::dock stays transitive but
# is named too for the RecentProjects store.
target_link_libraries(ace_app PUBLIC ace::dock ace::gl ace::platform ace::commands
                                     ace::project ace::dockmodel imgui)

add_executable(arbitraryeditor src/app/main.cpp)
target_link_libraries(arbitraryeditor PRIVATE ace::app)

# ---- tests (Catch2 comes from libarbc) -------------------------------------
enable_testing()
add_executable(ace_tests tests/interact_test.cpp tests/binding_test.cpp
                         tests/platform_test.cpp tests/render_probe_test.cpp
                         tests/dockmodel_test.cpp tests/view_registry_test.cpp
                         tests/tool_rail_test.cpp tests/workspaces_test.cpp
                         tests/project_open_test.cpp tests/commands_test.cpp
                         tests/project_save_test.cpp tests/exec_new_test.cpp
                         tests/recent_projects_test.cpp tests/project_entry_test.cpp)
target_link_libraries(ace_tests PRIVATE ace::interact ace::project ace::render ace::platform
                                        ace::dockmodel ace::commands arbc::arbc
                                        Catch2::Catch2WithMain)
# The render_offline golden compares against raw sRGB8 bytes under tests/goldens
# (D-render_probe-6); the byte-compare helper resolves them by absolute path.
target_compile_definitions(ace_tests PRIVATE
  ACE_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/goldens")
add_test(NAME ace_tests COMMAND ace_tests)

# The app_shell test suite (docs §9): the pure frame-loop predicate (Catch2
# unit), the offscreen-GL "init + N frames + shutdown" smoke, and the ImGui Test
# Engine e2e that drives the shell headless by widget id. All three run against
# the SDL offscreen video driver + software GL (Mesa llvmpipe), so they need no
# display — the per-test ENVIRONMENT below pins that path so `ctest` is portable.
add_executable(ace_shell_test tests/app_loop_test.cpp tests/shell_smoke_test.cpp
                              tests/shell_e2e_test.cpp tests/dockspace_e2e_test.cpp
                              tests/view_registry_e2e_test.cpp tests/tool_rail_e2e_test.cpp
                              tests/workspaces_e2e_test.cpp tests/app_state_e2e_test.cpp
                              tests/exec_new_argv_test.cpp tests/exec_new_e2e_test.cpp
                              tests/app_project_gateway_test.cpp tests/open_ui_e2e_test.cpp
                              tests/save_ui_e2e_test.cpp)
target_link_libraries(ace_shell_test PRIVATE ace::app Catch2::Catch2WithMain)
add_test(NAME ace_shell_test COMMAND ace_shell_test)
# SDL3 reads the hint env var SDL_VIDEO_DRIVER; SDL_VIDEODRIVER is the legacy
# name — set both so the windowed-path smoke (which relies on the env, not the
# offscreen hint) stays displayless too. EGL_PLATFORM=surfaceless keeps the
# offscreen driver off any DRI/KMS device, and MESA_LOADER_DRIVER_OVERRIDE +
# GALLIUM_DRIVER pin Mesa's llvmpipe software rasterizer. NOTE: do NOT add
# LIBGL_ALWAYS_SOFTWARE here — with an EGL device present it conflicts with the
# explicit device selection ("Not allowed to force software rendering...") and
# crashes Mesa in driCreateNewScreen3 during SDL_GL context init.
set_tests_properties(ace_shell_test PROPERTIES
  ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen;SDL_VIDEODRIVER=offscreen;EGL_PLATFORM=surfaceless;MESA_LOADER_DRIVER_OVERRIDE=llvmpipe;GALLIUM_DRIVER=llvmpipe")
