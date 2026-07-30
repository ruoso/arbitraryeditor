#include <ace/render/canvas_host.hpp>
#include <ace/render/canvas_renderer.hpp>
#include <ace/render/render.hpp>
#include <ace/writer/writer_thread.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/runtime/damage_router.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp> // settle_external_loads (the D-writer_thread-10 nudge)
#include <arbc/runtime/interactive.hpp>
#include <arbc/runtime/worker_pool.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
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
  bool stop = false; // stop requested (UI thread -> render thread)

  // The document's ONE writer thread (borrowed, may be null == "the caller is the identity";
  // D-writer_thread-8). The writer-priority document lease this host used to hold is GONE, not
  // renamed (D-writer_thread-11 / Constraint 4): both races it guarded are fixed in libarbc
  // v0.3.0 (the DamageAccumulator carries its own mutex; step() no longer publishes off the
  // writer thread), and the remaining problem was identity, which a lock cannot fix. Reads stay
  // lock-free; writes are posted here.
  writer::WriterThread* writer = nullptr;

  // Run `work` on the writer thread and BLOCK. Called from the render thread (and, at teardown,
  // from the owner's thread) — NEVER while holding `mu`: `submit_sync` blocks the caller until the
  // writer runs the closure, so posting under the host lock would hold `mu` across a cross-thread
  // round-trip. With no writer bound the caller IS the one identity and it runs inline. False ==
  // the writer refused (D-writer_thread-6).
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
  // per-entry isolation-scope submits (editor.canvas.isolation_scope): the entered composition or
  // nullopt (Root). One value slot beside pending_cameras, under the same lock — no new primitive.
  std::map<std::string, std::optional<arbc::ObjectId>, std::less<>> pending_scopes;

  std::map<std::string, std::unique_ptr<Entry>, std::less<>> entries;

  // TEST-ONLY drive-phase seam (D-pending_removes_order-3): fired once between the
  // pending-adds swap and the entry-map lock so a headless fixture can post an add+remove in
  // that otherwise-unreachable window. Null and untouched in the app; set only by the inline
  // single-threaded unit fixtures before any thread spawns.
  std::function<void()> after_adds_swap_hook;

  // Wake the render loop parked in run()'s WorkerPool::wait_completions — the single fan-out poke.
  // Split out of CanvasHost::poke so the writer-thread settle nudge (nudge_settle) can call it
  // without going through the public API. The loop's ONE wait is the shared pool's completion
  // substrate (D-render_loop_liveness_wake-1), so a host event wakes it exactly as a worker's
  // tile-completion poke() does: bump the settle generation and broadcast. Never blocks, takes no
  // host lock.
  void wake() { pool.poke(); }

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

void CanvasHost::set_after_adds_swap_hook(std::function<void()> hook) {
  impl_->after_adds_swap_hook = std::move(hook);
}

void CanvasHost::add(std::string id, arbc::Document& document, const arbc::Registry* registry,
                     arbc::KindBridge* bridge) {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->pending_adds.push_back(Impl::PendingAdd{std::move(id), &document, registry, bridge});
  }
  impl_->wake();
}

void CanvasHost::remove(std::string_view id) {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->pending_removes.emplace_back(id);
  }
  impl_->wake();
}

void CanvasHost::request_resize(std::string_view id, int width, int height) {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->pending_resizes[std::string(id)] = {width, height};
  }
  impl_->wake();
}

void CanvasHost::request_camera(std::string_view id, const arbc::Affine& camera) {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->pending_cameras[std::string(id)] = camera;
  }
  impl_->wake();
}

void CanvasHost::request_scope(std::string_view id, std::optional<arbc::ObjectId> entered) {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->pending_scopes[std::string(id)] = entered;
  }
  impl_->wake();
}

void CanvasHost::poke() { impl_->wake(); }

void CanvasHost::stop() {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->stop = true;
  }
  // Poke the completion substrate (via the fan-out wake) so a run() parked in wait_completions
  // returns promptly, then join (Constraint 4).
  impl_->wake();
}

