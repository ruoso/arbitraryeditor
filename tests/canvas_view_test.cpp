// editor.canvas.view — the interactive canvas driver, headless + GL-free
// (docs/01-architecture.md §9). Exercises render::CanvasRenderer without a GL
// context: it drives a frame over a known probe Document, asserts the still-scene
// early-out, the display-byte shape, and resize re-framing, and carries the first
// INTERACTIVE golden — proving the HostViewport + InteractiveRenderer compositor
// path composes byte-for-byte like the offline render_offline reference
// (render_probe). The inline executor (WorkerPoolConfig{}) makes the frame
// deterministic and byte-exact (D-canvas_view-2).
#include <ace/project/project.hpp>
#include <ace/render/canvas_renderer.hpp>
#include <ace/render/render.hpp>

#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>

#include "golden_support.hpp"

using ace::project::build_probe_document;
using ace::project::ProbeDocument;
using ace::render::CanvasRenderer;
using ace::render::Srgb8Image;

namespace {
constexpr int k_w = ace::project::k_probe_width;
constexpr int k_h = ace::project::k_probe_height;
} // namespace

TEST_CASE("canvas_view: first step composites, then the still scene early-outs") {
  const ProbeDocument probe = build_probe_document();
  CanvasRenderer driver(*probe.document);
  driver.resize(k_w, k_h);

  // The first step always composites (frames_issued() == 1); the playhead is
  // pinned, so a second step on the unchanged scene issues ZERO further frames
  // (the still-scene early-out, host_viewport.hpp).
  driver.step();
  CHECK(driver.frames_issued() == 1);
  driver.step();
  CHECK(driver.frames_issued() == 1);

  // The settled image is the pane size, tightly packed straight-alpha sRGB8.
  const Srgb8Image& image = driver.image();
  CHECK(image.width == k_w);
  CHECK(image.height == k_h);
  CHECK(image.pixels.size() == static_cast<std::size_t>(k_w) * k_h * 4);
}

TEST_CASE("canvas_view: resize re-frames to the new dimensions") {
  const ProbeDocument probe = build_probe_document();
  CanvasRenderer driver(*probe.document);
  driver.resize(k_w, k_h);
  driver.step();
  REQUIRE(driver.image().width == k_w);

  driver.resize(48, 32);
  driver.step();
  CHECK(driver.width() == 48);
  CHECK(driver.height() == 32);
  CHECK(driver.image().width == 48);
  CHECK(driver.image().height == 32);
  CHECK(driver.image().pixels.size() == static_cast<std::size_t>(48) * 32 * 4);
}

TEST_CASE("canvas_view: a zero-area pane renders nothing (no allocation)") {
  const ProbeDocument probe = build_probe_document();
  CanvasRenderer driver(*probe.document);
  driver.resize(0, 0);
  driver.step();
  CHECK(driver.frames_issued() == 0);
  CHECK(driver.image().pixels.empty());
}

TEST_CASE("canvas_view: interactive golden matches the offline reference byte-for-byte") {
  const ProbeDocument probe = build_probe_document();
  CanvasRenderer driver(*probe.document);
  driver.resize(k_w, k_h);
  driver.step();
  const Srgb8Image& interactive = driver.image();

  // 1. Byte-exact against the committed interactive golden (GL-free lane).
  const std::string golden = "canvas_view_" + std::to_string(interactive.width) + "x" +
                             std::to_string(interactive.height) + ".rgba8";
  CHECK(ace_test::compare_golden(golden, interactive.pixels));

  // 2. Cross-check: the interactive compositor path (HostViewport +
  //    InteractiveRenderer) composes pixel-for-pixel like the byte-exact offline
  //    render_offline reference render_probe shipped, for the same document +
  //    framing. This is the guarantee A6's Backend seam rests on.
  const Srgb8Image offline = ace::render::render_document_srgb8(*probe.document, k_w, k_h);
  CHECK(interactive.pixels == offline.pixels);
}
