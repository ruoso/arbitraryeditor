// editor.canvas.grid — the composition-grid UI e2e (docs §9, offscreen software-GL lane; modeled
// on tests/gizmo_e2e_test.cpp + tests/look_through_e2e_test.cpp). Boots the shell over a REAL
// commands::AppState, seeds two bounded raster cells over an opaque backdrop, and drives the
// per-pane grid control in the Canvas body. Asserts: (i) the "Show grid###grid_show" overlay toggle
// flips canvas.grid_visible and the grid actually RENDERS (a differential pixel check — the grid-on
// frame differs from the grid-off frame, since toggling the grid re-strokes the pane's draw list
// without re-rendering the composition); (ii) with a known spacing a body drag that lands a cell's
// edge just inside snap tolerance of a k*spacing line snaps FLUSH onto it while content_bounds
// (native resolution) is UNCHANGED (D8) — one journal entry; (iii) a Cmd/Ctrl body drag near the
// same line does NOT snap (bypass, §6:258); (iv) with the grid HIDDEN the same drag does NOT grid-
// snap (D-grid-3, visibility coupling); (v) the GROUP path (≥2 selected) snaps the union box to the
// grid too, one journal entry. Writer ops run on the MAIN thread through apply_edit.
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/commands/selection.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/interact/interact.hpp>
#include <ace/interact/pick.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
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
using ace::dockmodel::ViewType;

