#pragma once

#include <ace/platform/result.hpp>
#include <ace/project/project.hpp>

#include <cstdint>
#include <system_error>
#include <type_traits>

// editor.project.gc — Clean up (GC): reclaim the owned asset bytes under
// `<root>/assets/` that no longer belong to the project (D13 / docs 00 §8 / A7).
// The reclamation half of the grow-only save sink (`save.hpp:57-58` — the sink
// NEVER deletes), driving the shipped libarbc mark-sweep
// (`arbc::gc_project_directory`). L1, ImGui/GL/SDL-free (A8): `<arbc/...>` +
// `<ace/platform/...>` + `<ace/project/...>` + std only. The GC library surface
// names NO JSON type, so this adds no `nlohmann_json` link dep (unlike `save`).

namespace ace::project {

// The observable counters of one sweep — a project-vocabulary mirror of
// `arbc::GcReport` (counters only, never a timing). In `dry_run`,
// `deleted`/`bytes_reclaimed` are what a real run WOULD remove.
struct GcOutcome {
  std::uint64_t scanned = 0;         // tile blobs on disk
  std::uint64_t referenced = 0;      // hashes the canonical roots reference
  std::uint64_t deleted = 0;         // orphans deleted (would-be, in dry_run)
  std::uint64_t bytes_reclaimed = 0; // their total size

  friend bool operator==(const GcOutcome&, const GcOutcome&) = default;
};

// Errors are values, never throws (house style, mirroring `SaveError`/`OpenError`).
// Fail-safe (Constraint 7 / `asset_gc.hpp:69-72`): on `MarkFailed`/`EnumerateFailed`
// NOTHING is deleted; a `RemoveFailed` is a partial run (a strict subset of orphans
// gone).
enum class GcError {
  MarkFailed = 1,  // a root `.arbc` could not be parsed, or a blobs entry was not a hash
  EnumerateFailed, // the on-disk tile set could not be listed / sized
  RemoveFailed,    // a blob deletion failed (a partial run: a strict subset gone)
};

// Bridge `GcError` into `std::error_code` so it rides `platform::Result<T>`'s
// error channel (its error alternative is a `std::error_code`).
std::error_code make_error_code(GcError error) noexcept;

// Reclaim the orphaned owned bytes under `<layout.root>/assets/` by driving the
// shipped library mark-sweep `arbc::gc_project_directory(layout.root, dry_run)`
// (D-gc-2): scan `<root>` for `*.arbc`, union their referenced tiles, and sweep
// the rest of `assets/tiles/`. `dry_run` reports the identical plan without
// deleting (the rail's confirm-before-sweep preview). Roots on the ON-DISK
// canonical, so it needs no live `Document` and no `FileSystem` seam — the library
// owns deletion via raw `std::filesystem` (Constraint 8). NO-OP (an empty
// `GcOutcome`, no sweep) when `layout.canonical` is absent: with zero `*.arbc`
// roots the library's referenced set is empty and it would reclaim EVERY blob, so
// a freshly-created, never-saved project must not sweep (Constraint 2 / D-gc-2).
// Errors are values; touches `assets/` only — never `workspace/`, never borrowed
// files (library contract + D13).
platform::Result<GcOutcome> gc_project(const ProjectLayout& layout, bool dry_run);

} // namespace ace::project

// `GcError` participates in the `std::error_code` machinery (ADL finds
// `ace::project::make_error_code`), so `result.error() == GcError::MarkFailed`
// works and the enum rides `platform::Result`'s error channel unchanged.
template <> struct std::is_error_code_enum<ace::project::GcError> : std::true_type {};
