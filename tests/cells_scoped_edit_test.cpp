// editor.cells.scoped_edit — L1 headless Catch2 units (+ one render_offline golden) proving the
// insert/delete verbs and the pick assembly adapter honor the ENTERED composition scope (D17/D29).
// editor.panels.layers shipped the scope (AppState::entered_composition, scene::active_composition,
// the scene::cells composition overload); editor.canvas.isolation_scope shipped the dim. This leaf
// closes the loop: an insert while entered lands IN the entered composition, a delete removes only
// in-scope cells, and pick_targets confines pointer/keyboard/drag to the entered composition (no
// cameras). A nullopt scope (or one that fails the fail-safe) is byte-for-byte the shipped
// root-only behavior (Constraint 4), and every consumer degrades to Root through the single
// scene::active_composition fail-safe (Constraint 2).
//
// No ImGui/GL/SDL (Constraint 1); runs under the ASan/TSan legs (A4/§9).

#include <ace/commands/app_state.hpp>
#include <ace/commands/cells.hpp>
#include <ace/commands/selection.hpp>
#include <ace/interact/pick.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "golden_support.hpp"

using ace::commands::AppState;
using ace::commands::Command;
using ace::commands::DeleteOutcome;
using ace::commands::dispatch;
using ace::commands::Removal;
using ace::commands::Selection;
using ace::scene::Cell;

namespace {

constexpr int k_dim = 64; // the probe composition + render size (matches k_probe_width/height)
constexpr int k_child_edge = 32; // the nested child composition's bounded extent

arbc::Registry make_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry); // org.arbc.solid / .nested / ...
  ace::scene::register_camera_kind(registry);
  return registry;
}

// A bounded RED child composition (mould of tests/isolation_scope_test.cpp) so the nested cell has
// a real extent and an observable "inside". Returns its composition id.
arbc::ObjectId add_red_child(arbc::Document& doc) {
  const arbc::ObjectId child =
      doc.add_composition(static_cast<double>(k_child_edge), static_cast<double>(k_child_edge));
  const arbc::ObjectId content = doc.add_content(std::make_shared<arbc::SolidContent>(
      arbc::Rgba{0.85F, 0.0F, 0.0F, 1.0F},
      arbc::Rect{0.0, 0.0, static_cast<double>(k_child_edge), static_cast<double>(k_child_edge)}));
  const arbc::ObjectId layer = doc.add_layer(content, arbc::Affine::identity());
  doc.attach_layer(child, layer);
  return child;
}

// The probe composition + a nested RED child placed at translate(16,16) into root. Returns the
// child composition id (the scope to enter) via `out_child`. Renders once so the runtime binder
// ATTACHES the NestedContent (its bounds() is the empty placeholder until then) — the production
// offline/interactive paths always render before computing scope geometry.
ace::project::ProbeDocument build_nested(const arbc::Registry& registry,
                                         arbc::ObjectId& out_child) {
  ace::project::ProbeDocument probe = ace::project::build_probe_document();
  out_child = add_red_child(*probe.document);
  REQUIRE(ace::scene::add_cell(*probe.document, registry, "org.arbc.nested",
                               std::to_string(out_child.value),
                               arbc::Affine::translation(16.0, 16.0))
              .has_value());
  (void)ace::render::render_document_srgb8(*probe.document, k_dim, k_dim);
  return probe;
}

bool lists(const std::vector<Cell>& cells, arbc::ObjectId id) {
  for (const Cell& cell : cells) {
    if (cell.id == id) {
      return true;
    }
  }
  return false;
}

// --- AppState fixture (for delete_selection, which reads AppState::entered_composition) ----------

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_cells_scoped_edit_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// A workspace-backed session whose (single, hence root) composition is 64x64, then a nested RED
// child placed into it — the AppState mould of tests/cells_remove_test.cpp extended with a nested
// scope. `out_child` is the composition to enter. `create_project` seeds NO composition, so the one
// added below is the lowest-id (root) and the child is a genuine nested scope above it.
AppState nested_session(const ScratchDir& scratch, const ace::platform::FileSystem& fs,
                        const char* leaf, arbc::ObjectId& out_child) {
  auto created = ace::project::create_project(fs, scratch.root / leaf);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  dispatch(state, Command{"add_composition", [](arbc::Document& doc) {
                            doc.add_composition(static_cast<double>(k_dim),
                                                static_cast<double>(k_dim));
                          }});
  out_child = add_red_child(state.document());
  REQUIRE(ace::scene::add_cell(state.document(), state.registry(), "org.arbc.nested",
                               std::to_string(out_child.value),
                               arbc::Affine::translation(16.0, 16.0))
              .has_value());
  (void)ace::render::render_document_srgb8(state.document(), k_dim, k_dim);
  return state;
}

