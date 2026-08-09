// editor.cells.model — L1 headless Catch2 units for inserting a cell of ANY
// registered kind through the `arbc::Registry` seam (A16, D-cells_model-1..9). The
// load-bearing property is the ABSENCE of a kind allowlist: `scene::insert_schemas`
// emits one entry per `registry.ids()` entry unconditionally, and a kind the editor
// has never seen inserts end-to-end through the raw-config fallback. The fields, types,
// defaults and units come from each kind's OWN advertised `arbc::KindInsertSchema`
// (issue #21) — the editor holds no per-kind grammar — and `build_config` delegates to
// the kind's `assemble` thunk, so validation is the factory's (surfaced at `add_cell`).
// Also covers the bounded default solid (issue #22), the editor's own camera schema,
// `scene::add_cell`'s failure-mutates-nothing contract, the cells/cameras split, the
// one-entry journal create, and the `project.arbc` roundtrip that needs no new kind
// registration. Plus the one `render_offline` golden (a nested cell at a computed
// placement) and its byte-equality companions.
//
// No ImGui/GL/SDL (Constraint 1); runs under the ASan/TSan legs (A4/§9).

#include <ace/commands/app_state.hpp>
#include <ace/commands/cells.hpp>
#include <ace/interact/interact.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/project/save.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/expected.hpp>
#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/content.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/model/model.hpp>
#include <arbc/model/records.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "golden_support.hpp"

using ace::commands::AppState;
using ace::commands::Command;
using ace::commands::dispatch;
using ace::scene::Cell;
using ace::scene::InsertValues;
using ace::scene::KindInsertSchema;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_cell_model_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// A kind the EDITOR has never heard of — the probe that proves the enumeration is
// registry-driven rather than a hard-coded set. Its config grammar is "anything
// non-empty is a colour name we ignore"; the point is only that the editor cannot
// know it, so it must arrive through the raw-config fallback.
constexpr const char* k_probe_kind = "org.example.probe";

void register_probe_kind(arbc::Registry& registry) {
  arbc::ContentFactory factory = [](arbc::ContentConfig config)
      -> arbc::expected<std::unique_ptr<arbc::Content>, std::string> {
    if (config.empty()) {
      return arbc::unexpected<std::string>("org.example.probe: config must not be empty");
    }
    return std::unique_ptr<arbc::Content>(
        std::make_unique<arbc::SolidContent>(arbc::Rgba{0.0F, 0.0F, 0.5F, 1.0F}));
  };
  (void)registry.add(k_probe_kind, std::move(factory), arbc::KindMetadata{"Probe", "1"});
}

// A foreign kind that DOES advertise a library `KindInsertSchema` (issue #21) — the
// counterpart to `register_probe_kind`'s no-schema kind. Its schema carries a `Text`
// field and a `Number` field so the editor mirrors named fields (and their display
// types) for a kind it has never heard of, purely from the library seam.
constexpr const char* k_schema_probe_kind = "org.example.schema_probe";

void register_schema_probe_kind(arbc::Registry& registry) {
  arbc::ContentFactory factory =
      [](arbc::ContentConfig) -> arbc::expected<std::unique_ptr<arbc::Content>, std::string> {
    return std::unique_ptr<arbc::Content>(
        std::make_unique<arbc::SolidContent>(arbc::Rgba{0.0F, 0.5F, 0.0F, 1.0F}));
  };
  arbc::KindInsertSchema schema;
  schema.fields = {
      arbc::KindInsertField{"label", arbc::KindInsertField::Type::Text, "hi", {}, {}, {}},
      arbc::KindInsertField{"gain", arbc::KindInsertField::Type::Number, "2", {}, {}, "dB"}};
  schema.assemble =
      [](std::span<const std::string> values) -> arbc::expected<std::string, std::string> {
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) {
      if (i != 0) {
        out.push_back('|');
      }
      out.append(values[i]);
    }
    return out;
  };
  (void)registry.add(k_schema_probe_kind, std::move(factory),
                     arbc::KindMetadata{"Schema Probe", "1"}, /*codec=*/std::nullopt,
                     /*binder=*/std::nullopt, /*state_walker=*/std::nullopt, std::move(schema));
}

// --- Provenance-classification probes (editor.cells.resolution; D-resolution-2) ---
// Two finite-detail kinds the EDITOR has never heard of, exercising the two D11 axes the
// classification reads GENERICALLY — never a `kind_id` allowlist (A16). `org.arbc.image`
// (the real ReferencedImage kind) is not linked into this binary, so these stand-ins pin
// the generic-facet property without the plugin.

// Bytes BORROWED from an external file: overrides `external_asset_ref()` non-empty and
// leaves `editable()` at the null default (exactly like `org.arbc.image`) — so it must
// classify ReferencedImage. Its `bounds()` is the decoded master extent, the native grid.
class ExternalRefProbe final : public arbc::Content {
public:
  std::optional<arbc::Rect> bounds() const override { return arbc::Rect{0.0, 0.0, 64.0, 48.0}; }
  arbc::Stability stability() const override { return arbc::Stability::Static; }
  std::optional<arbc::TimeRange> time_extent() const override { return std::nullopt; }
  std::optional<arbc::RenderResult> render(const arbc::RenderRequest&,
                                           std::shared_ptr<arbc::RenderCompletion>) override {
    return std::nullopt;
  }
  std::string_view external_asset_ref() const override { return d_uri; }

private:
  std::string d_uri = "file:///borrowed/photo.png";
};

constexpr const char* k_external_probe_kind = "org.example.external_probe";

void register_external_probe_kind(arbc::Registry& registry) {
  arbc::ContentFactory factory =
      [](arbc::ContentConfig) -> arbc::expected<std::unique_ptr<arbc::Content>, std::string> {
    return std::unique_ptr<arbc::Content>(std::make_unique<ExternalRefProbe>());
  };
  (void)registry.add(k_external_probe_kind, std::move(factory),
                     arbc::KindMetadata{"External Probe", "1"});
}

// OWNS a mutable pixel grid: returns a non-null `editable()` (itself) like `org.arbc.raster`,
// under a kind id the editor cannot know — so it must classify PaintedRaster through the
// GENERIC `editable()` facet, not a `kind_id == "org.arbc.raster"` switch. The `arbc::Editable`
// methods are stubs; this leaf ships no grid mutation, so they are never exercised
// (D-resolution-5).
class EditableProbe final : public arbc::Content, public arbc::Editable {
public:
  std::optional<arbc::Rect> bounds() const override { return arbc::Rect{0.0, 0.0, 40.0, 30.0}; }
  arbc::Stability stability() const override { return arbc::Stability::Static; }
  std::optional<arbc::TimeRange> time_extent() const override { return std::nullopt; }
  std::optional<arbc::RenderResult> render(const arbc::RenderRequest&,
                                           std::shared_ptr<arbc::RenderCompletion>) override {
    return std::nullopt;
  }
  arbc::Editable* editable() override { return this; }

