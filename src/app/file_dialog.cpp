#include <ace/app/file_dialog.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <memory>
#include <utility>

namespace ace::app {

struct SdlFileDialog::Pending {
  SdlFileDialog* owner;
  FileDialog::Callback callback;
};

SdlFileDialog::SdlFileDialog(SDL_Window* parent) : parent_(parent) {}

SdlFileDialog::~SdlFileDialog() {
  // Detach every in-flight pick: a callback that fires after us sees a null owner and
  // self-deletes without touching freed state (shutdown-cancel). A live pick can only exist
  // after `show()` has crossed to the OS picker (excluded below), so the body is unreachable
  // headless — cover the same shutdown-cancel via the excluded `show`/`on_dialog` glue.
  for (Pending* pending : pending_) {
    pending->owner = nullptr; // GCOVR_EXCL_LINE
  }
}

// The native-dialog crossing is SDL/OS glue with no headless driver (the offscreen lane has no
// file picker), exactly like `SdlFolderDialog` — the import LOGIC it feeds is covered directly
// (the gateway verb + `drop_device_point` + the e2e's scripted FileDialog). Excluded from the
// coverage gate on that basis, mirroring `src/app/main.cpp`'s untestable entry glue.
// GCOVR_EXCL_START
void SdlFileDialog::show(Callback on_pick) {
  auto* pending = new Pending{this, std::move(on_pick)};
  pending_.push_back(pending);
  SDL_ShowOpenFileDialog(&SdlFileDialog::on_dialog, pending, parent_, /*filters=*/nullptr,
                         /*nfilters=*/0, /*default_location=*/nullptr, /*allow_many=*/false);
}

void SdlFileDialog::on_dialog(void* userdata, const char* const* filelist, int /*filter*/) {
  std::unique_ptr<Pending> pending(static_cast<Pending*>(userdata));
  std::optional<std::filesystem::path> result;
  if (filelist != nullptr && filelist[0] != nullptr && filelist[0][0] != '\0') {
    result = std::filesystem::path(filelist[0]);
  }
  SdlFileDialog* owner = pending->owner;
  if (owner == nullptr) {
    return; // torn down while in flight — drop the result (Pending freed here)
  }
  owner->pending_.erase(std::remove(owner->pending_.begin(), owner->pending_.end(), pending.get()),
                        owner->pending_.end());
  pending->callback(result);
}
// GCOVR_EXCL_STOP

} // namespace ace::app
