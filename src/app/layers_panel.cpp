#include <ace/app/canvas_view.hpp>
#include <ace/app/layers_panel.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/commands/cells.hpp> // reorder_cell_command
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/ids.hpp>

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ace::app {
namespace {

// A compact provenance affordance (D11): the editable? class from the generic `DetailSource`
// (never a `kind_id` switch, A16/D-resolution-2) and the bytes-where? class from the generic
// `borrowed` flag. Distinct text per class so a painted-raster row and a referenced-image row
// read differently (the e2e's provenance assertion), reusing the same classification the
// inspector's `describe_detail_source` is keyed on — one provenance vocabulary across both panels.
std::string provenance_tag(const scene::CellDetail& detail) {
  const char* source = "resolution-independent";
  switch (detail.source) {
  case scene::DetailSource::PaintedRaster:
    source = "painted";
    break;
  case scene::DetailSource::ReferencedImage:
    source = "referenced";
    break;
  case scene::DetailSource::ResolutionIndependent:
    break;
  }
  return std::string(source) + ", " + (detail.borrowed ? "borrowed" : "owned");
}

// Apply a row click to the ONE shared selection (Constraint 6): plain = Replace, Shift = Add,
// Cmd/Ctrl = Toggle. Cmd/Ctrl is Ctrl OR Super (D7). Selection is transient app state — never a
// transaction — so this mutates `AppState::selection()` directly, exactly as the canvas does.
void apply_row_selection(commands::Selection& selection, arbc::ObjectId id) {
  const ImGuiIO& io = ImGui::GetIO();
  const bool cmd = io.KeyCtrl || io.KeySuper;
  if (io.KeyShift) {
    selection.add(id);
  } else if (cmd) {
    selection.toggle(id);
  } else {
    selection.select(id);
  }
}

// A read-only, indented peek of a composition's cells (expand, D-layers-5): no drag, no disclosure,
// and — critically — no selection change, "peek children, still editing the parent" (D17). Each row
// carries a stable `###peek_row_<id>` so the e2e can assert the children were revealed; clicks are
// intentionally ignored (peek is read-only). One level deep.
void draw_peek_children(const std::vector<scene::Cell>& children) {
  ImGui::Indent();
  if (children.empty()) {
    ImGui::TextDisabled("(empty)");
  }
  // Front→back, the same orientation as the top-level list.
  for (std::size_t i = children.size(); i-- > 0;) {
    const scene::Cell& child = children[i];
    const std::string name = child.kind_id.empty() ? "(unknown kind)" : child.kind_id;
    const std::string label = name + "  [" + provenance_tag(child.detail) + "]###peek_row_" +
                              std::to_string(child.id.value);
    (void)ImGui::Selectable(label.c_str(), false); // read-only: the click result is discarded
  }
  ImGui::Unindent();
}

} // namespace

LayersPanel::LayersPanel(commands::AppState& state, CanvasView& canvas)
    : state_(state), canvas_(canvas) {}

void LayersPanel::draw(std::string_view /*view_id*/) {
  arbc::Document& document = state_.document();
  const arbc::Registry& registry = state_.registry();
  std::optional<arbc::ObjectId>& entered = state_.entered_composition();

  // The composition every read/edit targets this frame — the entered scope when it names a live
  // composition, else Root (the per-frame fail-safe, Constraint 8). Re-resolved fresh here so a
  // scope whose composition vanished silently degrades to Root.
  const arbc::ObjectId active = scene::active_composition(document, entered);

  // --- Breadcrumb (Root ▸ … ▸ child), DERIVED each frame (Constraint 9) ------------------------
  const std::vector<scene::Breadcrumb> path = scene::composition_path(document, registry, entered);
  for (std::size_t k = 0; k < path.size(); ++k) {
    if (k > 0) {
      ImGui::SameLine();
      ImGui::TextUnformatted(">");
      ImGui::SameLine();
    }
    const std::string crumb =
        path[k].label + "###crumb_" + std::to_string(path[k].composition.value);
    if (ImGui::SmallButton(crumb.c_str())) {
      // Climbing is transient session state, never a transaction (Constraint 8): crumb 0 = Root.
      entered = (k == 0) ? std::nullopt : std::optional<arbc::ObjectId>(path[k].composition);
    }
  }
  ImGui::Separator();

  // --- Cameras section — flat, selectable, NOT reorderable (Constraint 4, A14) -----------------
  const std::vector<scene::Camera> cameras = scene::cameras(document);
  ImGui::TextUnformatted("Cameras");
  if (cameras.empty()) {
    ImGui::TextDisabled("No cameras.");
  }
  for (const scene::Camera& cam : cameras) {
    const bool selected = state_.selection().contains(cam.id);
    const std::string label = cam.name + "  (" + std::to_string(cam.resolution.width) + "x" +
                              std::to_string(cam.resolution.height) + ")###camera_row_" +
                              std::to_string(cam.id.value);
    if (ImGui::Selectable(label.c_str(), selected)) {
      apply_row_selection(state_.selection(), cam.id);
    }
  }

  ImGui::Separator();

  // --- Layers section — the ACTIVE composition's cells, front→back (Constraint 1/2) ------------
  ImGui::TextUnformatted("Layers");
  const std::vector<scene::Cell> cells = scene::cells(document, registry, active);
  if (cells.empty()) {
    // A defined empty state (Constraint's empty-composition case) with a stable id and no stale
    // rows.
    (void)ImGui::Selectable("No layers in this composition.###layers_empty", false);
  }

  // Deferred so a reorder's `apply_edit` (which mutates the document) runs AFTER the row walk over
  // the frame's `cells` snapshot, not mid-iteration. Next frame re-reads the new order.
  std::optional<std::pair<arbc::ObjectId, std::uint32_t>> pending_reorder;

  const int count = static_cast<int>(cells.size());
  for (int slot = 0; slot < count; ++slot) {
    // Front→back: list slot 0 is the frontmost = the LAST (top-of-z) member of the bottom→top walk.
    const scene::Cell& cell = cells[static_cast<std::size_t>(count - 1 - slot)];
    const arbc::ObjectId id = cell.id;
    const std::optional<arbc::ObjectId> child = scene::nested_composition_of(document, id);
    const bool nested = child.has_value();

    // No PushID here: every per-row widget already carries a globally-unique `###..._<id>` id keyed
    // by the cell's ObjectId, so the rows are addressable as "Layers/###layer_row_<id>" directly
    // (an extra id-stack level would hide them from the stable ref path the e2e drives).

    // Disclosure triangle — EXPAND (peek), never select/enter (Constraint 6/7). Its own widget id,
    // so clicking it does not trigger the row Selectable.
    bool expanded = false;
    if (nested) {
      expanded = expanded_.count(id) != 0;
      const std::string arrow_id = "###layer_expand_" + std::to_string(id.value);
      if (ImGui::ArrowButton(arrow_id.c_str(), expanded ? ImGuiDir_Down : ImGuiDir_Right)) {
        if (expanded) {
          expanded_.erase(id);
        } else {
          expanded_.insert(id);
        }
        expanded = !expanded;
      }
      ImGui::SameLine();
    } else {
      // Keep the row bodies left-aligned whether or not a triangle is present.
      ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), 0.0F));
      ImGui::SameLine();
    }

    const std::string name = cell.kind_id.empty() ? "(unknown kind)" : cell.kind_id;
    // Provenance leads the visible label (D11) so it is distinct per class in the row text itself —
    // painted vs referenced vs owned/borrowed. The stable `###layer_row_<id>` id keys the row
    // across reorders regardless of the display text.
    const std::string label = "[" + provenance_tag(cell.detail) + "] " + name + "###layer_row_" +
                              std::to_string(id.value);
    const bool selected = state_.selection().contains(id);
    if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
      if (nested && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        // ENTER (Constraint 8): set the project-level scope to the child. Transient, no
        // transaction.
        entered = child;
      } else {
        apply_row_selection(state_.selection(), id);
      }
    }

    // Drag-reorder — z-order (Constraint 2/3). The payload carries the dragged row's list slot.
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
      ImGui::SetDragDropPayload("ACE_LAYER_ROW", &slot, sizeof(slot));
      ImGui::TextUnformatted(name.c_str());
      ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ACE_LAYER_ROW")) {
        const int from_slot = *static_cast<const int*>(payload->Data);
        const scene::MembershipMove move = scene::list_drag_to_membership(from_slot, slot, count);
        const arbc::ObjectId moved = cells[static_cast<std::size_t>(count - 1 - from_slot)].id;
        pending_reorder = std::make_pair(moved, move.to_index);
      }
      ImGui::EndDragDropTarget();
    }

    if (nested && expanded) {
      draw_peek_children(scene::cells(document, registry, *child));
    }
  }

  if (pending_reorder.has_value()) {
    const commands::Command command =
        commands::reorder_cell_command(active, pending_reorder->first, pending_reorder->second);
    canvas_.apply_edit([this, command] { command.apply(state_.document()); });
  }
}

} // namespace ace::app
