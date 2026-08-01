#pragma once

#include <ace/project/project.hpp> // scene -> project edge (used across this component)

#include <arbc/base/expected.hpp>
#include <arbc/base/geometry.hpp>      // arbc::Rect, arbc::Vec2
#include <arbc/base/ids.hpp>           // arbc::ObjectId
#include <arbc/base/transform.hpp>     // arbc::Affine
#include <arbc/media/pixel_traits.hpp> // arbc::WorkingPixel (brush_dab color, premultiplied-linear)

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
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
// entry, unconditionally and in registration order. The fields, their types, defaults
// and units come from the kind's OWN advertised `arbc::KindInsertSchema` (read back
// through `arbc::Registry::insert_schema(id)`, issue #21); the editor renders fields
// and hands strings back and never learns a kind's separator. A kind that advertises
// no schema upstream gets the raw-config fallback and is still insertable end-to-end,
// so a future plugin host that registers on the `Registry` surfaces here automatically.

// A display hint for the modal, mirrored from `arbc::KindInsertField::Type`
// (D-insert_schema-7). Purely advisory: the L3 modal renders every field as free text,
// and no code anywhere branches on this to decide WHETHER a kind is insertable — the
// dock POD deliberately drops the type entirely.
enum class InsertFieldType {
  Integer, // a whole number (a raster's width/height)
  Number,  // a real number (a solid's channels and extent)
  Text,    // free text — the raw-config fallback, and any string field
};

// One input the caller must collect before a kind's factory can be called.
struct InsertField {
  std::string id;    // stable field id the caller keys its values by (the arbc field name)
  std::string label; // human label — the field name plus its unit, when the kind gives one
  InsertFieldType type = InsertFieldType::Text;
  // The prefilled value, verbatim from the kind's advertised `default_value` (or empty
  // for the raw-config fallback). The kind owns its defaults now — the editor no longer
  // composition-matches a raster's resolution (D-insert_schema-6).
  std::string initial;
};

// One insertable kind. There is exactly one of these per `registry.ids()` entry.
struct KindInsertSchema {
  std::string kind_id;    // the registry id, verbatim
  std::string human_name; // `KindMetadata::human_name`, or `kind_id` when absent
  std::vector<InsertField> fields;
  // True when the kind advertised NO upstream schema: the single `config` field is
  // passed to the kind's factory VERBATIM. A first-class path, not an error — it is
  // what makes the no-allowlist property testable (Constraint 2).
  bool raw_config = false;
  // The kind's OWN config assembler, copied from `arbc::KindInsertSchema::assemble`
  // (issue #21): it joins the collected values, in `fields` order, into the opaque
  // `ContentConfig` string, so the GRAMMAR stays the kind's secret (D-insert_schema-2).
  // Empty for a raw-config schema (the single field travels verbatim).
  std::function<arbc::expected<std::string, std::string>(std::span<const std::string>)> assemble;
};

// The field id every raw-config fallback schema carries.
inline constexpr std::string_view k_raw_config_field = "config";

// One schema per advertised kind, in registration order — unconditionally. There is
// no filter by id, by metadata, or by "is it visual" on this path (Constraint 2);
// `insert_schemas(r).size() == r.ids().size()` always holds. Each entry's fields are
// mirrored from the kind's own `arbc::Registry::insert_schema(id)`; a kind that
// advertises none gets the raw-config fallback.
std::vector<KindInsertSchema> insert_schemas(const arbc::Registry& registry);

// The user's answers, keyed by `InsertField::id`.
using InsertValues = std::vector<std::pair<std::string, std::string>>;

// Assemble the kind's opaque `arbc::ContentConfig` string from `values`. The values are
// consumed POSITIONALLY — one per advertised field, in `fields` order (D-insert_schema-3,
// matching `assemble`'s `std::span` contract and robust to duplicate field names) — and
// handed to the kind's OWN `assemble` thunk (library kinds). A raw-config schema passes
// its one field through verbatim and lets the kind's factory be the validator. The editor
// no longer parses or rejects a value: validation is the factory's, at `add_cell`
// (D-insert_schema-2 / Constraint 4).
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

