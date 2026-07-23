#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>

#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/model/model.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp>
#include <arbc/runtime/filesystem_asset_source.hpp>

#include <cstddef>
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

// How many of `document`'s recovered content records the binding table could not
// serve (A19). `arbc::Document::open` takes no `Registry` and runs no factory, so a
// workspace-mapped document's id→`Content` side-map starts EMPTY: the record graph
// comes back verbatim — composition, layers, transforms, persisted `StateHandle`
// slots — while `resolve()` answers null for every recovered record, for EVERY kind.
// Walking `DocRoot::for_each_layer` (the global, ascending-object-id walk, so a
// content placed inside a nested composition is counted too) and testing each layer's
// bound content against `resolve` is therefore the direct measurement of "how much of
// this mapped document is unusable". Zero means the map is serviceable: the record
// graph was all there was to recover. The editor attaches each placed content by
// exactly one layer (`scene::add_cell` / `scene::add_camera`), so the tally is the
// count of unrecoverable placed objects.
//
// A lock-free pinned read (A4) on the opening thread, before the document is handed
// to any renderer; `resolve` is documented safe from any thread regardless.
std::size_t count_unbindable_content(const arbc::Document& document) {
  const arbc::DocStatePtr pinned = document.pin();
  if (pinned == nullptr) {
    return 0;
  }
  std::size_t unbindable = 0;
  pinned->for_each_layer([&](const arbc::LayerRecord& layer) {
    if (layer.content.valid() && document.resolve(layer.content) == nullptr) {
      ++unbindable;
    }
  });
  return unbindable;
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

  // Fast, durable-by-default path (D-open-3): map the crash-durable workspace when the
  // file is present — then KEEP the mapped document only when it is actually usable.
  //
  // What the map path really does (A19, pinned by tests/arbc_pin_test.cpp and
  // tests/project_open_test.cpp): `arbc::Document::open(path, housekeeping)` takes no
  // `Registry` and runs no factory (arbc `runtime/document.hpp:76-85`), so it restores
  // the RECORD GRAPH ONLY. The composition, the layers, their transforms and the
  // persisted `StateHandle` slots all come back; the id→`Content` side-map starts empty,
  // so `resolve()` answers null for every recovered record and `for_each_content()`
  // visits none. That is KIND-AGNOSTIC — verified for a non-editable built-in
  // (`org.arbc.solid`) and an editable kind alike — so a mapped reopen of a
  // content-bearing project yields zero cells AND zero cameras, both of which read
  // through `Document::resolve`. The canonical rebuild (`load_document` over the
  // `Registry` codec table) is the ONLY route that produces live content.
  //
  // NOTE for the next reader, because the previous version of this comment said
  // otherwise: this guard is NOT about libarbc aborting. It was first written that way
  // — v0.1.0's `Model::rebuild_counts` asserted every recovered `ContentRecord` carried
  // an INERT `StateHandle` (A15) — and at the v0.3.0 pin that assert is GONE
  // (`model.cpp:768-783` collects the handle instead; the arbc#5 `KindStateWalker` /
  // `replay_recovered_content_state` trio exists but its input is unreachable from
  // `arbc::Document`). Mapping a checkpointed camera is safe today. It is simply useless
  // for content — which is why the guard outlives its own dead premise, for a larger
  // reason than A15 states. Deleting it would silently empty every reopened project.
  //
  // So: map, then INSPECT. `register_extra_kinds` survives only as a cheap
  // SHORT-CIRCUIT — an editor-kind session with a canonical present will always reject a
  // content-bearing map, so we do not open a potentially large workspace arena just to
  // discard it, and today's I/O profile on the common editor reopen is unchanged
  // (D-slab_adopt-4). Every other caller maps and is judged on what came back.
  const bool canonical_exists = fs.exists(layout.canonical);
  const bool skip_map_for_editor_kinds = register_extra_kinds && canonical_exists;
  if (fs.exists(layout.workspace_file) && !skip_map_for_editor_kinds) {
    auto mapped = arbc::Document::open(layout.workspace_file.string());
    if (mapped.has_value()) {
      const std::size_t unbindable = count_unbindable_content(**mapped);
      // Keep the map when it bound everything (the fast path survives where it is
      // harmless, preserving A13's recovery of unpublished RECORD-level edits — layer
      // transforms, z-order, composition size), and keep it when there is no canonical
      // to rebuild from: the mapped document is then all the project has, and returning
      // `NoProject` instead would be strictly worse. In that second case the loss is
      // REPORTED rather than silently swallowed (D-slab_adopt-5 / D-slab-3's residual).
      if (unbindable == 0 || !canonical_exists) {
        OpenedProject opened;
        opened.document = std::move(*mapped);
        opened.layout = layout;
        opened.rebuilt_from_canonical = false;
        opened.unbindable_content_records = unbindable;
        return opened;
      }
      // Unbindable content plus a canonical floor: fall through. `mapped` dies at the
      // end of this block, unmapping the workspace file before `rebuild_from_canonical`
      // truncates and re-mints it.
    }
    // A returned WorkspaceFileError (truncated / another machine / stale) is not an
    // error — it falls through to rebuild-from-canonical below.
  }

  // Rebuild from the canonical core. If it too is absent, this is not a project.
  if (!canonical_exists) {
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
