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
#include <ace/interact/interact.hpp>
#include <ace/interact/pick.hpp>
#include <ace/platform/threads.hpp>
#include <ace/project/project.hpp>
#include <ace/render/canvas_host.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>                 // register_builtin_kinds (org.arbc.nested/.solid)
#include <arbc/compositor/anchored_viewports.hpp> // k_reanchor_scale_threshold (nav anchor_depth)
#include <arbc/contract/registry.hpp>             // arbc::Registry
#include <arbc/kind_raster/raster_content.hpp>    // DecodedImage / RasterContent (bounded content)
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp> // KindBridge / load_document / settle_external_loads
#include <arbc/runtime/interactive.hpp>
#include <arbc/runtime/worker_pool.hpp>
#include <arbc/serialize/load_context.hpp> // arbc::AssetSource (the deferring double's base)

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
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

  // One edit to the SHARED document via apply_edit (runs synchronously here on the writer
  // thread), then a drive fans the resulting damage out to both entries.
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

  // A UI-thread edit through apply_edit runs on THIS (writer) thread; the render thread's
  // per-frame read needs no lock (arbc v0.2.0 COW content bindings, #10/#11), so the edit and
  // the read do not race. The edit then fans out, advancing both entries off-thread. Mutating
  // the shared document off the writer thread would break arbc's single writer-identity contract.
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

// --- editor.canvas.single_writer: UI-thread edits run lock-free against the render read ----
//
// Every UI-thread Document mutation runs through apply_edit on the writer thread; the render
// thread's per-frame for_each_content walk needs NO lock because arbc v0.2.0 publishes the
// content bindings copy-on-write behind an atomic (#10/#11) — the walk reads a stable snapshot
// while the edit rebinds. The real-pool streamed-edit overlap below is the TSan acceptance
// anchor (formerly edit_render_sync's doc_mu mutual-exclusion contract, retired with the lock).

TEST_CASE("canvas_host: a stream of UI-thread edits runs clean against the render read "
          "(edit_render_sync TSan anchor)") {
  ProbeDocument probe = build_probe_document();
  // The acceptance anchor (editor.canvas.single_writer). Real interactive pool (worker threads)
  // + a bounded budget + a spawned render thread looping drive_once -> for_each_content, so the
  // read of the content bindings genuinely overlaps the UI-thread writes in wall-clock time (the
  // interleaving CI contention hit). The UI thread streams many edits — each a d_contents
  // mutation (add_content/add_layer/attach_layer, an implicit transact) — through apply_edit on
  // the writer thread. No lock serializes them against the read: arbc v0.2.0 publishes the
  // content bindings copy-on-write behind an atomic (#10/#11), so the render walk reads a stable
  // snapshot while add_content rebinds.
  //
  // Regression-guard note (NOT a tautology): under arbc v0.1.0 this SAME interleaving raced on
  // the writer-owned d_contents (the debt doc_mu papered over). v0.2.0's COW publish is what
  // makes the lock-free read clean; pin back to v0.1.0 and TSan re-reports the race on d_contents.
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

  // Stream the edits: many rounds so TSan gets a wide overlap window against the looping read.
  constexpr int k_edits = 64;
  for (int i = 0; i < k_edits; ++i) {
    host.apply_edit([&] { damage(*probe.document, probe.composition); });
  }
  // Both live readers converge on the streamed revisions off-thread — the fan-out reached them.
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 2 && host.published_sequence("canvas#2") >= 2;
  }));

  // Clean stop -> join; entries tear down before the shared pool (Constraint 5).
  host.stop();
  handle->join();
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

// --- editor.canvas.nested_composition_binding: the external-arrival settle hook ----------
//
// The interactive CanvasRenderer/CanvasHost now wires a real KindBridge/Registry
// DocumentBinding, so a Document holding a nested child whose bytes arrive from a DEFERRING
// AssetSource after load settles-and-composites that child each frame — where an empty binding
// leaves it forever blank. These pin the before/after at the L2 unit tier, byte-exact against
// the offline-settled reference (D-nested_composition_binding-1/3).

