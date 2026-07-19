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
#include <arbc/compositor/anchored_viewports.hpp> // k_reanchor_scale_threshold (nav anchor_depth)
#include <arbc/kind_raster/raster_content.hpp>    // DecodedImage / RasterContent (bounded content)
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
#include <vector>

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

// A sized-but-content-free document: one composition, no layers/content — every composite
// leaves the working-space target transparent black (a blank frame the content gate withholds).
std::unique_ptr<arbc::Document> build_empty_doc() {
  auto doc = std::make_unique<arbc::Document>();
  doc->add_composition(static_cast<double>(k_w), static_cast<double>(k_h));
  return doc;
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

  // One edit to the SHARED document via apply_edit (runs synchronously here, under the
  // document lock), then a drive fans the resulting damage out to both entries.
  host.apply_edit([&] { damage(*probe.document, probe.composition); });
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

TEST_CASE(
    "canvas_host: a blank first frame is withheld per entry; a resized content entry republishes") {
  const ProbeDocument probe = build_probe_document();
  auto empty = build_empty_doc();
  CanvasHost host = make_inline_host();

  // canvas#1 over a content-free document: every composite is transparent, so the content
  // gate withholds its publish and the per-entry sequence stays 0. canvas#2 over the probe
  // (a solid fill) composites content inline and publishes normally — the gate is per-entry.
  host.add("canvas#1", *empty);
  host.request_resize("canvas#1", k_w, k_h);
  host.add("canvas#2", *probe.document);
  host.request_resize("canvas#2", k_w, k_h);
  host.drive_once();
  host.drive_once(); // a second still drive re-scans the blank entry and still withholds

  CHECK(host.published_sequence("canvas#1") == 0); // blank -> withheld
  CHECK(host.published_sequence("canvas#2") >= 1); // content -> published
  std::uint64_t sb = 0;
  Srgb8Image fb;
  CHECK_FALSE(host.consume("canvas#1", sb, fb)); // nothing published for the blank entry

  // The content gate is once-only, NOT re-keyed per size: after a published content entry
  // resizes, its transient frames publish normally (the gate does not withhold again), so
  // the still-scene early-out re-key (published_frames reset to 0) republishes the new size.
  const std::uint64_t before = host.published_sequence("canvas#2");
  host.request_resize("canvas#2", 48, 32);
  host.drive_once();
  CHECK(host.published_sequence("canvas#2") > before);
  std::uint64_t s2 = 0;
  Srgb8Image f2;
  REQUIRE(host.consume("canvas#2", s2, f2));
  CHECK(f2.width == 48);
  CHECK(f2.height == 32);
}

TEST_CASE("canvas_host: once content has published, a partial refinement publishes normally") {
  ProbeDocument probe = build_probe_document();
  CanvasHost host = make_inline_host();
  host.add("canvas#1", *probe.document);
  host.request_resize("canvas#1", k_w, k_h);
  host.drive_once();
  REQUIRE(host.published_sequence("canvas#1") == 1); // first content frame published

  // Once content has published (content_published latched) the gate is skipped, so a later
  // damaged frame publishes on the normal frames_issued advance (the sequence stays monotonic).
  host.apply_edit([&] { damage(*probe.document, probe.composition); });
  host.drive_once();
  CHECK(host.published_sequence("canvas#1") == 2);
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

  // A UI-thread edit through apply_edit runs on THIS thread but under the document lock the
  // render thread also holds around its per-frame read, so the two never overlap; the edit
  // then fans out, advancing both entries off-thread. Mutating the shared document directly
  // here (outside apply_edit) would race the render thread's live read (TSan).
  const std::uint64_t before1 = host.published_sequence("canvas#1");
  const std::uint64_t before2 = host.published_sequence("canvas#2");
  host.apply_edit([&] { damage(*probe.document, probe.composition); });
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") > before1 &&
           host.published_sequence("canvas#2") > before2;
  }));

  // A camera submit rides the SAME lock as resize/poke (editor.canvas.nav): the second
  // UI→render channel, applied on the render thread before step(). A non-identity camera
  // is device damage, so canvas#1's sequence advances off-thread — the pending-Affine slot
  // is lock-guarded exactly like the pending resize, so ASan/TSan see no torn read/write.
  const std::uint64_t cam_before = host.published_sequence("canvas#1");
  host.request_camera("canvas#1", arbc::Affine{1.0, 0.0, 0.0, 1.0, -6.0, -4.0});
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") > cam_before; }));

  // Remove one entry mid-run — its cache frees on the render thread, no leak.
  host.remove("canvas#2");
  REQUIRE(pump_until([&] { return host.live_count() == 1; }));

  // Clean stop -> join (Constraint 5); entries tear down before the shared pool.
  host.stop();
  handle->join();
  CHECK(host.live_count() == 1);
  CHECK(host.published_sequence("canvas#1") >= 2);
}

