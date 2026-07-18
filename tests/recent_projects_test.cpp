#include <ace/dockmodel/recent_projects.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

// editor.project.open_ui — the RecentProjects MRU prefs store unit (D22), modeled
// on tests/workspaces_test.cpp: add MRU-front + de-dup, the N=10 cap, text-file
// round-trip via a real temp FileSystem, prune of vanished / non-project entries
// on load, and a missing / corrupt store degrading to empty.

namespace {

// A throwaway directory under the OS temp dir, wiped on entry and exit.
struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_recent_projects_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// Make `parent/name` a recognizable project directory (D16): an enumerable dir
// holding a project.arbc. is_project_directory only checks existence, so a real
// libarbc document is unnecessary here.
std::filesystem::path make_project(const std::filesystem::path& parent, const std::string& name) {
  const std::filesystem::path dir = parent / name;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::ofstream(dir / "project.arbc") << "x";
  return dir;
}

// The always-keep validator (pruning disabled) for the add/cap/round-trip cases.
bool keep_all(const std::filesystem::path&) { return true; }

} // namespace

using ace::dockmodel::RecentProjects;

TEST_CASE("RecentProjects add pushes MRU-front and de-dups", "[recent_projects]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path prefs = scratch.root / "prefs";
  const std::filesystem::path a = make_project(scratch.root, "a");
  const std::filesystem::path b = make_project(scratch.root, "b");

  RecentProjects recent(prefs, fs);
  REQUIRE(recent.add(a));
  REQUIRE(recent.add(b));
  {
    const std::vector<std::filesystem::path> list = recent.load(keep_all);
    REQUIRE(list.size() == 2);
    REQUIRE(list.front() == std::filesystem::weakly_canonical(b)); // most-recent
    REQUIRE(list.back() == std::filesystem::weakly_canonical(a));
  }
  // Re-adding `a` moves it to the front without duplicating.
  REQUIRE(recent.add(a));
  const std::vector<std::filesystem::path> list = recent.load(keep_all);
  REQUIRE(list.size() == 2);
  REQUIRE(list.front() == std::filesystem::weakly_canonical(a));

  // An empty path is rejected outright.
  REQUIRE_FALSE(recent.add(std::filesystem::path{}));
}

TEST_CASE("RecentProjects caps at k_max_entries dropping the oldest", "[recent_projects]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  RecentProjects recent(scratch.root / "prefs", fs);

  std::vector<std::filesystem::path> added;
  for (std::size_t i = 0; i < RecentProjects::k_max_entries + 3; ++i) {
    const std::filesystem::path dir = make_project(scratch.root, "p" + std::to_string(i));
    added.push_back(std::filesystem::weakly_canonical(dir));
    REQUIRE(recent.add(dir));
  }
  const std::vector<std::filesystem::path> list = recent.load(keep_all);
  REQUIRE(list.size() == RecentProjects::k_max_entries);
  REQUIRE(list.front() == added.back()); // the most-recent add survives
  // The three oldest were dropped past the cap.
  REQUIRE(std::find(list.begin(), list.end(), added.front()) == list.end());
}

TEST_CASE("RecentProjects round-trips through the versioned text file", "[recent_projects]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path prefs = scratch.root / "prefs";
  const std::filesystem::path a = make_project(scratch.root, "a");
  {
    RecentProjects recent(prefs, fs);
    REQUIRE(recent.add(a));
  }
  // A fresh store over the same root sees the persisted, absolute entry.
  RecentProjects reopened(prefs, fs);
  const std::vector<std::filesystem::path> list = reopened.load(keep_all);
  REQUIRE(list.size() == 1);
  REQUIRE(list.front() == std::filesystem::weakly_canonical(a));
  REQUIRE(list.front().is_absolute());
}

TEST_CASE("RecentProjects load prunes vanished and non-project entries", "[recent_projects]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path prefs = scratch.root / "prefs";
  const std::filesystem::path good = make_project(scratch.root, "good");
  const std::filesystem::path gone = make_project(scratch.root, "gone");
  const std::filesystem::path plain = scratch.root / "plain"; // a dir, not a project
  std::error_code ec;
  std::filesystem::create_directories(plain, ec);

  RecentProjects recent(prefs, fs);
  REQUIRE(recent.add(good));
  REQUIRE(recent.add(gone));
  REQUIRE(recent.add(plain));
  std::filesystem::remove_all(gone, ec); // now vanished

  const auto validator = [&fs](const std::filesystem::path& p) {
    return ace::project::is_project_directory(fs, p);
  };
  const std::vector<std::filesystem::path> list = recent.load(validator);
  REQUIRE(list.size() == 1);
  REQUIRE(list.front() == std::filesystem::weakly_canonical(good));

  // The prune self-healed the file: a fresh keep-all load now sees only `good`.
  RecentProjects reopened(prefs, fs);
  REQUIRE(reopened.load(keep_all).size() == 1);
}

TEST_CASE("RecentProjects degrades a missing or corrupt store to empty", "[recent_projects]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path prefs = scratch.root / "prefs";

  // Missing store: loads empty, no throw.
  RecentProjects recent(prefs, fs);
  REQUIRE(recent.load(keep_all).empty());

  // Corrupt store: garbage under the prefs root degrades to empty (no throw).
  std::error_code ec;
  std::filesystem::create_directories(prefs, ec);
  std::ofstream(prefs / "recent-projects.acerp") << "not a valid header\ngarbage line\n";
  RecentProjects corrupt(prefs, fs);
  REQUIRE(corrupt.load(keep_all).empty());
}
