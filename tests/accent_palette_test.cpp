// editor.canvas.accent_palette — the value-identity oracle (docs §9's Catch2 layer,
// D-accent_palette-3). The property this refactor must preserve is "this expression evaluates
// to that 32-bit value", which a `==` against the literal pins exactly, where a framebuffer
// read pins it only to ±8 per channel after llvmpipe blending. Every assertion below is
// written against the RAW `IM_COL32(...)` literal the refactor replaced — never against
// another palette constant — so the test stays an independent oracle rather than a tautology.
//
// It lives in `ace_shell_test` rather than `ace_tests` because only that target links
// `ace::app` (CMakeLists.txt), following tests/focused_framing_test.cpp's precedent. It needs
// no GL, no window and no ImGui context — only the `IM_COL32` macro.
#include <ace/app/accent.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>

#include <cstdint>

using ace::app::accent;
using ace::app::camera_frame;
using ace::app::k_focus_marker_color;
using ace::app::Rgb;
using ace::app::rgba;

namespace {

// A `std::uint8_t` the compiler cannot fold away, so the `constexpr` helpers are actually
// EXECUTED (and therefore instrumented by gcov) rather than only constant-evaluated. Marked
// volatile-read so no optimisation level can turn the call sites back into literals.
std::uint8_t runtime_alpha(std::uint8_t a) {
  volatile std::uint8_t cell = a;
  return static_cast<std::uint8_t>(cell);
}

} // namespace

TEST_CASE("accent palette: the five accent sites reproduce their shipped literals",
          "[accent_palette]") {
  // The focus marker (canvas_view.cpp:297) and the active camera frame / selected-cell outline.
  CHECK(accent(255) == IM_COL32(120, 200, 255, 255));
  // The marquee band.
  CHECK(accent(220) == IM_COL32(120, 200, 255, 220));
  // The marquee wash.
  CHECK(accent(40) == IM_COL32(120, 200, 255, 40));
}

TEST_CASE("accent palette: the camera-frame hue is the amber, not the accent", "[accent_palette]") {
  CHECK(camera_frame(200) == IM_COL32(255, 210, 80, 200));
  // Again with an alpha the compiler cannot fold, so the wrapper body is executed rather than
  // only constant-evaluated (the gcov hazard the coverage gate cares about).
  CHECK(camera_frame(runtime_alpha(200)) == IM_COL32(255, 210, 80, 200));
  // The inactive frame is a DIFFERENT hue with a different meaning: `col_frame` and
  // `col_active` must keep selecting between two distinct values.
  CHECK(camera_frame(200) != accent(200));
}

TEST_CASE("accent palette: alpha is the only thing the parameter changes", "[accent_palette]") {
  const ImU32 alpha_mask = static_cast<ImU32>(0xFFU) << IM_COL32_A_SHIFT;
  const ImU32 reference = accent(runtime_alpha(255)) & ~alpha_mask;
  for (const std::uint8_t a :
       {std::uint8_t{0}, std::uint8_t{40}, std::uint8_t{220}, std::uint8_t{255}}) {
    const ImU32 packed = accent(runtime_alpha(a));
    // The three non-alpha bytes never move…
    CHECK((packed & ~alpha_mask) == reference);
    // …and the alpha byte is exactly what was asked for.
    CHECK(((packed >> IM_COL32_A_SHIFT) & 0xFFU) == static_cast<ImU32>(a));
  }
}

TEST_CASE("accent palette: rgba packs through IM_COL32", "[accent_palette]") {
  // Pinning the packer to the macro: hand-rolled shifts would disagree with `IM_COL32`'s
  // `IM_COL32_R_SHIFT`/`IM_COL32_A_SHIFT` under `IMGUI_USE_BGRA_PACKED_COLOR`.
  CHECK(rgba(Rgb{1, 2, 3}, 4) == IM_COL32(1, 2, 3, 4));
  CHECK(rgba(Rgb{1, 2, 3}, runtime_alpha(4)) == IM_COL32(1, 2, 3, 4));
}

TEST_CASE("accent palette: the focus marker constant is the opaque accent", "[accent_palette]") {
  // D-focused_canvas_indicator-4 at the value level: alpha 255 is what makes the e2e's ±8
  // accent-dominance probe robust, so a translucent tint here is a regression, not a tweak.
  CHECK(k_focus_marker_color == IM_COL32(120, 200, 255, 255));
  CHECK(k_focus_marker_color == accent(255));
}
