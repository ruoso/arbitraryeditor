// editor.cameras.mint_from_focused_canvas — the pure selection rule, headless (docs §9's
// Catch2 layer). `ace::app::framing_for_focus` is deliberately extracted OUT of `CanvasView`
// (D-mint_from_focused_canvas-3) precisely so this file can exist: `CanvasView` needs a GL
// context, a render thread and an ImGui context, and the three branches that matter most —
// focused-but-unsized, focused-but-closed, all-unsized — are the ones an e2e cannot cheaply
// construct. Nothing here touches ImGui, GL or a Document.
//
// It lives in `ace_shell_test` rather than `ace_tests` because only that target links
// `ace::app` (CMakeLists.txt), even though the code under test is ImGui-free.
#include <ace/app/view_framing.hpp>

#include <arbc/base/transform.hpp>

#include <catch2/catch_test_macros.hpp>

#include <span>
#include <string_view>
#include <vector>

using ace::app::framing_for_focus;
using ace::app::PaneFraming;
using ace::app::ViewFraming;

namespace {

// One distinguishable camera per pane, so a "right size, wrong camera" implementation cannot
// pass by accident: the translation encodes which pane the framing came from.
arbc::Affine cam(double tag) { return arbc::Affine::translation(tag, tag * 2.0); }

bool same(const arbc::Affine& a, const arbc::Affine& b) { return a == b; }

} // namespace

TEST_CASE("framing_for_focus: the focused pane wins over view-id order") {
  const std::vector<PaneFraming> panes{
      {"canvas#1", ViewFraming{cam(1.0), 100, 50}},
      {"canvas#2", ViewFraming{cam(2.0), 300, 200}},
  };

  const ViewFraming focused = framing_for_focus(panes, "canvas#2");
  CHECK(focused.pane_w == 300);
  CHECK(focused.pane_h == 200);
  CHECK(same(focused.camera, cam(2.0))); // the camera travels WITH the size

  // Anti-vacuity: the SAME input with no focus id resolves to canvas#1, so an implementation
  // that ignores its `focused_view_id` argument fails this pair.
  const ViewFraming primary = framing_for_focus(panes, "");
  CHECK(primary.pane_w == 100);
  CHECK(primary.pane_h == 50);
  CHECK(same(primary.camera, cam(1.0)));
}

TEST_CASE("framing_for_focus: an empty focus id reproduces the lowest-id rule exactly") {
  // Including the part that is NOT simply "the first entry": an unsized lower-id pane is
  // skipped in favour of a sized higher-id one. This is the branch every shipped
  // single-canvas suite exercises through `primary_framing()` (Constraint 3/6).
  const std::vector<PaneFraming> panes{
      {"canvas#1", ViewFraming{cam(1.0), 0, 0}},
      {"canvas#2", ViewFraming{cam(2.0), 640, 480}},
      {"canvas#3", ViewFraming{cam(3.0), 320, 240}},
  };

  const ViewFraming primary = framing_for_focus(panes, "");
  CHECK(primary.pane_w == 640);
  CHECK(primary.pane_h == 480);
  CHECK(same(primary.camera, cam(2.0)));
}

TEST_CASE("framing_for_focus: a present-but-unsized focused pane falls back, never refuses") {
  // Returning the zero sentinel here would trip `live_view_framing()`'s `pane_w > 0` test and
  // DISABLE a mint the user can plainly perform, widening D-new_shot_from_view-2's refusal
  // (Constraint 4).
  const std::vector<PaneFraming> panes{
      {"canvas#1", ViewFraming{cam(1.0), 800, 600}},
      {"canvas#2", ViewFraming{cam(2.0), 0, 0}},
  };

  const ViewFraming framing = framing_for_focus(panes, "canvas#2");
  CHECK(framing.pane_w == 800);
  CHECK(framing.pane_h == 600);
  CHECK(same(framing.camera, cam(1.0)));
  // A half-sized pane (one non-positive dimension) is unsized too.
  const std::vector<PaneFraming> half{
      {"canvas#1", ViewFraming{cam(1.0), 800, 600}},
      {"canvas#2", ViewFraming{cam(2.0), 300, 0}},
  };
  CHECK(framing_for_focus(half, "canvas#2").pane_w == 800);
}

TEST_CASE("framing_for_focus: a focus id naming no pane falls back identically") {
  // The stale hint: the focused canvas was closed between the stamp and the query (or the id
  // was never seen at all). Same degradation as unsized — the fallback, not the sentinel.
  const std::vector<PaneFraming> panes{
      {"canvas#1", ViewFraming{cam(1.0), 128, 96}},
      {"canvas#3", ViewFraming{cam(3.0), 64, 64}},
  };

  const ViewFraming closed = framing_for_focus(panes, "canvas#2");
  CHECK(closed.pane_w == 128);
  CHECK(closed.pane_h == 96);
  CHECK(same(closed.camera, cam(1.0)));
  CHECK(framing_for_focus(panes, "not-a-canvas").pane_w == 128);
}

TEST_CASE("framing_for_focus: the focused pane wins even as the only sized one") {
  const std::vector<PaneFraming> panes{
      {"canvas#1", ViewFraming{cam(1.0), 0, 0}},
      {"canvas#2", ViewFraming{cam(2.0), 0, 0}},
      {"canvas#3", ViewFraming{cam(3.0), 77, 33}},
  };

  const ViewFraming framing = framing_for_focus(panes, "canvas#3");
  CHECK(framing.pane_w == 77);
  CHECK(framing.pane_h == 33);
  CHECK(same(framing.camera, cam(3.0)));
}

TEST_CASE("framing_for_focus: no live canvas yields the zero sentinel") {
  // The two ways a session reaches "no viewport" — D18 has no keep-a-canvas guardrail, so
  // this is a reachable product state. `AppProjectGateway::live_view_framing()` keys off
  // `pane_w > 0`, so this is what preserves D-new_shot_from_view-2's refusal through the swap.
  const std::vector<PaneFraming> none;
  const ViewFraming empty = framing_for_focus(none, "canvas#1");
  CHECK(empty.pane_w == 0);
  CHECK(empty.pane_h == 0);
  CHECK(same(empty.camera, arbc::Affine::identity()));

  const std::vector<PaneFraming> unsized{
      {"canvas#1", ViewFraming{cam(1.0), 0, 0}},
      {"canvas#2", ViewFraming{cam(2.0), 0, 0}},
  };
  CHECK(framing_for_focus(unsized, "canvas#1").pane_w == 0);
  CHECK(framing_for_focus(unsized, "").pane_w == 0);
  CHECK(framing_for_focus(unsized, "").pane_h == 0);
}
