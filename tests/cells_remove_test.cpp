// editor.cells.remove — L1 headless Catch2 units for taking a placed object back OUT of the
// composition through `arbc::Document::remove_content` (D7/D15/D-cells_remove-1..9). The
// load-bearing properties: a delete is exactly ONE library transaction (against the insert's
// two), it is undoable BY CONSTRUCTION on the SAME `ObjectId` with the editor writing no
// inverse, an N-object delete is N entries and N undo steps (the accepted asymmetry, pinned
// rather than engineered away), the verb is kind-AGNOSTIC so a selected camera deletes by the
// same path, a stale selected id is skipped rather than faulting, and the selection is emptied
// by the delete itself with no frame pump. Plus the round-trip byte-INVARIANCE case: insert
// changes the rendered image, delete puts it back byte-for-byte.
//
// No ImGui/GL/SDL (Constraint 1); runs under the ASan/TSan legs (A4/§9).

#include <ace/commands/app_state.hpp>
#include <ace/commands/cells.hpp>
#include <ace/commands/selection.hpp>
#include <ace/interact/interact.hpp>
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
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "golden_support.hpp"

using ace::commands::AppState;
using ace::commands::Command;
using ace::commands::DeleteOutcome;
using ace::commands::dispatch;
using ace::commands::DispatchOutcome;
using ace::commands::Removal;
using ace::commands::Selection;
using ace::scene::Camera;
using ace::scene::Cell;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_cells_remove_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// A fresh workspace-backed session with a root composition to place objects in (the mould of
// tests/cell_model_test.cpp). `AppState`'s registry already carries the built-ins plus
// `org.arbc.camera`, so both halves of D-cells_remove-1 are reachable.
AppState session_with_composition(const ScratchDir& scratch, const ace::platform::FileSystem& fs,
                                  const char* leaf) {
  auto created = ace::project::create_project(fs, scratch.root / leaf);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  dispatch(state, Command{"add_composition",
                          [](arbc::Document& doc) { doc.add_composition(64.0, 64.0); }});
  return state;
}

// Insert one cell through the shipped verb and hand back its content id.
arbc::ObjectId insert(AppState& state, const char* kind_id, const char* config,
                      const arbc::Affine& placement) {
  const arbc::expected<arbc::ObjectId, std::string> added =
      ace::scene::add_cell(state.document(), state.registry(), kind_id, config, placement);
  REQUIRE(added.has_value());
  return *added;
}

std::vector<Cell> cells_of(const AppState& state) {
  return ace::scene::cells(state.document(), state.registry());
}

// The `{content, layer}` pair for a content id, off the live document.
Removal removal_for(const AppState& state, arbc::ObjectId content) {
  for (const Cell& cell : cells_of(state)) {
    if (cell.id == content) {
      return Removal{cell.id, cell.layer};
    }
  }
  for (const Camera& camera : ace::scene::cameras(state.document())) {
    if (camera.id == content) {
      return Removal{camera.id, camera.layer};
    }
  }
  FAIL("no live target for the requested content id");
  return Removal{};
}

bool lists(const std::vector<Cell>& cells, arbc::ObjectId id) {
  for (const Cell& cell : cells) {
    if (cell.id == id) {
      return true;
    }
  }
  return false;
}

bool lists(const std::vector<Camera>& cameras, arbc::ObjectId id) {
  for (const Camera& camera : cameras) {
    if (camera.id == id) {
      return true;
    }
  }
  return false;
}

std::size_t depth(const AppState& state) { return state.document().journal().depth(); }
std::uint64_t revision(const AppState& state) { return state.document().pin()->revision(); }

// --- the golden fixture (reused verbatim from tests/cell_model_test.cpp) ------------------
// A bounded 32x32 red solid in its own composition, embedded in the probe's green 64x64 root
// as an `org.arbc.nested` cell — the exact graph behind
// `tests/goldens/cells_insert_nested_64x64.rgba8`, which anchors the middle render of the
// round-trip case as a KNOWN-GOOD image.
constexpr int k_child_edge = 32;

