#pragma once

#include <ace/app/view_framing.hpp>
#include <ace/interact/interact.hpp>
#include <ace/interact/pick.hpp>
#include <ace/platform/threads.hpp>
#include <ace/render/canvas_host.hpp>
#include <ace/render/render.hpp>
#include <ace/writer/writer_thread.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ace::commands {
class AppState;
} // namespace ace::commands

namespace ace::scene {
struct Camera;
} // namespace ace::scene

namespace ace::views {
struct CanvasInput;
} // namespace ace::views

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
  // `writer` is the DOCUMENT's one writer thread (editor.canvas.writer_thread). It must already
  // be running and must have executed the document's creation/load — `arbc::load_document` is the
  // FIRST write and binds the identity, so a session opened on the UI thread and then edited
  // through here would be two identities, which is the exact bug this leaf removes. Its lifetime
  // strictly encloses this view's AND the document's (D-writer_thread-6): construct it before the
  // `AppState`, `stop()` it after this view is destroyed and before the document is released.
  CanvasView(commands::AppState& state, writer::WriterThread& writer);
  ~CanvasView();

  CanvasView(const CanvasView&) = delete;
  CanvasView& operator=(const CanvasView&) = delete;

  // Draw the Canvas body for `view_id` into the CURRENT ImGui window at `pane_width`x
  // `pane_height` device pixels (the dockspace owns Begin/End + the tab ✕). Lazily adds
  // the host entry + presenter on this id's first appearance. A zero/degenerate pane
  // renders nothing (Constraint 7). Requires a current GL context.
  void draw_content(std::string_view view_id, int pane_width, int pane_height);

  // THE edit seam (D-writer_thread-11). POST `edit` to the document's one writer thread, block
  // until it has run, run the writer-turn epilogue there too, and then wake EVERY live canvas.
  // Synchronous by default because most call sites are result-carrying and capture by reference
  // (`run_edit([&]{ moved = undo(...).moved; })`, insert returning the new id the UI selects) —
  // making those async would be a silent lifetime bug, not a latency win (D-writer_thread-3).
  //
  // The render read takes no lock and needs none: the binding table publishes copy-on-write
  // (arbc#10/#11), the DamageAccumulator carries its own mutex, and `step()` declines to publish
  // off the writer thread (v0.3.0). What this call buys is IDENTITY, not exclusion — the whole
  // point A4.1 makes and a mutex cannot deliver.
  //
  // Callable from any thread (the UI thread, and the ImGui Test Engine's coroutine thread in the
  // e2e rig); re-entrant calls from inside a posted closure run inline rather than deadlocking.
  void apply_edit(const std::function<void()>& edit);

  // Run writer-thread work that is NOT an edit — the save path's `capture_snapshot`, which reads
  // writer-owned state but publishes no revision (D-writer_thread-7). Same posting and blocking
  // as `apply_edit`, WITHOUT the epilogue and the canvas wake, both of which would be pure cost.
  void on_writer(const std::function<void()>& work);

  // Install the writer-turn epilogue (arch A18): run at the end of every `apply_edit`, ON THE
  // WRITER THREAD, so it may read writer-owned document structure the edit just changed
  // (libarbc's `Journal::entry_at`, for one) and publish it for the UI thread. The shell binds it
  // to `commands::publish_history`, so EVERY document mutation — including this class's own
  // manipulator commits and the camera inspector's bare `scene::` transactions, neither of which
  // passes through a `commands` verb — republishes the History panel's snapshot. It moved here
  // from `CanvasHost` with the edit seam: `render` no longer owns the document write path.
  // Set once at wiring time, before edits start.
  void set_post_edit_hook(std::function<void()> hook);

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

  // The per-canvas active-camera selection for `view_id` (editor.cameras.look_through,
  // D-look_through-1): `nullopt` = the free viewport camera (default); a shot's `ObjectId`
  // = look through that shot. Session state, never a transaction (D15/D-model-3). The
  // Overview's per-camera "look through" button (editor.panels.overview, D-look_through-5)
  // and the e2e both drive this. An unknown id is a no-op / reports nullopt.
  void set_look_through(std::string_view view_id, std::optional<arbc::ObjectId> shot);
  std::optional<arbc::ObjectId> look_through(std::string_view view_id) const;

  // The deep-zoom anchor-path depth for `view_id` (0 for an unknown id / in-band): the
  // rebasing observability signal (D-nav-5), surfaced for the e2e to prove wheel-zoom
  // engaged the library's re-anchoring.
  std::size_t anchor_depth(std::string_view view_id) const;

  // The composition-unit length of `view_id`'s current scale bar (0 for an unknown id):
  // the last value computed from that pane's transient camera. The e2e asserts it changes
  // after a zoom (the scale readout tracks the camera; D-nav-6).
  double scale_bar_units(std::string_view view_id) const;

  // The transient viewport framing of the first (lowest-id) live, sized canvas pane —
  // camera + device size, BY VALUE. A zero-pane `ViewFraming` when no canvas has been
  // sized yet. `editor.cells.model` reads this to compute a freshly-inserted cell's
  // provisional placement from what the user is actually looking at (Constraint 7);
  // nothing here mutates the Document, so it is a plain UI-thread session read.
  ViewFraming primary_framing() const;

  // The framing of the canvas pane the user most recently WORKED IN — the source both
  // framing-derived verbs read (D23's "which viewport", D-mint_from_focused_canvas-1/-4):
  // the focused pane's framing when it is live and sized, else `primary_framing()`'s
  // lowest-id fallback, else the zero "no live canvas" sentinel. With a single canvas open
  // the two accessors are bit-identical.
  ViewFraming focused_framing() const;

  // The STICKY focused-canvas hint itself (empty when no canvas has held focus, or when the
  // one that did was closed) — stamped in `draw_content` from `ImGui::IsWindowFocused`, not
  // polled at query time: the rail `Selectable` that triggers a mint steals focus first, so a
  // poll would see no focused canvas on every mint (D-mint_from_focused_canvas-1). Exposed so
  // the e2e can pin the TRACKING separately from the selection rule.
  std::string_view focused_view_id() const;

  // WHICH canvas pane the framing-derived verbs will actually act on — the sticky hint run
  // through the SAME `focus_target` rule `focused_framing()` consumes, so it names the
  // lowest-id FALLBACK pane whenever the hint is empty, stale, or names an unsized pane, and
  // is empty only when no live canvas is sized (D-focused_canvas_indicator-1/-5). "Lowest id"
  // is `dockmodel::view_id_less`'s NUMERIC order, applied by `pane_rows()` (D23,
  // D-view_id_natural_order-2): "canvas#2", not "canvas#10". This is what
  // `draw_content` marks with the focused-canvas border, and what the e2e asserts so the RULE
  // is pinned separately from the pixels.
  //
  // Lifetime: the result borrows `presenters_`'s own key storage, exactly like
  // `focused_view_id()`'s — valid until the next `reconcile()` drops that pane.
  std::string_view indicated_view_id() const;

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
    // The camera this pane is ACTUALLY SHOWING, refreshed once per frame: the shot's derived
    // comp->device camera while looking through one, else `camera`. Paired with
    // `requested_width/height` it is the pane's `ViewFraming` — and it exists because that
    // pair is otherwise INCOHERENT in look-through mode, where the size is the shot's fitted
    // crop (D-look_through-2) while `camera` is frozen at its last free value
    // (D-look_through-6). Both framing accessors read this, never `camera`
    // (D-mint_from_focused_canvas-5).
    arbc::Affine framing_camera = arbc::Affine::identity();
    double scale_bar_units = 0.0; // last scale-bar length (composition units)
    // The active-camera selection (editor.cameras.look_through, D-look_through-1): nullopt =
    // free viewport, an ObjectId = look through that shot. UI-thread-only session state.
    std::optional<arbc::ObjectId> look_through;
    // The camera last submitted through host_.request_camera. While a shot is selected the
    // submitted camera is the shot's (not `camera`, which is left at its free value —
    // D-look_through-6), so the two diverge; this dedups the per-frame submit across both.
    arbc::Affine submitted = arbc::Affine::identity();
    // --- camera-frame gizmo (editor.cameras.manip; D-manip-4) ------------------
    // The in-progress border-grab, if any: previewed as UI-thread session state (the gizmo
    // redraws at the dragged `Affine`, no journal churn) and committed as ONE
    // set_layer_transform through apply_edit on release. `gizmo_camera` is unset when no
    // grab is active.
    std::optional<arbc::ObjectId> gizmo_camera; // the shot being reframed
    arbc::ObjectId gizmo_layer;                 // its binding layer (set_layer_transform target)
    interact::FrameHandle gizmo_handle = interact::FrameHandle::None;
    arbc::Affine gizmo_start_frame = arbc::Affine::identity(); // the frame at grab (preview base)
    arbc::Vec2 gizmo_grab_comp{};                              // pointer in composition at grab
    int gizmo_res_w = 0;
    int gizmo_res_h = 0;
    // --- marquee gesture (editor.cells.selection; Constraint 1) -----------------
    // The ONLY selection-adjacent per-pane state this leaf adds — and it is a GESTURE, not a
    // selection: the one project-level `commands::Selection` lives on `AppState` (D19/A5/A7)
    // and the canvas holds no copy of it. Armed by a press that hit nothing, cleared on
    // release / lost activation.
    bool marquee_active = false;
    arbc::Vec2 marquee_anchor{}; // the press anchor in COMPOSITION units
  };

  // Draw THIS canvas's camera picker (Viewport | shots from scene::cameras) as a compact
  // overlay at the pane's top-left, setting `p.look_through` on a click (D-look_through-5).
  void draw_camera_picker(std::string_view view_id, Presenter& p,
                          const std::vector<scene::Camera>& cameras);

  // Draw the shot-camera frame gizmos over the free-viewport pane and drive a direct-
  // manipulation border-grab (editor.cameras.manip; D-manip-4): hit-test each camera's
  // frame outline, preview a re-crop / move / dutch drag as session state (the gizmo
  // rectangle redraws at the dragged `Affine`, no journal churn), and commit ONE
  // set_layer_transform through apply_edit on release (one undo step per gesture). The
  // frame math is pure L1 interact; this only maps composition<->screen and commits.
  // `origin_x`/`origin_y` are the pane's top-left in screen pixels.
  void draw_frame_gizmos(std::string_view view_id, Presenter& p,
                         const std::vector<scene::Camera>& cameras, const views::CanvasInput& in,
                         float origin_x, float origin_y);

  // Drive the project-level selection over this pane and draw its chrome (editor.cells.selection;
  // D7/D19). Prunes stale ids against `targets` (Constraint 10), applies the L1 pick/modifier
  // policy's `SelectionChange` onto the ONE `commands::Selection` on `AppState` — the canvas
  // keeps no copy (Constraint 1) — and draws a stroked outline per selected cell plus the
  // in-progress marquee rectangle (D-selection-8; handles are `editor.cells.gizmo`'s). Selecting
  // is never a transaction (Constraint 2): nothing here opens a `transact` or calls `apply_edit`.
  // `origin_x`/`origin_y` are the pane's top-left in screen pixels.
  void draw_selection(std::string_view view_id, Presenter& p,
                      const std::vector<interact::PickTarget>& targets,
                      const views::CanvasInput& in, float origin_x, float origin_y);

  // Project `presenters_` into the pure rule's input rows, SORTED by `dockmodel::view_id_less`
  // — the map's own byte order is not the view-id order (it puts "canvas#10" before "canvas#2"),
  // and supplying the order is this projection's job, not the rule's (D-view_id_natural_order-4).
  // The rows' `view_id`s BORROW the map's keys (view_framing.hpp's non-owning contract), so a
  // `string_view` the rule returns outlives the returned vector but not the next `reconcile()`.
  std::vector<PaneFraming> pane_rows() const;

  // Project `presenters_` (through `pane_rows()`, so in `view_id_less` order) into
  // `framing_for_focus`'s pure input and apply it with `focused` as the hint. Both public
  // framing accessors are one call to this,
  // which is what makes their single-canvas bit-identity structural rather than a claim about
  // two hand-written loops (D-mint_from_focused_canvas-3).
  ViewFraming framing_for(std::string_view focused) const;

  commands::AppState& state_;
  // The document's one writer thread (borrowed; it outlives this view and the document).
  writer::WriterThread& writer_;
  render::CanvasHost host_;
  platform::NativeThreads threads_;
  std::unique_ptr<platform::JoinHandle> render_thread_;
  // The writer-turn epilogue (A18), run inside every posted edit closure. Set once at wiring
  // time from the thread that wires; read on the writer thread.
  std::function<void()> post_edit_hook_;
  std::map<std::string, Presenter, std::less<>> presenters_;
  // The sticky focused-canvas hint (D-mint_from_focused_canvas-1/-2): UI-thread session
  // state, written only in `draw_content`, cleared only in `reconcile` when that pane's
  // presenter is dropped. Never serialized, never a transaction, never read by
  // `commands`/`scene`/`project` (D15/D19).
  std::string focused_view_id_;
};

} // namespace ace::app
