// editor.canvas.history_published_reads — L1 headless units for the published
// history snapshot (arch A18). libarbc v0.3.0 publishes the journal's cursor and
// depth as any-thread atomics but deliberately NOT its entry vector: `entry_at`
// hands out a reference into writer-owned memory a concurrent commit may
// reallocate. These cases pin the host-side publication that replaces it — the
// refresh points (the `commands` verbs AND the `CanvasHost` writer-turn hook, which
// is the only one a bare `scene::` transaction passes through), the stamp guard, the
// immutability of a held snapshot, and `navigate_to`'s clamped end-stopped walk.
// The concurrent-reader case gives the whole thing TSan/ASan coverage on every lane
// (there is no per-test sanitizer tagging). Mirrors the ScratchDir + create_project
// fixture pattern of undo_test.cpp / camera_model_test.cpp.

#include <ace/commands/app_state.hpp>
#include <ace/commands/history.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/render/canvas_host.hpp>
#include <ace/scene/camera.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/model/model.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/worker_pool.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

using ace::commands::AppState;
using ace::commands::Command;
using ace::commands::dispatch;
using ace::commands::HistorySnapshot;
using ace::commands::navigate_to;
using ace::commands::publish_history;

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

} // namespace

TEST_CASE("history publish: a fresh session publishes a non-null empty snapshot") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = fresh_session(scratch, fs, "fresh");

  // Frame 0, before any edit: the panel must still get a valid pointer, so the
  // AppState constructor publishes.
  const std::shared_ptr<const HistorySnapshot> snapshot = state.history().load();
  REQUIRE(snapshot != nullptr);
  CHECK(snapshot->names.empty());
  CHECK(snapshot->cursor == 0);
}

TEST_CASE("history publish: dispatch appends the entry name and advances the published cursor") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  Session session = make_session(scratch, fs, "dispatch");
  const std::size_t base = session.base;

  seed(session, 3);

  const std::shared_ptr<const HistorySnapshot> snapshot = session.state.history().load();
  REQUIRE(snapshot != nullptr);
  REQUIRE(snapshot->names.size() == base + 3);
  CHECK(snapshot->names[base + 0] == "edit#0");
  CHECK(snapshot->names[base + 1] == "edit#1");
  CHECK(snapshot->names[base + 2] == "edit#2");
  CHECK(snapshot->cursor == base + 3);
  // The published shape agrees with the live journal it was built from.
  CHECK(snapshot->names.size() == session.state.document().journal().depth());
  CHECK(snapshot->cursor == session.state.document().journal().cursor());
}

TEST_CASE("history publish: undo and redo move the published cursor and leave the names") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  Session session = make_session(scratch, fs, "navverbs");
  const std::size_t base = session.base;

  seed(session, 3);

  REQUIRE(ace::commands::undo(session.state).moved);
  {
    // The applied/redoable split the panel dims on: the names are untouched, only the
    // cursor moved.
    const std::shared_ptr<const HistorySnapshot> snapshot = session.state.history().load();
    REQUIRE(snapshot->names.size() == base + 3);
    CHECK(snapshot->names[base + 2] == "edit#2");
    CHECK(snapshot->cursor == base + 2);
  }

  REQUIRE(ace::commands::redo(session.state).moved);
  {
    const std::shared_ptr<const HistorySnapshot> snapshot = session.state.history().load();
    CHECK(snapshot->names.size() == base + 3);
    CHECK(snapshot->names[base + 2] == "edit#2");
    CHECK(snapshot->cursor == base + 3);
  }
}

TEST_CASE("history publish: a commit after an undo republishes the truncated name list") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  Session session = make_session(scratch, fs, "truncate");
  const std::size_t base = session.base;

  seed(session, 3);
  REQUIRE(ace::commands::undo(session.state).moved);
  REQUIRE(ace::commands::undo(session.state).moved);
  REQUIRE(session.state.document().journal().cursor() == base + 1);

  // A fresh commit off a rewound cursor DROPS the redo tail before appending, so the
  // published list must SHRINK — the snapshot tracks trimming, not just growth.
  dispatch(session.state, named_edit(session, "edit#3", 0.25));

  const std::shared_ptr<const HistorySnapshot> snapshot = session.state.history().load();
  REQUIRE(snapshot->names.size() == base + 2);
  CHECK(snapshot->names[base + 0] == "edit#0");
  CHECK(snapshot->names.back() == "edit#3");
  CHECK(snapshot->cursor == base + 2);
}

