// editor.cells.selection — L1 headless Catch2 units for the pick policy core + the one
// assembly adapter + the `commands::Selection` verbs (D7 / §6 "Selection (one tool)" /
// D-selection-1..11). Every rule that could be WRONG lives in `ace/interact/pick.hpp` and is
// exercised here against hand-built primitive targets: cell-body hits under an arbitrary
// affine, the camera's click-through interior, z-order + topmost-first, select-behind cycling,
// unbounded content, exact marquee overlap, the modifier policy, and the degenerate ends.
// Plus the `pick_targets` assembly over a REAL `arbc::Document`, `Selection::prune` /
// `replace_all` / `add_all`, the rename-preserves-identity payoff, and two byte-INVARIANCE
// cases pinning that selection chrome never enters the composite and that selecting is never a
// transaction. No ImGui/GL/SDL.
#include <ace/commands/selection.hpp>
#include <ace/interact/interact.hpp>
#include <ace/interact/pick.hpp>
#include <ace/project/project.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "golden_support.hpp"

using ace::commands::Selection;
using ace::interact::all_ids;
using ace::interact::click_selection;
using ace::interact::FrameHandle;
using ace::interact::marquee;
using ace::interact::marquee_selection;
using ace::interact::pick;
using ace::interact::pick_behind;
using ace::interact::pick_stack;
using ace::interact::pick_targets;
using ace::interact::PickHit;
using ace::interact::PickKind;
using ace::interact::PickModifiers;
using ace::interact::PickTarget;
using ace::interact::placed_quad;
using ace::interact::SelectionChange;
using ace::interact::SelectOp;

namespace {

arbc::ObjectId oid(std::uint64_t value) { return arbc::ObjectId{value}; }

PickTarget cell_target(std::uint64_t id, const arbc::Affine& placement,
                       std::optional<arbc::Rect> extent) {
  return PickTarget{oid(id), oid(id + 1000), PickKind::Cell, placement, extent};
}

PickTarget camera_target(std::uint64_t id, const arbc::Affine& frame, int width, int height) {
  return PickTarget{oid(id), oid(id + 1000), PickKind::Camera, frame,
                    arbc::Rect{0.0, 0.0, static_cast<double>(width), static_cast<double>(height)}};
}

// A 10x10 square centred on its own origin — so a rotation about the placement's origin is a
// rotation about the cell's centre and the placed shape is an obvious diamond.
constexpr arbc::Rect k_centred{-5.0, -5.0, 5.0, 5.0};

// Rotate 45 degrees, shear x by 0.5y, then translate to (50, 50): an ARBITRARY affine, the
// case D-selection-3 exists for. The placed shape is a parallelogram whose AABB has corners
// nowhere near it.
arbc::Affine rotated_sheared_placement() {
  const double k = std::sqrt(0.5);
  const arbc::Affine shear{1.0, 0.0, 0.5, 1.0, 0.0, 0.0};
  const arbc::Affine rotate{k, k, -k, k, 0.0, 0.0};
  return arbc::compose(arbc::Affine::translation(50.0, 50.0), arbc::compose(rotate, shear));
}

// Tight tolerances (composition units) so the camera's label band and its border are
// distinguishable from each other and from a 200-unit interior.
constexpr double k_edge_tol = 2.0;
constexpr double k_corner_tol = 3.0;

// The golden fixture (reused verbatim from tests/cell_model_test.cpp's nested-insert golden):
// a bounded 32x32 red solid in its own composition, embedded in the probe's green 64x64 root
// as an `org.arbc.nested` cell. Reused here as the KNOWN-GOOD starting image, so a regression
// in the render path is distinguishable from a regression in selection.
constexpr int k_child_edge = 32;

arbc::ObjectId add_child_composition(arbc::Document& doc) {
  const arbc::ObjectId child =
      doc.add_composition(static_cast<double>(k_child_edge), static_cast<double>(k_child_edge));
  const arbc::ObjectId content = doc.add_content(std::make_shared<arbc::SolidContent>(
      arbc::Rgba{0.6F, 0.0F, 0.0F, 1.0F},
      arbc::Rect{0.0, 0.0, static_cast<double>(k_child_edge), static_cast<double>(k_child_edge)}));
  doc.attach_layer(child, doc.add_layer(content, arbc::Affine::identity()));
  return child;
}

arbc::Registry selection_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  return registry;
}

