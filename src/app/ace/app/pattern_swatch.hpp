#pragma once

#include <ace/app/accent.hpp>
#include <ace/interact/interact.hpp> // OverviewPattern / HatchSegment / hatch_segments

#include <arbc/base/geometry.hpp>

#include <imgui.h>

#include <algorithm>

namespace ace::app {

// The shared list↔overview pattern-swatch inking + draw (editor.panels.hatch_swatch /
// D-hatch_swatch-6). Promoted out of `overview_panel.cpp`'s anonymous namespace so the Layers-list
// swatch and the overview box ink from ONE table — the no-forking guarantee (D-overview-4) must
// hold at the color layer, not just the hatch STYLE layer. Both panels feed a
// `interact::overview_pattern` value in; the color/fill/border derive here so a design retune
// touches one place.

// The fixed fallback hatch palette (§5:204 "pattern-count-before-color"): `color_index == -1`
// (a distinct hatch style is carrying the load) uses the neutral ink; a `>=0` index picks a
// desaturated hue so a wrapped style still reads as a distinct fill. `selected` uses the accent.
// Visual polish only — a design pass retunes these without touching the model (D-overview-4).
inline ImU32 hatch_ink(int color_index, bool selected) {
  if (selected) {
    return accent(235); // the accent for the selected box's/row's hatch
  }
  static const ImU32 palette[] = {
      IM_COL32(210, 210, 210, 180), IM_COL32(230, 170, 150, 180), IM_COL32(170, 210, 170, 180),
      IM_COL32(170, 190, 230, 180), IM_COL32(220, 210, 160, 180), IM_COL32(210, 170, 210, 180),
  };
  if (color_index < 0) {
    return palette[0];
  }
  const int n = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
  return palette[color_index % n];
}

// Draw the list-side pattern swatch: a `size`-px filled, hatched, bordered square at screen
// `origin` carrying the SAME identity the overview box draws (fill under the hatch, content-space
// hatch lines through `hatch_segments`, dotted-border-analogue solid border). The square's own
// content extent is
// `[0, size]²`, so `hatch_segments` runs in swatch-pixel space and the endpoints only need the
// `origin` offset — the overview's placement/transform mapping collapses to a translation here. A
// `hovered` swatch gets a brightened accent border (Constraint 7: reads as "pointer is here", not
// "selected"); the two states compose (a selected-and-hovered row shows both).
inline void draw_pattern_swatch(ImDrawList* dl, ImVec2 origin, float size,
                                const interact::OverviewPattern& pat, bool selected, bool hovered) {
  const ImVec2 br{origin.x + size, origin.y + size};
  const ImU32 fill = selected ? IM_COL32(60, 110, 150, 90) : IM_COL32(70, 70, 78, 110);
  dl->AddRectFilled(origin, br, fill);
  const arbc::Rect e{0.0, 0.0, static_cast<double>(size), static_cast<double>(size)};
  const double spacing = std::max(1.0, static_cast<double>(size) / 4.0);
  const ImU32 ink = hatch_ink(pat.color_index, selected);
  for (const interact::HatchSegment& seg : interact::hatch_segments(e, spacing, pat.style)) {
    dl->AddLine(
        ImVec2{origin.x + static_cast<float>(seg.a.x), origin.y + static_cast<float>(seg.a.y)},
        ImVec2{origin.x + static_cast<float>(seg.b.x), origin.y + static_cast<float>(seg.b.y)}, ink,
        1.0F);
  }
  const ImU32 border =
      hovered ? accent(230) : (selected ? accent(255) : IM_COL32(200, 200, 200, 220));
  dl->AddRect(origin, br, border, 0.0F, 0, hovered ? 2.0F : 1.0F);
}

} // namespace ace::app
