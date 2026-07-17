#pragma once

namespace ace::app {

// Pure, UI-agnostic frame-loop lifecycle predicate, factored out of the shell so
// it can be unit-tested headless (refinement Acceptance-criteria §"L1 Catch2
// units"). Returns whether the main loop should run another frame.
//
//   `frames_done`     — frames already rendered this run.
//   `max_frames`      — frame cap; <= 0 means run until quit (the windowed app),
//                       > 0 stops once `frames_done` reaches it (tests).
//   `quit_requested`  — an SDL quit/close arrived; always stops the loop.
bool should_continue_loop(int frames_done, int max_frames, bool quit_requested);

} // namespace ace::app
