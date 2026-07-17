// editor.dock.workspaces — L1 Catch2 units (docs/01-architecture.md §9). Headless
// core: the DockLayout text projection round-trip + malformed matrix, the three
// D21 built-ins, the WorkspaceStore over a ScratchDir + NativeFileSystem, and
// ViewRegistry::adopt's no-alias guarantee. UI-agnostic — no ImGui (A8).
#include <ace/dockmodel/view_registry.hpp>
#include <ace/dockmodel/workspaces.hpp>
#include <ace/platform/filesystem.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

using ace::dockmodel::DockLayout;
using ace::dockmodel::DockNode;
using ace::dockmodel::parse_layout;
using ace::dockmodel::serialize_layout;
using ace::dockmodel::SplitOrientation;
using ace::dockmodel::ViewRegistry;
using ace::dockmodel::ViewType;
using ace::dockmodel::workspace_builtins;
using ace::dockmodel::WorkspacePreset;
using ace::dockmodel::WorkspaceStore;
using ace::platform::NativeFileSystem;

namespace {

// A throwaway directory under the OS temp dir (mirrors tests/platform_test.cpp).
// Fixed name — wiped on entry and exit so reruns are clean. A distinct name from
// the platform test so the two never collide when ctest runs them in parallel.
struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_workspace_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

bool has_preset(const std::vector<WorkspacePreset>& presets, const std::string& name) {
  return std::any_of(presets.begin(), presets.end(),
                     [&name](const WorkspacePreset& p) { return p.name == name; });
}

} // namespace

TEST_CASE("workspaces: serialize→parse round-trips the layout matrix") {
  std::vector<DockLayout> matrix;
  matrix.push_back(DockLayout{});                              // empty
  matrix.push_back(DockLayout(DockNode::leaf({"inspector"}))); // lone singleton leaf
  matrix.push_back(DockLayout(DockNode::leaf({"canvas#3"})));  // lone multi-instance leaf
  matrix.push_back(DockLayout(DockNode::leaf({"layers", "inspector", "color"}, 2))); // active != 0
  // A deep asymmetric split tree with a non-default ratio and a multi-tab leaf.
  matrix.push_back(DockLayout(DockNode::split(
      SplitOrientation::Horizontal, 0.62, DockNode::leaf({"canvas#1"}),
      DockNode::split(SplitOrientation::Vertical, 0.25, DockNode::leaf({"overview", "layers"}, 1),
                      DockNode::leaf({"history", "export"})))));

  for (const DockLayout& layout : matrix) {
    REQUIRE(layout.valid());
    const std::string text = serialize_layout(layout);
    const std::optional<DockLayout> parsed = parse_layout(text);
    REQUIRE(parsed.has_value());
    CHECK(parsed->valid());
    CHECK(*parsed == layout); // bit-exact round-trip, including the ratio
    CHECK(parsed->view_ids() == layout.view_ids());
  }
}

TEST_CASE("workspaces: malformed input yields nullopt without throwing") {
  const std::vector<std::string> bad = {
      "",                                         // empty
      "ace-workspace 2\nleaf 0 inspector\n",      // bad version tag
      "leaf 0 inspector\n",                       // missing header
      "ace-workspace 1\n",                        // header only, no body
      "ace-workspace 1\nsplit H 0.5\nleaf 0 a\n", // truncated split (one child)
      "ace-workspace 1\nsplit H 1.5\nleaf 0 canvas#1\nleaf 0 inspector\n", // ratio out of (0,1)
      "ace-workspace 1\nsplit H\nleaf 0 canvas#1\nleaf 0 inspector\n",     // split missing ratio
      "ace-workspace 1\nsplit X 0.5\nleaf 0 canvas#1\nleaf 0 inspector\n", // bad orientation token
      "ace-workspace 1\nsplit H bad\nleaf 0 canvas#1\nleaf 0 inspector\n", // non-numeric ratio
      "ace-workspace 1\nsplit H 0.5\nleaf 0 canvas#1\nleaf 0 canvas#1\n",  // duplicate view id
      "ace-workspace 1\nwibble 0 inspector\n",                             // unknown node token
      "ace-workspace 1\nleaf 5 inspector\n",                               // out-of-range active
      "ace-workspace 1\nleaf 0 not_a_view\n",                              // unknown view id
      "ace-workspace 1\nleaf 0 canvas\n",           // bare multi-instance slug (needs #N)
      "ace-workspace 1\nleaf 0\n",                  // leaf with no ids
      "ace-workspace 1\nleaf x inspector\n",        // non-numeric active
      "ace-workspace 1\nempty\nleaf 0 inspector\n", // trailing garbage after empty
  };
  for (const std::string& text : bad) {
    CHECK_FALSE(parse_layout(text).has_value());
  }
}

