// editor.canvas.arbc_v030 — the libarbc pin guard, headless + GL-free
// (docs/01-architecture.md §9). Two jobs, one file:
//
//  1. PIN EFFECTIVENESS (D-arbc_v030-2). `ARBC_GIT_TAG` is a `CACHE` variable set WITHOUT
//     `FORCE` (CMakeLists.txt:25) and `scripts/gate` reconfigures in place without wiping
//     `build/`, so a developer with an existing build tree can bump the tag, see a green
//     gate, and still be linked against v0.2.0 — silently. There is no `arbc_version`
//     symbol to assert at runtime, and a subproject's `<NAME>_VERSION` is not visible in
//     the parent scope after `FetchContent_MakeAvailable`, so a CMake `VERSION_LESS` guard
//     would never fire. The guard is therefore the COMPILER: this file NAMES the four
//     v0.3.0-only `Document` members (`on_writer_thread`, `external_loads_ready`,
//     `set_external_load_settler`, `external_loads_auto_settled`), so a stale tree fails to
//     build rather than passing (Constraint 2 / Constraint 7).
//
//  2. THE SEMANTIC PIN `editor.canvas.writer_thread` INHERITS (D-arbc_v030-4/-5). Writer
//     identity is UNBOUND until the first transaction and `Model::on_writer_thread()`
//     answers `true` while unbound, so "the UI thread transacts before the render thread
//     ever steps" is load-bearing, not incidental; the v0.3.0 auto-settle path is armed but
//     unreachable in this editor; and the journal's enable state is now an any-thread read
//     (arbc#15), which is what lets `src/dock/dock.cpp:337,343` keep calling `can_undo()` /
//     `can_redo()` per frame once the UI thread stops being the writer.
//
// This leaf consumes NONE of the new surface in `src/` (D-arbc_v030-1) — these cases are
// the only place it is named. The spawned-thread cases run in the ASan and TSan lanes.

#include <ace/platform/threads.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <functional>
#include <memory>

namespace {

// Run `work` on a freshly spawned thread and join it. The join is the happens-before edge
// that makes the captured answer safe to read back (and keeps the TSan lane quiet); a
// FOREIGN thread is the only observation that distinguishes "unbound" from "bound to me",
// because on the calling thread `on_writer_thread()` is true in both cases (D-arbc_v030-5).
void on_a_foreign_thread(const std::function<void()>& work) {
  ace::platform::NativeThreads threads;
  std::unique_ptr<ace::platform::JoinHandle> handle = threads.spawn(work);
  handle->join();
}

bool foreign_on_writer_thread(const arbc::Document& doc) {
  bool answer = false;
  on_a_foreign_thread([&] { answer = doc.on_writer_thread(); });
  return answer;
}

// The four any-thread journal words arbc#15 published as atomics, sampled as one tuple so
// a main-thread read and a foreign-thread read can be compared wholesale.
struct JournalReads {
  bool can_undo{false};
  bool can_redo{false};
  std::size_t depth{0};
  std::size_t cursor{0};

  bool operator==(const JournalReads&) const = default;
};

JournalReads sample(const arbc::Journal& journal) {
  return JournalReads{journal.can_undo(), journal.can_redo(), journal.depth(), journal.cursor()};
}

JournalReads foreign_sample(const arbc::Journal& journal) {
  JournalReads reads;
  on_a_foreign_thread([&] { reads = sample(journal); });
  return reads;
}

// One placed solid over a root composition — the cheapest document that journals several
// entries. Returns the layer, so a caller can keep editing it.
arbc::ObjectId seed_placed_solid(arbc::Document& doc, arbc::ObjectId composition) {
  const arbc::ObjectId content =
      doc.add_content(std::make_shared<arbc::SolidContent>(arbc::Rgba{0.2F, 0.6F, 0.9F, 1.0F}));
  const arbc::ObjectId layer = doc.add_layer(content, arbc::Affine::identity());
  doc.attach_layer(composition, layer);
  return layer;
}

} // namespace

TEST_CASE("arbc pin: the v0.3.0 writer-identity surface exists") {
  arbc::Document doc;

  // Every name below is absent at v0.2.0. A tree still resolving the old tag never reaches
  // the assertions — it fails at the compiler, which is the entire point of this case.
  CHECK(doc.on_writer_thread());                 // unbound: the caller would BECOME the writer
  CHECK(doc.external_loads_ready() == 0);        // nothing fetched, so nothing queued
  CHECK(doc.external_loads_auto_settled() == 0); // and nothing auto-installed
  doc.set_external_load_settler({});             // clearing an uninstalled slot is a no-op
  CHECK(doc.external_loads_auto_settled() == 0);
}

