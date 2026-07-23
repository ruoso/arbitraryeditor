// editor.canvas.view_id_natural_order — the TEN-canvas UI e2e (docs §9, the offscreen
// software-GL ASan lane). Borrows tests/multi_canvas_mint_e2e_test.cpp's rig wholesale
// (ScratchDir, the E2EState/UserData hand-off, pump_until, layout_contains, the headless boot,
// the drive loop) with `settle()` generalized from that file's hardcoded two-pane sum to a
// caller-supplied id list.
//
// What it proves: past nine canvases the framing fallback must resolve to `canvas#2`, not
// `canvas#10`. `CanvasView::presenters_` is a `std::map<std::string, …>`, so its own order is
// BYTE order — "canvas#1", "canvas#10", "canvas#11", …, "canvas#2" — and the pre-fix
// `pane_rows()` handed that straight to the rule. On the unfixed tree phase 5 below reports
// "canvas#10" (confirmed by the implementer with the `std::sort` line removed).
//
// Every assertion is on MODEL state — `indicated_view_id()` and `focused_framing()` — and there
// is NO pixel probe (D-view_id_natural_order-6): the defect is about which NAME the rule
// resolves to, and tests/focused_canvas_indicator_e2e_test.cpp already owns the proof that the
// marker's pixels follow that name.
#include <ace/app/canvas_view.hpp>
#include <ace/app/shell.hpp>
#include <ace/app/view_framing.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/scene/cell.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/expected.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "writer_session.hpp"

using ace::app::CanvasView;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::app::ViewFraming;
using ace::commands::AppState;
using ace::dockmodel::ViewType;