TEST_CASE("history publish: a held snapshot is immutable across later commits") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  Session session = make_session(scratch, fs, "immutable");
  const std::size_t base = session.base;

  seed(session, 2);

  // Hold the pointer, exactly as a reader mid-frame would.
  const std::shared_ptr<const HistorySnapshot> held = session.state.history().load();
  REQUIRE(held != nullptr);
  const std::vector<std::string> names_before = held->names;
  const std::size_t cursor_before = held->cursor;

  seed(session, 3); // three further commits land under the holder

  // THIS is the assertion that retires the `entry_at` reallocation hazard: refresh
  // REPLACES the published pointer, it never mutates the pointee, so the value a reader
  // is holding cannot move under it no matter how many commits land.
  CHECK(held->names == names_before);
  CHECK(held->cursor == cursor_before);
  CHECK(held->names.size() == base + 2);
  // The publisher has genuinely let go of this generation — the holder is now its SOLE
  // owner and the value is still perfectly readable, which is only true because the
  // snapshot is a self-contained value and not a view into writer-owned memory.
  CHECK(held.use_count() == 1);
  CHECK(session.state.history().load() != held);
  CHECK(session.state.history().load()->names.size() == base + 5);
}

TEST_CASE("history publish: refresh is stamp-guarded and republishes nothing on an unchanged "
          "journal") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  Session session = make_session(scratch, fs, "stamp");

  seed(session, 2);
  const std::shared_ptr<const HistorySnapshot> before = session.state.history().load();

  // Two back-to-back refreshes on an unmoved journal: pointer identity is preserved,
  // which is what makes the deliberate double refresh (verb AND writer-turn hook) free.
  publish_history(session.state);
  CHECK(session.state.history().load() == before);
  publish_history(session.state);
  CHECK(session.state.history().load() == before);

  // A command with no `apply` journals nothing, so dispatch's own refresh is a
  // stamp-hit too.
  const auto outcome = dispatch(session.state, Command{"inert", {}});
  CHECK(outcome.journal_entries_added == 0);
  CHECK(session.state.history().load() == before);
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
  CHECK(state.history().load()->cursor == 1);
  CHECK(state.history().load()->names.size() == depth); // names survive navigation

  const auto forward = navigate_to(state, depth);
  CHECK(forward.steps == depth - 1);
  CHECK(forward.cursor == depth);
  CHECK_FALSE(forward.can_redo);
  CHECK(state.history().load()->cursor == depth);
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
  CHECK(state.history().load()->cursor == 0);

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

TEST_CASE("history publish: a bare scene transaction refreshes through the writer-turn hook") {
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

  // The deterministic inline host (no worker threads; no entries added — apply_edit takes
  // the document lease and runs the closure regardless).
  ace::render::CanvasHost host(arbc::WorkerPoolConfig{}, std::chrono::hours(1));
  host.set_post_edit_hook([&state] { publish_history(state); });

  const std::size_t names_before = state.history().load()->names.size();

  // A BARE scene transaction inside a raw apply_edit closure — the camera-inspector
  // shape (src/app/camera_inspector.cpp). It never touches a `commands` verb, so
  // verb-only refresh would leave the panel stale; only the writer-turn hook covers it.
  host.apply_edit([&] {
    ace::scene::set_camera_resolution(state.document(), registry, camera,
                                      ace::scene::Resolution{32, 24});
  });

  const std::shared_ptr<const HistorySnapshot> after = state.history().load();
  REQUIRE(after->names.size() > names_before);
  CHECK(after->names.back() == "set_camera_resolution");
  CHECK(after->cursor == after->names.size());
}

TEST_CASE("history publish: a spawned reader walks published snapshots while the writer commits") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  Session session = make_session(scratch, fs, "concurrent");
  AppState& state = session.state;
  const std::size_t base = session.base;

  seed(session, 2);

  // The inverted control: the PRE-change panel could not be written this way at all —
  // a reader thread calling `entry_at` IS the race this leaf removes. Here the reader
  // touches nothing but the atomic pointer and the immutable value behind it, so the
  // whole loop is clean under TSan (this file runs on every lane, gcc-tsan included).
  std::atomic<bool> done{false};
  std::atomic<std::size_t> reads{0};
  std::atomic<bool> consistent{true};

  std::thread reader([&] {
    while (!done.load(std::memory_order_relaxed)) {
      const std::shared_ptr<const HistorySnapshot> snapshot = state.history().load();
      if (!snapshot) {
        consistent.store(false, std::memory_order_relaxed);
        continue;
      }
      // Every loaded snapshot is self-consistent: the cursor indexes into its OWN name
      // list, and every named entry is a fully built string. (Indices below `base` are
      // the fixture's anonymous setup entries — the Document's self-committing wrappers
      // journal no name.)
      if (snapshot->cursor > snapshot->names.size()) {
        consistent.store(false, std::memory_order_relaxed);
      }
      for (std::size_t i = 0; i < snapshot->names.size(); ++i) {
        if (i >= base && snapshot->names[i].empty()) {
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
  CHECK(state.history().load()->cursor == state.document().journal().cursor());
  CHECK(state.history().load()->names.size() == state.document().journal().depth());
}