TEST_CASE("arbc pin: writer identity is unbound until the first transaction, then binds to "
          "that thread") {
  arbc::Document doc;

  // Unbound. `Model::on_writer_thread()` answers true for EVERY thread here, because any of
  // them would become the writer by transacting — the surprising arm, and the one that makes
  // a render thread's first `step()` on a never-transacted document able to steal the
  // identity (host_viewport.cpp:164 asks exactly this predicate).
  CHECK(doc.on_writer_thread());
  CHECK(foreign_on_writer_thread(doc));

  // One main-thread edit. `Document::add_composition` routes through `Document::begin()` ->
  // `Model::transact()` -> `Model::Transaction`'s ctor, which is one of the only two sites
  // that bind the identity (the other is `Model::navigate()`).
  const arbc::ObjectId composition = doc.add_composition(64.0, 64.0);
  REQUIRE(composition.valid());

  // Bound — and now the predicate discriminates. This is the invariant
  // `editor.canvas.writer_thread`'s tripwire design rests on: true inside every posted
  // closure, false on the UI and render threads.
  CHECK(doc.on_writer_thread());
  CHECK_FALSE(foreign_on_writer_thread(doc));

  // Binding is once-and-for-all: further main-thread edits do not rebind, and no amount of
  // foreign observation does either.
  seed_placed_solid(doc, composition);
  CHECK(doc.on_writer_thread());
  CHECK_FALSE(foreign_on_writer_thread(doc));
}

TEST_CASE("arbc pin: a document with no external references never arms the auto-settle path") {
  arbc::Document doc;
  const arbc::ObjectId composition = doc.add_composition(64.0, 64.0);
  REQUIRE(composition.valid());

  // Stand in for what `HostViewport`'s ctor installs at v0.3.0 (host_viewport.cpp:115): a
  // writer-thread settler the document runs at the top of every `begin()`.
  std::size_t settler_calls = 0;
  doc.set_external_load_settler([&settler_calls] {
    ++settler_calls;
    return std::size_t{0};
  });

  // N edits, every one of them through `Document::begin()` — i.e. every one of them past
  // the settler's early-out (document.cpp:174).
  const arbc::ObjectId layer = seed_placed_solid(doc, composition);
  for (int i = 0; i < 4; ++i) {
    doc.set_layer_transform(layer, arbc::Affine::translation(static_cast<double>(i), 0.0));
  }
  REQUIRE(doc.journal().depth() >= 4);

  // The third link of the chain is missing and cannot be supplied by any editor input
  // (D-arbc_v030-4): the editor's nested cells are in-document `ObjectId` children, never a
  // serialized `params.ref`, and its only `arbc::load_document` passes a
  // `FilesystemAssetSource`, which resolves inline. So `external_loads_ready()` stays 0, the
  // early-out always fires, and the bump is behaviourally inert in `src/`.
  CHECK(doc.external_loads_ready() == 0);
  CHECK(doc.pending_external_loads() == 0);
  CHECK(settler_calls == 0);
  CHECK(doc.external_loads_auto_settled() == 0);

  // The day the editor grows a `params.ref` author path or a deferring `AssetSource`, this
  // case fails and points at D-arbc_v030-4 — including the now-corrected `KindBridge`
  // confinement comment in src/render/canvas_renderer.cpp.

  doc.set_external_load_settler({}); // release the install, as ~HostViewport does
}

TEST_CASE("arbc pin: journal enable state is readable from a non-writer thread") {
  arbc::Document doc;
  const arbc::ObjectId composition = doc.add_composition(64.0, 64.0);
  REQUIRE(composition.valid());
  seed_placed_solid(doc, composition); // the main thread is now the bound writer
  REQUIRE_FALSE(foreign_on_writer_thread(doc));

  arbc::Journal& journal = doc.journal();
  const JournalReads after_edits = sample(journal);
  REQUIRE(after_edits.depth > 0);
  CHECK(after_edits.cursor == after_edits.depth);
  CHECK(after_edits.can_undo);
  CHECK_FALSE(after_edits.can_redo);

  // The arbc#15 property: all four words are relaxed atomics the writer publishes, so a
  // thread that is NOT the writer reads them lock-free and sees the same values. This is
  // what keeps src/dock/dock.cpp:337,343's per-frame `can_undo()`/`can_redo()` legal once
  // `editor.canvas.writer_thread` moves the writer off the UI thread.
  CHECK(foreign_sample(journal) == after_edits);

  // Navigate on the writer thread (`undo()` is still WRITER-THREAD-ONLY at v0.3.0) and
  // re-observe: the foreign reads TRACK the history rather than reporting a constant — both
  // enable bits flip and the cursor moves, with the depth held.
  REQUIRE(journal.undo());
  const JournalReads after_undo = sample(journal);
  CHECK(after_undo.depth == after_edits.depth);
  CHECK(after_undo.cursor == after_edits.cursor - 1);
  CHECK(after_undo.can_redo);
  CHECK(foreign_sample(journal) == after_undo);

  REQUIRE(journal.redo());
  CHECK(foreign_sample(journal) == after_edits);
}
