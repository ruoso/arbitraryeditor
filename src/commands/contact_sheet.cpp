#include <ace/commands/contact_sheet.hpp>
#include <ace/commands/export.hpp>
#include <ace/commands/inter_regular_ttf.hpp>
#include <ace/scene/camera.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/media/pixel_traits.hpp>
#include <arbc/runtime/document.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The ONE translation unit in the build that instantiates the vendored TrueType
// rasterizer (A27, D-truetype_captions-2), mirroring png_encode.cpp's stb_image_write
// containment: reached through the PRIVATE `third_party` include dir, pinned by
// `scripts/check_levels.py`'s `EXTERNAL_ALLOWED["stb_truetype"] = {"commands"}`.
// STBTT_STATIC keeps every stb symbol internal to this TU. `#pragma STDC FP_CONTRACT
// OFF` is what makes the LINEAR caption composite bit-reproducible across gcc/clang: it
// forbids the compiler from fusing a `mul + add` into an FMA, which the two toolchains
// contract differently, and the two goldens are byte-exact with no tolerance
// (Constraint 10).
#pragma STDC FP_CONTRACT OFF
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

namespace ace::commands {
namespace {

// The bundled Inter face, parsed once from the embedded blob (function-local static, so
// the parse is thread-safe and happens on first caption draw). `stbtt_fontinfo` holds
// only offsets and a pointer into `k_inter_regular_ttf`, whose storage is static and
// outlives every use — so returning it by value is a POD copy of those offsets.
const stbtt_fontinfo& face() {
  static const stbtt_fontinfo info = [] {
    stbtt_fontinfo f{};
    stbtt_InitFont(&f, k_inter_regular_ttf, stbtt_GetFontOffsetForIndex(k_inter_regular_ttf, 0));
    return f;
  }();
  return info;
}

// The rasterization scale for `scale`: the face's em box mapped to
// `k_caption_pixel_height * scale` pixels. A fixed pixel height at a fixed sub-pixel
// policy is what keeps the raster deterministic and platform-independent (Constraint 10).
float pixel_scale(int scale) {
  return stbtt_ScaleForPixelHeight(&face(), static_cast<float>(k_caption_pixel_height * scale));
}

// The horizontal advance for one code point in pixels, rounded once. Uncovered code
// points map to glyph 0 (`.notdef`) inside stb, so they advance by the face's `.notdef`
// advance — no special case here (D-truetype_captions-5/6).
int advance_px(char32_t code_point, float sf) {
  int advance = 0;
  int left_side_bearing = 0;
  stbtt_GetCodepointHMetrics(&face(), static_cast<int>(code_point), &advance, &left_side_bearing);
  return static_cast<int>(std::lround(static_cast<float>(advance) * sf));
}

// Composite one 8-bit coverage sample of a grey ink over the destination pixel `at` in
// the LINEAR working space, re-encoding to straight-alpha sRGB8 — the premultiplied-
// linear `over` `render` uses (D-truetype_captions-3). `ink_linear` is 0 (black shadow)
// or 1 (white text). A coverage edge is an alpha blend, not a copy, which is exactly why
// A21 clause (2)'s copy-only rule is narrowed to the tile image and text composites here.
void composite_coverage(Srgb8Image& target, std::size_t at, std::uint8_t coverage,
                        float ink_linear) {
  // The caller only composites covered samples (`coverage != 0`), so `src_a > 0` and
  // therefore `out_a >= src_a > 0` — the divide below is always well-defined.
  const float src_a = static_cast<float>(coverage) / 255.0F;
  const float inv = 1.0F - src_a;
  const float dst_a = arbc::unorm8_decode(target.pixels[at + 3]);
  const float out_a = src_a + dst_a * inv;
  const float src_premul = ink_linear * src_a;
  for (std::size_t channel = 0; channel < 3; ++channel) {
    const float dst_premul = arbc::srgb8_to_linear(target.pixels[at + channel]) * dst_a;
    const float out_premul = src_premul + dst_premul * inv;
    target.pixels[at + channel] = arbc::linear_to_srgb8(out_premul / out_a);
  }
  target.pixels[at + 3] = arbc::unorm8_encode(out_a);
}

// The long edge becomes `tile_edge`; the short edge follows by rounded integer
// division, floored at 1 so a pathological aspect still has a pixel to render into.
// Deterministic by construction — no floating point anywhere in the layout.
void fit_inside(int tile_edge, int native_w, int native_h, int& fit_w, int& fit_h) {
  if (native_w <= 0 || native_h <= 0 || tile_edge <= 0) {
    fit_w = 0;
    fit_h = 0;
    return;
  }
  const std::int64_t edge = tile_edge;
  if (native_w >= native_h) {
    fit_w = tile_edge;
    fit_h =
        static_cast<int>(std::max<std::int64_t>(1, (edge * native_h + native_w / 2) / native_w));
  } else {
    fit_h = tile_edge;
    fit_w =
        static_cast<int>(std::max<std::int64_t>(1, (edge * native_w + native_h / 2) / native_h));
  }
}

// One tile's geometry inside its cell: the box, the centred fitted render, and the
// caption strip below with the name already truncated to the tile's own width
// (Constraint 4 — a caption never leaves its column).
void place_contact_tile(const ContactSheetLayout& layout, int index, int native_w, int native_h,
                        ContactTile& tile) {
  const ContactCellRect cell = contact_sheet_cell(layout, index);
  tile.box_x = cell.x;
  tile.box_y = cell.y;
  fit_inside(layout.tile_edge, native_w, native_h, tile.width, tile.height);
  tile.x = cell.x + (layout.tile_edge - tile.width) / 2;
  tile.y = cell.y + (layout.tile_edge - tile.height) / 2;
  tile.caption_x = cell.x;
  tile.caption_y = cell.y + layout.tile_edge;
  tile.caption = fit_text(tile.camera_name, layout.tile_edge, layout.caption_scale);
}

} // namespace

// ---- the caption face --------------------------------------------------------

char32_t next_code_point(std::string_view text, std::size_t& pos) {
  // A call past the end still ADVANCES: `pos` strictly increasing is the property that
  // makes every walk over this function provably terminate, misuse included.
  if (pos >= text.size()) {
    ++pos;
    return k_replacement_code_point;
  }
  const auto byte_at = [&text](std::size_t at) { return static_cast<unsigned char>(text[at]); };
  const unsigned char lead = byte_at(pos);
  if (lead < 0x80U) {
    ++pos;
    return static_cast<char32_t>(lead);
  }

  // The Unicode 3.9 well-formed byte-sequence table, spelled as a per-lead range for
  // the FIRST continuation byte. Encoding it here — rather than decoding freely and
  // range-checking afterwards — is what rejects overlongs (0xC0/0xC1, 0xE0 0x80),
  // surrogate encodings (0xED 0xA0) and out-of-range scalars (0xF4 0x90, 0xF5+)
  // WITHOUT ever accumulating a value the table could be indexed with.
  unsigned int length = 0;
  char32_t value = 0;
  unsigned char low = 0x80U;
  unsigned char high = 0xBFU;
  if (lead >= 0xC2U && lead <= 0xDFU) {
    length = 2;
    value = static_cast<char32_t>(lead & 0x1FU);
  } else if (lead >= 0xE0U && lead <= 0xEFU) {
    length = 3;
    value = static_cast<char32_t>(lead & 0x0FU);
    if (lead == 0xE0U) {
      low = 0xA0U; // no overlong three-byte form
    } else if (lead == 0xEDU) {
      high = 0x9FU; // no surrogate encoding
    }
  } else if (lead >= 0xF0U && lead <= 0xF4U) {
    length = 4;
    value = static_cast<char32_t>(lead & 0x07U);
    if (lead == 0xF0U) {
      low = 0x90U; // no overlong four-byte form
    } else if (lead == 0xF4U) {
      high = 0x8FU; // nothing past U+10FFFF
    }
  } else {
    // A lone continuation byte (0x80..0xBF), an overlong lead (0xC0/0xC1) or a byte no
    // UTF-8 sequence can start (0xF5..0xFF): a maximal subpart of exactly one byte.
    ++pos;
    return k_replacement_code_point;
  }

  for (unsigned int i = 1; i < length; ++i) {
    // Truncated tail or a byte outside the permitted range: the maximal subpart is
    // everything ACCEPTED so far, and the offending byte is left for the next call to
    // classify on its own terms.
    if (pos + i >= text.size() || byte_at(pos + i) < low || byte_at(pos + i) > high) {
      pos += i;
      return k_replacement_code_point;
    }
    value = (value << 6) | static_cast<char32_t>(byte_at(pos + i) & 0x3FU);
    low = 0x80U;
    high = 0xBFU;
  }
  pos += length;
  return value;
}

int text_width(std::string_view text, int scale) {
  if (scale <= 0) {
    return 0;
  }
  const float sf = pixel_scale(scale);
  int width = 0;
  std::size_t pos = 0;
  while (pos < text.size()) {
    width += advance_px(next_code_point(text, pos), sf);
  }
  return width;
}

std::string fit_text(std::string_view text, int max_width, int scale) {
  if (scale <= 0 || max_width <= 0) {
    return {};
  }
  if (text_width(text, scale) <= max_width) {
    return std::string(text);
  }
  // Truncation is VISIBLE: the ellipsis is what tells the user the name continues. If not
  // even the ellipsis fits, the honest answer is nothing (width 0, still <= max_width),
  // which keeps `text_width(result) <= max_width` a total law (Constraint 4).
  const int ellipsis = text_width("...", scale);
  if (ellipsis > max_width) {
    return {};
  }
  const float sf = pixel_scale(scale);
  const int budget = max_width - ellipsis;
  int used = 0;
  std::size_t take = 0;
  std::size_t pos = 0;
  // The walk advances by decoded CODE POINT and `take` is the byte offset AFTER the last
  // accepted one, so the cut can never land inside a multi-byte sequence: valid UTF-8 in
  // is valid UTF-8 out, and the result measures `used + ellipsis <= max_width` because
  // advances are additive per code point with no kerning (D-latin1-5).
  while (pos < text.size()) {
    const int advance = advance_px(next_code_point(text, pos), sf);
    if (used + advance > budget) {
      break;
    }
    used += advance;
    take = pos;
  }
  return std::string(text.substr(0, take)) + "...";
}

void draw_text(Srgb8Image& target, int x, int y, std::string_view text, int scale) {
  if (scale <= 0 || target.width <= 0 || target.height <= 0 ||
      target.pixels.size() !=
          static_cast<std::size_t>(target.width) * static_cast<std::size_t>(target.height) * 4) {
    return;
  }
  const float sf = pixel_scale(scale);
  int ascent = 0;
  int descent = 0;
  int line_gap = 0;
  stbtt_GetFontVMetrics(&face(), &ascent, &descent, &line_gap);
  // The baseline sits `ascent` pixels below the strip top, so the tallest covered glyph
  // reaches the strip top and no higher; every write is clipped to the strip and to the
  // text's own advance span, so no ink leaves the caption strip (Constraint 7).
  const int baseline = y + static_cast<int>(std::lround(static_cast<float>(ascent) * sf));
  const int strip_x0 = x;
  const int strip_x1 = x + text_width(text, scale);
  const int strip_y0 = y;
  const int strip_y1 = y + k_glyph_cell_height * scale;
  // Two passes, shadow then white, so a later glyph's shadow never composites over an
  // earlier glyph's white text (D-truetype_captions-3, keeping the D-sheet-4 pair).
  for (int pass = 0; pass < 2; ++pass) {
    const float ink = pass == 0 ? 0.0F : 1.0F;
    const int offset = pass == 0 ? scale : 0;
    int pen = x;
    std::size_t pos = 0;
    while (pos < text.size()) {
      const char32_t code_point = next_code_point(text, pos);
      int glyph_w = 0;
      int glyph_h = 0;
      int glyph_x = 0;
      int glyph_y = 0;
      unsigned char* bitmap = stbtt_GetCodepointBitmap(
          &face(), sf, sf, static_cast<int>(code_point), &glyph_w, &glyph_h, &glyph_x, &glyph_y);
      if (bitmap != nullptr) {
        for (int row = 0; row < glyph_h; ++row) {
          const int ty = baseline + glyph_y + row + offset;
          if (ty < strip_y0 || ty >= strip_y1 || ty < 0 || ty >= target.height) {
            continue;
          }
          for (int col = 0; col < glyph_w; ++col) {
            const std::uint8_t coverage =
                bitmap[static_cast<std::size_t>(row) * static_cast<std::size_t>(glyph_w) +
                       static_cast<std::size_t>(col)];
            if (coverage == 0) {
              continue;
            }
            const int tx = pen + glyph_x + col + offset;
            if (tx < strip_x0 || tx >= strip_x1 || tx < 0 || tx >= target.width) {
              continue;
            }
            const std::size_t at =
                (static_cast<std::size_t>(ty) * static_cast<std::size_t>(target.width) +
                 static_cast<std::size_t>(tx)) *
                4;
            composite_coverage(target, at, coverage, ink);
          }
        }
        stbtt_FreeBitmap(bitmap, nullptr);
      }
      pen += advance_px(code_point, sf);
    }
  }
}

// ---- the layout --------------------------------------------------------------

ContactSheetLayout contact_sheet_layout(int count, int tile_edge) {
  ContactSheetLayout layout;
  if (count <= 0) {
    return layout;
  }
  layout.count = count;
  layout.tile_edge = std::clamp(tile_edge, k_contact_tile_min, k_contact_tile_max);
  // `ceil(sqrt(count))` by integer search — no `std::sqrt`, so the grid cannot shift
  // under a different libm's rounding and the layout stays bit-reproducible.
  int cols = 1;
  while (cols * cols < count) {
    ++cols;
  }
  layout.cols = cols;
  layout.rows = (count + cols - 1) / cols;
  // The caption has to be legible at the size the user chose, and nothing else
  // governs that: one replication step per 128px of tile, capped so a 1024px tile
  // does not get a caption taller than the thumbnail is interesting.
  layout.caption_scale = std::clamp(layout.tile_edge / 128, 1, 4);
  layout.caption_height = k_glyph_cell_height * layout.caption_scale;
  layout.cell_width = layout.tile_edge;
  layout.cell_height = layout.tile_edge + layout.caption_height;
  layout.width = k_contact_gutter + layout.cols * (layout.cell_width + k_contact_gutter);
  layout.height = k_contact_gutter + layout.rows * (layout.cell_height + k_contact_gutter);
  return layout;
}

ContactCellRect contact_sheet_cell(const ContactSheetLayout& layout, int index) {
  ContactCellRect rect;
  if (layout.cols <= 0 || index < 0 || index >= layout.count) {
    return rect;
  }
  const int row = index / layout.cols;
  const int col = index % layout.cols;
  rect.x = k_contact_gutter + col * (layout.cell_width + k_contact_gutter);
  rect.y = k_contact_gutter + row * (layout.cell_height + k_contact_gutter);
  rect.width = layout.cell_width;
  rect.height = layout.cell_height;
  return rect;
}

// ---- composition -------------------------------------------------------------

Srgb8Image compose_contact_sheet(const ContactSheetPlan& plan,
                                 const std::vector<Srgb8Image>& renders,
                                 const std::optional<Rgba8>& background) {
  Srgb8Image sheet;
  if (plan.tiles.empty() || plan.width <= 0 || plan.height <= 0) {
    return sheet;
  }
  const std::size_t pixels =
      static_cast<std::size_t>(plan.width) * static_cast<std::size_t>(plan.height);
  sheet.width = plan.width;
  sheet.height = plan.height;
  sheet.pixels.resize(pixels * 4);
  // Allocated FILLED with the chosen background — transparent black by default. Every
  // gutter pixel, every letterbox pixel inside a non-square tile's box and every empty
  // slot of a partial last row is exactly this value and is never touched again.
  const Rgba8 fill = background.value_or(Rgba8{0, 0, 0, 0});
  for (std::size_t i = 0; i < pixels; ++i) {
    sheet.pixels[i * 4] = fill.r;
    sheet.pixels[i * 4 + 1] = fill.g;
    sheet.pixels[i * 4 + 2] = fill.b;
    sheet.pixels[i * 4 + 3] = fill.a;
  }

  for (std::size_t i = 0; i < plan.tiles.size(); ++i) {
    const ContactTile& tile = plan.tiles[i];
    if (i < renders.size()) {
      const Srgb8Image& render = renders[i];
      const std::size_t expected =
          static_cast<std::size_t>(tile.width) * static_cast<std::size_t>(tile.height) * 4;
      // A render that degraded (empty, or the wrong size) leaves its tile at
      // background and costs the sheet nothing else — errors are values (Constraint 9).
      if (render.width == tile.width && render.height == tile.height && tile.width > 0 &&
          tile.height > 0 && render.pixels.size() == expected) {
        for (int row = 0; row < tile.height; ++row) {
          const int dest_y = tile.y + row;
          if (dest_y < 0 || dest_y >= sheet.height) {
            continue;
          }
          const int x0 = std::max(0, tile.x);
          const int x1 = std::min(sheet.width, tile.x + tile.width);
          if (x1 <= x0) {
            continue;
          }
          const std::size_t src =
              (static_cast<std::size_t>(row) * tile.width + static_cast<std::size_t>(x0 - tile.x)) *
              4;
          const std::size_t dst =
              (static_cast<std::size_t>(dest_y) * sheet.width + static_cast<std::size_t>(x0)) * 4;
          // A COPY, not a composite: the destination under a tile is either fully
          // transparent or exactly this tile's own opaque backing, so `over`
          // degenerates to `replace` and no colour arithmetic is needed (D-sheet-2).
          std::copy(render.pixels.begin() + static_cast<std::ptrdiff_t>(src),
                    render.pixels.begin() +
                        static_cast<std::ptrdiff_t>(src + static_cast<std::size_t>(x1 - x0) * 4),
                    sheet.pixels.begin() + static_cast<std::ptrdiff_t>(dst));
        }
      }
    }
    draw_text(sheet, tile.caption_x, tile.caption_y, tile.caption, plan.caption_scale);
  }
  return sheet;
}

// ---- the plan ----------------------------------------------------------------

ContactSheetPlan plan_contact_sheet(const arbc::Document& document,
                                    const std::vector<arbc::ObjectId>& selected,
                                    const ExportOptions& options, const ShotCameraFn& shot_camera,
                                    const std::vector<std::string>& taken_stems) {
  ContactSheetPlan plan;
  plan.refused = true; // until a composable sheet is proven
  // Refuse rather than guess (D23), in exactly the batch phase's words.
  if (selected.empty()) {
    plan.reason = "No cameras selected — tick at least one camera to export.";
    return plan;
  }
  if (options.destination.empty()) {
    plan.reason = "No destination directory.";
    return plan;
  }
  if (!shot_camera) {
    plan.reason = "No render-camera derivation installed.";
    return plan;
  }

  // Layer order, over the lock-free `pin()` reader seam — a read, so nothing is
  // posted to the writer (D15 / Constraint 13). The sheet is planned over the TICKED
  // set, not over the batch's surviving subset: a camera the batch refused for
  // exceeding the byte budget at N x still belongs on the map of what was asked for
  // (Constraint 7), and at tile resolution it costs a few hundred kilobytes.
  std::vector<scene::Camera> tiled;
  for (const scene::Camera& camera : scene::cameras(document)) {
    if (std::find(selected.begin(), selected.end(), camera.id) != selected.end()) {
      tiled.push_back(camera);
    }
  }
  if (tiled.empty()) {
    plan.reason = "None of the selected cameras still exist.";
    return plan;
  }

  const ContactSheetLayout layout =
      contact_sheet_layout(static_cast<int>(tiled.size()), options.tile_edge);
  plan.width = layout.width;
  plan.height = layout.height;
  plan.cols = layout.cols;
  plan.rows = layout.rows;
  plan.tile_edge = layout.tile_edge;
  plan.caption_scale = layout.caption_scale;

  // The same refuse-as-a-value budget the batch applies per item (D-export-4 /
  // D-sheet-7): name the resource, name the requested size, refuse the ONE thing —
  // the batch phase still runs and still writes its files.
  const std::int64_t bytes =
      static_cast<std::int64_t>(layout.width) * static_cast<std::int64_t>(layout.height) * 4;
  if (bytes > k_max_export_bytes) {
    plan.reason = "Contact sheet at " + std::to_string(layout.width) + "x" +
                  std::to_string(layout.height) + " needs " + std::to_string(bytes) +
                  " bytes, over the " + std::to_string(k_max_export_bytes) + "-byte limit.";
    return plan;
  }

  // D-sheet-6: the sheet's stem is the EDITOR's choice, so it is the one that moves.
  // Seeding `take_unique_stem` with the batch plan's stems is what makes a camera
  // literally named `contact-sheet` keep `contact-sheet.png`.
  std::vector<std::string> used = taken_stems;
  const std::string stem = take_unique_stem(sanitize_stem(k_contact_sheet_stem, 0), used);
  plan.path = options.destination / (stem + ".png");

  plan.tiles.reserve(tiled.size());
  for (std::size_t i = 0; i < tiled.size(); ++i) {
    const scene::Camera& camera = tiled[i];
    ContactTile tile;
    tile.camera = camera.id;
    tile.camera_name = camera.name;
    place_contact_tile(layout, static_cast<int>(i), camera.resolution.width,
                       camera.resolution.height, tile);
    if (tile.width > 0 && tile.height > 0) {
      // The injected derivation at the TILE's own output box — the identical call the
      // batch makes at the item's output box, with a smaller box instead of a larger
      // one. This is D-sheet-1 in one line: a tile is a render, not a downscale.
      tile.render_camera = shot_camera(camera.frame, camera.resolution.width,
                                       camera.resolution.height, tile.width, tile.height);
    }
    plan.tiles.push_back(std::move(tile));
  }
  plan.refused = false;
  return plan;
}

} // namespace ace::commands
