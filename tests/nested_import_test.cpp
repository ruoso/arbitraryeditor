// editor.import.nested — L1 headless Catch2 units for placing another project's `.arbc` as a
// BORROWED `org.arbc.nested` cell carrying the library's EXTERNAL nested reference (`params.ref`),
// not an inline copy (D11/D12/D13, A16/A31). Covers the new L1 verb `scene::add_nested_reference`
// (an unresolved external-ref cell, minted through the DEDICATED seam because the built-in
// `make_nested` factory is numeric-`ObjectId`-only, D-nested-1), the one-action-one-entry
// insert/undo/redo, the scoped insert, the `scene::classify_detail` nested arm (borrowed provenance
// from the distinct `external_composition_ref()` virtual, D-nested-5), the identity placement over
// empty bounds (D-nested-4), the save->reopen round-trip that resolves the ref INLINE through the
// editor's `FilesystemAssetSource` (pending_external_loads()==0, the child resolved, D-nested-3),
// and one `render_offline` golden of the reopen-resolved composite. The L4 entry points (the drop
// verb, the FileDialog, `drop_device_point`) are the e2e's (tests/nested_import_e2e_test.cpp) —
// `ace_tests` links no `ace::app`.
//
// No ImGui/GL/SDL (Constraint 1); runs under the ASan/TSan legs (A4/§9).

#include <ace/commands/app_state.hpp>
#include <ace/commands/cells.hpp>
#include <ace/commands/image_import.hpp>
#include <ace/interact/interact.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/import_asset.hpp>
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
#include <arbc/kind_nested/nested_content.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/model.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "golden_support.hpp"

using ace::commands::AppState;
using ace::commands::Command;
using ace::commands::dispatch;
using ace::scene::Cell;
using ace::scene::DetailSource;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_nested_import_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// A registry seeded exactly like `commands::register_editor_kinds` — builtins (which carry
// `org.arbc.nested` and `org.arbc.solid`) + camera + the A29 image kind (present so the
// classifier regression guard can mint an image cell beside a nested one).
arbc::Registry nested_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  ace::commands::register_image_kind(registry);
  return registry;
}

// A fresh workspace-backed session with a 64x64 root composition to place the nested reference in.
AppState session_with_composition(const ScratchDir& scratch, const ace::platform::FileSystem& fs,
                                  const char* leaf) {
  auto created = ace::project::create_project(fs, scratch.root / leaf);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  dispatch(state, Command{"add_composition",
                          [](arbc::Document& doc) { doc.add_composition(64.0, 64.0); }});
  return state;
}

// Build a CHILD project on disk: a single blue-filled 32x32 solid composition, saved so its
// `<root>/project.arbc` is a full serialized document the parent's external ref resolves against on
// reopen. The solid is minted through the REGISTRY (`scene::add_cell`, a proper kind token) so it
// serializes; a raw `add_content` would carry no token and fail to round-trip. Returns the absolute
// path of that `project.arbc`.
std::filesystem::path build_child_project(const ScratchDir& scratch,
                                          const ace::platform::FileSystem& fs, const char* leaf) {
  const std::filesystem::path root = scratch.root / leaf;
  {
    auto created = ace::project::create_project(fs, root);
    REQUIRE(created.has_value());
    AppState state(std::move(*created));
    dispatch(state, Command{"add_child_composition",
                            [](arbc::Document& doc) { doc.add_composition(32.0, 32.0); }});
    // "r,g,b,a,x,y,w,h": a blue solid filling the child's 32x32 composition.
    REQUIRE(ace::scene::add_cell(state.document(), state.registry(),
                                 std::string(arbc::SolidContent::kind_id),
                                 "0.1,0.2,0.8,1,0,0,32,32", arbc::Affine::identity())
                .has_value());
    REQUIRE(ace::commands::save_project(state, fs).has_value());
  } // released: workspace checkpointed + unmapped
  return ace::project::project_layout(root).canonical;
}

