#pragma once

#include <ace/platform/filesystem.hpp>
#include <ace/platform/result.hpp>
#include <ace/project/project.hpp>

#include <arbc/base/expected.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/serialize/save_context.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string_view>
#include <system_error>
#include <type_traits>

// editor.project.save — the canonical publish direction (D-save-1). The symmetric
// mirror of editor.project.open's load path: re-emit the portable, content-addressed
// `project.arbc` (+ owned `assets/` bytes) from the live workspace `Document`. Per
// D16/§9 the workspace is durable by default, so Save is a *publish* step, not a
// race against a crash — the workspace and its checkpoints are untouched here.
// L1, ImGui/GL/SDL-free (A8): `<arbc/...>` + `<ace/platform/...>` + std only.

namespace ace::project {

// Errors are values, never throws (house style, mirroring `OpenError`): a caller
// (`commands::save_project`) branches on the value and, on failure, leaves the
// session dirty — the durable workspace is unaffected regardless.
enum class SaveError {
  SerializeFailed = 1, // capture/serialize failed (a non-finite scalar, no codec, …)
  AssetWriteFailed,    // an owned asset blob could not be durably written
  IoError,             // a filesystem fault ensuring `assets/` or publishing `project.arbc`
};

// Bridge `SaveError` into `std::error_code` so it rides `platform::Result<T>`'s
// error channel (its error alternative is a `std::error_code`).
std::error_code make_error_code(SaveError error) noexcept;

// The observable effect of one publish (Acceptance): the revision that was dumped
// (the dirty model's new clean baseline) and how many owned blobs this save newly
// wrote to `assets/` (0 for solid/probe content, which hands no bytes to the sink).
struct SaveOutcome {
  std::uint64_t revision = 0;
  std::uint64_t assets_written = 0;
};

// A `project`-side content-addressed `arbc::AssetSink` over the editor's own
// `platform::FileSystem` seam (D-save-2, Constraint 5) — the write-side mirror of
// the load path's `arbc::FilesystemAssetSource`. Routing through the injected
// `FileSystem` (rather than libarbc's direct-disk `arbc::FilesystemAssetSink`)
// keeps owned-asset I/O on the seam the WASM port swaps (A3) and makes the sink
// unit-testable in isolation. Content-addressed, WRITE-IF-ABSENT (an unchanged blob
// is neither re-hashed nor re-written — the "+ changed owned tiles" incrementality),
// crash-atomic (temp sibling + rename via `atomic_replace`), and it NEVER deletes:
// reclaiming orphaned blobs is `editor.project.gc`, never a side effect of save.
class FilesystemAssetSink final : public arbc::AssetSink {
public:
  explicit FilesystemAssetSink(const platform::FileSystem& fs) : fs_(fs) {}