// The probe document + the nested cell at the placement the shipped UI path computes — the
// exact graph behind `tests/goldens/cells_insert_nested_64x64.rgba8`.
ace::project::ProbeDocument build_golden_fixture(const arbc::Registry& registry) {
  ace::project::ProbeDocument probe = ace::project::build_probe_document();
  const arbc::ObjectId child = add_child_composition(*probe.document);
  const arbc::Rect child_extent{0.0, 0.0, static_cast<double>(k_child_edge),
                                static_cast<double>(k_child_edge)};
  const arbc::Affine placement =
      ace::interact::place_in_view(arbc::Affine::identity(), ace::project::k_probe_width,
                                   ace::project::k_probe_height, child_extent);
  REQUIRE(ace::scene::add_cell(*probe.document, registry, "org.arbc.nested",
                               std::to_string(child.value), placement)
              .has_value());
  return probe;
}

void apply_change(Selection& selection, const SelectionChange& change) {
  // The same four-arm switch `app::CanvasView` runs — kept here so the unit lane exercises the
  // policy end-to-end onto the shipped verbs without linking L4.
  switch (change.op) {
  case SelectOp::None:
    break;
  case SelectOp::Replace:
    selection.replace_all(change.ids);
    break;
  case SelectOp::Add:
    selection.add_all(change.ids);
    break;
  case SelectOp::Toggle:
    for (const arbc::ObjectId id : change.ids) {
      selection.toggle(id);
    }
    break;
  case SelectOp::Clear:
    selection.clear();
    break;
  }
}

// The full gesture suite the byte-invariance + no-transaction cases drive: click, Cmd-click
// cycle, marquee, Shift-marquee, Select-All, Escape.
void drive_gesture_suite(Selection& selection, const std::vector<PickTarget>& targets) {
  const arbc::Vec2 over{32.0, 32.0};
  apply_change(selection, click_selection(targets, over, k_edge_tol, k_corner_tol,
                                          PickModifiers{false, false}, selection.primary()));
  for (int i = 0; i < 3; ++i) { // Cmd/Ctrl-click cycles down the stack, wrapping
    apply_change(selection, click_selection(targets, over, k_edge_tol, k_corner_tol,
                                            PickModifiers{false, true}, selection.primary()));
  }
  apply_change(selection, marquee_selection(targets, arbc::Rect{0.0, 0.0, 64.0, 64.0}, false));
  apply_change(selection, marquee_selection(targets, arbc::Rect{0.0, 0.0, 64.0, 64.0}, true));
  selection.replace_all(all_ids(targets)); // Select-All (Cmd/Ctrl-A)
  selection.clear();                       // Deselect (Escape)
}

} // namespace

// --- Cell body: exact under an arbitrary affine (D-selection-3) ----------------------------

TEST_CASE("selection: a cell body is hit exactly under a rotated+sheared placement, not by its "
          "AABB") {
  const arbc::Affine placement = rotated_sheared_placement();
  const std::vector<PickTarget> targets = {cell_target(1, placement, k_centred)};

  // The placed centre is a hit.
  const arbc::Vec2 centre = placement.apply(arbc::Vec2{0.0, 0.0});
  const PickHit inside = pick(targets, centre, k_edge_tol, k_corner_tol);
  CHECK(inside.hit);
  CHECK(inside.id == oid(1));
  CHECK(inside.kind == PickKind::Cell);
  CHECK(inside.handle == FrameHandle::None); // a body is a region, not an outline handle

  // A point INSIDE the placed AABB but OUTSIDE the parallelogram: the assertion an AABB
  // implementation fails. The AABB's own corner is the sharpest such point.
  const arbc::Rect aabb = placement.map_rect(k_centred);
  const arbc::Vec2 aabb_corner{aabb.x0 + 0.5, aabb.y0 + 0.5};
  REQUIRE(aabb_corner.x > aabb.x0);
  REQUIRE(aabb_corner.x < aabb.x1);
  REQUIRE(aabb_corner.y > aabb.y0);
  REQUIRE(aabb_corner.y < aabb.y1);
  CHECK_FALSE(pick(targets, aabb_corner, k_edge_tol, k_corner_tol).hit);
  CHECK(pick_stack(targets, aabb_corner, k_edge_tol, k_corner_tol).empty());

  // A cell body takes NO tolerance: a point just outside the placed quad is a miss even at a
  // generous tolerance (Constraint 4).
  const std::optional<std::array<arbc::Vec2, 4>> quad = placed_quad(targets.front());
  REQUIRE(quad.has_value());
  CHECK_FALSE(pick(targets, aabb_corner, 50.0, 50.0).hit);
}