arbc::ObjectId add_child_composition(arbc::Document& doc) {
  const arbc::ObjectId child =
      doc.add_composition(static_cast<double>(k_child_edge), static_cast<double>(k_child_edge));
  const arbc::ObjectId content = doc.add_content(std::make_shared<arbc::SolidContent>(
      arbc::Rgba{0.6F, 0.0F, 0.0F, 1.0F},
      arbc::Rect{0.0, 0.0, static_cast<double>(k_child_edge), static_cast<double>(k_child_edge)}));
  doc.attach_layer(child, doc.add_layer(content, arbc::Affine::identity()));
  return child;
}

} // namespace

// --- One entry per delete (Constraint 3 / D15) -------------------------------------------

TEST_CASE("cells remove: one delete is ONE journal entry, against the insert's two") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "one_entry");

  ace::commands::InsertCellOutcome inserted;
  const Command insert_command = ace::commands::insert_cell_command(
      state.registry(), "org.arbc.raster", "8x8", arbc::Affine::translation(2.0, 3.0), inserted);
  const DispatchOutcome insert_outcome = dispatch(state, insert_command);
  // The contrast this case exists to pin: `Document::add_content` self-commits (it is the
  // only call that binds a Content vtable) and the placing layer is a second transaction, so
  // a CREATE is two entries — but `remove_content` composes all three teardowns into one, so
  // a DELETE is one (D-cells_remove-2).
  CHECK(insert_outcome.journal_entries_added == 2);
  REQUIRE(inserted.content.valid());

  const Removal target = removal_for(state, inserted.content);
  const std::size_t depth_before = depth(state);
  const std::uint64_t revision_before = revision(state);

  bool removed = false;
  const Command remove_command =
      ace::commands::remove_cell_command(target.content, target.layer, removed);
  const DispatchOutcome removed_outcome = dispatch(state, remove_command);

  CHECK(removed);
  CHECK(removed_outcome.journal_entries_added == 1);
  CHECK(depth(state) == depth_before + 1);
  CHECK(revision(state) > revision_before);
  CHECK(cells_of(state).empty());
}

// --- Undo restores on the SAME ObjectId; redo removes it again (D15/D-cells_remove-2) -----

TEST_CASE("cells remove: undo restores the cell on the same ObjectId, redo removes it again") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "undo_redo");

  const arbc::Affine placement{2.0, 0.0, 0.0, 2.0, 5.0, 7.0};
  const arbc::ObjectId content = insert(state, "org.arbc.raster", "16x24", placement);
  const std::vector<Cell> before = cells_of(state);
  REQUIRE(before.size() == 1);

  bool removed = false;
  dispatch(state, ace::commands::remove_cell_command(before[0].id, before[0].layer, removed));
  REQUIRE(removed);
  REQUIRE(cells_of(state).empty());

  // Undoable BY CONSTRUCTION, through the journal alone — the editor wrote no inverse and
  // no snapshot. One undo restores the exact record slot: same id, same layer, same
  // placement, same kind.
  REQUIRE(ace::commands::undo(state).moved);
  const std::vector<Cell> restored = cells_of(state);
  REQUIRE(restored.size() == 1);
  CHECK(restored[0].id == content);
  CHECK(restored[0].layer == before[0].layer);
  CHECK(restored[0].placement == placement);
  CHECK(restored[0].kind_id == "org.arbc.raster");

  REQUIRE(ace::commands::redo(state).moved);
  CHECK(cells_of(state).empty());
}

// --- Undo does NOT restore the selection (D-selection-7) ----------------------------------

