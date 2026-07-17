#include <ace/dock/dock.hpp>
#include <ace/views/views.hpp>

#include <imgui.h>
#include <imgui_internal.h> // DockBuilder* (the WIP docking-builder API)

#include <string>
#include <utility>

namespace ace::dock {
namespace {

constexpr const char* k_panel_a_title = "Panel A";
constexpr const char* k_panel_b_title = "Panel B";

// Recursively translate a DockLayout subtree into ImGui dock nodes rooted at
// `node_id`: a split creates two children (H → left|right, V → top|bottom) and
// recurses; a leaf docks each of its view ids into the node so the matching
// window snaps there when it Begin()s (D-dockspace-1/-3).
void build_node(ImGuiID node_id, const ace::dockmodel::DockNode& node) {
  if (node.is_leaf()) {
    for (const std::string& view : node.tabs) {
      ImGui::DockBuilderDockWindow(view.c_str(), node_id);
    }
    return;
  }
  const ImGuiDir dir = node.orientation == ace::dockmodel::SplitOrientation::Horizontal
                           ? ImGuiDir_Left
                           : ImGuiDir_Up;
  ImGuiID first_id = 0;
  ImGuiID second_id = 0;
  ImGui::DockBuilderSplitNode(node_id, dir, static_cast<float>(node.ratio), &first_id, &second_id);
  build_node(first_id, node.children[0]);
  build_node(second_id, node.children[1]);
}

// A plain placeholder pane. Always paired End() (docked windows still require
// it). Real view content arrives with editor.dock.view_registry.
void draw_panel(const char* title) {
  if (ImGui::Begin(title)) {
    ImGui::TextUnformatted("Placeholder pane — editor.dock.view_registry supplies real views.");
  }
  ImGui::End();
}

} // namespace

const char* name() { return "dock"; }

const char* panel_a_title() { return k_panel_a_title; }

const char* panel_b_title() { return k_panel_b_title; }

ace::dockmodel::DockLayout default_layout() {
  return ace::dockmodel::DockLayout::make_default(
      {ace::views::probe_pane_title(), panel_a_title(), panel_b_title()});
}

Dockspace::Dockspace(ace::dockmodel::DockLayout layout) : layout_(std::move(layout)) {}

void Dockspace::draw() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  // Full-window uniform dockspace over the main viewport, no reserved central
  // node — every node is a peer (D18 / D-dockspace-5). The returned id is stable
  // per viewport, so it is safe to build against on the first frame.
  dockspace_id_ = ImGui::DockSpaceOverViewport(0, viewport, ImGuiDockNodeFlags_None);

  if (!built_ && !layout_.empty()) {
    // Seed the live tree from the DockLayout exactly once; ImGui owns it after.
    ImGui::DockBuilderRemoveNode(dockspace_id_);
    ImGui::DockBuilderAddNode(dockspace_id_, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id_, viewport->WorkSize);
    build_node(dockspace_id_, layout_.root());
    ImGui::DockBuilderFinish(dockspace_id_);
    built_ = true;
  }

  draw_panel(panel_a_title());
  draw_panel(panel_b_title());
}

} // namespace ace::dock
