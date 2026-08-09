#include <ace/project/project.hpp> // project::seed_kind_bridge
#include <ace/scene/camera.hpp>    // CameraContent::kind_id (the cells/cameras split)
#include <ace/scene/cell.hpp>

#include <arbc/contract/content.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_nested/nested_content.hpp> // NestedContent — the external-ref import verb (A31)
#include <arbc/kind_raster/raster_content.hpp> // RasterContent::paint, round_dab, CoverageSampler
#include <arbc/media/pixel_traits.hpp>         // arbc::WorkingPixel (premultiplied-linear color)
#include <arbc/model/model.hpp>
#include <arbc/model/records.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ace::scene {
namespace {

// The library's per-field display type -> the editor's advisory display hint
// (D-insert_schema-7). Purely presentational: the dock POD drops the type entirely and
// the modal renders free text, so nothing branches on this to decide insertability.
InsertFieldType map_field_type(arbc::KindInsertField::Type type) {
  switch (type) {
  case arbc::KindInsertField::Type::Integer:
    return InsertFieldType::Integer;
  case arbc::KindInsertField::Type::Number:
    return InsertFieldType::Number;
  case arbc::KindInsertField::Type::ObjectId:
    return InsertFieldType::ObjectId;
  case arbc::KindInsertField::Type::Text:
    break;
  }
  return InsertFieldType::Text;
}

// The `ContentRecord.kind` token for `kind_id`, computed from a bridge seeded
// identically to `project::save_project`'s (via `project::seed_kind_bridge`) so the
// token stored on a freshly-inserted cell is the one the save-side bridge resolves
// back to the kind string — the same rule `scene::add_camera` follows.
std::uint64_t cell_token(const arbc::Registry& registry, std::string_view kind_id) {
  arbc::KindBridge bridge;
  project::seed_kind_bridge(bridge, registry);
  const arbc::KindMetadata* metadata = registry.metadata(kind_id);
  return bridge.intern(kind_id, metadata != nullptr ? std::string_view(metadata->version)
                                                    : std::string_view{});
}

// The root (lowest-id) composition, or an invalid id when the document has none.
arbc::ObjectId root_composition(const arbc::DocRoot& state) {
  arbc::ObjectId root_id;
  const arbc::CompositionRecord* rec = nullptr;
  if (!state.find_first_composition(root_id, rec) || rec == nullptr) {
    return arbc::ObjectId{};
  }
  return root_id;
}

// The active composition resolved against an ALREADY-PINNED generation (D-scoped_edit-1 /
// Constraint 2): the entered scope when it names a live composition of `state`, else Root. The
// `DocRoot`-taking sibling of the public `active_composition` — the edit verbs resolve the
// scope off the SAME pin the mutation lands on, so a scope gone stale between the UI-thread
// read and the writer-thread apply degrades to Root there and then (never a phantom), with no
// redundant second pin. `active_composition` below delegates here after pinning, so the
// fail-safe rule lives in exactly one place (D-scoped_edit-6).
arbc::ObjectId active_composition_in(const arbc::DocRoot& state,
                                     std::optional<arbc::ObjectId> entered) {
  if (entered && entered->valid() && state.find_composition(*entered) != nullptr) {
    return *entered;
  }
  return root_composition(state);
}

arbc::expected<std::unique_ptr<arbc::Content>, std::string>
make_content(const arbc::Registry& registry, std::string_view kind_id, std::string_view config) {
  const arbc::ContentFactory* factory = registry.factory(kind_id);
  if (factory == nullptr || !*factory) {
    return arbc::unexpected<std::string>(std::string(kind_id) + ": not a registered kind");
  }
  // Errors are values (Constraint 3): the kind's own message is the UI, which is
  // automatically correct for kinds the editor has never heard of.
  arbc::expected<std::unique_ptr<arbc::Content>, std::string> made = (*factory)(config);
  if (!made) {
    return arbc::unexpected<std::string>(made.error());
  }
  if (*made == nullptr) {
    return arbc::unexpected<std::string>(std::string(kind_id) + ": factory produced no content");
  }
  return std::move(*made);
}

// Classify a resolved content's pixel provenance from the GENERIC D11 facets, never its
// kind id (A16 / D-resolution-2). `editable()` non-null ⇒ it owns a mutable grid
// (PaintedRaster); else a non-empty `external_asset_ref()` ⇒ it borrows an external file
// (ReferencedImage); else it has no detail floor (ResolutionIndependent). The native pixel
// grid is `bounds`' dimensions for the two finite-detail sources — 1 native pixel = 1
// content unit before placement (D-resolution-1) — and `nullopt` for the independent case
// and for an unavailable image (empty bounds). `editable()` is a non-const facet virtual
// but a pure discovery read (it mints nothing) — const-clean under the render walk (the
// TSan anchor covers it). `bounds` is the same value the caller already read off this pin.
// D11's "bytes where?" axis by URI SHAPE (editor.import.paste / D-paste-2): a pasted image the
// project MINTED lives project-relative under `assets/` (the `tile_blob_uri` scheme,
// `mint_owned_asset`) — OWNED; an imported photo keeps an external absolute/`file://` URI —
// BORROWED. The URI already encodes the answer, so no per-kind owned/borrowed flag is needed (A16).
bool is_owned_asset_ref(std::string_view ref) {
  constexpr std::string_view k_owned_prefix = "assets/";
  return ref.substr(0, k_owned_prefix.size()) == k_owned_prefix;
}

CellDetail classify_detail(arbc::Content& content, const std::optional<arbc::Rect>& bounds) {
  CellDetail detail;
  // D11's "bytes where?" axis, orthogonal to the editable? classification below and read from the
  // SAME generic facet `ReferencedImage` keys on (A16 / D-layers-4). An empty asset ref means the
  // bytes are owned (a painted grid, a resolution-independent cell). A NON-empty ref splits by URI
  // shape (D-paste-2): a project-relative `assets/` URI is a minted OWNED image (owned); an
  // external absolute/`file://` URI is a borrowed referenced photo (borrowed).
  const std::string_view asset_ref = content.external_asset_ref();
  // A nested `org.arbc.nested` cell answers `external_composition_ref()`, NOT the image-only
  // `external_asset_ref()` the classifier reads for a photo — deliberately distinct virtuals
  // (`content.hpp:652,672`, D-nested-5). A borrowed external `.arbc` reference carries the SAME
  // borrowed provenance as a borrowed image, split owned/borrowed by the identical URI shape
  // (D-paste-2): an external absolute/`file://` URI is borrowed, a project-relative `assets/` URI
  // (post-Consolidate) is owned. Read the composition ref only when there is no asset ref — the two
  // are mutually exclusive across kinds (image answers the former's twin, nested the latter), so a
  // photo's classification is untouched (the acceptance regression guard).
  const std::string_view composition_ref = content.external_composition_ref();
  const std::string_view external_ref = !asset_ref.empty() ? asset_ref : composition_ref;
  detail.borrowed = !external_ref.empty() && !is_owned_asset_ref(external_ref);
  if (content.editable() != nullptr) {
    detail.source = DetailSource::PaintedRaster;
  } else if (!asset_ref.empty()) {
    detail.source = DetailSource::ReferencedImage; // owned OR borrowed — the `borrowed` flag splits
  } else {
    // ResolutionIndependent (the default): a solid / procedural / nested cell has no native detail
    // floor and no native px. A nested reference still carried its `borrowed` provenance above, so
    // the Layers source-file/relink readout lights up (D11/D13) without a per-kind `kind_id`
    // switch.
    return detail;
  }
  // A finite-detail cell: its content-space extent numerically IS its pixel grid
  // (D-resolution-1). An absent or empty extent (an unavailable image) leaves the native
  // grid `nullopt` — health N/A, never a false verdict (Constraint 4).
  if (bounds.has_value() && !bounds->empty()) {
    detail.native_pixels = std::pair<int, int>{static_cast<int>(std::lround(bounds->width())),
                                               static_cast<int>(std::lround(bounds->height()))};
  }
  return detail;
}

// The shared cell walk `cells()` and its composition-parameterised overload both drive: over an
// already-taken pin `state` and a `bridge` seeded from the same registry, collect every non-camera
// member of `composition` in bottom→top membership order. Empty for an invalid id or a composition
// the document does not hold. Factored out so the root and the nested (expand/enter) reads stay one
// walk with one kind-resolution / camera-exclusion / unknown-passthrough rule (D-layers-5).
std::vector<Cell> walk_cells(const arbc::Document& document, const arbc::KindBridge& bridge,
                             const arbc::DocRoot& state, arbc::ObjectId composition) {
  std::vector<Cell> result;
  if (!composition.valid() || state.find_composition(composition) == nullptr) {
    return result;
  }
  state.for_each_layer_in(composition, [&](arbc::ObjectId layer_id) {
    const arbc::LayerRecord* layer = state.find_layer(layer_id);
    if (layer == nullptr || !layer->content.valid()) {
      return;
    }
    const arbc::ContentRecord* record = state.find_content(layer->content);
    if (record == nullptr) {
      return;
    }
    std::string_view id;
    std::string_view version;
    const bool named = bridge.lookup(record->kind, id, version);
    if (named && id == CameraContent::kind_id) {
      return; // a camera is a scene object, but it is not a cell (A14)
    }
    std::optional<arbc::Rect> bounds;
    CellDetail detail;
    if (arbc::Content* content = document.resolve(layer->content); content != nullptr) {
      bounds = content->bounds();
      detail = classify_detail(*content, bounds);
    }
    result.push_back(Cell{layer->content, layer_id, named ? std::string(id) : std::string(),
                          layer->transform, bounds, detail, layer->opacity, layer->visible()});
  });
  return result;
}

} // namespace