TEST_CASE("cells remove: the selection stays empty across an undo of the delete") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "undo_selection");

  const arbc::ObjectId content = insert(state, "org.arbc.raster", "8x8", arbc::Affine::identity());
  state.selection().select(content);
  REQUIRE(ace::commands::can_delete(state));

  const DeleteOutcome outcome = ace::commands::delete_selection(state);
  CHECK(outcome.removed == 1);
  CHECK(state.selection().empty());

  // selection.md's documented companion contract, pinned at the leaf that makes it
  // reachable: a redo/undo that restores the same object does NOT restore the selection.
  REQUIRE(ace::commands::undo(state).moved);
  REQUIRE(cells_of(state).size() == 1);
  CHECK(state.selection().empty());
  CHECK_FALSE(state.selection().contains(content));
  CHECK(state.selection().primary() == arbc::ObjectId{});
}

// --- N objects => N entries => N undos (D-cells_remove-2) ---------------------------------

TEST_CASE("cells remove: a 3-object delete is 3 entries and takes 3 undos, in reverse order") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "three");

  const arbc::ObjectId first = insert(state, "org.arbc.raster", "8x8", arbc::Affine::identity());
  const arbc::ObjectId second =
      insert(state, "org.arbc.raster", "8x8", arbc::Affine::translation(10.0, 0.0));
  const arbc::ObjectId third =
      insert(state, "org.arbc.raster", "8x8", arbc::Affine::translation(20.0, 0.0));
  state.selection().select(first);
  state.selection().add(second);
  state.selection().add(third);

  const std::size_t depth_before = depth(state);
  const DeleteOutcome outcome = ace::commands::delete_selection(state);

  // The accepted asymmetry: `remove_content` self-commits and exposes no coalesce hook, so
  // there is no place to fold N removals into one undo step. Recorded, not engineered away.
  CHECK(outcome.removed == 3);
  CHECK(outcome.journal_entries_added == 3);
  CHECK(depth(state) == depth_before + 3);
  CHECK(cells_of(state).empty());

  // Reverse order: the LAST-deleted object comes back first.
  REQUIRE(ace::commands::undo(state).moved);
  CHECK(lists(cells_of(state), third));
  CHECK(cells_of(state).size() == 1);
  REQUIRE(ace::commands::undo(state).moved);
  CHECK(lists(cells_of(state), second));
  CHECK(cells_of(state).size() == 2);
  REQUIRE(ace::commands::undo(state).moved);
  const std::vector<Cell> all = cells_of(state);
  REQUIRE(all.size() == 3);
  CHECK(lists(all, first));
  CHECK(lists(all, second));
  CHECK(lists(all, third));
}

// --- A camera deletes by the SAME verb (D-cells_remove-1) ---------------------------------

TEST_CASE("cells remove: a selected camera is removed by the same kind-agnostic verb") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "camera");

  const arbc::ObjectId cell = insert(state, "org.arbc.raster", "8x8", arbc::Affine::identity());
  arbc::ObjectId camera;
  dispatch(state, Command{"add_camera", [&camera, &state](arbc::Document& doc) {
                            camera = ace::scene::add_camera(doc, state.registry(), "shot",
                                                            ace::scene::Resolution{32, 24},
                                                            arbc::Affine::translation(4.0, 4.0));
                          }});
  REQUIRE(camera.valid());
  REQUIRE(ace::scene::cameras(state.document()).size() == 1);

  state.selection().select(camera);
  const std::size_t depth_before = depth(state);
  const DeleteOutcome outcome = ace::commands::delete_selection(state);

  CHECK(outcome.removed == 1);
  CHECK(outcome.journal_entries_added == 1);
  CHECK(depth(state) == depth_before + 1);
  CHECK(ace::scene::cameras(state.document()).empty());
  // A cells-only delete would leave cameras permanently undeletable; the cell is untouched.
  const std::vector<Cell> remaining = cells_of(state);
  REQUIRE(remaining.size() == 1);
  CHECK(remaining[0].id == cell);
}

