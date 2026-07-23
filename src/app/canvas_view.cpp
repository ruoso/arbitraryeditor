#include <ace/app/accent.hpp>
#include <ace/app/canvas_view.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/gl/gl.hpp>
#include <ace/interact/interact.hpp>
#include <ace/interact/pick.hpp>
#include <ace/project/project.hpp>
#include <ace/render/canvas_host.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {
// The zoom factor per wheel notch (editor.canvas.nav): each notch multiplies the
// composition→device scale by this, so a wheel of `w` notches zooms by k_zoom_base^w.
constexpr double k_zoom_base = 1.2;

// The focused-canvas marker (editor.canvas.focused_canvas_indicator; D-focused_canvas_indicator-4):
// the same accent this file already gives the active camera frame, the marquee and the selection
// outline, so the marker joins an existing visual language rather than introducing a colour — but
// OPAQUE, deliberately: an unblended stroke lands identically over whatever the pane happens to be
// showing, which is what lets the e2e probe it under software GL without a content-dependent
// expectation. A hairline (1 px, no rounding) reads as chrome rather than as a drawn object, and is
// thinner than the 2 px selection outline that shares the hue. The colour itself lives in
// `ace/app/accent.hpp` as `k_focus_marker_color`; the thickness is a draw-call parameter with one
// call site, not a palette entry (D-accent_palette-4).
constexpr float k_focus_marker_thickness = 1.0F;

// The four-arm switch that turns L1's `SelectionChange` VALUE into a mutation of the ONE
// project-level selection (D-selection-2). `interact` may not see `commands` and does not: this
// is the whole of the L4 side of the modifier policy, and every branch it dispatches to is a
// shipped `commands::Selection` verb.
void apply_selection_change(ace::commands::Selection& selection,
                            const ace::interact::SelectionChange& change) {
  switch (change.op) {
  case ace::interact::SelectOp::None:
    break; // a Shift-miss: leave the selection exactly as it is
  case ace::interact::SelectOp::Replace:
    selection.replace_all(change.ids);
    break;
  case ace::interact::SelectOp::Add:
    selection.add_all(change.ids);
    break;
  case ace::interact::SelectOp::Toggle:
    for (const arbc::ObjectId id : change.ids) {
      selection.toggle(id);
    }
    break;
  case ace::interact::SelectOp::Clear:
    selection.clear();
    break;
  }
}

// The composition-space marquee rectangle spanned by two corners, normalised so x0<x1/y0<y1
// regardless of the drag direction (a rect built backwards would be `empty()` and select
// nothing).
arbc::Rect marquee_rect(arbc::Vec2 anchor, arbc::Vec2 pointer) {
  return arbc::Rect{std::min(anchor.x, pointer.x), std::min(anchor.y, pointer.y),
                    std::max(anchor.x, pointer.x), std::max(anchor.y, pointer.y)};
}
} // namespace

namespace ace::app {

CanvasView::CanvasView(commands::AppState& state, writer::WriterThread& writer)
    : state_(state), writer_(writer) {
  // Bind the document's writer thread BEFORE the render thread exists: the host posts its own
  // WRITER-THREAD-ONLY work there (the per-document DamageRouter, every HostViewport ctor/dtor —
  // D-writer_thread-8), and the first of those happens on the render thread's first iteration.
  host_.set_writer(&writer_);
  // Spawn the ONE render thread through the portable spawn seam (A3): it runs the host's
  // blocking drive loop off the UI thread for ALL canvases. The loop stops when
  // host_.stop() is called at teardown (destroy()), so the predicate is trivially false.
  render_thread_ = threads_.spawn([this] { host_.run([] { return false; }); });
}

CanvasView::~CanvasView() { destroy(); }

void CanvasView::draw_content(std::string_view view_id, int pane_width, int pane_height) {
  if (pane_width <= 0 || pane_height <= 0) {
    return; // degenerate pane — render nothing until it has area (Constraint 7).
  }
  // Remember which canvas the user last WORKED IN (D-mint_from_focused_canvas-1). The dock
  // begins each pane's window with the view id AS the window name (src/dock/dock.cpp), and
  // this body runs inside that Begin, so this answers "is THIS canvas focused?" with no id
  // plumbing. RootAndChildWindows because the pane hosts the camera-picker overlay and popups
  // — clicking one of those is working in this canvas (Constraint 11).
  //
  // STICKY: set on a focused frame, never cleared on an unfocused one. The rail item that
  // triggers a mint is an ImGui::Selectable in the Tool Rail window, so by the time the verb
  // runs NO canvas is focused; a per-frame poll would therefore fall back to canvas#1 on
  // every single mint — i.e. reproduce the exact bug this hint exists to fix. A background
  // dock tab does not draw at all, so a poll would also lose a focused-then-tabbed-behind
  // pane. Cleared only in reconcile(), when this pane leaves the layout.
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    focused_view_id_ = view_id;
  }
  // Lazily register this canvas#N on its first appearance: add the host entry (idempotent)
  // and its presenter (D-multi_canvas-5). The entry's cache is constructed on the render
  // thread; the first settled frame arrives asynchronously through the double-buffer.
  auto it = presenters_.find(view_id);
  if (it == presenters_.end()) {
    it = presenters_.emplace(std::string(view_id), Presenter{}).first;
    // Thread the app's process-persistent kind Registry into the render path so this canvas's
    // HostViewport settles deferred external nested children each frame — the same Registry the
    // save/load paths use, so custom kinds resolve identically across save/load/render (A14,
    // editor.canvas.nested_composition_binding). The L4 app owns both AppState and CanvasHost;
    // the Registry crosses as a libarbc const arbc::Registry* (no commands->render edge).
    // ...and the DOCUMENT-SCOPED KindBridge beside it (D-writer_thread-9): the document holds one
    // external-load settler slot, so every canvas over it shares one writer-owned bridge rather
    // than each renderer owning its own.
    host_.add(std::string(view_id), state_.document(), &state_.registry(), &state_.kind_bridge());
  }
  Presenter& p = it->second;