std::vector<KindInsertSchema> insert_schemas(const arbc::Registry& registry) {
  std::vector<KindInsertSchema> schemas;
  const std::vector<std::string_view> ids = registry.ids();
  schemas.reserve(ids.size());
  // ONE entry per advertised id, unconditionally and in registration order (Constraint
  // 2). There is deliberately no filter on this path — a gate here is exactly the
  // hard-coded kind set `docs/00-design.md:505-511` forbids, and the unit suite asserts
  // `schemas.size() == ids.size()` for a registry carrying a kind the editor has never
  // seen. Each id's fields come from the kind's OWN `insert_schema(id)`; the editor holds
  // no per-kind grammar (D-insert_schema-1).
  for (const std::string_view id : ids) {
    KindInsertSchema schema;
    schema.kind_id = std::string(id);
    const arbc::KindMetadata* metadata = registry.metadata(id);
    schema.human_name = (metadata != nullptr && !metadata->human_name.empty())
                            ? metadata->human_name
                            : schema.kind_id;
    const arbc::KindInsertSchema* advertised = registry.insert_schema(id);
    if (advertised != nullptr) {
      // Mirror the kind's advertised fields and carry its own assembler (Constraint 5):
      // id = the field name; label = name plus " (unit)" when a unit is given; initial =
      // the kind's default; type = the library display hint. `min`/`max` are not
      // surfaced — the factory validates (Constraint 4).
      schema.assemble = advertised->assemble;
      schema.fields.reserve(advertised->fields.size());
      for (const arbc::KindInsertField& field : advertised->fields) {
        schema.fields.push_back(InsertField{
            field.name, field.unit.empty() ? field.name : field.name + " (" + field.unit + ")",
            map_field_type(field.type), field.default_value});
      }
    } else {
      // The null path is a FIRST-CLASS fallback, not an error (Constraint 2): a kind that
      // advertises no schema (nested, fade, crossfade, or any editor-unknown kind) travels
      // its opaque `ContentConfig` verbatim and its factory is the validator.
      schema.raw_config = true;
      schema.fields.push_back(
          InsertField{std::string(k_raw_config_field), "Config", InsertFieldType::Text, ""});
    }
    schemas.push_back(std::move(schema));
  }
  return schemas;
}

