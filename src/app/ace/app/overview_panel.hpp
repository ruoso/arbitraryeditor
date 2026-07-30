#pragma once

#include <ace/interact/interact.hpp> // interact::CellHandle (the gizmo handle vocabulary)

#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace ace::commands {
class AppState;
} // namespace ace::commands

namespace ace::app {

class CanvasView;

// The composition overview (editor.panels.overview; D6/§5): the real body for the singleton
// `ViewType::Overview`, the render-free schematic view of the WHOLE active composition D6 keeps
// co-primary with the Layers list over the one shared selection. It draws every active-composition
// cell as a CONTENT-SPACE hatched, dotted-bordered box at its true relative position/size/rotation
// (front fills occlude back, dotted borders always on top — z-order without a list, §5:183-189),
// every camera as a distinct frame + label + look-through/fit affordances (the export/shot map,
// §5:173), and the focused canvas pane's visible-composition rect as a highlighted, draggable
// navigator handle (§5:179). It is an EDITABLE layout surface: clicks/marquees feed the shared
// primitive-only pick core (`interact::pick_targets`/`click_selection`/`marquee_selection`) onto
// the one `commands::Selection`; dragging a box commits one `transform_cells_command` (move,
// placement only — D8); bring-forward/send-back reuse `scene::reorder_cell` (§6). The navigator
// drives ONLY the transient viewport camera through `CanvasView::drive_focused_camera` (no journal
// entry, not undoable — D24/D15). It reflects the entered-composition scope by re-rooting to
// `scene::active_composition` and dimming the rest, with the shipped `composition_path` breadcrumb;
// cameras are not pickable while entered (D29, free through `pick_targets`). Painting stays
// canvas-only (§6). Reads use the shipped UI-thread scene read-seam (A18); the only session state
// it owns is the in-progress drag/marquee gesture — never a selection copy (D19).
class OverviewPanel {
public:
  OverviewPanel(commands::AppState& state, CanvasView& canvas);

  // Draw the schematic into the CURRENT ImGui window (the dockspace owns the enclosing Begin/End).
  void draw(std::string_view view_id);

private:
  commands::AppState& state_;
  CanvasView& canvas_;

  // The in-progress box/frame move gesture (Constraint 8): UI-only session state, previewed as the
  // dragged placement and committed as ONE `transform_cells_command` on release. `active` is false
  // between gestures. Kind-agnostic — a cell placement and a camera frame are both a layer
  // transform.
  struct MoveGesture {
    bool active = false;
    arbc::ObjectId content; // the selection key (what a click selects / a drag re-places)
    arbc::ObjectId layer;   // the placing layer / frame carrier (the transform target)
    arbc::Affine start;     // the placement at grab (the preview base)
    arbc::Vec2 grab_comp;   // the pointer in composition units at grab
    bool moved = false;     // a genuine drag happened (else the release is a click)
  };
  MoveGesture move_;

  // The in-progress marquee gesture (Constraint 6): armed by a press on empty schematic space,
  // resolved to a `marquee_selection` on release. `anchor_comp` is the press point in composition
  // units.
  struct MarqueeGesture {
    bool active = false;
    bool moved = false; // a genuine drag happened (else the release is an empty-space click)
    arbc::Vec2 anchor_comp;
  };
  MarqueeGesture marquee_;

  // The in-progress transform-gizmo handle-drag (editor.panels.overview_gizmo; D-overview_gizmo-5):
  // a sibling of MoveGesture/MarqueeGesture — UI-thread-only session state (Constraint 10), never
  // journaled. Armed when a press over the selected box's gizmo hits a handle; the drag previews
  // the shipped-verb transform UI-only and commits ONE `transform_cells_command` on release (D8,
  // one undo per gesture). `group` selects the single-cell path vs the >=2-selection union-box
  // path. No transform arithmetic lives here — every handle/transform verb is the shipped L1
  // `interact` (D-overview_gizmo-1).
  struct GizmoGesture {
    bool active = false;
    bool group = false;
    bool moved = false; // a genuine drag happened (else the release is a click)
    interact::CellHandle handle = interact::CellHandle::None;
    arbc::Vec2 grab_comp{}; // the pointer in composition units at grab
    arbc::Vec2 pivot{};     // the transform pivot in composition units at grab
    // Single-cell path:
    arbc::ObjectId content;                        // the selection key
    arbc::ObjectId layer;                          // the placing layer (the transform target)
    arbc::Affine start = arbc::Affine::identity(); // the placement at grab (the preview base)
    arbc::Rect extent{};                           // the cell's content extent at grab
    // Group path: every selected member's (layer, start placement) at grab + the union box.
    std::vector<std::pair<arbc::ObjectId, arbc::Affine>> group_start;
    arbc::Rect group_extent{};
  };
  GizmoGesture gizmo_;

  // The persistent draggable gizmo pivot (UI session state): seeded to the box/union center, moved
  // by a Pivot-handle drag, reset when the gizmo target changes (mirrors CanvasView's cell pivot).
  std::optional<arbc::Vec2> gizmo_pivot_;
  std::optional<arbc::ObjectId> gizmo_pivot_for_;   // the single-cell key it was seeded for
  std::optional<arbc::Rect> gizmo_group_pivot_for_; // the union box it was seeded for
};

} // namespace ace::app
