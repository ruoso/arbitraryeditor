#pragma once

// editor.panels.color — the sRGB↔premultiplied-linear boundary made a library primitive
// (D10 / §7). The editor is the invisible translator: the user works entirely in sRGB /
// straight-alpha (`SrgbColor`), paint consumers read the derived premultiplied-linear
// `arbc::WorkingPixel`, and premultiplied/linear values are NEVER surfaced (D10 :279).
//
// Lives in `commands` because `commands` is already arbc-aware and already the editor's
// color-conversion site (the filled-background composite in `render.cpp` and the contact
// sheet both call these same libarbc primitives) — so this adds no component and no new
// levelization edge (D-color-2 / Constraint 8). The conversion pair is a THIN wrapper over
// the library transfer helpers (`arbc/media/pixel_traits.hpp`); there is no hand-rolled
// transfer function here.

#include <arbc/base/transform.hpp>     // arbc::Affine (the eyedropper's view camera)
#include <arbc/media/pixel_traits.hpp> // arbc::WorkingPixel + the transfer/premultiply helpers

#include <cstdint>

namespace arbc {
class Document;
} // namespace arbc

namespace ace::commands {

// The canonical user-space paint color (D10 / D-color-2): sRGB, 8-bit, straight-alpha RGBA —
// the native v1 precision (a hex / 0–255 picker over the sRGB gamut). The stored truth; the
// premultiplied-linear working value is DERIVED at the boundary and never stored. Defaults to
// opaque black `{0,0,0,255}` so the derived working pixel is `{0,0,0,1}`, byte-identical to the
// brush's shipped placeholder (Constraint 3).
struct SrgbColor {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;

  friend constexpr bool operator==(const SrgbColor&, const SrgbColor&) = default;
};

// Decode straight-alpha sRGB8 → premultiplied linear-light `arbc::WorkingPixel` (D-color-2).
// Wraps `arbc::srgb8_to_linear` (colour) + `arbc::unorm8_decode` (alpha) + `arbc::premultiply`,
// the EXACT primitives the filled-background composite already calls (`render.cpp:64-71`) — so
// the result is byte-identical to the library's own `Rgba8Srgb` codec, not a re-derivation. The
// result is premultiplied (each rgb ≤ a).
arbc::WorkingPixel srgb_to_working(const SrgbColor& color);

// The exact inverse of `srgb_to_working` (`arbc::unpremultiply` + `arbc::linear_to_srgb8` +
// `arbc::unorm8_encode`): premultiplied-linear → straight-alpha sRGB8, for the eyedropper's
// decode of a composited sample back to the user-space value it sets. Round-trips every
// representable 8-bit color exactly with `srgb_to_working`.
SrgbColor working_to_srgb(const arbc::WorkingPixel& working);

// Sample the COMPOSITED result at device point `(device_x, device_y)` as seen through `camera`
// (composition units → device pixels), returning the premultiplied-linear `arbc::WorkingPixel`
// the user sees there (D10 :287-290 / D-color-4). This is a RENDER through the camera, not a
// framebuffer lookup: it translates `camera` so the sampled device point lands at output pixel
// (0,0) and renders exactly that one pixel via `arbc::render_offline` over a `CpuBackend`, then
// decodes the composition's working-space pixel (whatever format it configured) through the
// library codec. An integer device translation reproduces the full-frame pixel byte-for-byte
// (the CPU backend's integer-aligned composite), so the 1×1 render equals the composited pixel
// at the click point — and it is byte-exact / headless-testable (the golden-in-a-value form).
// A point outside all coverage samples transparent `{0,0,0,0}`; a defensive render failure
// (an unstorable working space) also samples transparent rather than throwing.
arbc::WorkingPixel sample_composited_color(const arbc::Document& document,
                                           const arbc::Affine& camera, double device_x,
                                           double device_y);

} // namespace ace::commands
