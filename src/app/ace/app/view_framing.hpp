#pragma once

#include <arbc/base/transform.hpp>

#include <span>
#include <string_view>

namespace ace::app {

// A canvas pane's TRANSIENT viewport framing, taken by VALUE (editor.cells.model
// Constraint 7 / D-nav-1): the composition-units -> device-pixels camera the pane is
// currently looking through, plus its device size. Session state — never a
// transaction, never persisted.
//
// It exists so the L4 gateway can compute a cell's provisional placement
// (`interact::place_in_view`) from what the user is actually looking at, without the
// gateway holding a `CanvasView&`: the shell binds a provider once, exactly as it
// binds the edit runner. A zero pane means "no live canvas", and the gateway falls
// back to framing the root composition itself.
struct ViewFraming {
  arbc::Affine camera = arbc::Affine::identity();
  int pane_w = 0;
  int pane_h = 0;
};

// One live canvas pane's offered framing, keyed by its dock view id ("canvas#1", "canvas#2",
// …) — the input row of the selection rule below. `view_id` is a NON-OWNING view into the
// caller's own key storage (`CanvasView::presenters_`'s keys), so a `PaneFraming` must not
// outlive it; every shipped caller builds the array and consumes it in one expression.
struct PaneFraming {
  std::string_view view_id;
  ViewFraming framing;
};

// WHICH pane the framing-derived verbs act on, BY NAME (D-focused_canvas_indicator-1): the
// winner-selection half of `framing_for_focus`, hoisted so a caller can ask *which pane* the
// verbs will act on rather than only *what framing* they will get. Returns the focused pane's
// id when that pane is present and sized, else the first sized pane's id in `panes_by_id`
// order (the lowest-id live pane), else an EMPTY `string_view` — the name-side spelling of the
// zero `ViewFraming` sentinel below.
//
// `framing_for_focus` is implemented ON TOP of this, so the chrome that NAMES the target pane
// (the focused-canvas marker) and the verb that CONSUMES its framing cannot drift apart: one
// rule, two projections of the same answer.
//
// The returned view borrows the winning row's `view_id`, i.e. the CALLER's key storage
// (`PaneFraming`'s non-owning contract above) — never the row array, which may be a temporary.
//
// Pure: no ImGui, no GL, no `CanvasView`.
std::string_view focus_target(std::span<const PaneFraming> panes_by_id,
                              std::string_view focused_view_id);

// WHICH pane's framing the framing-derived verbs act on (D23's "which viewport, when more
// than one canvas is open", D-mint_from_focused_canvas-3): the FOCUSED pane's framing when
// that pane is present and sized, else the FIRST sized pane in `panes_by_id` order (the
// caller passes them view-id ordered, so that is the lowest-id live pane), else the zero
// `ViewFraming` — the "no live canvas" sentinel documented above, which
// `AppProjectGateway::live_view_framing()` keys off to refuse a mint.
//
// The rule is TOTAL: a focused id naming no pane (a closed canvas, or one never focused) and
// a focused pane that exists but has never been sized both DEGRADE to the fallback, never to
// the sentinel — refusing a mint the user can plainly perform would be a regression wearing a
// fix's clothes (D-mint_from_focused_canvas-2). An empty `focused_view_id` therefore
// reproduces the historical lowest-id rule exactly, which is what keeps
// `CanvasView::primary_framing()` bit-identical.
//
// Pure: no ImGui, no GL, no `CanvasView` — the whole branch matrix is headless-testable
// (docs/01-architecture.md §8's "logic belongs where Catch2 can see it").
ViewFraming framing_for_focus(std::span<const PaneFraming> panes_by_id,
                              std::string_view focused_view_id);

} // namespace ace::app
