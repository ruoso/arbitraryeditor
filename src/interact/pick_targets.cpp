#include <ace/interact/pick.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <vector>

// The ONE assembly adapter (A17 (b)). This translation unit is the SOLE `interact -> scene`
// include in the component: the policy core in pick.cpp names no `scene` type, so the
// primitive-only convention A16 established for the geometry helpers (`place_in_view`,
// `hit_frame`, `recrop_frame`, …) is untouched. The `interact -> scene` edge itself is already
// declared in the §8 DAG (`docs/01-architecture.md:210`, `scripts/check_levels.py`); it has
// simply never been exercised until now, so no checker edit is required.

namespace ace::interact {

std::vector<PickTarget> pick_targets(const arbc::Document& document,
                                     const arbc::Registry& registry) {
  std::vector<PickTarget> targets;
  // Cells first, in layer (z) order bottom-to-top, each carrying the extent read off the SAME
  // pinned generation as its placement (D-selection-11). A cell whose kind token does not
  // resolve is still a target — `cells()` reports it with an empty `kind_id` rather than
  // dropping it, and an unknown-passthrough cell is still selectable (D-cells_model-8).
  const std::vector<scene::Cell> cells = scene::cells(document, registry);
  const std::vector<scene::Camera> cameras = scene::cameras(document);
  targets.reserve(cells.size() + cameras.size());
  for (const scene::Cell& cell : cells) {
    targets.push_back(
        PickTarget{cell.id, cell.layer, PickKind::Cell, cell.placement, cell.content_bounds});
  }
  // Cameras appended ABOVE every cell (D-selection-6). A camera's pickable extent is its OUTPUT
  // rectangle, NOT `Content::bounds()` — a camera contributes zero pixels by design (A14), so
  // its bounds are deliberately empty while the thing you grab is its frame outline.
  for (const scene::Camera& camera : cameras) {
    const arbc::Rect output{0.0, 0.0, static_cast<double>(camera.resolution.width),
                            static_cast<double>(camera.resolution.height)};
    targets.push_back(PickTarget{camera.id, camera.layer, PickKind::Camera, camera.frame, output});
  }
  return targets;
}

} // namespace ace::interact
