// editor.cells.resolution — the cell resolution + health inspector UI e2e (docs §9, the
// offscreen software-GL lane; modeled on tests/camera_manip_e2e_test.cpp). Boots the shell over a
// REAL commands::AppState, seeds a composition with a painted raster, a referenced image, a solid
// and a shot `Hero`, and drives the Inspector by selecting each object (the project selection is
// keyed by content ObjectId, so `Selection::select` answers "which cell's resolution do we show").
// Asserts, through the SAME scene/interact seams the inspector reads (the repo's e2e philosophy —
// scene truth, not flaky software-GL text): (i) a selected raster shows its native resolution and
// placed size as TWO DISTINCT values and a health badge whose verdict matches
// interact::resolution_health for the capturing camera; (ii) scaling the raster's placement up so
// the camera out-resolves it flips the badge to Soft while the native resolution is UNCHANGED
// (placement is not resample, D8); (iii) a referenced image reads source-limited with NO resample
// affordance (D-resolution-5, no dead UI); (iv) a solid reports resolution-independent (no health
// verdict); (v) a selected camera surfaces the per-camera health read of the cells it captures
// (the read editor.cameras.manip deferred here). Writer ops run on the MAIN thread through
// apply_edit. No committed screenshot golden: software-GL pixels are flaky by construction and
// ACE_GOLDEN_DIR is not wired into ace_shell_test; the byte-exactness lives in the CPU unit tests
// (tests/resolution_test.cpp, tests/cell_model_test.cpp).
#include <ace/app/camera_inspector.hpp>
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/interact/interact.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/contract/content.hpp>
#include <arbc/kind_raster/raster_content.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "writer_session.hpp"

