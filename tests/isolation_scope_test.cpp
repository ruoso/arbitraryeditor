// editor.canvas.isolation_scope — L1 focus-quad logic (Catch2, headless) + the byte-exact
// render_offline goldens for the canvas isolation dim (docs/00-design.md D17). The scope model, the
// re-root and the breadcrumb shipped with editor.panels.layers; this leaf owns the CANVAS
// reflection: scene::composition_focus_quad (pure geometry over the pinned snapshot) and the L2
// render dim composite reached through render_document_srgb8_scoped. The dim composites in the
// LINEAR WORKING SPACE (D10) and a nullopt/unresolvable scope is byte-identical to the un-scoped
// render (Constraint 2 — the regression guarantee that keeps every other canvas/export golden
// valid).
#include <ace/project/project.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_nested/nested_content.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "golden_support.hpp"

namespace {

constexpr int k_dim = 64; // the probe composition + render size (matches k_probe_width/height)
constexpr int k_child_edge = 32; // the nested child composition's bounded extent

arbc::Registry make_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry); // org.arbc.solid / .nested / ...
  ace::scene::register_camera_kind(registry);
  return registry;
}

// A bounded RED child composition (so the nested cell has a real extent to place and an observable
// "inside" region). Returns its composition id.
arbc::ObjectId add_red_child(arbc::Document& doc) {
  const arbc::ObjectId child =
      doc.add_composition(static_cast<double>(k_child_edge), static_cast<double>(k_child_edge));
  const arbc::ObjectId content = doc.add_content(std::make_shared<arbc::SolidContent>(
      arbc::Rgba{0.85F, 0.0F, 0.0F, 1.0F},
      arbc::Rect{0.0, 0.0, static_cast<double>(k_child_edge), static_cast<double>(k_child_edge)}));
  const arbc::ObjectId layer = doc.add_layer(content, arbc::Affine::identity());
  doc.attach_layer(child, layer);
  return child;
}

// A doc: the probe composition + a nested RED child placed by `placement` into root. Returns the
// child composition id (the scope to enter) via `out_child`. Renders once before returning so the
// runtime binder ATTACHES the NestedContent — until attached its bounds() is the empty placeholder
// {0,0,0,0}, and both the focus-quad geometry and the dim depend on the resolved child extent (the
// production offline/interactive paths always render before computing the quad).
ace::project::ProbeDocument scene_with_nested(const arbc::Registry& registry,
                                              const arbc::Affine& placement,
                                              arbc::ObjectId& out_child) {
  ace::project::ProbeDocument probe = ace::project::build_probe_document();
  out_child = add_red_child(*probe.document);
  REQUIRE(ace::scene::add_cell(*probe.document, registry, "org.arbc.nested",
                               std::to_string(out_child.value), placement)
              .has_value());
  (void)ace::render::render_document_srgb8(*probe.document, k_dim, k_dim);
  return probe;
}

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

bool corner_is(const arbc::Vec2& c, double x, double y) { return near(c.x, x) && near(c.y, y); }

} // namespace

TEST_CASE("isolation_scope: focus quad is the wrapping cell's placement × content_bounds") {
  const arbc::Registry registry = make_registry();
  arbc::ObjectId child;
  const arbc::Affine placement = arbc::Affine::translation(16.0, 16.0);
  const ace::project::ProbeDocument probe = scene_with_nested(registry, placement, child);

  const std::optional<ace::scene::FocusQuad> quad =
      ace::scene::composition_focus_quad(*probe.document, child);
  REQUIRE(quad.has_value());
  // The four corners of {0,0}..{32,32} translated by (16,16), in content-bounds order.
  CHECK(corner_is(quad->corners[0], 16.0, 16.0));
  CHECK(corner_is(quad->corners[1], 48.0, 16.0));
  CHECK(corner_is(quad->corners[2], 48.0, 48.0));
  CHECK(corner_is(quad->corners[3], 16.0, 48.0));
}

TEST_CASE("isolation_scope: rotated/sheared placement yields a true quad") {
  const arbc::Registry registry = make_registry();
  arbc::ObjectId child;
  // A rotation+shear (non-axis-aligned linear part): a,b,c,d = a genuine rotation-ish basis, so the
  // mapped corners are NOT an axis-aligned box.
  const arbc::Affine placement{1.0, 0.5, -0.25, 1.0, 20.0, 8.0};
  const ace::project::ProbeDocument probe = scene_with_nested(registry, placement, child);

  const std::optional<ace::scene::FocusQuad> quad =
      ace::scene::composition_focus_quad(*probe.document, child);
  REQUIRE(quad.has_value());

  // Each returned corner is the placement applied to the corresponding content-bounds corner — a
  // TRUE quad, not the AABB of the four.
  const double e = static_cast<double>(k_child_edge);
  const arbc::Vec2 src[4] = {{0.0, 0.0}, {e, 0.0}, {e, e}, {0.0, e}};
  for (std::size_t i = 0; i < 4; ++i) {
    const arbc::Vec2 want = placement.apply(src[i]);
    CHECK(near(quad->corners[i].x, want.x));
    CHECK(near(quad->corners[i].y, want.y));
  }
  // Non-axis-aligned: the top edge is not horizontal (corner 0 and 1 differ in y), proving the mask
  // dims the true complement rather than an AABB.
  CHECK(!near(quad->corners[0].y, quad->corners[1].y));
  CHECK(!near(quad->corners[1].x, quad->corners[2].x));
}