  // Read the persisted shot cameras ONCE per frame over the lock-free pin() snapshot (A4;
  // no lock, no render-cache touch — D-look_through-7). Reused by the look-through resolve
  // below and the picker overlay at the end.
  const std::vector<scene::Camera> cameras = scene::cameras(state_.document());

  // Resolve this frame's target framing (D-look_through-2). Free viewport (nullopt) = a
  // pane-sized render through the transient camera (today's behavior). A selected shot = its
  // EXACT crop: the shot's pane-fit resolution + the derived comp->device camera, both fed
  // through the SETTLED resize + request_camera channels — no new render channel. A selection
  // whose shot is gone (GC'd/deleted, cameras() no longer lists it) or degenerate falls back
  // to the free viewport (fail-safe, never a crash or a stale frame — Constraint 7).
  int target_w = pane_width;
  int target_h = pane_height;
  std::optional<arbc::Affine> shot_camera;
  if (p.look_through) {
    for (const scene::Camera& cam : cameras) {
      if (cam.id == *p.look_through) {
        const interact::LookThrough lt = interact::look_through(
            cam.frame, cam.resolution.width, cam.resolution.height, pane_width, pane_height);
        if (lt.out_w > 0 && lt.out_h > 0) {
          target_w = lt.out_w;
          target_h = lt.out_h;
          shot_camera = lt.camera;
        }
        break;
      }
    }
  }

  // The framing this pane OFFERS the framing-derived verbs, paired with the target size
  // below: the shot's derived comp->device camera while looking through one, else the free
  // transient camera (D-mint_from_focused_canvas-5). Set here so a pane whose first frame has
  // not landed yet still reports a COHERENT (camera, size) pair, and refreshed after the nav
  // gestures below so the pair never lags a frame behind what is on screen.
  p.framing_camera = shot_camera ? *shot_camera : p.camera;

  // The target size drives this entry's viewport: post a resize REQUEST only on a genuine
  // change (Constraint 1/7). Sizing the viewport to the shot's crop is what clips it, so the
  // letterbox margins are clean bars, not surrounding composition (D-look_through-2).
  if (target_w != p.requested_width || target_h != p.requested_height) {
    p.requested_width = target_w;
    p.requested_height = target_h;
    host_.request_resize(view_id, target_w, target_h);
  }

  // Consume this entry's latest settled frame from its double-buffer (non-blocking);
  // upload to GL only when the sequence advanced (Constraint 3). A fresh upload on the
  // first frame or a size change (a new texture object), an in-place update thereafter.
  if (host_.consume(view_id, p.consumed_seq, p.image) && p.image.width > 0 && p.image.height > 0) {
    const bool size_changed =
        p.texture == 0 || p.image.width != p.tex_width || p.image.height != p.tex_height;
    if (size_changed) {
      if (p.texture != 0) {
        gl::destroy_texture(p.texture);
        p.texture = 0;
      }
      p.texture = gl::upload_rgba8(p.image.pixels.data(), p.image.width, p.image.height);
      p.tex_width = p.image.width;
      p.tex_height = p.image.height;
    } else {
      gl::update_rgba8(p.texture, p.image.pixels.data(), p.image.width, p.image.height);
    }
  }

