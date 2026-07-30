// editor.panels.color — the picker / eyedropper / boundary UI e2e (docs §9, the offscreen
// software-GL lane; rig cloned from tests/brush_e2e_test.cpp + tests/inspector_e2e_test.cpp). Boots
// the shell over a REAL commands::AppState, seeds an opaque backdrop + a bounded org.arbc.raster
// cell, registers the shipped ColorPanel body on a standalone "Color" window and threads the rail's
// active tool into the Canvas body, then drives the picker by ref path and the canvas by raw mouse.
//
// Asserts (the D10 boundary, end to end):
//   * boot continuity (Constraint 3) — with NO picker interaction a Brush stroke paints opaque
//   black
//     (unchanged from before the color panel);
//   * picker → state — setting the 0–255 RGB inputs drives AppState::active_color() to the entered
//     sRGB value;
//   * picker → brush (the boundary proof) — after picking a non-black color, painting deposits dabs
//     whose WorkingPixel equals srgb_to_working(active_color()), proving the sRGB→premul-linear
//     wire;
//   * eyedropper → state — clicking a known composited color under the Eyedropper sets
//   active_color()
//     to its sRGB (round-tripped from the composited premul-linear), and a subsequent stroke paints
//     with it.
//   * eyedropper modifier → own color (editor.panels.color_eyedrop_cell) — Alt-clicking a selected
//     BACK cell under an opaque FRONT sibling sets active_color() to the back cell's OWN sRGB
//     (isolated single-cell render), while a plain click at the same point yields the FRONT
//     composite — the modifier and the default visibly diverge;
//   * eyedropper modifier fallback — Alt-clicking with NO cell selected degrades to the composited
//     sample (never empty/black), proving the std::nullopt graceful fallback.
#include <ace/app/canvas_view.hpp>
#include <ace/app/color_panel.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/commands/color.hpp>
#include <ace/commands/selection.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/dockmodel/tool_rail.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/project/project.hpp>
#include <ace/scene/cell.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/kind_raster/raster_content.hpp>
#include <arbc/media/pixel_traits.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "writer_session.hpp"

using ace::app::CanvasView;
using ace::app::ColorPanel;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::commands::AppState;
using ace::commands::SrgbColor;
using ace::dockmodel::ToolId;
using ace::dockmodel::ViewType;

