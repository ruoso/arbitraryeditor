#pragma once

#include <ace/dockmodel/view_registry.hpp>

#include <functional>
#include <string_view>

namespace ace::commands {
// The one owned project session (A7/D19); the History body reads its journal and
// loops the shipped undo/redo verbs. Forward-declared to keep this header light —
// views.cpp includes <ace/commands/app_state.hpp> for the definition.
class AppState;
} // namespace ace::commands

namespace ace::views {

// The views component. See docs/01-architecture.md §8 (component levelization).
const char* name();

// The stable ImGui window title/id of the render_probe pane — exposed so the
// e2e can drive the pane by the same id the view draws under.
const char* probe_pane_title();

// The render_probe pane (A6 display tail): one ImGui::Begin/Image/End window
// showing the offline-rendered texture. `texture` is a gl::upload_rgba8 handle;
// `width`/`height` are the image dimensions. The standalone pre-registry pane —
// the dockspace draws the Canvas view body via draw_probe_image instead.
void draw_probe_pane(unsigned int texture, int width, int height);

// The render_probe image (the Canvas view body — D18 "the canvas is a view"):
// draws just the Image into the CURRENT ImGui window (no Begin/End — the
// dockspace owns the window + the tab ✕). draw_probe_pane wraps this.
void draw_probe_image(unsigned int texture, int width, int height);

// A per-type view body: draws the view's content into the CURRENT ImGui window.
// `view_id` is the dockmodel instance id being drawn (a type may draw multiple
// instances). The dockspace owns the enclosing Begin/End + the close button.
using ViewBody = std::function<void(std::string_view view_id)>;

// Register (or replace) the body drawn for `type` — the seam each downstream
// panel leaf (inspector/layers/…/canvas.view) plugs its real body into without
// touching the L1 catalog (D-view-registry-5). Passing an empty std::function
// restores the default labeled placeholder.
void register_view_body(dockmodel::ViewType type, ViewBody body);

// Draw the body of the view `view_id` into the current window: parse the id to
// its ViewType and dispatch to that type's registered body, or a default
// labeled placeholder when none is registered. An unparseable id draws an
// "unknown view" placeholder.
void draw_view(std::string_view view_id);

// The History view body (D18 "History is a view"; D-history-1): renders the one
// owned session's undo journal as an ordered list — a synthetic base row (the
// pre-first-edit state) plus one row per journal entry in chronological order, the
// head highlighted and future/redoable entries dimmed — and navigates it when a row
// is clicked by looping the shipped commands::undo / commands::redo verbs toward the
// row's target cursor (single-step only; D-history-4/5). A pure reader/navigator: it
// reads depth()/cursor()/entry_at(i).name fresh each frame, never mutates the
// journal directly, and keeps no shadow copy (Constraint 1/6). Draws into the
// CURRENT window (the dockspace owns Begin/End). The L4 shell registers it capturing
// the one AppState& and clears it on exit (D-history-3).
void draw_history(commands::AppState& state, std::string_view view_id);

} // namespace ace::views
