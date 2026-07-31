#pragma once

#include <ace/platform/filesystem.hpp>

#include <filesystem>
#include <string>

namespace ace::project {

// A brought-in file resolved to a BORROWED external asset (editor.import.image, D11/D13):
// the file's encoded bytes (embedded so the photo decodes and renders immediately at import)
// plus its absolute-path URI kept as a BORROWED reference — external, UN-relativized. This is
// `project`'s URI/file turf (the byte-read and the path->URI normalization); the image-config
// frame it feeds is assembled in `commands`, the one editor component that links the image
// kind (A29).
struct BorrowedAsset {
  // The file's absolute path, kept verbatim as a borrowed external reference. NOT copied into
  // `assets/` and NOT relativized here — Consolidate does both later, reversibly (D13). On
  // reopen `arbc::FilesystemAssetSource` re-resolves this URI (a `file://` prefix is stripped,
  // anything else is a plain path) to re-fetch the pixels (A19/A4.1a).
  std::string uri;
  // The encoded file bytes. EMPTY when the file is unreadable/absent — the D13 "unavailable"
  // case, a fact about the environment, never an error: the URI is still kept and the minted
  // image reports empty bounds until the borrow is relinked (Consolidate's job).
  std::string bytes;
};

// Read `path` through the `FileSystem` seam (A3 — never a raw `FILE*`) and normalize it to a
// borrowed absolute-path URI. An unreadable file yields EMPTY bytes with the URI still set
// (the unavailable case), never a throw — mirroring `FilesystemAssetSource`'s empty-on-absence
// contract. The path is made absolute but deliberately NOT relativized (borrowed, D13).
BorrowedAsset borrow_asset_file(const platform::FileSystem& fs, const std::filesystem::path& path);

} // namespace ace::project
