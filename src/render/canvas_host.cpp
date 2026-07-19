#include <ace/render/canvas_host.hpp>
#include <ace/render/canvas_renderer.hpp>
#include <ace/render/render.hpp>

#include <arbc/base/transform.hpp>
#include <arbc/runtime/damage_router.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/interactive.hpp>
#include <arbc/runtime/worker_pool.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
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
        std::chrono::steady_clock::duration budget)
      : renderer(doc, pool, router, budget) {}

  CanvasRenderer renderer;
  Srgb8Image published;                     // front buffer the consumer reads (guarded)
  Srgb8Image back;                          // producer scratch (render thread, off-lock)
  std::uint64_t sequence = 0;               // published-frame sequence (guarded)
  std::uint64_t published_frames = 0;       // frames_issued() last published (render thread)
  std::atomic<std::size_t> anchor_depth{0}; // deep-zoom depth, UI reads lock-free (D-nav-5)
};

struct CanvasHost::Impl {
  Impl(arbc::WorkerPoolConfig pool_config, std::chrono::steady_clock::duration frame_budget)
      : budget(frame_budget), pool(std::move(pool_config)) {}

  struct PendingAdd {
    std::string id;
    arbc::Document* document;
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

  // Guards every render-thread document ACCESS (the per-frame read in step(), plus the
  // resize / camera / cache-construction reads) against a UI-thread document MUTATION.
  // arbc::Document is single-writer (SlotStore binds the writer thread on first write) and
  // has NO internal lock, yet the render thread walks the live document each frame
  // (bind_operators iterates its content) — so a UI-thread edit must be mutually excluded
  // with that read. The writer stays the UI thread (apply_edit runs on the caller);
  // doc_mu only serializes it against the render thread's read window. Always taken OUTER
  // to `mu` on the render thread; UI paths take at most one of the two, never nested.
  std::mutex doc_mu;

  // UI-thread lifecycle requests, serviced on the render thread at the top of a drive
  // iteration so every cache stays render-thread-confined (Constraint 3).
  std::vector<PendingAdd> pending_adds;
  std::vector<std::string> pending_removes;
  std::map<std::string, std::pair<int, int>, std::less<>> pending_resizes;
  std::map<std::string, arbc::Affine, std::less<>> pending_cameras; // per-entry camera submits

  std::map<std::string, std::unique_ptr<Entry>, std::less<>> entries;

  std::atomic<std::uint64_t> iterations{0};
};

CanvasHost::CanvasHost()
    : impl_(std::make_unique<Impl>(arbc::default_interactive_pool_config(),
                                   k_default_frame_budget)) {}

CanvasHost::CanvasHost(arbc::WorkerPoolConfig pool_config,
                       std::chrono::steady_clock::duration frame_budget)
    : impl_(std::make_unique<Impl>(std::move(pool_config), frame_budget)) {}

CanvasHost::~CanvasHost() = default;

void CanvasHost::add(std::string id, arbc::Document& document) {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->pending_adds.push_back(Impl::PendingAdd{std::move(id), &document});
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

void CanvasHost::apply_edit(const std::function<void()>& edit) {
  // Run the mutation on the CALLING thread (the writer — SlotStore binds the writer thread
  // lazily on first write, so the document's writer must stay one consistent thread), but
  // under doc_mu so it cannot overlap the render thread's per-frame document read.
  {
    std::lock_guard<std::mutex> edit_lock(impl_->doc_mu);
    edit();
  }
  // Then wake the loop to re-render every live entry over the mutated document (one writer,
  // N observers — Constraint 4); the edit's damage already fanned out via the DamageRouter.
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->dirty = true;
  }
  impl_->cv.notify_all();
}

void CanvasHost::poke() {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->dirty = true;
  }
  impl_->cv.notify_all();
}

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

  // Hold doc_mu across the whole iteration: every document access below (cache
  // construction, resize/camera, step()'s per-frame read) is thereby mutually excluded
  // with a UI-thread apply_edit mutation. Taken OUTER to `mu` (which is re-acquired for the
  // snapshot and the publish swap); apply_edit takes only doc_mu, so the orders never
  // cross and cannot deadlock.
  std::lock_guard<std::mutex> doc_lock(impl_->doc_mu);

  {
    std::lock_guard<std::mutex> lock(impl_->mu);

    // 1. Service adds (construct the render-thread-confined cache here, Constraint 3). The
    //    document's DamageRouter is created on first use so every canvas over that document
    //    shares the one sink slot (fan-out), rather than each viewport evicting the last.
    for (Impl::PendingAdd& pending : impl_->pending_adds) {
      if (impl_->entries.find(pending.id) == impl_->entries.end()) {
        std::unique_ptr<arbc::DamageRouter>& router = impl_->routers[pending.document];
        if (!router) {
          router = std::make_unique<arbc::DamageRouter>(*pending.document);
        }
        impl_->entries.emplace(pending.id, std::make_unique<Entry>(*pending.document, impl_->pool,
                                                                   *router, impl_->budget));
      }
    }
    impl_->pending_adds.clear();

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

    // 3. Snapshot the live set + apply any pending resize/camera (dropping unknown ids).
    items.reserve(impl_->entries.size());
    for (auto& [id, entry] : impl_->entries) {
      auto rit = impl_->pending_resizes.find(id);
      const bool do_resize = rit != impl_->pending_resizes.end();
      auto cit = impl_->pending_cameras.find(id);
      const bool do_camera = cit != impl_->pending_cameras.end();
      items.push_back(DriveItem{entry.get(), do_resize, do_resize ? rit->second.first : 0,
                                do_resize ? rit->second.second : 0, do_camera,
                                do_camera ? cit->second : arbc::Affine::identity()});
    }
    impl_->pending_resizes.clear();
    impl_->pending_cameras.clear();
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
      // The reconstructed viewport restarts frame numbering at 0, so re-key the publish
      // gate to guarantee the first frame at the new size publishes.
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
    // Snapshot deep-zoom anchor depth for the UI's lock-free observability read (D-nav-5).
    entry->anchor_depth.store(entry->renderer.anchor_depth(), std::memory_order_relaxed);

    const std::uint64_t frames = entry->renderer.frames_issued();
    // A still scene / zero-area pane issues no new frame — publish nothing (Constraint 4).
    if (frames == 0 || frames == entry->published_frames) {
      continue;
    }
    entry->published_frames = frames;

    // Copy the settled image off-lock, then publish under a short lock via a full-buffer
    // swap — a torn frame is never observable and the consumer never aliases the
    // producer's buffers (frame_sync's double-buffer, per entry).
    entry->back = entry->renderer.image();
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
    if (drive_once()) {
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

const arbc::WorkerPool& CanvasHost::worker_pool() const { return impl_->pool; }

const arbc::WorkerPool* CanvasHost::entry_pool(std::string_view id) const {
  std::lock_guard<std::mutex> lock(impl_->mu);
  auto it = impl_->entries.find(id);
  return it == impl_->entries.end() ? nullptr : it->second->renderer.borrowed_pool();
}

} // namespace ace::render