TEST_CASE("isolation_scope: multi-level nesting resolves the focus quad in ROOT space") {
  const arbc::Registry registry = make_registry();
  ace::project::ProbeDocument probe = ace::project::build_probe_document();
  arbc::Document& doc = *probe.document;

  // Grandchild G (24x24) inside child C (48x48) inside root: entering G must return its placed
  // extent accumulated up BOTH placements into root space (the descend recursion + arbc::compose).
  const arbc::ObjectId grand = doc.add_composition(24.0, 24.0);
  const arbc::ObjectId gred = doc.add_content(std::make_shared<arbc::SolidContent>(
      arbc::Rgba{0.8F, 0.0F, 0.0F, 1.0F}, arbc::Rect{0.0, 0.0, 24.0, 24.0}));
  doc.attach_layer(grand, doc.add_layer(gred, arbc::Affine::identity()));

  const arbc::ObjectId childc = doc.add_composition(48.0, 48.0);
  // Cell B: G nested INTO C at translate(4,4) — built raw since add_cell only targets root.
  const arbc::ObjectId nested_b = doc.add_content(std::make_shared<arbc::NestedContent>(grand));
  doc.attach_layer(childc, doc.add_layer(nested_b, arbc::Affine::translation(4.0, 4.0)));

  // Cell A: C nested INTO root at translate(8,8).
  REQUIRE(ace::scene::add_cell(doc, registry, "org.arbc.nested", std::to_string(childc.value),
                               arbc::Affine::translation(8.0, 8.0))
              .has_value());
  (void)ace::render::render_document_srgb8(doc, k_dim, k_dim); // attach the binder recursively

  const std::optional<ace::scene::FocusQuad> quad = ace::scene::composition_focus_quad(doc, grand);
  REQUIRE(quad.has_value());
  // compose(translate(8,8), translate(4,4)) = translate(12,12) applied to {0,0}..{24,24}.
  CHECK(corner_is(quad->corners[0], 12.0, 12.0));
  CHECK(corner_is(quad->corners[1], 36.0, 12.0));
  CHECK(corner_is(quad->corners[2], 36.0, 36.0));
  CHECK(corner_is(quad->corners[3], 12.0, 36.0));
}

TEST_CASE("isolation_scope: fail-safe — Root / unresolvable / non-nested id yields nullopt") {
  const arbc::Registry registry = make_registry();
  arbc::ObjectId child;
  const ace::project::ProbeDocument probe =
      scene_with_nested(registry, arbc::Affine::translation(16.0, 16.0), child);

  // nullopt scope == Root: no focus.
  CHECK(!ace::scene::composition_focus_quad(*probe.document, std::nullopt).has_value());
  // A GC'd / foreign id that names no live composition.
  CHECK(!ace::scene::composition_focus_quad(*probe.document, arbc::ObjectId{999999}).has_value());
  // A plain (non-composition) content id — the child's red solid content is not a composition.
  const std::vector<ace::scene::Cell> child_cells =
      ace::scene::cells(*probe.document, registry, child);
  REQUIRE(!child_cells.empty());
  CHECK(!ace::scene::composition_focus_quad(*probe.document, child_cells.front().id).has_value());
  // The root composition itself is not an isolation scope.
  const arbc::ObjectId root = ace::scene::active_composition(*probe.document, std::nullopt);
  CHECK(!ace::scene::composition_focus_quad(*probe.document, root).has_value());
}

