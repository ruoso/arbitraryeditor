#include <ace/interact/pick.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/ids.hpp>

#include <optional>
#include <vector>

// The ONE assembly adapter (A17 (b)). This translation unit is the SOLE `interact -> scene`
// include in the component: the policy core in pick.cpp names no `scene` type, so the
// primitive-only convention A16 established for the geometry helpers (`place_in_view`,
// `hit_frame`, `recrop_frame`, …) is untouched. The `interact -> scene` edge itself is already
// declared in the §8 DAG (`docs/01-architecture.md:210`, `scripts/check_levels.py`); it has
// simply never been exercised until now, so no checker edit is required.

namespace ace::interact {

std::vector<PickTarget> pick_targets(const arbc::Document& document, const arbc::Registry& registry,
                                     std::optional<arbc::ObjectId> entered) {
  std::vector<PickTarget> targets;
  // Resolve the raw scope through the shared fail-safe (D-scoped_edit-4/-6): a `nullopt` scope,
  // or one naming a GC'd/undone-away/foreign id, degrades to Root, so the confinement below can
  // never strand the user in an un-editable phantom composition (Constraint 2).
  const arbc::ObjectId active = scene::active_composition(document, entered);
  const arbc::ObjectId root = scene::active_composition(document, std::nullopt);
  const bool entered_scope = active.valid() && active != root;
  // Cells first, in layer (z) order bottom-to-top, each carrying the extent read off the SAME
  // pinned generation as its placement (D-selection-11). A cell whose kind token does not
  // resolve is still a target — `cells()` reports it with an empty `kind_id` rather than
  // dropping it, and an unknown-passthrough cell is still selectable (D-cells_model-8). While
  // entered, the walk is re-rooted to the ACTIVE composition, so only in-scope cells are
  // pickable (Constraint 7) — the same set the delete resolver now sees, so selection and
  // deletion agree.
  const std::vector<scene::Cell> cells = scene::cells(document, registry, active);
  targets.reserve(cells.size());
  for (const scene::Cell& cell : cells) {
    targets.push_back(
        PickTarget{cell.id, cell.layer, PickKind::Cell, cell.placement, cell.content_bounds});
  }
  // Cameras appended ABOVE every cell (D-selection-6) — but ONLY at Root. A camera is a
  // project-level viewport tool (A14/D19), not a member of a nested composition, so while
  // entered it is dimmed by the scrim and `remove_cells` would skip it; leaving it pickable
  // would be a "select-but-can't-act" contradiction of the visual lock (Constraint 8 /
  // D-scoped_edit-4). A camera's pickable extent is its OUTPUT rectangle, NOT `Content::bounds()`
  // — a camera contributes zero pixels by design (A14), so its bounds are deliberately empty
  // while the thing you grab is its frame outline.
  if (!entered_scope) {
    const std::vector<scene::Camera> cameras = scene::cameras(document);
    targets.reserve(targets.size() + cameras.size());
    for (const scene::Camera& camera : cameras) {
      const arbc::Rect output{0.0, 0.0, static_cast<double>(camera.resolution.width),
                              static_cast<double>(camera.resolution.height)};
      targets.push_back(
          PickTarget{camera.id, camera.layer, PickKind::Camera, camera.frame, output});
    }
  }
  return targets;
}

} // namespace ace::interact
