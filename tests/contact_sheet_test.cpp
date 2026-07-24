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
#include <arbc/media/surface_format.hpp>
#include <arbc/runtime/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
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
using ace::commands::k_glyph_cell_width;
using ace::commands::plan_contact_sheet;
using ace::commands::plan_export;
using ace::commands::Rgba8;
using ace::commands::run_export;
using ace::commands::Srgb8Image;
using ace::commands::text_cells;
using ace::commands::text_set_bits;
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
  return [](const arbc::Affine&, int width, int height, const std::optional<Rgba8>& background) {
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
    renders.push_back(render(tile.render_camera, tile.width, tile.height, background));
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

// ---- the caption face (D-sheet-4) --------------------------------------------

TEST_CASE("contact_sheet: captions write only glyph and shadow pixels") {
  Srgb8Image strip;
  strip.width = 200;
  strip.height = k_glyph_cell_height * 2;
  strip.pixels.assign(static_cast<std::size_t>(strip.width) * strip.height * 4, 0);
  const std::string name = "Hero 1";
  draw_text(strip, 0, 0, name, 1);

  std::set<std::array<std::uint8_t, 4>> colours;
  std::size_t white = 0;
  std::size_t black = 0;
  for (std::size_t i = 0; i < strip.pixels.size(); i += 4) {
    colours.insert(
        {strip.pixels[i], strip.pixels[i + 1], strip.pixels[i + 2], strip.pixels[i + 3]});
    if (strip.pixels[i] == 255 && strip.pixels[i + 3] == 255) {
      ++white;
    } else if (strip.pixels[i] == 0 && strip.pixels[i + 3] == 255) {
      ++black;
    }
  }
  // Exactly three: background, opaque white, opaque black. No alpha ramp anywhere,
  // which is what keeps a caption a pure pixel write under D-sheet-2.
  CHECK(colours.size() == 3);
  // ANTI-VACUITY: the strip is genuinely not uniformly background, and the white pixel
  // count is exactly the table's set-bit count for this string.
  CHECK(white > 0);
  CHECK(black > 0);
  CHECK(white == static_cast<std::size_t>(text_set_bits(name)));

  // At scale 2 each set bit becomes a 2x2 block.
  Srgb8Image scaled;
  scaled.width = 200;
  scaled.height = k_glyph_cell_height * 3;
  scaled.pixels.assign(static_cast<std::size_t>(scaled.width) * scaled.height * 4, 0);
  draw_text(scaled, 0, 0, name, 2);
  std::size_t white2 = 0;
  for (std::size_t i = 0; i < scaled.pixels.size(); i += 4) {
    if (scaled.pixels[i] == 255 && scaled.pixels[i + 3] == 255) {
      ++white2;
    }
  }
  CHECK(white2 == static_cast<std::size_t>(text_set_bits(name)) * 4);

  // A space draws nothing at all, and a non-positive scale is a no-op, not a crash.
  Srgb8Image blank = strip;
  const std::vector<std::uint8_t> before = blank.pixels;
  draw_text(blank, 0, 0, "  ", 1);
  CHECK(blank.pixels == before);
  draw_text(blank, 0, 0, name, 0);
  CHECK(blank.pixels == before);
  Srgb8Image degenerate;
  draw_text(degenerate, 0, 0, name, 1); // an empty target is a no-op
  CHECK(degenerate.pixels.empty());
}

TEST_CASE("contact_sheet: a caption too wide for the tile is truncated, never overdrawn") {
  const std::string huge(200, 'W');
  const int tile_edge = 64;
  const std::string fitted = fit_text(huge, tile_edge, 1);
  CHECK(fitted.size() < huge.size());
  CHECK(fitted.substr(fitted.size() - 3) == "...");
  CHECK(text_width(fitted, 1) <= tile_edge);
  // A name that already fits is returned untouched — no gratuitous ellipsis.
  CHECK(fit_text("Hi", tile_edge, 1) == "Hi");
  // Room for nothing but the ellipsis, and room for nothing at all.
  CHECK(fit_text(huge, k_glyph_cell_width * 2, 1) == "..");
  CHECK(fit_text(huge, 1, 1).empty());
  CHECK(fit_text(huge, tile_edge, 0).empty());

  // And nothing outside the tile's own column is touched: draw the fitted caption at a
  // tile origin inside a wider image and check every pixel beyond the column.
  Srgb8Image target;
  target.width = 3 * tile_edge;
  target.height = k_glyph_cell_height * 2;
  target.pixels.assign(static_cast<std::size_t>(target.width) * target.height * 4, 0);
  const int origin = tile_edge;
  draw_text(target, origin, 0, fitted, 1);
  bool inside_drawn = false;
  for (int y = 0; y < target.height; ++y) {
    for (int x = 0; x < target.width; ++x) {
      const Rgba8 px = pixel_at(target, x, y);
      if (x >= origin && x < origin + tile_edge) {
        inside_drawn = inside_drawn || px != Rgba8{0, 0, 0, 0};
        continue;
      }
      REQUIRE(px == Rgba8{0, 0, 0, 0});
    }
  }
  CHECK(inside_drawn); // anti-vacuity: something really was drawn inside the column
}

TEST_CASE("contact_sheet: unmapped bytes render as one fallback box, not one per byte") {
  const std::string cafe = "Caf\xc3\xa9"; // "Café" in UTF-8 — 5 bytes
  CHECK(cafe.size() == 5);
  // C, a, f and ONE box for the two-byte sequence.
  CHECK(text_cells(cafe) == 4);
  CHECK(text_width(cafe, 1) == 4 * k_glyph_cell_width);
  // A longer run still collapses to one box; two runs separated by a mapped byte are two.
  CHECK(text_cells("\xc3\xa9\xc3\xa9") == 1);
  CHECK(text_cells("\xc3\xa9-\xc3\xa9") == 3);
  CHECK(text_cells("") == 0);
  // Control characters and DEL are unmapped too, and never read out of the table.
  CHECK(text_cells(std::string("a\x7f\x01\x1f", 4)) == 2);

  // What is DRAWN agrees with what is MEASURED.
  Srgb8Image strip;
  strip.width = 64;
  strip.height = k_glyph_cell_height * 2;
  strip.pixels.assign(static_cast<std::size_t>(strip.width) * strip.height * 4, 0);
  draw_text(strip, 0, 0, cafe, 1);
  int rightmost = -1;
  for (int y = 0; y < strip.height; ++y) {
    for (int x = 0; x < strip.width; ++x) {
      if (pixel_at(strip, x, y) != Rgba8{0, 0, 0, 0}) {
        rightmost = std::max(rightmost, x);
      }
    }
  }
  CHECK(rightmost >= 0);
  CHECK(rightmost < text_width(cafe, 1)); // four cells wide, not five
  CHECK(rightmost >= 3 * k_glyph_cell_width);
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
  runner.render = [&](const arbc::Affine&, int width, int height, const std::optional<Rgba8>&) {
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
      service.set_renderer([&doc, &in_sheet, &tiles](const arbc::Affine&, int width, int height,
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
      [&doc](const arbc::Affine& camera, int width, int height,
             const std::optional<Rgba8>& background) -> ace::render::Srgb8Image {
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
