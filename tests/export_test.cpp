// editor.cameras.export — L1 headless Catch2 units for the export kernel (D14, A20),
// plus the three goldens §9 assigns export (`docs/01-architecture.md:300`).
//
// Everything here runs with the two impure steps INJECTED (D-export-1): a stub
// renderer for the plan/refusal/cancel/progress/report matrix, and the REAL
// `interact::viewport_camera_for_shot` for the geometry laws — which is the point of
// the injection, since it puts the whole user-facing policy surface inside headless
// Catch2 reach. No ImGui/GL/SDL; the goldens run GL-free through
// `render::render_document_srgb8` (render_offline), exactly as look_through's does.
#include <ace/commands/export.hpp>
#include <ace/interact/interact.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/result.hpp>
#include <ace/platform/threads.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_raster/raster_content.hpp>
#include <arbc/media/surface_format.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "golden_support.hpp"

using ace::commands::encode_png;
using ace::commands::ExportItem;
using ace::commands::ExportOptions;
using ace::commands::ExportPlan;
using ace::commands::ExportProgress;
using ace::commands::ExportReport;
using ace::commands::ExportRunner;
using ace::commands::ExportService;
using ace::commands::ExportState;
using ace::commands::plan_export;
using ace::commands::Rgba8;
using ace::commands::run_export;
using ace::commands::sanitize_stem;
using ace::commands::Srgb8Image;
using ace::interact::viewport_camera_for_shot;

namespace {

const std::filesystem::path k_dest = std::filesystem::temp_directory_path() / "ace_export_dest";

// The real derivation, injected exactly as the L4 shell binds it (Constraint 1: the
// exporter re-derives nothing).
ace::commands::ShotCameraFn real_shot_camera() { return &viewport_camera_for_shot; }

// ---- fixtures ---------------------------------------------------------------

// A document + a registry seeded with the camera kind, so cameras can be authored.
struct CameraDoc {
  arbc::Registry registry;
  std::unique_ptr<arbc::Document> document = std::make_unique<arbc::Document>();
  arbc::ObjectId root{};

  CameraDoc() {
    arbc::register_builtin_kinds(registry);
    ace::scene::register_camera_kind(registry);
    root = document->add_composition(64.0, 64.0);
  }

  arbc::ObjectId add(const std::string& name, int width, int height,
                     const arbc::Affine& frame = arbc::Affine::identity()) {
    return ace::scene::add_camera(*document, registry, name, ace::scene::Resolution{width, height},
                                  frame);
  }
};

// A 64x64 document whose only content is a BOUNDED opaque raster placed on a
// half-pixel offset: the interior is fully opaque, the exterior fully transparent,
// and the edge pixels carry PARTIAL alpha — which is what makes the filled-background
// golden's anti-vacuity check (a gamma-space blend differs) possible at all.
std::unique_ptr<arbc::Document> build_export_doc() {
  auto doc = std::make_unique<arbc::Document>();
  const arbc::ObjectId root = doc->add_composition(64.0, 64.0);
  arbc::DecodedImage img;
  img.width = 32;
  img.height = 32;
  img.format = arbc::k_working_rgba32f;
  img.bytes.resize(static_cast<std::size_t>(32) * 32 * 4 * sizeof(float));
  auto* fp = reinterpret_cast<float*>(img.bytes.data());
  for (int i = 0; i < 32 * 32; ++i) { // opaque red, premultiplied linear
    fp[i * 4] = 0.8F;
    fp[i * 4 + 1] = 0.0F;
    fp[i * 4 + 2] = 0.0F;
    fp[i * 4 + 3] = 1.0F;
  }
  const arbc::ObjectId raster =
      doc->add_content(std::make_shared<arbc::RasterContent>(std::move(img)));
  doc->attach_layer(root, doc->add_layer(raster, arbc::Affine::translation(16.5, 16.5)));
  return doc;
}

// ---- test doubles -----------------------------------------------------------

// An in-memory filesystem that RECORDS every write, so "wrote nothing" is a real
// assertion; `fail_for` makes one path report an error the way a full disk would.
class RecordingFileSystem final : public ace::platform::FileSystem {
public:
  mutable std::map<std::filesystem::path, std::string> files;
  mutable std::vector<std::filesystem::path> writes;
  std::filesystem::path fail_for;

