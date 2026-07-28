#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>

#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/model/model.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp>
#include <arbc/runtime/filesystem_asset_source.hpp>
#include <arbc/runtime/raster_tile_store.hpp> // arbc::RasterTileStore (the load SEEDS it)
#include <arbc/serialize/codec.hpp>           // arbc::CodecTable (complete type for builtin_codecs)
#include <arbc/serialize/load_context.hpp> // arbc::LoadContext (arbc::open_document's reconstruction ctx)

#include <charconv>
#include <cstddef>
#include <cstdint>
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
    case OpenError::TargetExists:
      return "the target path already exists";
    }
    return "unknown project open error";
  }
};

const std::error_category& open_error_category() noexcept {
  static const OpenErrorCategory category;
  return category;
}

// The D27 guard in `create_project` returned false for `fs.exists(root)`
// immediately before the scaffold, so every byte under `root` was written by
// THIS call — removing it can destroy nothing but our own debris (A26). Runs
// unconditionally on each post-guard failure branch: `remove_tree` on an absent
// path is success (idempotent), so the mkdir branch that failed before `root`
// materialized rolls back harmlessly too (D-create_rollback-5). The removal's
// own error is DISCARDED on purpose — the caller's actionable fact is why the
// CREATE failed, and a failed cleanup only restores the pre-leaf behaviour
// (D-create_rollback-3). The merged mint/checkpoint tail destroys any live
// mmap-backed `Document` (via its scoped `unique_ptr`) BEFORE calling this
// (D-create_rollback-2); the mkdir and gitignore branches hold no live
// `Document` and call it bare.
void discard_partial_scaffold(const platform::FileSystem& fs, const std::filesystem::path& root) {
  (void)fs.remove_tree(root);
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
//
//
// `tiles` is the CALLER'S store (A23 / Constraint 6) — never one minted here. The load
// SEEDS it: every tile it decodes carries a blob name the reader already knows, so seeding
// makes the session's first save a pure memo sweep instead of a full re-hash of the
// document. It must therefore outlive this call and belong to the `Document`, which is
// exactly what separates it from the transient `Registry`/`KindBridge`/`AssetSource`
// (D-open-7). The `TileDecodeDispatch` stays null: a worker-backed decode needs an
// `arbc::WorkerPool` L1 cannot reach and which does not exist at open time, and a null
// dispatch decodes INLINE, byte-identically to the serial load (D-raster_tile_store-4,
// `arbc/runtime/document_serialize.hpp:212`). `open.md` Constraint 7 is kept, not amended.
//
// Taken BY REFERENCE-TO-`unique_ptr`, not as a raw pointer, for one reason: a seeded memo
// holds owning `BlockRef` pins into `document`'s pool (`raster_tile_store.hpp:141`), so the
// one branch that loads successfully and THEN fails (a faulted `checkpoint`) would destroy
// the pool here while the caller's store still pinned into it. Releasing the store inside
// that branch, while `document` is still alive, is the same release-before-the-`Document`
// rule Constraint 2 states for the declaration order of `OpenedProject`/`AppState`.
platform::Result<std::unique_ptr<arbc::Document>>
rebuild_from_canonical(const ProjectLayout& layout, std::string_view canonical_bytes,
                       const std::function<void(arbc::Registry&)>& register_extra_kinds,
                       std::unique_ptr<arbc::RasterTileStore>& tiles) {
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
  // Install content-identity capture BEFORE the load so every `ContentRecord` the load mints
  // carries its construction identity into the freshly-rebuilt workspace (Constraint 3 /
  // D-reconstructing_reopen-5): `load_document` binds each recovered content through
  // `Document::add_content`, which snapshots the identity when this capture is installed, so the
  // rebuilt workspace is itself reconstructable on its NEXT map reopen instead of degrading to a
  // full canonical rebuild every time. The capture BORROWS `codecs` and `bridge`, both local and
  // alive through the load and the checkpoint below; it is cleared before the document escapes
  // this scope so no dangling closure survives (the session re-installs its own over `registry_`
  // in `AppState`, editor.project.reconstructing_reopen).
  const arbc::CodecTable codecs = arbc::builtin_codecs(registry, tiles.get());
  document->set_content_identity_capture(arbc::codec_identity_capture(codecs, bridge));
  const auto loaded = arbc::load_document(canonical_bytes, *document, bridge, registry,
                                          layout.canonical.string(), &assets, tiles.get(),
                                          /*decode=*/nullptr);
  if (!loaded.has_value()) {
    return make_error_code(OpenError::CorruptDocument);
  }
  if (!document->checkpoint().has_value()) {
    // The load may already have seeded the memo with pins into `document`'s pool; release
    // them HERE, before `document` unwinds at the return below (see the note above).
    tiles.reset();
    return make_error_code(OpenError::IoError);
  }
  // The load-time capture has done its job (the checkpoint above persisted the captured
  // identity); clear it so the moved-out `document` carries no closure into the freed `codecs`
  // and `bridge` locals — `AppState` installs the session capture over its own long-lived state.
  document->set_content_identity_capture({});
  return document;
}

// Whether the mapped-reconstruct reopen recovered a document in sync with the last publish
// (D28 / D-reconstructing_reopen-3/-4). Reads the `workspace/published.rev` sidecar through the
// `platform::FileSystem` seam (A3, never a raw `std::filesystem`) and returns true IFF it is
// present AND names exactly the WORKSPACE'S LAST-CHECKPOINTED revision.
//
// The mapped document's `pin()->revision()` is that checkpointed revision plus one: `open_document`
// rebinds objects onto EXISTING records and "publishes no version" (arbc `document_serialize.cpp`),
// but `arbc::Document::open` — the map replay it wraps — advances the revision by exactly one
// bookkeeping step (verified in `tests/arbc_pin_test.cpp` for 0, 1, and 2 reconstructed records,
// so the step is the MAP's, not the reconstruction's). A clean publish wrote the checkpointed
// revision to the sidecar (`save_project` captured the same revision the workspace was
// checkpointed at), so IN SYNC is `published == reopened_revision - 1`. Absent, unreadable,
// unparseable, or mismatched all answer FALSE — the safe direction (a false-dirty, never a
// false-clean): a false-clean would need the sidecar to name a revision the workspace does not
// hold, which the publish-then-sidecar write ordering forbids (`save_project`); and because the
// only revision that reads clean is exactly `reopened - 1`, ANY unsaved edit (≥ +1) or a future
// change to the map's replay step degrades to false-dirty rather than false-clean. A lock-free
// `pin()` read (A4) on the opening thread, before the document is handed to any renderer.
bool mapped_reopen_in_sync(const platform::FileSystem& fs, const ProjectLayout& layout,
                           const arbc::Document& document) {
  if (!fs.exists(layout.published_rev)) {
    return false;
  }
  const platform::Result<std::string> contents = fs.read_file(layout.published_rev);
  if (!contents.has_value()) {
    return false;
  }
  std::string_view text = *contents;
  // Trim a trailing newline a hand-edited sidecar might carry; the writer adds none, but
  // otherwise `from_chars`'s tail check below would reject it.
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
    text.remove_suffix(1);
  }
  std::uint64_t published = 0;
  const char* const first = text.data();
  const char* const last = text.data() + text.size();
  const std::from_chars_result parsed = std::from_chars(first, last, published);
  if (parsed.ec != std::errc{} || parsed.ptr != last) {
    return false; // not a clean decimal `std::uint64_t`: treat as stale → dirty
  }
  const arbc::DocStatePtr pinned = document.pin();
  return pinned != nullptr && pinned->revision() > 0 && published == pinned->revision() - 1;
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
  layout.published_rev = layout.workspace_dir / "published.rev";
  return layout;
}

