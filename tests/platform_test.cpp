#include <ace/platform/platform_services.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Headless L0 unit tests for the PlatformServices seam (refinement Acceptance).
// Filesystem round-trip + error/edge branches, monotonic clock, thread-spawn
// over a shared atomic (the leaf's designated ASan/TSan target), and a
// fakeability check proving the seam is injectable.

using namespace ace::platform;

namespace {

// A throwaway directory under the OS temp dir for the filesystem cases. Fixed
// name (no RNG dependency) — wiped on entry and exit so reruns are clean.
struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_platform_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

} // namespace

TEST_CASE("NativeFileSystem: write/read round-trips a whole file") {
  ScratchDir scratch;
  NativeFileSystem fs;
  const auto file = scratch.root / "hello.txt";

  CHECK_FALSE(fs.exists(file));
  REQUIRE_FALSE(static_cast<bool>(fs.write_file(file, "hello world")));
  CHECK(fs.exists(file));

  const auto read = fs.read_file(file);
  REQUIRE(read.has_value());
  CHECK(read.value() == "hello world");
}

TEST_CASE("NativeFileSystem: make_directories is mkdir -p and idempotent") {
  ScratchDir scratch;
  NativeFileSystem fs;
  const auto nested = scratch.root / "a" / "b" / "c";

  REQUIRE_FALSE(static_cast<bool>(fs.make_directories(nested)));
  CHECK(fs.exists(nested));
  // Second call over an existing tree must still report success.
  CHECK_FALSE(static_cast<bool>(fs.make_directories(nested)));
}

TEST_CASE("NativeFileSystem: list_directory returns the expected entry set") {
  ScratchDir scratch;
  NativeFileSystem fs;
  REQUIRE_FALSE(static_cast<bool>(fs.write_file(scratch.root / "one", "1")));
  REQUIRE_FALSE(static_cast<bool>(fs.write_file(scratch.root / "two", "2")));
  REQUIRE_FALSE(static_cast<bool>(fs.make_directories(scratch.root / "sub")));

  const auto listing = fs.list_directory(scratch.root);
  REQUIRE(listing.has_value());
  std::vector<std::string> names;
  for (const auto& p : listing.value()) {
    names.push_back(p.filename().string());
  }
  std::sort(names.begin(), names.end());
  CHECK(names == std::vector<std::string>{"one", "sub", "two"});
}

TEST_CASE("NativeFileSystem: list_directory on a missing dir yields a typed error") {
  ScratchDir scratch;
  NativeFileSystem fs;
  const auto listing = fs.list_directory(scratch.root / "nope");
  CHECK_FALSE(listing.has_value());
  CHECK(static_cast<bool>(listing.error()));
}

TEST_CASE("NativeFileSystem: reading a missing file yields a typed error, not a crash") {
  ScratchDir scratch;
  NativeFileSystem fs;
  const auto read = fs.read_file(scratch.root / "ghost");
  REQUIRE_FALSE(read.has_value());
  CHECK(read.error() == std::errc::no_such_file_or_directory);
}

TEST_CASE("NativeFileSystem: write_file into a missing directory surfaces an error") {
  ScratchDir scratch;
  NativeFileSystem fs;
  const auto ec = fs.write_file(scratch.root / "no" / "such" / "file", "x");
  CHECK(static_cast<bool>(ec));
}

TEST_CASE("NativeFileSystem: atomic_replace publishes the new whole file") {
  ScratchDir scratch;
  NativeFileSystem fs;
  const auto file = scratch.root / "state.json";
  REQUIRE_FALSE(static_cast<bool>(fs.write_file(file, "OLD-OLD-OLD")));

  REQUIRE_FALSE(static_cast<bool>(fs.atomic_replace(file, "NEW"))); // shorter than old
  const auto read = fs.read_file(file);
  REQUIRE(read.has_value());
  CHECK(read.value() == "NEW"); // fully replaced, not truncated-in-place
}

TEST_CASE("NativeFileSystem: a failed atomic_replace leaves the old content intact") {
  ScratchDir scratch;
  NativeFileSystem fs;
  const auto file = scratch.root / "state.json";
  REQUIRE_FALSE(static_cast<bool>(fs.write_file(file, "OLD")));

  // Force the staged temp-write to fail by occupying atomic_replace's stable
  // staging sibling (<file>.tmp) with a directory, so the temp file cannot be
  // created. The rename never runs; the original must survive whole.
  auto temp = file;
  temp += ".tmp";
  std::error_code ec;
  std::filesystem::create_directory(temp, ec);
  REQUIRE_FALSE(static_cast<bool>(ec));

  const auto err = fs.atomic_replace(file, "NEW");
  CHECK(static_cast<bool>(err)); // surfaced, not thrown

  const auto read = fs.read_file(file);
  REQUIRE(read.has_value());
  CHECK(read.value() == "OLD"); // either old or new — here old, never truncated
}