bool CanvasHost::drive_once() {
  struct DriveItem {
    Entry* entry;
    bool do_resize;
    int width;
    int height;
    bool do_camera;
    arbc::Affine camera;
    bool do_scope;
    std::optional<arbc::ObjectId> scope;
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
  // TEST-ONLY (D-pending_removes_order-3): off-lock between the swap and the entry-map lock,
  // the single-threaded window where a fixture can post the add+remove interleave that a
  // remove must pre-empt. Null and skipped in the app.
  if (impl_->after_adds_swap_hook) {
    impl_->after_adds_swap_hook();
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

    // 2. Service removes (extract, erase, destruct off-lock below). Per removed id, reconcile
    //    the entries map AND every queue it can name — symmetric with the per-id resize/camera
    //    discipline in step 3 (D-pending_removes_order-1/2). The pending_adds erase is what
    //    closes the drop window: an add(id) posted AFTER this iteration's swap sits in
    //    pending_adds now, invisible to the entries map; without cancelling it the remove finds
    //    no live entry, does nothing, and that queued add resurrects the removed canvas next
    //    iteration. A same-iteration add is instead pre-empted above — inserted into `entries`
    //    by the fresh loop, then extracted into `dying` here. A remove has nothing to wait for
    //    (unlike a deferred resize, which a later add gives meaning): an id that is neither
    //    live, nor a queued add, nor a queued resize/camera is a silent no-op, matching add's
    //    idempotence — and a stale remove is never retained to poison a genuinely-new later add.
    for (const std::string& id : impl_->pending_removes) {
      auto it = impl_->entries.find(id);
      if (it != impl_->entries.end()) {
        dying.push_back(std::move(it->second));
        impl_->entries.erase(it);
      }
      std::erase_if(impl_->pending_adds,
                    [&id](const Impl::PendingAdd& add) { return add.id == id; });
      impl_->pending_resizes.erase(id);
      impl_->pending_cameras.erase(id);
      impl_->pending_scopes.erase(id);
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
    //    wakes the loop itself (wake()'s poke), so the deferred request applies on the iteration
    //    that services it.
    //    A request for a REMOVED id is erased by step 2 above; one for an id never added is inert
    //    and bounded (both maps are keyed by id, so a repeat overwrites rather than accumulates).
    items.reserve(impl_->entries.size());
    for (auto& [id, entry] : impl_->entries) {
      auto rit = impl_->pending_resizes.find(id);
      const bool do_resize = rit != impl_->pending_resizes.end();
      auto cit = impl_->pending_cameras.find(id);
      const bool do_camera = cit != impl_->pending_cameras.end();
      auto sit = impl_->pending_scopes.find(id);
      const bool do_scope = sit != impl_->pending_scopes.end();
      items.push_back(DriveItem{entry.get(), do_resize, do_resize ? rit->second.first : 0,
                                do_resize ? rit->second.second : 0, do_camera,
                                do_camera ? cit->second : arbc::Affine::identity(), do_scope,
                                do_scope ? sit->second : std::optional<arbc::ObjectId>{}});
      if (do_resize) {
        impl_->pending_resizes.erase(rit);
      }
      if (do_camera) {
        impl_->pending_cameras.erase(cit);
      }
      if (do_scope) {
        impl_->pending_scopes.erase(sit);
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
    // Apply the submitted isolation scope (editor.canvas.isolation_scope / D-isolation_scope-3): a
    // value change bumps the renderer's frame version so the still-scene canvas re-publishes the
    // (un)dimmed frame this cycle. One channel slot beside the camera, same discipline (A5).
    if (item.do_scope) {
      entry->renderer.set_scope(item.scope);
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
  // The render loop's ONE wait is the shared WorkerPool's completion substrate
  // (D-render_loop_liveness_wake-1). `cursor` is a render-thread-local seeded from the pool's
  // settle generation BEFORE each drive: a worker that settles a tile between drive_once's
  // pending().tiles poll and the park below bumps the generation past this cursor, so
  // wait_completions returns at once instead of parking cold — closing the blank_first_frame
  // lost-wakeup (Constraint 2). The wake condition is the settle GENERATION, never the racy
  // pending().tiles poll that stranded the pre-fix loop.
  arbc::CompletionCursor cursor;
  for (;;) {
    cursor.drained_gen = impl_->pool.settle_generation();
    {
      std::lock_guard<std::mutex> lock(impl_->mu);
      if (impl_->stop || (should_stop && should_stop())) {
        return;
      }
    }
    // No lease around the iteration any more (D-writer_thread-11): the render walk reads the
    // document lock-free (`pin()` + the copy-on-write binding table), the DamageAccumulator
    // carries its own mutex since the v0.3.0 pin, and `step()` declines to publish off the
    // writer thread — so nothing here needs mutual exclusion against an edit. What the
    // iteration DOES owe the writer, it posts (D-writer_thread-8/10).
    const bool more_pending = drive_once();
    // Park on the pool instead of the pre-fix busy-spin (more_pending -> re-arm dirty -> immediate
    // re-drive with no wait). A worker's poke() on tile completion re-drives us; wake() pokes for
    // host events and stop(); a settle that raced the seed above already advanced the generation,
    // so the park returns immediately. While work is outstanding (more_pending) the timeout is a
    // defensive re-check cadence for a follow-up composite of already-reaped arrivals that owes no
    // further worker poke (Constraint 3); a settled/empty document (more_pending == false) parks
    // indefinitely with ZERO wakeups (Constraint 1) until a host event or stop pokes it.
    const std::optional<std::chrono::steady_clock::time_point> until =
        more_pending ? std::optional<
                           std::chrono::steady_clock::time_point>{std::chrono::steady_clock::now() +
                                                                  impl_->budget}
                     : std::nullopt;
    impl_->pool.wait_completions(cursor, until);
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
