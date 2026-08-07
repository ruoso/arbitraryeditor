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
#include <ace/commands/app_state.hpp>
#include <ace/commands/cells.hpp>
#include <ace/commands/color.hpp>
#include <ace/commands/export.hpp>
#include <ace/commands/image_import.hpp>
#include <ace/interact/interact.hpp>
#include <ace/interact/pick.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/threads.hpp>
#include <ace/project/import_asset.hpp>
#include <ace/project/project.hpp>
#include <ace/render/canvas_host.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>
#include <ace/writer/writer_thread.hpp>

#include <arbc/base/geometry.hpp> // arbc::Vec2 (brush dab centers)
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>                 // register_builtin_kinds (org.arbc.nested/.solid)
#include <arbc/compositor/anchored_viewports.hpp> // k_reanchor_scale_threshold (nav anchor_depth)
#include <arbc/contract/registry.hpp>             // arbc::Registry
#include <arbc/kind_nested/nested_content.hpp>
#include <arbc/kind_raster/raster_content.hpp> // DecodedImage / RasterContent (bounded content)
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/media/pixel_traits.hpp> // arbc::WorkingPixel (brush color)
#include <arbc/model/journal.hpp>      // Journal::cursor() (the coalesced-entry count)
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp> // KindBridge / load_document / settle_external_loads
#include <arbc/runtime/interactive.hpp>
#include <arbc/runtime/worker_pool.hpp>
#include <arbc/serialize/load_context.hpp> // arbc::AssetSource (the deferring double's base)

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath> // std::isfinite (final-pixel finiteness)
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
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

// THE edit seam, as the shipped app now binds it (editor.canvas.writer_thread,
// D-writer_thread-11): POST the mutation to the document's one writer thread, block until it has
// run, then wake every live canvas. `CanvasHost::apply_edit` and its writer-priority document
// lease are gone — `render` no longer owns the document write path, and the render walk's read
// takes no lock because it needs none (COW bindings, the accumulator's own mutex, and a `step()`
// that declines to publish off the writer thread).
//
// With an INLINE-degenerate writer (`WriterThread{}`) the closure runs on the caller, which is
// what keeps the deterministic drive_once() fixtures below thread-free and byte-exact
// (D-writer_thread-5 / Constraint 6).
void apply_edit(ace::writer::WriterThread& writer, CanvasHost& host,
                const std::function<void()>& edit) {
  writer.submit_sync(edit);
  host.poke();
}

// Build a document ON the writer thread. The FIRST write binds the document's writer identity for
// its lifetime (`SlotStore::allocate` asserts it in debug builds), so a fixture that builds on the
// main thread and then posts its edits would be TWO identities — the exact bug this leaf removes.
// The shipped bootstrap has the same shape: the writer starts before `open_or_create_app_state`.
template <class Build>
auto build_on_writer(ace::writer::WriterThread& writer, Build build_fn) -> decltype(build_fn()) {
  decltype(build_fn()) built{};
  writer.submit_sync([&] { built = build_fn(); });
  return built;
}

// Read the WRITER-OWNED external-load counters on the writer thread, where libarbc says they
// live: `Document::pending_external_loads()` and `external_loads_auto_settled()` are documented
// writer-thread (only `external_loads_ready()` and `on_writer_thread()` are any-thread), so a test
// that polled them from the main thread would be asserting the very thing this leaf forbids — and
// the hermetic gcc-tsan lane reports exactly that. Posting the read also supplies the
// happens-before edge that makes the returned value meaningful.
struct LoadCounters {
  std::size_t pending = 0;
  std::uint64_t auto_settled = 0;
};

LoadCounters load_counters(ace::writer::WriterThread& writer, const arbc::Document& doc) {
  LoadCounters out;
  writer.submit_sync([&] {
    out.pending = doc.pending_external_loads();
    out.auto_settled = doc.external_loads_auto_settled();
  });
  return out;
}

// Deadline-based pump for the off-thread lifecycle case — holds under a sanitizer build's
// slowdown (no fixed iteration count).
// A temp dir wiped on entry and exit — the export TSan anchor below writes real files
// through the real `NativeFileSystem`, so a short/malformed one is observable.
struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_canvas_host_export_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

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
  ace::writer::WriterThread writer; // inline degenerate: this thread IS the writer identity
  ProbeDocument probe = build_probe_document();
  CanvasHost host = make_inline_host();
  seed_two(host, *probe.document, k_w, k_h);
  REQUIRE(host.published_sequence("canvas#1") == 1);
  REQUIRE(host.published_sequence("canvas#2") == 1);

  // One edit to the SHARED document via apply_edit (runs synchronously here on the writer
  // thread), then a drive fans the resulting damage out to both entries.
  apply_edit(writer, host, [&] { damage(*probe.document, probe.composition); });
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
  ace::writer::WriterThread writer; // inline degenerate: this thread IS the writer identity
  ProbeDocument probe = build_probe_document();
  CanvasHost host = make_inline_host();
  host.add("canvas#1", *probe.document);
  host.request_resize("canvas#1", k_w, k_h);
  host.drive_once();
  REQUIRE(host.published_sequence("canvas#1") == 1); // first content frame published

  // Once content has published (content_published latched) the gate is skipped, so a later
  // damaged frame publishes on the normal frames_issued advance (the sequence stays monotonic).
  apply_edit(writer, host, [&] { damage(*probe.document, probe.composition); });
  host.drive_once();
  CHECK(host.published_sequence("canvas#1") == 2);
}

TEST_CASE("canvas_host: the REAL shared-pool lifecycle runs clean off a spawned render thread") {
  // The document's ONE writer identity, bound the way the shipped bootstrap binds it
  // (D-writer_thread-6): the writer starts BEFORE the document exists, the document is built
  // ON it (the first write binds the identity for life), and it is stopped only after the host
  // — whose entry/router teardown posts WRITER-THREAD-ONLY work — has been destroyed. The
  // declaration order below IS that contract: `writer` outlives `host`.
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  ProbeDocument probe = build_on_writer(writer, [] { return build_probe_document(); });
  // The shipped path: a real interactive WorkerPool (worker threads) + a bounded budget —
  // the escalated ASan/TSan target (N renderers ‖ one pool ‖ one render thread ‖ UI
  // writer+poke ‖ HousekeepingThread ‖ N double-buffer handoffs).
  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);

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
  apply_edit(writer, host, [&] { damage(*probe.document, probe.composition); });
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
  // The document's ONE writer identity, bound the way the shipped bootstrap binds it
  // (D-writer_thread-6): the writer starts BEFORE the document exists, the document is built
  // ON it (the first write binds the identity for life), and it is stopped only after the host
  // — whose entry/router teardown posts WRITER-THREAD-ONLY work — has been destroyed. The
  // declaration order below IS that contract: `writer` outlives `host`.
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  ProbeDocument probe = build_on_writer(writer, [] { return build_probe_document(); });
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
  host.set_writer(&writer);
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
    apply_edit(writer, host, [&] { damage(*probe.document, probe.composition); });
  }
  // Both live readers converge on the streamed revisions off-thread — the fan-out reached them.
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 2 && host.published_sequence("canvas#2") >= 2;
  }));

  // Clean stop -> join; entries tear down before the shared pool (Constraint 5).
  host.stop();
  handle->join();
}

TEST_CASE("canvas_host: streamed isolation-scope toggles run clean against the render read "
          "(isolation_scope TSan anchor)") {
  // editor.canvas.isolation_scope: the new per-canvas scope channel coexists with the settled
  // camera/resize channels under the ONE host-lock discipline — it adds a value slot, not a new
  // synchronization primitive (Constraint 8). The UI thread streams request_scope(entered) <->
  // request_scope(nullopt) toggles interleaved with request_camera and apply_edit(damage) while the
  // render thread drive_once's BOTH entries and each convert recomputes the focus quad from the
  // lock-free pin() snapshot (for_each_layer_in + resolve + bounds) — so a scope read genuinely
  // overlaps the writes in wall-clock time. Must be data-race-clean.
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  ProbeDocument probe = build_on_writer(writer, [] { return build_probe_document(); });

  // A nested child under root so a scope of `child` drives the full focus-quad DFS under
  // contention, not just the fail-safe early-out.
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  arbc::ObjectId child;
  writer.submit_sync([&] {
    child = probe.document->add_composition(32.0, 32.0);
    const arbc::ObjectId content = probe.document->add_content(std::make_shared<arbc::SolidContent>(
        arbc::Rgba{0.7F, 0.0F, 0.0F, 1.0F}, arbc::Rect{0.0, 0.0, 32.0, 32.0}));
    const arbc::ObjectId layer = probe.document->add_layer(content, arbc::Affine::identity());
    probe.document->attach_layer(child, layer);
    (void)ace::scene::add_cell(*probe.document, registry, "org.arbc.nested",
                               std::to_string(child.value), arbc::Affine::translation(16.0, 16.0));
  });

  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *probe.document, &registry);
  host.request_resize("canvas#1", k_w, k_h);
  host.add("canvas#2", *probe.document, &registry);
  host.request_resize("canvas#2", k_w, k_h);
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 1 && host.published_sequence("canvas#2") >= 1;
  }));

  // Stream the toggles: many rounds so TSan gets a wide overlap window against the looping read.
  constexpr int k_rounds = 64;
  for (int i = 0; i < k_rounds; ++i) {
    const std::optional<arbc::ObjectId> scope =
        (i % 2 == 0) ? std::optional<arbc::ObjectId>{child} : std::optional<arbc::ObjectId>{};
    host.request_scope("canvas#1", scope);
    host.request_scope("canvas#2", scope);
    host.request_camera("canvas#1",
                        arbc::Affine{1.0, 0.0, 0.0, 1.0, static_cast<double>(-(i % 4)), 0.0});
    apply_edit(writer, host, [&] { damage(*probe.document, probe.composition); });
  }
  // Both live readers converge on the streamed revisions off-thread.
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 2 && host.published_sequence("canvas#2") >= 2;
  }));

  host.stop();
  handle->join();
}

TEST_CASE("canvas_host: streamed SCOPED inserts/deletes run clean against the render read "
          "(cells.scoped_edit TSan anchor)") {
  // editor.cells.scoped_edit Acceptance (Threading / Constraint 3): a scoped `add_cell` /
  // `remove_cells` — the entered composition captured BY VALUE into the writer-thread closure —
  // streamed through `apply_edit` on the real interactive pool while the render thread walks the
  // SAME document over the lock-free pin() seam AND consumes the per-canvas `set_scope` channel the
  // isolation_scope leaf feeds (request_scope). The edit's captured scope id is a plain
  // `std::optional<arbc::ObjectId>` value — it shares no mutable state with the render-thread scope
  // generation, so a scoped edit overlapping a scope toggle is data-race-clean. No new lock, no new
  // thread, no new suppression: the scope is a value and the transaction machinery is the
  // already-covered single-writer seam.
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  ProbeDocument probe = build_on_writer(writer, [] { return build_probe_document(); });

  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  arbc::ObjectId child;
  writer.submit_sync([&] {
    child = probe.document->add_composition(32.0, 32.0);
    const arbc::ObjectId content = probe.document->add_content(std::make_shared<arbc::SolidContent>(
        arbc::Rgba{0.7F, 0.0F, 0.0F, 1.0F}, arbc::Rect{0.0, 0.0, 32.0, 32.0}));
    const arbc::ObjectId layer = probe.document->add_layer(content, arbc::Affine::identity());
    probe.document->attach_layer(child, layer);
    (void)ace::scene::add_cell(*probe.document, registry, "org.arbc.nested",
                               std::to_string(child.value), arbc::Affine::translation(16.0, 16.0));
  });

  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *probe.document, &registry);
  host.request_resize("canvas#1", k_w, k_h);
  host.add("canvas#2", *probe.document, &registry);
  host.request_resize("canvas#2", k_w, k_h);
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 1 && host.published_sequence("canvas#2") >= 1;
  }));

  // The scope value the closures capture BY VALUE — never a live AppState field read on the writer
  // thread (Constraint 3). Both canvases run entered on the render side too, so the scoped edit
  // overlaps a live focus-quad recompute off the same pin().
  const std::optional<arbc::ObjectId> entered{child};
  host.request_scope("canvas#1", entered);
  host.request_scope("canvas#2", entered);

  constexpr int k_rounds = 32;
  arbc::ObjectId minted;
  for (int i = 0; i < k_rounds; ++i) {
    // Insert a scoped cell (scope captured by value), then delete exactly it with a scoped removal
    // — both verbs threaded through the entered scope, overlapping the render read. `apply_edit`
    // (submit_sync) runs each closure to completion on the writer thread, so `minted` is set before
    // the delete reads it.
    apply_edit(writer, host, [&, entered] {
      const auto added =
          ace::scene::add_cell(*probe.document, registry, "org.arbc.solid", "0,0,1,1,0,0,16,16",
                               arbc::Affine::identity(), entered);
      REQUIRE(added.has_value());
      minted = *added;
    });
    apply_edit(writer, host, [&, entered] {
      arbc::ObjectId layer;
      for (const ace::scene::Cell& cell : ace::scene::cells(*probe.document, registry, child)) {
        if (cell.id == minted) {
          layer = cell.layer;
        }
      }
      const std::vector<ace::scene::CellRemoval> batch{ace::scene::CellRemoval{minted, layer}};
      (void)ace::scene::remove_cells(*probe.document, batch, entered);
    });
  }
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 2 && host.published_sequence("canvas#2") >= 2;
  }));

  host.stop();
  handle->join();

  // The scoped delete removed exactly what the scoped insert added each round: the child holds only
  // its original red solid, and the root holds only the probe solid + the nested wrapper.
  CHECK(ace::scene::cells(*probe.document, registry, child).size() == 1);
}