  // Write `bytes` under the already-resolved (absolute) URI. Returns `true` when the
  // bytes were actually written, `false` when the name is already present (dedup),
  // or `AssetSinkError::WriteFailed` on an I/O fault (never a silent pixel drop).
  arbc::expected<bool, arbc::AssetSinkError> put(std::string_view resolved_uri,
                                                 std::span<const std::byte> bytes) override;
  bool contains(std::string_view resolved_uri) const override;
  std::uint64_t blobs_written() const noexcept override { return blobs_written_; }

private:
  const platform::FileSystem& fs_;
  std::uint64_t blobs_written_ = 0;
};

// Run one closure on the document's writer thread and return only after it has run — the
// opaque post seam a save uses to reach the writer without `project` learning what a
// `writer::WriterThread` is (§8 keeps `project` at base+platform+libarbc). Empty means "the
// caller IS the writer identity": the closure runs inline, which is exactly the headless and
// single-threaded case.
using WriterPost = std::function<void(const std::function<void()>&)>;

// Publish the live `doc` as the canonical `project.arbc` (+ owned `assets/` bytes)
// under `layout` (D-save-1). Captures the document on the WRITER THREAD
// (`arbc::capture_snapshot`) — via `post_writer`, or inline when the caller is already the
// writer — then serializes off the pinned immutable snapshot over `builtin_codecs(registry)`,
// routes owned bytes to `<root>/assets/` through a `FilesystemAssetSink`, and
// ATOMICALLY publishes the JSON to `layout.canonical` via `atomic_replace` (D16).
// The `registry` is the caller's persistent seeded one (`AppState::registry()`,
// D-app_state-2). Errors are values.
//
// The CAPTURE/SERIALIZE SPLIT is load-bearing, not an optimization detail (D-writer_thread-7):
// `capture_snapshot` reads writer-owned state (the content side-map, the unknown-field stash)
// and so must be posted, but everything after it runs over an immutable snapshot and must NOT
// be — holding the single writer thread for the whole serialize+write would stall every queued
// edit and every canvas's viewport rebuild behind a disk publish.
//
// `tiles` is the DOCUMENT'S `org.arbc.raster` hash memo (A23 / D-raster_tile_store-2), the one
// `open_project`/`create_project` minted beside the `Document` and `commands::AppState` holds
// for the process's life. It is bound into the raster codec BY CLOSURE at codec-table
// construction (`builtin_codecs(registry, tiles)`), so it must be supplied here — there is no
// post-hoc attach. A save through a live memo re-hashes only the tiles the user actually
// touched; `nullptr` is libarbc's own documented degradation — exactly
// `builtin_codecs(registry)`, still writing correct pixels, just re-hashing the whole document
// (`arbc/runtime/document_serialize.hpp:134`) — which is what keeps every pre-existing call
// site and test source-compatible. Trailing and defaulted for that reason.
platform::Result<SaveOutcome> save_project(const platform::FileSystem& fs,
                                           const ProjectLayout& layout, const arbc::Document& doc,
                                           const arbc::Registry& registry,
                                           const WriterPost& post_writer = {},
                                           arbc::RasterTileStore* tiles = nullptr);

// Publish a COPY of the live `doc` as a fresh project rooted at `target_root` — the
// portable core (`project.arbc` + content-addressed `assets/`) plus a
// `workspace/`-excluding `.gitignore` — the "Save As (copy the directory)" verb
// (D16 §9 / D-save_as-1). The "copy" is a re-publish of the *live* document into
// `project_layout(target_root)` via the trusted `save_project` core, NOT a raw byte
// copy: it captures the current in-memory state and writes only the bytes the
// document actually references. It creates NO `workspace/` (the exec'd sibling
// rebuilds it from the canonical core) and NO `exports/`, and it does NOT touch the
// source project. Refuses to clobber: if `target_root` already holds a
// `project.arbc` it returns `std::errc::file_exists` and writes nothing
// (D-save_as-4). Errors are values (the `SaveError` publish faults ride through
// unchanged); never throws.
//
// Save As takes NO tile store, and that is a correctness rule rather than an oversight (A23
// clause 2, with the full argument at the `save_project` call in `save.cpp`): the raster codec
// resolves a memo HIT entirely inside the memo and never hands the tile to the `AssetSink`, so
// a memo is implicitly scoped to ONE `assets/`. Sharing the session's into a fresh root would
// publish a canonical naming tile blobs nobody wrote there — a silently pixel-less copy. A
// Save As therefore re-hashes the document once, libarbc's documented correct-but-
// non-incremental mode, and leaves the caller's own memo untouched.
platform::Result<SaveOutcome> save_project_as(const platform::FileSystem& fs,
                                              const std::filesystem::path& target_root,
                                              const arbc::Document& doc,
                                              const arbc::Registry& registry,
                                              const WriterPost& post_writer = {});

} // namespace ace::project

// `SaveError` participates in the `std::error_code` machinery (ADL finds
// `ace::project::make_error_code`), so `result.error() == SaveError::IoError`
// works and the enum rides `platform::Result`'s error channel unchanged.
template <> struct std::is_error_code_enum<ace::project::SaveError> : std::true_type {};
