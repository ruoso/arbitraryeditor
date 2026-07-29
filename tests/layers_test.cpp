// editor.panels.layers — L1 headless Catch2 units for the Layers panel's pure logic and scene
// verbs. Covers the front→back drag→membership conversion (`scene::list_drag_to_membership`,
// Constraint 2, cross-checked against `scene::z_order_position`), the reorder verb
// (`scene::reorder_cell`, Constraint 3 — one entry, undoable, same-object, no-op rejects), the
// scope-targeted reorder + the `cells(…, composition)` overload + `nested_composition_of`
// (Constraint 7), the borrowed/owned provenance axis on `CellDetail` (Constraint 5, D11), and the
// entered-composition scope + derived breadcrumb (`scene::active_composition` /
// `scene::composition_path`, Constraint 8/9). No ImGui/GL/SDL (§8); runs under ASan/TSan (§9).

#include <ace/commands/app_state.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/expected.hpp>
#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/content.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp> // arbc::KindBridge

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using ace::commands::AppState;
using ace::scene::Cell;
using ace::scene::DetailSource;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_layers_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

AppState session_with_composition(const ScratchDir& scratch, const ace::platform::FileSystem& fs,
                                  const char* leaf) {
  auto created = ace::project::create_project(fs, scratch.root / leaf);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  state.document().add_composition(64.0, 64.0);
  return state;
}

// A referenced-image-shaped kind: `external_asset_ref()` non-empty, `editable()` null — it MUST
// classify ReferencedImage + borrowed through the generic facets, never a kind string (A16).
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

// A pasted-owned-read-only-image-shaped kind: bytes owned (external_asset_ref EMPTY, the base
// default), read-only (editable null). It classifies owned (borrowed=false); with no editable
// grid and no external ref it is resolution-independent — the borrowed axis is what distinguishes
// it from the referenced photo, which is the whole point of D11's second axis (D-layers-4).
class OwnedImageProbe final : public arbc::Content {
public:
  std::optional<arbc::Rect> bounds() const override { return arbc::Rect{0.0, 0.0, 32.0, 24.0}; }
  arbc::Stability stability() const override { return arbc::Stability::Static; }
  std::optional<arbc::TimeRange> time_extent() const override { return std::nullopt; }
  std::optional<arbc::RenderResult> render(const arbc::RenderRequest&,
                                           std::shared_ptr<arbc::RenderCompletion>) override {
    return std::nullopt;
  }
};

constexpr const char* k_external_probe_kind = "org.example.external_probe";
constexpr const char* k_owned_probe_kind = "org.example.owned_probe";

arbc::Registry layers_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  arbc::ContentFactory external =
      [](arbc::ContentConfig) -> arbc::expected<std::unique_ptr<arbc::Content>, std::string> {
    return std::unique_ptr<arbc::Content>(std::make_unique<ExternalRefProbe>());
  };
  (void)registry.add(k_external_probe_kind, std::move(external),
                     arbc::KindMetadata{"External Probe", "1"});
  arbc::ContentFactory owned =
      [](arbc::ContentConfig) -> arbc::expected<std::unique_ptr<arbc::Content>, std::string> {
    return std::unique_ptr<arbc::Content>(std::make_unique<OwnedImageProbe>());
  };
  (void)registry.add(k_owned_probe_kind, std::move(owned), arbc::KindMetadata{"Owned Probe", "1"});
  return registry;
}

