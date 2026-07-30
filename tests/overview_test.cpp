// L1 unit tests for editor.panels.overview (D6/§5): the two new pure `interact` helpers
// (`overview_pattern`, `hatch_segments`) plus the overview's geometry/pick/navigator math, all
// exercised headless against the shipped `interact`/`scene` seams. No ImGui — this is the
// ASan/TSan-clean lane (docs/01-architecture.md §8/§9).

#include <ace/commands/selection.hpp>
#include <ace/interact/interact.hpp>
#include <ace/interact/pick.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <optional>
#include <vector>

using ace::interact::HatchSegment;
using ace::interact::HatchStyle;
using ace::interact::OverviewPattern;

namespace {

arbc::Registry overview_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  return registry;
}

double seg_len(const HatchSegment& s) {
  return std::sqrt((s.b.x - s.a.x) * (s.b.x - s.a.x) + (s.b.y - s.a.y) * (s.b.y - s.a.y));
}

} // namespace

TEST_CASE(
    "overview: fit_region round-trips a composition point through the panel and its inverse") {
  // Constraint 3: the whole-composition fit maps composition -> panel and back within tolerance.
  const arbc::Rect bounds{0.0, 0.0, 100.0, 100.0};
  const arbc::Affine t = ace::interact::fit_region(bounds, 200.0, 100.0);
  REQUIRE(t.inverse().has_value());
  // Uniform fit into the SHORTER pane axis (100 tall): scale 1.0, centred horizontally.
  const arbc::Vec2 centre{50.0, 50.0};
  const arbc::Vec2 panel = t.apply(centre);
  const arbc::Vec2 back = t.inverse()->apply(panel);
  CHECK(std::abs(back.x - centre.x) < 1e-9);
  CHECK(std::abs(back.y - centre.y) < 1e-9);
  // The whole composition lands inside the pane rect (minimap semantics).
  CHECK(panel.x >= -1e-9);
  CHECK(panel.x <= 200.0 + 1e-9);
  CHECK(panel.y >= -1e-9);
  CHECK(panel.y <= 100.0 + 1e-9);

  // A degenerate composition / pane yields identity — no NaN escapes (D-fit_bounds-3).
  CHECK(ace::interact::fit_region(arbc::Rect{0.0, 0.0, 0.0, 0.0}, 200.0, 100.0) ==
        arbc::Affine::identity());
  CHECK(ace::interact::fit_region(bounds, 0.0, 100.0) == arbc::Affine::identity());
}

TEST_CASE("overview: overview_pattern is deterministic and falls back to color past N styles") {
  // Same ordinal -> same pattern (the swatch a future Layers row reuses).
  const OverviewPattern a = ace::interact::overview_pattern(2, 10);
  const OverviewPattern b = ace::interact::overview_pattern(2, 10);
  CHECK(a.style.angle_rad == b.style.angle_rad);
  CHECK(a.style.cross == b.style.cross);
  CHECK(a.color_index == b.color_index);

  // The first N ordinals are distinguished by HATCH STYLE alone (color_index == -1).
  for (int i = 0; i < ace::interact::k_overview_hatch_style_count; ++i) {
    CHECK(ace::interact::overview_pattern(i, 32).color_index == -1);
  }
  // Exactly at N the styles wrap and the color fallback engages (§5:204).
  CHECK(ace::interact::overview_pattern(ace::interact::k_overview_hatch_style_count, 32)
            .color_index >= 0);
  // Distinct styles across the first N.
  bool all_distinct = true;
  for (int i = 0; i < ace::interact::k_overview_hatch_style_count; ++i) {
    for (int j = i + 1; j < ace::interact::k_overview_hatch_style_count; ++j) {
      const OverviewPattern pi = ace::interact::overview_pattern(i, 32);
      const OverviewPattern pj = ace::interact::overview_pattern(j, 32);
      if (pi.style.angle_rad == pj.style.angle_rad && pi.style.cross == pj.style.cross) {
        all_distinct = false;
      }
    }
  }
  CHECK(all_distinct);

  // A degenerate count collapses to the style-0 default; an out-of-range ordinal is clamped.
  CHECK(ace::interact::overview_pattern(5, 0).color_index == -1);
  const OverviewPattern clamped = ace::interact::overview_pattern(99, 3); // clamp to ordinal 2
  CHECK(clamped.style.angle_rad == ace::interact::overview_pattern(2, 3).style.angle_rad);
}