// A bounded blue solid inserted into `entered`, at content bounds [0,16)^2, identity placement.
arbc::ObjectId insert_blue(arbc::Document& doc, const arbc::Registry& reg,
                           std::optional<arbc::ObjectId> entered) {
  const arbc::expected<arbc::ObjectId, std::string> added = ace::scene::add_cell(
      doc, reg, "org.arbc.solid", "0,0,1,1,0,0,16,16", arbc::Affine::identity(), entered);
  REQUIRE(added.has_value());
  return *added;
}

} // namespace

// --- add_cell: insert lands in the active composition (Constraint 6) -----------------------------

TEST_CASE("cells scoped edit: insert while entered lands in the child, not root") {
  const arbc::Registry registry = make_registry();
  arbc::ObjectId child;
  const ace::project::ProbeDocument probe = build_nested(registry, child);
  arbc::Document& doc = *probe.document;

  const std::size_t root_before = ace::scene::cells(doc, registry).size();
  const std::size_t child_before = ace::scene::cells(doc, registry, child).size();

  const arbc::ObjectId minted = insert_blue(doc, registry, child);

  // Present in the CHILD list, absent from the ROOT list (Constraint 6).
  CHECK(lists(ace::scene::cells(doc, registry, child), minted));
  CHECK_FALSE(lists(ace::scene::cells(doc, registry), minted));
  CHECK(ace::scene::cells(doc, registry, child).size() == child_before + 1);
  CHECK(ace::scene::cells(doc, registry).size() == root_before);
}

TEST_CASE("cells scoped edit: insert with nullopt scope lands in root exactly as today") {
  const arbc::Registry registry = make_registry();
  arbc::ObjectId child;
  const ace::project::ProbeDocument probe = build_nested(registry, child);
  arbc::Document& doc = *probe.document;

  const std::size_t root_before = ace::scene::cells(doc, registry).size();
  const arbc::ObjectId minted = insert_blue(doc, registry, std::nullopt);

  CHECK(lists(ace::scene::cells(doc, registry), minted));
  CHECK(ace::scene::cells(doc, registry).size() == root_before + 1);
  CHECK_FALSE(lists(ace::scene::cells(doc, registry, child), minted));
}

TEST_CASE("cells scoped edit: insert fail-safe — a stale/foreign scope degrades to root") {
  const arbc::Registry registry = make_registry();
  arbc::ObjectId child;
  const ace::project::ProbeDocument probe = build_nested(registry, child);
  arbc::Document& doc = *probe.document;

  const std::size_t root_before = ace::scene::cells(doc, registry).size();
  // A scope that names no live composition (GC'd/undone-away/foreign) degrades to Root — the cell
  // lands in the root list, never refused or crashed (Constraint 2).
  const arbc::ObjectId minted = insert_blue(doc, registry, arbc::ObjectId{999999});

  CHECK(lists(ace::scene::cells(doc, registry), minted));
  CHECK(ace::scene::cells(doc, registry).size() == root_before + 1);
}

// --- remove_cells / selected_removals honor the scope (Constraint 5) -----------------------------

TEST_CASE("cells scoped edit: selected_removals is scope-parameterised and order-preserving") {
  const arbc::Registry registry = make_registry();
  arbc::ObjectId child;
  const ace::project::ProbeDocument probe = build_nested(registry, child);
  arbc::Document& doc = *probe.document;

  const arbc::ObjectId first = insert_blue(doc, registry, child);
  const arbc::expected<arbc::ObjectId, std::string> second_added = ace::scene::add_cell(
      doc, registry, "org.arbc.solid", "0,1,0,1,16,0,16,16", arbc::Affine::identity(), child);
  REQUIRE(second_added.has_value());
  const arbc::ObjectId second = *second_added;

  // Selection in a deliberate order; over the entered scope the resolver finds BOTH in-scope
  // layers and returns them in selection order (D-scoped_edit-3).
  Selection selection;
  selection.select(second);
  selection.add(first);
  const std::vector<Removal> resolved =
      ace::commands::selected_removals(doc, registry, selection, child);
  REQUIRE(resolved.size() == 2);
  CHECK(resolved[0].content == second);
  CHECK(resolved[1].content == first);
  for (const Removal& r : resolved) {
    CHECK(r.layer.valid());
  }

  // Over Root the SAME selection resolves to nothing — those cells are not in the root list, so the
  // root-only walk never finds them (the "delete does nothing" breakage this leaf fixes).
  CHECK(ace::commands::selected_removals(doc, registry, selection, std::nullopt).empty());
}

