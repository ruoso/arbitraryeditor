// editor.project.raster_tile_store — the incremental-save hash memo, threaded through both
// directions of serialization (refinement tasks/refinements/editor/raster_tile_store.md, A23).
//
// The claim under test is NOT "the right bytes land on disk" — `project_save_test` and
// `project_open_test` already pin that, and they pass unmodified, which is itself the proof
// that the trailing `tiles` parameter is source-compatible and behaviour-preserving. The claim
// here is HOW MUCH WORK a save does: `RasterTileStore::tiles_hashed()` advances by exactly one
// per tile actually hashed and NOT on a memo hit
// (`arbc/runtime/raster_tile_store.hpp:112-118`). That counter is the load-bearing witness —
// `SaveOutcome::assets_written` alone would pass today with zero memoisation, because the
// `FilesystemAssetSink` already dedups WRITES (`save.cpp:70-98`) while the SHA-256 that
// produces each blob's name is recomputed regardless (D-raster_tile_store-5).
//
// Headless L1 only (§8/§9): `commands` + `project` + `scene` + libarbc, no ImGui/GL/SDL.

#include <ace/commands/app_state.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/project/save.hpp>
#include <ace/scene/cell.hpp>

#include <arbc/base/geometry.hpp>
#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/kind_raster/raster_content.hpp>
#include <arbc/media/pixel_format.hpp>
#include <arbc/media/pixel_traits.hpp>
#include <arbc/model/model.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/raster_tile_store.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <set>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

using ace::commands::AppState;

namespace {

// The platform_test ScratchDir pattern (not a shared header): a unique temp dir wiped on
// construction and destruction. The leaf name is distinct per suite so the suites never
// collide inside the one `ace_tests` binary.
struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_raster_tile_store_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;
};

// A raster wide enough to span more than one level-0 tile: `org.arbc.raster`'s factory builds
// at `k_default_tile_edge` (256), so "512x512" is a 2x2 grid = FOUR tiles. One tile would make
// "hashes exactly the touched tiles" vacuous — every dab would touch all of them.
constexpr const char* k_raster_config = "512x512";
constexpr std::uint64_t k_raster_tiles = 4;

// A dab landing wholly inside the tile at grid (0,0) — pixels [0,256) x [0,256) — so exactly
// ONE of the four tiles is copied into the new version and the other three keep their
// `BlockSlotRef` identity (the CoW property the memo hits on).
constexpr arbc::Rect k_one_tile_dab{2.0, 2.0, 6.0, 6.0};

// Open (or create) a session and give it a root composition — `scene::add_cell` places into
// the root and refuses when there is none.
AppState session_at(const ace::platform::FileSystem& fs, const std::filesystem::path& root) {
  auto created = ace::project::create_project(fs, root);
  REQUIRE(created.has_value());
  REQUIRE(created.value().tiles != nullptr);
  AppState state(std::move(*created));
  state.document().add_composition(512.0, 512.0);
  return state;
}

// The REAL insert path (A16 / D-cells_model-9): the content's kind token is the
// registry-seeded one the save-side bridge resolves, so a hand-minted token would render but
// not serialize.
arbc::ObjectId insert_raster(AppState& state) {
  const auto added = ace::scene::add_cell(state.document(), state.registry(), "org.arbc.raster",
                                          k_raster_config, arbc::Affine::identity());
  REQUIRE(added.has_value());
  state.document().drain();
  return *added;
}

// Paint one dab through the pinned library's own raster entry point, exactly as libarbc's
// `tests/raster_tile_store_golden.t.cpp:249-289` does. No painting TOOL exists yet
// (`editor.paint.*` is unscheduled), and this leaf ships no UI — the point is the memo's
// behaviour under a real CoW paint, not a gesture.
void dab(AppState& state, arbc::ObjectId content, const arbc::Rect& region) {
  auto* raster = dynamic_cast<arbc::RasterContent*>(state.document().resolve(content));
  REQUIRE(raster != nullptr);
  {
    arbc::Model::Transaction txn = state.document().transact("dab");
    raster->paint(txn, content, region, arbc::WorkingPixel{1.0F, 0.0F, 0.0F, 1.0F});
    REQUIRE(txn.commit());
  }
  // WITHOUT this the published version's `BlockSlotRef` identity has not settled and the memo
  // cannot hit (the golden's note at `raster_tile_store_golden.t.cpp:249-289`).
  state.document().drain();
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Every content-addressed blob name under `assets/`, flattened out of the `<xx>/` fan-out.
std::set<std::string> blob_names(const std::filesystem::path& assets_dir) {
  std::set<std::string> names;
  std::error_code ec;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(assets_dir, ec)) {
    if (entry.is_regular_file(ec)) {
      names.insert(entry.path().filename().string());
    }
  }
  return names;
}

} // namespace