  bool exists(const std::filesystem::path& path) const override { return files.count(path) != 0; }
  ace::platform::Result<std::vector<std::filesystem::path>>
  list_directory(const std::filesystem::path&) const override {
    return std::vector<std::filesystem::path>{};
  }
  ace::platform::Result<std::string> read_file(const std::filesystem::path& path) const override {
    const auto it = files.find(path);
    if (it == files.end()) {
      return std::make_error_code(std::errc::no_such_file_or_directory);
    }
    return it->second;
  }
  std::error_code write_file(const std::filesystem::path& path,
                             std::string_view contents) const override {
    writes.push_back(path);
    if (!fail_for.empty() && path == fail_for) {
      return std::make_error_code(std::errc::no_space_on_device);
    }
    files[path] = std::string(contents);
    return {};
  }
  std::error_code make_directories(const std::filesystem::path&) const override { return {}; }
  std::error_code atomic_replace(const std::filesystem::path& path,
                                 std::string_view contents) const override {
    return write_file(path, contents);
  }
  // In-memory prefix erase (A26). Export never deletes — "wrote nothing" stays the assertion
  // here — so this only re-signs the double against the seam's new pure virtual.
  std::error_code remove_tree(const std::filesystem::path& path) const override {
    if (path.empty()) {
      return std::make_error_code(std::errc::invalid_argument);
    }
    std::erase_if(files, [&path](const auto& entry) {
      const std::string key = entry.first.string();
      const std::string prefix = path.string();
      return key == prefix || key.rfind(prefix + "/", 0) == 0;
    });
    return {};
  }
};

// A stub renderer producing a well-formed (uniform, half-transparent) image of the
// requested size — enough for the report/progress/cancel matrix, and cheap.
ace::commands::RenderFn stub_renderer(std::atomic<int>* calls = nullptr) {
  return
      [calls](const arbc::Affine&, int width, int height, const std::optional<Rgba8>& background) {
        if (calls != nullptr) {
          calls->fetch_add(1);
        }
        Srgb8Image image;
        image.width = width;
        image.height = height;
        image.pixels.assign(static_cast<std::size_t>(width) * height * 4, 0);
        for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
          image.pixels[i] = 200;
          image.pixels[i + 3] = background ? background->a : 128;
        }
        return image;
      };
}

// ---- a test-local PNG chunk walker (D-export-11's anti-vacuity layer) ---------

