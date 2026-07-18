#include <ace/commands/selection.hpp>

#include <algorithm>

namespace ace::commands {

bool Selection::contains(arbc::ObjectId id) const {
  return std::find(items_.begin(), items_.end(), id) != items_.end();
}

void Selection::select(arbc::ObjectId id) {
  items_.clear();
  items_.push_back(id);
  primary_ = id;
}

void Selection::add(arbc::ObjectId id) {
  if (!contains(id)) {
    items_.push_back(id);
  }
  primary_ = id;
}

void Selection::toggle(arbc::ObjectId id) {
  const auto it = std::find(items_.begin(), items_.end(), id);
  if (it != items_.end()) {
    items_.erase(it);
    primary_ = items_.empty() ? arbc::ObjectId{} : items_.back();
  } else {
    items_.push_back(id);
    primary_ = id;
  }
}

void Selection::clear() {
  items_.clear();
  primary_ = arbc::ObjectId{};
}

} // namespace ace::commands
