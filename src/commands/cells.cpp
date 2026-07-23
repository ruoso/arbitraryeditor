#include <ace/commands/cells.hpp>

#include <arbc/base/expected.hpp>
#include <arbc/runtime/document.hpp>

#include <utility>

namespace ace::commands {

Command insert_cell_command(const arbc::Registry& registry, std::string kind_id, std::string config,
                            const arbc::Affine& placement, InsertCellOutcome& outcome) {
  outcome = InsertCellOutcome{};
  return Command{"insert_cell", [&registry, &outcome, kind_id = std::move(kind_id),
                                 config = std::move(config), placement](arbc::Document& doc) {
                   const arbc::expected<arbc::ObjectId, std::string> added =
                       scene::add_cell(doc, registry, kind_id, config, placement);
                   if (added) {
                     outcome.content = *added;
                   } else {
                     outcome.error = added.error();
                   }
                 }};
}

} // namespace ace::commands
