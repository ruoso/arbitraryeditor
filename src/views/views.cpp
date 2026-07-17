#include <ace/dockmodel/view_registry.hpp>
#include <ace/interact/interact.hpp>
#include <ace/render/render.hpp>
#include <ace/views/views.hpp>

#include <imgui.h>

#include <array>
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

} // namespace ace::views
