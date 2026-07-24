#pragma once

// The editor's shared pixel-buffer value type (docs/01-architecture.md A20,
// D-export-2). It lived in L2 `render` until `editor.cameras.export` needed to name
// the image the renderer produces from L1 `commands` — and `commands -> render` is
// not merely undeclared but LEVEL-INVERTING (`render` is L2). It names no libarbc
// type, no GL type and no ImGui type, which is exactly `base`'s charter (§8 "value
// types"), so it moves down rather than being duplicated: one definition shared by
// the component that FILLS the buffer and the component that ENCODES it, instead of
// two that can silently disagree on stride, alpha convention or channel order —
// the class of bug a byte-exact golden exists to catch, made undetectable by
// construction. `ace::render::Srgb8Image` remains as an alias, so every shipped call
// site compiles unchanged.

#include <cstdint>
#include <vector>

namespace ace::base {

// A tightly-packed, straight-alpha sRGB8 RGBA image (w*h*4 bytes, no stride):
// the display/golden/export format (libarbc's `k_fast_rgba8srgb`). Directly
// GL-uploadable as RGBA8 and directly encodable as an 8-bit RGBA PNG.
struct Srgb8Image {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> pixels;
};

// One straight-alpha sRGB8 RGBA colour — the export panel's filled background
// (D14's "transparent/filled bg" knob). A value in the SAME encoding as
// `Srgb8Image`'s samples, so the two never disagree; the linearization into the
// working space is `render`'s job, through libarbc's own transfer helpers
// (D-export-5). `commands` carries it only as an opaque option.
struct Rgba8 {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;

  friend bool operator==(const Rgba8&, const Rgba8&) = default;
};

} // namespace ace::base
