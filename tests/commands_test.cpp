// editor.project.app_state — L1 headless units for the process's one owned
// project session and the action→libarbc-transaction dispatch seam. Reuses the
// ScratchDir filesystem-round-trip pattern (tests/platform_test.cpp). The
// workspace-backed session holds libarbc's checkpointable Document (its live
// HousekeepingThread) for the whole case and runs under ASan/TSan (A4/§9).

#include <ace/commands/app_state.hpp>
#include <ace/commands/selection.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/model/model.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <system_error>
#include <type_traits>
#include <utility>

using ace::commands::AppState;
using ace::commands::Command;
using ace::commands::dispatch;
using ace::commands::open_or_create_app_state;
using ace::commands::Selection;
using ace::project::OpenError;

namespace {

// A temp dir wiped on entry and exit (the platform_test pattern), named distinctly
// so the commands suite never collides with the platform/project suites in the
// one ace_tests binary.
struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_commands_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// A proving command that adds a full-frame solid content — a single Document
// wrapper call, so exactly one journal entry (the concrete taxonomy is a later
// edit leaf; this is a fixture, not shipped state).
Command add_solid_content(arbc::ObjectId& out_id) {
  return Command{"add_content", [&out_id](arbc::Document& doc) {
                   out_id = doc.add_content(
                       std::make_shared<arbc::SolidContent>(arbc::Rgba{0.0F, 0.5F, 0.0F, 1.0F}));
                 }};
}

} // namespace

// AppState is the single owner: move-only, never copyable (A7).
static_assert(!std::is_copy_constructible_v<AppState>);
static_assert(!std::is_copy_assignable_v<AppState>);
static_assert(std::is_move_constructible_v<AppState>);

TEST_CASE("AppState owns one workspace-backed Document, a seeded Registry, and the layout") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "owned";

  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));

  // A7: the session holds exactly one live workspace-backed Document.
  CHECK(state.document().workspace_backed());
  CHECK_FALSE(state.rebuilt_from_canonical());

  // D-open-7: the persistent kind Registry is register_builtin_kinds-seeded here.
  CHECK(state.registry().size() > 0);
  CHECK(state.registry().factory(arbc::SolidContent::kind_id) != nullptr);

  // The layout points save at <root>/project.arbc (the dump target).
  CHECK(state.layout().canonical == root / "project.arbc");

  // Moving transfers the single ownership; the moved-to session drives the Document.
  AppState moved(std::move(state));
  CHECK(moved.document().workspace_backed());
}

// editor.project.reopen_degradation_notice — the A19 count is FERRIED onto the session,
// not recomputed (Constraint 1). `open_project` produces it once at bootstrap; the values
// it produces across the open paths (0 on a rebuild, 0 on a content-free map, non-zero
// only on the never-saved lossy reopen) are pinned upstream in tests/project_open_test.cpp
// and deliberately not re-asserted here. What this pins is the ferry itself and, just as
// importantly, that a session with nothing to report never reports a phantom loss — a
// notice on a clean project would be worse than the silence it replaces.
TEST_CASE("AppState carries the reopen unbindable-content count off the OpenedProject") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;

  SECTION("a lossy open's count arrives verbatim on the session") {
    auto created = ace::project::create_project(fs, scratch.root / "lossy");
    REQUIRE(created.has_value());
    // Stand in for the never-saved workspace-mapped reopen: the only path that yields a
    // non-zero count. This unit owns the CARRY, so the count is scripted rather than
    // re-derived from a hand-built lossy workspace.
    created.value().unbindable_content_records = 7;
    AppState state(std::move(*created));
    CHECK(state.unbindable_content_records() == 7);
  }

  SECTION("a rebuild-from-canonical session reports no loss") {
    auto created = ace::project::create_project(fs, scratch.root / "rebuilt");
    REQUIRE(created.has_value());
    created.value().rebuilt_from_canonical = true;
    AppState state(std::move(*created));
    CHECK(state.rebuilt_from_canonical());
    CHECK(state.unbindable_content_records() == 0);
  }

  SECTION("a fresh create_project session reports no loss") {
    auto created = ace::project::create_project(fs, scratch.root / "fresh");
    REQUIRE(created.has_value());
    AppState state(std::move(*created));
    CHECK(state.unbindable_content_records() == 0);
  }
}

