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
#include <string>
#include <string_view>
#include <vector>

using ace::app::focus_target;
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

// --- editor.canvas.focused_canvas_indicator ---------------------------------------------
// The rule projected onto the pane's NAME (D-focused_canvas_indicator-1). The focused-canvas
// marker draws on the pane `focus_target` names, so these cases are the headless half of "the
// marker can never point at a pane other than the one the verb acts on"; the consistency
// property below is the half that makes that structural rather than aspirational.

namespace {

// One row of the shared branch matrix: BOTH `focus_target` and `framing_for_focus` are driven
// over every fixture, so a future change to the rule that moves one and not the other cannot
// pass. `view_id`s are string literals (static storage), so the non-owning `PaneFraming`
// contract holds for the whole test.
struct Fixture {
  std::string_view label;
  std::vector<PaneFraming> panes;
  std::string_view hint;
  std::string_view want_target;
};

std::vector<Fixture> fixtures() {
  return {
      {"focused pane wins over view-id order",
       {{"canvas#1", ViewFraming{cam(1.0), 100, 50}},
        {"canvas#2", ViewFraming{cam(2.0), 300, 200}}},
       "canvas#2",
       "canvas#2"},
      // Three panes with the focused one LAST: a stub returning `panes[0].view_id` fails here.
      {"focused pane wins from the end of a three-pane dock",
       {{"canvas#1", ViewFraming{cam(1.0), 100, 50}},
        {"canvas#2", ViewFraming{cam(2.0), 300, 200}},
        {"canvas#3", ViewFraming{cam(3.0), 640, 480}}},
       "canvas#3",
       "canvas#3"},
      {"focused pane wins from the middle of a three-pane dock",
       {{"canvas#1", ViewFraming{cam(1.0), 100, 50}},
        {"canvas#2", ViewFraming{cam(2.0), 300, 200}},
        {"canvas#3", ViewFraming{cam(3.0), 640, 480}}},
       "canvas#2",
       "canvas#2"},
      // The marker must appear on the FALLBACK pane in each of the three degrade branches —
      // exactly the states where the raw sticky hint names nothing or names the wrong pane.
      {"a present-but-unsized focused pane degrades to the fallback",
       {{"canvas#1", ViewFraming{cam(1.0), 800, 600}}, {"canvas#2", ViewFraming{cam(2.0), 0, 0}}},
       "canvas#2",
       "canvas#1"},
      {"a half-sized focused pane is unsized too",
       {{"canvas#1", ViewFraming{cam(1.0), 800, 600}}, {"canvas#2", ViewFraming{cam(2.0), 300, 0}}},
       "canvas#2",
       "canvas#1"},
      {"a hint naming a CLOSED canvas degrades to the fallback",
       {{"canvas#1", ViewFraming{cam(1.0), 128, 96}}, {"canvas#3", ViewFraming{cam(3.0), 64, 64}}},
       "canvas#2",
       "canvas#1"},
      {"a hint naming nothing at all degrades to the fallback",
       {{"canvas#1", ViewFraming{cam(1.0), 128, 96}}, {"canvas#3", ViewFraming{cam(3.0), 64, 64}}},
       "not-a-canvas",
       "canvas#1"},
      // The never-focused session: the historical `primary_framing()` path, marker included.
      {"an empty hint takes the lowest-id SIZED pane, not the first row",
       {{"canvas#1", ViewFraming{cam(1.0), 0, 0}},
        {"canvas#2", ViewFraming{cam(2.0), 640, 480}},
        {"canvas#3", ViewFraming{cam(3.0), 320, 240}}},
       "",
       "canvas#2"},
      {"an empty hint takes the first row when it is sized",
       {{"canvas#1", ViewFraming{cam(1.0), 100, 50}},
        {"canvas#2", ViewFraming{cam(2.0), 300, 200}}},
       "",
       "canvas#1"},
      {"the focused pane wins even as the only sized one",
       {{"canvas#1", ViewFraming{cam(1.0), 0, 0}},
        {"canvas#2", ViewFraming{cam(2.0), 0, 0}},
        {"canvas#3", ViewFraming{cam(3.0), 77, 33}}},
       "canvas#3",
       "canvas#3"},
      // Nothing to mark: D18 lets every canvas be closed, and a pane with no area is not a
      // framing target — the marker is absent, matching the mint's refusal.
      {"panes exist but none is sized: nothing is marked",
       {{"canvas#1", ViewFraming{cam(1.0), 0, 0}}, {"canvas#2", ViewFraming{cam(2.0), 0, 0}}},
       "canvas#1",
       ""},
      {"panes exist but none is sized, with no hint either",
       {{"canvas#1", ViewFraming{cam(1.0), 0, 0}}, {"canvas#2", ViewFraming{cam(2.0), 0, 0}}},
       "",
       ""},
      {"no panes at all, with a stale hint", {}, "canvas#1", ""},
      {"no panes at all, with no hint", {}, "", ""},
  };
}

const PaneFraming* row_named(const std::vector<PaneFraming>& panes, std::string_view id) {
  for (const PaneFraming& pane : panes) {
    if (pane.view_id == id) {
      return &pane;
    }
  }
  return nullptr;
}

} // namespace

TEST_CASE("focus_target: names the pane the framing rule resolves to, across the whole matrix") {
  for (const Fixture& f : fixtures()) {
    INFO(f.label);
    CHECK(focus_target(f.panes, f.hint) == f.want_target);
  }
}