// Mint a cell of `kind_id` from `config` and place it in the ACTIVE composition at
// `placement`, returning the new `Content`'s `ObjectId`.
//
// `entered` is the raw isolation scope (`AppState::entered_composition`, D17/D29):
// `nullopt` = Root, an `ObjectId` = the nested composition the user has entered. The target
// composition is resolved through the `active_composition` fail-safe against the SAME pin the
// create lands on (D-scoped_edit-1) — a scope naming a GC'd/undone-away/foreign id degrades to
// Root there and then (Constraint 2), never a phantom un-editable composition. With `nullopt`
// (or a failed fail-safe) the cell lands in Root exactly as before (Constraint 4).
//
// Content is constructed ONLY through `registry.factory(kind_id)` — never by naming
// a concrete arbc type, which would be the hard-coded kind set A16 forbids — and the
// factory runs FIRST: a refusing kind (`org.arbc.fade`) or a malformed config
// returns the kind's OWN error string with the `Document` untouched (no content
// minted, no transaction opened, zero journal entries; Constraint 3).
//
// On success this is ONE libarbc transaction (D-one_action_one_entry-1):
// `Document::create_content_and_attach` binds the `Content` vtable, adds the placing layer,
// and attaches it inside a single `transact` — everything `add_content` + `add_layer` +
// `attach_layer` did, in the same order, so a create is ONE journal entry and one undo press,
// with no intermediate published state in which the content is attached to nothing.
arbc::expected<arbc::ObjectId, std::string>
add_cell(arbc::Document& document, const arbc::Registry& registry, std::string_view kind_id,
         std::string_view config, const arbc::Affine& placement,
         std::optional<arbc::ObjectId> entered = std::nullopt);

// Mint an `org.arbc.nested` cell carrying the library's EXTERNAL nested reference — the third D12
// import path (editor.import.nested / A31): placing another project's `.arbc` document into the
// active composition as a cell whose `params.ref` is the borrowed URI of the picked file, NOT an
// inline copy of its graph (D-nested-2). Returns the new `Content`'s `ObjectId`.
//
// This is a DEDICATED construction seam, not the A16 factory path `add_cell` uses (D-nested-1):
// the built-in `make_nested` factory accepts only a numeric in-document child `ObjectId`
// (`builtin_kinds.cpp:181-188`) and has no config grammar for a `ref` URI, so the generic
// `registry.factory(id)(config)` seam CANNOT construct an external reference. This verb constructs
// `arbc::NestedContent(arbc::ObjectId{}, std::string(ref_uri))` directly — the public
// `arbc/kind_nested/nested_content.hpp` built-in type on the already-declared `scene → libarbc`
// edge (the A14 camera mould) — and attaches it via `create_content_and_attach`. It must NOT
// re-register the `org.arbc.nested` factory (that would break the in-document nested insert path).
//
// `ref_uri` is the borrowed absolute-path URI of the picked `.arbc` (`project::borrow_asset_file`'s
// URI half, no embedded bytes — nested resolves through the `AssetSource`, not an embed channel):
// kept verbatim, un-relativized (D-image-5 / D13). An EMPTY `ref_uri` is refused with the document
// untouched (a cancelled/empty pick imports nothing, Constraint 3) — the factory-first discipline.
//
// `entered` is the isolation scope resolved fail-safe against the create's OWN pin, identical to
// `add_cell` (D-scoped_edit-1): a nested `.arbc` placed while a composition is entered lands there;
// a vanished/foreign scope degrades to Root. The freshly minted cell has `child == ObjectId{}`
// (unresolved), so it renders the doc-05 placeholder in-session and resolves to the referenced
// composition on the NEXT reopen, where the editor's `FilesystemAssetSource` loads it INLINE
// (D-nested-3); live in-session resolution needs a libarbc seam and is out of scope (parking lot).
//
// On success this is ONE libarbc transaction (D-one_action_one_entry-1): one journal entry, one
// undo press, no intermediate published state. Placement rides in as a finished `arbc::Affine`
// (identity over the unattached content's empty bounds, D-nested-4). WRITER-THREAD ONLY; wrap it in
// a `commands::insert_nested_reference_command` and `apply_edit` it — never a direct L4 mutation.
arbc::expected<arbc::ObjectId, std::string>
add_nested_reference(arbc::Document& document, const arbc::Registry& registry,
                     std::string_view ref_uri, const arbc::Affine& placement,
                     std::optional<arbc::ObjectId> entered = std::nullopt);

