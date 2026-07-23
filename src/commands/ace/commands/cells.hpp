#pragma once

#include <ace/commands/app_state.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>

#include <string>

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

} // namespace ace::commands
