#include <ace/app/app_loop.hpp>

namespace ace::app {

bool should_continue_loop(int frames_done, int max_frames, bool quit_requested) {
  if (quit_requested) {
    return false;
  }
  if (max_frames > 0 && frames_done >= max_frames) {
    return false;
  }
  return true;
}

} // namespace ace::app