// One resolved deletion target for the batch verb below: the `Content` a selection names and
// the `Layer` that places it in the root composition. A scene-local mirror of
// `commands::Removal`, kept here because `commands` depends on `scene`, not the reverse (the
// §8 DAG), so the batch verb names no `commands` type (D-one_action_one_entry-3).
struct CellRemoval {
  arbc::ObjectId content;
  arbc::ObjectId layer;
};

// The declared inverse of `add_cell` (editor.cells.one_action_one_entry): make N placed
// objects — the `(content, layer)` pairs `removals` names — leave the ROOT composition as ONE
// user-visible action. Kind-AGNOSTIC — a camera is a `Content` + a `Layer` structurally
// identical to a cell (A14), and D7 makes them "one shape", so the same verb removes either
// (D-cells_remove-1).
//
// The whole batch is ONE `arbc::Document::remove_contents(span<Removal>)` call
// (`document.hpp:151`): one transaction — atomic (an observer sees the whole selection present
// or wholly gone), revision +1, ONE journal entry, one damage flush — and it owns the
// invariant that each content's binding row stays RETAINED while the journal holds the removal.
// So the delete is undoable BY CONSTRUCTION as a unit: the editor writes no inverse and no
// snapshot, and ONE undo restores all N objects on their SAME `ObjectId`s with layers and
// placements intact (D15 / D-one_action_one_entry-2).
//
// The ACTIVE composition is resolved ONCE from `entered` through the `active_composition`
// fail-safe against the live pin (D-scoped_edit-1) and each removal is validated against that
// same pinned generation: a removal with an invalid id, a layer that is not a live member of
// the ACTIVE composition, or a layer that does not place its stated content — a target already
// deleted, undone away, GC'd, or belonging to a DIFFERENT (e.g. out-of-scope) composition — is
// SKIPPED, not an error (Constraint 5). Returns the number of removals that actually left the
// composition (never more than `removals.size()`); an empty or wholly-stale batch mutates
// NOTHING and returns 0.
//
// `entered` is the raw isolation scope (`AppState::entered_composition`, D17/D29): `nullopt` =
// Root, an `ObjectId` = the entered nested composition. While entered, the membership gate
// validates each removal against the ENTERED composition, so an in-scope layer passes and a
// root (out-of-scope) layer is skipped — symmetric with `add_cell`, which inserts into the
// same active composition. A scope naming a vanished id degrades to Root (Constraint 2); with
// `nullopt` this is byte-for-byte the shipped root-only removal (Constraint 4). WRITER-THREAD
// ONLY (`document.hpp:151`); wrap it in a `commands::Command` and `dispatch` it so the edit
// rides the single-writer seam.
std::size_t remove_cells(arbc::Document& document, std::span<const CellRemoval> removals,
                         std::optional<arbc::ObjectId> entered = std::nullopt);

// Where a cell's pixels come from, classified from the GENERIC `arbc::Content` facets
// D11 defines (`docs/00-design.md:478`) — NEVER a `kind_id` string-switch (A16 /
// D-resolution-2). `editable()` non-null ⇒ the content owns a mutable pixel grid (D11's
// "editable?" axis); else a non-empty `external_asset_ref()` ⇒ it borrows an external
// file (D11's "bytes where?" axis); else it has no finite-detail floor. So a future
// plugin kind is classified automatically: an editable kind is resamplable, an
// external-ref kind is source-limited, anything else is resolution-independent.
enum class DetailSource {
  PaintedRaster,         // owns a mutable grid — soft is fixable (resample, D8)
  ReferencedImage,       // borrows an external file — soft is terminal (source-limited, D8)
  ResolutionIndependent, // a solid / procedural / nested cell — no detail floor at all
};

