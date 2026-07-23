#pragma once

#include <ace/commands/app_state.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace arbc {
class Registry;
} // namespace arbc

namespace ace::commands {

// The outcome of one insert, filled in when the command RUNS (i.e. inside
// `dispatch`, synchronously on the writer thread). Errors are values: a refusing
// kind or a malformed config leaves `content` invalid and `error` holding the
// kind's OWN message, with the document untouched (Constraint 3).
struct InsertCellOutcome {
  arbc::ObjectId content; // the minted cell content; invalid on failure
  std::string error;      // empty on success
};

// The dispatchable "insert a cell" verb (Constraint 4): the mutation rides
// `commands::Command`/`dispatch` so `AppState`'s revision + dirty bookkeeping (A13)
// stays correct and the edit lands in the journal like any other. The create costs
// TWO journal entries (`scene::add_cell`'s two-transaction shape, D-cells_model-7);
// the D15 observable contract still holds — one undo detaches the placing layer and
// the cell leaves `scene::cells()`, one redo restores it on the same `ObjectId`.
//
// `registry` and `outcome` are held BY REFERENCE by the returned command and must
// outlive the `dispatch` call (which is synchronous).
Command insert_cell_command(const arbc::Registry& registry, std::string kind_id, std::string config,
                            const arbc::Affine& placement, InsertCellOutcome& outcome);

// --- Delete (editor.cells.remove) -------------------------------------------------------

// One resolved deletion target: the `Content` the selection names and the `Layer` that
// places it in the root composition — the exact pair `arbc::Document::remove_content`
// takes. A selection holds only content ids (D-selection-1); the layer is what has to be
// looked up, and looking it up is the whole job of `selected_removals`.
struct Removal {
  arbc::ObjectId content;
  arbc::ObjectId layer;
};

// Resolve `selection` into the removals it names, in SELECTION order — the pure,
// document-reading half of a delete, so every rule below is unit-testable with no frame
// pump and no gateway.
//
// Reads `scene::cells` + `scene::cameras` rather than `interact::pick_targets`: `commands`
// may not depend on `interact` (the §8 DAG), and both accessors already return the
// `{content, layer}` pair the library verb needs (D-cells_remove-3). Kind-agnostic — a
// selected camera resolves exactly like a selected cell (D-cells_remove-1).
//
// A selected id with no live target — already deleted, undone away, GC'd out from under the
// selection — is SKIPPED, not an error (Constraint 5). The result is therefore never longer
// than the selection and carries no duplicates (`Selection` is a set).
std::vector<Removal> selected_removals(const arbc::Document& document,
                                       const arbc::Registry& registry, const Selection& selection);

// The dispatchable "remove one placed object" verb: exactly one `scene::remove_cell`, hence
// exactly ONE libarbc transaction and one journal entry, so `dispatch`'s
// one-command-one-transaction contract holds literally (Constraint 3). A multi-object
// delete is N of these commands, NOT one command looping N removals.
//
// `removed` is filled in when the command RUNS (synchronously, inside `dispatch`, on the
// writer thread): false means the target was stale and nothing was mutated. It is held BY
// REFERENCE and must outlive the `dispatch` call.
Command remove_cell_command(arbc::ObjectId content, arbc::ObjectId layer, bool& removed);

// The observable result of one `delete_selection`. `removed` counts the objects that
// actually left the composition (stale members of the selection are skipped, so it can be
// smaller than the selection); `journal_entries_added` is the summed `dispatch` delta —
// ONE per removed object, which is also the number of undo presses it takes to restore
// them all (the accepted asymmetry, D-cells_remove-2).
struct DeleteOutcome {
  std::size_t removed = 0;
  std::size_t journal_entries_added = 0;
};

// Delete the whole project-level selection (D19): resolve the targets against the LIVE
// document here, on the writer thread — never from a UI-side snapshot taken frames earlier,
// which an interleaved undo/redo could have invalidated (D-cells_remove-3) — dispatch one
// `remove_cell_command` per target, then EMPTY the selection.
//
// The selection is cleared by the delete itself rather than left to the next frame's
// `Selection::prune` (D-cells_remove-7): the objects are gone, so no id can survive, and the
// post-condition is observable with no frame pump and from a surface with no canvas open.
// `prune` stays as shipped — it is the guard for the undo and GC paths, which delete does
// not replace. An empty (or wholly stale) selection is a no-op: zero entries, unchanged
// revision, nothing thrown. Never marks the session clean — a delete advances the revision
// and therefore re-dirties it (Constraint 8 / D-undo-4).
DeleteOutcome delete_selection(AppState& state);

// Whether Delete has anything to act on — the rail's disabled-state and the chord's gate,
// mirroring `journal().can_undo()`'s role for Ctrl+Z (D-undo-3). Deliberately the cheap
// question ("is anything selected?") rather than a full document walk: it is polled every
// frame from the UI thread, and a selection holding only stale ids is already a documented
// no-op that `Selection::prune` clears on the next canvas frame.
bool can_delete(const AppState& state);

} // namespace ace::commands
