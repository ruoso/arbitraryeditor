#pragma once

#include <ace/dock/dock.hpp>

#include <string>

namespace ace::dock {

// The stable ImGui window id of the welcome — exposed so the e2e composes every
// ref from ONE symbol rather than a literal, exactly as `tool_rail_title()` does
// for the rail (D-welcome-4).
const char* welcome_window_title();

// The pre-project launcher's whole window (docs/00-design.md D26,
// docs/01-architecture.md A22): the chrome a process with NO project shows.
//
// D22 puts the entry affordances on the tool rail, and D18 makes the rail home
// base *of a project window* — so a process holding zero `Document`s has no rail,
// no dockspace and no canvas, and therefore no surface to offer anything on. This
// is that surface: a SECOND host for the same `dock::ProjectGateway` the rail
// hosts (A12's inversion reused, not duplicated), offering exactly D22's three
// verbs — New Project… / Open Project… / Open Recent — over the same native folder
// pick, the same parent-plus-name compose (the shared `NewProjectModal`) and the
// same pruned MRU store. No scratch verb, no skip, no Quit button and no
// remembered default (D-welcome-5).
//
// It is MODAL IN THE UX SENSE but a full-viewport window in the implementation
// (D-welcome-4): the launcher window contains nothing else, so it is already
// unreachable-behind, while New's compose popup is a genuine `BeginPopupModal`
// that must open ABOVE it — and stacking a modal on a modal is the fiddly path to
// an identical picture.
//
// `dock` includes only its own component's headers, imgui and std here: the
// launcher reaches process launch, SDL and the prefs store through the gateway
// abstraction it declares, never by including `<ace/commands/…>` or
// `<ace/platform/…>` (§8 / A12). Choosing a verb that actually SPAWNED, or
// dismissing, latches `exit_requested()`; the L4 `run_welcome_launcher` reads it
// as the frame loop's stop condition and the process exits (D19: the launcher
// never becomes the project window).
class WelcomeWindow {
public:
  // The project-entry gateway the three verbs drive (A12 / D22) — the same
  // non-owning injection style `Dockspace` uses. Null until the app wires one at
  // bootstrap (or a test injects a fake); when null the welcome draws its heading
  // and nothing actionable.
  void set_project_gateway(ProjectGateway* gateway) { project_gateway_ = gateway; }
  ProjectGateway* project_gateway() const { return project_gateway_; }

  // Draw one frame of the welcome (the launcher's entire draw-content), including
  // the compose modal and the dismissal check.
  void draw();

  // The one-way exit latch (D-welcome-8): true once a verb VALIDATED AND SPAWNED —
  // the sibling exists before the launcher goes away — or once the user dismissed
  // (the OS window close, or `Esc` with the compose modal not open). A cancelled
  // pick, a refused target and a failed spawn all leave it false with the welcome
  // up and feedback on screen.
  bool exit_requested() const { return exit_requested_; }

  // Inline feedback for the last verb (a non-project pick, a vanished recent, a
  // failed spawn, an invalid New name), rendered with TextWrapped. Empty means
  // "no message" — the rail's `project_feedback()` by another name.
  std::string& feedback() { return feedback_; }
  const std::string& feedback() const { return feedback_; }

private:
  ProjectGateway* project_gateway_ = nullptr; // project-entry seam (app-wired, may be null)
  NewProjectModal new_project_;               // this host's own compose-modal state
  std::string feedback_;                      // inline feedback for the last verb
  bool exit_requested_ = false;               // the one-way exit latch
};

} // namespace ace::dock
