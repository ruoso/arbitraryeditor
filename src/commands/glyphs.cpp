// editor.cameras.contact_sheet — the embedded bitmap glyph table (D-sheet-4, A21),
// widened to printable Latin-1 by `editor.cameras.caption_latin1` (D-latin1-2).
//
// A headless L1 job has no font: §8 makes `views`/`dock` "the ONLY layer that sees
// ImGui", so ImGui's atlas is structurally unreachable from `commands`, and A20's
// ledger says the editor vendors exactly ONE encode dependency — which stays true
// only if captions cost no second external. So the caption face is DATA: 191 cells
// covering U+0020..U+007E and U+00A0..U+00FF, a 5x7 glyph in a 6x8 box, one byte per
// row with the glyph's five columns in the LOW FIVE BITS, leftmost column = bit 4.
// 1528 bytes in TWO contiguous blocks — a second array rather than one widened one, so
// the shipped ASCII bytes below are provably untouched and `[0x7F, 0x9F]` (DEL and the
// C1 controls) stays unmapped. Compile-time constant, no rasterizer, no `assets/`
// directory to ship and locate at runtime, and no second entry in A20's
// one-vendored-encode-dependency ledger.
//
// This TU is pure data — no branches, no arcs — so gcov attributes nothing to it.
// `text_width` / `fit_text` / `draw_text` (the code that READS the table) live in
// contact_sheet.cpp, where they are covered by tests/contact_sheet_test.cpp.

#include <ace/commands/contact_sheet.hpp>

#include <array>
#include <cstdint>

