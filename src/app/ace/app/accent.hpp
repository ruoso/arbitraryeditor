#pragma once

#include <imgui.h>

#include <cstdint>

namespace ace::app {

// The editor's chrome colour vocabulary (editor.canvas.accent_palette). Every piece of
// canvas chrome that used to carry a bare `IM_COL32(...)` at its draw site now names its
// hue here and passes its own alpha — the alphas are per-site by design
// (D-focused_canvas_indicator-4): an unblended 1 px marker, a 40/255 wash and a 220/255
// band mean different things, so the helpers take alpha as a PARAMETER and deliberately
// carry no default that would let a call site drift silently.
//
// Everything is `constexpr`, so each call site is still a compile-time constant and the
// generated draw calls are byte-identical to the literals they replaced. This is a named
// palette, not a theming mechanism (D-accent_palette-6): no runtime mutability, no style
// struct, no preferences plumbing. It is the seam a future theming leaf would repoint.

// A hue, decomposed. Kept as one aggregate rather than three loose bytes so a hue can be
// passed around as a single value.
struct Rgb {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
};

// Pack a hue + alpha into ImGui's `ImU32`. Defined in terms of `IM_COL32` on purpose: the
// macro expands through `IM_COL32_R_SHIFT`/`IM_COL32_A_SHIFT`, which flip under
// `IMGUI_USE_BGRA_PACKED_COLOR`, so hand-rolled shifts would hard-code the very layout the
// macro exists to hide.
constexpr ImU32 rgba(Rgb c, std::uint8_t a) { return IM_COL32(c.r, c.g, c.b, a); }

// The accent: the focused-canvas marker, the active camera frame, the marquee wash and
// band, and the selected-cell outline.
inline constexpr Rgb k_accent{120, 200, 255};

// The inactive camera frame's amber — a DIFFERENT hue with a different meaning, named
// through the same packer rather than folded into the accent.
inline constexpr Rgb k_camera_frame{255, 210, 80};

constexpr ImU32 accent(std::uint8_t a) { return rgba(k_accent, a); }
constexpr ImU32 camera_frame(std::uint8_t a) { return rgba(k_camera_frame, a); }

// The focused-canvas marker's colour (D-focused_canvas_indicator-4): the accent, OPAQUE.
// Alpha 255 is not incidental — an unblended stroke lands identically over whatever the
// pane happens to be showing, which is what lets the e2e probe it under software GL
// without a content-dependent expectation.
inline constexpr ImU32 k_focus_marker_color = accent(255);

} // namespace ace::app
