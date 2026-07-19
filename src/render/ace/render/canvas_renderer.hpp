#pragma once

#include <ace/render/render.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace arbc {
class Document;
class WorkerPool;
class DamageRouter;
class Registry;
struct Affine;
} // namespace arbc

namespace ace::render {

// The single-canvas interactive driver (editor.canvas.view; A5 "a canvas view is
// one HostViewport + InteractiveRenderer over the shared Document"). It composes
// the libarbc pieces the host-interactive example wires by hand — a CpuBackend
// (behind the A6 Backend seam), a SurfacePool, a TileCache, one persistent target
// Surface in the document's working space, one InteractiveRenderer, and a
// HostViewport bound via the Document& constructor against the app's one owned
// Document — and drives one frame per UI tick, converting the settled target to
// straight-alpha sRGB8 (D10 — via CpuBackend::convert, never hand-rolled).
//
// GL-FREE and ImGui-FREE (Constraint 2): it produces CPU sRGB8 bytes; the GL
// upload is a separate gl/app step, so the whole compositor path is
// headless-unit- and golden-testable without a GL context.
//
// Two construction modes (editor.canvas.multi_canvas / D-multi_canvas-4 parameterizes
// the two hard-coded choices this bundle used to own):
//   - the Document& ctor OWNS a thread-free inline executor (WorkerPoolConfig{}) and
//     settles the whole frame in one step (an effectively unbounded budget) — the
//     deterministic golden path D-canvas_view-2 ships;
//   - the (Document&, WorkerPool&, budget) ctor BORROWS a shared WorkerPool (the pool
//     the CanvasHost owns and every canvas shares, runtime.shared_worker_pool) and
//     drives each step under a caller-chosen BOUNDED budget so one heavy canvas on the
//     shared render thread cannot starve another (D-multi_canvas-3). The borrowed pool
//     MUST outlive this renderer (interactive.hpp lifetime rule).
// Both modes are ONE code path — determinism is a config choice, not a code fork; a
// WorkerPoolConfig{} pool is byte-identical to the parallel path.
//
// The off-UI-thread driver + latest-frame double-buffer is editor.canvas.frame_sync;
// N canvases sharing one WorkerPool is editor.canvas.multi_canvas. `document` and
// its collaborators are held by reference and MUST outlive this object (the
// HostViewport ctor's lifetime contract).
class CanvasRenderer {
public:
  // The optional `registry` is the app's process-persistent kind Registry
  // (editor.canvas.nested_composition_binding, D-nested_composition_binding-1/2). When
  // supplied, this renderer seeds a per-renderer KindBridge from it once and hands the
  // HostViewport a populated DocumentBinding{&bridge, registry}, so step() derives and runs
  // settle_external_loads each frame — installing (and compositing) nested children whose
  // bytes arrive from a deferring AssetSource after the document loaded. When null (the
  // default), the binding stays empty — the right shape for a ref-free, programmatically-built
  // document (host_viewport.hpp:122-124), byte-for-byte the pre-leaf behaviour. The Registry
  // is a libarbc type crossing the seam (no commands->render edge); it MUST outlive this
  // renderer. Mirrors the {Registry, KindBridge} pair the save/load paths already thread (A14).
  explicit CanvasRenderer(arbc::Document& document, const arbc::Registry* registry = nullptr);
  // Borrow a shared WorkerPool + a shared per-document DamageRouter, and drive each frame
  // under a bounded budget (editor.canvas.multi_canvas). The router (not the Model's single
  // set_damage_sink slot) fans a commit's damage out to EVERY viewport sharing the
  // document, so N canvases all re-render an edit (`damage_router.hpp`). `pool` and `router`
  // MUST outlive this renderer. `registry` carries the external-arrival settle binding as
  // above (default null == empty binding).
  CanvasRenderer(arbc::Document& document, arbc::WorkerPool& pool, arbc::DamageRouter& router,
                 std::chrono::steady_clock::duration budget,
                 const arbc::Registry* registry = nullptr);
  ~CanvasRenderer();

  CanvasRenderer(const CanvasRenderer&) = delete;
  CanvasRenderer& operator=(const CanvasRenderer&) = delete;

  // Frame the canvas at `width`x`height` device pixels: reallocate the target
  // Surface and reconstruct the (non-movable) HostViewport bundle at the new
  // size. A degenerate/zero size renders nothing (no allocation) — Constraint 7.
  void resize(int width, int height);

  // Set the viewport camera (composition units -> device pixels), the second
  // UI→render state channel this leaf adds beside resize (editor.canvas.nav,
  // D-nav-3). Applied to the live HostViewport via set_camera AND retained, so a
  // later rebuild() (a resize) reframes with the current camera rather than
  // resetting to identity (Constraint 3). RENDER-THREAD-confined: the HostViewport
  // and its TileCache are render-thread-only (A4), so only the driver's drive loop
  // calls this. A value-identical camera is free — the next step issues no frame.
  void set_camera(const arbc::Affine& camera);

  // Drive one HostViewport::step(). The first step always composites
  // (frames_issued() == 1); a subsequent step on an unchanged, playhead-pinned
  // scene issues zero further frames (the still-scene early-out). Re-converts the
  // settled target to sRGB8 only when a new frame was issued. Returns true when the
  // renderer is NOT settled and the caller should re-drive next cycle
  // (D-multi_canvas-3): either the bounded-budget pass owes a follow-up frame
  // (StepOutcome::schedule_follow_up) OR a tile is still in flight (a dispatched worker
  // miss not yet reaped) — the work-in-flight term keeps the host loop hot through async
  // tile resolution when editor.canvas.blank_first_frame withholds the blank frame's
  // publish (there is no async-completion wake). Always false for the settle-fully inline
  // path (nothing left pending).
  bool step();

  // The current settled straight-alpha sRGB8 image (empty before the first frame
  // or at a zero size). Tightly packed w*h*4, GL-uploadable — Constraint 5.
  const Srgb8Image& image() const;

  // The HostViewport's issued-frame counter — the change signal the app uses to
  // upload GL only when the frame moved (Constraint 6). Zero before the first
  // step or at a zero size.
  std::uint64_t frames_issued() const;

  // The current framing size in device pixels (0 until the first non-degenerate
  // resize).
  int width() const;
  int height() const;

  // The HostViewport's current anchor-path depth (zoom-in pushes, zoom-out pops):
  // the deep-zoom rebasing observability signal the editor surfaces but never
  // drives (D-nav-5). 0 before the first frame, at a zero size, or while the
  // camera stays within the well-conditioned band. RENDER-THREAD-confined.
  std::size_t anchor_depth() const;

  // The shared WorkerPool this renderer BORROWS, or nullptr when it owns an inline
  // executor. The CanvasHost exposes this so a test can prove N canvases borrow one
  // pool (acceptance criterion f).
  const arbc::WorkerPool* borrowed_pool() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ace::render