// --- The camera's interior is click-through (the D7 assertion) -----------------------------

TEST_CASE("selection: a camera is grabbed only by its border/label; its interior is "
          "click-through to the cell it frames") {
  // A 200x200 camera at the origin, framing a 50x50 cell at (75,75) — fully inside it.
  const std::vector<PickTarget> targets = {
      cell_target(1, arbc::Affine::translation(75.0, 75.0), arbc::Rect{0.0, 0.0, 50.0, 50.0}),
      camera_target(2, arbc::Affine::identity(), 200, 200)};

  // (a) A click in the SHARED interior returns the CELL, not the camera — a camera never
  // blocks editing the art inside it.
  const PickHit shared = pick(targets, arbc::Vec2{100.0, 100.0}, k_edge_tol, k_corner_tol);
  REQUIRE(shared.hit);
  CHECK(shared.id == oid(1));
  CHECK(shared.kind == PickKind::Cell);
  // The camera contributes NO entry at all to the interior stack.
  const std::vector<PickHit> interior =
      pick_stack(targets, arbc::Vec2{100.0, 100.0}, k_edge_tol, k_corner_tol);
  REQUIRE(interior.size() == 1);
  CHECK(interior.front().kind == PickKind::Cell);

  // (b) A click on the camera's BORDER returns the camera with a non-None handle — delegation
  // to the shipped `hit_frame` asserted by the handle, not by re-deriving the geometry.
  const PickHit border = pick(targets, arbc::Vec2{0.0, 100.0}, k_edge_tol, k_corner_tol);
  REQUIRE(border.hit);
  CHECK(border.id == oid(2));
  CHECK(border.kind == PickKind::Camera);
  CHECK(border.handle != FrameHandle::None);
  CHECK(border.handle == ace::interact::hit_frame(arbc::Affine::identity(), 200, 200,
                                                  arbc::Vec2{0.0, 100.0}, k_edge_tol,
                                                  k_corner_tol));

  // (c) A click on the LABEL band just outside the top edge returns the camera with Label.
  const PickHit label = pick(targets, arbc::Vec2{40.0, -4.0}, k_edge_tol, k_corner_tol);
  REQUIRE(label.hit);
  CHECK(label.id == oid(2));
  CHECK(label.handle == FrameHandle::Label);

  // (d) A point far outside everything hits nothing.
  CHECK_FALSE(pick(targets, arbc::Vec2{-500.0, -500.0}, k_edge_tol, k_corner_tol).hit);
}

// --- Z-order, topmost-first, cameras above every cell (Constraint 5) -----------------------

TEST_CASE("selection: the pick stack is z-ordered front-to-back and cameras sort above cells") {
  const arbc::Rect square{0.0, 0.0, 10.0, 10.0};
  const std::vector<PickTarget> cells = {cell_target(1, arbc::Affine::identity(), square),
                                         cell_target(2, arbc::Affine::identity(), square),
                                         cell_target(3, arbc::Affine::identity(), square)};

  // Three overlapping cells at a common point: front-to-back is the REVERSE of layer order.
  const std::vector<PickHit> stack =
      pick_stack(cells, arbc::Vec2{5.0, 5.0}, k_edge_tol, k_corner_tol);
  REQUIRE(stack.size() == 3);
  CHECK(stack[0].id == oid(3));
  CHECK(stack[1].id == oid(2));
  CHECK(stack[2].id == oid(1));
  CHECK(stack[0].index == 2); // the index back into the input span
  CHECK(pick(cells, arbc::Vec2{5.0, 5.0}, k_edge_tol, k_corner_tol).id == oid(3));

  // A camera overlapping all three, grabbed at its border, sorts above every cell.
  std::vector<PickTarget> with_camera = cells;
  with_camera.push_back(camera_target(4, arbc::Affine::identity(), 10, 10));
  const std::vector<PickHit> merged =
      pick_stack(with_camera, arbc::Vec2{0.0, 5.0}, k_edge_tol, k_corner_tol);
  REQUIRE(merged.size() == 4);
  CHECK(merged.front().id == oid(4));
  CHECK(merged.front().kind == PickKind::Camera);
  for (std::size_t i = 1; i < merged.size(); ++i) {
    CHECK(merged[i].kind == PickKind::Cell);
  }
}

// --- Select-behind cycles and wraps, with no hidden state (D-selection-4) -------------------