namespace {
// The DEFERRING AssetSource double, libarbc's own async_external_load.t.cpp recipe: request()
// records (uri, on_ready) and fires NOTHING; fire_all() releases the outstanding continuations
// on the test's command. A document loaded through it holds pending_external_loads() == 1 until
// a settle installs the arrived child — so the async boundary is crossed deterministically, no
// sleeps, no flake.
class DeferringAssetSource final : public arbc::AssetSource {
public:
  void put(std::string uri, std::string bytes) {
    d_files.insert_or_assign(std::move(uri), std::move(bytes));
  }
  void request(std::string_view resolved_uri,
               std::function<void(std::string_view)> on_ready) override {
    d_outstanding.push_back(Request{std::string(resolved_uri), std::move(on_ready)});
  }
  // Deliver every outstanding request right now (absent bytes == empty, the AssetSource
  // absence contract), returning how many fired.
  std::size_t fire_all() {
    std::vector<Request> firing;
    firing.swap(d_outstanding);
    for (const Request& r : firing) {
      const auto it = d_files.find(r.uri);
      r.on_ready(it != d_files.end() ? std::string_view(it->second) : std::string_view{});
    }
    return firing.size();
  }

private:
  struct Request {
    std::string uri;
    std::function<void(std::string_view)> on_ready;
  };
  std::unordered_map<std::string, std::string> d_files;
  std::vector<Request> d_outstanding;
};

// A one-layer parent document embedding `ref` through org.arbc.nested (canvas 16x16), and the
// green-solid leaf child its ref resolves to (canvas 8x8, so the composited child covers only
// part of the frame — genuine straight-alpha coverage, not a full-frame fill).
std::string nesting_doc(std::string_view ref) {
  std::string layer = R"({"kind":"org.arbc.nested","kind_version":"1","params":{"ref":")";
  layer += ref;
  layer += R"("}})";
  return R"({"arbc":{"format":1},"composition":{"canvas":[0,0,16,16],"layers":[)" + layer + "]}}";
}
constexpr const char* k_leaf =
    R"({"arbc":{"format":1},"composition":{"canvas":[0,0,8,8],"layers":[)"
    R"({"kind":"org.arbc.solid","kind_version":"1","params":{"color":[0.0,1.0,0.0,1.0]}}]}})";

// Load a parent document that references `mem/child.arbc` through the deferring `source` (which
// already holds the child bytes): the parent loads WITHOUT the child, so pending == 1 until a
// settle. `bridge` is the load-side KindBridge (kept alive by the caller); the host seeds its
// OWN bridge from the same registry.
std::unique_ptr<arbc::Document> load_pending_nested(DeferringAssetSource& source,
                                                    arbc::KindBridge& bridge,
                                                    const arbc::Registry& registry) {
  source.put("mem/child.arbc", k_leaf);
  auto doc = std::make_unique<arbc::Document>();
  REQUIRE(arbc::load_document(nesting_doc("child.arbc"), *doc, bridge, registry, "mem/parent.arbc",
                              &source));
  REQUIRE(doc->pending_external_loads() == 1);
  return doc;
}
} // namespace

