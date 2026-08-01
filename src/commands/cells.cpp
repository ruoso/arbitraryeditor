#include <ace/commands/cells.hpp>
#include <ace/scene/camera.hpp> // scene::cameras — a camera is deletable by the same verb (A14/D7)

#include <arbc/base/expected.hpp>
#include <arbc/runtime/document.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace ace::commands {

Command insert_cell_command(const arbc::Registry& registry, std::string kind_id, std::string config,
                            const arbc::Affine& placement, InsertCellOutcome& outcome,
                            std::optional<arbc::ObjectId> entered) {
  outcome = InsertCellOutcome{};
  // `entered` is captured BY VALUE (D-scoped_edit-2 / Constraint 3): the UI-thread scope
  // snapshot travels into the writer-thread closure, and `scene::add_cell` resolves it
  // fail-safe against the pin the create lands on — the writer never reads live `AppState`.
  return Command{"insert_cell",
                 [&registry, &outcome, kind_id = std::move(kind_id), config = std::move(config),
                  placement, entered](arbc::Document& doc) {
                   const arbc::expected<arbc::ObjectId, std::string> added =
                       scene::add_cell(doc, registry, kind_id, config, placement, entered);
                   if (added) {
                     outcome.content = *added;
                   } else {
                     outcome.error = added.error();
                   }
                 }};
}

Command insert_nested_reference_command(const arbc::Registry& registry, std::string ref_uri,
                                        const arbc::Affine& placement, InsertCellOutcome& outcome,
                                        std::optional<arbc::ObjectId> entered) {
  outcome = InsertCellOutcome{};
  // `ref_uri` + `entered` captured BY VALUE (D-scoped_edit-2 / Constraint 3): the UI-thread pick
  // travels into the writer-thread closure, and `scene::add_nested_reference` resolves the scope
  // fail-safe against the pin the create lands on — the writer never reads live `AppState`. The
  // DEDICATED seam, never the generic factory (D-nested-1): the built-in `make_nested` is
  // numeric-`ObjectId`-only and cannot mint an external `params.ref` reference.
  return Command{"insert_nested_reference", [&registry, &outcome, ref_uri = std::move(ref_uri),
                                             placement, entered](arbc::Document& doc) {
                   const arbc::expected<arbc::ObjectId, std::string> added =
                       scene::add_nested_reference(doc, registry, ref_uri, placement, entered);
                   if (added) {
                     outcome.content = *added;
                   } else {
                     outcome.error = added.error();
                   }
                 }};
}

