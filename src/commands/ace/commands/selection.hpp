#pragma once

#include <arbc/base/ids.hpp>

#include <cstddef>
#include <vector>

namespace ace::commands {

// The project-level selection (docs/00-design.md D19/A5, refinement Decision
// D-app_state-3): the ONE selection the whole project shares — every canvas and
// panel reads it, no canvas owns one (a canvas is only a camera, A5). It is
// TRANSIENT app state, not scene data (D15): mutating it is never a libarbc
// transaction and never touches the document journal, so undoing an edit never
// undoes a click. A plain ordered set of `arbc::ObjectId` plus a primary/active
// member; headless and ImGui/GL/SDL-free (A8).
class Selection {
public:
  bool empty() const { return items_.empty(); }
  std::size_t size() const { return items_.size(); }

  // The selected object ids, in selection order (most-recently-added last).
  const std::vector<arbc::ObjectId>& items() const { return items_; }

  // The primary/active object — the invalid `ObjectId{}` when the selection is
  // empty (doc: id zero is never valid).
  arbc::ObjectId primary() const { return primary_; }

  bool contains(arbc::ObjectId id) const;

  // Replace the whole selection with `id` (primary := id).
  void select(arbc::ObjectId id);
  // Add `id` if not already present; primary := id either way.
  void add(arbc::ObjectId id);
  // Remove `id` if present, else add it; primary follows the change.
  void toggle(arbc::ObjectId id);
  // Empty the selection (primary := the invalid id).
  void clear();

private:
  std::vector<arbc::ObjectId> items_;
  arbc::ObjectId primary_{};
};

} // namespace ace::commands
