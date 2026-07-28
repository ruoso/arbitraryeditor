// editor.canvas.history_snapshot_adopt — L1 headless units for the History panel's read
// of the LIBRARY's published history projection (arch A18, as amended). v0.4.0's
// `arbc::Journal::history()` publishes an any-thread immutable snapshot of the projection a
// panel draws — one `HistoryRow{name, byte_cost}` per stored entry, rows shared by pointer —
// so the host-side `HistoryPublisher`/`HistorySnapshot` mirror is retired. These cases pin the
// EDITOR-owned half of that read: the `commands` verbs (dispatch/undo/redo) produce the right
// rows+cursor through `state.history()`, `navigate_to`'s clamped end-stopped walk, the
// bypassing-`scene`-edit path (which the retired post-edit hook used to cover and the library now
// covers structurally), the pure `clamp_history_cursor` bounds helper fed synthetic
// out-of-bounds inputs, and the concurrent-reader case that gives the two-atomic-loads-plus-clamp
// read TSan/ASan coverage on every lane. The host-publisher mechanics (refresh stamp-guard,
// pointer-identity on an unchanged journal, held-snapshot immutability) retired WITH the mirror —
// their subject is now a libarbc guarantee, pinned for existence by arbc_pin_test.cpp.
// Mirrors the ScratchDir + create_project fixture pattern of undo_test.cpp / camera_model_test.cpp.

#include <ace/commands/app_state.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/camera.hpp>
#include <ace/writer/writer_thread.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/model/model.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

using ace::commands::AppState;
using ace::commands::clamp_history_cursor;
using ace::commands::Command;
using ace::commands::dispatch;
using ace::commands::HistoryModel;
using ace::commands::navigate_to;

namespace {

// A temp dir wiped on entry and exit, named distinctly so this suite never collides
// with the other suites in the one ace_tests binary.
struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_history_publish_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

AppState fresh_session(const ScratchDir& scratch, const ace::platform::FileSystem& fs,
                       const char* leaf) {
  auto created = ace::project::create_project(fs, scratch.root / leaf);
  REQUIRE(created.has_value());
  return AppState(std::move(*created));
}

// The entry name at index `i` of a history model — the payload the panel renders. The rows
// are shared_ptrs into the library's immutable snapshot.
const std::string& row_name(const HistoryModel& model, std::size_t i) {
  return (*model.rows)[i]->name;
}

// A session carrying one content + one layer to drive NAMED transactions against.
// Only `doc.transact(name)` puts a name on a journal entry — the Document's
// self-committing wrappers (`add_content`, `add_layer`, …) commit unnamed — so the two
// setup edits are anonymous entries of their own. `base` records how many entries
// precede the named ones, exactly as history_e2e_test.cpp indexes from `depth - 3`.
struct Session {
  AppState state;
  arbc::ObjectId layer{};
  std::size_t base = 0;
};

Session make_session(const ScratchDir& scratch, const ace::platform::FileSystem& fs,
                     const char* leaf) {
  Session session{fresh_session(scratch, fs, leaf)};
  arbc::ObjectId content{};
  dispatch(session.state, Command{"add_content", [&content](arbc::Document& doc) {
                                    content = doc.add_content(std::make_shared<arbc::SolidContent>(
                                        arbc::Rgba{0.0F, 0.5F, 0.0F, 1.0F}));
                                  }});
  dispatch(session.state, Command{"add_layer", [content, &session](arbc::Document& doc) {
                                    session.layer =
                                        doc.add_layer(content, arbc::Affine::identity());
                                  }});
  session.base = session.state.document().journal().depth();
  return session;
}

// One NAMED edit producing exactly one journal entry carrying `name` — the payload the
// History panel renders. No coalescing key, so consecutive edits never fold.
Command named_edit(const Session& session, std::string name, double opacity) {
  const arbc::ObjectId layer = session.layer;
  return Command{name, [name, layer, opacity](arbc::Document& doc) {
                   arbc::Model::Transaction txn = doc.transact(name);
                   txn.set_opacity(layer, opacity);
                   const auto committed = txn.commit();
                   CHECK(committed.has_value());
                 }};
}

// Seed `count` distinctly named edits ("edit#0", "edit#1", …), each a distinct opacity
// so no commit is a no-op.
void seed(Session& session, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    dispatch(session.state,
             named_edit(session, "edit#" + std::to_string(i), 0.9 - 0.05 * static_cast<double>(i)));
  }
}

