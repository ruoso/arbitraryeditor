#include <ace/project/project.hpp> // project::seed_kind_bridge
#include <ace/scene/camera.hpp>    // CameraContent::kind_id (the cells/cameras split)
#include <ace/scene/cell.hpp>

#include <arbc/contract/content.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/model/model.hpp>
#include <arbc/model/records.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ace::scene {
namespace {

// The three built-in config grammars the editor carries an adapter for
// (`builtin_kinds.cpp:169-207`). These are ENHANCEMENT keys, never a gate: every id
// not named here still gets a schema (the raw-config fallback) and is still
// insertable, which is the whole point of A16. Kept as local literals rather than
// including the concrete kind headers — `scene` names no arbc content type.
constexpr std::string_view k_raster_kind = "org.arbc.raster";
constexpr std::string_view k_solid_kind = "org.arbc.solid";
constexpr std::string_view k_nested_kind = "org.arbc.nested";

std::string_view trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
    text.remove_prefix(1);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
    text.remove_suffix(1);
  }
  return text;
}

// Full-consumption decimal parse (libarbc's own config parsers reject trailing
// garbage, so accepting it here would only defer the failure into the factory).
bool parse_positive_int(std::string_view text, long long& out) {
  const std::string_view body = trim(text);
  if (body.empty()) {
    return false;
  }
  long long value = 0;
  for (const char c : body) {
    if (c < '0' || c > '9') {
      return false;
    }
    if (value > (std::numeric_limits<int>::max() - (c - '0')) / 10) {
      return false; // overflows the extent range every arbc kind accepts
    }
    value = value * 10 + (c - '0');
  }
  if (value <= 0) {
    return false;
  }
  out = value;
  return true;
}

bool parse_number(std::string_view text, double& out) {
  const std::string_view body = trim(text);
  if (body.empty()) {
    return false;
  }
  const std::string owned(body);
  char* end = nullptr;
  out = std::strtod(owned.c_str(), &end);
  return end == owned.c_str() + owned.size() && std::isfinite(out);
}

std::vector<std::string_view> split(std::string_view text, char separator) {
  std::vector<std::string_view> parts;
  std::size_t start = 0;
  while (true) {
    const std::size_t at = text.find(separator, start);
    if (at == std::string_view::npos) {
      parts.push_back(text.substr(start));
      return parts;
    }
    parts.push_back(text.substr(start, at - start));
    start = at + 1;
  }
}

const std::string* find_value(const InsertValues& values, std::string_view field) {
  for (const std::pair<std::string, std::string>& entry : values) {
    if (entry.first == field) {
      return &entry.second;
    }
  }
  return nullptr;
}

// The `size` field's prefill: the root composition's own extent clamped to >= 1, or
// `1024x1024` when the composition is absent or degenerate (Constraint 8).
std::string size_initial(const std::optional<project::CompositionSize>& composition) {
  int width = k_fallback_resolution;
  int height = k_fallback_resolution;
  if (composition.has_value() && std::isfinite(composition->width) &&
      std::isfinite(composition->height)) {
    const auto clamp = [](double value) {
      if (!(value >= 1.0)) {
        return 1;
      }
      if (value >= static_cast<double>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
      }
      return static_cast<int>(value);
    };
    width = clamp(composition->width);
    height = clamp(composition->height);
  }
  return std::to_string(width) + "x" + std::to_string(height);
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

} // namespace

