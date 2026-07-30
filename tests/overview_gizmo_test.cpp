// editor.panels.overview_gizmo — L1 unit pins for the full scale/rotate/shear gizmo on the
// schematic overview boxes. There is ZERO new L1 math (D-overview_gizmo-1): every transform verb,
// the handle hit-test, the placed-quad anchor, and the snapping engine already ship as pure L1
// `interact` from editor.cells.gizmo. These cases pin the OVERVIEW SEAM contract — that feeding the
// shipped verbs through the `OverviewXform` fit scale reaches composition space correctly and
// reuses the shipped math faithfully (no forking), that the grab tolerance is screen-constant, that
// a handle-drag stays placement-only (D8) and lands as one transaction, that scope confines the
// grab (D29), and that a multi-selection drives the shipped group path.

#include <ace/commands/cells.hpp>
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
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <optional>
#include <vector>

using ace::interact::cell_pivot;
using ace::interact::CellHandle;
using ace::interact::fit_region;
using ace::interact::group_transform;
using ace::interact::hit_cell;
using ace::interact::PickKind;
using ace::interact::PickTarget;
using ace::interact::rotate_cell;
using ace::interact::scale_cell;
using ace::interact::selected_extent;
using ace::interact::shear_cell;
using Catch::Approx;

namespace {

arbc::Registry overview_gizmo_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  return registry;
}

PickTarget cell_target(const arbc::Affine& placement, const arbc::Rect& extent) {
  PickTarget t;
  t.kind = PickKind::Cell;
  t.placement = placement;
  t.extent = extent;
  return t;
}

// The overview's screen-constant grab-tolerance convention (overview_panel.cpp:286-288): a fixed
// screen-px radius divided by the fit scale (px per composition unit) is a composition-unit
// tolerance `hit_cell` wants.
double overview_corner_tol(double scale) { return 6.0 / scale; }
double overview_edge_tol(double scale) { return 4.0 / scale; }

} // namespace

TEST_CASE("overview_gizmo: handle math is view-independent") {
  // A box under an arbitrary (rotated + translated) composition affine — the exact case §5/D6 keeps
  // (a rotated cell's box tilts with it), so the gizmo must anchor to the rotated placed quad.
  const arbc::Affine placement{std::cos(0.6), std::sin(0.6), -std::sin(0.6),
                               std::cos(0.6), 40.0,          -12.0};
  const arbc::Rect extent{0.0, 0.0, 16.0, 16.0};
  const PickTarget target = cell_target(placement, extent);
  const arbc::Vec2 pivot = cell_pivot(placement, extent);

  // Two DIFFERENT view scales: the overview's whole-composition fit and a canvas view scale. The
  // fit affine's scale is `t.a` (uniform), exactly as `OverviewXform::scale()` returns.
  const arbc::Affine t_ov = fit_region(arbc::Rect{-32.0, -32.0, 96.0, 96.0}, 400.0, 400.0);
  const arbc::Affine t_cv = fit_region(arbc::Rect{-32.0, -32.0, 96.0, 96.0}, 120.0, 120.0);
  REQUIRE(t_ov.inverse().has_value());
  REQUIRE(t_cv.inverse().has_value());
  const double s_ov = t_ov.a;
  const double s_cv = t_cv.a;
  REQUIRE(s_ov != Approx(s_cv)); // genuinely different zooms

  // The composition-space pointer ON the bottom-right corner. Fed through each view's tolerance
  // regime, `hit_cell` classifies the SAME handle — the classification is composition-space, so the
  // overview and the canvas grab the identical corner.
  const arbc::Vec2 corner_comp = placement.apply({extent.x1, extent.y1});
  const CellHandle h_ov =
      hit_cell(target, pivot, corner_comp, overview_edge_tol(s_ov), overview_corner_tol(s_ov));
  const CellHandle h_cv =
      hit_cell(target, pivot, corner_comp, overview_edge_tol(s_cv), overview_corner_tol(s_cv));
  CHECK(h_ov == CellHandle::ScaleBottomRight);
  CHECK(h_cv == CellHandle::ScaleBottomRight);
  CHECK(h_ov == h_cv);

  // A drag point reached by round-tripping a SCREEN position through EACH view's inverse (the
  // `OverviewXform::to_comp` path — Constraint 2: never feed panel px to a composition-space verb).
  // Both routes recover the same composition point, so the shipped `scale_cell` returns the SAME
  // placement — faithful reuse, no forked overview math.
  const arbc::Vec2 drag_comp = placement.apply({22.0, 22.0});
  const arbc::Vec2 via_ov = t_ov.inverse()->apply(t_ov.apply(drag_comp));
  const arbc::Vec2 via_cv = t_cv.inverse()->apply(t_cv.apply(drag_comp));
  const arbc::Affine r_ov =
      scale_cell(placement, extent, CellHandle::ScaleBottomRight, via_ov, pivot, false, false);
  const arbc::Affine r_cv =
      scale_cell(placement, extent, CellHandle::ScaleBottomRight, via_cv, pivot, false, false);
  const arbc::Affine r_truth =
      scale_cell(placement, extent, CellHandle::ScaleBottomRight, drag_comp, pivot, false, false);
  CHECK(r_ov.a == Approx(r_cv.a));
  CHECK(r_ov.d == Approx(r_cv.d));
  CHECK(r_ov.a == Approx(r_truth.a));
  CHECK(r_ov.tx == Approx(r_truth.tx));
  CHECK(r_ov.ty == Approx(r_truth.ty));
}