// The reopen registrar, mirroring `commands::register_editor_kinds` ordering (nested/solid are
// built-ins, so only camera + image are editor-authored, but the ordering is kept for token
// compatibility on reopen).
void register_editor_kinds(arbc::Registry& registry) {
  ace::scene::register_camera_kind(registry);
  ace::commands::register_image_kind(registry);
}

// Resolve the single `org.arbc.nested` cell of `document`'s root composition.
const Cell* nested_cell(const std::vector<Cell>& cells) {
  for (const Cell& cell : cells) {
    if (cell.kind_id == std::string(arbc::NestedContent::kind_id)) {
      return &cell;
    }
  }
  return nullptr;
}

} // namespace

// --- add_nested_reference: the dedicated external-ref seam (Constraint 2 / D-nested-1) -------

TEST_CASE("nested_import: add_nested_reference mints an unresolved external-ref cell") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "mint");
  const std::string uri = "/abs/path/other.arbc";

  const arbc::expected<arbc::ObjectId, std::string> added = ace::scene::add_nested_reference(
      state.document(), state.registry(), uri, arbc::Affine::translation(4.0, 5.0));
  REQUIRE(added.has_value());

  const std::vector<Cell> cells = ace::scene::cells(state.document(), state.registry());
  REQUIRE(cells.size() == 1);
  CHECK(cells[0].kind_id == std::string(arbc::NestedContent::kind_id));
  CHECK(cells[0].placement.tx == 4.0);
  CHECK(cells[0].placement.ty == 5.0);

  arbc::Content* content = state.document().resolve(cells[0].id);
  REQUIRE(content != nullptr);
  // The external reference is authored verbatim on BOTH the generic virtual and the kind's twin,
  // and the child is UNRESOLVED (`ObjectId{}`) — the placeholder-live, resolved-on-reopen shape.
  CHECK(content->external_composition_ref() == uri);
  CHECK_FALSE(content->composition_ref().valid());
}

TEST_CASE("nested_import: an empty reference URI is refused with the document untouched") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "empty");
  const std::size_t depth_baseline = state.document().journal().depth();

  const arbc::expected<arbc::ObjectId, std::string> added = ace::scene::add_nested_reference(
      state.document(), state.registry(), "", arbc::Affine::identity());
  CHECK_FALSE(added.has_value());
  CHECK(state.document().journal().depth() == depth_baseline);
  CHECK(ace::scene::cells(state.document(), state.registry()).empty());
}

// --- One action, one entry (Constraint 3 / D-one_action_one_entry-1) -------------------------

TEST_CASE("nested_import: an insert is ONE journal entry undone whole and redone on its ObjectId") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "onejournal");
  const std::size_t depth_baseline = state.document().journal().depth();

  ace::commands::InsertCellOutcome outcome;
  const Command command = ace::commands::insert_nested_reference_command(
      state.registry(), "/abs/child.arbc", arbc::Affine::translation(1.0, 2.0), outcome);
  const ace::commands::DispatchOutcome dispatched = dispatch(state, command);
  CHECK(dispatched.journal_entries_added == 1);
  CHECK(state.document().journal().depth() == depth_baseline + 1);
  CHECK(outcome.error.empty());
  REQUIRE(outcome.content.valid());
  const arbc::ObjectId minted = outcome.content;

  // One undo removes the cell whole (no orphan content).
  REQUIRE(ace::commands::undo(state).moved);
  CHECK(ace::scene::cells(state.document(), state.registry()).empty());
  CHECK(state.document().pin()->find_content(minted) == nullptr);

  // One redo restores it on the SAME ObjectId.
  REQUIRE(ace::commands::redo(state).moved);
  const std::vector<Cell> cells = ace::scene::cells(state.document(), state.registry());
  REQUIRE(cells.size() == 1);
  CHECK(cells[0].id == minted);
}

// --- Scoped insert (Constraint 4 / D-scoped_edit-1) ------------------------------------------

