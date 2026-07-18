#pragma once

#include <ace/commands/app_state.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ace::app {

class FolderDialog;

// The L4 concrete project-entry gateway (docs/01-architecture.md A12, D22). It is
// the sole holder of the native folder dialog seam, the dockmodel::RecentProjects
// prefs store, the platform::ProcessLauncher + current-executable path, and the
// L1 `project` validate/compose helpers. Every mutating action terminates in
// commands::open_another_project — a detached SIBLING `exec` (D19/A7): this
// process's one Document is never swapped and open_project/create_project are
// never called in-process. Errors are values (no throws across the seam).
//
// The Save + dirty query (A13) are the exception: they act on the ONE in-process
// `AppState` this gateway now holds (`save()` -> `commands::save_project`,
// `is_dirty()` -> `AppState::is_dirty()`), not a sibling process. The same seam,
// same dependency inversion — the L3 rail reaches the L4 session through it without
// an illegal `dock -> commands` edge.
class AppProjectGateway final : public ace::dock::ProjectGateway {
public:
  AppProjectGateway(ace::dockmodel::RecentProjects& recent,
                    const ace::platform::FileSystem& filesystem, FolderDialog& dialog,
                    const ace::platform::ProcessLauncher& launcher,
                    std::filesystem::path executable, ace::commands::AppState& app_state);

  bool open_project(const std::filesystem::path& dir) override;
  bool new_project(const std::filesystem::path& parent, const std::string& name) override;
  bool open_recent(const std::filesystem::path& dir) override;
  void pick_folder(std::function<void(std::optional<std::filesystem::path>)> on_pick) override;
  std::vector<std::filesystem::path> recent_projects() const override;
  bool save() override;
  bool is_dirty() const override;

private:
  bool spawn(const std::filesystem::path& dir);

  ace::dockmodel::RecentProjects& recent_;
  const ace::platform::FileSystem& filesystem_;
  FolderDialog& dialog_;
  const ace::platform::ProcessLauncher& launcher_;
  std::filesystem::path executable_;
  ace::commands::AppState& app_state_; // the one in-process session (A7) Save/dirty drive
};

} // namespace ace::app
