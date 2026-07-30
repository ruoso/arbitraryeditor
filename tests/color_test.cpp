// editor.panels.color — L1 headless Catch2 units for the sRGB↔premultiplied-linear boundary
// (D10 / §7 / D-color-1..5). Three groups, all ImGui/GL/SDL-free:
//   * the `commands` conversion pair (`srgb_to_working` / `working_to_srgb`) is byte-exact against
//     the hand-composed library primitives, is premultiplied, and round-trips 8-bit colors;
//   * the `AppState` active-color field defaults to opaque black, round-trips, journals NOTHING
//     (it is not a transaction), and survives a defaulted move;
//   * `sample_composited_color` is a byte-exact `render_offline` sampler — interior equals the
//     composited pixel a full render produces, exterior is transparent (the golden-in-a-value
//     form).
#include <ace/commands/app_state.hpp>
#include <ace/commands/color.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>

#include <arbc/backend_cpu/cpu_backend.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/compositor/compositor.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/media/pixel_traits.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/offline.hpp>
#include <arbc/surface/surface.hpp>
#include <arbc/surface/typed_span.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

using ace::commands::AppState;
using ace::commands::sample_composited_color;
using ace::commands::srgb_to_working;
using ace::commands::SrgbColor;
using ace::commands::working_to_srgb;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_color_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// The hand-composed library truth (D-color-2): the exact three-step the filled-background composite
// runs (`render.cpp:64-71`). `srgb_to_working` must equal this byte-for-byte.
arbc::WorkingPixel hand_working(const SrgbColor& c) {
  return arbc::premultiply(
      arbc::WorkingPixel{arbc::srgb8_to_linear(c.r), arbc::srgb8_to_linear(c.g),
                         arbc::srgb8_to_linear(c.b), arbc::unorm8_decode(c.a)});
}

// A byte-exact reference: the composited working pixel a FULL `w`x`h` render produces at (px,py),
// decoded through the same library codec `sample_composited_color` uses. What the sampler's 1×1
// render must reproduce.
arbc::WorkingPixel full_render_pixel(const arbc::Document& doc, int w, int h, int px, int py) {
  arbc::CpuBackend backend;
  const arbc::Viewport viewport{w, h, arbc::Affine::identity()};
  const arbc::expected<std::unique_ptr<arbc::Surface>, arbc::SurfaceError> frame =
      arbc::render_offline(doc, viewport, backend);
  REQUIRE(frame.has_value());
  return arbc::visit_surface(**frame, [&](auto typed) -> arbc::WorkingPixel {
    using Traits = arbc::PixelTraits<decltype(typed)::format>;
    const std::size_t idx = (static_cast<std::size_t>(py) * static_cast<std::size_t>(w) +
                             static_cast<std::size_t>(px)) *
                            Traits::channels;
    return Traits::decode(typed.data.data() + idx);
  });
}

} // namespace

// --- The conversion pair (Constraint 2 / D-color-2) -----------------------------------------

TEST_CASE("color: srgb_to_working is byte-exact against the library primitives and premultiplied") {
  const SrgbColor cases[] = {
      SrgbColor{0, 0, 0, 255},       // opaque black (the default / brush placeholder)
      SrgbColor{255, 255, 255, 255}, // opaque white
      SrgbColor{128, 128, 128, 255}, // mid-gray
      SrgbColor{230, 40, 40, 255},   // a saturated primary
      SrgbColor{200, 100, 50, 128},  // semi-transparent
  };
  for (const SrgbColor& c : cases) {
    const arbc::WorkingPixel working = srgb_to_working(c);
    CHECK(working == hand_working(c)); // byte-identical to the hand-composed library result
    // Premultiplied: every colour channel is scaled by alpha, so each rgb <= a.
    CHECK(working[0] <= working[3]);
    CHECK(working[1] <= working[3]);
    CHECK(working[2] <= working[3]);
  }
}

