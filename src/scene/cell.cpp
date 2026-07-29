#include <ace/project/project.hpp> // project::seed_kind_bridge
#include <ace/scene/camera.hpp>    // CameraContent::kind_id (the cells/cameras split)
#include <ace/scene/cell.hpp>

#include <arbc/contract/content.hpp>
#include <arbc/contract/registry.hpp>
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
CellDetail classify_detail(arbc::Content& content, const std::optional<arbc::Rect>& bounds) {
  CellDetail detail;
  if (content.editable() != nullptr) {
    detail.source = DetailSource::PaintedRaster;
  } else if (!content.external_asset_ref().empty()) {
    detail.source = DetailSource::ReferencedImage;
  } else {
    return detail; // ResolutionIndependent (the default): no native floor, no native px
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
         std::string_view config, const arbc::Affine& placement) {
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
    composition = root_composition(*state);
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

std::size_t remove_cells(arbc::Document& document, std::span<const CellRemoval> removals) {
  if (removals.empty()) {
    return 0; // an empty batch is a no-op that publishes nothing (Constraint 2)
  }
  // Resolve + validate every removal against the live pinned generation BEFORE opening
  // anything, so a wholly-stale batch costs exactly one snapshot read and leaves the
  // document byte-for-byte untouched (Constraint 5). The root composition is resolved ONCE
  // here (root-only, remove Constraint 12 / D-one_action_one_entry-3): every removal names it.
  std::vector<arbc::Document::Removal> validated;
  validated.reserve(removals.size());
  {
    const arbc::DocStatePtr state = document.pin();
    if (!state) {
      return 0;
    }
    const arbc::ObjectId composition = root_composition(*state);
    if (!composition.valid()) {
      return 0; // no root composition to remove from
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
        continue; // live layer, but not in the root composition (a nested scope's)
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
  std::vector<Cell> result;
  // The reverse map: seeded from the same registry the insert side interned through,
  // so a token minted by `add_cell` (or by `save`/`open`) names the same kind here.
  arbc::KindBridge bridge;
  project::seed_kind_bridge(bridge, registry);

  const arbc::DocStatePtr state = document.pin();
  if (!state) {
    return result;
  }
  arbc::ObjectId root_id;
  const arbc::CompositionRecord* root_rec = nullptr;
  if (!state->find_first_composition(root_id, root_rec) || root_rec == nullptr) {
    return result; // no composition — no cells
  }
  state->for_each_layer_in(root_id, [&](arbc::ObjectId layer_id) {
    const arbc::LayerRecord* layer = state->find_layer(layer_id);
    if (layer == nullptr || !layer->content.valid()) {
      return;
    }
    const arbc::ContentRecord* record = state->find_content(layer->content);
    if (record == nullptr) {
      return;
    }
    std::string_view id;
    std::string_view version;
    const bool named = bridge.lookup(record->kind, id, version);
    if (named && id == CameraContent::kind_id) {
      return; // a camera is a scene object, but it is not a cell (A14)
    }
    // The live extent, off the SAME pinned generation as the placement (D-selection-11).
    // `Document::resolve` is a lock-free pinned read of the copy-on-write binding table, so
    // this adds no lock and no second walk; an unbound id (or an unbounded kind) is `nullopt`,
    // which is the honest "covers the whole plane" answer, not an error.
    std::optional<arbc::Rect> bounds;
    CellDetail detail;
    if (arbc::Content* content = document.resolve(layer->content); content != nullptr) {
      bounds = content->bounds();
      detail = classify_detail(*content, bounds);
    }
    // An unresolvable token surfaces with an empty kind_id rather than vanishing —
    // an unknown-passthrough cell is still a cell (D-cells_model-8). Opacity/visibility ride
    // out of the same LayerRecord so the inspector's Appearance block reads them off this walk.
    result.push_back(Cell{layer->content, layer_id, named ? std::string(id) : std::string(),
                          layer->transform, bounds, detail, layer->opacity, layer->visible()});
  });
  return result;
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

} // namespace ace::scene
