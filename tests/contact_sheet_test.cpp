// editor.cameras.contact_sheet — L1 headless Catch2 units for the sheet's layout,
// caption face and composition (D14, A21), plus the composed-sheet golden.
//
// The harness is `tests/export_test.cpp`'s, reused deliberately: the same recording
// fake filesystem and the same stub-renderer shape, with the two impure steps INJECTED
// (D-export-1) so the whole grid/fit/caption/refusal/cancel/progress matrix sits inside
// headless Catch2 reach. The golden runs GL-free through `render::render_document_srgb8`,
// bound exactly as `src/app/shell.cpp:385-394` binds it.
//
// The load-bearing law here is that composition COPIES (D-sheet-2): every tile rect is
// asserted byte-identical to an independent render of the same camera at the same fitted
// size, so any filter, premultiply or blend introduced anywhere in the path breaks these
// cases immediately rather than silently changing the picture.
#include <ace/base/image.hpp>
#include <ace/commands/contact_sheet.hpp>
#include <ace/commands/export.hpp>
#include <ace/interact/interact.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/platform/result.hpp>
#include <ace/platform/threads.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_raster/raster_content.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/media/pixel_traits.hpp>
#include <arbc/media/surface_format.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "golden_support.hpp"

using ace::commands::compose_contact_sheet;
using ace::commands::contact_sheet_cell;
using ace::commands::contact_sheet_layout;
using ace::commands::ContactCellRect;
using ace::commands::ContactSheetLayout;
using ace::commands::ContactSheetPlan;
using ace::commands::ContactTile;
using ace::commands::draw_text;
using ace::commands::ExportOptions;
using ace::commands::ExportPlan;
using ace::commands::ExportProgress;
using ace::commands::ExportReport;
using ace::commands::ExportRunner;
using ace::commands::ExportService;
using ace::commands::ExportState;
using ace::commands::fit_text;
using ace::commands::k_contact_gutter;
using ace::commands::k_contact_tile_max;
using ace::commands::k_contact_tile_min;
using ace::commands::k_glyph_cell_height;
using ace::commands::k_replacement_code_point;
using ace::commands::next_code_point;
using ace::commands::plan_contact_sheet;
using ace::commands::plan_export;
using ace::commands::Rgba8;
using ace::commands::run_export;
using ace::commands::Srgb8Image;
using ace::commands::text_width;
using ace::interact::viewport_camera_for_shot;

namespace {

const std::filesystem::path k_dest =
    std::filesystem::temp_directory_path() / "ace_contact_sheet_dest";

ace::commands::ShotCameraFn real_shot_camera() { return &viewport_camera_for_shot; }

// ---- fixtures ---------------------------------------------------------------

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

// The `tests/look_through_test.cpp:37-59` recipe: a full-frame green backdrop under a
// BOUNDED 16x16 red raster at (8,8), so a camera framing a sub-region renders genuinely
// different pixels from the whole-document view — which is what makes the golden's
// per-tile anti-vacuity comparison meaningful rather than three copies of one image.
std::unique_ptr<arbc::Document> build_sheet_doc() {
  auto doc = std::make_unique<arbc::Document>();
  const arbc::ObjectId root = doc->add_composition(64.0, 64.0);
  const arbc::ObjectId bg =
      doc->add_content(std::make_shared<arbc::SolidContent>(arbc::Rgba{0.0F, 0.35F, 0.0F, 1.0F}));
  doc->attach_layer(root, doc->add_layer(bg, arbc::Affine::identity()));
  arbc::DecodedImage img;
  img.width = 16;
  img.height = 16;
  img.format = arbc::k_working_rgba32f;
  img.bytes.resize(static_cast<std::size_t>(16) * 16 * 4 * sizeof(float));
  auto* fp = reinterpret_cast<float*>(img.bytes.data());
  for (int i = 0; i < 16 * 16; ++i) { // opaque red, premultiplied linear
    fp[i * 4] = 0.6F;
    fp[i * 4 + 1] = 0.0F;
    fp[i * 4 + 2] = 0.0F;
    fp[i * 4 + 3] = 1.0F;
  }
  const arbc::ObjectId raster =
      doc->add_content(std::make_shared<arbc::RasterContent>(std::move(img)));
  doc->attach_layer(root, doc->add_layer(raster, arbc::Affine::translation(8.0, 8.0)));
  return doc;
}

// ---- test doubles -----------------------------------------------------------

class RecordingFileSystem final : public ace::platform::FileSystem {
public:
  mutable std::map<std::filesystem::path, std::string> files;
  mutable std::vector<std::filesystem::path> writes;

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
    files[path] = std::string(contents);
    return {};
  }
  std::error_code make_directories(const std::filesystem::path&) const override { return {}; }
  std::error_code atomic_replace(const std::filesystem::path& path,
                                 std::string_view contents) const override {
    return write_file(path, contents);
  }
  // In-memory prefix erase (A26). A contact sheet never deletes, so this exists to satisfy the
  // pure virtual — and it stays honest rather than becoming a silent no-op.
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

// A renderer whose pixels are a KNOWN per-pixel gradient, keyed by the requested size
// so no two tiles can accidentally agree. `memcmp`-ing a tile rect against a re-run of
// this function is what turns "composition never blends" into an assertion.
Srgb8Image gradient_render(int width, int height, int key) {
  Srgb8Image image;
  image.width = width;
  image.height = height;
  if (width <= 0 || height <= 0) {
    return image;
  }
  image.pixels.resize(static_cast<std::size_t>(width) * height * 4);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::size_t at = (static_cast<std::size_t>(y) * width + x) * 4;
      image.pixels[at] = static_cast<std::uint8_t>((x * 7 + key * 31) & 0xFF);
      image.pixels[at + 1] = static_cast<std::uint8_t>((y * 5 + key * 17) & 0xFF);
      image.pixels[at + 2] = static_cast<std::uint8_t>((x + y + key) & 0xFF);
      image.pixels[at + 3] = 200;
    }
  }
  return image;
}