TEST_CASE("selection: select-behind walks front->middle->back->front, driven only by the "
          "current selection") {
  const arbc::Rect square{0.0, 0.0, 10.0, 10.0};
  const std::vector<PickTarget> targets = {cell_target(1, arbc::Affine::identity(), square),
                                           cell_target(2, arbc::Affine::identity(), square),
                                           cell_target(3, arbc::Affine::identity(), square)};
  const arbc::Vec2 point{5.0, 5.0};

  // With nothing selected it is exactly `pick`.
  CHECK(pick_behind(targets, point, k_edge_tol, k_corner_tol, arbc::ObjectId{}).id == oid(3));
  // With an id that is not in this stack it is also exactly `pick`.
  CHECK(pick_behind(targets, point, k_edge_tol, k_corner_tol, oid(999)).id == oid(3));

  // Repeated cycling visits front -> middle -> back -> front.
  arbc::ObjectId selected = oid(3);
  selected = pick_behind(targets, point, k_edge_tol, k_corner_tol, selected).id;
  CHECK(selected == oid(2));
  selected = pick_behind(targets, point, k_edge_tol, k_corner_tol, selected).id;
  CHECK(selected == oid(1));
  selected = pick_behind(targets, point, k_edge_tol, k_corner_tol, selected).id;
  CHECK(selected == oid(3)); // wrapped

  // A miss stays a miss under the behind gate.
  CHECK_FALSE(pick_behind(targets, arbc::Vec2{-99.0, -99.0}, k_edge_tol, k_corner_tol, oid(3)).hit);
}

// --- Unbounded content: click-selectable, never marquee-selectable (Constraint 6) ----------

TEST_CASE("selection: unbounded content is hit by a click anywhere and excluded from every "
          "marquee") {
  const std::vector<PickTarget> targets = {
      cell_target(1, arbc::Affine::identity(), std::nullopt), // the factory-built solid
      cell_target(2, arbc::Affine::translation(100.0, 100.0), arbc::Rect{0.0, 0.0, 10.0, 10.0})};

  // A click FAR outside every bounded target still hits the unbounded one — it genuinely
  // covers the plane, which is what you see there.
  const PickHit far = pick(targets, arbc::Vec2{-5000.0, 4000.0}, k_edge_tol, k_corner_tol);
  REQUIRE(far.hit);
  CHECK(far.id == oid(1));

  // Where they overlap it sits at the BOTTOM of the stack — what Cmd/Ctrl-click reaches after
  // the bounded cells above it.
  const std::vector<PickHit> stack =
      pick_stack(targets, arbc::Vec2{105.0, 105.0}, k_edge_tol, k_corner_tol);
  REQUIRE(stack.size() == 2);
  CHECK(stack.front().id == oid(2));
  CHECK(stack.back().id == oid(1));

  // A marquee covering the WHOLE composition still never selects it (the deliberate asymmetry).
  const std::vector<arbc::ObjectId> caught =
      marquee(targets, arbc::Rect{-10000.0, -10000.0, 10000.0, 10000.0});
  REQUIRE(caught.size() == 1);
  CHECK(caught.front() == oid(2));
  CHECK_FALSE(placed_quad(targets.front()).has_value()); // no outline chrome either
}

// --- Marquee overlap is exact for arbitrary placements (Constraint 7 / D-selection-5) -------

TEST_CASE("selection: marquee overlap is exact — an AABB-only overlap does not select") {
  const arbc::Affine placement = rotated_sheared_placement();
  const std::vector<PickTarget> targets = {cell_target(1, placement, k_centred)};
  const arbc::Rect aabb = placement.map_rect(k_centred);

  // A marquee tucked into the AABB's corner: it OVERLAPS the AABB but not the parallelogram.
  const arbc::Rect corner_rect{aabb.x0 + 0.1, aabb.y0 + 0.1, aabb.x0 + 1.5, aabb.y0 + 1.5};
  REQUIRE_FALSE(corner_rect.intersect(aabb).empty()); // it really does overlap the AABB
  CHECK(marquee(targets, corner_rect).empty());       // …and is still not selected

  // Touching (partial) overlap selects — enclosure is NOT required.
  const arbc::Vec2 centre = placement.apply(arbc::Vec2{0.0, 0.0});
  const arbc::Rect touching{centre.x - 1.0, centre.y - 1.0, centre.x + 20.0, centre.y + 20.0};
  REQUIRE(marquee(targets, touching).size() == 1);
  CHECK(marquee(targets, touching).front() == oid(1));

  // A marquee fully INSIDE a large cell selects it (no separating axis exists).
  const std::vector<PickTarget> big = {
      cell_target(2, arbc::Affine::identity(), arbc::Rect{0.0, 0.0, 100.0, 100.0})};
  REQUIRE(marquee(big, arbc::Rect{40.0, 40.0, 60.0, 60.0}).size() == 1);

  // An empty / degenerate marquee selects nothing.
  CHECK(marquee(big, arbc::Rect{10.0, 10.0, 10.0, 10.0}).empty());
  CHECK(marquee(big, arbc::Rect{60.0, 60.0, 40.0, 40.0}).empty()); // built backwards
}

