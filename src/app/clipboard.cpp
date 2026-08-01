#include <ace/app/clipboard.hpp>

#include <SDL3/SDL.h>

#include <cstddef>

namespace ace::app {

namespace {

// Preference order for an encoded clipboard image (D-paste-4): PNG first (what most apps place),
// then the common encoded mimes imdec decodes. Raw-pixel mimes are deliberately ABSENT — v1 does
// not re-encode raw RGBA (no encoder vendored), so a raw-only clipboard falls through to `nullopt`.
constexpr const char* k_image_mimes[] = {"image/png", "image/jpeg", "image/jpg", "image/bmp",
                                         "image/gif", "image/webp", "image/tiff"};

} // namespace

// SDL/OS glue with no headless driver (the offscreen lane has no OS clipboard), exactly like
// `SdlFileDialog::show` — the paste LOGIC it feeds (the gateway verb + the scripted-fake e2e) is
// covered directly. Excluded from the coverage gate on that basis, mirroring `main.cpp`'s glue.
// GCOVR_EXCL_START
std::optional<ClipboardImage> SdlClipboard::read_image() {
  for (const char* mime : k_image_mimes) {
    if (!SDL_HasClipboardData(mime)) {
      continue;
    }
    std::size_t size = 0;
    void* data = SDL_GetClipboardData(mime, &size);
    if (data == nullptr || size == 0) {
      SDL_free(data);
      continue;
    }
    ClipboardImage image;
    image.mime = mime;
    image.bytes.assign(static_cast<const char*>(data), size);
    SDL_free(data);
    return image;
  }
  return std::nullopt; // nothing pasteable — a graceful no-op, never an error (Constraint 7)
}
// GCOVR_EXCL_STOP

} // namespace ace::app