// A well-formed uniform render, enough for the report/progress/cancel matrix.
ace::commands::RenderFn stub_renderer() {
  // The stub ignores the batch pin (D-pinned-1) — a leading unused `DocStatePtr`.
  return [](const arbc::DocStatePtr&, const arbc::Affine&, int width, int height,
            const std::optional<Rgba8>& background) {
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

Rgba8 pixel_at(const Srgb8Image& image, int x, int y) {
  const std::size_t at = (static_cast<std::size_t>(y) * image.width + x) * 4;
  return Rgba8{image.pixels[at], image.pixels[at + 1], image.pixels[at + 2], image.pixels[at + 3]};
}

// ---- caption-face probes (editor.cameras.caption_latin1) ---------------------

// The test's OWN UTF-8 encoder, so `next_code_point` is never validated against itself.
std::string utf8(char32_t code_point) {
  const auto byte = [](unsigned int value) {
    return static_cast<char>(static_cast<unsigned char>(value));
  };
  std::string out;
  if (code_point < 0x80U) {
    out.push_back(byte(code_point));
  } else if (code_point < 0x800U) {
    out.push_back(byte(0xC0U | (code_point >> 6)));
    out.push_back(byte(0x80U | (code_point & 0x3FU)));
  } else if (code_point < 0x10000U) {
    out.push_back(byte(0xE0U | (code_point >> 12)));
    out.push_back(byte(0x80U | ((code_point >> 6) & 0x3FU)));
    out.push_back(byte(0x80U | (code_point & 0x3FU)));
  } else {
    out.push_back(byte(0xF0U | (code_point >> 18)));
    out.push_back(byte(0x80U | ((code_point >> 12) & 0x3FU)));
    out.push_back(byte(0x80U | ((code_point >> 6) & 0x3FU)));
    out.push_back(byte(0x80U | (code_point & 0x3FU)));
  }
  return out;
}

// How many U+FFFD a decode of `text` yields — the measure the truncation law's
// "introduces no replacement the input did not have" is stated against.
int replacements(std::string_view text) {
  int count = 0;
  std::size_t pos = 0;
  while (pos < text.size()) {
    if (next_code_point(text, pos) == k_replacement_code_point) {
      ++count;
    }
  }
  return count;
}

bool rects_intersect(const ContactCellRect& a, const ContactCellRect& b) {
  return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

// Render every tile of `plan` through `render`, in plan order — what `run_export`'s
// sheet phase does, hoisted so the composition cases can drive it directly.
std::vector<Srgb8Image> render_tiles(const ContactSheetPlan& plan,
                                     const ace::commands::RenderFn& render,
                                     const std::optional<Rgba8>& background) {
  std::vector<Srgb8Image> renders;
  renders.reserve(plan.tiles.size());
  for (const ContactTile& tile : plan.tiles) {
    // The compose-logic cases render against the 2-arg path (a null pin the closures
    // ignore); run_export's own pinning is exercised by export_test.cpp / canvas_host_test.
    renders.push_back(
        render(arbc::DocStatePtr{}, tile.render_camera, tile.width, tile.height, background));
  }
  return renders;
}

} // namespace

// ---- the grid (D-sheet-3) ----------------------------------------------------

TEST_CASE("contact_sheet: the grid is ceil(sqrt(N)) columns with the rows that follow") {
  for (int n = 1; n <= 17; ++n) {
    INFO("N = " << n);
    const ContactSheetLayout layout = contact_sheet_layout(n, 96);
    int expect_cols = 1;
    while (expect_cols * expect_cols < n) {
      ++expect_cols;
    }
    CHECK(layout.cols == expect_cols);
    CHECK(layout.rows == (n + layout.cols - 1) / layout.cols);
    CHECK(layout.rows * layout.cols >= n);
    // No wholly empty trailing row: dropping the last row would no longer hold N.
    CHECK((layout.rows - 1) * layout.cols < n);
    CHECK(layout.width == k_contact_gutter + layout.cols * (layout.cell_width + k_contact_gutter));
    CHECK(layout.height ==
          k_contact_gutter + layout.rows * (layout.cell_height + k_contact_gutter));
  }

  // `tile_edge` is the ONLY knob, and it is clamped rather than trusted.
  CHECK(contact_sheet_layout(4, 1).tile_edge == k_contact_tile_min);
  CHECK(contact_sheet_layout(4, 1 << 20).tile_edge == k_contact_tile_max);
  CHECK(contact_sheet_layout(4, 96).tile_edge == 96);
  // A degenerate count is all zeroes, never a division by cols == 0.
  CHECK(contact_sheet_layout(0, 96) == ContactSheetLayout{});
  CHECK(contact_sheet_layout(-3, 96) == ContactSheetLayout{});
  CHECK(contact_sheet_cell(contact_sheet_layout(4, 96), 9) == ContactCellRect{});
  CHECK(contact_sheet_cell(contact_sheet_layout(4, 96), -1) == ContactCellRect{});
}

TEST_CASE("contact_sheet: tile boxes tile the sheet without overlap and stay inside it") {
  for (int n = 1; n <= 17; ++n) {
    const ContactSheetLayout layout = contact_sheet_layout(n, 96);
    std::vector<ContactCellRect> cells;
    for (int i = 0; i < n; ++i) {
      cells.push_back(contact_sheet_cell(layout, i));
    }
    for (int i = 0; i < n; ++i) {
      INFO("N = " << n << ", cell " << i);
      CHECK(cells[i].x >= 0);
      CHECK(cells[i].y >= 0);
      CHECK(cells[i].x + cells[i].width <= layout.width);
      CHECK(cells[i].y + cells[i].height <= layout.height);
      for (int j = i + 1; j < n; ++j) {
        INFO("vs cell " << j);
        CHECK_FALSE(rects_intersect(cells[i], cells[j]));
      }
    }
    // Exactly one gutter between neighbours, horizontally and vertically.
    if (n > 1 && layout.cols > 1) {
      CHECK(cells[1].x - (cells[0].x + cells[0].width) == k_contact_gutter);
    }
    if (layout.rows > 1) {
      const ContactCellRect below = contact_sheet_cell(layout, layout.cols);
      CHECK(below.y - (cells[0].y + cells[0].height) == k_contact_gutter);
    }
  }
}

TEST_CASE("contact_sheet: each tile preserves its camera's aspect inside the common box") {
  CameraDoc doc;
  const arbc::Affine frame = arbc::Affine::translation(3.0, -2.0);
  const arbc::ObjectId wide = doc.add("Wide", 160, 90, frame);
  const arbc::ObjectId square = doc.add("Square", 64, 64, frame);
  const arbc::ObjectId tall = doc.add("Tall", 90, 160, frame);

  ExportOptions options;
  options.destination = k_dest;
  options.tile_edge = 96;
  const ContactSheetPlan plan =
      plan_contact_sheet(*doc.document, {wide, square, tall}, options, real_shot_camera(), {});
  REQUIRE(plan.tiles.size() == 3);
  CHECK_FALSE(plan.refused);
  CHECK(plan.reason.empty());

  struct Expect {
    int native_w;
    int native_h;
  };
  const std::vector<Expect> expected = {{160, 90}, {64, 64}, {90, 160}};
  for (std::size_t i = 0; i < plan.tiles.size(); ++i) {
    const ContactTile& tile = plan.tiles[i];
    INFO("tile " << i);
    CHECK(tile.width >= 1);
    CHECK(tile.height >= 1);
    CHECK(std::max(tile.width, tile.height) == plan.tile_edge); // the long edge fills the box
    CHECK(tile.width <= plan.tile_edge);
    CHECK(tile.height <= plan.tile_edge);
    // The fitted aspect matches the camera's to within one pixel on the short edge.
    const int ideal = expected[i].native_w >= expected[i].native_h
                          ? (plan.tile_edge * expected[i].native_h + expected[i].native_w / 2) /
                                expected[i].native_w
                          : (plan.tile_edge * expected[i].native_w + expected[i].native_h / 2) /
                                expected[i].native_h;
    const int actual = expected[i].native_w >= expected[i].native_h ? tile.height : tile.width;
    CHECK(std::abs(actual - ideal) <= 1);
    // Centred with a floor-divided offset inside its own box.
    CHECK(tile.x == tile.box_x + (plan.tile_edge - tile.width) / 2);
    CHECK(tile.y == tile.box_y + (plan.tile_edge - tile.height) / 2);
    CHECK(tile.caption_x == tile.box_x);
    CHECK(tile.caption_y == tile.box_y + plan.tile_edge);
  }
  CHECK(plan.tiles[1].width == 96);
  CHECK(plan.tiles[1].height == 96); // 1:1 fills the box exactly
}

TEST_CASE(
    "contact_sheet: the render camera comes from the injected derivation at the FITTED size") {
  CameraDoc doc;
  const arbc::Affine frame{1.0, 0.0, 0.0, 1.0, 12.0, 7.0};
  const arbc::ObjectId wide = doc.add("Hero", 1920, 1080, frame);

  ExportOptions options;
  options.destination = k_dest;
  options.tile_edge = 96;
  const ContactSheetPlan plan =
      plan_contact_sheet(*doc.document, {wide}, options, real_shot_camera(), {});
  REQUIRE(plan.tiles.size() == 1);
  const ContactTile& tile = plan.tiles[0];
  CHECK(tile.width == 96);
  CHECK(tile.height == 54);
  // D-sheet-1: the SAME derivation the batch uses, at the tile's own output box.
  CHECK(tile.render_camera == viewport_camera_for_shot(frame, 1920, 1080, tile.width, tile.height));
  // ANTI-VACUITY: it is genuinely neither the native camera nor a square-box camera,
  // so a "derive once and reuse" implementation cannot pass.
  CHECK_FALSE(tile.render_camera == viewport_camera_for_shot(frame, 1920, 1080, 1920, 1080));
  CHECK_FALSE(tile.render_camera == viewport_camera_for_shot(frame, 1920, 1080, 96, 96));
}

// ---- composition (D-sheet-2) -------------------------------------------------

TEST_CASE("contact_sheet: composition never blends — a tile rect is byte-identical to its render") {
  CameraDoc doc;
  const arbc::Affine frame = arbc::Affine::translation(2.0, 3.0);
  const arbc::ObjectId a = doc.add("Wide", 160, 90, frame);
  const arbc::ObjectId b = doc.add("Square", 64, 64, frame);
  const arbc::ObjectId c = doc.add("Tall", 90, 160, frame);

  ExportOptions options;
  options.destination = k_dest;
  options.tile_edge = 96;
  const ContactSheetPlan plan =
      plan_contact_sheet(*doc.document, {a, b, c}, options, real_shot_camera(), {});
  REQUIRE(plan.tiles.size() == 3);

  std::vector<Srgb8Image> renders;
  for (std::size_t i = 0; i < plan.tiles.size(); ++i) {
    renders.push_back(
        gradient_render(plan.tiles[i].width, plan.tiles[i].height, static_cast<int>(i) + 1));
  }
  const Srgb8Image sheet = compose_contact_sheet(plan, renders, std::nullopt);
  REQUIRE(sheet.width == plan.width);
  REQUIRE(sheet.height == plan.height);
  REQUIRE(sheet.pixels.size() ==
          static_cast<std::size_t>(plan.width) * static_cast<std::size_t>(plan.height) * 4);

  for (std::size_t i = 0; i < plan.tiles.size(); ++i) {
    const ContactTile& tile = plan.tiles[i];
    INFO("tile " << i);
    for (int row = 0; row < tile.height; ++row) {
      const std::size_t src = static_cast<std::size_t>(row) * tile.width * 4;
      const std::size_t dst = (static_cast<std::size_t>(tile.y + row) * sheet.width + tile.x) * 4;
      // A COPY, byte for byte. Any blend, premultiply or filter breaks this line.
      REQUIRE(std::memcmp(&sheet.pixels[dst], &renders[i].pixels[src],
                          static_cast<std::size_t>(tile.width) * 4) == 0);
    }
  }
}

TEST_CASE(
    "contact_sheet: the sheet fill is exactly the chosen background, transparent by default") {
  CameraDoc doc;
  // Three cameras in a 2x2 grid, so the fourth slot is genuinely empty.
  const arbc::ObjectId a = doc.add("A", 64, 64);
  const arbc::ObjectId b = doc.add("B", 128, 64); // 2:1 — letterboxed inside its box
  const arbc::ObjectId c = doc.add("C", 64, 64);

  ExportOptions options;
  options.destination = k_dest;
  options.tile_edge = 64;
  const ContactSheetPlan plan =
      plan_contact_sheet(*doc.document, {a, b, c}, options, real_shot_camera(), {});
  REQUIRE(plan.tiles.size() == 3);
  REQUIRE(plan.cols == 2);
  REQUIRE(plan.rows == 2);

  const auto check_fill = [&](const std::optional<Rgba8>& background, Rgba8 expect) {
    std::vector<Srgb8Image> renders;
    for (std::size_t i = 0; i < plan.tiles.size(); ++i) {
      renders.push_back(gradient_render(plan.tiles[i].width, plan.tiles[i].height, 9));
    }
    const Srgb8Image sheet = compose_contact_sheet(plan, renders, background);
    REQUIRE(sheet.width == plan.width);
    // A gutter pixel.
    CHECK(pixel_at(sheet, 0, 0) == expect);
    CHECK(pixel_at(sheet, sheet.width - 1, sheet.height - 1) == expect);
    // A letterbox pixel inside the 2:1 tile's own box, above the fitted image.
    const ContactTile& wide = plan.tiles[1];
    REQUIRE(wide.y > wide.box_y);
    CHECK(pixel_at(sheet, wide.box_x + 1, wide.box_y) == expect);
    // The empty fourth slot — box AND caption strip. A 4-camera grid at the same tile
    // edge has the identical geometry, so its cell 3 IS the slot 3 cameras leave empty.
    const ContactCellRect empty = contact_sheet_cell(contact_sheet_layout(4, plan.tile_edge), 3);
    REQUIRE(empty.width == plan.tile_edge);
    for (int y = empty.y; y < empty.y + empty.height; ++y) {
      for (int x = empty.x; x < empty.x + empty.width; ++x) {
        REQUIRE(pixel_at(sheet, x, y) == expect);
      }
    }
  };
  check_fill(std::nullopt, Rgba8{0, 0, 0, 0});
  check_fill(Rgba8{18, 200, 33, 255}, Rgba8{18, 200, 33, 255});
}

TEST_CASE("contact_sheet: a degraded render leaves its tile at background, never a throw") {
  CameraDoc doc;
  const arbc::ObjectId a = doc.add("A", 64, 64);
  const arbc::ObjectId b = doc.add("B", 64, 64);
  ExportOptions options;
  options.destination = k_dest;
  options.tile_edge = 64;
  const ContactSheetPlan plan =
      plan_contact_sheet(*doc.document, {a, b}, options, real_shot_camera(), {});
  REQUIRE(plan.tiles.size() == 2);

  // Tile 0 renders; tile 1 comes back empty (render_offline's error path).
  std::vector<Srgb8Image> renders;
  renders.push_back(gradient_render(plan.tiles[0].width, plan.tiles[0].height, 3));
  renders.push_back(Srgb8Image{});
  const Srgb8Image sheet = compose_contact_sheet(plan, renders, std::nullopt);
  REQUIRE(sheet.width == plan.width);
  CHECK(pixel_at(sheet, plan.tiles[0].x + 4, plan.tiles[0].y + 4) !=
        Rgba8{0, 0, 0, 0}); // tile 0 landed
  CHECK(pixel_at(sheet, plan.tiles[1].x + 4, plan.tiles[1].y + 4) == Rgba8{0, 0, 0, 0});

  // Fewer renders than tiles, a wrongly-sized render, and a degenerate plan are all
  // values, not crashes.
  CHECK(compose_contact_sheet(plan, {}, std::nullopt).width == plan.width);
  CHECK(compose_contact_sheet(plan, {gradient_render(5, 5, 1), gradient_render(5, 5, 2)},
                              std::nullopt)
            .width == plan.width);
  CHECK(compose_contact_sheet(ContactSheetPlan{}, {}, std::nullopt).pixels.empty());
}

// ---- the caption face (A27, D-truetype_captions) -----------------------------

// The alpha channel over a TRANSPARENT scratch is the stb coverage of the drawn ink
// (premultiplied-linear `over` degenerates to `out_a = coverage`), so these probes read
// the antialiased face through the public `draw_text` surface, never a table.
Srgb8Image draw_on_transparent(std::string_view text, int scale, int width, int height) {
  Srgb8Image img;
  img.width = width;
  img.height = height;
  img.pixels.assign(static_cast<std::size_t>(width) * height * 4, 0);
  draw_text(img, 0, 0, text, scale);
  return img;
}

std::size_t ink_pixels(const Srgb8Image& img) {
  std::size_t n = 0;
  for (std::size_t i = 3; i < img.pixels.size(); i += 4) {
    if (img.pixels[i] != 0) {
      ++n;
    }
  }
  return n;
}

TEST_CASE("contact_sheet: the bundled face draws distinct antialiased glyphs") {
  const std::vector<std::string> glyphs = {"A", "a", "\xc3\x80", "g"}; // A, a, À, g (descender)
  std::vector<std::vector<std::uint8_t>> footprints;
  bool any_intermediate = false;
  for (const std::string& g : glyphs) {
    const Srgb8Image img = draw_on_transparent(g, 2, 32, k_glyph_cell_height * 2);
    std::vector<std::uint8_t> footprint(static_cast<std::size_t>(img.width) * img.height, 0);
    for (std::size_t p = 0; p < footprint.size(); ++p) {
      const std::uint8_t alpha = img.pixels[p * 4 + 3];
      footprint[p] = alpha != 0 ? std::uint8_t{1} : std::uint8_t{0};
      // The antialiasing signal a 1-bit table cannot produce: an edge pixel at an
      // intermediate intensity, neither background nor full ink.
      if (alpha != 0 && alpha != 255) {
        any_intermediate = true;
      }
    }
    INFO("glyph " << g);
    CHECK(ink_pixels(img) > 0); // every glyph draws something
    footprints.push_back(std::move(footprint));
  }
  CHECK(any_intermediate);
  // The four footprints differ pairwise — a real face, not one cell replicated.
  for (std::size_t i = 0; i < footprints.size(); ++i) {
    for (std::size_t j = i + 1; j < footprints.size(); ++j) {
      INFO("footprints " << glyphs[i] << " vs " << glyphs[j]);
      CHECK(footprints[i] != footprints[j]);
    }
  }
  // ANTI-VACUITY: an empty string draws nothing; "A" at scale 2 lights strictly more
  // pixels than at scale 1.
  CHECK(ink_pixels(draw_on_transparent("", 2, 32, k_glyph_cell_height * 2)) == 0);
  CHECK(ink_pixels(draw_on_transparent("A", 2, 32, k_glyph_cell_height * 2)) >
        ink_pixels(draw_on_transparent("A", 1, 32, k_glyph_cell_height * 2)));

  // A non-positive scale is a no-op, and an empty target is a no-op, not a crash.
  Srgb8Image strip = draw_on_transparent("Hero 1", 1, 200, k_glyph_cell_height * 2);
  const std::vector<std::uint8_t> before = strip.pixels;
  draw_text(strip, 0, 0, "Hero 1", 0);
  CHECK(strip.pixels == before);
  Srgb8Image degenerate;
  draw_text(degenerate, 0, 0, "Hero 1", 1);
  CHECK(degenerate.pixels.empty());
}

TEST_CASE("contact_sheet: an uncovered code point renders .notdef ink") {
  const int w = 16;
  const int h = k_glyph_cell_height * 2;
  // U+4E2D (中) is outside Inter's coverage; the malformed byte 0xFF decodes to U+FFFD,
  // itself uncovered. Both render the face's own `.notdef` box — non-zero ink
  // (Constraint 6, D-truetype_captions-5), so the missing glyph is the font's business.
  CHECK(ink_pixels(draw_on_transparent(utf8(0x4E2DU), 2, w, h)) > 0);
  CHECK(ink_pixels(draw_on_transparent("\xff", 2, w, h)) > 0);
  CHECK(replacements("\xff") == 1); // ... and 0xFF really did decode to U+FFFD
  // ANTI-VACUITY: a space at those columns is blank, so the `.notdef` box is proven
  // drawn, not assumed.
  CHECK(ink_pixels(draw_on_transparent(" ", 2, w, h)) == 0);
}

TEST_CASE("contact_sheet: the UTF-8 decoder is total and always advances") {
  struct Row {
    std::string bytes;
    std::vector<char32_t> expected;
  };
  const char32_t bad = k_replacement_code_point;
  // Well-formed rows first, then one row per ill-formed CLASS. The expectations follow
  // Unicode 3.9's maximal-subpart rule: a lead byte no sequence can start is a subpart
  // of one byte, and a lead whose continuation is out of range keeps only what it
  // accepted — so `C0 80` is TWO replacements (0xC0 can never lead), while `C3` alone
  // is one.
  const std::vector<Row> rows = {
      {"", {}},
      {"Hi!", {U'H', U'i', U'!'}},
      {std::string("a\0b", 3), {U'a', 0, U'b'}},  // an embedded NUL is a code point
      {"\xc3\xa9", {0x00E9}},                     // é
      {"\xe4\xb8\xad", {0x4E2D}},                 // 中
      {"\xf0\x9f\x98\x80", {0x1F600}},            // 😀
      {"\x7f", {0x7F}},                           // DEL decodes, it just does not map
      {"\xc3", {bad}},                            // truncated two-byte tail
      {"\xe4\xb8", {bad}},                        // truncated three-byte tail
      {"\xf0\x9f\x98", {bad}},                    // truncated four-byte tail
      {"\x80", {bad}},                            // a lone continuation byte
      {"\xc0\x80", {bad, bad}},                   // overlong two-byte form
      {"\xe0\x80\x80", {bad, bad, bad}},          // overlong three-byte form
      {"\xed\xa0\x80", {bad, bad, bad}},          // a surrogate encoding
      {"\xf5\x80\x80\x80", {bad, bad, bad, bad}}, // past U+10FFFF
      {"\xf4\x90\x80\x80", {bad, bad, bad, bad}}, // past U+10FFFF, valid lead
      {"\xfe", {bad}},                            // no UTF-8 sequence starts with FE
      {"\xff", {bad}},                            // ... or FF
      {"a\xc3", {U'a', bad}},                     // ill-formed tail after good text
      {"\xc3"
       "z",
       {bad, U'z'}}, // the offending byte is re-classified on its own terms
  };

  for (const Row& row : rows) {
    INFO("row of " << row.bytes.size() << " bytes");
    std::vector<char32_t> decoded;
    std::size_t pos = 0;
    while (pos < row.bytes.size()) {
      const std::size_t before = pos;
      decoded.push_back(next_code_point(row.bytes, pos));
      // ANTI-VACUITY / termination: `pos` STRICTLY increases on every call, so a
      // decoder that stalled would hang here rather than pass.
      REQUIRE(pos > before);
      REQUIRE(pos <= row.bytes.size());
    }
    CHECK(decoded == row.expected);
  }

  // ANTI-VACUITY: the well-formed rows produce NO replacement at all, so a decoder that
  // answered U+FFFD for everything fails this case instead of passing it.
  CHECK(replacements("Hi!") == 0);
  CHECK(replacements("\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80") == 0);
  CHECK(replacements("\xc3") == 1);

  // A call past the end still advances, so even misuse terminates.
  std::size_t past = 4;
  CHECK(next_code_point("ab", past) == k_replacement_code_point);
  CHECK(past == 5);
}

TEST_CASE("contact_sheet: a caption truncates on a code-point boundary") {
  const std::vector<std::string> corpus = {
      "Café Extérieur", "Ñandú", "ÀÉÎÕÜ çøæß", "Hero", "中文 mixed Ünicode",
  };
  bool truncated_somewhere = false;
  for (const std::string& name : corpus) {
    const int before = replacements(name);
    for (int scale = 1; scale <= 2; ++scale) {
      for (int max_width = 0; max_width <= 200; ++max_width) {
        INFO("name " << name << " scale " << scale << " width " << max_width);
        const std::string fitted = fit_text(name, max_width, scale);
        // The cut lands on a code-point boundary, so re-decoding introduces no U+FFFD
        // the input did not already have (D-latin1-5).
        CHECK(replacements(fitted) <= before);
        CHECK(text_width(fitted, scale) <= max_width);
        if (fitted != name && fitted.size() >= 3 && fitted.substr(fitted.size() - 3) == "...") {
          truncated_somewhere = true;
        }
      }
    }
  }
  CHECK(truncated_somewhere); // ANTI-VACUITY: the sweep really does truncate

  // A well-formed accented name in, well-formed UTF-8 out, at every width.
  const std::string cafe = "Café Extérieur";
  for (int max_width = 0; max_width <= 200; ++max_width) {
    INFO("width " << max_width);
    CHECK(replacements(fit_text(cafe, max_width, 1)) == 0);
  }
  // And the byte walk's off-by-one is gone: cutting between the two bytes of `é` would
  // leave a lone 0xC3 here.
  CHECK(fit_text("Café", 6 * 4, 1) == "Café");
  CHECK(replacements(fit_text("Café Extérieur", 6 * 8, 1)) == 0);
}

TEST_CASE("contact_sheet: caption width and fit follow the font advances") {
  // `text_width` is the sum of the face's scaled advances: zero for the empty string,
  // monotone non-decreasing as characters are appended, and growing with `scale`.
  CHECK(text_width("", 1) == 0);
  CHECK(text_width("Hero 1", 0) == 0);
  const std::string name = "Hero 1";
  int prev = 0;
  for (std::size_t n = 1; n <= name.size(); ++n) {
    const int width = text_width(name.substr(0, n), 1);
    INFO("prefix length " << n);
    CHECK(width >= prev);
    prev = width;
  }
  CHECK(text_width(name, 2) > text_width(name, 1));

  // `fit_text` never returns a caption wider than `max_width`; the [0,200] sweep both
  // truncates (with a trailing ellipsis) and leaves a short name untruncated.
  const std::string long_name = "A very long camera name that will not fit";
  bool truncated = false;
  bool untruncated = false;
  for (int max_width = 0; max_width <= 200; ++max_width) {
    INFO("max_width " << max_width);
    const std::string fitted = fit_text(long_name, max_width, 1);
    CHECK(text_width(fitted, 1) <= max_width);
    if (fitted.size() >= 3 && fitted.substr(fitted.size() - 3) == "...") {
      truncated = true;
    }
    if (fit_text("Hi", max_width, 1) == "Hi") {
      untruncated = true;
    }
  }
  CHECK(truncated);   // ANTI-VACUITY: the sweep really does truncate ...
  CHECK(untruncated); // ... and really does leave a short name whole.
  // A name that already fits is returned verbatim; a wider name truncates on a
  // code-point boundary, never mid-`é`.
  CHECK(fit_text("Caf\xc3\xa9", 200, 1) == "Caf\xc3\xa9");
  CHECK(replacements(fit_text("Caf\xc3\xa9 Ext\xc3\xa9rieur", 40, 1)) == 0);
}

TEST_CASE("contact_sheet: captions composite coverage in linear space") {
  // ONE isolated accented glyph (curved edges are plentiful, and no neighbour overlaps
  // its pixels) so every white-only edge pixel is a SINGLE coverage composite whose
  // reference is exact — a multi-glyph name would double-composite where edges meet.
  const std::string name = "\xc3\xa9"; // é
  const int scale = 3;
  const int w = 24;
  const int h = k_glyph_cell_height * scale + 4;

  // Draw the same glyph over TRANSPARENT and over an opaque MID-GREY. Over transparent, a
  // white-only pixel is `{255,255,255, coverage}` (premultiplied-linear `over` with an
  // un-premultiplied re-encode), so its alpha IS the stb coverage of that pixel.
  const Srgb8Image over_clear = draw_on_transparent(name, scale, w, h);
  Srgb8Image over_grey;
  over_grey.width = w;
  over_grey.height = h;
  over_grey.pixels.assign(static_cast<std::size_t>(w) * h * 4, 0);
  for (std::size_t i = 0; i < over_grey.pixels.size(); i += 4) {
    over_grey.pixels[i] = 128;
    over_grey.pixels[i + 1] = 128;
    over_grey.pixels[i + 2] = 128;
    over_grey.pixels[i + 3] = 255;
  }
  draw_text(over_grey, 0, 0, name, scale);

  const float bg_lin = arbc::srgb8_to_linear(128);
  bool checked_edge = false;
  bool partial_alpha_over_clear = false;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const Rgba8 clear = pixel_at(over_clear, x, y);
      // A pure white-pass edge pixel: rgb == white, alpha strictly partial (so it carries
      // no shadow contribution — the shadow is black and would pull rgb below 255).
      if (clear.r == 255 && clear.g == 255 && clear.b == 255 && clear.a > 0 && clear.a < 255) {
        partial_alpha_over_clear = true;
        const float coverage = static_cast<float>(clear.a) / 255.0F;
        // The LINEAR reference `render` computes, and the gamma-space blend it must NOT be.
        const int ref_linear = arbc::linear_to_srgb8(coverage + (1.0F - coverage) * bg_lin);
        const int ref_gamma =
            static_cast<int>(std::lround(coverage * 255.0F + (1.0F - coverage) * 128.0F));
        const Rgba8 grey = pixel_at(over_grey, x, y);
        // Only mid-coverage edges, where the two models are far enough apart (the exact
        // opaque re-encode of `out_a` can wobble the linear byte by 1, so demand a margin).
        if (std::abs(ref_linear - ref_gamma) >= 4) {
          INFO("edge at (" << x << "," << y << ") coverage byte " << int(clear.a));
          const int actual = grey.r;
          CHECK(std::abs(actual - ref_linear) <= 1); // matches the linear reference ...
          CHECK(std::abs(actual - ref_gamma) >= 3);  // ... and is nowhere near the gamma blend
          CHECK(grey.a == 255);                      // opaque bg stays opaque
          checked_edge = true;
        }
      }
    }
  }
  CHECK(partial_alpha_over_clear); // transparent bg yields premultiplied-linear partial alpha
  CHECK(checked_edge);             // at least one edge distinguishes linear from gamma

  // The white + black-shadow pair holds on BOTH backgrounds: light ink (brighter than the
  // bg) and shadow ink (darker than it) both render, and the shadow's centroid sits down
  // and to the right of the light centroid — the (+scale,+scale) offset (Constraint 5).
  for (const Srgb8Image* img : std::array<const Srgb8Image*, 2>{&over_grey, &over_clear}) {
    double lx = 0;
    double ly = 0;
    double dx = 0;
    double dy = 0;
    std::size_t light = 0;
    std::size_t dark = 0;
    for (int y = 0; y < img->height; ++y) {
      for (int x = 0; x < img->width; ++x) {
        const Rgba8 px = pixel_at(*img, x, y);
        if (px.r > 160) { // brighter than mid-grey: white text
          lx += x;
          ly += y;
          ++light;
        } else if (px.r < 96) { // darker than mid-grey: shadow
          dx += x;
          dy += y;
          ++dark;
        }
      }
    }
    REQUIRE(light > 0);
    REQUIRE(dark > 0);
    CHECK(dx / static_cast<double>(dark) >
          lx / static_cast<double>(light)); // shadow is to the right
    CHECK(dy / static_cast<double>(dark) > ly / static_cast<double>(light)); // ... and below
  }
}

