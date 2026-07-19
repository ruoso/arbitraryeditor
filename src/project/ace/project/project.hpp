#pragma once

#include <ace/platform/filesystem.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp> // arbc::KindBridge

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace ace::project {

// The project component. See docs/01-architecture.md §8 (component levelization).
const char* name();

// The render_probe frame geometry — the single source of truth the render
// offline path, the golden (tests/goldens/render_probe_<W>x<H>.rgba8) and the
// app-layer texture all size against.
inline constexpr int k_probe_width = 64;
inline constexpr int k_probe_height = 64;

// The probe's full-frame solid fill: opaque, PREMULTIPLIED linear-light working
// colour (doc 07). Chosen distinct from the shell clear colour (0.10,0.10,0.12,
// src/app/shell.cpp) so the e2e can tell the rendered texture from the
// background (refinement Constraint 6 / D-render_probe-7).
inline constexpr arbc::Rgba k_probe_color{0.0F, 0.5F, 0.0F, 1.0F};

// The trivial probe document: one full-frame solid-colour cell in a composition.
// Owns the anonymous in-process libarbc Document (A1: real objects, no FFI); the
// ids are handed back so render/tests can introspect the built graph.
struct ProbeDocument {
  std::unique_ptr<arbc::Document> document;
  arbc::ObjectId composition;
  arbc::ObjectId layer;
  arbc::ObjectId content;
};

// Build the trivial probe document (GL-free, L1): add a composition, an unbounded
// solid content of k_probe_color, a layer at the identity transform, and attach
// the layer. The offline render (ace::render) drives this straight into a frame.
ProbeDocument build_probe_document();

// The root composition's authored canvas bounds, origin-anchored at [0,0]
// (editor.canvas.fit_bounds). A composition carries only `canvas_w`/`canvas_h`
// (no origin), so the authored region is [0,0]→[width,height] in composition units.
struct CompositionSize {
  double width = 0.0;
  double height = 0.0;
};

// The reader mirror of `Document::add_composition` (D-fit_bounds-2): pin a version
// and return the root (lowest-id) composition's authored `canvas_w`/`canvas_h` — the
// same root the compositor anchors on (`find_first_composition`, the v0.1 root rule).
// Returns `std::nullopt` when the document has no composition or the authored size is
// degenerate (`canvas_w`/`canvas_h` not `> 0`), so "nothing to fit" is a first-class,
// testable case (D-fit_bounds-3). Lives in L1 `project` beside the writer; the app
// layer feeds the result to `interact::fit` on reset-to-fit. A lock-free `pin()` read
// (A4) — legal on the UI (writer) thread, touches no render-thread cache.
std::optional<CompositionSize> root_composition_size(const arbc::Document& document);

// --- project directory open / create (editor.project.open) -----------------

// The canonical on-disk bundle layout (D16, docs/00-design.md §9): a project is a
// directory holding `project.arbc` (portable snapshot) + `assets/` (owned bytes) +
// `workspace/` (live mmap arena + checkpoints) [+ `exports/`]. The editor defines
// the layout and points the library's workspace path into `workspace/`; the
// workspace filename is editor-chosen (D-open-2). Pure path arithmetic — no I/O.
struct ProjectLayout {
  std::filesystem::path root;
  std::filesystem::path canonical;      // <root>/project.arbc (the dump target — save's)
  std::filesystem::path assets_dir;     // <root>/assets
  std::filesystem::path workspace_dir;  // <root>/workspace
  std::filesystem::path workspace_file; // <root>/workspace/document.arbcws (D-open-2)
  std::filesystem::path exports_dir;    // <root>/exports
  std::filesystem::path gitignore;      // <root>/.gitignore
};

// Resolve the canonical bundle paths for a project rooted at `root` (no I/O).
ProjectLayout project_layout(const std::filesystem::path& root);

// Seed `bridge` so it resolves EVERY registered kind's `ContentRecord.kind` token,
// not just the built-in leaf kinds `arbc::KindBridge()` pre-interns
// (editor.cameras.model A14). Interns each id in `registry.ids()` order with its
// metadata version; built-in ids intern idempotently (their pre-interned tokens are
// unchanged, so a solid/probe document is byte-identical). Shared by
// `save_project`'s writer-thread capture and by a custom-kind author
// (`scene::add_camera`) so the token stored on a fresh content matches the token the
// save-side bridge — seeded from the SAME registry, in the SAME order — resolves.
void seed_kind_bridge(arbc::KindBridge& bridge, const arbc::Registry& registry);

// The `.gitignore` body a project scaffold writes at its root: the machine-local
// `workspace/` scratch is rebuilt from the canonical core and excluded from
// sharing/VCS (D16, D-open-5). Written by `create_project` and by Save As's
// publish-copy (`save_project_as`), so it lives on the shared `project` header.
inline constexpr std::string_view k_gitignore_body = "workspace/\n";

// Errors are values, never throws (D-open-6): a caller (app_state) branches on the
// value. A missing/corrupt nested or borrowed asset does NOT surface here — the
// library's `load_document` loads a doc-05 placeholder and never makes a project
// unopenable (D13/relink).
enum class OpenError {
  NotADirectory = 1, // `root` is not an existing directory
  NoProject,         // neither a usable workspace nor a `project.arbc` is present
  CorruptDocument,   // a `project.arbc` is present but failed to parse
  IoError,           // a filesystem fault, or a workspace file this build cannot mint
};

// Bridge `OpenError` into `std::error_code` so it rides `platform::Result<T>`'s
// error channel (its error alternative is a `std::error_code`).
std::error_code make_error_code(OpenError error) noexcept;

// A live workspace-backed `Document` plus the paths it resolved from. This leaf
// produces the document; lifetime ownership (one per process, A7) is
// `editor.project.app_state`'s, not this leaf's.
struct OpenedProject {
  std::unique_ptr<arbc::Document> document;
  ProjectLayout layout;
  // True when `open_project` rebuilt from the canonical `project.arbc` (a fresh
  // clone, another machine, or an unusable workspace) rather than mapping the
  // crash-durable workspace. Always false for `create_project`.
  bool rebuilt_from_canonical = false;
};

// Open a project directory into a live `Document` — the LOAD direction only
// (D-open-3). Maps the crash-durable `workspace/` if usable; on a missing or
// unusable workspace file, rebuilds from the canonical `project.arbc`
// (create a fresh workspace, `load_document` the canonical bytes, checkpoint).
// Directory enumeration and reading `project.arbc` go through `fs`; the workspace
// file and the document go through libarbc (D-platform_services-4).
//
// `register_extra_kinds` is the extra-kinds registration hook (D-reopen-1,
// editor.cameras.reopen_codec): an optional callback applied to the TRANSIENT
// rebuild-from-canonical registry right after `arbc::register_builtin_kinds`, so
// the load path recognizes an editor-authored `Content` kind (e.g. `org.arbc.camera`)
// and reconstructs it as its live typed Content instead of degrading it to
// `arbc::PlaceholderContent`. It is typed ONLY on `arbc::Registry` — `project` stays
// ignorant of WHICH kind it registers (no `project->scene` edge, Constraint 1); the
// concrete registrar is named by the caller at a level that already sees `scene`
// (`commands`). Absent by default (an empty `std::function`, skipped when unset), so
// current callers and the workspace-map fast path — which runs no codec — are
// unaffected (Constraint 3/6).
platform::Result<OpenedProject>
open_project(const platform::FileSystem& fs, const std::filesystem::path& root,
             const std::function<void(arbc::Registry&)>& register_extra_kinds = {});

// Scaffold a new project directory (`assets/`, `workspace/`, `exports/`, and a
// `workspace/`-excluding `.gitignore`) and mint a fresh workspace-backed
// `Document`, durable by default via checkpoint (D-open-4/5). Does NOT write
// `project.arbc` — the canonical dump is `editor.project.save`'s publish step.
platform::Result<OpenedProject> create_project(const platform::FileSystem& fs,
                                               const std::filesystem::path& root);

// --- open_ui validate / compose helpers (editor.project.open_ui) -----------

// True when `root` is an existing directory that already holds a project (D16):
// either the live workspace file or the canonical `project.arbc` is present under
// an enumerable directory (the same recognition `open_project` applies inline).
// A pure existence check via `project_layout` — it opens NO `Document` (A7), so
// it is safe to pre-validate an Open target before spawning a sibling editor on
// it and doubles as the recent-list pruner (D-open_ui-5). Never throws.
bool is_project_directory(const platform::FileSystem& fs, const std::filesystem::path& root);

// Compose the target directory for a New project from a chosen parent location and
// a project `name` (D-open_ui-4). Pure path arithmetic — NO I/O, opens no
// `Document`. Returns `nullopt` for an empty parent, an empty/blank name, or a
// name that is not a single path component (contains a `/` or `\\` separator, or
// is `.` / `..`) — so no name can escape `parent` by traversal (Constraint 2).
// The composed path is deliberately NOT created here: the sibling editor's
// create-vs-open bootstrap scaffolds the not-yet-existing directory.
std::optional<std::filesystem::path> compose_new_project_target(const std::filesystem::path& parent,
                                                                std::string_view name);

} // namespace ace::project

// `OpenError` participates in the `std::error_code` machinery (ADL finds
// `ace::project::make_error_code`), so `result.error() == OpenError::NoProject`
// works and the enum rides `platform::Result`'s error channel unchanged.
template <> struct std::is_error_code_enum<ace::project::OpenError> : std::true_type {};
