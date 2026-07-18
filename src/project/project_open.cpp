#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>

#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp>
#include <arbc/runtime/filesystem_asset_source.hpp>

#include <filesystem>
#include <memory>
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

// The workspace is machine-local scratch, rebuilt from the canonical core and
// excluded from sharing/VCS (D16, D-open-5).
constexpr std::string_view k_gitignore_body = "workspace/\n";

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
// are transient to this one load (D-open-7).
platform::Result<std::unique_ptr<arbc::Document>>
rebuild_from_canonical(const ProjectLayout& layout, std::string_view canonical_bytes) {
  platform::Result<std::unique_ptr<arbc::Document>> minted =
      create_workspace_document(layout.workspace_file);
  if (!minted.has_value()) {
    return minted.error();
  }
  std::unique_ptr<arbc::Document> document = std::move(*minted);

  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  arbc::KindBridge bridge;
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

platform::Result<OpenedProject> open_project(const platform::FileSystem& fs,
                                             const std::filesystem::path& root) {
  const ProjectLayout layout = project_layout(root);

  // `root` must be an existing directory: enumerating it fails on a regular file
  // or an absent path — exactly "not a project directory".
  if (!fs.list_directory(root).has_value()) {
    return make_error_code(OpenError::NotADirectory);
  }

  // Fast, durable-by-default path (D-open-3): map the crash-durable workspace when
  // the file is present and this build can map it.
  if (fs.exists(layout.workspace_file)) {
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
      rebuild_from_canonical(layout, *bytes);
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

} // namespace ace::project
