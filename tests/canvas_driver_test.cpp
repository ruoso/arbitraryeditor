// editor.canvas.frame_sync — the off-UI-thread canvas driver, headless + GL-free
// (docs/01-architecture.md §9). Exercises render::CanvasDriver — the render-thread
// drive loop + latest-frame double-buffer — WITHOUT a GL context. Drives iterations
// with the deterministic inline executor (through CanvasRenderer) so the handoff is
// reproducible (D-frame_sync-3): asserts publish/consume + sequencing, the still-scene
// early-out across the buffer, resize-request application on the render side, a real
// spawned drive loop stopping on a predicate, and that the double-buffered frame stays
// byte-identical to the offline reference + the committed interactive golden (the
// off-thread handoff perturbs no pixels).
#include <ace/platform/threads.hpp>
#include <ace/project/project.hpp>
#include <ace/render/canvas_driver.hpp>
#include <ace/render/render.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "golden_support.hpp"

using ace::project::build_probe_document;
using ace::project::ProbeDocument;
using ace::render::CanvasDriver;
using ace::render::frame_has_content;
using ace::render::Srgb8Image;

namespace {
constexpr int k_w = ace::project::k_probe_width;
constexpr int k_h = ace::project::k_probe_height;

// A sized-but-content-free document: one composition, no layers/content — so every
// composite leaves the working-space target transparent black (a blank frame).
std::unique_ptr<arbc::Document> build_empty_doc() {
  auto doc = std::make_unique<arbc::Document>();
  doc->add_composition(static_cast<double>(k_w), static_cast<double>(k_h));
  return doc;
}
} // namespace

TEST_CASE("frame_has_content keys on straight-alpha coverage, never on colour") {
  // An empty image carries no coverage.
  CHECK_FALSE(frame_has_content(Srgb8Image{}));

  // A single all-zero-alpha quad -> no coverage.
  Srgb8Image blank;
  blank.width = 1;
  blank.height = 1;
  blank.pixels = {0, 0, 0, 0};
  CHECK_FALSE(frame_has_content(blank));

  // One alpha-255 pixel -> content.
  Srgb8Image opaque = blank;
  opaque.pixels = {10, 20, 30, 255};
  CHECK(frame_has_content(opaque));

  // Opaque BLACK (0,0,0,255) still trips it — it keys on alpha, not colour, so opaque
  // content of any colour (including a background collision) is coverage.
  Srgb8Image opaque_black = blank;
  opaque_black.pixels = {0, 0, 0, 255};
  CHECK(frame_has_content(opaque_black));

  // Non-zero RGB with zero alpha (200,0,0,0) -> NOT content (colour without coverage).
  Srgb8Image rgb_no_alpha = blank;
  rgb_no_alpha.pixels = {200, 0, 0, 0};
  CHECK_FALSE(frame_has_content(rgb_no_alpha));

  // A multi-pixel buffer trips on ANY single covered pixel (the last one here).
  Srgb8Image multi;
  multi.width = 2;
  multi.height = 1;
  multi.pixels = {0, 0, 0, 0, 0, 0, 0, 7};
  CHECK(frame_has_content(multi));
}

TEST_CASE("canvas_driver: a blank first frame is withheld — the sequence never advances on empty") {
  auto doc = build_empty_doc();
  CanvasDriver driver(*doc);
  driver.request_resize(k_w, k_h);

  // The first drive composites no tile (a transparent frame), so the content gate
  // withholds the publish: drive_once() returns false and the sequence stays at 0 even
  // though the first step issued a frame. A further still drive still publishes nothing.
  CHECK_FALSE(driver.drive_once());
  CHECK(driver.published_sequence() == 0);
  CHECK_FALSE(driver.drive_once());
  CHECK(driver.published_sequence() == 0);

  // The consumer sees no frame (nothing was published).
  std::uint64_t seq = 0;
  Srgb8Image frame;
  CHECK_FALSE(driver.consume(seq, frame));
}

TEST_CASE("canvas_driver: publish/consume — a settled frame is consumed once, then no new frame") {
  const ProbeDocument probe = build_probe_document();
  CanvasDriver driver(*probe.document);
  driver.request_resize(k_w, k_h);

  // One drive iteration settles the scene and publishes a fresh frame.
  CHECK(driver.drive_once());
  CHECK(driver.published_sequence() == 1);

  std::uint64_t seq = 0;
  Srgb8Image frame;
  REQUIRE(driver.consume(seq, frame));
  CHECK(seq == 1);
  CHECK(frame.width == k_w);
  CHECK(frame.height == k_h);
  CHECK(frame.pixels.size() == static_cast<std::size_t>(k_w) * k_h * 4);

  // A second consume at the same sequence reports "no new frame" (out untouched).
  CHECK_FALSE(driver.consume(seq, frame));
  CHECK(seq == 1);

  // A further still-scene iteration publishes nothing (the early-out across the
  // buffer): the sequence holds and the consumer still sees no new frame.
  CHECK_FALSE(driver.drive_once());
  CHECK(driver.published_sequence() == 1);
  CHECK_FALSE(driver.consume(seq, frame));
}

