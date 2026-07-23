#include <ace/render/canvas_host.hpp>
#include <ace/render/canvas_renderer.hpp>
#include <ace/render/render.hpp>
#include <ace/writer/writer_thread.hpp>

#include <arbc/base/transform.hpp>
#include <arbc/runtime/damage_router.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp> // settle_external_loads (the D-writer_thread-10 nudge)
#include <arbc/runtime/interactive.hpp>
#include <arbc/runtime/worker_pool.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ace::render {

namespace {
// The bounded per-frame interactive budget the shipped host round-robins across all
// canvases (D-multi_canvas-3): a deadline-limited slice per entry per cycle, so one
// heavy canvas cannot starve another on the single shared render thread. Unsettled
// entries re-drive next cycle via StepOutcome::schedule_follow_up.
constexpr std::chrono::milliseconds k_default_frame_budget{8};
} // namespace

// One hosted canvas: its own CanvasRenderer (own HostViewport / TileCache / target
// Surface, borrowing the shared pool) + frame_sync's latest-frame double-buffer, reused
// verbatim (D-multi_canvas-1). The render thread owns `renderer`/`back`/`published_frames`
// (touched only in drive_once, off-lock); `published`/`sequence` are guarded by the host
// mutex — the single-producer/single-consumer handoff.
struct CanvasHost::Entry {
  Entry(arbc::Document& doc, arbc::WorkerPool& pool, arbc::DamageRouter& router,
        std::chrono::steady_clock::duration budget, const arbc::Registry* reg,
        arbc::KindBridge* kind_bridge, writer::WriterThread* writer)
      : renderer(doc, pool, router, budget, reg, kind_bridge, writer), document(&doc),
        registry(reg), bridge(kind_bridge) {}

  CanvasRenderer renderer;
  // What the D-writer_thread-10 async settle nudge needs, and nothing more: the document, the
  // document-scoped bridge and the registry. No render-thread state crosses into that closure.
  arbc::Document* document = nullptr;
  const arbc::Registry* registry = nullptr;
  arbc::KindBridge* bridge = nullptr;
  // One deduped wake in flight at a time: set before posting, cleared when the closure runs, so a
  // still-unsettled arrival re-nudges on the next frame instead of queueing one settle per frame.
  // shared_ptr because the entry may be removed and destroyed before the posted closure runs.
  std::shared_ptr<std::atomic<bool>> settle_in_flight = std::make_shared<std::atomic<bool>>(false);
  Srgb8Image published;                     // front buffer the consumer reads (guarded)
  Srgb8Image back;                          // producer scratch (render thread, off-lock)
  std::uint64_t sequence = 0;               // published-frame sequence (guarded)
  std::uint64_t published_frames = 0;       // frames_issued() last published (render thread)
  bool content_published = false;           // any content frame published? (render thread;
                                            // once-only content gate, blank_first_frame)
  std::atomic<std::size_t> anchor_depth{0}; // deep-zoom depth, UI reads lock-free (D-nav-5)
};

struct CanvasHost::Impl {
  Impl(arbc::WorkerPoolConfig pool_config, std::chrono::steady_clock::duration frame_budget)
      : budget(frame_budget), pool(std::move(pool_config)) {}

  ~Impl();

  struct PendingAdd {
    std::string id;
    arbc::Document* document;
    const arbc::Registry* registry; // the app's persistent kind Registry (may be null)
    arbc::KindBridge* bridge;       // the document-scoped bridge (D-writer_thread-9; may be null)
  };

  std::chrono::steady_clock::duration budget;

  // Declared before `entries`/`routers` so it OUTLIVES every borrowing renderer: entries
  // tear down first, draining into this pool, then the pool joins its workers (Constraint 2).
  arbc::WorkerPool pool;

  // One DamageRouter per distinct Document, occupying that Model's single set_damage_sink
  // slot and fanning a commit's damage out to EVERY canvas over the document. Declared
  // before `entries` so the router outlives every viewport registered with it (the router
  // dtor asserts an empty registrant list). Lazily created when the document's first entry
  // is added; kept for the document's lifetime (an empty router is inert).
  std::map<arbc::Document*, std::unique_ptr<arbc::DamageRouter>> routers;

  mutable std::mutex mu;
  std::condition_variable cv;
  bool stop = false;  // stop requested (UI thread -> render thread)
  bool dirty = false; // work pending: an add/remove/resize/poke/self-signal