  // Draw the last uploaded frame every tick so the canvas stays visible between new frames
  // (the render thread only publishes on change). The camera picker overlay is drawn AFTER,
  // regardless of whether a frame exists yet, so a canvas can be retargeted before its first
  // frame lands.
  const ImVec2 pane_origin = ImGui::GetCursorScreenPos();
  if (p.texture != 0 && p.tex_width > 0 && p.tex_height > 0) {
    if (shot_camera) {
      // Look through a shot: present its crop letterboxed. Nav is INERT here — the gesture
      // pipeline is skipped, so Presenter::camera is preserved at its last free value and
      // clearing the selection restores that framing (D-look_through-6). The submitted camera
      // is the shot's, below.
      views::draw_letterboxed(p.texture, p.tex_width, p.tex_height, pane_width, pane_height);
    } else {
      // Free viewport (editor.canvas.nav): draw + read the always-on navigation gestures over
      // the pane. Thread the raw gesture through the L1 interact math (D-nav-2) into the
      // transient camera. reset-to-fit frames the root composition's authored canvas bounds
      // (D-fit_bounds-1); wheel zooms about the cursor; a Space-drag pans (D9).
      const views::CanvasInput in =
          views::draw_canvas_interactive(p.texture, p.tex_width, p.tex_height);
      arbc::Affine camera = p.camera;
      if (in.reset) {
        camera = arbc::Affine::identity();
        if (const std::optional<project::CompositionSize> size =
                project::root_composition_size(state_.document())) {
          camera = interact::fit(size->width, size->height, p.tex_width, p.tex_height);
        }
      }
      if (in.wheel != 0.0F) {
        camera = interact::zoom(camera, arbc::Vec2{in.focus_x, in.focus_y},
                                std::pow(k_zoom_base, static_cast<double>(in.wheel)));
      }
      if (in.panning && (in.pan_dx != 0.0F || in.pan_dy != 0.0F)) {
        camera = interact::pan(camera, in.pan_dx, in.pan_dy);
      }
      // The deep-zoom navigation aid (editor.canvas.nav_aids; D24): Shift+F frames the
      // current selection's extent into the pane. The three doc-named aids collapse to one
      // kind-agnostic gesture — a single cell is fit-to-cell, a single camera fit-to-frame,
      // a multi-selection zoom-to-selection (D-nav_aids-1). The region is the shipped
      // `selected_extent` over the SAME per-frame `pick_targets` the hit-test builds; it is
      // a UI-thread read with no transaction (D-nav_aids-4), and the transient camera rides
      // the unchanged `request_camera` submit below — no journal entry, no dirty (D-nav_aids-3).
      // A selection with nothing bounded (empty, or only unbounded fills) is REFUSED: the
      // camera is left exactly where it is (D-nav_aids-5 / Constraint 6).
      if (in.frame_selection) {
        const std::vector<interact::PickTarget> aid_targets =
            interact::pick_targets(state_.document(), state_.registry());
        if (const std::optional<arbc::Rect> extent =
                interact::selected_extent(aid_targets, state_.selection().items())) {
          camera = interact::fit_region(*extent, p.tex_width, p.tex_height);
        }
      }
      p.camera = camera;

      // The scale bar (composition units per screen span, never a "%"; D2 §3 / D-nav-6):
      // a bar targeting a quarter of the pane's short edge.
      const double target_px = 0.25 * std::min(p.tex_width, p.tex_height);
      const interact::ScaleBar bar = interact::scale_bar(p.camera, target_px);
      p.scale_bar_units = bar.units;
      views::draw_scale_bar(bar.units, bar.device_px);

      // The camera-frame gizmo overlay (editor.cameras.manip; D-manip-4): hit-test/drag the
      // shot frames in the free viewport and commit a reframe. Drawn over the pane, under the
      // picker (which is drawn last). Free viewport only — reframing a shot is a scene edit,
      // distinct from looking through one (D9).
      draw_frame_gizmos(view_id, p, cameras, in, pane_origin.x, pane_origin.y);

      // The project-level selection over the SAME press (editor.cells.selection; D7's ONE
      // select tool): a press over a camera border both selects that camera and starts the
      // grab above — one gesture, not two (Constraint 11). The pick stack is assembled in L1
      // (A17) as a plain lock-free `pin()` read; it opens no transaction and takes no lease,
      // so this deliberately does NOT go through apply_edit (Constraint 2/12).
      const std::vector<interact::PickTarget> targets =
          interact::pick_targets(state_.document(), state_.registry());
      draw_selection(view_id, p, targets, in, pane_origin.x, pane_origin.y);
    }

    // Submit the resolved camera — the shot's affine while looking through, else the free
    // transient camera — on a real change (D-nav-1: never a transact; the submit rides the
    // per-entry render-thread channel, Constraint 2). Dedup against the last submitted value,
    // which diverges from p.camera while a shot is selected.
    p.framing_camera = shot_camera ? *shot_camera : p.camera;
    const arbc::Affine want = p.framing_camera;
    if (!(want == p.submitted)) {
      p.submitted = want;
      host_.request_camera(view_id, want);
    }
  }

