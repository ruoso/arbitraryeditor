#pragma once

#include <ace/dockmodel/view_registry.hpp>

#include <functional>
#include <string_view>

namespace ace::commands {
// The one owned project session (A7/D19); the History body reads its journal and
// loops the shipped undo/redo verbs. Forward-declared to keep this header light —
// views.cpp includes <ace/commands/app_state.hpp> for the definition.
class AppState;
// The async export job (A20 / D-export-7); the Export body drives it and reads its
// published progress snapshot. Forward-declared for the same reason.
class ExportService;
} // namespace ace::commands

namespace ace::views {

// The views component. See docs/01-architecture.md §8 (component levelization).
const char* name();

// The stable ImGui window title/id of the render_probe pane — exposed so the
// e2e can drive the pane by the same id the view draws under.
const char* probe_pane_title();

// The render_probe pane (A6 display tail): one ImGui::Begin/Image/End window
// showing the offline-rendered texture. `texture` is a gl::upload_rgba8 handle;
// `width`/`height` are the image dimensions. The standalone pre-registry pane —
// the dockspace draws the Canvas view body via draw_probe_image instead.
void draw_probe_pane(unsigned int texture, int width, int height);

// The render_probe image (the Canvas view body — D18 "the canvas is a view"):
// draws just the Image into the CURRENT ImGui window (no Begin/End — the
// dockspace owns the window + the tab ✕). draw_probe_pane wraps this.
void draw_probe_image(unsigned int texture, int width, int height);

// The Canvas view body (editor.canvas.view; D18 "the canvas is a view"): draws
// the driver's current GL texture as an Image into the CURRENT ImGui window (no
// Begin/End — the dockspace owns the canvas#N window + the tab ✕). `texture` is a
// gl::upload_rgba8 handle for the settled sRGB8 frame; `width`/`height` are the
// texture's pixel dimensions. Reuses the render_probe tile→GL display primitive.
void draw_canvas_image(unsigned int texture, int width, int height);

// The raw navigation gesture read over the canvas pane (editor.canvas.nav): the
// always-on wheel-zoom + Space-drag pan + reset-to-fit input, in DEVICE pixels. No
// camera math here (that is L1 interact, D-nav-2/7) — this only reports the ImGui
// input the app threads through the interact math and submits to the render thread.
struct CanvasInput {
  bool hovered = false; // the cursor is over the pane
  float wheel = 0.0F;   // vertical wheel notches this frame (0 unless hovered)
  bool panning = false; // a Space-held left-drag is in progress (D9)
  float pan_dx = 0.0F;  // the drag delta this frame, device px
  float pan_dy = 0.0F;
  float focus_x = 0.0F; // the cursor, relative to the image top-left (device px)
  float focus_y = 0.0F;
  bool reset = false; // reset-to-fit requested (F while hovered, D-nav-7)
  bool frame_selection =
      false; // frame-the-selection nav aid requested (Shift+F while hovered, D-nav_aids-6)
  // --- camera-frame gizmo input (editor.cameras.manip; D7/D9) -----------------
  // The raw left-button + modifier read the app threads through the interact frame math
  // to hit-test / drag a shot camera's frame. No camera knowledge here — the app owns the
  // camera list and commits the edit (A11: interact stays pure math). `pressed`/`released`
  // are the button EDGES (grab start / commit) and `down` the held-drag state.
  bool pressed = false;  // left button went down over the pane this frame
  bool down = false;     // left button is held over the pane (a drag is in progress)
  bool released = false; // left button released this frame (a grab's commit edge)
  bool shift = false;    // Shift constrains (15° / aspect / axis-lock, D9)
  bool alt = false;      // Alt from center (D9)
  bool ctrl = false;     // Cmd/Ctrl bypass-snap (D9)
  bool rotate = false;   // the dutch-rotation gate held (R; D-manip-5, modifier-gated)
  // --- selection input (editor.cells.selection; D7/D-selection-10) -------------
  // The PRESS ANCHOR: the pointer at the activation edge, relative to the image top-left
  // (device px) — a marquee needs where the drag BEGAN, and `focus_x/focus_y` report only
  // where it is now. Stable for the whole drag (read from ImGui's recorded click position),
  // so the L4 wiring stays stateless about the anchor.
  float press_x = 0.0F;
  float press_y = 0.0F;
  // The Super/Cmd modifier, reported SEPARATELY from `ctrl`. D7 says "Cmd/Ctrl select-behind",
  // and `ctrl` is `io.KeyCtrl` only — folding Super into it would silently change
  // `editor.cameras.manip`'s bypass-snap gate, which is not this leaf's to change, while
  // leaving it out would make select-behind unreachable on macOS. Consumers OR the two.
  bool super = false;
};

// Draw the Canvas body AND read its navigation gestures: the `draw_canvas_image`
// tile→GL Image plus an InvisibleButton overlay (an ImGui::Image is inert) that
// captures hover/drag/wheel over the SAME pane rect. Returns the wheel-zoom /
// Space-drag pan / reset-to-fit input for this frame (editor.canvas.nav / D-nav-4).
// Draws into the CURRENT window (the dockspace owns Begin/End).
CanvasInput draw_canvas_interactive(unsigned int texture, int width, int height);

// Present a look-through shot crop letterboxed into the pane (editor.cameras.look_through,
// D-look_through-2/3): fills the `pane_width`x`pane_height` window with neutral bars and
// centres the `tex_width`x`tex_height` shot texture (already the shot's pane-fit crop) over
// them, so the letterbox margins are clean bars — never surrounding composition. Leaves the
// cursor at the pane origin so the caller can overlay chrome (the camera picker) predictably.
// Draws into the CURRENT window (the dockspace owns Begin/End).
void draw_letterboxed(unsigned int texture, int tex_width, int tex_height, int pane_width,
                      int pane_height);

// The composition-units scale-bar overlay (D2 §3 / D-nav-6): a bar `device_px`
// wide labelled with its `units` composition-unit length, pinned bottom-left of the
// CURRENT window — never a "%". A zero/degenerate bar draws nothing. The nice-number
// math is L1 interact; this only renders the result.
void draw_scale_bar(double units, double device_px);

// A per-type view body: draws the view's content into the CURRENT ImGui window.
// `view_id` is the dockmodel instance id being drawn (a type may draw multiple
// instances). The dockspace owns the enclosing Begin/End + the close button.
using ViewBody = std::function<void(std::string_view view_id)>;

// Register (or replace) the body drawn for `type` — the seam each downstream
// panel leaf (inspector/layers/…/canvas.view) plugs its real body into without
// touching the L1 catalog (D-view-registry-5). Passing an empty std::function
// restores the default labeled placeholder.
void register_view_body(dockmodel::ViewType type, ViewBody body);

// Draw the body of the view `view_id` into the current window: parse the id to
// its ViewType and dispatch to that type's registered body, or a default
// labeled placeholder when none is registered. An unparseable id draws an
// "unknown view" placeholder.
void draw_view(std::string_view view_id);

// The History view body (D18 "History is a view"; D-history-1): renders the one
// owned session's undo journal as an ordered list — a synthetic base row (the
// pre-first-edit state) plus one row per journal entry in chronological order, the
// head highlighted and future/redoable entries dimmed — and navigates it when a row
// is clicked, through the L1 commands::navigate_to verb (D-history-4/5).
//
// A pure reader/navigator that touches NO journal internals (arch A18 /
// D-history_published_reads-1/2): it renders from ONE `commands::HistorySnapshot`
// loaded per frame off `AppState::history()` — the immutable value the writer thread
// publishes at each writer-turn boundary — never from the writer-owned entry vector
// libarbc's `entry_at` hands out. Names and cursor come from that same loaded pointer,
// so a frame's list, highlight and dim-split are always one self-consistent generation.
// It never mutates the journal directly (Constraint 1). Draws into the CURRENT window
// (the dockspace owns Begin/End). The L4 shell registers it capturing the one AppState&
// and clears it on exit (D-history-3).
void draw_history(commands::AppState& state, std::string_view view_id);

// The Export view body (D14 / D-export-9 / A20): a camera tick-list, the two D14
// knobs (an integer N x scale multiplier and a transparent-vs-filled background),
// the destination, and a live progress readout with Export / Cancel.
//
// A PANEL, not a rail modal, and deliberately so: export carries persistent state,
// which is the inverse of A16's "inserting is a one-shot confirmed op" reasoning.
// It also adds no `ProjectGateway` virtual — `views` already reaches `commands`,
// `scene` and `interact`, so the A12/A13/A16 inversion (which exists because `dock`
// may not see `commands`) simply does not apply here.
//
// A pure READER of the document (Constraint 7): it opens no transaction, adds no
// journal entry, and posts nothing to the writer. It reads `scene::cameras()` per
// frame and the service's A18-published progress snapshot once per frame; the job
// itself runs on the service's own `platform::Threads` thread.
//
// Every interactive widget carries an explicit `###id` so the ImGui Test Engine can
// drive it. `ExportState` is per-panel-instance but the Export view is a singleton,
// so the tick-list lives in the body's own file-local state, keyed by nothing.
// Draws into the CURRENT window (the dockspace owns Begin/End). The L4 shell
// registers it capturing the one `AppState&` + `ExportService&` and clears it before
// they die — the seam is process-global.
void draw_export(commands::ExportService& service, commands::AppState& state,
                 std::string_view view_id);

} // namespace ace::views