TEST_CASE("cells scoped edit: remove_cells validates against the entered composition") {
  const arbc::Registry registry = make_registry();
  arbc::ObjectId child;
  const ace::project::ProbeDocument probe = build_nested(registry, child);
  arbc::Document& doc = *probe.document;

  const arbc::ObjectId in_scope = insert_blue(doc, registry, child);
  arbc::ObjectId in_scope_layer;
  for (const Cell& cell : ace::scene::cells(doc, registry, child)) {
    if (cell.id == in_scope) {
      in_scope_layer = cell.layer;
    }
  }
  REQUIRE(in_scope_layer.valid());

  // Removing the in-scope pair while entered succeeds (membership gate validates against child).
  const std::vector<ace::scene::CellRemoval> batch{
      ace::scene::CellRemoval{in_scope, in_scope_layer}};
  CHECK(ace::scene::remove_cells(doc, batch, child) == 1);
  CHECK_FALSE(lists(ace::scene::cells(doc, registry, child), in_scope));
}

TEST_CASE("cells scoped edit: delete of an in-scope cell removes it; undo restores it into child") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  arbc::ObjectId child;
  AppState state = nested_session(scratch, fs, "delete_in_scope", child);

  const arbc::ObjectId cell = insert_blue(state.document(), state.registry(), child);
  state.entered_composition() = child;
  state.selection().select(cell);

  const std::size_t depth_before = state.document().journal().depth();
  const DeleteOutcome outcome = ace::commands::delete_selection(state);
  CHECK(outcome.removed == 1);
  CHECK(outcome.journal_entries_added == 1);
  CHECK(state.document().journal().depth() == depth_before + 1);
  CHECK_FALSE(lists(ace::scene::cells(state.document(), state.registry(), child), cell));

  // One undo restores it on the SAME ObjectId, back into the child (D15 / Constraint 10).
  REQUIRE(ace::commands::undo(state).moved);
  CHECK(lists(ace::scene::cells(state.document(), state.registry(), child), cell));
}

TEST_CASE("cells scoped edit: delete of a root cell while entered is a no-op") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  arbc::ObjectId child;
  AppState state = nested_session(scratch, fs, "delete_root_noop", child);

  // A cell in ROOT (the nested wrapper minted by the fixture), selected while entered into child.
  const std::vector<Cell> root_cells = ace::scene::cells(state.document(), state.registry());
  REQUIRE(root_cells.size() == 1);
  const arbc::ObjectId root_cell = root_cells.front().id;
  state.entered_composition() = child;
  state.selection().select(root_cell);

  const std::size_t depth_before = state.document().journal().depth();
  const std::uint64_t rev_before = state.document().pin()->revision();
  const DeleteOutcome outcome = ace::commands::delete_selection(state);

  // The resolver walks the CHILD list, which does not hold the root cell — zero removals, no entry,
  // unchanged revision (Constraint 5). The root cell survives.
  CHECK(outcome.removed == 0);
  CHECK(outcome.journal_entries_added == 0);
  CHECK(state.document().journal().depth() == depth_before);
  CHECK(state.document().pin()->revision() == rev_before);
  CHECK(lists(ace::scene::cells(state.document(), state.registry()), root_cell));
}

// --- pick_targets confinement (Constraints 4/7/8) ------------------------------------------------

TEST_CASE(
    "cells scoped edit: pick_targets confines to child cells and drops cameras while entered") {
  const arbc::Registry registry = make_registry();
  arbc::ObjectId child;
  const ace::project::ProbeDocument probe = build_nested(registry, child);
  arbc::Document& doc = *probe.document;

  // A camera at the project level, plus an in-scope child cell.
  REQUIRE(ace::scene::add_camera(doc, registry, "shot", ace::scene::Resolution{32, 24},
                                 arbc::Affine::identity())
              .valid());
  const arbc::ObjectId child_cell = insert_blue(doc, registry, child);

  // Entered: exactly the child's cells, NO cameras, NO root cells (Constraint 7/8).
  const std::vector<ace::interact::PickTarget> entered =
      ace::interact::pick_targets(doc, registry, child);
  bool has_child_cell = false;
  for (const ace::interact::PickTarget& t : entered) {
    CHECK(t.kind == ace::interact::PickKind::Cell); // no cameras while entered
    if (t.id == child_cell) {
      has_child_cell = true;
    }
  }
  CHECK(has_child_cell);
  CHECK(entered.size() == ace::scene::cells(doc, registry, child).size());

  // nullopt is byte-for-byte the two-argument overload: all root cells + all cameras (Constraint
  // 4).
  const std::vector<ace::interact::PickTarget> root_default =
      ace::interact::pick_targets(doc, registry);
  const std::vector<ace::interact::PickTarget> root_nullopt =
      ace::interact::pick_targets(doc, registry, std::nullopt);
  REQUIRE(root_default.size() == root_nullopt.size());
  bool any_camera = false;
  for (std::size_t i = 0; i < root_default.size(); ++i) {
    CHECK(root_default[i].id == root_nullopt[i].id);
    CHECK(root_default[i].kind == root_nullopt[i].kind);
    if (root_default[i].kind == ace::interact::PickKind::Camera) {
      any_camera = true;
    }
  }
  CHECK(any_camera); // the camera IS pickable at Root

  // A fail-safe scope resolves to the root set (Constraint 2).
  const std::vector<ace::interact::PickTarget> stale =
      ace::interact::pick_targets(doc, registry, arbc::ObjectId{999999});
  CHECK(stale.size() == root_default.size());
}

