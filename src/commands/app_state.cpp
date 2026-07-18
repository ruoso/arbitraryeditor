#include <ace/commands/app_state.hpp>
#include <ace/commands/exec_new.hpp>

#include <arbc/builtin_kinds.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/model/model.hpp>

#include <filesystem>
#include <system_error>
#include <utility>

namespace ace::commands {

AppState::AppState(project::OpenedProject opened)
    : document_(std::move(opened.document)), layout_(std::move(opened.layout)),
      rebuilt_from_canonical_(opened.rebuilt_from_canonical) {
  // The persistent, lifetime-scoped kind Registry (D-open-7): seeded once here,
  // not rebuilt per open. `save`/export and the future A6 plugin seam reuse it.
  arbc::register_builtin_kinds(registry_);
  // The dirty baseline (D-save-4): a session rebuilt from the canonical `project.arbc`
  // starts CLEAN (the workspace was just built from the snapshot); a fresh
  // `create_project` or a workspace-mapped open starts DIRTY (`saved_revision_`
  // stays `nullopt` — no known-published snapshot this session).
  if (rebuilt_from_canonical_) {
    saved_revision_ = document_->pin()->revision();
  }
}

DispatchOutcome dispatch(AppState& state, const Command& command) {
  arbc::Document& doc = state.document();
  const std::size_t before = doc.journal().depth();
  if (command.apply) {
    command.apply(doc); // writer-thread, synchronous (A4); the wrappers self-commit
  }
  DispatchOutcome outcome;
  outcome.journal_entries_added = doc.journal().depth() - before;
  outcome.revision = doc.pin()->revision();
  return outcome;
}

UndoOutcome undo(AppState& state) {
  // Undo IS the library journal (D15 / Constraint 1): drive the cursor and report
  // whether it moved plus the resulting revision / can-undo / can-redo state. No
  // editor-side inverse list, no snapshot — `journal().undo()` republishes the
  // touched objects at their *before* edge as a forward publish. Writer-thread (A4);
  // `mark_saved` is deliberately NOT called (D-undo-4 / Constraint 4).
  arbc::Document& doc = state.document();
  const bool moved = doc.journal().undo();
  return UndoOutcome{moved, doc.pin()->revision(), doc.journal().can_undo(),
                     doc.journal().can_redo()};
}

UndoOutcome redo(AppState& state) {
  arbc::Document& doc = state.document();
  const bool moved = doc.journal().redo();
  return UndoOutcome{moved, doc.pin()->revision(), doc.journal().can_undo(),
                     doc.journal().can_redo()};
}

platform::Result<AppState> open_or_create_app_state(const platform::FileSystem& fs,
                                                    const std::filesystem::path& root) {
  if (fs.exists(root)) {
    auto opened = project::open_project(fs, root);
    if (!opened) {
      return opened.error();
    }
    return AppState(std::move(*opened));
  }
  auto created = project::create_project(fs, root);
  if (!created) {
    return created.error();
  }
  return AppState(std::move(*created));
}

platform::Result<project::SaveOutcome> save_project(AppState& state,
                                                    const platform::FileSystem& fs) {
  // The dump lives in `project` (D-save-1); `commands` only wires the session into
  // it and updates the dirty baseline. Capture runs on this (writer) thread (A4).
  platform::Result<project::SaveOutcome> published =
      project::save_project(fs, state.layout(), state.document(), state.registry());
  if (!published.has_value()) {
    return published.error(); // a failed publish leaves the session dirty
  }
  state.mark_saved(published.value().revision);
  return *published;
}

platform::Result<project::SaveOutcome> save_project_as(AppState& state,
                                                       const platform::FileSystem& fs,
                                                       const platform::ProcessLauncher& launcher,
                                                       const std::filesystem::path& executable,
                                                       const std::filesystem::path& target_root) {
  // Reject an empty target before any I/O or exec (Constraint 5, mirroring
  // `open_another_project`): an empty path would publish into the CWD and spawn a
  // mystery sibling. The launcher is never touched and nothing is written.
  if (target_root.empty()) {
    return std::make_error_code(std::errc::invalid_argument);
  }

  // Canonicalize ONCE to an absolute path so the publish and the sibling `exec`
  // agree on the destination regardless of the child's CWD (Constraint 5). `absolute`
  // roots it lexically at the CWD; `weakly_canonical` then normalizes. Neither throws.
  std::error_code ec;
  std::filesystem::path resolved = std::filesystem::absolute(target_root, ec);
  if (!ec) {
    resolved = std::filesystem::weakly_canonical(resolved, ec);
  }
  if (ec) {
    return ec;
  }

  // Publish a COPY of the LIVE document into the target (D-save_as-1). Capture runs
  // on this (writer) thread (A4). The current session is deliberately left untouched
  // — no `mark_saved`, no `layout_` rebind (D-save_as-2 / Constraint 4).
  platform::Result<project::SaveOutcome> published =
      project::save_project_as(fs, resolved, state.document(), state.registry());
  if (!published.has_value()) {
    return published.error(); // a failed publish execs nothing (Constraint 7)
  }

  // Open the copy in a detached sibling editor on the same absolute path. A failed
  // exec leaves the (successfully published) copy on disk and returns the error.
  if (const std::error_code exec_ec = open_another_project(launcher, executable, resolved)) {
    return exec_ec;
  }

  return published;
}

platform::Result<project::GcOutcome> gc_project(AppState& state, bool dry_run) {
  // The sweep lives in `project` (D-gc-1); `commands` only selects this session's
  // layout and drives it. NOT a transaction (Constraint 3): no revision bump, no
  // journal entry, no `mark_saved` — the dirty baseline and revision are untouched.
  // Roots on the on-disk canonical (D-gc-2), so it needs no live-document
  // serialization and no `FileSystem` seam.
  return project::gc_project(state.layout(), dry_run);
}

} // namespace ace::commands
