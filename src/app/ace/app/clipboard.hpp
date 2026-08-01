#pragma once

#include <optional>
#include <string>

namespace ace::app {

// An ENCODED image read off the system clipboard (editor.import.paste / A30 / D-paste-4): the
// encoded bytes (PNG/JPEG/… — whatever imdec decodes, symmetric with import) plus the mime type
// they were offered under. NOT raw pixels: v1 accepts encoded clipboard images only, so a
// raw-RGBA-only clipboard is `nullopt` (a graceful no-op), never re-encoded here.
struct ClipboardImage {
  std::string bytes;
  std::string mime;
};

// The clipboard-read seam (docs/01-architecture.md A12 mould / D-paste-3), the exact inversion of
// the `FileDialog` seam: `AppProjectGateway` drives this ABSTRACT interface for `paste_image()`,
// the concrete `SdlClipboard` wraps SDL3's `SDL_GetClipboardData` (SDL is L4-only, A8), and tests
// inject a scriptable fake so the paste verb is exercised without a real OS clipboard (none exists
// headless). Synchronous: the clipboard read runs on the UI (event-pump) thread inside the verb.
class Clipboard {
public:
  virtual ~Clipboard() = default;

  // Read an ENCODED image off the clipboard, or `nullopt` when there is nothing pasteable — an
  // empty clipboard, a text-only clipboard, or a raw-pixels-only clipboard (D-paste-4). A no-op is
  // a value, never an error: `paste_image()` inserts no cell and raises no dialog.
  virtual std::optional<ClipboardImage> read_image() = 0;
};

// The SDL-backed clipboard over `SDL_GetClipboardData` (the only SDL in this seam, L4).
// Deliberately thin and NOT unit-tested — the offscreen driver has no clipboard — so the testable
// paste logic lives in the gateway verb and the scripted-fake e2e, not here (the `SdlFileDialog`
// precedent).
class SdlClipboard final : public Clipboard {
public:
  SdlClipboard() = default;

  std::optional<ClipboardImage> read_image() override;
};

} // namespace ace::app
