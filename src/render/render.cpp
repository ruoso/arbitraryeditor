#include <ace/gl/gl.hpp>
#include <ace/project/project.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/scene.hpp>

#include <arbc/backend_cpu/cpu_backend.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/compositor/compositor.hpp>
#include <arbc/media/pixel_format.hpp>
#include <arbc/media/pixel_traits.hpp>
#include <arbc/media/surface_format.hpp>
#include <arbc/runtime/offline.hpp>
#include <arbc/surface/surface.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ace::render {

const char* name() { return "render"; }

Srgb8Image render_document_srgb8(const arbc::Document& document, int width, int height,
                                 const arbc::Affine& camera) {
  arbc::CpuBackend backend;
  const arbc::Viewport viewport{width, height, camera};
  // render_offline sources the root composition as the anchor and returns the
  // frame in the composition's working space (premultiplied linear rgba32f).
  const auto frame = arbc::render_offline(document, viewport, backend);
  // A straight-alpha sRGB8 target; convert() does the un-premultiply + gamma
  // encode the library owns (D-render_probe-4), leaving the editor the
  // invisible translator (D10) rather than an owner of colour math.
  const auto srgb = backend.make_surface(width, height, arbc::k_fast_rgba8srgb);

  Srgb8Image image;
  if (frame.has_value() && srgb.has_value()) {
    backend.convert(**srgb, **frame);
    const std::span<const std::uint8_t> bytes = (*srgb)->span<arbc::PixelFormat::Rgba8Srgb>();
    image.width = width;
    image.height = height;
    image.pixels.assign(bytes.begin(), bytes.end());
  }
  return image;
}

Srgb8Image render_document_srgb8_over(const arbc::Document& document, int width, int height,
                                      const arbc::Affine& camera,
                                      std::array<std::uint8_t, 4> background) {
  arbc::CpuBackend backend;
  const arbc::Viewport viewport{width, height, camera};
  const auto frame = arbc::render_offline(document, viewport, backend);
  const auto srgb = backend.make_surface(width, height, arbc::k_fast_rgba8srgb);

  Srgb8Image image;
  if (!frame.has_value() || !srgb.has_value()) {
    return image;
  }
  // A second surface in the SAME working space the frame came back in (whatever the
  // composition configured), so the background fill and the source-over blend both
  // happen in linear premultiplied floats — never over the encoded sRGB8 bytes
  // (D10 / D-export-5).
  const auto filled = backend.make_surface(width, height, (*frame)->format());
  if (!filled.has_value()) {
    return image;
  }
  // The library owns the transfer: sRGB8 -> linear for the colour channels, a plain
  // unorm decode for alpha, then libarbc's one tested `premultiply` — because
  // `Backend::clear` takes a PREMULTIPLIED working-space sample.
  const arbc::WorkingPixel straight{
      arbc::srgb8_to_linear(background[0]), arbc::srgb8_to_linear(background[1]),
      arbc::srgb8_to_linear(background[2]), arbc::unorm8_decode(background[3])};
  const arbc::WorkingPixel premultiplied = arbc::premultiply(straight);
  backend.clear(**filled, premultiplied[0], premultiplied[1], premultiplied[2], premultiplied[3]);
  // Source-over at identity: the CPU backend's Catmull-Rom tap is interpolating, so an
  // integer-aligned composite reproduces the incumbent texel byte-for-byte — this adds
  // a blend, not a resample.
  backend.composite(**filled, **frame, arbc::Affine::identity(), 1.0);

  backend.convert(**srgb, **filled);
  const std::span<const std::uint8_t> bytes = (*srgb)->span<arbc::PixelFormat::Rgba8Srgb>();
  image.width = width;
  image.height = height;
  image.pixels.assign(bytes.begin(), bytes.end());
  return image;
}

Srgb8Image render_probe_srgb8(int width, int height) {
  const project::ProbeDocument probe = project::build_probe_document();
  return render_document_srgb8(*probe.document, width, height);
}

bool frame_has_content(const Srgb8Image& frame) {
  // Straight-alpha coverage: alpha lives at byte +3 of each tightly-packed RGBA quad
  // (render.hpp). Any non-zero alpha byte means a tile composited over the
  // transparent-black target (D-blank_first_frame-1).
  for (std::size_t i = 3; i < frame.pixels.size(); i += 4) {
    if (frame.pixels[i] != 0) {
      return true;
    }
  }
  return false;
}

} // namespace ace::render