  arbc::StateHandle capture() override { return {}; }
  void restore(arbc::StateHandle) override {}
  std::size_t state_cost(arbc::StateHandle) const override { return 0; }
  void retain(arbc::StateHandle) override {}
  void release(arbc::StateHandle) override {}
};

constexpr const char* k_editable_probe_kind = "org.example.editable_probe";

void register_editable_probe_kind(arbc::Registry& registry) {
  arbc::ContentFactory factory =
      [](arbc::ContentConfig) -> arbc::expected<std::unique_ptr<arbc::Content>, std::string> {
    return std::unique_ptr<arbc::Content>(std::make_unique<EditableProbe>());
  };
  (void)registry.add(k_editable_probe_kind, std::move(factory),
                     arbc::KindMetadata{"Editable Probe", "1"});
}

// A composition-WRAPPING kind the EDITOR has never heard of (editor.cells.objectid_field_picker /
// D-objectid_field_picker-1): it overrides the generic `composition_ref()` facet to name a child
// composition, exactly as `org.arbc.nested` does, but under a foreign kind id. So
// `composition_options` must reach it through the FACET, never a `kind_id == "org.arbc.nested"`
// switch (A16 witness) — the same discipline `nested_composition_of` holds. Its config grammar is a
// bare decimal child ObjectId, mirroring `make_nested`.
class CompositionProbe final : public arbc::Content {
public:
  explicit CompositionProbe(arbc::ObjectId child) : d_child(child) {}
  std::optional<arbc::Rect> bounds() const override { return std::nullopt; }
  arbc::Stability stability() const override { return arbc::Stability::Static; }
  std::optional<arbc::TimeRange> time_extent() const override { return std::nullopt; }
  std::optional<arbc::RenderResult> render(const arbc::RenderRequest&,
                                           std::shared_ptr<arbc::RenderCompletion>) override {
    return std::nullopt;
  }
  arbc::ObjectId composition_ref() const override { return d_child; }

private:
  arbc::ObjectId d_child;
};

constexpr const char* k_composition_probe_kind = "org.example.wrapper";

void register_composition_probe_kind(arbc::Registry& registry) {
  arbc::ContentFactory factory = [](arbc::ContentConfig config)
      -> arbc::expected<std::unique_ptr<arbc::Content>, std::string> {
    std::uint64_t id = 0;
    const auto [ptr, ec] = std::from_chars(config.data(), config.data() + config.size(), id);
    if (ec != std::errc{} || id == 0) {
      return arbc::unexpected<std::string>("org.example.wrapper: expected a decimal ObjectId");
    }
    return std::unique_ptr<arbc::Content>(std::make_unique<CompositionProbe>(arbc::ObjectId{id}));
  };
  (void)registry.add(k_composition_probe_kind, std::move(factory),
                     arbc::KindMetadata{"Wrapper Probe", "1"});
}

// The editor's own registry seeding (mirrors `commands::register_editor_kinds`).
arbc::Registry cell_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  return registry;
}

arbc::Registry cell_registry_with_probe() {
  arbc::Registry registry = cell_registry();
  register_probe_kind(registry);        // a foreign kind WITHOUT a schema (raw-config)
  register_schema_probe_kind(registry); // a foreign kind WITH a library schema
  return registry;
}

arbc::Registry cell_registry_with_detail_probes() {
  arbc::Registry registry = cell_registry();
  register_external_probe_kind(registry); // an unknown ReferencedImage-shaped kind
  register_editable_probe_kind(registry); // an unknown PaintedRaster-shaped kind
  return registry;
}

// A fresh workspace-backed session with a root composition to place cells in.
AppState session_with_composition(const ScratchDir& scratch, const ace::platform::FileSystem& fs,
                                  const char* leaf) {
  auto created = ace::project::create_project(fs, scratch.root / leaf);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  dispatch(state, Command{"add_composition",
                          [](arbc::Document& doc) { doc.add_composition(64.0, 64.0); }});
  return state;
}

const KindInsertSchema& schema_for(const std::vector<KindInsertSchema>& schemas,
                                   std::string_view kind_id) {
  for (const KindInsertSchema& schema : schemas) {
    if (schema.kind_id == kind_id) {
      return schema;
    }
  }
  FAIL("no schema for " << kind_id);
  return schemas.front();
}

std::string config_for(const std::vector<KindInsertSchema>& schemas, std::string_view kind_id,
                       const InsertValues& values) {
  const arbc::expected<std::string, std::string> config =
      ace::scene::build_config(schema_for(schemas, kind_id), values);
  REQUIRE(config.has_value());
  return *config;
}

} // namespace

// --- No allowlist: the enumeration (Constraint 2 / D-cells_model-1) ----------

TEST_CASE("insert_schemas emits one entry per registered kind, in registration order") {
  const arbc::Registry registry = cell_registry_with_probe();
  const std::vector<std::string_view> ids = registry.ids();
  const std::vector<KindInsertSchema> schemas = ace::scene::insert_schemas(registry);

  // THE assertion that fails the moment someone adds a filter: one entry per id,
  // same order, no exceptions — not for the camera kind, not for the operator kinds
  // whose factories always fail, not for a kind the editor has never seen.
  REQUIRE(schemas.size() == ids.size());
  for (std::size_t i = 0; i < ids.size(); ++i) {
    CHECK(schemas[i].kind_id == std::string(ids[i]));
  }
  // The refusing operator kinds are OFFERED, not hidden — their own error message is
  // the UI (Constraint 3), which stays correct for future kinds automatically.
  CHECK(schema_for(schemas, "org.arbc.fade").kind_id == "org.arbc.fade");
  CHECK(schema_for(schemas, "org.arbc.crossfade").kind_id == "org.arbc.crossfade");

  // Human names come from the kind's own metadata, never an editor-side table.
  CHECK(schema_for(schemas, "org.arbc.raster").human_name == "Raster");
  CHECK(schema_for(schemas, k_probe_kind).human_name == "Probe");

  // The editor-unknown kind gets the raw-config fallback: a single free-text field
  // whose value travels to the kind's factory verbatim (the probe advertises no schema).
  const KindInsertSchema& probe = schema_for(schemas, k_probe_kind);
  CHECK(probe.raw_config);
  REQUIRE(probe.fields.size() == 1);
  CHECK(probe.fields[0].id == std::string(ace::scene::k_raw_config_field));
  CHECK(probe.fields[0].type == ace::scene::InsertFieldType::Text);

  // A schema-bearing kind emits its OWN advertised named fields, read back through
  // `Registry::insert_schema(id)` — never an editor adapter (D-insert_schema-1).
  const KindInsertSchema& raster = schema_for(schemas, "org.arbc.raster");
  CHECK_FALSE(raster.raw_config);
  REQUIRE(raster.fields.size() == 2);
  CHECK(raster.fields[0].id == "width");
  CHECK(raster.fields[1].id == "height");
  CHECK(raster.fields[0].type == ace::scene::InsertFieldType::Integer);

  // A FOREIGN kind the editor has never seen, but which DOES advertise a library schema,
  // gets its named fields and display types mirrored automatically — the plugin seam
  // working as designed (Acceptance 1). Both the `Text` and `Number` display hints map.
  const KindInsertSchema& schema_probe = schema_for(schemas, k_schema_probe_kind);
  CHECK_FALSE(schema_probe.raw_config);
  REQUIRE(schema_probe.fields.size() == 2);
  CHECK(schema_probe.fields[0].id == "label");
  CHECK(schema_probe.fields[0].type == ace::scene::InsertFieldType::Text);
  CHECK(schema_probe.fields[1].id == "gain");
  CHECK(schema_probe.fields[1].label == "gain (dB)"); // unit composed into the label
  CHECK(schema_probe.fields[1].type == ace::scene::InsertFieldType::Number);
  // Its own assembler builds the config — the editor never learns the '|' separator.
  CHECK(config_for(schemas, k_schema_probe_kind, InsertValues{{"label", "hi"}, {"gain", "3"}}) ==
        "hi|3");

  // v0.5.0 (issue #33): `org.arbc.nested` now advertises a LIBRARY schema — a single LABELLED
  // `child` field of type `ObjectId` (min 1, no default), no longer the raw-config box it fell to
  // at v0.4.0. This is the property the parked item closes, and it exercises the NEW
  // `map_field_type` `ObjectId` arm this pin adds: the library `KindInsertField::Type::ObjectId`
  // maps to `InsertFieldType::ObjectId` rather than silently collapsing to `Text`, so the one bit
  // the type carries survives. Rendering it as a composition picker is a later leaf
  // (editor.cells.objectid_field_picker) — here it is a labelled field, which is the whole point.
  const KindInsertSchema& nested = schema_for(schemas, "org.arbc.nested");
  CHECK_FALSE(nested.raw_config);
  REQUIRE(nested.fields.size() == 1);
  CHECK(nested.fields[0].id == "child");
  CHECK(nested.fields[0].label == "child"); // labelled — no unit, so the bare field name
  CHECK(nested.fields[0].type == ace::scene::InsertFieldType::ObjectId);
  CHECK(nested.fields[0].initial.empty()); // no default: there is no child id that is valid empty
}