using ace::app::CameraInspector;
using ace::app::CanvasView;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::commands::AppState;
using ace::dockmodel::ViewType;
using ace::interact::ResolutionVerdict;
using ace::scene::DetailSource;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  explicit ScratchDir(const char* tag)
      : root(std::filesystem::temp_directory_path() / (std::string("ace_resolution_e2e_") + tag)) {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// A finite-detail content whose bytes are BORROWED from an external file — the generic
// ReferencedImage provenance (D11 "bytes where?" axis), exactly the shape `org.arbc.image` has.
// The image plugin is not linked into ace_shell_test, so this stand-in exercises the same generic
// classification. Added straight through `doc.add_content` (the fixture writes a live vtable, not
// a registry factory), so it needs no kind registration.
class ExternalRefProbe final : public arbc::Content {
public:
  std::optional<arbc::Rect> bounds() const override { return arbc::Rect{0.0, 0.0, 64.0, 48.0}; }
  arbc::Stability stability() const override { return arbc::Stability::Static; }
  std::optional<arbc::TimeRange> time_extent() const override { return std::nullopt; }
  std::optional<arbc::RenderResult> render(const arbc::RenderRequest&,
                                           std::shared_ptr<arbc::RenderCompletion>) override {
    return std::nullopt;
  }
  std::string_view external_asset_ref() const override { return d_uri; }

private:
  std::string d_uri = "file:///borrowed/photo.png";
};

// A 200x200 native painted raster placed at scale 0.5 (so its PLACED size, 100x100 composition
// units, differs from its native resolution, 200x200 px — two distinct numbers, D7/D8), a bounded
// solid (resolution-independent), and a referenced image. Hero (100x100 output, frame 1:1) samples
// the raster BELOW native initially (r == 0.5, Crisp); scaling the raster's placement up makes it
// out-resolve it (Soft).
constexpr int k_raster_native = 200;
const arbc::Affine k_raster_placed{0.5, 0.0, 0.0, 0.5, 100.0, 100.0};
const arbc::Affine k_raster_scaled_up{2.0, 0.0, 0.0, 2.0, 100.0, 100.0};

void seed(AppState& state, arbc::ObjectId& hero_out) {
  arbc::Document& doc = state.document();
  const arbc::ObjectId comp = doc.add_composition(512.0, 512.0);

  // An unbounded solid (resolution-independent — no detail floor, and its native resolution AND
  // placed size both read n/a in the inspector, the D-cells_model-3 "covers the whole plane" case).
  const arbc::ObjectId solid =
      doc.add_content(std::make_shared<arbc::SolidContent>(arbc::Rgba{0.0F, 0.3F, 0.0F, 1.0F}));
  doc.attach_layer(comp, doc.add_layer(solid, arbc::Affine::translation(300.0, 300.0)));

  // A painted raster (owns a mutable grid — PaintedRaster, native px = its bounds).
  arbc::DecodedImage img;
  img.width = k_raster_native;
  img.height = k_raster_native;
  img.format = arbc::k_working_rgba32f;
  img.bytes.resize(static_cast<std::size_t>(k_raster_native) * k_raster_native * 4 * sizeof(float));
  auto* fp = reinterpret_cast<float*>(img.bytes.data());
  for (int i = 0; i < k_raster_native * k_raster_native; ++i) {
    fp[i * 4] = 0.6F;
    fp[i * 4 + 3] = 1.0F;
  }
  const arbc::ObjectId raster =
      doc.add_content(std::make_shared<arbc::RasterContent>(std::move(img)));
  doc.attach_layer(comp, doc.add_layer(raster, k_raster_placed));

  // A referenced image (borrows an external file — ReferencedImage, source-limited).
  const arbc::ObjectId image = doc.add_content(std::make_shared<ExternalRefProbe>());
  doc.attach_layer(comp, doc.add_layer(image, arbc::Affine::translation(20.0, 20.0)));

  hero_out = ace::scene::add_camera(doc, state.registry(), "Hero", ace::scene::Resolution{100, 100},
                                    arbc::Affine::translation(100.0, 100.0));
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
  arbc::ObjectId hero_id;
};

// The first cell with the given provenance, off the live pinned snapshot (scene truth).
std::optional<ace::scene::Cell> cell_of_source(AppState& state, DetailSource source) {
  for (const ace::scene::Cell& c : ace::scene::cells(state.document(), state.registry())) {
    if (c.detail.source == source) {
      return c;
    }
  }
  return std::nullopt;
}

// The worst-case (softest) health of `cell` across the cameras that capture it — the exact
// quantity the inspector's badge leads with (draw_cell_readout / draw_capture_health).
ace::interact::ResolutionHealth worst_health(const ace::scene::Cell& cell, AppState& state) {
  ace::interact::ResolutionHealth worst;
  if (!cell.detail.native_pixels.has_value()) {
    return worst;
  }
  for (const ace::scene::Camera& cam : ace::scene::cameras(state.document())) {
    const ace::interact::ResolutionHealth h = ace::interact::resolution_health(
        cell.detail.native_pixels->first, cell.detail.native_pixels->second, cell.placement,
        cam.resolution.width, cam.resolution.height, cam.frame);
    if (h.verdict == ResolutionVerdict::NotApplicable) {
      continue;
    }
    if (worst.verdict == ResolutionVerdict::NotApplicable || h.ratio > worst.ratio) {
      worst = h;
    }
  }
  return worst;
}

} // namespace