TEST_CASE("isolation_scope: unbounded child (no content_bounds) yields nullopt") {
  const arbc::Registry registry = make_registry();
  ace::project::ProbeDocument probe = ace::project::build_probe_document();
  // A child composition with a NON-POSITIVE canvas whose only member is an UNBOUNDED solid: the
  // nested wrapper's bounds() then resolves through the union-of-members path to nullopt, so there
  // is no finite focus region to isolate (Constraint 5 — never dims the whole plane's complement).
  const arbc::ObjectId child = probe.document->add_composition(0.0, 0.0);
  const arbc::ObjectId content = probe.document->add_content(
      std::make_shared<arbc::SolidContent>(arbc::Rgba{0.3F, 0.3F, 0.3F, 1.0F}));
  const arbc::ObjectId layer = probe.document->add_layer(content, arbc::Affine::identity());
  probe.document->attach_layer(child, layer);
  REQUIRE(ace::scene::add_cell(*probe.document, registry, "org.arbc.nested",
                               std::to_string(child.value), arbc::Affine::translation(8.0, 8.0))
              .has_value());
  (void)ace::render::render_document_srgb8(*probe.document, k_dim, k_dim); // attach the binder

  CHECK(!ace::scene::composition_focus_quad(*probe.document, child).has_value());
  // And the scoped render is therefore byte-identical to the un-scoped one — no dim on an unbounded
  // child (would otherwise dim an empty complement).
  const ace::render::Srgb8Image baseline =
      ace::render::render_document_srgb8(*probe.document, k_dim, k_dim);
  CHECK(ace::render::render_document_srgb8_scoped(*probe.document, k_dim, k_dim,
                                                  arbc::Affine::identity(), child)
            .pixels == baseline.pixels);
}

TEST_CASE(
    "isolation_scope: no-scope render is byte-identical to the un-scoped render (Constraint 2)") {
  const arbc::Registry registry = make_registry();
  arbc::ObjectId child;
  const ace::project::ProbeDocument probe =
      scene_with_nested(registry, arbc::Affine::translation(16.0, 16.0), child);

  const ace::render::Srgb8Image baseline =
      ace::render::render_document_srgb8(*probe.document, k_dim, k_dim);
  // nullopt, a foreign id, and Root itself all resolve to no focus quad ⇒ no scrim ⇒ identical
  // bytes.
  CHECK(ace::render::render_document_srgb8_scoped(*probe.document, k_dim, k_dim,
                                                  arbc::Affine::identity(), std::nullopt)
            .pixels == baseline.pixels);
  CHECK(ace::render::render_document_srgb8_scoped(*probe.document, k_dim, k_dim,
                                                  arbc::Affine::identity(), arbc::ObjectId{999999})
            .pixels == baseline.pixels);
}

TEST_CASE("isolation_scope: the scoped render dims the complement of the focus quad (golden)") {
  const arbc::Registry registry = make_registry();
  arbc::ObjectId child;
  const ace::project::ProbeDocument probe =
      scene_with_nested(registry, arbc::Affine::translation(16.0, 16.0), child);

  const ace::render::Srgb8Image baseline =
      ace::render::render_document_srgb8(*probe.document, k_dim, k_dim);
  const ace::render::Srgb8Image dimmed = ace::render::render_document_srgb8_scoped(
      *probe.document, k_dim, k_dim, arbc::Affine::identity(), child);
  REQUIRE(dimmed.pixels.size() == static_cast<std::size_t>(k_dim) * k_dim * 4);

  const auto lum_at = [](const ace::render::Srgb8Image& img, int x, int y) {
    const std::size_t at =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) + x) * 4;
    return static_cast<int>(img.pixels[at]) + img.pixels[at + 1] + img.pixels[at + 2];
  };
  // Inside the child's placed extent [16,48): bright and UNCHANGED from the baseline.
  CHECK(lum_at(dimmed, 32, 32) == lum_at(baseline, 32, 32));
  // Outside it: measurably DARKER than the baseline (the dim scrim).
  CHECK(lum_at(dimmed, 4, 4) < lum_at(baseline, 4, 4));
  CHECK(lum_at(dimmed, 60, 60) < lum_at(baseline, 60, 60));

  CHECK(ace_test::compare_golden("isolation_scope_dim_64x64.rgba8", dimmed.pixels));
}

TEST_CASE("isolation_scope: the dim complement follows the true rotated quad (golden)") {
  const arbc::Registry registry = make_registry();
  arbc::ObjectId child;
  // A rotated placement: the child's placed extent is a diamond-ish quad, so the dimmed complement
  // must follow the true edges, not an axis-aligned box.
  const arbc::Affine placement{0.9, 0.35, -0.35, 0.9, 20.0, 6.0};
  const ace::project::ProbeDocument probe = scene_with_nested(registry, placement, child);

  const ace::render::Srgb8Image dimmed = ace::render::render_document_srgb8_scoped(
      *probe.document, k_dim, k_dim, arbc::Affine::identity(), child);
  REQUIRE(dimmed.pixels.size() == static_cast<std::size_t>(k_dim) * k_dim * 4);

  CHECK(ace_test::compare_golden("isolation_scope_dim_rotated_64x64.rgba8", dimmed.pixels));
}