TEST_CASE("contact_sheet: captions stay within their strip") {
  const std::string widest = "\xc3\x91"
                             "and\xc3\xba Ext\xc3\xa9rieur Caf\xc3\xa9"; // Ñandú Extérieur Café
  for (int scale = 1; scale <= 2; ++scale) {
    INFO("scale " << scale);
    const int strip_h = k_glyph_cell_height * scale;
    Srgb8Image target;
    target.width = 400;
    target.height = strip_h * 3; // room above and below the strip
    target.pixels.assign(static_cast<std::size_t>(target.width) * target.height * 4, 0);
    const int caption_x = 20;
    const int caption_y = strip_h; // strip origin one strip-height down, so "above" exists
    draw_text(target, caption_x, caption_y, widest, scale);
    const int text_w = text_width(widest, scale);
    bool inside_drawn = false;
    for (int y = 0; y < target.height; ++y) {
      for (int x = 0; x < target.width; ++x) {
        const Rgba8 px = pixel_at(target, x, y);
        const bool in_strip =
            y >= caption_y && y < caption_y + strip_h && x >= caption_x && x < caption_x + text_w;
        if (in_strip) {
          inside_drawn = inside_drawn || px != Rgba8{0, 0, 0, 0};
        } else {
          // Constraint 7: nothing above `caption_y`, nothing below the strip, nothing
          // outside the advance span — including the descender and shadow rows.
          REQUIRE(px == Rgba8{0, 0, 0, 0});
        }
      }
    }
    CHECK(inside_drawn);
  }
}

