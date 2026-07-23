#pragma once

#include <ace/commands/app_state.hpp>
#include <ace/scene/camera.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>

#include <string>

namespace arbc {
class Document;
class Registry;
} // namespace arbc

namespace ace::commands {

// --- Minting a camera (editor.cameras.frame_selection; D23/A14) ---------------------------
//
// The slot `app_state.hpp:103-108` reserves for the camera edit leaves. Before this file every
// camera mutation in the tree was either a bare `scene::` call wrapped by an L4 caller or a
// hand-rolled `Command{"add_camera", ...}` lambda inside a test; the mint verbs, the inspector
// and the overview now share ONE factory (D-frame_selection-6).
//
// `commands` may not include `ace/interact/` (the §8 DAG), so the geometry — "which objects,
// what region, what resolution" — is computed by the L4 caller and arrives here as a finished
// `(name, Resolution, Affine)` triple (D-frame_selection-5). This layer stays purely
// document-facing.

// The outcome of one mint, filled in when the command RUNS (i.e. inside `dispatch`,
// synchronously on the writer thread). Errors are values: a document with no root composition
// to place the camera in leaves `camera` invalid and `error` non-empty, with the document
// untouched (Constraint 6).
struct AddCameraOutcome {
  arbc::ObjectId camera; // the minted camera content; invalid on failure
  std::string error;     // empty on success
};

// The dispatchable "mint a camera" verb: exactly ONE `scene::add_camera`, so the mutation
// rides `commands::Command`/`dispatch` and `AppState`'s revision + dirty bookkeeping (A13)
// stays correct. The create costs TWO journal entries — `Document::add_content` self-commits
// and the binding layer is a second transaction (`camera.hpp:145-150`), the shape D-cells_model-7
// already accepted for the insert side — while the D15 observable contract still holds: ONE
// undo detaches the layer and the camera leaves `scene::cameras()`, one redo restores it on the
// same `ObjectId` (Constraint 9). The command does not loop and does not hand-compose the two
// transactions into one.
//
// `registry` and `outcome` are held BY REFERENCE by the returned command and must outlive the
// `dispatch` call (which is synchronous).
Command add_camera_command(const arbc::Registry& registry, std::string name,
                           scene::Resolution resolution, const arbc::Affine& frame,
                           AddCameraOutcome& outcome);

// The auto-name a mint gives its camera (D-frame_selection-9): the first `"Camera <n>"`,
// `n >= 1`, not already a name in `scene::cameras()`. "Camera" rather than "Shot" because that
// is the word the user sees — the inspector section is titled "Cameras" and the canvas picker
// lists camera names. FIRST-FREE rather than a monotonic counter, so minting, undoing, and
// minting again reuses the name instead of leaking to "Camera 3" — deterministic, and therefore
// assertable in the e2e. It does not rename existing cameras and tolerates a duplicate the user
// creates by hand afterwards (Constraint 12); rename is the shipped escape hatch
// (`scene::rename_camera`), so the name is a starting point, not a commitment.
std::string next_camera_name(const arbc::Document& document);

// Whether Frame Selection has anything to act on — the rail's disabled-state gate. Deliberately
// COARSER than the actual refusal condition (D-frame_selection-7): it is the cheap question
// ("is anything selected?"), because an exact gate would have to walk the whole document and
// union the placed extents EVERY frame the rail draws, from a component that structurally
// cannot call `interact::pick_targets` at all. A selection of only UNBOUNDED content is
// therefore an enabled item that mutates nothing — the same coarseness `can_delete` ships.
bool can_frame_selection(const AppState& state);

} // namespace ace::commands
