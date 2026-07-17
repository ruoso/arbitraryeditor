cmake_minimum_required(VERSION 3.24)
project(arbitraryeditor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# One static library per levelized component (docs/01-architecture.md §8). The
# level DAG is the source of truth in scripts/check_levels.py; the CMake link
# edges below follow it. libarbc, Dear ImGui, and SDL3 arrive via FetchContent
# in later foundation leaves (render_probe / app_shell); this skeleton links
# only the internal graph so the gate is green from the start.
function(ace_component name)
  cmake_parse_arguments(C "" "" "DEPENDS" ${ARGN})
  file(GLOB _srcs CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/${name}/*.cpp")
  add_library(ace_${name} STATIC ${_srcs})
  add_library(ace::${name} ALIAS ace_${name})
  target_include_directories(ace_${name} PUBLIC "${CMAKE_SOURCE_DIR}/src/${name}")
  target_compile_features(ace_${name} PUBLIC cxx_std_20)
  if(C_DEPENDS)
    list(TRANSFORM C_DEPENDS PREPEND ace_)
    target_link_libraries(ace_${name} PUBLIC ${C_DEPENDS})
  endif()
endfunction()

# L0
ace_component(base)
ace_component(platform DEPENDS base)
ace_component(gl DEPENDS base)
# L1 — the UI-agnostic core
ace_component(project DEPENDS base platform)
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

include(CTest)
if(BUILD_TESTING)
  include(FetchContent)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(Catch2)

  add_executable(ace_tests tests/interact_test.cpp)
  target_link_libraries(ace_tests PRIVATE ace::interact Catch2::Catch2WithMain)
  add_test(NAME ace_tests COMMAND ace_tests)
endif()
