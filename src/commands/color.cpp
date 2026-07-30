#include <ace/commands/color.hpp>

#include <arbc/backend_cpu/cpu_backend.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/compositor/compositor.hpp> // arbc::Viewport
#include <arbc/media/pixel_traits.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/offline.hpp>
#include <arbc/surface/surface.hpp>
#include <arbc/surface/typed_span.hpp> // arbc::visit_surface (format-generic decode)

#include <memory>

namespace ace::commands {

arbc::WorkingPixel srgb_to_working(const SrgbColor& color) {
  // The library owns the transfer (D-color-2): sRGB8 → linear for the colour channels, a plain
  // unorm decode for alpha, then libarbc's one tested `premultiply` — the exact three-step the
  // filled-background composite uses (`render.cpp:64-71`), so this is byte-identical to the
  // `Rgba8Srgb` codec, never a hand-rolled EOTF.
  const arbc::WorkingPixel straight{arbc::srgb8_to_linear(color.r), arbc::srgb8_to_linear(color.g),
                                    arbc::srgb8_to_linear(color.b), arbc::unorm8_decode(color.a)};
  return arbc::premultiply(straight);
}

SrgbColor working_to_srgb(const arbc::WorkingPixel& working) {
  // The exact inverse: unpremultiply, then encode the colour channels with the sRGB OETF and the
  // alpha with the plain unorm quantizer (the `Rgba8Srgb` encode, `pixel_traits.hpp:205-211`).
  const arbc::WorkingPixel straight = arbc::unpremultiply(working);
  return SrgbColor{arbc::linear_to_srgb8(straight[0]), arbc::linear_to_srgb8(straight[1]),
                   arbc::linear_to_srgb8(straight[2]), arbc::unorm8_encode(straight[3])};
}

arbc::WorkingPixel sample_composited_color(const arbc::Document& document,
                                           const arbc::Affine& camera, double device_x,
                                           double device_y) {
  // Translate the viewport camera so the sampled device point lands at output pixel (0,0), then
  // render exactly that one pixel (D10 :287-290 — a render through a camera, not a lookup). An
  // integer device translation reproduces the full-frame pixel byte-for-byte (the CPU backend's
  // integer-aligned composite, `render.cpp`), so this 1×1 render equals the composited result the
  // user sees at the click point. `render_offline` sources the root composition when the viewport
  // carries no anchor, exactly as `render_document_srgb8` relies on.
  const arbc::Affine shifted =
      arbc::compose(arbc::Affine::translation(-device_x, -device_y), camera);
  arbc::CpuBackend backend;
  const arbc::Viewport viewport{1, 1, shifted};
  const arbc::expected<std::unique_ptr<arbc::Surface>, arbc::SurfaceError> frame =
      arbc::render_offline(document, viewport, backend);
  if (!frame.has_value()) {
    return {0.0F, 0.0F, 0.0F, 0.0F}; // defensive: an unstorable working space samples transparent
  }
  // Decode the single composited pixel from whatever working format the composition configured
  // (rgba16f by default) into a premultiplied-linear `WorkingPixel` — the library owns the codec,
  // so no format assumption leaks here.
  return arbc::visit_surface(**frame, [](auto typed) -> arbc::WorkingPixel {
    using Traits = arbc::PixelTraits<decltype(typed)::format>;
    if (typed.data.size() < Traits::channels) {
      return {0.0F, 0.0F, 0.0F, 0.0F}; // no CPU readback (defensive): transparent
    }
    return Traits::decode(typed.data.data());
  });
}

} // namespace ace::commands
