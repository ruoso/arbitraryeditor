// editor.panels.overview_gizmo — the full scale/rotate/shear gizmo on schematic overview boxes, UI
// e2e (docs §9, the offscreen software-GL lane; modeled on tests/overview_e2e_test.cpp +
// tests/gizmo_e2e_test.cpp). Boots the shell over a REAL AppState with a LIVE canvas#1 (so the
// navigator/fit is sized) and the OverviewPanel body, seeds an axis-aligned transform box, a
// ROTATED box (proving `placed_quad` anchoring, not an AABB), a two-box group, a fixed reference
// box (used to recover the composition->screen fit each frame), a camera, and a nested composition.
// It selects a box and drives each gizmo handle by RAW mouse position through the shipped L1 verbs:
// a corner drag scales proportionally (Shift = free distort), an edge drag stretches 1D, the rotate
// ring rotates (Shift snaps 15°), Ctrl+edge shears; a mid-drag preview leaves the document/journal
// untouched and a release commits ONE undoable transaction; a rotated box's corner drag scales it
// (anchor proof); a
// >=2 selection shows one group gizmo whose corner drag scales every member as one transaction; and
// while a composition is entered no out-of-scope box or camera gizmo is reachable (D29). Writer ops
// run on the MAIN thread through apply_edit / the writer session (the inline pool sidesteps the
// pinned nested-render worker-detach race).
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/overview_panel.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/project/project.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/worker_pool.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "writer_session.hpp"

using ace::app::CanvasView;
using ace::app::OverviewPanel;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::commands::AppState;
using ace::dockmodel::ViewType;

