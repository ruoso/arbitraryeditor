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
set(ARBC_SOURCE_DIR "/home/ruoso/devel/arbitrarycomposer"
    CACHE STRING "Local libarbc checkout for co-development")
set(_editor_build_testing "${BUILD_TESTING}")
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(arbc SOURCE_DIR "${ARBC_SOURCE_DIR}")
FetchContent_MakeAvailable(arbc)
set(BUILD_TESTING "${_editor_build_testing}" CACHE BOOL "" FORCE)

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
ace_component(platform DEPENDS base)
ace_component(gl DEPENDS base)
# L1 — the UI-agnostic core (project owns the libarbc Document)
ace_component(project DEPENDS base platform LIBS arbc::arbc)
ace_component(scene DEPENDS base project)
ace_component(interact DEPENDS base scene)
ace_component(commands DEPENDS base project scene)
ace_component(dockmodel DEPENDS base platform)
# L2
ace_component(render DEPENDS base project scene gl)
# L3 — the ImGui layer
ace_component(views DEPENDS scene interact commands render dockmodel)
ace_component(dock DEPENDS dockmodel views)

# L4 — the app
add_executable(arbitraryeditor src/app/main.cpp)
target_link_libraries(arbitraryeditor PRIVATE ace::dock)

# ---- tests (Catch2 comes from libarbc) -------------------------------------
enable_testing()
add_executable(ace_tests tests/interact_test.cpp tests/binding_test.cpp)
target_link_libraries(ace_tests PRIVATE ace::interact ace::project arbc::arbc
                                        Catch2::Catch2WithMain)
add_test(NAME ace_tests COMMAND ace_tests)
