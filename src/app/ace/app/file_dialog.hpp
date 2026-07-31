#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

struct SDL_Window;

namespace ace::app {

// The native FILE-picker seam (docs/01-architecture.md A12 / editor.import.image D-image-4).
// The exact `FolderDialog` mould: AppProjectGateway drives this ABSTRACT interface for the
// "Place image…" affordance; the concrete SdlFileDialog wraps SDL3's async
// SDL_ShowOpenFileDialog (SDL is L4-only, A8), and tests inject a scriptable fake so the import
// verb is exercised without a real OS dialog (no OS dialog exists headless). The pick is
// asynchronous: `show` returns immediately and the callback fires on a later UI-thread frame.
class FileDialog {
public:
  using Callback = std::function<void(std::optional<std::filesystem::path>)>;

  virtual ~FileDialog() = default;

  // Open the OS file picker. Returns immediately; `on_pick` is invoked on a later frame with
  // the chosen file, or nullopt on cancel/error.
  virtual void show(Callback on_pick) = 0;
};

// The SDL-backed native file dialog (the only SDL in this seam, L4). Each in-flight pick is a
// heap node whose ownership transfers to the SDL callback; on destruction the dialog detaches
// any live node so a callback that fires after teardown neither dangles nor leaks
// (shutdown-cancel, the FolderDialog precedent). Single-threaded: SDL delivers the result on the
// event-pumping (UI) thread, so no cross-thread synchronization is needed (TSan scope: no race
// across the deferred callback). Deliberately thin and NOT unit-tested — the offscreen driver
// has no file picker — so the testable import logic lives in the gateway verb, not here.
class SdlFileDialog final : public FileDialog {
public:
  explicit SdlFileDialog(SDL_Window* parent = nullptr);
  ~SdlFileDialog() override;
  SdlFileDialog(const SdlFileDialog&) = delete;
  SdlFileDialog& operator=(const SdlFileDialog&) = delete;

  void show(Callback on_pick) override;

private:
  struct Pending;
  static void on_dialog(void* userdata, const char* const* filelist, int filter);

  SDL_Window* parent_;
  std::vector<Pending*> pending_;
};

} // namespace ace::app