TEST_CASE("cells remove: a mixed cell+camera selection deletes both, in selection order") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "mixed");

  const arbc::ObjectId cell = insert(state, "org.arbc.raster", "8x8", arbc::Affine::identity());
  arbc::ObjectId camera;
  dispatch(state, Command{"add_camera", [&camera, &state](arbc::Document& doc) {
                            camera = ace::scene::add_camera(doc, state.registry(), "shot",
                                                            ace::scene::Resolution{32, 24},
                                                            arbc::Affine::identity());
                          }});
  REQUIRE(camera.valid());

  // Selection order, not document order: the camera was selected FIRST.
  state.selection().select(camera);
  state.selection().add(cell);
  const std::vector<Removal> resolved =
      ace::commands::selected_removals(state.document(), state.registry(), state.selection());
  REQUIRE(resolved.size() == 2);
  CHECK(resolved[0].content == camera);
  CHECK(resolved[1].content == cell);

  const DeleteOutcome outcome = ace::commands::delete_selection(state);
  CHECK(outcome.removed == 2);
  CHECK(outcome.journal_entries_added == 2);
  CHECK(cells_of(state).empty());
  CHECK(ace::scene::cameras(state.document()).empty());
  CHECK(state.selection().empty());
}

// --- Errors are values: a stale id is skipped (Constraint 5) ------------------------------

TEST_CASE("cells remove: a stale selected id is skipped, not an error") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "stale");

  const arbc::ObjectId live = insert(state, "org.arbc.raster", "8x8", arbc::Affine::identity());
  const arbc::ObjectId ghost{999999};
  state.selection().select(ghost);
  state.selection().add(live);

  // The resolver omits it entirely — the pure half of the contract.
  const std::vector<Removal> resolved =
      ace::commands::selected_removals(state.document(), state.registry(), state.selection());
  REQUIRE(resolved.size() == 1);
  CHECK(resolved[0].content == live);

  const std::size_t depth_before = depth(state);
  const DeleteOutcome outcome = ace::commands::delete_selection(state);
  CHECK(outcome.removed == 1);
  CHECK(outcome.journal_entries_added == 1); // entries only for the live member
  CHECK(depth(state) == depth_before + 1);
  CHECK(cells_of(state).empty());

  // And the scene verb itself opens no transaction for a stale or mismatched pair.
  const std::uint64_t revision_before = revision(state);
  CHECK_FALSE(ace::scene::remove_cell(state.document(), ghost, ghost));
  CHECK_FALSE(ace::scene::remove_cell(state.document(), arbc::ObjectId{}, arbc::ObjectId{}));
  CHECK_FALSE(ace::scene::remove_cell(state.document(), live, ghost));
  CHECK(depth(state) == depth_before + 1);
  CHECK(revision(state) == revision_before);
}

TEST_CASE("cells remove: an empty selection is a no-op with zero entries") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "empty");

  insert(state, "org.arbc.raster", "8x8", arbc::Affine::identity());
  REQUIRE(state.selection().empty());
  CHECK_FALSE(ace::commands::can_delete(state));

  const std::size_t depth_before = depth(state);
  const std::uint64_t revision_before = revision(state);
  const DeleteOutcome outcome = ace::commands::delete_selection(state);

  CHECK(outcome.removed == 0);
  CHECK(outcome.journal_entries_added == 0);
  CHECK(depth(state) == depth_before);
  CHECK(revision(state) == revision_before);
  CHECK(cells_of(state).size() == 1);
  CHECK(ace::commands::selected_removals(state.document(), state.registry(), state.selection())
            .empty());
}

// --- The selection is emptied by the delete itself (Constraint 7 / D-cells_remove-7) ------

