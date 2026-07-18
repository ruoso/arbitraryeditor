// editor.project.exec_new — child-side boot e2e (Acceptance "UI e2e"). exec_new
// adds no widget (the New/Open affordance is editor.project.open_ui), so there is
// no button to drive. What it adds — "a parsed project-dir argument boots into
// THAT project" — is pinned here: feed parse_args({exe, scratch}) into run_editor
// headless and assert, through the on_ready seam, that the resulting AppState
// opened exactly that directory. The single-Document-per-process invariant stays
// pinned by tests/app_state_e2e_test.cpp.

#include <ace/app/args.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <system_error>

namespace {

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_exec_new_e2e_test") {
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

TEST_CASE("exec_new e2e: a parsed project-dir argument boots into that project") {
  ScratchDir scratch;
  const std::filesystem::path proj = scratch.root / "proj"; // does not exist yet -> created
  const std::string proj_str = proj.string();
  const char* argv[] = {"arbitraryeditor", proj_str.c_str()};

  auto parsed = ace::app::parse_args(2, argv);
  REQUIRE(parsed.ok);
  CHECK(parsed.options.project_dir == proj);
  // Drive the parsed options through the real boot, headless (SDL offscreen +
  // software GL), for a bounded frame count.
  parsed.options.headless = true;
  parsed.options.max_frames = 3;
  parsed.options.width = 320;
  parsed.options.height = 240;

  int sessions_seen = 0;
  bool opened_target = false;
  const auto on_ready = [&](ace::commands::AppState& state) {
    ++sessions_seen; // fired exactly once -> exactly one session for the lifetime
    opened_target = state.layout().canonical == (proj / "project.arbc");
  };

  const int rc = ace::app::run_editor(parsed.options, on_ready);

  CHECK(rc == 0);
  CHECK(sessions_seen == 1);
  CHECK(opened_target); // the argv-supplied directory is the one the process owns
}