// Replicate `scene::add_cell`'s create-content-and-attach into an ARBITRARY composition (add_cell
// only targets root). The token is interned exactly as `cell_token` does so `cells()` resolves the
// kind back. This is the plumbing `editor.cells.scoped_edit` will fold into the verbs later.
arbc::ObjectId attach_into(arbc::Document& document, const arbc::Registry& registry,
                           arbc::ObjectId composition, std::string_view kind_id,
                           std::string_view config, const arbc::Affine& placement) {
  const arbc::ContentFactory* factory = registry.factory(kind_id);
  REQUIRE(factory != nullptr);
  REQUIRE(static_cast<bool>(*factory));
  arbc::expected<std::unique_ptr<arbc::Content>, std::string> made = (*factory)(config);
  REQUIRE(made.has_value());
  arbc::KindBridge bridge;
  ace::project::seed_kind_bridge(bridge, registry);
  const arbc::KindMetadata* metadata = registry.metadata(kind_id);
  const std::uint64_t token = bridge.intern(
      kind_id, metadata != nullptr ? std::string_view(metadata->version) : std::string_view{});
  const arbc::Document::Placed placed = document.create_content_and_attach(
      std::shared_ptr<arbc::Content>(std::move(*made)), token, composition, placement);
  return placed.content;
}

std::vector<arbc::ObjectId> ids_of(const std::vector<Cell>& cells) {
  std::vector<arbc::ObjectId> ids;
  ids.reserve(cells.size());
  for (const Cell& c : cells) {
    ids.push_back(c.id);
  }
  return ids;
}

} // namespace

// --- Drag → membership conversion (Constraint 2) -----------------------------

TEST_CASE("layers: list_drag_to_membership reflects front→back slots to bottom→top indices") {
  // Hand-computed for a 4-row list (front→back slots 0..3 over bottom→top membership 0..3).
  CHECK(ace::scene::list_drag_to_membership(0, 0, 4).from_index == 3);
  CHECK(ace::scene::list_drag_to_membership(0, 0, 4).to_index == 3);
  CHECK(ace::scene::list_drag_to_membership(0, 3, 4).from_index == 3);
  CHECK(ace::scene::list_drag_to_membership(0, 3, 4).to_index == 0);
  CHECK(ace::scene::list_drag_to_membership(3, 0, 4).from_index == 0);
  CHECK(ace::scene::list_drag_to_membership(3, 0, 4).to_index == 3);
  CHECK(ace::scene::list_drag_to_membership(1, 2, 4).from_index == 2);
  CHECK(ace::scene::list_drag_to_membership(1, 2, 4).to_index == 1);
  // Out-of-range slots clamp into [0, count); a degenerate count is a zeroed no-op.
  CHECK(ace::scene::list_drag_to_membership(-1, 5, 4).from_index == 3);
  CHECK(ace::scene::list_drag_to_membership(-1, 5, 4).to_index == 0);
  CHECK(ace::scene::list_drag_to_membership(0, 0, 0).from_index == 0);
  CHECK(ace::scene::list_drag_to_membership(0, 0, 0).to_index == 0);
}

TEST_CASE("layers: list_drag_to_membership agrees with z_order_position for every slot") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "dragmap");
  arbc::Registry registry = layers_registry();
  for (int i = 0; i < 4; ++i) {
    REQUIRE(ace::scene::add_cell(state.document(), registry, "org.arbc.raster", "8x8",
                                 arbc::Affine::translation(i, i))
                .has_value());
  }
  const std::vector<Cell> cells = ace::scene::cells(state.document(), registry);
  REQUIRE(cells.size() == 4);
  const int count = 4;
  // The cell at front→back list slot s is cells[count-1-s]; its membership index (the drag's
  // from_index) is exactly `z_order_position(...).index`. So the two never disagree about "layer
  // N".
  for (int slot = 0; slot < count; ++slot) {
    const Cell& at_slot = cells[static_cast<std::size_t>(count - 1 - slot)];
    const ace::scene::MembershipMove move = ace::scene::list_drag_to_membership(slot, slot, count);
    CHECK(static_cast<int>(move.from_index) ==
          ace::scene::z_order_position(cells, at_slot.id).index);
  }
}

// --- reorder_cell (Constraint 3) ---------------------------------------------

