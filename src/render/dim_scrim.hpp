#pragma once

// The isolation-scope dim composite (editor.canvas.isolation_scope / D-isolation_scope-1),
// render-private (NOT under ace/render/): shared by render.cpp's offline scoped entry point and
// canvas_renderer.cpp's interactive path so both bake one identical dim into the CpuBackend frame.
#include <ace/scene/cell.hpp> // scene::FocusQuad — render -> scene, an already-declared L2->L1 edge (§8)

namespace arbc {
class CpuBackend;
class Surface;
struct Affine;
} // namespace arbc

namespace ace::render {

// The neutral isolation dim strength (Constraint 4): the coverage of the premultiplied-black scrim
// laid over everything OUTSIDE the entered composition's focus quad, in the `k_grid_line_cap`
// named-constant spirit. Outside pixels keep (1 - alpha) of their linear working value; the child
// region is left bright. A design tweak to the exact strength is a one-line change pinned by the
// `isolation_scope_dim_64x64` golden, not a re-refinement.
inline constexpr float k_isolation_dim_alpha = 0.6F;

// Composite the isolation dim over `frame` (a working-space surface) IN PLACE: dim everything
// OUTSIDE `focus_comp` — a quad in composition/root space — mapped to device pixels through
// `camera` (composition units -> device pixels), leaving the quad interior untouched. D10-clean: a
// premultiplied working-space scrim source-over-composited via `CpuBackend::composite`, NEVER an
// alpha-blend over the packed sRGB8 display bytes. A rotated / sheared placement yields a true
// quad, so the dimmed complement follows the real edges, not an AABB. No-op (leaves `frame`
// untouched) when the working format is not Rgba32fLinearPremul or the scrim allocation fails.
void composite_isolation_dim(arbc::CpuBackend& backend, arbc::Surface& frame,
                             const scene::FocusQuad& focus_comp, const arbc::Affine& camera);

} // namespace ace::render