// ---- the refusals (D23 / D-sheet-7) ------------------------------------------

TEST_CASE("contact_sheet: an empty tick-list is refused, not guessed") {
  CameraDoc doc;
  const arbc::ObjectId a = doc.add("Hero", 32, 32);

  ExportOptions options;
  options.destination = k_dest;
  options.contact_sheet = true;
  const ContactSheetPlan empty =
      plan_contact_sheet(*doc.document, {}, options, real_shot_camera(), {});
  CHECK(empty.tiles.empty());
  CHECK(empty.refused);
  CHECK_FALSE(empty.reason.empty());

  // An unresolved destination, an unbound derivation, and cameras that no longer exist.
  ExportOptions no_dest = options;
  no_dest.destination.clear();
  CHECK_FALSE(
      plan_contact_sheet(*doc.document, {a}, no_dest, real_shot_camera(), {}).reason.empty());
  CHECK_FALSE(plan_contact_sheet(*doc.document, {a}, options, {}, {}).reason.empty());
  CHECK_FALSE(plan_contact_sheet(*doc.document, {arbc::ObjectId{}}, options, real_shot_camera(), {})
                  .reason.empty());

  // Through the whole run: nothing is written and the state is Failed.
  RecordingFileSystem fs;
  ExportRunner runner;
  runner.render = stub_renderer();
  runner.filesystem = &fs;
  const ExportPlan plan = plan_export(*doc.document, {}, options, real_shot_camera());
  const ExportReport report = run_export(plan, options, runner);
  CHECK(report.state == ExportState::Failed);
  CHECK_FALSE(report.contact_sheet.has_value());
  CHECK(fs.writes.empty());

  // Both outputs off is refused rather than reinterpreted.
  ExportOptions neither = options;
  neither.contact_sheet = false;
  neither.write_items = false;
  const ExportPlan nothing = plan_export(*doc.document, {a}, neither, real_shot_camera());
  CHECK(nothing.items.empty());
  CHECK_FALSE(nothing.contact_sheet.has_value());
  CHECK_FALSE(nothing.reason.empty());
}

