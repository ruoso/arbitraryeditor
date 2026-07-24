// The editor's ENCODE line (D-export-3, A20): the ONE translation unit in the whole
// build that instantiates the vendored `stb_image_write.h`.
//
// Nothing in the tree could be reused — libarbc is decode-only and says encoding is
// the host's job (`arbc/runtime/offline_sequence.hpp:68`), and the only PNG writer in
// the build ships inside `imgui_test_engine`, reachable only from ImGui-linked targets
// (L3/L4 under the A8 seam) with its `stbi_*` symbols static to one .cpp. So the
// editor vendors one single-header, public-domain encoder at `third_party/stb/`,
// linked with the header-only-PRIVATE idiom `CMakeLists.txt` already documents for
// nlohmann on `ace_project`, and CONTAINED by `scripts/check_levels.py`'s
// `EXTERNAL_ALLOWED["stb_write"] = {"commands"}` — so the containment is CI-enforced
// rather than conventional.
//
// `STBI_WRITE_NO_STDIO` is what makes A3 structural rather than aspirational: with no
// `FILE*` entry point compiled in, the encoder CANNOT bypass `platform::FileSystem`.
// Every byte leaves through `stbi_write_png_to_func` into a caller-owned vector.
#include <ace/commands/export.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb/stb_image_write.h>

namespace ace::commands {
namespace {

// stb's PNG knobs are mutable globals with implicit defaults. Pin them to NAMED
// constants here so a vendored-header bump that moves a default shows up as a
// deliberate diff rather than as a silently regenerated `.png` golden (D-export-11).
constexpr int k_png_compression_level = 8; // zlib level stb's deflate targets
constexpr int k_png_filter = -1;           // -1 = stb's per-scanline adaptive heuristic

// Applied exactly once, thread-safely, via a function-local static: the globals are
// process-wide, so assigning them on every encode would be a benign-but-reported data
// race the moment two encodes ever overlap.
int pin_stb_png_settings() {
  stbi_write_png_compression_level = k_png_compression_level;
  stbi_write_force_png_filter = k_png_filter;
  return 0;
}

void append_bytes(void* context, void* data, int size) {
  auto* out = static_cast<std::vector<std::uint8_t>*>(context);
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  out->insert(out->end(), bytes, bytes + size);
}

} // namespace

std::vector<std::uint8_t> encode_png(const Srgb8Image& image) {
  std::vector<std::uint8_t> out;
  if (image.width <= 0 || image.height <= 0) {
    return out;
  }
  const std::size_t expected =
      static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4;
  // A buffer that does not match its own declared geometry is a caller bug, and
  // handing it to stb would read past the end. Errors are values (Constraint 4): an
  // empty result, never a malformed file and never a crash.
  if (image.pixels.size() != expected) {
    return out;
  }

  static const int pinned = pin_stb_png_settings();
  (void)pinned;

  out.reserve(expected / 2);
  // 4 components = RGBA8, straight alpha — exactly `Srgb8Image`'s layout — and a
  // stride of w*4, since the buffer is tightly packed with no padding.
  const int ok = stbi_write_png_to_func(&append_bytes, &out, image.width, image.height, 4,
                                        image.pixels.data(), image.width * 4);
  if (ok == 0) {
    out.clear();
  }
  return out;
}

} // namespace ace::commands
