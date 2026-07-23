#include <ace/app/camera_inspector.hpp>
#include <ace/app/canvas_view.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/interact/interact.hpp>
#include <ace/scene/camera.hpp>

#include <arbc/base/transform.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace ace::app {

CameraInspector::CameraInspector(commands::AppState& state, CanvasView& canvas)
    : state_(state), canvas_(canvas) {}

namespace {

// The aspect presets (D9: W×H + aspect presets). Each is a target ratio; applying one holds
// the pixel height and sets the width to the ratio (so the frame-follow is a real re-fit,
// D-manip-7), never an anamorphic pixel.
struct AspectPreset {
  const char* label;
  int w;
  int h;
};
constexpr AspectPreset k_presets[] = {{"1:1", 1, 1}, {"4:3", 4, 3}, {"16:9", 16, 9}, {"2:1", 2, 1}};

} // namespace

void CameraInspector::draw(std::string_view /*view_id*/) {
  const std::vector<scene::Camera> cameras = scene::cameras(state_.document());
  ImGui::TextUnformatted("Cameras");
  ImGui::Separator();
  if (cameras.empty()) {
    // Names the affordance that now exists (editor.cameras.new_shot_from_view): before that
    // leaf this sentence told the user to do something the product had no verb for. The
    // empty-state BUTTON is `editor.panels.inspector`'s (D-new_shot_from_view-8) — the
    // inspector holds a CanvasView&, not a ProjectGateway&, so wiring one here would either
    // ripple through three e2e constructors or duplicate the L4 join.
    ImGui::TextDisabled("No cameras — use New Shot From View in the rail.");
    return;
  }

  // A compact chooser (no unified-selection dependency — Constraint 8). Stable ###ids keep
  // the rows drivable by the e2e regardless of (possibly duplicate) camera names.
  for (std::size_t i = 0; i < cameras.size(); ++i) {
    const std::string label = cameras[i].name + "###cam_" + std::to_string(i);
    if (ImGui::Selectable(label.c_str(), cameras[i].id == target_)) {
      target_ = cameras[i].id;
    }
  }

  // Resolve the target: the selected camera if it still exists, else the first (fail-safe
  // when the prior target was deleted / undone away).
  const scene::Camera* target = &cameras.front();
  for (const scene::Camera& c : cameras) {
    if (c.id == target_) {
      target = &c;
      break;
    }
  }
  target_ = target->id;

  // Sync the pending W×H fields from the target's stored resolution when the target changes
  // or its resolution moved out from under the fields (an apply / undo / external edit); an
  // in-progress unapplied field edit is preserved otherwise.
  if (target_ != synced_id_ || target->resolution.width != synced_w_ ||
      target->resolution.height != synced_h_) {
    width_ = target->resolution.width;
    height_ = target->resolution.height;
    synced_id_ = target_;
    synced_w_ = target->resolution.width;
    synced_h_ = target->resolution.height;
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Output resolution");
  ImGui::InputInt("Width", &width_);
  ImGui::InputInt("Height", &height_);

  // "Type the resolution" (D7): a W×H change edits pixel count only — the frame is untouched
  // (D8/D9 independence). Committed as ONE undoable transaction through apply_edit.
  if (ImGui::Button("Apply Resolution")) {
    const arbc::ObjectId id = target_;
    const scene::Resolution res{std::max(1, width_), std::max(1, height_)};
    canvas_.apply_edit([this, id, res] {
      scene::set_camera_resolution(state_.document(), state_.registry(), id, res);
    });
  }

  // Aspect presets (D9): editing the aspect changes pixel count AND aspect, and the frame
  // follows (D-manip-7) — the resolution edit + the follow-frame committed as ONE transaction
  // (one undo step). Holds the pixel height, sets the width to the ratio, and re-fits the
  // frame to hold its horizontal extent.
  ImGui::TextUnformatted("Aspect");
  const arbc::ObjectId id = target_;
  const arbc::ObjectId layer = target->layer;
  const arbc::Affine frame = target->frame;
  const int cur_w = target->resolution.width;
  const int cur_h = target->resolution.height;
  bool first = true;
  for (const AspectPreset& preset : k_presets) {
    if (!first) {
      ImGui::SameLine();
    }
    first = false;
    if (ImGui::Button(preset.label)) {
      const int new_w = std::max(
          1, static_cast<int>(std::lround(static_cast<double>(cur_h) * preset.w / preset.h)));
      const int new_h = std::max(1, cur_h);
      const arbc::Affine follow = interact::refit_frame_to_aspect(frame, cur_w, new_w);
      canvas_.apply_edit([this, id, layer, new_w, new_h, follow] {
        scene::set_camera_resolution_and_frame(state_.document(), state_.registry(), id, layer,
                                               scene::Resolution{new_w, new_h}, follow);
      });
    }
  }
}

} // namespace ace::app