TEST_CASE("contact_sheet: an oversized sheet is refused as a value, and the batch still runs") {
  CameraDoc doc;
  std::vector<arbc::ObjectId> ids;
  // 400 cameras at the maximum tile edge: 20 x 20 tiles of 1024px is a ~21000 x 21000
  // sheet, far past k_max_export_bytes, while every item is a trivial 8x8 render.
  for (int i = 0; i < 400; ++i) {
    ids.push_back(doc.add("Cam " + std::to_string(i), 8, 8));
  }
  ExportOptions options;
  options.destination = k_dest;
  options.contact_sheet = true;
  options.tile_edge = k_contact_tile_max;

  const ExportPlan plan = plan_export(*doc.document, ids, options, real_shot_camera());
  REQUIRE(plan.items.size() == 400);
  REQUIRE(plan.contact_sheet.has_value());
  CHECK(plan.contact_sheet->refused);
  CHECK(plan.contact_sheet->tiles.empty());
  // The requested dimensions are IN the message — refuse-rather-than-guess names the
  // resource and the size (D-export-4 / D23).
  CHECK(plan.contact_sheet->reason.find(std::to_string(plan.contact_sheet->width)) !=
        std::string::npos);
  CHECK(plan.contact_sheet->reason.find(std::to_string(plan.contact_sheet->height)) !=
        std::string::npos);

  RecordingFileSystem fs;
  ExportRunner runner;
  runner.render = stub_renderer();
  runner.filesystem = &fs;
  const ExportReport report = run_export(plan, options, runner);
  CHECK(report.state == ExportState::Finished);
  CHECK(report.written == 400); // every batch item still written
  CHECK(report.refused == 1);
  REQUIRE(report.contact_sheet.has_value());
  CHECK(report.contact_sheet->refused);
  CHECK_FALSE(report.contact_sheet->written);
  CHECK_FALSE(report.contact_sheet->message.empty());
  CHECK(fs.writes.size() == 400); // no sheet on disk
}

