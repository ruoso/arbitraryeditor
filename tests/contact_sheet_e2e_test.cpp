// editor.cameras.contact_sheet — the Export panel's contact-sheet UI e2e (docs §9, the
// offscreen software-GL lane), built on tests/export_e2e_test.cpp's harness: the REAL
// `app::AppProjectGateway` over a REAL `commands::AppState` on a ScratchDir project, a
// live `CanvasView`, and the shipped Export body over a real `commands::ExportService`
// wired exactly as `run_editor` wires it.
//
// Asserts, by stable widget id, on MODEL state and ON-DISK FILES — never on pixels
// (software-GL pixels are flaky, per the save_ui/gc_ui precedent). The shipped
// `export_panel` test is left untouched: it is the cheapest possible proof that the
// defaults reproduce the batch-only behaviour exactly.
//
// The sheet's on-disk bytes are checked WITHOUT introducing a PNG decoder (D-export-11):
// the signature and the IHDR geometry come out of the container's own header, and the
// filled-background phase recomposes the sheet in-test through the same public
// `compose_contact_sheet` + `encode_png` and compares the resulting BYTES.
#include <ace/app/camera_inspector.hpp>
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/commands/contact_sheet.hpp>
#include <ace/commands/export.hpp>
#include <ace/dock/dock.hpp>
#include <ace/dockmodel/recent_projects.hpp>
#include <ace/dockmodel/view_registry.hpp>
#include <ace/interact/interact.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/process_launcher.hpp>
#include <ace/platform/threads.hpp>
#include <ace/project/project.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>
#include <ace/scene/cell.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/expected.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "writer_session.hpp"

using ace::app::CameraInspector;
using ace::app::CanvasView;
using ace::app::Shell;
using ace::app::ShellOptions;
using ace::commands::AppState;
using ace::commands::ExportService;
using ace::commands::ExportState;
using ace::dockmodel::ViewType;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  explicit ScratchDir(const char* leaf)
      : root(std::filesystem::temp_directory_path() /
             ("ace_contact_sheet_e2e_test_" + std::string(leaf))) {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

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

// PNG geometry straight out of the IHDR chunk, which always immediately follows the
// 8-byte signature. No decoder is introduced (D-export-11).
struct PngHeader {
  bool signature = false;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  int bit_depth = 0;
  int color_type = 0;
};

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>());
}

PngHeader read_png_header(const std::filesystem::path& path) {
  static constexpr std::array<std::uint8_t, 8> k_sig{0x89, 0x50, 0x4E, 0x47,
                                                     0x0D, 0x0A, 0x1A, 0x0A};
  PngHeader header;
  const std::vector<std::uint8_t> bytes = read_bytes(path);
  if (bytes.size() < 33 || !std::equal(k_sig.begin(), k_sig.end(), bytes.begin())) {
    return header;
  }
  header.signature = true;
  const auto be32 = [&bytes](std::size_t at) {
    return (static_cast<std::uint32_t>(bytes[at]) << 24) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[at + 3]);
  };
  header.width = be32(16);
  header.height = be32(20);
  header.bit_depth = bytes[24];
  header.color_type = bytes[25];
  return header;
}

std::size_t count_pngs(const std::filesystem::path& dir) {
  std::size_t n = 0;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.path().extension() == ".png") {
      ++n;
    }
  }
  return n;
}

// The document facts the panel must NEVER move (D15 / Constraint 13).
struct DocFacts {
  std::size_t camera_count = 0;
  std::vector<ace::scene::Resolution> resolutions;
  std::vector<arbc::Affine> frames;
  std::vector<arbc::ObjectId> selection;
  std::size_t journal_depth = 0;
  std::size_t journal_cursor = 0;

  friend bool operator==(const DocFacts&, const DocFacts&) = default;
};

DocFacts facts_of(AppState& state) {
  DocFacts facts;
  for (const ace::scene::Camera& camera : ace::scene::cameras(state.document())) {
    facts.resolutions.push_back(camera.resolution);
    facts.frames.push_back(camera.frame);
  }
  facts.camera_count = facts.resolutions.size();
  facts.selection = state.selection().items();
  facts.journal_depth = state.document().journal().depth();
  facts.journal_cursor = state.document().journal().cursor();
  return facts;
}