  // The document's ONE writer thread (borrowed, may be null == "the caller is the identity";
  // D-writer_thread-8). The writer-priority document lease this host used to hold is GONE, not
  // renamed (D-writer_thread-11 / Constraint 4): both races it guarded are fixed in libarbc
  // v0.3.0 (the DamageAccumulator carries its own mutex; step() no longer publishes off the
  // writer thread), and the remaining problem was identity, which a lock cannot fix. Reads stay
  // lock-free; writes are posted here.
  writer::WriterThread* writer = nullptr;

  // Run `work` on the writer thread and BLOCK. Called from the render thread (and, at teardown,
  // from the owner's thread) — NEVER while holding `mu`: the posted settle nudge calls back into
  // poke(), which takes `mu`, so posting under it would deadlock. With no writer bound the caller
  // IS the one identity and it runs inline. False == the writer refused (D-writer_thread-6).
  bool on_writer(const std::function<void()>& work) {
    if (writer == nullptr) {
      work();
      return true;
    }
    return writer->submit_sync(work);
  }

  // UI-thread lifecycle requests, serviced on the render thread at the top of a drive
  // iteration so every cache stays render-thread-confined (Constraint 3).
  std::vector<PendingAdd> pending_adds;
  std::vector<std::string> pending_removes;
  std::map<std::string, std::pair<int, int>, std::less<>> pending_resizes;
  std::map<std::string, arbc::Affine, std::less<>> pending_cameras; // per-entry camera submits

  std::map<std::string, std::unique_ptr<Entry>, std::less<>> entries;

  // Wake the render loop (the fan-out poke). Split out of CanvasHost::poke so the writer-thread
  // settle nudge can call it without going through the public API.
  void wake() {
    {
      std::lock_guard<std::mutex> lock(mu);
      dirty = true;
    }
    cv.notify_all();
  }

  // The render thread's arrival NUDGE (D-writer_thread-10): the step saw arrivals it could not
  // install (it is not the writer thread), so post ONE async settle. Async because the render
  // thread must never block per frame on an arrival (Constraint 3); deduped by an in-flight flag
  // so a still-unsettled arrival re-nudges next frame rather than queueing one settle per frame.
  // This only SHORTENS latency to the observing frame — the writer's own idle poll and arbc's
  // auto-settler at the next begin() reach the same install, and all three are idempotent (the
  // settle drains the ready queue; re-entry is suppressed by arbc's InstallScope).
  void nudge_settle(Entry& entry) {
    if (writer == nullptr || entry.bridge == nullptr || entry.registry == nullptr) {
      return; // the empty binding / single-threaded fixtures: the step settled it itself
    }
    if (entry.settle_in_flight->exchange(true)) {
      return; // one wake already queued
    }
    const std::shared_ptr<std::atomic<bool>> flag = entry.settle_in_flight;
    arbc::Document* document = entry.document;
    arbc::KindBridge* bridge = entry.bridge;
    const arbc::Registry* reg = entry.registry;
    if (!writer->submit([this, document, bridge, reg, flag] {
          const std::size_t installed = arbc::settle_external_loads(*document, *bridge, *reg);
          settles_installed.fetch_add(installed, std::memory_order_relaxed);
          flag->store(false);
          wake(); // the install publishes a revision and flushes damage — re-render it
        })) {
      flag->store(false); // the writer is stopped; nothing to settle for a dying document
    }
  }

  std::atomic<std::uint64_t> iterations{0};
  // Arrivals the nudges above INSTALLED (behavioral counter): written on the writer thread, read
  // from anywhere.
  std::atomic<std::uint64_t> settles_installed{0};
};

CanvasHost::Impl::~Impl() {
  // Runs on the owner's thread AFTER the render thread has been stopped and joined, so nothing
  // posts here except us. Two obligations, in order:
  //
  //  1. FIFO barrier. A nudge posted by the last driven iteration may still be queued behind us;
  //     one sync submission flushes everything ahead of it (D-writer_thread-2's total order), so
  //     no closure capturing `this` outlives this object.
  //  2. Tear down the WRITER-THREAD-ONLY state in dependency order: every entry (each renderer's
  //     HostViewport releases the settler slot and unregisters from its router, posted inside
  //     CanvasRenderer) and then the routers themselves (~DamageRouter clears the model's damage
  //     slot and asserts an empty registrant list). A refused post means the writer is already
  //     stopped AND joined, so the slots are quiescent — a destructor cannot skip, and running
  //     inline against a joined writer cannot mint a live second identity.
  if (writer != nullptr) {
    writer->submit_sync([] {});
  }
  entries.clear();
  for (auto& [document, router] : routers) {
    if (!router) {
      continue;
    }
    if (!on_writer([&router] { router.reset(); })) {
      router.reset();
    }
  }
  routers.clear();
}

