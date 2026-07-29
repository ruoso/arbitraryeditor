#include <ace/app/camera_inspector.hpp>
#include <ace/app/canvas_view.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/interact/interact.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

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

// The worst-case (softest) health of `cell` across the cameras that capture it — the ratio
// the readout leads with (Constraint / D-resolution-4). Cameras for which the verdict is N/A
// (a degenerate frame) are skipped; if no camera yields a verdict, the result is
// NotApplicable. `cell` must carry a native pixel grid (the caller gates on it).
interact::ResolutionHealth worst_cell_health(const scene::Cell& cell,
                                             const std::vector<scene::Camera>& cameras) {
  interact::ResolutionHealth worst;
  const std::pair<int, int> native = *cell.detail.native_pixels;
  for (const scene::Camera& cam : cameras) {
    const interact::ResolutionHealth h =
        interact::resolution_health(native.first, native.second, cell.placement,
                                    cam.resolution.width, cam.resolution.height, cam.frame);
    if (h.verdict == interact::ResolutionVerdict::NotApplicable) {
      continue;
    }
    if (worst.verdict == interact::ResolutionVerdict::NotApplicable || h.ratio > worst.ratio) {
      worst = h;
    }
  }
  return worst;
}

// The selected cell's native/placed size + D8 health/provenance (editor.cells.resolution).
// Native resolution and placed size are shown as DISTINCT numbers (D7/D8): the placement
// scale is never presented as a resolution, and the native grid never derives from it.
void draw_cell_readout(const scene::Cell& cell, const std::vector<scene::Camera>& cameras) {
  ImGui::TextUnformatted("Cell");
  ImGui::Separator();

  // Native resolution — the content's own pixel grid, unchanged by any placement drag (D8).
  if (cell.detail.native_pixels.has_value()) {
    ImGui::Text("Native resolution: %d x %d px", cell.detail.native_pixels->first,
                cell.detail.native_pixels->second);
  } else {
    ImGui::TextUnformatted("Native resolution: n/a (resolution-independent)");
  }
  // Placed size — the placement-mapped content extent in composition units (D7/§6:234), a
  // pure Affine::map_rect, SEPARATE from the native pixel count.
  if (cell.content_bounds.has_value()) {
    const arbc::Rect placed = cell.placement.map_rect(*cell.content_bounds);
    ImGui::Text("Placed size: %.0f x %.0f composition units", placed.width(), placed.height());
  } else {
    ImGui::TextUnformatted("Placed size: n/a (unbounded)");
  }

  // Provenance + health (D8/D11), from the generic classification — never a kind switch.
  switch (cell.detail.source) {
  case scene::DetailSource::ResolutionIndependent:
    // No detail floor: no health verdict, never a false "soft" (Constraint 4).
    ImGui::TextUnformatted("Resolution-independent - no native detail floor.");
    return;
  case scene::DetailSource::ReferencedImage:
    // A terminal, fully-true statement (D8/§6:242): the file is the floor, nothing to
    // resample to. No resample affordance is offered (D-resolution-5: no dead UI).
    ImGui::TextUnformatted("Source-limited - no crisper detail exists.");
    break;
  case scene::DetailSource::PaintedRaster:
    break;
  }

  // A finite-detail cell whose grid is unavailable (an image that failed to decode: empty
  // bounds) reports no verdict rather than a false one (Constraint 4).
  if (!cell.detail.native_pixels.has_value()) {
    return;
  }
  const interact::ResolutionHealth health = worst_cell_health(cell, cameras);
  if (health.verdict == interact::ResolutionVerdict::NotApplicable) {
    ImGui::TextUnformatted("Health: n/a (no capturing camera).");
    return;
  }
  if (health.verdict == interact::ResolutionVerdict::Soft) {
    // "soft - sampled at 1.4x . detail floor 2048" (docs/00-design.md:157): the softness and,
    // for a painted raster, the detail floor the (deferred) resample would grow.
    ImGui::Text("Health: soft - sampled at %.2fx . detail floor %d x %d px", health.ratio,
                cell.detail.native_pixels->first, cell.detail.native_pixels->second);
  } else {
    ImGui::Text("Health: crisp - sampled at %.2fx (at or below native).", health.ratio);
  }
}

// The per-camera resolution-health read of the cells `camera` captures — the read
// editor.cameras.manip explicitly deferred to this leaf (D9/D-manip-6). Resolution-
// independent cells (no native floor) are omitted; there is nothing to judge.
void draw_capture_health(const scene::Camera& camera, const std::vector<scene::Cell>& cells) {
  ImGui::Separator();
  ImGui::TextUnformatted("Capture health");
  bool any = false;
  for (const scene::Cell& cell : cells) {
    if (!cell.detail.native_pixels.has_value()) {
      continue;
    }
    const interact::ResolutionHealth h = interact::resolution_health(
        cell.detail.native_pixels->first, cell.detail.native_pixels->second, cell.placement,
        camera.resolution.width, camera.resolution.height, camera.frame);
    if (h.verdict == interact::ResolutionVerdict::NotApplicable) {
      continue;
    }
    any = true;
    ImGui::Text("- %s: %s (%.2fx)", cell.kind_id.empty() ? "cell" : cell.kind_id.c_str(),
                h.verdict == interact::ResolutionVerdict::Soft ? "soft" : "crisp", h.ratio);
  }
  if (!any) {
    ImGui::TextDisabled("No finite-detail cells captured.");
  }
}

} // namespace

void CameraInspector::draw(std::string_view /*view_id*/) {
  const std::vector<scene::Camera> cameras = scene::cameras(state_.document());
  const std::vector<scene::Cell> cells = scene::cells(state_.document(), state_.registry());

  // The selection readout (editor.cells.resolution, D-resolution-6): a selected CELL's
  // native/placed size + D8 health/provenance, read fresh each frame off the live pinned
  // snapshot (Constraint 6). The project selection is keyed by content ObjectId
  // (D-selection-1), so `primary()` answers "which cell's resolution do we show" with no new
  // state; a stale id simply matches nothing. A selected CAMERA is served by the camera
  // section below (its per-camera capture health).
  const arbc::ObjectId selected = state_.selection().primary();
  for (const scene::Cell& cell : cells) {
    if (cell.id == selected) {
      draw_cell_readout(cell, cameras);
      ImGui::Separator();
      break;
    }
  }

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

  // The per-camera resolution-health read of the cells the target camera captures — the read
  // editor.cameras.manip deferred to this leaf (D9/D-manip-6). Pure read over the same pinned
  // `cells` snapshot; opens no transaction.
  draw_capture_health(*target, cells);
}

} // namespace ace::app
