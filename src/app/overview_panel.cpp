#include <ace/app/canvas_view.hpp>
#include <ace/app/overview_panel.hpp>
#include <ace/app/pattern_swatch.hpp> // hatch_ink — the shared list↔overview color table (D-hatch_swatch-6)
#include <ace/app/view_framing.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/commands/cells.hpp> // transform_cells_command, reorder_cell_command, LayerTransform
#include <ace/commands/selection.hpp> // Selection
#include <ace/interact/interact.hpp>  // fit_region, pan, zoom, hatch_segments, overview_pattern
#include <ace/interact/pick.hpp>      // pick_targets, click_selection, marquee_selection
#include <ace/project/project.hpp> // root_composition_size (the whole-composition fallback frame)
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ace::app {
namespace {

// The overview's composition<->panel mapping (Constraint 3): the single whole-composition
// `fit_region` affine `t` (composition -> panel-local pixels) plus the pane's screen `origin`.
struct OverviewXform {
  arbc::Affine t = arbc::Affine::identity();
  ImVec2 origin{0.0F, 0.0F};
  bool valid = false;

  ImVec2 to_screen(arbc::Vec2 c) const {
    const arbc::Vec2 p = t.apply(c);
    return ImVec2{origin.x + static_cast<float>(p.x), origin.y + static_cast<float>(p.y)};
  }
  std::optional<arbc::Vec2> to_comp(ImVec2 s) const {
    const std::optional<arbc::Affine> inv = t.inverse();
    if (!inv) {
      return std::nullopt;
    }
    return inv->apply(arbc::Vec2{static_cast<double>(s.x) - static_cast<double>(origin.x),
                                 static_cast<double>(s.y) - static_cast<double>(origin.y)});
  }
  // Panel pixels per composition unit (uniform — `fit_region` is a uniform scale).
  double scale() const { return t.a; }
};

// A dashed segment — the dotted-border primitive ImGui's draw list lacks (Constraint 4). Golden is
// N/A for this panel, so the exact dash/gap is signal-only visual polish.
void dashed_line(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float thickness) {
  const float dx = b.x - a.x;
  const float dy = b.y - a.y;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (!(len > 0.0F)) {
    return;
  }
  const float ux = dx / len;
  const float uy = dy / len;
  constexpr float k_dash = 5.0F;
  constexpr float k_gap = 4.0F;
  for (float pos = 0.0F; pos < len; pos += k_dash + k_gap) {
    const float end = std::min(pos + k_dash, len);
    dl->AddLine(ImVec2{a.x + ux * pos, a.y + uy * pos}, ImVec2{a.x + ux * end, a.y + uy * end}, col,
                thickness);
  }
}

// The screen-space AABB (min, size) of a placed content box — the InvisibleButton geometry a
// rotated box still gets a stable hit area from. `nullopt` when the extent is unbounded/degenerate.
std::optional<std::pair<ImVec2, ImVec2>> box_aabb(const OverviewXform& x, const arbc::Affine& p,
                                                  const arbc::Rect& e) {
  if (e.empty()) {
    return std::nullopt;
  }
  const ImVec2 c[4] = {x.to_screen(p.apply({e.x0, e.y0})), x.to_screen(p.apply({e.x1, e.y0})),
                       x.to_screen(p.apply({e.x1, e.y1})), x.to_screen(p.apply({e.x0, e.y1}))};
  ImVec2 lo = c[0];
  ImVec2 hi = c[0];
  for (int i = 1; i < 4; ++i) {
    lo.x = std::min(lo.x, c[i].x);
    lo.y = std::min(lo.y, c[i].y);
    hi.x = std::max(hi.x, c[i].x);
    hi.y = std::max(hi.y, c[i].y);
  }
  const ImVec2 size{hi.x - lo.x, hi.y - lo.y};
  if (!(size.x > 0.0F) || !(size.y > 0.0F)) {
    return std::nullopt;
  }
  return std::make_pair(lo, size);
}

// Apply an `interact::SelectionChange` onto the ONE project-level selection (D-selection-2): the
// same four-arm switch the canvas and the selection unit test run — the panel keeps no copy (D19).
void apply_change(commands::Selection& selection, const interact::SelectionChange& change) {
  switch (change.op) {
  case interact::SelectOp::None:
    break;
  case interact::SelectOp::Replace:
    selection.replace_all(change.ids);
    break;
  case interact::SelectOp::Add:
    selection.add_all(change.ids);
    break;
  case interact::SelectOp::Toggle:
    for (const arbc::ObjectId id : change.ids) {
      selection.toggle(id);
    }
    break;
  case interact::SelectOp::Clear:
    selection.clear();
    break;
  }
}

// A camera is grabbed by its LABEL (D7: border/label grab, interior click-through). Clicking the
// label is an intentional camera grab, so it selects with row modifiers (Shift=Add,
// Cmd/Ctrl=Toggle, plain=Replace) exactly like the Layers camera row — never routed through the
// interior-click-through pick policy (which the schematic BACKGROUND still honors for a click over
// a camera's interior).
void apply_row_selection(commands::Selection& selection, arbc::ObjectId id) {
  const ImGuiIO& io = ImGui::GetIO();
  if (io.KeyShift) {
    selection.add(id);
  } else if (io.KeyCtrl || io.KeySuper) {
    selection.toggle(id);
  } else {
    selection.select(id);
  }
}

interact::PickModifiers read_mods() {
  const ImGuiIO& io = ImGui::GetIO();
  interact::PickModifiers mods;
  mods.shift = io.KeyShift;
  mods.behind = io.KeyCtrl || io.KeySuper; // D7: Cmd/Ctrl select-behind (macOS reachable)
  return mods;
}

} // namespace

OverviewPanel::OverviewPanel(commands::AppState& state, CanvasView& canvas)
    : state_(state), canvas_(canvas) {}

void OverviewPanel::draw(std::string_view /*view_id*/) {
  arbc::Document& document = state_.document();
  const arbc::Registry& registry = state_.registry();
  std::optional<arbc::ObjectId>& entered = state_.entered_composition();

  // The scope every read/edit targets this frame (the per-frame fail-safe, D-layers-3): the entered
  // composition when it names a live one, else Root. Root itself is the `nullopt`-entered resolve.
  const arbc::ObjectId active = scene::active_composition(document, entered);
  const arbc::ObjectId root = scene::active_composition(document, std::nullopt);
  const bool at_root = active == root;

  // Prune stale ids off the shared selection against the FULL live set (Constraint 6 /
  // D-selection-7) — the unscoped set, so an out-of-scope-but-live id is never dropped by the
  // overview.
  {
    const std::vector<interact::PickTarget> all =
        interact::pick_targets(document, registry, std::nullopt);
    state_.selection().prune(interact::all_ids(all));
  }

  // --- Breadcrumb (Root ▸ … ▸ child), derived each frame (Constraint 10) -----------------------
  const std::vector<scene::Breadcrumb> path = scene::composition_path(document, registry, entered);
  for (std::size_t k = 0; k < path.size(); ++k) {
    if (k > 0) {
      ImGui::SameLine();
      ImGui::TextUnformatted(">");
      ImGui::SameLine();
    }
    const std::string crumb =
        path[k].label + "###ov_crumb_" + std::to_string(path[k].composition.value);
    if (ImGui::SmallButton(crumb.c_str())) {
      entered = (k == 0) ? std::nullopt : std::optional<arbc::ObjectId>(path[k].composition);
    }
  }

  // --- Navigator + z-order toolbar (Constraint 7/8) --------------------------------------------
  const ViewFraming framing = canvas_.focused_framing();
  const bool have_canvas = framing.pane_w > 0 && framing.pane_h > 0;
  {
    // The zoom control (D24): scale-bar discipline, never a "%". A no-op with no live canvas.
    const interact::ScaleBar bar =
        have_canvas
            ? interact::scale_bar(framing.camera, static_cast<double>(framing.pane_w) * 0.25)
            : interact::ScaleBar{};
    if (ImGui::SmallButton("-###ov_zoom_out") && have_canvas) {
      canvas_.drive_focused_camera(interact::zoom(
          framing.camera, arbc::Vec2{framing.pane_w * 0.5, framing.pane_h * 0.5}, 0.8));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+###ov_zoom") && have_canvas) {
      canvas_.drive_focused_camera(interact::zoom(
          framing.camera, arbc::Vec2{framing.pane_w * 0.5, framing.pane_h * 0.5}, 1.25));
    }
    ImGui::SameLine();
    ImGui::Text("%.0f u", bar.units);
    ImGui::SameLine();
    // Bring-forward / send-back over the primary selection (§6) — the shipped reorder verb.
    const arbc::ObjectId primary = state_.selection().primary();
    const bool z_enabled = primary.valid();
    ImGui::BeginDisabled(!z_enabled);
    const std::vector<scene::Cell> z_cells = scene::cells(document, registry, active);
    const scene::ZOrderPosition zpos = scene::z_order_position(z_cells, primary);
    std::optional<std::pair<arbc::ObjectId, std::uint32_t>> pending_reorder;
    if (ImGui::SmallButton("Back###ov_back") && zpos.index > 0) {
      pending_reorder = std::make_pair(primary, static_cast<std::uint32_t>(zpos.index - 1));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Fwd###ov_forward") && zpos.index >= 0 && zpos.index + 1 < zpos.count) {
      pending_reorder = std::make_pair(primary, static_cast<std::uint32_t>(zpos.index + 1));
    }
    ImGui::EndDisabled();
    if (pending_reorder.has_value()) {
      const commands::Command command =
          commands::reorder_cell_command(active, pending_reorder->first, pending_reorder->second);
      canvas_.apply_edit([this, command] { command.apply(state_.document()); });
    }
  }
  ImGui::Separator();

  // --- The schematic area -----------------------------------------------------------------------
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  constexpr float k_margin = 8.0F;
  const float inner_w = avail.x - 2.0F * k_margin;
  const float inner_h = avail.y - 2.0F * k_margin;
  if (!active.valid() || !(inner_w > 0.0F) || !(inner_h > 0.0F)) {
    ImGui::TextDisabled("(no composition)");
    return;
  }

  const std::vector<scene::Cell> cells = scene::cells(document, registry, active);
  const std::vector<scene::Camera> cameras =
      at_root ? scene::cameras(document) : std::vector<scene::Camera>{};

  // The whole-composition bounds to auto-fit (minimap semantics, §5:165): the union of every placed
  // BOUNDED cell + every camera frame, else the authored composition size, else a unit fallback.
  std::optional<arbc::Rect> bounds;
  const auto grow = [&bounds](const arbc::Rect& r) {
    if (r.empty()) {
      return;
    }
    if (!bounds) {
      bounds = r;
    } else {
      bounds = arbc::Rect{std::min(bounds->x0, r.x0), std::min(bounds->y0, r.y0),
                          std::max(bounds->x1, r.x1), std::max(bounds->y1, r.y1)};
    }
  };
  for (const scene::Cell& cell : cells) {
    if (cell.content_bounds.has_value()) {
      grow(cell.placement.map_rect(*cell.content_bounds));
    }
  }
  for (const scene::Camera& cam : cameras) {
    grow(cam.frame.map_rect(arbc::Rect::from_size(static_cast<double>(cam.resolution.width),
                                                  static_cast<double>(cam.resolution.height))));
  }
  if (!bounds) {
    if (const std::optional<project::CompositionSize> size =
            project::root_composition_size(document)) {
      bounds = arbc::Rect{0.0, 0.0, size->width, size->height};
    } else {
      bounds = arbc::Rect{0.0, 0.0, 256.0, 256.0};
    }
  }
  // A little padding around the union so boxes are not flush to the panel edge.
  const double pad = std::max(bounds->width(), bounds->height()) * 0.08 + 1.0;
  const arbc::Rect padded{bounds->x0 - pad, bounds->y0 - pad, bounds->x1 + pad, bounds->y1 + pad};

  OverviewXform x;
  const ImVec2 area_origin = ImGui::GetCursorScreenPos();
  x.origin = ImVec2{area_origin.x + k_margin, area_origin.y + k_margin};
  x.t = interact::fit_region(padded, inner_w, inner_h);
  x.valid = x.t.a > 0.0 && std::isfinite(x.t.a);
  if (!x.valid) {
    ImGui::TextDisabled("(nothing to frame)");
    return;
  }

  const double comp_per_px = 1.0 / x.scale();
  const double edge_tol = 4.0 * comp_per_px;   // border/label grab tolerance in composition units
  const double corner_tol = 6.0 * comp_per_px; // near-corner precedence tolerance

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 area_max{x.origin.x + inner_w, x.origin.y + inner_h};

  // The pointer in composition units this frame (for gestures); `nullopt` if the transform is
  // degenerate (already guarded, but keep the total contract).
  const std::optional<arbc::Vec2> mouse_comp = x.to_comp(ImGui::GetIO().MousePos);

  // --- Transform gizmo target resolution (editor.panels.overview_gizmo; D-overview_gizmo-5) ------
  // The scope-confined pick set the gizmo hit-tests over — the SAME set the overview's
  // click/marquee use — so scope confinement + camera-lock-while-entered fall out for free (D29).
  // The gizmo shows for exactly ONE selected bounded CELL (single path) or a >=2 selection with a
  // bounded union (group path); cameras keep shipped move-only (D-overview_gizmo-4/7), an unbounded
  // cell has no box. Every handle/transform/snap verb below is the shipped L1 `interact`
  // (D-overview_gizmo-1) — no transform arithmetic lives in this panel.
  const std::vector<interact::PickTarget> gizmo_targets =
      interact::pick_targets(document, registry, entered);
  const commands::Selection& sel = state_.selection();
  const interact::PickTarget* gizmo_cell = nullptr;
  std::optional<arbc::Rect> gizmo_union;
  if (sel.size() == 1) {
    for (const interact::PickTarget& t : gizmo_targets) {
      if (t.id == sel.primary() && t.kind == interact::PickKind::Cell && t.extent) {
        gizmo_cell = &t;
        break;
      }
    }
  } else if (sel.size() >= 2) {
    gizmo_union = interact::selected_extent(gizmo_targets, sel.items());
    if (gizmo_union && gizmo_union->empty()) {
      gizmo_union.reset();
    }
  }

  // The persistent draggable pivot (UI session state): seeded to the box/union center, reset when
  // the gizmo target changes; a Pivot-handle drag moves it in pass 2 (mirrors CanvasView).
  std::optional<arbc::Vec2> gizmo_pivot;
  if (gizmo_cell != nullptr) {
    if (!gizmo_pivot_ || gizmo_pivot_for_ != sel.primary()) {
      gizmo_pivot_ = interact::cell_pivot(gizmo_cell->placement, *gizmo_cell->extent);
      gizmo_pivot_for_ = sel.primary();
      gizmo_group_pivot_for_.reset();
    }
    gizmo_pivot = gizmo_pivot_;
  } else if (gizmo_union) {
    if (!gizmo_pivot_ || gizmo_group_pivot_for_ != *gizmo_union) {
      gizmo_pivot_ = interact::cell_pivot(arbc::Affine::identity(), *gizmo_union);
      gizmo_group_pivot_for_ = *gizmo_union;
      gizmo_pivot_for_.reset();
    }
    gizmo_pivot = gizmo_pivot_;
  } else {
    gizmo_pivot_.reset();
    gizmo_pivot_for_.reset();
    gizmo_group_pivot_for_.reset();
  }

  // The shipped scale/rotate/shear composers, driven verbatim (Body/Pivot handled inline where the
  // snap-guides / pivot-follow live). `pv` is the gesture pivot, `ptr` the composition-space
  // pointer.
  const auto compose_srs = [&](interact::CellHandle handle, const arbc::Affine& start,
                               const arbc::Rect& ext, arbc::Vec2 pv, arbc::Vec2 ptr, bool shift,
                               bool alt, bool cmd) -> arbc::Affine {
    if (handle == interact::CellHandle::Rotate) {
      return interact::rotate_cell(start, pv, interact::drag_angle(pv, gizmo_.grab_comp, ptr),
                                   shift);
    }
    if (interact::is_cell_edge(handle) && cmd) {
      return interact::shear_cell(start, ext, handle, ptr, pv); // Cmd/Ctrl + edge = shear (§6)
    }
    if (interact::is_cell_scale_handle(handle)) {
      return interact::scale_cell(start, ext, handle, ptr, pv, shift, alt);
    }
    return start;
  };
  // The Body move, snapped to the OTHER placed objects (object-only — the overview draws no grid,
  // D-grid-3); Cmd/Ctrl bypasses (§6:258).
  const auto compose_body = [&](const arbc::Affine& start, const arbc::Rect& ext, arbc::Vec2 ptr,
                                bool cmd, std::span<const interact::PickTarget> others) {
    const arbc::Affine moved =
        interact::move_cell(start, ptr.x - gizmo_.grab_comp.x, ptr.y - gizmo_.grab_comp.y);
    return interact::snap_placement(moved, ext, others, corner_tol, cmd);
  };
  // The gesture result at composition-space `ptr`: the (layer, placement) batch to preview/commit.
  // Empty for a pivot-move or an identity gesture (Constraint 5). `guides_out`/`union_out` are
  // optional pass-2 draw outputs. Single-cell yields a 0/1-entry batch; group composes the ONE
  // union delta onto every captured member via `group_transform` (D-overview_gizmo-6).
  const auto gesture_batch = [&](arbc::Vec2 ptr, bool shift, bool alt, bool cmd,
                                 std::vector<interact::SnapGuide>* guides_out,
                                 arbc::Affine* union_out) -> std::vector<commands::LayerTransform> {
    std::vector<commands::LayerTransform> batch;
    if (gizmo_.handle == interact::CellHandle::Pivot) {
      return batch;
    }
    if (!gizmo_.group) {
      std::vector<interact::PickTarget> others;
      for (const interact::PickTarget& t : gizmo_targets) {
        if (t.id != gizmo_.content) {
          others.push_back(t);
        }
      }
      arbc::Affine result = gizmo_.start;
      if (gizmo_.handle == interact::CellHandle::Body) {
        const interact::SnapResult snap =
            compose_body(gizmo_.start, gizmo_.extent, ptr, cmd, others);
        result = snap.placement;
        if (guides_out != nullptr) {
          *guides_out = snap.guides;
        }
      } else {
        result = compose_srs(gizmo_.handle, gizmo_.start, gizmo_.extent, gizmo_.pivot, ptr, shift,
                             alt, cmd);
      }
      if (!(result == gizmo_.start)) {
        batch.push_back(commands::LayerTransform{gizmo_.layer, result});
      }
      return batch;
    }
    std::vector<interact::PickTarget> others;
    for (const interact::PickTarget& t : gizmo_targets) {
      if (!sel.contains(t.id)) {
        others.push_back(t);
      }
    }
    arbc::Affine new_union = arbc::Affine::identity();
    if (gizmo_.handle == interact::CellHandle::Body) {
      const interact::SnapResult snap =
          compose_body(arbc::Affine::identity(), gizmo_.group_extent, ptr, cmd, others);
      new_union = snap.placement;
      if (guides_out != nullptr) {
        *guides_out = snap.guides;
      }
    } else {
      new_union = compose_srs(gizmo_.handle, arbc::Affine::identity(), gizmo_.group_extent,
                              gizmo_.pivot, ptr, shift, alt, cmd);
    }
    if (union_out != nullptr) {
      *union_out = new_union;
    }
    std::vector<arbc::Affine> starts;
    starts.reserve(gizmo_.group_start.size());
    for (const std::pair<arbc::ObjectId, arbc::Affine>& m : gizmo_.group_start) {
      starts.push_back(m.second);
    }
    const std::vector<arbc::Affine> previews =
        interact::group_transform(arbc::Affine::identity(), new_union, starts);
    for (std::size_t i = 0; i < gizmo_.group_start.size() && i < previews.size(); ++i) {
      if (!(previews[i] == gizmo_.group_start[i].second)) {
        batch.push_back(commands::LayerTransform{gizmo_.group_start[i].first, previews[i]});
      }
    }
    return batch;
  };

  // The cross-panel hover target this frame (editor.panels.hatch_swatch / D-hatch_swatch-2/5). A
  // CELL box is a draw-list quad with a click-through interior, so its hover resolves through the
  // SAME L1 hit-test a click uses (`interact::pick`) rather than the AABB InvisibleButton — the
  // hovered box is exactly the one a click would select. Cameras are real ImGui label items, so
  // their hover is captured by `IsItemHovered()` in the camera loop below (which overrides a cell
  // pick, matching the label-grab precedence). Authored onto `AppState` at end-of-frame, but ONLY
  // when this panel's window holds the pointer (single-writer, D-hatch_swatch-2).
  std::optional<arbc::ObjectId> hover_target;
  if (mouse_comp) {
    const interact::PickHit hit = interact::pick(gizmo_targets, *mouse_comp, edge_tol, corner_tol);
    if (hit.hit && hit.kind == interact::PickKind::Cell) {
      hover_target = hit.id;
    }
  }

  // ============================ PASS 1: interaction (ImGui items) ==============================
  // A completed box/frame move (Constraint 8) and a completed gizmo gesture (Constraint 5), each
  // committed as ONE transaction at end-of-pass. Declared up here so the background item's group
  // gizmo release and the per-cell gizmo release both fill them.
  std::optional<std::pair<arbc::ObjectId, arbc::Affine>> pending_transform;
  std::optional<std::vector<commands::LayerTransform>> pending_gizmo_batch;

  // Background item — marquee + empty-space clicks (submitted first, lowest priority). It also
  // hosts the GROUP transform gizmo's SCALE-HANDLE grab (>=2 selection, D-overview_gizmo-6): the
  // union's 8 scale handles sit on the union box boundary over empty schematic space, so resolving
  // the grab here (through `interact::hit_cell` over the union treated as one virtual cell) makes
  // them reachable WITHOUT a full-rect group button that would swallow the marquee. The union
  // INTERIOR stays marquee/click-through — the overview's click-through model (D-overview_gizmo-5)
  // — so a group can be scaled while empty space inside its bounds still marquees. Only the scale
  // handles are hosted here: the rotate ring's wide off-box halo would shadow marquee starts near
  // the union corners, so group ROTATE stays a canvas capability (the single-cell overview rotate
  // ring is owned by that cell's widened button, where no such ambiguity exists). Pivot/Body are
  // excluded so the interior stays marquee/click-through.
  interact::CellHandle bg_group_handle = interact::CellHandle::None;
  if (mouse_comp && gizmo_union && gizmo_pivot) {
    interact::PickTarget vcell;
    vcell.kind = interact::PickKind::Cell;
    vcell.placement = arbc::Affine::identity();
    vcell.extent = *gizmo_union;
    const interact::CellHandle h =
        interact::hit_cell(vcell, *gizmo_pivot, *mouse_comp, edge_tol, corner_tol);
    if (interact::is_cell_scale_handle(h)) {
      bg_group_handle = h; // scale handles only — the interior + rotate halo stay marquee-able
    }
  }
  ImGui::SetNextItemAllowOverlap();
  ImGui::SetCursorScreenPos(x.origin);
  ImGui::InvisibleButton("###ov_bg", ImVec2{inner_w, inner_h});
  if (ImGui::IsItemActivated() && mouse_comp) {
    if (bg_group_handle != interact::CellHandle::None) {
      gizmo_ = GizmoGesture{};
      gizmo_.active = true;
      gizmo_.group = true;
      gizmo_.handle = bg_group_handle;
      gizmo_.grab_comp = *mouse_comp;
      gizmo_.pivot = *gizmo_pivot;
      gizmo_.group_extent = *gizmo_union;
      for (const interact::PickTarget& t : gizmo_targets) {
        if (sel.contains(t.id)) {
          gizmo_.group_start.emplace_back(t.layer, t.placement);
        }
      }
      marquee_.active = false;
    } else {
      marquee_ = MarqueeGesture{true, false, *mouse_comp};
    }
  }
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    if (gizmo_.active && gizmo_.group) {
      gizmo_.moved = true;
    } else if (marquee_.active) {
      marquee_.moved = true;
    }
  }
  if (ImGui::IsItemDeactivated()) {
    if (gizmo_.active && gizmo_.group) {
      // Commit the group transform as ONE `transform_cells_command` (an identity/zero-travel
      // gesture yields an empty batch and nothing commits).
      if (gizmo_.moved && mouse_comp) {
        const ImGuiIO& io = ImGui::GetIO();
        std::vector<commands::LayerTransform> batch = gesture_batch(
            *mouse_comp, io.KeyShift, io.KeyAlt, io.KeyCtrl || io.KeySuper, nullptr, nullptr);
        if (!batch.empty()) {
          pending_gizmo_batch = std::move(batch);
        }
      }
      gizmo_.active = false;
    } else if (marquee_.active) {
      const std::vector<interact::PickTarget> targets =
          interact::pick_targets(document, registry, entered);
      if (mouse_comp && marquee_.moved) {
        const arbc::Rect rect{std::min(marquee_.anchor_comp.x, mouse_comp->x),
                              std::min(marquee_.anchor_comp.y, mouse_comp->y),
                              std::max(marquee_.anchor_comp.x, mouse_comp->x),
                              std::max(marquee_.anchor_comp.y, mouse_comp->y)};
        apply_change(state_.selection(),
                     interact::marquee_selection(targets, rect, ImGui::GetIO().KeyShift));
      } else {
        apply_change(state_.selection(),
                     interact::click_selection(targets, marquee_.anchor_comp, edge_tol, corner_tol,
                                               read_mods(), state_.selection().primary()));
      }
      marquee_.active = false;
    }
  }

  // The live-viewport visible-composition quad (Constraint 7): stroked below, and its centre is the
  // navigator drag handle submitted LAST (so it wins its small spot while the schematic interior
  // stays free for marquee/selection — the whole-composition minimap must remain marquee-able).
  std::optional<std::array<arbc::Vec2, 4>> viewport_quad;
  if (have_canvas) {
    if (const std::optional<arbc::Affine> inv = framing.camera.inverse()) {
      const double pw = framing.pane_w;
      const double ph = framing.pane_h;
      viewport_quad = std::array<arbc::Vec2, 4>{inv->apply({0.0, 0.0}), inv->apply({pw, 0.0}),
                                                inv->apply({pw, ph}), inv->apply({0.0, ph})};
    }
  }

  // The screen radius of the gizmo handle envelope beyond a box AABB: the rotate ring sits at
  // `corner_tol * 3` OUTSIDE a corner (pick.cpp hit_cell), so pad by that (in px) plus a little
  // slop — the widened `###ov_cell_<id>` hit area the selected box needs to catch its outside
  // handles (D-overview_gizmo-5), regardless of the whole-composition fit zoom (Constraint 3).
  const float handle_pad = static_cast<float>(corner_tol * 3.0 * x.scale()) + 4.0F;

  // Per-cell items, BOTTOM→TOP so the front box wins an overlap (Constraint 4). A drag moves;
  // a plain release selects through the pick core. The SELECTED single cell also drives the
  // transform gizmo: its hit area is widened to the handle envelope and its press is routed
  // through `interact::hit_cell` (D-overview_gizmo-5).
  for (const scene::Cell& cell : cells) {
    if (!cell.content_bounds.has_value()) {
      continue; // an unbounded fill has no box; it stays selectable through the background pick
    }
    const std::optional<std::pair<ImVec2, ImVec2>> aabb =
        box_aabb(x, cell.placement, *cell.content_bounds);
    if (!aabb) {
      continue;
    }
    const bool is_gizmo_cell = gizmo_cell != nullptr && gizmo_cell->id == cell.id;
    ImVec2 btn_min = aabb->first;
    ImVec2 btn_size = aabb->second;
    if (is_gizmo_cell) {
      btn_min = ImVec2{btn_min.x - handle_pad, btn_min.y - handle_pad};
      btn_size = ImVec2{btn_size.x + 2.0F * handle_pad, btn_size.y + 2.0F * handle_pad};
    }
    // AllowOverlap so a FRONT box (submitted later, bottom→top) wins a click over the one behind it
    // — front-occludes-back for hit-testing too, not just the fill (§5:183-189).
    ImGui::SetNextItemAllowOverlap();
    ImGui::SetCursorScreenPos(btn_min);
    const std::string id = "###ov_cell_" + std::to_string(cell.id.value);
    ImGui::InvisibleButton(id.c_str(), btn_size);
    if (ImGui::IsItemActivated() && mouse_comp) {
      // Double-click a NESTED box to ENTER its composition (D17, Select ≠ enter): set the
      // project-level scope; the schematic re-roots next frame. Not a move/gizmo gesture.
      const std::optional<arbc::ObjectId> child = scene::nested_composition_of(document, cell.id);
      if (child.has_value() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        entered = child;
        move_.active = false;
        gizmo_.active = false;
      } else if (is_gizmo_cell && gizmo_pivot) {
        // The selected cell's gizmo: resolve the grabbed handle through the shipped L1 hit-test
        // over the exact placed quad. A handle hit arms the gizmo drag; a miss (in the widened
        // margin, off the box + ring) falls back to the plain move/click. The overview does NOT
        // offer a draggable pivot (a canvas-only nicety on this coarse surface): a Pivot hit — the
        // dot sits at the box center, so a center-drag would otherwise grab it instead of moving —
        // is treated as a Body move, and the pivot dot is drawn as a non-grabbable center marker.
        interact::CellHandle handle =
            interact::hit_cell(*gizmo_cell, *gizmo_pivot, *mouse_comp, edge_tol, corner_tol);
        if (handle == interact::CellHandle::Pivot) {
          handle = interact::CellHandle::Body;
        }
        if (handle != interact::CellHandle::None) {
          gizmo_ = GizmoGesture{};
          gizmo_.active = true;
          gizmo_.group = false;
          gizmo_.handle = handle;
          gizmo_.grab_comp = *mouse_comp;
          gizmo_.pivot = *gizmo_pivot;
          gizmo_.content = cell.id;
          gizmo_.layer = cell.layer;
          gizmo_.start = gizmo_cell->placement;
          gizmo_.extent = *gizmo_cell->extent;
          move_.active = false;
        } else {
          move_ = MoveGesture{true, cell.id, cell.layer, cell.placement, *mouse_comp, false};
        }
      } else {
        move_ = MoveGesture{true, cell.id, cell.layer, cell.placement, *mouse_comp, false};
      }
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      if (gizmo_.active && !gizmo_.group && gizmo_.content == cell.id) {
        gizmo_.moved = true;
      } else if (move_.active && move_.content == cell.id) {
        move_.moved = true;
      }
    }
    if (ImGui::IsItemDeactivated()) {
      if (gizmo_.active && !gizmo_.group && gizmo_.content == cell.id) {
        // Commit ONE `transform_cells_command` (Constraint 5): a real drag on a transform handle;
        // a pivot-move / identity gesture yields an empty batch and nothing commits, and a
        // non-drag body press is handed back to the pick policy (the gizmo must not swallow a
        // click — D7).
        if (gizmo_.moved && mouse_comp && gizmo_.handle != interact::CellHandle::Pivot) {
          const ImGuiIO& io = ImGui::GetIO();
          std::vector<commands::LayerTransform> batch = gesture_batch(
              *mouse_comp, io.KeyShift, io.KeyAlt, io.KeyCtrl || io.KeySuper, nullptr, nullptr);
          if (!batch.empty()) {
            pending_gizmo_batch = std::move(batch);
          }
        } else if (!gizmo_.moved && gizmo_.handle == interact::CellHandle::Body) {
          apply_change(state_.selection(),
                       interact::click_selection(gizmo_targets, gizmo_.grab_comp, edge_tol,
                                                 corner_tol, read_mods(),
                                                 state_.selection().primary()));
        }
        gizmo_.active = false;
      } else if (move_.active && move_.content == cell.id) {
        if (move_.moved && mouse_comp) {
          pending_transform = std::make_pair(
              move_.layer, interact::move_cell(move_.start, mouse_comp->x - move_.grab_comp.x,
                                               mouse_comp->y - move_.grab_comp.y));
        } else {
          apply_change(state_.selection(), interact::click_selection(
                                               gizmo_targets, move_.grab_comp, edge_tol, corner_tol,
                                               read_mods(), state_.selection().primary()));
        }
        move_.active = false;
      }
    }
  }

  // Per-camera items (only at Root, D29 — while entered `pick_targets` already drops cameras). The
  // camera's NAME LABEL is the select/move grab (§5:173, D7 label grab); the frame interior stays
  // click-through to the schematic background. Small affordances fit the viewport to the camera and
  // toggle look-through.
  for (const scene::Camera& cam : cameras) {
    const arbc::Rect res_rect = arbc::Rect::from_size(static_cast<double>(cam.resolution.width),
                                                      static_cast<double>(cam.resolution.height));
    const std::optional<std::pair<ImVec2, ImVec2>> aabb = box_aabb(x, cam.frame, res_rect);
    if (!aabb) {
      continue;
    }
    // The label at the frame's top-left corner — the addressable camera grab.
    ImGui::SetNextItemAllowOverlap();
    ImGui::SetCursorScreenPos(ImVec2{aabb->first.x, aabb->first.y});
    const std::string label = cam.name + "###ov_cam_" + std::to_string(cam.id.value);
    ImGui::SmallButton(label.c_str());
    if (ImGui::IsItemHovered()) {
      hover_target =
          cam.id; // a hovered camera label wins the cross-highlight (label-grab precedence)
    }
    if (ImGui::IsItemActivated() && mouse_comp) {
      move_ = MoveGesture{true, cam.id, cam.layer, cam.frame, *mouse_comp, false};
    }
    if (move_.active && move_.content == cam.id && ImGui::IsItemActive() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      move_.moved = true;
    }
    if (ImGui::IsItemDeactivated() && move_.active && move_.content == cam.id) {
      if (move_.moved && mouse_comp) {
        pending_transform = std::make_pair(
            move_.layer, interact::move_cell(move_.start, mouse_comp->x - move_.grab_comp.x,
                                             mouse_comp->y - move_.grab_comp.y));
      } else {
        apply_row_selection(state_.selection(), cam.id); // a label grab selects the camera (D7)
      }
      move_.active = false;
    }
    // Fit-to-camera + look-through affordances beside the label.
    ImGui::SameLine();
    const std::string fit_id = "Fit###ov_camfit_" + std::to_string(cam.id.value);
    if (ImGui::SmallButton(fit_id.c_str()) && have_canvas) {
      const arbc::Rect frame_rect = cam.frame.map_rect(res_rect);
      if (!frame_rect.empty()) {
        canvas_.drive_focused_camera(
            interact::fit_region(frame_rect, framing.pane_w, framing.pane_h));
      }
    }
    ImGui::SameLine();
    const std::string look_id = "Look###ov_camlook_" + std::to_string(cam.id.value);
    if (ImGui::SmallButton(look_id.c_str())) {
      const std::string_view pane = canvas_.indicated_view_id();
      if (!pane.empty()) {
        const std::optional<arbc::ObjectId> cur = canvas_.look_through(pane);
        canvas_.set_look_through(pane, cur == std::optional<arbc::ObjectId>(cam.id)
                                           ? std::nullopt
                                           : std::optional<arbc::ObjectId>(cam.id));
      }
    }
  }

  // The navigator drag handle (Constraint 7), submitted LAST so it wins its small central spot.
  // Dragging it pans the FOCUSED CANVAS viewport camera only — no journal entry, not undoable
  // (D24/D15) — through `drive_focused_camera`, a no-op when no live canvas exists.
  ImVec2 viewport_handle_centre{0.0F, 0.0F};
  if (viewport_quad) {
    viewport_handle_centre = ImVec2{0.0F, 0.0F};
    for (const arbc::Vec2& c : *viewport_quad) {
      const ImVec2 s = x.to_screen(c);
      viewport_handle_centre.x += s.x * 0.25F;
      viewport_handle_centre.y += s.y * 0.25F;
    }
    // Clamp the handle into the schematic so a mostly-off-screen framing still offers a grab.
    viewport_handle_centre.x =
        std::clamp(viewport_handle_centre.x, x.origin.x + 10.0F, area_max.x - 10.0F);
    viewport_handle_centre.y =
        std::clamp(viewport_handle_centre.y, x.origin.y + 10.0F, area_max.y - 10.0F);
    constexpr float k_handle = 20.0F;
    ImGui::SetCursorScreenPos(ImVec2{viewport_handle_centre.x - k_handle * 0.5F,
                                     viewport_handle_centre.y - k_handle * 0.5F});
    ImGui::InvisibleButton("###ov_viewport", ImVec2{k_handle, k_handle});
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      const ImVec2 d = ImGui::GetIO().MouseDelta;
      const double cam_scale = framing.camera.max_scale();
      if (cam_scale > 0.0 && std::isfinite(cam_scale) && (d.x != 0.0F || d.y != 0.0F)) {
        // Move the framing by the pointer's composition-space delta: `d` panel px is
        // `d * comp_per_px` composition units, i.e. `d * comp_per_px * cam_scale` device px of
        // camera pan (the `interact::pan` framing-shift relation).
        const double dev_dx = static_cast<double>(d.x) * comp_per_px * cam_scale;
        const double dev_dy = static_cast<double>(d.y) * comp_per_px * cam_scale;
        canvas_.drive_focused_camera(interact::pan(framing.camera, dev_dx, dev_dy));
      }
    }
  }

  // A defined, ADDRESSABLE empty state (Constraint's empty-composition case): a stable id the e2e
  // asserts, plus the visual hint drawn in pass 2. No boxes means no stale items.
  if (cells.empty() && cameras.empty()) {
    ImGui::SetCursorScreenPos(x.origin);
    ImGui::InvisibleButton("###ov_empty", ImVec2{120.0F, 20.0F});
  }

  // Commit a completed box/frame move as ONE transaction (Constraint 8): placement only (D8).
  if (pending_transform.has_value()) {
    const commands::Command command = commands::transform_cells_command(
        {commands::LayerTransform{pending_transform->first, pending_transform->second}});
    canvas_.apply_edit([this, command] { command.apply(state_.document()); });
  }
  // Commit a completed gizmo gesture as ONE transaction (Constraint 5): placement only (D8), the
  // whole single/group batch in one undoable step through the same funnel (D-overview_gizmo-2).
  if (pending_gizmo_batch.has_value() && !pending_gizmo_batch->empty()) {
    const commands::Command command = commands::transform_cells_command(*pending_gizmo_batch);
    canvas_.apply_edit([this, command] { command.apply(state_.document()); });
  }

  // ============================ PASS 2: draw (draw list, on top) ===============================
  // Reserve the schematic area so the window sizes correctly.
  ImGui::SetCursorScreenPos(area_origin);
  ImGui::Dummy(avail);

  // The live gesture preview (Constraint 5): the placement(s) to DRAW while a gizmo drag is in
  // flight — UI-only, the document is untouched until release. A Pivot-handle drag moves the pivot
  // only. Keyed by placing LAYER so a group can override cells AND cameras uniformly.
  std::vector<commands::LayerTransform> gizmo_preview_batch;
  arbc::Affine gizmo_preview_union = arbc::Affine::identity();
  std::vector<interact::SnapGuide> gizmo_preview_guides;
  if (gizmo_.active && gizmo_.moved && mouse_comp) {
    if (gizmo_.handle == interact::CellHandle::Pivot) {
      gizmo_pivot_ = *mouse_comp; // pivot-follow — UI session state, no document change / no commit
      gizmo_pivot = gizmo_pivot_;
    } else {
      const ImGuiIO& io = ImGui::GetIO();
      gizmo_preview_batch =
          gesture_batch(*mouse_comp, io.KeyShift, io.KeyAlt, io.KeyCtrl || io.KeySuper,
                        &gizmo_preview_guides, &gizmo_preview_union);
    }
  }
  const auto preview_placement = [&](arbc::ObjectId layer, const arbc::Affine& fallback) {
    for (const commands::LayerTransform& lt : gizmo_preview_batch) {
      if (lt.layer == layer) {
        return lt.placement;
      }
    }
    return fallback;
  };
  // The placement to DRAW an object at this frame: its live move preview, else its gizmo preview,
  // else its stored placement.
  const auto draw_placement = [&](arbc::ObjectId content, arbc::ObjectId layer,
                                  const arbc::Affine& base) -> arbc::Affine {
    if (move_.active && move_.moved && move_.content == content && mouse_comp) {
      return interact::move_cell(move_.start, mouse_comp->x - move_.grab_comp.x,
                                 mouse_comp->y - move_.grab_comp.y);
    }
    return preview_placement(layer, base);
  };

  // A subtle isolation scrim when entered (Constraint 10): the "everything outside is dimmed" cue,
  // an ImGui draw (never a libarbc render), matching `editor.canvas.isolation_scope`'s intent.
  if (!at_root) {
    dl->AddRectFilled(x.origin, area_max, IM_COL32(0, 0, 0, 70));
  }

  const commands::Selection& selection = state_.selection();
  // The cross-panel hover target authored by whichever panel holds the pointer (D-hatch_swatch-2),
  // read here to draw the hover treatment on the matching box/frame — distinct from selection.
  const std::optional<arbc::ObjectId>& hovered = state_.hovered_object();

  // Fills + hatch, BOTTOM→TOP: a front cell's fill occludes the one behind it in the overlap, and
  // the semi-opaque hatch keeps the behind pattern faintly visible (§5:183-189).
  int ordinal = 0;
  const int cell_count = static_cast<int>(cells.size());
  for (const scene::Cell& cell : cells) {
    const int this_ordinal = ordinal++;
    if (!cell.content_bounds.has_value()) {
      continue;
    }
    const arbc::Affine placement = draw_placement(cell.id, cell.layer, cell.placement);
    const arbc::Rect& e = *cell.content_bounds;
    const bool selected = selection.contains(cell.id);
    const interact::OverviewPattern pat = interact::overview_pattern(this_ordinal, cell_count);
    const ImVec2 tl = x.to_screen(placement.apply({e.x0, e.y0}));
    const ImVec2 tr = x.to_screen(placement.apply({e.x1, e.y0}));
    const ImVec2 br = x.to_screen(placement.apply({e.x1, e.y1}));
    const ImVec2 bl = x.to_screen(placement.apply({e.x0, e.y1}));
    // A low-alpha solid fill under the hatch so the front box OCCLUDES (dominant), while the hatch
    // gaps let the behind pattern read faintly (§5:186).
    const ImU32 fill = selected ? IM_COL32(60, 110, 150, 90) : IM_COL32(70, 70, 78, 110);
    dl->AddQuadFilled(tl, tr, br, bl, fill);
    // Content-space hatch, mapped through `placement` so it scales/tilts with the cell (§5:201).
    const double spacing = std::max(1.0, std::min(e.width(), e.height()) / 8.0);
    const ImU32 ink = hatch_ink(pat.color_index, selected);
    for (const interact::HatchSegment& seg : interact::hatch_segments(e, spacing, pat.style)) {
      dl->AddLine(x.to_screen(placement.apply(seg.a)), x.to_screen(placement.apply(seg.b)), ink,
                  1.0F);
    }
  }

  // Dotted borders, ON TOP of every fill (Constraint 4): an occluded cell's full extent stays
  // traceable underneath.
  for (const scene::Cell& cell : cells) {
    if (!cell.content_bounds.has_value()) {
      continue;
    }
    const arbc::Affine placement = draw_placement(cell.id, cell.layer, cell.placement);
    const arbc::Rect& e = *cell.content_bounds;
    const bool selected = selection.contains(cell.id);
    const ImVec2 tl = x.to_screen(placement.apply({e.x0, e.y0}));
    const ImVec2 tr = x.to_screen(placement.apply({e.x1, e.y0}));
    const ImVec2 br = x.to_screen(placement.apply({e.x1, e.y1}));
    const ImVec2 bl = x.to_screen(placement.apply({e.x0, e.y1}));
    const ImU32 border = selected ? IM_COL32(120, 200, 255, 255) : IM_COL32(200, 200, 200, 220);
    const float th = selected ? 2.0F : 1.0F;
    dashed_line(dl, tl, tr, border, th);
    dashed_line(dl, tr, br, border, th);
    dashed_line(dl, br, bl, border, th);
    dashed_line(dl, bl, tl, border, th);
    // Hover cross-highlight (Constraint 7): a SOLID accent outline over the box, distinct from the
    // dotted selection border — reads as "the pointer is on this object", composing with selection.
    if (hovered && *hovered == cell.id) {
      dl->AddQuad(tl, tr, br, bl, accent(230), 2.0F);
    }
  }

  // The transform gizmo overlay (editor.panels.overview_gizmo): the placed outline + 8 scale
  // handles + pivot dot, anchored to `placed_quad` mapped through `OverviewXform::to_screen`
  // (D-overview_gizmo-5). Drawn AT the previewed placement/union while a drag is in flight; the box
  // fills/borders above already preview the same transform (single moves the one box, group moves
  // every member), so the group needs no separate per-member outlines here.
  if (gizmo_pivot && (gizmo_cell != nullptr || gizmo_union)) {
    const bool group = gizmo_cell == nullptr;
    const bool dragging =
        gizmo_.active && gizmo_.moved && mouse_comp && gizmo_.handle != interact::CellHandle::Pivot;
    arbc::Affine gp = group ? arbc::Affine::identity() : gizmo_cell->placement;
    const arbc::Rect ext = group ? *gizmo_union : *gizmo_cell->extent;
    if (dragging) {
      gp =
          group ? gizmo_preview_union : preview_placement(gizmo_cell->layer, gizmo_cell->placement);
    }
    const arbc::Vec2 c_tl = gp.apply({ext.x0, ext.y0});
    const arbc::Vec2 c_tr = gp.apply({ext.x1, ext.y0});
    const arbc::Vec2 c_br = gp.apply({ext.x1, ext.y1});
    const arbc::Vec2 c_bl = gp.apply({ext.x0, ext.y1});
    const ImU32 col = accent(255);
    dl->AddQuad(x.to_screen(c_tl), x.to_screen(c_tr), x.to_screen(c_br), x.to_screen(c_bl), col,
                2.0F);
    const arbc::Vec2 handles[8] = {c_tl,
                                   c_tr,
                                   c_br,
                                   c_bl,
                                   {(c_tl.x + c_tr.x) * 0.5, (c_tl.y + c_tr.y) * 0.5},
                                   {(c_tr.x + c_br.x) * 0.5, (c_tr.y + c_br.y) * 0.5},
                                   {(c_br.x + c_bl.x) * 0.5, (c_br.y + c_bl.y) * 0.5},
                                   {(c_bl.x + c_tl.x) * 0.5, (c_bl.y + c_tl.y) * 0.5}};
    constexpr float hs = 3.0F;
    for (const arbc::Vec2& q : handles) {
      const ImVec2 s = x.to_screen(q);
      dl->AddRectFilled(ImVec2{s.x - hs, s.y - hs}, ImVec2{s.x + hs, s.y + hs}, col);
    }
    const ImVec2 sp = x.to_screen(*gizmo_pivot);
    dl->AddCircleFilled(sp, 4.0F, IM_COL32(255, 210, 120, 255));
    dl->AddCircle(sp, 4.0F, IM_COL32(0, 0, 0, 180));
    for (const interact::SnapGuide& g : gizmo_preview_guides) {
      dl->AddLine(x.to_screen(g.a), x.to_screen(g.b), IM_COL32(255, 90, 200, 230), 1.0F);
    }
  }

  // Cameras as distinct SOLID frames (never a hatch box, §5:173) + a label — always-on-top chrome.
  for (const scene::Camera& cam : cameras) {
    const arbc::Rect res_rect = arbc::Rect::from_size(static_cast<double>(cam.resolution.width),
                                                      static_cast<double>(cam.resolution.height));
    const arbc::Affine frame = draw_placement(cam.id, cam.layer, cam.frame);
    const bool selected = selection.contains(cam.id);
    const ImVec2 tl = x.to_screen(frame.apply({res_rect.x0, res_rect.y0}));
    const ImVec2 tr = x.to_screen(frame.apply({res_rect.x1, res_rect.y0}));
    const ImVec2 br = x.to_screen(frame.apply({res_rect.x1, res_rect.y1}));
    const ImVec2 bl = x.to_screen(frame.apply({res_rect.x0, res_rect.y1}));
    const ImU32 col = selected ? IM_COL32(120, 200, 255, 255) : IM_COL32(255, 210, 120, 255);
    dl->AddQuad(tl, tr, br, bl, col, selected ? 2.5F : 1.5F); // the name shows on the label button
    // Camera cross-highlight (Constraint 4): a hovered camera frame lights even though it carries
    // no swatch — a plain id match, so cameras participate in the cross-panel hover for free.
    if (hovered && *hovered == cam.id) {
      dl->AddQuad(tl, tr, br, bl, accent(230), 3.0F);
    }
  }

  // The live viewport rect, highlighted on top of all (§5:179).
  if (viewport_quad) {
    const ImVec2 q[4] = {x.to_screen((*viewport_quad)[0]), x.to_screen((*viewport_quad)[1]),
                         x.to_screen((*viewport_quad)[2]), x.to_screen((*viewport_quad)[3])};
    dl->AddQuadFilled(q[0], q[1], q[2], q[3], IM_COL32(120, 200, 255, 30));
    dl->AddQuad(q[0], q[1], q[2], q[3], IM_COL32(120, 200, 255, 220), 2.0F);
    // The central drag handle (the `###ov_viewport` grab).
    dl->AddCircleFilled(viewport_handle_centre, 5.0F, IM_COL32(120, 200, 255, 255));
  }

  // The in-progress marquee rectangle.
  if (marquee_.active && marquee_.moved && mouse_comp) {
    const ImVec2 a = x.to_screen(marquee_.anchor_comp);
    const ImVec2 b = x.to_screen(*mouse_comp);
    dl->AddRect(ImVec2{std::min(a.x, b.x), std::min(a.y, b.y)},
                ImVec2{std::max(a.x, b.x), std::max(a.y, b.y)}, IM_COL32(180, 220, 255, 220));
  }

  // A defined empty state (Constraint's empty-composition case): no boxes, a labeled hint.
  if (cells.empty() && cameras.empty()) {
    dl->AddText(ImVec2{x.origin.x + 6.0F, x.origin.y + 6.0F}, IM_COL32(160, 160, 160, 255),
                "(empty composition)");
  }

  // Author the cross-panel hover target for this frame — but ONLY when THIS panel's window holds
  // the pointer (single-writer-per-frame, D-hatch_swatch-2). Writing the resolved id (or clearing
  // it to `nullopt` when over no box/label) is exclusive: the non-hovered Layers panel never
  // writes, so the two singletons never contend and no shell-level per-frame reset is needed
  // (Constraint 5).
  if (ImGui::IsWindowHovered()) {
    state_.set_hovered(hover_target);
  }
}

} // namespace ace::app
