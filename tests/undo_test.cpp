// editor.project.undo — L1 headless units proving undo/redo IS libarbc's
// document-wide transaction journal (D15), never a reimplemented stack. Undo/redo
// are pure cursor NAVIGATION (ordinary forward publishes, revision +1), continuous
// gestures coalesce to one step via `next_gesture_key()`, and transient state stays
// off the journal. Mirrors the ScratchDir + create_project + add_solid_content
// fixture pattern of commands_test.cpp; runs under ASan/TSan over the live
// workspace-backed Document (A4/§9).

#include <ace/commands/app_state.hpp>
#include <ace/commands/selection.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/model/journal_entry.hpp>
#include <arbc/model/model.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

using ace::commands::AppState;
using ace::commands::Command;
using ace::commands::dispatch;
using ace::commands::Selection;

namespace {

// A temp dir wiped on entry and exit, named distinctly so the undo suite never
// collides with the other suites in the one ace_tests binary.
struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_undo_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// A proving command that adds a full-frame solid content — one Document wrapper
// call, so exactly one journal entry (the concrete taxonomy is a later edit leaf;
// this is a fixture, not shipped state).
Command add_solid_content(arbc::ObjectId& out_id) {
  return Command{"add_content", [&out_id](arbc::Document& doc) {
                   out_id = doc.add_content(
                       std::make_shared<arbc::SolidContent>(arbc::Rgba{0.0F, 0.5F, 0.0F, 1.0F}));
                 }};
}

// A single "add composition" edit — one wrapper call, one entry.
Command add_composition() {
  return Command{"add_composition", [](arbc::Document& doc) { doc.add_composition(64.0, 64.0); }};
}

// Build a layer to drive gesture edits against (content + layer, each its own
// entry). Returns the layer id.
arbc::ObjectId make_layer(AppState& state) {
  arbc::ObjectId content{};
  dispatch(state, add_solid_content(content));
  arbc::ObjectId layer{};
  dispatch(state, Command{"add_layer", [content, &layer](arbc::Document& doc) {
                            layer = doc.add_layer(content, arbc::Affine::identity());
                          }});
  return layer;
}

// One coalesced gesture-frame command: opens a transaction stamped with `key` and
// nudges the layer's opacity, so consecutive same-key frames fold to one entry.
Command stroke_frame(arbc::ObjectId layer, std::uint64_t key, double opacity) {
  return Command{"stroke", [layer, key, opacity](arbc::Document& doc) {
                   auto t = doc.transact("stroke");
                   t.coalesce(key);
                   t.set_opacity(layer, opacity);
                   const auto committed = t.commit();
                   CHECK(committed.has_value());
                 }};
}

} // namespace

TEST_CASE("undo/redo navigate the journal cursor LIFO as forward publishes") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "roundtrip");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));

  const arbc::Journal& journal = state.document().journal();
  const std::size_t depth0 = journal.depth();
  const std::size_t cursor0 = journal.cursor();

  dispatch(state, add_composition());
  arbc::ObjectId content{};
  const auto e2 = dispatch(state, add_solid_content(content));
  CHECK(journal.depth() == depth0 + 2);
  CHECK(journal.cursor() == cursor0 + 2);
  CHECK_FALSE(journal.can_redo());

  // Undo the second edit (LIFO): the cursor drops by one, redo becomes possible,
  // and the revision ADVANCES — navigation is a forward publish, never a rewind.
  const auto u2 = ace::commands::undo(state);
  CHECK(u2.moved);
  CHECK(journal.cursor() == cursor0 + 1);
  CHECK(u2.can_undo);
  CHECK(u2.can_redo);
  CHECK(u2.revision > e2.revision);

  // Undo the first edit — back to the pre-edit cursor, nothing left to undo.
  const auto u1 = ace::commands::undo(state);
  CHECK(u1.moved);
  CHECK(journal.cursor() == cursor0);
  CHECK_FALSE(u1.can_undo);
  CHECK(u1.can_redo);
  CHECK(u1.revision > u2.revision);

  // Redo walks forward again; at the tip redo is exhausted, and each redo advances
  // the revision (forward publish) too.
  const auto r1 = ace::commands::redo(state);
  CHECK(r1.moved);
  CHECK(journal.cursor() == cursor0 + 1);
  const auto r2 = ace::commands::redo(state);
  CHECK(r2.moved);
  CHECK(journal.cursor() == cursor0 + 2);
  CHECK_FALSE(r2.can_redo);
  CHECK(r2.revision > r1.revision);
}

TEST_CASE("undo/redo on a fresh session are safe no-ops") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "empty");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));

  CHECK_FALSE(state.document().journal().can_undo());
  const auto u = ace::commands::undo(state); // nothing to undo -> moved == false, no throw
  CHECK_FALSE(u.moved);
  CHECK_FALSE(u.can_undo);

  CHECK_FALSE(state.document().journal().can_redo());
  const auto r = ace::commands::redo(state); // symmetric at the tip
  CHECK_FALSE(r.moved);
  CHECK_FALSE(r.can_redo);
}