arbc::Registry camera_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  return registry;
}

// A hand-built history row — the synthetic input the clamp unit is fed, independent of any
// live journal.
std::shared_ptr<const arbc::HistoryRow> make_row(std::string name) {
  return std::make_shared<const arbc::HistoryRow>(arbc::HistoryRow{std::move(name), 0});
}

} // namespace

TEST_CASE("history model: a fresh session yields a non-null empty snapshot") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = fresh_session(scratch, fs, "fresh");

  // Frame 0, before any edit: the panel must still get a valid pointer. The library publishes a
  // non-null empty snapshot for a journal with no entries, so no host-side seed is needed.
  const HistoryModel model = state.history();
  REQUIRE(model.rows != nullptr);
  CHECK(model.rows->empty());
  CHECK(model.cursor == 0);
}

TEST_CASE("history model: dispatch appends the entry name and advances the clamped cursor") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  Session session = make_session(scratch, fs, "dispatch");
  const std::size_t base = session.base;

  seed(session, 3);

  const HistoryModel model = session.state.history();
  REQUIRE(model.rows != nullptr);
  REQUIRE(model.rows->size() == base + 3);
  CHECK(row_name(model, base + 0) == "edit#0");
  CHECK(row_name(model, base + 1) == "edit#1");
  CHECK(row_name(model, base + 2) == "edit#2");
  CHECK(model.cursor == base + 3);
  // The published shape agrees with the live journal it was built from.
  CHECK(model.rows->size() == session.state.document().journal().depth());
  CHECK(model.cursor == session.state.document().journal().cursor());
}

TEST_CASE("history model: undo and redo move the cursor and leave the rows") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  Session session = make_session(scratch, fs, "navverbs");
  const std::size_t base = session.base;

  seed(session, 3);

  REQUIRE(ace::commands::undo(session.state).moved);
  {
    // The applied/redoable split the panel dims on: the rows are untouched, only the
    // cursor moved.
    const HistoryModel model = session.state.history();
    REQUIRE(model.rows->size() == base + 3);
    CHECK(row_name(model, base + 2) == "edit#2");
    CHECK(model.cursor == base + 2);
  }

  REQUIRE(ace::commands::redo(session.state).moved);
  {
    const HistoryModel model = session.state.history();
    CHECK(model.rows->size() == base + 3);
    CHECK(row_name(model, base + 2) == "edit#2");
    CHECK(model.cursor == base + 3);
  }
}

TEST_CASE("history model: a commit after an undo republishes the truncated row list") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  Session session = make_session(scratch, fs, "truncate");
  const std::size_t base = session.base;

  seed(session, 3);
  REQUIRE(ace::commands::undo(session.state).moved);
  REQUIRE(ace::commands::undo(session.state).moved);
  REQUIRE(session.state.document().journal().cursor() == base + 1);

  // A fresh commit off a rewound cursor DROPS the redo tail before appending, so the
  // published list must SHRINK — the snapshot tracks trimming, not just growth. This is
  // exactly why `depth()` is non-monotonic and the read must clamp (D-history_snapshot_adopt-3).
  dispatch(session.state, named_edit(session, "edit#3", 0.25));

  const HistoryModel model = session.state.history();
  REQUIRE(model.rows->size() == base + 2);
  CHECK(row_name(model, base + 0) == "edit#0");
  CHECK(model.rows->back()->name == "edit#3");
  CHECK(model.cursor == base + 2);
}