TEST_CASE("a kind the editor has never seen inserts end-to-end through the fallback") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "probe_kind");
  register_probe_kind(state.registry());

  const std::vector<KindInsertSchema> schemas = ace::scene::insert_schemas(state.registry());
  const std::string config =
      config_for(schemas, k_probe_kind, InsertValues{{"config", "anything-at-all"}});
  CHECK(config == "anything-at-all"); // verbatim — the kind owns its grammar

  const arbc::expected<arbc::ObjectId, std::string> inserted = ace::scene::add_cell(
      state.document(), state.registry(), k_probe_kind, config, arbc::Affine::identity());
  REQUIRE(inserted.has_value());

  const std::vector<Cell> cells = ace::scene::cells(state.document(), state.registry());
  REQUIRE(cells.size() == 1);
  CHECK(cells[0].kind_id == k_probe_kind);
  CHECK(cells[0].id == *inserted);

  // Its own factory is the validator — the editor never second-guesses the grammar.
  const arbc::expected<arbc::ObjectId, std::string> refused = ace::scene::add_cell(
      state.document(), state.registry(), k_probe_kind, "", arbc::Affine::identity());
  REQUIRE_FALSE(refused.has_value());
  CHECK(refused.error() == "org.example.probe: config must not be empty");
}

// --- build_config delegates to the kind's own assemble (D-insert_schema-2/3) --

TEST_CASE("build_config delegates to the kind's own assemble — the editor owns no grammar") {
  const arbc::Registry registry = cell_registry_with_probe();
  const std::vector<KindInsertSchema> schemas = ace::scene::insert_schemas(registry);

  // Solid's 8 fields are comma-joined BY THE KIND (issue #21); the editor never learns
  // the separator and hands the values back in field order (D-insert_schema-2/3).
  CHECK(config_for(schemas, "org.arbc.solid",
                   InsertValues{{"red", "0.5"},
                                {"green", "0"},
                                {"blue", "0"},
                                {"alpha", "1"},
                                {"x", "0"},
                                {"y", "0"},
                                {"width", "256"},
                                {"height", "256"}}) == "0.5,0,0,1,0,0,256,256");
  // Raster's two fields are x-joined by the kind.
  CHECK(config_for(schemas, "org.arbc.raster",
                   InsertValues{{"width", "1024"}, {"height", "768"}}) == "1024x768");
  // `assemble` only JOINS — it never trims, because normalisation was editor grammar
  // knowledge and that is exactly what this leaf retired. Whitespace travels verbatim to
  // the kind's factory, the single validator.
  CHECK(config_for(schemas, "org.arbc.raster",
                   InsertValues{{"width", " 1024 "}, {"height", " 768 "}}) == " 1024 x 768 ");
  // Nested advertises a library schema as of v0.5.0 (issue #33): its one LABELLED `child`
  // ObjectId field is no longer the raw-config fallback, and its OWN assembler joins the single
  // collected value verbatim (the editor never learns the ',' separator).
  CHECK_FALSE(schema_for(schemas, "org.arbc.nested").raw_config);
  CHECK(config_for(schemas, "org.arbc.nested", InsertValues{{"child", "7"}}) == "7");
  // The editor-unknown probe's fallback passes its one field through untouched.
  CHECK(config_for(schemas, k_probe_kind, InsertValues{{"config", "  raw , stuff "}}) ==
        "  raw , stuff ");
  // THE headline property: a kind the editor NEVER wrote an adapter for (tone) still gets
  // its named fields and a working assemble, with zero editor code per kind.
  CHECK(config_for(schemas, "org.arbc.tone",
                   InsertValues{{"frequency", "440"}, {"amplitude", "0.5"}}) == "440,0.5");

  // `build_config` no longer parses or rejects a VALUE (D-insert_schema-2): a malformed
  // value is not caught here — it is the factory's job now, asserted at `add_cell` below.
  // The one editor-side error is a value the caller never collected (a short vector).
  const arbc::expected<std::string, std::string> missing = ace::scene::build_config(
      schema_for(schemas, "org.arbc.raster"), InsertValues{{"width", "8"}});
  CHECK_FALSE(missing.has_value());
}

