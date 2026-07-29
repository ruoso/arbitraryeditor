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
//   * THE TILE IMAGE COPIES, IT NEVER BLENDS (D-sheet-2, narrowed by
//     D-truetype_captions-4). The sheet is allocated filled with the chosen background,
//     tile rects never overlap, and every tile was rendered with the SAME background the
//     sheet is filled with — so under a tile the destination is either fully transparent
//     or exactly that tile's own opaque backing, `over` degenerates to `replace`, and
//     each tile rect is reached with ZERO colour arithmetic. That is directly testable
//     as a byte-identity between each tile rect and an independent render of the same
//     camera, which any later filter, premultiply or blend breaks immediately. Only the
//     CAPTION text composites (below) — the copy-only law is scoped to the tile image.
//
// Captions rasterize a build-embedded TrueType face — Inter Regular (static `glyf`,
// OFL 1.1), turned into a linked `const` byte array by a CMake step and rasterized by
// vendored `stb/stb_truetype.h` (A27, D-truetype_captions-1/2). §8 makes `views`/`dock`
// the ONLY layer that sees ImGui, so a headless L1 job has no font atlas to borrow; an
// embedded face is the dependency-free, background-blind way to draw real text in
// `commands`, and it is walked as DECODED CODE POINTS (D-latin1-1) because a camera name
// is free UTF-8 text. `stb_truetype` yields an 8-bit COVERAGE bitmap per glyph, so a
// caption is an antialiased composite of coverage over the strip background in the LINEAR
// working space (libarbc's `srgb8_to_linear`/`linear_to_srgb8`, the transfer `render`
// already uses), keeping the white-glyph + black-shadow legibility pair
// (D-truetype_captions-3). An uncovered code point renders the face's own `.notdef`, so
// the missing-glyph box is the font's business, not a hand-drawn table's
// (D-truetype_captions-5).
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

// ---- the caption face (A27, D-truetype_captions-1) --------------------------

// The caption strip is `k_glyph_cell_height * caption_scale` pixels tall (the layout
// below). The bundled face is rasterized at `k_caption_pixel_height * scale` pixels so a
// glyph's em box PLUS the one-pixel-per-scale shadow offset fits inside that strip:
// 7 < 8 leaves exactly the shadow row (D-truetype_captions-6). `k_glyph_cell_height` is
// kept from the retired table only to define the strip geometry — the glyph SHAPES are
// now the font's, not a hand-drawn cell's.
inline constexpr int k_glyph_cell_height = 8;
inline constexpr int k_caption_pixel_height = 7;

// The code point an ill-formed maximal subpart decodes to (Unicode 3.9). It is outside
// the face's coverage, so malformed input renders the face's `.notdef`, exactly as any
// other uncovered code point does (D-truetype_captions-5).
inline constexpr char32_t k_replacement_code_point = 0xFFFDU;

// Decode the scalar at `pos` and advance `pos` past it. Total: an ill-formed sequence
// yields `k_replacement_code_point` and consumes exactly one maximal subpart, so `pos`
// strictly increases on every call, nothing is read past `text.size()`, and the walk
// terminates on every input including truncated, overlong, surrogate-encoding and
// out-of-range sequences (D-latin1-1).
char32_t next_code_point(std::string_view text, std::size_t& pos);

// The pixel width `draw_text` occupies for `text` at `scale`: the sum of the face's
// scaled horizontal advances over the decoded code points. Zero for the empty string or
// a non-positive scale, monotone non-decreasing as text is appended, and the exact
// measure `fit_text` respects so a fitted caption re-measures consistently
// (D-truetype_captions-6).
int text_width(std::string_view text, int scale);

// The longest prefix of `text` that fits `max_width` pixels at `scale`, with a trailing
// `...` when anything was dropped (Constraint 4: a caption never leaves its tile column).
// Empty when not even the ellipsis fits. The cut lands on a CODE-POINT boundary, so valid
// UTF-8 in is valid UTF-8 out (D-latin1-5), and `text_width(result, scale)` is never
// greater than `max_width`.
std::string fit_text(std::string_view text, int max_width, int scale);

// Draw `text` into `target` with its caption strip's top-left at (x, y). Each decoded
// code point is rasterized through the embedded face at `k_caption_pixel_height * scale`
// pixels and composited as 8-bit COVERAGE over the destination in the LINEAR working
// space: a black shadow at (+scale, +scale) first, then white text, each an antialiased
// alpha composite (D-truetype_captions-3). An uncovered code point — and U+FFFD from
// malformed input — renders the face's own `.notdef` (D-truetype_captions-5). Writes are
// clipped to the `k_glyph_cell_height * scale` strip and to `[x, x + text_width)`, so no
// ink leaves the strip (Constraint 7); a `target` whose buffer does not match its
// declared geometry is left untouched.
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