CanvasHost::CanvasHost()
    : impl_(std::make_unique<Impl>(arbc::default_interactive_pool_config(),
                                   k_default_frame_budget)) {}

CanvasHost::CanvasHost(arbc::WorkerPoolConfig pool_config,
                       std::chrono::steady_clock::duration frame_budget)
    : impl_(std::make_unique<Impl>(std::move(pool_config), frame_budget)) {}

CanvasHost::~CanvasHost() = default;

void CanvasHost::set_writer(writer::WriterThread* writer) { impl_->writer = writer; }

void CanvasHost::add(std::string id, arbc::Document& document, const arbc::Registry* registry,
                     arbc::KindBridge* bridge) {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->pending_adds.push_back(Impl::PendingAdd{std::move(id), &document, registry, bridge});
    impl_->dirty = true;
  }
  impl_->cv.notify_all();
}

void CanvasHost::remove(std::string_view id) {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->pending_removes.emplace_back(id);
    impl_->dirty = true;
  }
  impl_->cv.notify_all();
}

void CanvasHost::request_resize(std::string_view id, int width, int height) {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->pending_resizes[std::string(id)] = {width, height};
    impl_->dirty = true;
  }
  impl_->cv.notify_all();
}

void CanvasHost::request_camera(std::string_view id, const arbc::Affine& camera) {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->pending_cameras[std::string(id)] = camera;
    impl_->dirty = true;
  }
  impl_->cv.notify_all();
}

void CanvasHost::poke() { impl_->wake(); }

void CanvasHost::stop() {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->stop = true;
  }
  impl_->cv.notify_all();
}

