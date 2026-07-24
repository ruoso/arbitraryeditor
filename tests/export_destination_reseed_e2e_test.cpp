// editor.cameras.export_destination_reseed — the Export panel's destination must always
// reflect the LIVE project's `exports/` directory, re-seeding whenever a different
// `ExportService` (a new `instance()`) draws the process-wide, file-static panel
// (`src/views/views.cpp:38-65,312-324`; D16 / D-export-10; docs A20). contact_sheet
// (016b63e) shipped that fix — keying the panel reset on the monotonic
// `ExportService::instance()` rather than the service's raw address — but WITHOUT a test
// that asserts the destination follows the project across a service change. This e2e is
// that regression pin (D-reseed-1/3).
//
// The invariant is exercised the one place it first bit: two projects sharing the single
// file-static `g_export_panel` inside the one `ace_shell_test` process. Session A seeds
// and exports into A/exports; a SECOND session B — its own ScratchDir, its own
// `ExportService` with a fresh `instance()` — is pointed at the same panel, and the test
// asserts B's export lands under B/exports and NOT A/exports (the re-seed), then that an
// in-session override survives on subsequent frames (the latch fires ONCE per service).
//
// Built on the export_e2e_test.cpp harness idioms (WriterSession, the REAL renderer /
// `viewport_camera_for_shot` wiring run_editor uses, pump_until / run_and_wait), asserting
// on MODEL state and ON-DISK FILES, never on pixels. The full CanvasView / AppProjectGateway
// apparatus is deliberately omitted: the re-seed lives entirely in `draw_export`'s
// owner-check + seed (`views.cpp:314-324`), a pure UI-thread read of `state.layout()`; it
// needs neither the canvas nor the gateway, and a dockspace can hold only one project at a
// time — the two-session sharing that IS the mechanism under test needs two live services
// drawing one panel, which the direct-draw harness gives without a second dockspace.
#include <ace/app/shell.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/commands/export.hpp>
#include <ace/interact/interact.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/threads.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>
#include <ace/views/views.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "writer_session.hpp"

using ace::app::Shell;
using ace::app::ShellOptions;
using ace::commands::AppState;
using ace::commands::ExportService;
using ace::commands::ExportState;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  explicit ScratchDir(const char* leaf)
      : root(std::filesystem::temp_directory_path() /
             ("ace_export_reseed_e2e_test_" + std::string(leaf))) {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

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

// The document facts the panel must NEVER move (Constraint 7 / D15): a re-seed is a pure
// reader — drawing or exporting across either session changes neither the camera set nor
// any camera's resolution or frame.
struct DocFacts {
  std::size_t camera_count = 0;
  std::vector<ace::scene::Resolution> resolutions;
  std::vector<arbc::Affine> frames;
  friend bool operator==(const DocFacts&, const DocFacts&) = default;
};

DocFacts facts_of(AppState& state) {
  DocFacts facts;
  for (const ace::scene::Camera& camera : ace::scene::cameras(state.document())) {
    facts.resolutions.push_back(camera.resolution);
    facts.frames.push_back(camera.frame);
  }
  facts.camera_count = facts.resolutions.size();
  return facts;
}

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

bool terminal(ExportState state) {
  return state == ExportState::Finished || state == ExportState::Cancelled ||
         state == ExportState::Failed;
}

// One project's live seams: the service (a fresh `instance()`) and the AppState it draws.
struct Bound {
  ExportService* service = nullptr;
  AppState* state = nullptr;
};

// What set_draw_content reads each frame and TestFunc swaps between phases: pointing the
// file-static panel at a second service+state is exactly what the shell would do after a
// new bootstrap (the harness note).
struct E2EState {
  const Bound* bound_a = nullptr;
  const Bound* bound_b = nullptr;
  const Bound** current = nullptr;
  std::filesystem::path exports_a;
  std::filesystem::path exports_b;
  std::filesystem::path elsewhere_b;
};

// Wire a service EXACTLY as run_editor wires it (shell.cpp): the shipped offline renderer,
// `viewport_camera_for_shot` verbatim, and the any-thread revision read. No destination
// picker — the override in phase 3 rides the `###export_destination` field directly, the
// same path export_e2e_test.cpp's phase 10 drives.
void wire_service(ExportService& service, AppState& state) {
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
}

// Seed one camera into a session's document (WRITER-THREAD ONLY — through `on_writer`).
// An identity frame at a small resolution keeps the render cheap; the file is written
// regardless of pixels (run_export writes any correctly-sized image), so no backdrop is
// needed — the assertion is on WHERE the file lands, not on its contents.
void seed_camera(ace::testing::WriterSession& session) {
  session.on_writer([&] {
    // A fresh project carries no composition (the shell's first real edit adds one); a
    // camera needs a root composition to be placed in, so mint it first.
    session.state().document().add_composition(128.0, 128.0);
    ace::scene::add_camera(session.state().document(), session.state().registry(), "Camera 1",
                           ace::scene::Resolution{24, 24}, arbc::Affine::identity());
  });
}

} // namespace

