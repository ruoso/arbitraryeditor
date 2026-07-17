#include <ace/dockmodel/view_registry.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ace::dockmodel {
namespace {

// The fixed catalog (D-view-registry-3). Ordered by ViewType value so
// view_descriptor(t) is a direct index. Canvas is the only multi-instance type.
constexpr std::array<ViewDescriptor, k_view_type_count> k_catalog = {{
    {ViewType::Canvas, "canvas", "Canvas", true},
    {ViewType::Layers, "layers", "Layers", false},
    {ViewType::Inspector, "inspector", "Inspector", false},
    {ViewType::Overview, "overview", "Overview", false},
    {ViewType::Color, "color", "Color", false},
    {ViewType::History, "history", "History", false},
    {ViewType::Assets, "assets", "Assets", false},
    {ViewType::Export, "export", "Export", false},
}};

std::size_t index_of(ViewType type) { return static_cast<std::size_t>(type); }

// Parse a positive, leading-zero-free decimal index. Returns 0 (rejected) for an
// empty string, a leading zero, or any non-digit — minted ids never carry those.
int parse_index(std::string_view digits) {
  if (digits.empty() || digits[0] == '0') {
    return 0;
  }
  int value = 0;
  for (const char c : digits) {
    if (c < '0' || c > '9') {
      return 0;
    }
    value = value * 10 + (c - '0');
  }
  return value;
}

} // namespace

const std::array<ViewDescriptor, k_view_type_count>& view_catalog() { return k_catalog; }

const ViewDescriptor& view_descriptor(ViewType type) { return k_catalog[index_of(type)]; }

std::optional<ViewType> view_type_for_slug(std::string_view slug) {
  for (const ViewDescriptor& d : k_catalog) {
    if (d.slug == slug) {
      return d.type;
    }
  }
  return std::nullopt;
}

std::string_view view_slug(ViewType type) { return view_descriptor(type).slug; }

std::string_view view_title(ViewType type) { return view_descriptor(type).title; }

std::optional<ParsedViewId> parse_view_id(std::string_view id) {
  const std::size_t hash = id.find('#');
  const std::string_view slug = id.substr(0, hash == std::string_view::npos ? id.size() : hash);
  const std::optional<ViewType> type = view_type_for_slug(slug);
  if (!type) {
    return std::nullopt;
  }
  const ViewDescriptor& desc = view_descriptor(*type);
  if (hash == std::string_view::npos) {
    // Bare slug — valid only for a singleton (a multi-instance view is slug#N).
    if (desc.multi_instance) {
      return std::nullopt;
    }
    return ParsedViewId{*type, 0};
  }
  // Has a `#N` suffix — valid only for a multi-instance type, with N >= 1.
  if (!desc.multi_instance) {
    return std::nullopt;
  }
  const int index = parse_index(id.substr(hash + 1));
  if (index == 0) {
    return std::nullopt;
  }
  return ParsedViewId{*type, index};
}

std::string ViewRegistry::mint_id(ViewType type) {
  const ViewDescriptor& desc = view_descriptor(type);
  if (!desc.multi_instance) {
    return std::string(desc.slug);
  }
  const int index = ++next_index_[index_of(type)];
  return std::string(desc.slug) + '#' + std::to_string(index);
}

std::string ViewRegistry::open(DockLayout& layout, ViewType type, std::string_view target) {
  const ViewDescriptor& desc = view_descriptor(type);
  // A singleton already present is idempotent: activate its tab, no duplicate.
  if (!desc.multi_instance) {
    const std::string slug(desc.slug);
    if (layout.contains(slug)) {
      layout.activate(slug);
      return slug;
    }
  }
  std::string id = mint_id(type);
  if (layout.empty()) {
    layout = DockLayout{DockNode::leaf({id})};
    return id;
  }
  // Insert into the named target leaf, or the last leaf in pre-order by default.
  if (!target.empty() && layout.contains(target)) {
    layout.insert_tab(target, id);
    return id;
  }
  // Default target: the last leaf in pre-order (a non-empty layout always has at
  // least one view id to anchor the new tab against).
  const std::vector<std::string> ids = layout.view_ids();
  layout.insert_tab(ids.back(), id);
  return id;
}

bool ViewRegistry::close(DockLayout& layout, std::string_view view_id) {
  return layout.remove_view(view_id);
}

std::string ViewRegistry::reopen(DockLayout& layout, ViewType type) { return open(layout, type); }

} // namespace ace::dockmodel
