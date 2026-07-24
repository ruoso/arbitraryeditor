#include <ace/commands/contact_sheet.hpp>
#include <ace/commands/export.hpp>
#include <ace/scene/camera.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/runtime/document.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ace::commands {
namespace {

// The one place a CODE POINT becomes a cell. Everything outside the two table ranges
// — a control character, a DEL, the C1 controls, a CJK ideograph, an emoji, and
// U+FFFD for ill-formed input — is UNMAPPED, and a maximal run of unmapped code points
// collapses to ONE fallback box (D-sheet-4, kept by D-latin1-4).
bool is_mapped(char32_t code_point) {
  return (code_point >= k_glyph_first && code_point <= k_glyph_last) ||
         (code_point >= k_latin1_first && code_point <= k_latin1_last);
}

// The 8 scanlines of the cell `code_point` draws as. Two range tests are the ONLY
// mapping, so no value can index outside a table (Constraint 3).
const std::uint8_t* glyph_rows(char32_t code_point) {
  if (code_point >= k_glyph_first && code_point <= k_glyph_last) {
    const std::size_t index = static_cast<std::size_t>(code_point - k_glyph_first);
    return k_glyph_table.data() + index * k_glyph_cell_height;
  }
  if (code_point >= k_latin1_first && code_point <= k_latin1_last) {
    const std::size_t index = static_cast<std::size_t>(code_point - k_latin1_first);
    return k_latin1_glyph_table.data() + index * k_glyph_cell_height;
  }
  return k_fallback_glyph.data();
}

// Walk `text` as cells, calling `visit(rows, cell_index)` once per drawn cell. The ONE
// iteration primitive every public caption function routes through — which is why
// decoding here fixes measurement, truncation and drawing at once (D-latin1-1).
template <class Visit> int for_each_cell(std::string_view text, Visit visit) {
  int cells = 0;
  bool in_unmapped_run = false;
  std::size_t pos = 0;
  while (pos < text.size()) {
    const char32_t code_point = next_code_point(text, pos);
    if (is_mapped(code_point)) {
      in_unmapped_run = false;
    } else if (in_unmapped_run) {
      continue; // still inside the SAME run — one box covers the whole of it
    } else {
      in_unmapped_run = true;
    }
    visit(glyph_rows(code_point), cells);
    ++cells;
  }
  return cells;
}

// Fill an axis-aligned rect with one opaque colour, clipped to `target`. The only
// pixel-writing primitive the captions use: no read of what is underneath, so no
// blend can hide here (D-sheet-2).
void fill_rect(Srgb8Image& target, int x, int y, int w, int h, std::uint8_t value) {
  const int x0 = std::max(0, x);
  const int y0 = std::max(0, y);
  const int x1 = std::min(target.width, x + w);
  const int y1 = std::min(target.height, y + h);
  for (int row = y0; row < y1; ++row) {
    for (int col = x0; col < x1; ++col) {
      const std::size_t at = (static_cast<std::size_t>(row) * target.width + col) * 4;
      target.pixels[at] = value;
      target.pixels[at + 1] = value;
      target.pixels[at + 2] = value;
      target.pixels[at + 3] = 255;
    }
  }
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

int text_cells(std::string_view text) {
  return for_each_cell(text, [](const std::uint8_t*, int) {});
}

int text_width(std::string_view text, int scale) {
  if (scale <= 0) {
    return 0;
  }
  return text_cells(text) * k_glyph_cell_width * scale;
}

int text_set_bits(std::string_view text) {
  int bits = 0;
  for_each_cell(text, [&bits](const std::uint8_t* rows, int) {
    for (int row = 0; row < k_glyph_cell_height; ++row) {
      for (int col = 0; col < k_glyph_width; ++col) {
        if ((rows[row] & (1U << (k_glyph_width - 1 - col))) != 0U) {
          ++bits;
        }
      }
    }
  });
  return bits;
}

std::string fit_text(std::string_view text, int max_width, int scale) {
  if (scale <= 0 || max_width <= 0) {
    return {};
  }
  const int budget = max_width / (k_glyph_cell_width * scale); // cells that fit
  if (budget <= 0) {
    return {};
  }
  if (text_cells(text) <= budget) {
    return std::string(text);
  }
  // Truncation is VISIBLE: the ellipsis is what tells the user the name continues.
  // With room for nothing else, the ellipsis alone is still the honest answer.
  if (budget <= 3) {
    return std::string(static_cast<std::size_t>(budget), '.');
  }
  const int keep = budget - 3;
  int cells = 0;
  bool in_unmapped_run = false;
  std::size_t take = 0;
  std::size_t pos = 0;
  // The walk advances by decoded CODE POINT and `take` is the byte offset AFTER the
  // last accepted one, so the cut can never land inside a multi-byte sequence: valid
  // UTF-8 in is valid UTF-8 out, and re-measuring the result agrees with the cell count
  // this loop computed (D-latin1-5).
  while (pos < text.size()) {
    const bool mapped = is_mapped(next_code_point(text, pos));
    const int add = (mapped || !in_unmapped_run) ? 1 : 0;
    if (cells + add > keep) {
      break;
    }
    cells += add;
    in_unmapped_run = !mapped;
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
  // Two passes, shadow then glyph, so a later cell's shadow can never eat an earlier
  // cell's white pixel: the white count is then exactly `text_set_bits * scale^2`,
  // which is what the caption's anti-vacuity law asserts.
  for (int pass = 0; pass < 2; ++pass) {
    const std::uint8_t value = pass == 0 ? std::uint8_t{0} : std::uint8_t{255};
    const int offset = pass == 0 ? scale : 0;
    for_each_cell(text, [&](const std::uint8_t* rows, int cell) {
      const int cell_x = x + cell * k_glyph_cell_width * scale + offset;
      const int cell_y = y + offset;
      for (int row = 0; row < k_glyph_cell_height; ++row) {
        for (int col = 0; col < k_glyph_width; ++col) {
          if ((rows[row] & (1U << (k_glyph_width - 1 - col))) == 0U) {
            continue;
          }
          fill_rect(target, cell_x + col * scale, cell_y + row * scale, scale, scale, value);
        }
      }
    });
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
