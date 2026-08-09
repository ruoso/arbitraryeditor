// The isolation-scope dim composite (editor.canvas.isolation_scope / D-isolation_scope-1).
//
// Float contraction is disabled for the whole TU: the per-pixel inside/outside decision feeds a
// byte-exact render_offline golden, so the quad-mapping and edge-cross arithmetic must be
// bit-identical across the gcc/clang × debug/release/asan/tsan matrix that shares one golden.
// `-ffp-contract` defaults DIFFER between gcc and clang, so a bare `a*x + c*y` could fuse to an FMA
// on one and not the other and flip a boundary pixel. OFF pins +,-,* to correctly-rounded IEEE —
// the same discipline scene/focus_quad.cpp and the TrueType caption goldens rely on.
#pragma STDC FP_CONTRACT OFF

#include "dim_scrim.hpp"

#include <arbc/backend_cpu/cpu_backend.hpp>
#include <arbc/base/geometry.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/media/blend_mode.hpp> // arbc::BlendMode::Normal (v0.6.0 required composite arg)
#include <arbc/media/pixel_format.hpp>
#include <arbc/surface/surface.hpp>

#include <array>
#include <cstddef>
#include <span>

namespace ace::render {
namespace {

// True iff the device-space point (px,py) lies inside (or on an edge of) the convex quad `q`,
// winding-agnostic: the four edge cross-products must all share a sign, with zeros (a point exactly
// on an edge) counted as inside. Only +,-,* (FP_CONTRACT OFF at the TU head), so the decision is
// bit-identical across the CI compiler/sanitizer matrix.
bool point_in_quad(const std::array<arbc::Vec2, 4>& q, double px, double py) {
  int sign = 0;
  for (int i = 0; i < 4; ++i) {
    const arbc::Vec2 a = q[static_cast<std::size_t>(i)];
    const arbc::Vec2 b = q[static_cast<std::size_t>((i + 1) & 3)];
    const double cross = (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
    if (cross < 0.0) {
      if (sign > 0) {
        return false;
      }
      sign = -1;
    } else if (cross > 0.0) {
      if (sign < 0) {
        return false;
      }
      sign = 1;
    }
  }
  return true;
}

} // namespace

void composite_isolation_dim(arbc::CpuBackend& backend, arbc::Surface& frame,
                             const scene::FocusQuad& focus_comp, const arbc::Affine& camera) {
  const int w = frame.width();
  const int h = frame.height();
  if (w <= 0 || h <= 0) {
    return;
  }
  // Project the focus quad's composition-space corners to device pixels once.
  std::array<arbc::Vec2, 4> dev{};
  for (std::size_t i = 0; i < dev.size(); ++i) {
    dev[i] = camera.apply(focus_comp.corners[i]);
  }
  // A working-space scrim the size of the frame; make_surface zero-inits => transparent black.
  const auto scrim = backend.make_surface(w, h, frame.format());
  if (!scrim.has_value()) {
    return;
  }
  const std::span<float> px = (*scrim)->span<arbc::PixelFormat::Rgba32fLinearPremul>();
  if (px.empty()) {
    return; // not the 32f working format (e.g. rgba16f) — skip rather than misinterpret bytes.
  }
  // Premultiplied neutral dim = black at coverage alpha => {0,0,0,a} OUTSIDE the quad, transparent
  // {0,0,0,0} INSIDE. Sampled at pixel CENTERS (x+0.5, y+0.5).
  const float a = k_isolation_dim_alpha;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const std::size_t base = 4 * (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                    static_cast<std::size_t>(x));
      const bool inside =
          point_in_quad(dev, static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5);
      px[base + 0] = 0.0F;
      px[base + 1] = 0.0F;
      px[base + 2] = 0.0F;
      px[base + 3] = inside ? 0.0F : a;
    }
  }
  // Source-over the scrim onto the frame at identity (integer-aligned => the Catmull-Rom tap
  // collapses to the incumbent texel, no drift): outside pixels become frame*(1-a), the child
  // region is unchanged. The frame stays in the linear premultiplied working space; the sRGB8
  // encode is a later CpuBackend::convert step (D10).
  backend.composite(frame, **scrim, arbc::Affine::identity(), 1.0, arbc::BlendMode::Normal);
}

} // namespace ace::render
