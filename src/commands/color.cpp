#include <ace/commands/color.hpp>

#include <arbc/backend_cpu/cpu_backend.hpp>
#include <arbc/base/geometry.hpp> // arbc::Rect (the 1×1 device clip)
#include <arbc/base/ids.hpp>      // arbc::ObjectId (the isolated sampler's layer target)
#include <arbc/base/transform.hpp>
#include <arbc/compositor/compositor.hpp>     // arbc::Viewport / render_layer / ContentResolver
#include <arbc/compositor/operator_graph.hpp> // arbc::is_operator (the leaf/operator gate)
#include <arbc/contract/content.hpp>          // arbc::Content::composition_ref()
#include <arbc/media/pixel_traits.hpp>
#include <arbc/model/model.hpp>   // arbc::DocStatePtr::find_layer / working_space
#include <arbc/model/records.hpp> // arbc::LayerRecord
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/offline.hpp>
#include <arbc/surface/surface.hpp>
#include <arbc/surface/surface_pool.hpp> // arbc::SurfacePool (render_layer's temp source)
#include <arbc/surface/typed_span.hpp>   // arbc::visit_surface (format-generic decode)

#include <memory>
#include <optional>

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

std::optional<arbc::WorkingPixel> sample_cell_color(const arbc::Document& document,
                                                    const arbc::Affine& camera,
                                                    arbc::ObjectId layer, double device_x,
                                                    double device_y) {
  // The active-cell modifier (D10 :477 / §7:290 / D-eyedrop_cell-1): the selected cell's OWN
  // straight colour, unmixed with any sibling, is a single-layer render through the active camera —
  // the twin of `sample_composited_color`, differing only in the render call (`render_layer` of one
  // layer instead of the whole-document `render_offline`) and the leaf/operator gate. A pure PINNED
  // read: no document mutation, no transaction, no writer thread — the pin, the layer record, its
  // content and the working space all come off ONE published generation.
  const arbc::DocStatePtr state = document.pin();
  if (!state) {
    return std::nullopt;
  }
  const arbc::LayerRecord* record = state->find_layer(layer);
  if (record == nullptr) {
    return std::nullopt; // empty selection, a camera, or an id that names no placed layer
  }
  // The gate (D-eyedrop_cell-2 / D-eyedrop_nested-3): a null resolve, or a TRUE operator —
  // non-empty `inputs()` with NO composition_ref, the library's `fade`/`crossfade`/`tone` — that a
  // bare `render_layer` stands up no pull service for, degrades to the composited sample. A nested
  // composition ALSO reports non-empty `inputs()` once bound to the document's pull service (its
  // child is an input), so `is_operator` is true for it too; the `!composition_ref().valid()`
  // clause carves the nested case OUT of this gate and routes it to the anchored `render_offline`
  // branch below — otherwise a bound nested cell would be misclassified as an operator and refused.
  // Still the library's structural predicate, still NOT a kind allow-list (respects
  // D-cells_model-8).
  arbc::Content* content = document.resolve(record->content);
  if (content == nullptr || (arbc::is_operator(content) && !content->composition_ref().valid())) {
    return std::nullopt;
  }
  // Translate the camera so the sampled device point lands at output pixel (0,0), then compose with
  // the layer's placement to get the content→device transform the render expects — the exact
  // 1×1-shift trick `sample_composited_color` uses, extended by the placement compose (Constraint
  // 2/4). Shared by the nested-branch anchored render and the leaf `render_layer` path.
  const arbc::Affine shifted =
      arbc::compose(arbc::Affine::translation(-device_x, -device_y), camera);
  const arbc::Affine composed = arbc::compose(shifted, record->transform);
  arbc::CpuBackend backend;
  // The nested-composition branch (D-eyedrop_nested-1/-2/-4, Constraints 1/2/4): a content
  // answering a valid `composition_ref()` is a nested composition (it passed the gate above via the
  // carve-out). Render the child composition IN ISOLATION from the parent's siblings by anchoring
  // the offline viewport at that child composition id: `Viewport::anchor` scopes the frame walk to
  // exactly that composition's own members (`compositor.hpp:20-27`), and `render_offline` stands up
  // the full pull service + operator binding (`offline.cpp:50-52`) so nested-within-nested
  // resolves. A PURE PINNED read — the caller-pinned overload renders against the SAME generation
  // the gate inspected (Constraint 2), single-threaded via `direct_dispatch()`, so it never enters
  // the interactive worker pool. The result is the child's own composited straight colour — the
  // cell's own colour, not the parent-placement-attenuated pixel (D-eyedrop_nested-4). A
  // null/unstorable frame returns std::nullopt, so the arm degrades to the composited sample
  // exactly as the leaf error path does.
  if (const arbc::ObjectId child = content->composition_ref(); child.valid()) {
    const arbc::Viewport viewport{1, 1, composed, child};
    const arbc::expected<std::unique_ptr<arbc::Surface>, arbc::SurfaceError> frame =
        arbc::render_offline(document, state, viewport, backend);
    if (!frame.has_value()) {
      return std::nullopt; // an unstorable working space or null pin (defensive)
    }
    return arbc::visit_surface(**frame, [](auto typed) -> arbc::WorkingPixel {
      using Traits = arbc::PixelTraits<decltype(typed)::format>;
      if (typed.data.size() < Traits::channels) {
        return arbc::WorkingPixel{0.0F, 0.0F, 0.0F,
                                  0.0F}; // no CPU readback (defensive): transparent
      }
      return Traits::decode(typed.data.data());
    });
  }
  // The leaf path (unchanged): render exactly this one layer source-over into a 1×1 target.
  const arbc::expected<std::unique_ptr<arbc::Surface>, arbc::SurfaceError> target =
      backend.make_surface(1, 1, state->working_space());
  if (!target.has_value()) {
    return std::nullopt; // an unstorable working space: no isolated sample (defensive)
  }
  // `render_layer` composites the layer source-over into the target, so start from transparent —
  // a culled layer (degenerate/out-of-bounds/sub-pixel) then reads as `{0,0,0,0}`, not residue.
  backend.clear(**target, 0.0F, 0.0F, 0.0F, 0.0F);
  arbc::SurfacePool pool(backend);
  const arbc::ContentResolver resolve = [&document](arbc::ObjectId id) {
    return document.resolve(id);
  };
  const arbc::Rect device_rect{0.0, 0.0, 1.0, 1.0};
  arbc::render_layer(resolve, *record, composed, device_rect, backend, pool, **target);
  // Decode the single premultiplied-linear working pixel through the library codec (whatever format
  // the composition configured), exactly as the composited sampler does.
  return arbc::visit_surface(**target, [](auto typed) -> arbc::WorkingPixel {
    using Traits = arbc::PixelTraits<decltype(typed)::format>;
    if (typed.data.size() < Traits::channels) {
      return arbc::WorkingPixel{0.0F, 0.0F, 0.0F, 0.0F}; // no CPU readback (defensive): transparent
    }
    return Traits::decode(typed.data.data());
  });
}

} // namespace ace::commands