TEST_CASE("a continuous gesture coalesces into one undo step") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "gesture");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));

  const arbc::ObjectId layer = make_layer(state);
  REQUIRE(layer.valid());

  const arbc::Journal& journal = state.document().journal();
  const std::size_t depth_before = journal.depth();
  const std::size_t cursor_before = journal.cursor();

  // One gesture: N frames sharing ONE key from next_gesture_key(). The library
  // folds consecutive same-key commits into the tip entry (Constraint 3).
  const std::uint64_t key = state.next_gesture_key();
  CHECK(key != arbc::k_no_coalesce);
  constexpr int k_frames = 5;
  for (int i = 0; i < k_frames; ++i) {
    dispatch(state, stroke_frame(layer, key, 0.1 * (i + 1)));
  }

  // The whole gesture is ONE undo unit: depth and cursor each grew by exactly 1.
  CHECK(journal.depth() == depth_before + 1);
  CHECK(journal.cursor() == cursor_before + 1);

  // A SINGLE undo unwinds the entire gesture back to the pre-gesture cursor.
  const auto u = ace::commands::undo(state);
  CHECK(u.moved);
  CHECK(journal.cursor() == cursor_before);
  CHECK(u.can_redo);
}

TEST_CASE("distinct gesture keys stay distinct undo units even when adjacent") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "collision");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));

  const arbc::ObjectId layer = make_layer(state);
  REQUIRE(layer.valid());

  const arbc::Journal& journal = state.document().journal();
  const std::size_t depth_before = journal.depth();
  const std::size_t cursor_before = journal.cursor();

  // Two gestures with distinct, monotonic, non-zero keys.
  const std::uint64_t k1 = state.next_gesture_key();
  const std::uint64_t k2 = state.next_gesture_key();
  CHECK(k1 != arbc::k_no_coalesce);
  CHECK(k2 != arbc::k_no_coalesce);
  CHECK(k1 != k2);
  CHECK(k2 > k1); // monotonic

  for (int i = 0; i < 3; ++i) {
    dispatch(state, stroke_frame(layer, k1, 0.2 + 0.1 * i));
  }
  for (int i = 0; i < 3; ++i) { // adjacent to gesture 1, but a DIFFERENT key
    dispatch(state, stroke_frame(layer, k2, 0.6 + 0.1 * i));
  }

  // Two undo units despite adjacency — the keys never collided.
  CHECK(journal.depth() == depth_before + 2);
  CHECK(journal.cursor() == cursor_before + 2);

  const auto u2 = ace::commands::undo(state); // reverts gesture 2
  CHECK(u2.moved);
  CHECK(journal.cursor() == cursor_before + 1);
  const auto u1 = ace::commands::undo(state); // reverts gesture 1
  CHECK(u1.moved);
  CHECK(journal.cursor() == cursor_before);
}

TEST_CASE("next_gesture_key is monotonic and never returns k_no_coalesce") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "keys");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));

  std::uint64_t prev = state.next_gesture_key();
  CHECK(prev != arbc::k_no_coalesce);
  for (int i = 0; i < 100; ++i) {
    const std::uint64_t next = state.next_gesture_key();
    CHECK(next != arbc::k_no_coalesce);
    CHECK(next > prev); // strictly increasing -> never repeats within a session
    prev = next;
  }
}

TEST_CASE("undo/redo advance the revision and never mark the session clean") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "dirty");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));

  // Publish to establish a clean baseline.
  const auto published = ace::commands::save_project(state, fs);
  REQUIRE(published.has_value());
  CHECK_FALSE(state.is_dirty());
  const std::uint64_t saved_rev = state.document().pin()->revision();

  // An edit dirties the session.
  dispatch(state, add_composition());
  CHECK(state.is_dirty());

  // Undo back to the saved CONTENT. Because navigation is a forward publish, the
  // revision advances PAST saved_revision_ — so the session stays dirty even though
  // the content matches the last save (D-undo-4 / Constraint 4: conservative toward
  // dirty, never a false clean, and undo/redo never call mark_saved).
  const auto u = ace::commands::undo(state);
  CHECK(u.moved);
  CHECK(u.revision > saved_rev);
  CHECK(state.is_dirty());

  // Redo likewise never restores clean.
  const auto r = ace::commands::redo(state);
  CHECK(r.moved);
  CHECK(r.revision > u.revision);
  CHECK(state.is_dirty());
}

TEST_CASE("transient state (Selection) never touches the journal") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "transient");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));

  // A committed edit so the journal is non-empty (a cursor value to hold constant).
  dispatch(state, add_composition());
  const arbc::Journal& journal = state.document().journal();
  const std::size_t depth_before = journal.depth();
  const std::size_t cursor_before = journal.cursor();
  const std::uint64_t rev_before = state.document().pin()->revision();

  // The D15 transient stand-in for viewport-camera navigation: a Selection mutation
  // is session state, never a transaction — it leaves depth, cursor, and revision
  // unchanged and is invisible to undo (Constraint 5).
  Selection& sel = state.selection();
  sel.select(arbc::ObjectId{1});
  sel.add(arbc::ObjectId{2});
  sel.clear();

  CHECK(journal.depth() == depth_before);
  CHECK(journal.cursor() == cursor_before);
  CHECK(state.document().pin()->revision() == rev_before);
  CHECK_FALSE(journal.can_redo()); // no phantom redo entry appeared
}