  // The focused-canvas marker (editor.canvas.focused_canvas_indicator; D23's "that pane is
  // shown, not inferred"): a hairline accent border just inside THIS pane's content rect, drawn
  // exactly when the shared resolution rule resolves to this pane. Read through
  // `indicated_view_id()` — i.e. the STICKY hint run through `focus_target`, never a live
  // `ImGui::IsWindowFocused` poll, which reports the rail during exactly the interaction the
  // marker exists to disambiguate, and never the raw hint, which names nothing on a fresh
  // session while the verb happily targets canvas#1 (D-focused_canvas_indicator-1/-3).
  //
  // Drawn OUTSIDE the "a frame has landed" branch above, beside the camera picker, so a
  // sized-but-cold pane — already a legitimate framing target — is marked from its first frame
  // rather than blinking on when its first texture arrives (Constraint 5). Draw-list only: no
  // item, no id, no hit-test, no cursor advance, so it perturbs neither `##canvas_nav`'s
  // AllowOverlap arrangement nor any rect an existing item occupies (Constraint 6). It never
  // reaches a libarbc render: this is ImGui chrome over the pane, not composition (Constraint 9).
  if (indicated_view_id() == view_id) {
    // Snap the rect to the pixel grid — the first/last row+column FULLY inside the content rect —
    // and let `AddRect` apply its own half-pixel inset from there, which is what centres the
    // hairline on those pixels' CENTRES. The snapping is load-bearing, not tidiness: a dock
    // splitter can leave `pane_origin` on a fractional pixel, and a stroke centred on a pixel
    // BOUNDARY is split by ImGui's anti-aliasing into two 50%-alpha columns blended against the
    // canvas beneath — exactly the content-dependent, unprobeable stroke
    // D-focused_canvas_indicator-4 rejects. Snapped, it is opaque and one pixel wide at any
    // origin, entirely inside the window clip rect so nothing is shaved. The rect framed is the
    // CONTENT rect: the canvas the verb acts on, not the tab.
    const ImVec2 marker_min(std::ceil(pane_origin.x), std::ceil(pane_origin.y));
    const ImVec2 marker_max(std::floor(pane_origin.x + static_cast<float>(pane_width)),
                            std::floor(pane_origin.y + static_cast<float>(pane_height)));
    if (marker_max.x > marker_min.x + 1.0F &&
        marker_max.y > marker_min.y + 1.0F) { // sub-2px pane: nothing to frame
      ImGui::GetWindowDrawList()->AddRect(marker_min, marker_max, k_focus_marker_color,
                                          /*rounding=*/0.0F, ImDrawFlags_None,
                                          k_focus_marker_thickness);
    }
  }

  // The per-canvas camera picker (D-look_through-5), drawn LAST so it overlays the pane's
  // top-left and stays clickable (the nav button yields via SetNextItemAllowOverlap).
  ImGui::SetCursorScreenPos(pane_origin);
  draw_camera_picker(view_id, p, cameras);
}

void CanvasView::draw_camera_picker(std::string_view view_id, Presenter& p,
                                    const std::vector<scene::Camera>& cameras) {
  // A compact per-canvas picker: "Viewport" (the free camera) plus one entry per shot from
  // scene::cameras. Clicking sets THIS canvas's selection (session state, no transaction).
  // ImGui ids are seeded per window, and each canvas#N draws into its own window, so the two
  // panes carry independent selections (A5/D19) and the e2e drives the buttons by their
  // labels within this canvas#N's window ref. `view_id` is unused today but names the target.
  (void)view_id;
  ImGui::TextUnformatted("Camera:");
  ImGui::SameLine();
  if (ImGui::SmallButton("Viewport")) {
    p.look_through.reset();
  }
  for (const scene::Camera& cam : cameras) {
    ImGui::SameLine();
    if (ImGui::SmallButton(cam.name.c_str())) {
      p.look_through = cam.id;
    }
  }
}