arbc::expected<std::string, std::string> build_config(const KindInsertSchema& schema,
                                                      const InsertValues& values) {
  // Values arrive positionally — one per advertised field, in `fields` order — because
  // the modal collects them that way and `assemble` consumes a `std::span` in the same
  // order (D-insert_schema-3, robust to duplicate field names). A short vector means the
  // caller never collected a field: an error value, not a silent default.
  if (values.size() < schema.fields.size()) {
    return arbc::unexpected<std::string>(schema.kind_id + ": missing insert field value");
  }
  if (schema.raw_config) {
    // The single field travels to the kind's factory verbatim — the editor knows no
    // grammar for it (Constraint 2/3).
    return values.front().second;
  }
  // A library-backed schema: hand the collected strings to the kind's OWN assembler, and
  // return its `expected` verbatim (D-insert_schema-2). The editor never re-implements the
  // grammar, so it cannot drift from the kind's factory.
  if (!schema.assemble) {
    return arbc::unexpected<std::string>(schema.kind_id + ": kind advertises no config assembler");
  }
  std::vector<std::string> collected;
  collected.reserve(schema.fields.size());
  for (std::size_t i = 0; i < schema.fields.size(); ++i) {
    collected.push_back(values[i].second);
  }
  return schema.assemble(collected);
}

