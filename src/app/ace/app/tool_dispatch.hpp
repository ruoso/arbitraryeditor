#pragma once

#include <ace/dockmodel/tool_rail.hpp>

namespace ace::app {

// The canvas pointer-gesture dispatch arm a plain drag runs under the active modal tool
// (editor.canvas.tool_dispatch; docs/00-design.md D20, docs/01-architecture.md A11 amendment).
// One arm per D20 tool: Select drives the always-on gizmo + pick/marquee stack (now gated to
// Select, D-gizmo-2), Pan routes a left-button drag into `interact::pan` (D-nav-4), and Brush /
// Eyedropper are the inert seams `editor.paint.brush` / `editor.color.color` fill
// (D-tool_dispatch-3). This is the L4 routing seam: `interact` holds the per-tool math and
// `dockmodel` holds the `ToolId`, and this app-layer value maps one to the other — the ONLY site
// that reads the active tool to select behaviour (D-tool_dispatch-1). A plain enum, so the closed
// D20 set stays a compile-time switch, never a polymorphic `Tool` vtable (D-tool_dispatch-2).
enum class DispatchArm { ObjectInteraction, ViewportPan, Brush, Eyedropper };

// Map the active modal tool to its canvas dispatch arm. A compile-time-EXHAUSTIVE `switch` over
// the closed D20 set with NO `default`, so adding a fifth `dockmodel::ToolId` surfaces as a
// `-Wswitch` diagnostic here rather than silently no-oping (Constraint 1 / D-tool_dispatch-2). Pure
// value logic with no ImGui/`interact`/`Presenter` dependency, so the closed-set/exhaustiveness
// invariant is provable headless in the ImGui-free Catch2 lane (`tests/canvas_view_test.cpp`).
inline DispatchArm dispatch_arm(dockmodel::ToolId tool) {
  switch (tool) {
  case dockmodel::ToolId::Select:
    return DispatchArm::ObjectInteraction;
  case dockmodel::ToolId::Pan:
    return DispatchArm::ViewportPan;
  case dockmodel::ToolId::Brush:
    return DispatchArm::Brush;
  case dockmodel::ToolId::Eyedropper:
    return DispatchArm::Eyedropper;
  }
  // Unreachable: the switch above is exhaustive over the closed D20 set (no `default`, so a fifth
  // tool trips `-Wswitch`). Kept only so the function has a return on every control path — never a
  // fallback that could silently absorb a new tool.
  return DispatchArm::ObjectInteraction;
}

} // namespace ace::app