TEST_CASE("library-backed field mapping mirrors each kind's advertised schema") {
  const arbc::Registry registry = cell_registry();
  const std::vector<KindInsertSchema> schemas = ace::scene::insert_schemas(registry);

  // Solid: 8 fields, defaults 1,1,1,1,0,0,256,256, `px` units on the extent fields
  // (issue #22's placeable-by-default solid). Names and order are the kind's own.
  const KindInsertSchema& solid = schema_for(schemas, "org.arbc.solid");
  CHECK_FALSE(solid.raw_config);
  REQUIRE(solid.fields.size() == 8);
  const char* const names[] = {"red", "green", "blue", "alpha", "x", "y", "width", "height"};
  const char* const defaults[] = {"1", "1", "1", "1", "0", "0", "256", "256"};
  for (std::size_t i = 0; i < 8; ++i) {
    CHECK(solid.fields[i].id == names[i]);
    CHECK(solid.fields[i].initial == defaults[i]);
  }
  // A unit is composed into the LABEL; the id stays the bare field name (Constraint 5).
  CHECK(solid.fields[0].label == "red"); // no unit -> bare name
  CHECK(solid.fields[4].label == "x (px)");
  CHECK(solid.fields[6].label == "width (px)");
  CHECK(solid.fields[0].type == ace::scene::InsertFieldType::Number);

  // Raster: width,height Integer, default 1024 — the kind's OWN default, not a
  // composition-matched prefill (D-insert_schema-6).
  const KindInsertSchema& raster = schema_for(schemas, "org.arbc.raster");
  REQUIRE(raster.fields.size() == 2);
  CHECK(raster.fields[0].id == "width");
  CHECK(raster.fields[1].id == "height");
  CHECK(raster.fields[0].initial == "1024");
  CHECK(raster.fields[1].initial == "1024");
  CHECK(raster.fields[0].type == ace::scene::InsertFieldType::Integer);

  // Tone: a kind the editor NEVER wrote an adapter for now gets named fields
  // automatically — the whole point of reading the kind's own schema.
  const KindInsertSchema& tone = schema_for(schemas, "org.arbc.tone");
  CHECK_FALSE(tone.raw_config);
  REQUIRE(tone.fields.size() == 2);
  CHECK(tone.fields[0].id == "frequency");
  CHECK(tone.fields[1].id == "amplitude");
  CHECK(tone.fields[0].label == "frequency (Hz)");

  // Camera: the editor's OWN kind advertises a ZERO-field, non-raw_config schema
  // (D-insert_schema-4) — the editor practises the no-allowlist rule it enforces on
  // everyone else, rather than showing a config box for a config-ignoring kind.
  CHECK(registry.insert_schema(ace::scene::CameraContent::kind_id) != nullptr);
  const KindInsertSchema& camera = schema_for(schemas, ace::scene::CameraContent::kind_id);
  CHECK_FALSE(camera.raw_config);
  CHECK(camera.fields.empty());
  const arbc::expected<std::string, std::string> empty =
      ace::scene::build_config(camera, InsertValues{});
  REQUIRE(empty.has_value());
  CHECK(empty->empty());
}

// --- add_cell / cells() (Constraint 3/9) -------------------------------------

TEST_CASE("add_cell mints solid, raster and nested cells that cells() reads back in z-order") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "happy_path");
  const std::vector<KindInsertSchema> schemas = ace::scene::insert_schemas(state.registry());

  // A child composition for the nested cell to embed.
  arbc::ObjectId child;
  dispatch(state, Command{"add_child", [&child](arbc::Document& doc) {
                            child = doc.add_composition(32.0, 32.0);
                          }});
  REQUIRE(child.valid());

  const arbc::Affine solid_at = arbc::Affine::identity();
  const arbc::Affine raster_at = arbc::Affine::translation(5.0, 7.0);
  const arbc::Affine nested_at{2.0, 0.0, 0.0, 2.0, 1.0, 3.0};

  REQUIRE(ace::scene::add_cell(state.document(), state.registry(), "org.arbc.solid",
                               config_for(schemas, "org.arbc.solid",
                                          InsertValues{{"red", "0.5"},
                                                       {"green", "0"},
                                                       {"blue", "0"},
                                                       {"alpha", "1"},
                                                       {"x", "0"},
                                                       {"y", "0"},
                                                       {"width", "256"},
                                                       {"height", "256"}}),
                               solid_at)
              .has_value());
  REQUIRE(ace::scene::add_cell(state.document(), state.registry(), "org.arbc.raster",
                               config_for(schemas, "org.arbc.raster",
                                          InsertValues{{"width", "16"}, {"height", "24"}}),
                               raster_at)
              .has_value());
  REQUIRE(ace::scene::add_cell(state.document(), state.registry(), "org.arbc.nested",
                               config_for(schemas, "org.arbc.nested",
                                          InsertValues{{"config", std::to_string(child.value)}}),
                               nested_at)
              .has_value());

  // Bottom-to-top membership order == insertion order (Constraint: z-order).
  const std::vector<Cell> cells = ace::scene::cells(state.document(), state.registry());
  REQUIRE(cells.size() == 3);
  CHECK(cells[0].kind_id == "org.arbc.solid");
  CHECK(cells[1].kind_id == "org.arbc.raster");
  CHECK(cells[2].kind_id == "org.arbc.nested");
  CHECK(cells[0].placement == solid_at);
  CHECK(cells[1].placement == raster_at);
  CHECK(cells[2].placement == nested_at);
  for (const Cell& cell : cells) {
    CHECK(cell.id.valid());
    CHECK(cell.layer.valid());
    CHECK(cell.id != cell.layer);
  }
}

// --- ObjectId field picker: composition enumeration (editor.cells.objectid_field_picker) --------

TEST_CASE("composition_options enumerates sub-compositions and excludes the root") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "options");

  // A single-composition document has NOTHING to nest — an empty picker, never a text fallback
  // (D-objectid_field_picker-5).
  CHECK(ace::scene::composition_options(state.document(), state.registry()).empty());

  // Add one sub-composition and a nested cell in Root that wraps it.
  arbc::ObjectId child;
  dispatch(state, Command{"add_child", [&child](arbc::Document& doc) {
                            child = doc.add_composition(32.0, 32.0);
                          }});
  REQUIRE(child.valid());
  REQUIRE(ace::scene::add_cell(state.document(), state.registry(), "org.arbc.nested",
                               std::to_string(child.value), arbc::Affine::identity())
              .has_value());

  const std::vector<ace::scene::CompositionOption> options =
      ace::scene::composition_options(state.document(), state.registry());
  // Exactly the ONE sub-composition — Root itself is excluded (D-objectid_field_picker-4) — with
  // the child's canonical decimal as the collected value and a non-empty display label (A16,
  // display only). The value is EXACTLY what a hand-typed decimal produces (Constraint 3).
  REQUIRE(options.size() == 1);
  CHECK(options[0].value == std::to_string(child.value));
  CHECK_FALSE(options[0].label.empty());
}

TEST_CASE("composition_options deduplicates when two cells wrap the same sub-composition") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "dedup");

  arbc::ObjectId child;
  dispatch(state, Command{"add_child", [&child](arbc::Document& doc) {
                            child = doc.add_composition(32.0, 32.0);
                          }});
  REQUIRE(child.valid());
  // TWO nested cells naming the SAME child composition.
  REQUIRE(ace::scene::add_cell(state.document(), state.registry(), "org.arbc.nested",
                               std::to_string(child.value), arbc::Affine::identity())
              .has_value());
  REQUIRE(ace::scene::add_cell(state.document(), state.registry(), "org.arbc.nested",
                               std::to_string(child.value), arbc::Affine::translation(4.0, 4.0))
              .has_value());

  // Deduped by composition id: ONE option for the one distinct sub-composition.
  const std::vector<ace::scene::CompositionOption> options =
      ace::scene::composition_options(state.document(), state.registry());
  REQUIRE(options.size() == 1);
  CHECK(options[0].value == std::to_string(child.value));
}

