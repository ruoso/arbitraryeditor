// editor.canvas.multi_canvas — the N-canvas host, headless + GL-free
// (docs/01-architecture.md §9). Exercises render::CanvasHost — one shared WorkerPool +
// a canvas#N-keyed map of per-canvas CanvasRenderer + double-buffer entries + one drive
// loop — WITHOUT a GL context. The deterministic tests drive with an INLINE pool
// (WorkerPoolConfig{}) + settle-fully budget so the handoff is reproducible
// (D-multi_canvas-4): two entries publish independent monotonic sequences, per-entry
// consume/resize, one edit poking BOTH (N observers, one writer), add/remove mutating the
// live set, the two entries borrowing ONE pool, and a hosted entry staying byte-identical
// to the offline reference + the committed golden. The final case drives the REAL shared
// WorkerPool (worker threads) through the full add->render->edit->remove->teardown
// lifecycle on a spawned render thread — the escalated ASan/TSan concurrency target.
#include <ace/platform/threads.hpp>
#include <ace/project/project.hpp>
#include <ace/render/canvas_host.hpp>
#include <ace/render/render.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/interactive.hpp>
#include <arbc/runtime/worker_pool.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "golden_support.hpp"

using ace::project::build_probe_document;
using ace::project::ProbeDocument;
using ace::render::CanvasHost;
using ace::render::Srgb8Image;

