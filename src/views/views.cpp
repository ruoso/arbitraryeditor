#include <ace/commands/app_state.hpp>
#include <ace/commands/export.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/interact/interact.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/ids.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ace::views {
namespace {

constexpr const char* k_probe_pane_title = "Render Probe";

// Per-type body overrides — the registration seam. Default-empty (a placeholder
// is drawn); a downstream leaf installs its real body via register_view_body.
// UI-thread-only state (refinement: no new threading), reset per registration.
std::array<ViewBody, dockmodel::k_view_type_count> g_bodies{};

std::size_t body_index(dockmodel::ViewType type) { return static_cast<std::size_t>(type); }

// The Export panel's persistent UI state (D-export-9: export is NOT a one-shot
// confirmed op, so it has state to keep). UI-thread-only, and file-local because
// `ViewType::Export` is a SINGLETON in the catalog — there is never a second
// instance to key it by. It is reset whenever the panel is drawn against a
// different `ExportService`, so a second session in the same process (every e2e in
// the one `ace_shell_test` binary) starts clean rather than inheriting stale ticks.
constexpr std::size_t k_destination_capacity = 1024;
struct ExportPanel {
  const commands::ExportService* owner = nullptr;
  std::vector<arbc::ObjectId> ticked;
  int scale = 1;
  bool filled_background = false;
  // Opaque black: an opaque default is what makes "tick filled, run" produce a fully
  // opaque PNG without the panel guessing a colour for the user.
  std::array<float, 4> background{0.0F, 0.0F, 0.0F, 1.0F};
  std::array<char, k_destination_capacity> destination{};
  bool destination_seeded = false;
};
ExportPanel g_export_panel{};

void set_destination(ExportPanel& panel, const std::string& value) {
  const std::size_t n = std::min(value.size(), k_destination_capacity - 1);
  std::memcpy(panel.destination.data(), value.data(), n);
  panel.destination[n] = '\0';
}

std::uint8_t to_srgb8(float channel) {
  return static_cast<std::uint8_t>(std::clamp(channel, 0.0F, 1.0F) * 255.0F + 0.5F);
}

const char* state_label(commands::ExportState state) {
  switch (state) {
  case commands::ExportState::Idle:
    return "Idle";
  case commands::ExportState::Running:
    return "Exporting";
  case commands::ExportState::Finished:
    return "Finished";
  case commands::ExportState::Cancelled:
    return "Cancelled";
  case commands::ExportState::Failed:
    return "Failed";
  }
  return "Idle";
}

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
    // F frames the whole document (reset-to-fit, D-nav-7); Shift+F frames the current
    // selection (the deep-zoom nav aid, D-nav_aids-6). One key press, split by Shift, so
    // the two are mutually exclusive — Shift+F is never also a reset (Constraint 6: an
    // empty-selection Shift+F must stay a no-op, not yank the view to the document).
    const bool f_pressed = ImGui::IsKeyPressed(ImGuiKey_F, /*repeat=*/false);
    in.reset = f_pressed && !io.KeyShift;
    in.frame_selection = f_pressed && io.KeyShift;
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
  in.super = io.KeySuper; // D7's "Cmd/Ctrl" needs both halves (D-selection-10)
  in.rotate = ImGui::IsKeyDown(ImGuiKey_R);
  // The press anchor a marquee drags from (D-selection-10). ImGui records the position of the
  // last left-press, so this stays valid for the WHOLE drag rather than only on the activation
  // frame — the L4 wiring reads it once at `pressed` and needs no anchor bookkeeping of its own.
  in.press_x = io.MouseClickedPos[0].x - origin.x;
  in.press_y = io.MouseClickedPos[0].y - origin.y;
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
  // Render from ONE published snapshot per frame (A18 / D-history_published_reads-1/2).
  // The journal's entry vector is writer-owned and libarbc's entry-inspection accessor is
  // writer-thread only — it hands out a reference INTO a vector a concurrent commit may
  // reallocate — so this panel never touches it, and reads no journal internals at all.
  // Instead the writer publishes an immutable `HistorySnapshot` at each writer-turn
  // boundary and the panel loads it by pointer, from any thread, lock-free. This
  // deliberately reverses D-history-6's "no shadow copy": the copy is not a cache the
  // panel maintains but a publication the writer performs, which is the only shape libarbc
  // doc 15 permits for cross-thread structure — and it is strictly cheaper, one vector
  // build per EDIT rather than three journal reads plus a concatenation per row per FRAME.
  //
  // Names and cursor come from the SAME loaded pointer, never mixed with a live re-read
  // of the journal's published cursor atomic: a cursor from a later generation can exceed
  // this snapshot's name count and index out of range at the "Redo <name>" affordance
  // (Constraint 2). Applied entries are [0, cursor); [cursor, depth) is the
  // redoable/future tail.
  const std::shared_ptr<const commands::HistorySnapshot> history = state.history().load();
  const std::size_t depth = history->names.size();
  const std::size_t cursor = history->cursor;