TEST_CASE("composition_options discovers an editor-unknown wrapping kind (A16 witness)") {
  arbc::Registry registry = cell_registry();
  register_composition_probe_kind(registry);
  arbc::Document doc;
  doc.add_composition(64.0, 64.0);                              // Root
  const arbc::ObjectId child = doc.add_composition(32.0, 32.0); // the sub-composition to enumerate
  REQUIRE(child.valid());

  // Wrap the child with a cell of the FOREIGN kind whose only claim to nestedness is its generic
  // `composition_ref()` override — no `org.arbc.nested` involved. Discovery branches on the facet,
  // not the kind id, so the foreign wrapper is enumerated all the same.
  REQUIRE(ace::scene::add_cell(doc, registry, k_composition_probe_kind, std::to_string(child.value),
                               arbc::Affine::identity())
              .has_value());

  const std::vector<ace::scene::CompositionOption> options =
      ace::scene::composition_options(doc, registry);
  REQUIRE(options.size() == 1);
  CHECK(options[0].value == std::to_string(child.value));
}

TEST_CASE(
    "a composition_options value round-trips into a nested cell that names that composition") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "roundtrip_opt");
  const std::vector<KindInsertSchema> schemas = ace::scene::insert_schemas(state.registry());

  arbc::ObjectId child;
  dispatch(state, Command{"add_child", [&child](arbc::Document& doc) {
                            child = doc.add_composition(32.0, 32.0);
                          }});
  REQUIRE(child.valid());
  // Seed the enumeration with an existing wrapper so the option set is non-empty.
  REQUIRE(ace::scene::add_cell(state.document(), state.registry(), "org.arbc.nested",
                               std::to_string(child.value), arbc::Affine::identity())
              .has_value());

  const std::vector<ace::scene::CompositionOption> options =
      ace::scene::composition_options(state.document(), state.registry());
  REQUIRE(options.size() == 1);

  // The picker's `value` string, fed positionally through the kind's OWN assemble via build_config
  // and add_cell, mints a nested cell whose composition_ref() equals the chosen composition —
  // proving the resolved decimal is BYTE-IDENTICAL to what the kind consumes (Constraint 3).
  const std::string config =
      config_for(schemas, "org.arbc.nested", InsertValues{{"child", options[0].value}});
  CHECK(config == options[0].value);
  const arbc::expected<arbc::ObjectId, std::string> minted =
      ace::scene::add_cell(state.document(), state.registry(), "org.arbc.nested", config,
                           arbc::Affine::translation(9, 9));
  REQUIRE(minted.has_value());
  const std::optional<arbc::ObjectId> resolved =
      ace::scene::nested_composition_of(state.document(), *minted);
  REQUIRE(resolved.has_value());
  CHECK(*resolved == child);
}

// --- Provenance classification (editor.cells.resolution; D-resolution-1/-2) --------------

TEST_CASE("cells() classifies pixel provenance from generic facets, never a kind allowlist") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "provenance");
  // A registry carrying two foreign finite-detail kinds the editor cannot know: the
  // classification must key on the generic `editable()`/`external_asset_ref()` facets
  // (D-resolution-2), so these are classified by shape, not by id.
  arbc::Registry registry = cell_registry_with_detail_probes();
  const std::vector<KindInsertSchema> schemas = ace::scene::insert_schemas(registry);

  // A child composition for the nested cell to embed (resolution-independent).
  arbc::ObjectId child;
  dispatch(state, Command{"add_child", [&child](arbc::Document& doc) {
                            child = doc.add_composition(32.0, 32.0);
                          }});
  REQUIRE(child.valid());

  const arbc::Affine at = arbc::Affine::identity();
  // z-order == insertion order: bounded solid, raster, external-ref image, editable plugin,
  // nested.
  REQUIRE(ace::scene::add_cell(state.document(), registry, "org.arbc.solid",
                               config_for(schemas, "org.arbc.solid",
                                          InsertValues{{"red", "1"},
                                                       {"green", "1"},
                                                       {"blue", "1"},
                                                       {"alpha", "1"},
                                                       {"x", "0"},
                                                       {"y", "0"},
                                                       {"width", "256"},
                                                       {"height", "256"}}),
                               at)
              .has_value());
  REQUIRE(ace::scene::add_cell(state.document(), registry, "org.arbc.raster",
                               config_for(schemas, "org.arbc.raster",
                                          InsertValues{{"width", "16"}, {"height", "24"}}),
                               at)
              .has_value());
  REQUIRE(
      ace::scene::add_cell(state.document(), registry, k_external_probe_kind, "x", at).has_value());
  REQUIRE(
      ace::scene::add_cell(state.document(), registry, k_editable_probe_kind, "x", at).has_value());
  REQUIRE(ace::scene::add_cell(state.document(), registry, "org.arbc.nested",
                               config_for(schemas, "org.arbc.nested",
                                          InsertValues{{"config", std::to_string(child.value)}}),
                               at)
              .has_value());

  const std::vector<Cell> cells = ace::scene::cells(state.document(), registry);
  REQUIRE(cells.size() == 5);

  // A bounded solid is a real placed rect (content_bounds present) but has NO native detail
  // floor — so it is ResolutionIndependent and its native px is nullopt EVEN THOUGH its
  // bounds are set. Native px is gated on the provenance source, not merely on bounds.
  CHECK(cells[0].kind_id == "org.arbc.solid");
  CHECK(cells[0].detail.source == ace::scene::DetailSource::ResolutionIndependent);
  CHECK(cells[0].content_bounds.has_value());
  CHECK_FALSE(cells[0].detail.native_pixels.has_value());

  // A painted raster owns a mutable grid (editable() != nullptr) — PaintedRaster, native px =
  // its content_bounds dims (D-resolution-1).
  CHECK(cells[1].kind_id == "org.arbc.raster");
  CHECK(cells[1].detail.source == ace::scene::DetailSource::PaintedRaster);
  REQUIRE(cells[1].detail.native_pixels.has_value());
  CHECK(cells[1].detail.native_pixels->first == 16);
  CHECK(cells[1].detail.native_pixels->second == 24);

  // A referenced image borrows an external file (external_asset_ref non-empty, editable null)
  // — ReferencedImage, native px = the decoded master extent.
  CHECK(cells[2].kind_id == k_external_probe_kind);
  CHECK(cells[2].detail.source == ace::scene::DetailSource::ReferencedImage);
  REQUIRE(cells[2].detail.native_pixels.has_value());
  CHECK(cells[2].detail.native_pixels->first == 64);
  CHECK(cells[2].detail.native_pixels->second == 48);

  // THE no-allowlist proof: an UNKNOWN editable kind (not org.arbc.raster) classifies
  // PaintedRaster purely because editable() != nullptr — a kind_id switch would misclassify
  // it (A16 / D-resolution-2).
  CHECK(cells[3].kind_id == k_editable_probe_kind);
  CHECK(cells[3].detail.source == ace::scene::DetailSource::PaintedRaster);
  REQUIRE(cells[3].detail.native_pixels.has_value());
  CHECK(cells[3].detail.native_pixels->first == 40);
  CHECK(cells[3].detail.native_pixels->second == 30);

  // A nested cell has neither facet — ResolutionIndependent, no native floor.
  CHECK(cells[4].kind_id == "org.arbc.nested");
  CHECK(cells[4].detail.source == ace::scene::DetailSource::ResolutionIndependent);
  CHECK_FALSE(cells[4].detail.native_pixels.has_value());
}

