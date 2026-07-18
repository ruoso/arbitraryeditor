#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <system_error>

// editor.project.open_ui — the pure L1 `project` validate/compose helpers
// (D-open_ui-4/-5): is_project_directory recognizes a scaffolded project (both the
// workspace-present and canonical-present cases) and rejects non-projects; the
// New-target composer rejects empty/invalid names and composes `parent / name`.

namespace {

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_project_entry_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

} // namespace

TEST_CASE("is_project_directory recognizes scaffolded projects", "[project_entry]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  std::error_code ec;

  // Canonical-present: a project.arbc under an enumerable directory.
  const std::filesystem::path canonical_case = scratch.root / "canonical";
  std::filesystem::create_directories(canonical_case, ec);
  std::ofstream(canonical_case / "project.arbc") << "x";
  REQUIRE(ace::project::is_project_directory(fs, canonical_case));

  // Workspace-present: only the live workspace file (a fresh, unsaved project).
  const std::filesystem::path workspace_case = scratch.root / "workspace_case";
  const ace::project::ProjectLayout layout = ace::project::project_layout(workspace_case);
  std::filesystem::create_directories(layout.workspace_dir, ec);
  std::ofstream(layout.workspace_file) << "x";
  REQUIRE(ace::project::is_project_directory(fs, workspace_case));
}

TEST_CASE("is_project_directory rejects non-projects", "[project_entry]") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  std::error_code ec;

  const std::filesystem::path empty_dir = scratch.root / "empty";
  std::filesystem::create_directories(empty_dir, ec);
  REQUIRE_FALSE(ace::project::is_project_directory(fs, empty_dir)); // no project files

  REQUIRE_FALSE(ace::project::is_project_directory(fs, scratch.root / "absent")); // no path

  const std::filesystem::path file_path = scratch.root / "a_file";
  std::ofstream(file_path) << "x";
  REQUIRE_FALSE(ace::project::is_project_directory(fs, file_path)); // not a directory
}

TEST_CASE("compose_new_project_target composes and validates", "[project_entry]") {
  const std::filesystem::path parent = "/tmp/projects";

  const auto ok = ace::project::compose_new_project_target(parent, "My Project");
  REQUIRE(ok.has_value());
  REQUIRE(*ok == parent / "My Project");

  // Rejections: empty / blank / separators / dot entries / empty parent.
  REQUIRE_FALSE(ace::project::compose_new_project_target(parent, "").has_value());
  REQUIRE_FALSE(ace::project::compose_new_project_target(parent, "   ").has_value());
  REQUIRE_FALSE(ace::project::compose_new_project_target(parent, "a/b").has_value());
  REQUIRE_FALSE(ace::project::compose_new_project_target(parent, "x\\y").has_value());
  REQUIRE_FALSE(ace::project::compose_new_project_target(parent, ".").has_value());
  REQUIRE_FALSE(ace::project::compose_new_project_target(parent, "..").has_value());
  REQUIRE_FALSE(ace::project::compose_new_project_target({}, "name").has_value());
}