TEST_CASE("layers: reorder_cell rewrites membership order, one entry, undoable, same object") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "reorder");
  arbc::Registry registry = layers_registry();
  const arbc::ObjectId root = ace::scene::active_composition(state.document(), std::nullopt);
  REQUIRE(root.valid());

  const auto a = ace::scene::add_cell(state.document(), registry, "org.arbc.raster", "8x8",
                                      arbc::Affine::identity());
  const auto b = ace::scene::add_cell(state.document(), registry, "org.arbc.raster", "8x8",
                                      arbc::Affine::translation(1, 1));
  const auto c = ace::scene::add_cell(state.document(), registry, "org.arbc.raster", "8x8",
                                      arbc::Affine::translation(2, 2));
  const auto d = ace::scene::add_cell(state.document(), registry, "org.arbc.raster", "8x8",
                                      arbc::Affine::translation(3, 3));
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  REQUIRE(c.has_value());
  REQUIRE(d.has_value());
  // Bottom→top membership as inserted: a, b, c, d.
  REQUIRE(ids_of(ace::scene::cells(state.document(), registry)) ==
          std::vector<arbc::ObjectId>{*a, *b, *c, *d});

  const arbc::Journal& journal = state.document().journal();

  SECTION("a valid move to a new membership index, one entry, same object, one-press undo") {
    const std::size_t depth_before = journal.depth();
    REQUIRE(ace::scene::reorder_cell(state.document(), root, *a, 2));
    CHECK(journal.depth() == depth_before + 1); // ONE journal entry
    std::vector<Cell> after = ace::scene::cells(state.document(), registry);
    // Stable move of a to index 2: the untouched members keep their relative order -> b, c, a, d.
    CHECK(ids_of(after) == std::vector<arbc::ObjectId>{*b, *c, *a, *d});
    CHECK(ace::scene::z_order_position(after, *a).index == 2); // moved cell keeps its ObjectId
    // Undo restores the prior order on the SAME objects in ONE press.
    const ace::commands::UndoOutcome u = ace::commands::undo(state);
    CHECK(u.moved);
    CHECK(ids_of(ace::scene::cells(state.document(), registry)) ==
          std::vector<arbc::ObjectId>{*a, *b, *c, *d});
  }

  SECTION("a downward move inserts before the source index") {
    // Move d (bottom→top index 3) down to index 0 — the insert point precedes the source slot.
    REQUIRE(ace::scene::reorder_cell(state.document(), root, *d, 0));
    CHECK(ids_of(ace::scene::cells(state.document(), registry)) ==
          std::vector<arbc::ObjectId>{*d, *a, *b, *c});
  }

  SECTION("no-ops mutate nothing and open no transaction") {
    const std::size_t depth_before = journal.depth();
    const arbc::ObjectId camera =
        ace::scene::add_camera(state.document(), registry, "Cam", ace::scene::Resolution{16, 16},
                               arbc::Affine::identity());
    REQUIRE(camera.valid());
    const std::size_t depth_after_camera = journal.depth();

    CHECK_FALSE(ace::scene::reorder_cell(state.document(), root, arbc::ObjectId{}, 1)); // invalid
    CHECK_FALSE(ace::scene::reorder_cell(state.document(), root, *a, 0)); // from_index==to_index
    CHECK_FALSE(ace::scene::reorder_cell(state.document(), root, *a, 4)); // out of range (count 4)
    CHECK_FALSE(ace::scene::reorder_cell(state.document(), root, camera, 1)); // a camera has no z
    (void)depth_before;
    CHECK(journal.depth() == depth_after_camera); // not one no-op journaled anything
    CHECK(ids_of(ace::scene::cells(state.document(), registry)) ==
          std::vector<arbc::ObjectId>{*a, *b, *c, *d}); // order intact
  }
}

// --- Scope-targeted reorder + the composition overload (Constraint 3/7) ------

