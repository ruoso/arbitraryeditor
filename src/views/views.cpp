#include <ace/interact/interact.hpp>
#include <ace/render/render.hpp>
#include <ace/views/views.hpp>

#include <imgui.h>

namespace ace::views {
namespace {

constexpr const char* k_probe_pane_title = "Render Probe";

} // namespace

const char* name() { return "views"; }

const char* probe_pane_title() { return k_probe_pane_title; }

void draw_probe_pane(unsigned int texture, int width, int height) {
  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
  // Auto-resize so the fixed-size image is never clipped by a default window.
  ImGui::Begin(k_probe_pane_title, nullptr, ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::Image(static_cast<ImTextureID>(texture),
               ImVec2(static_cast<float>(width), static_cast<float>(height)));
  ImGui::End();
}

} // namespace ace::views