TEST_CASE("canvas_host: a streamed borrowed-image import runs clean against the render read "
          "(import_image TSan anchor)") {
  // editor.import.image Acceptance (Threading / Constraint / A13): the import mutation rides
  // `apply_edit` -> the document's ONE writer thread, and the new `org.arbc.image` cell AND its
  // DECODE publish through the pin() seam — so an import overlapping the off-thread render read of
  // the SAME Document is data-race-clean. No new thread and no new lock: the decode's global
  // pyramid cache carries its own mutex, and the content binding publishes copy-on-write like
  // every other add_cell. The render walk here genuinely composites the borrowed image on the
  // interactive worker pool while the UI thread streams more imports.
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  ProbeDocument probe = build_on_writer(writer, [] { return build_probe_document(); });

  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::commands::register_image_kind(registry);

  // The borrowed-image config, assembled once on the test thread (the file read is the import
  // verb's turf, never the writer's): the fixture bytes embedded so each minted cell decodes.
  const std::filesystem::path fixture = std::filesystem::path(ACE_FIXTURE_DIR) / "photo_12x8.ppm";
  std::ifstream in(fixture, std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  REQUIRE(!bytes.empty());
  const std::string uri = std::filesystem::absolute(fixture).string();
  const std::string config = ace::commands::image_config(uri, uri, bytes);

  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *probe.document, &registry);
  host.request_resize("canvas#1", k_w, k_h);
  host.add("canvas#2", *probe.document, &registry);
  host.request_resize("canvas#2", k_w, k_h);
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 1 && host.published_sequence("canvas#2") >= 1;
  }));

  // Stream borrowed-image imports while both canvases render the same document off-thread: each
  // `add_cell` mints a new cell + decodes it, published through pin() (Constraint 4).
  constexpr int k_imports = 16;
  for (int i = 0; i < k_imports; ++i) {
    apply_edit(writer, host, [&, i] {
      const auto added = ace::scene::add_cell(
          *probe.document, registry, ace::commands::image_kind_id, config,
          arbc::Affine::translation(2.0 * static_cast<double>(i), 3.0 * static_cast<double>(i)));
      REQUIRE(added.has_value());
    });
  }
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 2 && host.published_sequence("canvas#2") >= 2;
  }));

  host.stop();
  handle->join();

  // Every import landed a live borrowed-image cell (the probe's own solid is the +1).
  std::size_t images = 0;
  for (const ace::scene::Cell& cell : ace::scene::cells(*probe.document, registry)) {
    if (cell.kind_id == std::string(ace::commands::image_kind_id)) {
      ++images;
    }
  }
  CHECK(images == static_cast<std::size_t>(k_imports));
}

TEST_CASE("canvas_host: a streamed nested-reference import runs clean against the render read "
          "(import_nested TSan anchor)") {
  // editor.import.nested Acceptance (Threading / A31 / A13): the nested-reference insert rides
  // `apply_edit` -> the document's ONE writer thread, and the freshly minted `org.arbc.nested` cell
  // is UNRESOLVED (`child == ObjectId{}`) — it never traverses the codec, so it renders the doc-05
  // placeholder with NO external-load traffic (`external_loads_ready() == 0`, D-nested-3). An
  // insert overlapping the off-thread render read of the SAME Document is therefore
  // data-race-clean: one writer-thread transaction, a placeholder composite, no new synchronization
  // (the edit<->render handoff is `edit_render_sync`'s charter).
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  ProbeDocument probe = build_on_writer(writer, [] { return build_probe_document(); });

  arbc::Registry registry;
  arbc::register_builtin_kinds(
      registry); // `org.arbc.nested` is a built-in — no plugin link (D-nested-6)

  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *probe.document, &registry);
  host.request_resize("canvas#1", k_w, k_h);
  host.add("canvas#2", *probe.document, &registry);
  host.request_resize("canvas#2", k_w, k_h);
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 1 && host.published_sequence("canvas#2") >= 1;
  }));

  // Stream nested-reference inserts while both canvases render the same document off-thread: each
  // `add_nested_reference` mints an UNRESOLVED external-ref cell, published through pin().
  constexpr int k_imports = 16;
  for (int i = 0; i < k_imports; ++i) {
    apply_edit(writer, host, [&, i] {
      const auto added = ace::scene::add_nested_reference(
          *probe.document, registry, "/abs/streamed/child.arbc",
          arbc::Affine::translation(2.0 * static_cast<double>(i), 3.0 * static_cast<double>(i)));
      REQUIRE(added.has_value());
    });
  }
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 2 && host.published_sequence("canvas#2") >= 2;
  }));

  host.stop();
  handle->join();

  // Every insert landed a live nested cell (the probe's own solid is the +1), and the unresolved
  // references generated ZERO external-load traffic — the placeholder path, exactly as scoped.
  std::size_t nested = 0;
  for (const ace::scene::Cell& cell : ace::scene::cells(*probe.document, registry)) {
    if (cell.kind_id == std::string(arbc::NestedContent::kind_id)) {
      ++nested;
    }
  }
  CHECK(nested == static_cast<std::size_t>(k_imports));
  CHECK(load_counters(writer, *probe.document).pending == 0);
  CHECK(probe.document->external_loads_ready() == 0);
}

TEST_CASE("canvas_host: a streamed owned-image paste runs clean against the render read "
          "(import_paste TSan anchor)") {
  // editor.import.paste Acceptance (Threading / A13/A30): a paste's mint + insert + decode ride the
  // same seams a borrowed import does — the MINT (a content-addressed blob write to `assets/`) runs
  // on the UI thread and touches no Document, and the OWNED `org.arbc.image` cell + its decode
  // publish through the pin() seam on the writer thread. So a paste overlapping the off-thread
  // render read of the SAME Document is data-race-clean. No new thread, no new lock: the only thing
  // that differs from the import anchor is the URI is project-relative owned (classify -> owned).
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  ProbeDocument probe = build_on_writer(writer, [] { return build_probe_document(); });

  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::commands::register_image_kind(registry);

  // MINT the clipboard bytes into a scratch project's `assets/` once on the test (UI) thread — the
  // paste verb's turf, never the writer's — then assemble the OWNED image config (bytes embedded so
  // each inserted cell decodes). The mint is off-Document, so it is not the race surface; the
  // insert stream below is.
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "ace_paste_tsan";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  const ace::project::ProjectLayout layout = ace::project::project_layout(root);
  const std::filesystem::path fixture = std::filesystem::path(ACE_FIXTURE_DIR) / "photo_12x8.ppm";
  std::ifstream in(fixture, std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  REQUIRE(!bytes.empty());
  const ace::project::OwnedAsset owned = ace::project::mint_owned_asset(fs, layout, bytes, "ppm");
  CHECK(owned.uri.rfind("assets/", 0) == 0);
  const std::string config = ace::commands::image_config(owned.uri, owned.uri, owned.bytes);

  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *probe.document, &registry);
  host.request_resize("canvas#1", k_w, k_h);
  host.add("canvas#2", *probe.document, &registry);
  host.request_resize("canvas#2", k_w, k_h);
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 1 && host.published_sequence("canvas#2") >= 1;
  }));

  // Stream owned-image pastes while both canvases render the same document off-thread.
  constexpr int k_pastes = 16;
  for (int i = 0; i < k_pastes; ++i) {
    apply_edit(writer, host, [&, i] {
      const auto added = ace::scene::add_cell(
          *probe.document, registry, ace::commands::image_kind_id, config,
          arbc::Affine::translation(2.0 * static_cast<double>(i), 3.0 * static_cast<double>(i)));
      REQUIRE(added.has_value());
    });
  }
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 2 && host.published_sequence("canvas#2") >= 2;
  }));

  host.stop();
  handle->join();

  // Every paste landed a live OWNED image cell, classified owned (not borrowed) off the URI shape.
  std::size_t images = 0;
  for (const ace::scene::Cell& cell : ace::scene::cells(*probe.document, registry)) {
    if (cell.kind_id == std::string(ace::commands::image_kind_id)) {
      ++images;
      CHECK_FALSE(cell.detail.borrowed);
    }
  }
  CHECK(images == static_cast<std::size_t>(k_pastes));
  std::filesystem::remove_all(root, ec);
}

TEST_CASE("canvas_host: a stream of UI-thread content REMOVALS runs clean against the render read "
          "(cells.remove TSan anchor)") {
  // The document's ONE writer identity, bound the way the shipped bootstrap binds it
  // (D-writer_thread-6): the writer starts BEFORE the document exists, the document is built
  // ON it (the first write binds the identity for life), and it is stopped only after the host
  // — whose entry/router teardown posts WRITER-THREAD-ONLY work — has been destroyed. The
  // declaration order below IS that contract: `writer` outlives `host`.
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  ProbeDocument probe = build_on_writer(writer, [] { return build_probe_document(); });
  // editor.cells.remove's one genuinely NEW threading surface: every prior edit only ADDED
  // records, so this is the first time a `ContentRecord` is ERASED while the render thread
  // walks the same document over the lock-free pin() seam. `Document::remove_content` runs
  // its three teardowns inside ONE transaction, so an observer sees the content present or
  // fully gone, never a layer naming an erased content; and Decision 1 keeps the content's
  // binding row alive while the journal holds the removal, so the render walk's
  // `resolve()` of a pinned older generation stays valid. Same real-pool + spawned-render-
  // thread rig as the insert anchor above, so the erase genuinely overlaps the read in
  // wall-clock time. Runs in the shipped ASan and TSan lanes; no new lane, no suppression.
  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *probe.document);
  host.request_resize("canvas#1", k_w, k_h);
  host.add("canvas#2", *probe.document);
  host.request_resize("canvas#2", k_w, k_h);
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 1 && host.published_sequence("canvas#2") >= 1;
  }));

  // Attach then erase, many rounds, each half its own apply_edit — a wide overlap window.
  constexpr int k_rounds = 32;
  for (int i = 0; i < k_rounds; ++i) {
    arbc::ObjectId content;
    arbc::ObjectId layer;
    apply_edit(writer, host, [&] {
      content = probe.document->add_content(
          std::make_shared<arbc::SolidContent>(arbc::Rgba{0.9F, 0.1F, 0.1F, 1.0F}));
      layer = probe.document->add_layer(content, arbc::Affine::identity());
      probe.document->attach_layer(probe.composition, layer);
    });
    apply_edit(writer, host,
               [&] { probe.document->remove_content(content, probe.composition, layer); });
  }
  // The frames keep arriving: both live readers advanced past the removals off-thread.
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 2 && host.published_sequence("canvas#2") >= 2;
  }));

  host.stop();
  handle->join();
}

TEST_CASE("canvas_host: a stream of UI-thread cell TRANSFORM commits runs clean against the render "
          "+ gizmo read (cells.gizmo TSan anchor)") {
  // The document's ONE writer identity, bound the shipped way (D-writer_thread-6): the writer
  // starts BEFORE the document, the document is built ON it, and it is stopped only after the host
  // is destroyed. The declaration order below IS that contract: `writer` outlives `host`.
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  ProbeDocument probe = build_on_writer(writer, [] { return build_probe_document(); });
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);

  // editor.cells.gizmo's new threading surface (Constraint 11): the gizmo commit is a REAL
  // writer-thread mutation (`set_layer_transform`, unlike selection's read-only gestures), and the
  // gizmo ASSEMBLY (`pick_targets`) is a lock-free `pin()` read run every UI frame. This drives a
  // rapid transform-gesture stream through apply_edit on the writer thread while the render thread
  // loops drive_once AND the UI thread re-reads pick_targets, so both reads genuinely overlap the
  // writes in wall-clock time. No new lock, no new thread — the writer-identity + pin() seam covers
  // it, which is what this anchor pins under ASan/TSan.
  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *probe.document);
  host.request_resize("canvas#1", k_w, k_h);
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 1; }));

  // Stream the transform commits: many rounds, each a set_layer_transform of the probe's layer to a
  // fresh (always-invertible) placement, with a pick_targets read interleaved each round.
  constexpr int k_gestures = 64;
  arbc::Affine last = arbc::Affine::identity();
  for (int i = 0; i < k_gestures; ++i) {
    const double s = 1.0 + static_cast<double>(i % 8) * 0.25; // a varying, non-degenerate scale
    last = arbc::Affine{s, 0.0, 0.0, s, static_cast<double>(i), static_cast<double>(-i)};
    apply_edit(writer, host, [&] { probe.document->set_layer_transform(probe.layer, last); });
    const std::vector<ace::interact::PickTarget> targets =
        ace::interact::pick_targets(*probe.document, registry);
    (void)targets; // the gizmo's per-frame assembly read, overlapping the writer + render reads
  }
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 2; }));

  // The final committed placement is what the gizmo reads back — stable and finite.
  bool found = false;
  for (const ace::interact::PickTarget& tgt :
       ace::interact::pick_targets(*probe.document, registry)) {
    if (tgt.layer == probe.layer) {
      found = true;
      CHECK(tgt.placement == last);
    }
  }
  CHECK(found);

  host.stop();
  handle->join();
}

