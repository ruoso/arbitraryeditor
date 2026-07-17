#include <ace/dockmodel/tool_rail.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ace::dockmodel {
namespace {

// The fixed tool catalog (D20 / D-tool_rail-2). Ordered by ToolId value so
// tool_descriptor(id) is a direct index. Select is the default (D7).
constexpr std::array<ToolDescriptor, k_tool_count> k_tool_catalog = {{
    {ToolId::Select, "select", "Select"},
    {ToolId::Brush, "brush", "Brush"},
    {ToolId::Eyedropper, "eyedropper", "Eyedropper"},
    {ToolId::Pan, "pan", "Pan"},
}};

std::size_t index_of(ToolId id) { return static_cast<std::size_t>(id); }

} // namespace

const std::array<ToolDescriptor, k_tool_count>& tool_catalog() { return k_tool_catalog; }

const ToolDescriptor& tool_descriptor(ToolId id) { return k_tool_catalog[index_of(id)]; }

std::string_view tool_slug(ToolId id) { return tool_descriptor(id).slug; }

std::string_view tool_title(ToolId id) { return tool_descriptor(id).title; }

std::vector<LauncherEntry> launcher_entries(const DockLayout& layout) {
  const std::vector<std::string> ids = layout.view_ids();
  std::vector<LauncherEntry> entries;
  entries.reserve(k_view_type_count);
  for (const ViewDescriptor& desc : view_catalog()) {
    // Open iff any open instance id parses to this type — the open-set IS
    // view_ids(), never a rail-side mirror (D-tool_rail-5).
    bool is_open = false;
    for (const std::string& id : ids) {
      const std::optional<ParsedViewId> parsed = parse_view_id(id);
      if (parsed && parsed->type == desc.type) {
        is_open = true;
        break;
      }
    }
    entries.push_back({desc.type, is_open});
  }
  return entries;
}

} // namespace ace::dockmodel
