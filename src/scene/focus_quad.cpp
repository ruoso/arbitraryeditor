// The entered-composition focus quad (editor.canvas.isolation_scope / D-isolation_scope-4).
//
// Float contraction is disabled for the whole TU: the four corners this returns feed the render's
// per-pixel dim MASK (a byte-exact render_offline golden), so the placement/compose arithmetic must
// be bit-identical across the gcc/clang × debug/release/asan/tsan matrix. `-ffp-contract` defaults
// DIFFER between gcc and clang, so a bare `a*x + c*y + tx` could fuse to an FMA on one and not the
// other and drift a corner by a ULP — enough to flip a pixel at a quad edge. OFF pins +,-,* to
// correctly-rounded IEEE, the same discipline the TrueType caption goldens rely on.
#pragma STDC FP_CONTRACT OFF

#include <ace/scene/cell.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/contract/content.hpp>
#include <arbc/model/model.hpp>
#include <arbc/model/records.hpp>
#include <arbc/runtime/document.hpp>

#include <array>
#include <optional>

namespace ace::scene {
namespace {

// The root composition id (lowest-id-wins, mirroring cell.cpp's file-static helper). Invalid when
// the document holds no composition at all.
arbc::ObjectId root_composition_id(const arbc::DocRoot& state) {
  arbc::ObjectId root_id;
  const arbc::CompositionRecord* rec = nullptr;
  if (!state.find_first_composition(root_id, rec) || rec == nullptr) {
    return arbc::ObjectId{};
  }
  return root_id;
}

// Depth-first descent from `comp` (whose composition-space -> ROOT-space transform is `to_root`)
// for the layer whose content wraps `target`. On success sets `out_to_root` to that wrapping
// content's CONTENT-space -> ROOT-space transform and `out_content` to its content id, then returns
// true. Registry-free (A16): only the generic `composition_ref()` link is followed, never a
// `kind_id` switch — so this needs no `KindBridge`/`Registry`. The composition_ref graph is a
// cycle-free DAG (the same property cell.cpp's PathBuilder relies on), so no visited set is needed.
bool descend(const arbc::Document& document, const arbc::DocRoot& state, arbc::ObjectId comp,
             const arbc::Affine& to_root, arbc::ObjectId target, arbc::Affine& out_to_root,
             arbc::ObjectId& out_content) {
  bool found = false;
  state.for_each_layer_in(comp, [&](arbc::ObjectId layer_id) {
    if (found) {
      return; // the wrapping cell is already located; ignore the rest of this composition.
    }
    const arbc::LayerRecord* layer = state.find_layer(layer_id);
    if (layer == nullptr || !layer->content.valid()) {
      return;
    }
    arbc::Content* content = document.resolve(layer->content);
    if (content == nullptr) {
      return;
    }
    const arbc::ObjectId child = content->composition_ref();
    if (!child.valid()) {
      return; // a plain (non-nested) cell — a camera, raster, solid, etc.
    }
    // content space -> comp space (the layer's placement) -> root space (the accumulator).
    const arbc::Affine child_to_root = arbc::compose(to_root, layer->transform);
    if (child == target) {
      out_to_root = child_to_root;
      out_content = layer->content;
      found = true;
      return;
    }
    // Multi-level nesting: the wrapping cell may live deeper. Only descend into a LIVE child
    // composition, carrying the accumulated transform so the returned quad lands in root space.
    if (state.find_composition(child) != nullptr &&
        descend(document, state, child, child_to_root, target, out_to_root, out_content)) {
      found = true;
    }
  });
  return found;
}

} // namespace

std::optional<FocusQuad> composition_focus_quad(const arbc::Document& document,
                                                std::optional<arbc::ObjectId> entered) {
  if (!entered || !entered->valid()) {
    return std::nullopt; // nullopt == Root: no focus, no dim.
  }
  const arbc::DocStatePtr state = document.pin();
  if (!state) {
    return std::nullopt;
  }
  if (state->find_composition(*entered) == nullptr) {
    return std::nullopt; // GC'd / undone-away / foreign id — fail-safe to no dim (Constraint 6).
  }
  const arbc::ObjectId root = root_composition_id(*state);
  if (!root.valid() || *entered == root) {
    return std::nullopt; // entering Root itself is not an isolation scope.
  }
  arbc::Affine to_root = arbc::Affine::identity();
  arbc::ObjectId wrapping_content;
  if (!descend(document, *state, root, arbc::Affine::identity(), *entered, to_root,
               wrapping_content)) {
    return std::nullopt; // no wrapping nested cell reaches this composition from Root.
  }
  arbc::Content* content = document.resolve(wrapping_content);
  if (content == nullptr) {
    return std::nullopt;
  }
  const std::optional<arbc::Rect> bounds = content->bounds();
  if (!bounds || bounds->empty()) {
    return std::nullopt; // unbounded child: no finite focus region (Constraint 5 — never dims all).
  }
  FocusQuad quad;
  quad.corners[0] = to_root.apply(arbc::Vec2{bounds->x0, bounds->y0});
  quad.corners[1] = to_root.apply(arbc::Vec2{bounds->x1, bounds->y0});
  quad.corners[2] = to_root.apply(arbc::Vec2{bounds->x1, bounds->y1});
  quad.corners[3] = to_root.apply(arbc::Vec2{bounds->x0, bounds->y1});
  return quad;
}

} // namespace ace::scene