TEST_CASE("overview_gizmo: grab tolerance is screen-constant") {
  const arbc::Rect extent{0.0, 0.0, 20.0, 20.0};
  const arbc::Affine placement = arbc::Affine::identity();
  const PickTarget target = cell_target(placement, extent);
  const arbc::Vec2 pivot = cell_pivot(placement, extent);

  // Two overview fit zooms: a large px-per-unit (zoomed in) and a small one (zoomed out).
  constexpr double s_big = 4.0;
  constexpr double s_small = 1.0;

  // A pointer 5.5 SCREEN px outside the top-left corner. In composition units that is a SMALL
  // offset when zoomed in and a LARGE one when zoomed out — the whole reason the tolerance must
  // track the zoom.
  constexpr double screen_px = 5.5;
  const arbc::Vec2 tl = placement.apply({extent.x0, extent.y0});
  const arbc::Vec2 grab_big{tl.x - screen_px / s_big, tl.y};     // 1.375 comp units off
  const arbc::Vec2 grab_small{tl.x - screen_px / s_small, tl.y}; // 5.5 comp units off

  // Screen-constant tolerance (6 px / scale) grabs the corner at BOTH zooms.
  CHECK(hit_cell(target, pivot, grab_big, overview_edge_tol(s_big), overview_corner_tol(s_big)) ==
        CellHandle::ScaleTopLeft);
  CHECK(hit_cell(target, pivot, grab_small, overview_edge_tol(s_small),
                 overview_corner_tol(s_small)) == CellHandle::ScaleTopLeft);

  // The negative pin: a composition-CONSTANT tolerance (frozen at the zoomed-in value) MISSES the
  // corner at the zoomed-out fit — the bug this convention avoids (D-overview_gizmo-3).
  const double frozen_edge = overview_edge_tol(s_big);
  const double frozen_corner = overview_corner_tol(s_big);
  CHECK(hit_cell(target, pivot, grab_small, frozen_edge, frozen_corner) !=
        CellHandle::ScaleTopLeft);
}

TEST_CASE("overview_gizmo: handle-drag is placement-only (D8)") {
  arbc::Registry registry = overview_gizmo_registry();
  arbc::Document doc;
  doc.add_composition(64.0, 64.0);
  const auto id = ace::scene::add_cell(doc, registry, "org.arbc.raster", "16x16",
                                       arbc::Affine::translation(8.0, 8.0));
  REQUIRE(id.has_value());

  const auto find_cell = [&]() {
    for (const ace::scene::Cell& c : ace::scene::cells(doc, registry)) {
      if (c.id == *id) {
        return c;
      }
    }
    FAIL("cell vanished");
    return ace::scene::Cell{};
  };

  const ace::scene::Cell seed = find_cell();
  REQUIRE(seed.content_bounds.has_value());
  const arbc::Rect extent = *seed.content_bounds;
  const arbc::Vec2 pivot = cell_pivot(seed.placement, extent);

  // Each shipped composer the overview drives, as the panel drives it.
  const arbc::Affine prop_scale = scale_cell(seed.placement, extent, CellHandle::ScaleBottomRight,
                                             {40.0, 40.0}, pivot, /*free_distort=*/false,
                                             /*about_pivot=*/false);
  CHECK(prop_scale.a == Approx(prop_scale.d)); // proportional by default
  const arbc::Affine free_scale = scale_cell(seed.placement, extent, CellHandle::ScaleBottomRight,
                                             {40.0, 24.0}, pivot, /*free_distort=*/true,
                                             /*about_pivot=*/false);
  CHECK(free_scale.a != Approx(free_scale.d)); // Shift = free distort
  const arbc::Affine edge_scale =
      scale_cell(seed.placement, extent, CellHandle::ScaleRight, {40.0, 16.0}, pivot, false, false);
  CHECK(edge_scale.d == Approx(1.0)); // 1D stretch — the orthogonal axis is untouched
  const arbc::Affine rot = rotate_cell(seed.placement, pivot, 0.4, /*snap_15=*/false);
  CHECK((rot.a * rot.a + rot.c * rot.c) == Approx(1.0)); // rotation preserves lengths (no resample)
  const arbc::Affine shear =
      shear_cell(seed.placement, extent, CellHandle::ScaleTop, {30.0, 8.0}, pivot);
  CHECK(shear.c != Approx(0.0)); // off-diagonal — a shear

  // The value committed through the overview's `transform_cells_command` funnel carries placement
  // ONLY: after the commit the layer's placement is the new affine and the content extent
  // (resolution / stored pixels) is UNCHANGED (D8, non-destructive).
  const arbc::Rect before_extent = *find_cell().content_bounds;
  ace::commands::transform_cells_command({ace::commands::LayerTransform{seed.layer, prop_scale}})
      .apply(doc);
  const ace::scene::Cell after = find_cell();
  CHECK(after.placement == prop_scale);
  REQUIRE(after.content_bounds.has_value());
  CHECK(after.content_bounds->x0 == Approx(before_extent.x0));
  CHECK(after.content_bounds->y0 == Approx(before_extent.y0));
  CHECK(after.content_bounds->x1 == Approx(before_extent.x1));
  CHECK(after.content_bounds->y1 == Approx(before_extent.y1));
}