void CanvasView::draw_frame_gizmos(std::string_view view_id, Presenter& p,
                                   const std::vector<scene::Camera>& cameras,
                                   const views::CanvasInput& in, float origin_x, float origin_y) {
  (void)view_id;
  const std::optional<arbc::Affine> view_inv = p.camera.inverse();
  if (!view_inv) {
    return; // degenerate viewport camera: no gizmo this frame
  }
  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  // Composition <-> screen for THIS pane's transient camera (comp -> device, offset by the
  // pane origin). The pointer is reported relative to the image top-left (device px), so its
  // composition position is the inverse-viewport of that.
  auto to_screen = [&](arbc::Vec2 comp) {
    const arbc::Vec2 dev = p.camera.apply(comp);
    return ImVec2(origin_x + static_cast<float>(dev.x), origin_y + static_cast<float>(dev.y));
  };
  const arbc::Vec2 pointer_comp = view_inv->apply(arbc::Vec2{in.focus_x, in.focus_y});
  const double scale = p.camera.max_scale(); // device px per composition unit
  const double edge_tol = scale > 0.0 ? 6.0 / scale : 0.0;
  const double corner_tol = scale > 0.0 ? 9.0 / scale : 0.0;

  const ImU32 col_frame = camera_frame(200);
  const ImU32 col_active = accent(255);

  // Draw one camera's covered-composition rectangle + corner handles, screen-mapped.
  auto draw_frame = [&](const arbc::Affine& frame, int rw, int rh, bool active) {
    const double dw = static_cast<double>(rw);
    const double dh = static_cast<double>(rh);
    const ImVec2 s_tl = to_screen(frame.apply(arbc::Vec2{0.0, 0.0}));
    const ImVec2 s_tr = to_screen(frame.apply(arbc::Vec2{dw, 0.0}));
    const ImVec2 s_br = to_screen(frame.apply(arbc::Vec2{dw, dh}));
    const ImVec2 s_bl = to_screen(frame.apply(arbc::Vec2{0.0, dh}));
    const ImU32 c = active ? col_active : col_frame;
    draw_list->AddQuad(s_tl, s_tr, s_br, s_bl, c, 2.0F);
    constexpr float hs = 3.0F;
    for (const ImVec2& h : {s_tl, s_tr, s_br, s_bl}) {
      draw_list->AddRectFilled(ImVec2(h.x - hs, h.y - hs), ImVec2(h.x + hs, h.y + hs), c);
    }
  };

  // An active grab: preview the dragged frame and commit ONE set_layer_transform on release.
  if (p.gizmo_camera) {
    const scene::Camera* cam = nullptr;
    for (const scene::Camera& c : cameras) {
      if (c.id == *p.gizmo_camera) {
        cam = &c;
        break;
      }
    }
    if (cam == nullptr) { // the shot vanished (undo/GC): drop the grab, fail-safe
      p.gizmo_camera.reset();
      p.gizmo_handle = interact::FrameHandle::None;
      return;
    }
    // The previewed frame from the grab base + current pointer — UNLESS Space is held, in
    // which case the drag is a nav pan of the VIEW, inert on the frame (Constraint 7), so
    // the preview stays at the grab base and the release commits nothing.
    arbc::Affine preview = p.gizmo_start_frame;
    if (!ImGui::IsKeyDown(ImGuiKey_Space)) {
      if (in.rotate) {
        // Dutch: the signed angle swept about the frame center (Shift snaps to 15°, D-manip-5).
        const arbc::Vec2 center =
            p.gizmo_start_frame.apply(arbc::Vec2{p.gizmo_res_w * 0.5, p.gizmo_res_h * 0.5});
        const double a0 =
            std::atan2(p.gizmo_grab_comp.y - center.y, p.gizmo_grab_comp.x - center.x);
        const double a1 = std::atan2(pointer_comp.y - center.y, pointer_comp.x - center.x);
        preview = interact::dutch_frame(p.gizmo_start_frame, p.gizmo_res_w, p.gizmo_res_h, a1 - a0,
                                        in.shift);
      } else if (interact::is_resize_handle(p.gizmo_handle)) {
        preview = interact::recrop_frame(p.gizmo_start_frame, p.gizmo_res_w, p.gizmo_res_h,
                                         p.gizmo_handle, pointer_comp);
      } else { // Move / Label: pan the covered region by the composition-space drag delta
        preview = interact::move_frame(p.gizmo_start_frame, pointer_comp.x - p.gizmo_grab_comp.x,
                                       pointer_comp.y - p.gizmo_grab_comp.y);
      }
    }
    draw_frame(preview, p.gizmo_res_w, p.gizmo_res_h, /*active=*/true);

    if (in.released) {
      const arbc::ObjectId layer = p.gizmo_layer;
      if (!(preview == p.gizmo_start_frame)) { // an inert (e.g. Space-held) drag adds no entry
        apply_edit(
            [this, layer, preview] { state_.document().set_layer_transform(layer, preview); });
      }
      p.gizmo_camera.reset();
      p.gizmo_handle = interact::FrameHandle::None;
    } else if (!in.down) { // lost activation without a release edge: drop the grab, no commit
      p.gizmo_camera.reset();
      p.gizmo_handle = interact::FrameHandle::None;
    }
    return;
  }

  // No active grab: draw all frames and start a grab on a border-press over a handle.
  interact::FrameHandle hover_handle = interact::FrameHandle::None;
  const scene::Camera* hover_cam = nullptr;
  for (const scene::Camera& cam : cameras) {
    // A SELECTED camera reuses the existing `active` frame style — the whole of its selection
    // chrome (D-selection-8): the handles are `editor.cells.gizmo`'s / this leaf's frame grab,
    // and the selection itself justifies exactly an outline.
    draw_frame(cam.frame, cam.resolution.width, cam.resolution.height,
               /*active=*/state_.selection().contains(cam.id));
    if (hover_cam == nullptr) {
      const interact::FrameHandle h =
          interact::hit_frame(cam.frame, cam.resolution.width, cam.resolution.height, pointer_comp,
                              edge_tol, corner_tol);
      if (h != interact::FrameHandle::None) {
        hover_handle = h;
        hover_cam = &cam;
      }
    }
  }
  // A left-press over a frame handle starts the grab — only when Space is up (Space is a nav
  // pan, Constraint 7). The interior is click-through (hit_frame -> None, D7), so an interior
  // press never grabs.
  if (in.pressed && hover_cam != nullptr && !ImGui::IsKeyDown(ImGuiKey_Space)) {
    p.gizmo_camera = hover_cam->id;
    p.gizmo_layer = hover_cam->layer;
    p.gizmo_handle = hover_handle;
    p.gizmo_start_frame = hover_cam->frame;
    p.gizmo_grab_comp = pointer_comp;
    p.gizmo_res_w = hover_cam->resolution.width;
    p.gizmo_res_h = hover_cam->resolution.height;
  }
}

