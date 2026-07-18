#pragma once

#include <ace/platform/filesystem.hpp>

#include <filesystem>
#include <functional>
#include <string_view>
#include <vector>

namespace ace::dockmodel {

// The recent-projects MRU store (docs/00-design.md D22, docs/01-architecture.md
// A12 — editor.project.open_ui). Cross-project local UI state — exactly D21's
// per-user prefs category — so it mirrors WorkspaceStore: a capped,
// most-recent-first list of ABSOLUTE project directories persisted to a single
// versioned line-oriented text file under an app-resolved prefs root, published
// atomically through the injectable platform::FileSystem seam (A3/A8 — no ImGui,
// no OS handles, {base, platform}-only). Fully unit-testable over a ScratchDir +
// NativeFileSystem and WASM-swappable.
//
// Pruning of vanished / no-longer-a-project directories rides an injected
// predicate rather than a direct `project` dependency: `dockmodel` may not
// include `<ace/project/...>` (§8 DAG — dockmodel deps are {base, platform}), so
// the caller (the L4 gateway) passes `project::is_project_directory` bound to its
// FileSystem. That keeps the store dependency-inverted and the level check green.
class RecentProjects {
public:
  // The MRU cap (D22): most-recent-first, oldest dropped past this many.
  static constexpr std::size_t k_max_entries = 10;

  // The validity predicate load() prunes against: true keeps the entry (it still
  // exists and is a project), false drops it. Bound by the caller to
  // `project::is_project_directory` so the level DAG stays intact.
  using Validator = std::function<bool(const std::filesystem::path&)>;

  RecentProjects(std::filesystem::path root, platform::FileSystem& filesystem);

  // The persisted MRU list, most-recent-first, pruned of entries failing
  // `is_valid` (vanished or no-longer-a-project — D22). When pruning changed the
  // list it is re-published so the file self-heals; a missing or corrupt store
  // loads empty (rebuild-from-default, D21). All returned paths are absolute.
  std::vector<std::filesystem::path> load(const Validator& is_valid) const;

  // Add `dir` at the MRU front: canonicalized to an absolute path (Constraint 6),
  // de-duped against an existing entry (which is moved to the front, not
  // duplicated), and capped at k_max_entries by dropping the oldest. Publishes
  // atomically. Returns false on an empty `dir` or an I/O failure.
  bool add(const std::filesystem::path& dir);

private:
  std::filesystem::path store_file() const;
  std::vector<std::filesystem::path> read_entries() const;
  bool write_entries(const std::vector<std::filesystem::path>& entries) const;

  std::filesystem::path root_;
  platform::FileSystem& filesystem_;
};

} // namespace ace::dockmodel
