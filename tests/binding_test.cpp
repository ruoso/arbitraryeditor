#include <arbc/backend_cpu/cpu_backend.hpp>
#include <arbc/media/surface_format.hpp>

#include <catch2/catch_test_macros.hpp>

// libarbc binding proof (docs/01-architecture.md A1/A6): the editor links the
// real library and can drive its backend.
TEST_CASE("libarbc links: CpuBackend allocates a working-format surface") {
  arbc::CpuBackend backend;
  const auto surface = backend.make_surface(4, 4, arbc::k_working_rgba32f);
  REQUIRE(surface.has_value());
  REQUIRE((*surface)->width() == 4);
}
