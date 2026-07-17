#pragma once

#include <optional>
#include <system_error>
#include <utility>

namespace ace::platform {

// A typed outcome for fallible platform operations (D-platform_services-6): a
// value on success, else a std::error_code the L1 consumers branch on as
// ordinary control flow. Chosen over throwing so the failure path is
// deterministically assertable in headless tests. (std::expected is C++23; this
// is the C++20 stand-in for the same shape.)
template <typename T> class Result {
public:
  Result(T value) : value_(std::move(value)) {}
  Result(std::error_code error) : error_(error) {}

  bool has_value() const { return value_.has_value(); }
  explicit operator bool() const { return has_value(); }

  const T& value() const { return *value_; }
  T& value() { return *value_; }
  const T& operator*() const { return *value_; }
  T& operator*() { return *value_; }

  std::error_code error() const { return error_; }

private:
  std::optional<T> value_;
  std::error_code error_;
};

} // namespace ace::platform
