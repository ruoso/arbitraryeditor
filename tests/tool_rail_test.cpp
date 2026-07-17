#include <ace/dockmodel/dockmodel.hpp>
#include <ace/dockmodel/tool_rail.hpp>
#include <ace/dockmodel/view_registry.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <vector>

// The L1 tool-rail headless unit suite (docs/01-architecture.md §9 :187): the
// fixed four-tool catalog, the default-Select active-tool selection, and the
// launcher model derived from a DockLayout (D20 / A11 / D-tool_rail-2/-5).
// UI-agnostic — no ImGui (A8 / refinement Constraint 2).
using ace::dockmodel::DockLayout;
using ace::dockmodel::LauncherEntry;
using ace::dockmodel::ToolId;
using ace::dockmodel::ToolSelection;
using ace::dockmodel::ViewRegistry;
using ace::dockmodel::ViewType;
namespace dm = ace::dockmodel;

namespace {
bool entry_open(const std::vector<LauncherEntry>& entries, ViewType type) {
  for (const LauncherEntry& e : entries) {
    if (e.type == type) {
      return e.is_open;
    }
  }
  return false;
}
} // namespace

TEST_CASE("tool_rail: the catalog is exactly Select/Brush/Eyedropper/Pan in order") {
  const auto& catalog = dm::tool_catalog();
  REQUIRE(catalog.size() == 4);
  REQUIRE(dm::k_tool_count == 4);

  // Every entry's `id` field indexes its own catalog slot (enum == order).
  for (std::size_t i = 0; i < catalog.size(); ++i) {
    CHECK(static_cast<std::size_t>(catalog[i].id) == i);
  }

  struct Expected {
    ToolId id;
    const char* slug;
    const char* title;
  };
  const Expected expected[] = {
      {ToolId::Select, "select", "Select"},
      {ToolId::Brush, "brush", "Brush"},
      {ToolId::Eyedropper, "eyedropper", "Eyedropper"},
      {ToolId::Pan, "pan", "Pan"},
  };
  for (const Expected& e : expected) {
    const dm::ToolDescriptor& d = dm::tool_descriptor(e.id);
    CHECK(d.slug == e.slug);
    CHECK(d.title == e.title);
    CHECK(dm::tool_slug(e.id) == e.slug);
    CHECK(dm::tool_title(e.id) == e.title);
  }

  // No "import" / "camera" mode: the note's casual six-item list is reconciled to
  // four (D20 / D-tool_rail-2) — camera folded into Select (D7), import not a mode.
  CHECK(catalog.size() == 4);
}

TEST_CASE("tool_rail: ToolSelection defaults to Select and round-trips every tool") {
  ToolSelection sel;
  CHECK(sel.active() == ToolId::Select); // default is Select (D20 / D7)

  for (const dm::ToolDescriptor& d : dm::tool_catalog()) {
    sel.select(d.id);
    CHECK(sel.active() == d.id); // selecting each catalog entry is observable
  }

  // Re-selecting Select round-trips back to the default.
  sel.select(ToolId::Select);
  CHECK(sel.active() == ToolId::Select);
}

TEST_CASE("tool_rail: launcher_entries mirrors the layout's open set in catalog order") {
  // An empty layout → one entry per view type, all closed.
  DockLayout empty;
  const std::vector<LauncherEntry> none = dm::launcher_entries(empty);
  REQUIRE(none.size() == dm::k_view_type_count);
  for (std::size_t i = 0; i < none.size(); ++i) {
    // Catalog order: entry i is the i-th view type.
    CHECK(none[i].type == dm::view_catalog()[i].type);
    CHECK_FALSE(none[i].is_open);
  }

  // Opening a singleton flips exactly that type's entry to open; the rest stay
  // closed — the launcher keeps no open-set of its own (D-tool_rail-5).
  ViewRegistry reg;
  DockLayout layout;
  reg.open(layout, ViewType::Inspector);
  const std::vector<LauncherEntry> one = dm::launcher_entries(layout);
  CHECK(entry_open(one, ViewType::Inspector));
  CHECK_FALSE(entry_open(one, ViewType::Canvas));
  CHECK_FALSE(entry_open(one, ViewType::Layers));

  // A multi-instance Canvas (canvas#N) also reads as open for the Canvas entry.
  reg.open(layout, ViewType::Canvas);
  const std::vector<LauncherEntry> two = dm::launcher_entries(layout);
  CHECK(entry_open(two, ViewType::Canvas));
  CHECK(entry_open(two, ViewType::Inspector));
  CHECK_FALSE(entry_open(two, ViewType::Export));
}
