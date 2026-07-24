#pragma once

// editor.cameras.export — turn saved shot cameras into files on disk (D14, A20).
//
// A camera IS the export spec: it already carries a framing and a resolution, so
// there is nothing to invent. The user ticks cameras, optionally sets an integer N x
// multiplier and a filled background, and the kernel here derives one item per
// camera — output size, render camera, destination path — renders each through an
// INJECTED renderer, encodes PNG, and writes through `platform::FileSystem`.
//
// Everything in this header is L1 and headless (A8). The two impure steps are
// injected rather than depended on (D-export-1):
//
//   * `RenderFn` is `render::render_document_srgb8` (or its filled-background
//     sibling), bound by L4. `commands -> render` is not merely undeclared but
//     LEVEL-INVERTING — `render` is L2 — and §9 puts "the bulk" of logic in L1, so
//     the one impure step is inverted exactly as A18 inverts `CanvasHost`'s
//     post-edit hook.
//   * `ShotCameraFn` is `interact::viewport_camera_for_shot`, bound VERBATIM by L4
//     (Constraint 1: the exporter re-derives, re-fits and letterboxes nothing).
//     `interact` is L1 BESIDE `commands`, not below it — `commands`' declared deps
//     are `{base, project, scene}` — so the same inversion applies, and `ALLOWED`
//     in `scripts/check_levels.py` is unchanged.
//
// Export is NOT a scene edit (D15): it mutates nothing, opens no transaction, adds
// no journal entry, is not undoable, and posts nothing to the writer thread. It
// reads through `pin()` / `for_each_content()`, which A4.1b's posting inventory
// leaves unposted.

#include <ace/base/image.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/threads.hpp>
#include <ace/scene/camera.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arbc {
class Document;
} // namespace arbc

