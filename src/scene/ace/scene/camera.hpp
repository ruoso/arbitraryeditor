#pragma once

#include <arbc/base/geometry.hpp>  // arbc::Rect
#include <arbc/base/ids.hpp>       // arbc::ObjectId
#include <arbc/base/transform.hpp> // arbc::Affine
#include <arbc/contract/content.hpp>

#include <cstdint>
#include <memory>
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
// non-rendering shot camera. Its serialized state is the camera's NAME + output
// RESOLUTION; the camera's frame placement is the binding `Layer`'s `Affine`
// transform (so a camera is the same ObjectId-addressable placed-object shape as a
// cell, D7). Contributes ZERO pixels to the composite (`bounds()` is empty, so the
// compositor culls it): a camera observes, it does not draw (D2, Constraint 5).
class CameraContent final : public arbc::Content {
public:
  CameraContent(std::string name, Resolution resolution);

  // Non-rendering: an empty `bounds()` culls the layer before any render call, so a
  // camera adds no pixels to `render_document_srgb8` (Constraint 5).
  std::optional<arbc::Rect> bounds() const override;
  arbc::Stability stability() const override;
  std::optional<arbc::TimeRange> time_extent() const override;
  std::optional<arbc::RenderResult> render(const arbc::RenderRequest& request,
                                           std::shared_ptr<arbc::RenderCompletion> done) override;

  // Read-only accessors the codec + `cameras()` read (mirrors `SolidContent::color`).
  const std::string& camera_name() const noexcept { return d_name; }
  Resolution resolution() const noexcept { return d_resolution; }

  static constexpr const char* kind_id = "org.arbc.camera";
  static constexpr const char* kind_version = "1";

private:
  std::string d_name;
  Resolution d_resolution;
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

// Rename the shot camera whose content id is `camera`, preserving its resolution,
// frame, and position in layer order. Returns true when the camera was found and
// renamed. WRITER-THREAD ONLY; undoable via the journal (D-model-4). Because a camera
// Content's serialized state is immutable and libarbc exposes no in-place content-
// param edit for a leaf kind, a rename REPLACES the camera's Content+Layer with a
// freshly-named pair at the same order index — so the renamed camera takes a new
// `ObjectId` (see the `editor.cameras.rename_stable_id` follow-up).
bool rename_camera(arbc::Document& document, const arbc::Registry& registry, arbc::ObjectId camera,
                   const std::string& new_name);

} // namespace ace::scene