TEST_CASE("resolution provenance probes expose exactly the generic facets they classify on") {
  // Pin the two stand-ins against the facets the classifier reads, directly (also covering the
  // doubles' trivial overrides): the external-ref probe borrows a file (non-empty
  // external_asset_ref, null editable) => ReferencedImage; the editable probe owns a grid
  // (non-null editable) => PaintedRaster. The Editable methods are inert (D-resolution-5).
  ExternalRefProbe ext;
  CHECK(ext.editable() == nullptr);
  CHECK_FALSE(ext.external_asset_ref().empty());
  REQUIRE(ext.bounds().has_value());
  CHECK(ext.bounds()->width() == 64.0);
  CHECK(ext.stability() == arbc::Stability::Static);
  CHECK_FALSE(ext.time_extent().has_value());

  EditableProbe ed;
  CHECK(ed.editable() == &ed);
  CHECK(ed.external_asset_ref().empty());
  REQUIRE(ed.bounds().has_value());
  CHECK(ed.bounds()->width() == 40.0);
  CHECK(ed.stability() == arbc::Stability::Static);
  CHECK_FALSE(ed.time_extent().has_value());
  const arbc::StateHandle handle = ed.capture();
  ed.retain(handle);
  CHECK(ed.state_cost(handle) == 0);
  ed.restore(handle);
  ed.release(handle);
}

TEST_CASE("a failing factory mutates nothing — no content, no transaction, no journal entry") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "failures");
  const std::size_t depth_before = state.document().journal().depth();
  const std::uint64_t revision_before = state.document().pin()->revision();

  const auto refuses = [&](std::string_view kind_id, std::string_view config,
                           std::string_view expect_contains) {
    const arbc::expected<arbc::ObjectId, std::string> added = ace::scene::add_cell(
        state.document(), state.registry(), kind_id, config, arbc::Affine::identity());
    REQUIRE_FALSE(added.has_value());
    CHECK(added.error().find(expect_contains) != std::string::npos);
    // The kind's own message reached the caller, and NOTHING moved.
    CHECK(state.document().journal().depth() == depth_before);
    CHECK(state.document().pin()->revision() == revision_before);
    CHECK(ace::scene::cells(state.document(), state.registry()).empty());
  };

  // Operator kinds whose registered factory ALWAYS errors — offered, never hidden.
  refuses("org.arbc.fade", "", "org.arbc.fade");
  refuses("org.arbc.crossfade", "", "org.arbc.crossfade");
  // A kind that is not registered at all.
  refuses("org.example.nope", "whatever", "not a registered kind");
  // A well-known kind with a malformed config: the kind's parser is the validator, and
  // (D-insert_schema-2) that is now the ONLY validator — `build_config` no longer rejects.
  refuses("org.arbc.raster", "0x0", "org.arbc.raster");
  refuses("org.arbc.solid", "not,a,colour,x", "org.arbc.solid");
  // issue #22's non-positive extent is an error VALUE at the factory: the assembled config
  // is well-formed (`build_config` joined it), but the solid factory refuses it.
  refuses("org.arbc.solid", "1,1,1,1,0,0,-4,10", "positive");
}

TEST_CASE("add_cell with no root composition is a no-op") {
  arbc::Document doc;
  const arbc::Registry registry = cell_registry();
  const arbc::expected<arbc::ObjectId, std::string> added =
      ace::scene::add_cell(doc, registry, "org.arbc.solid", "0,0,0,1", arbc::Affine::identity());
  REQUIRE_FALSE(added.has_value());
  CHECK(added.error().find("no root composition") != std::string::npos);
  CHECK(ace::scene::cells(doc, registry).empty());
}

TEST_CASE("cells() excludes cameras and reports an unresolvable token as an empty kind") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "split");

  dispatch(state, Command{"add_camera", [&state](arbc::Document& doc) {
                            ace::scene::add_camera(doc, state.registry(), "shot",
                                                   ace::scene::Resolution{640, 480},
                                                   arbc::Affine::identity());
                          }});
  REQUIRE(ace::scene::add_cell(state.document(), state.registry(), "org.arbc.solid", "0,0,0,1",
                               arbc::Affine::identity())
              .has_value());

  // The two lists are disjoint and complete: A14's "identical in shape" without
  // conflating them (what panels.layers' cameras+layers sections depend on).
  CHECK(ace::scene::cells(state.document(), state.registry()).size() == 1);
  CHECK(ace::scene::cameras(state.document()).size() == 1);
  CHECK(ace::scene::cells(state.document(), state.registry())[0].kind_id == "org.arbc.solid");

  // A content carrying NO kind token (the unknown-passthrough shape) still lists —
  // an unnamed cell is a cell, not a dropped one.
  dispatch(state,
           Command{"untokened", [](arbc::Document& doc) {
                     const arbc::ObjectId content = doc.add_content(
                         std::make_shared<arbc::SolidContent>(arbc::Rgba{0.F, 0.F, 0.F, 1.F}));
                     const arbc::ObjectId layer = doc.add_layer(content, arbc::Affine::identity());
                     arbc::ObjectId root;
                     const arbc::CompositionRecord* rec = nullptr;
                     REQUIRE(doc.pin()->find_first_composition(root, rec));
                     doc.attach_layer(root, layer);
                   }});
  const std::vector<Cell> cells = ace::scene::cells(state.document(), state.registry());
  REQUIRE(cells.size() == 2);
  CHECK(cells[1].kind_id.empty());
}

// --- The commands verb + journal contract (Constraint 4) ---------------------

