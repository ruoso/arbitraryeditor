// editor.canvas.writer_thread — the L1 closure executor, headless and libarbc-free
// (docs/01-architecture.md §9). `writer::WriterThread` holds no `Document` and names no library
// type (D-writer_thread-1), so every acceptance criterion below is expressible with nothing but
// `std::thread::id`, a vector and two counters — which is the whole reason the component exists
// at that level rather than on `CanvasHost`.
//
// What is pinned here: one FIFO across both entries (D-writer_thread-2), the sync entry's
// two-way happens-before edge, one stable writer identity distinct from every submitter (D-1),
// re-entrancy (D-4), drain-on-stop and refusal-after-stop (D-6), the inline degenerate mode
// (D-5), and the idle-work arm/disarm contract (D-10).
#include <ace/platform/threads.hpp>
#include <ace/writer/writer_thread.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using ace::writer::WriterThread;

namespace {

// Deadline-based pump — holds under a sanitizer build's slowdown (no fixed iteration count).
template <class Ready> bool pump_until(Ready ready) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (std::chrono::steady_clock::now() < deadline) {
    if (ready()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return ready();
}

} // namespace

TEST_CASE("writer_thread: mixed submit/submit_sync execute in submission order (one FIFO)") {
  ace::platform::NativeThreads threads;
  WriterThread writer(threads);

  std::vector<int> order;
  // Only the writer thread touches `order` — every closure runs on it, and the final read
  // happens-after the last submit_sync's return edge.
  for (int i = 0; i < 8; ++i) {
    writer.submit([&order, i] { order.push_back(i); });
  }
  // A sync verb queued BEHIND an async burst runs after all of it: the cross-entry total order
  // is the reason both entries share one queue (D-writer_thread-2 / Constraint 5).
  writer.submit_sync([&order] { order.push_back(100); });

  REQUIRE(order.size() == 9);
  for (int i = 0; i < 8; ++i) {
    CHECK(order[static_cast<std::size_t>(i)] == i);
  }
  CHECK(order[8] == 100);
  CHECK(writer.executed() == 9);
  writer.stop();
}

TEST_CASE("writer_thread: submit_sync returns only after its closure ran, and publishes its "
          "writes to the caller") {
  ace::platform::NativeThreads threads;
  WriterThread writer(threads);

  // A plain (non-atomic) local, written on the writer thread and read here on return: the sync
  // entry's happens-before edge in the closure->caller direction is what makes that legal, and
  // what makes the shipped result-carrying call sites (`run_edit([&]{ moved = ... })`) sound.
  int result = 0;
  bool ran = false;
  CHECK(writer.submit_sync([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(5)); // make "returned early" visible
    result = 42;
    ran = true;
  }));
  CHECK(ran);
  CHECK(result == 42);
  writer.stop();
}

TEST_CASE("writer_thread: every closure runs on ONE thread, and it is not any submitting thread") {
  ace::platform::NativeThreads threads;
  WriterThread writer(threads);

  std::mutex mu;
  std::vector<std::thread::id> observed;
  std::vector<std::thread::id> submitters;

  const auto submit_from_here = [&] {
    {
      std::lock_guard<std::mutex> lock(mu);
      submitters.push_back(std::this_thread::get_id());
    }
    // The tripwire is an assertion, not a comment: the identity is queryable from any thread.
    CHECK_FALSE(writer.on_writer_thread());
    writer.submit_sync([&] {
      std::lock_guard<std::mutex> lock(mu);
      observed.push_back(std::this_thread::get_id());
      CHECK(writer.on_writer_thread());
    });
  };

  submit_from_here(); // the main thread
  std::thread other(submit_from_here);
  other.join();
  std::thread another(submit_from_here);
  another.join();

  REQUIRE(observed.size() == 3);
  CHECK(observed[0] == observed[1]);
  CHECK(observed[1] == observed[2]);
  REQUIRE(submitters.size() == 3);
  for (const std::thread::id submitter : submitters) {
    CHECK(observed[0] != submitter);
  }
  writer.stop();
}

TEST_CASE("writer_thread: re-entrant submit_sync runs inline; re-entrant submit enqueues in "
          "order") {
  ace::platform::NativeThreads threads;
  WriterThread writer(threads);

  std::vector<std::string> order;
  writer.submit_sync([&] {
    order.emplace_back("outer-begin");
    // Enqueueing behind ourselves would wait on a queue only we can drain: run inline (D-4).
    CHECK(writer.submit_sync([&] { order.emplace_back("inner-sync"); }));
    // ...whereas a re-entrant ASYNC submit cannot deadlock, so it keeps its ordering semantics
    // and lands after the outer closure finishes.
    CHECK(writer.submit([&] { order.emplace_back("inner-async"); }));
    order.emplace_back("outer-end");
  });
  // Flush the re-entrant async behind a barrier (FIFO, so this returns after it ran).
  writer.submit_sync([] {});

  REQUIRE(order.size() == 4);
  CHECK(order[0] == "outer-begin");
  CHECK(order[1] == "inner-sync");
  CHECK(order[2] == "outer-end");
  CHECK(order[3] == "inner-async");
  writer.stop();
}

