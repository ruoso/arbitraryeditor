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

void Selection::replace_all(std::span<const arbc::ObjectId> ids) {
  items_.clear();
  primary_ = arbc::ObjectId{};
  for (const arbc::ObjectId id : ids) {
    if (!contains(id)) {
      items_.push_back(id);
    }
    primary_ = id;
  }
}

void Selection::add_all(std::span<const arbc::ObjectId> ids) {
  for (const arbc::ObjectId id : ids) {
    if (!contains(id)) {
      items_.push_back(id);
    }
    primary_ = id;
  }
}

void Selection::prune(std::span<const arbc::ObjectId> live) {
  const bool primary_survives =
      std::find(live.begin(), live.end(), primary_) != live.end() && primary_.valid();
  items_.erase(std::remove_if(items_.begin(), items_.end(),
                              [&live](arbc::ObjectId id) {
                                return std::find(live.begin(), live.end(), id) == live.end();
                              }),
               items_.end());
  // The primary re-points only when it did NOT survive: a stale primary over a still-live
  // member would be exactly the dangling handle this verb exists to prevent.
  if (!primary_survives) {
    primary_ = items_.empty() ? arbc::ObjectId{} : items_.back();
  }
}

} // namespace ace::commands
