#pragma once

#include <arbc/base/transform.hpp>

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

} // namespace ace::app