namespace {

// Geometry in COMPOSITION units, 1:1 with pane-relative device px (canvas#1's viewport starts at
// identity — the brush_e2e contract). A 60x60 raster at (40,40) covers [40,100]^2; the opaque
// unbounded backdrop covers the whole 256x256 composition, so a click well outside the raster
// samples the KNOWN backdrop colour.
constexpr double k_raster_at = 40.0;
// The backdrop's straight-alpha premultiplied-linear colour, seeded opaque so its composite over
// transparent black is itself — the eyedropper's known target.
constexpr arbc::WorkingPixel k_backdrop{0.15F, 0.2F, 0.25F, 1.0F};
// The active-cell modifier's occlusion pair (editor.panels.color_eyedrop_cell): a BACK solid over
// comp [120,170]^2 partly covered by a FRONT solid over [140,190]^2 added after it (so the front is
// higher in z). Both OPAQUE, so config value == premultiplied == straight colour, and the composite
// inside the overlap is the front alone. A back-only point (125,125) selects the back cell; the
// overlap point (155,155) is where the modifier (back's own colour) and the default (front
// composite) diverge.
constexpr arbc::WorkingPixel k_back{0.5F, 0.25F, 0.1F, 1.0F};
constexpr arbc::WorkingPixel k_front{0.2F, 0.4F, 0.8F, 1.0F};

struct ScratchDir {
  std::filesystem::path root;
  explicit ScratchDir(const char* tag)
      : root(std::filesystem::temp_directory_path() / (std::string("ace_color_e2e_") + tag)) {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

arbc::ObjectId seed(AppState& state, arbc::ObjectId& back, arbc::ObjectId& front,
                    arbc::ObjectId& nested) {
  arbc::Document& doc = state.document();
  doc.add_composition(256.0, 256.0);
  // Opaque unbounded backdrop first (so the pane issues a non-transparent first frame AND the
  // eyedropper has a known composited colour to sample).
  ace::scene::add_cell(doc, state.registry(), "org.arbc.solid", "0.15,0.2,0.25,1",
                       arbc::Affine::identity());
  const arbc::expected<arbc::ObjectId, std::string> r =
      ace::scene::add_cell(doc, state.registry(), "org.arbc.raster", "60x60",
                           arbc::Affine::translation(k_raster_at, k_raster_at));
  // The occlusion pair: bounded solids (config carries the "x,y,w,h" extent), identity placement,
  // BACK added before FRONT so the front is on top. Both leaf cells (isolable by
  // sample_cell_color).
  const arbc::expected<arbc::ObjectId, std::string> b =
      ace::scene::add_cell(doc, state.registry(), "org.arbc.solid", "0.5,0.25,0.1,1,120,120,50,50",
                           arbc::Affine::identity());
  const arbc::expected<arbc::ObjectId, std::string> f =
      ace::scene::add_cell(doc, state.registry(), "org.arbc.solid", "0.2,0.4,0.8,1,140,140,50,50",
                           arbc::Affine::identity());
  back = b.has_value() ? *b : arbc::ObjectId{};
  front = f.has_value() ? *f : arbc::ObjectId{};
  // A NESTED cell in an empty bottom-left region ([10,50]x[190,230]) — a non-isolable cell (its
  // content answers a valid `composition_ref()`, so `sample_cell_color` gates to std::nullopt). It
  // is pickable (bounds-based, D-selection-11) yet renders transparent (empty child), so the
  // isolated eyedropper degrades to the composited backdrop there — the `break` fall-through
  // (editor.panels.color_eyedrop_cell / canvas_view.cpp). Placed clear of every earlier sample
  // point so phases i–vi are undisturbed.
  const arbc::ObjectId child = doc.add_composition(40.0, 40.0);
  const arbc::expected<arbc::ObjectId, std::string> n =
      ace::scene::add_cell(doc, state.registry(), "org.arbc.nested", std::to_string(child.value),
                           arbc::Affine::translation(10.0, 190.0));
  nested = n.has_value() ? *n : arbc::ObjectId{};
  return r.has_value() ? *r : arbc::ObjectId{};
}

// The raster's live level-0 pixel buffer (premultiplied-linear working floats), the pixels the
// brush deposits — a lock-free pinned read of the resolved content.
std::vector<float> raster_pixels(AppState& state, arbc::ObjectId cell) {
  auto* raster = dynamic_cast<arbc::RasterContent*>(state.document().resolve(cell));
  return raster != nullptr ? raster->store().base_table()->level_pixels(0) : std::vector<float>{};
}

// The deposited colour: the RGBA quad with the greatest alpha — a full-coverage dab centre, whose
// premultiplied-linear value IS the paint colour (source-over onto transparent at coverage 1).
arbc::WorkingPixel max_alpha_pixel(const std::vector<float>& px) {
  arbc::WorkingPixel best{0.0F, 0.0F, 0.0F, 0.0F};
  for (std::size_t i = 0; i + 3 < px.size(); i += 4) {
    if (px[i + 3] > best[3]) {
      best = {px[i], px[i + 1], px[i + 2], px[i + 3]};
    }
  }
  return best;
}

bool near_working(const arbc::WorkingPixel& a, const arbc::WorkingPixel& b, float tol) {
  for (int i = 0; i < 4; ++i) {
    if (std::abs(a[i] - b[i]) > tol) {
      return false;
    }
  }
  return true;
}

bool near_srgb(const SrgbColor& a, const SrgbColor& b, int tol) {
  return std::abs(static_cast<int>(a.r) - static_cast<int>(b.r)) <= tol &&
         std::abs(static_cast<int>(a.g) - static_cast<int>(b.g)) <= tol &&
         std::abs(static_cast<int>(a.b) - static_cast<int>(b.b)) <= tol &&
         std::abs(static_cast<int>(a.a) - static_cast<int>(b.a)) <= tol;
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
  arbc::ObjectId raster;
  arbc::ObjectId back;
  arbc::ObjectId front;
  arbc::ObjectId nested;
  std::atomic<bool> request_undo{false};
  std::atomic<bool> undo_done{false};
};

} // namespace

TEST_CASE("color e2e: boot continuity, picker->state, picker->brush boundary, eyedropper->state") {
  ScratchDir scratch("main");
  ace::platform::NativeFileSystem fs;
  ace::testing::WriterSession session(scratch.root / "color");
  REQUIRE(session.ok());
  AppState& state = session.state();
  arbc::ObjectId raster;
  arbc::ObjectId back;
  arbc::ObjectId front;
  arbc::ObjectId nested;
  session.on_writer([&] { raster = seed(state, back, front, nested); });
  REQUIRE(raster.valid());
  REQUIRE(back.valid());
  REQUIRE(front.valid());
  REQUIRE(nested.valid());

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 900;
  opts.height = 640;
  REQUIRE(shell.init(opts));

  CanvasView canvas(state, session.writer());
  ColorPanel color_panel(state);
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

  // Draw the dock AND a standalone Color window each frame (the same body the shell registers on
  // ViewType::Color), driven by the "Color" window ref.
  shell.set_draw_content([&dockspace, &canvas, &color_panel]() {
    dockspace.draw();
    // Float the Color window well to the RIGHT of the canvas paint region (pane-relative [40,200]),
    // so a canvas click under the Brush/Eyedropper tools lands on the canvas, not this window.
    ImGui::SetNextWindowPos(ImVec2(640.0F, 20.0F), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(240.0F, 420.0F), ImGuiCond_Once);
    ImGui::Begin("Color");
    color_panel.draw("color");
    ImGui::End();
    canvas.reconcile(dockspace.layout().view_ids());
  });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&canvas, &state, &dockspace, raster, back, front, nested};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "color", "boot_picker_brush_eyedropper");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    CanvasView& canvas = *e2e->canvas;
    AppState& state = *e2e->state;
    ace::dock::Dockspace& dockspace = *e2e->dockspace;
    ace::commands::Selection& sel = state.selection();
    const arbc::ObjectId raster = e2e->raster;
    const arbc::ObjectId back = e2e->back;
    const arbc::ObjectId front = e2e->front;
    const arbc::ObjectId nested = e2e->nested;

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
    const auto select_raster = [&]() {
      set_tool(ToolId::Select);
      click_at(at(70.0F, 70.0F));
      IM_CHECK(sel.primary() == raster);
    };
    const auto paint_stroke = [&]() {
      set_tool(ToolId::Brush);
      drag(at(55.0F, 55.0F), at(85.0F, 85.0F));
    };
    // Drive the picker's 0–255 RGB inputs to move the active colour to a KNOWN value, so a later
    // eyedrop is observable as a change (the same ref path phases ii/iv use).
    const auto set_rgb = [&](int r, int g, int b) {
      ctx->WindowFocus("Color");
      ctx->Yield(2);
      ctx->ItemInputValue("Color/###color_picker/##rgb/##X", r);
      ctx->ItemInputValue("Color/###color_picker/##rgb/##Y", g);
      ctx->ItemInputValue("Color/###color_picker/##rgb/##Z", b);
      ctx->Yield(2);
    };
    // An Eyedropper click with Alt HELD (the active-cell modifier, D-eyedrop_cell-4): the arm reads
    // `in.alt` off `io.KeyAlt`, so hold the mod across the whole press/release.
    const auto alt_click_at = [&](ImVec2 pos) {
      ctx->KeyDown(ImGuiMod_Alt);
      click_at(pos);
      ctx->KeyUp(ImGuiMod_Alt);
    };

