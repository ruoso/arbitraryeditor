#pragma once

#include <ace/base/image.hpp>

#include <arbc/base/transform.hpp>

#include <array>
#include <cstdint>

namespace arbc {
class Document;
} // namespace arbc

namespace ace::render {

// The render component. See docs/01-architecture.md §8 (component levelization).
const char* name();

// A tightly-packed, straight-alpha sRGB8 RGBA image (w*h*4 bytes, no stride):
// the display/golden format (k_fast_rgba8srgb). Directly GL-uploadable as RGBA8.
//
// The DEFINITION moved down to L0 `base` with editor.cameras.export (A20 /
// D-export-2) so the L1 export kernel can name the image this component produces
// without a level-inverting `commands -> render` edge; this alias keeps every
// shipped `ace::render::Srgb8Image` call site compiling unchanged.
using Srgb8Image = base::Srgb8Image;

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

// `render_document_srgb8` with a FILLED background composited underneath the frame
// (D14's transparent-vs-filled knob; D-export-5). `background` is one straight-alpha
// sRGB8 RGBA quad, in the same encoding the returned image uses.
//
// The composite happens in the LINEAR WORKING SPACE, never over the sRGB8 bytes: D10
// makes the editor the invisible translator, and `src*a + bg*(1-a)` on gamma-encoded
// samples is the classic dark-halo-on-every-antialiased-edge bug. So the background is
// linearized and premultiplied through libarbc's OWN transfer helpers
// (`arbc/media/pixel_traits.hpp`), laid down with `Backend::clear` (which takes a
// premultiplied working-space sample), the offline frame is source-over-composited on
// top with `Backend::composite` at identity — which the CPU backend's Catmull-Rom tap
// reproduces byte-for-byte at integer alignment — and only then does the same
// `CpuBackend::convert` tail encode to sRGB8. No colour math is hand-rolled here.
//
// A fully transparent `background` is NOT the same call as `render_document_srgb8`
// (it still round-trips through a second working-space surface); the export path
// therefore takes the plain function for its transparent default, which is why the
// common case adds no new rendering surface at all. Returns an empty image on the
// (defensive) error path, exactly as `render_document_srgb8` does.
Srgb8Image render_document_srgb8_over(const arbc::Document& document, int width, int height,
                                      const arbc::Affine& camera,
                                      std::array<std::uint8_t, 4> background);

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
