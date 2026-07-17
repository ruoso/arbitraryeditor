#pragma once

namespace ace::views {

// The views component. See docs/01-architecture.md §8 (component levelization).
const char* name();

// The stable ImGui window title/id of the render_probe pane — exposed so the
// e2e can drive the pane by the same id the view draws under.
const char* probe_pane_title();

// The render_probe pane (A6 display tail): one ImGui::Begin/Image/End window
// showing the offline-rendered texture. `texture` is a gl::upload_rgba8 handle;
// `width`/`height` are the image dimensions. No dockspace — one static pane
// (refinement Constraint 7).
void draw_probe_pane(unsigned int texture, int width, int height);

} // namespace ace::views
