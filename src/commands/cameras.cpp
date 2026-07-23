#include <ace/commands/cameras.hpp>

#include <arbc/runtime/document.hpp>

#include <cstddef>
#include <utility>
#include <vector>

namespace ace::commands {

Command add_camera_command(const arbc::Registry& registry, std::string name,
                           scene::Resolution resolution, const arbc::Affine& frame,
                           AddCameraOutcome& outcome) {
  outcome = AddCameraOutcome{};
  return Command{"add_camera", [&registry, &outcome, name = std::move(name), resolution,
                                frame](arbc::Document& doc) {
                   const arbc::ObjectId added =
                       scene::add_camera(doc, registry, name, resolution, frame);
                   if (added.valid()) {
                     outcome.camera = added;
                   } else {
                     // The one refusal `scene::add_camera` has: no root composition to place
                     // the camera in. It opens no transaction, so the document is untouched.
                     outcome.error = "no root composition to place a camera in";
                   }
                 }};
}

std::string next_camera_name(const arbc::Document& document) {
  const std::vector<scene::Camera> existing = scene::cameras(document);
  // Terminates by pigeonhole: at most `existing.size()` of the candidates can be taken.
  for (std::size_t n = 1;; ++n) {
    std::string candidate = "Camera " + std::to_string(n);
    bool taken = false;
    for (const scene::Camera& camera : existing) {
      if (camera.name == candidate) {
        taken = true;
        break;
      }
    }
    if (!taken) {
      return candidate;
    }
  }
}

bool can_frame_selection(const AppState& state) { return !state.selection().empty(); }

} // namespace ace::commands