void CanvasView::draw_selection(std::string_view view_id, Presenter& p,
                                const std::vector<interact::PickTarget>& targets,
                                const views::CanvasInput& in, float origin_x, float origin_y) {
  (void)view_id;
  // The ONE project-level selection (D19/A5/A7): read fresh off AppState and written through
  // the same reference. The Presenter holds no copy — only the in-progress marquee gesture.
  commands::Selection& selection = state_.selection();

  // Prune stale ids once per frame, at the one place the live set is already computed
  // (D-selection-7 / Constraint 10): undo, GC, or a delete can drop a selected object, and every
  // consumer this leaf unblocks resolves selected ids against the document. A redo that restores
  // the same object does NOT restore the selection — documented, not a bug.
  const std::vector<arbc::ObjectId> live = interact::all_ids(targets);
  selection.prune(live);

  const std::optional<arbc::Affine> view_inv = p.camera.inverse();
  if (!view_inv) {
    p.marquee_active = false;
    return; // degenerate viewport camera: no picking and no chrome this frame
  }
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  auto to_screen = [&](arbc::Vec2 comp) {
    const arbc::Vec2 dev = p.camera.apply(comp);
    return ImVec2(origin_x + static_cast<float>(dev.x), origin_y + static_cast<float>(dev.y));
  };
  const arbc::Vec2 pointer_comp = view_inv->apply(arbc::Vec2{in.focus_x, in.focus_y});
  // Tolerances in COMPOSITION units, converted from screen px through the view scale — the same
  // recipe the frame gizmo uses, so a camera border stays equally grabbable at every zoom
  // (Constraint 4). A cell body takes none: it is a filled region, not an outline.
  const double scale = p.camera.max_scale();
  const double edge_tol = scale > 0.0 ? 6.0 / scale : 0.0;
  const double corner_tol = scale > 0.0 ? 9.0 / scale : 0.0;
  // Space held ⇒ the drag is a nav pan of the VIEW, inert on objects (Constraint 11).
  const bool space = ImGui::IsKeyDown(ImGuiKey_Space);
  const bool cmd = in.ctrl || in.super; // D7's "Cmd/Ctrl" needs both halves (D-selection-10)

  if (in.pressed && !space) {
    const interact::SelectionChange change =
        interact::click_selection(targets, pointer_comp, edge_tol, corner_tol,
                                  interact::PickModifiers{in.shift, cmd}, selection.primary());
    apply_selection_change(selection, change);
    // The policy returns Clear/None only on a MISS (a hit is always Replace/Toggle), so this is
    // exactly "the press landed on empty canvas" — which starts a marquee, unless the same press
    // just started a camera frame grab.
    const bool missed =
        change.op == interact::SelectOp::Clear || change.op == interact::SelectOp::None;
    if (missed && !p.gizmo_camera) {
      p.marquee_active = true;
      p.marquee_anchor = view_inv->apply(arbc::Vec2{in.press_x, in.press_y});
    }
  }

  if (p.marquee_active) {
    const arbc::Rect rect = marquee_rect(p.marquee_anchor, pointer_comp);
    if (in.released) {
      // Commit on the release edge. A press-and-release with no movement leaves a degenerate
      // rect, so a plain click on empty canvas resolves to Clear and a Shift-click to None —
      // the same answer `click_selection` already gave on the press.
      apply_selection_change(selection, interact::marquee_selection(targets, rect, in.shift));
      p.marquee_active = false;
    } else if (!in.down || space) {
      p.marquee_active = false; // lost activation, or Space took the drag: drop the gesture
    } else {
      // The rubber band, mapped through the view camera as a QUAD so it stays correct under any
      // viewport affine.
      const ImVec2 a = to_screen(arbc::Vec2{rect.x0, rect.y0});
      const ImVec2 b = to_screen(arbc::Vec2{rect.x1, rect.y0});
      const ImVec2 c = to_screen(arbc::Vec2{rect.x1, rect.y1});
      const ImVec2 d = to_screen(arbc::Vec2{rect.x0, rect.y1});
      draw_list->AddQuadFilled(a, b, c, d, accent(40));
      draw_list->AddQuad(a, b, c, d, accent(220), 1.0F);
    }
  }

  // Select-All / Deselect, scoped to the HOVERED pane (D-selection-9) — matching how the `F`
  // reset-to-fit chord already works, and keeping Escape out of a modal's way. "All" means all:
  // cells, cameras, and unbounded content alike, because D7/D20 make them one shape.
  if (in.hovered) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, /*repeat=*/false)) {
      selection.clear();
    } else if (cmd && ImGui::IsKeyPressed(ImGuiKey_A, /*repeat=*/false)) {
      selection.replace_all(live);
    }
  }

  // Selection chrome: a stroked quad along each selected CELL's placed outline (D-selection-8).
  // Selected cameras are drawn by draw_frame_gizmos in its existing `active` style, and
  // unbounded content has no outline to draw — it covers the plane, which is the honest picture.
  for (const interact::PickTarget& target : targets) {
    if (target.kind != interact::PickKind::Cell || !selection.contains(target.id)) {
      continue;
    }
    const std::optional<std::array<arbc::Vec2, 4>> quad = interact::placed_quad(target);
    if (!quad) {
      continue;
    }
    draw_list->AddQuad(to_screen((*quad)[0]), to_screen((*quad)[1]), to_screen((*quad)[2]),
                       to_screen((*quad)[3]), accent(255), 2.0F);
  }
}