TEST_CASE("layers: reorder_cell and cells() target a given composition, root untouched") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "scoped");
  arbc::Registry registry = layers_registry();
  arbc::Document& doc = state.document();
  const arbc::ObjectId root = ace::scene::active_composition(doc, std::nullopt);
  REQUIRE(root.valid());

  // Root has two cells; a child composition has three of its own.
  const auto r0 =
      ace::scene::add_cell(doc, registry, "org.arbc.raster", "8x8", arbc::Affine::identity());
  const auto r1 = ace::scene::add_cell(doc, registry, "org.arbc.raster", "8x8",
                                       arbc::Affine::translation(1, 1));
  REQUIRE(r0.has_value());
  REQUIRE(r1.has_value());
  const arbc::ObjectId child = doc.add_composition(32.0, 32.0);
  REQUIRE(child.valid());
  const arbc::ObjectId c0 =
      attach_into(doc, registry, child, "org.arbc.raster", "8x8", arbc::Affine::identity());
  const arbc::ObjectId c1 =
      attach_into(doc, registry, child, "org.arbc.raster", "8x8", arbc::Affine::translation(1, 1));
  const arbc::ObjectId c2 =
      attach_into(doc, registry, child, "org.arbc.raster", "8x8", arbc::Affine::translation(2, 2));

  // The composition-parameterised overload walks the child bottom→top, same shape as the root walk.
  const std::vector<Cell> child_cells = ace::scene::cells(doc, registry, child);
  REQUIRE(ids_of(child_cells) == std::vector<arbc::ObjectId>{c0, c1, c2});
  CHECK(child_cells.front().kind_id == "org.arbc.raster");

  // Reorder inside the child; the child's order changes, the root's does not.
  REQUIRE(ace::scene::reorder_cell(doc, child, c0, 2));
  CHECK(ids_of(ace::scene::cells(doc, registry, child)) == std::vector<arbc::ObjectId>{c1, c2, c0});
  CHECK(ids_of(ace::scene::cells(doc, registry, root)) == std::vector<arbc::ObjectId>{*r0, *r1});
  CHECK(ids_of(ace::scene::cells(doc, registry)) == std::vector<arbc::ObjectId>{*r0, *r1});

  // A cell that is not a member of the target composition is a no-op (root cell against child).
  CHECK_FALSE(ace::scene::reorder_cell(doc, child, *r0, 1));
}

// --- Provenance + owned/borrowed classification (Constraint 5, D11) ----------

TEST_CASE("layers: provenance classifies painted/referenced/pasted/nested via generic facets") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "provenance");
  arbc::Registry registry = layers_registry();
  arbc::Document& doc = state.document();

  const arbc::ObjectId child = doc.add_composition(16.0, 16.0);
  REQUIRE(child.valid());

  REQUIRE(ace::scene::add_cell(doc, registry, "org.arbc.raster", "8x8", arbc::Affine::identity())
              .has_value()); // painted raster (owned, editable)
  REQUIRE(ace::scene::add_cell(doc, registry, k_external_probe_kind, "",
                               arbc::Affine::translation(1, 1))
              .has_value()); // referenced borrowed image
  REQUIRE(
      ace::scene::add_cell(doc, registry, k_owned_probe_kind, "", arbc::Affine::translation(2, 2))
          .has_value()); // pasted owned read-only image
  REQUIRE(ace::scene::add_cell(doc, registry, "org.arbc.nested", std::to_string(child.value),
                               arbc::Affine::translation(3, 3))
              .has_value()); // nested cell (resolution-independent)

  const std::vector<Cell> cells = ace::scene::cells(doc, registry);
  REQUIRE(cells.size() == 4);

  // Painted raster: editable grid -> PaintedRaster, bytes owned.
  CHECK(cells[0].detail.source == DetailSource::PaintedRaster);
  CHECK_FALSE(cells[0].detail.borrowed);
  // Referenced photo: external asset ref -> ReferencedImage, borrowed.
  CHECK(cells[1].detail.source == DetailSource::ReferencedImage);
  CHECK(cells[1].detail.borrowed);
  // Pasted owned image: no editable, no external ref -> resolution-independent, but OWNED — the
  // borrowed axis is what separates it from the referenced photo (D11's second axis).
  CHECK(cells[2].detail.source == DetailSource::ResolutionIndependent);
  CHECK_FALSE(cells[2].detail.borrowed);
  // Nested cell: resolution-independent, owned.
  CHECK(cells[3].detail.source == DetailSource::ResolutionIndependent);
  CHECK_FALSE(cells[3].detail.borrowed);

  // Classification is generic — the same describe_detail_source vocabulary the inspector shows.
  CHECK(std::string(ace::scene::describe_detail_source(cells[0].detail.source)) ==
        "Painted raster - editable, owned");
  CHECK(std::string(ace::scene::describe_detail_source(cells[1].detail.source)) ==
        "Referenced image - source-limited");
}