// --- The modifier policy (D-selection-2) ---------------------------------------------------

TEST_CASE("selection: the click/marquee modifier policy returns the right SelectionChange") {
  const arbc::Rect square{0.0, 0.0, 10.0, 10.0};
  const std::vector<PickTarget> targets = {cell_target(1, arbc::Affine::identity(), square),
                                           cell_target(2, arbc::Affine::identity(), square)};
  const arbc::Vec2 hit{5.0, 5.0};
  const arbc::Vec2 miss{-99.0, -99.0};

  const SelectionChange plain_hit =
      click_selection(targets, hit, k_edge_tol, k_corner_tol, PickModifiers{}, arbc::ObjectId{});
  CHECK(plain_hit.op == SelectOp::Replace);
  REQUIRE(plain_hit.ids.size() == 1);
  CHECK(plain_hit.ids.front() == oid(2)); // the topmost

  CHECK(click_selection(targets, miss, k_edge_tol, k_corner_tol, PickModifiers{}, arbc::ObjectId{})
            .op == SelectOp::Clear);

  const SelectionChange shift_hit = click_selection(targets, hit, k_edge_tol, k_corner_tol,
                                                    PickModifiers{true, false}, arbc::ObjectId{});
  CHECK(shift_hit.op == SelectOp::Toggle);
  REQUIRE(shift_hit.ids.size() == 1);
  CHECK(shift_hit.ids.front() == oid(2));

  // A Shift-MISS must not wipe the selection.
  CHECK(click_selection(targets, miss, k_edge_tol, k_corner_tol, PickModifiers{true, false},
                        arbc::ObjectId{})
            .op == SelectOp::None);

  // Cmd/Ctrl-click replaces with the entry behind the current one.
  const SelectionChange behind =
      click_selection(targets, hit, k_edge_tol, k_corner_tol, PickModifiers{false, true}, oid(2));
  CHECK(behind.op == SelectOp::Replace);
  REQUIRE(behind.ids.size() == 1);
  CHECK(behind.ids.front() == oid(1));

  // Marquee: plain replaces, Shift adds, empty plain clears, empty Shift is inert.
  const arbc::Rect over{0.0, 0.0, 10.0, 10.0};
  const arbc::Rect nowhere{500.0, 500.0, 510.0, 510.0};
  const SelectionChange sweep = marquee_selection(targets, over, false);
  CHECK(sweep.op == SelectOp::Replace);
  CHECK(sweep.ids.size() == 2);
  CHECK(marquee_selection(targets, over, true).op == SelectOp::Add);
  CHECK(marquee_selection(targets, nowhere, false).op == SelectOp::Clear);
  CHECK(marquee_selection(targets, nowhere, true).op == SelectOp::None);
  CHECK(marquee_selection(targets, nowhere, true).ids.empty());
}

// --- Degenerate inputs return misses, never NaN (the D-fit_bounds-3 discipline) ------------

