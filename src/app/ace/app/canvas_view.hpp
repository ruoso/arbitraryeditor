#pragma once

#include <ace/platform/threads.hpp>
#include <ace/render/canvas_host.hpp>
#include <ace/render/render.hpp>

#include <arbc/base/transform.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ace::commands {
class AppState;
} // namespace ace::commands

namespace ace::app {

// The shell's Canvas subsystem at the app layer (L4): ONE render::CanvasHost over the
// one owned Document, ONE render thread, and a canvas#N-keyed set of per-pane GL-texture
// presenters (editor.canvas.multi_canvas; A5 "N renderers share one WorkerPool"). This
// supersedes editor.canvas.frame_sync's render-thread-per-CanvasView: the shell holds a
// single CanvasView, and every canvas#N pane the dock mints renders INDEPENDENTLY through
// one shared host + one shared thread (D-multi_canvas-2).
//
// At construction it spawns the one render thread (through platform::NativeThreads, A3)
// that runs the host's drive loop off the UI thread (A4 "rendering is never on it"). Each
// UI frame, per canvas#N pane: it ensures the host has that entry (add is idempotent),
// sizes it to the pane (a resize REQUEST the render thread services), consumes the
// entry's latest settled frame from its double-buffer, and uploads it to that id's own GL
// texture — a fresh gl::upload_rgba8 on the first frame / a resize, an in-place
// gl::update_rgba8 when a new sequence lands at the same size (Constraint 3), no GL
// traffic when the sequence has not moved. An edit poke wakes EVERY live entry (the
// fan-out — one writer, N observers; Constraint 4). Panes that leave the dock layout are
// reconciled away (host entry freed on the render thread, texture on the UI thread;
// Constraint 7). Teardown is deterministic: stop -> join the one thread -> release every
// texture (Constraint 5). Bind against the process's one owned Document via the AppState&
// the shell holds; it must outlive this view.
class CanvasView {
public:
  explicit CanvasView(commands::AppState& state);
  ~CanvasView();

  CanvasView(const CanvasView&) = delete;
  CanvasView& operator=(const CanvasView&) = delete;

  // Draw the Canvas body for `view_id` into the CURRENT ImGui window at `pane_width`x
  // `pane_height` device pixels (the dockspace owns Begin/End + the tab ✕). Lazily adds
  // the host entry + presenter on this id's first appearance. A zero/degenerate pane
  // renders nothing (Constraint 7). Requires a current GL context.
  void draw_content(std::string_view view_id, int pane_width, int pane_height);

  // Run a UI-thread Document-mutating edit serialized against the off-thread render read
  // (editor.canvas.edit_render_sync, D-edit_render_sync-2): forwards to
  // CanvasHost::apply_edit, so the mutation runs inside the render thread's per-frame
  // `doc_mu` window and then wakes EVERY live canvas. The race-free replacement for
  // "mutate the Document, then poke()" — the edit verbs (undo/redo via the gateway runner)
  // funnel through here. Runs the edit synchronously on the calling (writer) thread.
  void apply_edit(const std::function<void()>& edit);

  // Wake the render thread to re-render EVERY live canvas after a UI-thread edit (the
  // fan-out poke seam the edit points drive; D-frame_sync-2 / Constraint 4). Thread-safe.
  // NOTE: a bare poke does NOT serialize the preceding mutation against the render read —
  // use apply_edit() for a Document mutation submitted while the render loop is live.
  void poke();

  // Reconcile the live presenters/entries against the dock's current view ids: drop the
  // host entry (render thread) and GL texture (UI thread) of any canvas#N that left the
  // layout (Constraint 7 / D-multi_canvas-5). Call each UI frame after the dock draws.
  void reconcile(const std::vector<std::string>& live_view_ids);

  // The published-frame sequence for `view_id` (0 for an unknown id) — the per-pane
  // test-visible liveness signal (>= 1 for a live frame; an advance after an edit poke
  // proves the off-thread re-render reached that entry's double-buffer).
  std::uint64_t frames_issued(std::string_view view_id) const;

  // The deep-zoom anchor-path depth for `view_id` (0 for an unknown id / in-band): the
  // rebasing observability signal (D-nav-5), surfaced for the e2e to prove wheel-zoom
  // engaged the library's re-anchoring.
  std::size_t anchor_depth(std::string_view view_id) const;

  // The composition-unit length of `view_id`'s current scale bar (0 for an unknown id):
  // the last value computed from that pane's transient camera. The e2e asserts it changes
  // after a zoom (the scale readout tracks the camera; D-nav-6).
  double scale_bar_units(std::string_view view_id) const;

  // Stop + join the render thread and release every GL texture while the context is
  // still valid (before shutdown). Safe to call twice (the destructor also calls it).
  void destroy();

private:
  // One per-canvas#N presenter: the latest frame consumed from that entry's double-buffer
  // and the GL texture it uploads to. All UI-thread state — including the TRANSIENT
  // viewport camera (D-nav-1: never a transact, never persisted; a per-pane value like
  // Selection), submitted to the render thread through host_.request_camera.
  struct Presenter {
    render::Srgb8Image image;
    std::uint64_t consumed_seq = 0;
    int requested_width = 0;
    int requested_height = 0;
    unsigned int texture = 0;
    int tex_width = 0;
    int tex_height = 0;
    arbc::Affine camera = arbc::Affine::identity(); // the transient viewport camera
    double scale_bar_units = 0.0;                   // last scale-bar length (composition units)
  };

  commands::AppState& state_;
  render::CanvasHost host_;
  platform::NativeThreads threads_;
  std::unique_ptr<platform::JoinHandle> render_thread_;
  std::map<std::string, Presenter, std::less<>> presenters_;
};

} // namespace ace::app