TEST_CASE("history clamp: clamp_history_cursor lands in [0, rows.size()] and keeps affordances "
          "in-bounds") {
  // The correctness-critical branch, exercised with SYNTHETIC out-of-bounds inputs a
  // single-threaded live-journal test cannot otherwise reach (Constraint 3): the cursor and the
  // row list are two independent published atoms, so a cursor from a later generation can name a
  // depth larger than this snapshot's size.
  const arbc::HistoryView empty;
  CHECK(clamp_history_cursor(empty, 0) == 0);
  CHECK(clamp_history_cursor(empty, 5) == 0); // out-of-range against an empty snapshot

  arbc::HistoryView rows;
  rows.push_back(make_row("a"));
  rows.push_back(make_row("b"));
  rows.push_back(make_row("c"));

  CHECK(clamp_history_cursor(rows, 0) == 0);
  CHECK(clamp_history_cursor(rows, 2) == 2);
  CHECK(clamp_history_cursor(rows, 3) == 3);    // the tip is a valid cursor (redo tail empty)
  CHECK(clamp_history_cursor(rows, 4) == 3);    // one past the tip clamps down
  CHECK(clamp_history_cursor(rows, 1000) == 3); // a far-future generation clamps to the tip
  CHECK(clamp_history_cursor(rows, SIZE_MAX) == 3);

  // Whatever raw cursor arrives, the clamped value keeps both affordance reads in-bounds:
  // the "Undo <name>" row is `rows[c-1]` (c>0), the "Redo <name>" row is `rows[c]` (c<size).
  for (std::size_t raw :
       {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{9}, SIZE_MAX}) {
    const std::size_t c = clamp_history_cursor(rows, raw);
    REQUIRE(c <= rows.size());
    if (c > 0) {
      CHECK(rows[c - 1] != nullptr); // Undo affordance indexes a real row
    }
    if (c < rows.size()) {
      CHECK(rows[c] != nullptr); // Redo affordance indexes a real row
    }
  }
}

TEST_CASE("history navigate_to: walks the cursor to an arbitrary target in both directions") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  Session session = make_session(scratch, fs, "navwalk");
  AppState& state = session.state;

  seed(session, 4);
  const std::size_t depth = state.document().journal().depth();
  REQUIRE(depth == session.base + 4);
  REQUIRE(state.document().journal().cursor() == depth);

  const auto back = navigate_to(state, 1);
  CHECK(back.steps == depth - 1);
  CHECK(back.cursor == 1);
  CHECK(back.can_undo);
  CHECK(back.can_redo);
  CHECK(state.history().cursor == 1);
  CHECK(state.history().rows->size() == depth); // rows survive navigation

  const auto forward = navigate_to(state, depth);
  CHECK(forward.steps == depth - 1);
  CHECK(forward.cursor == depth);
  CHECK_FALSE(forward.can_redo);
  CHECK(state.history().cursor == depth);
}

TEST_CASE("history navigate_to: clamps out-of-range targets and end-stops") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  Session session = make_session(scratch, fs, "navclamp");
  AppState& state = session.state;

  seed(session, 3);
  const std::size_t depth = state.document().journal().depth();

  // Past the tip clamps to the tip. From the tip that is a zero-step no-op.
  const auto high = navigate_to(state, depth + 10);
  CHECK(high.cursor == depth);
  CHECK(high.steps == 0);

  // The base row's target: all the way down, nothing left to undo.
  const auto low = navigate_to(state, 0);
  CHECK(low.cursor == 0);
  CHECK(low.steps == depth);
  CHECK_FALSE(low.can_undo);
  CHECK(low.can_redo);
  CHECK_FALSE(state.document().journal().can_undo());
  CHECK(state.history().cursor == 0);

  // And back up past the tip from the base — the clamp works from below too.
  const auto up = navigate_to(state, depth + 10);
  CHECK(up.cursor == depth);
  CHECK(up.steps == depth);
}

TEST_CASE("history navigate_to: targeting the current cursor is a zero-step no-op") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  Session session = make_session(scratch, fs, "navnoop");
  AppState& state = session.state;

  seed(session, 3);
  const std::size_t cursor = state.document().journal().cursor();
  const std::uint64_t revision = state.document().pin()->revision();

  // Clicking the current head: no verb runs, so no forward publish and no revision bump.
  const auto same = navigate_to(state, cursor);
  CHECK(same.steps == 0);
  CHECK(same.cursor == cursor);
  CHECK(same.revision == revision);
  CHECK(state.document().pin()->revision() == revision);
  CHECK(state.document().journal().depth() == cursor);
}

