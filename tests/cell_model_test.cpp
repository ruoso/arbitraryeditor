// editor.cells.model — L1 headless Catch2 units for inserting a cell of ANY
// registered kind through the `arbc::Registry` seam (A16, D-cells_model-1..9). The
// load-bearing property is the ABSENCE of a kind allowlist: `scene::insert_schemas`
// emits one entry per `registry.ids()` entry unconditionally, and a kind the editor
// has never seen inserts end-to-end through the raw-config fallback. Also covers the
// grammar adapters, the resolution prefill, `scene::add_cell`'s failure-mutates-
// nothing contract, the cells/cameras split, the two-entry journal create, and the
// `project.arbc` roundtrip that needs no new kind registration. Plus the one
// `render_offline` golden (a nested cell at a computed placement) and its two
// degenerate byte-equality companions.
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
#include <arbc/contract/registry.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/model/model.hpp>
#include <arbc/model/records.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
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

// The editor's own registry seeding (mirrors `commands::register_editor_kinds`).
arbc::Registry cell_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  return registry;
}

arbc::Registry cell_registry_with_probe() {
  arbc::Registry registry = cell_registry();
  register_probe_kind(registry);
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
  // whose value travels to the kind's factory verbatim.
  const KindInsertSchema& probe = schema_for(schemas, k_probe_kind);
  CHECK(probe.raw_config);
  REQUIRE(probe.fields.size() == 1);
  CHECK(probe.fields[0].id == std::string(ace::scene::k_raw_config_field));
  CHECK(probe.fields[0].type == ace::scene::InsertFieldType::Text);

  // The adapters are an ENHANCEMENT over that universal enumeration, not a gate.
  const KindInsertSchema& raster = schema_for(schemas, "org.arbc.raster");
  CHECK_FALSE(raster.raw_config);
  REQUIRE(raster.fields.size() == 1);
  CHECK(raster.fields[0].id == "size");
  CHECK(raster.fields[0].type == ace::scene::InsertFieldType::Size);
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

// --- The grammar adapters (D-cells_model-2) ----------------------------------

TEST_CASE("build_config assembles the known grammars and rejects malformed values") {
  const arbc::Registry registry = cell_registry_with_probe();
  const std::vector<KindInsertSchema> schemas = ace::scene::insert_schemas(registry);

  // Raster: "<w>x<h>". Normalised, because libarbc's config split does not trim.
  CHECK(config_for(schemas, "org.arbc.raster", InsertValues{{"size", "1024x768"}}) == "1024x768");
  CHECK(config_for(schemas, "org.arbc.raster", InsertValues{{"size", " 1024 x 768 "}}) ==
        "1024x768");
  // Solid: "r,g,b,a" premultiplied working floats.
  CHECK(config_for(schemas, "org.arbc.solid", InsertValues{{"color", "0.5,0,0,1"}}) == "0.5,0,0,1");
  CHECK(config_for(schemas, "org.arbc.solid", InsertValues{{"color", "0.5, 0, 0, 1"}}) ==
        "0.5,0,0,1");
  // Nested: the child composition's decimal ObjectId.
  CHECK(config_for(schemas, "org.arbc.nested", InsertValues{{"child", " 7 "}}) == "7");
  // The fallback passes its one field through untouched, junk included.
  CHECK(config_for(schemas, k_probe_kind, InsertValues{{"config", "  raw , stuff "}}) ==
        "  raw , stuff ");

  // Malformed values are ERROR VALUES, never a silent default (Constraint 8).
  const auto rejects = [&schemas](std::string_view kind_id, const InsertValues& values) {
    const arbc::expected<std::string, std::string> config =
        ace::scene::build_config(schema_for(schemas, kind_id), values);
    CHECK_FALSE(config.has_value());
  };
  rejects("org.arbc.raster", InsertValues{{"size", ""}});
  rejects("org.arbc.raster", InsertValues{{"size", "0x768"}});
  rejects("org.arbc.raster", InsertValues{{"size", "-4x8"}});
  rejects("org.arbc.raster", InsertValues{{"size", "big"}});
  rejects("org.arbc.raster", InsertValues{{"size", "1024"}});
  rejects("org.arbc.raster", InsertValues{}); // the field was never collected
  rejects("org.arbc.solid", InsertValues{{"color", "1,2,3"}});
  rejects("org.arbc.solid", InsertValues{{"color", "a,b,c,d"}});
  rejects("org.arbc.nested", InsertValues{{"child", "0"}});
  rejects("org.arbc.nested", InsertValues{{"child", "-1"}});
  rejects(k_probe_kind, InsertValues{{"wrong_field", "x"}});
}

TEST_CASE("the raster resolution field is prefilled from the composition, never auto-applied") {
  const arbc::Registry registry = cell_registry();

  // Prefilled from the root composition's own extent (a probe document is 64x64).
  const ace::project::ProbeDocument probe = ace::project::build_probe_document();
  const std::vector<KindInsertSchema> seeded =
      ace::scene::insert_schemas(registry, ace::project::root_composition_size(*probe.document));
  CHECK(schema_for(seeded, "org.arbc.raster").fields[0].initial == "64x64");

  // No composition (or a degenerate authored size) falls back to 1024x1024.
  const arbc::Document empty;
  const std::vector<KindInsertSchema> bare =
      ace::scene::insert_schemas(registry, ace::project::root_composition_size(empty));
  CHECK(schema_for(bare, "org.arbc.raster").fields[0].initial == "1024x1024");
  CHECK(schema_for(bare, "org.arbc.raster").fields[0].initial ==
        std::to_string(ace::scene::k_fallback_resolution) + "x" +
            std::to_string(ace::scene::k_fallback_resolution));

  // The field is ALWAYS present — the user specifies the resolution at insert
  // (docs/00-design.md:116-119); an empty value blocks the insert rather than
  // silently applying a default.
  REQUIRE(schema_for(seeded, "org.arbc.raster").fields.size() == 1);
  const arbc::expected<std::string, std::string> blank =
      ace::scene::build_config(schema_for(seeded, "org.arbc.raster"), InsertValues{{"size", ""}});
  CHECK_FALSE(blank.has_value());
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

  REQUIRE(ace::scene::add_cell(
              state.document(), state.registry(), "org.arbc.solid",
              config_for(schemas, "org.arbc.solid", InsertValues{{"color", "0.5,0,0,1"}}), solid_at)
              .has_value());
  REQUIRE(ace::scene::add_cell(
              state.document(), state.registry(), "org.arbc.raster",
              config_for(schemas, "org.arbc.raster", InsertValues{{"size", "16x24"}}), raster_at)
              .has_value());
  REQUIRE(ace::scene::add_cell(state.document(), state.registry(), "org.arbc.nested",
                               config_for(schemas, "org.arbc.nested",
                                          InsertValues{{"child", std::to_string(child.value)}}),
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
  // A well-known kind with a malformed config: the kind's parser is the validator.
  refuses("org.arbc.raster", "0x0", "org.arbc.raster");
  refuses("org.arbc.solid", "not,a,colour,x", "org.arbc.solid");
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

TEST_CASE("dispatching insert_cell adds two journal entries and undo/redo is id-stable") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "journal");

  ace::commands::InsertCellOutcome outcome;
  const Command command = ace::commands::insert_cell_command(
      state.registry(), "org.arbc.raster", "8x8", arbc::Affine::translation(2.0, 3.0), outcome);
  const ace::commands::DispatchOutcome dispatched = dispatch(state, command);

  // TWO entries: `Document::add_content` self-commits (it is the only call that binds
  // a Content vtable), then one transact adds + attaches the placing layer. Cells and
  // cameras are identical here on purpose (D-cells_model-7 / D-rename-4).
  CHECK(dispatched.journal_entries_added == 2);
  CHECK(outcome.error.empty());
  REQUIRE(outcome.content.valid());

  const std::vector<Cell> before = ace::scene::cells(state.document(), state.registry());
  REQUIRE(before.size() == 1);
  CHECK(before[0].id == outcome.content);

  // The D15 OBSERVABLE contract: `cells()` keys off composition membership, so ONE
  // undo detaches the layer and the cell disappears; one redo restores it, id-stable.
  REQUIRE(ace::commands::undo(state).moved);
  CHECK(ace::scene::cells(state.document(), state.registry()).empty());
  REQUIRE(ace::commands::redo(state).moved);
  const std::vector<Cell> after = ace::scene::cells(state.document(), state.registry());
  REQUIRE(after.size() == 1);
  CHECK(after[0].id == outcome.content);
  CHECK(after[0].layer == before[0].layer);
  CHECK(after[0].placement == before[0].placement);
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

TEST_CASE("an inserted opaque solid is unbounded: it fills the frame uniformly") {
  const ace::project::ProbeDocument probe = ace::project::build_probe_document();
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);

  // A Registry-constructed solid has NO bounds — its config grammar admits none
  // (D-cells_model-3) — so it paints everywhere and its placement is a no-op. That
  // consequence is accepted and pinned here rather than special-cased away.
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
