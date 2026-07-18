#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

struct SDL_Window;

namespace ace::app {

// The native folder-picker seam (docs/01-architecture.md A12 / Constraint 5).
// AppProjectGateway drives this ABSTRACT interface; the concrete SdlFolderDialog
// wraps SDL3's async SDL_ShowOpenFolderDialog (SDL is L4-only, A8), and tests
// inject a scriptable fake so the gateway logic is exercised without a real OS
// dialog. The pick is asynchronous: `show` returns immediately and the callback
// fires on a later UI-thread frame.
class FolderDialog {
public:
  using Callback = std::function<void(std::optional<std::filesystem::path>)>;

  virtual ~FolderDialog() = default;

  // Open the OS folder picker. Returns immediately; `on_pick` is invoked on a
  // later frame with the chosen directory, or nullopt on cancel/error.
  virtual void show(Callback on_pick) = 0;
};

// The SDL-backed native folder dialog (the only SDL in this leaf, L4). Each
// in-flight pick is a heap node whose ownership transfers to the SDL callback; on
// destruction the dialog detaches any live node so a callback that fires after
// teardown neither dangles nor leaks (shutdown-cancel, Constraint 5). Single-
// threaded: SDL delivers the result on the event-pumping (UI) thread, so no
// cross-thread synchronization is needed (TSan scope: no race across the deferred
// callback).
class SdlFolderDialog final : public FolderDialog {
public:
  explicit SdlFolderDialog(SDL_Window* parent = nullptr);
  ~SdlFolderDialog() override;
  SdlFolderDialog(const SdlFolderDialog&) = delete;
  SdlFolderDialog& operator=(const SdlFolderDialog&) = delete;

  void show(Callback on_pick) override;

private:
  struct Pending;
  static void on_dialog(void* userdata, const char* const* filelist, int filter);

  SDL_Window* parent_;
  std::vector<Pending*> pending_;
};

} // namespace ace::app
