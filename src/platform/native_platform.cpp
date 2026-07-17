#include <ace/platform/clock.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/threads.hpp>

#include <fstream>
#include <iterator>
#include <thread>
#include <utility>

namespace ace::platform {

// ---- clock -----------------------------------------------------------------

Clock::Duration NativeClock::now() const {
  return std::chrono::steady_clock::now().time_since_epoch();
}

// ---- threads ---------------------------------------------------------------

namespace {

// Owns one std::thread; join/detach forward to it. The bare editor-owned
// auxiliary-thread primitive (D-platform_services-3) — deliberately not a pool.
class NativeJoinHandle final : public JoinHandle {
public:
  explicit NativeJoinHandle(std::function<void()> work) : thread_(std::move(work)) {}
  void join() override { thread_.join(); }
  void detach() override { thread_.detach(); }
  bool joinable() const override { return thread_.joinable(); }

private:
  std::thread thread_;
};

} // namespace

std::unique_ptr<JoinHandle> NativeThreads::spawn(std::function<void()> work) {
  return std::make_unique<NativeJoinHandle>(std::move(work));
}

// ---- filesystem ------------------------------------------------------------

bool NativeFileSystem::exists(const std::filesystem::path& path) const {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

Result<std::vector<std::filesystem::path>>
NativeFileSystem::list_directory(const std::filesystem::path& dir) const {
  std::error_code ec;
  std::filesystem::directory_iterator it(dir, ec);
  if (ec) {
    return ec;
  }
  const std::filesystem::directory_iterator end;
  std::vector<std::filesystem::path> entries;
  while (it != end) {
    entries.push_back(it->path());
    it.increment(ec);
    if (ec) {
      return ec;
    }
  }
  return entries;
}

Result<std::string> NativeFileSystem::read_file(const std::filesystem::path& path) const {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::make_error_code(std::errc::no_such_file_or_directory);
  }
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::error_code NativeFileSystem::write_file(const std::filesystem::path& path,
                                             std::string_view contents) const {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return std::make_error_code(std::errc::io_error);
  }
  out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!out) {
    return std::make_error_code(std::errc::io_error);
  }
  return {};
}

std::error_code NativeFileSystem::make_directories(const std::filesystem::path& dir) const {
  std::error_code ec;
  // create_directories returns false when `dir` already exists but leaves `ec`
  // clear — so pre-existing directories are reported as success (idempotent).
  std::filesystem::create_directories(dir, ec);
  return ec;
}

std::error_code NativeFileSystem::atomic_replace(const std::filesystem::path& path,
                                                 std::string_view contents) const {
  // Stage into a sibling temp (same directory → same filesystem, so the rename
  // is atomic), then rename over the target. The target is never opened for
  // writing directly, so it is never left truncated.
  std::filesystem::path temp = path;
  temp += ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
      return std::make_error_code(std::errc::io_error);
    }
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    out.flush();
    if (!out) {
      std::error_code rm;
      std::filesystem::remove(temp, rm);
      return std::make_error_code(std::errc::io_error);
    }
  }
  std::error_code ec;
  std::filesystem::rename(temp, path, ec);
  if (ec) {
    std::error_code rm;
    std::filesystem::remove(temp, rm);
    return ec;
  }
  return {};
}

} // namespace ace::platform