  // Affordance labels — the entries the Ctrl+Z / Ctrl+Shift+Z chord acts on next:
  // the head entry to undo, the tip entry to redo (D-history / Constraint 7).
  if (cursor > 0) {
    ImGui::TextDisabled("Undo %s", history->names[cursor - 1].c_str());
  }
  if (cursor < depth) {
    ImGui::TextDisabled("Redo %s", history->names[cursor].c_str());
  }
  ImGui::Separator();

  // The ordered list: a synthetic base row (the pre-first-edit state, target cursor
  // 0) above one row per journal entry in chronological order (D-history-4). The row
  // at cursor-1 — or the base row when cursor == 0 — is the highlighted head; future
  // entries [cursor, depth) are dimmed. A click records the row's target cursor; the L1
  // verb below reaches it. Stable ###ids keep the rows drivable by the e2e regardless of
  // the (possibly duplicate) entry names, and they index the SNAPSHOT by position, which
  // is what makes them unchanged by this leaf.
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
    const std::string label = history->names[i] + "###entry" + std::to_string(i);
    if (ImGui::Selectable(label.c_str(), is_head)) {
      target = i + 1;
    }
    if (!applied) {
      ImGui::PopStyleColor();
    }
  }

  // Click-to-jump: one L1 verb, not a loop here (D-history_published_reads-4). The
  // bounded, end-stopped single-step walk moved into `commands::navigate_to` — its loop
  // conditions re-read the live journal cursor, so keeping it here would keep a journal
  // read in L3. Behaviour is unchanged: same clamping at the base and the tip, and
  // clicking the current head targets the current cursor and walks zero steps. The panel
  // still never mutates the journal directly (Constraint 1).
  if (target) {
    commands::navigate_to(state, *target);
  }
}