TEST_CASE("dispatching insert_cell adds ONE journal entry and one undo removes the cell whole") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "journal");

  const std::size_t depth_baseline = state.document().journal().depth();

  ace::commands::InsertCellOutcome outcome;
  const Command command = ace::commands::insert_cell_command(
      state.registry(), "org.arbc.raster", "8x8", arbc::Affine::translation(2.0, 3.0), outcome);
  const ace::commands::DispatchOutcome dispatched = dispatch(state, command);

  // ONE entry (D-one_action_one_entry-1): `create_content_and_attach` binds the Content vtable,
  // adds the placing layer, and attaches it inside a SINGLE transaction — no intermediate
  // published state in which a content exists attached to nothing. This is the COUNT that
  // distinguishes the shapes: the retired two-entry create landed `== 2` here.
  CHECK(dispatched.journal_entries_added == 1);
  CHECK(state.document().journal().depth() == depth_baseline + 1);
  CHECK(outcome.error.empty());
  REQUIRE(outcome.content.valid());

  const std::vector<Cell> before = ace::scene::cells(state.document(), state.registry());
  REQUIRE(before.size() == 1);
  CHECK(before[0].id == outcome.content);

  // Undo-WHOLENESS (D-one_action_one_entry-5): ONE undo reverses the create entirely. `cells()`
  // is empty AND the content RECORD is gone — not merely detached, as the two-entry shape's
  // first undo left it (an orphan content a membership check cannot see). A record-level
  // `find_content` is what distinguishes a whole reversal from a half one; the two-entry create
  // needed a SECOND undo to reach this state.
  REQUIRE(ace::commands::undo(state).moved);
  CHECK(ace::scene::cells(state.document(), state.registry()).empty());
  CHECK(state.document().pin()->find_content(outcome.content) == nullptr);

  // One redo restores it whole and id-stable — the round trip proves the single-entry reversal.
  REQUIRE(ace::commands::redo(state).moved);
  const std::vector<Cell> after = ace::scene::cells(state.document(), state.registry());
  REQUIRE(after.size() == 1);
  CHECK(after[0].id == outcome.content);
  CHECK(after[0].layer == before[0].layer);
  CHECK(after[0].placement == before[0].placement);
  CHECK(state.document().pin()->find_content(outcome.content) != nullptr);
}

TEST_CASE("a refused insert command reports the kind's error and leaves the journal alone") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "journal_fail");

  ace::commands::InsertCellOutcome outcome;
  const Command command = ace::commands::insert_cell_command(state.registry(), "org.arbc.fade", "",
                                                             arbc::Affine::identity(), outcome);
  const ace::commands::DispatchOutcome dispatched = dispatch(state, command);
  CHECK(dispatched.journal_entries_added == 0);
  CHECK_FALSE(outcome.content.valid());
  CHECK(outcome.error.find("org.arbc.fade") != std::string::npos);
  CHECK(ace::scene::cells(state.document(), state.registry()).empty());
}

// --- Persistence (Constraint 10 / D-cells_model-9) ---------------------------

TEST_CASE("cells round-trip through save_project / open_project with NO new kind registration") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "roundtrip";

  arbc::Affine solid_at = arbc::Affine::identity();
  const arbc::Affine raster_at = arbc::Affine::translation(4.0, 6.0);
  const arbc::Affine nested_at{1.5, 0.0, 0.0, 1.5, -2.0, 8.0};
  {
    AppState state = session_with_composition(scratch, fs, "roundtrip");
    arbc::ObjectId child;
    dispatch(state, Command{"add_child", [&child](arbc::Document& doc) {
                              child = doc.add_composition(32.0, 32.0);
                            }});
    solid_at = arbc::Affine::translation(1.0, 2.0);
    REQUIRE(ace::scene::add_cell(state.document(), state.registry(), "org.arbc.solid", "0.25,0,0,1",
                                 solid_at)
                .has_value());
    REQUIRE(ace::scene::add_cell(state.document(), state.registry(), "org.arbc.raster", "16x16",
                                 raster_at)
                .has_value());
    REQUIRE(ace::scene::add_cell(state.document(), state.registry(), "org.arbc.nested",
                                 std::to_string(child.value), nested_at)
                .has_value());
    REQUIRE(ace::commands::save_project(state, fs).has_value());
  } // released: workspace checkpointed + unmapped

  // Shed the workspace so the reopen MUST rebuild from canonical — the codec path,
  // which is what proves the cells serialize generically.
  std::error_code ec;
  std::filesystem::remove_all(ace::project::project_layout(root).workspace_dir, ec);

  // Reopened with NO extra-kinds callback at all: this leaf registers no editor kind,
  // so A15's rebuild-from-canonical policy is untouched and the built-in cell kinds
  // round-trip through the generic codec table.
  auto reopened = ace::project::open_project(fs, root);
  REQUIRE(reopened.has_value());
  CHECK(reopened.value().rebuilt_from_canonical);

  arbc::Registry load_registry;
  arbc::register_builtin_kinds(load_registry);
  const std::vector<Cell> cells = ace::scene::cells(*reopened.value().document, load_registry);
  REQUIRE(cells.size() == 3);
  CHECK(cells[0].kind_id == "org.arbc.solid");
  CHECK(cells[1].kind_id == "org.arbc.raster");
  CHECK(cells[2].kind_id == "org.arbc.nested");
  CHECK(cells[0].placement == solid_at);
  CHECK(cells[1].placement == raster_at);
  CHECK(cells[2].placement == nested_at);
}

// --- Rendered output: the golden + its two degenerate companions -------------

namespace {

// The golden fixture: the green 64x64 probe composition, plus a SECOND composition
// holding an opaque bounded solid, embedded in the root as an `org.arbc.nested` cell
// through the SHIPPED `scene::add_cell` path at a placement computed by
// `interact::place_in_view`. Nested is the only factory-constructible BOUNDED,
// VISIBLE built-in kind, so this is the one golden that proves factory route +
// computed placement + attach order compose to exact pixels — a solid-only golden
// would be a flat fill that passes even with the placement wrong.
constexpr int k_child_edge = 32;

arbc::ObjectId add_child_composition(arbc::Document& doc) {
  const arbc::ObjectId child =
      doc.add_composition(static_cast<double>(k_child_edge), static_cast<double>(k_child_edge));
  // Bounded so the nested cell has a real extent to place (an unbounded child solid
  // would fill the synthetic viewport and the placement would be unobservable).
  const arbc::ObjectId content = doc.add_content(std::make_shared<arbc::SolidContent>(
      arbc::Rgba{0.6F, 0.0F, 0.0F, 1.0F},
      arbc::Rect{0.0, 0.0, static_cast<double>(k_child_edge), static_cast<double>(k_child_edge)}));
  const arbc::ObjectId layer = doc.add_layer(content, arbc::Affine::identity());
  doc.attach_layer(child, layer);
  return child;
}

} // namespace

TEST_CASE("a nested cell inserted at a computed placement matches the sRGB8 golden") {
  const ace::project::ProbeDocument probe = ace::project::build_probe_document();
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);

  const arbc::ObjectId child = add_child_composition(*probe.document);
  const arbc::Rect child_extent{0.0, 0.0, static_cast<double>(k_child_edge),
                                static_cast<double>(k_child_edge)};
  // The placement the shipped UI path would compute for this framing: the whole
  // 64x64 composition visible at identity, the child's 32-unit extent scaled to half
  // the shorter visible edge (32 units => scale 1) and centred => translate(16,16).
  const arbc::Affine placement =
      ace::interact::place_in_view(arbc::Affine::identity(), ace::project::k_probe_width,
                                   ace::project::k_probe_height, child_extent);
  CHECK(placement.a == 1.0);
  CHECK(placement.tx == 16.0);
  CHECK(placement.ty == 16.0);

  REQUIRE(ace::scene::add_cell(*probe.document, registry, "org.arbc.nested",
                               std::to_string(child.value), placement)
              .has_value());

  const ace::render::Srgb8Image image = ace::render::render_document_srgb8(
      *probe.document, ace::project::k_probe_width, ace::project::k_probe_height);
  REQUIRE(image.pixels.size() == static_cast<std::size_t>(ace::project::k_probe_width) *
                                     static_cast<std::size_t>(ace::project::k_probe_height) * 4);

  // Spot-check the geometry the golden encodes, so a regenerated golden can never
  // silently bless a wrong placement: the corner is the probe's green, the centre is
  // the embedded child's red.
  const auto pixel_at = [&image](int x, int y) {
    const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                            static_cast<std::size_t>(x)) *
                           4;
    return std::vector<std::uint8_t>{image.pixels[at], image.pixels[at + 1], image.pixels[at + 2],
                                     image.pixels[at + 3]};
  };
  const std::vector<std::uint8_t> corner = pixel_at(2, 2);
  const std::vector<std::uint8_t> centre = pixel_at(32, 32);
  CHECK(corner[1] > corner[0]);                     // green dominant outside the cell
  CHECK(centre[0] > centre[1]);                     // red dominant inside it
  CHECK(pixel_at(14, 32)[1] > pixel_at(14, 32)[0]); // just LEFT of the cell: green
  CHECK(pixel_at(18, 32)[0] > pixel_at(18, 32)[1]); // just inside it: red

  CHECK(ace_test::compare_golden("cells_insert_nested_64x64.rgba8", image.pixels));
}