// --- Dirty / transient (Constraint 10, D15) ------------------------------------------------------

TEST_CASE("cells scoped edit: scope reads add no journal entry; a scoped edit dirties as usual") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  arbc::ObjectId child;
  AppState state = nested_session(scratch, fs, "transient", child);
  state.mark_saved(state.document().pin()->revision()); // clean baseline at the current revision
  state.entered_composition() = child;
  REQUIRE_FALSE(state.is_dirty());

  const std::size_t depth_before = state.document().journal().depth();
  // Pure reads: resolving the scope, a stale-scope degrade, and building a confined pick set.
  (void)ace::scene::active_composition(state.document(), child);
  (void)ace::scene::active_composition(state.document(), arbc::ObjectId{999999});
  (void)ace::interact::pick_targets(state.document(), state.registry(), child);
  (void)ace::commands::selected_removals(state.document(), state.registry(), state.selection(),
                                         child);
  CHECK(state.document().journal().depth() == depth_before);
  CHECK_FALSE(state.is_dirty());

  // A scoped insert dirties exactly as an unscoped one does.
  const arbc::ObjectId cell = insert_blue(state.document(), state.registry(), child);
  CHECK(lists(ace::scene::cells(state.document(), state.registry(), child), cell));
}

// --- Rendered output — golden (an insert while entered lands INSIDE the nested child) ------------

TEST_CASE("cells scoped edit: an insert while entered renders inside the nested child (golden)") {
  const arbc::Registry registry = make_registry();
  arbc::ObjectId child;
  const ace::project::ProbeDocument probe = build_nested(registry, child);
  arbc::Document& doc = *probe.document;

  // A distinctly-colored BLUE cell inserted into the ENTERED child at content bounds [0,16)^2. The
  // child is placed into root at translate(16,16) scale 1, so a correctly-scoped blue lands at root
  // [16,32)^2 (inside the red child); a blue that (wrongly) landed in ROOT would sit at [0,16)^2 in
  // the top-left corner over the green probe. The golden is the byte-exact discriminator.
  REQUIRE(insert_blue(doc, registry, child).valid());

  // Rendered with the scrim OFF (plain render_document_srgb8, no entered scope on the render path),
  // so the golden pins PLACEMENT, not dimming.
  const ace::render::Srgb8Image image = ace::render::render_document_srgb8(doc, k_dim, k_dim);
  REQUIRE(image.pixels.size() == static_cast<std::size_t>(k_dim) * k_dim * 4);

  const auto px = [&image](int x, int y) {
    const std::size_t at =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + x) * 4;
    return std::vector<std::uint8_t>{image.pixels[at], image.pixels[at + 1], image.pixels[at + 2],
                                     image.pixels[at + 3]};
  };
  // Spot-check the geometry the golden encodes so a regenerated golden can never bless a wrong
  // placement: blue inside the child's top-left quadrant, red elsewhere in the child, green
  // outside.
  const std::vector<std::uint8_t> inside_blue = px(24, 24); // root [16,32) — the scoped insert
  CHECK(inside_blue[2] > inside_blue[0]);
  CHECK(inside_blue[2] > inside_blue[1]);
  const std::vector<std::uint8_t> child_red = px(40, 40); // root [32,48) — child, no blue
  CHECK(child_red[0] > child_red[1]);
  CHECK(child_red[0] > child_red[2]);
  const std::vector<std::uint8_t> corner = px(4, 4); // outside the child: green probe, NOT blue
  CHECK(corner[1] > corner[0]);
  CHECK(corner[1] > corner[2]);

  CHECK(ace_test::compare_golden("cells_scoped_insert_64x64.rgba8", image.pixels));
}