// A cell's pixel provenance plus its native pixel grid, derived during the same pinned
// `cells()` walk that fills `content_bounds` (D-resolution-1/-2). The inspector reads
// this to state whether a cell's softness is fixable and to show its native resolution.
struct CellDetail {
  DetailSource source = DetailSource::ResolutionIndependent;
  // The native pixel grid: the `content_bounds` dimensions for a raster/image — whose
  // content-space extent numerically IS its pixel grid, 1 native pixel = 1 content unit
  // before placement (D-resolution-1) — as `(width, height)`. `nullopt` for a
  // `ResolutionIndependent` cell and for an unavailable image (empty bounds): those have
  // no native resolution to show and no health verdict (health N/A, Constraint 4).
  std::optional<std::pair<int, int>> native_pixels;
  // D11's "bytes where?" axis, orthogonal to the editable? axis `source` carries: `true` when
  // the content borrows an external file (a non-empty generic `external_asset_ref()`), `false`
  // when its bytes are owned (in `assets/`, or a painted grid, or resolution-independent). Read
  // off the SAME pinned walk from the generic `arbc::Content` facet, never a `kind_id` switch
  // (A16 / D-resolution-2), so the Layers list surfaces owned-vs-borrowed for any kind including
  // a future plugin (D-layers-4). A referenced borrowed photo is `ReferencedImage` + borrowed;
  // a pasted owned image is owned; a painted raster is `PaintedRaster` + owned.
  bool borrowed = false;
};

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
  // The pixel provenance + native grid, from the same pinned snapshot (D-resolution-1/-2).
  // The inspector's native-resolution readout and D8 health/provenance line read this.
  CellDetail detail;
  // The placing layer's composite opacity (straight-alpha, D10) and visibility
  // (`k_layer_visible`), read off the SAME pinned snapshot. The inspector's Appearance block
  // shows and edits these two — the only novel cell edits this panel introduces
  // (editor.panels.inspector / D-inspector-2). Distinct from the paint-color alpha
  // (editor.panels.color, D-inspector-3): this is how strongly the whole cell composites.
  double opacity = 1.0;
  bool visible = true;
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

// The same walk, parameterised on which composition to list — the one-argument generalisation
// of the root-only overload above (which is exactly `cells(document, registry, root)`). Walks
// `composition`'s members in the SAME bottom→top membership order, shape, kind resolution, and
// camera exclusion; an invalid id or a composition the document does not hold yields an empty
// list. This is the data source the Layers panel's expand (peek a nested child, D-layers-5) and
// enter (re-root to a nested child, D-layers-3) draw from. PINNED read over `pin()` (A18).
std::vector<Cell> cells(const arbc::Document& document, const arbc::Registry& registry,
                        arbc::ObjectId composition);

// The child composition a nested cell (`org.arbc.nested`) wraps, read GENERICALLY off
// `arbc::Content::composition_ref()` — never a `kind_id` switch (A16 / D-resolution-2), so any
// kind that exposes a composition ref resolves. `nullopt` for a non-nested cell (no ref) or an
// unresolvable id. The Layers panel calls this to decide whether a row gets a disclosure
// triangle and where expand/enter descend (Constraint 7). PINNED read over `resolve()`.
std::optional<arbc::ObjectId> nested_composition_of(const arbc::Document& document,
                                                    arbc::ObjectId cell);

// The composition the Layers panel's reads and its reorder verb currently TARGET: the entered
// composition when it names a live composition, else the root (Constraint 8's fail-safe — a
// scope naming a GC'd/undone-away/foreign composition resolves to Root next frame, the
// look_through precedent D-look_through-7). `nullopt` entered ⇒ Root. Invalid when the document
// has no composition. PINNED read over `pin()`.
arbc::ObjectId active_composition(const arbc::Document& document,
                                  std::optional<arbc::ObjectId> entered);

