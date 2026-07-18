#include <ace/app/args.hpp>

#include <cstdio>

namespace ace::app {

ParsedArgs parse_args(int argc, const char* const* argv) {
  ParsedArgs parsed;
  // argv[0] is the program name; project-dir positionals start at argv[1].
  const int positionals = argc - 1;
  if (positionals > 1) {
    parsed.ok = false; // more than one project directory is ambiguous (D-exec_new-3)
    return parsed;
  }
  if (positionals == 1) {
    parsed.options.project_dir = argv[1];
  }
  return parsed;
}

int run_editor_argv(int argc, const char* const* argv) {
  const ParsedArgs parsed = parse_args(argc, argv);
  if (!parsed.ok) {
    std::fprintf(stderr, "usage: arbitraryeditor [<project-dir>]\n");
    return 2;
  }
  return run_editor(parsed.options);
}

} // namespace ace::app
