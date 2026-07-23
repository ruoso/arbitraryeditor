#include <ace/project/save.hpp>

#include <arbc/runtime/document_serialize.hpp>
#include <arbc/serialize/codec.hpp> // arbc::CodecTable (complete type for builtin_codecs)
#include <arbc/serialize/save_context.hpp>
#include <arbc/serialize/writer.hpp> // arbc::SerializeError

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

// editor.project.save — publish the canonical `project.arbc` + owned `assets/` from
// the live workspace `Document` (D-save-1..5). The mirror of `project_open.cpp`'s
// load path: a transient `KindBridge` symmetric with the load bridge, a
// `builtin_codecs` table off the persistent registry, and a content-addressed sink
// beneath `assets/`. Errors are values (Constraint 7); no editor-owned thread —
// capture runs synchronously in the writer context (A4).

namespace ace::project {
namespace {

// `SaveError` gets a `std::error_code` category so it can ride the error channel of
// `platform::Result<T>` (Constraint 7, mirroring `OpenErrorCategory`).
class SaveErrorCategory final : public std::error_category {
public:
  const char* name() const noexcept override { return "ace.project.save"; }

  std::string message(int value) const override {
    switch (static_cast<SaveError>(value)) {
    case SaveError::SerializeFailed:
      return "document serialization failed";
    case SaveError::AssetWriteFailed:
      return "owned asset write failed";
    case SaveError::IoError:
      return "project I/O error";
    }
    return "unknown project save error";
  }
};

const std::error_category& save_error_category() noexcept {
  static const SaveErrorCategory category;
  return category;
}

// Strip a `file://` scheme prefix, mirroring the load path's
// `arbc::FilesystemAssetSource`: a resolved URI is otherwise taken as a plain
// filesystem path (scheme dispatch is a stubbed extension point, doc 08).
std::string uri_to_path(std::string_view resolved_uri) {
  constexpr std::string_view k_file_scheme = "file://";
  if (resolved_uri.substr(0, k_file_scheme.size()) == k_file_scheme) {
    resolved_uri.remove_prefix(k_file_scheme.size());
  }
  return std::string(resolved_uri);
}

} // namespace

std::error_code make_error_code(SaveError error) noexcept {
  return {static_cast<int>(error), save_error_category()};
}

bool FilesystemAssetSink::contains(std::string_view resolved_uri) const {
  return fs_.exists(uri_to_path(resolved_uri));
}

arbc::expected<bool, arbc::AssetSinkError>
FilesystemAssetSink::put(std::string_view resolved_uri, std::span<const std::byte> bytes) {
  // Write-if-absent: content-addressing makes presence-by-name sufficient proof the
  // bytes are right, so an already-present blob is left completely alone — that is
  // what makes an incremental save incremental (the "+ changed owned tiles" case).
  if (contains(resolved_uri)) {
    return false;
  }
  const std::filesystem::path path = uri_to_path(resolved_uri);
  // The content-addressed fan-out means a save is routinely the first thing to touch
  // `assets/<xx>/`; materialize the parent before publishing the blob.
  if (const std::filesystem::path parent = path.parent_path();
      !parent.empty() && fs_.make_directories(parent)) {
    return arbc::unexpected<arbc::AssetSinkError>({arbc::AssetSinkError::Kind::WriteFailed});
  }
  // Crash-atomic (temp sibling + rename): a truncated blob left under a valid hash
  // name would poison every future write-if-absent check and silently lose pixels.
  const std::string_view view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  if (fs_.atomic_replace(path, view)) {
    return arbc::unexpected<arbc::AssetSinkError>({arbc::AssetSinkError::Kind::WriteFailed});
  }
  ++blobs_written_;
  return true;
}

platform::Result<SaveOutcome> save_project(const platform::FileSystem& fs,
                                           const ProjectLayout& layout, const arbc::Document& doc,
                                           const arbc::Registry& registry,
                                           const WriterPost& post_writer) {
  // Ensure `assets/` (and, by extension, the root) exists before any blob or the
  // canonical publish. Save is a publish step; `make_directories` is idempotent
  // mkdir -p. The workspace and its checkpoints are deliberately untouched (D16/§9).
  if (fs.make_directories(layout.assets_dir)) {
    return make_error_code(SaveError::IoError);
  }

  // A transient `KindBridge`, symmetric with the load path (D-save-3): the built-in
  // registry makes the token<->kind mapping deterministic, so a fresh bridge
  // resolves the document's tokens identically to the load that reads it back.
  // `KindBridge()` pre-interns only the built-in leaf kinds, so seed it from the
  // registry too — interning every registered kind id in `ids()` order gives a
  // custom editor kind (`org.arbc.camera`, editor.cameras.model A14) a token this
  // fresh bridge resolves, matching the token the author stored via the SAME
  // registry-seeded ordering. Built-in ids intern idempotently (their pre-interned
  // tokens are unchanged), so this is byte-identical for a solid/probe document.
  arbc::KindBridge bridge;
  seed_kind_bridge(bridge, registry);
  const arbc::CodecTable codecs = arbc::builtin_codecs(registry);

  // Owned bytes route to `assets/` through the content-addressed sink; the base URI
  // is the canonical path so the library resolves each asset reference beside it
  // (`<root>/project.arbc` + `assets/…` -> `<root>/assets/…`).
  FilesystemAssetSink sink(fs);
  arbc::SaveContext ctx(layout.canonical.string());
  ctx.set_asset_sink(&sink);

  // POST the capture, serialize off-thread (D-writer_thread-7). `capture_snapshot` is a READ,
  // but it copies the writer-owned content side-map and unknown-field stash, so it belongs on the
  // writer thread; everything past it runs over an immutable, off-thread-safe snapshot, which is
  // exactly the split `document_serialize.hpp` designed. With no `post_writer` the caller is
  // already the writer identity (the headless path) and it captures inline.
  arbc::ContentSnapshot snapshot;
  const std::function<void()> capture = [&] { snapshot = arbc::capture_snapshot(doc, bridge); };
  if (post_writer) {
    post_writer(capture);
  } else {
    capture();
  }
  if (!snapshot.state) {
    // The post was refused (the writer is stopping) — nothing was captured, so publish nothing
    // rather than dumping an empty document over a live `project.arbc`.
    return make_error_code(SaveError::SerializeFailed);
  }
  const std::uint64_t revision = snapshot.state->revision();
  const arbc::expected<std::string, arbc::SerializeError> bytes =
      arbc::serialize_snapshot(snapshot, codecs, ctx);
  if (!bytes.has_value()) {
    switch (bytes.error().kind) {
    case arbc::SerializeError::Kind::AssetSinkMissing:
    case arbc::SerializeError::Kind::AssetWriteFailed:
      return make_error_code(SaveError::AssetWriteFailed);
    default:
      return make_error_code(SaveError::SerializeFailed);
    }
  }

  // Atomic publish (D16 / Constraint 3): a crash or concurrent reader sees either
  // the whole old `project.arbc` or the whole new one, never a truncated snapshot.
  if (fs.atomic_replace(layout.canonical, *bytes)) {
    return make_error_code(SaveError::IoError);
  }

  SaveOutcome outcome;
  outcome.revision = revision;
  outcome.assets_written = sink.blobs_written();
  return outcome;
}

platform::Result<SaveOutcome> save_project_as(const platform::FileSystem& fs,
                                              const std::filesystem::path& target_root,
                                              const arbc::Document& doc,
                                              const arbc::Registry& registry,
                                              const WriterPost& post_writer) {
  const ProjectLayout layout = project_layout(target_root);

  // Refuse to clobber an existing project (D-save_as-4): silently `atomic_replace`-ing
  // a *different* project's canonical is the one Save-As error that destroys unrelated
  // work. An existing `project.arbc` under the target is a data-loss footgun — return
  // `file_exists` (a value, house style) and leave the target untouched. A populated
  // directory WITHOUT a `project.arbc` is a fine destination, so the guard is narrow.
  if (fs.exists(layout.canonical)) {
    return std::make_error_code(std::errc::file_exists);
  }

  // The "copy" is a re-publish of the LIVE document into the new root (D-save_as-1),
  // the exact mechanism plain Save already trusts: reuse `save_project` against the
  // target layout so `project.arbc` + the content-addressed `assets/` land under
  // `target_root`. Captures the current in-memory state; the source project is never
  // touched. `save_project` `make_directories`-es `assets/` (and thus the root).
  platform::Result<SaveOutcome> published = save_project(fs, layout, doc, registry, post_writer);
  if (!published.has_value()) {
    return published.error();
  }

  // Exclude the machine-local `workspace/` scratch from VCS (D16/D-open-5), matching
  // `create_project`'s scaffold so the copy is a first-class portable project. No
  // `workspace/` is created here — the exec'd sibling rebuilds it from the canonical.
  if (fs.atomic_replace(layout.gitignore, k_gitignore_body)) {
    return make_error_code(SaveError::IoError);
  }

  return published;
}

} // namespace ace::project
