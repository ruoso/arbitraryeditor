// editor.cameras.export — the Export panel's UI e2e (docs §9, the offscreen software-GL
// lane; modelled on tests/frame_selection_e2e_test.cpp + tests/new_shot_from_view_e2e_test.cpp).
// Drives the REAL `app::AppProjectGateway` over a REAL `commands::AppState` on a ScratchDir
// project, with a live `CanvasView` and the shipped Export body over a real
// `commands::ExportService` wired exactly as `run_editor` wires it — the shipped
// `render::render_document_srgb8`, `interact::viewport_camera_for_shot` verbatim, and the
// shipped `FolderDialog` folder seam.
//
// Asserts, by stable widget id, on MODEL state and ON-DISK FILES — never on pixels (software-GL
// pixels are flaky, per the save_ui/gc_ui precedent): the empty state disables Export; ticking
// enables it; one tick writes exactly one PNG named by camera at the camera's own resolution; N x
// multiplies the IHDR geometry and OVERWRITES in place (no `-2` on disk, the .tji decision);
// two cameras sharing a name disambiguate WITHIN the plan (`Camera 1.png` + `Camera 1-2.png`);
// a filled background yields fully opaque output; cancel is observable and leaves complete files;
// the panel mutates NOTHING (camera set, resolutions, frames, selection and journal depth are
// unchanged across every run, and Ctrl+Z undoes the rename rather than an export); and the
// destination override rides the shipped folder seam.
#include <ace/app/camera_inspector.hpp>
#include <ace/app/canvas_view.hpp>
#include <ace/app/folder_dialog.hpp>
#include <ace/app/project_gateway.hpp>
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
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
             ("ace_export_e2e_test_" + std::string(leaf))) {
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

// The shipped folder seam, scripted: `show` delivers synchronously, exactly as
// tests/app_project_gateway_test.cpp's fake does.
class ScriptedFolderDialog final : public ace::app::FolderDialog {
public:
  std::optional<std::filesystem::path> next;
  bool shown = false;
  void show(Callback on_pick) override {
    shown = true;
    on_pick(next);
  }
};

class NoopLauncher final : public ace::platform::ProcessLauncher {
public:
  std::error_code spawn_detached(const std::filesystem::path&,
                                 const std::vector<std::string>&) const override {
    return {};
  }
};

// PNG geometry read straight out of the IHDR chunk, which always immediately follows the
// 8-byte signature. No decoder is introduced (D-export-11): the assertions are on the
// container's own header plus the report the panel produced.
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

// The document facts the panel must NEVER move (Constraint 7 / D15).
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
  ace::dock::Dockspace* dockspace;
  AppState* state;
  CanvasView* canvas;
  ExportService* service;
  ScriptedFolderDialog* dialog;
  std::filesystem::path exports_dir;
  std::filesystem::path elsewhere;
  // A regular FILE, used as the parent of an undestinable destination: `make_directories`
  // fails with ENOTDIR under it and every write below it fails too.
  std::filesystem::path blocked;
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

TEST_CASE("export e2e: the Export panel writes PNGs named by camera, at N x, without touching "
          "the document") {
  ScratchDir scratch("main");
  ace::platform::NativeFileSystem fs;
  ace::platform::NativeThreads threads;
  ace::testing::WriterSession session(scratch.root / "proj");
  REQUIRE(session.ok());
  AppState& state = session.state();
  session.on_writer([&] { state.document().add_composition(128.0, 128.0); });

  // An OPAQUE backdrop plus a bounded cell, so the canvas has content to publish (its
  // gate withholds an all-transparent frame) and every export renders real pixels.
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

  // The export service, wired EXACTLY as run_editor wires it (shell.cpp): the shipped
  // offline renderer, `viewport_camera_for_shot` verbatim, the any-thread revision read,
  // and the shipped FolderDialog seam.
  ScriptedFolderDialog export_dialog;
  ExportService service(threads, fs);
  service.set_shot_camera(&ace::interact::viewport_camera_for_shot);
  service.set_revision([&state] { return state.document().pin()->revision(); });
  service.set_renderer([&state](const arbc::Affine& camera, int width, int height,
                                const std::optional<ace::commands::Rgba8>& background) {
    if (!background) {
      return ace::render::render_document_srgb8(state.document(), width, height, camera);
    }
    return ace::render::render_document_srgb8_over(
        state.document(), width, height, camera,
        {background->r, background->g, background->b, background->a});
  });
  service.set_destination_picker(
      [&export_dialog](const std::function<void(std::optional<std::filesystem::path>)>& on_pick) {
        export_dialog.show(on_pick);
      });
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

  E2EState e2e{&dockspace,
               &state,
               &canvas,
               &service,
               &export_dialog,
               state.layout().exports_dir,
               scratch.root / "elsewhere",
               scratch.root / "blocked"};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "cameras", "export_panel");
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
    const auto disabled = [ctx](const std::string& ref) {
      return (ctx->ItemInfo(ref.c_str()).ItemFlags & ImGuiItemFlags_Disabled) != 0;
    };
    // IM_CHECK expands to an early `return`, so this helper stays assertion-free and the
    // call sites do the checking.
    const auto run_and_wait = [&]() -> std::shared_ptr<const ace::commands::ExportReport> {
      ctx->ItemClick(run_button.c_str());
      pump_until(ctx, [&] { return terminal(service.progress()->state); });
      // The terminal PROGRESS snapshot is published from inside `run_export`, before the
      // job stores the report and clears `running_` — so waiting only on the progress
      // state can still read the PREVIOUS run's report. `running_` is the last write.
      pump_until(ctx, [&] { return !service.running(); });
      return service.report();
    };

    ctx->Yield(2);
    IM_CHECK(pump_until(ctx, [&] { return canvas.frames_issued("canvas#1") >= 1; }));

    // --- (1) The rail's view launcher opens the catalogued Export view -----------------
    // Before this leaf it drew the generic "no body registered" placeholder.
    IM_CHECK(cameras().empty());
    ctx->ItemClick((rail + "/Export").c_str());
    ctx->Yield(3);
    IM_CHECK(ctx->WindowInfo("export").ID != 0);
    IM_CHECK(ctx->ItemExists(run_button.c_str()));
    IM_CHECK(disabled(run_button));    // refuse rather than guess: nothing to export
    IM_CHECK(disabled(cancel_button)); // nothing running

    // --- (2) Two minted cameras appear in the tick-list, both unticked -----------------
    settle(ctx, canvas);
    ctx->ItemClick(shot_item.c_str());
    IM_CHECK(pump_until(ctx, [&] { return cameras().size() == 1; }));
    // A canvas nav between the mints, so the second shot frames a different region.
    ctx->MouseMove("canvas#1/##canvas_nav");
    ctx->MouseWheelY(3.0F);
    settle(ctx, canvas);
    ctx->ItemClick(shot_item.c_str());
    IM_CHECK(pump_until(ctx, [&] { return cameras().size() == 2; }));
    ctx->Yield(2);
    IM_CHECK(ctx->ItemExists("export/###export_cam_0"));
    IM_CHECK(ctx->ItemExists("export/###export_cam_1"));
    IM_CHECK_STR_EQ(cameras()[0].name.c_str(), "Camera 1");
    IM_CHECK_STR_EQ(cameras()[1].name.c_str(), "Camera 2");
    IM_CHECK(disabled(run_button)); // both start UNTICKED

    // Pin the resolutions so the render cost is bounded and the IHDR assertions are exact.
    canvas.apply_edit([&state] {
      for (const ace::scene::Camera& camera : ace::scene::cameras(state.document())) {
        ace::scene::set_camera_resolution(state.document(), state.registry(), camera.id,
                                          ace::scene::Resolution{48, 32});
      }
    });
    IM_CHECK(pump_until(ctx, [&] { return cameras()[1].resolution.width == 48; }));

    // --- (3) One tick, one run, one file at the camera's own resolution ----------------
    DocFacts before = facts_of(state);
    ctx->ItemClick("export/###export_cam_0");
    ctx->Yield(2);
    IM_CHECK(!disabled(run_button));
    // The tick is a TOGGLE over a set of ids: unticking drops it back out, and Export
    // goes back to refusing rather than falling through to "everything".
    ctx->ItemClick("export/###export_cam_0");
    ctx->Yield(2);
    IM_CHECK(disabled(run_button));
    ctx->ItemClick("export/###export_cam_0");
    ctx->Yield(2);
    IM_CHECK(!disabled(run_button));
    const std::shared_ptr<const ace::commands::ExportReport> first = run_and_wait();
    IM_CHECK(first != nullptr);
    IM_CHECK(first->items.size() == 1);
    IM_CHECK(first->written == 1);
    IM_CHECK(first->state == ExportState::Finished);
    const std::filesystem::path camera_1 = e2e->exports_dir / "Camera 1.png";
    IM_CHECK(std::filesystem::exists(camera_1));
    IM_CHECK(count_pngs(e2e->exports_dir) == 1);
    PngHeader header = read_png_header(camera_1);
    IM_CHECK(header.signature);
    IM_CHECK(header.bit_depth == 8);
    IM_CHECK(header.color_type == 6);
    IM_CHECK(header.width == static_cast<std::uint32_t>(cameras()[0].resolution.width));
    IM_CHECK(header.height == static_cast<std::uint32_t>(cameras()[0].resolution.height));
    // (8) The panel touched NOTHING.
    IM_CHECK(facts_of(state) == before);

    // --- (4) Both ticked at 2x: two files, Camera 1 OVERWRITTEN in place ---------------
    before = facts_of(state);
    ctx->ItemClick("export/###export_cam_1");
    ctx->ItemInputValue("export/###export_scale", 2);
    ctx->Yield(2);
    const std::shared_ptr<const ace::commands::ExportReport> second = run_and_wait();
    IM_CHECK(second != nullptr);
    IM_CHECK(second->items.size() == 2);
    IM_CHECK(second->written == 2);
    IM_CHECK(count_pngs(e2e->exports_dir) == 2); // NOT three — no `Camera 1-2.png` on disk
    IM_CHECK(std::filesystem::exists(e2e->exports_dir / "Camera 2.png"));
    IM_CHECK(!std::filesystem::exists(e2e->exports_dir / "Camera 1-2.png"));
    header = read_png_header(camera_1);
    IM_CHECK(header.width == static_cast<std::uint32_t>(2 * cameras()[0].resolution.width));
    IM_CHECK(header.height == static_cast<std::uint32_t>(2 * cameras()[0].resolution.height));
    IM_CHECK(facts_of(state) == before);

    // --- (5) Two cameras sharing a name disambiguate WITHIN the plan -------------------
    // The rename rides the shipped L1 verb through the writer identity — the Inspector
    // ships no rename field yet (editor.panels.inspector), so there is no widget to drive.
    const arbc::ObjectId second_id = cameras()[1].id;
    canvas.apply_edit([&state, second_id] {
      ace::scene::rename_camera(state.document(), state.registry(), second_id, "Camera 1");
    });
    IM_CHECK(pump_until(ctx, [&] { return cameras()[1].name == "Camera 1"; }));
    ctx->ItemInputValue("export/###export_scale", 1);
    ctx->Yield(2);
    before = facts_of(state);
    const std::shared_ptr<const ace::commands::ExportReport> third = run_and_wait();
    IM_CHECK(third != nullptr);
    IM_CHECK(third->items.size() == 2);
    IM_CHECK(third->written == 2);
    IM_CHECK(std::filesystem::exists(camera_1));
    IM_CHECK(std::filesystem::exists(e2e->exports_dir / "Camera 1-2.png"));
    IM_CHECK(facts_of(state) == before);

    // --- (8) Ctrl+Z undoes the RENAME, not an export ----------------------------------
    // Export is not a transaction (D15): it added no journal entry, so the next undo is
    // still the rename.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    IM_CHECK(pump_until(ctx, [&] { return cameras()[1].name == "Camera 2"; }));
    IM_CHECK(cameras().size() == 2);

    // --- (6) A filled background yields fully opaque output ----------------------------
    before = facts_of(state);
    ctx->ItemClick("export/###export_bg_filled"); // reveals ###export_bg_color, opaque by default
    ctx->Yield(2);
    // The assertion is on the OUTCOME, not on ImGui's internal decomposition of a
    // ColorEdit4 (which registers sub-items, not one item under the label id): every
    // rendered pixel comes back fully opaque, which is only true if the colour the
    // panel handed the renderer was actually applied under the content.
    const std::shared_ptr<const ace::commands::ExportReport> filled = run_and_wait();
    IM_CHECK(filled != nullptr);
    IM_CHECK(filled->items.size() == 2);
    IM_CHECK(filled->written == 2);
    for (const ace::commands::ExportItemResult& item : filled->items) {
      IM_CHECK(item.opaque); // every rendered pixel fully opaque — no decoder needed
    }
    ctx->ItemClick("export/###export_bg_filled"); // back to the transparent default
    ctx->Yield(2);
    IM_CHECK(facts_of(state) == before);

    // --- (7) Progress and cancel are observable ----------------------------------------
    // A batch big enough that cancel lands BETWEEN items rather than after the last one.
    canvas.apply_edit([&state] {
      for (const ace::scene::Camera& camera : ace::scene::cameras(state.document())) {
        ace::scene::set_camera_resolution(state.document(), state.registry(), camera.id,
                                          ace::scene::Resolution{160, 120});
      }
    });
    IM_CHECK(pump_until(ctx, [&] { return cameras()[0].resolution.width == 160; }));
    ctx->ItemInputValue("export/###export_scale", 6);
    ctx->Yield(2);
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
    // Whatever HAD been written is a complete, signature-valid PNG — a started item
    // always finishes (Constraint 10).
    for (const ace::commands::ExportItemResult& item : cancelled->items) {
      if (item.written) {
        IM_CHECK(read_png_header(item.path).signature);
      }
    }
    IM_CHECK(pump_until(ctx, [&] { return !service.running(); }));
    ctx->Yield(2);
    IM_CHECK(disabled(cancel_button)); // disabled again once idle
    IM_CHECK(facts_of(state) == before);

    // --- (9) The destination override rides the shipped FolderDialog seam --------------
    std::error_code ec;
    std::filesystem::create_directories(e2e->elsewhere, ec);
    e2e->dialog->next = e2e->elsewhere;
    const std::size_t exports_before = count_pngs(e2e->exports_dir);
    ctx->ItemClick("export/###export_browse");
    ctx->Yield(2);
    IM_CHECK(e2e->dialog->shown);
    canvas.apply_edit([&state] {
      for (const ace::scene::Camera& camera : ace::scene::cameras(state.document())) {
        ace::scene::set_camera_resolution(state.document(), state.registry(), camera.id,
                                          ace::scene::Resolution{32, 32});
      }
    });
    IM_CHECK(pump_until(ctx, [&] { return cameras()[0].resolution.width == 32; }));
    ctx->ItemInputValue("export/###export_scale", 1);
    ctx->Yield(2);
    before = facts_of(state);
    const std::shared_ptr<const ace::commands::ExportReport> elsewhere = run_and_wait();
    IM_CHECK(elsewhere != nullptr);
    IM_CHECK(elsewhere->items.size() == 2);
    IM_CHECK(elsewhere->written == 2);
    IM_CHECK(count_pngs(e2e->elsewhere) == 2);
    IM_CHECK(count_pngs(e2e->exports_dir) == exports_before); // untouched
    IM_CHECK(elsewhere->items[0].path.parent_path() == e2e->elsewhere);
    IM_CHECK(facts_of(state) == before);

    // --- (10) An unwritable destination is REPORTED on screen, not thrown ---------------
    // Constraint 4 all the way out to the panel: `blocked` is a regular FILE, so
    // `make_directories` fails with ENOTDIR and every write beneath it fails too. The
    // batch still runs to completion item by item, and BOTH the plan-level reason and
    // the per-item failure messages are drawn — a stated failure, never a silent no-op.
    std::ofstream(e2e->blocked, std::ios::binary) << "not a directory";
    IM_CHECK(std::filesystem::is_regular_file(e2e->blocked));
    before = facts_of(state);
    const std::string doomed = (e2e->blocked / "out").string();
    ctx->ItemInputValue("export/###export_destination", doomed.c_str());
    ctx->Yield(2);
    const std::shared_ptr<const ace::commands::ExportReport> unwritable = run_and_wait();
    IM_CHECK(unwritable != nullptr);
    IM_CHECK(unwritable->state == ExportState::Finished); // ran to the end, item by item
    IM_CHECK(unwritable->items.size() == 2);
    IM_CHECK(unwritable->written == 0);
    IM_CHECK(unwritable->failed == 2);
    IM_CHECK(!unwritable->reason.empty()); // "Destination not created: ..."
    for (const ace::commands::ExportItemResult& item : unwritable->items) {
      IM_CHECK(!item.written);
      IM_CHECK(!item.message.empty()); // "Write failed for '...': ..."
    }
    IM_CHECK(count_pngs(e2e->elsewhere) == 2); // the previous run's output is untouched
    ctx->Yield(3);                             // the panel draws the reason + the messages
    IM_CHECK(ctx->ItemExists("export/###export_status"));
    IM_CHECK(facts_of(state) == before);

    // --- (11) A ticked camera UNDO removed: an empty plan is Failed, with a reason ------
    // The tick list holds ids, not rows, so walking the journal back past both mints
    // leaves the panel ticking cameras that no longer exist. The button stays enabled —
    // the user did tick something — and the run refuses rather than guessing, reporting
    // Failed plus WHY instead of quietly writing nothing.
    for (int i = 0; i < 80 && state.document().journal().cursor() > 0; ++i) {
      const std::size_t cursor = state.document().journal().cursor();
      ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
      ctx->Yield(4);
      if (state.document().journal().cursor() == cursor) {
        ctx->Yield(8); // the undo rides the writer thread; give it a beat before re-pressing
      }
    }
    IM_CHECK(pump_until(ctx, [&] { return cameras().empty(); }));
    ctx->Yield(2);
    IM_CHECK(!ctx->ItemExists("export/###export_cam_0")); // no rows left to tick
    IM_CHECK(!disabled(run_button));                      // still ticked, so still enabled
    const std::shared_ptr<const ace::commands::ExportReport> stale = run_and_wait();
    IM_CHECK(stale != nullptr);
    IM_CHECK(stale->state == ExportState::Failed);
    IM_CHECK(stale->items.empty());
    IM_CHECK(stale->written == 0);
    IM_CHECK(!stale->reason.empty()); // "None of the selected cameras still exist."
    ctx->Yield(3);                    // the panel draws the "Failed" label plus the reason
    IM_CHECK(ctx->ItemExists("export/###export_status"));
    IM_CHECK(count_pngs(e2e->elsewhere) == 2); // an empty plan wrote nothing anywhere
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
