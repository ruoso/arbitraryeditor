#pragma once

#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>

#include <filesystem>
#include <string>
#include <string_view>

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

// A pasted/minted OWNED read-only asset (editor.import.paste, D11/D13/A30): the encoded bytes
// content-addressed into the LIVE project's `assets/` and referenced by a PROJECT-RELATIVE owned
// URI. The owned mirror of `BorrowedAsset` — a pasted bitmap has no source file to borrow, so the
// project mints it: the bytes live under `assets/`, so `scene::classify_detail` reports it owned
// (not borrowed) and GC roots the blob off the live cell (D11 "bytes where?" axis).
struct OwnedAsset {
  // The project-relative owned URI (`assets/images/<xx>/<hash>[.ext]`, the raster tile scheme),
  // authored onto the cell as `params.source`. Relative (not absolute, not `file://`) so an
  // in-place Save persists it verbatim and a reopen re-resolves it against the project root
  // through the same `arbc::FilesystemAssetSource` the borrowed path uses (A29/A4.1a).
  std::string uri;
  // The encoded bytes, echoed back so the caller embeds them in the image config for immediate
  // decode/render — identical to the borrowed path's embed, no async asset wait.
  std::string bytes;
};

// Content-address `bytes` (the raster codec's `hash_tile`/`tile_blob_uri` model) and write them
// WRITE-IF-ABSENT into `layout`'s `assets/` through the shipped `FilesystemAssetSink`, returning a
// project-relative owned URI (`assets/images/<xx>/<hash>[.ext]`) + the bytes (A30, D-paste-1). The
// same bytes always mint the SAME URI and write the blob at most once (content-address dedup); the
// URI resolves — against the project's canonical base, exactly as save/reopen do — to the written
// blob. `ext` (a bare extension like `png`, no dot) is appended for on-disk legibility only and is
// optional; the decode is format-sniffed from the bytes, never the name. Writing a blob is NOT a
// document edit (like a tile, an orphan the GC reaps) — it opens no transaction. A write fault is
// swallowed (the URI + embedded bytes still render immediately; the reopen would find no blob),
// mirroring `borrow_asset_file`'s best-effort, never-throws contract.
OwnedAsset mint_owned_asset(const platform::FileSystem& fs, const ProjectLayout& layout,
                            std::string_view bytes, std::string_view ext = {});

} // namespace ace::project
