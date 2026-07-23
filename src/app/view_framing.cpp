#include <ace/app/view_framing.hpp>

#include <span>
#include <string_view>

namespace ace::app {

namespace {
// A pane carries a framing only once it has area — the degenerate-pane early-out in
// `CanvasView::draw_content` means a zero-area pane never renders, and a zero pane is the
// header's "no live canvas" sentinel rather than a framing.
bool sized(const ViewFraming& framing) { return framing.pane_w > 0 && framing.pane_h > 0; }
} // namespace

std::string_view focus_target(std::span<const PaneFraming> panes_by_id,
                              std::string_view focused_view_id) {
  // (1) The focused pane, when it is still present AND sized. An empty focus id (the
  // `primary_framing()` projection) skips this pass entirely, so the fallback below IS the
  // historical lowest-id rule, unchanged.
  if (!focused_view_id.empty()) {
    for (const PaneFraming& pane : panes_by_id) {
      if (pane.view_id == focused_view_id && sized(pane.framing)) {
        return pane.view_id;
      }
    }
  }
  // (2) The fallback: the first sized pane in view-id order. Reached by a stale hint (the
  // focused canvas was closed), an unsized one, and the never-focused session alike — all
  // three are "the user can plainly see a canvas", so none of them may refuse.
  for (const PaneFraming& pane : panes_by_id) {
    if (sized(pane.framing)) {
      return pane.view_id;
    }
  }
  // (3) No sized pane at all: the "no live canvas" answer.
  return std::string_view{};
}

ViewFraming framing_for_focus(std::span<const PaneFraming> panes_by_id,
                              std::string_view focused_view_id) {
  // Resolve the target BY NAME through the one rule, then hand back that row's framing. The
  // behaviour is bit-for-bit what the three-branch loop above always produced; what changed is
  // that the branch matrix now lives in exactly one place, so chrome naming the target pane and
  // a verb consuming its framing are structurally incapable of disagreeing
  // (D-focused_canvas_indicator-1). An empty target matches no row (view ids are never empty),
  // so it falls through to the "no live canvas" sentinel.
  const std::string_view target = focus_target(panes_by_id, focused_view_id);
  for (const PaneFraming& pane : panes_by_id) {
    if (pane.view_id == target) {
      return pane.framing;
    }
  }
  return ViewFraming{};
}

} // namespace ace::app