arbc::expected<std::optional<arbc::Rect>, std::string>
probe_bounds(const arbc::Registry& registry, std::string_view kind_id, std::string_view config) {
  arbc::expected<std::unique_ptr<arbc::Content>, std::string> content =
      make_content(registry, kind_id, config);
  if (!content) {
    return arbc::unexpected<std::string>(content.error());
  }
  return (*content)->bounds();
}

arbc::expected<arbc::ObjectId, std::string>
add_cell(arbc::Document& document, const arbc::Registry& registry, std::string_view kind_id,
         std::string_view config, const arbc::Affine& placement,
         std::optional<arbc::ObjectId> entered) {
  // Factory FIRST (Constraint 3): everything below this point mutates the document,
  // so a refused kind or a malformed config leaves it byte-for-byte untouched.
  arbc::expected<std::unique_ptr<arbc::Content>, std::string> made =
      make_content(registry, kind_id, config);
  if (!made) {
    return arbc::unexpected<std::string>(made.error());
  }
  arbc::ObjectId composition;
  {
    const arbc::DocStatePtr state = document.pin();
    if (!state) {
      return arbc::unexpected<std::string>("the document has no published state");
    }
    // Resolve the scope against THIS pin (D-scoped_edit-1): the cell lands in the entered
    // composition when it is still live, else Root (Constraint 2/4/6).
    composition = active_composition_in(*state, entered);
  }
  if (!composition.valid()) {
    return arbc::unexpected<std::string>("no root composition to place a cell in");
  }
  // ONE journal entry (D-one_action_one_entry-1): `create_content_and_attach` binds the
  // `Content` vtable, adds the placing layer, and attaches it inside a SINGLE transaction —
  // everything `add_content` + `add_layer` + `attach_layer` did, in the same order, with no
  // intermediate published state in which a content exists attached to nothing, and one undo
  // press to reverse the create whole. Placement rides in as the finished `arbc::Affine`
  // (Constraint 6); the library's default opacity is kept.
  const arbc::Document::Placed placed =
      document.create_content_and_attach(std::shared_ptr<arbc::Content>(std::move(*made)),
                                         cell_token(registry, kind_id), composition, placement);
  return placed.content;
}

arbc::expected<arbc::ObjectId, std::string>
add_nested_reference(arbc::Document& document, const arbc::Registry& registry,
                     std::string_view ref_uri, const arbc::Affine& placement,
                     std::optional<arbc::ObjectId> entered) {
  // An empty URI is a value error, not a mutation: refuse BEFORE touching the document (the
  // `add_cell` factory-first discipline), so a cancelled/empty pick leaves it byte-for-byte
  // untouched (Constraint 3 / D-nested-2). Everything below mutates.
  if (ref_uri.empty()) {
    return arbc::unexpected<std::string>("org.arbc.nested: empty external reference URI");
  }
  arbc::ObjectId composition;
  {
    const arbc::DocStatePtr state = document.pin();
    if (!state) {
      return arbc::unexpected<std::string>("the document has no published state");
    }
    // Resolve the scope against THIS pin exactly as `add_cell` does (D-scoped_edit-1): a nested
    // `.arbc` placed while a composition is entered lands there; a vanished scope degrades to Root.
    composition = active_composition_in(*state, entered);
  }
  if (!composition.valid()) {
    return arbc::unexpected<std::string>("no root composition to place a nested reference in");
  }
  // The DEDICATED construction seam (A31 / D-nested-1): the built-in `make_nested` factory is
  // numeric-`ObjectId`-only, so the generic `registry.factory(id)(config)` path image/paste mint
  // through cannot construct an EXTERNAL reference. Construct `NestedContent(ObjectId{}, ref)`
  // directly — an unresolved child (`child == ObjectId{}`) holding the borrowed authored URI
  // verbatim (D-nested-2), which renders the doc-05 placeholder in-session and resolves inline on
  // reopen through the editor's `FilesystemAssetSource` (D-nested-3). This does NOT re-register the
  // `org.arbc.nested` factory (that would break the in-document nested insert; A31 alt-rejected).
  auto content = std::make_shared<arbc::NestedContent>(arbc::ObjectId{}, std::string(ref_uri));
  // ONE journal entry (D-one_action_one_entry-1), the `add_cell` mould minus the factory: bind,
  // add the placing layer, and attach inside a SINGLE transaction — one undo press reverses the
  // whole create. Placement rides in as the finished `arbc::Affine` (identity over the unattached
  // content's empty bounds, D-nested-4); the library's default opacity is kept.
  const arbc::Document::Placed placed = document.create_content_and_attach(
      std::move(content), cell_token(registry, arbc::NestedContent::kind_id), composition,
      placement);
  return placed.content;
}