// One crumb of the derived breadcrumb `Root ▸ … ▸ child` (D17). `composition` is the composition
// the crumb re-enters (the root's own id at index 0); `label` is "Root" at index 0, else the
// descent cell's kind id (display only — never branched on, A16), or "Nested" when unresolvable.
struct Breadcrumb {
  arbc::ObjectId composition;
  std::string label;
};

// The breadcrumb path from Root down to `entered`, DERIVED each frame (never stored, Constraint 9)
// by descending nested-cell `composition_ref()` links from the root composition. Index 0 is
// always Root. When `entered` is `nullopt`, invalid, or not reachable from Root (GC'd/undone away),
// the path is just `[Root]` — the same fail-safe `active_composition` applies. The panel maps a
// click on crumb `k` to a scope of `nullopt` (k == 0) or `path[k].composition`. PURE over the pin,
// unit-tested independently of ImGui. Empty only when the document has no composition at all.
std::vector<Breadcrumb> composition_path(const arbc::Document& document,
                                         const arbc::Registry& registry,
                                         std::optional<arbc::ObjectId> entered);

// The entered composition's PLACED FOCUS QUAD — the four corners of the wrapping nested cell's
// `placement` applied to its `content_bounds`, transformed all the way up to ROOT composition
// space (the space render's viewport camera maps to device). This is D17's "the child": the
// canvas keeps it bright and dims its complement (editor.canvas.isolation_scope /
// D-isolation_scope-4). The corners are returned in content-bounds order — (x0,y0), (x1,y0),
// (x1,y1), (x0,y1) — mapped through the accumulated placement, so a rotated/sheared placement
// yields a TRUE quad, not an AABB; the render dims the true complement.
//
// REGISTRY-FREE by design (A16): it descends the composition tree using only the generic
// `arbc::Content::composition_ref()` link + layer placements off the pinned model, never a
// `kind_id` switch, so `render` calls it without threading a `Registry`. `nullopt` for every
// no-dim case (the render's "no scrim" signal), matching the layers `active_composition` fail-safe
// (Constraint 6): the scope is `nullopt`/Root, an id that no longer names a live composition
// (GC'd / undone-away / foreign), a composition Root does not reach through a wrapping nested cell,
// or a wrapping cell whose content is UNBOUNDED (no finite focus region — never dims the whole
// plane's empty complement, Constraint 5). PINNED read over `pin()` (A18), lock-free.
struct FocusQuad {
  std::array<arbc::Vec2, 4> corners;
};
std::optional<FocusQuad> composition_focus_quad(const arbc::Document& document,
                                                std::optional<arbc::ObjectId> entered);

// The (from_index, to_index) pair in bottom→top membership order for a front→back list drag from
// list slot `list_from` to slot `list_to`, over a list of `count` rows. PURE (Constraint 2). The
// layers section renders `cells()` REVERSED (slot 0 = frontmost = top of z), so each endpoint maps
// slot → `count-1-slot`; the reversal is its own inverse. Agrees with `z_order_position` for every
// slot (a cell at list slot s has membership index `count-1-s`). Slots outside `[0,count)` clamp
// in.
struct MembershipMove {
  std::uint32_t from_index = 0;
  std::uint32_t to_index = 0;
};
MembershipMove list_drag_to_membership(int list_from, int list_to, int count);

