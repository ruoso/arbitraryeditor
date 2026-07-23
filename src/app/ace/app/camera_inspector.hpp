#pragma once

#include <arbc/base/ids.hpp>

#include <string_view>

namespace ace::commands {
class AppState;
} // namespace ace::commands

namespace ace::app {

class CanvasView;

// The first-cut camera resolution inspector (editor.cameras.manip; D-manip-6): a compact
// ViewType::Inspector body that lists the shot cameras and edits the targeted camera's
// output resolution — W×H numeric (the "type the resolution" control, D7; the frame is
// unchanged) plus aspect presets (the frame follows, D-manip-7). Every edit is an undoable
// Document transaction routed through CanvasView::apply_edit (the settled edit-render-sync
// seam), so it is serialized against the render read and wakes every live canvas. The dense
// multi-property Inspector sheet, the per-camera resolution-health readout, cross-object
// snapping, and unified ObjectId-keyed selection are already-scheduled sibling leaves' scope
// (Constraint 8); this leaf targets a camera by a compact chooser (no selection dependency).
// UI-thread-only session state (the target + the pending W×H fields); no new threading.
class CameraInspector {
public:
  CameraInspector(commands::AppState& state, CanvasView& canvas);

  // Draw the resolution inspector into the CURRENT ImGui window (the dockspace owns the
  // enclosing Begin/End). `view_id` is the Inspector instance id (unused — a singleton).
  void draw(std::string_view view_id);

private:
  commands::AppState& state_;
  CanvasView& canvas_;
  arbc::ObjectId target_;    // the camera the inspector edits (invalid => the first camera)
  int width_ = 0;            // the pending output-width edit field
  int height_ = 0;           // the pending output-height edit field
  arbc::ObjectId synced_id_; // which camera width_/height_ were last synced from
  int synced_w_ = 0;
  int synced_h_ = 0;
};

} // namespace ace::app