std::size_t remove_cells(arbc::Document& document, std::span<const CellRemoval> removals,
                         std::optional<arbc::ObjectId> entered) {
  if (removals.empty()) {
    return 0; // an empty batch is a no-op that publishes nothing (Constraint 2)
  }
  // Resolve + validate every removal against the live pinned generation BEFORE opening
  // anything, so a wholly-stale batch costs exactly one snapshot read and leaves the
  // document byte-for-byte untouched (Constraint 5). The ACTIVE composition is resolved ONCE
  // here from the scope (D-scoped_edit-1): while entered the membership gate validates against
  // the entered composition, so an in-scope layer passes and a root (out-of-scope) layer is
  // skipped; a vanished scope degrades to Root (Constraint 2/5).
  std::vector<arbc::Document::Removal> validated;
  validated.reserve(removals.size());
  {
    const arbc::DocStatePtr state = document.pin();
    if (!state) {
      return 0;
    }
    const arbc::ObjectId composition = active_composition_in(*state, entered);
    if (!composition.valid()) {
      return 0; // no composition to remove from
    }
    for (const CellRemoval& removal : removals) {
      if (!removal.content.valid() || !removal.layer.valid()) {
        continue; // a selected id with no live target — skipped, not an error (Constraint 5)
      }
      const arbc::LayerRecord* record = state->find_layer(removal.layer);
      if (record == nullptr || record->content != removal.content) {
        continue; // no such layer, or it does not place `content`
      }
      bool member = false;
      state->for_each_layer_in(composition, [&](arbc::ObjectId layer_id) {
        if (layer_id == removal.layer) {
          member = true;
        }
      });
      if (!member) {
        continue; // live layer, but not in the ACTIVE composition (a different scope's)
      }
      validated.push_back(arbc::Document::Removal{removal.content, composition, removal.layer});
    }
  }
  if (validated.empty()) {
    return 0; // every target stale: no library call, nothing published (Constraint 2)
  }
  // ONE library transaction for the whole batch: one journal entry, one revision bump, one
  // undo press to restore all N (D-one_action_one_entry-2 / D15 / Constraint 2/4). Returns
  // the validated count — the objects that actually left the composition.
  document.remove_contents(validated);
  return validated.size();
}

std::vector<Cell> cells(const arbc::Document& document, const arbc::Registry& registry) {
  // The reverse map: seeded from the same registry the insert side interned through,
  // so a token minted by `add_cell` (or by `save`/`open`) names the same kind here.
  arbc::KindBridge bridge;
  project::seed_kind_bridge(bridge, registry);

  const arbc::DocStatePtr state = document.pin();
  if (!state) {
    return {};
  }
  // The root (lowest-id) composition; empty when the document has none. The whole walk — kind
  // resolution off `bridge`, the camera exclusion, the unknown-passthrough empty `kind_id`, the
  // opacity/visibility off the `LayerRecord`, the `content_bounds`/`detail` off the SAME pinned
  // generation as the placement (D-selection-11) — lives in `walk_cells`, shared with the
  // composition-parameterised overload below.
  return walk_cells(document, bridge, *state, root_composition(*state));
}

std::vector<Cell> cells(const arbc::Document& document, const arbc::Registry& registry,
                        arbc::ObjectId composition) {
  arbc::KindBridge bridge;
  project::seed_kind_bridge(bridge, registry);

  const arbc::DocStatePtr state = document.pin();
  if (!state) {
    return {};
  }
  return walk_cells(document, bridge, *state, composition);
}

std::optional<arbc::ObjectId> nested_composition_of(const arbc::Document& document,
                                                    arbc::ObjectId cell) {
  if (!cell.valid()) {
    return std::nullopt;
  }
  // Generic facet read (A16 / D-resolution-2): the child composition a nested kind wraps rides
  // `composition_ref()`, never a `kind_id` string-switch. A non-nested cell returns an invalid ref.
  arbc::Content* content = document.resolve(cell);
  if (content == nullptr) {
    return std::nullopt;
  }
  const arbc::ObjectId child = content->composition_ref();
  if (!child.valid()) {
    return std::nullopt;
  }
  return child;
}