TEST_CASE("canvas_host: a stream of UI-thread GROUP transform batches runs clean against the "
          "render + gizmo read (cells.group_transform TSan anchor)") {
  // Same writer-outlives-host contract as the cells.gizmo anchor above; the difference this leaf
  // pins is that the group commit is ONE `transform_cells_command` transaction spanning N layers
  // (D-group_transform-1), so the atomic multi-layer publish overlaps the render walk + the
  // per-frame `pick_targets` read on the real interactive pool. No new lock, no new thread — the
  // one batch transaction rides the same writer-identity + pin() seam.
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  ProbeDocument probe = build_on_writer(writer, [] { return build_probe_document(); });
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);

  // Two placed rasters (plus the probe's own cell) give the group batch ≥2 layers to rewrite in one
  // transaction. Seed on the writer; report success back to the main thread for the assertion.
  std::vector<arbc::ObjectId> layers;
  bool seeded = false;
  writer.submit_sync([&] {
    bool ok = true;
    for (int i = 0; i < 2; ++i) {
      const auto added = ace::scene::add_cell(*probe.document, registry, "org.arbc.raster", "16x16",
                                              arbc::Affine::translation(8.0 + 20.0 * i, 8.0));
      ok = ok && added.has_value();
    }
    for (const ace::scene::Cell& cell : ace::scene::cells(*probe.document, registry)) {
      layers.push_back(cell.layer);
    }
    seeded = ok;
  });
  REQUIRE(seeded);
  REQUIRE(layers.size() >= 2);

  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *probe.document);
  host.request_resize("canvas#1", k_w, k_h);
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 1; }));

  // Stream the group batches: each round rewrites EVERY layer to a fresh (always-invertible)
  // placement in one transaction, with a pick_targets read interleaved.
  constexpr int k_gestures = 64;
  std::vector<ace::commands::LayerTransform> last;
  for (int i = 0; i < k_gestures; ++i) {
    const double s = 1.0 + static_cast<double>(i % 8) * 0.25; // a varying, non-degenerate scale
    std::vector<ace::commands::LayerTransform> batch;
    for (std::size_t j = 0; j < layers.size(); ++j) {
      batch.push_back(ace::commands::LayerTransform{
          layers[j], arbc::Affine{s, 0.0, 0.0, s, static_cast<double>(i) + static_cast<double>(j),
                                  static_cast<double>(-i)}});
    }
    last = batch;
    apply_edit(writer, host,
               [&] { ace::commands::transform_cells_command(batch).apply(*probe.document); });
    const std::vector<ace::interact::PickTarget> targets =
        ace::interact::pick_targets(*probe.document, registry);
    (void)targets; // the gizmo's per-frame assembly read, overlapping the writer + render reads
  }
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 2; }));

  // Every layer carries its last batched placement — the whole group swapped, none partial.
  const std::vector<ace::interact::PickTarget> final_targets =
      ace::interact::pick_targets(*probe.document, registry);
  for (const ace::commands::LayerTransform& lt : last) {
    bool found = false;
    for (const ace::interact::PickTarget& tgt : final_targets) {
      if (tgt.layer == lt.layer) {
        found = true;
        CHECK(tgt.placement == lt.placement);
      }
    }
    CHECK(found);
  }

  host.stop();
  handle->join();
}

TEST_CASE("canvas_host: a stream of UI-thread OVERVIEW gizmo transform commits runs clean against "
          "the render + scoped pick read (panels.overview_gizmo TSan anchor)") {
  // The overview gizmo commits the SAME `transform_cells_command` funnel as the canvas gizmo/group
  // (D-overview_gizmo-2), driven from the UI thread with a scoped `pick_targets(document, registry,
  // entered)` read interleaved — exactly the overview's per-frame assembly. This anchor repeats
  // both a single-cell (1-layer) and a group (N-layer) commit against the real interactive pool to
  // prove the placement path adds no new cross-thread surface (Constraint 10): the gizmo session
  // state is UI-thread-only, so only the shipped writer-identity + pin() seam is exercised.
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  ProbeDocument probe = build_on_writer(writer, [] { return build_probe_document(); });
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);

  std::vector<arbc::ObjectId> layers;
  bool seeded = false;
  writer.submit_sync([&] {
    bool ok = true;
    for (int i = 0; i < 2; ++i) {
      const auto added = ace::scene::add_cell(*probe.document, registry, "org.arbc.raster", "16x16",
                                              arbc::Affine::translation(8.0 + 20.0 * i, 8.0));
      ok = ok && added.has_value();
    }
    for (const ace::scene::Cell& cell : ace::scene::cells(*probe.document, registry)) {
      layers.push_back(cell.layer);
    }
    seeded = ok;
  });
  REQUIRE(seeded);
  REQUIRE(layers.size() >= 2);

  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *probe.document);
  host.request_resize("canvas#1", k_w, k_h);
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 1; }));

  // Alternate a single-cell gizmo commit (one layer) and a group commit (all layers), each a fresh
  // always-invertible placement, with the scoped pick read interleaved as the overview draws it.
  constexpr int k_gestures = 64;
  std::vector<ace::commands::LayerTransform> last;
  for (int i = 0; i < k_gestures; ++i) {
    const double s = 1.0 + static_cast<double>(i % 8) * 0.25; // a varying, non-degenerate scale
    std::vector<ace::commands::LayerTransform> batch;
    if (i % 2 == 0) {
      batch.push_back(ace::commands::LayerTransform{
          layers[static_cast<std::size_t>(i) % layers.size()],
          arbc::Affine{s, 0.0, 0.0, s, static_cast<double>(i), static_cast<double>(-i)}});
    } else {
      for (std::size_t j = 0; j < layers.size(); ++j) {
        batch.push_back(ace::commands::LayerTransform{
            layers[j], arbc::Affine{s, 0.0, 0.0, s, static_cast<double>(i) + static_cast<double>(j),
                                    static_cast<double>(-i)}});
      }
    }
    last = batch;
    apply_edit(writer, host,
               [&] { ace::commands::transform_cells_command(batch).apply(*probe.document); });
    const std::vector<ace::interact::PickTarget> targets =
        ace::interact::pick_targets(*probe.document, registry, std::nullopt);
    (void)targets; // the overview's per-frame scoped assembly read, overlapping writer + render
  }
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 2; }));

  // Every layer named in the LAST batch carries that placement — the final commit fully landed.
  const std::vector<ace::interact::PickTarget> final_targets =
      ace::interact::pick_targets(*probe.document, registry, std::nullopt);
  for (const ace::commands::LayerTransform& lt : last) {
    bool found = false;
    for (const ace::interact::PickTarget& tgt : final_targets) {
      if (tgt.layer == lt.layer) {
        found = true;
        CHECK(tgt.placement == lt.placement);
      }
    }
    CHECK(found);
  }

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

TEST_CASE("canvas_host: a scope change republishes the still frame; a no-op scope does not "
          "(editor.canvas.isolation_scope)") {
  // The scope channel bumps the renderer's frame version on a genuine value change, so the still
  // scene re-publishes the newly (un)dimmed frame; a value-identical submit is free (set_scope
  // early return). Deterministic inline pool so drive_once settles in one pass and the sequence is
  // exact.
  auto doc = build_raster_doc();
  CanvasHost host = make_inline_host();
  host.add("canvas#1", *doc);
  host.request_resize("canvas#1", k_w, k_h);
  settle(host);
  const std::uint64_t seq0 = host.published_sequence("canvas#1");
  REQUIRE(seq0 >= 1);

  // nullopt == the initial scope: set_scope early-returns, nothing republishes.
  host.request_scope("canvas#1", std::nullopt);
  settle(host);
  CHECK(host.published_sequence("canvas#1") == seq0);

  // A real change (nullopt -> some id) bumps the scope generation, so the still scene republishes
  // even though no new arbc frame was issued (the mechanism a pure scope toggle relies on).
  host.request_scope("canvas#1", arbc::ObjectId{1});
  settle(host);
  CHECK(host.published_sequence("canvas#1") > seq0);
  const std::uint64_t seq1 = host.published_sequence("canvas#1");

  // Re-sending the SAME value is a no-op (set_scope early return): no further republish.
  host.request_scope("canvas#1", arbc::ObjectId{1});
  settle(host);
  CHECK(host.published_sequence("canvas#1") == seq1);
}

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

TEST_CASE("canvas_host: a resize/camera for a not-yet-live entry is DEFERRED not dropped") {
  auto doc = build_raster_doc();
  CanvasHost host = make_inline_host();
  // `add` and `request_resize` are two separate UI-thread calls, so a drive iteration can land
  // BETWEEN them: it swaps the pending adds, then reaches the pending-request snapshot with this
  // id not yet in `entries`. A request for a not-yet-live id must SURVIVE that iteration —
  // discarding it strands the entry at zero area, where it builds no viewport, issues no frame
  // and publishes no sequence, i.e. a silent and PERMANENT stall (the intermittent gcc-tsan hang
  // in the edit_render_sync anchor was canvas#2's resize lost through exactly this window).
  host.request_resize("canvas#1", k_w, k_h);
  host.request_camera("canvas#1", k_nav_camera);
  settle(host); // an iteration with NO live entry at all: both requests must stay queued

  host.add("canvas#1", *doc);
  settle(host);

  std::uint64_t s = 0;
  Srgb8Image f;
  REQUIRE(host.consume("canvas#1", s, f));
  CHECK(f.width == k_w);
  CHECK(f.height == k_h);
  CHECK(f.pixels == ace::render::render_document_srgb8(*doc, k_w, k_h, k_nav_camera).pixels);
}

TEST_CASE("canvas_host: an add and a remove for the same id in one iteration — the entry never "
          "surfaces") {
  // Both calls collapse into ONE drive iteration: the fresh loop inserts canvas#1 into
  // `entries`, then the remove step extracts it into `dying` before any step/publish — so the
  // entry is created and destructed under the same lock and never surfaces
  // (D-pending_removes_order-2, the same-iteration pre-emption guard).
  auto doc = build_raster_doc();
  CanvasHost host = make_inline_host();
  host.add("canvas#1", *doc);
  host.remove("canvas#1");
  settle(host);

  CHECK(host.live_count() == 0);
  CHECK(host.published_sequence("canvas#1") == 0);
  std::uint64_t s = 0;
  Srgb8Image f;
  CHECK_FALSE(host.consume("canvas#1", s, f));
}

TEST_CASE("canvas_host: a remove interleaved between the pending-adds swap and the entry-map lock "
          "pre-empts the queued add") {
  // The drop window this leaf closes (D-pending_removes_order-1): the add is posted AFTER the
  // iteration's pending_adds.swap, so it is NOT in the swapped-out `adds` local — it sits in
  // pending_adds when removes run. On today's code the remove finds no live entry, does
  // nothing, and the queued add resurrects the canvas on the NEXT iteration; the fix cancels
  // it in pending_adds so the entry never surfaces. The drive-phase seam makes that
  // single-threaded window deterministic.
  auto doc = build_raster_doc();
  CanvasHost host = make_inline_host();
  bool fired = false;
  host.set_after_adds_swap_hook([&] {
    if (fired) {
      return; // one-shot: post the interleave exactly once, in the first iteration's window
    }
    fired = true;
    host.add("canvas#1", *doc);
    host.remove("canvas#1");
  });

  // Drive several explicit iterations: with the bug the resurrection lands on iteration 2
  // (settle() would stop at iteration 1's false return and miss it), so pin the entry ABSENT
  // across the following iterations, not just after the first.
  for (int i = 0; i < 5; ++i) {
    host.drive_once();
  }

  CHECK(fired); // the seam actually fired the interleave
  CHECK(host.live_count() == 0);
  CHECK(host.published_sequence("canvas#1") == 0);
  std::uint64_t s = 0;
  Srgb8Image f;
  CHECK_FALSE(host.consume("canvas#1", s, f));
}

TEST_CASE("canvas_host: a remove for an unknown id is a silent no-op") {
  // A remove naming an id that is neither live, nor a queued add, nor a queued resize/camera
  // does nothing and is not an error (Constraint 3) — and crucially is NOT retained: a
  // genuinely-new later add(id) the user asks for still surfaces, unpoisoned.
  auto doc = build_raster_doc();
  CanvasHost host = make_inline_host();
  host.remove("ghost");
  settle(host);
  CHECK(host.live_count() == 0);

  host.add("ghost", *doc);
  host.request_resize("ghost", k_w, k_h);
  settle(host);
  CHECK(host.live_count() == 1);
  std::uint64_t s = 0;
  Srgb8Image f;
  CHECK(host.consume("ghost", s, f));
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
  // The document's ONE writer identity, bound the way the shipped bootstrap binds it
  // (D-writer_thread-6): the writer starts BEFORE the document exists, the document is built
  // ON it (the first write binds the identity for life), and it is stopped only after the host
  // — whose entry/router teardown posts WRITER-THREAD-ONLY work — has been destroyed. The
  // declaration order below IS that contract: `writer` outlives `host`.
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  // The gate's liveness path: under a real WorkerPool + bounded budget the first step() can
  // issue a blank frame (tiles still resolving) that the content gate withholds. There is NO
  // async-completion wake, so the drive loop must stay armed while the renderer keeps issuing
  // new frames (tiles in flight) until a content frame composites and publishes — otherwise
  // the canvas stalls blank at sequence 0. build_raster_doc's full-frame solid background is
  // covered content the compositor resolves within a few bounded steps.
  auto doc = build_on_writer(writer, [] { return build_raster_doc(); });
  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
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

// --- editor.canvas.render_loop_liveness_wake: park-on-completions, no spin, no lost wakeup ----
//
// The blank_first_frame content gate withholds sequence 0 until the first composited coverage; the
// pre-fix run() loop stayed alive through that blank phase by BUSY-SPINNING on the racy
// pending().tiles poll, with a rare LOST WAKEUP when the poll flickered empty while a worker was
// still settling a tile (the group_transform ASan/TSan flake). The fix parks the loop on the shared
// WorkerPool's completion substrate (wait_completions/poke), seeded from settle_generation() before
// each drive, so a worker's completion re-drives it event-driven and a settle that races the poll
// is caught by the generation, not missed. Direct-seeded rasters (build_raster_doc): no external
// load (Constraint 7), no threaded nested render (the arbc nested-render detach-race memo).

TEST_CASE(
    "canvas_host: the render loop reaches the first content frame in BOUNDED drive iterations "
    "under the REAL pool — park-and-wake, not spin "
    "(editor.canvas.render_loop_liveness_wake)") {
  // Same writer-outlives-host contract as the blank-then-content fixture above.
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  auto doc = build_on_writer(writer, [] { return build_raster_doc(); });
  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *doc);
  host.request_resize("canvas#1", k_w, k_h);
  // No pokes after the initial add/resize: the loop must reach content ON ITS OWN — the property
  // the pre-fix lost-wakeup could violate.
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 1; }));

  // The load-bearing assertion (the direct regression guard for the flake): park-and-wake is
  // O(completion rounds); the pre-fix spin burns hundreds-to-thousands of drive_once() calls over
  // the SAME millisecond-scale tile resolution. A generous bound fails the pre-fix spin (and, in
  // the racy window, the pump_until deadline above) yet passes the event-driven fix comfortably,
  // even under a sanitizer's slowdown — it is a real assertion, not a tautology.
  CHECK(host.iterations() < 500);

  host.stop();
  handle->join();
}