TEST_CASE("canvas_host: the wired binding settles a deferred external nested child and composites "
          "it — byte-exact vs the offline-settled reference") {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);

  DeferringAssetSource source;
  arbc::KindBridge load_bridge;
  std::unique_ptr<arbc::Document> doc = load_pending_nested(source, load_bridge, registry);

  CanvasHost host = make_inline_host();
  host.add("canvas#1", *doc, &registry); // the wired binding {&bridge, &registry}
  host.request_resize("canvas#1", k_w, k_h);

  // First drive with nothing arrived: the settle finds an empty queue and the placeholder
  // composites no coverage, so the content gate withholds (sequence stays 0).
  host.drive_once();
  CHECK(host.published_sequence("canvas#1") == 0);
  CHECK(doc->pending_external_loads() == 1);

  // The bytes come back on the source's own schedule — the host does not know. Its next step()
  // runs settle_external_loads FIRST, installs the child, and the SAME frame composites it.
  REQUIRE(source.fire_all() == 1);
  settle(host);

  CHECK(doc->pending_external_loads() == 0);       // the queue drained
  CHECK(host.published_sequence("canvas#1") >= 1); // and a content frame published
  std::uint64_t s = 0;
  Srgb8Image frame;
  REQUIRE(host.consume("canvas#1", s, frame));
  CHECK(ace::render::frame_has_content(frame)); // the child's straight-alpha coverage

  // Byte-exact vs the settled reference. NOTE the reference is NOT render_document_srgb8:
  // render_offline never binds operators (offline.cpp — no bind_operators/OperatorBindingScope),
  // so it composites NO nested-composition operator at all, settled or not, and yields a blank
  // frame for this document. The convergence claim is therefore proved against a MATCHED
  // compositor: an INDEPENDENT copy pre-settled to quiescence (loop settle_external_loads to 0),
  // then rendered through an EMPTY-binding interactive host — once the child is installed the
  // empty binding composites it via bind_operators (the settle hook is what an empty binding
  // lacks, not the compositor). The wired settle must converge to that fully-settled render
  // byte-for-byte. (render_offline's inability to render — or settle — nested compositions is a
  // separate offline/export-path gap, surfaced to the parking lot.)
  DeferringAssetSource ref_source;
  arbc::KindBridge ref_bridge;
  std::unique_ptr<arbc::Document> ref_doc = load_pending_nested(ref_source, ref_bridge, registry);
  REQUIRE(ref_source.fire_all() == 1);
  for (int i = 0; i < 8 && arbc::settle_external_loads(*ref_doc, ref_bridge, registry) != 0; ++i) {
  }
  REQUIRE(ref_doc->pending_external_loads() == 0); // pre-settled to quiescence

  CanvasHost ref_host = make_inline_host();
  ref_host.add("canvas#1", *ref_doc); // empty binding: the doc is already settled
  ref_host.request_resize("canvas#1", k_w, k_h);
  settle(ref_host);
  std::uint64_t rs = 0;
  Srgb8Image reference;
  REQUIRE(ref_host.consume("canvas#1", rs, reference));
  CHECK(ace::render::frame_has_content(reference));
  CHECK(frame.pixels == reference.pixels);
}

TEST_CASE("canvas_host: the empty binding leaves a deferred external nested child blank (the "
          "before-state)") {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);

  DeferringAssetSource source;
  arbc::KindBridge load_bridge;
  std::unique_ptr<arbc::Document> doc = load_pending_nested(source, load_bridge, registry);

  // No registry supplied -> the empty DocumentBinding{} -> HostViewport derives NO settle hook.
  CanvasHost host = make_inline_host();
  host.add("canvas#1", *doc); // registry defaults null
  host.request_resize("canvas#1", k_w, k_h);

  REQUIRE(source.fire_all() == 1); // the bytes arrive, but nothing ever settles them
  settle(host);

  CHECK(doc->pending_external_loads() == 1);       // never drained — the child never installs
  CHECK(host.published_sequence("canvas#1") == 0); // blank -> withheld, reproducing the debt
  std::uint64_t s = 0;
  Srgb8Image frame;
  CHECK_FALSE(host.consume("canvas#1", s, frame)); // nothing was ever published
}

// --- editor.cameras.look_through: a look-through canvas is data-race-clean ---------------
//
// The threading acceptance (docs §9 ASan/TSan lane): two entries over one document, canvas#2
// looking through a shot. The UI thread streams cameras.manip-style frame edits to that shot
// through apply_edit (on the writer thread) while, each iteration, re-reading scene::cameras(doc)
// via the lock-free pin() snapshot, re-deriving the look-through viewport, and submitting the
// settled request_resize + request_camera channels — the exact per-frame path draw_content walks.
// This leaf adds NO new shared mutable state (the selection is UI-thread-only), so the per-frame
// pin() derivation and the settled channels coexist race-free with the concurrent writer: the
// render read is lock-free (arbc v0.2.0 COW content bindings, #10/#11; D-look_through-7).
// Residual Mesa leaks via lsan.supp.

