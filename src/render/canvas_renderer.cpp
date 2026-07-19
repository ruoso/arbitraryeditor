#include <ace/project/project.hpp> // seed_kind_bridge (render->project, L2->L1)
#include <ace/render/canvas_renderer.hpp>
#include <ace/render/render.hpp>

#include <arbc/backend_cpu/cpu_backend.hpp>
#include <arbc/base/time.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/cache/key_shapes.hpp>
#include <arbc/compositor/compositor.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/media/pixel_format.hpp>
#include <arbc/media/surface_format.hpp>
#include <arbc/runtime/damage_router.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp> // arbc::KindBridge
#include <arbc/runtime/host_viewport.hpp>
#include <arbc/runtime/interactive.hpp>
#include <arbc/runtime/worker_pool.hpp>
#include <arbc/surface/surface.hpp>
#include <arbc/surface/surface_pool.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace ace::render {

// The tile-cache byte budget the host-interactive example uses for one viewport.
constexpr std::size_t k_tile_cache_bytes = 64U * 1024 * 1024;

struct CanvasRenderer::Impl {
  Impl(arbc::Document& doc, arbc::WorkerPool* borrowed, arbc::DamageRouter* damage_router,
       std::chrono::steady_clock::duration frame_budget, const arbc::Registry* reg)
      : document(doc), pool(backend), cache(k_tile_cache_bytes), borrowed_pool(borrowed),
        router(damage_router), budget(frame_budget), registry(reg) {
    // Seed the per-renderer KindBridge ONCE from the app's persistent Registry (not per
    // rebuild()), so it resolves every registered kind's token the same way the save/load
    // bridges do (A14). Render-thread-confined: owned here, mutated only by step()'s settle.
    // A null registry leaves the bridge unseeded and the binding empty (Constraint 3).
    if (registry != nullptr) {
      project::seed_kind_bridge(bridge, *registry);
    }
  }

  // Rebuild the size-dependent bundle (target Surface + InteractiveRenderer +
  // HostViewport) at the current width/height. A zero/degenerate size tears the
  // bundle down and allocates nothing (Constraint 7). The HostViewport is
  // non-movable, so a resize reconstructs it rather than reseating a member.
  void rebuild() {
    // Destroy in dependency order: the viewport references the renderer + target.
    viewport.reset();
    renderer.reset();
    target.reset();
    image = Srgb8Image{};
    converted_frames = 0;

    if (width <= 0 || height <= 0) {
      return; // zero-area: allocate nothing (Constraint 7).
    }
    // The target lives in the document's working space (premultiplied-linear
    // rgba32f), matching examples/host-interactive/main.cpp and the offline path.
    // A make_surface failure (defensive) leaves the bundle torn down, image empty.
    auto surface = backend.make_surface(width, height, document.pin()->working_space());
    if (surface.has_value()) {
      target = std::move(*surface);

      // Either BORROW the host's shared WorkerPool (runtime.shared_worker_pool,
      // multi_canvas) or OWN a thread-free inline executor (WorkerPoolConfig{},
      // worker_count == 0 — the deterministic golden mode, D-canvas_view-2). One code
      // path: a WorkerPoolConfig{} pool is byte-identical to the parallel path.
      if (borrowed_pool != nullptr) {
        renderer.emplace(*borrowed_pool);
      } else {
        renderer.emplace(arbc::WorkerPoolConfig{});
      }

      arbc::HostViewport::Config config;
      // Frame with the CURRENT camera, not identity — so a resize (which reconstructs
      // the non-movable viewport) preserves the user's pan/zoom (Constraint 3 /
      // editor.canvas.nav). The default camera is identity, the pre-nav framing.
      config.viewport = arbc::Viewport{width, height, camera};
      // The per-frame budget: settle-fully (an effectively unbounded hour) for the
      // inline golden path, or a caller-chosen BOUNDED slice for the shared-thread host
      // so one heavy canvas cannot starve another (D-multi_canvas-3).
      config.budget = budget;
      // The shared DamageRouter (multi_canvas): the viewport registers ITS accumulator with
      // the router instead of evicting the Model's single sink slot, so a commit fans out to
      // every canvas over this document. Null for the single-canvas own-pool golden path
      // (one viewport, the direct single-slot install).
      config.router = router;
      // Populate the DocumentBinding only when a registry was supplied: both non-null =>
      // HostViewport derives settle_external_loads(doc, bridge, *registry) and runs it at the
      // top of every step(), so a nested child whose bytes arrive from a deferring AssetSource
      // installs-and-composites (editor.canvas.nested_composition_binding, D-...-1). With no
      // registry the binding stays {} — the correct shape for a ref-free document
      // (host_viewport.hpp:122-124), byte-identical to the pre-leaf behaviour (Constraint 3).
      const arbc::HostViewport::DocumentBinding binding =
          registry != nullptr ? arbc::HostViewport::DocumentBinding{&bridge, registry}
                              : arbc::HostViewport::DocumentBinding{};
      viewport =
          std::make_unique<arbc::HostViewport>(*renderer, document, binding, backend, pool, cache,
                                               *target, arbc::HostViewport::Clock{}, config);
      // Pin the playhead so a still, undamaged scene issues zero further frames
      // (the still-scene early-out) — matching the offline path's fixed time.
      viewport->set_playhead_source([] { return arbc::Time::zero(); });
    }
  }