arbc::ObjectId active_composition(const arbc::Document& document,
                                  std::optional<arbc::ObjectId> entered) {
  const arbc::DocStatePtr state = document.pin();
  if (!state) {
    return {};
  }
  // Fail-safe (Constraint 8 / D-look_through-7): the entered scope targets a live composition, else
  // Root. A `nullopt` scope, or one naming a GC'd/undone-away/foreign id, resolves to Root here —
  // re-evaluated every frame, so a vanished scope silently degrades rather than crashing. The rule
  // lives in `active_composition_in`, shared with the edit verbs' apply-time resolution.
  return active_composition_in(*state, entered);
}

ZOrderPosition z_order_position(const std::vector<Cell>& ordered, arbc::ObjectId id) {
  ZOrderPosition pos;
  pos.count = static_cast<int>(ordered.size());
  for (std::size_t i = 0; i < ordered.size(); ++i) {
    if (ordered[i].id == id) {
      pos.index = static_cast<int>(i);
      break;
    }
  }
  return pos;
}

const char* describe_detail_source(DetailSource source) {
  switch (source) {
  case DetailSource::PaintedRaster:
    return "Painted raster - editable, owned";
  case DetailSource::ReferencedImage:
    return "Referenced image - source-limited";
  case DetailSource::ResolutionIndependent:
    break;
  }
  return "Resolution-independent - no native detail floor";
}

namespace {

// The root-composition layer that places content `cell`, or an invalid id when none does — the
// reverse of the `cells()` layer->content walk, over the same lock-free pinned reader seam. The
// opacity/visibility verbs reach the `LayerRecord` (where those attributes live, not on the
// content) from the cell's content identity, which is the shared selection's key.
arbc::ObjectId placing_layer(const arbc::Document& document, arbc::ObjectId cell) {
  const arbc::DocStatePtr state = document.pin();
  if (!state) {
    return {};
  }
  const arbc::ObjectId composition = root_composition(*state);
  if (!composition.valid()) {
    return {};
  }
  arbc::ObjectId found;
  state->for_each_layer_in(composition, [&](arbc::ObjectId layer_id) {
    if (found.valid()) {
      return;
    }
    const arbc::LayerRecord* layer = state->find_layer(layer_id);
    if (layer != nullptr && layer->content == cell) {
      found = layer_id;
    }
  });
  return found;
}

// One raw membership slot of a composition: the content it places and whether that content is a
// CAMERA. `cells()` filters cameras out, so cells-space (camera-excluded) indices — which the list,
// `z_order_position`, and the drag helper all speak — must be translated to raw `reorder_layer`
// indices, which include camera slots. A camera is detected registry-free by the same
// `dynamic_cast<CameraContent*>` the `set_cell_*` verbs use (A14), so this needs no registry.
struct RawSlot {
  arbc::ObjectId content;
  bool is_camera = false;
};
std::vector<RawSlot> raw_membership(const arbc::Document& document, const arbc::DocRoot& state,
                                    arbc::ObjectId composition) {
  std::vector<RawSlot> raw;
  state.for_each_layer_in(composition, [&](arbc::ObjectId layer_id) {
    const arbc::LayerRecord* layer = state.find_layer(layer_id);
    const arbc::ObjectId content = layer != nullptr ? layer->content : arbc::ObjectId{};
    const bool camera = dynamic_cast<CameraContent*>(document.resolve(content)) != nullptr;
    raw.push_back(RawSlot{content, camera});
  });
  return raw;
}

// Descend nested-cell `composition_ref()` links from `comp` collecting the crumb path to `target`.
// Pushes each visited composition (with a display label — "Root" seeded by the caller, else the
// descent cell's kind id, or "Nested" when unresolvable) as it goes and pops on a dead branch, so
// `out` ends holding exactly `[comp … target]` on success and is left unchanged on failure. Pure
// display, never a behavior branch on the kind (A16). Cycle-free by construction over a DAG.
struct PathBuilder {
  const arbc::Document& document;
  const arbc::DocRoot& state;
  const arbc::KindBridge& bridge;
  arbc::ObjectId target;
  std::vector<Breadcrumb>& out;

  bool descend(arbc::ObjectId comp, std::string label) {
    out.push_back(Breadcrumb{comp, std::move(label)});
    if (comp == target) {
      return true;
    }
    bool found = false;
    state.for_each_layer_in(comp, [&](arbc::ObjectId layer_id) {
      if (found) {
        return;
      }
      const arbc::LayerRecord* layer = state.find_layer(layer_id);
      if (layer == nullptr || !layer->content.valid()) {
        return;
      }
      arbc::Content* content = document.resolve(layer->content);
      if (content == nullptr) {
        return;
      }
      const arbc::ObjectId child = content->composition_ref();
      if (!child.valid()) {
        return;
      }
      std::string child_label = "Nested";
      if (const arbc::ContentRecord* rec = state.find_content(layer->content); rec != nullptr) {
        std::string_view id;
        std::string_view version;
        if (bridge.lookup(rec->kind, id, version) && !id.empty()) {
          child_label = std::string(id);
        }
      }
      if (descend(child, std::move(child_label))) {
        found = true;
      }
    });
    if (!found) {
      out.pop_back();
    }
    return found;
  }
};

} // namespace

