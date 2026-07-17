#pragma once

#include <ace/dockmodel/dockmodel.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace ace::dockmodel {

// The fixed catalog of view *types* D18 names (docs/00-design.md:479 / §10). The
// `.tji` note (tasks/00-editor.tji:73) enumerates exactly eight — no more, no
// fewer (D-view-registry-3). Canvas is the multi-camera view (D18/D19: canvases
// are cameras, many side by side); the other seven are project-level singletons
// (D19). There is no standalone "Cameras" type — cameras live within Overview /
// Layers (D6, docs/00-design.md:467). The enumerator order is the catalog order.
enum class ViewType { Canvas, Layers, Inspector, Overview, Color, History, Assets, Export };

// The number of registered view types — the catalog is exactly this size.
inline constexpr std::size_t k_view_type_count = 8;

// One catalog entry: the type, its stable id stem (`slug`), its human display
// `title`, and whether it may be instantiated more than once. Pure data — no
// ImGui, no handles (A8 / refinement Constraint 2); a view is identified only by
// its stable string id, so the model stays headless-testable and is the seam
// `editor.dock.workspaces` will serialize.
struct ViewDescriptor {
  ViewType type;
  std::string_view slug;  // e.g. "inspector" — the instance-id stem
  std::string_view title; // e.g. "Inspector" — the display title
  bool multi_instance;    // true only for Canvas
};

// The fixed catalog, indexed by ViewType value (exactly k_view_type_count).
const std::array<ViewDescriptor, k_view_type_count>& view_catalog();

// The descriptor for `type` (indexes view_catalog() by the enumerator value).
const ViewDescriptor& view_descriptor(ViewType type);
// The slug→type lookup; nullopt for an unknown slug.
std::optional<ViewType> view_type_for_slug(std::string_view slug);
// The type→slug / type→title projections (bare descriptor field accessors).
std::string_view view_slug(ViewType type);
std::string_view view_title(ViewType type);

// A parsed instance id: the view type plus its instance index. `index` is 0 for
// a singleton (a bare slug); >=1 for a multi-instance `slug#N` (D-view-registry-4).
struct ParsedViewId {
  ViewType type;
  int index;
  bool operator==(const ParsedViewId&) const = default;
};

// Parse an instance id back to (type, index). A singleton is a bare slug
// ("inspector" → {Inspector, 0}); a multi-instance type is `slug#N` with a
// positive, leading-zero-free N ("canvas#2" → {Canvas, 2}). Rejects (nullopt):
// an unknown slug, a `#` suffix on a singleton, a missing / zero / non-numeric /
// leading-zero index on a multi type, and a bare multi-instance slug.
std::optional<ParsedViewId> parse_view_id(std::string_view id);

// The view registry: the static catalog plus the *only* per-instance state — a
// monotonic, non-recycling counter per multi-instance type (D-view-registry-4).
// The set of open views IS DockLayout::view_ids(); open/close/reopen are thin
// orchestration over the existing DockLayout mutations (D-view-registry-2). No
// second registry of "what's open."
class ViewRegistry {
public:
  // Mint the next instance id for `type`: the bare slug for a singleton; a fresh
  // "slug#N" (monotonic, never reused even after a close) for a multi-instance
  // type. Advances the per-type counter for multi-instance types only.
  std::string mint_id(ViewType type);

  // Open a view of `type` into `layout`; returns the instance id now present.
  // Into an EMPTY layout: seed a fresh root leaf holding the new id. Into a
  // non-empty layout: insert as a tab of the target leaf — the leaf holding
  // `target`, or (when `target` is empty / absent) the last leaf in pre-order.
  // A singleton already present is IDEMPOTENT: its existing tab is activated and
  // its id returned, no duplicate. A multi-instance type mints a distinct id
  // every call.
  std::string open(DockLayout& layout, ViewType type, std::string_view target = {});

  // Close the view `view_id` (DockLayout::remove_view: an emptied leaf collapses,
  // the last view leaves the layout empty). Returns false if `view_id` is absent.
  bool close(DockLayout& layout, std::string_view view_id);

  // Reopen a singleton `type` under its stable slug — the named entry point for
  // the close-everything → reopen round-trip (D-view-registry-6). From an empty
  // layout seeds a fresh root leaf. Equivalent to open() for a singleton.
  std::string reopen(DockLayout& layout, ViewType type);

private:
  // Per-type "next index" — advanced only for multi-instance types. Monotonic
  // and never decremented, so a stale id can never alias a freshly opened pane.
  std::array<int, k_view_type_count> next_index_{};
};

} // namespace ace::dockmodel
