#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>

#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp>
#include <arbc/runtime/filesystem_asset_source.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

// editor.project.open — turn a project directory into a live libarbc `Document`,
// and scaffold a new one. Load direction only (D-open-1..7). Directory
// scaffolding/enumeration goes through `platform::FileSystem`; the workspace file
// and the document go through libarbc (D-platform_services-4). Errors are values
// (D-open-6); no editor-owned thread (the library owns the per-`Document`
// HousekeepingThread that checkpoints the workspace, A4).

namespace ace::project {
namespace {

// `OpenError` gets a `std::error_code` category so it can ride the
// `std::error_code` error channel of `platform::Result<T>` (D-open-6).
class OpenErrorCategory final : public std::error_category {
public:
  const char* name() const noexcept override { return "ace.project.open"; }

  std::string message(int value) const override {
    switch (static_cast<OpenError>(value)) {
    case OpenError::NotADirectory:
      return "path is not a project directory";
    case OpenError::NoProject:
      return "no project (neither a workspace nor a project.arbc)";
    case OpenError::CorruptDocument:
      return "project.arbc failed to parse";
    case OpenError::IoError:
      return "project I/O error";
    }
    return "unknown project open error";
  }
};

const std::error_category& open_error_category() noexcept {
  static const OpenErrorCategory category;
  return category;
}

// Mint a fresh workspace-backed document over `workspace_file`. `Document::create`
// truncates any stale/garbage file already there (O_CREAT|O_TRUNC), so it doubles
// as the "overwrite the unusable workspace" step of the rebuild path. A workspace
// file this build cannot mint comes back as a `WorkspaceFileError` value, mapped
// to `IoError` (never a throw).
platform::Result<std::unique_ptr<arbc::Document>>
create_workspace_document(const std::filesystem::path& workspace_file) {
  auto created = arbc::Document::create(workspace_file.string());
  if (!created.has_value()) {
    return make_error_code(OpenError::IoError);
  }
  return std::move(*created);
}

// Rebuild a document from the canonical `project.arbc` bytes (D-open-3): a fresh
// workspace, `load_document` of the canonical bytes, then a checkpoint so the
// recovery is itself crash-durable. The kind Registry + KindBridge + AssetSource
// are transient to this one load (D-open-7). `register_extra_kinds`, when set,
// augments the transient load registry with the caller's editor-authored kinds
// right after the built-ins (D-reopen-1) — the seam that keeps a persisted camera a
// live `scene::CameraContent` on reopen instead of a degraded placeholder — while
// `project` stays typed only on `arbc::Registry` (no `project->scene` edge).
platform::Result<std::unique_ptr<arbc::Document>>
rebuild_from_canonical(const ProjectLayout& layout, std::string_view canonical_bytes,
                       const std::function<void(arbc::Registry&)>& register_extra_kinds) {
  platform::Result<std::unique_ptr<arbc::Document>> minted =
      create_workspace_document(layout.workspace_file);
  if (!minted.has_value()) {
    return minted.error();
  }
  std::unique_ptr<arbc::Document> document = std::move(*minted);

  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  // Apply the caller's editor-kind registrar (idempotent, first-wins) so
  // `load_document` finds the codec for an editor-authored kind rather than degrading
  // the record to `PlaceholderContent` (Constraint 2/4). Skipped when unset.
  if (register_extra_kinds) {
    register_extra_kinds(registry);
  }
  // Seed the load bridge from the registry, exactly as `save_project` seeds its own
  // (save.cpp:115-116). `KindBridge()` alone pre-interns only the built-in LEAF kinds
  // and `load_document` then interns the rest in FILE-ENCOUNTER order — which makes
  // the reloaded document's `ContentRecord.kind` tokens depend on what the file
  // happened to contain. Seeding first makes them registry-order deterministic and
  // therefore identical to the tokens the author-side mints
  // (`scene::add_cell` / `scene::add_camera`), which is what lets a token-based kind
  // read-back (`scene::cells`, D-cells_model-8) survive a reopen. Tokens are never
  // serialized, so this changes no bytes on disk.
  arbc::KindBridge bridge;
  seed_kind_bridge(bridge, registry);
  arbc::FilesystemAssetSource assets;
  const auto loaded = arbc::load_document(canonical_bytes, *document, bridge, registry,
                                          layout.canonical.string(), &assets);
  if (!loaded.has_value()) {
    return make_error_code(OpenError::CorruptDocument);
  }
  if (!document->checkpoint().has_value()) {
    return make_error_code(OpenError::IoError);
  }
  return document;
}

} // namespace

std::error_code make_error_code(OpenError error) noexcept {
  return {static_cast<int>(error), open_error_category()};
}

ProjectLayout project_layout(const std::filesystem::path& root) {
  ProjectLayout layout;
  layout.root = root;
  layout.canonical = root / "project.arbc";
  layout.assets_dir = root / "assets";
  layout.workspace_dir = root / "workspace";
  layout.workspace_file = layout.workspace_dir / "document.arbcws";
  layout.exports_dir = root / "exports";
  layout.gitignore = root / ".gitignore";
  return layout;
}

platform::Result<OpenedProject>
open_project(const platform::FileSystem& fs, const std::filesystem::path& root,
             const std::function<void(arbc::Registry&)>& register_extra_kinds) {
  const ProjectLayout layout = project_layout(root);

  // `root` must be an existing directory: enumerating it fails on a regular file
  // or an absent path — exactly "not a project directory".
  if (!fs.list_directory(root).has_value()) {
    return make_error_code(OpenError::NotADirectory);
  }

  // Fast, durable-by-default path (D-open-3): map the crash-durable workspace when
  // the file is present and this build can map it — UNLESS this session may hold an
  // editor-defined *editable* kind and a canonical baseline exists to rebuild from
  // (A15). libarbc v0.1.0's map path (`Document::open` -> `Model::rebuild_counts`)
  // asserts on a recovered non-inert `StateHandle` (`model.cpp:771`) and exposes no
  // per-kind state-slab walk hook, so a checkpointed camera aborts (debug) / corrupts
  // the handle (release) there. A non-empty `register_extra_kinds` is the only
  // crash-safe signal a custom editable Content MAY be in the map — an *unpublished*
  // camera can sit in the map with the canonical still camera-free, so canonical-content
  // detection would fail open and abort. So we fail safe: force rebuild-from-canonical
  // for an editor-kind session with a canonical floor (D-slab-1/2), accepting the
  // conservative false-positive that a camera-free editor session rebuilds too. The
  // fast path stays verbatim for callers that register no editor kinds (tools/tests,
  // Constraint 3) and for the never-saved case with no canonical to rebuild from
  // (Constraint 4 / D-slab-3 — a rebuild would only return `NoProject`).
  const bool force_rebuild_for_editor_kinds = register_extra_kinds && fs.exists(layout.canonical);
  if (fs.exists(layout.workspace_file) && !force_rebuild_for_editor_kinds) {
    auto mapped = arbc::Document::open(layout.workspace_file.string());
    if (mapped.has_value()) {
      OpenedProject opened;
      opened.document = std::move(*mapped);
      opened.layout = layout;
      opened.rebuilt_from_canonical = false;
      return opened;
    }
    // A returned WorkspaceFileError (truncated / another machine / stale) is not an
    // error — it falls through to rebuild-from-canonical below.
  }

  // Rebuild from the canonical core. If it too is absent, this is not a project.
  if (!fs.exists(layout.canonical)) {
    return make_error_code(OpenError::NoProject);
  }
  const platform::Result<std::string> bytes = fs.read_file(layout.canonical);
  if (!bytes.has_value()) {
    return make_error_code(OpenError::IoError);
  }

  // The workspace subtree may not exist yet (a fresh clone carries only the core);
  // materialize it before libarbc mints the workspace file inside it.
  if (fs.make_directories(layout.workspace_dir)) {
    return make_error_code(OpenError::IoError);
  }

  platform::Result<std::unique_ptr<arbc::Document>> rebuilt =
      rebuild_from_canonical(layout, *bytes, register_extra_kinds);
  if (!rebuilt.has_value()) {
    return rebuilt.error();
  }

  OpenedProject opened;
  opened.document = std::move(*rebuilt);
  opened.layout = layout;
  opened.rebuilt_from_canonical = true;
  return opened;
}

platform::Result<OpenedProject> create_project(const platform::FileSystem& fs,
                                               const std::filesystem::path& root) {
  const ProjectLayout layout = project_layout(root);

  // Scaffold the canonical subdirectories (mkdir -p, idempotent). Creating each
  // leaf also materializes `root` (D16 layout, D-open-5).
  for (const std::filesystem::path& dir :
       {layout.assets_dir, layout.workspace_dir, layout.exports_dir}) {
    if (fs.make_directories(dir)) {
      return make_error_code(OpenError::IoError);
    }
  }

  // Exclude the machine-local workspace scratch from VCS, atomically (D-open-5).
  if (fs.atomic_replace(layout.gitignore, k_gitignore_body)) {
    return make_error_code(OpenError::IoError);
  }

  // Mint a fresh workspace-backed document and make it durable by default via
  // checkpoint (D-open-4). No project.arbc is written — that is save's publish step.
  platform::Result<std::unique_ptr<arbc::Document>> minted =
      create_workspace_document(layout.workspace_file);
  if (!minted.has_value()) {
    return minted.error();
  }
  std::unique_ptr<arbc::Document> document = std::move(*minted);
  if (!document->checkpoint().has_value()) {
    return make_error_code(OpenError::IoError);
  }

  OpenedProject opened;
  opened.document = std::move(document);
  opened.layout = layout;
  opened.rebuilt_from_canonical = false;
  return opened;
}

bool is_project_directory(const platform::FileSystem& fs, const std::filesystem::path& root) {
  // Mirror open_project's recognition without opening a Document (A7): `root` must
  // be an enumerable directory, and either the workspace file or the canonical
  // project.arbc must be present.
  if (!fs.list_directory(root).has_value()) {
    return false;
  }
  const ProjectLayout layout = project_layout(root);
  return fs.exists(layout.workspace_file) || fs.exists(layout.canonical);
}

std::optional<std::filesystem::path> compose_new_project_target(const std::filesystem::path& parent,
                                                                std::string_view name) {
  if (parent.empty() || name.empty()) {
    return std::nullopt;
  }
  // A project name must be exactly one path component: reject separators (which
  // would nest or, via `..`, escape) and the dot entries. Whitespace-only names
  // are rejected too so a blank field never composes `parent / " "`.
  if (name.find('/') != std::string_view::npos || name.find('\\') != std::string_view::npos) {
    return std::nullopt;
  }
  if (name == "." || name == "..") {
    return std::nullopt;
  }
  if (name.find_first_not_of(" \t") == std::string_view::npos) {
    return std::nullopt;
  }
  return parent / std::filesystem::path(name);
}

} // namespace ace::project
