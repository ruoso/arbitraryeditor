// editor.paint.brush — the paint-stroke e2e (docs §9, the offscreen software-GL lane; rig cloned
// from tests/tool_dispatch_e2e_test.cpp + tests/gizmo_e2e_test.cpp). Boots the shell over a REAL
// commands::AppState, seeds a bounded org.arbc.raster cell (plus an opaque backdrop so the pane
// issues its first frame, and a bounded non-raster solid), threads the rail's active tool into the
// Canvas body, and drives "canvas#1/##canvas_nav" by RAW MOUSE POSITION under the Brush tool.
//
// Asserts:
//   * paint + one undo — a press-drag-release over the selected raster changes its pixels and rises
//     the applied-entry count journal().cursor() by EXACTLY 1 (a whole stroke coalesces to one
//     entry, D15); one undo press restores the exact prior pixels (byte-equal). cursor() (not
//     depth()) is the measure, since undo() rewinds the cursor but leaves the entry in the tail.
//   * size field reachable — the on-canvas size widget is addressable by its stable id (the ring is
//     drawn every Brush frame; its screen-locked radius is a pure function, unit-pinned at L1).
//   * dispatch gating — a Select drag paints nothing (D20); switching back to Brush paints again.
//   * no writable target (Constraint 4) — with a non-raster / no cell selected, a Brush drag paints
//     nothing.
//   * nav survives (Constraint 1) — with Brush active, a Space-drag pans and deposits NO dab, and
//     the wheel still zooms.
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/commands/selection.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/dockmodel/tool_rail.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/interact/pick.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/kind_raster/raster_content.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "writer_session.hpp"
#include <GLES3/gl3.h>

using ace::app::CanvasView;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::commands::AppState;
using ace::dockmodel::ToolId;
using ace::dockmodel::ViewType;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  explicit ScratchDir(const char* tag)
      : root(std::filesystem::temp_directory_path() / (std::string("ace_brush_e2e_") + tag)) {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// Geometry in COMPOSITION units, 1:1 with pane-relative device px (canvas#1's viewport starts at
// identity). A 60x60 raster at (40,40) covers [40,100]^2; a 40x40 non-raster solid at (150,150)
// covers [150,190]^2; empty canvas is out beyond x=100 below the size widget row.
constexpr double k_raster_at = 40.0;
constexpr double k_solid_at = 150.0;

struct Seeded {
  arbc::ObjectId raster;
  arbc::ObjectId solid;
};

Seeded seed(AppState& state) {
  arbc::Document& doc = state.document();
  const arbc::ObjectId comp = doc.add_composition(256.0, 256.0);
  // Opaque unbounded backdrop first, so the pane issues its first (non-transparent) frame — the
  // render gate withholds an all-transparent frame (gizmo_e2e note).
  ace::scene::add_cell(doc, state.registry(), "org.arbc.solid", "0.15,0.2,0.25,1",
                       arbc::Affine::identity());
  Seeded out;
  const arbc::expected<arbc::ObjectId, std::string> r =
      ace::scene::add_cell(doc, state.registry(), "org.arbc.raster", "60x60",
                           arbc::Affine::translation(k_raster_at, k_raster_at));
  out.raster = r.has_value() ? *r : arbc::ObjectId{};
  // A bounded, pickable, NON-raster cell (a Constraint-4 no-writable-target primary).
  const arbc::Rect extent{0.0, 0.0, 40.0, 40.0};
  out.solid = doc.add_content(
      std::make_shared<arbc::SolidContent>(arbc::Rgba{0.0F, 0.0F, 0.6F, 1.0F}, extent));
  doc.attach_layer(comp,
                   doc.add_layer(out.solid, arbc::Affine::translation(k_solid_at, k_solid_at)));
  return out;
}

// The raster's live level-0 pixel buffer (a lock-free pinned read of the resolved content) — the
// pixels the brush mutates. Empty if the id no longer resolves to a raster.
std::vector<float> raster_pixels(AppState& state, arbc::ObjectId cell) {
  auto* raster = dynamic_cast<arbc::RasterContent*>(state.document().resolve(cell));
  return raster != nullptr ? raster->store().base_table()->level_pixels(0) : std::vector<float>{};
}

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
  CanvasView* canvas;
  AppState* state;
  ace::dock::Dockspace* dockspace;
  Seeded ids;
  std::atomic<bool> request_undo{false};
  std::atomic<bool> undo_done{false};
};

} // namespace

