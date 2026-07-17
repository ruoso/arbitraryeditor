#include <ace/dock/dock.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/views/views.hpp>

#include <imgui.h>
#include <imgui_internal.h> // DockBuilder* (the WIP docking-builder API)

#include <string>
#include <utility>
#include <vector>

namespace ace::dock {
namespace {

using ace::dockmodel::SplitOrientation;
using ace::dockmodel::ViewType;

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
  const ImGuiDir dir =
      node.orientation == SplitOrientation::Horizontal ? ImGuiDir_Left : ImGuiDir_Up;
  ImGuiID first_id = 0;
  ImGuiID second_id = 0;
  ImGui::DockBuilderSplitNode(node_id, dir, static_cast<float>(node.ratio), &first_id, &second_id);
  build_node(first_id, node.children[0]);
  build_node(second_id, node.children[1]);
}

} // namespace

const char* name() { return "dock"; }

std::vector<ViewType> default_initial_views() {
  return {ViewType::Canvas, ViewType::Inspector, ViewType::Layers, ViewType::Overview};
}

Dockspace::Dockspace(std::vector<ViewType> initial) {
  if (initial.empty()) {
    return;
  }
  // The first view fills a lone root leaf; the second splits off a panel column;
  // the rest tab into that column. Minting flows through the registry so the
  // instance ids and the multi-instance counter stay consistent with the layout.
  const std::string canvas = registry_.open(layout_, initial.front());
  if (initial.size() == 1) {
    return;
  }
  const std::string column = registry_.mint_id(initial[1]);
  layout_.split_leaf(canvas, SplitOrientation::Horizontal, 0.62, column);
  for (std::size_t i = 2; i < initial.size(); ++i) {
    layout_.insert_tab(column, registry_.mint_id(initial[i]));
  }
}

void Dockspace::draw() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  // Full-window uniform dockspace over the main viewport, no reserved central
  // node — every node is a peer (D18 / D-dockspace-5). The returned id is stable
  // per viewport, so it is safe to build against.
  dockspace_id_ = ImGui::DockSpaceOverViewport(0, viewport, ImGuiDockNodeFlags_None);

  if (!layout_.empty() && (!built_ || rebuild_)) {
    // Seed / re-seed the live tree from the authoritative DockLayout: on the
    // first frame, and after a programmatic open/reopen so the new window docks
    // per the model (the model is the source of truth — D-view-registry-6).
    ImGui::DockBuilderRemoveNode(dockspace_id_);
    ImGui::DockBuilderAddNode(dockspace_id_, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id_, viewport->WorkSize);
    build_node(dockspace_id_, layout_.root());
    ImGui::DockBuilderFinish(dockspace_id_);
    built_ = true;
    rebuild_ = false;
  }

  // Draw every open view by its instance id; a cleared p_open (the tab ✕) routes
  // through the L1 close so the model stays authoritative.
  std::vector<std::string> to_close;
  for (const std::string& id : layout_.view_ids()) {
    bool open = true;
    if (ImGui::Begin(id.c_str(), &open)) {
      views::draw_view(id);
    }
    ImGui::End();
    if (!open) {
      to_close.push_back(id);
    }
  }
  for (const std::string& id : to_close) {
    registry_.close(layout_, id);
  }
}

std::string Dockspace::open(ViewType type) {
  std::string id = registry_.open(layout_, type);
  rebuild_ = true; // the new window must dock into the model layout next frame
  return id;
}

std::string Dockspace::reopen(ViewType type) {
  std::string id = registry_.reopen(layout_, type);
  rebuild_ = true;
  return id;
}

bool Dockspace::close(std::string_view view_id) { return registry_.close(layout_, view_id); }

} // namespace ace::dock
