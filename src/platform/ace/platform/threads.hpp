#pragma once

#include <functional>
#include <memory>

namespace ace::platform {

// Handle to one spawned auxiliary thread. A bare join/detach primitive, NOT a
// pool (D-platform_services-3): it never wraps, replaces, or duplicates
// libarbc's WorkerPool / HousekeepingThread (A4), and touches no tile cache.
class JoinHandle {
public:
  virtual ~JoinHandle() = default;
  virtual void join() = 0;
  virtual void detach() = 0;
  virtual bool joinable() const = 0;
};

// Thread-spawn faculty: spawn(callable) -> join-handle, for editor-owned
// auxiliary threads only (e.g. the later async export-with-progress). This is
// the one place the WASM port maps Emscripten pthreads.
class Threads {
public:
  virtual ~Threads() = default;
  virtual std::unique_ptr<JoinHandle> spawn(std::function<void()> work) = 0;
};

// Native impl over std::thread.
class NativeThreads final : public Threads {
public:
  std::unique_ptr<JoinHandle> spawn(std::function<void()> work) override;
};

} // namespace ace::platform
