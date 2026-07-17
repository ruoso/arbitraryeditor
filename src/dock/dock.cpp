#include <ace/dock/dock.hpp>
#include <ace/dockmodel/tool_rail.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/views/views.hpp>

#include <imgui.h>
#include <imgui_internal.h> // DockBuilder* (the WIP docking-builder API)

#include <string>
#include <utility>
#include <vector>

namespace ace::dock {
namespace {

using ace::dockmodel::LauncherEntry;
using ace::dockmodel::SplitOrientation;
using ace::dockmodel::ToolDescriptor;
using ace::dockmodel::ViewType;

// The fixed rail width: enough to hold the widest text label ("Eyedropper") this
// leaf — icon art is deferred to editor.packaging (Open questions / Constraint).
constexpr float k_rail_width = 140.0f;

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

const char* tool_rail_title() { return "Tool Rail"; }

void draw_tool_rail(Dockspace& dockspace) {
  // Modal tools (D20): a single active-tool selection over the tool catalog. The
  // selection is observable state ONLY — nothing on the canvas reads it yet
  // (D-tool_rail-4). Each button carries a stable text id so the e2e can click it.
  ImGui::TextUnformatted("Tools");
  const ace::dockmodel::ToolId active = dockspace.tools().active();
  for (const ToolDescriptor& tool : ace::dockmodel::tool_catalog()) {
    const std::string label(tool.title);
    if (ImGui::Selectable(label.c_str(), tool.id == active)) {
      dockspace.tools().select(tool.id);
    }
  }

  ImGui::Separator();

  // The view launcher (§10 :446-450, the home base): one entry per view type,
  // click to open — or, for a singleton already present, focus — via the existing
  // Dockspace surface (Dockspace::open is idempotent for singletons, mints a fresh
  // canvas#N for the multi-instance Canvas). `is_open` reflects the authoritative
  // layout (D-tool_rail-5), so with the layout empty one click restores the view —
  // the home-base guarantee that nothing can be lost.
  ImGui::TextUnformatted("Views");
  for (const LauncherEntry& entry : ace::dockmodel::launcher_entries(dockspace.layout())) {
    const std::string label(ace::dockmodel::view_title(entry.type));
    if (ImGui::Selectable(label.c_str(), entry.is_open)) {
      dockspace.open(entry.type);
    }
  }
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
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImVec2 work_pos = viewport->WorkPos;
  const ImVec2 work_size = viewport->WorkSize;

  // The fixed left tool rail (home base): a real, non-dockable window pinned to
  // the left of the work area, sized deterministically each frame (no viewport
  // work-inset feedback — that lags a frame and jitters the docked geometry). It
  // reserves k_rail_width; the dockspace host below fills the remainder, so no
  // view ever overlaps the rail. Chrome — NoTitleBar/NoResize/NoMove/NoDocking —
  // so it can never be closed or docked (D-tool_rail-3).
  ImGui::SetNextWindowPos(work_pos);
  ImGui::SetNextWindowSize(ImVec2(k_rail_width, work_size.y));
  const ImGuiWindowFlags rail_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::Begin(tool_rail_title(), nullptr, rail_flags)) {
    draw_tool_rail(*this);
  }
  ImGui::End();

  // The uniform dockspace host fills the work area to the RIGHT of the rail — a
  // borderless, immovable host window carrying the DockSpace node, no reserved
  // central node so every node is a peer (D18 / D-dockspace-5). NoBringToFront so
  // the docked view windows always render above the host.
  const ImVec2 host_size(work_size.x - k_rail_width, work_size.y);
  ImGui::SetNextWindowPos(ImVec2(work_pos.x + k_rail_width, work_pos.y));
  ImGui::SetNextWindowSize(host_size);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  const ImGuiWindowFlags host_flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
  ImGui::Begin("##dockspace_host", nullptr, host_flags);
  ImGui::PopStyleVar(3);
  // The node id is hashed against the host window; it is stable per frame and is
  // what the DockBuilder seed + every view's DockBuilderDockWindow target.
  dockspace_id_ = ImGui::GetID("dockspace");
  ImGui::DockSpace(dockspace_id_, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

  if (!layout_.empty() && (!built_ || rebuild_)) {
    // Seed / re-seed the live tree from the authoritative DockLayout: on the
    // first frame, and after a programmatic open/reopen so the new window docks
    // per the model (the model is the source of truth — D-view-registry-6).
    ImGui::DockBuilderRemoveNode(dockspace_id_);
    ImGui::DockBuilderAddNode(dockspace_id_, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id_, host_size);
    build_node(dockspace_id_, layout_.root());
    ImGui::DockBuilderFinish(dockspace_id_);
    built_ = true;
    rebuild_ = false;
  }
  ImGui::End(); // ##dockspace_host

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
