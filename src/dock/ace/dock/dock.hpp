#pragma once

#include <ace/dockmodel/dockmodel.hpp>
#include <ace/dockmodel/tool_rail.hpp>
#include <ace/dockmodel/view_registry.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace ace::dock {

// The dock component. See docs/01-architecture.md §8 (component levelization).
const char* name();

// The default starter arrangement (the eight-type catalog is opened lazily by
// the launcher — tool_rail): a Canvas fills one side; Inspector / Layers /
// Overview share the other side as a tab-group. Deterministic — the "rebuild the
// default" side of the close-everything round-trip.
std::vector<ace::dockmodel::ViewType> default_initial_views();

// The main-viewport dockspace host (D18 fully-uniform shell). It owns a
// ViewRegistry + the authoritative DockLayout (D-view-registry-2): each frame it
// submits a full-viewport DockSpace and draws every open view by its instance id
// via views::draw_view, inside a window carrying a `bool* p_open`. When ImGui
// clears p_open (the tab ✕), it routes the close through the L1 registry so the
// DockLayout — not ImGui's transient tree — stays the single source of truth
// reopen and workspaces read (D-view-registry-6). On the first frame (and after a
// programmatic open/reopen) it translates the DockLayout into ImGui's live tree
// via DockBuilder (io.IniFilename == nullptr → built in-code, not from imgui.ini).
class Dockspace {
public:
  // Seed by opening `initial` view types in order through the owned registry (so
  // instance ids + the multi-instance counter stay consistent). The first fills
  // a lone root leaf; the rest arrange a split-off panel column.
  explicit Dockspace(std::vector<ace::dockmodel::ViewType> initial = default_initial_views());

  // Draw one frame of the dockspace host + every open view body. Call inside the
  // shell's draw-content.
  void draw();

  // The ImGui dockspace node id (an ImGuiID), valid after the first draw(). The
  // e2e uses it to assert the host node exists (DockBuilderGetNode != null).
  unsigned int dockspace_id() const { return dockspace_id_; }

  // Catalog-driven mutation the launcher (tool_rail) and the e2e drive by type /
  // id. open/reopen mint through the registry and mark the live tree for a
  // re-seed next draw() so the new window docks; close routes through the model.
  std::string open(ace::dockmodel::ViewType type);
  std::string reopen(ace::dockmodel::ViewType type);
  bool close(std::string_view view_id);

  // The authoritative layout (the set of open views is layout().view_ids()).
  const ace::dockmodel::DockLayout& layout() const { return layout_; }

  // The rail's active-tool selection (A11). Observable UI state the tool rail
  // mutates on click; nothing on the canvas reads it yet (D-tool_rail-4). The e2e
  // reads it through this accessor to assert the active tool after a click.
  ace::dockmodel::ToolSelection& tools() { return tools_; }
  const ace::dockmodel::ToolSelection& tools() const { return tools_; }

private:
  ace::dockmodel::ViewRegistry registry_;
  ace::dockmodel::DockLayout layout_;
  ace::dockmodel::ToolSelection tools_; // the rail's active-tool selection (A11)
  bool built_ = false;                  // DockBuilder seeded at least once
  bool rebuild_ = false;                // a programmatic open/reopen needs a re-seed
  unsigned int dockspace_id_ = 0;       // ImGuiID; assigned on first draw()
};

// The stable ImGui window id of the fixed tool rail — exposed so the e2e can
// drive the rail's buttons under the same id the rail draws under (Constraint 7).
const char* tool_rail_title();

// Draw the fixed left tool rail (D18 / §10 "home base"): the view launcher (one
// entry per view_catalog() type, click to open/focus via Dockspace::open) plus
// the modal tools (Select/Brush/Eyedropper/Pan over `dockspace.tools()`). Chrome,
// not a view: always present, never closable/dockable (D-tool_rail-3). Called by
// Dockspace::draw() inside the fixed left rail window; the dockspace host fills
// the work area to its right, so the rail reserves space and nothing overlaps it.
void draw_tool_rail(Dockspace& dockspace);

} // namespace ace::dock
