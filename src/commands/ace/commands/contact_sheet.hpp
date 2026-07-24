#pragma once

// editor.cameras.contact_sheet — tile every ticked camera into ONE PNG (D14, A21).
//
// The sheet is the cheap realization of D6's shot map: `ceil(sqrt(N))` columns of
// square tile boxes, each camera's render aspect-fitted and centred inside its box,
// thin gutters between them, a small caption strip under each box carrying the
// camera's name, all on the background the Export panel's own option selects.
//
// Two invariants make this header as small as it is:
//
//   * A TILE IS A RENDER, NOT A RESAMPLE (D-sheet-1). The fitted tile size is
//     computed FIRST, and the tile is produced by exactly one call to A20's already
//     injected `RenderFn` at that size — the same operation the shipped N x
//     multiplier performs in the other direction. So there is no image filter here,
//     no mip chain, no `arbc/media/image_resampler.hpp` include and no new L2 entry
//     point; `commands` keeps its shipped dependency set exactly.
//   * COMPOSITION COPIES, IT NEVER BLENDS (D-sheet-2). The sheet is allocated filled
//     with the chosen background, tile rects never overlap, and every tile was
//     rendered with the SAME background the sheet is filled with — so under a tile
//     the destination is either fully transparent or exactly that tile's own opaque
//     backing, `over` degenerates to `replace`, and the correct composite is reached
//     with ZERO colour arithmetic in L1. That is directly testable as a byte-identity
//     between each tile rect and an independent render of the same camera, which any
//     later filter, premultiply or blend breaks immediately.
//
// Captions come from an embedded bitmap glyph table (`src/commands/glyphs.cpp`,
// D-sheet-4) covering ASCII and the printable Latin-1 Supplement (D-latin1-2), walked
// as DECODED CODE POINTS (D-latin1-1) because a camera name is free text carrying UTF-8
// bytes — because §8 makes `views`/`dock` the ONLY layer that sees ImGui and this
// is a headless L1 job: there is no font atlas to borrow. Glyphs are 1-bit cells
// scaled by integer pixel replication and written as opaque white over an opaque
// black shadow — legible on light, dark, mid-grey and transparent backgrounds
// WITHOUT analysing the background, which is what keeps colour math out of
// `commands` and keeps a caption a pure pixel write under D-sheet-2.
//
// `plan_contact_sheet` lives in <ace/commands/export.hpp>: it needs `ExportOptions`
// and `ShotCameraFn`, and that header includes this one so `ExportPlan` can carry the
// sheet as its second phase.

#include <ace/base/image.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ace::commands {

using Srgb8Image = base::Srgb8Image;
using Rgba8 = base::Rgba8;

// ---- the caption face (D-sheet-4) -------------------------------------------

// A 5x7 glyph in a 6x8 cell: the spare column and the spare row are the advance
// padding, and the spare row is exactly where a bottom-row shadow lands, which is
// what makes a caption strip of `k_glyph_cell_height * scale` pixels sufficient.
inline constexpr int k_glyph_width = 5;
inline constexpr int k_glyph_cell_width = 6;
inline constexpr int k_glyph_cell_height = 8;
// U+0020 .. U+007E, the printable ASCII range.
inline constexpr int k_glyph_count = 95;
inline constexpr char32_t k_glyph_first = U' ';
inline constexpr char32_t k_glyph_last = U'~';
// U+00A0 .. U+00FF, the printable Latin-1 Supplement (`editor.cameras.caption_latin1`,
// A21 as amended). A SECOND contiguous block rather than one widened array: the
// shipped ASCII bytes stay byte-identical, and `[0x7F, 0x9F]` — DEL and the C1
// controls — stays deliberately UNMAPPED, so a control character still boxes.
inline constexpr int k_latin1_glyph_count = 96;
inline constexpr char32_t k_latin1_first = char32_t{0x00A0};
inline constexpr char32_t k_latin1_last = char32_t{0x00FF};

// The tables themselves, defined in `src/commands/glyphs.cpp` — one byte per scanline,
// the glyph's five columns in the LOW FIVE BITS, leftmost column = bit 4.
extern const std::array<std::uint8_t, k_glyph_count * k_glyph_cell_height> k_glyph_table;
extern const std::array<std::uint8_t, k_latin1_glyph_count * k_glyph_cell_height>
    k_latin1_glyph_table;
// The hollow box drawn for any code point the tables do not map. One box per unmapped
// RUN, so a CJK name does not explode into one box per character.
extern const std::array<std::uint8_t, k_glyph_cell_height> k_fallback_glyph;

// The code point an ill-formed maximal subpart decodes to. Itself unmapped, so
// malformed input joins the same fallback run as any other undrawable text.
inline constexpr char32_t k_replacement_code_point = 0xFFFDU;

// Decode the scalar at `pos` and advance `pos` past it. Total: an ill-formed sequence
// yields `k_replacement_code_point` and consumes exactly one maximal subpart, so `pos`
// strictly increases on every call, nothing is read past `text.size()`, and the walk
// terminates on every input including truncated, overlong, surrogate-encoding and
// out-of-range sequences (D-latin1-1).
char32_t next_code_point(std::string_view text, std::size_t& pos);

// How many CELLS `text` draws as. `text` is decoded as UTF-8: one cell per mapped code
// point, and one per maximal run of unmapped ones. `"Café"` (5 UTF-8 bytes) is four
// cells — `C`, `a`, `f`, `é` — while `"中文"` is one box (D-latin1-4).
int text_cells(std::string_view text);

// The pixel width `draw_text` occupies for `text` at `scale`. Zero for a
// non-positive scale.
int text_width(std::string_view text, int scale);