    ctx->MouseMove("canvas#1/##canvas_nav"); // establish the viewport for raw-position moves

    // --- (i) Boot continuity: NO picker interaction -> a stroke paints OPAQUE BLACK. -------------
    IM_CHECK((state.active_color() == SrgbColor{0, 0, 0, 255}));
    select_raster();
    paint_stroke();
    IM_CHECK(near_working(max_alpha_pixel(raster_pixels(state, raster)),
                          arbc::WorkingPixel{0.0F, 0.0F, 0.0F, 1.0F}, 0.02F));
    undo_one(); // back to a transparent raster for the next phase

    // --- (ii) Picker -> state: the 0–255 RGB inputs drive AppState::active_color(). --------------
    const SrgbColor picked{230, 40, 40, 255};
    ctx->WindowFocus("Color");
    ctx->Yield(2);
    ctx->ItemInputValue("Color/###color_picker/##rgb/##X", static_cast<int>(picked.r));
    ctx->ItemInputValue("Color/###color_picker/##rgb/##Y", static_cast<int>(picked.g));
    ctx->ItemInputValue("Color/###color_picker/##rgb/##Z", static_cast<int>(picked.b));
    ctx->Yield(2);
    IM_CHECK(near_srgb(state.active_color(), picked, 1));

    // --- (iii) Picker -> brush (the boundary proof): the deposited WorkingPixel is ---------------
    //           srgb_to_working(active_color()), i.e. the sRGB->premul-linear wire holds.
    select_raster();
    paint_stroke();
    IM_CHECK(near_working(max_alpha_pixel(raster_pixels(state, raster)),
                          ace::commands::srgb_to_working(state.active_color()), 0.03F));
    undo_one();