TEST_CASE("overview_gizmo: one transaction per gesture") {
  arbc::Registry registry = overview_gizmo_registry();
  arbc::Document doc;
  doc.add_composition(64.0, 64.0);
  const auto id = ace::scene::add_cell(doc, registry, "org.arbc.raster", "16x16",
                                       arbc::Affine::translation(8.0, 8.0));
  REQUIRE(id.has_value());

  const auto find_cell = [&]() {
    for (const ace::scene::Cell& c : ace::scene::cells(doc, registry)) {
      if (c.id == *id) {
        return c;
      }
    }
    FAIL("cell vanished");
    return ace::scene::Cell{};
  };

  const ace::scene::Cell seed = find_cell();
  const arbc::Rect extent = *seed.content_bounds;
  const arbc::Vec2 pivot = cell_pivot(seed.placement, extent);

  // A PREVIEW run (the shipped verbs the panel calls each drag frame) touches neither the document
  // nor the journal — the drag previews as UI-only session state (Constraint 5).
  const std::size_t cursor_before = doc.journal().cursor();
  const arbc::Affine gesture = scale_cell(seed.placement, extent, CellHandle::ScaleBottomRight,
                                          {40.0, 40.0}, pivot, false, false);
  CHECK(doc.journal().cursor() == cursor_before);
  CHECK(find_cell().placement == seed.placement);

  // A single release commits EXACTLY ONE journal entry that undo reverts as a unit.
  ace::commands::transform_cells_command({ace::commands::LayerTransform{seed.layer, gesture}})
      .apply(doc);
  CHECK(doc.journal().cursor() == cursor_before + 1);
  CHECK(find_cell().placement == gesture);
  REQUIRE(doc.journal().undo());
  CHECK(find_cell().placement == seed.placement);
  CHECK(doc.journal().cursor() == cursor_before);
}

TEST_CASE("overview_gizmo: scope confines the grab (D29)") {
  arbc::Registry registry = overview_gizmo_registry();
  arbc::Document doc;
  const arbc::ObjectId root = doc.add_composition(128.0, 128.0);
  REQUIRE(root.valid());

  const auto root_cell = ace::scene::add_cell(doc, registry, "org.arbc.raster", "16x16",
                                              arbc::Affine::translation(20.0, 20.0));
  REQUIRE(root_cell.has_value());
  const arbc::ObjectId camera = ace::scene::add_camera(
      doc, registry, "Hero", ace::scene::Resolution{32, 24}, arbc::Affine::translation(60.0, 60.0));
  REQUIRE(camera.valid());

  const arbc::ObjectId child = doc.add_composition(48.0, 48.0);
  REQUIRE(child.valid());
  const auto child_cell = ace::scene::add_cell(doc, registry, "org.arbc.raster", "8x8",
                                               arbc::Affine::identity(), child);
  REQUIRE(child_cell.has_value());
  REQUIRE(ace::scene::add_cell(doc, registry, "org.arbc.nested", std::to_string(child.value),
                               arbc::Affine::translation(4.0, 4.0))
              .has_value());

  // The gizmo hit-tests over the SCOPED pick set the overview already builds (Constraint 6/7):
  // while `child` is entered, only the child cell is present — the out-of-scope root cell and the
  // camera drop out, so no gizmo (single or group) can even target them (scope confinement is free,
  // D29).
  const std::vector<PickTarget> scoped = ace::interact::pick_targets(doc, registry, child);
  REQUIRE(scoped.size() == 1);
  CHECK(scoped.front().id == *child_cell);
  for (const PickTarget& t : scoped) {
    CHECK(t.id != *root_cell);
    CHECK(t.id != camera);
    CHECK(t.kind == PickKind::Cell);
  }

  // A single-selection of the OUT-OF-SCOPE root cell resolves to no gizmo target in the scoped set,
  // and a group union over out-of-scope members is empty — nothing is grabbable.
  CHECK(!selected_extent(scoped, std::vector<arbc::ObjectId>{*root_cell}).has_value());
  CHECK(!selected_extent(scoped, std::vector<arbc::ObjectId>{camera}).has_value());

  // The in-scope child cell IS grabbable through the same hit-test.
  const PickTarget& t = scoped.front();
  REQUIRE(t.extent.has_value());
  const arbc::Vec2 pivot = cell_pivot(t.placement, *t.extent);
  // An off-center interior point (NOT the pivot dot, which would win by precedence): a real body
  // grab inside the scoped cell's placed box.
  const arbc::Vec2 body = t.placement.apply(
      {t.extent->x0 + t.extent->width() * 0.25, t.extent->y0 + t.extent->height() * 0.25});
  CHECK(hit_cell(t, pivot, body, 1.0, 1.5) == CellHandle::Body);
}