TEST_CASE("a freshly inserted raster is transparent: the rendered bytes are unchanged") {
  const ace::project::ProbeDocument probe = ace::project::build_probe_document();
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);

  const ace::render::Srgb8Image before = ace::render::render_document_srgb8(
      *probe.document, ace::project::k_probe_width, ace::project::k_probe_height);

  // `org.arbc.raster`'s factory builds a TRANSPARENT raster (the production "new
  // paint layer" semantic): inserting one changes the scene graph but not a pixel,
  // which is exactly what makes it a blank canvas for editor.paint.brush.
  REQUIRE(ace::scene::add_cell(*probe.document, registry, "org.arbc.raster", "16x16",
                               arbc::Affine::translation(8.0, 8.0))
              .has_value());
  const ace::render::Srgb8Image after = ace::render::render_document_srgb8(
      *probe.document, ace::project::k_probe_width, ace::project::k_probe_height);

  // The probe's own solid was minted with no kind token, so it lists as an
  // unnamed cell (D-cells_model-8) alongside the raster we just inserted.
  const std::vector<Cell> listed = ace::scene::cells(*probe.document, registry);
  REQUIRE(listed.size() == 2);
  CHECK(listed.front().kind_id.empty());
  CHECK(listed.back().kind_id == "org.arbc.raster");
  CHECK(after.pixels == before.pixels);
}

TEST_CASE("an explicit 4-field solid is unbounded: it fills the frame uniformly") {
  const ace::project::ProbeDocument probe = ace::project::build_probe_document();
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);

  // The 4-field `r,g,b,a` grammar still yields an UNBOUNDED solid (issue #22 kept it as
  // the honest "background fill"): no bounds, so it paints everywhere and its placement is
  // a no-op. That consequence is pinned here rather than special-cased away.
  const arbc::expected<std::optional<arbc::Rect>, std::string> bounds =
      ace::scene::probe_bounds(registry, "org.arbc.solid", "0,0,0.75,1");
  REQUIRE(bounds.has_value());
  CHECK_FALSE(bounds->has_value());
  const arbc::Affine placement = ace::interact::place_in_view(
      arbc::Affine::identity(), ace::project::k_probe_width, ace::project::k_probe_height, *bounds);
  CHECK(placement == arbc::Affine::identity());

  REQUIRE(ace::scene::add_cell(*probe.document, registry, "org.arbc.solid", "0,0,0.75,1", placement)
              .has_value());
  const ace::render::Srgb8Image image = ace::render::render_document_srgb8(
      *probe.document, ace::project::k_probe_width, ace::project::k_probe_height);
  REQUIRE(image.pixels.size() >= 4);
  const std::vector<std::uint8_t> first(image.pixels.begin(), image.pixels.begin() + 4);
  CHECK(first[2] > first[0]); // blue-dominant: the inserted solid, not the green probe
  CHECK(first[3] == 255);
  for (std::size_t at = 0; at + 4 <= image.pixels.size(); at += 4) {
    REQUIRE(image.pixels[at] == first[0]);
    REQUIRE(image.pixels[at + 1] == first[1]);
    REQUIRE(image.pixels[at + 2] == first[2]);
    REQUIRE(image.pixels[at + 3] == first[3]);
  }
}

TEST_CASE("a default 8-field solid is bounded and places as a non-full-frame rectangle") {
  const ace::project::ProbeDocument probe = ace::project::build_probe_document();
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);

  // issue #22 / D-insert_schema-5: the dialog-default solid carries an extent, so
  // `probe_bounds` returns a real Rect and `place_in_view` centres+scales it — a
  // non-identity placement, retiring D-cells_model-3's unbounded-solid consequence.
  const std::string config = "1,1,1,1,0,0,256,256"; // the schema defaults, comma-joined
  const arbc::expected<std::optional<arbc::Rect>, std::string> bounds =
      ace::scene::probe_bounds(registry, "org.arbc.solid", config);
  REQUIRE(bounds.has_value());
  REQUIRE(bounds->has_value());
  const arbc::Affine placement = ace::interact::place_in_view(
      arbc::Affine::identity(), ace::project::k_probe_width, ace::project::k_probe_height, *bounds);
  CHECK_FALSE(placement == arbc::Affine::identity()); // placed, not the unbounded no-op

  REQUIRE(ace::scene::add_cell(*probe.document, registry, "org.arbc.solid", config, placement)
              .has_value());
  const std::vector<Cell> listed = ace::scene::cells(*probe.document, registry);
  REQUIRE(listed.size() == 2); // the probe's own untokened solid + this one
  CHECK(listed.back().kind_id == "org.arbc.solid");
  CHECK(listed.back().content_bounds.has_value()); // bounded, unlike the 4-field fill

  // The rendered frame is NOT a uniform fill: the 256-unit solid, scaled into the 64x64
  // view, leaves the green probe visible at the corners — the byte companion that pins the
  // 8-field grammar as a placed rectangle beside the 4-field uniform fill above.
  const ace::render::Srgb8Image image = ace::render::render_document_srgb8(
      *probe.document, ace::project::k_probe_width, ace::project::k_probe_height);
  const auto pixel_at = [&image](int x, int y) {
    const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                            static_cast<std::size_t>(x)) *
                           4;
    return std::vector<std::uint8_t>{image.pixels[at], image.pixels[at + 1], image.pixels[at + 2],
                                     image.pixels[at + 3]};
  };
  const std::vector<std::uint8_t> corner = pixel_at(2, 2);
  const std::vector<std::uint8_t> centre = pixel_at(32, 32);
  CHECK(corner[1] > corner[0]); // green probe still visible at the corner (not covered)
  CHECK(corner[1] > corner[2]);
  CHECK(centre[0] > 200); // the white solid at the centre
  CHECK(centre[1] > 200);
  CHECK(centre[2] > 200);
}
