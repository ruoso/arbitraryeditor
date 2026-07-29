#pragma once

#include <arbc/base/ids.hpp>

#include <string_view>
#include <unordered_set>

namespace ace::commands {
class AppState;
} // namespace ace::commands

namespace ace::app {

class CanvasView;

// The Layers panel (editor.panels.layers; D6/D11/D17): the real body for `ViewType::Layers`, the
// order/hierarchy/naming structure view D6 keeps co-primary with the overview over the one shared
// selection. Two sections: a flat, non-reorderable Cameras list (each row selectable into the
// shared selection, A14/D7) and a Layers list of the ACTIVE composition's cells drawn front→back
// (top-of-list = frontmost = top-of-z), each row showing its referenced/owned/painted provenance
// (D11, reusing the inspector's generic-facet classification) and — for a nested cell — a
// disclosure triangle. Three distinct gestures over the layers list (D17, Select ≠ expand ≠ enter):
// single-click SELECTS (Replace; Shift = Add; Cmd/Ctrl = Toggle), the disclosure triangle EXPANDS
// (peek children read-only, still editing the parent), a double-click ENTERS (sets the
// project-level entered-composition scope, re-roots the list to the child, and renders a breadcrumb
// that climbs out). A drag REORDERS a layer's z-order through the one `scene::reorder_cell` verb,
// dispatched on `CanvasView::apply_edit` (the writer funnel, never a direct L4 `Document`
// mutation). Reads use the shipped UI-thread scene read-seam (A18). The only UI-thread-only session
// state it owns is the set of expanded rows; the entered scope lives project-level on `AppState`
// (D-layers-3).
class LayersPanel {
public:
  LayersPanel(commands::AppState& state, CanvasView& canvas);

  // Draw the panel into the CURRENT ImGui window (the dockspace owns the enclosing Begin/End).
  void draw(std::string_view view_id);

private:
  commands::AppState& state_;
  CanvasView& canvas_;
  // Which nested rows are expanded to peek their children (transient UI state; a stale id simply
  // matches no live row, so it needs no explicit prune). Not the entered scope — expand ≠ enter.
  std::unordered_set<arbc::ObjectId> expanded_;
};

} // namespace ace::app