bool CanvasHost::drive_once() {
  struct DriveItem {
    Entry* entry;
    bool do_resize;
    int width;
    int height;
    bool do_camera;
    arbc::Affine camera;
  };
  std::vector<DriveItem> items;
  // Entries removed this iteration destruct AFTER the lock is released (a
  // ~InteractiveRenderer drain into the shared pool can block; never hold `mu` for it).
  std::vector<std::unique_ptr<Entry>> dying;

  // No document lease any more (D-writer_thread-11): the render walk's read is lock-free, and
  // the two races the lease guarded are library-fixed at the v0.3.0 pin. `mu` is still taken for
  // the pending-request snapshot, the entry-map mutation and the publish swap.
  //
  // CRITICAL: nothing in this function posts to the writer thread while holding `mu`. The posted
  // settle nudge calls back into wake(), which takes `mu`, so a sync post under it would
  // deadlock. The DamageRouter creation (step 1) and the per-entry HostViewport rebuild/teardown
  // (steps 2 and 4, inside CanvasRenderer) are therefore all done OFF the lock.

  // 1. Service adds. `entries`/`routers` are mutated only here, on the render thread, so reading
  //    them off-lock is sound (other threads only ever READ `entries`, under `mu`); the
  //    entry-map INSERT is taken back under the lock at the end. The document's DamageRouter is
  //    created on first use so every canvas over that document shares the one sink slot
  //    (fan-out) rather than each viewport evicting the last — and its ctor occupies
  //    `Model::set_damage_sink`, a WRITER-THREAD-ONLY slot, so it is POSTED (D-writer_thread-8).
  std::vector<Impl::PendingAdd> adds;
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    adds.swap(impl_->pending_adds);
  }
  std::vector<std::pair<std::string, std::unique_ptr<Entry>>> fresh;
  for (Impl::PendingAdd& pending : adds) {
    if (impl_->entries.find(pending.id) != impl_->entries.end()) {
      continue; // idempotent: a second add with a live id is a no-op
    }
    std::unique_ptr<arbc::DamageRouter>& router = impl_->routers[pending.document];
    if (!router) {
      arbc::Document* document = pending.document;
      std::unique_ptr<arbc::DamageRouter> made;
      if (!impl_->on_writer([&] { made = std::make_unique<arbc::DamageRouter>(*document); })) {
        continue; // the writer refused: the document is going away, so serve no new canvas
      }
      router = std::move(made);
    }
    fresh.emplace_back(pending.id, std::make_unique<Entry>(*pending.document, impl_->pool, *router,
                                                           impl_->budget, pending.registry,
                                                           pending.bridge, impl_->writer));
  }

  {
    std::lock_guard<std::mutex> lock(impl_->mu);

    for (auto& [id, entry] : fresh) {
      impl_->entries.emplace(id, std::move(entry));
    }

    // 2. Service removes (extract, erase, destruct off-lock below).
    for (const std::string& id : impl_->pending_removes) {
      auto it = impl_->entries.find(id);
      if (it != impl_->entries.end()) {
        dying.push_back(std::move(it->second));
        impl_->entries.erase(it);
      }
      impl_->pending_resizes.erase(id);
      impl_->pending_cameras.erase(id);
    }
    impl_->pending_removes.clear();

    // 3. Snapshot the live set + CONSUME each live entry's pending resize/camera. Only the
    //    requests actually applied are erased: a request for an id that is not live YET stays
    //    QUEUED. `add` and `request_resize` are two separate UI-thread calls, so an iteration can
    //    land between them — swapping the pending adds above, then arriving here with the entry
    //    still absent. Clearing the whole map there dropped that resize permanently, and an entry
    //    that never receives its size stays zero-area: it builds no viewport, issues no frame and
    //    publishes no sequence, i.e. a silent and PERMANENT stall (the intermittent gcc-tsan hang
    //    in the edit_render_sync anchor). No extra wakeup is owed — the add that creates the entry
    //    sets `dirty` itself, so the deferred request applies on the iteration that services it.
    //    A request for a REMOVED id is erased by step 2 above; one for an id never added is inert
    //    and bounded (both maps are keyed by id, so a repeat overwrites rather than accumulates).
    items.reserve(impl_->entries.size());
    for (auto& [id, entry] : impl_->entries) {
      auto rit = impl_->pending_resizes.find(id);
      const bool do_resize = rit != impl_->pending_resizes.end();
      auto cit = impl_->pending_cameras.find(id);
      const bool do_camera = cit != impl_->pending_cameras.end();
      items.push_back(DriveItem{entry.get(), do_resize, do_resize ? rit->second.first : 0,
                                do_resize ? rit->second.second : 0, do_camera,
                                do_camera ? cit->second : arbc::Affine::identity()});
      if (do_resize) {
        impl_->pending_resizes.erase(rit);
      }
      if (do_camera) {
        impl_->pending_cameras.erase(cit);
      }
    }
  }

  impl_->iterations.fetch_add(1, std::memory_order_relaxed);

  bool more_pending = false;
  // 4. Off-lock: resize + step + publish each entry. The pointers stay valid — only the
  //    render thread mutates the map, and it does so only under the lock above.
  for (const DriveItem& item : items) {
    Entry* entry = item.entry;
    if (item.do_resize &&
        (item.width != entry->renderer.width() || item.height != entry->renderer.height())) {
      entry->renderer.resize(item.width, item.height);
      // The reconstructed viewport restarts frame numbering at 0, so re-key the still-scene
      // early-out (reset published_frames to 0) — the first frame at a new size always
      // publishes, even when both sizes settle at the same frames_issued() count. The
      // content gate does NOT re-arm here (content_published is once-only): a resize after
      // the canvas has shown the scene publishes its transient frames normally, as before.
      entry->published_frames = 0;
    }
    // Apply the submitted camera after any resize (the renderer holds it, so the resize
    // already reframed with the current camera); a camera change is device damage
    // (editor.canvas.nav / D-nav-3).
    if (item.do_camera) {
      entry->renderer.set_camera(item.camera);
    }

    // A bounded step: schedule_follow_up means the budget did not settle the frame, so
    // this entry must be re-driven next cycle (D-multi_canvas-3).
    if (entry->renderer.step()) {
      more_pending = true;
    }
    // The step landed arrivals it declined to install, because it is not the writer thread
    // (arbc#13): post one deduped async settle (D-writer_thread-10). Never blocks the frame.
    if (entry->renderer.external_loads_ready() > 0) {
      impl_->nudge_settle(*entry);
    }
    // Snapshot deep-zoom anchor depth for the UI's lock-free observability read (D-nav-5).
    entry->anchor_depth.store(entry->renderer.anchor_depth(), std::memory_order_relaxed);

    const std::uint64_t frames = entry->renderer.frames_issued();
    // A still scene / zero-area pane issues no new frame — publish nothing (Constraint 4).
    if (frames == 0 || frames == entry->published_frames) {
      continue;
    }

    // Copy the settled image off-lock, then publish under a short lock via a full-buffer
    // swap — a torn frame is never observable and the consumer never aliases the
    // producer's buffers (frame_sync's double-buffer, per entry).
    entry->back = entry->renderer.image();
    // Withhold the sequence until the FIRST frame composites non-empty tile content: under
    // the bounded per-frame budget the first step() can issue frame 1 before any tile
    // resolved, so advancing entry->sequence for that blank frame would make a frame-count
    // settle heuristic fire on empty (editor.canvas.blank_first_frame, D-blank_first_frame-1).
    // `content_published` is a once-only latch (NOT re-keyed on resize): it gates only the
    // very first content frame, so once the canvas has shown the scene every later frame —
    // partial refinements AND the transient frames after a resize — publishes normally.
    // Loop liveness through the initial blank phase is preserved by step()'s work-in-flight
    // return (more_pending, set at the step() call above): a still-resolving canvas keeps
    // driving until its first content frame composites and publishes; a genuinely blank/empty
    // document leaves nothing pending, so step() returns false and the loop idles at sequence
    // 0 — no busy-spin (D-blank_first_frame-3, as corrected: schedule_follow_up alone does not
    // witness an in-flight tile).
    if (!entry->content_published && !frame_has_content(entry->back)) {
      continue;
    }
    entry->content_published = true;
    entry->published_frames = frames;

    {
      std::lock_guard<std::mutex> lock(impl_->mu);
      std::swap(entry->published, entry->back);
      ++entry->sequence;
    }
    more_pending = true;
  }
  // `dying` destructs here, off-lock.
  return more_pending;
}

