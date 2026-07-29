#pragma once

// editor.cameras.truetype_captions (A27, D-truetype_captions-2): the bundled Inter
// Regular face, embedded as a linked `const` byte array by a CMake build step
// (`cmake/embed_font.cmake` reads `assets/fonts/Inter-Regular.ttf` and emits the
// generated definition into the build dir). Declared here so the ONE `commands` TU that
// instantiates `stb_truetype` can point the rasterizer at compile-time data — no runtime
// file I/O, nothing for a headless offline job to fail to find, exactly as the retired
// `constexpr` glyph table was compile-time data.
//
// The definitions are `extern const` (external linkage) in the generated TU; the array
// is never read at run time from `assets/` — that path is a build INPUT only.

#include <cstdint>

namespace ace::commands {

extern const std::uint8_t k_inter_regular_ttf[];
extern const unsigned int k_inter_regular_ttf_size;

} // namespace ace::commands
