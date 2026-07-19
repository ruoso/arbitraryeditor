#pragma once

#include <arbc/base/geometry.hpp>  // arbc::Rect
#include <arbc/base/ids.hpp>       // arbc::ObjectId
#include <arbc/base/transform.hpp> // arbc::Affine
#include <arbc/contract/content.hpp>
#include <arbc/model/model.hpp>   // arbc::Model::Transaction, set_content_state
#include <arbc/model/records.hpp> // arbc::StateHandle, arbc::SlotIndex

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace arbc {
class Document;
class Registry;
} // namespace arbc

namespace ace::scene {

// A shot camera's output resolution in device pixels (D9: frame != resolution).
struct Resolution {
  int width = 0;
  int height = 0;

  friend bool operator==(const Resolution&, const Resolution&) = default;
};

// The editor's FIRST custom libarbc `Content` kind (A14, D-model-1): a persisted,
// non-rendering shot camera. Its state is the camera's NAME + output RESOLUTION,
// carried through the `arbc::Editable` facet as MUTABLE, undoable content state
// (D-rename-1/2) so a rename edits the name IN PLACE — preserving the camera's
// `ObjectId` so the shared `ObjectId`-keyed selection keeps its handle across a
// rename (D7). The camera's frame placement is the binding `Layer`'s `Affine`
// transform (so a camera is the same ObjectId-addressable placed-object shape as a
// cell, D7). Contributes ZERO pixels to the composite (`bounds()` is empty, so the
// compositor culls it): a camera observes, it does not draw (D2, Constraint 5).
class CameraContent final : public arbc::Content, public arbc::Editable {
public:
  CameraContent(std::string name, Resolution resolution);

  // Non-rendering: an empty `bounds()` culls the layer before any render call, so a
  // camera adds no pixels to `render_document_srgb8` (Constraint 5).
  std::optional<arbc::Rect> bounds() const override;
  arbc::Stability stability() const override;
  std::optional<arbc::TimeRange> time_extent() const override;
  std::optional<arbc::RenderResult> render(const arbc::RenderRequest& request,
                                           std::shared_ptr<arbc::RenderCompletion> done) override;

  // Opt into the editable-state facet (D-rename-2): the name/resolution is mutable,
  // undoable content state addressed by an opaque `StateHandle`.
  arbc::Editable* editable() override { return this; }

  // --- arbc::Editable ---
  // A small versioned `{name, resolution}` store — the `FakeEditable`/`RasterStore`
  // shape (slot table + free list + a live `d_base` handle), NOT raster's tile
  // machinery. Thread contract (Constraint 5, A4/A5): `capture`/`restore`/`retain`/
  // `state_cost` run on the WRITER thread while `release` arrives on the DRAIN thread,
  // so the store is mutex-guarded.
  arbc::StateHandle capture() override;
  void restore(arbc::StateHandle state) override;
  std::size_t state_cost(arbc::StateHandle state) const override;
  void retain(arbc::StateHandle state) override;
  void release(arbc::StateHandle state) override;

  // Rename in place under transactional discipline (the `RasterContent::paint` shape,
  // minus the pixels): mint a new version carrying `new_name` over the CURRENT
  // resolution, adopt it as the live base, and journal it via `set_content_state` so
  // the camera keeps its `ObjectId` and `undo` restores the prior name on that same
  // object (D-rename-1, Constraint 2/3). `self` is this content's `ObjectId`.
  // WRITER-THREAD ONLY; the caller wraps it in a `commands::Command` for undo/redo.
  void set_name(arbc::Model::Transaction& txn, arbc::ObjectId self, std::string new_name);

  // Read-only accessors the codec + `cameras()` read: the CURRENT base version's state
  // (so a persisted or round-tripped camera reflects the latest rename, Constraint 4).
  std::string camera_name() const;
  Resolution resolution() const;

  static constexpr const char* kind_id = "org.arbc.camera";
  static constexpr const char* kind_version = "1";

private:
  // One captured version of the editable state, indexed by `StateHandle::slot`.
  struct Version {
    std::string name;
    Resolution resolution;
    std::uint32_t refcount = 0; // published-record pins (retain +1 / release -1)
  };