// --- editor.canvas.nav: the per-entry camera channel + deep-zoom observability ------

namespace {
// A camera-DEPENDENT document: an unbounded solid background plus a BOUNDED 16x16 raster
// square (finite content, unlike the full-frame probe solid), so a pan camera reveals a
// genuinely different frame — the interactive path must thread the exact Affine through.
std::unique_ptr<arbc::Document> build_raster_doc() {
  auto doc = std::make_unique<arbc::Document>();
  const arbc::ObjectId root = doc->add_composition(64.0, 64.0);
  const arbc::ObjectId bg =
      doc->add_content(std::make_shared<arbc::SolidContent>(arbc::Rgba{0.0F, 0.35F, 0.0F, 1.0F}));
  doc->attach_layer(root, doc->add_layer(bg, arbc::Affine::identity()));
  arbc::DecodedImage img;
  img.width = 16;
  img.height = 16;
  img.format = arbc::k_working_rgba32f;
  img.bytes.resize(static_cast<std::size_t>(16) * 16 * 4 * sizeof(float));
  auto* fp = reinterpret_cast<float*>(img.bytes.data());
  for (int i = 0; i < 16 * 16; ++i) { // opaque red, premultiplied linear
    fp[i * 4] = 0.6F;
    fp[i * 4 + 1] = 0.0F;
    fp[i * 4 + 2] = 0.0F;
    fp[i * 4 + 3] = 1.0F;
  }
  const arbc::ObjectId raster =
      doc->add_content(std::make_shared<arbc::RasterContent>(std::move(img)));
  doc->attach_layer(root, doc->add_layer(raster, arbc::Affine::translation(8.0, 8.0)));
  return doc;
}

// A native-scale (integer-translation) pan camera: non-identity, camera-dependent, and — at
// native scale, so no scale-rung/mip resample divergence — the interactive path stays
// BYTE-IDENTICAL to render_offline. A zoom would legitimately diverge (the progressive
// interactive rungs vs. the single-shot offline resample), which is why the byte-exact
// cross-check uses a translation.
constexpr arbc::Affine k_nav_camera{1.0, 0.0, 0.0, 1.0, -12.0, -8.0};

// A nested composition chain (comps[0] is the lowest-id root anchor; each holds a layer
// whose content is the next composition, placed by `edge`). Zooming past the
// well-conditioned band re-anchors DOWN the chain, so anchor_depth rises — the deep-zoom
// rebasing the editor surfaces (D-nav-5). Mirrors libarbc's own host_viewport chain fixture.
constexpr double kNestC = 1000.0;
constexpr double kNestCtr = 500.0;
constexpr double kNestLvl = 0.001;
arbc::Affine nest_edge() {
  return arbc::compose(arbc::Affine::translation(kNestCtr - 0.5, kNestCtr - 0.5),
                       arbc::Affine::scaling(kNestLvl, kNestLvl));
}
arbc::Affine nest_camera(double scale) {
  return arbc::compose(arbc::Affine::translation(kNestCtr, kNestCtr),
                       arbc::compose(arbc::Affine::scaling(scale, scale),
                                     arbc::Affine::translation(-kNestCtr, -kNestCtr)));
}
std::unique_ptr<arbc::Document> build_nested_doc(int levels) {
  auto doc = std::make_unique<arbc::Document>();
  std::vector<arbc::ObjectId> comps;
  for (int i = 0; i <= levels; ++i) {
    comps.push_back(doc->add_composition(kNestC, kNestC));
  }
  const arbc::ObjectId leaf =
      doc->add_content(std::make_shared<arbc::SolidContent>(arbc::Rgba{0.0F, 0.5F, 0.0F, 1.0F}));
  doc->attach_layer(comps[static_cast<std::size_t>(levels)],
                    doc->add_layer(leaf, arbc::Affine::identity()));
  for (int i = 0; i < levels; ++i) {
    doc->attach_layer(comps[static_cast<std::size_t>(i)],
                      doc->add_layer(comps[static_cast<std::size_t>(i + 1)], nest_edge()));
  }
  return doc;
}

// Drive the inline host to settle (each drive_once publishes at most one frame; a settled
// still scene returns false), bounded so a defect cannot hang the suite.
void settle(CanvasHost& host) {
  for (int i = 0; i < 32 && host.drive_once(); ++i) {
  }
}
} // namespace