TEST_CASE("focus_target: the target's view_id borrows the CALLER's key storage") {
  // The marker's accessor hands this `string_view` out of the scope that built the rows
  // (`CanvasView::indicated_view_id()`), which is only sound because the view points into the
  // caller's keys — `presenters_`'s, here the local strings — and never into the row array.
  const std::string first = "canvas#1";
  const std::string second = "canvas#2";
  std::vector<PaneFraming> panes{{first, ViewFraming{cam(1.0), 100, 50}},
                                 {second, ViewFraming{cam(2.0), 300, 200}}};
  std::string_view target = focus_target(panes, "canvas#2");
  panes.clear(); // the rows are gone; the key storage is not
  CHECK(target == "canvas#2");
  CHECK(target.data() == second.data());
}

TEST_CASE("focus_target: framing_for_focus is the SAME rule, projected onto the framing") {
  // THE anti-divergence assertion (Constraint 1). Over every fixture above: the framing the
  // verbs consume is field-for-field — camera included — the framing of the row the marker
  // names, and is the zero sentinel exactly when nothing is marked. Splitting the rule into two
  // parallel implementations breaks this, whichever half is changed.
  for (const Fixture& f : fixtures()) {
    INFO(f.label);
    const std::string_view target = focus_target(f.panes, f.hint);
    const ViewFraming framing = framing_for_focus(f.panes, f.hint);
    if (target.empty()) {
      CHECK(framing.pane_w == 0);
      CHECK(framing.pane_h == 0);
      CHECK(same(framing.camera, arbc::Affine::identity()));
      continue;
    }
    const PaneFraming* row = row_named(f.panes, target);
    REQUIRE(row != nullptr); // a named target always names a real row
    CHECK(framing.pane_w == row->framing.pane_w);
    CHECK(framing.pane_h == row->framing.pane_h);
    CHECK(same(framing.camera, row->framing.camera));
    // …and a marked pane is always one the verbs can actually act on.
    CHECK(row->framing.pane_w > 0);
    CHECK(row->framing.pane_h > 0);
  }
}

TEST_CASE("focus_target: the matrix itself is non-vacuous") {
  // Guards the two tests above from passing against a degenerate stub: the fixture set must
  // contain a resolved target carrying a NON-IDENTITY camera at a non-zero size (so `return ""`
  // fails), an unresolved one (so `return panes[0].view_id` fails on an all-unsized dock), and a
  // three-pane case whose winner is not the first row (so "first row" fails the focused branch).
  bool saw_non_trivial_target = false;
  bool saw_empty_target = false;
  bool saw_deep_non_first_winner = false;
  for (const Fixture& f : fixtures()) {
    const std::string_view target = focus_target(f.panes, f.hint);
    if (target.empty()) {
      saw_empty_target = true;
      continue;
    }
    const PaneFraming* row = row_named(f.panes, target);
    REQUIRE(row != nullptr);
    if (row->framing.pane_w > 0 && row->framing.pane_h > 0 &&
        !same(row->framing.camera, arbc::Affine::identity())) {
      saw_non_trivial_target = true;
    }
    if (f.panes.size() >= 3 && target != f.panes.front().view_id && !f.hint.empty()) {
      saw_deep_non_first_winner = true;
    }
  }
  CHECK(saw_non_trivial_target);
  CHECK(saw_empty_target);
  CHECK(saw_deep_non_first_winner);
}

// --- editor.canvas.view_id_natural_order -------------------------------------------------

TEST_CASE("focus_target: consumes the caller's span order verbatim and never re-orders it") {
  // THIS CASE ASSERTS THE "WRONG" PANE ON PURPOSE — it is not a stale expectation
  // (D-view_id_natural_order-5). `canvas#10` wins here because the CALLER handed it first, and
  // supplying the order is the caller's job: the numeric order that makes `canvas#2` the real
  // fallback lives in `dockmodel::view_id_less` and is applied by `CanvasView::pane_rows()`
  // BEFORE the span reaches this rule (Constraint 1, inherited D-focused_canvas_indicator-1).
  //
  // So an implementer who "fixes the ordering bug again" inside `focus_target` — installing a
  // second authority on which id is lower, the exact divergence class this file exists to
  // foreclose — turns this case red. That is what it is for.
  const std::vector<PaneFraming> panes{
      {"canvas#10", ViewFraming{cam(10.0), 640, 480}},
      {"canvas#2", ViewFraming{cam(2.0), 320, 240}},
  };

  CHECK(focus_target(panes, "") == "canvas#10");
  const ViewFraming framing = framing_for_focus(panes, "");
  CHECK(framing.pane_w == 640);
  CHECK(framing.pane_h == 480);
  CHECK(same(framing.camera, cam(10.0))); // the framing projection moves with the name

  // Anti-vacuity: the SAME two rows in the order `pane_rows()` actually supplies resolve to
  // canvas#2, so this pair also pins that the rule is order-SENSITIVE rather than inert.
  const std::vector<PaneFraming> ordered{
      {"canvas#2", ViewFraming{cam(2.0), 320, 240}},
      {"canvas#10", ViewFraming{cam(10.0), 640, 480}},
  };
  CHECK(focus_target(ordered, "") == "canvas#2");
  CHECK(framing_for_focus(ordered, "").pane_w == 320);
}
