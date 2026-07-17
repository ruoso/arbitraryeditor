#pragma once

#include <ace/dockmodel/dockmodel.hpp>

namespace ace::dock {

// The dock component. See docs/01-architecture.md §8 (component levelization).
const char* name();

// Stable ImGui window ids of the two plain placeholder panels. Until
// editor.dock.view_registry supplies a real view catalog, these (plus the
// render_probe pane as a canvas stand-in) exercise the docking mechanics and
// anchor the e2e (refinement D-dockspace-4). Exposed so the e2e drives them by
// the same id they draw under.
const char* panel_a_title();
const char* panel_b_title();

// The single bootstrap layout (refinement Constraint 7): the render_probe pane
// (canvas stand-in — D18 "canvas is a view") fills one side, the two plain
// panels share the other side as a tab-group. The whole viewport is tiled with
// peers — no reserved central node (D-dockspace-5).
ace::dockmodel::DockLayout default_layout();

// The main-viewport dockspace host (D18 fully-uniform shell). Each frame it
// submits a full-viewport DockSpace and draws the plain placeholder panels; on
// the first frame it translates its seed DockLayout into ImGui's live dock tree
// via DockBuilder (io.IniFilename == nullptr → the layout is built in-code, not
// restored from imgui.ini — D-dockspace-3). ImGui then owns the interactive
// drag-to-dock / split / resize / tab mechanics (D-dockspace-1). The render_probe
// pane is drawn by the app layer (it owns the GL texture); DockBuilder still
// assigns it to its node by the same window id.
class Dockspace {
public:
  explicit Dockspace(ace::dockmodel::DockLayout layout);

  // Draw one frame of the dockspace host + placeholder panels. Call inside the
  // shell's draw-content, before the app's render_probe pane is drawn.
  void draw();

  // The ImGui dockspace node id (an ImGuiID), valid after the first draw(). The
  // e2e uses it to assert the host node exists (DockBuilderGetNode != null).
  unsigned int dockspace_id() const { return dockspace_id_; }

private:
  ace::dockmodel::DockLayout layout_;
  bool built_ = false;
  unsigned int dockspace_id_ = 0; // ImGuiID; assigned on first draw()
};

} // namespace ace::dock