namespace {

constexpr double k_spacing = 128.0; // the grid spacing the snap tests drive (line at x=128)
constexpr double k_a_at = 20.0;     // cell A covers [20,80]^2
constexpr double k_b_at_y = 110.0;  // cell B covers [20,80] x [110,170]
constexpr double k_edge = 60.0;     // both cells are 60x60 rasters

struct ScratchDir {
  std::filesystem::path root;
  explicit ScratchDir(const char* tag)
      : root(std::filesystem::temp_directory_path() / (std::string("ace_grid_e2e_") + tag)) {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

struct Seeded {
  arbc::ObjectId cell_a;
  arbc::ObjectId cell_b;
};

Seeded seed(AppState& state) {
  arbc::Document& doc = state.document();
  doc.add_composition(320.0, 320.0);
  Seeded out;
  // An OPAQUE backdrop, added FIRST (bottom of the stack). Without opaque content the canvas render
  // gate withholds the all-transparent first frame and the `frames_issued >= 1` wait times out
  // (tests/gizmo_e2e_test.cpp). Being unbounded it has no extent: it contributes no snap edge, so
  // the ONLY snap targets in these tests are the grid lines this leaf mints.
  ace::scene::add_cell(doc, state.registry(), "org.arbc.solid", "0.15,0.2,0.25,1",
                       arbc::Affine::identity());
  const auto a = ace::scene::add_cell(doc, state.registry(), "org.arbc.raster", "60x60",
                                      arbc::Affine::translation(k_a_at, k_a_at));
  const auto b = ace::scene::add_cell(doc, state.registry(), "org.arbc.raster", "60x60",
                                      arbc::Affine::translation(k_a_at, k_b_at_y));
  out.cell_a = a.has_value() ? *a : arbc::ObjectId{};
  out.cell_b = b.has_value() ? *b : arbc::ObjectId{};
  return out;
}

bool capture_pixels(ImGuiID /*viewport_id*/, int x, int y, int w, int h, unsigned int* pixels,
                    void* /*user_data*/) {
  glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  return glGetError() == GL_NO_ERROR;
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
  Seeded ids;
  std::atomic<bool> request_undo{false};
  std::atomic<bool> undo_done{false};
  std::atomic<bool> capture_off{false};
  std::atomic<bool> capture_on{false};
  std::atomic<bool> off_done{false};
  std::atomic<bool> on_done{false};
};

std::optional<arbc::Affine> cell_placement(AppState& state, arbc::ObjectId id) {
  for (const ace::scene::Cell& c : ace::scene::cells(state.document(), state.registry())) {
    if (c.id == id) {
      return c.placement;
    }
  }
  return std::nullopt;
}
std::optional<arbc::Rect> cell_bounds(AppState& state, arbc::ObjectId id) {
  for (const ace::scene::Cell& c : ace::scene::cells(state.document(), state.registry())) {
    if (c.id == id) {
      return c.content_bounds;
    }
  }
  return std::nullopt;
}

} // namespace

TEST_CASE("grid e2e: toggle renders, snaps a cell + the group to the grid, coupled to visibility") {
  ScratchDir scratch("main");
  ace::platform::NativeFileSystem fs;
  ace::testing::WriterSession session(scratch.root / "gr");
  REQUIRE(session.ok());
  AppState& state = session.state();
  Seeded ids;
  session.on_writer([&] { ids = seed(state); });
  REQUIRE(ids.cell_a.valid());
  REQUIRE(ids.cell_b.valid());

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 900;
  opts.height = 640;
  REQUIRE(shell.init(opts));

  CanvasView canvas(state, session.writer());
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

  shell.set_draw_content([&dockspace, &canvas]() {
    dockspace.draw();
    canvas.reconcile(dockspace.layout().view_ids());
  });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&canvas, &state, ids};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "grid", "composition_grid");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    CanvasView& canvas = *e2e->canvas;
    AppState& state = *e2e->state;
    ace::commands::Selection& sel = state.selection();
    const Seeded ids = e2e->ids;
    const arbc::Journal& journal = state.document().journal();

    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));
    ctx->WindowFocus("canvas#1");
    ctx->Yield(3);

    // canvas#1's viewport is identity: a composition point maps to screen as pane_origin + comp.
    const ImGuiTestItemInfo info = ctx->ItemInfo("canvas#1/##canvas_nav");
    const ImVec2 origin(info.RectFull.Min.x, info.RectFull.Min.y);
    IM_CHECK(origin.x >= 0.0F && origin.y >= 0.0F);
    const auto at = [&](double cx, double cy) {
      return ImVec2(origin.x + static_cast<float>(cx), origin.y + static_cast<float>(cy));
    };
    const auto drag = [&](ImVec2 from, ImVec2 to) {
      ctx->MouseMoveToPos(from);
      ctx->MouseDown(0);
      ctx->MouseMoveToPos(to);
      ctx->MouseUp(0);
      ctx->Yield(3);
    };
    const auto click = [&](ImVec2 pos) {
      ctx->MouseMoveToPos(pos);
      ctx->MouseDown(0);
      ctx->MouseUp(0);
      ctx->Yield(2);
    };
    const auto undo_one = [&]() {
      e2e->request_undo.store(true);
      IM_CHECK(pump_until(ctx, [&] { return e2e->undo_done.load(); }));
      e2e->undo_done.store(false);
      ctx->Yield(2);
    };

    ctx->MouseMove("canvas#1/##canvas_nav"); // establish the viewport for raw-position moves

    // Select cell A (a body click; the gizmo appears next frame for the ONE selected cell).
    click(at(35.0, 65.0));
    IM_CHECK(sel.primary() == ids.cell_a);
    IM_CHECK(sel.size() == 1);
    const std::optional<arbc::Rect> bounds_seed = cell_bounds(state, ids.cell_a);
    REQUIRE(bounds_seed.has_value());

    // Let the composition settle so the grid-off/on pixel comparison isolates the grid overlay
    // (the composition frame only advances on a change, and nothing edits it between the two
    // captures — the grid is pure draw-list chrome).
    {
      std::uint64_t last = canvas.frames_issued("canvas#1");
      int stable = 0;
      for (int i = 0; i < 400 && stable < 8; ++i) {
        ctx->Yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const std::uint64_t now = canvas.frames_issued("canvas#1");
        if (now == last) {
          ++stable;
        } else {
          stable = 0;
          last = now;
        }
      }
    }

    // --- (i) grid-off baseline, then toggle "Show grid" ON and prove it renders. -----------------
    IM_CHECK(!canvas.grid_visible("canvas#1"));
    e2e->capture_off.store(true);
    IM_CHECK(pump_until(ctx, [&] { return e2e->off_done.load(); }));

    canvas.set_grid_spacing("canvas#1", k_spacing);
    ctx->ItemClick("canvas#1/Show grid###grid_show");
    ctx->Yield(3);
    IM_CHECK(canvas.grid_visible("canvas#1"));
    IM_CHECK(canvas.grid_spacing("canvas#1") == Catch::Approx(k_spacing));
    e2e->capture_on.store(true);
    IM_CHECK(pump_until(ctx, [&] { return e2e->on_done.load(); }));

    // --- (ii) a body drag lands cell A's left edge just inside tol of the x=128 grid line: it
    // snaps FLUSH onto 128, one journal entry, content_bounds (native resolution) UNCHANGED (D8).
    // --
    {
      const std::size_t depth = journal.cursor();
      // grab on the cell body, move +106 so the left edge (20+106=126) sits 2 units inside the
      // ~9-unit snap tolerance of the x=128 line.
      drag(at(35.0, 65.0), at(141.0, 65.0));
      IM_CHECK(journal.cursor() == depth + 1);
      const arbc::Affine snapped = *cell_placement(state, ids.cell_a);
      IM_CHECK(snapped.tx == Catch::Approx(k_spacing).margin(0.5)); // left edge flush on x=128
      IM_CHECK(cell_bounds(state, ids.cell_a) == bounds_seed);      // native resolution UNCHANGED
      undo_one();
      IM_CHECK(cell_placement(state, ids.cell_a)->tx == Catch::Approx(k_a_at));
    }

    // --- (iii) a Cmd/Ctrl body drag near the same line does NOT snap (bypass, §6:258). -----------
    {
      const std::size_t depth = journal.cursor();
      ctx->KeyDown(ImGuiMod_Ctrl);
      drag(at(35.0, 65.0), at(141.0, 65.0));
      ctx->KeyUp(ImGuiMod_Ctrl);
      IM_CHECK(journal.cursor() == depth + 1);
      const arbc::Affine free = *cell_placement(state, ids.cell_a);
      IM_CHECK(free.tx == Catch::Approx(k_a_at + 106.0).margin(2.0)); // free at ~126, NOT 128
      IM_CHECK(!(free.tx == Catch::Approx(k_spacing).margin(0.5)));
      undo_one();
    }

    // --- (iv) hide the grid: the SAME drag no longer grid-snaps (D-grid-3, visibility coupling).
    // --
    {
      ctx->ItemClick("canvas#1/Show grid###grid_show");
      ctx->Yield(3);
      IM_CHECK(!canvas.grid_visible("canvas#1"));
      const std::size_t depth = journal.cursor();
      drag(at(35.0, 65.0), at(141.0, 65.0));
      IM_CHECK(journal.cursor() == depth + 1);
      const arbc::Affine free = *cell_placement(state, ids.cell_a);
      IM_CHECK(free.tx == Catch::Approx(k_a_at + 106.0).margin(2.0)); // free at ~126, no grid snap
      IM_CHECK(!(free.tx == Catch::Approx(k_spacing).margin(0.5)));
      undo_one();
    }

    // --- (v) the GROUP path: with both cells selected and the grid shown, a body drag snaps the
    // union box to the grid too, one journal entry (the :807-808 call site). ----------------------
    {
      canvas.set_grid_visible("canvas#1", true); // grid back on via the programmatic setter
      ctx->Yield(3);
      IM_CHECK(canvas.grid_visible("canvas#1"));
      click(at(35.0, 65.0)); // re-select A alone
      IM_CHECK(sel.primary() == ids.cell_a);
      ctx->KeyDown(ImGuiMod_Shift);
      click(at(k_a_at + 15.0, k_b_at_y + 30.0)); // shift-add cell B
      ctx->KeyUp(ImGuiMod_Shift);
      IM_CHECK(sel.size() == 2);

      const std::size_t depth = journal.cursor();
      const arbc::Affine a_before = *cell_placement(state, ids.cell_a);
      const arbc::Affine b_before = *cell_placement(state, ids.cell_b);
      // Grab the union body (over cell A) and move +106: the union's left edge (20+106=126) snaps
      // flush onto x=128, carrying BOTH members by the same delta.
      drag(at(35.0, 65.0), at(141.0, 65.0));
      IM_CHECK(journal.cursor() == depth + 1); // ONE journal entry for the group
      IM_CHECK(!(*cell_placement(state, ids.cell_a) == a_before));
      IM_CHECK(!(*cell_placement(state, ids.cell_b) == b_before));
      IM_CHECK(cell_placement(state, ids.cell_a)->tx == Catch::Approx(k_spacing).margin(0.5));
      IM_CHECK(cell_placement(state, ids.cell_b)->tx == Catch::Approx(k_spacing).margin(0.5));
      undo_one();
      IM_CHECK(*cell_placement(state, ids.cell_a) == a_before);
      IM_CHECK(*cell_placement(state, ids.cell_b) == b_before);
    }
  };
  ImGuiTestEngine_QueueTest(engine, test);

  std::vector<unsigned char> frame_off;
  std::vector<unsigned char> frame_on;
  int cap_w = 0;
  int cap_h = 0;
  auto grab_frame = [&]() {
    const ImGuiIO& io = ImGui::GetIO();
    cap_w = static_cast<int>(io.DisplaySize.x);
    cap_h = static_cast<int>(io.DisplaySize.y);
    if (cap_w <= 0 || cap_h <= 0) {
      return;
    }
    std::vector<unsigned int> px(static_cast<std::size_t>(cap_w) * cap_h);
    if (!capture_pixels(0, 0, 0, cap_w, cap_h, px.data(), nullptr)) {
      return;
    }
    const auto* bytes = reinterpret_cast<const unsigned char*>(px.data());
    if (e2e.capture_off.load() && frame_off.empty()) {
      frame_off.assign(bytes, bytes + px.size() * 4);
      e2e.off_done.store(true);
    }
    if (e2e.capture_on.load() && frame_on.empty()) {
      frame_on.assign(bytes, bytes + px.size() * 4);
      e2e.on_done.store(true);
    }
  };

  const int k_max_frames = 200000;
  int frames = 0;
  while (!ImGuiTestEngine_IsTestQueueEmpty(engine) && frames < k_max_frames) {
    shell.new_frame();
    shell.draw_ui();
    shell.render(grab_frame);
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

  // The screenshot baseline (§9 exception — app-layer overlay chrome, not a libarbc composition, so
  // no render_offline golden): the grid RENDERS. Toggling the grid re-strokes the pane's draw list
  // over the SAME composition texture (a grid toggle is pure chrome, no re-render), so the grid-on
  // frame must differ from the grid-off frame in the pane BELOW the top-left overlay controls.
  // A byte-exact golden is avoided deliberately — software-GL raster pixels are flaky (the
  // look_through / multi_canvas precedent); a robust per-frame DIFFERENCE is the right signal.
  CHECK(!frame_off.empty());
  CHECK(!frame_on.empty());
  CHECK(frame_off.size() == frame_on.size());
  if (!frame_off.empty() && frame_off.size() == frame_on.size() && cap_w > 0 && cap_h > 0) {
    std::size_t differing = 0;
    // Sample the lower half of the frame (away from the top-left grid/camera overlay, whose
    // checkbox tick would otherwise register as a spurious difference); the grid's hairlines cross
    // this band and shift its pixels.
    for (int row = cap_h / 2; row < cap_h; ++row) {
      for (int col = 0; col < cap_w; ++col) {
        const std::size_t idx = (static_cast<std::size_t>(row) * cap_w + col) * 4;
        if (idx + 2 >= frame_off.size()) {
          continue;
        }
        if (frame_off[idx] != frame_on[idx] || frame_off[idx + 1] != frame_on[idx + 1] ||
            frame_off[idx + 2] != frame_on[idx + 2]) {
          ++differing;
        }
      }
    }
    CHECK(differing > 50); // the grid drew many hairline pixels the empty pane did not
  }

  canvas.destroy();
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