TEST_CASE("contact_sheet: cancel between tiles writes no partial sheet") {
  CameraDoc doc;
  std::vector<arbc::ObjectId> ids;
  for (int i = 0; i < 6; ++i) {
    ids.push_back(doc.add("Cam " + std::to_string(i), 16, 16));
  }
  ExportOptions options;
  options.destination = k_dest;
  options.contact_sheet = true;
  options.tile_edge = 64;
  const ExportPlan plan = plan_export(*doc.document, ids, options, real_shot_camera());
  REQUIRE(plan.items.size() == 6);
  REQUIRE(plan.contact_sheet.has_value());
  REQUIRE(plan.contact_sheet->tiles.size() == 6);

  RecordingFileSystem fs;
  std::atomic<bool> cancel{false};
  int tile_calls = 0;
  ExportRunner runner;
  runner.filesystem = &fs;
  runner.cancel = &cancel;
  runner.render = [&](const arbc::DocStatePtr&, const arbc::Affine&, int width, int height,
                      const std::optional<Rgba8>&) {
    // Let the whole batch through, then stop on the SECOND tile render.
    if (width == 64 || height == 64) {
      if (++tile_calls == 2) {
        cancel.store(true);
      }
    }
    Srgb8Image image;
    image.width = width;
    image.height = height;
    image.pixels.assign(static_cast<std::size_t>(width) * height * 4, 255);
    return image;
  };

  const ExportReport report = run_export(plan, options, runner);
  CHECK(report.state == ExportState::Cancelled);
  CHECK(report.items.size() == 6);
  CHECK(report.written == 6); // the batch's files, all intact
  // A half-composed grid on disk would be indistinguishable from a complete one.
  CHECK_FALSE(report.contact_sheet.has_value());
  CHECK(fs.files.count(plan.contact_sheet->path) == 0);
  CHECK(fs.writes.size() == 6);
}

// ---- the filename (D-sheet-6) ------------------------------------------------

TEST_CASE("contact_sheet: the sheet stem yields to a camera of the same name") {
  CameraDoc doc;
  const arbc::ObjectId hostile = doc.add("contact-sheet", 16, 16);
  const arbc::ObjectId other = doc.add("Hero", 16, 16);

  ExportOptions options;
  options.destination = k_dest;
  options.contact_sheet = true;
  options.tile_edge = 64;

  const ExportPlan both = plan_export(*doc.document, {hostile, other}, options, real_shot_camera());
  REQUIRE(both.items.size() == 2);
  REQUIRE(both.contact_sheet.has_value());
  CHECK(both.items[0].path.filename() == "contact-sheet.png"); // the CAMERA keeps it
  CHECK(both.contact_sheet->path.filename() == "contact-sheet-2.png");

  // Sheet only: the batch took nothing, so the sheet keeps the plain stem.
  ExportOptions sheet_only = options;
  sheet_only.write_items = false;
  const ExportPlan alone =
      plan_export(*doc.document, {hostile, other}, sheet_only, real_shot_camera());
  CHECK(alone.items.empty());
  REQUIRE(alone.contact_sheet.has_value());
  CHECK(alone.contact_sheet->path.filename() == "contact-sheet.png");
  CHECK(alone.contact_sheet->path.parent_path() == k_dest);

  // A pre-existing file on disk is OVERWRITTEN in place, not suffixed (D-export-6).
  RecordingFileSystem fs;
  ExportRunner runner;
  runner.render = stub_renderer();
  runner.filesystem = &fs;
  CHECK(run_export(alone, sheet_only, runner).written == 1);
  CHECK(run_export(alone, sheet_only, runner).written == 1);
  CHECK(fs.files.size() == 1);
  CHECK(fs.writes.size() == 2);
}

// ---- progress and determinism ------------------------------------------------

TEST_CASE("contact_sheet: progress counts both phases and stays monotone and terminal") {
  CameraDoc doc;
  std::vector<arbc::ObjectId> ids;
  for (int i = 0; i < 3; ++i) {
    ids.push_back(doc.add("Cam " + std::to_string(i), 16, 16));
  }
  ExportOptions options;
  options.destination = k_dest;
  options.contact_sheet = true;
  options.tile_edge = 64;
  const ExportPlan plan = plan_export(*doc.document, ids, options, real_shot_camera());
  REQUIRE(plan.items.size() == 3);
  REQUIRE(plan.contact_sheet->tiles.size() == 3);

  RecordingFileSystem fs;
  std::vector<ExportProgress> published;
  ExportRunner runner;
  runner.render = stub_renderer();
  runner.filesystem = &fs;
  runner.publish = [&published](const ExportProgress& p) { published.push_back(p); };

  const ExportReport report = run_export(plan, options, runner);
  CHECK(report.written == 4); // three cameras plus the sheet

  REQUIRE_FALSE(published.empty());
  std::size_t terminal = 0;
  std::size_t last_done = 0;
  std::size_t named = 0;
  for (const ExportProgress& p : published) {
    CHECK(p.total == 6); // 3 items + 3 tiles, fixed from the first snapshot
    CHECK(p.done >= last_done);
    last_done = p.done;
    if (!p.current_name.empty()) {
      CHECK(p.current_name.rfind("Cam ", 0) == 0); // a camera, in either phase
      ++named;
    }
    if (p.state == ExportState::Finished || p.state == ExportState::Cancelled ||
        p.state == ExportState::Failed) {
      ++terminal;
    }
  }
  CHECK(terminal == 1);
  CHECK(named == 6); // every render across both phases announced its camera
  CHECK(published.back().state == ExportState::Finished);
  CHECK(published.back().done == 6);
}

