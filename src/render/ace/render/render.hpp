#pragma once

#include <cstdint>
#include <vector>

namespace ace::render {

// The render component. See docs/01-architecture.md §8 (component levelization).
const char* name();

// A tightly-packed, straight-alpha sRGB8 RGBA image (w*h*4 bytes, no stride):
// the display/golden format (k_fast_rgba8srgb). Directly GL-uploadable as RGBA8.
struct Srgb8Image {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> pixels;
};

// Offline-render the probe document to straight-alpha sRGB8 (A6 display tail,
// D10): render_offline into the composition's working space, then let libarbc's
// CpuBackend::convert do the un-premultiply + sRGB encode (never hand-rolled).
// GL-FREE (D-render_probe-5): issues no GL, so the byte-exact golden runs on the
// plain headless lane. Returns an empty image on the (defensive) error path.
Srgb8Image render_probe_srgb8(int width, int height);

} // namespace ace::render