namespace {

// Ten canvases: the smallest count at which byte order and numeric order DISAGREE, since
// "canvas#10" is the first id whose index needs two digits.
constexpr int k_canvas_count = 10;

std::string canvas_id(int n) { return "canvas#" + std::to_string(n); }

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir()
      : root(std::filesystem::temp_directory_path() / "ace_view_id_natural_order_e2e_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// TestFunc is a plain function pointer (std::function is disabled in this build), so the
// collaborators are threaded through UserData rather than captured.
struct E2EState {
  ace::dock::Dockspace* dockspace;
  CanvasView* canvas;
};

template <class Ready> bool pump_until(ImGuiTestContext* ctx, Ready ready) {
  // A generous ceiling: ten canvases over one software-GL renderer settle far slower on the
  // offscreen ASan/TSan lanes than a two-pane rig, and slower still on a low-core CI host — this
  // bounds a genuine hang without racing that legitimate latency (paired with the raised
  // per-test watchdog below, which the 60s default would otherwise trip first).
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
  while (std::chrono::steady_clock::now() < deadline) {
    if (ready()) {
      return true;
    }
    ctx->Yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return ready();
}

bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

bool affine_near(const arbc::Affine& a, const arbc::Affine& b, double tol = 1e-6) {
  return near(a.a, b.a, tol) && near(a.b, b.b, tol) && near(a.c, b.c, tol) && near(a.d, b.d, tol) &&
         near(a.tx, b.tx, tol) && near(a.ty, b.ty, tol);
}

bool layout_contains(const ace::dock::Dockspace& d, const std::string& id) {
  const std::vector<std::string> ids = d.layout().view_ids();
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

} // namespace

TEST_CASE("view_id_natural_order e2e: the fallback lands on canvas#2, not canvas#10") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  // The writer identity, bound before the document exists and stopped after the canvas
  // is gone (editor.canvas.writer_thread; see tests/writer_session.hpp).
  ace::testing::WriterSession session(scratch.root / "order");
  REQUIRE(session.ok());
  AppState& state = session.state();
  // Fixture seeding IS a document write: post it to the identity the open just bound.
  session.on_writer([&] { state.document().add_composition(128.0, 128.0); });
  // An OPAQUE (and unbounded) backdrop so no pane's composite is ever blank — the canvas
  // withholds an all-transparent frame, and every one of the ten panes must publish for the
  // sizing pass below to be meaningful.
  arbc::expected<arbc::ObjectId, std::string> backdrop = arbc::unexpected<std::string>("unset");
  session.on_writer([&] {
    backdrop = ace::scene::add_cell(state.document(), state.registry(), "org.arbc.solid",
                                    "0.15,0.2,0.25,1", arbc::Affine::identity());
  });
  REQUIRE(backdrop.has_value());

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  // Larger than the mint rig's 900x640: ten canvases plus the seeded singletons share one dock
  // leaf's tab bar, and a roomier bar keeps the per-pane rects comfortably non-degenerate.
  // Nothing in this leaf depends on the window size.
  opts.width = 1280;
  opts.height = 240;
  REQUIRE(shell.init(opts));

  CanvasView canvas(state, session.writer());
  ace::views::register_view_body(ViewType::Canvas, [&canvas](std::string_view view_id) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content(view_id, static_cast<int>(avail.x), static_cast<int>(avail.y));
  });

  // No project gateway is wired: every assertion here reads `CanvasView` directly, and the rail's
  // Views launcher (the only rail item this test drives) is drawn with or without one
  // (src/dock/dock.cpp). One fewer collaborator, no change to the code under test.
  ace::dock::Dockspace dockspace;
  shell.set_draw_content([&dockspace, &canvas]() {
    dockspace.draw();
    canvas.reconcile(dockspace.layout().view_ids());
  });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  // Raise the per-test watchdog well above its 60s default: fronting, sizing and distinctly
  // zooming ten canvases over one offscreen software-GL renderer legitimately runs past a minute
  // under ASan/TSan (and longer on a low-core CI host). The watchdog is a hang tripwire, not an
  // assertion — every IM_CHECK below still runs and still gates. Warning is pushed out too so the
  // expected long run does not spam the log.
  te_io.ConfigWatchdogWarning = 900.0f;
  te_io.ConfigWatchdogKillTest = 900.0f;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&dockspace, &canvas};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "multi_canvas", "view_id_natural_order");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    CanvasView& canvas = *e2e->canvas;
    ace::dock::Dockspace& dockspace = *e2e->dockspace;
    const std::string rail = ace::dock::tool_rail_title();
    std::vector<std::string> ids;
    for (int n = 1; n <= k_canvas_count; ++n) {
      ids.push_back(canvas_id(n));
    }
    ctx->Yield(2);

    // --- (1) Boot with one canvas ---------------------------------------------------
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));
    IM_CHECK(!layout_contains(dockspace, "canvas#2"));

    // --- (2) Open nine more -----------------------------------------------------------
    // The Views launcher mints canvas#2 … canvas#10 through `ViewRegistry::open`, which is what
    // makes the premise real: the monotonic, never-recycling counter (D-view-registry-4) genuinely
    // reaches ten in a session, and every new canvas tabs into the same leaf.
    for (int n = 2; n <= k_canvas_count; ++n) {
      ctx->ItemClick((rail + "/Canvas").c_str());
      ctx->Yield(3);
    }
    for (const std::string& id : ids) {
      IM_CHECK(layout_contains(dockspace, id));
    }

    // --- (3) Size all ten, and make each pane's framing DISTINGUISHABLE ---------------
    // The anti-vacuity phase (Constraint 6). Nine of the ten share one dock leaf and only the
    // ACTIVE tab runs `draw_content`, so a pane never brought to the front keeps
    // requested_width/height == 0 and is invisible to the rule's `sized()` test — a ten-pane test
    // that skips this pass passes on the UNFIXED tree and proves nothing.
    //
    // The wheel-zoom is the second half of the job: the nine tab-mates share one leaf and
    // therefore one device size, so without a per-pane camera "the fallback landed on canvas#2"
    // would be indistinguishable from "…on canvas#7" through `focused_framing()`.
    std::array<ViewFraming, k_canvas_count + 1> framing{};
    for (int n = 2; n <= k_canvas_count; ++n) {
      const std::string id = canvas_id(n);
      ctx->WindowFocus(id.c_str());
      IM_CHECK(pump_until(ctx, [&] { return canvas.focused_view_id() == id; }));
      // Constraint 6 (anti-vacuity): front the pane so `draw_content` sizes it, and wait for its
      // first published frame — that is what both makes it non-vacuously sized AND draws the
      // ##canvas_nav the wheel-zoom below targets (the nav is submitted only once the pane has a
      // texture, i.e. a consumed frame; src/app/canvas_view.cpp:202).
      IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued(id) >= 1; }));
      ctx->MouseMove((id + "/##canvas_nav").c_str());
      const std::uint64_t pre_zoom = canvas.frames_issued(id);
      ctx->MouseWheelY(static_cast<float>(n - 1)); // a distinct zoom per pane
      // Pace on THIS pane's OWN re-render: the zoom is device damage, so canvas#N publishes a fresh
      // frame; waiting for that one frame both confirms the gesture landed and throttles the
      // fronting loop so the single shared software-GL renderer never accumulates a nine-pane
      // backlog it cannot clear inside a pump deadline. A global "every pane quiet" settle, by
      // contrast, over nine tab-mates each still progressively refining an opaque backdrop, never
      // goes quiet in time on the offscreen ASan lane — the throughput stall that hung this leaf.
      IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued(id) > pre_zoom; }));
      // The hint survives the hover + wheel, so this really is canvas#N's own framing.
      IM_CHECK(canvas.focused_view_id() == id);
      framing[n] = canvas.focused_framing();
      IM_CHECK(framing[n].pane_w > 0);
      IM_CHECK(framing[n].pane_h > 0);
    }
    // Every pane in 2…10 drew, stamped the hint and navigated: all nine are sized, and no two
    // report the same camera — so a later "wrong pane" answer cannot hide behind a coincidence.
    IM_CHECK(canvas.focused_view_id() == "canvas#10");
    for (int n = 2; n <= k_canvas_count; ++n) {
      for (int m = n + 1; m <= k_canvas_count; ++m) {
        IM_CHECK(!affine_near(framing[n].camera, framing[m].camera));
      }
    }

    // --- (4) Arm the fallback ---------------------------------------------------------
    // Closing the hinted pane is the only shipped path that empties the sticky hint
    // (`CanvasView::reconcile`), so it is how the fallback branch becomes the live branch — over
    // nine sized panes, canvas#2 … canvas#10.
    ctx->WindowFocus("canvas#1");
    IM_CHECK(pump_until(ctx, [&] { return canvas.focused_view_id() == "canvas#1"; }));
    // Park ImGui's own focus on a NON-canvas tab first. The nine survivors share one dock leaf,
    // so if that leaf's ACTIVE tab were still a canvas it would take focus the instant canvas#1's
    // window stops being submitted and re-stamp the hint in the same frame reconcile cleared it —
    // resolving through the FOCUS branch and leaving the fallback branch untested. "inspector" is
    // a seeded tab of that same leaf and stamps nothing; the hint is sticky, so it still names
    // canvas#1 across this focus change.
    ctx->WindowFocus("inspector");
    ctx->Yield(3);
    IM_CHECK(canvas.focused_view_id() == "canvas#1");
    IM_CHECK(dockspace.close("canvas#1"));
    ctx->Yield(3);
    // Wait for the close to finalize in the layout — the same `reconcile` that drops canvas#1
    // clears the sticky hint and erases its presenter (`CanvasView::reconcile`), both UI-thread
    // effects observable here without waiting on the render thread to free the host entry.
    IM_CHECK(pump_until(ctx, [&] { return !layout_contains(dockspace, "canvas#1"); }));
    IM_CHECK(canvas.focused_view_id().empty()); // reconcile cleared the hint with the pane

    // --- (5) THE assertion this leaf exists for ---------------------------------------
    // Byte order over the nine survivors starts "canvas#10"; numeric order starts "canvas#2".
    // On the pre-fix tree this reports "canvas#10".
    IM_CHECK(canvas.indicated_view_id() == "canvas#2");
    // …and the VERB-facing half moved with the marker-facing half (Constraint 1): the framing the
    // mint/insert would consume is canvas#2's, not canvas#10's.
    const ViewFraming fallback = canvas.focused_framing();
    IM_CHECK(affine_near(fallback.camera, framing[2].camera));
    IM_CHECK(!affine_near(fallback.camera, framing[10].camera));
    IM_CHECK(fallback.pane_w == framing[2].pane_w);
    IM_CHECK(fallback.pane_h == framing[2].pane_h);
    // `primary_framing()` is the same rule with no hint, so it must agree.
    IM_CHECK(affine_near(canvas.primary_framing().camera, framing[2].camera));

    // --- (6) The order is TOTAL, not one lucky comparison ------------------------------
    // A comparator that special-cases the #1/#10 pair, or a min_element that happened to fire
    // once, answers "canvas#10" here.
    IM_CHECK(dockspace.close("canvas#2"));
    ctx->Yield(3);
    IM_CHECK(pump_until(ctx, [&] { return !layout_contains(dockspace, "canvas#2"); }));
    IM_CHECK(canvas.focused_view_id().empty());
    IM_CHECK(canvas.indicated_view_id() == "canvas#3");
    const ViewFraming next = canvas.focused_framing();
    IM_CHECK(affine_near(next.camera, framing[3].camera));
    IM_CHECK(!affine_near(next.camera, framing[10].camera));
    IM_CHECK(!affine_near(next.camera, framing[2].camera));
  };
  ImGuiTestEngine_QueueTest(engine, test);

  const int k_max_frames = 200000;
  int frames = 0;
  while (!ImGuiTestEngine_IsTestQueueEmpty(engine) && frames < k_max_frames) {
    shell.new_frame();
    shell.draw_ui();
    shell.render();
    ImGuiTestEngine_PostSwap(engine);
    ++frames;
  }

  int count_tested = 0;
  int count_success = 0;
  ImGuiTestEngine_GetResult(engine, count_tested, count_success);
  ImGuiTestEngine_Stop(engine);
  ace::views::register_view_body(ViewType::Canvas, {});

  CHECK(frames < k_max_frames);
  CHECK(count_tested == 1);
  CHECK(count_success == 1);

  canvas.destroy();
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