    // --- (iv) Eyedropper -> state: sample a KNOWN composited colour, set active, then paint it. --
    // First move the active colour AWAY from the target so the sample is observable as a change.
    ctx->WindowFocus("Color");
    ctx->Yield(2);
    ctx->ItemInputValue("Color/###color_picker/##rgb/##X", 40);
    ctx->ItemInputValue("Color/###color_picker/##rgb/##Y", 200);
    ctx->ItemInputValue("Color/###color_picker/##rgb/##Z", 60);
    ctx->Yield(2);
    // Eyedrop the backdrop well outside the raster ([40,100]^2): the composite there is the known
    // opaque backdrop colour, so active_color() round-trips to its sRGB (through the composited
    // premul-linear), within a couple codes of the f16 working-space store.
    const SrgbColor expected_backdrop = ace::commands::working_to_srgb(k_backdrop);
    set_tool(ToolId::Eyedropper);
    click_at(at(200.0F, 200.0F));
    IM_CHECK(near_srgb(state.active_color(), expected_backdrop, 4));

    // A subsequent brush stroke paints with the eyedropped colour (the wire holds the other way).
    select_raster();
    paint_stroke();
    IM_CHECK(near_working(max_alpha_pixel(raster_pixels(state, raster)),
                          ace::commands::srgb_to_working(state.active_color()), 0.03F));

    // --- (v) Modifier -> own colour (the isolation proof, editor.panels.color_eyedrop_cell).
    // ------ Select the BACK cell (a back-only point, outside the front), move the active colour
    // away, then Alt-Eyedrop the OVERLAP: active_color() is the back cell's OWN sRGB. A plain
    // (no-Alt) Eyedrop at the SAME point yields the FRONT/composite colour — the modifier and the
    // default diverge.
    const SrgbColor back_srgb = ace::commands::working_to_srgb(k_back);
    const SrgbColor front_srgb = ace::commands::working_to_srgb(k_front);
    set_tool(ToolId::Select);
    click_at(at(125.0F, 125.0F)); // inside back [120,170]^2, outside front [140,190]^2
    IM_CHECK(sel.primary() == back);
    set_rgb(10, 250,
            10); // move away from both back and front so the sample is an observable change
    set_tool(ToolId::Eyedropper);
    alt_click_at(at(155.0F, 155.0F)); // inside the overlap: the back cell's OWN colour, isolated
    IM_CHECK(near_srgb(state.active_color(), back_srgb, 4));
    click_at(at(155.0F, 155.0F)); // plain: the composite there is the FRONT (opaque, on top)
    IM_CHECK(near_srgb(state.active_color(), front_srgb, 4));

    // --- (vi) Graceful fallback (Constraint 4 / D-eyedrop_cell-3): Alt-Eyedrop with NO cell
    // -------- selected sets active_color() to the COMPOSITED sample, not empty/black — the
    // std::nullopt fallback. Clear the selection so the modifier has no cell to isolate, then
    // Alt-click the backdrop; the arm degrades to the composited sample it always shipped.
    sel.clear();
    IM_CHECK(!sel.primary().valid());
    set_rgb(40, 200, 60); // away from the backdrop so the fallback sample is observable
    const SrgbColor expected_backdrop_fallback = ace::commands::working_to_srgb(k_backdrop);
    set_tool(ToolId::Eyedropper);
    alt_click_at(at(200.0F, 200.0F)); // backdrop-only region; no cell selected -> composited sample
    IM_CHECK(near_srgb(state.active_color(), expected_backdrop_fallback, 4));

    // --- (vii) Non-isolable selected cell -> the `break` fall-through (editor.panels.
    // -------- color_eyedrop_cell). Select the NESTED cell, then Alt-Eyedrop it: the modifier
    // enters the selected-cell arm, but `sample_cell_color` gates a nested (composition_ref) cell
    // to std::nullopt, so the gesture breaks out and degrades to the composited sample — the empty
    // child renders transparent, so the composite there is the backdrop. Distinct from (vi): here a
    // cell IS selected, exercising the loop's `break`, not the invalid-primary skip.
    set_tool(ToolId::Select);
    click_at(at(30.0F, 210.0F)); // inside the nested cell's bounds ([10,50]x[190,230])
    IM_CHECK(sel.primary() == nested);
    set_rgb(250, 10, 250); // away from the backdrop so the composited fall-through is observable
    set_tool(ToolId::Eyedropper);
    alt_click_at(at(30.0F, 210.0F)); // selected nested -> nullopt -> break -> composited backdrop
    IM_CHECK(near_srgb(state.active_color(), expected_backdrop_fallback, 4));
  };
  ImGuiTestEngine_QueueTest(engine, test);

  const int k_max_frames = 200000;
  int frames = 0;
  while (!ImGuiTestEngine_IsTestQueueEmpty(engine) && frames < k_max_frames) {
    shell.new_frame();
    shell.draw_ui();
    shell.render();
    ImGuiTestEngine_PostSwap(engine);
    // Undo requests run on THIS (main) thread through the same apply_edit seam as the edits.
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