TEST_CASE("history model: a bare scene transaction shows up in state.history() with no verb") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = fresh_session(scratch, fs, "hook");

  const arbc::Registry registry = camera_registry();
  dispatch(state, Command{"add_composition",
                          [](arbc::Document& doc) { doc.add_composition(64.0, 64.0); }});
  arbc::ObjectId camera{};
  dispatch(state, Command{"add_camera", [&registry, &camera](arbc::Document& doc) {
                            camera = ace::scene::add_camera(doc, registry, "cam",
                                                            ace::scene::Resolution{64, 48},
                                                            arbc::Affine::identity());
                          }});
  REQUIRE(camera.value != 0);

  // The edit seam exactly as `CanvasView::apply_edit` now assembles it
  // (D-history_snapshot_adopt-4): ONE unit of writer-thread work carrying JUST the mutation — the
  // retired post-edit hook is gone, because the library republishes `journal().history()` inside
  // the commit regardless of path. This is the case that PROVES the retirement is sound: a bare
  // `scene::` transaction that never touches a `commands` verb still shows up in `state.history()`.
  ace::writer::WriterThread writer;
  const auto apply_edit = [&](const std::function<void()>& edit) {
    writer.submit_sync([&] { edit(); });
  };

  const std::size_t rows_before = state.history().rows->size();

  // A BARE scene transaction inside a raw apply_edit closure — the camera-inspector
  // shape (src/app/camera_inspector.cpp). No verb, no hook; only the library's per-commit
  // republish covers it.
  apply_edit([&] {
    ace::scene::set_camera_resolution(state.document(), registry, camera,
                                      ace::scene::Resolution{32, 24});
  });

  const HistoryModel after = state.history();
  REQUIRE(after.rows->size() > rows_before);
  CHECK(after.rows->back()->name == "set_camera_resolution");
  CHECK(after.cursor == after.rows->size());
}

TEST_CASE("history model: a spawned reader walks published snapshots while the writer commits") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  Session session = make_session(scratch, fs, "concurrent");
  AppState& state = session.state;
  const std::size_t base = session.base;

  seed(session, 2);

  // The inverted control: the PRE-change panel could not be written this way at all — a reader
  // thread calling `entry_at` IS the race this leaf's ancestor removed. Here the reader does two
  // independent atomic loads (`journal().history()` + `journal().cursor()`) and a clamp, touching
  // nothing but atomics and the immutable values behind them, so the whole loop is clean under
  // TSan (this file runs on every lane, gcc-tsan included).
  std::atomic<bool> done{false};
  std::atomic<std::size_t> reads{0};
  std::atomic<bool> consistent{true};

  std::thread reader([&] {
    while (!done.load(std::memory_order_relaxed)) {
      const HistoryModel model = state.history();
      if (!model.rows) {
        consistent.store(false, std::memory_order_relaxed);
        continue;
      }
      // Every observation satisfies `cursor <= rows->size()` — the clamp guarantees it even
      // though the two loads came from possibly-different generations — and every named entry is
      // a fully built string. (Indices below `base` are the fixture's anonymous setup entries —
      // the Document's self-committing wrappers journal no name.)
      if (model.cursor > model.rows->size()) {
        consistent.store(false, std::memory_order_relaxed);
      }
      for (std::size_t i = 0; i < model.rows->size(); ++i) {
        if (i >= base && (*model.rows)[i]->name.empty()) {
          consistent.store(false, std::memory_order_relaxed);
        }
      }
      reads.fetch_add(1, std::memory_order_relaxed);
    }
  });

  for (int i = 0; i < 64; ++i) {
    dispatch(session.state, named_edit(session, "commit#" + std::to_string(i),
                                       0.5 + 0.005 * static_cast<double>(i)));
    ace::commands::undo(state);
    ace::commands::redo(state);
    navigate_to(state, 1);
    navigate_to(state, state.document().journal().depth());
  }

  done.store(true, std::memory_order_relaxed);
  reader.join();

  CHECK(consistent.load());
  CHECK(reads.load() > 0);
  CHECK(state.history().cursor == state.document().journal().cursor());
  CHECK(state.history().rows->size() == state.document().journal().depth());
}