void CanvasView::set_look_through(std::string_view view_id, std::optional<arbc::ObjectId> shot) {
  auto it = presenters_.find(view_id);
  if (it != presenters_.end()) {
    it->second.look_through = shot;
  }
}

std::optional<arbc::ObjectId> CanvasView::look_through(std::string_view view_id) const {
  auto it = presenters_.find(view_id);
  return it == presenters_.end() ? std::nullopt : it->second.look_through;
}

void CanvasView::apply_edit(const std::function<void()>& edit) {
  // ONE unit of writer-thread work: the mutation plus the A18 epilogue, so the epilogue reads the
  // writer-owned structure the edit just changed without a second round trip and without any
  // window in which another submitter could interleave.
  writer_.submit_sync([&] {
    // The identity tripwire, at the ONE point every document mutation passes through
    // (D-writer_thread-1): libarbc's own predicate, asserted rather than commented. It fires if
    // the writer was bound by something other than the posted open — i.e. the exact bug this leaf
    // removes, and the one a `SlotStore::allocate` assert would otherwise report much deeper in.
    assert(state_.document().on_writer_thread() && "edits must run on the document's writer");
    edit();
    if (post_edit_hook_) {
      post_edit_hook_();
    }
  });
  // Then wake every live entry to re-render the damage (one writer, N observers — Constraint 4).
  // A refused submission (the writer is stopping) ran nothing, and a poke on a stopping host is
  // inert, so the unconditional wake is correct either way.
  host_.poke();
}

void CanvasView::on_writer(const std::function<void()>& work) { writer_.submit_sync(work); }

void CanvasView::set_post_edit_hook(std::function<void()> hook) {
  post_edit_hook_ = std::move(hook);
}

void CanvasView::poke() { host_.poke(); }