TEST_CASE("overview_gizmo: group transform") {
  arbc::Registry registry = overview_gizmo_registry();
  arbc::Document doc;
  doc.add_composition(128.0, 128.0);
  const auto a = ace::scene::add_cell(doc, registry, "org.arbc.raster", "16x16",
                                      arbc::Affine::translation(8.0, 8.0));
  const auto b = ace::scene::add_cell(doc, registry, "org.arbc.raster", "16x16",
                                      arbc::Affine::translation(40.0, 40.0));
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());

  const auto layer_of = [&](arbc::ObjectId content) {
    for (const ace::scene::Cell& c : ace::scene::cells(doc, registry)) {
      if (c.id == content) {
        return c.layer;
      }
    }
    FAIL("cell vanished");
    return arbc::ObjectId{};
  };
  const arbc::ObjectId layer_a = layer_of(*a);
  const arbc::ObjectId layer_b = layer_of(*b);

  const std::vector<PickTarget> targets = ace::interact::pick_targets(doc, registry);
  const std::optional<arbc::Rect> uni =
      selected_extent(targets, std::vector<arbc::ObjectId>{*a, *b});
  REQUIRE(uni.has_value());
  REQUIRE(!uni->empty());

  // Drive the union box (identity-placed) through the shipped verb about its center pivot, then
  // compose the ONE delta onto every member — exactly the panel's group path (D-overview_gizmo-6).
  const arbc::Vec2 pivot = cell_pivot(arbc::Affine::identity(), *uni);
  const arbc::Affine new_union =
      scale_cell(arbc::Affine::identity(), *uni, CellHandle::ScaleBottomRight,
                 {uni->x1 + uni->width(), uni->y1 + uni->height()}, pivot,
                 /*free_distort=*/false, /*about_pivot=*/true);
  const std::vector<arbc::Affine> starts = {arbc::Affine::translation(8.0, 8.0),
                                            arbc::Affine::translation(40.0, 40.0)};
  const std::vector<arbc::Affine> previews =
      group_transform(arbc::Affine::identity(), new_union, starts);
  REQUIRE(previews.size() == 2);
  // The SAME uniform delta lands on both members (a > 1 scale-up, and neither is left at its
  // start).
  CHECK(previews[0].a == Approx(previews[1].a));
  CHECK(previews[0].a > 1.0);
  CHECK(!(previews[0] == starts[0]));
  CHECK(!(previews[1] == starts[1]));

  // Committed as ONE transaction over the whole batch (one journal entry, one atomic publish).
  const std::size_t cursor_before = doc.journal().cursor();
  ace::commands::transform_cells_command({ace::commands::LayerTransform{layer_a, previews[0]},
                                          ace::commands::LayerTransform{layer_b, previews[1]}})
      .apply(doc);
  CHECK(doc.journal().cursor() == cursor_before + 1);
  const auto placement_of = [&](arbc::ObjectId content) {
    for (const ace::scene::Cell& c : ace::scene::cells(doc, registry)) {
      if (c.id == content) {
        return c.placement;
      }
    }
    FAIL("cell vanished");
    return arbc::Affine::identity();
  };
  CHECK(placement_of(*a) == previews[0]);
  CHECK(placement_of(*b) == previews[1]);
}