TEST_CASE("canvas_driver: an edit damages the scene — the next iteration advances the sequence") {
  ProbeDocument probe = build_probe_document();
  CanvasDriver driver(*probe.document);
  driver.request_resize(k_w, k_h);

  CHECK(driver.drive_once());
  std::uint64_t seq = 0;
  Srgb8Image frame;
  REQUIRE(driver.consume(seq, frame));
  REQUIRE(seq == 1);

  // Attach a full-frame covering layer (a distinct opaque colour) — a writer-thread
  // edit that damages the composition. The next drive re-composites and publishes.
  const arbc::ObjectId content = probe.document->add_content(
      std::make_shared<arbc::SolidContent>(arbc::Rgba{0.9F, 0.1F, 0.1F, 1.0F}));
  const arbc::ObjectId layer = probe.document->add_layer(content, arbc::Affine::identity());
  probe.document->attach_layer(probe.composition, layer);

  CHECK(driver.drive_once());
  CHECK(driver.published_sequence() == 2);
  REQUIRE(driver.consume(seq, frame));
  CHECK(seq == 2);
  CHECK(frame.width == k_w);
  CHECK(frame.height == k_h);
}

TEST_CASE("canvas_driver: a resize request is applied on the render side") {
  const ProbeDocument probe = build_probe_document();
  CanvasDriver driver(*probe.document);
  driver.request_resize(k_w, k_h);
  CHECK(driver.drive_once());

  std::uint64_t seq = 0;
  Srgb8Image frame;
  REQUIRE(driver.consume(seq, frame));
  CHECK(frame.width == k_w);

  // Post a new size; the NEXT iteration reframes and publishes at the new dimensions.
  driver.request_resize(48, 32);
  CHECK(driver.drive_once());
  REQUIRE(driver.consume(seq, frame));
  CHECK(frame.width == 48);
  CHECK(frame.height == 32);
  CHECK(frame.pixels.size() == static_cast<std::size_t>(48) * 32 * 4);
}

TEST_CASE(
    "canvas_driver: the run loop stops on its predicate and joins cleanly, publishing a frame") {
  const ProbeDocument probe = build_probe_document();
  CanvasDriver driver(*probe.document);
  driver.request_resize(k_w, k_h);

  // Spawn the drive loop on a real auxiliary thread (the A3 spawn seam) — the genuine
  // off-thread concurrency this leaf's sanitizer lane exercises. The predicate stops
  // the loop once it has iterated at least three times.
  ace::platform::NativeThreads threads;
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { driver.run([&] { return driver.iterations() >= 3; }); });

  // Feed it work: a poke on a settled scene still drives an iteration (and publishes
  // nothing), so the counter advances toward the stop threshold.
  while (driver.iterations() < 3) {
    driver.poke();
    std::this_thread::yield();
  }
  handle->join();

  CHECK(driver.iterations() >= 3);
  CHECK(driver.published_sequence() >= 1); // at least the first settled frame reached the buffer
}

TEST_CASE("canvas_driver: request_camera drives a fresh frame; a resize preserves it; "
          "anchor_depth is surfaced") {
  const ProbeDocument probe = build_probe_document();
  CanvasDriver driver(*probe.document);
  driver.request_resize(k_w, k_h);
  CHECK(driver.drive_once());
  std::uint64_t seq = 0;
  Srgb8Image frame;
  REQUIRE(driver.consume(seq, frame));
  REQUIRE(seq == 1);
  // A still, in-band identity frame anchors at the root: depth 0 (surfaced through the
  // render-thread-snapshotted atomic).
  CHECK(driver.anchor_depth() == 0);

  // A non-identity camera submit is device damage — the next drive re-composites and
  // publishes even for the uniform probe (frames_issued advances), reaching set_camera on
  // the render thread (editor.canvas.nav / D-nav-3).
  driver.request_camera(arbc::Affine{1.0, 0.0, 0.0, 1.0, -8.0, -5.0});
  CHECK(driver.drive_once());
  CHECK(driver.published_sequence() == 2);
  CHECK(driver.anchor_depth() == 0); // still in-band

  // A resize after the camera submit reframes at the new size with the camera preserved
  // (the renderer holds it across rebuild, Constraint 3) — the drive publishes the new size.
  driver.request_resize(40, 24);
  CHECK(driver.drive_once());
  REQUIRE(driver.consume(seq, frame));
  CHECK(frame.width == 40);
  CHECK(frame.height == 24);
}

TEST_CASE("canvas_driver: the double-buffered frame is byte-exact vs offline + the golden") {
  const ProbeDocument probe = build_probe_document();
  CanvasDriver driver(*probe.document);
  driver.request_resize(k_w, k_h);
  CHECK(driver.drive_once());

  std::uint64_t seq = 0;
  Srgb8Image frame;
  REQUIRE(driver.consume(seq, frame));

  // 1. Byte-exact against the committed interactive golden (GL-free lane), reusing
  //    the canvas_view golden — the off-thread handoff does not perturb pixels.
  const std::string golden =
      "canvas_view_" + std::to_string(frame.width) + "x" + std::to_string(frame.height) + ".rgba8";
  CHECK(ace_test::compare_golden(golden, frame.pixels));

  // 2. Cross-check: identical to the byte-exact offline render_document_srgb8 for the
  //    same document + framing (inline executor, deterministic).
  const Srgb8Image offline = ace::render::render_document_srgb8(*probe.document, k_w, k_h);
  CHECK(frame.pixels == offline.pixels);
}
