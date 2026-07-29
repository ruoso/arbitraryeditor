#pragma once

#include <ace/app/camera_inspector.hpp>

#include <arbc/base/ids.hpp>

#include <string_view>

namespace ace::commands {
class AppState;
} // namespace ace::commands

namespace ace::app {

class CanvasView;

// The dense per-object property sheet (editor.panels.inspector; D7/§6): the real body for
// `ViewType::Inspector`, keyed to the shared selection's primary object (D-inspector, A18 read
// seam). For a CELL it draws three stacked blocks — Transform (units) READOUT, Appearance &
// stacking (opacity slider + visibility toggle + z-order readout), and Source (provenance +
// resolution health). For a CAMERA it defers to the shipped `CameraInspector` block (output
// resolution + capture health), kept intact and routed to when the primary is a camera. With
// nothing selected it shows a defined empty state (the `prune` guarantee makes a dangling id
// rare). Every cell edit routes through a `scene` verb dispatched on `CanvasView::apply_edit`
// (the one writer funnel, D-inspector-2); reads use the shipped UI-thread scene read-seam
// (A18 / D-inspector-6). UI-thread-only session state (only the delegated camera block's).
class InspectorPanel {
public:
  InspectorPanel(commands::AppState& state, CanvasView& canvas);

  // Draw the sheet into the CURRENT ImGui window (the dockspace owns the enclosing Begin/End).
  // `view_id` is the Inspector instance id (a singleton), forwarded to the camera block.
  void draw(std::string_view view_id);

private:
  commands::AppState& state_;
  CanvasView& canvas_;
  CameraInspector camera_; // the shipped camera block, reused verbatim for a selected camera
};

} // namespace ace::app