TEST_CASE("Selection is transient project-level state — never a transaction") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "sel");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));

  const std::size_t depth_before = state.document().journal().depth();
  const std::uint64_t rev_before = state.document().pin()->revision();

  const arbc::ObjectId a{1};
  const arbc::ObjectId b{2};
  Selection& sel = state.selection();

  CHECK(sel.empty());
  CHECK_FALSE(sel.primary().valid());

  sel.select(a);
  CHECK(sel.size() == 1);
  CHECK(sel.contains(a));
  CHECK(sel.primary() == a);

  sel.add(b);
  CHECK(sel.size() == 2);
  CHECK(sel.primary() == b);
  sel.add(b); // idempotent: already present, no duplicate
  CHECK(sel.size() == 2);

  sel.toggle(a); // present -> removed; primary falls back to the remaining tail
  CHECK_FALSE(sel.contains(a));
  CHECK(sel.primary() == b);
  sel.toggle(a); // absent -> added; primary becomes a
  CHECK(sel.contains(a));
  CHECK(sel.primary() == a);

  sel.select(a); // replace-all collapses to a single entry
  CHECK(sel.size() == 1);
  CHECK(sel.items().size() == 1);

  sel.clear();
  CHECK(sel.empty());
  CHECK_FALSE(sel.primary().valid());

  // D15: selection is not scene data — none of the above touched the journal or
  // bumped the document revision.
  CHECK(state.document().journal().depth() == depth_before);
  CHECK(state.document().pin()->revision() == rev_before);
}

TEST_CASE("dispatch turns one command into exactly one libarbc transaction") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "dispatch");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));

  const std::size_t depth0 = state.document().journal().depth();
  CHECK_FALSE(state.document().journal().can_undo());

  // Each Document wrapper is its own transaction: one journal entry, one revision
  // bump per command (the one-command-one-entry boundary).
  const auto add_comp = dispatch(state, Command{"add_composition", [](arbc::Document& doc) {
                                                  doc.add_composition(64.0, 64.0);
                                                }});
  CHECK(add_comp.journal_entries_added == 1);
  CHECK(state.document().journal().depth() == depth0 + 1);
  CHECK(state.document().journal().can_undo()); // free undo — the journal IS undo

  arbc::ObjectId content{};
  const auto add_content = dispatch(state, add_solid_content(content));
  CHECK(add_content.journal_entries_added == 1);
  CHECK(content.valid());
  CHECK(add_content.revision > add_comp.revision); // the revision advanced

  const auto add_layer = dispatch(state, Command{"add_layer", [content](arbc::Document& doc) {
                                                   doc.add_layer(content, arbc::Affine::identity());
                                                 }});
  CHECK(add_layer.journal_entries_added == 1);
  CHECK(add_layer.revision > add_content.revision);

  const std::size_t depth_after = state.document().journal().depth();

  // A no-op command body appends no entry (and does not bump the revision).
  const auto noop = dispatch(state, Command{"noop", [](arbc::Document&) {}});
  CHECK(noop.journal_entries_added == 0);
  CHECK(noop.revision == add_layer.revision);

  // A command with no apply at all is also inert.
  const auto empty = dispatch(state, Command{"empty", {}});
  CHECK(empty.journal_entries_added == 0);
  CHECK(state.document().journal().depth() == depth_after);
}

TEST_CASE("open_or_create_app_state resolves a path: create-when-new, open-when-present") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;

  SECTION("a not-yet-existing directory is created") {
    const auto session = open_or_create_app_state(fs, scratch.root / "fresh");
    REQUIRE(session.has_value());
    CHECK(session.value().document().workspace_backed());
    CHECK_FALSE(session.value().rebuilt_from_canonical());
  }

  SECTION("an existing project directory is opened") {
    const std::filesystem::path root = scratch.root / "existing";
    {
      auto created = ace::project::create_project(fs, root);
      REQUIRE(created.has_value());
      REQUIRE(created.value().document->checkpoint().has_value());
    } // the first session is dropped (workspace unmapped) before reopening
    const auto session = open_or_create_app_state(fs, root);
    REQUIRE(session.has_value());
    CHECK(session.value().document().workspace_backed());
  }
}

TEST_CASE("open_or_create_app_state surfaces OpenError values, never throws") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;

  SECTION("a path that is a file is NotADirectory") {
    const std::filesystem::path file = scratch.root / "a_file";
    REQUIRE_FALSE(static_cast<bool>(fs.write_file(file, "hi")));
    const auto session = open_or_create_app_state(fs, file);
    REQUIRE_FALSE(session.has_value());
    CHECK(session.error() == OpenError::NotADirectory);
  }

  SECTION("an existing empty directory with no project is NoProject") {
    const std::filesystem::path root = scratch.root / "empty";
    REQUIRE_FALSE(static_cast<bool>(fs.make_directories(root)));
    const auto session = open_or_create_app_state(fs, root);
    REQUIRE_FALSE(session.has_value());
    CHECK(session.error() == OpenError::NoProject);
  }
}