std::vector<Removal> selected_removals(const arbc::Document& document,
                                       const arbc::Registry& registry, const Selection& selection,
                                       std::optional<arbc::ObjectId> entered) {
  std::vector<Removal> removals;
  if (selection.empty()) {
    return removals;
  }
  // Both accessors read the document, never `interact::pick_targets` (D-cells_remove-3).
  // The two lists are disjoint by construction — `cells()` excludes `org.arbc.camera`
  // layers (A14) — so trying cells first costs no correctness.
  //
  // The cell walk is re-rooted to the ACTIVE composition (D-scoped_edit-3 / Constraint 5): while
  // entered, an in-scope cell is NOT in the root list, so the shipped root-only walk would never
  // find its layer and the delete would silently no-op. The composition overload finds it; a
  // vanished scope degrades to Root through the same fail-safe (Constraint 2). Cameras stay the
  // project-level list — the pick confinement keeps their ids out of an entered selection, and
  // `remove_cells`' membership gate is the backstop for any that slip through.
  const std::vector<scene::Cell> cells =
      scene::cells(document, registry, scene::active_composition(document, entered));
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

Command remove_cells_command(std::vector<Removal> removals, std::size_t& removed,
                             std::optional<arbc::ObjectId> entered) {
  removed = 0;
  // `entered` captured BY VALUE (D-scoped_edit-2): the writer-thread closure holds an immutable
  // scope snapshot, exactly as it holds the removal list.
  return Command{"remove_cells",
                 [removals = std::move(removals), &removed, entered](arbc::Document& doc) {
                   // Marshal the editor's two-field removals into the scene-local batch type
                   // (`commands::Removal` stays composition-free, D-one_action_one_entry-3);
                   // `scene::remove_cells` resolves the ACTIVE composition once from `entered`
                   // and validates each pair against the live pin, skipping stale or
                   // out-of-scope targets (Constraint 5).
                   std::vector<scene::CellRemoval> targets;
                   targets.reserve(removals.size());
                   for (const Removal& removal : removals) {
                     targets.push_back(scene::CellRemoval{removal.content, removal.layer});
                   }
                   removed = scene::remove_cells(doc, targets, entered);
                 }};
}

Command transform_cells_command(std::vector<LayerTransform> placements) {
  return Command{
      "group_transform", [placements = std::move(placements)](arbc::Document& doc) {
        if (placements.empty()) {
          return; // nothing to place: open no transaction, add no entry (Constraint 5)
        }
        // Validate each member against the LIVE pin — a stale layer (undone/GC'd out
        // from under the selection) is skipped, exactly as `scene::remove_cells`
        // validates its removals (Constraint 5). This also makes the zero-live-layer
        // batch a true no-op: with nothing to touch we open NO transaction, so an
        // all-stale drag commits nothing rather than publishing an empty entry.
        const arbc::DocStatePtr pin = doc.pin();
        std::vector<const LayerTransform*> live;
        live.reserve(placements.size());
        for (const LayerTransform& placement : placements) {
          if (pin && pin->find_layer(placement.layer) != nullptr) {
            live.push_back(&placement);
          }
        }
        if (live.empty()) {
          return; // zero live layers: commit nothing (Constraint 5 / D-group_transform-1)
        }
        // ONE transaction for the whole batch: `commit()` assembles ONE journal entry
        // however many layers it touched (`model.hpp`), so this is natively one entry,
        // one revision bump, and one atomic publish (D-group_transform-1) — the same
        // batch atomicity `remove_cells_command` gets from one `scene::remove_cells`.
        auto txn = doc.transact("group_transform");
        for (const LayerTransform* placement : live) {
          txn.set_transform(placement->layer, placement->placement);
        }
        txn.commit();
      }};
}

Command reorder_cell_command(arbc::ObjectId composition, arbc::ObjectId moved,
                             std::uint32_t to_index) {
  // Captured by value; `scene::reorder_cell` resolves the cell's live `from_index` and no-ops on a
  // stale/camera/non-member target or an out-of-range/equal index (opening no transaction), so the
  // command needs no separate pre-validation — the one writer-thread verb owns all of it.
  return Command{"reorder_cell", [composition, moved, to_index](arbc::Document& doc) {
                   scene::reorder_cell(doc, composition, moved, to_index);
                 }};
}

DeleteOutcome delete_selection(AppState& state) {
  DeleteOutcome outcome;
  // The isolation scope is read HERE, beside `state.selection()` (D-scoped_edit-2 /
  // D-cells_remove-3): the same resolution point, same threading posture — a plain optional
  // snapshotted by value, then threaded into both the resolver and the command. While entered,
  // `selected_removals` finds in-scope layers and `remove_cells`' gate validates against the
  // entered composition, so the delete confines to the scope (Constraint 5).
  const std::optional<arbc::ObjectId> entered = state.entered_composition();
  std::vector<Removal> removals =
      selected_removals(state.document(), state.registry(), state.selection(), entered);
  // ONE command wrapping ONE `remove_contents` over the whole span (D-one_action_one_entry-2):
  // one journal entry, one undo press for N objects — one `Command` = one libarbc transaction
  // (remove Constraint 3, restored). An empty/wholly-stale batch adds no entry (Constraint 2).
  std::size_t removed = 0;
  const Command command = remove_cells_command(std::move(removals), removed, entered);
  const DispatchOutcome dispatched = dispatch(state, command);
  outcome.removed = removed;
  outcome.journal_entries_added = dispatched.journal_entries_added;
  // The honest post-condition (D-cells_remove-7): the objects are gone, so no selected id
  // can survive. Unconditional — clearing an already-empty selection is itself a no-op.
  state.selection().clear();
  return outcome;
}

bool can_delete(const AppState& state) { return !state.selection().empty(); }

} // namespace ace::commands