TEST_CASE("canvas_host: a look-through canvas streams frame edits clean against the render read "
          "(look_through TSan anchor)") {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);

  auto doc = build_raster_doc(); // a bounded raster square over a solid bg (a root composition)
  // Seed the shot in the root composition on THIS (writer) thread, before the render loop.
  const arbc::ObjectId cam_id = ace::scene::add_camera(
      *doc, registry, "hero", ace::scene::Resolution{32, 32}, arbc::Affine::translation(8.0, 8.0));
  REQUIRE(cam_id.valid());

  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  ace::platform::NativeThreads threads;
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *doc, &registry); // free
  host.request_resize("canvas#1", k_w, k_h);
  host.add("canvas#2", *doc, &registry); // looks through the shot (submitted in the loop)
  host.request_resize("canvas#2", k_w, k_h);
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 1 && host.published_sequence("canvas#2") >= 1;
  }));

  // Stream frame edits to the shot; each round re-derives + re-submits canvas#2's look-through.
  constexpr int k_iters = 48;
  for (int i = 0; i < k_iters; ++i) {
    // A cameras.manip-style frame edit through apply_edit (on the writer thread; the render
    // read is lock-free via COW): nudge the shot's binding-layer transform.
    host.apply_edit([&] {
      const std::vector<ace::scene::Camera> cams = ace::scene::cameras(*doc);
      if (!cams.empty()) {
        doc->set_layer_transform(cams.front().layer, arbc::Affine::translation(
                                                         8.0 + static_cast<double>(i) * 0.25, 8.0));
      }
    });
    // The per-frame look-through resolve: a lock-free pin() read, then the settled
    // resize + camera submit. A vanished shot would fall back to the pane (fail-safe).
    const std::vector<ace::scene::Camera> cams = ace::scene::cameras(*doc);
    if (!cams.empty()) {
      const ace::interact::LookThrough lt =
          ace::interact::look_through(cams.front().frame, cams.front().resolution.width,
                                      cams.front().resolution.height, k_w, k_h);
      if (lt.out_w > 0 && lt.out_h > 0) {
        host.request_resize("canvas#2", lt.out_w, lt.out_h);
        host.request_camera("canvas#2", lt.camera);
      }
    }
  }

  // Both live entries converge on the streamed revisions off-thread — clean under the sanitizers.
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 2 && host.published_sequence("canvas#2") >= 2;
  }));

  host.stop();
  handle->join();
}