namespace ace::commands {

using Srgb8Image = base::Srgb8Image;
using Rgba8 = base::Rgba8;

// The per-item output budget (D-export-4), as a bound on the RGBA8 result's byte
// count (`out_w * out_h * 4`).
//
// Derivation: an item holds BOTH the straight-alpha sRGB8 result (4 bytes/px) and
// libarbc's premultiplied-linear working-space target (16 bytes/px at rgba32f) live
// at once — 20 bytes per output pixel — so a 512 MiB RGBA8 cap bounds one item at
// ~2.5 GiB of resident pixels, which a workstation survives. It is deliberately
// comfortable at the top of the MINTING clamp: `interact::k_max_mint_resolution` is
// 8192, and 8192x8192 RGBA8 is 256 MiB, so every minted camera exports at 1x and at
// 2x on one side. What it refuses is the case the mint clamp cannot see — a
// hand-typed resolution (editor.cameras.manip imposes no bound) multiplied by N x:
// "8192x8192 at 2x" is a 1 GiB result over a 5 GiB working target, and at 8x it is
// the 17 GiB render that kills the process before `render_offline` can return an
// error value at all. Refusing as a VALUE, per item, is what keeps Constraint 4
// whole and tells the user WHICH camera and HOW big (D23's refuse-rather-than-guess).
inline constexpr std::int64_t k_max_export_bytes = 512LL * 1024 * 1024;

// The knobs D14 names, plus the destination D16 defaults to `exports/`.
struct ExportOptions {
  // Where files land. Empty means "the caller has not resolved one yet" — the panel
  // seeds it from `AppState::layout().exports_dir` (D-export-10) and the user may
  // override it through the shipped folder-picker seam.
  std::filesystem::path destination;
  // The N x scale multiplier (D14): `out = N * native`, a genuine higher-resolution
  // COMPOSE, never a post-hoc resample (Constraint 2). Clamped to >= 1 at plan time;
  // the default is 1x (the .tji pre-exec decision).
  int scale = 1;
  // nullopt = TRANSPARENT, the default (the .tji pre-exec decision: it preserves
  // alpha, a filled background is a deliberate opt-in). On the transparent path the
  // renderer is byte-identical to the shipped `render_document_srgb8`.
  std::optional<Rgba8> background;
};

// One planned output. A refused item still occupies its slot (Constraint 3: the
// refusal is per item, so one oversized camera never kills a batch).
struct ExportItem {
  arbc::ObjectId camera{};
  std::string camera_name;
  std::filesystem::path path; // absolute-or-destination-relative; always under `destination`
  int width = 0;              // N * camera resolution
  int height = 0;
  // The comp -> device render camera, `viewport_camera_for_shot(frame, native_w,
  // native_h, width, height)` verbatim. At N == 1 this reproduces the exact camera
  // the shot was minted from, which is what makes an export reproduce what
  // look_through previews.
  arbc::Affine render_camera = arbc::Affine::identity();
  bool refused = false;
  std::string message; // the refusal reason; empty when the item is renderable
};

struct ExportPlan {
  std::vector<ExportItem> items;
  // Why the plan is empty, when it is (no cameras ticked, no destination). Refuse
  // rather than guess — `run_export` over an empty plan writes nothing.
  std::string reason;
};

// The job's lifecycle, published with every progress snapshot.
enum class ExportState {
  Idle,      // nothing has run this session
  Running,   // a job is in flight
  Finished,  // ran to completion (individual items may still have failed)
  Cancelled, // stopped between items at the user's request
  Failed,    // refused to start (an empty plan, no renderer, no filesystem)
};

// The cross-thread progress readout (Constraint 10 / A18): a SELF-CONTAINED value the
// worker publishes by pointer and the UI thread `load()`s once per frame, so a frame's
// readout is always one coherent generation — no mutex, no half-written struct.
struct ExportProgress {
  std::size_t done = 0;  // items finished (written, failed or refused)
  std::size_t total = 0; // items in the plan
  std::string current_name;
  ExportState state = ExportState::Idle;
};

// What actually happened to one item. Every failure is a VALUE (Constraint 4):
// nothing throws, nothing aborts, and a failed item never stops the batch.
struct ExportItemResult {
  std::string camera_name;
  std::filesystem::path path;
  int width = 0;
  int height = 0;
  std::size_t bytes = 0; // encoded PNG size
  bool written = false;
  bool refused = false;
  // Whether every pixel of the RENDERED image was fully opaque — the observable the
  // filled-background option is asserted through without introducing a PNG decoder.
  bool opaque = false;
  std::string message; // empty on success
};

struct ExportReport {
  std::vector<ExportItemResult> items;
  std::size_t written = 0;
  std::size_t failed = 0;
  std::size_t refused = 0;
  ExportState state = ExportState::Idle;
  std::string reason; // carried from the plan when there was nothing to do
  // Batch coherence is REPORTED, not enforced (D-export-8): `render_offline` pins the
  // CURRENT version per call, so an edit landing mid-batch can make item 3 reflect a
  // document item 1 did not. Blocking edits contradicts D14's async promise and A4's
  // responsive UI; freezing the version is not available (the library renders a
  // `const Document&`, not a pin). Two `revision()` reads turn a silent incoherence
  // into a stated one.
  std::uint64_t start_revision = 0;
  std::uint64_t end_revision = 0;
  bool document_changed_during_export = false;
};

// The injected offline render (D-export-1). `background` nullopt = transparent.
using RenderFn = std::function<Srgb8Image(const arbc::Affine& camera, int width, int height,
                                          const std::optional<Rgba8>& background)>;

// The injected comp -> device render-camera derivation: `interact::viewport_camera_for_shot`.
using ShotCameraFn = std::function<arbc::Affine(const arbc::Affine& frame, int native_w,
                                                int native_h, int out_w, int out_h)>;

// The injected any-thread document-revision read (`document.pin()->revision()`), for
// D-export-8's coherence flag. Empty leaves both revisions 0 and the flag false.
using RevisionFn = std::function<std::uint64_t()>;

// The injected progress publication (A18). Empty publishes nothing.
using ProgressFn = std::function<void(const ExportProgress&)>;

// The injected destination picker (D-export-10): L4 binds this to the SHIPPED
// `app::FolderDialog::show` (A12). The user picks a DIRECTORY, never a filename —
// D14 names files by camera — so no `SDL_ShowSaveFileDialog` surface is introduced.
// Asynchronous, exactly like the folder seam it wraps: the inner callback fires on a
// later UI-thread frame, or with nullopt on cancel.
using PickDirectoryFn =
    std::function<void(std::function<void(std::optional<std::filesystem::path>)>)>;

// Reduce a free-text camera name to a portable filename stem (Constraint 11 /
// D-export-6). Keeps alphanumerics, space, `-`, `_` and `.`; DROPS path separators,
// drive-letter colons, control characters and embedded NULs; collapses runs of `.`
// to one and trims leading/trailing dots and spaces; prefixes the Windows reserved
// device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1-9`, `LPT1-9`) with `_`; and falls
// back to `camera-<index+1>` when nothing survives.
//
// Doing this in L1 rather than trusting the filesystem is what makes "a camera name
// cannot write outside the destination" a headless Catch2 assertion instead of a
// platform behaviour.
std::string sanitize_stem(std::string_view name, std::size_t index);

// Build the plan for `selected` cameras (D-export-6). Items come out in
// `scene::cameras()` order — layer order — one per selected camera that still
// exists, with distinct paths all directly under `options.destination`. Within one
// plan, repeated stems are disambiguated `-2`, `-3`, ...; the FIRST occurrence is
// never renumbered. A collision with an existing file on disk is NOT disambiguated:
// it overwrites in place (the .tji pre-exec decision — batch export is idempotent,
// mirroring Save's re-dump).
//
// An empty selection or an empty destination yields an empty plan carrying a
// `reason`, never a guess.
ExportPlan plan_export(const arbc::Document& document, const std::vector<arbc::ObjectId>& selected,
                       const ExportOptions& options, const ShotCameraFn& shot_camera);

// Encode a straight-alpha sRGB8 RGBA image as an 8-bit RGBA PNG (D-export-3).
// Degenerate input — a non-positive dimension, or `pixels.size() != w*h*4` — yields
// an EMPTY vector rather than a malformed file. Pure; allocates only the result.
std::vector<std::uint8_t> encode_png(const Srgb8Image& image);

// The collaborators `run_export` drives, all injected (D-export-1).
struct ExportRunner {
  RenderFn render;                                  // required
  const platform::FileSystem* filesystem = nullptr; // required
  ProgressFn publish;                               // optional (A18)
  // Checked BETWEEN items (Constraint 10): a single `render_offline` call is not
  // interruptible and must not be pretended otherwise.
  const std::atomic<bool>* cancel = nullptr;
  RevisionFn revision; // optional (D-export-8)
};

// Run `plan`: for each item, render -> encode -> write, publishing a fresh progress
// snapshot before and after. Never throws; every outcome is a value.
ExportReport run_export(const ExportPlan& plan, const ExportOptions& options,
                        const ExportRunner& runner);

// The async job (D-export-7): ONE thread from `platform::Threads::spawn` — the
// faculty whose own charter names this consumer — publishing an A18 immutable
// progress snapshot and cancellable between items.
//
// Deliberately NOT the `WriterThread` (its posting inventory is WRITES; queuing a
// multi-second render behind the writer's FIFO would stall every edit) and NOT
// libarbc's shared `WorkerPool` (A5 gives that to the interactive renderers).
//
// LIFETIME (Constraint 8): the in-flight job renders the one owned `Document`, so the
// service must be `join()`ed — or destroyed — INSIDE the scope that encloses the
// document's lifetime. The destructor joins; `detach()` is never called.
class ExportService {
public:
  ExportService(platform::Threads& threads, const platform::FileSystem& filesystem);
  ~ExportService();

