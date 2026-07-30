#pragma once

#include <string_view>

namespace ace::commands {
class AppState;
} // namespace ace::commands

namespace ace::app {

// The conventional sRGB color picker (editor.panels.color; D10 / §7 / D-color-3): the real body
// for the singleton `ViewType::Color` view. Draws `ImGui::ColorPicker4` — HSV area + hue, `#RRGGBB`
// hex, 0–255 RGB, straight-alpha opacity — editing the canonical `commands::AppState` sRGB active
// color. The float↔8-bit marshalling happens HERE, at the widget boundary (Constraint 4): the
// stored truth is 8-bit sRGB straight-alpha, the widget speaks 0..1 floats, and
// premultiplied/linear values are never shown (D10 :279). No bespoke picker widget, no ImGui below
// L3; the panel reaches the L1 active-color seam through the `views→commands` edge it already
// holds. A pure reader/writer of `AppState` session state — no transaction, no journal entry,
// nothing posted to the writer.
class ColorPanel {
public:
  explicit ColorPanel(commands::AppState& state);

  // Draw the picker into the CURRENT ImGui window (the dockspace owns the enclosing Begin/End).
  // `view_id` is the Color instance id (a project-level singleton, D19), accepted for symmetry with
  // the other view bodies.
  void draw(std::string_view view_id);

private:
  commands::AppState& state_;
};

} // namespace ace::app