struct E2EState {
  AppState* state;
  CanvasView* canvas;
  ExportService* service;
  const ace::commands::RenderFn* render;
  std::filesystem::path exports_dir;
};

template <class Ready> bool pump_until(ImGuiTestContext* ctx, Ready ready) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (std::chrono::steady_clock::now() < deadline) {
    if (ready()) {
      return true;
    }
    ctx->Yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return ready();
}

// Pump until the canvas stops publishing new frames — the settled state the mint reads
// its framing from (the new_shot_from_view_e2e recipe).
void settle(ImGuiTestContext* ctx, CanvasView& canvas) {
  std::uint64_t last = canvas.frames_issued("canvas#1");
  for (int quiet = 0; quiet < 40;) {
    ctx->Yield();
    const std::uint64_t now = canvas.frames_issued("canvas#1");
    quiet = (now == last) ? quiet + 1 : 0;
    last = now;
  }
}

bool terminal(ExportState state) {
  return state == ExportState::Finished || state == ExportState::Cancelled ||
         state == ExportState::Failed;
}

} // namespace

TEST_CASE("contact_sheet e2e: the Export panel writes one tiled sheet, opt-in, without touching "
          "the document") {
  ScratchDir scratch("main");
  ace::platform::NativeFileSystem fs;
  ace::platform::NativeThreads threads;
  ace::testing::WriterSession session(scratch.root / "proj");
  REQUIRE(session.ok());
  AppState& state = session.state();
  session.on_writer([&] { state.document().add_composition(128.0, 128.0); });

  // An OPAQUE backdrop plus a bounded cell, so the canvas has content to publish (its
  // gate withholds an all-transparent frame) and every tile renders real pixels.
  arbc::expected<arbc::ObjectId, std::string> backdrop = arbc::unexpected<std::string>("unset");
  arbc::expected<arbc::ObjectId, std::string> cell = arbc::unexpected<std::string>("unset");
  session.on_writer([&] {
    backdrop = ace::scene::add_cell(state.document(), state.registry(), "org.arbc.solid",
                                    "0.15,0.2,0.25,1", arbc::Affine::identity());
    cell = ace::scene::add_cell(state.document(), state.registry(), "org.arbc.raster", "32x32",
                                arbc::Affine::translation(24.0, 24.0));
  });
  REQUIRE(backdrop.has_value());
  REQUIRE(cell.has_value());

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

  // The shipped renderer callable, held by name so the test body can recompose a sheet
  // with the IDENTICAL renderer the service used (phase 6).
  const ace::commands::RenderFn render_fn =
      [&state](const arbc::Affine& camera, int width, int height,
               const std::optional<ace::commands::Rgba8>& background) {
        if (!background) {
          return ace::render::render_document_srgb8(state.document(), width, height, camera);
        }
        return ace::render::render_document_srgb8_over(
            state.document(), width, height, camera,
            {background->r, background->g, background->b, background->a});
      };

  ExportService service(threads, fs);
  service.set_shot_camera(&ace::interact::viewport_camera_for_shot);
  service.set_revision([&state] { return state.document().pin()->revision(); });
  service.set_renderer(render_fn);
  ace::views::register_view_body(ViewType::Export, [&service, &state](std::string_view view_id) {
    ace::views::draw_export(service, state, view_id);
  });

  ace::dock::Dockspace dockspace;
  ace::dockmodel::RecentProjects recent(scratch.root / "prefs", fs);
  NoopFolderDialog gateway_dialog;
  NoopLauncher launcher;
  ace::app::AppProjectGateway gateway(recent, fs, gateway_dialog, launcher,
                                      "/usr/bin/arbitraryeditor", state);
  gateway.set_edit_runner(
      [&canvas](const std::function<void()>& edit) { canvas.apply_edit(edit); });
  gateway.set_view_framing([&canvas] { return canvas.primary_framing(); });
  dockspace.set_project_gateway(&gateway);

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

  E2EState e2e{&state, &canvas, &service, &render_fn, state.layout().exports_dir};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "cameras", "contact_sheet_panel");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    AppState& state = *e2e->state;
    CanvasView& canvas = *e2e->canvas;
    ExportService& service = *e2e->service;
    const std::string rail = ace::dock::tool_rail_title();
    const std::string shot_item = rail + "/###new_shot_from_view";
    const auto cameras = [&state] { return ace::scene::cameras(state.document()); };
    const auto run_button = std::string("export/###export_run");
    const auto cancel_button = std::string("export/###export_cancel");
    const std::filesystem::path sheet_path = e2e->exports_dir / "contact-sheet.png";
    const std::filesystem::path sheet_2_path = e2e->exports_dir / "contact-sheet-2.png";
    const auto disabled = [ctx](const std::string& ref) {
      return (ctx->ItemInfo(ref.c_str()).ItemFlags & ImGuiItemFlags_Disabled) != 0;
    };
    const auto ticked = [ctx](const std::string& ref) {
      return (ctx->ItemInfo(ref.c_str()).StatusFlags & ImGuiItemStatusFlags_Checked) != 0;
    };
    const auto run_and_wait = [&]() -> std::shared_ptr<const ace::commands::ExportReport> {
      ctx->ItemClick(run_button.c_str());
      pump_until(ctx, [&] { return terminal(service.progress()->state); });
      // `running_` is the LAST write of the job, so waiting only on the progress state
      // can still read the PREVIOUS run's report.
      pump_until(ctx, [&] { return !service.running(); });
      return service.report();
    };
    // The sheet plan the panel's own inputs derive, recomputed in-test from the SAME
    // public seams — which is what makes the IHDR assertions exact rather than "some
    // plausible size".
    const auto sheet_plan = [&](int tile_edge, bool write_items) {
      ace::commands::ExportOptions probe;
      probe.destination = e2e->exports_dir;
      probe.contact_sheet = true;
      probe.write_items = write_items;
      probe.tile_edge = tile_edge;
      std::vector<arbc::ObjectId> ids;
      for (const ace::scene::Camera& camera : cameras()) {
        ids.push_back(camera.id);
      }
      return ace::commands::plan_export(state.document(), ids, probe,
                                        &ace::interact::viewport_camera_for_shot)
          .contact_sheet.value();
    };

    ctx->Yield(2);
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));

    // --- (1) The panel offers BOTH outputs, with the sheet genuinely opt-in ------------
    ctx->ItemClick((rail + "/Export").c_str());
    ctx->Yield(3);
    IM_CHECK(ctx->WindowInfo("export").ID != 0);
    IM_CHECK(ctx->ItemExists("export/###export_items"));
    IM_CHECK(ctx->ItemExists("export/###export_contact_sheet"));
    IM_CHECK(ctx->ItemExists("export/###export_tile"));
    IM_CHECK(ticked("export/###export_items"));          // the shipped path is the default
    IM_CHECK(!ticked("export/###export_contact_sheet")); // the new mode is opt-in
    IM_CHECK(disabled(run_button));                      // nothing ticked yet

    // Three cameras with distinct resolutions, so the grid has three different aspects.
    for (int i = 0; i < 3; ++i) {
      settle(ctx, canvas);
      ctx->ItemClick(shot_item.c_str());
      IM_CHECK(
          pump_until(ctx, [&, i] { return cameras().size() == static_cast<std::size_t>(i + 1); }));
      if (i < 2) {
        ctx->MouseMove("canvas#1/##canvas_nav");
        ctx->MouseWheelY(3.0F);
      }
    }
    ctx->Yield(2);
    canvas.apply_edit([&state] {
      const std::vector<ace::scene::Camera> list = ace::scene::cameras(state.document());
      const ace::scene::Resolution sizes[3] = {{48, 32}, {32, 32}, {24, 48}};
      for (std::size_t i = 0; i < list.size() && i < 3; ++i) {
        ace::scene::set_camera_resolution(state.document(), state.registry(), list[i].id, sizes[i]);
      }
    });
    IM_CHECK(pump_until(ctx, [&] { return cameras()[2].resolution.height == 48; }));

    // --- (2) The defaults still write per-camera files and NO sheet --------------------
    DocFacts before = facts_of(state);
    for (int i = 0; i < 3; ++i) {
      ctx->ItemClick(("export/###export_cam_" + std::to_string(i)).c_str());
    }
    ctx->Yield(2);
    IM_CHECK(!disabled(run_button));
    const std::shared_ptr<const ace::commands::ExportReport> batch = run_and_wait();
    IM_CHECK(batch != nullptr);
    IM_CHECK(batch->items.size() == 3);
    IM_CHECK(batch->written == 3);
    IM_CHECK(!batch->contact_sheet.has_value());
    IM_CHECK(count_pngs(e2e->exports_dir) == 3);
    IM_CHECK(!std::filesystem::exists(sheet_path));
    IM_CHECK(facts_of(state) == before);

    // --- (3) Sheet only: exactly one NEW file, at the planned dimensions ---------------
    const std::filesystem::path camera_1 = e2e->exports_dir / "Camera 1.png";
    IM_CHECK(std::filesystem::exists(camera_1));
    const std::vector<std::uint8_t> camera_1_before = read_bytes(camera_1);
    before = facts_of(state);
    ctx->ItemClick("export/###export_contact_sheet");
    ctx->ItemClick("export/###export_items");
    ctx->Yield(2);
    IM_CHECK(ticked("export/###export_contact_sheet"));
    IM_CHECK(!ticked("export/###export_items"));
    const ace::commands::ContactSheetPlan planned_256 =
        sheet_plan(ace::commands::k_contact_tile_default, false);
    IM_CHECK(planned_256.tiles.size() == 3);
    const std::shared_ptr<const ace::commands::ExportReport> sheet_only = run_and_wait();
    IM_CHECK(sheet_only != nullptr);
    IM_CHECK(sheet_only->items.empty()); // no per-camera renders at all
    IM_CHECK(sheet_only->written == 1);
    IM_CHECK(sheet_only->contact_sheet.has_value());
    IM_CHECK(sheet_only->contact_sheet->written);
    IM_CHECK(sheet_only->contact_sheet->path == sheet_path);
    IM_CHECK(count_pngs(e2e->exports_dir) == 4); // the three camera files plus the sheet
    PngHeader header = read_png_header(sheet_path);
    IM_CHECK(header.signature);
    IM_CHECK(header.bit_depth == 8);
    IM_CHECK(header.color_type == 6);
    IM_CHECK(header.width == static_cast<std::uint32_t>(planned_256.width));
    IM_CHECK(header.height == static_cast<std::uint32_t>(planned_256.height));
    IM_CHECK(read_bytes(camera_1) == camera_1_before); // no per-camera file touched
    IM_CHECK(facts_of(state) == before);

    // --- (4) The tile knob is LIVE, and the sheet overwrites in place ------------------
    before = facts_of(state);
    ctx->ItemInputValue("export/###export_tile", 128);
    ctx->Yield(2);
    const ace::commands::ContactSheetPlan planned_128 = sheet_plan(128, false);
    IM_CHECK(planned_128.width != planned_256.width); // anti-vacuity: the knob moves it
    const std::shared_ptr<const ace::commands::ExportReport> retiled = run_and_wait();
    IM_CHECK(retiled != nullptr);
    IM_CHECK(retiled->written == 1);
    header = read_png_header(sheet_path);
    IM_CHECK(header.width == static_cast<std::uint32_t>(planned_128.width));
    IM_CHECK(header.height == static_cast<std::uint32_t>(planned_128.height));
    IM_CHECK(count_pngs(e2e->exports_dir) == 4);      // overwritten IN PLACE
    IM_CHECK(!std::filesystem::exists(sheet_2_path)); // no `-2` on disk (D-export-6)
    IM_CHECK(facts_of(state) == before);

    // --- (5) Both outputs from ONE dispatch -------------------------------------------
    before = facts_of(state);
    ctx->ItemClick("export/###export_items");
    ctx->Yield(2);
    IM_CHECK(ticked("export/###export_items"));
    const std::shared_ptr<const ace::commands::ExportReport> both = run_and_wait();
    IM_CHECK(both != nullptr);
    IM_CHECK(both->items.size() == 3);
    IM_CHECK(both->contact_sheet.has_value());
    IM_CHECK(both->written == 4); // three cameras AND the sheet
    IM_CHECK(both->state == ExportState::Finished);
    IM_CHECK(count_pngs(e2e->exports_dir) == 4);
    ctx->Yield(2);
    IM_CHECK(ctx->ItemExists("export/###export_status"));
    IM_CHECK(terminal(service.progress()->state));
    IM_CHECK(service.progress()->done == service.progress()->total);
    IM_CHECK(service.progress()->total == 6); // 3 items + 3 tiles, both phases counted
    IM_CHECK(facts_of(state) == before);

    // --- (6) A filled background yields a fully opaque sheet ---------------------------
    // Asserted by RECOMPOSING the sheet in-test through the same public
    // `compose_contact_sheet` + `encode_png` and comparing the bytes: no PNG decoder is
    // introduced, and the comparison is stronger than a size check.
    before = facts_of(state);
    ctx->ItemClick("export/###export_items"); // sheet only again, so the run is cheap
    ctx->ItemClick("export/###export_bg_filled");
    ctx->Yield(2);
    const std::shared_ptr<const ace::commands::ExportReport> filled = run_and_wait();
    IM_CHECK(filled != nullptr);
    IM_CHECK(filled->contact_sheet.has_value());
    IM_CHECK(filled->contact_sheet->written);
    IM_CHECK(filled->contact_sheet->opaque); // every pixel of the sheet fully opaque
    const ace::commands::ContactSheetPlan replay = sheet_plan(128, false);
    const std::optional<ace::commands::Rgba8> opaque_black = ace::commands::Rgba8{0, 0, 0, 255};
    std::vector<ace::commands::Srgb8Image> renders;
    for (const ace::commands::ContactTile& tile : replay.tiles) {
      renders.push_back((*e2e->render)(tile.render_camera, tile.width, tile.height, opaque_black));
    }
    const ace::commands::Srgb8Image recomposed =
        ace::commands::compose_contact_sheet(replay, renders, opaque_black);
    IM_CHECK(recomposed.width == replay.width);
    const std::vector<std::uint8_t> encoded = ace::commands::encode_png(recomposed);
    IM_CHECK(!encoded.empty());
    IM_CHECK(encoded == read_bytes(sheet_path));
    ctx->ItemClick("export/###export_bg_filled"); // back to the transparent default
    ctx->Yield(2);
    IM_CHECK(facts_of(state) == before);

    // --- (7) A camera named `contact-sheet` KEEPS the name; the sheet moves ------------
    // The rename rides the shipped L1 verb through the writer identity — the Inspector
    // ships no rename field yet (editor.panels.inspector), so there is no widget for it.
    const arbc::ObjectId first_id = cameras()[0].id;
    canvas.apply_edit([&state, first_id] {
      ace::scene::rename_camera(state.document(), state.registry(), first_id, "contact-sheet");
    });
    IM_CHECK(pump_until(ctx, [&] { return cameras()[0].name == "contact-sheet"; }));
    ctx->ItemClick("export/###export_items"); // both outputs
    ctx->Yield(2);
    before = facts_of(state);
    const std::shared_ptr<const ace::commands::ExportReport> collided = run_and_wait();
    IM_CHECK(collided != nullptr);
    IM_CHECK(collided->items.size() == 3);
    IM_CHECK(collided->contact_sheet.has_value());
    IM_CHECK(collided->items[0].path == sheet_path);         // the CAMERA keeps it
    IM_CHECK(collided->contact_sheet->path == sheet_2_path); // the sheet moved
    IM_CHECK(std::filesystem::exists(sheet_path));
    IM_CHECK(std::filesystem::exists(sheet_2_path));
    IM_CHECK(facts_of(state) == before);

    // --- (8) Both outputs off DISABLES Export -----------------------------------------
    ctx->ItemClick("export/###export_items");
    ctx->ItemClick("export/###export_contact_sheet");
    ctx->Yield(2);
    IM_CHECK(!ticked("export/###export_items"));
    IM_CHECK(!ticked("export/###export_contact_sheet"));
    IM_CHECK(disabled(run_button)); // refuse rather than guess
    ctx->ItemClick("export/###export_contact_sheet");
    ctx->Yield(2);
    IM_CHECK(!disabled(run_button)); // one output is enough

    // --- (9) Cancel DURING the sheet phase leaves the file on disk untouched -----------
    // Enough tiles at the maximum tile edge that the cancel lands BETWEEN tile renders.
    canvas.apply_edit([&state] {
      for (int i = 0; i < 5; ++i) {
        ace::scene::add_camera(state.document(), state.registry(), "Bulk " + std::to_string(i),
                               ace::scene::Resolution{64, 48}, arbc::Affine::identity());
      }
    });
    IM_CHECK(pump_until(ctx, [&] { return cameras().size() == 8; }));
    ctx->Yield(2);
    for (int i = 3; i < 8; ++i) {
      ctx->ItemClick(("export/###export_cam_" + std::to_string(i)).c_str());
    }
    ctx->ItemInputValue("export/###export_tile", ace::commands::k_contact_tile_max);
    ctx->Yield(2);
    const std::vector<std::uint8_t> sheet_2_before = read_bytes(sheet_2_path);
    IM_CHECK(!sheet_2_before.empty());
    before = facts_of(state);
    IM_CHECK(disabled(cancel_button)); // disabled when idle
    ctx->ItemClick(run_button.c_str());
    ctx->Yield();
    IM_CHECK(!disabled(cancel_button)); // enabled while running
    ctx->ItemClick(cancel_button.c_str());
    IM_CHECK(pump_until(ctx, [&] { return terminal(service.progress()->state); }));
    IM_CHECK(service.progress()->state == ExportState::Cancelled);
    IM_CHECK(ctx->ItemExists("export/###export_status"));
    const std::shared_ptr<const ace::commands::ExportReport> cancelled = service.report();
    IM_CHECK(cancelled->state == ExportState::Cancelled);
    IM_CHECK(!cancelled->contact_sheet.has_value()); // no partial sheet is ever claimed
    // The sheet the previous phase wrote is byte-for-byte where it was: a cancelled
    // sheet is abandoned BEFORE encode, so nothing reached disk (D-sheet-7).
    IM_CHECK(read_bytes(sheet_2_path) == sheet_2_before);
    IM_CHECK(pump_until(ctx, [&] { return !service.running(); }));
    ctx->Yield(2);
    IM_CHECK(disabled(cancel_button)); // disabled again once idle
    IM_CHECK(facts_of(state) == before);

    // --- (10) Ctrl+Z undoes the RENAME, not an export ---------------------------------
    // Export is not a transaction (D15): it added no journal entry, so the next undo is
    // still the last real edit.
    const std::size_t cameras_before_undo = cameras().size();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    IM_CHECK(pump_until(ctx, [&] { return cameras().size() != cameras_before_undo; }));
    IM_CHECK(cameras()[0].name == "contact-sheet"); // the rename is still further back
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
  // The seam is process-global: clear both bodies before the state they capture dies.
  ace::views::register_view_body(ViewType::Canvas, {});
  ace::views::register_view_body(ViewType::Export, {});

  CHECK(frames < k_max_frames);
  CHECK(count_tested == 1);
  CHECK(count_success == 1);

  // Constraint 8: the export job renders the one owned Document, so it is joined BEFORE
  // the canvas is torn down and long before `WriterSession` releases the document.
  service.cancel();
  service.join();
  canvas.destroy();
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