TEST_CASE("resolution e2e: cell native/placed size, health badge, provenance, camera capture") {
  ScratchDir scratch("main");
  ace::platform::NativeFileSystem fs;
  ace::testing::WriterSession session(scratch.root / "res");
  REQUIRE(session.ok());
  AppState& state = session.state();
  // Fixture seeding IS a document write: post it to the identity the open just bound.
  arbc::ObjectId hero;
  session.on_writer([&] { seed(state, hero); });
  REQUIRE(hero.valid());

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 900;
  opts.height = 640;
  REQUIRE(shell.init(opts));

  CanvasView canvas(state, session.writer());
  CameraInspector inspector(state, canvas);
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

  // Draw the dock AND a standalone Inspector window each frame (the same body the shell registers
  // on ViewType::Inspector) so selecting an object exercises the real readout path.
  shell.set_draw_content([&dockspace, &canvas, &inspector]() {
    dockspace.draw();
    ImGui::Begin("Inspector");
    inspector.draw("inspector");
    ImGui::End();
    canvas.reconcile(dockspace.layout().view_ids());
  });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&canvas, &state, hero};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "resolution", "cell_health_inspector");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    CanvasView& canvas = *e2e->canvas;
    AppState& state = *e2e->state;
    const arbc::ObjectId hero = e2e->hero_id;

    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));
    ctx->Yield(3);

    // --- (i) Select the raster: native resolution and placed size are TWO DISTINCT values; the
    // health badge matches interact::resolution_health for the capturing camera (Crisp). --------
    const std::optional<ace::scene::Cell> raster =
        cell_of_source(state, DetailSource::PaintedRaster);
    IM_CHECK(raster.has_value());
    state.selection().select(raster->id);
    ctx->Yield(3); // draw the inspector with the raster selected (exercises draw_cell_readout)
    IM_CHECK(raster->detail.native_pixels.has_value());
    IM_CHECK(raster->detail.native_pixels->first == k_raster_native); // native px = 200
    IM_CHECK(raster->content_bounds.has_value());
    const arbc::Rect placed = raster->placement.map_rect(*raster->content_bounds);
    IM_CHECK(placed.width() == 100.0); // placed size 100 comp units != native 200 px (distinct)
    IM_CHECK(placed.width() != static_cast<double>(raster->detail.native_pixels->first));
    const ace::interact::ResolutionHealth h0 = worst_health(*raster, state);
    IM_CHECK(h0.verdict == ResolutionVerdict::Crisp); // the camera samples below native here

    // --- (ii) Scale the raster's placement up so the camera out-resolves it: the badge flips to
    // Soft with the ratio, and the native resolution is UNCHANGED (placement is not resample). ---
    canvas.apply_edit(
        [&] { state.document().set_layer_transform(raster->layer, k_raster_scaled_up); });
    ctx->Yield(3);
    const std::optional<ace::scene::Cell> raster2 =
        cell_of_source(state, DetailSource::PaintedRaster);
    IM_CHECK(raster2.has_value());
    IM_CHECK(raster2->detail.native_pixels.has_value());
    IM_CHECK(raster2->detail.native_pixels->first == k_raster_native); // native UNCHANGED (D8)
    IM_CHECK(raster2->detail.native_pixels->second == k_raster_native);
    const ace::interact::ResolutionHealth h1 = worst_health(*raster2, state);
    IM_CHECK(h1.verdict == ResolutionVerdict::Soft);
    IM_CHECK(h1.ratio > 1.0);

    // --- (iii) Select the referenced image: source-limited, and NO resample affordance anywhere
    // in the inspector (D-resolution-5 — no dead UI). --------------------------------------------
    const std::optional<ace::scene::Cell> image =
        cell_of_source(state, DetailSource::ReferencedImage);
    IM_CHECK(image.has_value());
    state.selection().select(image->id);
    ctx->Yield(3);
    IM_CHECK(image->detail.source == DetailSource::ReferencedImage);
    // The leaf ships no resample verb, so the inspector renders no such control — its absence is
    // the assertion (a soft referenced image is terminal, "source-limited").
    IM_CHECK(!ctx->ItemExists("Inspector/Resample to crisp"));
    IM_CHECK(!ctx->ItemExists("Inspector/Resample"));

    // --- (iv) Select the solid: resolution-independent, no health verdict. ----------------------
    const std::optional<ace::scene::Cell> solid =
        cell_of_source(state, DetailSource::ResolutionIndependent);
    IM_CHECK(solid.has_value());
    state.selection().select(solid->id);
    ctx->Yield(3);
    IM_CHECK(!solid->detail.native_pixels.has_value());
    IM_CHECK(worst_health(*solid, state).verdict == ResolutionVerdict::NotApplicable);

    // --- (v) Select the camera: the per-camera health read of the cells it captures appears (the
    // read editor.cameras.manip deferred here). The raster (soft under Hero) is a captured
    // finite-detail cell, so a verdict exists. -------------------------------------------------
    state.selection().select(hero);
    ctx->Yield(3);
    const std::vector<ace::scene::Camera> cameras = ace::scene::cameras(state.document());
    IM_CHECK(!cameras.empty());
    const ace::interact::ResolutionHealth capture = worst_health(*raster2, state);
    IM_CHECK(capture.verdict == ResolutionVerdict::Soft); // Hero captures the raster, softly
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
