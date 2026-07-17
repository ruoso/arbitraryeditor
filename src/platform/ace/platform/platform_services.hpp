#pragma once

#include <ace/platform/clock.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/threads.hpp>

namespace ace::platform {

// The injectable seam (A3, D-platform_services-2): an aggregate of the three
// faculties, handed to consumers by reference and wired at app bootstrap (L4).
// No process-global singleton and no #ifdef switch — the WASM port supplies a
// different concrete subclass at this same seam, and tests supply a fake. This
// is exactly the swap point §7 keeps: the Emscripten preset reuses everything
// but platform/.
class PlatformServices {
public:
  virtual ~PlatformServices() = default;
  virtual FileSystem& filesystem() = 0;
  virtual Clock& clock() = 0;
  virtual Threads& threads() = 0;
};

// The native aggregate: std filesystem + std::thread + steady_clock. Constructed
// once at bootstrap and injected; owns its three concrete faculties.
class NativePlatformServices final : public PlatformServices {
public:
  FileSystem& filesystem() override { return filesystem_; }
  Clock& clock() override { return clock_; }
  Threads& threads() override { return threads_; }

private:
  NativeFileSystem filesystem_;
  NativeClock clock_;
  NativeThreads threads_;
};

} // namespace ace::platform
