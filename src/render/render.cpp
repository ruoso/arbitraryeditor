#include <ace/gl/gl.hpp>
#include <ace/project/project.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/scene.hpp>

#include <arbc/backend_cpu/cpu_backend.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/compositor/compositor.hpp>
#include <arbc/media/pixel_format.hpp>
#include <arbc/media/surface_format.hpp>
#include <arbc/runtime/offline.hpp>
#include <arbc/surface/surface.hpp>

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