TEST_CASE("cells remove: the selection is empty immediately after the delete, with no frame") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "clears");

  const arbc::ObjectId one = insert(state, "org.arbc.raster", "8x8", arbc::Affine::identity());
  const arbc::ObjectId two =
      insert(state, "org.arbc.raster", "8x8", arbc::Affine::translation(9.0, 0.0));
  state.selection().select(one);
  state.selection().add(two);
  REQUIRE(state.selection().size() == 2);
  REQUIRE(state.selection().primary() == two);

  // No `Selection::prune`, no canvas, no frame pump — the post-condition is the delete's own.
  ace::commands::delete_selection(state);
  CHECK(state.selection().empty());
  CHECK(state.selection().size() == 0);
  CHECK(state.selection().primary() == arbc::ObjectId{});
  CHECK_FALSE(ace::commands::can_delete(state));
}

// --- Dirty bookkeeping (Constraint 8 / A13 / D-undo-4) ------------------------------------

TEST_CASE("cells remove: a delete re-dirties a saved session and undo never marks it clean") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "dirty");

  const arbc::ObjectId content = insert(state, "org.arbc.raster", "8x8", arbc::Affine::identity());
  state.mark_saved(revision(state));
  REQUIRE_FALSE(state.is_dirty());

  state.selection().select(content);
  ace::commands::delete_selection(state);
  CHECK(state.is_dirty()); // a delete advances the revision past the saved baseline

  // Undo is a FORWARD publish, so it never returns the revision to a prior value and never
  // touches the dirty baseline (D-undo-4).
  REQUIRE(ace::commands::undo(state).moved);
  CHECK(state.is_dirty());
}

// --- The resolver is pure and order-preserving --------------------------------------------

TEST_CASE("cells remove: selected_removals is pure, order-preserving and duplicate-free") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "resolver");

  const arbc::ObjectId bottom = insert(state, "org.arbc.raster", "8x8", arbc::Affine::identity());
  const arbc::ObjectId middle =
      insert(state, "org.arbc.raster", "8x8", arbc::Affine::translation(10.0, 0.0));
  const arbc::ObjectId top =
      insert(state, "org.arbc.raster", "8x8", arbc::Affine::translation(20.0, 0.0));

  // Selection order deliberately reversed against z-order; `add` of an existing id is a
  // re-point, not a duplicate.
  Selection selection;
  selection.select(top);
  selection.add(bottom);
  selection.add(middle);
  selection.add(top);

  const std::vector<Cell> listed = cells_of(state);
  REQUIRE(listed.size() == 3);
  const std::size_t depth_before = depth(state);
  const std::uint64_t revision_before = revision(state);

  const std::vector<Removal> resolved =
      ace::commands::selected_removals(state.document(), state.registry(), selection);
  REQUIRE(resolved.size() == 3);
  CHECK(resolved[0].content == top);
  CHECK(resolved[1].content == bottom);
  CHECK(resolved[2].content == middle);
  // Each pair is the `{content, layer}` `scene::cells` reports for that content.
  for (const Removal& removal : resolved) {
    bool matched = false;
    for (const Cell& cell : listed) {
      if (cell.id == removal.content) {
        CHECK(cell.layer == removal.layer);
        matched = true;
      }
    }
    CHECK(matched);
  }

  // Pure: resolving mutated nothing.
  CHECK(depth(state) == depth_before);
  CHECK(revision(state) == revision_before);
  CHECK(cells_of(state).size() == 3);
}

// --- Root composition only (Constraint 12 / D-cells_remove-8) ------------------------------

TEST_CASE("cells remove: a layer in a NESTED composition is not removable by this verb") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "nested_scope");

  arbc::ObjectId nested_content;
  arbc::ObjectId nested_layer;
  dispatch(state, Command{"nested", [&](arbc::Document& doc) {
                            const arbc::ObjectId child = doc.add_composition(32.0, 32.0);
                            nested_content = doc.add_content(
                                std::make_shared<arbc::SolidContent>(arbc::Rgba{1.0F, 0, 0, 1.0F}));
                            nested_layer = doc.add_layer(nested_content, arbc::Affine::identity());
                            doc.attach_layer(child, nested_layer);
                          }});
  REQUIRE(nested_layer.valid());

  const std::size_t depth_before = depth(state);
  const std::uint64_t revision_before = revision(state);
  // Symmetric with `add_cell`, which only inserts into the root; entered/isolated nested
  // scope is `editor.panels.layers`'.
  CHECK_FALSE(ace::scene::remove_cell(state.document(), nested_content, nested_layer));
  CHECK(depth(state) == depth_before);
  CHECK(revision(state) == revision_before);
}

