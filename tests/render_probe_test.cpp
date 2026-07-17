#include <ace/project/project.hpp>
#include <ace/render/render.hpp>

#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "golden_support.hpp"

// editor.foundation.render_probe: the L1 doc-builder unit and the first
// render_offline byte-exact golden (docs §9). GL-free — both run on the plain
// headless ace_tests lane, no GL context (D-render_probe-5).

TEST_CASE("render_probe: the trivial Document builds with a solid cell") {
  const ace::project::ProbeDocument probe = ace::project::build_probe_document();

  REQUIRE(probe.document != nullptr);
  CHECK(probe.composition.valid());
  CHECK(probe.layer.valid());
  CHECK(probe.content.valid());

  // resolve() binds the solid Content we added (A1: the id names a real object).
  arbc::Content* content = probe.document->resolve(probe.content);
  REQUIRE(content != nullptr);
  CHECK(dynamic_cast<arbc::SolidContent*>(content) != nullptr);
}

TEST_CASE("render_probe: offline render matches the sRGB8 golden") {
  const ace::render::Srgb8Image image =
      ace::render::render_probe_srgb8(ace::project::k_probe_width, ace::project::k_probe_height);

  REQUIRE(image.width == ace::project::k_probe_width);
  REQUIRE(image.height == ace::project::k_probe_height);
  REQUIRE(image.pixels.size() ==
          static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4);

  const std::string golden =
      "render_probe_" + std::to_string(image.width) + "x" + std::to_string(image.height) + ".rgba8";
  CHECK(ace_test::compare_golden(golden, image.pixels));
}

TEST_CASE("render_probe: the golden compare rejects a mismatch and dumps the actual") {
  const std::vector<std::uint8_t> wrong(16, 0);
  // A missing golden and a byte-differing golden both fail and dump for triage.
  CHECK_FALSE(ace_test::compare_golden("render_probe_missing.rgba8", wrong));
  const std::string real = "render_probe_" + std::to_string(ace::project::k_probe_width) + "x" +
                           std::to_string(ace::project::k_probe_height) + ".rgba8";
  CHECK_FALSE(ace_test::compare_golden(real, wrong));
}