TEST_CASE("color: the conversion pair round-trips 8-bit colors exactly") {
  // Opaque colors round-trip exactly for every code (the library's transfer is an exact 8-bit
  // inverse, and at alpha 255 premultiply/unpremultiply are the identity).
  const std::uint8_t spread[] = {0, 1, 7, 63, 64, 128, 200, 254, 255};
  for (const std::uint8_t r : spread) {
    for (const std::uint8_t g : spread) {
      for (const std::uint8_t b : spread) {
        const SrgbColor c{r, g, b, 255};
        CHECK(working_to_srgb(srgb_to_working(c)) == c);
      }
    }
  }
  // A semi-transparent color also inverts to the original 8-bit RGBA.
  const SrgbColor semi{200, 100, 50, 128};
  CHECK(working_to_srgb(srgb_to_working(semi)) == semi);
  // A fully transparent color's straight color is unrecoverable (premultiply zeroes it) — the
  // honest boundary: alpha 0 encodes to transparent, never a phantom colour surfaced to the user.
  CHECK(working_to_srgb(arbc::WorkingPixel{0.0F, 0.0F, 0.0F, 0.0F}) == SrgbColor{0, 0, 0, 0});
}

// --- The AppState active-color field (Constraints 1, 3) -------------------------------------

TEST_CASE("color: AppState active color defaults to opaque black, round-trips, journals nothing, "
          "survives a move") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "color");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));

  // Boot continuity (Constraint 3): the default is opaque black straight → {0,0,0,1} working,
  // byte-identical to the brush's retired placeholder.
  CHECK(state.active_color() == SrgbColor{0, 0, 0, 255});
  CHECK(state.active_working_color() == arbc::WorkingPixel{0.0F, 0.0F, 0.0F, 1.0F});

  // set_active_color round-trips through active_color() AND the derived working value.
  const std::size_t cursor_before = state.document().journal().cursor();
  const std::size_t depth_before = state.document().journal().depth();
  const std::uint64_t revision_before = state.document().pin()->revision();

  const SrgbColor picked{230, 40, 40, 255};
  state.set_active_color(picked);
  CHECK(state.active_color() == picked);
  CHECK(state.active_working_color() == srgb_to_working(picked));

  // Setting the active color is transient session state, NOT a transaction (Constraint 1 / D15):
  // no journal entry, no cursor move, no revision bump.
  CHECK(state.document().journal().cursor() == cursor_before);
  CHECK(state.document().journal().depth() == depth_before);
  CHECK(state.document().pin()->revision() == revision_before);

  // The active color survives a defaulted move of AppState (the move ctor is load-bearing).
  AppState moved(std::move(state));
  CHECK(moved.active_color() == picked);
  CHECK(moved.active_working_color() == srgb_to_working(picked));
}

// --- The composited sampler (Constraint 5 / D-color-4) — render_offline byte-exact -----------

TEST_CASE("color: sample_composited_color returns the exact composited pixel; exterior is "
          "transparent") {
  // A solid cell bounded to comp [10,50]^2 over an otherwise transparent 64x64 composition. The
  // color is PREMULTIPLIED linear (SolidContent's contract), opaque, so the composite over
  // transparent black is that pixel exactly.
  constexpr int k_edge = 64;
  arbc::Document doc;
  const arbc::ObjectId comp =
      doc.add_composition(static_cast<double>(k_edge), static_cast<double>(k_edge));
  const arbc::ObjectId content = doc.add_content(std::make_shared<arbc::SolidContent>(
      arbc::Rgba{0.5F, 0.25F, 0.1F, 1.0F}, arbc::Rect{10.0, 10.0, 50.0, 50.0}));
  doc.attach_layer(comp, doc.add_layer(content, arbc::Affine::identity()));

  const arbc::Affine camera = arbc::Affine::identity();

  // Interior (30,30): the 1×1 sample equals the full-render pixel byte-for-byte (the golden-in-a-
  // value form), has real coverage, and is premultiplied.
  const arbc::WorkingPixel interior = sample_composited_color(doc, camera, 30.0, 30.0);
  CHECK(interior == full_render_pixel(doc, k_edge, k_edge, 30, 30));
  CHECK(interior[3] > 0.0F); // covered
  CHECK(interior[0] <= interior[3]);
  CHECK(interior[1] <= interior[3]);
  CHECK(interior[2] <= interior[3]);
  // Decoded back to sRGB it is a real, opaque colour — never a premultiplied/linear leak.
  CHECK(working_to_srgb(interior).a == 255);

  // Exterior (2,2): outside the solid's coverage, so transparent black — the eyedropper reads
  // "nothing here", not a phantom colour.
  const arbc::WorkingPixel exterior = sample_composited_color(doc, camera, 2.0, 2.0);
  CHECK(exterior == arbc::WorkingPixel{0.0F, 0.0F, 0.0F, 0.0F});
}