TEST_CASE("selection: degenerate placements/extents/resolutions/points are misses, never NaN") {
  const arbc::Vec2 origin{0.0, 0.0};

  // A non-invertible placement.
  const std::vector<PickTarget> collapsed = {
      cell_target(1, arbc::Affine{0.0, 0.0, 0.0, 0.0, 0.0, 0.0}, arbc::Rect{0.0, 0.0, 10.0, 10.0})};
  CHECK_FALSE(pick(collapsed, origin, k_edge_tol, k_corner_tol).hit);
  CHECK(pick_stack(collapsed, origin, k_edge_tol, k_corner_tol).empty());
  CHECK(marquee(collapsed, arbc::Rect{-50.0, -50.0, 50.0, 50.0}).empty());
  CHECK_FALSE(placed_quad(collapsed.front()).has_value());

  // A zero-area extent.
  const std::vector<PickTarget> flat = {
      cell_target(2, arbc::Affine::identity(), arbc::Rect{5.0, 5.0, 5.0, 5.0})};
  CHECK_FALSE(pick(flat, arbc::Vec2{5.0, 5.0}, k_edge_tol, k_corner_tol).hit);
  CHECK(marquee(flat, arbc::Rect{-50.0, -50.0, 50.0, 50.0}).empty());

  // A non-positive camera resolution.
  const std::vector<PickTarget> zero_res = {camera_target(3, arbc::Affine::identity(), 0, 0)};
  CHECK_FALSE(pick(zero_res, origin, k_edge_tol, k_corner_tol).hit);
  CHECK(pick_stack(zero_res, origin, k_edge_tol, k_corner_tol).empty());

  // A non-finite pick point.
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  const std::vector<PickTarget> ok = {
      cell_target(4, arbc::Affine::identity(), arbc::Rect{0.0, 0.0, 10.0, 10.0})};
  CHECK_FALSE(pick(ok, arbc::Vec2{nan, 5.0}, k_edge_tol, k_corner_tol).hit);
  CHECK_FALSE(pick(ok, arbc::Vec2{5.0, inf}, k_edge_tol, k_corner_tol).hit);
  CHECK(pick_stack(ok, arbc::Vec2{nan, nan}, k_edge_tol, k_corner_tol).empty());
  // …and a non-finite marquee rect selects nothing.
  CHECK(marquee(ok, arbc::Rect{nan, nan, 10.0, 10.0}).empty());

  // An empty target list is a miss on every entry point.
  const std::vector<PickTarget> none;
  CHECK_FALSE(pick(none, origin, k_edge_tol, k_corner_tol).hit);
  CHECK_FALSE(pick_behind(none, origin, k_edge_tol, k_corner_tol, oid(1)).hit);
  CHECK(marquee(none, arbc::Rect{0.0, 0.0, 10.0, 10.0}).empty());
  CHECK(all_ids(none).empty());
}

// --- `pick_targets` assembly over a real Document (A17 (b)) ---------------------------------

TEST_CASE("selection: pick_targets assembles cells in layer order then cameras above them") {
  arbc::Registry registry = selection_registry();
  arbc::Document doc;
  doc.add_composition(64.0, 64.0);

  const arbc::Affine first_at = arbc::Affine::translation(4.0, 4.0);
  const arbc::Affine second_at = arbc::Affine::translation(20.0, 20.0);
  const auto first = ace::scene::add_cell(doc, registry, "org.arbc.raster", "16x16", first_at);
  const auto second = ace::scene::add_cell(doc, registry, "org.arbc.raster", "8x8", second_at);
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  const arbc::Affine frame = arbc::Affine::translation(8.0, 8.0);
  const arbc::ObjectId camera =
      ace::scene::add_camera(doc, registry, "Hero", ace::scene::Resolution{32, 24}, frame);
  REQUIRE(camera.valid());

  const std::vector<PickTarget> targets = pick_targets(doc, registry);
  REQUIRE(targets.size() == 3);

  // The two cells, in layer order, with their LIVE extents — which must agree with the shipped
  // pre-mint probe of the same kind/config.
  CHECK(targets[0].kind == PickKind::Cell);
  CHECK(targets[0].id == *first);
  CHECK(targets[0].placement == first_at);
  const auto probed = ace::scene::probe_bounds(registry, "org.arbc.raster", "16x16");
  REQUIRE(probed.has_value());
  CHECK(targets[0].extent == *probed);
  CHECK(targets[1].kind == PickKind::Cell);
  CHECK(targets[1].id == *second);
  CHECK(targets[1].placement == second_at);

  // The camera LAST (above every cell), its FRAME as placement and its OUTPUT rectangle as
  // extent — not `Content::bounds()`, which is deliberately empty for a camera (A14).
  CHECK(targets[2].kind == PickKind::Camera);
  CHECK(targets[2].id == camera);
  CHECK(targets[2].placement == frame);
  REQUIRE(targets[2].extent.has_value());
  CHECK(*targets[2].extent == arbc::Rect{0.0, 0.0, 32.0, 24.0});
  // The camera's own layer is carried through (what a frame edit targets).
  CHECK(targets[2].layer == ace::scene::cameras(doc).front().layer);

  // And the camera really is pickable only by its border: its interior is click-through.
  const arbc::Vec2 interior = frame.apply(arbc::Vec2{16.0, 12.0});
  const PickHit inside = pick(targets, interior, 1.0, 1.5);
  CHECK((!inside.hit || inside.kind == PickKind::Cell));
}