TEST_CASE("brush e2e: a Brush drag paints the selected raster as ONE undoable stroke; Select/no-"
          "target/Space-nav paint nothing") {
  ScratchDir scratch("main");
  ace::platform::NativeFileSystem fs;
  ace::testing::WriterSession session(scratch.root / "brush");
  REQUIRE(session.ok());
  AppState& state = session.state();
  Seeded ids;
  session.on_writer([&] { ids = seed(state); });
  REQUIRE(ids.raster.valid());
  REQUIRE(ids.solid.valid());
  REQUIRE(state.selection().empty());

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 900;
  opts.height = 640;
  REQUIRE(shell.init(opts));

  CanvasView canvas(state, session.writer());
  ace::dock::Dockspace dockspace;
  ace::views::register_view_body(ViewType::Canvas, [&canvas, &dockspace](std::string_view view_id) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    canvas.draw_content(view_id, static_cast<int>(avail.x), static_cast<int>(avail.y),
                        dockspace.tools().active());
  });

  ace::dockmodel::RecentProjects recent(scratch.root / "prefs", fs);
  NoopFolderDialog dialog;
  NoopLauncher launcher;
  ace::app::AppProjectGateway gateway(recent, fs, dialog, launcher, "/usr/bin/arbitraryeditor",
                                      state);
  gateway.set_edit_runner(
      [&canvas](const std::function<void()>& edit) { canvas.apply_edit(edit); });

  shell.set_draw_content([&dockspace, &canvas]() {
    dockspace.draw();
    canvas.reconcile(dockspace.layout().view_ids());
  });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&canvas, &state, &dockspace, ids};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "brush", "paint_undo_gating_nav");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    CanvasView& canvas = *e2e->canvas;
    AppState& state = *e2e->state;
    ace::dock::Dockspace& dockspace = *e2e->dockspace;
    ace::commands::Selection& sel = state.selection();
    const arbc::Journal& journal = state.document().journal();
    const arbc::ObjectId raster = e2e->ids.raster;
    const arbc::ObjectId solid = e2e->ids.solid;

    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));
    ctx->WindowFocus("canvas#1");
    ctx->Yield(3);

    const auto pane_origin = [&]() {
      const ImGuiTestItemInfo info = ctx->ItemInfo("canvas#1/##canvas_nav");
      return ImVec2(info.RectFull.Min.x, info.RectFull.Min.y);
    };
    const ImVec2 origin = pane_origin();
    IM_CHECK(origin.x >= 0.0F && origin.y >= 0.0F);
    const auto at = [&origin](float cx, float cy) { return ImVec2(origin.x + cx, origin.y + cy); };
    const auto click_at = [&](ImVec2 pos) {
      ctx->MouseMoveToPos(pos);
      ctx->MouseDown(0);
      ctx->MouseUp(0);
      ctx->Yield(2);
    };
    const auto drag = [&](ImVec2 from, ImVec2 to) {
      ctx->MouseMoveToPos(from);
      ctx->MouseDown(0);
      ctx->MouseMoveToPos(to);
      ctx->MouseUp(0);
      ctx->Yield(2);
    };
    const auto set_tool = [&](ToolId t) {
      dockspace.tools().select(t);
      ctx->Yield(3);
    };
    const auto undo_one = [&]() {
      e2e->request_undo.store(true);
      IM_CHECK(pump_until(ctx, [&] { return e2e->undo_done.load(); }));
      e2e->undo_done.store(false);
      ctx->Yield(2);
    };

    ctx->MouseMove("canvas#1/##canvas_nav"); // establish the viewport for raw-position moves

    // Select the raster (a body click under the Select tool), then switch to Brush.
    set_tool(ToolId::Select);
    click_at(at(70.0F, 70.0F));
    IM_CHECK(sel.primary() == raster);
    IM_CHECK(sel.size() == 1);

    // --- paint + one undo: a stroke is one journal entry; undo restores the pixels. ------------
    set_tool(ToolId::Brush);
    // The size widget is reachable by its stable id (D5): clicking it sets a new size (the ring is
    // drawn every Brush frame; its exact radius is a screenshot-baseline / L1-unit concern).
    ctx->MouseMove("canvas#1/##brush_size");
    ctx->MouseDown(0);
    ctx->MouseUp(0);
    ctx->Yield(2);

    const std::vector<float> before = raster_pixels(state, raster);
    IM_CHECK(!before.empty());
    const std::size_t cursor0 = journal.cursor();
    const std::uint64_t rev0 = state.document().pin()->revision();
    drag(at(55.0F, 55.0F), at(85.0F, 85.0F));  // press-drag-release across the raster
    IM_CHECK(journal.cursor() == cursor0 + 1); // the WHOLE stroke folded to one entry (D15)
    IM_CHECK(state.document().pin()->revision() > rev0);
    const std::vector<float> painted = raster_pixels(state, raster);
    IM_CHECK(painted != before); // pixels changed

    undo_one();
    IM_CHECK(journal.cursor() == cursor0);
    IM_CHECK(raster_pixels(state, raster) == before); // one press restored the exact prior pixels

    // --- dispatch gating: a Select drag paints nothing; Brush paints again. --------------------
    set_tool(ToolId::Select);
    const std::size_t cursor_sel = journal.cursor();
    drag(at(200.0F, 40.0F), at(240.0F, 80.0F));       // marquee over EMPTY canvas: no commit
    IM_CHECK(journal.cursor() == cursor_sel);         // Select never paints / commits here
    IM_CHECK(raster_pixels(state, raster) == before); // and the raster is untouched
    click_at(at(70.0F, 70.0F));                       // re-select the raster
    IM_CHECK(sel.primary() == raster);
    set_tool(ToolId::Brush);
    drag(at(55.0F, 55.0F), at(85.0F, 85.0F));
    IM_CHECK(journal.cursor() == cursor_sel + 1); // Brush paints again
    IM_CHECK(raster_pixels(state, raster) != before);

    // --- no writable target (Constraint 4): a non-raster primary paints nothing. ---------------
    set_tool(ToolId::Select);
    click_at(at(170.0F, 170.0F)); // select the bounded solid (non-raster)
    IM_CHECK(sel.primary() == solid);
    set_tool(ToolId::Brush);
    const std::size_t cursor_solid = journal.cursor();
    drag(at(160.0F, 160.0F), at(185.0F, 185.0F)); // over the solid
    IM_CHECK(journal.cursor() == cursor_solid);   // non-raster primary => no paint

    // --- nav survives under Brush (Constraint 1): Space pans + deposits no dab; wheel zooms. ----
    // Re-select the raster so a stray dab WOULD be detectable, then prove Space suppresses it.
    set_tool(ToolId::Select);
    click_at(at(70.0F, 70.0F));
    IM_CHECK(sel.primary() == raster);
    set_tool(ToolId::Brush);
    const std::size_t cursor_nav = journal.cursor();
    const arbc::Affine view_before_space = canvas.primary_framing().camera;
    ctx->KeyDown(ImGuiKey_Space);
    drag(at(55.0F, 55.0F), at(80.0F, 75.0F)); // Space-held drag over the raster
    ctx->KeyUp(ImGuiKey_Space);
    ctx->Yield(3);
    IM_CHECK(journal.cursor() == cursor_nav);                          // NO dab under Space
    IM_CHECK(!(canvas.primary_framing().camera == view_before_space)); // Space panned instead

    const double units_before = canvas.scale_bar_units("canvas#1");
    IM_CHECK(units_before > 0.0);
    ctx->MouseMoveToPos(at(60.0F, 60.0F));
    ctx->MouseWheelY(10.0F);
    ctx->Yield(3);
    const double units_after = canvas.scale_bar_units("canvas#1");
    IM_CHECK(units_after > 0.0);
    IM_CHECK(units_after < units_before); // wheel still zooms under Brush
  };
  ImGuiTestEngine_QueueTest(engine, test);

  const int k_max_frames = 200000;
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