// Move the CELL whose content id is `moved` to bottom→top membership index `to_index` within
// `composition` — the interactive z-order reorder (D6/D-layers-2), minted as an exact mirror of
// `set_camera_resolution`: resolve the cell's placing layer and its current membership index (its
// `from_index`) on the live pin, then ONE `document.transact("reorder_cell")` +
// `Transaction::reorder_layer` + commit. The cell keeps its `ObjectId`, so the shared selection
// and `undo` hold on the SAME object across the reorder. Returns false — a no-op that opens NO
// transaction and adds NO journal entry — for an unresolvable id, a camera (cameras carry no
// z-order, A14), a cell that is not a member of `composition`, or a `to_index` that is out of
// range or equal to `from_index` (the `reorder_layer` no-op contract). WRITER-THREAD ONLY; wrap it
// in a `commands::reorder_cell_command` and `apply_edit` it — never a direct L4 `Document`
// mutation.
bool reorder_cell(arbc::Document& document, arbc::ObjectId composition, arbc::ObjectId moved,
                  std::uint32_t to_index);

// The (index, count) z-order position of the cell whose content id is `id` within `ordered`
// (the back->front layer order `cells()` returns), for the inspector's "layer N of M" readout
// (D6, a READOUT only — interactive reorder is `editor.panels.layers`'). `index` is -1 when
// `id` is absent; `count` is always `ordered.size()`. PURE.
struct ZOrderPosition {
  int index = -1;
  int count = 0;
};
ZOrderPosition z_order_position(const std::vector<Cell>& ordered, arbc::ObjectId id);

// A one-line provenance description of a cell's pixel source (D11) — the referenced / owned /
// painted classification the inspector's Source block shows. Keyed on the GENERIC `DetailSource`
// (never a `kind_id` string, A16 / D-resolution-2). PURE.
const char* describe_detail_source(DetailSource source);

// Set the composite opacity of the CELL whose content id is `cell` (straight-alpha, D10),
// clamped to [0,1]. Preserves the cell's `ObjectId`, placement, order and every other facet, so
// the shared selection and `undo` hold on the SAME object across the edit. Returns false (a
// no-op that mutates nothing) for an unresolvable id or a non-cell (a camera) — the exact
// `scene::set_camera_resolution` no-op-returning-false mould (Constraint 3 / D-inspector-2).
// WRITER-THREAD ONLY; ONE `document.transact` = one undoable journal entry.
bool set_cell_opacity(arbc::Document& document, const arbc::Registry& registry, arbc::ObjectId cell,
                      double opacity);

// Set the visibility (`k_layer_visible`) of the CELL whose content id is `cell`, preserving its
// `ObjectId` and every other facet (the shared selection / `undo` keep the same object). Returns
// false (a no-op) for an unresolvable id or a non-cell. WRITER-THREAD ONLY; ONE transaction.
bool set_cell_visible(arbc::Document& document, const arbc::Registry& registry, arbc::ObjectId cell,
                      bool visible);

// Composite a batch of soft round brush dabs into the PAINTED-RASTER cell whose content id is
// `cell` (editor.paint.brush; D3/§4, the maps table `docs/00-design.md:532`). Each center in
// `centers` is a level-0 (content) pixel coordinate; every dab is an `arbc::round_dab` of
// `outer_radius` content px with an `inner_radius` soft core, composited premultiplied-linear
// source-over in `color` (premultiplied-linear working floats). ALL dabs land in ONE
// `document.transact` stamped with `coalesce_key`, so a whole stroke of many per-frame calls
// sharing one key folds to ONE undoable journal entry (D15 / `Transaction::coalesce`).
//
// The target is resolved through the GENERIC editable facet (`Content::editable()` non-null ==
// PaintedRaster, the `classify_detail` test — never a `kind_id` switch, A16); the concrete
// `arbc::RasterContent` is then reached to drive `paint`. Returns false — mutating nothing,
// opening NO transaction — for an unknown id, a camera, a non-editable (non-raster) cell, an
// empty `centers`, or a non-positive/non-finite radius. The dab writes PIXELS ONLY: the cell's
// placement, `content_bounds` and native resolution are untouched (D8). WRITER-THREAD ONLY.
bool brush_dab(arbc::Document& document, const arbc::Registry& registry, arbc::ObjectId cell,
               std::span<const arbc::Vec2> centers, double inner_radius, double outer_radius,
               const arbc::WorkingPixel& color, std::uint64_t coalesce_key);

} // namespace ace::scene
