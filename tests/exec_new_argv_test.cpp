// editor.project.exec_new — the child-side argv contract (Acceptance "Argv
// contract"). parse_args is a pure L4 helper (the app_loop_test precedent), so it
// unit-tests headless with no SDL/GL; run_editor_argv's malformed-invocation
// branch is exercised here too (it returns before ever touching run_editor).

#include <ace/app/args.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using ace::app::parse_args;
using ace::app::run_editor_argv;

TEST_CASE("parse_args: no positional arg leaves project_dir empty (scratch)") {
  const char* argv[] = {"arbitraryeditor"};
  const auto parsed = parse_args(1, argv);
  CHECK(parsed.ok);
  CHECK(parsed.options.project_dir.empty()); // empty => bootstrap creates a scratch project
}

TEST_CASE("parse_args: exactly one positional arg becomes the project_dir") {
  const char* argv[] = {"arbitraryeditor", "/tmp/some/proj"};
  const auto parsed = parse_args(2, argv);
  CHECK(parsed.ok);
  CHECK(parsed.options.project_dir == std::filesystem::path("/tmp/some/proj"));
}

TEST_CASE("parse_args: more than one positional is a signalled usage error") {
  const char* argv[] = {"arbitraryeditor", "a", "b"};
  const auto parsed = parse_args(3, argv);
  CHECK_FALSE(parsed.ok);
}

TEST_CASE("run_editor_argv: a malformed invocation returns non-zero without launching") {
  const char* argv[] = {"arbitraryeditor", "a", "b"};
  const int rc = run_editor_argv(3, argv);
  CHECK(rc != 0); // usage printed to stderr, run_editor never reached
}
