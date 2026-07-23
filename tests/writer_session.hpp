#pragma once

// The shipped bootstrap ORDER, packaged for a fixture (editor.canvas.writer_thread,
// D-writer_thread-1/6). Every e2e that stands up a live `CanvasView` over a real `AppState`
// needs the same three-step dance the shell's `run_editor` performs, in the same order, for the
// same reason:
//
//   1. Spawn the writer thread FIRST — before any document exists.
//   2. Build the session ON it. `create_project`'s first checkpoint (and `load_document` on the
//      open path) is the FIRST write, and the first write BINDS the document's writer identity
//      for its lifetime — `SlotStore::allocate` asserts that identity in debug builds. A fixture
//      that builds the document on the main thread and then posts its edits to a writer thread
//      is TWO identities: the exact bug this leaf removes, relocated into the test rig.
//   3. Stop the writer AFTER the canvas is gone and BEFORE the session is released. Declaring the
//      `CanvasView` *after* this object gets step 3 for free: the view (and the host whose entry /
//      DamageRouter teardown posts WRITER-THREAD-ONLY work) destructs first, then this
//      destructor drains-and-joins the writer, then it releases the document.
//
// Any document write a fixture performs outside a `CanvasView::apply_edit` — scene seeding, a
// direct `scene::add_camera` — must go through `on_writer` for the same reason.
#include <ace/commands/app_state.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/threads.hpp>
#include <ace/writer/writer_thread.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <utility>

namespace ace::testing {

class WriterSession {
public:
  explicit WriterSession(std::filesystem::path root) {
    writer_.submit_sync(
        [&] { session_.emplace(ace::commands::open_or_create_app_state(fs_, std::move(root))); });
  }

  ~WriterSession() {
    // Drain and join before the document dies (D-writer_thread-6): a queued save or checkpoint
    // must not be discarded at exit.
    writer_.stop();
    session_.reset();
  }

  WriterSession(const WriterSession&) = delete;
  WriterSession& operator=(const WriterSession&) = delete;

  bool ok() const { return session_.has_value() && session_->has_value(); }
  ace::commands::AppState& state() { return **session_; }
  ace::writer::WriterThread& writer() { return writer_; }
  const ace::platform::FileSystem& filesystem() const { return fs_; }

  // Run fixture-side document work on the writer thread — seeding IS a write.
  void on_writer(const std::function<void()>& work) { writer_.submit_sync(work); }

private:
  ace::platform::NativeFileSystem fs_;
  ace::platform::NativeThreads threads_;
  ace::writer::WriterThread writer_{threads_};
  std::optional<ace::platform::Result<ace::commands::AppState>> session_;
};

} // namespace ace::testing