namespace {

// Shared scene constants (seeding + drive math read the SAME values). All within a 200x200 root.
constexpr double k_ref_x = 6.0;
constexpr double k_ref_y = 6.0;
constexpr double k_ref_ext = 16.0;
constexpr double k_tbox_x = 120.0;
constexpr double k_tbox_y = 20.0;
constexpr double k_tbox_ext = 24.0;
constexpr double k_rot_x = 40.0;
constexpr double k_rot_y = 120.0;
constexpr double k_rot_ext = 20.0;
constexpr double k_rot_angle = 0.6;
constexpr double k_ga_x = 140.0;
constexpr double k_ga_y = 150.0;
constexpr double k_gb_x = 175.0;
constexpr double k_gb_y = 175.0;
constexpr double k_g_ext = 16.0;

struct ScratchDir {
  std::filesystem::path root;
  explicit ScratchDir(const char* tag)
      : root(std::filesystem::temp_directory_path() /
             (std::string("ace_overview_gizmo_e2e_") + tag)) {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

template <class Ready> bool pump_until(ImGuiTestContext* ctx, Ready ready) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (std::chrono::steady_clock::now() < deadline) {
    if (ready()) {
      return true;
    }
    ctx->Yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return ready();
}

class NoopFolderDialog final : public ace::app::FolderDialog {
public:
  void show(Callback) override {}
};
class NoopLauncher final : public ace::platform::ProcessLauncher {
public:
  std::error_code spawn_detached(const std::filesystem::path&,
                                 const std::vector<std::string>&) const override {
    return {};
  }
};

struct E2EState {
  CanvasView* canvas = nullptr;
  AppState* state = nullptr;
  arbc::ObjectId ref_box; // fixed, never selected/transformed — the fit reference
  arbc::ObjectId tbox;    // axis-aligned transform box
  arbc::ObjectId rot_box; // rotated box (placed_quad anchoring proof)
  arbc::ObjectId gbox_a;
  arbc::ObjectId gbox_b;
  arbc::ObjectId camera;
  arbc::ObjectId nested;
  arbc::ObjectId child_comp;
  arbc::ObjectId child_cell;
  arbc::ObjectId root_comp;
  std::atomic<bool> request_undo{false};
  std::atomic<bool> undo_done{false};
};

std::string ref_cell(arbc::ObjectId id) {
  return "Overview/###ov_cell_" + std::to_string(id.value);
}
std::string ref_cam(arbc::ObjectId id) { return "Overview/###ov_cam_" + std::to_string(id.value); }
std::string ref_crumb(arbc::ObjectId id) {
  return "Overview/###ov_crumb_" + std::to_string(id.value);
}

std::optional<arbc::Affine> placement_of(AppState& state, arbc::ObjectId id) {
  for (const ace::scene::Cell& c : ace::scene::cells(state.document(), state.registry())) {
    if (c.id == id) {
      return c.placement;
    }
  }
  return std::nullopt;
}

} // namespace

TEST_CASE("overview_gizmo e2e: eight handles, rotated anchor, preview/commit, scope, group") {
  ScratchDir scratch("main");
  ace::platform::NativeFileSystem fs;
  ace::testing::WriterSession session(scratch.root / "ov");
  REQUIRE(session.ok());
  AppState& state = session.state();

  E2EState e2e;
  e2e.state = &state;
  session.on_writer([&] {
    arbc::Document& doc = state.document();
    const arbc::Registry& reg = state.registry();
    doc.add_composition(200.0, 200.0);
    // An unbounded solid base (no box in the schematic) so the composition has a renderable ground
    // — the live canvas#1 scaffolding needs published pixels for `frames_issued` to advance.
    (void)ace::scene::add_cell(doc, reg, "org.arbc.solid", "0.1,0.2,0.3,1",
                               arbc::Affine::identity());
    e2e.ref_box = *ace::scene::add_cell(doc, reg, "org.arbc.raster", "16x16",
                                        arbc::Affine::translation(k_ref_x, k_ref_y));
    e2e.tbox = *ace::scene::add_cell(doc, reg, "org.arbc.raster", "24x24",
                                     arbc::Affine::translation(k_tbox_x, k_tbox_y));
    e2e.rot_box = *ace::scene::add_cell(doc, reg, "org.arbc.raster", "20x20",
                                        arbc::Affine{std::cos(k_rot_angle), std::sin(k_rot_angle),
                                                     -std::sin(k_rot_angle), std::cos(k_rot_angle),
                                                     k_rot_x, k_rot_y});
    e2e.gbox_a = *ace::scene::add_cell(doc, reg, "org.arbc.raster", "16x16",
                                       arbc::Affine::translation(k_ga_x, k_ga_y));
    e2e.gbox_b = *ace::scene::add_cell(doc, reg, "org.arbc.raster", "16x16",
                                       arbc::Affine::translation(k_gb_x, k_gb_y));
    e2e.camera = ace::scene::add_camera(doc, reg, "Hero", ace::scene::Resolution{20, 16},
                                        arbc::Affine::translation(170.0, 8.0));
    e2e.child_comp = doc.add_composition(48.0, 48.0);
    e2e.child_cell = *ace::scene::add_cell(doc, reg, "org.arbc.raster", "8x8",
                                           arbc::Affine::identity(), e2e.child_comp);
    e2e.nested =
        *ace::scene::add_cell(doc, reg, "org.arbc.nested", std::to_string(e2e.child_comp.value),
                              arbc::Affine::translation(10.0, 60.0));
  });
  REQUIRE(e2e.tbox.valid());
  REQUIRE(e2e.rot_box.valid());
  e2e.root_comp = ace::scene::active_composition(state.document(), std::nullopt);
  REQUIRE(e2e.root_comp.valid());

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 1100;
  opts.height = 760;
  REQUIRE(shell.init(opts));

  // The inline settle-fully pool (`WorkerPoolConfig{}`) sidesteps the pinned arbc v0.4.0
  // nested-render worker-detach race — the same convention the overview e2e uses; interactive
  // worker threading is covered by the TSan case and nothing here depends on the pool.
  CanvasView canvas(state, session.writer(), arbc::WorkerPoolConfig{}, std::chrono::hours(1));
  e2e.canvas = &canvas;
  OverviewPanel overview(state, canvas);

  ace::views::register_view_body(ViewType::Canvas, [&canvas](std::string_view view_id) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content(view_id, static_cast<int>(avail.x), static_cast<int>(avail.y));
  });

  ace::dock::Dockspace dockspace;
  ace::dockmodel::RecentProjects recent(scratch.root / "prefs", fs);
  NoopFolderDialog dialog;
  NoopLauncher launcher;
  ace::app::AppProjectGateway gateway(recent, fs, dialog, launcher, "/usr/bin/arbitraryeditor",
                                      state);
  gateway.set_edit_runner(
      [&canvas](const std::function<void()>& edit) { canvas.apply_edit(edit); });

