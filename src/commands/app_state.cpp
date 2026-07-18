#include <ace/commands/app_state.hpp>

#include <arbc/builtin_kinds.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/model/model.hpp>

#include <utility>

namespace ace::commands {

AppState::AppState(project::OpenedProject opened)
    : document_(std::move(opened.document)), layout_(std::move(opened.layout)),
      rebuilt_from_canonical_(opened.rebuilt_from_canonical) {
  // The persistent, lifetime-scoped kind Registry (D-open-7): seeded once here,
  // not rebuilt per open. `save`/export and the future A6 plugin seam reuse it.
  arbc::register_builtin_kinds(registry_);
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

} // namespace ace::commands