TEST_CASE("export reseed e2e: the destination follows the live project across a service change") {
  ScratchDir scratch_a("a");
  ScratchDir scratch_b("b");
  ace::platform::NativeThreads threads;
  ace::platform::NativeFileSystem fs;

  ace::testing::WriterSession session_a(scratch_a.root / "proj");
  REQUIRE(session_a.ok());
  ace::testing::WriterSession session_b(scratch_b.root / "proj");
  REQUIRE(session_b.ok());
  seed_camera(session_a);
  seed_camera(session_b);
  AppState& state_a = session_a.state();
  AppState& state_b = session_b.state();
  REQUIRE(state_a.layout().exports_dir != state_b.layout().exports_dir);

  Shell shell;
  ShellOptions opts;
  opts.headless = true;
  opts.width = 640;
  opts.height = 480;
  REQUIRE(shell.init(opts));

  ExportService service_a(threads, fs);
  ExportService service_b(threads, fs);
  wire_service(service_a, state_a);
  wire_service(service_b, state_b);
  REQUIRE(service_a.instance() != service_b.instance()); // the identity the re-seed keys on

  const Bound bound_a{&service_a, &state_a};
  const Bound bound_b{&service_b, &state_b};
  const Bound* current = &bound_a;

  // The single file-static `g_export_panel` is drawn once per frame against whichever
  // service `current` points at — the process-wide sharing that is the mechanism itself.
  shell.set_draw_content([&current]() {
    ImGui::Begin("export");
    ace::views::draw_export(*current->service, *current->state, "export");
    ImGui::End();
  });

  ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO& te_io = ImGuiTestEngine_GetIO(engine);
  te_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  te_io.ConfigNoThrottle = true;
  ImGuiTestEngine_Start(engine, shell.imgui_context());

  E2EState e2e{&bound_a,
               &bound_b,
               &current,
               state_a.layout().exports_dir,
               state_b.layout().exports_dir,
               scratch_b.root / "elsewhere"};
  ImGuiTest* test = IM_REGISTER_TEST(engine, "cameras", "export_destination_reseed");
  test->UserData = &e2e;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* e2e = static_cast<E2EState*>(ctx->Test->UserData);
    const auto run_button = std::string("export/###export_run");
    const auto disabled = [ctx](const std::string& ref) {
      return (ctx->ItemInfo(ref.c_str()).ItemFlags & ImGuiItemFlags_Disabled) != 0;
    };
    const auto run_and_wait =
        [&](ExportService& service) -> std::shared_ptr<const ace::commands::ExportReport> {
      ctx->ItemClick(run_button.c_str());
      pump_until(ctx, [&] { return terminal(service.progress()->state); });
      // `running_` is the last write of a completed job (the report is stored just before
      // it clears), so waiting on it — not merely the terminal progress state — guarantees
      // `report()` is this run's.
      pump_until(ctx, [&] { return !service.running(); });
      return service.report();
    };

    ctx->Yield(2);
    IM_CHECK(ctx->ItemExists("export/###export_cam_0")); // the seeded camera's row

    // --- (1) Baseline: session A seeds its own exports/ and exports there ---------------
    // No override — the report's item path proves the panel seeded A's `exports_dir`,
    // never empty and never the process CWD.
    DocFacts before_a = facts_of(*e2e->bound_a->state);
    ctx->ItemClick("export/###export_cam_0");
    ctx->Yield(2);
    IM_CHECK(!disabled(run_button)); // enabled: ticked + seeded
    const std::shared_ptr<const ace::commands::ExportReport> first =
        run_and_wait(*e2e->bound_a->service);
    IM_CHECK(first != nullptr);
    IM_CHECK(first->state == ExportState::Finished);
    IM_CHECK(first->items.size() == 1);
    IM_CHECK(first->written == 1);
    IM_CHECK(first->items[0].path.parent_path() == e2e->exports_a);
    IM_CHECK(std::filesystem::exists(e2e->exports_a / "Camera 1.png"));
    IM_CHECK(count_pngs(e2e->exports_a) == 1);
    IM_CHECK(facts_of(*e2e->bound_a->state) == before_a); // the panel touched nothing

    // --- (2) The re-seed across a service change (the pin) ------------------------------
    // Point the file-static panel at session B's service+state and pump a frame: B's
    // `instance()` differs, so `draw_export` resets the panel (clearing A's ticks AND
    // A's destination) and re-seeds from B's `exports_dir` on the next frame. A fresh run
    // — again WITHOUT overriding — must land under B/exports and leave A/exports untouched.
    // Had the panel retained A's destination (the bug), B's export would target A/exports.
    DocFacts before_b = facts_of(*e2e->bound_b->state);
    const std::size_t exports_a_before = count_pngs(e2e->exports_a);
    *e2e->current = e2e->bound_b;
    ctx->Yield(2); // one frame for the reset+reseed against B
    IM_CHECK(ctx->ItemExists("export/###export_cam_0"));
    IM_CHECK(disabled(run_button)); // reset cleared A's tick → disabled again
    ctx->ItemClick("export/###export_cam_0");
    ctx->Yield(2);
    const std::shared_ptr<const ace::commands::ExportReport> second =
        run_and_wait(*e2e->bound_b->service);
    IM_CHECK(second != nullptr);
    IM_CHECK(second->state == ExportState::Finished);
    IM_CHECK(second->items.size() == 1);
    IM_CHECK(second->written == 1);
    IM_CHECK(second->items[0].path.parent_path() == e2e->exports_b); // followed the LIVE project
    IM_CHECK(std::filesystem::exists(e2e->exports_b / "Camera 1.png"));
    IM_CHECK(count_pngs(e2e->exports_b) == 1);
    IM_CHECK(count_pngs(e2e->exports_a) == exports_a_before); // A/exports untouched by B's run

    // --- (3) Override survives within a session (Constraint 5 anti-vacuity) -------------
    // Still session B: override the destination via the field, then run again. The re-seed
    // fired ONCE on the service change and must NOT clobber the override on later frames —
    // the file lands where the user pointed it, not back in B/exports.
    std::error_code ec;
    std::filesystem::create_directories(e2e->elsewhere_b, ec);
    const std::string dest = e2e->elsewhere_b.string();
    ctx->ItemInputValue("export/###export_destination", dest.c_str());
    ctx->Yield(2);
    const std::shared_ptr<const ace::commands::ExportReport> third =
        run_and_wait(*e2e->bound_b->service);
    IM_CHECK(third != nullptr);
    IM_CHECK(third->state == ExportState::Finished);
    IM_CHECK(third->items.size() == 1);
    IM_CHECK(third->written == 1);
    IM_CHECK(third->items[0].path.parent_path() == e2e->elsewhere_b); // override honoured
    IM_CHECK(count_pngs(e2e->elsewhere_b) == 1);
    IM_CHECK(count_pngs(e2e->exports_b) == 1); // NOT re-seeded back over the override

    // --- (4) The documents are untouched across both sessions --------------------------
    IM_CHECK(facts_of(*e2e->bound_b->state) == before_b);
    IM_CHECK(facts_of(*e2e->bound_a->state) == before_a);
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

  CHECK(frames < k_max_frames);
  CHECK(count_tested == 1);
  CHECK(count_success == 1);

  // Constraint 8: each service renders its own Document, so both are joined before the
  // canvas-less sessions release their documents.
  service_a.cancel();
  service_a.join();
  service_b.cancel();
  service_b.join();
  shell.shutdown();
  ImGuiTestEngine_DestroyContext(engine);
}