// --- editor.cameras.manip: streamed frame re-crops + resolution edits are data-race-clean ----
//
// The threading acceptance (docs §9 ASan/TSan lane): the UI thread streams BOTH cameras.manip
// mutation channels to a shot — frame re-crops (interact::recrop_frame -> set_layer_transform,
// the binding-layer Affine) AND in-place resolution edits (scene::set_camera_resolution, the
// Content editable state) — through apply_edit (on the writer thread) while the render thread
// re-reads scene::cameras(pin()) each frame and re-derives the look-through viewport for a second
// canvas. This leaf adds NO new shared mutable state (the drag preview is UI-thread-only), so both
// channels coexist race-free with the concurrent render read: the read is lock-free via arbc
// v0.2.0 COW content bindings (#10/#11; D-manip / Constraint 5). Residual Mesa leaks via lsan.supp.
TEST_CASE("canvas_host: streamed manip frame + resolution edits stay clean against the render "
          "read (cameras.manip TSan anchor)") {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);

  auto doc = build_raster_doc();
  const arbc::ObjectId cam_id = ace::scene::add_camera(
      *doc, registry, "hero", ace::scene::Resolution{32, 32}, arbc::Affine::translation(8.0, 8.0));
  REQUIRE(cam_id.valid());

  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  ace::platform::NativeThreads threads;
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *doc, &registry); // free
  host.request_resize("canvas#1", k_w, k_h);
  host.add("canvas#2", *doc, &registry); // looks through the shot (submitted in the loop)
  host.request_resize("canvas#2", k_w, k_h);
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 1 && host.published_sequence("canvas#2") >= 1;
  }));

  constexpr int k_iters = 48;
  for (int i = 0; i < k_iters; ++i) {
    // Two manip channels through apply_edit (on the writer thread; render read lock-free via COW):
    // a frame re-crop (interact math -> set_layer_transform) AND a resolution edit
    // (set_camera_resolution). The re-crop math runs on the UI thread over the pin() frame.
    host.apply_edit([&] {
      const std::vector<ace::scene::Camera> cams = ace::scene::cameras(*doc);
      if (cams.empty()) {
        return;
      }
      const ace::scene::Camera& cam = cams.front();
      const arbc::Affine recropped =
          ace::interact::recrop_frame(cam.frame, cam.resolution.width, cam.resolution.height,
                                      ace::interact::FrameHandle::CornerBottomRight,
                                      arbc::Vec2{20.0 + static_cast<double>(i) * 0.25, 20.0});
      doc->set_layer_transform(cam.layer, recropped);
      const int side = 24 + (i % 8);
      ace::scene::set_camera_resolution(*doc, registry, cam.id, ace::scene::Resolution{side, side});
    });
    // The per-frame look-through resolve: a lock-free pin() read, then the settled submit.
    const std::vector<ace::scene::Camera> cams = ace::scene::cameras(*doc);
    if (!cams.empty()) {
      const ace::interact::LookThrough lt =
          ace::interact::look_through(cams.front().frame, cams.front().resolution.width,
                                      cams.front().resolution.height, k_w, k_h);
      if (lt.out_w > 0 && lt.out_h > 0) {
        host.request_resize("canvas#2", lt.out_w, lt.out_h);
        host.request_camera("canvas#2", lt.camera);
      }
    }
  }

  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 2 && host.published_sequence("canvas#2") >= 2;
  }));

  host.stop();
  handle->join();
}

TEST_CASE("canvas_host: a ref-free document renders byte-identically through the wired and empty "
          "binding (settle is a no-op)") {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  const ProbeDocument probe = build_probe_document();

  // One host, one document, two entries: canvas#1 wired (registry supplied), canvas#2 empty.
  // On a document with no external references the settle hook is a no-op queue check, so both
  // must composite the identical bytes (Constraint 4) — and both equal the offline reference.
  CanvasHost host = make_inline_host();
  host.add("canvas#1", *probe.document, &registry);
  host.request_resize("canvas#1", k_w, k_h);
  host.add("canvas#2", *probe.document); // empty binding
  host.request_resize("canvas#2", k_w, k_h);
  settle(host);

  std::uint64_t s1 = 0;
  Srgb8Image wired;
  REQUIRE(host.consume("canvas#1", s1, wired));
  std::uint64_t s2 = 0;
  Srgb8Image empty;
  REQUIRE(host.consume("canvas#2", s2, empty));

  CHECK(wired.pixels == empty.pixels);
  const Srgb8Image offline = ace::render::render_document_srgb8(*probe.document, k_w, k_h);
  CHECK(wired.pixels == offline.pixels);
}