  // Mint a fresh version carrying `{name, resolution}` and return its slot. Reuses a
  // recycled slot when one is free, else grows the table. `d_mutex` MUST be held.
  arbc::SlotIndex mint_version(std::string name, Resolution resolution);

  mutable std::mutex d_mutex;          // guards the store (writer retain vs drain release)
  std::vector<Version> d_versions;     // indexed by slot
  std::vector<arbc::SlotIndex> d_free; // recycled slots
  arbc::StateHandle d_base;            // the live version (what accessors + `capture` read)
};

// Register `org.arbc.camera`'s factory + JSON-free codec on `registry` (A14,
// D-model-2/5) so `project`'s generic snapshot save/load round-trips cameras with NO
// `project->scene` edge: `arbc::builtin_codecs(registry)` folds every registry-carried
// codec into the save/load table. Idempotent (a duplicate registration is ignored).
void register_camera_kind(arbc::Registry& registry);

// The `ObjectId`-addressable camera as `cameras()` reports it (Constraint 7): its
// content id, the binding layer id, name, resolution, and frame `Affine` (the layer
// transform). `layer` is what `cameras.manip` reframes through `set_layer_transform`.
struct Camera {
  arbc::ObjectId id;    // the org.arbc.camera Content object (the camera's identity)
  arbc::ObjectId layer; // the layer that places it (frame carrier)
  std::string name;
  Resolution resolution;
  arbc::Affine frame; // the binding layer's Affine transform
};

// Read every persisted shot camera in the document's root composition, in layer
// order (Constraint 7), over the lock-free `pin()` reader seam (the mould of
// `project::root_composition_size`). Empty for a fresh project (no shots yet).
std::vector<Camera> cameras(const arbc::Document& document);

// Create a shot camera in the document's root composition (D-model-1, Constraint 3):
// one `org.arbc.camera` Content carrying `name` + `resolution`, bound by one Layer
// whose `Affine` transform IS the frame placement (`frame`), so the camera is the
// same ObjectId-addressable placed-object shape as a cell (D7). `registry` supplies
// the kind token — seeded (via `project::seed_kind_bridge`) so it matches the
// save-side bridge, letting the generic snapshot save serialize the camera.
//
// Returns the new camera's content `ObjectId`, or an invalid `ObjectId` when the
// document has no root composition to place the camera in. WRITER-THREAD ONLY (A4);
// wrap in a `commands::Command` and `dispatch` it for undo/redo (D-model-4). NOTE:
// binding the Content vtable (`Document::add_content`) and attaching the frame layer
// are two libarbc transactions — the vtable seam offers no atomic content+layer+
// attach — so a create spans two journal entries; because `cameras()` keys off
// composition membership, a single undo detaches the layer and the camera disappears,
// and a single redo restores it (the observable D15 contract).
arbc::ObjectId add_camera(arbc::Document& document, const arbc::Registry& registry,
                          const std::string& name, Resolution resolution,
                          const arbc::Affine& frame);

// Rename the shot camera whose content id is `camera`, preserving its `ObjectId`,
// binding layer, resolution, frame, and position in layer order (Constraint 2).
// Returns true when the camera was found and renamed, false when `camera` is unknown
// or not a `CameraContent` (a no-op, Constraint 3). WRITER-THREAD ONLY; undoable via
// the journal (D-model-4). The rename edits the name IN PLACE through the camera's
// `arbc::Editable` facet in ONE `set_content_state` transaction (D-rename-1) — so the
// renamed camera is the SAME object and any consumer that remembers it by `ObjectId`
// (the shared selection, the future manip/overview) keeps its handle across the
// rename. `registry` is unused now (no new Content is minted) but kept for signature
// stability with `add_camera`.
bool rename_camera(arbc::Document& document, const arbc::Registry& registry, arbc::ObjectId camera,
                   const std::string& new_name);

} // namespace ace::scene
