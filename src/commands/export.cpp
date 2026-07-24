#include <ace/commands/export.hpp>
#include <ace/scene/camera.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/runtime/document.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace ace::commands {
namespace {

// The Windows reserved device names (D-export-6). A file called `CON` (or `aux.png`)
// is un-creatable on Windows, so the stem is prefixed rather than emitted as-is —
// sanitizing for the most restrictive target keeps one derived filename portable.
constexpr std::string_view k_reserved_names[] = {
    "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
    "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};

char upper(char ch) { return (ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - 'a' + 'A') : ch; }

// Reserved-ness is decided on the part BEFORE the first dot, case-insensitively —
// `aux.png` is as unusable as `AUX` on Windows.
bool is_reserved_device_name(const std::string& stem) {
  std::string base = stem.substr(0, stem.find('.'));
  for (char& ch : base) {
    ch = upper(ch);
  }
  return std::find(std::begin(k_reserved_names), std::end(k_reserved_names), base) !=
         std::end(k_reserved_names);
}

// Take the next free variant of `base` within one plan: `base`, then `base-2`,
// `base-3`, ... The FIRST occurrence is never renumbered (D-export-6): a user whose
// camera names are already distinct — i.e. every normal export — gets exactly the
// filenames the .tji decided, and only a genuine within-batch duplicate is suffixed.
std::string take_unique_stem(const std::string& base, std::vector<std::string>& used) {
  std::string candidate = base;
  for (int n = 2; std::find(used.begin(), used.end(), candidate) != used.end(); ++n) {
    candidate = base + "-" + std::to_string(n);
  }
  used.push_back(candidate);
  return candidate;
}

// Whether every pixel is fully opaque — the observable the filled-background option
// is asserted through, without introducing a PNG decoder just for tests.
bool all_opaque(const Srgb8Image& image) {
  if (image.pixels.empty()) {
    return false;
  }
  for (std::size_t i = 3; i < image.pixels.size(); i += 4) {
    if (image.pixels[i] != 255) {
      return false;
    }
  }
  return true;
}

} // namespace

std::string sanitize_stem(std::string_view name, std::size_t index) {
  std::string out;
  out.reserve(name.size());
  for (const char raw : name) {
    const unsigned char ch = static_cast<unsigned char>(raw);
    // The portable set. Everything else — `/`, `\`, the drive-letter `:`, control
    // characters, an embedded NUL, and any non-ASCII byte — is DROPPED, which is what
    // structurally prevents a camera name from addressing anything outside the
    // destination directory (Constraint 11).
    const bool portable = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                          (ch >= '0' && ch <= '9') || ch == ' ' || ch == '-' || ch == '_' ||
                          ch == '.';
    if (!portable) {
      continue;
    }
    // Collapse runs of dots, so `../..` cannot survive as a `..` traversal even once
    // the separators are gone.
    if (ch == '.' && !out.empty() && out.back() == '.') {
      continue;
    }
    out.push_back(static_cast<char>(ch));
  }
  // Trim leading/trailing dots and spaces: a leading dot hides the file on POSIX, and
  // a trailing dot or space is silently stripped by Windows (so two distinct camera
  // names would collapse to one file).
  const std::size_t first = out.find_first_not_of(". ");
  if (first == std::string::npos) {
    return "camera-" + std::to_string(index + 1);
  }
  out = out.substr(first, out.find_last_not_of(". ") - first + 1);
  if (is_reserved_device_name(out)) {
    out.insert(out.begin(), '_');
  }
  return out;
}

ExportPlan plan_export(const arbc::Document& document, const std::vector<arbc::ObjectId>& selected,
                       const ExportOptions& options, const ShotCameraFn& shot_camera) {
  ExportPlan plan;
  // Refuse rather than guess (D23 read across): an empty tick-list is not "export
  // everything", and an unresolved destination is not "somewhere sensible".
  if (selected.empty()) {
    plan.reason = "No cameras selected — tick at least one camera to export.";
    return plan;
  }
  if (options.destination.empty()) {
    plan.reason = "No destination directory.";
    return plan;
  }
  if (!shot_camera) {
    plan.reason = "No render-camera derivation installed.";
    return plan;
  }

  const int scale = std::max(1, options.scale);
  // Layer order, over the lock-free `pin()` reader seam — a read, so nothing is
  // posted to the writer (Constraint 7).
  const std::vector<scene::Camera> cameras = scene::cameras(document);
  std::vector<std::string> used;
  for (const scene::Camera& camera : cameras) {
    if (std::find(selected.begin(), selected.end(), camera.id) == selected.end()) {
      continue;
    }
    ExportItem item;
    item.camera = camera.id;
    item.camera_name = camera.name;
    const std::string stem = take_unique_stem(sanitize_stem(camera.name, plan.items.size()), used);
    item.path = options.destination / (stem + ".png");

    // Constraint 2: the N x multiplier is a resolution MULTIPLY, not a resample. The
    // pixels are genuinely composed at the higher resolution by the SAME derivation.
    const std::int64_t out_w = static_cast<std::int64_t>(camera.resolution.width) * scale;
    const std::int64_t out_h = static_cast<std::int64_t>(camera.resolution.height) * scale;
    if (out_w <= 0 || out_h <= 0) {
      item.refused = true;
      item.message = "Camera '" + camera.name + "' has no output resolution.";
      plan.items.push_back(std::move(item));
      continue;
    }
    const std::int64_t bytes = out_w * out_h * 4;
    if (bytes > k_max_export_bytes) {
      item.refused = true;
      item.message = "Camera '" + camera.name + "' at " + std::to_string(out_w) + "x" +
                     std::to_string(out_h) + " needs " + std::to_string(bytes) +
                     " bytes, over the " + std::to_string(k_max_export_bytes) + "-byte limit.";
      constexpr std::int64_t k_int_max = std::numeric_limits<int>::max();
      item.width = static_cast<int>(std::min(out_w, k_int_max));
      item.height = static_cast<int>(std::min(out_h, k_int_max));
      plan.items.push_back(std::move(item));
      continue;
    }
    item.width = static_cast<int>(out_w);
    item.height = static_cast<int>(out_h);
    // `interact::viewport_camera_for_shot` VERBATIM, through the injected seam — the
    // exporter re-derives, re-fits and letterboxes nothing (Constraint 1).
    item.render_camera = shot_camera(camera.frame, camera.resolution.width,
                                     camera.resolution.height, item.width, item.height);
    plan.items.push_back(std::move(item));
  }

  if (plan.items.empty()) {
    plan.reason = "None of the selected cameras still exist.";
  }
  return plan;
}

ExportReport run_export(const ExportPlan& plan, const ExportOptions& options,
                        const ExportRunner& runner) {
  ExportReport report;
  report.reason = plan.reason;

  const auto publish = [&](std::size_t done, std::string_view current, ExportState state) {
    if (!runner.publish) {
      return;
    }
    ExportProgress progress;
    progress.done = done;
    progress.total = plan.items.size();
    progress.current_name = std::string(current);
    progress.state = state;
    runner.publish(progress);
  };

  if (runner.revision) {
    report.start_revision = runner.revision();
    report.end_revision = report.start_revision;
  }

  if (plan.items.empty() || !runner.render || runner.filesystem == nullptr) {
    if (report.reason.empty()) {
      report.reason = runner.render == nullptr       ? "No renderer installed."
                      : runner.filesystem == nullptr ? "No filesystem installed."
                                                     : "Nothing to export.";
    }
    report.state = ExportState::Failed;
    publish(0, {}, report.state);
    return report;
  }

  report.state = ExportState::Running;
  publish(0, {}, ExportState::Running);
  // `save_project_as` deliberately scaffolds no `exports/`, and the user may point the
  // destination anywhere, so seed it here. A failure is recorded, not thrown: the
  // per-item write below reports the real consequence.
  if (const std::error_code ec = runner.filesystem->make_directories(options.destination)) {
    report.reason = "Destination not created: " + ec.message();
  }

  bool cancelled = false;
  for (const ExportItem& item : plan.items) {
    // Cancellation is honest at ITEM granularity (Constraint 10): `render_offline`
    // exposes no cancellation hook, so a started item always finishes and its file is
    // always complete.
    if (runner.cancel != nullptr && runner.cancel->load(std::memory_order_acquire)) {
      cancelled = true;
      break;
    }
    publish(report.items.size(), item.camera_name, ExportState::Running);

    ExportItemResult result;
    result.camera_name = item.camera_name;
    result.path = item.path;
    result.width = item.width;
    result.height = item.height;

    if (item.refused) {
      result.refused = true;
      result.message = item.message;
      ++report.refused;
    } else {
      const Srgb8Image image =
          runner.render(item.render_camera, item.width, item.height, options.background);
      const std::size_t expected =
          static_cast<std::size_t>(item.width) * static_cast<std::size_t>(item.height) * 4;
      if (image.width != item.width || image.height != item.height ||
          image.pixels.size() != expected) {
        result.message = "Render failed for '" + item.camera_name + "'.";
        ++report.failed;
      } else {
        result.opaque = all_opaque(image);
        const std::vector<std::uint8_t> png = encode_png(image);
        if (png.empty()) {
          result.message = "PNG encode failed for '" + item.camera_name + "'.";
          ++report.failed;
        } else {
          // The FileSystem seam is `string_view`-based, so the PNG blob crosses as a
          // view over its own bytes — no copy, no `FILE*`, no `<fstream>` (Constraint 6).
          const std::error_code ec = runner.filesystem->write_file(
              item.path, std::string_view(reinterpret_cast<const char*>(png.data()), png.size()));
          if (ec) {
            result.message = "Write failed for '" + item.camera_name + "': " + ec.message();
            ++report.failed;
          } else {
            result.written = true;
            result.bytes = png.size();
            ++report.written;
          }
        }
      }
    }
    report.items.push_back(std::move(result));
  }

  report.state = cancelled ? ExportState::Cancelled : ExportState::Finished;
  if (runner.revision) {
    report.end_revision = runner.revision();
    report.document_changed_during_export = report.end_revision != report.start_revision;
  }
  publish(report.items.size(), {}, report.state);
  return report;
}

// ---- the async job (D-export-7) --------------------------------------------

ExportService::ExportService(platform::Threads& threads, const platform::FileSystem& filesystem)
    : threads_(threads), filesystem_(filesystem),
      progress_(std::make_shared<const ExportProgress>()) {}

ExportService::~ExportService() {
  // Cancel-then-join, never detach (Constraint 8). The shell destroys this INSIDE the
  // scope enclosing the `Document`'s lifetime, so by the time the document is released
  // no worker can still be reading it; cancelling first keeps shutdown from blocking on
  // the tail of a long batch (the item in flight still finishes, so its file is whole).
  cancel();
  join();
}

void ExportService::set_renderer(RenderFn render) { render_ = std::move(render); }

void ExportService::set_shot_camera(ShotCameraFn shot_camera) {
  shot_camera_ = std::move(shot_camera);
}

void ExportService::set_revision(RevisionFn revision) { revision_ = std::move(revision); }

void ExportService::set_destination_picker(PickDirectoryFn picker) {
  pick_directory_ = std::move(picker);
}

void ExportService::pick_destination(
    std::function<void(std::optional<std::filesystem::path>)> on_pick) const {
  if (pick_directory_) {
    pick_directory_(std::move(on_pick));
  }
}

bool ExportService::start(ExportPlan plan, ExportOptions options) {
  if (running_.load(std::memory_order_acquire)) {
    return false;
  }
  // Reap the previous job's handle before spawning: the join is also the
  // happens-before edge that lets this thread rewrite `plan_` / `options_`.
  join();
  if (!render_) {
    return false;
  }
  plan_ = std::move(plan);
  options_ = std::move(options);
  cancel_.store(false, std::memory_order_release);
  running_.store(true, std::memory_order_release);
  progress_.store(std::make_shared<const ExportProgress>(
                      ExportProgress{0, plan_.items.size(), std::string(), ExportState::Running}),
                  std::memory_order_release);

  ExportRunner runner;
  runner.render = render_;
  runner.filesystem = &filesystem_;
  runner.cancel = &cancel_;
  runner.revision = revision_;
  // A18: the worker never mutates a shared struct — it builds a self-contained value
  // and REPLACES the published pointer, so a snapshot the UI holds stays valid and
  // internally consistent across any number of later publications.
  runner.publish = [this](const ExportProgress& progress) {
    progress_.store(std::make_shared<const ExportProgress>(progress), std::memory_order_release);
  };

  job_ = threads_.spawn([this, runner] {
    ExportReport report = run_export(plan_, options_, runner);
    // Publish the report BEFORE clearing `running_`, so a UI thread that sees the job
    // finish always finds the report that explains it.
    report_.store(std::make_shared<const ExportReport>(std::move(report)),
                  std::memory_order_release);
    running_.store(false, std::memory_order_release);
  });
  if (!job_) {
    running_.store(false, std::memory_order_release);
    return false;
  }
  return true;
}

void ExportService::cancel() { cancel_.store(true, std::memory_order_release); }

void ExportService::join() {
  if (job_ && job_->joinable()) {
    job_->join();
  }
  job_.reset();
}

} // namespace ace::commands