TEST_CASE("canvas_host: a stream of UI-thread cell inserts runs clean against the render read") {
  // editor.cells.model Acceptance (Threading): the SHIPPED insert path — a registry
  // factory call, `Document::add_content` (which binds a Content vtable and
  // self-commits), then one transact adding + attaching the placing layer — streamed
  // through `apply_edit` on the REAL interactive pool against a live rendering canvas.
  // Structurally heavier than `damage()`'s solid add (a raster mints a tile pyramid),
  // so it widens the overlap window the D-edit_render_sync-3 anchor above opens. No
  // new lock and no new thread: A4.1 writer-identity holds because every edit runs on
  // the caller, and the render walk reads the COW-published bindings lock-free.
  ProbeDocument probe = build_probe_document();
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
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

  constexpr int k_inserts = 24;
  for (int i = 0; i < k_inserts; ++i) {
    host.apply_edit([&] {
      // Alternate an opaque solid (a full-frame repaint) with a small raster (a tile
      // pyramid mint) so both the cheap and the allocating insert overlap the read.
      const bool solid = (i % 2) == 0;
      const auto added = ace::scene::add_cell(
          *probe.document, registry, solid ? "org.arbc.solid" : "org.arbc.raster",
          solid ? "0.1,0.2,0.3,1" : "24x24",
          arbc::Affine::translation(static_cast<double>(i), static_cast<double>(i)));
      REQUIRE(added.has_value());
    });
  }
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 2 && host.published_sequence("canvas#2") >= 2;
  }));

  host.stop();
  handle->join();

  // Every insert landed exactly once: the probe's own untokened solid plus k_inserts.
  CHECK(ace::scene::cells(*probe.document, registry).size() ==
        static_cast<std::size_t>(k_inserts) + 1);
}

TEST_CASE("canvas_host: UI-thread pick_targets reads run clean against the live render walk") {
  // editor.cells.selection Acceptance (Threading): selection mutates NOTHING, so this leaf adds
  // no writer path — but `interact::pick_targets` now calls a `Content` VIRTUAL
  // (`arbc::Content::bounds()`, via `scene::cells`' pinned walk, D-selection-11) on the UI
  // thread against a document a render thread is walking. The lock-free `pin()` seam covers the
  // record walk; whether it also covers the `bounds()` call is the specific thing this case
  // pins. Repeated pick_targets + pick on the UI thread against a LIVE rendering real-pool host
  // (the D-edit_render_sync-3 anchor) while cell inserts stream through `apply_edit`. No new
  // lock and no new thread; a TSan/ASan report here is the tripwire the refinement's parking-lot
  // item names (the fix would be a libarbc-side contract statement, not an editor-side lock).
  ProbeDocument probe = build_probe_document();
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  ace::platform::NativeThreads threads;
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *probe.document, &registry);
  host.request_resize("canvas#1", k_w, k_h);
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 1; }));

  // A camera in the mix so the merge half of the adapter (cells + cameras) is walked too.
  host.apply_edit([&] {
    REQUIRE(ace::scene::add_camera(*probe.document, registry, "Hero",
                                   ace::scene::Resolution{16, 16},
                                   arbc::Affine::translation(4.0, 4.0))
                .valid());
  });

  constexpr int k_inserts = 24;
  std::size_t last_targets = 0;
  for (int i = 0; i < k_inserts; ++i) {
    host.apply_edit([&] {
      const bool solid = (i % 2) == 0;
      const auto added = ace::scene::add_cell(
          *probe.document, registry, solid ? "org.arbc.solid" : "org.arbc.raster",
          solid ? "0.1,0.2,0.3,1" : "24x24",
          arbc::Affine::translation(static_cast<double>(i), static_cast<double>(i)));
      REQUIRE(added.has_value());
    });
    // The UI-thread read the canvas performs EVERY frame: assemble the stack (which resolves
    // each Content and calls bounds()) and hit-test it, straight against the live render walk.
    for (int r = 0; r < 4; ++r) {
      const std::vector<ace::interact::PickTarget> targets =
          ace::interact::pick_targets(*probe.document, registry);
      last_targets = targets.size();
      const ace::interact::PickHit hit =
          ace::interact::pick(targets, arbc::Vec2{8.0, 8.0}, 2.0, 3.0);
      REQUIRE(hit.hit); // the probe's own unbounded solid is always under the cursor
      REQUIRE(ace::interact::marquee(targets, arbc::Rect{0.0, 0.0, 64.0, 64.0}).size() <=
              targets.size());
    }
  }
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 2; }));

  host.stop();
  handle->join();

  // A stable final target count: the probe's own untokened solid + every insert + the camera.
  CHECK(last_targets == static_cast<std::size_t>(k_inserts) + 2);
  CHECK(ace::interact::pick_targets(*probe.document, registry).size() == last_targets);
}