TEST_CASE(
    "overview: hatch_segments is content-space — rotation-invariant count, scales with extent") {
  const HatchStyle horizontal{0.0, false}; // lines along +x, stepped in y
  const arbc::Rect small{0.0, 0.0, 100.0, 80.0};
  const std::vector<HatchSegment> segs = ace::interact::hatch_segments(small, 20.0, horizontal);
  // y in [0,80], origin-anchored lines at 0,20,40,60,80 -> 5 lines, each spanning the 100-wide box.
  REQUIRE(segs.size() == 5);
  for (const HatchSegment& s : segs) {
    CHECK(std::abs(seg_len(s) - 100.0) < 1e-9);
  }

  // SCALES WITH THE CONTENT EXTENT: a box twice as tall gets ~twice the lines.
  const arbc::Rect tall{0.0, 0.0, 100.0, 160.0};
  CHECK(ace::interact::hatch_segments(tall, 20.0, horizontal).size() == 9); // 0..160 step 20

  // ROTATION-INVARIANT + TILTS WITH THE CELL: the segments are generated pre-placement, so a
  // rotated placement leaves the COUNT unchanged and merely rotates the mapped segments (§5:201).
  const double c = std::cos(1.5707963267948966); // 90 degrees
  const double s90 = std::sin(1.5707963267948966);
  const arbc::Affine rot{c, s90, -s90, c, 0.0, 0.0};
  const std::vector<HatchSegment>& content_segs = segs; // identical count regardless of placement
  CHECK(content_segs.size() == 5);
  for (const HatchSegment& seg : content_segs) {
    const arbc::Vec2 ma = rot.apply(seg.a);
    const arbc::Vec2 mb = rot.apply(seg.b);
    // A 90-degree rotation turns the horizontal content line vertical (dx ~ 0), length preserved.
    CHECK(std::abs(mb.x - ma.x) < 1e-9);
    CHECK(std::abs(seg_len(HatchSegment{ma, mb}) - 100.0) < 1e-9);
  }

  // A cross style appends the perpendicular set.
  CHECK(ace::interact::hatch_segments(small, 20.0, HatchStyle{0.0, true}).size() == 11);

  // Degenerate spacing / bounds -> a defined empty no-op.
  CHECK(ace::interact::hatch_segments(small, 0.0, horizontal).empty());
  CHECK(ace::interact::hatch_segments(arbc::Rect{0.0, 0.0, 0.0, 0.0}, 20.0, horizontal).empty());
  // An over-fine hatch is declined entirely (the over-dense guard).
  CHECK(ace::interact::hatch_segments(small, 1e-6, horizontal).empty());
}

TEST_CASE("overview: cells are drawn bottom->top in scene::cells order, agreeing with z_order") {
  // Constraint 4: the box draw order IS `scene::cells` bottom->top, so front occludes back.
  arbc::Registry registry = overview_registry();
  arbc::Document doc;
  doc.add_composition(128.0, 128.0);
  std::vector<arbc::ObjectId> inserted;
  for (int i = 0; i < 4; ++i) {
    const auto id = ace::scene::add_cell(doc, registry, "org.arbc.raster", "16x16",
                                         arbc::Affine::translation(4.0 * i, 4.0 * i));
    REQUIRE(id.has_value());
    inserted.push_back(*id);
  }
  const std::vector<ace::scene::Cell> cells = ace::scene::cells(doc, registry);
  REQUIRE(cells.size() == 4);
  for (int i = 0; i < 4; ++i) {
    CHECK(cells[static_cast<std::size_t>(i)].id == inserted[static_cast<std::size_t>(i)]);
    CHECK(ace::scene::z_order_position(cells, inserted[static_cast<std::size_t>(i)]).index == i);
  }
}