TEST_CASE("writer_thread: stop() DRAINS queued work, then refuses every later submission") {
  ace::platform::NativeThreads threads;
  WriterThread writer(threads);

  std::atomic<int> ran{0};
  // Block the writer inside the first closure so the rest genuinely pile up behind stop().
  std::atomic<bool> release{false};
  writer.submit([&] {
    ++ran;
    while (!release.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  CHECK(pump_until([&] { return ran.load() == 1; }));
  for (int i = 0; i < 16; ++i) {
    CHECK(writer.submit([&] { ++ran; }));
  }
  release.store(true);

  // A queued save or checkpoint must not be dropped at exit (D-writer_thread-6).
  writer.stop();
  CHECK(ran.load() == 17);
  CHECK(writer.stopped());

  // After stop, submissions are REFUSED — not run inline, which would mint a second writer
  // identity at exactly the worst moment.
  bool late = false;
  CHECK_FALSE(writer.submit([&] { late = true; }));
  CHECK_FALSE(writer.submit_sync([&] { late = true; }));
  CHECK_FALSE(late);
  CHECK(writer.executed() == 17);
}

TEST_CASE("writer_thread: the inline degenerate mode spawns nothing and runs on the caller") {
  // No platform::Threads => no thread (D-writer_thread-5). This is what keeps the headless
  // Catch2/golden fixtures deterministic and thread-free, and what a pthread-less WASM build
  // runs: still exactly ONE writer identity, and no blocking wait on the browser main thread.
  WriterThread writer;

  const std::thread::id caller = std::this_thread::get_id();
  std::vector<std::thread::id> observed;
  CHECK(writer.on_writer_thread()); // in degenerate mode the caller IS the identity

  writer.submit([&] { observed.push_back(std::this_thread::get_id()); });
  writer.submit_sync([&] { observed.push_back(std::this_thread::get_id()); });

  REQUIRE(observed.size() == 2);
  CHECK(observed[0] == caller);
  CHECK(observed[1] == caller);
  CHECK(writer.executed() == 2);

  writer.stop();
  bool late = false;
  CHECK_FALSE(writer.submit_sync([&] { late = true; }));
  CHECK_FALSE(late);
}

TEST_CASE("writer_thread: idle work runs when the queue drains and never interleaves with a "
          "closure") {
  ace::platform::NativeThreads threads;
  WriterThread writer(threads);

  std::atomic<int> inside_closure{0};
  std::atomic<int> overlaps{0};
  std::atomic<int> polls{0};
  writer.set_idle_work([&] {
    if (inside_closure.load() != 0) {
      ++overlaps; // structurally impossible: same thread, run BETWEEN queue items
    }
    ++polls;
    return false; // disarmed
  });

  for (int i = 0; i < 8; ++i) {
    writer.submit([&] {
      ++inside_closure;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      --inside_closure;
    });
  }
  writer.submit_sync([] {});
  CHECK(pump_until([&] { return polls.load() > 0; }));
  CHECK(overlaps.load() == 0);
  writer.stop();
}

TEST_CASE("writer_thread: an ARMED idle poll re-polls on a bounded wait; a DISARMED one costs "
          "zero wakeups") {
  ace::platform::NativeThreads threads;
  WriterThread writer(threads);

  std::atomic<bool> armed{true};
  writer.set_idle_work([&] { return armed.load(); });

  // Armed with NOTHING submitted: the writer must keep polling on its own (this is the case a
  // deferred external arrival on a completely idle app depends on — nobody edits, nothing
  // renders, and the arrival must still be consumed).
  const std::uint64_t start = writer.idle_polls();
  CHECK(pump_until([&] { return writer.idle_polls() > start + 2; }));

  // Disarm: the writer falls back to an indefinite wait on the queue. No timer, no wakeups.
  armed.store(false);
  writer.submit_sync([] {}); // force one more drain so the disarmed value is observed
  CHECK(pump_until([&] { return writer.idle_polls() > 0; }));
  const std::uint64_t quiescent = writer.idle_polls();
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  CHECK(writer.idle_polls() == quiescent);

  // ...and a fresh submission drains again, so the poll is re-evaluated rather than lost.
  writer.submit_sync([] {});
  CHECK(pump_until([&] { return writer.idle_polls() > quiescent; }));

  // stop() must not leave a poll armed (D-writer_thread-10): re-arm, stop, and check it stalls.
  armed.store(true);
  writer.stop();
  const std::uint64_t final_polls = writer.idle_polls();
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  CHECK(writer.idle_polls() == final_polls);
}

TEST_CASE("writer_thread: arming an idle poll wakes an already-sleeping writer") {
  // A writer parked on an indefinite queue wait must still notice a poll installed afterwards —
  // otherwise the first arrival on an idle document would sit until something unrelated was
  // submitted.
  ace::platform::NativeThreads threads;
  WriterThread writer(threads);
  writer.submit_sync([] {}); // drain once so the writer is definitely parked

  std::atomic<int> polls{0};
  writer.set_idle_work([&] {
    ++polls;
    return false;
  });
  CHECK(pump_until([&] { return polls.load() > 0; }));
  writer.stop();
}