void draw_export(commands::ExportService& service, commands::AppState& state,
                 std::string_view /*view_id*/) {
  ExportPanel& panel = g_export_panel;
  if (panel.owner != &service) {
    panel = ExportPanel{};
    panel.owner = &service;
  }
  if (!panel.destination_seeded) {
    // D16 / D-export-10: exports default into the project's own `exports/` — the
    // directory the scaffold has always created and nothing has ever written into.
    set_destination(panel, state.layout().exports_dir.string());
    panel.destination_seeded = true;
  }

  // A pure READ of the document (Constraint 7): layer order, over the lock-free
  // `pin()` seam, every frame. No transaction, no journal entry, no writer post.
  const std::vector<scene::Camera> cameras = scene::cameras(state.document());
  const bool running = service.running();

  ImGui::TextUnformatted("Cameras");
  ImGui::Separator();
  if (cameras.empty()) {
    // The affordance that exists rather than a dead end (the empty-state lesson
    // `editor.cameras.new_shot_from_view` closed at camera_inspector.cpp:41).
    ImGui::TextDisabled("No cameras — mint one with New Shot From View or Frame Selection.");
  }
  for (std::size_t i = 0; i < cameras.size(); ++i) {
    const arbc::ObjectId id = cameras[i].id;
    const auto found = std::find(panel.ticked.begin(), panel.ticked.end(), id);
    bool ticked = found != panel.ticked.end();
    // Stable ###ids keep the rows drivable by the e2e regardless of (legitimately
    // duplicate) camera names — the same reason the History and Inspector lists use them.
    const std::string label = cameras[i].name + "###export_cam_" + std::to_string(i);
    if (ImGui::Checkbox(label.c_str(), &ticked)) {
      if (ticked) {
        panel.ticked.push_back(id);
      } else {
        panel.ticked.erase(found);
      }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d x %d", cameras[i].resolution.width, cameras[i].resolution.height);
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Options");
  // D14's two knobs. The multiplier is an integer >= 1 (Constraint 2): `out = N *
  // native`, genuinely composed at the higher resolution, never a post-hoc resample.
  ImGui::InputInt("Scale (N x)###export_scale", &panel.scale);
  panel.scale = std::max(1, panel.scale);
  // Transparent is the DEFAULT (it preserves alpha); a filled background is a
  // deliberate opt-in, and on the transparent path the render is byte-identical to
  // the shipped `render_document_srgb8`.
  ImGui::Checkbox("Filled background###export_bg_filled", &panel.filled_background);
  if (panel.filled_background) {
    ImGui::ColorEdit4("Background###export_bg_color", panel.background.data());
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Destination");
  ImGui::InputText("###export_destination", panel.destination.data(), panel.destination.size());
  if (service.can_pick_destination()) {
    // The SHIPPED folder seam (A12 / D-export-10), injected by L4. Asynchronous: the
    // callback fires on a later UI-thread frame, so capturing the file-local panel is
    // safe — it outlives every frame.
    if (ImGui::Button("Browse…###export_browse")) {
      service.pick_destination([&panel](std::optional<std::filesystem::path> dir) {
        if (dir) {
          set_destination(panel, dir->string());
        }
      });
    }
    ImGui::SameLine();
  }

  // Refuse rather than guess: with nothing ticked (or no destination, or no bound
  // derivation) Export is DISABLED, not silently interpreted as "everything".
  const bool can_run = !running && !panel.ticked.empty() && panel.destination[0] != '\0' &&
                       static_cast<bool>(service.shot_camera());
  ImGui::BeginDisabled(!can_run);
  if (ImGui::Button("Export###export_run")) {
    commands::ExportOptions options;
    options.destination = std::filesystem::path(panel.destination.data());
    options.scale = panel.scale;
    if (panel.filled_background) {
      options.background =
          commands::Rgba8{to_srgb8(panel.background[0]), to_srgb8(panel.background[1]),
                          to_srgb8(panel.background[2]), to_srgb8(panel.background[3])};
    }
    service.start(
        commands::plan_export(state.document(), panel.ticked, options, service.shot_camera()),
        options);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!running);
  if (ImGui::Button("Cancel###export_cancel")) {
    service.cancel();
  }
  ImGui::EndDisabled();

  // ONE published snapshot per frame (A18), so `done`/`total`/`current_name` are
  // always the same generation — never a half-written struct read under no lock.
  const std::shared_ptr<const commands::ExportProgress> progress = service.progress();
  std::string status = state_label(progress->state);
  if (progress->total > 0) {
    status += " " + std::to_string(progress->done) + "/" + std::to_string(progress->total);
  }
  if (!progress->current_name.empty()) {
    status += " — " + progress->current_name;
  }
  ImGui::SmallButton((status + "###export_status").c_str());

  if (const std::shared_ptr<const commands::ExportReport> report = service.report()) {
    ImGui::TextDisabled("%zu written, %zu failed, %zu refused", report->written, report->failed,
                        report->refused);
    if (!report->reason.empty()) {
      ImGui::TextWrapped("%s", report->reason.c_str());
    }
    for (const commands::ExportItemResult& item : report->items) {
      if (!item.message.empty()) {
        ImGui::TextWrapped("%s", item.message.c_str());
      }
    }
    // D-export-8: batch coherence is REPORTED, not enforced — an edit that landed
    // mid-batch is stated rather than silently mixed in.
    if (report->document_changed_during_export) {
      ImGui::TextDisabled("The document changed during this export.");
    }
  }
}

} // namespace ace::views