bool set_cell_opacity(arbc::Document& document, const arbc::Registry& /*registry*/,
                      arbc::ObjectId cell, double opacity) {
  // Reject a camera's content up front — the `set_camera_resolution` no-op-returning-false
  // mould (Constraint 3). `resolve` is null for an unknown id and the cast is null for a
  // non-camera, so a genuine cell falls through to the placing-layer lookup.
  if (dynamic_cast<CameraContent*>(document.resolve(cell)) != nullptr) {
    return false;
  }
  const arbc::ObjectId layer = placing_layer(document, cell);
  if (!layer.valid()) {
    return false; // an unresolvable / unplaced content: a no-op that mutates nothing
  }
  // Straight-alpha, clamped to [0,1] (D10). ONE transaction: the cell keeps its `ObjectId`,
  // placement and order — only its layer opacity changes — so the shared selection and `undo`
  // hold on the same object. Never a direct `Document` mutation from L4 (D-inspector-2).
  const double clamped = std::clamp(opacity, 0.0, 1.0);
  auto txn = document.transact("set_cell_opacity");
  txn.set_opacity(layer, clamped);
  txn.commit();
  return true;
}

bool set_cell_visible(arbc::Document& document, const arbc::Registry& /*registry*/,
                      arbc::ObjectId cell, bool visible) {
  if (dynamic_cast<CameraContent*>(document.resolve(cell)) != nullptr) {
    return false;
  }
  const arbc::ObjectId layer = placing_layer(document, cell);
  if (!layer.valid()) {
    return false;
  }
  auto txn = document.transact("set_cell_visible");
  txn.set_visible(layer, visible);
  txn.commit();
  return true;
}

bool brush_dab(arbc::Document& document, const arbc::Registry& /*registry*/, arbc::ObjectId cell,
               std::span<const arbc::Vec2> centers, double inner_radius, double outer_radius,
               const arbc::WorkingPixel& color, std::uint64_t coalesce_key) {
  // Nothing to paint / a degenerate dab: a no-op that opens NO transaction (the
  // set_cell_opacity discipline — a false return, never a half-open edit).
  if (centers.empty() || !(outer_radius > 0.0) || !std::isfinite(outer_radius) ||
      !std::isfinite(inner_radius)) {
    return false;
  }
  arbc::Content* content = document.resolve(cell);
  if (content == nullptr) {
    return false; // unknown id
  }
  // Reject a camera up front (the set_cell_opacity mould), then require the GENERIC editable
  // facet — `editable()` non-null IS the PaintedRaster test `classify_detail` uses (A16), never
  // a `kind_id` switch. The concrete `RasterContent` is only reached to drive `paint`.
  if (dynamic_cast<CameraContent*>(content) != nullptr || content->editable() == nullptr) {
    return false;
  }
  auto* raster = dynamic_cast<arbc::RasterContent*>(content);
  if (raster == nullptr) {
    return false; // editable but not a raster we can paint
  }
  // ONE transaction for every dab in this call, stamped with the gesture key so a stroke of
  // many per-frame calls sharing one key folds to ONE journal entry (D15). Each dab copies only
  // its touched tiles (CoW, O(touched tiles)) and adds its damage to `txn` (raster_content.hpp).
  auto txn = document.transact("brush");
  txn.coalesce(coalesce_key);
  const double inner = std::clamp(inner_radius, 0.0, outer_radius);
  for (const arbc::Vec2& c : centers) {
    if (!std::isfinite(c.x) || !std::isfinite(c.y)) {
      continue; // skip a non-finite center rather than write a NaN region
    }
    const arbc::Rect region{c.x - outer_radius, c.y - outer_radius, c.x + outer_radius,
                            c.y + outer_radius};
    const arbc::CoverageSampler coverage = arbc::round_dab(c.x, c.y, inner, outer_radius, 1.0F);
    raster->paint(txn, cell, region, color, coverage);
  }
  txn.commit();
  return true;
}