  ExportService(const ExportService&) = delete;
  ExportService& operator=(const ExportService&) = delete;

  // Installed once by L4 at bootstrap.
  void set_renderer(RenderFn render);
  void set_shot_camera(ShotCameraFn shot_camera);
  void set_revision(RevisionFn revision);
  void set_destination_picker(PickDirectoryFn picker);

  // The derivation the panel needs to build a plan. Never null once L4 has bound it.
  const ShotCameraFn& shot_camera() const { return shot_camera_; }

  bool can_pick_destination() const { return static_cast<bool>(pick_directory_); }

  // UI THREAD. Open the injected folder picker; `on_pick` fires on a later frame.
  // A no-op when no picker is installed (the headless fixtures).
  void pick_destination(std::function<void(std::optional<std::filesystem::path>)> on_pick) const;

  // UI THREAD. Spawn a job over `plan`. Returns false (and publishes nothing) when a
  // job is already in flight or no renderer is installed.
  bool start(ExportPlan plan, ExportOptions options);

  // UI THREAD. Ask the in-flight job to stop after the current item.
  void cancel();

  bool running() const { return running_.load(std::memory_order_acquire); }

  // ANY THREAD, lock-free. Never null — an Idle snapshot is published at construction.
  std::shared_ptr<const ExportProgress> progress() const {
    return progress_.load(std::memory_order_acquire);
  }

  // ANY THREAD, lock-free. Null until a job has completed.
  std::shared_ptr<const ExportReport> report() const {
    return report_.load(std::memory_order_acquire);
  }

  // Join the spawned thread if any. Idempotent; safe when nothing was ever started.
  void join();

private:
  platform::Threads& threads_;
  const platform::FileSystem& filesystem_;
  RenderFn render_;
  ShotCameraFn shot_camera_;
  RevisionFn revision_;
  PickDirectoryFn pick_directory_;
  ExportPlan plan_;
  ExportOptions options_;
  std::atomic<bool> running_{false};
  std::atomic<bool> cancel_{false};
  std::atomic<std::shared_ptr<const ExportProgress>> progress_;
  std::atomic<std::shared_ptr<const ExportReport>> report_;
  std::unique_ptr<platform::JoinHandle> job_;
};

} // namespace ace::commands
