#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ace::dockmodel {

// The dockmodel component. See docs/01-architecture.md §8 (component levelization).
const char* name();

// A split node's orientation. Horizontal splits left|right (first=left);
// Vertical splits top|bottom (first=top). See docs/00-design.md §10 (D18):
// "the tree splits H/V recursively — the same model all the way down".
enum class SplitOrientation { Horizontal, Vertical };

// One node of the dock split-tree (docs/00-design.md §10 / D18). A node is
// EITHER a leaf (a tab-group: an ordered, non-empty list of view ids + the
// index of the active tab, `children` empty) OR a split (exactly two children
// with an `orientation` + a `ratio` in (0,1), `tabs` empty). The recursion is
// value-typed (children held by value) so the whole tree copies / compares /
// serializes as plain data — no ImGui, no handles (A8; refinement Constraint 2).
struct DockNode {
  // Split payload (meaningful when is_split()).
  SplitOrientation orientation = SplitOrientation::Horizontal;
  double ratio = 0.5;             // fraction of the space given to `first` (the dir child)
  std::vector<DockNode> children; // empty => leaf, exactly 2 => split

  // Leaf payload (meaningful when is_leaf()).
  std::vector<std::string> tabs; // ordered view ids
  std::size_t active = 0;        // index into `tabs` of the focused view

  bool is_leaf() const { return children.empty(); }
  bool is_split() const { return children.size() == 2; }

  // Value-typed structural equality (recurses through children/tabs).
  bool operator==(const DockNode&) const = default;

  // A leaf holding the given ordered view ids (active defaults to the first).
  static DockNode leaf(std::vector<std::string> tabs, std::size_t active = 0);
  // A split of two subtrees; `ratio` is the fraction given to `first`.
  static DockNode split(SplitOrientation orientation, double ratio, DockNode first,
                        DockNode second);
};

// A declarative dockspace layout: the recursive split-tree of tab-groups the
// `dock` component seeds ImGui's live docking tree from, and the serialization
// projection `editor.dock.workspaces` will persist (D-dockspace-1/-2). The tree
// may be empty (every view closed — D18: "anything can be closed", no
// keep-a-canvas guardrail). Operations locate leaves by the view id they hold.
class DockLayout {
public:
  DockLayout() = default; // the empty layout (no root)
  explicit DockLayout(DockNode root) : root_(std::move(root)) {}

  // The single bootstrap arrangement (refinement Constraint 7): the first view
  // fills one side, the rest share the other side as a tab-group. With one view
  // it is a lone leaf; with none it is empty. Deterministic — the "rebuild the
  // default" side of the close-everything round-trip.
  static DockLayout make_default(const std::vector<std::string>& views);

  bool empty() const { return !root_.has_value(); }
  const DockNode& root() const { return *root_; }

  // True if every split ratio is in (0,1), every leaf is non-empty with a valid
  // active index, and no view id repeats anywhere in the tree. An empty layout
  // is vacuously valid.
  bool valid() const;

  bool contains(std::string_view view) const;
  std::size_t node_count() const;            // every split + leaf node
  std::size_t leaf_count() const;            // leaf (tab-group) nodes only
  std::vector<std::string> view_ids() const; // all view ids, pre-order

  // Add `new_view` as a new tab of the leaf holding `target_view`, and make it
  // active. No-op returning false if `target_view` is absent or `new_view`
  // already exists anywhere (ids stay unique).
  bool insert_tab(std::string_view target_view, std::string new_view);

  // Replace the leaf holding `target_view` with a split: the original leaf as
  // `first`, a fresh leaf holding [`new_view`] as `second`. Returns false if
  // `ratio` is not in (0,1), `target_view` is absent, or `new_view` already
  // exists. (ImGui owns the live split; this only seeds it — D-dockspace-1.)
  bool split_leaf(std::string_view target_view, SplitOrientation orientation, double ratio,
                  std::string new_view);

  // Remove `view` from its leaf. A leaf emptied by the removal collapses: its
  // sibling is promoted into the parent split; the last remaining view leaves
  // the layout empty. Returns false if `view` is absent.
  bool remove_view(std::string_view view);

  // Make `view` the active tab of its leaf. Returns false if `view` is absent.
  bool activate(std::string_view view);

  bool operator==(const DockLayout&) const = default;

private:
  std::optional<DockNode> root_;
};

} // namespace ace::dockmodel
