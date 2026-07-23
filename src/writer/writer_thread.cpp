#include <ace/writer/writer_thread.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace ace::writer {

namespace {
// The bounded re-poll interval used ONLY while the idle work reports itself armed
// (D-writer_thread-10). Disarmed, the writer waits indefinitely on the queue and this constant
// is never reached — which is what makes "zero timer wakeups for a document with no external
// references" literal rather than aspirational.
constexpr std::chrono::milliseconds k_armed_poll_interval{2};
} // namespace

struct WriterThread::Impl {
  // One queued unit of work. A SYNC submission borrows the caller's closure (no copy — the call
  // site keeps its by-reference captures) and carries the flag the submitter waits on; an ASYNC
  // submission owns its closure, because the submitter is already gone by the time it runs.
  struct Item {
    const std::function<void()>* borrowed = nullptr;
    std::function<void()> owned;
    bool* done = nullptr;
  };

  std::unique_ptr<platform::JoinHandle> handle;

  mutable std::mutex mu;
  std::condition_variable queue_cv; // writer waits here for work / stop / a re-arm
  std::condition_variable done_cv;  // sync submitters wait here for their own completion
  std::deque<Item> queue;
  bool stopping = false;
  bool started = false;
  std::thread::id writer_id{}; // the bound identity; default (== "no thread") in inline mode
  bool threaded = false;
  // Held by shared_ptr so the writer can COPY the handle under the lock and then call it with
  // the lock released, without a `std::function` copy per drain and without racing a concurrent
  // `set_idle_work` that replaces it mid-poll.
  std::shared_ptr<const std::function<bool()>> idle;
  // Bumped by `set_idle_work` so an indefinitely-waiting writer re-evaluates rather than
  // sleeping through a freshly-armed poll (the wait predicate is otherwise queue-only).
  std::uint64_t wake_epoch = 0;
  std::uint64_t executed = 0;
  std::uint64_t idle_polls = 0;

  void run();
};

void WriterThread::Impl::run() {
  {
    std::lock_guard<std::mutex> lock(mu);
    writer_id = std::this_thread::get_id();
    started = true;
  }
  queue_cv.notify_all(); // the constructor's startup handshake

  for (;;) {
    Item item;
    {
      std::unique_lock<std::mutex> lock(mu);
      while (queue.empty()) {
        // `stop()` DRAINS: we only leave once the queue is actually empty, so a queued save or
        // checkpoint is never discarded at exit (D-writer_thread-6).
        if (stopping) {
          return;
        }
        // The queue has drained — this is the moment the writer does its OWN work
        // (D-writer_thread-10). Copy the handle under the lock, poll with the lock RELEASED (the
        // poll touches the document, not this queue), then re-check.
        const std::shared_ptr<const std::function<bool()>> poll = idle;
        bool armed = false;
        if (poll && *poll) {
          lock.unlock();
          armed = (*poll)();
          lock.lock();
          ++idle_polls;
          if (!queue.empty()) {
            break;
          }
          if (stopping) {
            return; // `stop()` never leaves a poll armed
          }
        }
        const std::uint64_t seen = wake_epoch;
        const auto ready = [&] { return !queue.empty() || stopping || wake_epoch != seen; };
        if (armed) {
          // A fetch is still outstanding: wait BOUNDEDLY and poll again.
          queue_cv.wait_for(lock, k_armed_poll_interval, ready);
        } else {
          // Nothing outstanding: sleep until something is actually submitted. No timer.
          queue_cv.wait(lock, ready);
        }
      }
      item = std::move(queue.front());
      queue.pop_front();
    }

    // Off-lock: the closure is the writer's whole point, and it must be able to call back into
    // `submit`/`submit_sync` (D-writer_thread-4) without deadlocking on our own mutex.
    if (item.borrowed != nullptr) {
      (*item.borrowed)();
    } else if (item.owned) {
      item.owned();
    }

    {
      std::lock_guard<std::mutex> lock(mu);
      ++executed;
      if (item.done != nullptr) {
        *item.done = true;
      }
    }
    if (item.done != nullptr) {
      done_cv.notify_all();
    }
  }
}

WriterThread::WriterThread() : impl_(std::make_unique<Impl>()) {}

WriterThread::WriterThread(platform::Threads& threads) : impl_(std::make_unique<Impl>()) {
  impl_->threaded = true;
  impl_->handle = threads.spawn([impl = impl_.get()] { impl->run(); });
  // Startup handshake: block until the thread has published its id, so `on_writer_thread()` and
  // the re-entrancy check in `submit_sync` are correct from the first submission onward.
  std::unique_lock<std::mutex> lock(impl_->mu);
  impl_->queue_cv.wait(lock, [this] { return impl_->started; });
}

WriterThread::~WriterThread() { stop(); }

bool WriterThread::submit(std::function<void()> work) {
  {
    std::unique_lock<std::mutex> lock(impl_->mu);
    if (impl_->stopping) {
      return false;
    }
    if (!impl_->threaded) {
      // Inline degenerate (D-writer_thread-5): the caller IS the writer.
      lock.unlock();
      if (work) {
        work();
      }
      std::lock_guard<std::mutex> counted(impl_->mu);
      ++impl_->executed;
      return true;
    }
    impl_->queue.push_back(Impl::Item{nullptr, std::move(work), nullptr});
  }
  impl_->queue_cv.notify_one();
  return true;
}

bool WriterThread::submit_sync(const std::function<void()>& work) {
  {
    std::unique_lock<std::mutex> lock(impl_->mu);
    if (impl_->stopping) {
      return false;
    }
    // Inline degenerate, OR re-entrant from inside a closure already on the writer: run here.
    // Enqueueing behind ourselves would wait for a queue only we can drain (D-writer_thread-4/5).
    if (!impl_->threaded || std::this_thread::get_id() == impl_->writer_id) {
      lock.unlock();
      if (work) {
        work();
      }
      std::lock_guard<std::mutex> counted(impl_->mu);
      ++impl_->executed;
      return true;
    }
    bool done = false;
    impl_->queue.push_back(Impl::Item{&work, {}, &done});
    impl_->queue_cv.notify_one();
    // A `stop()` that lands after this push still DRAINS us, so there is no lost wakeup here:
    // the item runs and sets `done` either way.
    impl_->done_cv.wait(lock, [&done] { return done; });
  }
  return true;
}

bool WriterThread::on_writer_thread() const noexcept {
  std::lock_guard<std::mutex> lock(impl_->mu);
  return !impl_->threaded || std::this_thread::get_id() == impl_->writer_id;
}

void WriterThread::set_idle_work(std::function<bool()> work) {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->idle = work ? std::make_shared<const std::function<bool()>>(std::move(work)) : nullptr;
    ++impl_->wake_epoch;
  }
  impl_->queue_cv.notify_all();
}

void WriterThread::stop() {
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->stopping = true;
  }
  impl_->queue_cv.notify_all();
  if (impl_->handle && impl_->handle->joinable()) {
    impl_->handle->join();
  }
  impl_->handle.reset();
}

bool WriterThread::stopped() const noexcept {
  std::lock_guard<std::mutex> lock(impl_->mu);
  return impl_->stopping;
}

std::uint64_t WriterThread::executed() const noexcept {
  std::lock_guard<std::mutex> lock(impl_->mu);
  return impl_->executed;
}

std::uint64_t WriterThread::idle_polls() const noexcept {
  std::lock_guard<std::mutex> lock(impl_->mu);
  return impl_->idle_polls;
}

} // namespace ace::writer