  shell.set_draw_content([&dockspace, &overview]() {
    dockspace.draw();
    ImGui::SetNextWindowSize(ImVec2{560.0F, 560.0F}, ImGuiCond_Once);
    ImGui::Begin("Overview");
    overview.draw("overview");
    ImGui::End();
  });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  ImGuiTest* test =
      IM_REGISTER_TEST(engine, "overview_gizmo", "handles_anchor_preview_scope_group");
  test->UserData = &e2e;

  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    AppState& state = *e2e->state;
    CanvasView& canvas = *e2e->canvas;
    const arbc::Journal& journal = state.document().journal();

    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));
    ctx->WindowFocus("Overview");
    ctx->Yield(3);

    // The composition->screen fit recovered each frame from the FIXED reference box's screen AABB
    // (an axis-aligned box's `###ov_cell_` rect is exactly `to_screen` of its placed corners). Fit
    // is uniform, so one axis gives the scale; the reference box is never selected (never widened).
    struct Xform {
      ImVec2 a;
      double s;
    };
    const auto xform = [&]() -> Xform {
      const ImGuiTestItemInfo info = ctx->ItemInfo(ref_cell(e2e->ref_box).c_str());
      const double s = static_cast<double>(info.RectFull.Max.x - info.RectFull.Min.x) / k_ref_ext;
      return Xform{ImVec2{info.RectFull.Min.x - static_cast<float>(s * k_ref_x),
                          info.RectFull.Min.y - static_cast<float>(s * k_ref_y)},
                   s};
    };
    const auto to_screen = [](const Xform& m, arbc::Vec2 c) {
      return ImVec2{m.a.x + static_cast<float>(m.s * c.x), m.a.y + static_cast<float>(m.s * c.y)};
    };
    const auto drag = [&](ImVec2 from, ImVec2 to, bool shift, bool ctrl) {
      if (shift) {
        ctx->KeyDown(ImGuiMod_Shift);
      }
      if (ctrl) {
        ctx->KeyDown(ImGuiMod_Ctrl);
      }
      ctx->MouseMoveToPos(from);
      ctx->MouseDown(0);
      ctx->MouseMoveToPos(to);
      ctx->MouseUp(0);
      if (ctrl) {
        ctx->KeyUp(ImGuiMod_Ctrl);
      }
      if (shift) {
        ctx->KeyUp(ImGuiMod_Shift);
      }
      ctx->Yield(3);
    };
    const auto undo_one = [&]() {
      e2e->request_undo.store(true);
      IM_CHECK(pump_until(ctx, [&] { return e2e->undo_done.load(); }));
      e2e->undo_done.store(false);
      ctx->Yield(2);
    };
    const auto pl = [&](arbc::ObjectId id) { return *placement_of(state, id); };

    // The gizmo shows for the selected box (8 handles anchored to placed_quad); its `###ov_cell_`
    // hit area widens to catch the outside handles.
    IM_CHECK(ctx->ItemExists(ref_cell(e2e->tbox).c_str()));

    // --- (i) Corner drag: proportional by default, free-distort under Shift. --------------------
    {
      ctx->ItemClick(ref_cell(e2e->tbox).c_str());
      ctx->Yield(2);
      IM_CHECK(state.selection().primary() == e2e->tbox);
      const Xform m = xform();
      const arbc::Affine before = pl(e2e->tbox);
      const arbc::Vec2 corner{k_tbox_x + k_tbox_ext, k_tbox_y + k_tbox_ext};
      drag(to_screen(m, corner), to_screen(m, {corner.x + 12.0, corner.y + 12.0}), false, false);
      const arbc::Affine prop = pl(e2e->tbox);
      IM_CHECK(prop.a > before.a);               // scaled up
      IM_CHECK(prop.a == Catch::Approx(prop.d)); // proportional (aspect preserved)
      undo_one();
      IM_CHECK(pl(e2e->tbox) == before);

      const Xform m2 = xform();
      drag(to_screen(m2, corner), to_screen(m2, {corner.x + 14.0, corner.y + 4.0}), true, false);
      const arbc::Affine dist = pl(e2e->tbox);
      IM_CHECK(dist.a != Catch::Approx(dist.d)); // Shift = free distort (independent x/y)
      undo_one();
      IM_CHECK(pl(e2e->tbox) == before);
    }

    // --- (ii) Edge drag: 1D stretch along the box's own axis. -----------------------------------
    {
      ctx->ItemClick(ref_cell(e2e->tbox).c_str());
      ctx->Yield(2);
      const Xform m = xform();
      const arbc::Affine before = pl(e2e->tbox);
      const arbc::Vec2 right_mid{k_tbox_x + k_tbox_ext, k_tbox_y + k_tbox_ext * 0.5};
      drag(to_screen(m, right_mid), to_screen(m, {right_mid.x + 14.0, right_mid.y}), false, false);
      const arbc::Affine after = pl(e2e->tbox);
      IM_CHECK(after.a != Catch::Approx(before.a)); // stretched along X
      IM_CHECK(after.d == Catch::Approx(before.d)); // the orthogonal axis is untouched (1D)
      undo_one();
      IM_CHECK(pl(e2e->tbox) == before);
    }

    // --- (iii) Rotate ring: Shift snaps to a 15° multiple. --------------------------------------
    {
      ctx->ItemClick(ref_cell(e2e->tbox).c_str());
      ctx->Yield(2);
      const Xform m = xform();
      const arbc::Affine before = pl(e2e->tbox);
      const arbc::Vec2 center{k_tbox_x + k_tbox_ext * 0.5, k_tbox_y + k_tbox_ext * 0.5};
      const arbc::Vec2 corner{k_tbox_x + k_tbox_ext, k_tbox_y + k_tbox_ext};
      const double off = 12.0 / m.s; // between corner_tol (6/s) and rotate_tol (18/s): the ring
      const arbc::Vec2 grab{corner.x + off * 0.7071, corner.y + off * 0.7071};
      // Sweep ~90° about the center (a clean 15° multiple under Shift snap).
      const arbc::Vec2 rel{grab.x - center.x, grab.y - center.y};
      const arbc::Vec2 target{center.x - rel.y, center.y + rel.x};
      drag(to_screen(m, grab), to_screen(m, target), true, false);
      const arbc::Affine after = pl(e2e->tbox);
      IM_CHECK(std::abs(after.b) > 0.5); // an off-diagonal appeared — the box rotated ~90°
      IM_CHECK(std::abs(after.a) < 0.5);
      undo_one();
      IM_CHECK(pl(e2e->tbox) == before);
    }

    // --- (iv) Ctrl + edge: shear (advanced). ----------------------------------------------------
    {
      ctx->ItemClick(ref_cell(e2e->tbox).c_str());
      ctx->Yield(2);
      const Xform m = xform();
      const arbc::Affine before = pl(e2e->tbox);
      const arbc::Vec2 top_mid{k_tbox_x + k_tbox_ext * 0.5, k_tbox_y};
      drag(to_screen(m, top_mid), to_screen(m, {top_mid.x + 12.0, top_mid.y}), false, true);
      const arbc::Affine after = pl(e2e->tbox);
      IM_CHECK(after.c != Catch::Approx(before.c)); // off-diagonal shear (y->x), not a translation
      undo_one();
      IM_CHECK(pl(e2e->tbox) == before);
    }

    // --- (v) placed_quad anchoring: a ROTATED box's corner drag SCALES (not moves) it. ----------
    // The gizmo anchors handles to the rotated placed quad (D-gizmo-7), so grabbing the rotated
    // corner scales; an AABB anchor would classify that point as body/miss and only move.
    {
      ctx->ItemClick(ref_cell(e2e->rot_box).c_str());
      ctx->Yield(2);
      IM_CHECK(state.selection().primary() == e2e->rot_box);
      const Xform m = xform();
      const arbc::Affine before = pl(e2e->rot_box);
      const arbc::Vec2 rcorner = before.apply({k_rot_ext, k_rot_ext});
      const arbc::Vec2 rcenter = before.apply({k_rot_ext * 0.5, k_rot_ext * 0.5});
      const arbc::Vec2 dir{rcorner.x - rcenter.x, rcorner.y - rcenter.y};
      const double len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
      const arbc::Vec2 out{rcorner.x + dir.x / len * 10.0, rcorner.y + dir.y / len * 10.0};
      drag(to_screen(m, rcorner), to_screen(m, out), false, false);
      const arbc::Affine after = pl(e2e->rot_box);
      // The LINEAR part changed => a scale (a body move would leave a/b/c/d intact, only tx/ty).
      const bool linear_changed =
          !(after.a == Catch::Approx(before.a) && after.b == Catch::Approx(before.b) &&
            after.c == Catch::Approx(before.c) && after.d == Catch::Approx(before.d));
      IM_CHECK(linear_changed);
      undo_one();
      IM_CHECK(pl(e2e->rot_box) == before);
    }

    // --- (vi) Preview then commit: mid-drag untouched; release = ONE entry; undo restores. ------
    {
      ctx->ItemClick(ref_cell(e2e->tbox).c_str());
      ctx->Yield(2);
      const Xform m = xform();
      const arbc::Affine before = pl(e2e->tbox);
      const std::size_t cursor_before = journal.cursor();
      const arbc::Vec2 corner{k_tbox_x + k_tbox_ext, k_tbox_y + k_tbox_ext};
      const ImVec2 from = to_screen(m, corner);
      const ImVec2 mid = to_screen(m, {corner.x + 6.0, corner.y + 6.0});
      const ImVec2 to = to_screen(m, {corner.x + 12.0, corner.y + 12.0});
      ctx->MouseMoveToPos(from);
      ctx->MouseDown(0);
      ctx->MouseMoveToPos(mid);
      ctx->Yield(2);
      IM_CHECK(journal.cursor() == cursor_before); // preview only — no transaction mid-drag
      IM_CHECK(pl(e2e->tbox) == before);           // the document is untouched during the drag
      ctx->MouseMoveToPos(to);
      ctx->MouseUp(0);
      ctx->Yield(3);
      IM_CHECK(journal.cursor() == cursor_before + 1); // release commits exactly ONE entry
      IM_CHECK(!(pl(e2e->tbox) == before));
      undo_one();
      IM_CHECK(pl(e2e->tbox) == before); // one-press undo restores the pre-drag placement
    }

    // --- (vii) Scope confinement (D29): entered => no out-of-scope box / camera gizmo reachable. -
    {
      ctx->ItemDoubleClick(ref_cell(e2e->nested).c_str());
      ctx->Yield(4);
      IM_CHECK(ctx->ItemExists(ref_cell(e2e->child_cell).c_str())); // re-rooted to the child
      IM_CHECK(!ctx->ItemExists(ref_cell(e2e->tbox).c_str()));      // root box gone (no gizmo)
      IM_CHECK(!ctx->ItemExists(ref_cell(e2e->rot_box).c_str()));
      IM_CHECK(!ctx->ItemExists(ref_cam(e2e->camera).c_str())); // no camera while entered
      ctx->ItemClick(ref_crumb(e2e->root_comp).c_str());        // climb out to Root
      ctx->Yield(3);
      IM_CHECK(ctx->ItemExists(ref_cell(e2e->tbox).c_str()));
    }

    // --- (viii) Group gizmo: a >=2 selection scales every member about the group pivot, one txn. -
    {
      ctx->ItemClick(ref_cell(e2e->gbox_a).c_str());
      ctx->Yield(2);
      ctx->KeyDown(ImGuiMod_Shift);
      ctx->ItemClick(ref_cell(e2e->gbox_b).c_str());
      ctx->KeyUp(ImGuiMod_Shift);
      ctx->Yield(2);
      IM_CHECK(state.selection().size() == 2);

      const Xform m = xform();
      const arbc::Affine before_a = pl(e2e->gbox_a);
      const arbc::Affine before_b = pl(e2e->gbox_b);
      const std::size_t cursor_before = journal.cursor();
      // The union's TOP-RIGHT corner handle — over EMPTY schematic space (the two boxes sit on a
      // diagonal), so the background item resolves the group handle grab (the interior stays
      // marquee/click-through, D-overview_gizmo-5). Union = (ga)-(gb+ext); TR = (gb+ext, ga).
      const arbc::Vec2 utr{k_gb_x + k_g_ext, k_ga_y};
      drag(to_screen(m, utr), to_screen(m, {utr.x + 14.0, utr.y - 14.0}), false, false);
      IM_CHECK(journal.cursor() == cursor_before + 1); // the whole batch is ONE transaction
      IM_CHECK(!(pl(e2e->gbox_a) == before_a));        // every member transformed
      IM_CHECK(!(pl(e2e->gbox_b) == before_b));
      IM_CHECK(pl(e2e->gbox_a).a > before_a.a); // the shared delta scaled both up
      IM_CHECK(pl(e2e->gbox_b).a > before_b.a);
      undo_one();
      IM_CHECK(pl(e2e->gbox_a) == before_a);
      IM_CHECK(pl(e2e->gbox_b) == before_b);
    }
  };
  ImGuiTestEngine_QueueTest(engine, test);

  const int k_max_frames = 300000;
  int frames = 0;
  while (!ImGuiTestEngine_IsTestQueueEmpty(engine) && frames < k_max_frames) {
    shell.new_frame();
    shell.draw_ui();
    shell.render();
    ImGuiTestEngine_PostSwap(engine);
    if (e2e.request_undo.exchange(false)) {
      canvas.apply_edit([&] { (void)ace::commands::undo(state); });
      e2e.undo_done.store(true);
    }
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