TEST_CASE("overview: the live-viewport quad is camera.inverse() over the pane corners") {
  // Constraint 7: the highlighted rect is the visible-composition quad the panel strokes.
  const int pane_w = 80;
  const int pane_h = 60;

  // A plain 2x-zoom camera at the origin: the visible quad is [0,40]x[0,30] composition units.
  const arbc::Affine plain = arbc::Affine::scaling(2.0, 2.0);
  REQUIRE(plain.inverse().has_value());
  const arbc::Vec2 tl = plain.inverse()->apply({0.0, 0.0});
  const arbc::Vec2 br =
      plain.inverse()->apply({static_cast<double>(pane_w), static_cast<double>(pane_h)});
  CHECK(std::abs(tl.x) < 1e-9);
  CHECK(std::abs(tl.y) < 1e-9);
  CHECK(std::abs(br.x - 40.0) < 1e-9);
  CHECK(std::abs(br.y - 30.0) < 1e-9);

  // A dutch-rotated camera: the quad is a true parallelogram (its top edge is no longer axis
  // aligned), so the derivation must go through the inverse, not an AABB.
  const double c = std::cos(0.3);
  const double s = std::sin(0.3);
  const arbc::Affine dutch{c, s, -s, c, 0.0, 0.0};
  REQUIRE(dutch.inverse().has_value());
  const arbc::Vec2 q0 = dutch.inverse()->apply({0.0, 0.0});
  const arbc::Vec2 q1 = dutch.inverse()->apply({static_cast<double>(pane_w), 0.0});
  CHECK(std::abs(q0.x) < 1e-9);
  CHECK(std::abs(q1.y) > 1e-6); // the top edge tilts — not axis aligned
}

TEST_CASE("overview: navigator fit/pan/zoom reuse the interact primitives; degenerate refuses") {
  // Constraint 7: "fit to this camera" == fit_region(camera_frame_rect, pane); pan/zoom mirror the
  // interact verbs; an empty framing leaves the camera unchanged (refuse-rather-than-guess).
  const arbc::Rect frame_rect{10.0, 10.0, 110.0, 90.0};
  const arbc::Affine fit = ace::interact::fit_region(frame_rect, 200.0, 200.0);
  REQUIRE(fit.inverse().has_value());
  // The framed rect maps inside the pane.
  const arbc::Vec2 mapped = fit.apply({60.0, 50.0}); // the rect centre
  CHECK(mapped.x >= -1e-9);
  CHECK(mapped.x <= 200.0 + 1e-9);

  const arbc::Affine cam = arbc::Affine::scaling(3.0, 3.0);
  const arbc::Affine panned = ace::interact::pan(cam, 12.0, -5.0);
  CHECK(std::abs(panned.tx - (cam.tx + 12.0)) < 1e-9);
  CHECK(std::abs(panned.ty - (cam.ty - 5.0)) < 1e-9);

  const arbc::Affine zoomed = ace::interact::zoom(cam, arbc::Vec2{40.0, 30.0}, 2.0);
  CHECK(std::abs(zoomed.max_scale() - cam.max_scale() * 2.0) < 1e-6);

  // Nothing to frame: an empty rect refuses (identity).
  CHECK(ace::interact::fit_region(arbc::Rect{5.0, 5.0, 5.0, 5.0}, 200.0, 200.0) ==
        arbc::Affine::identity());
}