std::vector<Breadcrumb> composition_path(const arbc::Document& document,
                                         const arbc::Registry& registry,
                                         std::optional<arbc::ObjectId> entered) {
  std::vector<Breadcrumb> result;
  arbc::KindBridge bridge;
  project::seed_kind_bridge(bridge, registry);

  const arbc::DocStatePtr state = document.pin();
  if (!state) {
    return result;
  }
  const arbc::ObjectId root = root_composition(*state);
  if (!root.valid()) {
    return result; // no composition at all — no breadcrumb
  }
  const arbc::ObjectId target = (entered && entered->valid()) ? *entered : root;
  PathBuilder builder{document, *state, bridge, target, result};
  if (!builder.descend(root, "Root")) {
    // Fail-safe (Constraint 9): the entered composition is not reachable from Root (GC'd, undone
    // away, or a foreign id) — the path degrades to Root only, exactly as `active_composition`
    // does.
    result.clear();
    result.push_back(Breadcrumb{root, "Root"});
  }
  return result;
}

MembershipMove list_drag_to_membership(int list_from, int list_to, int count) {
  MembershipMove move;
  if (count <= 0) {
    return move;
  }
  const int last = count - 1;
  const int from = std::clamp(list_from, 0, last);
  const int to = std::clamp(list_to, 0, last);
  // The layers section is `cells()` REVERSED (slot 0 = frontmost = top of z), so a front→back list
  // slot maps to a bottom→top membership index by `count-1-slot` — its own inverse (Constraint 2).
  move.from_index = static_cast<std::uint32_t>(last - from);
  move.to_index = static_cast<std::uint32_t>(last - to);
  return move;
}

bool reorder_cell(arbc::Document& document, arbc::ObjectId composition, arbc::ObjectId moved,
                  std::uint32_t to_index) {
  if (!composition.valid() || !moved.valid()) {
    return false;
  }
  // Reject a camera's content up front — cameras carry no z-order (A14); the
  // `set_camera_resolution`/`set_cell_*` no-op-returning-false mould (Constraint 3).
  if (dynamic_cast<CameraContent*>(document.resolve(moved)) != nullptr) {
    return false;
  }
  const arbc::DocStatePtr state = document.pin();
  if (!state || state->find_composition(composition) == nullptr) {
    return false;
  }
  const std::vector<RawSlot> raw = raw_membership(document, *state, composition);

  // `moved`'s raw index and its current CELLS-SPACE index (cameras excluded), plus the cells count.
  std::uint32_t from_raw = 0;
  std::uint32_t moved_cells_index = 0;
  std::uint32_t cells_count = 0;
  bool found = false;
  for (std::uint32_t j = 0; j < raw.size(); ++j) {
    if (raw[j].content == moved && !found) {
      from_raw = j;
      moved_cells_index = cells_count;
      found = true;
    }
    if (!raw[j].is_camera) {
      ++cells_count;
    }
  }
  if (!found) {
    return false; // `moved` is not a member of `composition` (e.g. a cell from a different scope)
  }
  if (to_index >= cells_count || to_index == moved_cells_index) {
    return false; // out-of-range or already there: a no-op that opens NO transaction (Constraint 3)
  }

  // Translate the cells-space `to_index` to a raw insert index into the ERASED array (moved
  // removed) — the index `reorder_layer` (erase-at-from, insert-at-to) consumes. The insert point
  // is the first slot in the erased array with exactly `to_index` non-camera slots before it, so
  // among the cells `moved` lands at position `to_index` regardless of interleaved camera slots.
  std::uint32_t to_raw = 0;
  std::uint32_t cells_before = 0;
  bool placed = false;
  for (std::uint32_t j = 0; j < raw.size(); ++j) {
    if (j == from_raw) {
      continue; // the erased array excludes `moved`
    }
    if (cells_before == to_index) {
      // A slot's index in the erased array is `j` when it precedes `moved`, else `j - 1`.
      to_raw = (j < from_raw) ? j : j - 1U;
      placed = true;
      break;
    }
    if (!raw[j].is_camera) {
      ++cells_before;
    }
  }
  if (!placed) {
    to_raw = static_cast<std::uint32_t>(raw.size()) - 1U; // past the last cell (erased-array end)
  }

  // ONE transaction: the cell keeps its `ObjectId`, placement, and content — only its order in the
  // composition's membership changes — so the shared selection and `undo` hold on the same object.
  auto txn = document.transact("reorder_cell");
  txn.reorder_layer(composition, from_raw, to_raw);
  txn.commit();
  return true;
}

} // namespace ace::scene