  // Convert the settled working-space target to straight-alpha sRGB8 (D10 — the
  // library owns the un-premultiply + sRGB encode; never hand-rolled). Mirrors
  // render_document_srgb8's convert-and-pack tail exactly.
  void convert() {
    // Only ever called from step() while `target` is live, so no null guard.
    image = Srgb8Image{};
    auto srgb = backend.make_surface(width, height, arbc::k_fast_rgba8srgb);
    if (srgb.has_value()) { // false only on a defensive allocation failure.
      backend.convert(**srgb, *target);
      const std::span<const std::uint8_t> bytes = (*srgb)->span<arbc::PixelFormat::Rgba8Srgb>();
      image.width = width;
      image.height = height;
      image.pixels.assign(bytes.begin(), bytes.end());
    }
  }

  arbc::Document& document;
  arbc::CpuBackend backend;
  arbc::SurfacePool pool;
  arbc::TileCache cache;

  // The shared pool this renderer borrows (nullptr == own an inline executor), the shared
  // per-document DamageRouter it registers with (nullptr == the direct single-slot install),
  // and the per-frame budget handed to HostViewport (D-multi_canvas-3/4). All fixed at ctor.
  arbc::WorkerPool* borrowed_pool = nullptr;
  arbc::DamageRouter* router = nullptr;
  std::chrono::steady_clock::duration budget = std::chrono::hours(1);

  // The app's process-persistent kind Registry (borrowed, may be null) + the per-renderer
  // KindBridge seeded from it once (editor.canvas.nested_composition_binding). The bridge is
  // the pointer the DocumentBinding hands settle_external_loads; render-thread-confined,
  // mutated only by this renderer's step() (D-nested_composition_binding-1/2).
  const arbc::Registry* registry = nullptr;
  arbc::KindBridge bridge;

  int width = 0;
  int height = 0;

  // The current viewport camera (editor.canvas.nav). Held here so rebuild() reframes
  // with it (Constraint 3); default identity is the pre-nav framing. Render-thread-only.
  arbc::Affine camera = arbc::Affine::identity();

  std::unique_ptr<arbc::Surface> target;
  std::optional<arbc::InteractiveRenderer> renderer;
  std::unique_ptr<arbc::HostViewport> viewport;

  Srgb8Image image;
  // The frame count last folded into `image`; re-convert only when it advances.
  std::uint64_t converted_frames = 0;
};

CanvasRenderer::CanvasRenderer(arbc::Document& document, const arbc::Registry* registry)
    : impl_(std::make_unique<Impl>(document, nullptr, nullptr, std::chrono::hours(1), registry)) {}

CanvasRenderer::CanvasRenderer(arbc::Document& document, arbc::WorkerPool& pool,
                               arbc::DamageRouter& router,
                               std::chrono::steady_clock::duration budget,
                               const arbc::Registry* registry)
    : impl_(std::make_unique<Impl>(document, &pool, &router, budget, registry)) {}

CanvasRenderer::~CanvasRenderer() = default;

void CanvasRenderer::resize(int width, int height) {
  impl_->width = width;
  impl_->height = height;
  impl_->rebuild();
}

void CanvasRenderer::set_camera(const arbc::Affine& camera) {
  impl_->camera = camera;
  // Apply to the live viewport too — a camera change is device damage, so the next
  // step() repaints at the new framing (host_viewport.hpp:190-196). At a zero size
  // there is no viewport; the held camera reaches the next rebuild().
  if (impl_->viewport) {
    impl_->viewport->set_camera(camera);
  }
}

bool CanvasRenderer::step() {
  if (!impl_->viewport) {
    return false; // zero-area / defensive: nothing to drive (Constraint 7).
  }
  const arbc::HostViewport::StepOutcome outcome = impl_->viewport->step();
  const std::uint64_t frames = impl_->viewport->frames_issued();
  if (frames != impl_->converted_frames) {
    impl_->convert();
    impl_->converted_frames = frames;
  }
  // "Not settled" — the host loop must re-drive this entry until true (D-multi_canvas-3):
  //   * schedule_follow_up: the bounded budget owes a follow-up frame; OR
  //   * a tile is still IN FLIGHT (a worker miss dispatched but not yet reaped —
  //     `InteractiveRenderer::pending().tiles`, the same work-in-flight witness
  //     HostViewport uses for its own idle early-out, host_viewport.cpp:198).
  // The in-flight term is load-bearing under editor.canvas.blank_first_frame: a degraded
  // first frame dispatches a worker miss and returns schedule_follow_up == false, so
  // without it the loop would go cold on the withheld blank frame and the resolving tile
  // would never be reaped — there is NO async-completion wake for the render loop. Always
  // false for the settle-fully inline path (the hour budget composites in one pass, nothing
  // left pending).
  return outcome.schedule_follow_up || !impl_->renderer->pending().tiles.empty();
}

const Srgb8Image& CanvasRenderer::image() const { return impl_->image; }

std::uint64_t CanvasRenderer::frames_issued() const {
  return impl_->viewport ? impl_->viewport->frames_issued() : 0;
}

int CanvasRenderer::width() const { return impl_->width; }
int CanvasRenderer::height() const { return impl_->height; }

std::size_t CanvasRenderer::anchor_depth() const {
  return impl_->viewport ? impl_->viewport->anchor_depth() : 0;
}

const arbc::WorkerPool* CanvasRenderer::borrowed_pool() const { return impl_->borrowed_pool; }

} // namespace ace::render
