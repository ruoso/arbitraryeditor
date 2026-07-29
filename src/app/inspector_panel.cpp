#include <ace/app/canvas_view.hpp>
#include <ace/app/inspector_panel.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/interact/interact.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/transform.hpp>

#include <imgui.h>

#include <utility>
#include <vector>

namespace ace::app {
namespace {

// The worst (softest) resolution-health verdict for `cell` across the cameras that capture it —
// the ratio the Source block leads with (D-resolution-4). Mirrors the shipped camera_inspector
// helper; `cell` must carry a native pixel grid (the caller gates on it).
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

// --- Block 1: Transform (units) — a live READOUT, never a numeric editor (D-inspector-1). -----
void draw_transform_block(const scene::Cell& cell) {
  ImGui::TextUnformatted("Transform");
  ImGui::Separator();
  const interact::PlacementReadout t =
      interact::decompose_placement(cell.placement, cell.content_bounds);
  if (!t.valid) {
    ImGui::TextUnformatted("Placement: n/a (degenerate)");
  } else {
    // Composition units (D7/§6) — never "px", which is a health readout only (D5).
    ImGui::Text("Position: %.1f, %.1f composition units", t.position.x, t.position.y);
    if (cell.content_bounds.has_value()) {
      ImGui::Text("Placed size: %.0f x %.0f composition units", t.placed.width(),
                  t.placed.height());
    } else {
      ImGui::TextUnformatted("Placed size: n/a (unbounded)");
    }
    ImGui::Text("Rotation: %.1f deg", t.rotation_deg);
    ImGui::Text("Shear: %.1f deg", t.shear_deg);
  }
  // Native/working resolution as a PLAIN readout beside the placement (D5: px is a health
  // readout only, never the placement unit). The transform block offers no control that changes
  // stored resolution (D8/D9); the resample action + health badge are editor.cells.resolution's.
  if (cell.detail.native_pixels.has_value()) {
    ImGui::Text("Native resolution: %d x %d px", cell.detail.native_pixels->first,
                cell.detail.native_pixels->second);
  } else {
    ImGui::TextUnformatted("Native resolution: n/a (resolution-independent)");
  }
}

// --- Block 2: Appearance & stacking — opacity slider + visibility toggle + z-order readout. ----
void draw_appearance_block(commands::AppState& state, CanvasView& canvas, const scene::Cell& cell,
                           const std::vector<scene::Cell>& cells) {
  ImGui::TextUnformatted("Appearance & stacking");
  ImGui::Separator();

  // Straight-alpha opacity (D10). The slider reads the LIVE `LayerRecord` opacity each frame, so
  // an undo/redo is reflected with no extra state. A change commits ONE undoable transaction
  // through `apply_edit` (never a direct `Document` mutation from L4, D-inspector-2).
  const arbc::ObjectId id = cell.id;
  float opacity = static_cast<float>(cell.opacity);
  if (ImGui::SliderFloat("Opacity", &opacity, 0.0F, 1.0F, "%.2f")) {
    const double value = opacity;
    canvas.apply_edit([&state, id, value] {
      scene::set_cell_opacity(state.document(), state.registry(), id, value);
    });
  }

  // Visibility toggle (`k_layer_visible`), the same one-transaction discipline.
  bool visible = cell.visible;
  if (ImGui::Checkbox("Visible", &visible)) {
    const bool value = visible;
    canvas.apply_edit([&state, id, value] {
      scene::set_cell_visible(state.document(), state.registry(), id, value);
    });
  }

  // Z-order position — a READOUT only (D6; interactive reorder is editor.panels.layers'). The
  // index is the cell's place in the already-ordered `cells()` list (back->front).
  const scene::ZOrderPosition pos = scene::z_order_position(cells, cell.id);
  if (pos.index >= 0) {
    ImGui::Text("Z-order: layer %d of %d (back to front)", pos.index + 1, pos.count);
  }
}

// --- Block 3: Source — provenance (D11) + resolution health, READ not recomputed (Constraint 6).
void draw_source_block(const scene::Cell& cell, const std::vector<scene::Camera>& cameras) {
  ImGui::TextUnformatted("Source");
  ImGui::Separator();
  // Provenance from the generic-facet classification (never a kind string, A16/D-resolution-2).
  ImGui::TextUnformatted(scene::describe_detail_source(cell.detail.source));
  if (!cell.detail.native_pixels.has_value()) {
    return; // resolution-independent or an unavailable image: no health verdict (Constraint 4)
  }
  // Health reuses the shipped interact::resolution_health path; the resample action + badge
  // styling are editor.cells.resolution's, so this is a plain readout.
  const interact::ResolutionHealth health = worst_cell_health(cell, cameras);
  if (health.verdict == interact::ResolutionVerdict::NotApplicable) {
    ImGui::TextUnformatted("Health: n/a (no capturing camera).");
  } else if (health.verdict == interact::ResolutionVerdict::Soft) {
    ImGui::Text("Health: soft - sampled at %.2fx . detail floor %d x %d px", health.ratio,
                cell.detail.native_pixels->first, cell.detail.native_pixels->second);
  } else {
    ImGui::Text("Health: crisp - sampled at %.2fx (at or below native).", health.ratio);
  }
}

} // namespace

InspectorPanel::InspectorPanel(commands::AppState& state, CanvasView& canvas)
    : state_(state), canvas_(canvas), camera_(state, canvas) {}

void InspectorPanel::draw(std::string_view view_id) {
  // The one project selection keys the sheet (D-inspector-6 / A18): a cell, a camera, or empty.
  // The canvas prunes a dangling id every frame (D-selection-7), so an invalid primary is the
  // honest "nothing selected", not a stale object.
  const arbc::ObjectId selected = state_.selection().primary();
  if (!selected.valid()) {
    ImGui::TextUnformatted("Nothing selected.");
    ImGui::TextDisabled("Select a cell or camera to inspect it.");
    return;
  }

  const std::vector<scene::Cell> cells = scene::cells(state_.document(), state_.registry());
  const std::vector<scene::Camera> cameras = scene::cameras(state_.document());

  // A selected CELL gets the three-block property sheet.
  for (const scene::Cell& cell : cells) {
    if (cell.id == selected) {
      draw_transform_block(cell);
      ImGui::Separator();
      draw_appearance_block(state_, canvas_, cell, cells);
      ImGui::Separator();
      draw_source_block(cell, cameras);
      return;
    }
  }

  // A selected CAMERA gets the shipped camera block, kept intact (D-inspector, "routes to it").
  for (const scene::Camera& cam : cameras) {
    if (cam.id == selected) {
      camera_.draw(view_id);
      return;
    }
  }

  // Selected id resolves to neither live object (a transient the prune guarantee makes rare).
  ImGui::TextUnformatted("Nothing selected.");
}

} // namespace ace::app