// Constraint 2 / D-raster_tile_store-6: the move CONSTRUCTOR is load-bearing
// (`open_or_create_app_state` returns by value), the move ASSIGNMENT is not — and a defaulted
// one would assign `document_` before `tiles_`, freeing the pool while the old memo still
// pinned into it.
static_assert(std::is_move_constructible_v<AppState>);
static_assert(!std::is_move_assignable_v<AppState>);

// THE HEADLINE CLAIM, and the one that fails without the memo: a save that changes nothing
// should cost nothing. Before this leaf, the second save re-hashed all four tiles.
TEST_CASE("a re-save of an untouched raster project hashes zero tiles and writes zero blobs") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_at(fs, scratch.root / "untouched");
  insert_raster(state);

  const auto first = ace::commands::save_project(state, fs);
  REQUIRE(first.has_value());
  const std::uint64_t hashed_after_first = state.tiles()->tiles_hashed();
  // A cold `create_project` memo really does hash the whole document once (Constraint 12).
  CHECK(hashed_after_first == k_raster_tiles);
  CHECK(first.value().assets_written > 0);

  const auto second = ace::commands::save_project(state, fs);
  REQUIRE(second.has_value());
  CHECK(state.tiles()->tiles_hashed() - hashed_after_first == 0);
  CHECK(second.value().assets_written == 0);
}

// The counter that separates an incremental save from a lie: write-if-absent alone would give
// the right blob count while still re-hashing the whole document.
TEST_CASE("a save after one dab hashes exactly the touched tiles") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_at(fs, scratch.root / "one_dab");
  const arbc::ObjectId content = insert_raster(state);

  REQUIRE(ace::commands::save_project(state, fs).has_value());
  const std::uint64_t hashed_before = state.tiles()->tiles_hashed();
  REQUIRE(hashed_before == k_raster_tiles);

  dab(state, content, k_one_tile_dab);

  const auto resaved = ace::commands::save_project(state, fs);
  REQUIRE(resaved.has_value());
  // |T| == 1: exactly ONE tile hashed, and exactly one new blob. The other three were memo
  // hits, because a CoW paint leaves their `BlockSlotRef` identity untouched.
  CHECK(state.tiles()->tiles_hashed() - hashed_before == 1);
  CHECK(resaved.value().assets_written == 1);
}

// The seeding half (Constraint 6): a rebuild-from-canonical open has just read every tile's
// blob name off disk, so re-hashing them on the session's first save would be pure waste.
TEST_CASE("a rebuild-from-canonical reopen seeds the memo: the next save hashes nothing") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "reseed";
  {
    AppState state = session_at(fs, root);
    insert_raster(state);
    REQUIRE(ace::commands::save_project(state, fs).has_value());
  } // the session destructs → the workspace file is released, its HousekeepingThread joined

  // Force D-open-3's rebuild branch, the manoeuvre `project_open_test` already performs.
  std::error_code ec;
  std::filesystem::remove_all(ace::project::project_layout(root).workspace_dir, ec);

  auto reopened = ace::project::open_project(fs, root);
  REQUIRE(reopened.has_value());
  REQUIRE(reopened.value().rebuilt_from_canonical);
  REQUIRE(reopened.value().tiles != nullptr);
  // Seeding is not hashing: the load records names it already knew.
  CHECK(reopened.value().tiles->tiles_hashed() == 0);
  CHECK(reopened.value().tiles->memoized() == k_raster_tiles);

  AppState state(std::move(*reopened));
  const auto saved = ace::commands::save_project(state, fs);
  REQUIRE(saved.has_value());
  CHECK(state.tiles()->tiles_hashed() == 0);
  CHECK(saved.value().assets_written == 0);
}

