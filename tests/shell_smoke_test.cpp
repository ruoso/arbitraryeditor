#include <ace/app/shell.hpp>

#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <system_error>

// Offscreen-GL headless smoke (docs §9): "init + N frames + shutdown" against
// the SDL offscreen video driver + software GL. No GPU/display required — the
// ctest ENVIRONMENT pins SDL_VIDEODRIVER=offscreen + llvmpipe. This is the
// per-platform bring-up check every later leaf inherits.
using ace::app::run_editor;
using ace::app::Shell;
using ace::app::ShellOptions;

TEST_CASE("shell smoke: run_editor init + N frames + shutdown headless") {
  ShellOptions opts;
  opts.headless = true;
  opts.max_frames = 5;
  opts.width = 320;
  opts.height = 240;
  REQUIRE(run_editor(opts) == 0);
}

TEST_CASE("shell smoke: run_editor exits 2 when the project dir cannot be opened") {
  // A project_dir that resolves to a regular file (not a directory) makes the
  // session bootstrap fail with OpenError::NotADirectory. init() still succeeds
  // headless, so run_editor takes the open-failure branch: it logs, tears the
  // partial shell down cleanly, and returns 2 — never a throw across the app
  // boundary (D-app_state-6 / Constraint 6).
  const std::filesystem::path not_a_dir =
      std::filesystem::temp_directory_path() / "ace_shell_smoke_not_a_dir";
  {
    std::ofstream(not_a_dir) << "not a project directory";
  }
  ShellOptions opts;
  opts.headless = true;
  opts.max_frames = 1;
  opts.width = 320;
  opts.height = 240;
  opts.project_dir = not_a_dir;
  CHECK(run_editor(opts) == 2);
  std::error_code ec;
  std::filesystem::remove(not_a_dir, ec);
}

TEST_CASE("shell smoke: piecewise lifecycle honours the frame cap") {
  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.max_frames = 3;
  opts.width = 256;
  opts.height = 256;
  REQUIRE(shell.init(opts));
  while (ace::app::should_continue_loop(shell.frames_rendered(), opts.max_frames,
                                        shell.quit_requested())) {
    shell.new_frame();
    shell.draw_ui();
    shell.render();
  }
  CHECK(shell.frames_rendered() == 3);
  CHECK_FALSE(shell.quit_requested());
  shell.shutdown();
}

TEST_CASE("shell smoke: the windowed path also runs under the offscreen driver") {
  // headless=false exercises the windowed-only branch (show window + vsync
  // swap-interval); the offscreen env keeps it displayless in CI.
  SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
  ShellOptions opts;
  opts.headless = false;
  opts.max_frames = 2;
  opts.width = 320;
  opts.height = 240;
  REQUIRE(run_editor(opts) == 0);
}

TEST_CASE("shell smoke: init fails cleanly on an unknown video driver") {
  // Force SDL to reject the driver so the failure path (and clean teardown of
  // partial state) is exercised. OVERRIDE beats the offscreen env var.
  SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "ace_no_such_driver", SDL_HINT_OVERRIDE);
  ShellOptions opts;
  opts.headless = false; // don't let init() re-hint the driver
  opts.max_frames = 1;
  CHECK(run_editor(opts) == 1);
  SDL_ResetHint(SDL_HINT_VIDEO_DRIVER); // restore the env-driven default
}