TEST_CASE("contact_sheet: layout and bytes are deterministic") {
  CameraDoc doc;
  const arbc::Affine frame = arbc::Affine::translation(5.0, -1.0);
  std::vector<arbc::ObjectId> ids;
  ids.push_back(doc.add("Hero", 96, 54, frame));
  ids.push_back(doc.add("Square", 40, 40, frame));
  ids.push_back(doc.add("Portrait", 30, 60, frame));

  ExportOptions options;
  options.destination = k_dest;
  options.contact_sheet = true;
  options.tile_edge = 96;

  const ContactSheetPlan first =
      plan_contact_sheet(*doc.document, ids, options, real_shot_camera(), {});
  const ContactSheetPlan again =
      plan_contact_sheet(*doc.document, ids, options, real_shot_camera(), {});
  CHECK(first == again); // field-by-field

  const auto renders = [&](const ContactSheetPlan& plan) {
    std::vector<Srgb8Image> out;
    for (std::size_t i = 0; i < plan.tiles.size(); ++i) {
      out.push_back(
          gradient_render(plan.tiles[i].width, plan.tiles[i].height, static_cast<int>(i) + 4));
    }
    return out;
  };
  const Srgb8Image a = compose_contact_sheet(first, renders(first), std::nullopt);
  const Srgb8Image b = compose_contact_sheet(again, renders(again), std::nullopt);
  CHECK(a.pixels == b.pixels);
  CHECK_FALSE(a.pixels.empty());
}

TEST_CASE("contact_sheet: the shipped defaults reproduce the batch-only path exactly") {
  CameraDoc doc;
  const arbc::ObjectId a = doc.add("Hero", 16, 16);
  const arbc::ObjectId b = doc.add("Wide", 32, 16);
  ExportOptions defaults;
  defaults.destination = k_dest;
  CHECK(defaults.write_items);
  CHECK_FALSE(defaults.contact_sheet);
  CHECK(defaults.tile_edge == ace::commands::k_contact_tile_default);

  const ExportPlan plan = plan_export(*doc.document, {a, b}, defaults, real_shot_camera());
  CHECK(plan.items.size() == 2);
  CHECK_FALSE(plan.contact_sheet.has_value());

  RecordingFileSystem fs;
  ExportRunner runner;
  runner.render = stub_renderer();
  runner.filesystem = &fs;
  const ExportReport report = run_export(plan, defaults, runner);
  CHECK(report.written == 2);
  CHECK_FALSE(report.contact_sheet.has_value());
  CHECK(fs.writes.size() == 2);
}

// ---- the async job across both phases (A18 / Constraint 8) -------------------

TEST_CASE("contact_sheet: the service publishes across the item -> tile transition") {
  CameraDoc doc;
  std::vector<arbc::ObjectId> ids;
  for (int i = 0; i < 8; ++i) {
    ids.push_back(doc.add("Cam " + std::to_string(i), 16, 16));
  }
  ExportOptions options;
  options.destination = k_dest;
  options.contact_sheet = true;
  options.tile_edge = 64;

  RecordingFileSystem fs;
  ace::platform::NativeThreads threads;
  ExportService service(threads, fs);
  service.set_shot_camera(real_shot_camera());
  service.set_renderer(stub_renderer());

  const std::shared_ptr<const ExportProgress> before = service.progress();
  REQUIRE(before != nullptr);

  std::atomic<bool> stop{false};
  std::atomic<int> reads{0};
  std::atomic<int> torn{0};
  auto reader = threads.spawn([&service, &stop, &reads, &torn] {
    while (!stop.load(std::memory_order_acquire)) {
      const std::shared_ptr<const ExportProgress> snapshot = service.progress();
      if (snapshot) {
        // One loaded pointer is one self-consistent generation for the whole read,
        // across the item -> tile transition as much as within a phase (A18).
        if (snapshot->done > snapshot->total) {
          torn.fetch_add(1);
        }
        reads.fetch_add(1);
      }
    }
  });

  REQUIRE(service.start(plan_export(*doc.document, ids, options, real_shot_camera()), options));
  service.join();
  stop.store(true, std::memory_order_release);
  reader->join();

  CHECK(torn.load() == 0);
  CHECK(reads.load() > 0);
  CHECK(before->state == ExportState::Idle); // immutable after publication
  CHECK(before->total == 0);
  const std::shared_ptr<const ExportProgress> after = service.progress();
  CHECK(after->state == ExportState::Finished);
  CHECK(after->total == 16); // 8 items + 8 tiles, from the FIRST snapshot
  CHECK(after->done == 16);
  const std::shared_ptr<const ExportReport> report = service.report();
  REQUIRE(report != nullptr);
  CHECK(report->written == 9);
  REQUIRE(report->contact_sheet.has_value());
  CHECK(report->contact_sheet->written);
}