namespace ace::commands {

// One row per scanline, `k_glyph_cell_height` rows per cell (the 5x7 face plus the
// cell's blank bottom row, where a bottom-row shadow lands), in code-point order
// from U+0020. Written as binary literals so the face is readable AS a face: each
// literal's `1` bits are the lit pixels of that scanline, left to right.
//
// Formatting is pinned below because the table IS a picture: eight literals per line
// is one glyph per line, which is what makes a wrong bit reviewable at all. A comment
// inside a braced init list makes clang-format re-flow to one literal per line — a
// 900-line wall nobody can check against the face it claims to be.
// clang-format off
const std::array<std::uint8_t, k_glyph_count * k_glyph_cell_height> k_glyph_table = {{
    // 0x20  space
    0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,
    // 0x21  !
    0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00000, 0b00100, 0b00000,
    // 0x22  "
    0b01010, 0b01010, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,
    // 0x23  #
    0b01010, 0b01010, 0b11111, 0b01010, 0b11111, 0b01010, 0b01010, 0b00000,
    // 0x24  $
    0b00100, 0b01111, 0b10100, 0b01110, 0b00101, 0b11110, 0b00100, 0b00000,
    // 0x25  %
    0b11000, 0b11001, 0b00010, 0b00100, 0b01000, 0b10011, 0b00011, 0b00000,
    // 0x26  &
    0b01100, 0b10010, 0b10100, 0b01000, 0b10101, 0b10010, 0b01101, 0b00000,
    // 0x27  '
    0b00100, 0b00100, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,
    // 0x28  (
    0b00010, 0b00100, 0b01000, 0b01000, 0b01000, 0b00100, 0b00010, 0b00000,
    // 0x29  )
    0b01000, 0b00100, 0b00010, 0b00010, 0b00010, 0b00100, 0b01000, 0b00000,
    // 0x2A  *
    0b00000, 0b10101, 0b01110, 0b11111, 0b01110, 0b10101, 0b00000, 0b00000,
    // 0x2B  +
    0b00000, 0b00100, 0b00100, 0b11111, 0b00100, 0b00100, 0b00000, 0b00000,
    // 0x2C  ,
    0b00000, 0b00000, 0b00000, 0b00000, 0b01100, 0b01100, 0b01000, 0b00000,
    // 0x2D  -
    0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000, 0b00000,
    // 0x2E  .
    0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b01100, 0b01100, 0b00000,
    // 0x2F  /
    0b00000, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b00000, 0b00000,
    // 0x30  0
    0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110, 0b00000,
    // 0x31  1
    0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110, 0b00000,
    // 0x32  2
    0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111, 0b00000,
    // 0x33  3
    0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110, 0b00000,
    // 0x34  4
    0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010, 0b00000,
    // 0x35  5
    0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110, 0b00000,
    // 0x36  6
    0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0x37  7
    0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000, 0b00000,
    // 0x38  8
    0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0x39  9
    0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100, 0b00000,
    // 0x3A  :
    0b00000, 0b01100, 0b01100, 0b00000, 0b01100, 0b01100, 0b00000, 0b00000,
    // 0x3B  ;
    0b00000, 0b01100, 0b01100, 0b00000, 0b01100, 0b01100, 0b01000, 0b00000,
    // 0x3C  <
    0b00010, 0b00100, 0b01000, 0b10000, 0b01000, 0b00100, 0b00010, 0b00000,
    // 0x3D  =
    0b00000, 0b00000, 0b11111, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000,
    // 0x3E  >
    0b01000, 0b00100, 0b00010, 0b00001, 0b00010, 0b00100, 0b01000, 0b00000,
    // 0x3F  ?
    0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b00000, 0b00100, 0b00000,
    // 0x40  @
    0b01110, 0b10001, 0b00001, 0b01101, 0b10101, 0b10101, 0b01111, 0b00000,
    // 0x41  A
    0b00100, 0b01010, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b00000,
    // 0x42  B
    0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110, 0b00000,
    // 0x43  C
    0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110, 0b00000,
    // 0x44  D
    0b11100, 0b10010, 0b10001, 0b10001, 0b10001, 0b10010, 0b11100, 0b00000,
    // 0x45  E
    0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111, 0b00000,
    // 0x46  F
    0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000, 0b00000,
    // 0x47  G
    0b01111, 0b10000, 0b10000, 0b10011, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0x48  H
    0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001, 0b00000,
    // 0x49  I
    0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110, 0b00000,
    // 0x4A  J
    0b00111, 0b00010, 0b00010, 0b00010, 0b00010, 0b10010, 0b01100, 0b00000,
    // 0x4B  K
    0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001, 0b00000,
    // 0x4C  L
    0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111, 0b00000,
    // 0x4D  M
    0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001, 0b00000,
    // 0x4E  N
    0b10001, 0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b00000,
    // 0x4F  O
    0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0x50  P
    0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000, 0b00000,
    // 0x51  Q
    0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101, 0b00000,
    // 0x52  R
    0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001, 0b00000,
    // 0x53  S
    0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110, 0b00000,
    // 0x54  T
    0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00000,
    // 0x55  U
    0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0x56  V
    0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100, 0b00000,
    // 0x57  W
    0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001, 0b00000,
    // 0x58  X
    0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001, 0b00000,
    // 0x59  Y
    0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100, 0b00000,
    // 0x5A  Z
    0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111, 0b00000,
    // 0x5B  [
    0b01110, 0b01000, 0b01000, 0b01000, 0b01000, 0b01000, 0b01110, 0b00000,
    // 0x5C  backslash
    0b00000, 0b10000, 0b01000, 0b00100, 0b00010, 0b00001, 0b00000, 0b00000,
    // 0x5D  ]
    0b01110, 0b00010, 0b00010, 0b00010, 0b00010, 0b00010, 0b01110, 0b00000,
    // 0x5E  ^
    0b00100, 0b01010, 0b10001, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,
    // 0x5F  _
    0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b11111, 0b00000,
    // 0x60  `
    0b01000, 0b00100, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,
    // 0x61  a
    0b00000, 0b00000, 0b01110, 0b00001, 0b01111, 0b10001, 0b01111, 0b00000,
    // 0x62  b
    0b10000, 0b10000, 0b11110, 0b10001, 0b10001, 0b10001, 0b11110, 0b00000,
    // 0x63  c
    0b00000, 0b00000, 0b01111, 0b10000, 0b10000, 0b10000, 0b01111, 0b00000,
    // 0x64  d
    0b00001, 0b00001, 0b01111, 0b10001, 0b10001, 0b10001, 0b01111, 0b00000,
    // 0x65  e
    0b00000, 0b00000, 0b01110, 0b10001, 0b11111, 0b10000, 0b01110, 0b00000,
    // 0x66  f
    0b00110, 0b01000, 0b01000, 0b11110, 0b01000, 0b01000, 0b01000, 0b00000,
    // 0x67  g
    0b00000, 0b01111, 0b10001, 0b10001, 0b01111, 0b00001, 0b01110, 0b00000,
    // 0x68  h
    0b10000, 0b10000, 0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b00000,
    // 0x69  i
    0b00100, 0b00000, 0b01100, 0b00100, 0b00100, 0b00100, 0b01110, 0b00000,
    // 0x6A  j
    0b00010, 0b00000, 0b00110, 0b00010, 0b00010, 0b10010, 0b01100, 0b00000,
    // 0x6B  k
    0b10000, 0b10000, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b00000,
    // 0x6C  l
    0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110, 0b00000,
    // 0x6D  m
    0b00000, 0b00000, 0b11010, 0b10101, 0b10101, 0b10001, 0b10001, 0b00000,
    // 0x6E  n
    0b00000, 0b00000, 0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b00000,
    // 0x6F  o
    0b00000, 0b00000, 0b01110, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0x70  p
    0b00000, 0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b00000,
    // 0x71  q
    0b00000, 0b01111, 0b10001, 0b10001, 0b01111, 0b00001, 0b00001, 0b00000,
    // 0x72  r
    0b00000, 0b00000, 0b10110, 0b11000, 0b10000, 0b10000, 0b10000, 0b00000,
    // 0x73  s
    0b00000, 0b00000, 0b01111, 0b10000, 0b01110, 0b00001, 0b11110, 0b00000,
    // 0x74  t
    0b01000, 0b01000, 0b11110, 0b01000, 0b01000, 0b01001, 0b00110, 0b00000,
    // 0x75  u
    0b00000, 0b00000, 0b10001, 0b10001, 0b10001, 0b10001, 0b01111, 0b00000,
    // 0x76  v
    0b00000, 0b00000, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100, 0b00000,
    // 0x77  w
    0b00000, 0b00000, 0b10001, 0b10001, 0b10101, 0b10101, 0b01010, 0b00000,
    // 0x78  x
    0b00000, 0b00000, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b00000,
    // 0x79  y
    0b00000, 0b10001, 0b10001, 0b10001, 0b01111, 0b00001, 0b01110, 0b00000,
    // 0x7A  z
    0b00000, 0b00000, 0b11111, 0b00010, 0b00100, 0b01000, 0b11111, 0b00000,
    // 0x7B  {
    0b00011, 0b00100, 0b00100, 0b01000, 0b00100, 0b00100, 0b00011, 0b00000,
    // 0x7C  |
    0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00000,
    // 0x7D  }
    0b11000, 0b00100, 0b00100, 0b00010, 0b00100, 0b00100, 0b11000, 0b00000,
    // 0x7E  ~
    0b00000, 0b01001, 0b10101, 0b10010, 0b00000, 0b00000, 0b00000, 0b00000,
}};

// The fallback face for any code point the tables do not map (D-sheet-4): one hollow
// box per unmapped RUN, so a CJK name does not explode into one box per character.
const std::array<std::uint8_t, k_glyph_cell_height> k_fallback_glyph = {{
    0b11111, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11111, 0b00000}};

// The SECOND block: U+00A0..U+00FF, the printable Latin-1 Supplement
// (`editor.cameras.caption_latin1`, D-latin1-2/D-latin1-3). Same contract as the ASCII
// block above — 5x7 face in a 6x8 cell, low five bits, leftmost = bit 4, row 7 blank
// because that is where a set bit's shadow lands.
//
// The letterforms are COMPOSED, not free-hand: an accented lowercase letter is its
// ASCII base with the diacritic on row 0 (the x-height forms already sit in rows 2-6);
// an accented uppercase letter is its ASCII base compressed into rows 1-6 with the
// diacritic on row 0. The diacritic rows are fixed — grave 0b01000, acute 0b00010,
// circumflex 0b00100, tilde 0b01101, diaeresis 0b01010, ring 0b01110 — which is what
// makes a wrong cell reviewable (check the accent row, check the base against its ASCII
// cell) instead of 768 binary literals of art. Two exceptions the rule cannot express:
// a CEDILLA hangs below, so `Ç`/`ç` compress upward and put the hook on row 6; and an
// accented `i` is DOTLESS, because the ASCII `i`'s dot is itself 0b00100 on row 0 and a
// circumflex over it would be the same eight bytes.
//
// U+00A0 (NBSP) and U+00AD (SHY) are MAPPED BUT BLANK — zero ink, full advance, exactly
// like U+0020: they are space/format characters, and drawing anything for them would be
// a lie. No other cell is blank, and the unit tests assert exactly that.
const std::array<std::uint8_t, k_latin1_glyph_count * k_glyph_cell_height> k_latin1_glyph_table = {{
    // 0xA0  no-break space (blank by decision)
    0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,
    // 0xA1  inverted !
    0b00100, 0b00000, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00000,
    // 0xA2  cent
    0b00100, 0b01111, 0b10100, 0b10100, 0b10100, 0b11110, 0b00100, 0b00000,
    // 0xA3  pound
    0b00110, 0b01001, 0b01000, 0b11110, 0b01000, 0b01000, 0b11111, 0b00000,
    // 0xA4  currency
    0b00000, 0b10001, 0b01110, 0b01010, 0b01110, 0b10001, 0b00000, 0b00000,
    // 0xA5  yen
    0b10001, 0b01010, 0b00100, 0b11111, 0b00100, 0b11111, 0b00100, 0b00000,
    // 0xA6  broken bar
    0b00100, 0b00100, 0b00100, 0b00000, 0b00100, 0b00100, 0b00100, 0b00000,
    // 0xA7  section
    0b01110, 0b10000, 0b01110, 0b10001, 0b01110, 0b00001, 0b01110, 0b00000,
    // 0xA8  diaeresis
    0b01010, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,
    // 0xA9  copyright
    0b01110, 0b10001, 0b10111, 0b10100, 0b10111, 0b10001, 0b01110, 0b00000,
    // 0xAA  feminine ordinal
    0b01110, 0b00010, 0b01110, 0b10010, 0b01110, 0b00000, 0b01110, 0b00000,
    // 0xAB  left guillemet
    0b00000, 0b00101, 0b01010, 0b10100, 0b01010, 0b00101, 0b00000, 0b00000,
    // 0xAC  not sign
    0b00000, 0b00000, 0b11111, 0b00001, 0b00001, 0b00000, 0b00000, 0b00000,
    // 0xAD  soft hyphen (blank by decision)
    0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,
    // 0xAE  registered
    0b01110, 0b10001, 0b11101, 0b10101, 0b11101, 0b10101, 0b01110, 0b00000,
    // 0xAF  macron
    0b11111, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,
    // 0xB0  degree
    0b01110, 0b01010, 0b01110, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,
    // 0xB1  plus-minus
    0b00100, 0b00100, 0b11111, 0b00100, 0b00100, 0b00000, 0b11111, 0b00000,
    // 0xB2  superscript 2
    0b01100, 0b00010, 0b00100, 0b01000, 0b01110, 0b00000, 0b00000, 0b00000,
    // 0xB3  superscript 3
    0b01100, 0b00010, 0b00100, 0b00010, 0b01100, 0b00000, 0b00000, 0b00000,
    // 0xB4  acute accent
    0b00010, 0b00100, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,
    // 0xB5  micro
    0b00000, 0b00000, 0b10001, 0b10001, 0b10001, 0b11110, 0b10000, 0b00000,
    // 0xB6  pilcrow
    0b01111, 0b11010, 0b11010, 0b01010, 0b00010, 0b00010, 0b00010, 0b00000,
    // 0xB7  middle dot
    0b00000, 0b00000, 0b00000, 0b00100, 0b00000, 0b00000, 0b00000, 0b00000,
    // 0xB8  cedilla
    0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00100, 0b00110, 0b00000,
    // 0xB9  superscript 1
    0b00100, 0b01100, 0b00100, 0b00100, 0b01110, 0b00000, 0b00000, 0b00000,
    // 0xBA  masculine ordinal
    0b01110, 0b10001, 0b10001, 0b01110, 0b00000, 0b00000, 0b01110, 0b00000,
    // 0xBB  right guillemet
    0b00000, 0b10100, 0b01010, 0b00101, 0b01010, 0b10100, 0b00000, 0b00000,
    // 0xBC  one quarter
    0b01000, 0b01001, 0b01010, 0b00100, 0b01010, 0b10011, 0b00001, 0b00000,
    // 0xBD  one half
    0b01000, 0b01001, 0b01010, 0b00100, 0b01011, 0b10001, 0b00011, 0b00000,
    // 0xBE  three quarters
    0b11000, 0b00101, 0b01010, 0b00100, 0b01010, 0b10011, 0b00001, 0b00000,
    // 0xBF  inverted ?
    0b00100, 0b00000, 0b00100, 0b01000, 0b10000, 0b10001, 0b01110, 0b00000,
    // 0xC0  A grave
    0b01000, 0b00100, 0b01010, 0b10001, 0b11111, 0b10001, 0b10001, 0b00000,
    // 0xC1  A acute
    0b00010, 0b00100, 0b01010, 0b10001, 0b11111, 0b10001, 0b10001, 0b00000,
    // 0xC2  A circumflex
    0b00100, 0b00100, 0b01010, 0b10001, 0b11111, 0b10001, 0b10001, 0b00000,
    // 0xC3  A tilde
    0b01101, 0b00100, 0b01010, 0b10001, 0b11111, 0b10001, 0b10001, 0b00000,
    // 0xC4  A diaeresis
    0b01010, 0b00100, 0b01010, 0b10001, 0b11111, 0b10001, 0b10001, 0b00000,
    // 0xC5  A ring
    0b01110, 0b00100, 0b01010, 0b10001, 0b11111, 0b10001, 0b10001, 0b00000,
    // 0xC6  AE
    0b01111, 0b10100, 0b10100, 0b11110, 0b10100, 0b10100, 0b10111, 0b00000,
    // 0xC7  C cedilla
    0b01110, 0b10001, 0b10000, 0b10000, 0b10001, 0b01110, 0b00110, 0b00000,
    // 0xC8  E grave
    0b01000, 0b11111, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111, 0b00000,
    // 0xC9  E acute
    0b00010, 0b11111, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111, 0b00000,
    // 0xCA  E circumflex
    0b00100, 0b11111, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111, 0b00000,
    // 0xCB  E diaeresis
    0b01010, 0b11111, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111, 0b00000,
    // 0xCC  I grave
    0b01000, 0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110, 0b00000,
    // 0xCD  I acute
    0b00010, 0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110, 0b00000,
    // 0xCE  I circumflex
    0b00100, 0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110, 0b00000,
    // 0xCF  I diaeresis
    0b01010, 0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110, 0b00000,
    // 0xD0  Eth
    0b11100, 0b10010, 0b10001, 0b11101, 0b10001, 0b10010, 0b11100, 0b00000,
    // 0xD1  N tilde
    0b01101, 0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b00000,
    // 0xD2  O grave
    0b01000, 0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0xD3  O acute
    0b00010, 0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0xD4  O circumflex
    0b00100, 0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0xD5  O tilde
    0b01101, 0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0xD6  O diaeresis
    0b01010, 0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0xD7  multiplication
    0b00000, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b00000, 0b00000,
    // 0xD8  O slash
    0b01110, 0b10011, 0b10101, 0b10101, 0b11001, 0b10001, 0b01110, 0b00000,
    // 0xD9  U grave
    0b01000, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0xDA  U acute
    0b00010, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0xDB  U circumflex
    0b00100, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0xDC  U diaeresis
    0b01010, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0xDD  Y acute
    0b00010, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100, 0b00000,
    // 0xDE  Thorn
    0b10000, 0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b00000,
    // 0xDF  sharp s
    0b01100, 0b10010, 0b10010, 0b10100, 0b10010, 0b10001, 0b10110, 0b00000,
    // 0xE0  a grave
    0b01000, 0b00000, 0b01110, 0b00001, 0b01111, 0b10001, 0b01111, 0b00000,
    // 0xE1  a acute
    0b00010, 0b00000, 0b01110, 0b00001, 0b01111, 0b10001, 0b01111, 0b00000,
    // 0xE2  a circumflex
    0b00100, 0b00000, 0b01110, 0b00001, 0b01111, 0b10001, 0b01111, 0b00000,
    // 0xE3  a tilde
    0b01101, 0b00000, 0b01110, 0b00001, 0b01111, 0b10001, 0b01111, 0b00000,
    // 0xE4  a diaeresis
    0b01010, 0b00000, 0b01110, 0b00001, 0b01111, 0b10001, 0b01111, 0b00000,
    // 0xE5  a ring
    0b01110, 0b00000, 0b01110, 0b00001, 0b01111, 0b10001, 0b01111, 0b00000,
    // 0xE6  ae
    0b00000, 0b00000, 0b01110, 0b00101, 0b01111, 0b10100, 0b01111, 0b00000,
    // 0xE7  c cedilla
    0b00000, 0b01111, 0b10000, 0b10000, 0b10000, 0b01111, 0b00110, 0b00000,
    // 0xE8  e grave
    0b01000, 0b00000, 0b01110, 0b10001, 0b11111, 0b10000, 0b01110, 0b00000,
    // 0xE9  e acute
    0b00010, 0b00000, 0b01110, 0b10001, 0b11111, 0b10000, 0b01110, 0b00000,
    // 0xEA  e circumflex
    0b00100, 0b00000, 0b01110, 0b10001, 0b11111, 0b10000, 0b01110, 0b00000,
    // 0xEB  e diaeresis
    0b01010, 0b00000, 0b01110, 0b10001, 0b11111, 0b10000, 0b01110, 0b00000,
    // 0xEC  i grave (dotless base)
    0b01000, 0b00000, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110, 0b00000,
    // 0xED  i acute (dotless base)
    0b00010, 0b00000, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110, 0b00000,
    // 0xEE  i circumflex (dotless base)
    0b00100, 0b00000, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110, 0b00000,
    // 0xEF  i diaeresis (dotless base)
    0b01010, 0b00000, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110, 0b00000,
    // 0xF0  eth
    0b01010, 0b00100, 0b01110, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0xF1  n tilde
    0b01101, 0b00000, 0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b00000,
    // 0xF2  o grave
    0b01000, 0b00000, 0b01110, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0xF3  o acute
    0b00010, 0b00000, 0b01110, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0xF4  o circumflex
    0b00100, 0b00000, 0b01110, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0xF5  o tilde
    0b01101, 0b00000, 0b01110, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0xF6  o diaeresis
    0b01010, 0b00000, 0b01110, 0b10001, 0b10001, 0b10001, 0b01110, 0b00000,
    // 0xF7  division
    0b00000, 0b00100, 0b00000, 0b11111, 0b00000, 0b00100, 0b00000, 0b00000,
    // 0xF8  o slash
    0b00000, 0b00000, 0b01110, 0b10011, 0b10101, 0b11001, 0b01110, 0b00000,
    // 0xF9  u grave
    0b01000, 0b00000, 0b10001, 0b10001, 0b10001, 0b10001, 0b01111, 0b00000,
    // 0xFA  u acute
    0b00010, 0b00000, 0b10001, 0b10001, 0b10001, 0b10001, 0b01111, 0b00000,
    // 0xFB  u circumflex
    0b00100, 0b00000, 0b10001, 0b10001, 0b10001, 0b10001, 0b01111, 0b00000,
    // 0xFC  u diaeresis
    0b01010, 0b00000, 0b10001, 0b10001, 0b10001, 0b10001, 0b01111, 0b00000,
    // 0xFD  y acute
    0b00010, 0b10001, 0b10001, 0b10001, 0b01111, 0b00001, 0b01110, 0b00000,
    // 0xFE  thorn
    0b10000, 0b10000, 0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b00000,
    // 0xFF  y diaeresis
    0b01010, 0b10001, 0b10001, 0b10001, 0b01111, 0b00001, 0b01110, 0b00000,
}};
// clang-format on

} // namespace ace::commands