TEST_CASE("nested_import: a scoped insert lands in the entered composition, else Root") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "scoped");
  // A second, live composition to enter.
  arbc::ObjectId entered;
  dispatch(state, Command{"add_scope", [&entered](arbc::Document& doc) {
                            entered = doc.add_composition(48.0, 48.0);
                          }});
  REQUIRE(entered.valid());

  // Entered names a live composition -> the cell lands THERE, not in Root.
  REQUIRE(ace::scene::add_nested_reference(state.document(), state.registry(), "/abs/a.arbc",
                                           arbc::Affine::identity(), entered)
              .has_value());
  CHECK(ace::scene::cells(state.document(), state.registry(), entered).size() == 1);

  // A vanished/foreign scope degrades to Root (the fail-safe).
  const arbc::ObjectId foreign{999999};
  REQUIRE(ace::scene::add_nested_reference(state.document(), state.registry(), "/abs/b.arbc",
                                           arbc::Affine::identity(), foreign)
              .has_value());
  // The root composition now holds the second insert; the entered scope is unchanged.
  CHECK(ace::scene::cells(state.document(), state.registry()).size() == 1);
  CHECK(ace::scene::cells(state.document(), state.registry(), entered).size() == 1);
}

// --- classify_detail nested arm (Constraint 7 / D-nested-5) ----------------------------------

TEST_CASE("nested_import: classify_detail reads external_composition_ref for borrowed provenance") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;

  SECTION("an external absolute .arbc ref classifies borrowed") {
    AppState state = session_with_composition(scratch, fs, "borrowed");
    REQUIRE(ace::scene::add_nested_reference(state.document(), state.registry(), "/abs/other.arbc",
                                             arbc::Affine::identity())
                .has_value());
    const std::vector<Cell> cells = ace::scene::cells(state.document(), state.registry());
    REQUIRE(cells.size() == 1);
    // A nested cell has no editable grid and no finite detail floor (ResolutionIndependent), but it
    // still carries borrowed provenance from the distinct `external_composition_ref()` virtual.
    CHECK(cells[0].detail.source == DetailSource::ResolutionIndependent);
    CHECK(cells[0].detail.borrowed);
    CHECK_FALSE(cells[0].detail.native_pixels.has_value());
  }

  SECTION("a project-relative assets/ ref (post-Consolidate shape) classifies owned") {
    AppState state = session_with_composition(scratch, fs, "owned");
    REQUIRE(ace::scene::add_nested_reference(state.document(), state.registry(),
                                             "assets/nested/child.arbc", arbc::Affine::identity())
                .has_value());
    const std::vector<Cell> cells = ace::scene::cells(state.document(), state.registry());
    REQUIRE(cells.size() == 1);
    CHECK(cells[0].detail.source == DetailSource::ResolutionIndependent);
    CHECK_FALSE(cells[0].detail.borrowed);
  }

  SECTION("an image cell's classification is unchanged (regression guard on distinct virtuals)") {
    AppState state = session_with_composition(scratch, fs, "imageguard");
    const std::filesystem::path fixture = std::filesystem::path(ACE_FIXTURE_DIR) / "photo_12x8.ppm";
    const ace::project::BorrowedAsset asset = ace::project::borrow_asset_file(fs, fixture);
    const std::string config = ace::commands::image_config(asset.uri, asset.uri, asset.bytes);
    REQUIRE(ace::scene::add_cell(state.document(), state.registry(), ace::commands::image_kind_id,
                                 config, arbc::Affine::identity())
                .has_value());
    const std::vector<Cell> cells = ace::scene::cells(state.document(), state.registry());
    REQUIRE(cells.size() == 1);
    CHECK(cells[0].detail.source == DetailSource::ReferencedImage);
    CHECK(cells[0].detail.borrowed);
  }
}

// --- Placement (Constraint 5 / D-nested-4) ---------------------------------------------------

TEST_CASE("nested_import: place_in_view over nullopt bounds yields identity") {
  // The unattached nested content reports empty bounds, so the base placement is identity — the
  // nested analog of image's native-px 1:1. The gateway then anchors the origin at the drop point.
  const arbc::Affine identity_cam = arbc::Affine::identity();
  CHECK(ace::interact::place_in_view(identity_cam, 200, 100, std::nullopt) ==
        arbc::Affine::identity());
  // A zoomed camera still yields identity over unbounded content (A16's unbounded rule).
  const arbc::Affine zoomed{2.0, 0.0, 0.0, 2.0, 10.0, 20.0};
  CHECK(ace::interact::place_in_view(zoomed, 200, 100, std::nullopt) == arbc::Affine::identity());
}

