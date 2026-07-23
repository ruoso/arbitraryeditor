#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace arbc {
class Journal;
} // namespace arbc

namespace ace::commands {

// The journal's user-visible shape, published as ONE immutable value (arch A18).
//
// `names` is the chronological entry-name list — index i is journal entry i — and
// `cursor` is the applied-entry count in `[0, names.size()]`, the split between the
// applied prefix `[0, cursor)` and the redoable tail `[cursor, names.size())`. The
// depth is simply `names.size()`; there is no third field.
//
// The cursor rides IN the snapshot rather than being re-read live from
// `Journal::cursor()` deliberately (D-history_published_reads-2): a reader indexes
// `names` AT `cursor` (the "Redo <name>" affordance), so a cursor from a later
// generation paired with an earlier generation's names is an out-of-bounds read.
// One loaded pointer = one self-consistent model for the whole frame.
struct HistorySnapshot {
  std::vector<std::string> names; // chronological entry names, [0, depth)
  std::size_t cursor = 0;         // applied-entry count, in [0, names.size()]
};

// The publish/load seam that keeps the UI thread off writer-owned document structure
// (arch A18). libarbc v0.3.0 publishes `can_undo()/can_redo()/depth()/cursor()` as
// relaxed atomics (arbc#15) but deliberately NOT the entry vector: `Journal::entry_at`
// hands out a reference into the writer-owned `d_entries`, which a concurrent commit
// may reallocate (`arbc/model/journal.hpp:120-125`). So the entry names are published
// host-side, in the only shape libarbc doc 15 permits for cross-thread structure — the
// writer builds a `const` value, the reader takes it by pointer.
//
//   - `refresh()` is WRITER-THREAD ONLY (it calls `entry_at`). It is called at every
//     writer-turn boundary: the `commands` verbs (`dispatch`/`undo`/`redo`/
//     `navigate_to`) and `CanvasHost`'s post-edit hook, which also catches the bare
//     `scene::` transactions that never pass through a verb.
//   - `load()` is ANY THREAD, lock-free, and hands back a snapshot that is immutable
//     after publication: refresh REPLACES the pointer, never mutates the pointee, so a
//     reader may hold one across any number of later commits and still see a stable,
//     self-consistent value. That immutability — not a lock — is what retires the
//     use-after-realloc hazard.
//
// `refresh` is stamp-guarded on `(depth(), cursor())`, both v0.3.0 any-thread atomics,
// so an unchanged journal republishes nothing and the belt-and-braces double refresh
// (verb AND hook) costs two atomic loads (D-history_published_reads-7). Never null: a
// default-constructed publisher already holds an empty snapshot, so a panel drawn on
// frame 0 before any edit still gets a valid pointer.
//
// Neither copyable nor movable (the atomic is not), which is why `AppState` holds it
// through a `unique_ptr` — see D-history_published_reads-6.
class HistoryPublisher {
public:
  HistoryPublisher() : published_(std::make_shared<const HistorySnapshot>()) {}

  HistoryPublisher(const HistoryPublisher&) = delete;
  HistoryPublisher& operator=(const HistoryPublisher&) = delete;

  // ANY THREAD: the currently published snapshot. Never null.
  std::shared_ptr<const HistorySnapshot> load() const {
    return published_.load(std::memory_order_acquire);
  }

  // WRITER THREAD ONLY: rebuild and publish from `journal` when its `(depth, cursor)`
  // stamp has moved since the last publication; otherwise leave the published pointer
  // untouched. Idempotent, so it is safe to call at every writer-turn exit.
  void refresh(const arbc::Journal& journal);

private:
  std::atomic<std::shared_ptr<const HistorySnapshot>> published_;
};

} // namespace ace::commands