// --- nested_composition_of (Constraint 7) ------------------------------------

TEST_CASE("layers: nested_composition_of resolves a nested cell, nullopt otherwise") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "nestedof");
  arbc::Registry registry = layers_registry();
  arbc::Document& doc = state.document();

  const arbc::ObjectId child = doc.add_composition(16.0, 16.0);
  REQUIRE(child.valid());
  const auto painted =
      ace::scene::add_cell(doc, registry, "org.arbc.raster", "8x8", arbc::Affine::identity());
  const auto nested = ace::scene::add_cell(doc, registry, "org.arbc.nested",
                                           std::to_string(child.value), arbc::Affine::identity());
  REQUIRE(painted.has_value());
  REQUIRE(nested.has_value());

  const std::optional<arbc::ObjectId> resolved = ace::scene::nested_composition_of(doc, *nested);
  REQUIRE(resolved.has_value());
  CHECK(*resolved == child);
  CHECK_FALSE(ace::scene::nested_composition_of(doc, *painted).has_value());
  CHECK_FALSE(ace::scene::nested_composition_of(doc, arbc::ObjectId{}).has_value());
}

// --- Entered scope + derived breadcrumb (Constraint 8/9) ---------------------

TEST_CASE("layers: entered scope resolves fail-safe; breadcrumb is derived and climbs") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "scope");
  arbc::Registry registry = layers_registry();
  arbc::Document& doc = state.document();
  const arbc::ObjectId root = ace::scene::active_composition(doc, std::nullopt);
  REQUIRE(root.valid());

  // Two-deep nest: root ▸ child ▸ grandchild, through org.arbc.nested cells.
  const arbc::ObjectId child = doc.add_composition(32.0, 32.0);
  const arbc::ObjectId grandchild = doc.add_composition(16.0, 16.0);
  REQUIRE(child.valid());
  REQUIRE(grandchild.valid());
  REQUIRE(ace::scene::add_cell(doc, registry, "org.arbc.nested", std::to_string(child.value),
                               arbc::Affine::identity())
              .has_value());
  const arbc::ObjectId nested_in_child =
      attach_into(doc, registry, child, "org.arbc.nested", std::to_string(grandchild.value),
                  arbc::Affine::identity());
  REQUIRE(nested_in_child.valid());

  SECTION("active_composition resolves the scope, falling back to Root when it vanishes") {
    CHECK(ace::scene::active_composition(doc, std::nullopt) == root);
    CHECK(ace::scene::active_composition(doc, child) == child);
    CHECK(ace::scene::active_composition(doc, grandchild) == grandchild);
    // A scope naming a non-composition or an absent id resolves to Root (Constraint 8 fail-safe).
    CHECK(ace::scene::active_composition(doc, arbc::ObjectId{999999}) == root);
    CHECK(ace::scene::active_composition(doc, nested_in_child) == root); // a content, not a comp
  }

  SECTION("the breadcrumb path is Root ▸ … ▸ child, derived and fail-safe") {
    const std::vector<ace::scene::Breadcrumb> root_path =
        ace::scene::composition_path(doc, registry, std::nullopt);
    REQUIRE(root_path.size() == 1);
    CHECK(root_path[0].label == "Root");
    CHECK(root_path[0].composition == root);

    const std::vector<ace::scene::Breadcrumb> one_deep =
        ace::scene::composition_path(doc, registry, child);
    REQUIRE(one_deep.size() == 2);
    CHECK(one_deep[0].composition == root);
    CHECK(one_deep[1].composition == child);

    const std::vector<ace::scene::Breadcrumb> two_deep =
        ace::scene::composition_path(doc, registry, grandchild);
    REQUIRE(two_deep.size() == 3);
    CHECK(two_deep[0].label == "Root");
    CHECK(two_deep[0].composition == root);
    CHECK(two_deep[1].composition == child);
    CHECK(two_deep[2].composition == grandchild);
    CHECK(two_deep[1].label == "org.arbc.nested"); // display-only label off the descent cell

    // Climbing to crumb k pops to that ancestor: k==0 -> Root (nullopt), else path[k].
    CHECK(ace::scene::active_composition(doc, two_deep[2].composition) == grandchild);
    CHECK(ace::scene::active_composition(doc, two_deep[1].composition) == child);

    // A scope not reachable from Root degrades to Root only (fail-safe).
    const std::vector<ace::scene::Breadcrumb> bogus =
        ace::scene::composition_path(doc, registry, arbc::ObjectId{999999});
    REQUIRE(bogus.size() == 1);
    CHECK(bogus[0].composition == root);
  }

  SECTION("entering / climbing adds zero journal entries and leaves is_dirty unchanged") {
    const arbc::Journal& journal = state.document().journal();
    const std::size_t depth_before = journal.depth();
    const bool dirty_before = state.is_dirty();

    state.entered_composition() = child;
    CHECK(ace::scene::active_composition(doc, state.entered_composition()) == child);
    state.entered_composition() = std::nullopt; // climb back to Root
    CHECK(ace::scene::active_composition(doc, state.entered_composition()) == root);

    CHECK(journal.depth() == depth_before);  // never a transaction (D15)
    CHECK(state.is_dirty() == dirty_before); // no dirty change
  }
}

