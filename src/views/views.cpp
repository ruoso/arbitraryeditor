#include <ace/commands/app_state.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/interact/interact.hpp>
#include <ace/render/render.hpp>
#include <ace/views/views.hpp>

#include <arbc/model/journal.hpp>
#include <arbc/model/journal_entry.hpp>
#include <arbc/runtime/document.hpp>

#include <imgui.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace ace::views {
namespace {

constexpr const char* k_probe_pane_title = "Render Probe";

// Per-type body overrides — the registration seam. Default-empty (a placeholder
// is drawn); a downstream leaf installs its real body via register_view_body.
// UI-thread-only state (refinement: no new threading), reset per registration.
std::array<ViewBody, dockmodel::k_view_type_count> g_bodies{};

std::size_t body_index(dockmodel::ViewType type) { return static_cast<std::size_t>(type); }

} // namespace

const char* name() { return "views"; }

const char* probe_pane_title() { return k_probe_pane_title; }

void draw_probe_image(unsigned int texture, int width, int height) {
  ImGui::Image(static_cast<ImTextureID>(texture),
               ImVec2(static_cast<float>(width), static_cast<float>(height)));
}

void draw_probe_pane(unsigned int texture, int width, int height) {
  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
  // Auto-resize so the fixed-size image is never clipped by a default window.
  ImGui::Begin(k_probe_pane_title, nullptr, ImGuiWindowFlags_AlwaysAutoResize);
  draw_probe_image(texture, width, height);
  ImGui::End();
}

void register_view_body(dockmodel::ViewType type, ViewBody body) {
  g_bodies[body_index(type)] = std::move(body);
}

void draw_view(std::string_view view_id) {
  const std::optional<dockmodel::ParsedViewId> parsed = dockmodel::parse_view_id(view_id);
  if (!parsed) {
    ImGui::Text("Unknown view: %.*s", static_cast<int>(view_id.size()), view_id.data());
    return;
  }
  const ViewBody& body = g_bodies[body_index(parsed->type)];
  if (body) {
    body(view_id);
    return;
  }
  // Default: a labeled placeholder (D-view-registry-5). Real bodies are the
  // downstream panel-content leaves, registered via register_view_body.
  const std::string_view title = dockmodel::view_title(parsed->type);
  ImGui::Text("%.*s", static_cast<int>(title.size()), title.data());
  ImGui::TextDisabled("Placeholder — real content arrives with this view's panel leaf.");
}

void draw_history(commands::AppState& state, std::string_view /*view_id*/) {
  // Read the journal fresh each frame — the model is the single source of truth,
  // no shadow copy (D-history-6). Applied entries are [0, cursor); [cursor, depth)
  // is the redoable/future tail.
  arbc::Journal& journal = state.document().journal();
  const std::size_t depth = journal.depth();
  const std::size_t cursor = journal.cursor();

  // Affordance labels — the entries the Ctrl+Z / Ctrl+Shift+Z chord acts on next:
  // the head entry to undo, the tip entry to redo (D-history / Constraint 7).
  if (cursor > 0) {
    ImGui::TextDisabled("Undo %s", journal.entry_at(cursor - 1).name.c_str());
  }
  if (cursor < depth) {
    ImGui::TextDisabled("Redo %s", journal.entry_at(cursor).name.c_str());
  }
  ImGui::Separator();

  // The ordered list: a synthetic base row (the pre-first-edit state, target cursor
  // 0) above one row per journal entry in chronological order (D-history-4). The row
  // at cursor-1 — or the base row when cursor == 0 — is the highlighted head; future
  // entries [cursor, depth) are dimmed. A click records the row's target cursor; the
  // navigation loop below reaches it. Stable ###ids keep the rows drivable by the
  // e2e regardless of the (possibly duplicate) entry names.
  std::optional<std::size_t> target;

  if (ImGui::Selectable("Base###base", cursor == 0)) {
    target = 0;
  }
  for (std::size_t i = 0; i < depth; ++i) {
    const bool applied = i < cursor;
    const bool is_head = i + 1 == cursor;
    if (!applied) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }
    const std::string label = journal.entry_at(i).name + "###entry" + std::to_string(i);
    if (ImGui::Selectable(label.c_str(), is_head)) {
      target = i + 1;
    }
    if (!applied) {
      ImGui::PopStyleColor();
    }
  }

  // Click-to-jump: a bounded single-step loop toward the target cursor, defensively
  // end-stopped when a verb reports no move (D-history-5). Clicking the current head
  // targets the current cursor and loops zero times. Navigation goes ONLY through
  // the shipped commands::undo / commands::redo verbs — the panel never mutates the
  // journal directly (Constraint 1); the library exposes single-step nav only.
  if (target) {
    while (journal.cursor() > *target && commands::undo(state).moved) {
    }
    while (journal.cursor() < *target && commands::redo(state).moved) {
    }
  }
}

} // namespace ace::views
