#include <ace/app/folder_dialog.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <memory>
#include <utility>

// editor.project.open_ui — the SDL-backed native folder dialog (A12). The only
// SDL in this leaf; everything above it speaks the abstract FolderDialog seam so
// the gateway logic is testable with a scriptable fake and the WASM port swaps in
// the File System Access API here (A3). This wrapper is NOT unit-tested — the
// offscreen driver has no folder picker — so it stays deliberately thin.

namespace ace::app {

struct SdlFolderDialog::Pending {
  SdlFolderDialog* owner;
  FolderDialog::Callback callback;
};

SdlFolderDialog::SdlFolderDialog(SDL_Window* parent) : parent_(parent) {}

SdlFolderDialog::~SdlFolderDialog() {
  // Detach every in-flight pick: a callback that fires after us sees a null owner
  // and self-deletes without touching freed state (shutdown-cancel).
  for (Pending* pending : pending_) {
    pending->owner = nullptr;
  }
}

void SdlFolderDialog::show(Callback on_pick) {
  auto* pending = new Pending{this, std::move(on_pick)};
  pending_.push_back(pending);
  SDL_ShowOpenFolderDialog(&SdlFolderDialog::on_dialog, pending, parent_, nullptr, false);
}

void SdlFolderDialog::on_dialog(void* userdata, const char* const* filelist, int /*filter*/) {
  std::unique_ptr<Pending> pending(static_cast<Pending*>(userdata));
  std::optional<std::filesystem::path> result;
  if (filelist != nullptr && filelist[0] != nullptr && filelist[0][0] != '\0') {
    result = std::filesystem::path(filelist[0]);
  }
  SdlFolderDialog* owner = pending->owner;
  if (owner == nullptr) {
    return; // torn down while in flight — drop the result (Pending freed here)
  }
  owner->pending_.erase(std::remove(owner->pending_.begin(), owner->pending_.end(), pending.get()),
                        owner->pending_.end());
  pending->callback(result);
}

} // namespace ace::app