// Constraint 12 / D-raster_tile_store-7: the workspace-map fast path never calls
// `load_document`, so nothing seeds its memo. That degradation is DOCUMENTED behaviour —
// bounded to one full hash per process and cheaper than eagerly hashing a mapped document at
// open — not a regression for someone to later "fix" by warming the memo at map time.
TEST_CASE("a workspace-mapped open leaves the memo cold") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "cold_map";
  {
    // A content-FREE project: `open_project`'s map-then-inspect rule (A19) rejects a mapped
    // document holding content it cannot bind, so a raster-bearing project with a canonical
    // present always falls through to the rebuild branch. The composition-only project is
    // therefore the only shape that actually exercises the map path with a canonical on disk.
    AppState state = session_at(fs, root);
    REQUIRE(ace::commands::save_project(state, fs).has_value());
    REQUIRE(state.document().checkpoint().has_value());
  }

  auto mapped = ace::project::open_project(fs, root);
  REQUIRE(mapped.has_value());
  REQUIRE_FALSE(mapped.value().rebuilt_from_canonical); // the map really was kept
  REQUIRE(mapped.value().tiles != nullptr);             // never null on success (Constraint 1)
  CHECK(mapped.value().tiles->memoized() == 0);         // …and stone cold
  CHECK(mapped.value().tiles->tiles_hashed() == 0);

  // The cost of a cold memo, made explicit: the first save of this session hashes every tile
  // it now holds, and every save after it is incremental.
  AppState state(std::move(*mapped));
  insert_raster(state);
  REQUIRE(ace::commands::save_project(state, fs).has_value());
  const std::uint64_t hashed_after_first = state.tiles()->tiles_hashed();
  CHECK(hashed_after_first == k_raster_tiles);

  REQUIRE(ace::commands::save_project(state, fs).has_value());
  CHECK(state.tiles()->tiles_hashed() == hashed_after_first);
}

// Constraint 11 — the leaf's CORRECTNESS assertion, standing in for a rendered golden (this
// leaf renders nothing). Memoisation changes how much work a save does, never what it writes:
// the same document state must publish the same `project.arbc` bytes and the same `assets/`
// blob set with the memo attached and without it.
TEST_CASE("a memoised save is byte-identical to a non-memoised save") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_at(fs, scratch.root / "identity");
  const arbc::ObjectId content = insert_raster(state);
  dab(state, content, k_one_tile_dab); // two DISTINCT tile contents, so the blob set is not trivial

  // Both saves are the FIRST into their destination, which is the only comparison that means
  // anything: a memo hit resolves inside the memo and never reaches the asset sink, so a memo
  // carried into a destination it was not built against would legitimately write nothing (see
  // the note in `project::save_project_as`). The session's memo is still cold here.
  const ace::project::ProjectLayout memoised = state.layout();
  const ace::project::ProjectLayout plain =
      ace::project::project_layout(scratch.root / "out_plain");
  REQUIRE(ace::project::save_project(fs, memoised, state.document(), state.registry(), {},
                                     state.tiles())
              .has_value());
  REQUIRE(ace::project::save_project(fs, plain, state.document(), state.registry(), {},
                                     /*tiles=*/nullptr)
              .has_value());

  const std::set<std::string> memoised_blobs = blob_names(memoised.assets_dir);
  CHECK(read_file(memoised.canonical) == read_file(plain.canonical));
  CHECK(memoised_blobs == blob_names(plain.assets_dir));
  CHECK_FALSE(memoised_blobs.empty());

  // …and the WARM memo changes only the work, not the output: re-saving into the same root
  // through the now-primed memo re-hashes nothing, writes nothing, and leaves both the
  // canonical bytes and the blob set exactly as they were.
  const std::string canonical_before = read_file(memoised.canonical);
  const std::uint64_t hashed_before = state.tiles()->tiles_hashed();
  const auto resaved = ace::project::save_project(fs, memoised, state.document(), state.registry(),
                                                  {}, state.tiles());
  REQUIRE(resaved.has_value());
  CHECK(state.tiles()->tiles_hashed() == hashed_before);
  CHECK(resaved.value().assets_written == 0);
  CHECK(read_file(memoised.canonical) == canonical_before);
  CHECK(blob_names(memoised.assets_dir) == memoised_blobs);
}