std::vector<KindInsertSchema> insert_schemas(const arbc::Registry& registry,
                                             std::optional<project::CompositionSize> composition) {
  std::vector<KindInsertSchema> schemas;
  const std::vector<std::string_view> ids = registry.ids();
  schemas.reserve(ids.size());
  // ONE entry per advertised id, unconditionally and in registration order. There is
  // deliberately no `continue` in this loop — a filter here is exactly the
  // hard-coded kind set `docs/00-design.md:505-511` forbids, and the unit suite
  // asserts `schemas.size() == ids.size()` for a registry carrying a kind the editor
  // has never seen.
  for (const std::string_view id : ids) {
    KindInsertSchema schema;
    schema.kind_id = std::string(id);
    const arbc::KindMetadata* metadata = registry.metadata(id);
    schema.human_name = (metadata != nullptr && !metadata->human_name.empty())
                            ? metadata->human_name
                            : schema.kind_id;
    if (id == k_raster_kind) {
      // Constraint 8: always rendered, always editable, prefilled from the
      // composition — the user specifies the resolution at insert.
      schema.fields.push_back(InsertField{"size", "Resolution (WxH)", InsertFieldType::Size,
                                          size_initial(composition)});
    } else if (id == k_solid_kind) {
      schema.fields.push_back(
          InsertField{"color", "Color (r,g,b,a)", InsertFieldType::Color, "0,0,0,1"});
    } else if (id == k_nested_kind) {
      schema.fields.push_back(
          InsertField{"child", "Child composition (ObjectId)", InsertFieldType::ObjectRef, ""});
    } else {
      // The fallback is a FIRST-CLASS path, not an error: the kind's own
      // `ContentConfig` grammar travels verbatim and its factory is the validator.
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
  const auto missing = [&schema](std::string_view field) {
    return arbc::unexpected<std::string>(schema.kind_id + ": missing value for \"" +
                                         std::string(field) + "\"");
  };
  if (schema.raw_config) {
    const std::string* raw = find_value(values, k_raw_config_field);
    if (raw == nullptr) {
      return missing(k_raw_config_field);
    }
    return *raw; // verbatim — the kind defines this grammar, not the editor
  }
  if (schema.kind_id == k_raster_kind) {
    const std::string* size = find_value(values, "size");
    if (size == nullptr) {
      return missing("size");
    }
    const std::vector<std::string_view> parts = split(*size, 'x');
    long long width = 0;
    long long height = 0;
    if (parts.size() != 2 || !parse_positive_int(parts[0], width) ||
        !parse_positive_int(parts[1], height)) {
      return arbc::unexpected<std::string>(
          schema.kind_id + ": resolution must be <width>x<height> with positive integers");
    }
    return std::to_string(width) + "x" + std::to_string(height);
  }
  if (schema.kind_id == k_solid_kind) {
    const std::string* color = find_value(values, "color");
    if (color == nullptr) {
      return missing("color");
    }
    const std::vector<std::string_view> parts = split(*color, ',');
    if (parts.size() != 4) {
      return arbc::unexpected<std::string>(schema.kind_id + ": color must be \"r,g,b,a\"");
    }
    std::string config;
    for (std::size_t i = 0; i < parts.size(); ++i) {
      double channel = 0.0;
      if (!parse_number(parts[i], channel)) {
        return arbc::unexpected<std::string>(schema.kind_id + ": color channel is not a number");
      }
      if (i != 0) {
        config += ',';
      }
      config += std::string(trim(parts[i]));
    }
    return config;
  }
  if (schema.kind_id == k_nested_kind) {
    const std::string* child = find_value(values, "child");
    if (child == nullptr) {
      return missing("child");
    }
    long long id = 0;
    if (!parse_positive_int(*child, id)) {
      return arbc::unexpected<std::string>(schema.kind_id +
                                           ": child must be a positive decimal ObjectId");
    }
    return std::to_string(id);
  }
  // A named-field schema with no adapter branch cannot arise from `insert_schemas`,
  // but a hand-built schema could; fail loudly rather than guessing a grammar.
  return arbc::unexpected<std::string>(schema.kind_id + ": no config grammar for this kind");
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
  // Two journal entries, exactly as `add_camera` (D-cells_model-7): `add_content`
  // self-commits because it is the only call that binds a `Content` vtable, then the
  // placing layer is added and attached in one further transaction.
  const arbc::ObjectId content = document.add_content(
      std::shared_ptr<arbc::Content>(std::move(*made)), cell_token(registry, kind_id));
  auto txn = document.transact("add_cell");
  const arbc::ObjectId layer = txn.add_layer(content, placement);
  txn.attach_layer(composition, layer);
  txn.commit();
  return content;
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
    if (const arbc::Content* content = document.resolve(layer->content); content != nullptr) {
      bounds = content->bounds();
    }
    // An unresolvable token surfaces with an empty kind_id rather than vanishing —
    // an unknown-passthrough cell is still a cell (D-cells_model-8).
    result.push_back(Cell{layer->content, layer_id, named ? std::string(id) : std::string(),
                          layer->transform, bounds});
  });
  return result;
}

} // namespace ace::scene
