#include <ace/platform/filesystem.hpp>
#include <ace/platform/result.hpp>
#include <ace/project/import_asset.hpp>

#include <filesystem>
#include <system_error>
#include <utility>

namespace ace::project {

BorrowedAsset borrow_asset_file(const platform::FileSystem& fs, const std::filesystem::path& path) {
  BorrowedAsset asset;

  // Normalize to an absolute path kept as the borrowed URI. `weakly_canonical` resolves
  // `..`/symlinks without requiring the file to exist; on any error fall back to `absolute`,
  // and on that failing too, the path verbatim — the URI is best-effort, never a throw.
  std::error_code ec;
  std::filesystem::path resolved = std::filesystem::weakly_canonical(path, ec);
  if (ec || resolved.empty()) {
    ec.clear();
    resolved = std::filesystem::absolute(path, ec);
  }
  asset.uri = (ec ? path : resolved).string();

  // Embed the bytes so the image decodes immediately. An unreadable/absent file leaves the
  // bytes empty (the D13 unavailable case) with the borrowed URI still recorded.
  platform::Result<std::string> read = fs.read_file(path);
  if (read.has_value()) {
    asset.bytes = std::move(*read);
  }
  return asset;
}

} // namespace ace::project
