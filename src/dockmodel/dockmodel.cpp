#include <ace/base/base.hpp>
#include <ace/dockmodel/dockmodel.hpp>

#include <algorithm>
#include <utility>

namespace ace::dockmodel {
namespace {

// Pre-order traversal: `f` is invoked on every node (splits and leaves).
template <typename F> void visit(const DockNode& node, F&& f) {
  f(node);
  for (const DockNode& child : node.children) {
    visit(child, f);
  }
}

void collect_ids(const DockNode& node, std::vector<std::string>& out) {
  visit(node, [&out](const DockNode& n) {
    if (n.is_leaf()) {
      out.insert(out.end(), n.tabs.begin(), n.tabs.end());
    }
  });
}

// Depth-first search for the leaf whose tab-group contains `view`; nullptr if
// no leaf holds it.
DockNode* find_leaf(DockNode& node, std::string_view view) {
  if (node.is_leaf()) {
    for (const std::string& id : node.tabs) {
      if (id == view) {
        return &node;
      }
    }
    return nullptr;
  }
  for (DockNode& child : node.children) {
    if (DockNode* hit = find_leaf(child, view)) {
      return hit;
    }
  }
  return nullptr;
}

bool node_valid(const DockNode& node, std::vector<std::string>& seen) {
  if (node.is_split()) {
    if (!(node.ratio > 0.0 && node.ratio < 1.0)) {
      return false;
    }
    return node_valid(node.children[0], seen) && node_valid(node.children[1], seen);
  }
  // Leaf: non-empty, active in range, ids globally unique.
  if (node.tabs.empty() || node.active >= node.tabs.size()) {
    return false;
  }
  for (const std::string& id : node.tabs) {
    if (std::find(seen.begin(), seen.end(), id) != seen.end()) {
      return false;
    }
    seen.push_back(id);
  }
  return true;
}

enum class RemoveOutcome { NotFound, Removed, LeafEmptied };

// Remove `view` from the subtree rooted at `node`. On a split whose child leaf
// empties, promote the sibling into `node` (the collapse). Reports LeafEmptied
// only when `node` itself is the emptied leaf, so the caller (its parent, or
// the layout root) performs the collapse / reset.
RemoveOutcome remove_rec(DockNode& node, std::string_view view) {
  if (node.is_leaf()) {
    auto it = std::find(node.tabs.begin(), node.tabs.end(), view);
    if (it == node.tabs.end()) {
      return RemoveOutcome::NotFound;
    }
    const std::size_t idx = static_cast<std::size_t>(it - node.tabs.begin());
    node.tabs.erase(it);
    if (node.tabs.empty()) {
      return RemoveOutcome::LeafEmptied;
    }
    if (idx < node.active) {
      --node.active;
    } else if (node.active >= node.tabs.size()) {
      node.active = node.tabs.size() - 1;
    }
    return RemoveOutcome::Removed;
  }
  for (int i = 0; i < 2; ++i) {
    const RemoveOutcome r = remove_rec(node.children[i], view);
    if (r == RemoveOutcome::NotFound) {
      continue;
    }
    if (r == RemoveOutcome::LeafEmptied) {
      DockNode sibling = std::move(node.children[1 - i]);
      node = std::move(sibling); // collapse: parent split becomes the sibling
    }
    return RemoveOutcome::Removed;
  }
  return RemoveOutcome::NotFound;
}

} // namespace

const char* name() { return "dockmodel"; }

DockNode DockNode::leaf(std::vector<std::string> tabs, std::size_t active) {
  DockNode n;
  n.tabs = std::move(tabs);
  n.active = active;
  return n;
}

DockNode DockNode::split(SplitOrientation orientation, double ratio, DockNode first,
                         DockNode second) {
  DockNode n;
  n.orientation = orientation;
  n.ratio = ratio;
  n.children.push_back(std::move(first));
  n.children.push_back(std::move(second));
  return n;
}

DockLayout DockLayout::make_default(const std::vector<std::string>& views) {
  if (views.empty()) {
    return DockLayout{};
  }
  if (views.size() == 1) {
    return DockLayout{DockNode::leaf(views)};
  }
  std::vector<std::string> rest(views.begin() + 1, views.end());
  return DockLayout{DockNode::split(SplitOrientation::Horizontal, 0.6,
                                    DockNode::leaf({views.front()}),
                                    DockNode::leaf(std::move(rest)))};
}

bool DockLayout::valid() const {
  if (empty()) {
    return true;
  }
  std::vector<std::string> seen;
  return node_valid(*root_, seen);
}

bool DockLayout::contains(std::string_view view) const {
  if (empty()) {
    return false;
  }
  return find_leaf(const_cast<DockNode&>(*root_), view) != nullptr;
}

std::size_t DockLayout::node_count() const {
  if (empty()) {
    return 0;
  }
  std::size_t count = 0;
  visit(*root_, [&count](const DockNode&) { ++count; });
  return count;
}

std::size_t DockLayout::leaf_count() const {
  if (empty()) {
    return 0;
  }
  std::size_t count = 0;
  visit(*root_, [&count](const DockNode& n) {
    if (n.is_leaf()) {
      ++count;
    }
  });
  return count;
}

std::vector<std::string> DockLayout::view_ids() const {
  std::vector<std::string> ids;
  if (!empty()) {
    collect_ids(*root_, ids);
  }
  return ids;
}

bool DockLayout::insert_tab(std::string_view target_view, std::string new_view) {
  if (empty() || contains(new_view)) {
    return false;
  }
  DockNode* leaf = find_leaf(*root_, target_view);
  if (leaf == nullptr) {
    return false;
  }
  leaf->tabs.push_back(std::move(new_view));
  leaf->active = leaf->tabs.size() - 1;
  return true;
}

bool DockLayout::split_leaf(std::string_view target_view, SplitOrientation orientation,
                            double ratio, std::string new_view) {
  if (empty() || !(ratio > 0.0 && ratio < 1.0) || contains(new_view)) {
    return false;
  }
  DockNode* leaf = find_leaf(*root_, target_view);
  if (leaf == nullptr) {
    return false;
  }
  DockNode original = std::move(*leaf);
  *leaf = DockNode::split(orientation, ratio, std::move(original),
                          DockNode::leaf({std::move(new_view)}));
  return true;
}

bool DockLayout::remove_view(std::string_view view) {
  if (empty()) {
    return false;
  }
  const RemoveOutcome r = remove_rec(*root_, view);
  if (r == RemoveOutcome::NotFound) {
    return false;
  }
  if (r == RemoveOutcome::LeafEmptied) {
    root_.reset(); // the root was the last (single) leaf — layout is now empty
  }
  return true;
}

bool DockLayout::activate(std::string_view view) {
  if (empty()) {
    return false;
  }
  DockNode* leaf = find_leaf(*root_, view);
  if (leaf == nullptr) {
    return false;
  }
  // find_leaf guarantees `view` is present in this leaf's tabs.
  const auto it = std::find(leaf->tabs.begin(), leaf->tabs.end(), view);
  leaf->active = static_cast<std::size_t>(it - leaf->tabs.begin());
  return true;
}

} // namespace ace::dockmodel