namespace {
constexpr int k_w = ace::project::k_probe_width;
constexpr int k_h = ace::project::k_probe_height;

// The deterministic host config: the inline degenerate pool (worker_count 0, byte-exact)
// + a settle-fully hour budget, so one drive_once() composites the whole frame.
CanvasHost make_inline_host() {
  return CanvasHost(arbc::WorkerPoolConfig{}, std::chrono::hours(1));
}

// Add + size two canvases over one document and settle them in one drive.
void seed_two(CanvasHost& host, arbc::Document& document, int w, int h) {
  host.add("canvas#1", document);
  host.request_resize("canvas#1", w, h);
  host.add("canvas#2", document);
  host.request_resize("canvas#2", w, h);
  host.drive_once();
}

// Attach a distinct opaque covering layer — a writer-thread edit that damages the
// composition so the next drive re-composites.
void damage(arbc::Document& document, arbc::ObjectId composition) {
  const arbc::ObjectId content = document.add_content(
      std::make_shared<arbc::SolidContent>(arbc::Rgba{0.9F, 0.1F, 0.1F, 1.0F}));
  const arbc::ObjectId layer = document.add_layer(content, arbc::Affine::identity());
  document.attach_layer(composition, layer);
}

// Deadline-based pump for the off-thread lifecycle case — holds under a sanitizer build's
// slowdown (no fixed iteration count).
template <class Ready> bool pump_until(Ready ready) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (std::chrono::steady_clock::now() < deadline) {
    if (ready()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return ready();
}
} // namespace

TEST_CASE("canvas_host: two entries over one document publish independent, monotonic frames") {
  const ProbeDocument probe = build_probe_document();
  CanvasHost host = make_inline_host();
  seed_two(host, *probe.document, k_w, k_h);

  // Each entry settled its own frame under its own sequence counter (both start at 1).
  CHECK(host.live_count() == 2);
  CHECK(host.published_sequence("canvas#1") == 1);
  CHECK(host.published_sequence("canvas#2") == 1);
}

TEST_CASE("canvas_host: consume returns each entry's own frame; a re-consume is a no-op") {
  const ProbeDocument probe = build_probe_document();
  CanvasHost host = make_inline_host();
  seed_two(host, *probe.document, k_w, k_h);

  std::uint64_t s1 = 0;
  Srgb8Image f1;
  REQUIRE(host.consume("canvas#1", s1, f1));
  CHECK(s1 == 1);
  CHECK(f1.width == k_w);
  CHECK(f1.height == k_h);
  CHECK(f1.pixels.size() == static_cast<std::size_t>(k_w) * k_h * 4);
  // A second consume at the same sequence reports "no new frame" (out untouched).
  CHECK_FALSE(host.consume("canvas#1", s1, f1));
  CHECK(s1 == 1);

  // canvas#2 serves ITS own frame independently (its sequence unaffected by canvas#1).
  std::uint64_t s2 = 0;
  Srgb8Image f2;
  REQUIRE(host.consume("canvas#2", s2, f2));
  CHECK(s2 == 1);
  CHECK(f2.width == k_w);

  // An unknown id is never served.
  std::uint64_t su = 0;
  Srgb8Image fu;
  CHECK_FALSE(host.consume("canvas#7", su, fu));
}

TEST_CASE("canvas_host: request_resize is per-entry — resizing one leaves the other's size") {
  const ProbeDocument probe = build_probe_document();
  CanvasHost host = make_inline_host();
  seed_two(host, *probe.document, k_w, k_h);

  // Reframe only canvas#1; the next drive republishes it at the new size.
  host.request_resize("canvas#1", 48, 32);
  host.drive_once();

  std::uint64_t s1 = 0;
  Srgb8Image f1;
  REQUIRE(host.consume("canvas#1", s1, f1));
  CHECK(f1.width == 48);
  CHECK(f1.height == 32);

  // canvas#2 was never resized: its still frame stayed at the original size and sequence.
  CHECK(host.published_sequence("canvas#2") == 1);
  std::uint64_t s2 = 0;
  Srgb8Image f2;
  REQUIRE(host.consume("canvas#2", s2, f2));
  CHECK(f2.width == k_w);
  CHECK(f2.height == k_h);
}

TEST_CASE("canvas_host: one edit + poke advances BOTH entries' sequences (N observers)") {
  ProbeDocument probe = build_probe_document();
  CanvasHost host = make_inline_host();
  seed_two(host, *probe.document, k_w, k_h);
  REQUIRE(host.published_sequence("canvas#1") == 1);
  REQUIRE(host.published_sequence("canvas#2") == 1);

  // One writer-thread edit to the SHARED document, then a single fan-out poke.
  damage(*probe.document, probe.composition);
  host.poke();
  host.drive_once();

  // Both observers of the one document re-rendered the new revision.
  CHECK(host.published_sequence("canvas#1") == 2);
  CHECK(host.published_sequence("canvas#2") == 2);
}

TEST_CASE("canvas_host: add/remove mutate the live set — a removed id is no longer served") {
  const ProbeDocument probe = build_probe_document();
  CanvasHost host = make_inline_host();
  seed_two(host, *probe.document, k_w, k_h);
  REQUIRE(host.live_count() == 2);

  host.remove("canvas#2");
  host.drive_once(); // the removal is serviced on the render side

  CHECK(host.live_count() == 1);
  // The removed id is quiet: sequence 0, never served.
  CHECK(host.published_sequence("canvas#2") == 0);
  std::uint64_t s = 0;
  Srgb8Image f;
  CHECK_FALSE(host.consume("canvas#2", s, f));
  // canvas#1 stays live and served.
  CHECK(host.published_sequence("canvas#1") >= 1);
}

TEST_CASE("canvas_host: the two entries borrow ONE shared WorkerPool") {
  const ProbeDocument probe = build_probe_document();
  CanvasHost host = make_inline_host();
  seed_two(host, *probe.document, k_w, k_h);

  // A single WorkerPool object backs both entries (not two) — the only correct
  // multi-viewport shape (A5 / interactive.hpp:230-237).
  CHECK(host.entry_pool("canvas#1") == &host.worker_pool());
  CHECK(host.entry_pool("canvas#2") == &host.worker_pool());
  CHECK(host.entry_pool("canvas#1") == host.entry_pool("canvas#2"));
  CHECK(host.entry_pool("canvas#1") != nullptr);
  CHECK(host.entry_pool("canvas#7") == nullptr); // unknown id
}

TEST_CASE("canvas_host: a hosted entry is byte-exact vs offline + the golden; a twin matches") {
  const ProbeDocument probe = build_probe_document();
  CanvasHost host = make_inline_host();
  host.add("canvas#1", *probe.document);
  host.request_resize("canvas#1", k_w, k_h);
  host.drive_once();

  std::uint64_t s1 = 0;
  Srgb8Image f1;
  REQUIRE(host.consume("canvas#1", s1, f1));

  // 1. Byte-exact against the committed interactive golden (the shared-pool wiring in its
  //    inline-degenerate form preserves D10/D-canvas_view-2 byte-exactness).
  const std::string golden =
      "canvas_view_" + std::to_string(f1.width) + "x" + std::to_string(f1.height) + ".rgba8";
  CHECK(ace_test::compare_golden(golden, f1.pixels));

  // 2. Cross-check: identical to the byte-exact offline render for the same framing.
  const Srgb8Image offline = ace::render::render_document_srgb8(*probe.document, k_w, k_h);
  CHECK(f1.pixels == offline.pixels);

  // 3. A second entry at the same size over the same document yields the identical bytes
  //    (two canvases, one document -> one image).
  host.add("canvas#2", *probe.document);
  host.request_resize("canvas#2", k_w, k_h);
  host.drive_once();
  std::uint64_t s2 = 0;
  Srgb8Image f2;
  REQUIRE(host.consume("canvas#2", s2, f2));
  CHECK(f2.pixels == f1.pixels);
}

TEST_CASE("canvas_host: the REAL shared-pool lifecycle runs clean off a spawned render thread") {
  ProbeDocument probe = build_probe_document();
  // The shipped path: a real interactive WorkerPool (worker threads) + a bounded budget —
  // the escalated ASan/TSan target (N renderers ‖ one pool ‖ one render thread ‖ UI
  // writer+poke ‖ HousekeepingThread ‖ N double-buffer handoffs).
  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));

  ace::platform::NativeThreads threads;
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *probe.document);
  host.request_resize("canvas#1", k_w, k_h);
  host.add("canvas#2", *probe.document);
  host.request_resize("canvas#2", k_w, k_h);
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 1 && host.published_sequence("canvas#2") >= 1;
  }));
  CHECK(host.iterations() >= 1); // the render thread drove real iterations

  // A writer-thread edit + fan-out poke advances both entries off-thread.
  const std::uint64_t before1 = host.published_sequence("canvas#1");
  const std::uint64_t before2 = host.published_sequence("canvas#2");
  damage(*probe.document, probe.composition);
  host.poke();
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") > before1 &&
           host.published_sequence("canvas#2") > before2;
  }));

  // Remove one entry mid-run — its cache frees on the render thread, no leak.
  host.remove("canvas#2");
  REQUIRE(pump_until([&] { return host.live_count() == 1; }));

  // Clean stop -> join (Constraint 5); entries tear down before the shared pool.
  host.stop();
  handle->join();
  CHECK(host.live_count() == 1);
  CHECK(host.published_sequence("canvas#1") >= 2);
}