TEST_CASE("contact_sheet: a job still in its SHEET phase at teardown is joined first") {
  // Constraint 8, across the longer job: the sheet phase reads the ONE owned Document
  // through the injected renderer, so ~ExportService must cancel-then-join INSIDE the
  // scope enclosing that document. Moving the join out turns this into a
  // use-after-free under ASan.
  RecordingFileSystem fs;
  ace::platform::NativeThreads threads;
  std::atomic<bool> in_sheet{false};
  std::atomic<int> tiles{0};

  {
    CameraDoc doc;
    std::vector<arbc::ObjectId> ids;
    for (int i = 0; i < 48; ++i) {
      ids.push_back(doc.add("Cam " + std::to_string(i), 16, 16));
    }
    ExportOptions options;
    options.destination = k_dest;
    options.write_items = false; // sheet ONLY, so every render is a tile render
    options.contact_sheet = true;
    options.tile_edge = 64;

    {
      ExportService service(threads, fs);
      service.set_shot_camera(real_shot_camera());
      service.set_renderer([&doc, &in_sheet, &tiles](const arbc::DocStatePtr&, const arbc::Affine&,
                                                     int width, int height,
                                                     const std::optional<Rgba8>&) {
        in_sheet.store(true);
        tiles.fetch_add(1);
        (void)ace::scene::cameras(*doc.document); // genuinely IN the document
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Srgb8Image image;
        image.width = width;
        image.height = height;
        image.pixels.assign(static_cast<std::size_t>(width) * height * 4, 128);
        return image;
      });
      REQUIRE(service.start(plan_export(*doc.document, ids, options, real_shot_camera()), options));
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
      while (tiles.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      REQUIRE(tiles.load() > 0);
      CHECK(service.running());
    } // ~ExportService cancels between tiles and JOINS — never detaches
    CHECK(in_sheet.load());
  } // the Document is released only now

  CHECK(tiles.load() > 0);
}

// ---- the golden (docs §9 / D-export-11) --------------------------------------

TEST_CASE("contact_sheet: the composed sheet is byte-exact vs the golden") {
  const std::unique_ptr<arbc::Document> doc = build_sheet_doc();
  // The renderer callable the L4 shell binds, VERBATIM (src/app/shell.cpp:385-394).
  const ace::commands::RenderFn shipped =
      [&doc](const arbc::DocStatePtr&, const arbc::Affine& camera, int width, int height,
             const std::optional<Rgba8>& background) -> ace::render::Srgb8Image {
    // The compose golden asserts byte-identity against direct 2-arg renders, so this
    // closure renders through the 2-arg path and ignores the batch pin (the pinning
    // itself is exercised in export_test.cpp / canvas_host_test.cpp).
    if (!background) {
      return ace::render::render_document_srgb8(*doc, width, height, camera);
    }
    return ace::render::render_document_srgb8_over(
        *doc, width, height, camera, {background->r, background->g, background->b, background->a});
  };

  // Three cameras of distinct aspects, the third with a name far too long for the tile
  // — so this one artifact pins the grid geometry, the aspect fit, the tile blit, the
  // empty slot's background fill, the caption glyphs, the shadow AND the ellipsis.
  CameraDoc cams;
  const arbc::Affine frame = arbc::Affine::translation(8.0, 8.0);
  const arbc::ObjectId hero = cams.add("Hero", 64, 64, frame);
  const arbc::ObjectId wide = cams.add("Wide", 96, 54, frame);
  const arbc::ObjectId tall = cams.add("A very long camera name that will not fit", 32, 64, frame);

  ExportOptions options;
  options.destination = k_dest;
  options.contact_sheet = true;
  options.tile_edge = 96;
  const ContactSheetPlan plan =
      plan_contact_sheet(*cams.document, {hero, wide, tall}, options, real_shot_camera(), {});
  REQUIRE(plan.tiles.size() == 3);
  REQUIRE(plan.cols == 2);
  REQUIRE(plan.rows == 2);

  // The tiles are rendered against the CONTENT document, through the shipped renderer.
  const std::vector<Srgb8Image> renders = render_tiles(plan, shipped, std::nullopt);
  const Srgb8Image sheet = compose_contact_sheet(plan, renders, std::nullopt);
  REQUIRE(sheet.width == plan.width);
  REQUIRE(sheet.height == plan.height);
  CHECK(ace_test::compare_golden("contact_sheet_3cam.rgba8", sheet.pixels));

  // ANTI-VACUITY 1: the sheet is genuinely not uniformly background.
  bool any_content = false;
  for (std::size_t i = 0; i < sheet.pixels.size(); i += 4) {
    if (sheet.pixels[i + 3] != 0) {
      any_content = true;
      break;
    }
  }
  CHECK(any_content);

  // ANTI-VACUITY 2: the empty fourth slot IS exactly background.
  const ContactCellRect empty = contact_sheet_cell(contact_sheet_layout(4, plan.tile_edge), 3);
  REQUIRE(empty.width == plan.tile_edge);
  for (int y = empty.y; y < empty.y + empty.height; ++y) {
    for (int x = empty.x; x < empty.x + empty.width; ++x) {
      REQUIRE(pixel_at(sheet, x, y) == Rgba8{0, 0, 0, 0});
    }
  }

  // ANTI-VACUITY 3: each occupied tile rect is byte-identical to an INDEPENDENT direct
  // render of that camera at its fitted size — so a sheet regenerated from a broken
  // composer cannot pass by agreeing with itself.
  const std::vector<ace::scene::Camera> list = ace::scene::cameras(*cams.document);
  REQUIRE(list.size() == 3);
  for (std::size_t i = 0; i < plan.tiles.size(); ++i) {
    const ContactTile& tile = plan.tiles[i];
    INFO("tile " << i << " (" << tile.camera_name << ")");
    const ace::render::Srgb8Image direct = ace::render::render_document_srgb8(
        *doc, tile.width, tile.height,
        viewport_camera_for_shot(list[i].frame, list[i].resolution.width, list[i].resolution.height,
                                 tile.width, tile.height));
    REQUIRE(direct.pixels.size() ==
            static_cast<std::size_t>(tile.width) * static_cast<std::size_t>(tile.height) * 4);
    for (int row = 0; row < tile.height; ++row) {
      const std::size_t src = static_cast<std::size_t>(row) * tile.width * 4;
      const std::size_t dst = (static_cast<std::size_t>(tile.y + row) * sheet.width + tile.x) * 4;
      REQUIRE(std::memcmp(&sheet.pixels[dst], &direct.pixels[src],
                          static_cast<std::size_t>(tile.width) * 4) == 0);
    }
  }

  // The long name really was truncated with an ellipsis, and it fits the tile column.
  CHECK(plan.tiles[2].caption != plan.tiles[2].camera_name);
  CHECK(plan.tiles[2].caption.substr(plan.tiles[2].caption.size() - 3) == "...");
  CHECK(text_width(plan.tiles[2].caption, plan.caption_scale) <= plan.tile_edge);
}

TEST_CASE("contact_sheet: an accented caption composes byte-exact vs the golden") {
  const std::unique_ptr<arbc::Document> doc = build_sheet_doc();
  const ace::commands::RenderFn shipped =
      [&doc](const arbc::DocStatePtr&, const arbc::Affine& camera, int width, int height,
             const std::optional<Rgba8>& background) -> ace::render::Srgb8Image {
    // The compose golden asserts byte-identity against direct 2-arg renders, so this
    // closure renders through the 2-arg path and ignores the batch pin (the pinning
    // itself is exercised in export_test.cpp / canvas_host_test.cpp).
    if (!background) {
      return ace::render::render_document_srgb8(*doc, width, height, camera);
    }
    return ace::render::render_document_srgb8_over(
        *doc, width, height, camera, {background->r, background->g, background->b, background->a});
  };

  // One caption now drawn IN FULL, one mixing Latin-1 with a CJK code point outside the
  // table — so this single artifact pins both the new glyphs and the surviving fallback
  // box (`editor.cameras.caption_latin1` acceptance).
  CameraDoc cams;
  const arbc::Affine frame = arbc::Affine::translation(8.0, 8.0);
  const arbc::ObjectId cafe = cams.add("Café Extérieur", 64, 64, frame);
  const arbc::ObjectId nandu = cams.add("Ñandú 中", 96, 54, frame);

  ExportOptions options;
  options.destination = k_dest;
  options.contact_sheet = true;
  options.tile_edge = 96;
  const ContactSheetPlan plan =
      plan_contact_sheet(*cams.document, {cafe, nandu}, options, real_shot_camera(), {});
  REQUIRE(plan.tiles.size() == 2);

  const std::vector<Srgb8Image> renders = render_tiles(plan, shipped, std::nullopt);
  const Srgb8Image sheet = compose_contact_sheet(plan, renders, std::nullopt);
  REQUIRE(sheet.width == plan.width);
  REQUIRE(sheet.height == plan.height);
  CHECK(ace_test::compare_golden("contact_sheet_latin1.rgba8", sheet.pixels));

  // ANTI-VACUITY 1: the sheet is genuinely not uniformly background.
  bool any_content = false;
  for (std::size_t i = 0; i < sheet.pixels.size(); i += 4) {
    if (sheet.pixels[i + 3] != 0) {
      any_content = true;
      break;
    }
  }
  CHECK(any_content);

  // ANTI-VACUITY 2: the accented glyphs are DRAWN, not merely counted. The caption strip
  // of tile 0 ("Café Extérieur") carries antialiased ink at columns that are pure
  // background in a same-geometry sheet whose first camera is captioned "Caf" — i.e. the
  // extra glyphs past "Caf" really put coverage down. (Over the transparent strip the ink
  // is premultiplied-linear `over`, so a white glyph edge is `{255,255,255,<255}`, never
  // fully opaque — the antialiasing a 1-bit table could not produce.)
  CameraDoc plain;
  const arbc::ObjectId caf = plain.add("Caf", 64, 64, frame);
  const arbc::ObjectId nandu2 = plain.add("Ñandú 中", 96, 54, frame);
  const ContactSheetPlan plan_plain =
      plan_contact_sheet(*plain.document, {caf, nandu2}, options, real_shot_camera(), {});
  const std::vector<Srgb8Image> renders_plain = render_tiles(plan_plain, shipped, std::nullopt);
  const Srgb8Image plain_sheet = compose_contact_sheet(plan_plain, renders_plain, std::nullopt);
  REQUIRE(plain_sheet.width == sheet.width);
  REQUIRE(plain_sheet.height == sheet.height);
  const ContactTile& tile0 = plan.tiles[0];
  const int strip_h = k_glyph_cell_height * plan.caption_scale;
  bool extra_ink = false;
  bool intermediate = false;
  for (int y = tile0.caption_y; y < tile0.caption_y + strip_h; ++y) {
    for (int x = tile0.caption_x; x < tile0.caption_x + plan.tile_edge; ++x) {
      const Rgba8 here = pixel_at(sheet, x, y);
      const Rgba8 there = pixel_at(plain_sheet, x, y);
      if (here.a != 0 && there.a == 0) { // ink where the "Caf" sheet is background
        extra_ink = true;
        if (here.a != 255) {
          intermediate = true; // ... and it is antialiased, not a 1-bit block
        }
      }
    }
  }
  CHECK(extra_ink);
  CHECK(intermediate);
}