TEST_CASE("NativeClock: successive now() reads are monotonic") {
  NativeClock clock;
  const auto a = clock.now();
  const auto b = clock.now();
  CHECK(b >= a);
}

TEST_CASE("NativeThreads: spawn/join over a shared atomic sums correctly") {
  NativeThreads threads;
  std::atomic<int> counter{0};
  constexpr int kWorkers = 8;
  constexpr int kPerWorker = 1000;

  std::vector<std::unique_ptr<JoinHandle>> handles;
  for (int i = 0; i < kWorkers; ++i) {
    handles.push_back(threads.spawn([&counter] {
      for (int j = 0; j < kPerWorker; ++j) {
        counter.fetch_add(1, std::memory_order_relaxed);
      }
    }));
  }
  for (auto& h : handles) {
    REQUIRE(h->joinable());
    h->join();
  }
  CHECK(counter.load() == kWorkers * kPerWorker);
}

TEST_CASE("NativeThreads: a spawned handle can be detached") {
  NativeThreads threads;
  // No captures, so the thread is safe to outlive the test.
  auto h = threads.spawn([] {});
  h->detach();
  CHECK_FALSE(h->joinable());
}

namespace {

// An in-test fake proving the seam is injectable (D-platform_services-2): an
// in-memory filesystem, no disk. A consumer written against the interface works
// unchanged against it — the shape the WASM port and future consumer tests use.
class FakeFileSystem final : public FileSystem {
public:
  mutable std::map<std::filesystem::path, std::string> files;

  bool exists(const std::filesystem::path& path) const override { return files.count(path) != 0; }
  Result<std::vector<std::filesystem::path>>
  list_directory(const std::filesystem::path& dir) const override {
    std::vector<std::filesystem::path> out;
    for (const auto& [k, v] : files) {
      if (k.parent_path() == dir) {
        out.push_back(k);
      }
    }
    return out;
  }
  Result<std::string> read_file(const std::filesystem::path& path) const override {
    const auto it = files.find(path);
    if (it == files.end()) {
      return std::make_error_code(std::errc::no_such_file_or_directory);
    }
    return it->second;
  }
  std::error_code write_file(const std::filesystem::path& path,
                             std::string_view contents) const override {
    files[path] = std::string(contents);
    return {};
  }
  std::error_code make_directories(const std::filesystem::path&) const override { return {}; }
  std::error_code atomic_replace(const std::filesystem::path& path,
                                 std::string_view contents) const override {
    files[path] = std::string(contents);
    return {};
  }
};

// A consumer written against the interface, oblivious to the concrete impl.
std::string load_or_default(FileSystem& fs, const std::filesystem::path& p, std::string fallback) {
  const auto r = fs.read_file(p);
  return r.has_value() ? r.value() : fallback;
}

// A test aggregate that swaps only the filesystem faculty — the exact seam the
// WASM port and consumer tests substitute at.
class FakePlatformServices final : public PlatformServices {
public:
  FileSystem& filesystem() override { return filesystem_; }
  Clock& clock() override { return clock_; }
  Threads& threads() override { return threads_; }

  FakeFileSystem filesystem_;
  NativeClock clock_;
  NativeThreads threads_;
};

} // namespace

TEST_CASE("PlatformServices seam is injectable — a fake filesystem satisfies it") {
  FakePlatformServices services;
  REQUIRE_FALSE(static_cast<bool>(services.filesystem().write_file("/vfs/layout.json", "preset")));

  CHECK(load_or_default(services.filesystem(), "/vfs/layout.json", "def") == "preset");
  CHECK(load_or_default(services.filesystem(), "/vfs/missing", "def") == "def");

  const auto listing = services.filesystem().list_directory("/vfs");
  REQUIRE(listing.has_value());
  CHECK(listing.value().size() == 1);
}

TEST_CASE("NativePlatformServices exposes the three native faculties") {
  NativePlatformServices platform;

  const auto t0 = platform.clock().now();
  CHECK(platform.clock().now() >= t0);

  ScratchDir scratch;
  const auto file = scratch.root / "svc.txt";
  REQUIRE_FALSE(static_cast<bool>(platform.filesystem().write_file(file, "data")));
  CHECK(platform.filesystem().read_file(file).value() == "data");

  std::atomic<int> n{0};
  auto h = platform.threads().spawn([&n] { n.store(7); });
  h->join();
  CHECK(n.load() == 7);
}