TEST_CASE("workspaces: builtins are Paint/Compose/Review, valid and deterministic") {
  const std::vector<WorkspacePreset>& a = workspace_builtins();
  REQUIRE(a.size() == 3);
  CHECK(a[0].name == "Paint");
  CHECK(a[1].name == "Compose");
  CHECK(a[2].name == "Review");
  for (const WorkspacePreset& preset : a) {
    CHECK(preset.builtin);
    CHECK(preset.layout.valid());
    // Each is a Canvas beside a stack — Canvas plus at least one other view.
    CHECK(preset.layout.contains("canvas#1"));
    CHECK(preset.layout.view_ids().size() >= 2);
  }
  CHECK(a[0].layout.view_ids() ==
        std::vector<std::string>{"canvas#1", "layers", "inspector", "color"});
  CHECK(a[1].layout.view_ids() == std::vector<std::string>{"canvas#1", "overview", "layers"});
  CHECK(a[2].layout.view_ids() == std::vector<std::string>{"canvas#1", "history", "export"});
  // Deterministic across calls.
  const std::vector<WorkspacePreset>& b = workspace_builtins();
  CHECK(a == b);
}

TEST_CASE("workspaces: WorkspaceStore over NativeFileSystem falls back, saves, removes") {
  ScratchDir scratch;
  NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "prefs";

  // A fresh (nonexistent) root yields exactly the built-ins.
  WorkspaceStore store(root, fs);
  std::vector<WorkspacePreset> presets = store.presets();
  CHECK(presets.size() == 3);
  CHECK(has_preset(presets, "Paint"));
  CHECK(has_preset(presets, "Compose"));
  CHECK(has_preset(presets, "Review"));

  // apply returns a built-in layout; an unknown name is nullopt.
  CHECK(store.apply("Compose").has_value());
  CHECK_FALSE(store.apply("Nope").has_value());

  // Save a user preset; a distinct layout so we can distinguish it on reload.
  const DockLayout mine(DockNode::split(SplitOrientation::Vertical, 0.4,
                                        DockNode::leaf({"canvas#2"}),
                                        DockNode::leaf({"assets", "export"}, 1)));
  REQUIRE(store.save("Mine", mine));
  CHECK(has_preset(store.presets(), "Mine"));

  // Reopen (a fresh store over the same root) reads the persisted preset back.
  WorkspaceStore reopened(root, fs);
  const std::optional<DockLayout> loaded = reopened.apply("Mine");
  REQUIRE(loaded.has_value());
  CHECK(*loaded == mine);

  // save rejects an empty name and an invalid (empty-leaf) layout.
  CHECK_FALSE(store.save("", mine));
  CHECK_FALSE(store.save("Bad", DockLayout(DockNode::leaf({}))));
  // save rejects a built-in name; remove refuses a built-in and an unknown name.
  CHECK_FALSE(store.save("Paint", mine));
  CHECK_FALSE(store.remove("Paint"));
  CHECK_FALSE(store.remove("Ghost"));

  // remove deletes the user preset; it is gone on the next read.
  REQUIRE(store.remove("Mine"));
  CHECK_FALSE(has_preset(store.presets(), "Mine"));
  CHECK_FALSE(WorkspaceStore(root, fs).apply("Mine").has_value());
}

TEST_CASE("workspaces: a corrupt store file falls back to built-ins without throwing") {
  ScratchDir scratch;
  NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "prefs";
  REQUIRE_FALSE(static_cast<bool>(fs.make_directories(root)));
  // A hand-written corrupt store (bad header / garbage body).
  REQUIRE_FALSE(
      static_cast<bool>(fs.write_file(root / "workspaces.acews", "not a workspace store\n@@@\n")));

  WorkspaceStore store(root, fs);
  const std::vector<WorkspacePreset> presets = store.presets(); // must not throw
  CHECK(presets.size() == 3);
  CHECK(has_preset(presets, "Paint"));
  CHECK(store.apply("Review").has_value());
}

TEST_CASE("workspaces: ViewRegistry::adopt re-seeds counters so mint_id never aliases") {
  ViewRegistry reg;
  // Adopting a layout containing canvas#3 makes the next Canvas mint canvas#4.
  const DockLayout layout(DockNode::split(SplitOrientation::Horizontal, 0.5,
                                          DockNode::leaf({"canvas#3"}),
                                          DockNode::leaf({"inspector"})));
  reg.adopt(layout);
  CHECK(reg.mint_id(ViewType::Canvas) == "canvas#4");

  // Monotonic: adopting a *lower* index never rewinds the counter.
  const DockLayout lower(DockNode::leaf({"canvas#2"}));
  reg.adopt(lower);
  CHECK(reg.mint_id(ViewType::Canvas) == "canvas#5");
}