// --- Front→back ordering + cameras split (Constraint 1) ----------------------

TEST_CASE("layers: layers section is cells reversed; cameras are separate; no camera in layers") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "ordering");
  arbc::Registry registry = layers_registry();
  arbc::Document& doc = state.document();

  const auto bottom =
      ace::scene::add_cell(doc, registry, "org.arbc.raster", "8x8", arbc::Affine::identity());
  const auto top = ace::scene::add_cell(doc, registry, "org.arbc.raster", "8x8",
                                        arbc::Affine::translation(1, 1));
  REQUIRE(bottom.has_value());
  REQUIRE(top.has_value());
  const arbc::ObjectId camera = ace::scene::add_camera(
      doc, registry, "Hero", ace::scene::Resolution{32, 32}, arbc::Affine::identity());
  REQUIRE(camera.valid());

  const std::vector<Cell> cells = ace::scene::cells(doc, registry); // bottom→top membership
  REQUIRE(ids_of(cells) == std::vector<arbc::ObjectId>{*bottom, *top});
  // The layers section renders this reversed: slot 0 is the frontmost (top of z).
  std::vector<arbc::ObjectId> front_to_back;
  for (std::size_t i = cells.size(); i-- > 0;) {
    front_to_back.push_back(cells[i].id);
  }
  CHECK(front_to_back == std::vector<arbc::ObjectId>{*top, *bottom});

  // No camera appears in the layers section (KindBridge split, A14); it is in cameras().
  for (const Cell& c : cells) {
    CHECK(c.id != camera);
  }
  const std::vector<ace::scene::Camera> cameras = ace::scene::cameras(doc);
  REQUIRE(cameras.size() == 1);
  CHECK(cameras.front().id == camera);
}