TEST_CASE("selection: a cell whose kind token does not resolve is still a pick target") {
  arbc::Registry registry = selection_registry();
  arbc::Document doc;
  const arbc::ObjectId comp = doc.add_composition(64.0, 64.0);
  // Minted with NO kind token (the `build_probe_document` shape) and UNBOUNDED — `cells()`
  // reports it with an empty kind_id, and picking must still reach it.
  const arbc::ObjectId content =
      doc.add_content(std::make_shared<arbc::SolidContent>(arbc::Rgba{0.0F, 0.5F, 0.0F, 1.0F}));
  doc.attach_layer(comp, doc.add_layer(content, arbc::Affine::identity()));

  REQUIRE(ace::scene::cells(doc, registry).size() == 1);
  CHECK(ace::scene::cells(doc, registry).front().kind_id.empty());
  CHECK_FALSE(ace::scene::cells(doc, registry).front().content_bounds.has_value());

  const std::vector<PickTarget> targets = pick_targets(doc, registry);
  REQUIRE(targets.size() == 1);
  CHECK(targets.front().id == content);
  CHECK_FALSE(targets.front().extent.has_value()); // unbounded, so a click anywhere hits it
  CHECK(pick(targets, arbc::Vec2{-1234.0, 5678.0}, k_edge_tol, k_corner_tol).hit);
}

// --- The `commands::Selection` verbs this leaf adds -----------------------------------------

TEST_CASE("selection: prune drops stale ids, preserves order, and re-points a dead primary") {
  Selection selection;
  selection.add(oid(1));
  selection.add(oid(2));
  selection.add(oid(3));
  REQUIRE(selection.size() == 3);
  REQUIRE(selection.primary() == oid(3));

  // A live set missing a NON-primary member: that id is gone, order and primary survive.
  const std::vector<arbc::ObjectId> live_without_2 = {oid(1), oid(3), oid(9)};
  selection.prune(live_without_2);
  REQUIRE(selection.size() == 2);
  CHECK(selection.items()[0] == oid(1));
  CHECK(selection.items()[1] == oid(3));
  CHECK(selection.primary() == oid(3));
  CHECK_FALSE(selection.contains(oid(2)));

  // Pruning away the PRIMARY re-points it to a surviving member.
  const std::vector<arbc::ObjectId> live_without_3 = {oid(1)};
  selection.prune(live_without_3);
  REQUIRE(selection.size() == 1);
  CHECK(selection.primary() == oid(1));

  // Pruning against an empty live set is `clear()`.
  selection.prune({});
  CHECK(selection.empty());
  CHECK_FALSE(selection.primary().valid());

  // Pruning an already-empty selection is inert.
  selection.prune(live_without_2);
  CHECK(selection.empty());
}

TEST_CASE("selection: replace_all sets the primary to the last id; add_all is duplicate-safe") {
  Selection selection;
  const std::vector<arbc::ObjectId> first = {oid(1), oid(2), oid(3)};
  selection.replace_all(first);
  REQUIRE(selection.size() == 3);
  CHECK(selection.primary() == oid(3));

  const std::vector<arbc::ObjectId> second = {oid(7), oid(8)};
  selection.replace_all(second);
  REQUIRE(selection.size() == 2);
  CHECK(selection.items()[0] == oid(7));
  CHECK(selection.primary() == oid(8));
  CHECK_FALSE(selection.contains(oid(1))); // previous members dropped

  // Adding is duplicate-safe against the current members AND within the argument, and it
  // preserves the existing order.
  const std::vector<arbc::ObjectId> more = {oid(8), oid(9), oid(9)};
  selection.add_all(more);
  REQUIRE(selection.size() == 3);
  CHECK(selection.items()[0] == oid(7));
  CHECK(selection.items()[1] == oid(8));
  CHECK(selection.items()[2] == oid(9));
  CHECK(selection.primary() == oid(9));

  // An empty replace is `clear()`; an empty add is inert.
  selection.add_all({});
  CHECK(selection.size() == 3);
  selection.replace_all({});
  CHECK(selection.empty());
  CHECK_FALSE(selection.primary().valid());
}

// --- Identity survives a rename (the D7 / rename_stable_id payoff) --------------------------