void CanvasView::reconcile(const std::vector<std::string>& live_view_ids) {
  // Drop any presenter whose canvas#N left the dock layout: free its host entry (the
  // render thread frees the cache) and its GL texture (here, on the UI thread with a live
  // context) — Constraint 7 / D-multi_canvas-5.
  for (auto it = presenters_.begin(); it != presenters_.end();) {
    if (std::find(live_view_ids.begin(), live_view_ids.end(), it->first) == live_view_ids.end()) {
      if (it->second.texture != 0) {
        gl::destroy_texture(it->second.texture);
      }
      // Drop the sticky focus hint with the pane it named (D-mint_from_focused_canvas-2).
      // The rule already degrades a stale id to the lowest-id fallback, so this is hygiene
      // rather than correctness — but view ids are minted from the layout and can be REUSED
      // after a close, and without the clear a freshly-opened canvas#2 would inherit the
      // previous canvas#2's focus without ever having been focused.
      if (it->first == focused_view_id_) {
        focused_view_id_.clear();
      }
      host_.remove(it->first);
      it = presenters_.erase(it);
    } else {
      ++it;
    }
  }
}

std::uint64_t CanvasView::frames_issued(std::string_view view_id) const {
  return host_.published_sequence(view_id);
}

std::size_t CanvasView::anchor_depth(std::string_view view_id) const {
  return host_.anchor_depth(view_id);
}

double CanvasView::scale_bar_units(std::string_view view_id) const {
  auto it = presenters_.find(view_id);
  return it == presenters_.end() ? 0.0 : it->second.scale_bar_units;
}

std::vector<PaneFraming> CanvasView::pane_rows() const {
  // `presenters_` is key-ordered but its keys are ordered by BYTES (`std::less<>`), which puts
  // "canvas#10" ahead of "canvas#2" — so the projection is re-ordered HERE, through dockmodel's
  // canonical `view_id_less`, before the rule ever sees it. That is the whole ordering
  // obligation and it lives on this side of the seam on purpose: `focus_target` consumes the
  // caller's span verbatim and stays id-format-agnostic, so there is exactly ONE authority on
  // "which id is lower" (D-view_id_natural_order-4, inherited D-focused_canvas_indicator-1).
  // With this order in hand "canvas#2" wins the fallback over "canvas#10", which is what D23's
  // numerically-lowest-id rule says. The `string_view` keys borrow the map's own storage, which
  // outlives the returned vector — the sort permutes the rows, never the keys they point into —
  // which is why `indicated_view_id()` may hand a `string_view` back out of a scope this vector
  // does not survive.
  std::vector<PaneFraming> panes;
  panes.reserve(presenters_.size());
  for (const auto& [id, presenter] : presenters_) {
    panes.push_back(PaneFraming{id, ViewFraming{presenter.framing_camera, presenter.requested_width,
                                                presenter.requested_height}});
  }
  std::sort(panes.begin(), panes.end(), [](const PaneFraming& a, const PaneFraming& b) {
    return dockmodel::view_id_less(a.view_id, b.view_id);
  });
  return panes;
}

ViewFraming CanvasView::framing_for(std::string_view focused) const {
  // Runs only on a user action (a mint or an insert), never per frame.
  const std::vector<PaneFraming> panes = pane_rows();
  return framing_for_focus(panes, focused);
}

ViewFraming CanvasView::primary_framing() const {
  // The EMPTY-focus projection of the shared rule: the first (lowest-id) live, sized pane —
  // a deterministic choice, not the most-recently-drawn one, and deliberately unchanged
  // (D-mint_from_focused_canvas-6). A pane that has never been sized carries no framing.
  return framing_for(std::string_view{});
}

ViewFraming CanvasView::focused_framing() const { return framing_for(focused_view_id_); }

std::string_view CanvasView::focused_view_id() const { return focused_view_id_; }

std::string_view CanvasView::indicated_view_id() const {
  // The SAME rule `focused_framing()` runs, projected onto the pane's NAME instead of its
  // framing (D-focused_canvas_indicator-1): the marker therefore follows the fallback branch
  // too, and cannot report a pane other than the one a verb would act on. The returned view
  // borrows `presenters_`'s keys, not `panes`, so it outlives this frame's projection.
  const std::vector<PaneFraming> panes = pane_rows();
  return focus_target(panes, focused_view_id_);
}

void CanvasView::destroy() {
  // Deterministic teardown (Constraint 5): stop the loop, wake it, and join BEFORE
  // releasing any GL texture or destroying the host. The render thread touches no GL, so
  // the join needs no live context; no thread outlives this view. The host's entries then
  // tear down before its shared pool (Constraint 2).
  if (render_thread_) {
    host_.stop();
    render_thread_->join();
    render_thread_.reset();
  }
  for (auto& [id, p] : presenters_) {
    if (p.texture != 0) {
      gl::destroy_texture(p.texture);
      p.texture = 0;
    }
  }
  presenters_.clear();
}

} // namespace ace::app
