#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/commands/exec_new.hpp>
#include <ace/project/project.hpp>

#include <utility>

// editor.project.open_ui — the L4 gateway wiring the already-built terminal
// mechanisms (editor.project.open validate helpers, editor.project.exec_new
// sibling spawn, the RecentProjects prefs store) behind the dock::ProjectGateway
// seam. No second Document is ever minted here (D-open_ui-1).

namespace ace::app {

AppProjectGateway::AppProjectGateway(ace::dockmodel::RecentProjects& recent,
                                     const ace::platform::FileSystem& filesystem,
                                     FolderDialog& dialog,
                                     const ace::platform::ProcessLauncher& launcher,
                                     std::filesystem::path executable)
    : recent_(recent), filesystem_(filesystem), dialog_(dialog), launcher_(launcher),
      executable_(std::move(executable)) {}

bool AppProjectGateway::spawn(const std::filesystem::path& dir) {
  // Empty error_code == a successful launch (D-open-6); a non-empty one means the
  // sibling `exec` failed. open_another_project canonicalizes `dir` to an absolute
  // path before handing it to the launcher (D-exec_new-4).
  return !static_cast<bool>(ace::commands::open_another_project(launcher_, executable_, dir));
}

bool AppProjectGateway::open_project(const std::filesystem::path& dir) {
  if (!ace::project::is_project_directory(filesystem_, dir)) {
    return false; // a non-project selection surfaces an error and spawns nothing
  }
  recent_.add(dir);
  return spawn(dir);
}

bool AppProjectGateway::new_project(const std::filesystem::path& parent, const std::string& name) {
  const std::optional<std::filesystem::path> target =
      ace::project::compose_new_project_target(parent, name);
  if (!target.has_value()) {
    return false; // empty / invalid / traversing name
  }
  // Do NOT record: the target does not exist yet — the child's create-branch
  // scaffolds it (D-open_ui-4), and it lands in the recent list on its own open.
  return spawn(*target);
}

bool AppProjectGateway::open_recent(const std::filesystem::path& dir) {
  if (!ace::project::is_project_directory(filesystem_, dir)) {
    return false; // pruned away since the list was last rendered
  }
  recent_.add(dir); // replay re-orders the entry MRU-front
  return spawn(dir);
}

void AppProjectGateway::pick_folder(
    std::function<void(std::optional<std::filesystem::path>)> on_pick) {
  dialog_.show(std::move(on_pick));
}

std::vector<std::filesystem::path> AppProjectGateway::recent_projects() const {
  // Prune through the L1 predicate, bound to our FileSystem — dockmodel may not
  // depend on `project`, so the validity check is injected here (A12 / §8).
  return recent_.load([this](const std::filesystem::path& dir) {
    return ace::project::is_project_directory(filesystem_, dir);
  });
}

} // namespace ace::app