TEST_CASE("canvas_host: request_camera reaches HostViewport::set_camera — the frame reframes") {
  auto doc = build_raster_doc();
  CanvasHost host = make_inline_host();
  host.add("canvas#1", *doc);
  host.request_resize("canvas#1", k_w, k_h);
  settle(host);
  std::uint64_t s = 0;
  Srgb8Image identity_frame;
  REQUIRE(host.consume("canvas#1", s, identity_frame));

  host.request_camera("canvas#1", k_nav_camera);
  settle(host);
  Srgb8Image nav_frame;
  REQUIRE(host.consume("canvas#1", s, nav_frame));

  // (e) The camera threaded through: the frame changed AND equals the byte-exact offline
  //     render for the SAME Affine — not just "some non-identity frame", the EXACT one.
  CHECK(nav_frame.pixels != identity_frame.pixels);
  const Srgb8Image offline = ace::render::render_document_srgb8(*doc, k_w, k_h, k_nav_camera);
  CHECK(nav_frame.pixels == offline.pixels);
}

TEST_CASE("canvas_host: a resize after a camera submit PRESERVES the camera (rebuild reapplies)") {
  auto doc = build_raster_doc();
  CanvasHost host = make_inline_host();
  host.add("canvas#1", *doc);
  host.request_resize("canvas#1", k_w, k_h);
  host.request_camera("canvas#1", k_nav_camera);
  settle(host);
  std::uint64_t s = 0;
  Srgb8Image f;
  REQUIRE(host.consume("canvas#1", s, f));

  // (f) A pane resize reconstructs the non-movable HostViewport; it must reframe with the
  //     held camera, not reset to identity (Constraint 3). The reframed frame equals the
  //     offline render at the NEW size through the SAME camera.
  host.request_resize("canvas#1", 48, 40);
  settle(host);
  Srgb8Image resized;
  REQUIRE(host.consume("canvas#1", s, resized));
  CHECK(resized.width == 48);
  CHECK(resized.height == 40);
  const Srgb8Image offline = ace::render::render_document_srgb8(*doc, 48, 40, k_nav_camera);
  CHECK(resized.pixels == offline.pixels);
}

TEST_CASE("canvas_host: request_camera is per-entry — one canvas's camera leaves the other's") {
  auto doc = build_raster_doc();
  CanvasHost host = make_inline_host();
  host.add("canvas#1", *doc);
  host.request_resize("canvas#1", k_w, k_h);
  host.add("canvas#2", *doc);
  host.request_resize("canvas#2", k_w, k_h);
  host.request_camera("canvas#1", k_nav_camera); // only canvas#1 pans
  settle(host);

  // (g) canvas#1 shows the panned framing; canvas#2 stays at identity — the two channels
  //     do not cross.
  std::uint64_t s1 = 0;
  Srgb8Image f1;
  REQUIRE(host.consume("canvas#1", s1, f1));
  std::uint64_t s2 = 0;
  Srgb8Image f2;
  REQUIRE(host.consume("canvas#2", s2, f2));
  CHECK(f1.pixels == ace::render::render_document_srgb8(*doc, k_w, k_h, k_nav_camera).pixels);
  CHECK(f2.pixels == ace::render::render_document_srgb8(*doc, k_w, k_h).pixels);
  CHECK(f1.pixels != f2.pixels);
}