void CanvasHost::run(const std::function<bool()>& should_stop) {
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(impl_->mu);
      impl_->cv.wait(lock,
                     [&] { return impl_->stop || impl_->dirty || (should_stop && should_stop()); });
      if (impl_->stop || (should_stop && should_stop())) {
        return;
      }
      impl_->dirty = false;
    }
    // No lease around the iteration any more (D-writer_thread-11): the render walk reads the
    // document lock-free (`pin()` + the copy-on-write binding table), the DamageAccumulator
    // carries its own mutex since the v0.3.0 pin, and `step()` declines to publish off the
    // writer thread — so nothing here needs mutual exclusion against an edit. What the
    // iteration DOES owe the writer, it posts (D-writer_thread-8/10).
    const bool more_pending = drive_once();
    if (more_pending) {
      // A frame settled or an entry is unsettled under its bounded budget; re-arm so the
      // next iteration re-checks rather than sleeping through the tail (Constraint 4c /
      // D-multi_canvas-3). A subsequent all-still iteration publishes nothing and idles.
      std::lock_guard<std::mutex> lock(impl_->mu);
      impl_->dirty = true;
    }
  }
}

bool CanvasHost::consume(std::string_view id, std::uint64_t& last_seq, Srgb8Image& out) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  auto it = impl_->entries.find(id);
  if (it == impl_->entries.end()) {
    return false; // an unknown / removed id is no longer served
  }
  Entry& entry = *it->second;
  if (entry.sequence == last_seq) {
    return false; // no frame newer than the caller last observed
  }
  // MOVE the front buffer out under the short lock (a bounded pointer steal). The
  // consumer takes exclusive ownership; the next publish fills a fresh front.
  out = std::move(entry.published);
  last_seq = entry.sequence;
  return true;
}

std::uint64_t CanvasHost::published_sequence(std::string_view id) const {
  std::lock_guard<std::mutex> lock(impl_->mu);
  auto it = impl_->entries.find(id);
  return it == impl_->entries.end() ? 0 : it->second->sequence;
}

std::size_t CanvasHost::anchor_depth(std::string_view id) const {
  std::lock_guard<std::mutex> lock(impl_->mu);
  auto it = impl_->entries.find(id);
  return it == impl_->entries.end() ? 0 : it->second->anchor_depth.load(std::memory_order_relaxed);
}

std::size_t CanvasHost::live_count() const {
  std::lock_guard<std::mutex> lock(impl_->mu);
  return impl_->entries.size();
}

std::uint64_t CanvasHost::iterations() const {
  return impl_->iterations.load(std::memory_order_relaxed);
}

std::uint64_t CanvasHost::settles_installed() const {
  return impl_->settles_installed.load(std::memory_order_relaxed);
}

const arbc::WorkerPool& CanvasHost::worker_pool() const { return impl_->pool; }

const arbc::WorkerPool* CanvasHost::entry_pool(std::string_view id) const {
  std::lock_guard<std::mutex> lock(impl_->mu);
  auto it = impl_->entries.find(id);
  return it == impl_->entries.end() ? nullptr : it->second->renderer.borrowed_pool();
}

} // namespace ace::render
