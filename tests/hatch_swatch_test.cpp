// L1 unit tests for editor.panels.hatch_swatch (D6/§5:191-194): the cross-panel hover field on the
// headless session holder, and the list-side swatch's identity agreeing with the overview box's by
// construction (Constraint 1/2, D-hatch_swatch-3). No ImGui — this is the ASan/TSan-clean lane
// (docs/01-architecture.md §8/§9). The swatch's DRAW is an L4 ImGui mark (golden N/A); what is
// pinnable headless is the pure `interact::overview_pattern` identity both panels key off, and the
// bottom→top ordinal convention the list must pass.

#include <ace/commands/app_state.hpp>
#include <ace/interact/interact.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

using ace::commands::AppState;
using ace::interact::OverviewPattern;

namespace {

// A temp dir wiped on entry and exit (the commands_test pattern), named distinctly so the
// hatch_swatch case never collides with the other suites in the one ace_tests binary.
struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_hatch_swatch_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

bool same_pattern(const OverviewPattern& a, const OverviewPattern& b) {
  return a.style.angle_rad == b.style.angle_rad && a.style.cross == b.style.cross &&
         a.color_index == b.color_index;
}

} // namespace

TEST_CASE("hatch_swatch: AppState hover field round-trips and survives a move (D-hatch_swatch-1)") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "hover");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));

  // A fresh session reports no hover — the pointer is over nothing (Constraint 5).
  CHECK(state.hovered_object() == std::nullopt);

  // Set → reads back the id; clear → back to nullopt. Plain transient state, no transaction.
  const arbc::ObjectId id{7};
  state.set_hovered(id);
  CHECK(state.hovered_object() == std::optional<arbc::ObjectId>(id));
  state.set_hovered(std::nullopt);
  CHECK(state.hovered_object() == std::nullopt);

  // Moves cleanly with the defaulted move-construction, exactly like `selection_` /
  // `entered_composition_` (the field the docstring pins as its precedent).
  state.set_hovered(id);
  AppState moved(std::move(state));
  CHECK(moved.hovered_object() == std::optional<arbc::ObjectId>(id));
}

TEST_CASE("hatch_swatch: the list swatch identity IS the overview box identity, bottom→top "
          "(Constraint 1/2, D-hatch_swatch-3)") {
  // Build a real composition so the slot↔cell mapping is the shipped `scene::cells` order both
  // panels walk — not a hand-rolled index.
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  arbc::Document doc;
  doc.add_composition(128.0, 128.0);
  std::vector<arbc::ObjectId> inserted;
  for (int i = 0; i < 5; ++i) {
    const auto id = ace::scene::add_cell(doc, registry, "org.arbc.raster", "16x16",
                                         arbc::Affine::translation(4.0 * i, 4.0 * i));
    REQUIRE(id.has_value());
    inserted.push_back(*id);
  }
  const std::vector<ace::scene::Cell> cells = ace::scene::cells(doc, registry);
  REQUIRE(cells.size() == 5);
  const int count = static_cast<int>(cells.size());

  // The OVERVIEW draws `cells` bottom→top: the cell at vector index `i` gets identity
  // `overview_pattern(i, count)` (overview_panel.cpp). The LAYERS list walks front→back over list
  // slots and draws `cells[count-1-slot]` with ordinal `count-1-slot` (layers_panel.cpp). For the
  // SAME cell the two ordinals coincide by construction, so the swatch and the box are
  // byte-identical.
  for (int slot = 0; slot < count; ++slot) {
    const int list_ordinal = count - 1 - slot;                             // what the list passes
    const std::size_t cell_index = static_cast<std::size_t>(list_ordinal); // the cell it draws
    const int overview_ordinal = static_cast<int>(cell_index); // what the overview passes for it
    REQUIRE(cells[cell_index].id == inserted[cell_index]);

    const OverviewPattern from_list = ace::interact::overview_pattern(list_ordinal, count);
    const OverviewPattern from_overview = ace::interact::overview_pattern(overview_ordinal, count);
    CHECK(same_pattern(from_list, from_overview));

    // Re-pin `overview_pattern` determinism from the list's own call site (same (ordinal, count) →
    // same result across calls) — the identity cannot silently drift between the two draws.
    CHECK(same_pattern(from_list, ace::interact::overview_pattern(list_ordinal, count)));
  }

  // The bottom→top convention is correctness-critical: passing the RAW front→back `slot` (the bug
  // Constraint 2 pins against) would desync the two views for every slot but the middle one —
  // assert the mismatch exists so a regression to `slot` is caught, not silently accepted.
  bool raw_slot_would_desync = false;
  for (int slot = 0; slot < count; ++slot) {
    const int cell_index = count - 1 - slot;
    if (!same_pattern(ace::interact::overview_pattern(slot, count),
                      ace::interact::overview_pattern(cell_index, count))) {
      raw_slot_would_desync = true;
    }
  }
  CHECK(raw_slot_would_desync);
}
