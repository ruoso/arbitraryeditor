#include <ace/app/color_panel.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/commands/color.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ace::app {
namespace {

// The widget→storage marshal (Constraint 4): a 0..1 float channel back to the native 8-bit sRGB
// precision, round-to-nearest and clamped. The inverse of `channel / 255.0F`, so a value the picker
// displays as an integer 0–255 round-trips to that exact byte.
std::uint8_t to_u8(float channel) {
  return static_cast<std::uint8_t>(std::lround(std::clamp(channel, 0.0F, 1.0F) * 255.0F));
}

} // namespace

ColorPanel::ColorPanel(commands::AppState& state) : state_(state) {}

void ColorPanel::draw(std::string_view view_id) {
  (void)view_id;

  // Marshal the stored 8-bit sRGB straight-alpha truth OUT to the widget's 0..1 floats. The picker
  // never sees premultiplied or linear values — it edits the user-space sRGB color directly (D10).
  const commands::SrgbColor current = state_.active_color();
  float rgba[4] = {static_cast<float>(current.r) / 255.0F, static_cast<float>(current.g) / 255.0F,
                   static_cast<float>(current.b) / 255.0F, static_cast<float>(current.a) / 255.0F};

  // The D10 picker (D-color-3): HSV area + hue, `#RRGGBB` hex, 0–255 RGB, and a straight-alpha
  // opacity bar. `Uint8`/`InputRGB` keep the numeric fields on the sRGB byte scale the storage
  // uses.
  const ImGuiColorEditFlags flags = ImGuiColorEditFlags_DisplayRGB |
                                    ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_AlphaBar |
                                    ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_InputRGB;
  if (ImGui::ColorPicker4("###color_picker", rgba, flags)) {
    // Marshal the edited floats BACK to the canonical 8-bit sRGB store. This is the only writer of
    // the active color from the picker side; paint consumers read the derived working pixel.
    state_.set_active_color(
        commands::SrgbColor{to_u8(rgba[0]), to_u8(rgba[1]), to_u8(rgba[2]), to_u8(rgba[3])});
  }
}

} // namespace ace::app
