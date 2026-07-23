#include <ace/commands/cells.hpp>
#include <ace/scene/camera.hpp> // scene::cameras — a camera is deletable by the same verb (A14/D7)

#include <arbc/base/expected.hpp>
#include <arbc/runtime/document.hpp>

#include <utility>
#include <vector>

namespace ace::commands {

Command insert_cell_command(const arbc::Registry& registry, std::string kind_id, std::string config,
                            const arbc::Affine& placement, InsertCellOutcome& outcome) {
  outcome = InsertCellOutcome{};
  return Command{"insert_cell", [&registry, &outcome, kind_id = std::move(kind_id),
                                 config = std::move(config), placement](arbc::Document& doc) {
                   const arbc::expected<arbc::ObjectId, std::string> added =
                       scene::add_cell(doc, registry, kind_id, config, placement);
                   if (added) {
                     outcome.content = *added;
                   } else {
                     outcome.error = added.error();
                   }
                 }};
}

std::vector<Removal> selected_removals(const arbc::Document& document,
                                       const arbc::Registry& registry, const Selection& selection) {
  std::vector<Removal> removals;
  if (selection.empty()) {
    return removals;
  }
  // Both accessors read the document, never `interact::pick_targets` (D-cells_remove-3).
  // The two lists are disjoint by construction — `cells()` excludes `org.arbc.camera`
  // layers (A14) — so trying cells first costs no correctness.
  const std::vector<scene::Cell> cells = scene::cells(document, registry);
  const std::vector<scene::Camera> cameras = scene::cameras(document);
  removals.reserve(selection.items().size());
  for (const arbc::ObjectId id : selection.items()) {
    arbc::ObjectId layer;
    for (const scene::Cell& cell : cells) {
      if (cell.id == id) {
        layer = cell.layer;
        break;
      }
    }
    if (!layer.valid()) {
      for (const scene::Camera& camera : cameras) {
        if (camera.id == id) {
          layer = camera.layer;
          break;
        }
      }
    }
    if (layer.valid()) {
      removals.push_back(Removal{id, layer});
    }
    // else: a selected id with no live target — skipped, not an error (Constraint 5).
  }
  return removals;
}

Command remove_cell_command(arbc::ObjectId content, arbc::ObjectId layer, bool& removed) {
  removed = false;
  return Command{"remove_cell", [content, layer, &removed](arbc::Document& doc) {
                   removed = scene::remove_cell(doc, content, layer);
                 }};
}

DeleteOutcome delete_selection(AppState& state) {
  DeleteOutcome outcome;
  // Resolved HERE, inside the edit, against the live document (D-cells_remove-3).
  const std::vector<Removal> removals =
      selected_removals(state.document(), state.registry(), state.selection());
  for (const Removal& removal : removals) {
    bool removed = false;
    const Command command = remove_cell_command(removal.content, removal.layer, removed);
    const DispatchOutcome dispatched = dispatch(state, command);
    if (removed) {
      ++outcome.removed;
    }
    outcome.journal_entries_added += dispatched.journal_entries_added;
  }
  // The honest post-condition (D-cells_remove-7): the objects are gone, so no selected id
  // can survive. Unconditional — clearing an already-empty selection is itself a no-op.
  state.selection().clear();
  return outcome;
}

bool can_delete(const AppState& state) { return !state.selection().empty(); }

} // namespace ace::commands