TEST_CASE("canvas_host: the render loop PARKS at rest (no busy-spin) and a host wake re-drives it "
          "(editor.canvas.render_loop_liveness_wake)") {
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  auto doc = build_on_writer(writer, [] { return build_raster_doc(); });
  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *doc);
  host.request_resize("canvas#1", k_w, k_h);
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 1; }));

  // At rest the scene is still and no worker is running: the loop PARKS on wait_completions with
  // ZERO wakeups (Constraints 1 & 3), it does not busy-spin. This host owns its pool, so there are
  // not even sibling settles to spuriously wake it — the liveness counter must hold steady across a
  // quiet window (a spinning loop would burn thousands of iterations here).
  const std::uint64_t at_rest = host.iterations();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(host.iterations() - at_rest < 50);

  // A host event pokes the pool (Impl::wake), interrupting the park (Constraint 4): the loop wakes,
  // re-drives, and republishes at the new size with NO other poke. This FAILS if wake() stops
  // poking the pool (the loop parked on wait_completions would sleep through the resize forever),
  // so it directly guards the wake()->pool.poke() half of the fix.
  const std::uint64_t before = host.published_sequence("canvas#1");
  host.request_resize("canvas#1", 48, 40);
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") > before; }));

  host.stop();
  handle->join();
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
// settle. `bridge` is the load-side KindBridge (kept alive by the caller).
//
// ASSERTION-FREE, because `load_document` is the FIRST write and therefore BINDS the document's
// writer identity — so the cases below run this ON the writer thread, and a Catch2 REQUIRE that
// threw inside a posted closure would unwind straight through the writer's loop. Callers assert
// the post-conditions from their own thread instead.
std::unique_ptr<arbc::Document> load_pending_nested_raw(DeferringAssetSource& source,
                                                        arbc::KindBridge& bridge,
                                                        const arbc::Registry& registry) {
  source.put("mem/child.arbc", k_leaf);
  auto doc = std::make_unique<arbc::Document>();
  if (!arbc::load_document(nesting_doc("child.arbc"), *doc, bridge, registry, "mem/parent.arbc",
                           &source)) {
    return nullptr;
  }
  return doc;
}

// The same, for the single-threaded fixtures (this thread IS the writer identity there).
std::unique_ptr<arbc::Document> load_pending_nested(DeferringAssetSource& source,
                                                    arbc::KindBridge& bridge,
                                                    const arbc::Registry& registry) {
  std::unique_ptr<arbc::Document> doc = load_pending_nested_raw(source, bridge, registry);
  REQUIRE(doc != nullptr);
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

  // Byte-exact vs the settled reference. NOTE the reference is NOT render_document_srgb8.
  //
  // The ORIGINAL reason given here was that render_offline binds no operators and so composites
  // no nested-composition operator at all. That is STALE and was corrected at the 2026-08-07
  // parking-lot triage: since the v0.4.0 render-path unification, render_offline runs the same
  // tiled driver as the interactive loop and DOES bind operators (arbc offline.cpp:52 calls
  // bind_operators after register_builtin_operator_binders). Do not propagate the old claim.
  //
  // The reference construction below is nonetheless still CORRECT, for the narrower reason that
  // survives: render_offline does not SETTLE deferred external loads (offline.cpp:78-82 passes
  // pending=nullptr), and this document's child arrives via a DeferringAssetSource — so an
  // offline render of it would still be blank here, for want of the settle rather than for want
  // of the binding. The convergence claim is proved against a MATCHED compositor: an INDEPENDENT
  // copy pre-settled to quiescence (loop settle_external_loads to 0), then rendered through an
  // EMPTY-binding interactive host — once the child is installed the empty binding composites it
  // via bind_operators (the settle hook is what an empty binding lacks, not the compositor). The
  // wired settle must converge to that fully-settled render byte-for-byte.
  //
  // (render_offline's inability to settle deferred external loads is carried in
  // tasks/parking-lot.md under "Waiting on evidence". It is LATENT, not live: the shipped
  // arbc::FilesystemAssetSource answers every request inline before request() returns, so no
  // external load is ever outstanding in production — the deferral here comes from this file's
  // DeferringAssetSource test double. The trigger is the editor gaining a genuinely deferring
  // asset source, e.g. a WASM/network one under A3's swappable platform seam.)
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

// --- editor.canvas.writer_thread: the writer consumes the deferred settle PROACTIVELY -------
//
// Since the v0.3.0 pin `HostViewport::step()` refuses to publish off the writer thread, so a
// landed arrival is work only the WRITER can do. D-writer_thread-10's claim is that the writer
// does not wait to be told: it polls whenever its queue drains, before waiting. The two cases
// below are the two halves of that claim — the arrival installs with NOTHING else running at all,
// and with a live render loop the D-10 nudge only shortens the latency (it does not double-settle).

TEST_CASE("canvas_host: an idle writer settles a deferred external arrival with NO edit and NO "
          "render loop (D-writer_thread-10)") {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);

  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);

  // ONE document-scoped, writer-owned KindBridge (D-writer_thread-9) — the same object the load
  // used and the one every canvas over this document is handed, because the document holds a
  // single external-load settler slot.
  DeferringAssetSource source;
  arbc::KindBridge bridge;
  std::unique_ptr<arbc::Document> doc =
      build_on_writer(writer, [&] { return load_pending_nested_raw(source, bridge, registry); });
  REQUIRE(doc != nullptr);
  REQUIRE(load_counters(writer, *doc).pending == 1);

  // The writer's idle work, bound exactly as `run_editor` binds it.
  std::atomic<int> installs{0};
  writer.set_idle_work([&] {
    if (doc->external_loads_ready() > 0) {
      installs += static_cast<int>(arbc::settle_external_loads(*doc, bridge, registry));
    }
    return doc->pending_external_loads() > 0;
  });

  // Nobody is editing. Nothing is rendering. There is no host and no render thread in this case
  // AT ALL — which is precisely the state in which the pre-writer_thread editor would have sat on
  // the placeholder forever, because no one was watching.
  REQUIRE(source.fire_all() == 1);
  // Poll an EDITOR-owned atomic, never the document's writer-owned counters (see load_counters).
  REQUIRE(pump_until([&] { return installs.load() >= 1; }));
  CHECK(doc->external_loads_ready() == 0); // any-thread, lock-free: the queue drained
  writer.set_idle_work({});                // disarm before reading the writer-owned counters
  CHECK(load_counters(writer, *doc).pending == 0);

  // The install is real, not just a drained counter: a driven frame now composites the child.
  CanvasHost host = make_inline_host();
  host.set_writer(&writer);
  host.add("canvas#1", *doc, &registry, &bridge);
  host.request_resize("canvas#1", k_w, k_h);
  settle(host);
  std::uint64_t s = 0;
  Srgb8Image frame;
  REQUIRE(host.consume("canvas#1", s, frame));
  CHECK(ace::render::frame_has_content(frame));
}

TEST_CASE("canvas_host: a LIVE render loop nudges the arrival onto the writer within the "
          "observing frame, and it settles exactly once (D-writer_thread-10)") {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);

  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);

  DeferringAssetSource source;
  arbc::KindBridge bridge;
  std::unique_ptr<arbc::Document> doc =
      build_on_writer(writer, [&] { return load_pending_nested_raw(source, bridge, registry); });
  REQUIRE(doc != nullptr);
  REQUIRE(load_counters(writer, *doc).pending == 1);

  // NO idle work here — deliberately. This case pins the OTHER wake path in isolation: the render
  // thread's per-frame `StepOutcome::external_loads_ready` report, turned into ONE deduped ASYNC
  // submission (the render thread must never block a frame on an arrival, Constraint 3). The
  // inline degenerate pool + settle-fully budget keeps the composite deterministic while the
  // drive loop genuinely runs off-thread.
  CanvasHost host(arbc::WorkerPoolConfig{}, std::chrono::hours(1));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });
  host.add("canvas#1", *doc, &registry, &bridge);
  host.request_resize("canvas#1", k_w, k_h);
  REQUIRE(pump_until([&] { return host.iterations() >= 1; }));
  CHECK(host.published_sequence("canvas#1") == 0); // the placeholder composites no coverage yet

  REQUIRE(source.fire_all() == 1);
  // Keep the loop stepping so a frame OBSERVES the landed arrival — that observation is the
  // nudge. `step()` itself installs nothing (it is not the writer thread and, since v0.3.0,
  // refuses to publish from there); it reports, and the writer installs.
  //
  // Poll the HOST's install counter, NOT `external_loads_ready()`: `settle_external_loads` takes
  // the whole ready queue as its FIRST act and only returns the installed count once the arrival
  // has been parsed and its child model built, so `external_loads_ready() == 0` is observable
  // strictly BEFORE `settles_installed` is bumped. Waiting on the drain and then reading the
  // counter races against the settle's own body — a window the asan lane loses.
  REQUIRE(pump_until([&] {
    host.poke();
    return host.settles_installed() >= 1;
  }));
  // With no edits in this case, arbc's own auto-settler at `Document::begin()` never runs and the
  // writer has no idle poll bound, so the nudge is the ONLY path that could have installed it.
  CHECK(host.settles_installed() == 1);
  CHECK(doc->external_loads_ready() == 0); // any-thread, lock-free: the queue drained

  // ...and the install composites: the child replaces the placeholder on a later frame.
  REQUIRE(pump_until([&] {
    host.poke();
    return host.published_sequence("canvas#1") >= 1;
  }));

  // EXACTLY ONE install, summed across every path that could have performed it: the host's async
  // nudge (`settles_installed`) and arbc's own auto-settler at the next `Document::begin()`
  // (`external_loads_auto_settled`). All three settle paths are idempotent — the settle drains the
  // ready queue and re-entry is suppressed by arbc's InstallScope — so whichever got there first,
  // the others installed nothing. That is the "without double-settling" half of D-writer_thread-10,
  // and it is why none of the three has to be load-bearing alone.
  std::uint64_t s = 0;
  Srgb8Image frame;
  REQUIRE(host.consume("canvas#1", s, frame));
  CHECK(ace::render::frame_has_content(frame));

  host.stop();
  handle->join();
  const LoadCounters counters = load_counters(writer, *doc);
  CHECK(counters.pending == 0);
  CHECK(host.settles_installed() + counters.auto_settled == 1);
}

// --- editor.canvas.writer_thread: the identity tripwire -------------------------------------
//
// The v0.3.0 predicate asserted directly (`Document::on_writer_thread()`), which is what makes
// this design testable rather than commented: ONE identity, it is the writer thread, and neither
// the UI thread nor the render thread is it.

