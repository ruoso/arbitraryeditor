#pragma once

#include <ace/platform/result.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace ace::platform {

// Directory/file faculty (D-platform_services-4). Fronts the editor's OWN file
// needs — enumerate a project directory, read/write layout presets, write export
// output — and deliberately does NOT model libarbc's mmap workspace (A3): that
// stays behind the library's document/workspace API. The WASM port swaps this
// for the File System Access API / OPFS.
class FileSystem {
public:
  virtual ~FileSystem() = default;

  virtual bool exists(const std::filesystem::path& path) const = 0;

  // Immediate entries of `dir` (order unspecified). Returns an error (never
  // throws) when `dir` is missing or not a directory.
  virtual Result<std::vector<std::filesystem::path>>
  list_directory(const std::filesystem::path& dir) const = 0;

  // Whole-file read as raw bytes. Returns the typed error on a missing or
  // unreadable file rather than throwing.
  virtual Result<std::string> read_file(const std::filesystem::path& path) const = 0;

  // Whole-file write (create/truncate). Non-atomic — use atomic_replace for the
  // editor's own local state (D16).
  virtual std::error_code write_file(const std::filesystem::path& path,
                                     std::string_view contents) const = 0;

  // mkdir -p: creates missing parents and succeeds (no error) if the directory
  // already exists.
  virtual std::error_code make_directories(const std::filesystem::path& dir) const = 0;

  // Atomic publish (D16): write a temp sibling then rename it over `path`, so a
  // concurrent reader or a crash sees either the whole old file or the whole new
  // file — never a truncated one. Save is a publish step, not a crash race.
  virtual std::error_code atomic_replace(const std::filesystem::path& path,
                                         std::string_view contents) const = 0;

  // Recursively remove `path` and everything beneath it. Idempotent: an absent
  // path is success, mirroring `make_directories`. Returns the typed error
  // (never throws) on a partial or failed removal, and refuses an empty path
  // with `std::errc::invalid_argument`. This is the seam's only destructive
  // faculty (A26) — the caller owns the policy question of what it is entitled
  // to delete.
  virtual std::error_code remove_tree(const std::filesystem::path& path) const = 0;
};

// Native impl over <filesystem> / <fstream>.
class NativeFileSystem final : public FileSystem {
public:
  bool exists(const std::filesystem::path& path) const override;
  Result<std::vector<std::filesystem::path>>
  list_directory(const std::filesystem::path& dir) const override;
  Result<std::string> read_file(const std::filesystem::path& path) const override;
  std::error_code write_file(const std::filesystem::path& path,
                             std::string_view contents) const override;
  std::error_code make_directories(const std::filesystem::path& dir) const override;
  std::error_code atomic_replace(const std::filesystem::path& path,
                                 std::string_view contents) const override;
  std::error_code remove_tree(const std::filesystem::path& path) const override;
};

} // namespace ace::platform
