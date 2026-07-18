#pragma once

#include <ace/commands/selection.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/result.hpp>
#include <ace/project/project.hpp>

#include <arbc/contract/registry.hpp>
#include <arbc/runtime/document.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace ace::commands {

// The process's ONE owned project session (docs/00-design.md D19, arch A7,
// refinement Decisions D-app_state-1/2). The app owns exactly one `Document` for
// its whole lifetime (one process = one project; the GC root-set is trivially
// that document), and this is the object that holds it. It takes ownership of the
// `OpenedProject` `editor.project.open` hands back — moving the `Document` in and
// keeping the `ProjectLayout` (so `save` can find `project.arbc`) — seeds a
// persistent kind `Registry` at construction (the D-open-7 deferral this leaf
// collects, reused by `save`/export and the future A6 plugin seam), and carries
// the project-level `Selection`. Move-only, one per process (A7): the single
// owner. The `Document`'s live `HousekeepingThread` (A4) therefore spans the whole
// run and is joined by the `Document` destructor at teardown. Headless — no
// ImGui/GL/SDL (A8); the L4 `app` holds an `AppState` for the process lifetime.
class AppState {
public:
  explicit AppState(project::OpenedProject opened);

  AppState(AppState&&) = default;
  AppState& operator=(AppState&&) = default;
  AppState(const AppState&) = delete;
  AppState& operator=(const AppState&) = delete;

  arbc::Document& document() { return *document_; }
  const arbc::Document& document() const { return *document_; }

  const project::ProjectLayout& layout() const { return layout_; }

  arbc::Registry& registry() { return registry_; }
  const arbc::Registry& registry() const { return registry_; }

  Selection& selection() { return selection_; }
  const Selection& selection() const { return selection_; }

  // Whether `open_project` rebuilt this session from the canonical `project.arbc`
  // rather than mapping the crash-durable workspace (always false for a fresh
  // `create_project`).
  bool rebuilt_from_canonical() const { return rebuilt_from_canonical_; }

private:
  std::unique_ptr<arbc::Document> document_;
  project::ProjectLayout layout_;
  arbc::Registry registry_;
  Selection selection_;
  bool rebuilt_from_canonical_ = false;
};

// A dispatchable editor action (refinement Decision D-app_state-5). This leaf
// ships the SEAM, not the concrete scene-edit taxonomy — those `Command`s land
// with their edit leaves (`editor.cells.*`, `editor.camera.*`). `apply` runs on
// the writer `Document` and, when well-formed, performs exactly one libarbc
// transaction via the `Document` transactional wrappers (`add_composition` /
// `add_content` / `add_layer` / `transact`), i.e. one journal entry, one revision
// bump. `name` labels the action (undo will surface it later).
struct Command {
  std::string name;
  std::function<void(arbc::Document&)> apply;
};

// The journal effect of one dispatched command — the one-command-one-entry
// boundary made observable (the invariant this leaf pins; `editor.project.undo`
// coalesces gestures, `editor.canvas.frame_sync` moves the submit off-thread).
struct DispatchOutcome {
  std::size_t journal_entries_added = 0; // journal().depth() delta (1 for a well-formed edit)
  std::uint64_t revision = 0;            // the document revision after applying
};

// Apply `command` on `state`'s writer `Document` (synchronous, single-writer,
// A4). Returns the journal delta + resulting revision. A command with no `apply`
// (or a no-op body) adds no entry.
DispatchOutcome dispatch(AppState& state, const Command& command);

// Resolve a project directory into an `AppState` (refinement Decision
// D-app_state-6): an existing path opens (`project::open_project`), a
// not-yet-existing one is created (`project::create_project`). Errors are values —
// a non-directory (`OpenError::NotADirectory`), an empty directory with no
// project (`NoProject`), a corrupt/faulted open — all ride `platform::Result`'s
// error channel, never a throw. The app-layer bootstrap calls this with a
// `NativeFileSystem` and holds the result for the process lifetime.
platform::Result<AppState> open_or_create_app_state(const platform::FileSystem& fs,
                                                    const std::filesystem::path& root);

} // namespace ace::commands