TEST_CASE("canvas_host: anchor_depth surfaces deep-zoom rebasing — non-zero in, back to 0 out") {
  auto doc = build_nested_doc(/*levels=*/1);
  CanvasHost host = make_inline_host();
  host.add("canvas#1", *doc);
  host.request_resize("canvas#1", k_w, k_h);
  settle(host);
  // (h) A still, in-band frame is anchored at the root: depth 0.
  CHECK(host.anchor_depth("canvas#1") == 0);
  CHECK(host.anchor_depth("canvas#7") == 0); // unknown id

  // Zoom PAST the well-conditioned band: step() re-anchors to the descendant composition.
  host.request_camera("canvas#1", nest_camera(arbc::k_reanchor_scale_threshold * 2.0));
  settle(host);
  CHECK(host.anchor_depth("canvas#1") == 1);

  // Zoom back far out: the anchor path pops and depth returns to 0 (rebasing observably
  // reversed).
  host.request_camera("canvas#1", nest_camera(1.0 / (arbc::k_reanchor_scale_threshold * 2.0)));
  settle(host);
  CHECK(host.anchor_depth("canvas#1") == 0);
}

TEST_CASE("canvas_host: an interactive-camera frame is byte-exact vs offline + the golden") {
  auto doc = build_raster_doc();
  CanvasHost host = make_inline_host();
  host.add("canvas#1", *doc);
  host.request_resize("canvas#1", k_w, k_h);
  host.request_camera("canvas#1", k_nav_camera);
  settle(host);
  std::uint64_t s = 0;
  Srgb8Image f;
  REQUIRE(host.consume("canvas#1", s, f));

  // 1. Byte-exact against the committed interactive-camera golden (inline degenerate pool,
  //    D10/D-canvas_view-2 byte-exactness preserved through the camera channel).
  CHECK(ace_test::compare_golden("canvas_nav_zoom_64x64.rgba8", f.pixels));
  // 2. Cross-check: identical to the offline render through the SAME Affine — the camera
  //    threads to the composite EXACTLY, not just approximately (D-nav-3).
  const Srgb8Image offline = ace::render::render_document_srgb8(*doc, k_w, k_h, k_nav_camera);
  CHECK(f.pixels == offline.pixels);
}

TEST_CASE("canvas_host: a blank-then-content first frame publishes under the REAL bounded pool") {
  // The gate's liveness path: under a real WorkerPool + bounded budget the first step() can
  // issue a blank frame (tiles still resolving) that the content gate withholds. There is NO
  // async-completion wake, so the drive loop must stay armed while the renderer keeps issuing
  // new frames (tiles in flight) until a content frame composites and publishes — otherwise
  // the canvas stalls blank at sequence 0. build_raster_doc's full-frame solid background is
  // covered content the compositor resolves within a few bounded steps.
  auto doc = build_raster_doc();
  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  ace::platform::NativeThreads threads;
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *doc);
  host.request_resize("canvas#1", k_w, k_h);
  // No pokes after the initial add/resize: the loop must reach the content frame on its own.
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 1; }));

  // A RESIZE re-keys the still-scene early-out (published_frames reset): the first frame at
  // the new size republishes on its own (the multi_canvas e2e's WindowFocus-resize path).
  const std::uint64_t before = host.published_sequence("canvas#1");
  host.request_resize("canvas#1", 48, 40);
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") > before; }));

  host.stop();
  handle->join();
  CHECK(host.published_sequence("canvas#1") > before);
}