std::uint32_t crc32_of(const std::uint8_t* data, std::size_t size) {
  static const std::array<std::uint32_t, 256> table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) {
        c = ((c & 1U) != 0U) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
      }
      t[i] = c;
    }
    return t;
  }();
  std::uint32_t crc = 0xFFFFFFFFU;
  for (std::size_t i = 0; i < size; ++i) {
    crc = table[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFU;
}

struct PngInfo {
  bool signature = false;
  bool structure_ok = false; // every chunk length/CRC parsed cleanly to the end
  bool crcs_ok = true;
  bool has_ihdr = false;
  bool has_iend = false;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  int bit_depth = 0;
  int color_type = 0;
  std::size_t idat_bytes = 0;
};

std::uint32_t be32(const std::uint8_t* p) {
  return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
         (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

// Walks the chunk stream: signature, then <length><type><data><crc32(type+data)>*.
// This is what proves the bytes are a WELL-FORMED PNG rather than merely equal to a
// previously recorded blob — so a golden regenerated from a broken encoder cannot pass.
PngInfo walk_png(const std::vector<std::uint8_t>& bytes) {
  static constexpr std::array<std::uint8_t, 8> k_sig{0x89, 0x50, 0x4E, 0x47,
                                                     0x0D, 0x0A, 0x1A, 0x0A};
  PngInfo info;
  if (bytes.size() < k_sig.size() || !std::equal(k_sig.begin(), k_sig.end(), bytes.begin())) {
    return info;
  }
  info.signature = true;
  std::size_t at = k_sig.size();
  while (at + 8 <= bytes.size()) {
    const std::uint32_t length = be32(&bytes[at]);
    const std::string type(reinterpret_cast<const char*>(&bytes[at + 4]), 4);
    if (at + 12 + length > bytes.size()) {
      return info; // truncated: structure_ok stays false
    }
    const std::uint32_t stored = be32(&bytes[at + 8 + length]);
    if (crc32_of(&bytes[at + 4], length + 4) != stored) {
      info.crcs_ok = false;
    }
    if (type == "IHDR" && length >= 13) {
      info.has_ihdr = true;
      info.width = be32(&bytes[at + 8]);
      info.height = be32(&bytes[at + 12]);
      info.bit_depth = bytes[at + 16];
      info.color_type = bytes[at + 17];
    } else if (type == "IDAT") {
      info.idat_bytes += length;
    } else if (type == "IEND") {
      info.has_iend = true;
      info.structure_ok = (at + 12 + length == bytes.size());
      return info;
    }
    at += 12 + length;
  }
  return info;
}

} // namespace

// ---- the plan ---------------------------------------------------------------

TEST_CASE("export: the plan is a total, injective function from ticked cameras to paths") {
  CameraDoc doc;
  const arbc::ObjectId a = doc.add("Hero", 64, 48);
  const arbc::ObjectId b = doc.add("Wide", 32, 32);
  const arbc::ObjectId c = doc.add("Detail", 16, 16);

  ExportOptions options;
  options.destination = k_dest;
  // Ticked in a DIFFERENT order than the document holds them: the plan is ordered by
  // `scene::cameras()` (layer order), not by the caller's tick order.
  const ExportPlan plan = plan_export(*doc.document, {c, a, b}, options, real_shot_camera());

  REQUIRE(plan.items.size() == 3);
  CHECK(plan.reason.empty());
  CHECK(plan.items[0].camera == a);
  CHECK(plan.items[1].camera == b);
  CHECK(plan.items[2].camera == c);

  std::vector<std::filesystem::path> paths;
  for (const ExportItem& item : plan.items) {
    CHECK_FALSE(item.refused);
    CHECK(item.path.is_absolute());
    CHECK(item.path.parent_path() == k_dest);
    CHECK(item.path.extension() == ".png");
    paths.push_back(item.path);
  }
  std::sort(paths.begin(), paths.end());
  CHECK(std::unique(paths.begin(), paths.end()) == paths.end()); // injective
  CHECK(plan.items[0].width == 64);
  CHECK(plan.items[0].height == 48);
}

TEST_CASE("export: filename sanitization is closed under hostile input") {
  struct Case {
    std::string name;
    bool expect_fallback;
  };
  const std::string with_nul = std::string("bad\0name", 8);
  const std::vector<Case> cases = {{"../../etc/passwd", false},
                                   {"a/b", false},
                                   {"C:\\x", false},
                                   {"  ", true},
                                   {"", true},
                                   {"....", true},
                                   {"CON", false},
                                   {"aux.png", false},
                                   {with_nul, false},
                                   {"tab\there", false}};

  for (std::size_t i = 0; i < cases.size(); ++i) {
    const std::string stem = sanitize_stem(cases[i].name, i);
    INFO("input index " << i);
    CHECK_FALSE(stem.empty());
    // No separator, no drive colon, no control character, no `..` run.
    CHECK(stem.find('/') == std::string::npos);
    CHECK(stem.find('\\') == std::string::npos);
    CHECK(stem.find(':') == std::string::npos);
    CHECK(stem.find("..") == std::string::npos);
    CHECK(stem.find('\0') == std::string::npos);
    CHECK(stem.find('\t') == std::string::npos);
    CHECK(stem.front() != '.');
    CHECK(stem.back() != '.');
    CHECK(stem.front() != ' ');
    CHECK(stem.back() != ' ');
    // Not a Windows reserved device name (the base, before the first dot).
    const std::string base = stem.substr(0, stem.find('.'));
    CHECK(base != "CON");
    CHECK(base != "aux");
    CHECK(base != "AUX");
    if (cases[i].expect_fallback) {
      CHECK(stem == "camera-" + std::to_string(i + 1)); // the positional fallback
    }
    // And the composed path cannot leave the destination.
    const std::filesystem::path path = k_dest / (stem + ".png");
    CHECK(path.parent_path() == k_dest);
  }
  CHECK(sanitize_stem("../../etc/passwd", 0) == "etcpasswd");
  CHECK(sanitize_stem("CON", 0) == "_CON");
  CHECK(sanitize_stem("aux.png", 0) == "_aux.png");
  CHECK(sanitize_stem("Camera 1", 0) == "Camera 1");
}

TEST_CASE("export: within-plan duplicate stems disambiguate deterministically") {
  CameraDoc doc;
  const arbc::ObjectId a = doc.add("Hero", 32, 32);
  const arbc::ObjectId b = doc.add("Hero", 32, 32);
  const arbc::ObjectId c = doc.add("Hero", 32, 32);

  ExportOptions options;
  options.destination = k_dest;

  const ExportPlan two = plan_export(*doc.document, {a, b}, options, real_shot_camera());
  REQUIRE(two.items.size() == 2);
  CHECK(two.items[0].path.filename() == "Hero.png"); // the FIRST is never renumbered
  CHECK(two.items[1].path.filename() == "Hero-2.png");

  const ExportPlan three = plan_export(*doc.document, {a, b, c}, options, real_shot_camera());
  REQUIRE(three.items.size() == 3);
  CHECK(three.items[0].path.filename() == "Hero.png");
  CHECK(three.items[1].path.filename() == "Hero-2.png");
  CHECK(three.items[2].path.filename() == "Hero-3.png");

  // Stable across runs: the same inputs derive the same paths.
  const ExportPlan again = plan_export(*doc.document, {a, b, c}, options, real_shot_camera());
  for (std::size_t i = 0; i < three.items.size(); ++i) {
    CHECK(again.items[i].path == three.items[i].path);
  }
}

TEST_CASE("export: an existing file on disk is overwritten in place, not suffixed") {
  // The .tji pre-exec decision: batch export is idempotent, mirroring Save's re-dump —
  // no auto-suffix, no prompt. Only WITHIN-batch duplicates are disambiguated.
  CameraDoc doc;
  const arbc::ObjectId a = doc.add("Hero", 8, 8);
  ExportOptions options;
  options.destination = k_dest;
  RecordingFileSystem fs;

  ExportRunner runner;
  runner.render = stub_renderer();
  runner.filesystem = &fs;

  const ExportPlan first = plan_export(*doc.document, {a}, options, real_shot_camera());
  CHECK(run_export(first, options, runner).written == 1);
  const ExportPlan second = plan_export(*doc.document, {a}, options, real_shot_camera());
  CHECK(second.items[0].path == first.items[0].path);
  CHECK(run_export(second, options, runner).written == 1);
  CHECK(fs.files.size() == 1); // one file, written twice
  CHECK(fs.writes.size() == 2);
}

TEST_CASE("export: the N x multiplier is a resolution multiply, not a resample") {
  CameraDoc doc;
  const arbc::Affine frame{1.0, 0.0, 0.0, 1.0, 12.0, 7.0};
  const arbc::ObjectId a = doc.add("Hero", 320, 200, frame);

  ExportOptions options;
  options.destination = k_dest;
  options.scale = 3;
  const ExportPlan plan = plan_export(*doc.document, {a}, options, real_shot_camera());
  REQUIRE(plan.items.size() == 1);
  CHECK(plan.items[0].width == 960);
  CHECK(plan.items[0].height == 600);
  // The SAME derivation at the larger output — not a post-hoc scale of the 1x camera.
  CHECK(plan.items[0].render_camera == viewport_camera_for_shot(frame, 320, 200, 960, 600));
  // Anti-vacuity: it is genuinely NOT the native camera.
  CHECK_FALSE(plan.items[0].render_camera == viewport_camera_for_shot(frame, 320, 200, 320, 200));

  // A non-positive scale is clamped to 1x rather than producing a degenerate item.
  ExportOptions zero = options;
  zero.scale = 0;
  const ExportPlan clamped = plan_export(*doc.document, {a}, zero, real_shot_camera());
  REQUIRE(clamped.items.size() == 1);
  CHECK(clamped.items[0].width == 320);
  CHECK(clamped.items[0].height == 200);
}

TEST_CASE("export: the byte budget refuses per item, not per batch") {
  CameraDoc doc;
  const arbc::ObjectId a = doc.add("Small A", 64, 64);
  // 20000 x 20000 x 4 = 1.6 GB — past k_max_export_bytes even at 1x.
  const arbc::ObjectId big = doc.add("Terapixel", 20000, 20000);
  const arbc::ObjectId c = doc.add("Small B", 32, 32);

  ExportOptions options;
  options.destination = k_dest;
  const ExportPlan plan = plan_export(*doc.document, {a, big, c}, options, real_shot_camera());

  REQUIRE(plan.items.size() == 3); // three items — the refusal does not remove one
  CHECK_FALSE(plan.items[0].refused);
  CHECK(plan.items[1].refused);
  CHECK_FALSE(plan.items[2].refused);
  CHECK(plan.items[1].message.find("Terapixel") != std::string::npos);
  CHECK(plan.items[1].message.find("20000x20000") != std::string::npos);

  // The other two still render and write; the batch is not killed.
  RecordingFileSystem fs;
  ExportRunner runner;
  runner.render = stub_renderer();
  runner.filesystem = &fs;
  const ExportReport report = run_export(plan, options, runner);
  CHECK(report.written == 2);
  CHECK(report.refused == 1);
  CHECK(report.failed == 0);
  CHECK(report.state == ExportState::Finished);
  CHECK(fs.writes.size() == 2);
}

TEST_CASE("export: an empty tick-list is refused, not guessed") {
  CameraDoc doc;
  doc.add("Hero", 32, 32);

  ExportOptions options;
  options.destination = k_dest;
  const ExportPlan plan = plan_export(*doc.document, {}, options, real_shot_camera());
  CHECK(plan.items.empty());
  CHECK_FALSE(plan.reason.empty());

  RecordingFileSystem fs;
  ExportRunner runner;
  runner.render = stub_renderer();
  runner.filesystem = &fs;
  const ExportReport report = run_export(plan, options, runner);
  CHECK(report.state == ExportState::Failed);
  CHECK(report.written == 0);
  CHECK(fs.writes.empty()); // NOTHING was written
  CHECK(report.reason == plan.reason);

  // Likewise an unresolved destination and an unbound derivation.
  ExportOptions no_dest;
  CHECK_FALSE(
      plan_export(*doc.document, {doc.add("X", 8, 8)}, no_dest, real_shot_camera()).reason.empty());
  CHECK_FALSE(plan_export(*doc.document, {doc.add("Y", 8, 8)}, options, {}).reason.empty());
}

// ---- the run ----------------------------------------------------------------

TEST_CASE("export: run_export reports every outcome as a value") {
  CameraDoc doc;
  const arbc::ObjectId a = doc.add("Alpha", 8, 8);
  const arbc::ObjectId b = doc.add("Beta", 8, 8);
  const arbc::ObjectId c = doc.add("Gamma", 8, 8);

  ExportOptions options;
  options.destination = k_dest;
  const ExportPlan plan = plan_export(*doc.document, {a, b, c}, options, real_shot_camera());
  REQUIRE(plan.items.size() == 3);

  RecordingFileSystem fs;
  fs.fail_for = plan.items[1].path;
  ExportRunner runner;
  runner.render = stub_renderer();
  runner.filesystem = &fs;
  runner.revision = [] { return std::uint64_t{7}; };

  const ExportReport report = run_export(plan, options, runner); // nothing throws
  REQUIRE(report.items.size() == 3);
  CHECK(report.items[0].written);
  CHECK_FALSE(report.items[1].written);
  CHECK_FALSE(report.items[1].message.empty());
  CHECK(report.items[1].message.find("Beta") != std::string::npos);
  CHECK(report.items[2].written);
  CHECK(report.written == 2);
  CHECK(report.failed == 1);
  CHECK(report.state == ExportState::Finished);
  CHECK(report.items[0].bytes > 0);
  // D-export-8: a still document reports no change.
  CHECK(report.start_revision == 7);
  CHECK(report.end_revision == 7);
  CHECK_FALSE(report.document_changed_during_export);

  // A renderer that degrades to an empty image (render_offline's error path) is a
  // per-item failure with a message, not a crash and not a written file.
  RecordingFileSystem empty_fs;
  ExportRunner broken = runner;
  broken.filesystem = &empty_fs;
  broken.render = [](const arbc::Affine&, int, int, const std::optional<Rgba8>&) {
    return Srgb8Image{};
  };
  const ExportReport failed = run_export(plan, options, broken);
  CHECK(failed.failed == 3);
  CHECK(failed.written == 0);
  CHECK(empty_fs.writes.empty());
}

TEST_CASE("export: a mid-batch document edit is reported, not silently mixed in") {
  CameraDoc doc;
  const arbc::ObjectId a = doc.add("Alpha", 8, 8);
  const arbc::ObjectId b = doc.add("Beta", 8, 8);
  ExportOptions options;
  options.destination = k_dest;
  const ExportPlan plan = plan_export(*doc.document, {a, b}, options, real_shot_camera());

  RecordingFileSystem fs;
  std::uint64_t revision = 3;
  ExportRunner runner;
  runner.render = stub_renderer();
  runner.filesystem = &fs;
  runner.revision = [&revision] { return revision++; }; // "moved" between the two reads

  const ExportReport report = run_export(plan, options, runner);
  CHECK(report.start_revision == 3);
  CHECK(report.end_revision == 4);
  CHECK(report.document_changed_during_export);
}

TEST_CASE("export: progress is monotone and terminal") {
  CameraDoc doc;
  std::vector<arbc::ObjectId> ids;
  for (int i = 0; i < 5; ++i) {
    ids.push_back(doc.add("Cam " + std::to_string(i), 8, 8));
  }
  ExportOptions options;
  options.destination = k_dest;
  const ExportPlan plan = plan_export(*doc.document, ids, options, real_shot_camera());
  REQUIRE(plan.items.size() == 5);

  RecordingFileSystem fs;
  std::vector<ExportProgress> published;
  ExportRunner runner;
  runner.render = stub_renderer();
  runner.filesystem = &fs;
  runner.publish = [&published](const ExportProgress& p) { published.push_back(p); };

  run_export(plan, options, runner);

  REQUIRE_FALSE(published.empty());
  std::size_t terminal = 0;
  std::size_t last_done = 0;
  for (const ExportProgress& p : published) {
    CHECK(p.total == 5);
    CHECK(p.done >= last_done); // non-decreasing
    last_done = p.done;
    if (p.state == ExportState::Finished || p.state == ExportState::Cancelled ||
        p.state == ExportState::Failed) {
      ++terminal;
    }
  }
  CHECK(terminal == 1);
  CHECK(published.back().state == ExportState::Finished);
  CHECK(published.back().done == 5);
}

TEST_CASE("export: cancel takes effect between items and leaves complete files") {
  CameraDoc doc;
  std::vector<arbc::ObjectId> ids;
  for (int i = 0; i < 5; ++i) {
    ids.push_back(doc.add("Cam " + std::to_string(i), 8, 8));
  }
  ExportOptions options;
  options.destination = k_dest;
  const ExportPlan plan = plan_export(*doc.document, ids, options, real_shot_camera());

  RecordingFileSystem fs;
  std::atomic<bool> cancel{false};
  int calls = 0;
  ExportRunner runner;
  runner.filesystem = &fs;
  runner.cancel = &cancel;
  runner.render = [&calls, &cancel](const arbc::Affine&, int width, int height,
                                    const std::optional<Rgba8>&) {
    // Ask to stop DURING the 2nd item: a started item still finishes, so its file is
    // whole (Constraint 10 — `render_offline` exposes no cancellation hook).
    if (++calls == 2) {
      cancel.store(true);
    }
    Srgb8Image image;
    image.width = width;
    image.height = height;
    image.pixels.assign(static_cast<std::size_t>(width) * height * 4, 255);
    return image;
  };

  const ExportReport report = run_export(plan, options, runner);
  CHECK(report.state == ExportState::Cancelled);
  CHECK(report.written == 2);
  CHECK(report.items.size() == 2);
  CHECK(fs.writes.size() == 2); // no third write
  // Both written files are complete, signature-valid PNGs.
  for (const auto& [path, contents] : fs.files) {
    const std::vector<std::uint8_t> bytes(contents.begin(), contents.end());
    const PngInfo info = walk_png(bytes);
    INFO(path.string());
    CHECK(info.structure_ok);
    CHECK(info.crcs_ok);
  }
}

// ---- the encoder ------------------------------------------------------------

TEST_CASE("export: encode_png produces a structurally valid PNG") {
  Srgb8Image image;
  image.width = 5;
  image.height = 3;
  image.pixels.resize(static_cast<std::size_t>(5) * 3 * 4);
  for (std::size_t i = 0; i < image.pixels.size(); ++i) {
    image.pixels[i] = static_cast<std::uint8_t>(i * 7);
  }

  const std::vector<std::uint8_t> png = encode_png(image);
  REQUIRE_FALSE(png.empty());
  const PngInfo info = walk_png(png);
  CHECK(info.signature);
  CHECK(info.structure_ok);
  CHECK(info.crcs_ok); // a correct CRC32 on EVERY chunk
  CHECK(info.has_ihdr);
  CHECK(info.has_iend);
  CHECK(info.width == 5);
  CHECK(info.height == 3);
  CHECK(info.bit_depth == 8);
  CHECK(info.color_type == 6); // RGBA
  CHECK(info.idat_bytes > 0);

  // Degenerate inputs are an empty vector, never a malformed file.
  CHECK(encode_png(Srgb8Image{}).empty());
  CHECK(encode_png(Srgb8Image{0, 0, {}}).empty());
  CHECK(encode_png(Srgb8Image{4, 4, std::vector<std::uint8_t>(10, 0)}).empty()); // size mismatch
  CHECK(encode_png(Srgb8Image{-1, 4, {}}).empty());
}

// ---- the goldens (docs/01-architecture.md :300, D-export-11) ------------------

TEST_CASE("export: render-through-camera at the camera's own resolution is golden") {
  const std::unique_ptr<arbc::Document> doc = build_export_doc();
  // The camera cameras/model.md:286-290 reserved for THIS leaf: a 64x64 shot, framed,
  // rendered at its OWN resolution.
  const arbc::Affine frame = arbc::Affine::translation(-4.0, -4.0);
  const arbc::Affine camera = viewport_camera_for_shot(frame, 64, 64, 64, 64);

  const ace::render::Srgb8Image image = ace::render::render_document_srgb8(*doc, 64, 64, camera);
  REQUIRE(image.pixels.size() == static_cast<std::size_t>(64) * 64 * 4);
  CHECK(ace::render::frame_has_content(image));
  CHECK(ace_test::compare_golden("export_camera_64x64.rgba8", image.pixels));

  // The export path adds no pixel of its own at N == 1 with a TRANSPARENT background:
  // the renderer callable the L4 shell binds returns the identical bytes.
  const ace::commands::RenderFn shipped =
      [&doc](const arbc::Affine& cam, int width, int height,
             const std::optional<Rgba8>& background) -> ace::render::Srgb8Image {
    if (!background) {
      return ace::render::render_document_srgb8(*doc, width, height, cam);
    }
    return ace::render::render_document_srgb8_over(
        *doc, width, height, cam, {background->r, background->g, background->b, background->a});
  };
  CHECK(shipped(camera, 64, 64, std::nullopt).pixels == image.pixels);

  // …and the encoded PNG is byte-exact too (legitimate because the encoder is a
  // CHECKED-IN header with pinned settings, not a fetched dependency).
  const std::vector<std::uint8_t> png = encode_png(image);
  const PngInfo info = walk_png(png);
  CHECK(info.structure_ok);
  CHECK(info.crcs_ok);
  CHECK(info.width == 64);
  CHECK(info.height == 64);
  CHECK(ace_test::compare_golden("export_camera_64x64.png", png));
}

TEST_CASE("export: the filled background composites in the linear working space") {
  const std::unique_ptr<arbc::Document> doc = build_export_doc();
  const arbc::Affine frame = arbc::Affine::translation(-4.0, -4.0);
  const arbc::Affine camera = viewport_camera_for_shot(frame, 64, 64, 64, 64);

  const ace::render::Srgb8Image transparent =
      ace::render::render_document_srgb8(*doc, 64, 64, camera);
  const std::array<std::uint8_t, 4> white{255, 255, 255, 255};
  const ace::render::Srgb8Image filled =
      ace::render::render_document_srgb8_over(*doc, 64, 64, camera, white);
  REQUIRE(filled.pixels.size() == transparent.pixels.size());
  CHECK(ace_test::compare_golden("export_filled_bg_64x64.rgba8", filled.pixels));

  // An opaque background yields a fully opaque image (what the panel's report surfaces).
  for (std::size_t i = 3; i < filled.pixels.size(); i += 4) {
    REQUIRE(filled.pixels[i] == 255);
  }

  // ANTI-VACUITY (D-export-5): a NAIVE sRGB8-space blend of the same inputs differs
  // from the golden at a partially-transparent edge pixel. An implementation that
  // composites in gamma space therefore cannot pass this case.
  std::size_t edge = transparent.pixels.size();
  for (std::size_t i = 3; i < transparent.pixels.size(); i += 4) {
    const std::uint8_t alpha = transparent.pixels[i];
    if (alpha > 16 && alpha < 239) {
      edge = i - 3;
      break;
    }
  }
  REQUIRE(edge < transparent.pixels.size()); // the fixture really has a soft edge
  const double alpha = transparent.pixels[edge + 3] / 255.0;
  bool differs = false;
  for (std::size_t ch = 0; ch < 3; ++ch) {
    const double src = transparent.pixels[edge + ch];
    const double naive = src * alpha + 255.0 * (1.0 - alpha);
    const auto naive8 = static_cast<int>(naive + 0.5);
    if (std::abs(naive8 - static_cast<int>(filled.pixels[edge + ch])) > 1) {
      differs = true;
    }
  }
  CHECK(differs);
}

// ---- the async job (D-export-7) ---------------------------------------------

TEST_CASE("export: the service publishes progress immutably while a reader loads it") {
  CameraDoc doc;
  std::vector<arbc::ObjectId> ids;
  for (int i = 0; i < 12; ++i) {
    ids.push_back(doc.add("Cam " + std::to_string(i), 8, 8));
  }
  ExportOptions options;
  options.destination = k_dest;

  RecordingFileSystem fs;
  ace::platform::NativeThreads threads;
  ExportService service(threads, fs);
  service.set_shot_camera(real_shot_camera());
  service.set_renderer(stub_renderer());

  // A snapshot taken before the run keeps its values across every later publication —
  // refresh REPLACES the pointer, never mutates the pointee (A18).
  const std::shared_ptr<const ExportProgress> before = service.progress();
  REQUIRE(before != nullptr);
  CHECK(before->state == ExportState::Idle);

  std::atomic<bool> stop{false};
  std::atomic<int> reads{0};
  std::atomic<int> torn{0};
  // A reader thread load()ing continuously while the worker publishes: lock-free,
  // any-thread, and TSan-clean by construction. Catch2's RunContext is single-threaded,
  // so the per-read verdict is accumulated here and asserted on the main thread after
  // the join — asserting in-thread would race Catch2's own state, not the service's.
  auto reader = threads.spawn([&service, &stop, &reads, &torn] {
    while (!stop.load(std::memory_order_acquire)) {
      const std::shared_ptr<const ExportProgress> snapshot = service.progress();
      if (snapshot) {
        // One loaded pointer = one self-consistent generation for the whole read.
        if (snapshot->done > snapshot->total) {
          torn.fetch_add(1);
        }
        reads.fetch_add(1);
      }
    }
  });

  REQUIRE(service.start(plan_export(*doc.document, ids, options, real_shot_camera()), options));
  CHECK_FALSE(service.start(ExportPlan{}, options)); // one job at a time
  service.join();
  stop.store(true, std::memory_order_release);
  reader->join();

  CHECK(torn.load() == 0);
  CHECK(reads.load() > 0);
  CHECK(before->state == ExportState::Idle); // immutable after publication
  CHECK(before->total == 0);
  const std::shared_ptr<const ExportProgress> after = service.progress();
  CHECK(after->state == ExportState::Finished);
  CHECK(after->done == 12);
  const std::shared_ptr<const ExportReport> report = service.report();
  REQUIRE(report != nullptr);
  CHECK(report->written == 12);
  CHECK_FALSE(service.running());
}

TEST_CASE("export: a job still running at teardown is joined before the document dies") {
  // Constraint 8: the export thread renders the ONE owned Document, so its join must
  // happen INSIDE the scope enclosing that document's lifetime. Moving the join out
  // turns this case into a use-after-free under ASan.
  RecordingFileSystem fs;
  ace::platform::NativeThreads threads;
  std::atomic<bool> worker_running{false};
  std::atomic<int> rendered{0};

  {
    CameraDoc doc;
    std::vector<arbc::ObjectId> ids;
    for (int i = 0; i < 24; ++i) {
      ids.push_back(doc.add("Cam " + std::to_string(i), 8, 8));
    }
    ExportOptions options;
    options.destination = k_dest;

    {
      ExportService service(threads, fs);
      service.set_shot_camera(real_shot_camera());
      // The renderer READS the document — exactly what makes a missed join fatal.
      service.set_renderer([&doc, &worker_running, &rendered](const arbc::Affine&, int width,
                                                              int height,
                                                              const std::optional<Rgba8>&) {
        worker_running.store(true);
        rendered.fetch_add(1);
        (void)ace::scene::cameras(*doc.document);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Srgb8Image image;
        image.width = width;
        image.height = height;
        image.pixels.assign(static_cast<std::size_t>(width) * height * 4, 128);
        return image;
      });
      REQUIRE(service.start(plan_export(*doc.document, ids, options, real_shot_camera()), options));
      // Wait until the worker is genuinely IN the document — the shell's frame loop is
      // what does this in production. Then leave the scope with the job in flight:
      // ~ExportService cancels between items and JOINS (never detaches).
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
      while (rendered.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      REQUIRE(rendered.load() > 0);
      CHECK(service.running());
    }
    CHECK(worker_running.load());
    CHECK_FALSE(rendered.load() == 0);
    // Nothing is still reading `doc` here — the join already happened.
  } // the Document is released only now

  CHECK(rendered.load() > 0);
}

// editor.cameras.export_destination_reseed (D-reseed-3): the identity property the
// panel's destination re-seed keys on. The reset in `draw_export` fires when
// `panel.owner != service.instance()`, so `instance()` MUST be unique per constructed
// service and immune to address reuse — the exact failure the pre-contact_sheet pointer
// key fell to. Pinned headlessly here so a regression to a constant/reused id fails
// deterministically, without depending on the allocator reproducing an address collision.
TEST_CASE("export: distinct services carry distinct instance ids") {
  RecordingFileSystem fs;
  ace::platform::NativeThreads threads;

  // Two live services: their ids are non-zero (0 is the panel's "bound to nothing yet")
  // and distinct — `next_instance()` starts at 1 and never repeats.
  ExportService a(threads, fs);
  ExportService b(threads, fs);
  CHECK(a.instance() != 0);
  CHECK(b.instance() != 0);
  CHECK(a.instance() != b.instance());

  // Address-reuse guard: the precise false-match the pointer key fell to. Build a
  // service on the heap, record its id and address, DESTROY it, then build a second on
  // the heap — the allocator is free to hand the freed bytes straight back. A pointer
  // comparison would then report "same panel"; the monotonic id must still differ.
  auto first = std::make_unique<ExportService>(threads, fs);
  const std::uint64_t first_id = first->instance();
  const ExportService* first_addr = first.get();
  first.reset();
  auto second = std::make_unique<ExportService>(threads, fs);
  const std::uint64_t second_id = second->instance();
  const ExportService* second_addr = second.get();

  // Surface whether the reuse was actually exercised this run (the property holds either
  // way, so the assertion below is not gated on the nondeterministic collision).
  const bool address_reused = (first_addr == second_addr);
  CAPTURE(address_reused);
  CHECK(second_id != 0);
  CHECK(first_id != second_id); // immune to reuse, whether or not the addresses matched
}

TEST_CASE("export: the service refuses to start without a renderer and joins cleanly") {
  RecordingFileSystem fs;
  ace::platform::NativeThreads threads;
  ExportService service(threads, fs);
  service.set_shot_camera(real_shot_camera());
  CHECK_FALSE(service.start(ExportPlan{}, ExportOptions{})); // no renderer installed
  CHECK_FALSE(service.running());
  CHECK(service.report() == nullptr);
  service.join(); // idempotent with nothing ever started
  CHECK_FALSE(service.can_pick_destination());

  bool picked = false;
  service.pick_destination([&picked](std::optional<std::filesystem::path>) { picked = true; });
  CHECK_FALSE(picked); // no picker installed → a no-op, never a crash
  service.set_destination_picker(
      [](const std::function<void(std::optional<std::filesystem::path>)>& on_pick) {
        on_pick(std::filesystem::path("/tmp/elsewhere"));
      });
  CHECK(service.can_pick_destination());
  std::filesystem::path chosen;
  service.pick_destination([&chosen](std::optional<std::filesystem::path> dir) {
    if (dir) {
      chosen = *dir;
    }
  });
  CHECK(chosen == std::filesystem::path("/tmp/elsewhere"));
}
