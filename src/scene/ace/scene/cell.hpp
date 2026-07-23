#pragma once

#include <ace/project/project.hpp> // project::CompositionSize

#include <arbc/base/expected.hpp>
#include <arbc/base/geometry.hpp>  // arbc::Rect
#include <arbc/base/ids.hpp>       // arbc::ObjectId
#include <arbc/base/transform.hpp> // arbc::Affine

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arbc {
class Document;
class Registry;
} // namespace arbc

namespace ace::scene {

// Cells — D3's "one placed unit of artwork" (`docs/00-design.md:32-45`): a `Content`
// of some library KIND, an ARBITRARY AFFINE placing it in composition space, and its
// OWN native/working resolution. Structurally identical to the camera object A14
// mints (one `Content` + one placing `Layer`), which is what lets D7's one select
// tool serve both.
//
// The load-bearing rule here is A16 / `docs/00-design.md:505-511`: **the editor holds
// no kind allowlist.** `insert_schemas` emits one entry per `arbc::Registry::ids()`
// entry, unconditionally and in registration order; the per-kind grammar adapters in
// `build_config` are an ENHANCEMENT layered on that universal enumeration, never a
// gate on it. A kind the editor has never seen gets the raw-config fallback schema
// and is still insertable end-to-end, so a future plugin host that registers on the
// `Registry` surfaces in this affordance automatically.

// How the modal SHOULD render one input for a kind's opaque `arbc::ContentConfig`
// (D-cells_model-2). Purely advisory: the L3 modal is free to render every field as
// free text, and no code anywhere branches on this to decide WHETHER a kind is
// insertable.
enum class InsertFieldType {
  Text,      // free text — the raw-config fallback, and any string grammar
  Size,      // "<width>x<height>", two positive integers
  Color,     // "r,g,b,a", four numbers (premultiplied working floats)
  ObjectRef, // a positive decimal `arbc::ObjectId`
};

// One input the caller must collect before a kind's factory can be called.
struct InsertField {
  std::string id;    // stable field id the caller keys its values by
  std::string label; // human label
  InsertFieldType type = InsertFieldType::Text;
  // The prefilled value. For a raster's `size` this is the root composition's own
  // extent (Constraint 8: resolution is a first-class insert input — visible,
  // editable, and never silently applied), NOT a hidden default.
  std::string initial;
};

// One insertable kind. There is exactly one of these per `registry.ids()` entry.
struct KindInsertSchema {
  std::string kind_id;    // the registry id, verbatim
  std::string human_name; // `KindMetadata::human_name`, or `kind_id` when absent
  std::vector<InsertField> fields;
  // True when the editor has no grammar adapter for this kind: the single `config`
  // field is passed to the kind's factory VERBATIM. A first-class path, not an
  // error — it is what makes the no-allowlist property testable.
  bool raw_config = false;
};

// The field id every raw-config fallback schema carries.
inline constexpr std::string_view k_raw_config_field = "config";

// The resolution prefill used when the document has no usable root composition
// (Constraint 8's "falling back to 1024x1024").
inline constexpr int k_fallback_resolution = 1024;

// One schema per advertised kind, in registration order — unconditionally. There is
// no filter by id, by metadata, or by "is it visual" on this path (Constraint 2);
// `insert_schemas(r, …).size() == r.ids().size()` always holds. `composition` seeds
// the resolution prefill (pass `project::root_composition_size(document)`).
std::vector<KindInsertSchema>
insert_schemas(const arbc::Registry& registry,
               std::optional<project::CompositionSize> composition = std::nullopt);

// The user's answers, keyed by `InsertField::id`.
using InsertValues = std::vector<std::pair<std::string, std::string>>;

// Assemble the kind's opaque `arbc::ContentConfig` string from `values`. The known
// grammars are normalised (`" 1024 x 768 "` -> `"1024x768"`, since libarbc's config
// split does not trim); a raw-config schema passes its one field through verbatim
// and lets the kind's own factory be the validator. A missing or malformed value is
// an ERROR VALUE, never a silent default (Constraint 8).
arbc::expected<std::string, std::string> build_config(const KindInsertSchema& schema,
                                                      const InsertValues& values);

// The extent a factory-built content of `kind_id`/`config` would report — the input
// `interact::place_in_view` needs BEFORE `add_cell` mints anything (Constraint 6
// keeps `add_cell` on a finished `Affine`). `nullopt` inside the value means
// UNBOUNDED, which is the honest answer for a factory-built `org.arbc.solid`
// (D-cells_model-3) and for a not-yet-attached `org.arbc.nested`. Costs one extra
// factory call; the throwaway content is destroyed before returning.
arbc::expected<std::optional<arbc::Rect>, std::string>
probe_bounds(const arbc::Registry& registry, std::string_view kind_id, std::string_view config);

// Mint a cell of `kind_id` from `config` and place it in the root composition at
// `placement`, returning the new `Content`'s `ObjectId`.
//
// Content is constructed ONLY through `registry.factory(kind_id)` — never by naming
// a concrete arbc type, which would be the hard-coded kind set A16 forbids — and the
// factory runs FIRST: a refusing kind (`org.arbc.fade`) or a malformed config
// returns the kind's OWN error string with the `Document` untouched (no content
// minted, no transaction opened, zero journal entries; Constraint 3).
//
// On success this spans TWO libarbc transactions, exactly as `add_camera` does:
// `Document::add_content` self-commits (it is the only call that binds a `Content`
// vtable), then one `transact` adds and attaches the placing layer. So a create is
// two journal entries — but the D15 observable contract holds, since `cells()` keys
// off composition membership: one undo detaches the layer and the cell disappears,
// one redo restores it on the same `ObjectId` (D-cells_model-7).
arbc::expected<arbc::ObjectId, std::string>
add_cell(arbc::Document& document, const arbc::Registry& registry, std::string_view kind_id,
         std::string_view config, const arbc::Affine& placement);

// The declared inverse of `add_cell` (editor.cells.remove): make the placed object whose
// content is `content`, placed by `layer`, leave the ROOT composition. Kind-AGNOSTIC — a
// camera is a `Content` + a `Layer` structurally identical to a cell (A14), and D7 makes
// them "one shape", so the same verb removes either (D-cells_remove-1).
//
// The whole deletion is `arbc::Document::remove_content(content, composition, layer)`
// (`document.hpp:131`), never a hand-composed `detach_layer` + `remove` + `remove`: the
// library wrapper is ONE transaction — atomic, revision +1, ONE journal entry, one damage
// flush — and it owns the invariant that the content's binding row stays RETAINED while
// the journal holds the removal. So the delete is undoable BY CONSTRUCTION: the editor
// writes no inverse and no snapshot, and one undo restores the object on the SAME
// `ObjectId` with its layer and placement intact (D15).
//
// Returns false, having opened no transaction and mutated NOTHING, when the document has
// no root composition, when either id is invalid, when `layer` is not a live member of the
// root composition, or when `layer` does not place `content` — a selected id whose target
// was already deleted (or undone away, or GC'd) is a SKIP, not an error. Deleting from an
// entered/isolated nested scope is `editor.panels.layers`', symmetric with `add_cell`,
// which only inserts into the root. WRITER-THREAD ONLY (`document.hpp:130`); wrap it in a
// `commands::Command` and `dispatch` it so the edit rides the single-writer seam.
bool remove_cell(arbc::Document& document, arbc::ObjectId content, arbc::ObjectId layer);

struct Cell {
  arbc::ObjectId id;      // the `Content` object (the cell's identity)
  arbc::ObjectId layer;   // the layer that places it
  std::string kind_id;    // resolved through the `KindBridge`; EMPTY when unresolvable
  arbc::Affine placement; // the placing layer's `Affine`
  // The live content's own extent in CONTENT space (`arbc::Content::bounds()`), read off the
  // SAME pinned snapshot that produced `placement` (D-selection-11) — so a consumer never
  // mixes an extent from one document generation with a placement from another. `nullopt`
  // means UNBOUNDED: a factory-built `org.arbc.solid` genuinely covers the whole plane
  // (D-cells_model-3), which `interact::pick` treats as a hit everywhere and the marquee
  // excludes. `interact::pick_targets`, the inspector's "placed size", the Layers list, the
  // Overview, and fit-to-cell all read this one number.
  std::optional<arbc::Rect> content_bounds;
};

// Every cell in the root composition, in layer (z) order, over the lock-free `pin()`
// reader seam. Kind identity is read back through a `KindBridge` seeded from the
// SAME `registry` (`arbc::Content` exposes no kind-id virtual and `ContentRecord`
// carries only a token), so this needs no `dynamic_cast` chain — which would be the
// allowlist again, in the accessor (D-cells_model-8). `org.arbc.camera` layers are
// excluded (A14's cells/cameras split); a layer whose token does not resolve is
// reported with an EMPTY `kind_id` rather than dropped, so an unknown-passthrough
// cell from a foreign `project.arbc` is still listable.
std::vector<Cell> cells(const arbc::Document& document, const arbc::Registry& registry);

} // namespace ace::scene
