#pragma once

#include <ace/dockmodel/dockmodel.hpp>
#include <ace/dockmodel/view_registry.hpp>

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

namespace ace::dockmodel {

// The modal tools (docs/00-design.md D20): the persistent pointer modes that
// determine what a plain canvas drag does. Exactly Select/Brush/Eyedropper/Pan —
// the WBS note's "camera" is folded into the one Select tool (D7) and "import" is
// drop/paste-gesture-driven, not a mode (D12). The enumerator order is the
// catalog order. Pure value data — no ImGui (A8 / refinement Constraint 2).
enum class ToolId { Select, Brush, Eyedropper, Pan };

// The number of modal tools — the tool catalog is exactly this size.
inline constexpr std::size_t k_tool_count = 4;

// One tool catalog entry: the id, its stable id stem (`slug`, the rail button id)
// and its human display `title`. No ImGui, no handles — the model stays
// headless-testable (A8 / refinement Constraint 2).
struct ToolDescriptor {
  ToolId id;
  std::string_view slug;  // e.g. "select" — the rail-button id stem
  std::string_view title; // e.g. "Select" — the display title
};

// The fixed tool catalog, indexed by ToolId value (exactly k_tool_count).
const std::array<ToolDescriptor, k_tool_count>& tool_catalog();

// The descriptor for `id` / its slug / its title (index tool_catalog() by value).
const ToolDescriptor& tool_descriptor(ToolId id);
std::string_view tool_slug(ToolId id);
std::string_view tool_title(ToolId id);

// The active-tool selection (docs/01-architecture.md A11): the headless UI state
// holding which modal tool is active. Default is Select (D20 / D7). At this leaf
// the selection is OBSERVABLE STATE ONLY — nothing on the canvas reads it yet;
// wiring the active tool to canvas interaction is deferred to
// editor.canvas.tool_dispatch (D20 / D-tool_rail-4).
class ToolSelection {
public:
  ToolId active() const { return active_; }
  void select(ToolId id) { active_ = id; }

private:
  ToolId active_ = ToolId::Select;
};

// One launcher entry: a view type and whether an instance of it is currently open
// in the layout. The launcher reflects open state from DockLayout::view_ids()
// (D-tool_rail-5) — it keeps NO open-set of its own, so it can never drift.
struct LauncherEntry {
  ViewType type;
  bool is_open;
  bool operator==(const LauncherEntry&) const = default;
};

// The launcher model for `layout`: one entry per view_catalog() type in catalog
// order, `is_open` true iff `layout` holds any instance of that type (a singleton
// under its bare slug, a Canvas under any canvas#N). An empty layout → every
// entry closed; opening a type flips its entry to open. This is the home-base
// model the rail draws (docs/00-design.md §10 :446-450).
std::vector<LauncherEntry> launcher_entries(const DockLayout& layout);

} // namespace ace::dockmodel