// --- Save -> reopen round-trip: the ref resolves INLINE (the crux, D-nested-3) ----------------

TEST_CASE("nested_import: a placed reference round-trips and resolves inline on reopen") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path child_arbc = build_child_project(scratch, fs, "child");
  const std::string child_uri = child_arbc.string();

  const std::filesystem::path parent_root = scratch.root / "parent";
  {
    auto created = ace::project::create_project(fs, parent_root);
    REQUIRE(created.has_value());
    AppState state(std::move(*created));
    dispatch(state, Command{"add_composition",
                            [](arbc::Document& doc) { doc.add_composition(64.0, 64.0); }});
    REQUIRE(ace::scene::add_nested_reference(state.document(), state.registry(), child_uri,
                                             arbc::Affine::translation(16.0, 16.0))
                .has_value());
    REQUIRE(ace::commands::save_project(state, fs).has_value());
  } // released: workspace checkpointed + unmapped

  // Force the canonical rebuild so the deserialize-time loader runs the ref resolution.
  std::error_code ec;
  std::filesystem::remove_all(ace::project::project_layout(parent_root).workspace_dir, ec);

  auto reopened = ace::project::open_project(fs, parent_root, register_editor_kinds);
  REQUIRE(reopened.has_value());
  CHECK(reopened.value().rebuilt_from_canonical);
  // The editor's FilesystemAssetSource resolved the ref INLINE — no external-load is pending.
  CHECK(reopened.value().document->pending_external_loads() == 0);

  arbc::Registry load_registry = nested_registry();
  const std::vector<Cell> cells = ace::scene::cells(*reopened.value().document, load_registry);
  const Cell* nested = nested_cell(cells);
  REQUIRE(nested != nullptr);
  CHECK(nested->placement.tx == 16.0);
  CHECK(nested->placement.ty == 16.0);

  arbc::Content* content = reopened.value().document->resolve(nested->id);
  REQUIRE(content != nullptr);
  // The reference round-tripped verbatim, and the child is now a LIVE composition in the host
  // model.
  CHECK(content->external_composition_ref() == child_uri);
  CHECK(content->composition_ref().valid());
  // Borrowed provenance survives the round-trip (an external absolute URI).
  CHECK(nested->detail.borrowed);
}

// --- render_offline golden: the reopen-resolved composite (the honest end-to-end proof) -------

TEST_CASE(
    "nested_import: the reopen-resolved external composition composites to the sRGB8 golden") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path child_arbc = build_child_project(scratch, fs, "gchild");

  const std::filesystem::path parent_root = scratch.root / "gparent";
  {
    auto created = ace::project::create_project(fs, parent_root);
    REQUIRE(created.has_value());
    AppState state(std::move(*created));
    dispatch(state, Command{"add_composition",
                            [](arbc::Document& doc) { doc.add_composition(64.0, 64.0); }});
    // A 32x32 child solid anchored at (16,16) lands centred in the 64x64 parent.
    REQUIRE(ace::scene::add_nested_reference(state.document(), state.registry(),
                                             child_arbc.string(),
                                             arbc::Affine::translation(16.0, 16.0))
                .has_value());
    REQUIRE(ace::commands::save_project(state, fs).has_value());
  }
  std::error_code ec;
  std::filesystem::remove_all(ace::project::project_layout(parent_root).workspace_dir, ec);

  auto reopened = ace::project::open_project(fs, parent_root, register_editor_kinds);
  REQUIRE(reopened.has_value());
  REQUIRE(reopened.value().document->pending_external_loads() == 0);

  const ace::render::Srgb8Image image = ace::render::render_document_srgb8(
      *reopened.value().document, ace::project::k_probe_width, ace::project::k_probe_height);
  CHECK(ace_test::compare_golden("import_nested_64x64.rgba8", image.pixels));
}
