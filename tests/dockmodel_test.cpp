#include <ace/dockmodel/dockmodel.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

// The L1 "dock model" headless unit suite (docs/01-architecture.md §9 :187):
// DockLayout invariants + the split/insert/remove/collapse operations the
// editor.dock.dockspace leaf lays down for view_registry / workspaces to extend.
// UI-agnostic — no ImGui (A8 / refinement Constraint 2).
using ace::dockmodel::DockLayout;
using ace::dockmodel::DockNode;
using ace::dockmodel::SplitOrientation;

TEST_CASE("dockmodel: make_default tiles the views (split + tab-group)") {
  const DockLayout layout = DockLayout::make_default({"canvas", "a", "b"});
  REQUIRE_FALSE(layout.empty());
  REQUIRE(layout.valid());

  const DockNode& root = layout.root();
  REQUIRE(root.is_split());
  CHECK(root.orientation == SplitOrientation::Horizontal);
  CHECK(root.ratio == 0.6);

  // First side: the lone canvas stand-in.
  REQUIRE(root.children[0].is_leaf());
  CHECK(root.children[0].tabs == std::vector<std::string>{"canvas"});

  // Second side: the rest as a tab-group, first tab active.
  REQUIRE(root.children[1].is_leaf());
  CHECK(root.children[1].tabs == std::vector<std::string>{"a", "b"});
  CHECK(root.children[1].active == 0);

  // Traversal visits every node: one split + two leaves.
  CHECK(layout.node_count() == 3);
  CHECK(layout.leaf_count() == 2);
  CHECK(layout.view_ids() == std::vector<std::string>{"canvas", "a", "b"});
  CHECK(layout.contains("b"));
  CHECK_FALSE(layout.contains("missing"));
}

TEST_CASE("dockmodel: make_default degenerate arities") {
  CHECK(DockLayout::make_default({}).empty());

  const DockLayout one = DockLayout::make_default({"solo"});
  REQUIRE_FALSE(one.empty());
  CHECK(one.root().is_leaf());
  CHECK(one.node_count() == 1);
  CHECK(one.valid());
}

TEST_CASE("dockmodel: invariants reject malformed trees") {
  // Split ratio must be in (0,1).
  CHECK_FALSE(DockLayout(DockNode::split(SplitOrientation::Vertical, 0.0, DockNode::leaf({"a"}),
                                         DockNode::leaf({"b"})))
                  .valid());
  CHECK_FALSE(DockLayout(DockNode::split(SplitOrientation::Vertical, 1.0, DockNode::leaf({"a"}),
                                         DockNode::leaf({"b"})))
                  .valid());

  // A leaf tab-group must be non-empty.
  CHECK_FALSE(DockLayout(DockNode::leaf({})).valid());

  // The active index must be in range.
  CHECK_FALSE(DockLayout(DockNode::leaf({"a"}, 5)).valid());

  // View ids are unique across the whole tree.
  CHECK_FALSE(DockLayout(DockNode::split(SplitOrientation::Horizontal, 0.5, DockNode::leaf({"dup"}),
                                         DockNode::leaf({"dup"})))
                  .valid());

  // The empty layout is vacuously valid.
  CHECK(DockLayout{}.valid());
}

TEST_CASE("dockmodel: insert_tab appends and activates") {
  DockLayout layout(DockNode::leaf({"a", "b"}));
  REQUIRE(layout.insert_tab("a", "c"));
  CHECK(layout.root().tabs == std::vector<std::string>{"a", "b", "c"});
  CHECK(layout.root().active == 2); // the inserted tab becomes active
  CHECK(layout.valid());

  // Duplicate ids are rejected (uniqueness invariant preserved).
  CHECK_FALSE(layout.insert_tab("a", "b"));
  // A missing target is rejected.
  CHECK_FALSE(layout.insert_tab("nope", "d"));
  // Insertion into the empty layout is a no-op.
  DockLayout empty;
  CHECK_FALSE(empty.insert_tab("a", "b"));
}