namespace {
// The heap-stable backing for a document's identity capture: `arbc::codec_identity_capture`
// borrows a codec table AND a bridge BY REFERENCE, so both must have a stable address for the
// captured document's whole life — even as the OWNER (e.g. `commands::AppState`) is moved by
// value. A `KindBridge` VALUE MEMBER of a movable owner would relocate and dangle the borrow, so
// the capture owns its OWN bridge here, seeded from the same registry the author-side mints
// intern through (`seed_kind_bridge` makes the tokens registry-order deterministic and therefore
// identical), rather than borrowing the owner's. Held behind one heap allocation whose address
// the returned `shared_ptr` keeps stable across every move.
struct CaptureBacking {
  arbc::KindBridge bridge;
  arbc::CodecTable codecs;
};
} // namespace

std::shared_ptr<void> install_content_identity_capture(arbc::Document& document,
                                                       const arbc::Registry& registry) {
  auto backing = std::make_shared<CaptureBacking>();
  seed_kind_bridge(backing->bridge, registry);
  backing->codecs = arbc::builtin_codecs(registry);
  document.set_content_identity_capture(
      arbc::codec_identity_capture(backing->codecs, backing->bridge));
  return backing;
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

  // The document's ONE raster hash memo, minted before the branch so BOTH open paths hand
  // it back on `OpenedProject` (A23 / Constraint 1): a workspace-mapped open leaves it COLD
  // (nothing to seed it from, D-raster_tile_store-7), a rebuild-from-canonical open seeds it
  // with the blob names `load_document` just read. Empty-and-cold is correct, only
  // non-incremental — the first save of that session hashes every tile once, every save
  // after it is incremental.
  std::unique_ptr<arbc::RasterTileStore> tiles = std::make_unique<arbc::RasterTileStore>();

  // Fast, durable-by-default path (D-open-3 / A19 amendment): map the crash-durable workspace
  // when the file is present, opening it through the RECONSTRUCTING `arbc::open_document`
  // (arbc#19). The content-bearing-map guard is RETIRED — the premise it stood on
  // (`arbc::Document::open` takes no `Registry` and runs no factory, so the map binds no
  // `Content` for any kind) no longer holds at the v0.4.0 pin. `open_document` is registry-aware:
  // it maps the workspace, then walks each recovered `ContentRecord` and rebuilds it through the
  // construction identity the record now carries — captured at `add_content` through the kind's
  // registered codec — using the SAME routing `load_document` runs for a canonical file. So the
  // map path is durable-by-default for EVERY kind at once, and the canonical rebuild survives
  // ONLY as the fallback for a missing, unusable, or pre-v0.4.0 workspace (below).
  //
  // The transient reconstruction Registry mirrors the rebuild path (D-open-7): built-ins plus,
  // when set, the caller's editor kinds (`register_extra_kinds`) so an editor-authored kind
  // reconstructs as its live typed Content. The bridge is seeded from it (deterministic tokens),
  // and `builtin_codecs(registry, tiles.get())` binds the session's ONE raster memo into the
  // codec table by closure — so a reconstruct decode SEEDS the store the first save sweeps,
  // decoding tiles INLINE (a null `TileDecodeDispatch`, byte-identical, A23 (4)). The
  // `LoadContext` carries the caller's asset source so owned tiles resolve from `assets/`.
  const bool canonical_exists = fs.exists(layout.canonical);
  if (fs.exists(layout.workspace_file)) {
    arbc::Registry registry;
    arbc::register_builtin_kinds(registry);
    if (register_extra_kinds) {
      register_extra_kinds(registry);
    }
    arbc::KindBridge bridge;
    seed_kind_bridge(bridge, registry);
    const arbc::CodecTable codecs = arbc::builtin_codecs(registry, tiles.get());
    arbc::LoadContext ctx(layout.canonical.string());
    arbc::FilesystemAssetSource assets;
    ctx.set_asset_source(&assets);
    auto reopened =
        arbc::open_document(layout.workspace_file.string(), registry, codecs, ctx, bridge);
    if (reopened.has_value()) {
      OpenedProject opened;
      opened.document = std::move(reopened->document);
      // Seeded by the reconstruction decode above (not cold): the codec memo took the blob
      // names every reconstructed tile carries, so the session's first save is a memo sweep.
      opened.tiles = std::move(tiles);
      opened.layout = layout;
      opened.rebuilt_from_canonical = false;
      // Re-keyed from "the map could not bind" to "open_document could not reconstruct" — a
      // record with no captured identity (pre-v0.4.0), no registered codec (plugin absent), or a
      // failed codec is left UNBOUND and reported, never defaulted (Constraint 4). Common case: 0.
      opened.unbindable_content_records = reopened->unreconstructed.size();
      // Seed the cross-session dirty verdict (D28): clean iff the sidecar matches the
      // reconstructed revision, else dirty (the safe direction).
      opened.mapped_in_sync = mapped_reopen_in_sync(fs, layout, *opened.document);
      return opened;
    }
    // A returned WorkspaceFileError (truncated / another machine / pre-map / pre-v0.4.0 without a
    // mappable arena) is not an error — it falls through to rebuild-from-canonical below.
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
      rebuild_from_canonical(layout, *bytes, register_extra_kinds, tiles);
  if (!rebuilt.has_value()) {
    return rebuilt.error();
  }

  OpenedProject opened;
  opened.document = std::move(*rebuilt);
  // Seeded by the load above: the next save is a pure memo sweep, not a full re-hash.
  opened.tiles = std::move(tiles);
  opened.layout = layout;
  opened.rebuilt_from_canonical = true;
  return opened;
}

platform::Result<OpenedProject> create_project(const platform::FileSystem& fs,
                                               const std::filesystem::path& root) {
  const ProjectLayout layout = project_layout(root);

  // Creation means creation (D27 / D-dir_is_project-1): the project directory IS the project
  // (D16), so a target that exists AT ALL — empty, populated, another project, or a regular
  // file — is a request to ADOPT rather than to create, and adoption is `open_project`'s verb.
  // Refused as a value BEFORE the scaffold below, so nothing is written and the target is left
  // byte-for-byte as it was. "Empty is fine" is deliberately not an exception: it would leave
  // New and Save As accepting different targets and put us in the business of deciding what
  // "empty" means.
  if (fs.exists(root)) {
    return make_error_code(OpenError::TargetExists);
  }

  // Scaffold the canonical subdirectories (mkdir -p, idempotent). Creating each
  // leaf also materializes `root` (D16 layout, D-open-5).
  for (const std::filesystem::path& dir :
       {layout.assets_dir, layout.workspace_dir, layout.exports_dir}) {
    if (fs.make_directories(dir)) {
      // A mid-loop failure can leave `root` plus a partial tree; roll it back so the
      // D27 guard above does not refuse the retry forever after (A26).
      discard_partial_scaffold(fs, root);
      return make_error_code(OpenError::IoError);
    }
  }

  // Exclude the machine-local workspace scratch from VCS, atomically (D-open-5).
  if (fs.atomic_replace(layout.gitignore, k_gitignore_body)) {
    discard_partial_scaffold(fs, root);
    return make_error_code(OpenError::IoError);
  }

  // Mint a fresh workspace-backed document and make it durable by default via checkpoint
  // (D-open-4). No project.arbc is written — that is save's publish step.
  //
  // Both post-mint failures — a mint that never yielded a `Document`, and a mint whose first
  // `checkpoint` faulted — converge on the single rollback below. `document` is scoped to
  // THIS block on purpose (D-create_rollback-2, satisfied by Constraint 3's "equivalent
  // scoped destruction"): a checkpoint fault does NOT return from inside, so it falls out of
  // the block and the live, mmap-backed map over `workspace/` and its HousekeepingThread are
  // torn down at the closing brace — BEFORE `discard_partial_scaffold` removes the subtree,
  // never via an end-of-scope unwind after a `return`. The success path moves `document` onto
  // `OpenedProject` and returns from inside the block, so that teardown never runs there.
  platform::Result<std::unique_ptr<arbc::Document>> minted =
      create_workspace_document(layout.workspace_file);
  if (minted.has_value()) {
    std::unique_ptr<arbc::Document> document = std::move(*minted);
    if (document->checkpoint().has_value()) {
      OpenedProject opened;
      opened.document = std::move(document);
      // A fresh project has no tiles to seed, but the store is still minted here so
      // `OpenedProject::tiles` is never null on success (Constraint 1) and "one store per
      // `Document`" holds on BOTH bootstrap paths. Cold and empty is the right start: the
      // first save of a newly painted project hashes what the user actually made.
      opened.tiles = std::make_unique<arbc::RasterTileStore>();
      opened.layout = layout;
      opened.rebuilt_from_canonical = false;
      return opened;
    }
  } // a checkpoint fault destroys the live `document` HERE, ahead of the rollback below

  // Reached by a mint failure (`minted` held an error, no `Document` ever in scope) and by a
  // checkpoint failure (the block above already destroyed the live `Document`). Either way no
  // live mapping remains over `workspace/`, so the bare rollback is safe. The mint error
  // `create_workspace_document` returns IS `OpenError::IoError`, so both branches report the
  // same value they returned inline before.
  discard_partial_scaffold(fs, root);
  return make_error_code(OpenError::IoError);
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
