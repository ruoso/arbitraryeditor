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
#include <cstdio>
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

void draw_canvas_image(unsigned int texture, int width, int height) {
  // The Canvas body reuses the render_probe tile→GL display primitive: an Image
  // into the dockspace-owned canvas#N window (D-canvas_view-4).
  draw_probe_image(texture, width, height);
}

CanvasInput draw_canvas_interactive(unsigned int texture, int width, int height) {
  CanvasInput in;
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  draw_probe_image(texture, width, height);
  // Overlay an InvisibleButton over the SAME rect: an ImGui::Image is inert (no id, no
  // interaction), so the pane cannot otherwise capture a drag or report hover. Rewind the
  // cursor to the image origin, then place the button covering the image (D-nav-3).
  // AllowOverlap lets the app draw chrome ON TOP of the pane (the look-through camera
  // picker, editor.cameras.look_through) and have it stay clickable — where no later item
  // overlaps, the button behaves exactly as before, so nav gestures are unaffected.
  ImGui::SetCursorScreenPos(origin);
  ImGui::SetNextItemAllowOverlap();
  ImGui::InvisibleButton("##canvas_nav",
                         ImVec2(static_cast<float>(width), static_cast<float>(height)),
                         ImGuiButtonFlags_MouseButtonLeft);

  const ImGuiIO& io = ImGui::GetIO();
  in.hovered = ImGui::IsItemHovered();
  in.focus_x = io.MousePos.x - origin.x;
  in.focus_y = io.MousePos.y - origin.y;
  if (in.hovered) {
    in.wheel = io.MouseWheel; // wheel-zoom about the cursor (D2 §3)
    in.reset = ImGui::IsKeyPressed(ImGuiKey_F, /*repeat=*/false); // reset-to-fit (D-nav-7)
  }
  // Space-held left-drag pans the viewport camera (D9) — the always-on gesture,
  // independent of the active modal tool (D-nav-4). IsItemActive() holds while the
  // left button is down after pressing over the pane.
  if (ImGui::IsItemActive() && ImGui::IsKeyDown(ImGuiKey_Space)) {
    in.panning = true;
    in.pan_dx = io.MouseDelta.x;
    in.pan_dy = io.MouseDelta.y;
  }
  // The camera-frame gizmo read (editor.cameras.manip): the button EDGES + modifiers over
  // the SAME pane rect. The app hit-tests/drives the frame math from this; nothing here
  // knows about cameras (A11 keeps the gizmo geometry in L1 interact). The gizmo shares the
  // `##canvas_nav` button, so a border-grab and a Space-pan are the same drag disambiguated
  // by the Space key (Space => nav pan, inert on the frame — Constraint 7).
  in.pressed = ImGui::IsItemActivated();
  in.down = ImGui::IsItemActive();
  in.released = ImGui::IsItemDeactivated();
  in.shift = io.KeyShift;
  in.alt = io.KeyAlt;
  in.ctrl = io.KeyCtrl;
  in.rotate = ImGui::IsKeyDown(ImGuiKey_R);
  return in;
}

void draw_letterboxed(unsigned int texture, int tex_width, int tex_height, int pane_width,
                      int pane_height) {
  if (pane_width <= 0 || pane_height <= 0 || tex_width <= 0 || tex_height <= 0) {
    return; // nothing to present
  }
  // A look-through canvas renders the shot's EXACT crop (the texture is already sized to
  // the shot's pane-fit resolution, so surrounding composition never bleeds in). Fill the
  // whole pane with neutral bars, then centre the crop over them — clean letterbox margins,
  // not the surrounding scene (D-look_through-2/3).
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  const ImVec2 far(origin.x + static_cast<float>(pane_width),
                   origin.y + static_cast<float>(pane_height));
  draw_list->AddRectFilled(origin, far, IM_COL32(0, 0, 0, 255));
  const float off_x = (static_cast<float>(pane_width) - static_cast<float>(tex_width)) * 0.5F;
  const float off_y = (static_cast<float>(pane_height) - static_cast<float>(tex_height)) * 0.5F;
  ImGui::SetCursorScreenPos(ImVec2(origin.x + off_x, origin.y + off_y));
  draw_probe_image(texture, tex_width, tex_height);
  // Leave the cursor at the pane origin so the caller's overlay chrome (the picker) lands
  // at a predictable top-left, independent of the centred image position.
  ImGui::SetCursorScreenPos(origin);
}

void draw_scale_bar(double units, double device_px) {
  if (!(device_px > 0.0) || !(units > 0.0)) {
    return; // a degenerate camera scale: nothing to draw
  }
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  const ImVec2 wpos = ImGui::GetWindowPos();
  const ImVec2 wsize = ImGui::GetWindowSize();
  constexpr float pad = 12.0F;
  const float y = wpos.y + wsize.y - pad;
  const float x0 = wpos.x + pad;
  const float x1 = x0 + static_cast<float>(device_px);
  const ImU32 col = IM_COL32(255, 255, 255, 220);
  // A bar with end ticks + a composition-unit label — the scale readout, never a "%".
  draw_list->AddLine(ImVec2(x0, y), ImVec2(x1, y), col, 2.0F);
  draw_list->AddLine(ImVec2(x0, y - 4.0F), ImVec2(x0, y + 4.0F), col, 2.0F);
  draw_list->AddLine(ImVec2(x1, y - 4.0F), ImVec2(x1, y + 4.0F), col, 2.0F);
  char label[64];
  std::snprintf(label, sizeof(label), "%g u", units);
  draw_list->AddText(ImVec2(x0, y - 18.0F), col, label);
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
