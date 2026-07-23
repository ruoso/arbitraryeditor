#include <ace/dockmodel/view_registry.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
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

namespace {

// `view_id_less`'s sort key: (0, type, index, "") for a well-formed id, (1, 0, 0, id) for
// anything else. The leading discriminator is what puts every unparseable string after every
// parseable one; the trailing `string_view` is what orders the unparseable tail by bytes and
// is deliberately EMPTY for a parseable id, so two ids never differ on a field that is not
// part of the order (D-view_id_natural_order-3).
//
// The index is the whole point: it is compared as an INT, so 2 < 10. The type comes from the
// enumerator, whose order IS the catalog order (view_registry.hpp) — the shell's one canonical
// view ordering, rather than a second, alphabetical notion of "view order" with no consumer.
//
// Built on `parse_view_id` rather than on a generic trailing-digit-run scan so there is exactly
// one answer to "is this a view id, and what index is it": a `strverscmp`-style sort would read
// "canvas#01" as 1 and tie it with "canvas#1", which the grammar deliberately rejects.
std::tuple<int, int, int, std::string_view> view_id_key(std::string_view id) {
  const std::optional<ParsedViewId> parsed = parse_view_id(id);
  if (!parsed) {
    return {1, 0, 0, id};
  }
  return {0, static_cast<int>(parsed->type), parsed->index, std::string_view{}};
}

} // namespace

bool view_id_less(std::string_view a, std::string_view b) {
  // One expression on purpose: the strict weak ordering is `std::tuple`'s, inherited rather
  // than argued for (Constraint 5).
  return view_id_key(a) < view_id_key(b);
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

void ViewRegistry::adopt(const DockLayout& layout) {
  for (const std::string& id : layout.view_ids()) {
    const std::optional<ParsedViewId> parsed = parse_view_id(id);
    if (!parsed || parsed->index == 0) {
      continue; // singletons carry no counter (index 0)
    }
    int& counter = next_index_[index_of(parsed->type)];
    if (parsed->index > counter) {
      counter = parsed->index; // monotonic: next mint yields index+1, no alias
    }
  }
}

} // namespace ace::dockmodel