TEST_CASE("dockmodel: split_leaf replaces a leaf with a parent split") {
  DockLayout layout(DockNode::leaf({"a", "b"}));
  REQUIRE(layout.split_leaf("a", SplitOrientation::Vertical, 0.25, "c"));

  const DockNode& root = layout.root();
  REQUIRE(root.is_split());
  CHECK(root.orientation == SplitOrientation::Vertical);
  CHECK(root.ratio == 0.25);
  CHECK(root.children[0].tabs == std::vector<std::string>{"a", "b"}); // original leaf
  CHECK(root.children[1].tabs == std::vector<std::string>{"c"});      // fresh leaf
  CHECK(layout.valid());

  // A ratio outside (0,1), a duplicate view, and a missing target all reject.
  CHECK_FALSE(layout.split_leaf("a", SplitOrientation::Vertical, 1.5, "d"));
  CHECK_FALSE(layout.split_leaf("a", SplitOrientation::Vertical, 0.5, "b"));
  CHECK_FALSE(layout.split_leaf("nope", SplitOrientation::Vertical, 0.5, "d"));
}

TEST_CASE("dockmodel: remove_view collapses an emptied leaf, promoting its sibling") {
  DockLayout layout = DockLayout::make_default({"canvas", "a", "b"});
  // Removing the lone-leaf side collapses the root split into the sibling.
  REQUIRE(layout.remove_view("canvas"));
  REQUIRE(layout.root().is_leaf());
  CHECK(layout.root().tabs == std::vector<std::string>{"a", "b"});
  CHECK(layout.valid());
  CHECK(layout.node_count() == 1);
}

TEST_CASE("dockmodel: remove_view reaches into the second subtree") {
  // A view living in the split's second child: the first child is searched and
  // skipped, then the tab is removed from the second leaf without collapsing.
  DockLayout layout(DockNode::split(SplitOrientation::Horizontal, 0.5, DockNode::leaf({"a"}),
                                    DockNode::leaf({"b", "c"})));
  REQUIRE(layout.remove_view("c"));
  REQUIRE(layout.root().is_split());
  CHECK(layout.root().children[1].tabs == std::vector<std::string>{"b"});
  CHECK(layout.valid());

  // Emptying the second child collapses the split, promoting the first child.
  REQUIRE(layout.remove_view("b"));
  REQUIRE(layout.root().is_leaf());
  CHECK(layout.root().tabs == std::vector<std::string>{"a"});
  CHECK(layout.valid());
}

TEST_CASE("dockmodel: remove_view adjusts the active tab and empties the root") {
  DockLayout layout(DockNode::leaf({"a", "b", "c"}, /*active=*/2));
  // Removing a tab before the active one shifts the active index down.
  REQUIRE(layout.remove_view("a"));
  CHECK(layout.root().tabs == std::vector<std::string>{"b", "c"});
  CHECK(layout.root().active == 1);

  // Removing the active last tab clamps the active index into range.
  REQUIRE(layout.remove_view("c"));
  CHECK(layout.root().tabs == std::vector<std::string>{"b"});
  CHECK(layout.root().active == 0);

  // Removing the final view empties the whole layout (D18: anything can close).
  REQUIRE(layout.remove_view("b"));
  CHECK(layout.empty());

  // Removing from an empty / absent view returns false.
  CHECK_FALSE(layout.remove_view("b"));
  DockLayout populated(DockNode::leaf({"x"}));
  CHECK_FALSE(populated.remove_view("absent"));
}

TEST_CASE("dockmodel: activate selects the named tab") {
  DockLayout layout(DockNode::leaf({"a", "b", "c"}));
  REQUIRE(layout.activate("c"));
  CHECK(layout.root().active == 2);
  CHECK_FALSE(layout.activate("absent"));
  CHECK_FALSE(DockLayout{}.activate("a"));
}

TEST_CASE("dockmodel: close-everything then rebuild yields the seed layout") {
  const std::vector<std::string> views{"canvas", "a", "b"};
  const DockLayout seed = DockLayout::make_default(views);

  // Tear the layout down view by view until nothing remains.
  DockLayout live = seed;
  for (const std::string& v : views) {
    REQUIRE(live.remove_view(v));
  }
  REQUIRE(live.empty());

  // Rebuilding the default reproduces the seed exactly (the workspaces seam).
  const DockLayout rebuilt = DockLayout::make_default(views);
  CHECK(rebuilt == seed);
}