TEST_CASE("overview: a panel click maps through the inverse transform to the same SelectionChange "
          "the canvas would; entered drops out-of-scope cells and all cameras") {
  arbc::Registry registry = overview_registry();
  arbc::Document doc;
  const arbc::ObjectId root = doc.add_composition(128.0, 128.0);
  REQUIRE(root.valid());

  const arbc::Affine at{1.0, 0.0, 0.0, 1.0, 20.0, 20.0};
  const auto cell = ace::scene::add_cell(doc, registry, "org.arbc.raster", "16x16", at);
  REQUIRE(cell.has_value());
  const arbc::ObjectId camera = ace::scene::add_camera(
      doc, registry, "Hero", ace::scene::Resolution{32, 24}, arbc::Affine::translation(60.0, 60.0));
  REQUIRE(camera.valid());

  // A nested child composition with its own cell, wrapped by a nested cell in Root.
  const arbc::ObjectId child = doc.add_composition(48.0, 48.0);
  REQUIRE(child.valid());
  const auto child_cell = ace::scene::add_cell(doc, registry, "org.arbc.raster", "8x8",
                                               arbc::Affine::identity(), child);
  REQUIRE(child_cell.has_value());
  REQUIRE(ace::scene::add_cell(doc, registry, "org.arbc.nested", std::to_string(child.value),
                               arbc::Affine::translation(4.0, 4.0))
              .has_value());

  // Root scope: a click over the raster's centre (composition-space) picks the raster — exactly
  // what the canvas does with the same point, since both feed `click_selection`.
  const std::vector<ace::interact::PickTarget> root_targets =
      ace::interact::pick_targets(doc, registry, std::nullopt);
  const arbc::Vec2 cell_centre{28.0, 28.0}; // (20,20)+(8,8): inside the placed 16x16 box
  const ace::interact::SelectionChange change = ace::interact::click_selection(
      root_targets, cell_centre, 2.0, 3.0, ace::interact::PickModifiers{}, arbc::ObjectId{});
  CHECK(change.op == ace::interact::SelectOp::Replace);
  REQUIRE(change.ids.size() == 1);
  CHECK(change.ids.front() == *cell);

  // The transform round-trip the panel uses to get from a panel-space point to `cell_centre`.
  const arbc::Rect bounds{0.0, 0.0, 128.0, 128.0};
  const arbc::Affine t = ace::interact::fit_region(bounds, 256.0, 256.0);
  REQUIRE(t.inverse().has_value());
  const arbc::Vec2 panel_pt = t.apply(cell_centre);
  const arbc::Vec2 back = t.inverse()->apply(panel_pt);
  CHECK(std::abs(back.x - cell_centre.x) < 1e-9);
  CHECK(std::abs(back.y - cell_centre.y) < 1e-9);

  // Entered scope (D29): the confined pick set holds ONLY the child's cell — no root cell, no
  // camera. Confinement drops out free through the single `pick_targets` adapter.
  const std::vector<ace::interact::PickTarget> scoped =
      ace::interact::pick_targets(doc, registry, child);
  REQUIRE(scoped.size() == 1);
  CHECK(scoped.front().id == *child_cell);
  for (const ace::interact::PickTarget& tgt : scoped) {
    CHECK(tgt.id != camera);
    CHECK(tgt.id != *cell);
    CHECK(tgt.kind == ace::interact::PickKind::Cell);
  }
}

TEST_CASE("overview: scope reflection re-roots the cell walk and derives the breadcrumb, "
          "fail-safe to Root") {
  arbc::Registry registry = overview_registry();
  arbc::Document doc;
  const arbc::ObjectId root = doc.add_composition(128.0, 128.0);
  REQUIRE(root.valid());
  REQUIRE(ace::scene::add_cell(doc, registry, "org.arbc.raster", "16x16", arbc::Affine::identity())
              .has_value());
  const arbc::ObjectId child = doc.add_composition(48.0, 48.0);
  REQUIRE(child.valid());
  const auto child_cell = ace::scene::add_cell(doc, registry, "org.arbc.raster", "8x8",
                                               arbc::Affine::identity(), child);
  REQUIRE(child_cell.has_value());
  REQUIRE(ace::scene::add_cell(doc, registry, "org.arbc.nested", std::to_string(child.value),
                               arbc::Affine::translation(4.0, 4.0))
              .has_value());

  // Entered: the overview cells come from the child composition; the breadcrumb is Root > child.
  const arbc::ObjectId active =
      ace::scene::active_composition(doc, std::optional<arbc::ObjectId>(child));
  CHECK(active == child);
  const std::vector<ace::scene::Cell> scoped = ace::scene::cells(doc, registry, active);
  REQUIRE(scoped.size() == 1);
  CHECK(scoped.front().id == *child_cell);
  const std::vector<ace::scene::Breadcrumb> path =
      ace::scene::composition_path(doc, registry, std::optional<arbc::ObjectId>(child));
  REQUIRE(path.size() == 2);
  CHECK(path.front().composition == root);
  CHECK(path.back().composition == child);

  // A vanished / bogus scope fails safe to Root: cells == root cells, breadcrumb == [Root].
  const arbc::ObjectId bogus{99999};
  CHECK(ace::scene::active_composition(doc, std::optional<arbc::ObjectId>(bogus)) == root);
  const std::vector<ace::scene::Breadcrumb> safe =
      ace::scene::composition_path(doc, registry, std::optional<arbc::ObjectId>(bogus));
  REQUIRE(safe.size() == 1);
  CHECK(safe.front().composition == root);
}
