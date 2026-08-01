#include <ace/platform/filesystem.hpp>
#include <ace/platform/result.hpp>
#include <ace/project/import_asset.hpp>
#include <ace/project/save.hpp> // FilesystemAssetSink — the shipped write-if-absent owned-asset sink

#include <arbc/serialize/save_context.hpp> // arbc::SaveContext — relative->absolute URI resolution
#include <arbc/serialize/tile_blob.hpp> // arbc::hash_tile / arbc::tile_blob_uri (content-address)

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace ace::project {

namespace {

// The owned-image content-address base, mirroring the raster codec's `assets/tiles/` scheme (a
// pasted image is already in the consolidated, project-relative owned form, D13/A30).
constexpr std::string_view k_owned_images_base = "assets/images/";

} // namespace

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

OwnedAsset mint_owned_asset(const platform::FileSystem& fs, const ProjectLayout& layout,
                            std::string_view bytes, std::string_view ext) {
  OwnedAsset asset;
  asset.bytes = std::string(bytes);

  // Content-address the bytes exactly as the raster codec addresses a tile: SHA-256 -> hex, split
  // into a two-hex fan-out dir + the full hash. Same bytes -> same URI (dedup); different bytes ->
  // a different URI. The extension is appended for on-disk legibility only (the decode sniffs the
  // bytes, not the name).
  const std::span<const std::byte> span(reinterpret_cast<const std::byte*>(bytes.data()),
                                        bytes.size());
  const std::string hash = arbc::hash_tile(span);
  asset.uri = arbc::tile_blob_uri(k_owned_images_base, hash);
  if (!ext.empty()) {
    asset.uri += '.';
    asset.uri += ext;
  }

  // Write WRITE-IF-ABSENT into the live project's `assets/` through the shipped sink, resolving the
  // project-relative URI against the canonical base the SAME way `save_project` and the reopen do
  // (base `<root>/project.arbc` -> `assets/...` lands at `<root>/assets/...`). A repeat mint of the
  // same bytes finds the blob present and no-ops; a write fault is swallowed (best-effort, the
  // embedded bytes still render — a document edit this is not, so there is no error to surface).
  FilesystemAssetSink sink(fs);
  arbc::SaveContext ctx(layout.canonical.string());
  ctx.set_asset_sink(&sink);
  (void)ctx.store_asset(asset.uri, span);
  return asset;
}

} // namespace ace::project