TEST_CASE("canvas_host: on_writer_thread() is true inside every posted closure and false on the "
          "UI and render threads (D-writer_thread-1)") {
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  ProbeDocument probe = build_on_writer(writer, [] { return build_probe_document(); });
  // The identity is bound by the load/build above, so it is already NOT this thread.
  CHECK_FALSE(probe.document->on_writer_thread());
  CHECK_FALSE(writer.on_writer_thread());

  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  // The render thread asks the SAME predicate from inside its own drive loop — this is the thread
  // that, before this leaf, installed the settler and published from `step()`, i.e. the second
  // identity the whole task exists to remove.
  std::atomic<bool> stop_render{false};
  std::atomic<int> render_polls{0};
  std::atomic<int> render_was_writer{0};
  std::unique_ptr<ace::platform::JoinHandle> handle = threads.spawn([&] {
    while (!stop_render.load()) {
      if (probe.document->on_writer_thread()) {
        ++render_was_writer;
      }
      ++render_polls;
      host.drive_once();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  host.add("canvas#1", *probe.document);
  host.request_resize("canvas#1", k_w, k_h);
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 1; }));

  // Every posted edit runs on the ONE identity — asked of libarbc, not of our own bookkeeping.
  std::atomic<int> on_writer{0};
  for (int i = 0; i < 8; ++i) {
    apply_edit(writer, host, [&] {
      if (probe.document->on_writer_thread() && writer.on_writer_thread()) {
        ++on_writer;
      }
      damage(*probe.document, probe.composition);
    });
  }
  CHECK(on_writer.load() == 8);

  stop_render.store(true);
  handle->join();
  CHECK(render_polls.load() > 0);
  CHECK(render_was_writer.load() == 0);            // the render thread is NOT the writer
  CHECK_FALSE(probe.document->on_writer_thread()); // and neither is the UI thread

  host.stop();
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
  // The document's ONE writer identity, bound the way the shipped bootstrap binds it
  // (D-writer_thread-6): the writer starts BEFORE the document exists, the document is built
  // ON it (the first write binds the identity for life), and it is stopped only after the host
  // — whose entry/router teardown posts WRITER-THREAD-ONLY work — has been destroyed. The
  // declaration order below IS that contract: `writer` outlives `host`.
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);

  auto doc = build_on_writer(writer, [] {
    return build_raster_doc();
  }); // a bounded raster square over a solid bg (a root composition)
  // Seed the shot in the root composition ON the writer thread, before the render loop —
  // `add_camera` transacts, and every transaction belongs to the one identity (D-writer_thread-1).
  arbc::ObjectId cam_id;
  writer.submit_sync([&] {
    cam_id = ace::scene::add_camera(*doc, registry, "hero", ace::scene::Resolution{32, 32},
                                    arbc::Affine::translation(8.0, 8.0));
  });
  REQUIRE(cam_id.valid());

  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
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
    apply_edit(writer, host, [&] {
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
  // The document's ONE writer identity, bound the way the shipped bootstrap binds it
  // (D-writer_thread-6): the writer starts BEFORE the document exists, the document is built
  // ON it (the first write binds the identity for life), and it is stopped only after the host
  // — whose entry/router teardown posts WRITER-THREAD-ONLY work — has been destroyed. The
  // declaration order below IS that contract: `writer` outlives `host`.
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);

  auto doc = build_on_writer(writer, [] { return build_raster_doc(); });
  arbc::ObjectId cam_id;
  writer.submit_sync([&] {
    cam_id = ace::scene::add_camera(*doc, registry, "hero", ace::scene::Resolution{32, 32},
                                    arbc::Affine::translation(8.0, 8.0));
  });
  REQUIRE(cam_id.valid());

  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
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
    apply_edit(writer, host, [&] {
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

TEST_CASE("canvas_host: a stream of UI-thread cell inserts then a batch delete runs clean against "
          "the render read") {
  // The document's ONE writer identity, bound the way the shipped bootstrap binds it
  // (D-writer_thread-6): the writer starts BEFORE the document exists, the document is built
  // ON it (the first write binds the identity for life), and it is stopped only after the host
  // — whose entry/router teardown posts WRITER-THREAD-ONLY work — has been destroyed. The
  // declaration order below IS that contract: `writer` outlives `host`.
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  // editor.cells.one_action_one_entry Acceptance (Threading): the SHIPPED insert path — a
  // registry factory call, then `Document::create_content_and_attach` binding the Content
  // vtable, adding the placing layer, and attaching it in ONE transaction — streamed through
  // `apply_edit` on the REAL interactive pool against a live rendering canvas, then a SYMMETRIC
  // batch delete (`scene::remove_cells` -> `Document::remove_contents`, one transaction for the
  // whole selection). Structurally heavier than `damage()`'s solid add (a raster mints a tile
  // pyramid), so it widens the overlap window the D-edit_render_sync-3 anchor above opens. No
  // new lock and no new thread: A4.1 writer-identity holds because every edit runs on the
  // caller, and the render walk reads the COW-published bindings lock-free. Collapsing the
  // create to one entry and the delete to one transaction REDUCES writer round-trips; it adds
  // no new shared state (Constraint 7).
  ProbeDocument probe = build_on_writer(writer, [] { return build_probe_document(); });
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
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
    apply_edit(writer, host, [&] {
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

  // Every insert landed exactly once: the probe's own untokened solid plus k_inserts.
  REQUIRE(ace::scene::cells(*probe.document, registry).size() ==
          static_cast<std::size_t>(k_inserts) + 1);

  // Now tear the whole set down in ONE `remove_contents` transaction on the writer thread,
  // overlapping the render walk exactly as the inserts did — the batch path under TSan.
  std::size_t removed = 0;
  apply_edit(writer, host, [&] {
    std::vector<ace::scene::CellRemoval> batch;
    for (const ace::scene::Cell& cell : ace::scene::cells(*probe.document, registry)) {
      batch.push_back(ace::scene::CellRemoval{cell.id, cell.layer});
    }
    removed = ace::scene::remove_cells(*probe.document, batch);
  });
  REQUIRE(pump_until([&] {
    return host.published_sequence("canvas#1") >= 3 && host.published_sequence("canvas#2") >= 3;
  }));

  host.stop();
  handle->join();

  // The one batch removed every placed object at once, leaving the root composition empty.
  CHECK(removed == static_cast<std::size_t>(k_inserts) + 1);
  CHECK(ace::scene::cells(*probe.document, registry).empty());
}

TEST_CASE("canvas_host: UI-thread pick_targets reads run clean against the live render walk") {
  // The document's ONE writer identity, bound the way the shipped bootstrap binds it
  // (D-writer_thread-6): the writer starts BEFORE the document exists, the document is built
  // ON it (the first write binds the identity for life), and it is stopped only after the host
  // — whose entry/router teardown posts WRITER-THREAD-ONLY work — has been destroyed. The
  // declaration order below IS that contract: `writer` outlives `host`.
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  // editor.cells.selection Acceptance (Threading): selection mutates NOTHING, so this leaf adds
  // no writer path — but `interact::pick_targets` calls a `Content` VIRTUAL
  // (`arbc::Content::bounds()`, via `scene::cells`' pinned walk, D-selection-11) on the UI
  // thread against a document a render thread is walking. The lock-free `pin()` seam covers the
  // record walk; the `bounds()` call is covered by libarbc's stated ANY-THREAD contract for
  // `Content::bounds()` (arbc#23, landed at the v0.4.0 pin, D-arbc_v040-5). Repeated
  // pick_targets + pick on the UI thread against a LIVE rendering real-pool host (the
  // D-edit_render_sync-3 anchor) while cell inserts stream through `apply_edit`. No new lock and
  // no new thread: this case is no longer a TRIPWIRE for an unstated race — it ASSERTS a stated
  // guarantee. At v0.3.0 the read was safe only because every editor kind fixes its extent at
  // construction (`scene::CameraContent::bounds()` is constant-empty, src/scene/ace/scene/
  // camera.hpp:46-48; every other kind the editor uses is a library built-in); at v0.4.0 it is
  // safe by contract. A TSan/ASan report here is therefore a real contract violation, not the
  // open design question it once flagged.
  ProbeDocument probe = build_on_writer(writer, [] { return build_probe_document(); });
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *probe.document, &registry);
  host.request_resize("canvas#1", k_w, k_h);
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 1; }));

  // A camera in the mix so the merge half of the adapter (cells + cameras) is walked too.
  apply_edit(writer, host, [&] {
    REQUIRE(ace::scene::add_camera(*probe.document, registry, "Hero",
                                   ace::scene::Resolution{16, 16},
                                   arbc::Affine::translation(4.0, 4.0))
                .valid());
  });

  constexpr int k_inserts = 24;
  std::size_t last_targets = 0;
  for (int i = 0; i < k_inserts; ++i) {
    apply_edit(writer, host, [&] {
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

TEST_CASE("canvas_host: UI-thread resolution-health reads run clean against the live render walk") {
  // editor.cells.resolution Acceptance (Threading): the inspector's cell-resolution readout adds
  // NO shared mutable state and NO mutation — it reads `scene::cells` (now also the
  // `editable()`/`external_asset_ref()` provenance classification beside the existing
  // `content_bounds` read, D-resolution-1/-2) and `scene::cameras` on the UI thread while a
  // render thread walks the SAME document over the lock-free `pin()`. `editable()` /
  // `external_asset_ref()` / `bounds()` are const reads of construction-stable content state
  // (this leaf grows no grid, so `bounds()` stays read-only — Constraint 5), covered by the same
  // any-thread `Content`-virtual contract the `pick_targets` anchor above asserts (arbc#23, the
  // v0.4.0 pin). A TSan/ASan report here is a real contract violation, not a design tripwire.
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  ProbeDocument probe = build_on_writer(writer, [] { return build_probe_document(); });
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *probe.document, &registry);
  host.request_resize("canvas#1", k_w, k_h);
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 1; }));

  // A camera to compare the cells' resolution health against.
  apply_edit(writer, host, [&] {
    REQUIRE(ace::scene::add_camera(*probe.document, registry, "Hero",
                                   ace::scene::Resolution{32, 32},
                                   arbc::Affine::translation(4.0, 4.0))
                .valid());
  });

  constexpr int k_inserts = 24;
  for (int i = 0; i < k_inserts; ++i) {
    apply_edit(writer, host, [&] {
      const bool solid = (i % 2) == 0;
      const auto added = ace::scene::add_cell(
          *probe.document, registry, solid ? "org.arbc.solid" : "org.arbc.raster",
          solid ? "0.1,0.2,0.3,1" : "24x24",
          arbc::Affine::translation(static_cast<double>(i), static_cast<double>(i)));
      REQUIRE(added.has_value());
    });
    // The UI-thread read the inspector performs EVERY frame: classify each cell's provenance
    // and compute its resolution health against each camera, straight against the live render
    // walk. The classification (`editable()`/`external_asset_ref()`) is the new read this leaf
    // adds; the health math is pure over the primitive values it yields.
    for (int r = 0; r < 4; ++r) {
      const std::vector<ace::scene::Cell> cells = ace::scene::cells(*probe.document, registry);
      const std::vector<ace::scene::Camera> cams = ace::scene::cameras(*probe.document);
      for (const ace::scene::Cell& cell : cells) {
        // A raster classifies PaintedRaster with a native grid; a solid is resolution-
        // independent — reading either facet is the race surface under test.
        if (!cell.detail.native_pixels.has_value()) {
          continue;
        }
        for (const ace::scene::Camera& cam : cams) {
          const ace::interact::ResolutionHealth health = ace::interact::resolution_health(
              cell.detail.native_pixels->first, cell.detail.native_pixels->second, cell.placement,
              cam.resolution.width, cam.resolution.height, cam.frame);
          REQUIRE(health.verdict != ace::interact::ResolutionVerdict::NotApplicable);
        }
        // editor.paint.paint_res (Threading): the on-canvas `~ N px on <cell>` detail-floor readout
        // performs the SAME per-frame const read — `cell.detail.native_pixels` presence +
        // `cell.placement` — off the live pin() while the render thread walks the same document.
        // The pure `brush_detail_floor` math over those raced reads adds NO new lock and NO new
        // thread; a TSan/ASan report here is a real contract violation. The paint stream itself
        // stays covered by brush's existing TSan anchor (unchanged).
        const ace::interact::BrushDetailFloor floor = ace::interact::brush_detail_floor(
            4.0, cell.placement, cell.detail.native_pixels.has_value());
        REQUIRE(floor.valid); // a raster cell (native px present) always yields a number
      }
    }
  }
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 2; }));

  host.stop();
  handle->join();

  // A stable final read: every raster insert carries a native grid classified PaintedRaster.
  const std::vector<ace::scene::Cell> final_cells = ace::scene::cells(*probe.document, registry);
  int painted = 0;
  for (const ace::scene::Cell& cell : final_cells) {
    if (cell.detail.source == ace::scene::DetailSource::PaintedRaster) {
      ++painted;
    }
  }
  CHECK(painted == k_inserts / 2);
}

TEST_CASE("canvas_host: UI-thread inspector reads + a set_cell_opacity edit run clean against the "
          "live render walk") {
  // editor.panels.inspector Acceptance (Threading): the inspector reads `scene::cells` +
  // `interact::decompose_placement` + `interact::resolution_health` on the UI thread and
  // dispatches a `set_cell_opacity` edit through `apply_edit` — concurrent with a render thread
  // walking the SAME document over the lock-free `pin()` seam. The reads are const reads of
  // construction-stable content state (Constraint 7 / A18); the edit rides the ONE writer funnel
  // (D-inspector-2), one transaction. No new lock, no new thread, no new suppression — a
  // TSan/ASan report here is a real contract violation. `writer` outlives `host` (the same
  // declaration-order contract the sibling cases above assert).
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  ProbeDocument probe = build_on_writer(writer, [] { return build_probe_document(); });
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *probe.document, &registry);
  host.request_resize("canvas#1", k_w, k_h);
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 1; }));

  // A camera so the health read has a capturing frame.
  apply_edit(writer, host, [&] {
    REQUIRE(ace::scene::add_camera(*probe.document, registry, "Hero",
                                   ace::scene::Resolution{64, 64}, arbc::Affine::scaling(0.5, 0.5))
                .valid());
  });
  // The painted raster the opacity edit targets, keyed by its content id (the selection's key).
  arbc::ObjectId target;
  apply_edit(writer, host, [&] {
    const auto added = ace::scene::add_cell(*probe.document, registry, "org.arbc.raster", "48x48",
                                            arbc::Affine::translation(2.0, 2.0));
    REQUIRE(added.has_value());
    target = *added;
  });

  constexpr int k_iters = 24;
  for (int i = 0; i < k_iters; ++i) {
    // The UI-thread read path the panel runs EVERY frame: cells + transform decomposition + the
    // resolution-health verdict, straight against the live render walk.
    for (int r = 0; r < 3; ++r) {
      const std::vector<ace::scene::Cell> cells = ace::scene::cells(*probe.document, registry);
      const std::vector<ace::scene::Camera> cams = ace::scene::cameras(*probe.document);
      for (const ace::scene::Cell& cell : cells) {
        const ace::interact::PlacementReadout t =
            ace::interact::decompose_placement(cell.placement, cell.content_bounds);
        (void)t;
        if (!cell.detail.native_pixels.has_value()) {
          continue;
        }
        for (const ace::scene::Camera& cam : cams) {
          (void)ace::interact::resolution_health(
              cell.detail.native_pixels->first, cell.detail.native_pixels->second, cell.placement,
              cam.resolution.width, cam.resolution.height, cam.frame);
        }
      }
    }
    // The inspector's edit, through the one writer funnel, interleaved with the reads above.
    const double op = (i % 2 == 0) ? 0.4 : 0.9;
    apply_edit(writer, host, [&] {
      REQUIRE(ace::scene::set_cell_opacity(*probe.document, registry, target, op));
    });
  }
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 2; }));

  host.stop();
  handle->join();

  // The final opacity is the last write (i == k_iters-1 == 23 => odd => 0.9).
  double final_opacity = -1.0;
  for (const ace::scene::Cell& cell : ace::scene::cells(*probe.document, registry)) {
    if (cell.id == target) {
      final_opacity = cell.opacity;
    }
  }
  CHECK(final_opacity > 0.89);
  CHECK(final_opacity < 0.91);
}