// Constraint 5 / D-raster_tile_store-3. Fails without `ctx.set_storage_format(...)`: the save
// runs at `SaveContext`'s own default and silently re-authors a 32f painting at 16f — a
// precision downgrade that also renames every blob in `assets/`, and the reason a memo seeded
// from disk would otherwise miss on every entry.
TEST_CASE("save re-emits the document's authored storage format") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "storage_format";
  {
    AppState state = session_at(fs, root);
    insert_raster(state);
    state.document().set_storage_format(arbc::PixelFormat::Rgba32fLinearPremul);
    REQUIRE(ace::commands::save_project(state, fs).has_value());
  }

  std::error_code ec;
  std::filesystem::remove_all(ace::project::project_layout(root).workspace_dir, ec);

  auto reopened = ace::project::open_project(fs, root);
  REQUIRE(reopened.has_value());
  REQUIRE(reopened.value().rebuilt_from_canonical);
  CHECK(reopened.value().document->storage_format() == arbc::PixelFormat::Rgba32fLinearPremul);
  // …and the memo seeded at THAT format, so the copy's first save is still free.
  CHECK(reopened.value().tiles->memoized() == k_raster_tiles);
}

// THE MEMO IS SCOPED TO ONE ASSET DESTINATION, and Save As is where that bites. A memo hit is
// resolved entirely inside the memo — the codec dispatches no encode and never hands the tile
// to the `AssetSink` (`codec_raster.cpp:155`) — so sharing the session's memo into a fresh root
// would publish a `project.arbc` naming tile blobs nobody ever wrote there: a pixel-less copy.
// `save_project_as` therefore takes no store at all. This case is the regression witness for
// that: the copy is COMPLETE, and the session's own memo comes through untouched.
TEST_CASE("Save As publishes a complete copy and leaves the session memo untouched") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "save_as_source";
  AppState state = session_at(fs, root);
  const arbc::ObjectId content = insert_raster(state);
  dab(state, content, k_one_tile_dab);

  REQUIRE(ace::commands::save_project(state, fs).has_value());
  const std::uint64_t hashed_before = state.tiles()->tiles_hashed();
  const std::set<std::string> source_blobs = blob_names(state.layout().assets_dir);
  REQUIRE(source_blobs.size() > 1); // the dab made a second distinct tile, so this is not vacuous

  const std::filesystem::path target = scratch.root / "save_as_copy";
  const auto copied = ace::project::save_project_as(fs, target, state.document(), state.registry());
  REQUIRE(copied.has_value());
  // Every blob really landed under the new root — the copy is portable on its own.
  CHECK(copied.value().assets_written == source_blobs.size());
  CHECK(blob_names(ace::project::project_layout(target).assets_dir) == source_blobs);
  // The re-hash the copy paid went to the codec's own throwaway store, not to the session's:
  // this session's counter is untouched…
  CHECK(state.tiles()->tiles_hashed() == hashed_before);
  // …and its next plain Save into its OWN root is still fully incremental.
  const auto resaved = ace::commands::save_project(state, fs);
  REQUIRE(resaved.has_value());
  CHECK(state.tiles()->tiles_hashed() == hashed_before);
  CHECK(resaved.value().assets_written == 0);
}

// Constraint 2: the memo holds owning `BlockRef` pins into the document's pool, so releasing
// it AFTER the document is a use-after-free. Declaration order handles destruction; the ASan
// lane (§9.1) is the assertion here — this case exists to put a SEEDED memo through both a
// move-construct and a teardown.
TEST_CASE("an AppState carrying a seeded memo destroys and moves cleanly") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_at(fs, scratch.root / "lifetime");
  const arbc::ObjectId content = insert_raster(state);
  dab(state, content, k_one_tile_dab);
  REQUIRE(ace::commands::save_project(state, fs).has_value());
  REQUIRE(state.tiles()->memoized() > 0); // there really are pins to release

  AppState moved(std::move(state));
  REQUIRE(moved.tiles() != nullptr);
  CHECK(moved.tiles()->memoized() > 0);
  // A further save through the moved-to session still hits the memo it carried over.
  const std::uint64_t hashed_before = moved.tiles()->tiles_hashed();
  REQUIRE(ace::commands::save_project(moved, fs).has_value());
  CHECK(moved.tiles()->tiles_hashed() == hashed_before);
} // both the moved-from husk and the live session unwind here
