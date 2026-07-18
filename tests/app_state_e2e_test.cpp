// editor.project.app_state — boot-lifecycle e2e (docs §9). Drives run_editor
// headless (SDL offscreen + software GL) for a bounded frame count against a
// project path under a ScratchDir, and asserts through the test-visible on_ready
// seam that the process comes up owning EXACTLY ONE workspace-backed AppState /
// Document for its lifetime (the A7 single-Document invariant end-to-end) and
// tears it down cleanly on shutdown (the HousekeepingThread joined — the ASan/TSan
// scope). No user-driven widget lands here: the canvas that DISPLAYS the Document
// is editor.canvas.view. This pins the ownership/bootstrap seam.

#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>

#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <system_error>

using ace::app::run_editor;
using ace::app::ShellOptions;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_app_state_e2e_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

} // namespace

TEST_CASE("app_state e2e: run_editor boots owning exactly one workspace Document") {
  ScratchDir scratch;
  ShellOptions opts;
  opts.headless = true;
  opts.max_frames = 5;
  opts.width = 320;
  opts.height = 240;
  opts.project_dir = scratch.root / "proj"; // does not exist yet -> created

  int sessions_seen = 0;
  bool workspace_backed = false;
  bool canonical_named = false;

  const auto on_ready = [&](ace::commands::AppState& state) {
    ++sessions_seen; // fired exactly once -> exactly one session for the lifetime
    workspace_backed = state.document().workspace_backed();
    canonical_named = state.layout().canonical == (scratch.root / "proj" / "project.arbc");
    // boot -> dispatch a command -> checkpoint -> shutdown: the sanitizer-scoped
    // lifecycle, exercising a writer-thread commit against the background
    // checkpointer over the session's life.
    const auto out = ace::commands::dispatch(
        state, ace::commands::Command{"add_composition", [](arbc::Document& doc) {
                                        doc.add_composition(64.0, 64.0);
                                      }});
    CHECK(out.journal_entries_added == 1);
    CHECK(state.document().checkpoint().has_value());
  };

  const int rc = run_editor(opts, on_ready);

  CHECK(rc == 0);
  CHECK(sessions_seen == 1);
  CHECK(workspace_backed);
  CHECK(canonical_named);
}
