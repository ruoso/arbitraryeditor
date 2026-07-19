#pragma once

#include <arbc/base/transform.hpp>

#include <cstdint>
#include <vector>

namespace arbc {
class Document;
} // namespace arbc

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

// Offline-render an arbitrary libarbc document to straight-alpha sRGB8 (A6 display
// tail, D10): render_offline into the composition's working space, then let
// libarbc's CpuBackend::convert do the un-premultiply + sRGB encode (never
// hand-rolled). GL-FREE (D-render_probe-5): issues no GL, so the byte-exact golden
// runs on the plain headless lane. Returns an empty image on the (defensive) error
// path. Reused by editor.project.open's load-fidelity golden to prove a reopened
// document reconstructs pixel-identically.
//
// `camera` (composition units -> device pixels) frames the render; it defaults to
// identity, the pre-nav framing. editor.canvas.nav passes a non-identity camera so
// the interactive-camera golden cross-checks byte-identically against the hosted
// entry driven through the same Affine (D-nav-3).
Srgb8Image render_document_srgb8(const arbc::Document& document, int width, int height,
                                 const arbc::Affine& camera = arbc::Affine::identity());

// The probe convenience: build the probe document (ace::project) and render it.
Srgb8Image render_probe_srgb8(int width, int height);

// True iff `frame` carries composited coverage — any straight-alpha byte (the `+3`
// of each tightly-packed RGBA quad) is non-zero. A freshly-made working-space target
// is transparent black (arbc CpuBackend) and the compositor composites content over
// it, so a frame that composited NO tile is all-transparent (every alpha byte 0) and
// any composited coverage yields >=1 non-zero alpha byte — the exact "did any tile
// composite" invariant. It keys on coverage, not colour, so opaque-black content
// (0,0,0,255) trips it and a background colour collision cannot. The canvas drivers
// gate the first publish per size on this so the published sequence never advances on
// a blank first frame (editor.canvas.blank_first_frame, D-blank_first_frame-1). Pure,
// GL-free; an empty image is content-free.
bool frame_has_content(const Srgb8Image& frame);

} // namespace ace::render