TEST_CASE("selection: a camera's selection and its pick-target index survive a rename") {
  arbc::Registry registry = selection_registry();
  arbc::Document doc;
  doc.add_composition(64.0, 64.0);
  REQUIRE(ace::scene::add_cell(doc, registry, "org.arbc.raster", "16x16",
                               arbc::Affine::translation(4.0, 4.0))
              .has_value());
  const arbc::ObjectId camera = ace::scene::add_camera(
      doc, registry, "Hero", ace::scene::Resolution{32, 32}, arbc::Affine::translation(8.0, 8.0));
  REQUIRE(camera.valid());

  Selection selection;
  selection.select(camera);
  const std::vector<PickTarget> before = pick_targets(doc, registry);
  REQUIRE(before.size() == 2);
  REQUIRE(before[1].id == camera);

  REQUIRE(ace::scene::rename_camera(doc, registry, camera, "Establishing"));

  // The renamed camera is the SAME object: an ObjectId-keyed selection keeps its handle, and
  // the pick stack still reports it at the same index with the same frame. This is the
  // assertion that would fail if a rename ever went back to minting a new ObjectId.
  CHECK(selection.contains(camera));
  CHECK(selection.primary() == camera);
  const std::vector<PickTarget> after = pick_targets(doc, registry);
  REQUIRE(after.size() == 2);
  CHECK(after[1].id == camera);
  CHECK(after[1].placement == before[1].placement);
  CHECK(after[1].layer == before[1].layer);
  CHECK(ace::scene::cameras(doc).front().name == "Establishing");
  // …and pruning against the live set after the rename keeps it selected.
  selection.prune(all_ids(after));
  CHECK(selection.contains(camera));
}

// --- Rendered output: byte-INVARIANCE, which is stronger here than a new baseline -----------

TEST_CASE("selection: a full gesture suite leaves the rendered bytes byte-identical") {
  arbc::Registry registry = selection_registry();
  ace::project::ProbeDocument probe = build_golden_fixture(registry);
  REQUIRE(ace::scene::add_camera(*probe.document, registry, "Hero", ace::scene::Resolution{32, 32},
                                 arbc::Affine::translation(16.0, 16.0))
              .valid());

  const ace::render::Srgb8Image before = ace::render::render_document_srgb8(
      *probe.document, ace::project::k_probe_width, ace::project::k_probe_height);
  // The known-good starting image: the committed nested-insert golden. A camera contributes
  // ZERO pixels (A14), so adding one leaves the composite untouched — which is exactly why a
  // regression in the RENDER path is distinguishable from one in SELECTION here.
  REQUIRE(ace_test::compare_golden("cells_insert_nested_64x64.rgba8", before.pixels));

  const std::vector<PickTarget> targets = pick_targets(*probe.document, registry);
  REQUIRE(targets.size() == 3); // the probe's own solid, the nested cell, the camera
  Selection selection;
  drive_gesture_suite(selection, targets);

  const ace::render::Srgb8Image after = ace::render::render_document_srgb8(
      *probe.document, ace::project::k_probe_width, ace::project::k_probe_height);
  // Selection chrome is ImGui draw-list output over the pane; it NEVER enters the composited
  // image (Constraint 13), and nothing the compositor reads moved (Constraint 2).
  CHECK(after.pixels == before.pixels);
}

TEST_CASE("selection: a full gesture suite adds no journal entry and bumps no revision") {
  arbc::Registry registry = selection_registry();
  ace::project::ProbeDocument probe = build_golden_fixture(registry);
  REQUIRE(ace::scene::add_camera(*probe.document, registry, "Hero", ace::scene::Resolution{32, 32},
                                 arbc::Affine::translation(16.0, 16.0))
              .valid());

  const std::size_t depth_before = probe.document->journal().depth();
  const std::size_t cursor_before = probe.document->journal().cursor();
  const std::uint64_t revision_before = probe.document->pin()->revision();

  const std::vector<PickTarget> targets = pick_targets(*probe.document, registry);
  Selection selection;
  drive_gesture_suite(selection, targets);
  // Re-assembling the stack is itself a read: it must not move the document either.
  const std::vector<PickTarget> again = pick_targets(*probe.document, registry);
  CHECK(again.size() == targets.size());
  selection.prune(all_ids(again));

  // D15 / D-app_state-3: selection is transient app state. No click, drag, modifier, or
  // Select-All may open a transaction, add a JournalEntry, or bump the revision.
  CHECK(probe.document->journal().depth() == depth_before);
  CHECK(probe.document->journal().cursor() == cursor_before);
  CHECK(probe.document->pin()->revision() == revision_before);
}