TEST_CASE("canvas_host: UI-thread layers reads + a reorder_cell edit run clean against the live "
          "render walk") {
  // editor.panels.layers Acceptance (Threading): the Layers panel reads `scene::cells(active)` +
  // `scene::cameras` + `scene::nested_composition_of` + the breadcrumb resolve
  // (`scene::active_composition` / `scene::composition_path`) on the UI thread and dispatches a
  // `reorder_cell` edit through `apply_edit` — concurrent with a render thread walking the SAME
  // document over the lock-free `pin()` seam. Reads are const reads of construction-stable content
  // state (Constraint 10 / A18); the edit rides the ONE writer funnel (D-layers-2), one
  // transaction. No new lock, no new thread, no new suppression. `writer` outlives `host`.
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  ProbeDocument probe = build_on_writer(writer, [] { return build_probe_document(); });
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *probe.document, &registry);
  host.request_resize("canvas#1", k_w, k_h);
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 1; }));

  // Two reorderable cells, a nested cell (so the reads exercise nested_composition_of + the
  // breadcrumb descent), and a camera (excluded from the layers section, present in cameras()).
  arbc::ObjectId moved;
  arbc::ObjectId child;
  apply_edit(writer, host, [&] {
    const auto a = ace::scene::add_cell(*probe.document, registry, "org.arbc.raster", "48x48",
                                        arbc::Affine::translation(2.0, 2.0));
    const auto b = ace::scene::add_cell(*probe.document, registry, "org.arbc.raster", "48x48",
                                        arbc::Affine::translation(4.0, 4.0));
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    moved = *a;
    child = probe.document->add_composition(32.0, 32.0);
    REQUIRE(ace::scene::add_cell(*probe.document, registry, "org.arbc.nested",
                                 std::to_string(child.value), arbc::Affine::translation(6.0, 6.0))
                .has_value());
    REQUIRE(ace::scene::add_camera(*probe.document, registry, "Hero",
                                   ace::scene::Resolution{64, 64}, arbc::Affine::scaling(0.5, 0.5))
                .valid());
  });

  const arbc::ObjectId root = ace::scene::active_composition(*probe.document, std::nullopt);
  REQUIRE(root.valid());
  const std::size_t seeded_count = ace::scene::cells(*probe.document, registry, root).size();

  constexpr int k_iters = 24;
  for (int i = 0; i < k_iters; ++i) {
    // The UI-thread read path the panel runs EVERY frame: the active composition, its cells, the
    // cameras, each cell's nested-child probe, and the derived breadcrumb — against the live walk.
    for (int r = 0; r < 3; ++r) {
      const arbc::ObjectId active = ace::scene::active_composition(*probe.document, std::nullopt);
      const std::vector<ace::scene::Cell> cells =
          ace::scene::cells(*probe.document, registry, active);
      const std::vector<ace::scene::Camera> cams = ace::scene::cameras(*probe.document);
      (void)cams;
      for (const ace::scene::Cell& cell : cells) {
        (void)ace::scene::nested_composition_of(*probe.document, cell.id);
      }
      (void)ace::scene::composition_path(*probe.document, registry, std::nullopt);
      (void)ace::scene::composition_path(*probe.document, registry, child);
    }
    // The panel's edit, through the one writer funnel, interleaved with the reads above: reorder
    // the moved cell between the bottom and top of the (cells-space) z-order.
    const std::uint32_t to_index = (i % 2 == 0) ? 2U : 0U;
    apply_edit(writer, host, [&] {
      const ace::commands::Command cmd = ace::commands::reorder_cell_command(root, moved, to_index);
      cmd.apply(*probe.document);
    });
  }
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 2; }));

  host.stop();
  handle->join();

  // The document still reads a consistent layers list on the same objects after the interleaving.
  const std::vector<ace::scene::Cell> final_cells = ace::scene::cells(
      *probe.document, registry, ace::scene::active_composition(*probe.document, std::nullopt));
  CHECK(final_cells.size() == seeded_count); // reorder preserves membership (camera stays excluded)
  bool moved_present = false;
  for (const ace::scene::Cell& cell : final_cells) {
    if (cell.id == moved) {
      moved_present = true;
    }
  }
  CHECK(moved_present); // the moved cell kept its ObjectId across every reorder
}

TEST_CASE("canvas_host: a frame-selection mint — a full document READ plus a camera WRITE in one "
          "apply_edit — runs clean against the live render walk") {
  // The document's ONE writer identity, bound the way the shipped bootstrap binds it
  // (D-writer_thread-6): the writer starts BEFORE the document exists, the document is built
  // ON it (the first write binds the identity for life), and it is stopped only after the host
  // — whose entry/router teardown posts WRITER-THREAD-ONLY work — has been destroyed. The
  // declaration order below IS that contract: `writer` outlives `host`.
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  // editor.cameras.frame_selection Acceptance (Threading): a genuinely NEW threading surface.
  // Every prior edit performed only a WRITE inside `apply_edit`; the mint performs a full
  // document READ WALK there too — `interact::pick_targets` resolves every Content and calls
  // the `bounds()` virtual — and then, in the SAME closure, `scene::add_camera` binds a Content
  // vtable and attaches a layer (two transactions). Resolving the geometry inside the edit is
  // D-cells_remove-3's rule strengthened (Constraint 7): the extents, not just the ids, must
  // come from the generation the transaction lands on. Driven on the REAL interactive pool
  // (the D-edit_render_sync-3 anchor) while the render thread walks the same document over the
  // lock-free `pin()` seam. No new lock, no new thread, no new suppression.
  ProbeDocument probe = build_on_writer(writer, [] { return build_probe_document(); });
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *probe.document, &registry);
  host.request_resize("canvas#1", k_w, k_h);
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 1; }));

  constexpr int k_mints = 16;
  std::size_t minted = 0;
  for (int i = 0; i < k_mints; ++i) {
    // A bounded cell to frame, so the union is never empty and the walk keeps growing.
    apply_edit(writer, host, [&] {
      const auto added = ace::scene::add_cell(
          *probe.document, registry, "org.arbc.raster", "24x24",
          arbc::Affine::translation(static_cast<double>(i) * 3.0, static_cast<double>(i) * 3.0));
      REQUIRE(added.has_value());
    });
    // The SHIPPED mint, whole, inside ONE apply_edit: read the stack, union the placed
    // extents, derive the framing, write the camera.
    apply_edit(writer, host, [&] {
      const std::vector<ace::interact::PickTarget> targets =
          ace::interact::pick_targets(*probe.document, registry);
      const std::vector<arbc::ObjectId> ids = ace::interact::all_ids(targets);
      const std::optional<arbc::Rect> extent = ace::interact::selected_extent(targets, ids);
      REQUIRE(extent.has_value()); // the inserted rasters are bounded
      const ace::interact::ShotFraming shot = ace::interact::shot_from_extent(*extent);
      REQUIRE(shot.width > 0);
      if (ace::scene::add_camera(*probe.document, registry, "Camera " + std::to_string(i + 1),
                                 ace::scene::Resolution{shot.width, shot.height}, shot.frame)
              .valid()) {
        ++minted;
      }
    });
  }
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 2; }));

  host.stop();
  handle->join();

  // Every mint landed exactly once, and the cameras are all still there (a camera renders zero
  // pixels, so the render walk saw them without ever compositing one).
  CHECK(minted == static_cast<std::size_t>(k_mints));
  CHECK(ace::scene::cameras(*probe.document).size() == static_cast<std::size_t>(k_mints));
}

// --- editor.canvas.writer_thread: the TSan acceptance anchor --------------------------------
//
// The escalated concurrency target this leaf owns, and the successor to the streamed-edit anchor
// above (which proved the lock-free COW read UNDER the writer-priority lease — a lease this leaf
// deletes rather than renames). Four things overlap in wall-clock time on the real interactive
// pool: streamed UI edits posted to the writer, a live off-thread render loop, a canvas added and
// removed MID-STREAM (so the per-entry HostViewport ctor/dtor — the WRITER-THREAD-ONLY settler
// slot and the DamageRouter registrant list — are posted while edits are in flight), and a
// deferred external child arriving from a thread that is neither. No lock stands between any of
// them; what makes it sound is that every WRITE has ONE identity.
//
// Regression-guard note (NOT a tautology): keep the writer thread but leave the HostViewport
// construction on the render thread and this is a live race on the document's settler slot and
// the router's registrant vector, which the hermetic gcc-tsan lane reports.
TEST_CASE("canvas_host: streamed edits + a live render loop + a canvas added/removed mid-stream + "
          "a deferred external arrival run clean (writer_thread TSan anchor)") {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);

  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);

  DeferringAssetSource source;
  arbc::KindBridge bridge; // the ONE document-scoped, writer-owned bridge (D-writer_thread-9)
  std::unique_ptr<arbc::Document> doc =
      build_on_writer(writer, [&] { return load_pending_nested_raw(source, bridge, registry); });
  REQUIRE(doc != nullptr);
  REQUIRE(load_counters(writer, *doc).pending == 1);
  // The parent composition libarbc's own lowest-id-wins root rule picks — the same anchor the
  // compositor walks from.
  arbc::ObjectId composition;
  {
    const arbc::CompositionRecord* rec = nullptr;
    REQUIRE(doc->pin()->find_first_composition(composition, rec));
  }

  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  // The writer's own idle poll, bound exactly as `run_editor` binds it — so the arrival has a
  // consumer even in the windows where no frame is being stepped at all.
  std::atomic<int> idle_installs{0};
  writer.set_idle_work([&] {
    if (doc->external_loads_ready() > 0) {
      idle_installs += static_cast<int>(arbc::settle_external_loads(*doc, bridge, registry));
      host.poke();
    }
    return doc->pending_external_loads() > 0;
  });

  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });
  host.add("canvas#1", *doc, &registry, &bridge);
  host.request_resize("canvas#1", k_w, k_h);
  REQUIRE(pump_until([&] { return host.iterations() >= 1; }));

  constexpr int k_edits = 48;
  for (int i = 0; i < k_edits; ++i) {
    // A canvas joins and leaves in the middle of the burst: its viewport is constructed and
    // destroyed ON the writer thread while these edits are being posted to that same thread.
    if (i == 8) {
      host.add("canvas#2", *doc, &registry, &bridge);
      host.request_resize("canvas#2", k_w, k_h);
    }
    if (i == 16) {
      REQUIRE(source.fire_all() == 1); // the bytes land on the test's thread — a third one
    }
    if (i == 24) {
      host.remove("canvas#2");
    }
    apply_edit(writer, host, [&] { damage(*doc, composition); });
  }

  // `fire_all` delivers synchronously, so `external_loads_ready()` is 1 on return and drops back
  // to 0 exactly when SOME path installs it. It is the one counter here that is any-thread and
  // lock-free, which is why the poll reads it and not `pending_external_loads()`.
  REQUIRE(pump_until([&] { return doc->external_loads_ready() == 0; }));
  REQUIRE(pump_until([&] { return host.live_count() == 1; }));
  // The arrival was installed, and exactly once, by whichever of the three idempotent paths got
  // there first (D-writer_thread-10). The composite under the real interactive pool is asserted
  // below, non-blank (editor.canvas.nested_real_pool): #17 (libarbc v0.4.0) routes the settled
  // child into the compositor, so this is the first real-pool published-pixel read-back in the
  // suite — byte-exact convergence stays owned by the deterministic inline case above.
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 1; }));
  std::uint64_t seq = 0;
  Srgb8Image frame;
  REQUIRE(host.consume("canvas#1", seq, frame));
  CHECK(ace::render::frame_has_content(frame));
  host.stop();
  handle->join();
  writer.set_idle_work({}); // disarm before the host the closure pokes goes out of scope
  const LoadCounters counters = load_counters(writer, *doc);
  CHECK(counters.pending == 0);
  CHECK(static_cast<std::uint64_t>(idle_installs.load()) + host.settles_installed() +
            counters.auto_settled ==
        1);
}