TEST_CASE("cells remove: a document with no root composition removes nothing") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "no_composition");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));

  // A freshly created project has no composition at all — the arm `add_cell` refuses with
  // "no root composition to place a cell in", which `remove_cell` mirrors as a plain false.
  arbc::ObjectId orphan_content;
  arbc::ObjectId orphan_layer;
  dispatch(state, Command{"orphan", [&](arbc::Document& doc) {
                            orphan_content = doc.add_content(
                                std::make_shared<arbc::SolidContent>(arbc::Rgba{0, 0, 1.0F, 1.0F}));
                            orphan_layer = doc.add_layer(orphan_content, arbc::Affine::identity());
                          }});
  REQUIRE(orphan_content.valid());
  REQUIRE(orphan_layer.valid());

  const std::size_t depth_before = depth(state);
  const std::uint64_t revision_before = revision(state);
  CHECK_FALSE(ace::scene::remove_cell(state.document(), orphan_content, orphan_layer));
  CHECK(depth(state) == depth_before);
  CHECK(revision(state) == revision_before);
}

// --- Rendered output: round-trip byte-INVARIANCE (no new golden) --------------------------

TEST_CASE("cells remove: deleting an inserted cell restores the rendered bytes exactly") {
  const ace::project::ProbeDocument probe = ace::project::build_probe_document();
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);

  const ace::render::Srgb8Image before = ace::render::render_document_srgb8(
      *probe.document, ace::project::k_probe_width, ace::project::k_probe_height);
  REQUIRE(before.pixels.size() == static_cast<std::size_t>(ace::project::k_probe_width) *
                                      static_cast<std::size_t>(ace::project::k_probe_height) * 4);

  const arbc::ObjectId child = add_child_composition(*probe.document);
  const arbc::Rect child_extent{0.0, 0.0, static_cast<double>(k_child_edge),
                                static_cast<double>(k_child_edge)};
  const arbc::Affine placement =
      ace::interact::place_in_view(arbc::Affine::identity(), ace::project::k_probe_width,
                                   ace::project::k_probe_height, child_extent);
  const arbc::expected<arbc::ObjectId, std::string> added = ace::scene::add_cell(
      *probe.document, registry, "org.arbc.nested", std::to_string(child.value), placement);
  REQUIRE(added.has_value());

  const ace::render::Srgb8Image middle = ace::render::render_document_srgb8(
      *probe.document, ace::project::k_probe_width, ace::project::k_probe_height);
  // The insert genuinely changed the image, so the invariance below cannot pass vacuously —
  // and it is the SHIPPED golden, so the middle state is a known-good baseline rather than a
  // new one this leaf invents.
  CHECK(middle.pixels != before.pixels);
  CHECK(ace_test::compare_golden("cells_insert_nested_64x64.rgba8", middle.pixels));

  arbc::ObjectId layer;
  for (const Cell& cell : ace::scene::cells(*probe.document, registry)) {
    if (cell.id == *added) {
      layer = cell.layer;
    }
  }
  REQUIRE(layer.valid());
  REQUIRE(ace::scene::remove_cell(*probe.document, *added, layer));

  const ace::render::Srgb8Image after = ace::render::render_document_srgb8(
      *probe.document, ace::project::k_probe_width, ace::project::k_probe_height);
  // Byte-identical to the pre-insert render: the delete is exactly the inverse, and the
  // retained (journal-held) content record contributes no pixel.
  CHECK(after.pixels == before.pixels);
}
