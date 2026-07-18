#include <ace/project/gc.hpp>

#include <arbc/base/expected.hpp>
#include <arbc/runtime/asset_gc.hpp>

#include <filesystem>
#include <string>
#include <system_error>

// editor.project.gc — the reclamation half of `save`'s grow-only asset sink
// (D-gc-1/2). Thin wiring: guard on canonical presence, drive the shipped
// `arbc::gc_project_directory` over the on-disk root, and map its report/error onto
// `platform::Result`. Errors are values (Constraint 7); no editor-owned thread, no
// live-document serialization — the mark walk reads the on-disk canonical's text.

namespace ace::project {
namespace {

// `GcError` gets a `std::error_code` category so it can ride the error channel of
// `platform::Result<T>` (Constraint 7, mirroring `SaveErrorCategory`).
class GcErrorCategory final : public std::error_category {
public:
  const char* name() const noexcept override { return "ace.project.gc"; }

  std::string message(int value) const override {
    switch (static_cast<GcError>(value)) {
    case GcError::MarkFailed:
      return "GC mark failed (a root document could not be read)";
    case GcError::EnumerateFailed:
      return "GC could not enumerate the on-disk asset store";
    case GcError::RemoveFailed:
      return "GC could not remove an orphaned asset blob";
    }
    return "unknown project GC error";
  }
};

const std::error_category& gc_error_category() noexcept {
  static const GcErrorCategory category;
  return category;
}

// Map the library's failure kind onto the `project` value. Fail-safe semantics are
// the library's (`asset_gc.hpp:69-72`); this only re-vocabularizes the kind.
GcError map_error(arbc::GcError::Kind kind) noexcept {
  switch (kind) {
  case arbc::GcError::Kind::MarkFailed:
    return GcError::MarkFailed;
  case arbc::GcError::Kind::EnumerateFailed:
    return GcError::EnumerateFailed;
  case arbc::GcError::Kind::RemoveFailed:
    return GcError::RemoveFailed;
  }
  return GcError::MarkFailed;
}

} // namespace

std::error_code make_error_code(GcError error) noexcept {
  return {static_cast<int>(error), gc_error_category()};
}

platform::Result<GcOutcome> gc_project(const ProjectLayout& layout, bool dry_run) {
  // No-canonical guard (Constraint 2 / D-gc-2): with zero `*.arbc` roots the
  // library's referenced set is empty and `gc_project_directory` would reclaim
  // EVERY blob under `assets/`. A freshly-created, never-saved project has no
  // `project.arbc` to root the mark on — no-op WITHOUT sweeping (belt-and-braces:
  // in that state `assets/` is in fact empty, the sink only runs on save).
  std::error_code ec;
  if (!std::filesystem::exists(layout.canonical, ec)) {
    return GcOutcome{};
  }

  // Drive the shipped mark-sweep against the on-disk canonical (D-gc-2): scan
  // `<root>` for `*.arbc`, union their referenced tiles, resolve `assets/tiles/`
  // against the root the same way save/load do, and sweep that shared store. A raw
  // `std::filesystem::path`, not the `FileSystem` seam — the library owns deletion
  // (Constraint 8). `dry_run` computes and reports the identical plan; on any error
  // nothing is deleted beyond a `RemoveFailed` partial (fail-safe).
  const arbc::expected<arbc::GcReport, arbc::GcError> report =
      arbc::gc_project_directory(layout.root, dry_run);
  if (!report.has_value()) {
    return make_error_code(map_error(report.error().kind));
  }
  return GcOutcome{report->scanned, report->referenced, report->deleted, report->bytes_reclaimed};
}

} // namespace ace::project
