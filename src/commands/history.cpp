#include <ace/commands/history.hpp>

#include <arbc/model/journal.hpp>
#include <arbc/model/journal_entry.hpp>

#include <cstddef>
#include <memory>
#include <utility>

namespace ace::commands {

void HistoryPublisher::refresh(const arbc::Journal& journal) {
  // The stamp (D-history_published_reads-7): both accessors are v0.3.0 relaxed
  // atomics, so this costs two loads on the no-change path — which is what makes the
  // deliberate double refresh (the `commands` verb AND `CanvasHost`'s post-edit hook)
  // free, and what keeps a jump across a long journal from rebuilding once per step.
  // Correctness never rests on it: the stamp only decides whether to SKIP work. A
  // coalesced commit — the one shape that can move the journal without moving the
  // stamp — leaves depth, cursor and the folded entry's `name` all unchanged, so
  // there is nothing to republish.
  const std::size_t depth = journal.depth();
  const std::size_t cursor = journal.cursor();
  // `load()` is never null — the constructor publishes an empty snapshot — so the stamp
  // comparison needs no null guard.
  const std::shared_ptr<const HistorySnapshot> current = load();
  if (current->names.size() == depth && current->cursor == cursor) {
    return;
  }

  // Rebuild from scratch rather than patching the previous snapshot: a commit after an
  // undo TRIMS the redo tail before appending, so growth is not the only delta, and the
  // published value must never be mutated in place (Constraint 3 — a reader may be
  // holding it). `entry_at` is the writer-thread-only read this whole class exists to
  // confine to one place.
  auto next = std::make_shared<HistorySnapshot>();
  next->names.reserve(depth);
  for (std::size_t i = 0; i < depth; ++i) {
    next->names.push_back(journal.entry_at(i).name);
  }
  next->cursor = cursor;

  // Publish by pointer swap. The release/acquire pair is what carries the freshly built
  // strings over to an any-thread `load()`.
  published_.store(std::shared_ptr<const HistorySnapshot>(std::move(next)),
                   std::memory_order_release);
}

} // namespace ace::commands