// The exact number of GLYPH (white) pixels `draw_text` writes for `text` at scale 1
// — the table's set-bit count over the cells `text` actually draws. The measure the
// caption's anti-vacuity law is asserted against.
int text_set_bits(std::string_view text);

// The longest prefix of `text` that fits `max_width` pixels at `scale`, with a
// trailing `...` when anything was dropped (Constraint 4: a caption never leaves its
// tile column). Empty when not even one cell fits. The cut lands on a CODE-POINT
// boundary, so valid UTF-8 in is valid UTF-8 out (D-latin1-5).
std::string fit_text(std::string_view text, int max_width, int scale);

// Draw `text` into `target` with its top-left cell at (x, y), integer-scaled by
// nearest-neighbour pixel replication: every set bit's opaque black shadow at
// (+scale, +scale) first, then every set bit as opaque white. Clipped to `target` —
// out-of-bounds pixels are dropped, never wrapped and never a write past the end.
// No antialiasing, no alpha ramp, no read of what is underneath (D-sheet-4).
void draw_text(Srgb8Image& target, int x, int y, std::string_view text, int scale);

// ---- the layout (D-sheet-3) --------------------------------------------------

// The fixed separation between cells, and around the sheet's border. A constant
// rather than a second knob: its only effect is cosmetic.
inline constexpr int k_contact_gutter = 8;
// The ONE layout knob's bounds. Below the minimum a caption is unreadable at any
// scale; above the maximum the sheet reaches `k_max_export_bytes` at a handful of
// cameras and the refusal, not the knob, would be doing the work.
inline constexpr int k_contact_tile_min = 32;
inline constexpr int k_contact_tile_max = 1024;
inline constexpr int k_contact_tile_default = 256;

// The sheet stem, before `sanitize_stem` / `take_unique_stem` (D-sheet-6). It yields
// to a camera that already took it: the camera's name is the USER's data, the
// sheet's is the editor's choice, so the sheet is the one that can move.
inline constexpr std::string_view k_contact_sheet_stem = "contact-sheet";

// Every number the sheet's geometry needs, a pure function of `(count, tile_edge)` —
// which is what makes the whole layout Catch2-testable with no document at all.
struct ContactSheetLayout {
  int count = 0;
  int cols = 0;
  int rows = 0;
  int tile_edge = 0;     // the square tile BOX edge, already clamped
  int caption_scale = 1; // the glyph replication factor
  int caption_height = 0;
  int cell_width = 0;  // == tile_edge
  int cell_height = 0; // tile_edge + caption_height
  int width = 0;       // the whole sheet
  int height = 0;

  friend bool operator==(const ContactSheetLayout&, const ContactSheetLayout&) = default;
};

// `cols = ceil(sqrt(count))`, `rows = ceil(count / cols)` — so the grid is as square
// as it can be and the trailing row is never wholly empty. `tile_edge` is clamped to
// [k_contact_tile_min, k_contact_tile_max]; a non-positive `count` is all zeroes.
ContactSheetLayout contact_sheet_layout(int count, int tile_edge);

// One cell's rect (the tile box PLUS its caption strip) in sheet coordinates.
struct ContactCellRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  friend bool operator==(const ContactCellRect&, const ContactCellRect&) = default;
};

// The cell rect of `index` (row-major). An out-of-range index is a zero rect.
ContactCellRect contact_sheet_cell(const ContactSheetLayout& layout, int index);

// ---- the plan ----------------------------------------------------------------

// One camera's slot on the sheet. `width`/`height` are the FITTED render size, so
// `render_camera` is `shot_camera(frame, res.w, res.h, width, height)` — the
// injected derivation at the tile's own output box, exactly as the batch phase
// derives at the item's output box (D-sheet-1).
struct ContactTile {
  arbc::ObjectId camera{};
  std::string camera_name;
  std::string caption; // `camera_name` already fitted to the tile width
  int box_x = 0;       // the tile BOX origin
  int box_y = 0;
  int x = 0; // the fitted render's origin — centred inside the box
  int y = 0;
  int width = 0; // the fitted render size
  int height = 0;
  int caption_x = 0;
  int caption_y = 0;
  arbc::Affine render_camera = arbc::Affine::identity();

  friend bool operator==(const ContactTile&, const ContactTile&) = default;
};

// The second phase's plan. A refused sheet is still a PLAN (Constraint 8 / D-sheet-7):
// it carries the reason and the dimensions it would have needed, and the batch phase
// still runs.
struct ContactSheetPlan {
  std::vector<ContactTile> tiles;
  int width = 0;
  int height = 0;
  int cols = 0;
  int rows = 0;
  int tile_edge = 0;
  int caption_scale = 1;
  std::filesystem::path path;
  bool refused = false;
  std::string reason; // empty exactly when the sheet is composable

  friend bool operator==(const ContactSheetPlan&, const ContactSheetPlan&) = default;
};

// Compose the sheet: allocate `plan.width x plan.height` filled with `background`
// (`{0,0,0,0}` when nullopt), COPY each render into its tile rect, then draw each
// caption. `renders[i]` belongs to `plan.tiles[i]`; a missing, empty or
// wrongly-sized render leaves its tile at background rather than throwing
// (Constraint 9). Returns an empty image for a refused or degenerate plan.
//
// No blend, no filter, no premultiply — `std::copy` per row and opaque pixel writes
// for the glyphs (D-sheet-2).
Srgb8Image compose_contact_sheet(const ContactSheetPlan& plan,
                                 const std::vector<Srgb8Image>& renders,
                                 const std::optional<Rgba8>& background);

} // namespace ace::commands