TEST_CASE("canvas_host: an export job renders the live document clean against the writer and the "
          "render loop (cameras.export TSan anchor)") {
  // editor.cameras.export adds the editor's SECOND long-lived auxiliary thread, so its
  // coverage is named rather than inherited. This is the direct test of Constraint 7's
  // claim that export is a PURE READER: a real `WriterThread` + a real `CanvasHost`
  // (real shared WorkerPool, real render thread) over ONE document, while a
  // `platform::Threads`-spawned export job renders that same document through
  // `render::render_document_srgb8` and UI-thread camera edits land throughout.
  //
  // The export side opens no transaction, adds no journal entry and posts NOTHING to
  // the writer — if it did, this case would deadlock or race rather than merely pass.
  ScratchDir scratch;
  ace::platform::NativeFileSystem filesystem;
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);

  auto doc = build_on_writer(writer, [] { return build_raster_doc(); });
  std::vector<arbc::ObjectId> camera_ids;
  writer.submit_sync([&] {
    for (int i = 0; i < 6; ++i) {
      camera_ids.push_back(ace::scene::add_camera(*doc, registry, "shot " + std::to_string(i),
                                                  ace::scene::Resolution{24, 24},
                                                  arbc::Affine::translation(4.0 * i, 4.0)));
    }
  });
  REQUIRE(camera_ids.size() == 6);

  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });
  host.add("canvas#1", *doc, &registry);
  host.request_resize("canvas#1", k_w, k_h);
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 1; }));

  ace::commands::ExportService service(threads, filesystem);
  service.set_shot_camera(&ace::interact::viewport_camera_for_shot);
  service.set_revision([&doc] { return doc->pin()->revision(); });
  service.set_pin([&doc] { return doc->pin(); });
  // The SHIPPED renderer — now #27's caller-pinned path (A20 / export_pinned) — reading a
  // FROZEN version off the export thread while the writer commits new versions.
  service.set_renderer([&doc](const arbc::DocStatePtr& pin, const arbc::Affine& camera, int width,
                              int height, const std::optional<ace::commands::Rgba8>& background) {
    if (!background) {
      return ace::render::render_document_srgb8_pinned(*doc, pin, width, height, camera);
    }
    return ace::render::render_document_srgb8_over_pinned(
        *doc, pin, width, height, camera,
        {background->r, background->g, background->b, background->a});
  });

  ace::commands::ExportOptions options;
  options.destination = scratch.root / "exports";
  REQUIRE(service.start(
      ace::commands::plan_export(*doc, camera_ids, options, service.shot_camera()), options));

  // UI-thread edits landing WHILE the export renders: a rename (a content edit) and a
  // fresh mint (a structural add), both through the writer identity.
  for (int i = 0; i < 24; ++i) {
    apply_edit(writer, host, [&] {
      ace::scene::rename_camera(*doc, registry, camera_ids[i % camera_ids.size()],
                                "shot " + std::to_string(i));
      if (i % 8 == 0) {
        ace::scene::add_camera(*doc, registry, "extra " + std::to_string(i),
                               ace::scene::Resolution{16, 16}, arbc::Affine::identity());
      }
    });
  }

  service.join();
  host.stop();
  handle->join();

  const std::shared_ptr<const ace::commands::ExportReport> report = service.report();
  REQUIRE(report != nullptr);
  CHECK(report->state == ace::commands::ExportState::Finished);
  CHECK(report->written == 6);
  // Every produced file is a real, signature-valid PNG — a racy read would show up as a
  // short or malformed one.
  for (const ace::commands::ExportItemResult& item : report->items) {
    INFO(item.path.string());
    REQUIRE(item.written);
    const std::vector<std::uint8_t> bytes = ace_test::read_file_bytes(item.path);
    REQUIRE(bytes.size() > 8);
    static constexpr std::array<std::uint8_t, 8> k_sig{0x89, 0x50, 0x4E, 0x47,
                                                       0x0D, 0x0A, 0x1A, 0x0A};
    CHECK(std::equal(k_sig.begin(), k_sig.end(), bytes.begin()));
  }
  // D-pinned-2: the flag is exactly the pinned-start/live-end revision comparison — now
  // an informational "the live document moved on" note, never a coherence warning.
  CHECK(report->document_changed_during_export == (report->start_revision != report->end_revision));
}

TEST_CASE("canvas_host: a batch export holds ONE pin across a concurrent writer, byte-identical to "
          "a quiet batch (cameras.export_pinned TSan anchor)") {
  // editor.cameras.export_pinned: the export job holds ONE `DocStatePtr` on the export
  // thread while the writer commits new versions on the writer thread — the lock-free
  // pinned-read contract (A4.1 / issue #23). Two acceptance obligations meet here: (a) NO
  // TSan data-race report (the sanitizer's own verdict on this lane), and (b) the exported
  // bytes match a no-concurrent-writer run — the pin isolates the reader. Both batches use
  // the SAME frozen pin, so the mid-batch commits cannot change a single output byte.
  ScratchDir scratch;
  ace::platform::NativeFileSystem filesystem;
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);

  auto doc = build_on_writer(writer, [] { return build_raster_doc(); });
  std::vector<arbc::ObjectId> camera_ids;
  writer.submit_sync([&] {
    for (int i = 0; i < 4; ++i) {
      camera_ids.push_back(ace::scene::add_camera(*doc, registry, "pin " + std::to_string(i),
                                                  ace::scene::Resolution{24, 24},
                                                  arbc::Affine::translation(4.0 * i, 4.0)));
    }
  });
  REQUIRE(camera_ids.size() == 4);

  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });
  host.add("canvas#1", *doc, &registry);
  host.request_resize("canvas#1", k_w, k_h);
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 1; }));

  // ONE frozen version, shared by both batches (and retained across every writer commit
  // below via the `shared_ptr` refcount the library owns). The export renders THIS version
  // regardless of when the job actually pins, which is what makes the byte-comparison
  // deterministic rather than a timing gamble.
  const arbc::DocStatePtr frozen = doc->pin();
  const auto wire = [&](ace::commands::ExportService& service) {
    service.set_shot_camera(&ace::interact::viewport_camera_for_shot);
    service.set_revision([&doc] { return doc->pin()->revision(); });
    service.set_pin([frozen] { return frozen; });
    service.set_renderer([&doc](const arbc::DocStatePtr& pin, const arbc::Affine& camera, int width,
                                int height, const std::optional<ace::commands::Rgba8>& background) {
      if (!background) {
        return ace::render::render_document_srgb8_pinned(*doc, pin, width, height, camera);
      }
      return ace::render::render_document_srgb8_over_pinned(
          *doc, pin, width, height, camera,
          {background->r, background->g, background->b, background->a});
    });
  };

  // (1) A QUIET batch — no concurrent writer — establishes the reference bytes.
  std::vector<std::vector<std::uint8_t>> quiet_bytes;
  {
    ace::commands::ExportOptions options;
    options.destination = scratch.root / "quiet";
    ace::commands::ExportService service(threads, filesystem);
    wire(service);
    REQUIRE(service.start(
        ace::commands::plan_export(*doc, camera_ids, options, service.shot_camera()), options));
    service.join();
    const std::shared_ptr<const ace::commands::ExportReport> report = service.report();
    REQUIRE(report != nullptr);
    REQUIRE(report->written == 4);
    for (const ace::commands::ExportItemResult& item : report->items) {
      REQUIRE(item.written);
      quiet_bytes.push_back(ace_test::read_file_bytes(item.path));
    }
  }

  // (2) A CONCURRENT batch — the writer commits new versions throughout, while the export
  // holds `frozen`. Renames and mints land on the LIVE document; the frozen render ignores
  // every one of them.
  std::vector<std::vector<std::uint8_t>> live_bytes;
  {
    ace::commands::ExportOptions options;
    options.destination = scratch.root / "concurrent";
    ace::commands::ExportService service(threads, filesystem);
    wire(service);
    REQUIRE(service.start(
        ace::commands::plan_export(*doc, camera_ids, options, service.shot_camera()), options));
    for (int i = 0; i < 24 && service.running(); ++i) {
      apply_edit(writer, host, [&] {
        ace::scene::rename_camera(*doc, registry, camera_ids[i % camera_ids.size()],
                                  "live " + std::to_string(i));
        if (i % 8 == 0) {
          ace::scene::add_camera(*doc, registry, "extra " + std::to_string(i),
                                 ace::scene::Resolution{16, 16}, arbc::Affine::identity());
        }
      });
    }
    service.join();
    const std::shared_ptr<const ace::commands::ExportReport> report = service.report();
    REQUIRE(report != nullptr);
    REQUIRE(report->written == 4);
    for (const ace::commands::ExportItemResult& item : report->items) {
      REQUIRE(item.written);
      live_bytes.push_back(ace_test::read_file_bytes(item.path));
    }
  }

  host.stop();
  handle->join();

  // (b) Byte-identical to the no-concurrent-writer run — the held pin isolated the reader
  // from every mid-batch commit.
  REQUIRE(quiet_bytes.size() == live_bytes.size());
  for (std::size_t i = 0; i < quiet_bytes.size(); ++i) {
    INFO("item " << i);
    REQUIRE(quiet_bytes[i].size() > 8);
    CHECK(quiet_bytes[i] == live_bytes[i]);
  }
}

TEST_CASE("canvas_host: the contact-sheet phase renders the live document clean against the "
          "writer and the render loop (cameras.contact_sheet TSan anchor)") {
  // editor.cameras.contact_sheet adds NO thread — it lengthens the export job with a
  // second phase — so its threading coverage is an extension of the anchor above
  // rather than a new lane. `write_items = false` makes EVERY render in this run a
  // TILE render, so what is under TSan here is exclusively the new phase: N lock-free
  // reads of the live document while a real `WriterThread` commits `add_camera` /
  // `rename_camera` edits from the UI thread and a real `CanvasHost` composites.
  //
  // This is the direct test of Constraint 13's pure-reader claim across the sheet: the
  // sheet phase opens no transaction, adds no journal entry and posts NOTHING to the
  // writer — if it did, this case would deadlock or race rather than merely pass.
  ScratchDir scratch;
  ace::platform::NativeFileSystem filesystem;
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);

  auto doc = build_on_writer(writer, [] { return build_raster_doc(); });
  std::vector<arbc::ObjectId> camera_ids;
  writer.submit_sync([&] {
    for (int i = 0; i < 6; ++i) {
      camera_ids.push_back(ace::scene::add_camera(*doc, registry, "tile " + std::to_string(i),
                                                  ace::scene::Resolution{24, 24},
                                                  arbc::Affine::translation(4.0 * i, 4.0)));
    }
  });
  REQUIRE(camera_ids.size() == 6);

  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });
  host.add("canvas#1", *doc, &registry);
  host.request_resize("canvas#1", k_w, k_h);
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 1; }));

  ace::commands::ExportService service(threads, filesystem);
  service.set_shot_camera(&ace::interact::viewport_camera_for_shot);
  service.set_revision([&doc] { return doc->pin()->revision(); });
  service.set_pin([&doc] { return doc->pin(); });
  service.set_renderer([&doc](const arbc::DocStatePtr& pin, const arbc::Affine& camera, int width,
                              int height, const std::optional<ace::commands::Rgba8>& background) {
    if (!background) {
      return ace::render::render_document_srgb8_pinned(*doc, pin, width, height, camera);
    }
    return ace::render::render_document_srgb8_over_pinned(
        *doc, pin, width, height, camera,
        {background->r, background->g, background->b, background->a});
  });

  ace::commands::ExportOptions options;
  options.destination = scratch.root / "exports";
  options.write_items = false; // sheet ONLY — every render below is a tile render
  options.contact_sheet = true;
  options.tile_edge = 64;
  const ace::commands::ExportPlan plan =
      ace::commands::plan_export(*doc, camera_ids, options, service.shot_camera());
  REQUIRE(plan.items.empty());
  REQUIRE(plan.contact_sheet.has_value());
  REQUIRE(plan.contact_sheet->tiles.size() == 6);
  REQUIRE(service.start(plan, options));

  // UI-thread edits landing WHILE the sheet renders: a rename (a content edit) and a
  // fresh mint (a structural add), both through the writer identity.
  for (int i = 0; i < 24; ++i) {
    apply_edit(writer, host, [&] {
      ace::scene::rename_camera(*doc, registry, camera_ids[i % camera_ids.size()],
                                "tile " + std::to_string(i));
      if (i % 8 == 0) {
        ace::scene::add_camera(*doc, registry, "extra " + std::to_string(i),
                               ace::scene::Resolution{16, 16}, arbc::Affine::identity());
      }
    });
  }

  service.join();
  host.stop();
  handle->join();

  const std::shared_ptr<const ace::commands::ExportReport> report = service.report();
  REQUIRE(report != nullptr);
  CHECK(report->state == ace::commands::ExportState::Finished);
  CHECK(report->items.empty()); // the batch phase planned nothing, so it wrote nothing
  CHECK(report->written == 1);
  REQUIRE(report->contact_sheet.has_value());
  REQUIRE(report->contact_sheet->written);
  // One real, signature-valid PNG on disk — a racy read would show up as a short or
  // malformed one.
  const std::vector<std::uint8_t> bytes = ace_test::read_file_bytes(report->contact_sheet->path);
  REQUIRE(bytes.size() > 8);
  static constexpr std::array<std::uint8_t, 8> k_sig{0x89, 0x50, 0x4E, 0x47,
                                                     0x0D, 0x0A, 0x1A, 0x0A};
  CHECK(std::equal(k_sig.begin(), k_sig.end(), bytes.begin()));
  CHECK(report->contact_sheet->width == plan.contact_sheet->width);
  CHECK(report->contact_sheet->height == plan.contact_sheet->height);
}

