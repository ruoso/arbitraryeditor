#pragma once

#include <chrono>

namespace ace::platform {

// Monotonic (steady) clock faculty (D-platform_services-5): now() never runs
// backwards, for frame pacing, undo gesture-coalescing windows, and timeouts.
// Wall-clock time (a file's mtime) is filesystem metadata, deliberately NOT
// exposed here — a non-monotonic system_clock would invite time-goes-backwards
// bugs across NTP steps / DST.
class Clock {
public:
  using Duration = std::chrono::steady_clock::duration;

  virtual ~Clock() = default;
  virtual Duration now() const = 0;
};

// Native impl over std::chrono::steady_clock.
class NativeClock final : public Clock {
public:
  Duration now() const override;
};

} // namespace ace::platform
