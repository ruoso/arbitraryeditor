#include <ace/dock/welcome.hpp>

#include <imgui.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// editor.project.welcome — the pre-project launcher's chrome (D26 / A22). A second
// host for the `dock::ProjectGateway` the tool rail hosts, drawn by L4's
// `run_welcome_launcher` in a process that owns ZERO Documents. Every verb here
// mirrors `draw_project_section`'s exactly (dock.cpp) — same async pick, same
// compose modal, same MRU query per frame, same feedback strings — because both
// surfaces must refuse the same targets and list the same recents.
//
// The "same feedback strings" clause is true BY CONSTRUCTION since A24, and was not
// before: both hosts now route every entry outcome through the one shared
// `entry_feedback` mapper, so neither can drift from the other. What stood here until
// then was a file-local `refusal_feedback` that reconstructed "refused target vs failed
// spawn" from MRU membership — sound about the code it cited, unsound as a
// discriminator (a discarded `RecentProjects::add` failure or a re-prune between the
// record and the redraw each flip it), and unavailable to the rail at all. The seam
// carries the answer now, so the inference is gone (D-entry_outcome-5).

namespace ace::dock {

const char* welcome_window_title() { return "Welcome"; }

void WelcomeWindow::draw() {
  // A viewport-sized, undecorated window rather than a BeginPopupModal (D-welcome-4):
  // there is nothing behind it to reach, and New's compose popup must open above it.
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus;
  ImGui::Begin(welcome_window_title(), nullptr, flags);
  ProjectGateway* gateway = project_gateway_;
  if (gateway != nullptr) {
    ImGui::TextUnformatted("Open a project to begin.");
    ImGui::Separator();

    // Stable, slash-free `###` widget ids (the visible label carries the ellipsis or
    // the path; the id after `###` is what the e2e drives by), the rail's house rule.
    //
    // New composes a NOT-YET-EXISTING target from a picked parent plus a typed name
    // (D22): the pick only seeds the compose modal — the spawn happens on Create, in
    // the shared draw_new_project_modal below.
    if (ImGui::Selectable("New Project…###welcome_new")) {
      NewProjectModal* modal = &new_project_;
      gateway->pick_folder([modal](std::optional<std::filesystem::path> picked) {
        if (picked.has_value()) {
          modal->open_on(*picked);
        }
        // A cancelled pick does nothing at all — no feedback, no spawn, and NO EXIT:
        // the user is still choosing (D-welcome-8).
      });
    }
    if (ImGui::Selectable("Open Project…###welcome_open")) {
      WelcomeWindow* self = this;
      gateway->pick_folder([self, gateway](std::optional<std::filesystem::path> picked) {
        if (!picked.has_value()) {
          return; // a cancelled pick spawns nothing and dismisses nothing
        }
        const ProjectEntryOutcome outcome = gateway->open_project(*picked);
        self->feedback_ = entry_feedback(outcome, "That folder is not a project.");
        if (outcome == ProjectEntryOutcome::succeeded) {
          self->exit_requested_ = true; // the sibling exists; this process is done
        }
      });
    }
    // Queried every frame, as the rail does: the store prunes vanished entries on
    // load, so the list the launcher shows is the list a project window would show.
    const std::vector<std::filesystem::path> recent = gateway->recent_projects();
    if (!recent.empty()) {
      ImGui::TextUnformatted("Open Recent");
      for (std::size_t i = 0; i < recent.size(); ++i) {
        const std::filesystem::path& dir = recent[i];
        std::string label = dir.string();
        label += "###welcome_recent";
        label += std::to_string(i);
        if (ImGui::Selectable(label.c_str())) {
          const ProjectEntryOutcome outcome = gateway->open_recent(dir);
          feedback_ = entry_feedback(outcome, "That project is no longer available.");
          if (outcome == ProjectEntryOutcome::succeeded) {
            exit_requested_ = true;
          }
        }
      }
    }
    if (!feedback_.empty()) {
      ImGui::TextWrapped("%s", feedback_.c_str());
    }
    // The SHARED compose modal (D-welcome-7) — same value type, same popup id, same
    // three refs the rail drives. Drawn every frame so BeginPopupModal stays balanced;
    // its `true` is the third way a verb spawns, so it latches the exit too.
    if (draw_new_project_modal(new_project_, *gateway, feedback_)) {
      exit_requested_ = true;
    }
  }
  ImGui::End();

  // Dismissal without choosing exits cleanly (D26): there is nothing to fall back to,
  // and inventing a fallback is exactly the throwaway project the launcher removes.
  // The OS window close arrives as `Shell::quit_requested()` and is OR-ed into the
  // launcher's loop condition by L4; `Esc` is handled here — GUARDED on the compose
  // modal not being open, because a BeginPopupModal opened with `p_open == nullptr`
  // does not consume Esc, so an unguarded check would exit the launcher out from under
  // a half-typed project name (D-welcome-5).
  if (!new_project_.open() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    exit_requested_ = true;
  }
}

} // namespace ace::dock