TEST_CASE("canvas_host: a stream of UI-thread cell TRANSFORM commits runs clean against the "
          "overview panel's read path (editor.panels.overview TSan anchor)") {
  // editor.panels.overview Acceptance (Threading, Constraint 11): the overview reads the shipped
  // UI-thread scene read-seam every frame — `scene::cells(active)` + `scene::cameras` +
  // `active_composition` + `composition_path` — while a box drag commits a REAL
  // `transform_cells_command` mutation through apply_edit. This drives that whole read path on the
  // UI thread against the live render walk while the transform stream rewrites a layer, so the
  // reads genuinely overlap the writes in wall-clock time. No new lock, no new thread, no new
  // suppression — the writer-identity + lock-free `pin()` seam covers it, exactly as the
  // cells.gizmo / pick_targets anchors above assert. (The navigator's `drive_focused_camera` and
  // `focused_framing` touch ONLY transient per-pane session state — no `Document` access and no
  // writer work — so they carry no cross-thread document hazard and are covered behaviourally by
  // the e2e, not this document-race anchor.)
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  ProbeDocument probe = build_on_writer(writer, [] { return build_probe_document(); });
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);

  // A couple of placed cells plus a camera so the overview's cells+cameras merge is fully walked.
  writer.submit_sync([&] {
    REQUIRE(ace::scene::add_cell(*probe.document, registry, "org.arbc.raster", "16x16",
                                 arbc::Affine::translation(8.0, 8.0))
                .has_value());
    REQUIRE(ace::scene::add_camera(*probe.document, registry, "Hero",
                                   ace::scene::Resolution{32, 24},
                                   arbc::Affine::translation(4.0, 4.0))
                .valid());
  });

  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *probe.document, &registry);
  host.request_resize("canvas#1", k_w, k_h);
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 1; }));

  constexpr int k_gestures = 48;
  arbc::Affine last = arbc::Affine::identity();
  std::size_t last_cells = 0;
  for (int i = 0; i < k_gestures; ++i) {
    const double s = 1.0 + static_cast<double>(i % 8) * 0.25; // a varying, non-degenerate scale
    last = arbc::Affine{s, 0.0, 0.0, s, static_cast<double>(i), static_cast<double>(-i)};
    apply_edit(writer, host, [&] {
      ace::commands::transform_cells_command({ace::commands::LayerTransform{probe.layer, last}})
          .apply(*probe.document);
    });
    // The overview's per-frame read path, overlapping the writer + render reads.
    const arbc::ObjectId active = ace::scene::active_composition(*probe.document, std::nullopt);
    const std::vector<ace::scene::Cell> cells =
        ace::scene::cells(*probe.document, registry, active);
    const std::vector<ace::scene::Camera> cameras = ace::scene::cameras(*probe.document);
    const std::vector<ace::scene::Breadcrumb> path =
        ace::scene::composition_path(*probe.document, registry, std::nullopt);
    last_cells = cells.size();
    (void)cameras;
    (void)path;
  }
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 2; }));

  // The committed placement is stable and finite; the read path saw a consistent snapshot.
  bool found = false;
  for (const ace::interact::PickTarget& tgt :
       ace::interact::pick_targets(*probe.document, registry)) {
    if (tgt.layer == probe.layer) {
      found = true;
      CHECK(tgt.placement == last);
    }
  }
  CHECK(found);
  CHECK(last_cells >= 1);

  host.stop();
  handle->join();
}

TEST_CASE("canvas_host: a stream of UI-thread brush-dab commits under one coalesce key runs clean "
          "against the render + pin read (paint.brush TSan anchor)") {
  // Same writer-outlives-host contract as the cells.gizmo anchor above. What this leaf pins is the
  // editor's first writer-thread MUTATION STREAM (D-brush-3): many small `RasterContent::paint`
  // commits per gesture, ALL stamped with one coalesce key, run through apply_edit on the writer
  // thread while the render thread loops AND the UI thread re-reads the pinned pixels each frame —
  // so both reads genuinely overlap the writes. A FLAT raster paint (no nested render), so the
  // arbc-nested-render-worker-detach-race inline-pool caveat does not apply and the REAL
  // interactive pool is correct. No new lock, no new thread.
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  ProbeDocument probe = build_on_writer(writer, [] { return build_probe_document(); });
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);

  // A bounded raster to paint into, seeded on the writer (the write binds identity). Report its id.
  arbc::ObjectId raster;
  writer.submit_sync([&] {
    const auto added = ace::scene::add_cell(*probe.document, registry, "org.arbc.raster", "32x32",
                                            arbc::Affine::translation(8.0, 8.0));
    if (added.has_value()) {
      raster = *added;
    }
  });
  REQUIRE(raster.valid());

  CanvasHost host(arbc::default_interactive_pool_config(), std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", *probe.document);
  host.request_resize("canvas#1", k_w, k_h);
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 1; }));

  const std::size_t cursor_before = probe.document->journal().cursor();

  // Stream the dab commits: many per-frame paints, ALL under ONE gesture key (a brush stroke), with
  // a pinned pixel read interleaved each round (the UI thread's per-frame read).
  constexpr int k_dabs = 64;
  const std::uint64_t key = 42;
  const arbc::WorkingPixel color{0.0F, 0.0F, 0.0F, 1.0F};
  for (int i = 0; i < k_dabs; ++i) {
    const std::vector<arbc::Vec2> centers{arbc::Vec2{4.0 + static_cast<double>(i % 24), 16.0}};
    apply_edit(writer, host, [&] {
      ace::scene::brush_dab(*probe.document, registry, raster, centers, 2.0, 4.0, color, key);
    });
    auto* r = dynamic_cast<arbc::RasterContent*>(probe.document->resolve(raster));
    if (r != nullptr) {
      (void)r->store().base_table()->pixel(0, 8, 16); // per-frame pin() read overlapping the writes
    }
  }
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 2; }));

  // The whole burst folded to ONE coalesced journal entry (D15), and the final pixels are finite.
  CHECK(probe.document->journal().cursor() == cursor_before + 1);
  auto* raster_content = dynamic_cast<arbc::RasterContent*>(probe.document->resolve(raster));
  REQUIRE(raster_content != nullptr);
  const arbc::WorkingPixel final_px = raster_content->store().base_table()->pixel(0, 8, 16);
  for (const float ch : final_px) {
    CHECK(std::isfinite(ch));
  }

  host.stop();
  handle->join();
}

TEST_CASE("canvas_host: a stream of active-color paints runs clean against the render read "
          "(panels.color TSan anchor)") {
  // editor.panels.color (D-color-1/-6): the UI thread drives `AppState::set_active_color` +
  // `active_working_color()` — the picker→paint path — and feeds the DERIVED `WorkingPixel` into
  // `apply_edit(brush_dab)` on the writer thread while the render thread walks the same document.
  // The color crosses to the writer as a COPIED value (the closure captures `color` by value), so
  // there is no shared mutable color state across threads: the UI thread alone touches
  // `active_color_` (it is blocked inside `submit_sync` while the writer runs), and the
  // writer/render threads never read it. No new lane, no new suppression.
  //
  // The document now also holds a NESTED cell (editor.panels.color_eyedrop_nested), which the
  // render walk composites — so this case renders over the INLINE `WorkerPoolConfig{}` pool per the
  // arbc-nested-render-worker-detach-race memo (a threaded nested render SIGSEGVs the v0.4.0 detach
  // path). The UI-thread nested sampler is unaffected either way: it renders through
  // `render_offline` with `direct_dispatch()` (single-threaded, `offline.cpp:50`), never the
  // interactive pool. The brush-paint into the raster stays a FLAT (no-nested) write, exactly as
  // before.
  //
  // The AppState is BUILT ON the writer thread (the first write binds the document's writer
  // identity for life, D-writer_thread-6), held in an `optional` so the move ctor — not the deleted
  // move assignment — places it, and it is destroyed while the writer still lives (declared before
  // it).
  ace::platform::NativeThreads threads;
  ace::writer::WriterThread writer(threads);
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;

  std::optional<ace::commands::AppState> state_opt;
  arbc::ObjectId raster;
  arbc::ObjectId nested;
  writer.submit_sync([&] {
    auto created = ace::project::create_project(fs, scratch.root / "color");
    if (!created.has_value()) {
      return; // asserted on the main thread below (Catch2 is not thread-safe here)
    }
    state_opt.emplace(std::move(*created));
    ace::commands::AppState& state = *state_opt;
    state.document().add_composition(static_cast<double>(k_w), static_cast<double>(k_h));
    // An opaque backdrop so the pane composites NON-blank content and publishes its first frame.
    ace::scene::add_cell(state.document(), state.registry(), "org.arbc.solid", "0.15,0.2,0.25,1",
                         arbc::Affine::identity());
    const auto added = ace::scene::add_cell(state.document(), state.registry(), "org.arbc.raster",
                                            "32x32", arbc::Affine::translation(8.0, 8.0));
    if (added.has_value()) {
      raster = *added;
    }
    // A NESTED cell (editor.panels.color_eyedrop_nested) in a clear region ([44,60]^2), whose CHILD
    // composition holds a solid. The isolated eyedropper modifier samples it through the anchored
    // `render_offline` branch — a UI-thread pinned read exercised concurrently with the live render
    // below, to prove the nested path is TSan-clean like the leaf/composited samplers.
    const arbc::ObjectId child = state.document().add_composition(16.0, 16.0);
    ace::scene::add_cell(state.document(), state.registry(), "org.arbc.solid", "0.7,0.3,0.5,1",
                         arbc::Affine::identity(), child);
    const auto nested_added =
        ace::scene::add_cell(state.document(), state.registry(), "org.arbc.nested",
                             std::to_string(child.value), arbc::Affine::translation(44.0, 44.0));
    if (nested_added.has_value()) {
      nested = *nested_added;
    }
  });
  REQUIRE(state_opt.has_value());
  REQUIRE(raster.valid());
  REQUIRE(nested.valid());
  ace::commands::AppState& state = *state_opt;

  // The raster's placing layer — the isolated eyedropper modifier samples through it
  // (editor.panels.color_eyedrop_cell). A UI-thread pinned read, taken once here.
  arbc::ObjectId raster_layer;
  arbc::ObjectId nested_layer;
  for (const ace::scene::Cell& cell : ace::scene::cells(state.document(), state.registry())) {
    if (cell.id == raster) {
      raster_layer = cell.layer;
    } else if (cell.id == nested) {
      nested_layer = cell.layer;
    }
  }
  REQUIRE(raster_layer.valid());
  REQUIRE(nested_layer.valid());

  // The INLINE pool (WorkerPoolConfig{}) — the document holds a nested cell the render composites,
  // so the arbc-nested-render-worker-detach-race memo requires it over the real threaded pool.
  CanvasHost host(arbc::WorkerPoolConfig{}, std::chrono::milliseconds(8));
  host.set_writer(&writer);
  std::unique_ptr<ace::platform::JoinHandle> handle =
      threads.spawn([&] { host.run([] { return false; }); });

  host.add("canvas#1", state.document());
  host.request_resize("canvas#1", k_w, k_h);
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 1; }));

  const std::size_t cursor_before = state.document().journal().cursor();

  // Stream the paints: each round the UI thread picks a fresh active color and derives the working
  // pixel, then posts one dab under ONE gesture key (a stroke). The derived color is captured BY
  // VALUE into the writer closure — the cross-thread copy the boundary relies on.
  constexpr int k_dabs = 64;
  const std::uint64_t key = 77;
  for (int i = 0; i < k_dabs; ++i) {
    state.set_active_color(ace::commands::SrgbColor{static_cast<std::uint8_t>(10 + i),
                                                    static_cast<std::uint8_t>(20 + i),
                                                    static_cast<std::uint8_t>(30 + i), 255});
    const arbc::WorkingPixel color = state.active_working_color(); // UI-thread read; copied below
    const std::vector<arbc::Vec2> centers{arbc::Vec2{4.0 + static_cast<double>(i % 24), 16.0}};
    apply_edit(writer, host, [&, color] {
      ace::scene::brush_dab(state.document(), state.registry(), raster, centers, 2.0, 4.0, color,
                            key);
    });
    // The active-cell eyedropper modifier's isolated sampler (editor.panels.color_eyedrop_cell):
    // a pure pinned read on the UI thread, whose `WorkingPixel` crosses to nothing shared, run
    // CONCURRENT with the live render walk to prove it is TSan-clean like the composited sampler —
    // no new lane, no new suppression. On the gate's `nullopt` (never here — a leaf raster) it is a
    // no-op; a value drives `set_active_color`, the same UI-thread-only field the loop already
    // owns.
    if (const std::optional<arbc::WorkingPixel> own = ace::commands::sample_cell_color(
            state.document(), arbc::Affine::identity(), raster_layer, 16.0, 16.0)) {
      state.set_active_color(ace::commands::working_to_srgb(*own));
    }
    // The NESTED-cell branch (editor.panels.color_eyedrop_nested): the same UI-thread pinned read,
    // but through the anchored `render_offline` path — single-threaded via `direct_dispatch()`
    // (`offline.cpp:50`), so it never enters the interactive worker pool and does not touch the
    // worker-detach path the `arbc-nested-render-worker-detach-race` memo warns about. Run
    // CONCURRENT with the live render walk to prove the nested sample is TSan-clean too — the
    // resulting `WorkingPixel` crosses to nothing shared. This is field-for-field the same
    // UI-thread `render_offline`-with-operator-binding the composited sampler already runs over a
    // document that contains a nested cell, so it adds no new concurrency class.
    if (const std::optional<arbc::WorkingPixel> nested_own = ace::commands::sample_cell_color(
            state.document(), arbc::Affine::identity(), nested_layer, 48.0, 48.0)) {
      state.set_active_color(ace::commands::working_to_srgb(*nested_own));
    }
  }
  REQUIRE(pump_until([&] { return host.published_sequence("canvas#1") >= 2; }));

  // The whole burst folded to ONE coalesced journal entry (D15), and the final pixels are finite.
  CHECK(state.document().journal().cursor() == cursor_before + 1);
  auto* raster_content = dynamic_cast<arbc::RasterContent*>(state.document().resolve(raster));
  REQUIRE(raster_content != nullptr);
  const arbc::WorkingPixel final_px = raster_content->store().base_table()->pixel(0, 8, 16);
  for (const float ch : final_px) {
    CHECK(std::isfinite(ch));
  }

  host.stop();
  handle->join();
  // `host` is declared AFTER `state_opt`, so it tears down FIRST (before the document it borrows
  // and the writer identity that document is bound to) — the writer-outlives-host /
  // host-before-document order the sibling anchors rely on.
}
