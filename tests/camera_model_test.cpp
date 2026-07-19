// editor.cameras.model — L1 headless Catch2 units for shot cameras as persisted
// scene objects (A14, D-model-1..5). Cameras are the editor's FIRST custom libarbc
// `Content` kind (`org.arbc.camera`, non-rendering): create / name / "new shot from
// view", the `cameras()` read accessor, the non-rendering invariant, and the
// `project.arbc` roundtrip through the registered codec + registry-seeded bridge.
// No ImGui/GL/SDL (Constraint 1); runs under the ASan/TSan legs over the live
// workspace-backed Document (A4/§9).

#include <ace/commands/app_state.hpp>
#include <ace/commands/selection.hpp>
#include <ace/interact/interact.hpp>
#include <ace/platform/filesystem.hpp>
#include <ace/project/project.hpp>
#include <ace/project/save.hpp>
#include <ace/render/render.hpp>
#include <ace/scene/camera.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/builtin_kinds.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/journal.hpp>
#include <arbc/model/model.hpp>
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using ace::commands::AppState;
using ace::commands::Command;
using ace::commands::dispatch;
using ace::scene::Camera;
using ace::scene::CameraContent;
using ace::scene::Resolution;

namespace {

struct ScratchDir {
  std::filesystem::path root;
  ScratchDir() : root(std::filesystem::temp_directory_path() / "ace_camera_model_test") {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

// A registry with the built-ins + the camera kind — what a load path (or a standalone
// save) needs to round-trip a camera (mirrors AppState's own seeding).
arbc::Registry camera_registry() {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  return registry;
}

// A fresh workspace-backed session with a root composition to place cameras in (a
// fresh `create_project` document is empty; canvas work adds the composition).
AppState session_with_composition(const ScratchDir& scratch, const ace::platform::FileSystem& fs,
                                  const char* leaf) {
  auto created = ace::project::create_project(fs, scratch.root / leaf);
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  dispatch(state, Command{"add_composition",
                          [](arbc::Document& doc) { doc.add_composition(64.0, 64.0); }});
  return state;
}

// Build the add-camera command bound to `state`'s registry (so the stored kind token
// matches the save-side bridge). Mirrors how a UI leaf will stitch the shipped L1
// primitives into a dispatchable, undoable action (D-model-4).
Command add_camera_command(const arbc::Registry& registry, std::string name, Resolution resolution,
                           const arbc::Affine& frame) {
  return Command{"add_camera",
                 [&registry, name = std::move(name), resolution, frame](arbc::Document& doc) {
                   ace::scene::add_camera(doc, registry, name, resolution, frame);
                 }};
}

// Author cameras (via `author`) in a fresh workspace-backed session, publish
// `project.arbc`, then release the live session and delete the `workspace/` subtree so
// a subsequent `open_project` MUST take the REBUILD-FROM-CANONICAL path — the load
// path `editor.cameras.reopen_codec` fixes (mirrors project_save_test's shed idiom).
// Returns the project root for the reopen. `author` uses `state.registry()` (seeded
// with the camera kind) so the stored token matches the save-side bridge.
std::filesystem::path persist_and_shed_workspace(const ScratchDir& scratch,
                                                 const ace::platform::FileSystem& fs,
                                                 const char* leaf,
                                                 const std::function<void(AppState&)>& author) {
  const std::filesystem::path root = scratch.root / leaf;
  {
    AppState state = session_with_composition(scratch, fs, leaf);
    author(state);
    REQUIRE(ace::commands::save_project(state, fs).has_value());
  } // state destructs → workspace unmapped, its HousekeepingThread joined
  std::error_code ec;
  std::filesystem::remove_all(ace::project::project_layout(root).workspace_dir, ec);
  return root;
}

// Like `persist_and_shed_workspace` but LEAVES the `workspace/` subtree in place: the
// released session's checkpointed workspace file stays on disk, so a subsequent
// `open_project` sees it present and would prefer the D-open-3 map fast path — the
// route `editor.cameras.workspace_reopen_slab` guards for editor-kind sessions (A15).
// Returns the project root for the reopen.
std::filesystem::path persist_keeping_workspace(const ScratchDir& scratch,
                                                const ace::platform::FileSystem& fs,
                                                const char* leaf,
                                                const std::function<void(AppState&)>& author) {
  const std::filesystem::path root = scratch.root / leaf;
  {
    AppState state = session_with_composition(scratch, fs, leaf);
    author(state);
    REQUIRE(ace::commands::save_project(state, fs).has_value());
  } // state destructs → workspace checkpointed + unmapped, HousekeepingThread joined
  return root; // workspace/ deliberately NOT removed — the fast path stays reachable
}

} // namespace

TEST_CASE("a fresh scratch project reports zero cameras") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "empty");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  CHECK(ace::scene::cameras(state.document()).empty()); // no composition, no shots
}

TEST_CASE("add_camera writes cameras cameras() reads back in order") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "add");
  const arbc::Registry registry = camera_registry();

  const arbc::Affine frame_a = arbc::Affine::translation(3.0, 4.0);
  const arbc::Affine frame_b{2.0, 0.0, 0.0, 2.0, 10.0, 20.0};
  dispatch(state, add_camera_command(registry, "hero", Resolution{1920, 1080}, frame_a));
  dispatch(state, add_camera_command(registry, "wide", Resolution{3840, 2160}, frame_b));

  const std::vector<Camera> cams = ace::scene::cameras(state.document());
  REQUIRE(cams.size() == 2);
  CHECK(cams[0].name == "hero");
  CHECK(cams[0].resolution == Resolution{1920, 1080});
  CHECK(cams[0].frame.tx == 3.0);
  CHECK(cams[0].frame.ty == 4.0);
  CHECK(cams[0].id.valid());
  CHECK(cams[0].layer.valid());
  CHECK(cams[1].name == "wide");
  CHECK(cams[1].resolution == Resolution{3840, 2160});
  CHECK(cams[1].frame.a == 2.0);
  CHECK(cams[1].frame.tx == 10.0);
}

TEST_CASE("add_camera with no root composition is a no-op") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  auto created = ace::project::create_project(fs, scratch.root / "nocomp");
  REQUIRE(created.has_value());
  AppState state(std::move(*created));
  const arbc::Registry registry = camera_registry();
  const arbc::ObjectId id = ace::scene::add_camera(state.document(), registry, "x",
                                                   Resolution{100, 100}, arbc::Affine::identity());
  CHECK_FALSE(id.valid());
  CHECK(ace::scene::cameras(state.document()).empty());
}

TEST_CASE("rename_camera changes the name, preserving resolution, frame, and order") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "rename");
  const arbc::Registry registry = camera_registry();

  const arbc::Affine frame_a = arbc::Affine::translation(3.0, 4.0);
  const arbc::Affine frame_b = arbc::Affine::translation(7.0, 8.0);
  dispatch(state, add_camera_command(registry, "first", Resolution{800, 600}, frame_a));
  dispatch(state, add_camera_command(registry, "second", Resolution{1024, 768}, frame_b));

  const Camera before = ace::scene::cameras(state.document())[0];
  const arbc::ObjectId first_id = before.id;
  const arbc::ObjectId first_layer = before.layer;
  const arbc::ObjectId second_id = ace::scene::cameras(state.document())[1].id;
  dispatch(state, Command{"rename_camera", [&registry, first_id](arbc::Document& doc) {
                            ace::scene::rename_camera(doc, registry, first_id, "renamed");
                          }});

  const std::vector<Camera> cams = ace::scene::cameras(state.document());
  REQUIRE(cams.size() == 2);
  CHECK(cams[0].name == "renamed"); // renamed in place (same order index)
  CHECK(cams[0].resolution == Resolution{800, 600});
  CHECK(cams[0].frame.tx == 3.0);
  // The headline: the renamed camera is the SAME object — content id AND binding-layer
  // id survive (fails against the old detach/re-add rename, passes on the in-place one).
  CHECK(cams[0].id == first_id);
  CHECK(cams[0].layer == first_layer);
  CHECK(cams[1].name == "second"); // untouched
  CHECK(cams[1].frame.tx == 7.0);
  CHECK(cams[1].id == second_id); // the sibling keeps its identity too
}

TEST_CASE("an ObjectId-keyed selection survives a rename (the D7 payoff)") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "sel");
  const arbc::Registry registry = camera_registry();

  dispatch(state, add_camera_command(registry, "first", Resolution{800, 600},
                                     arbc::Affine::translation(3.0, 4.0)));
  const arbc::ObjectId first_id = ace::scene::cameras(state.document())[0].id;

  // The shared selection remembers the camera by ObjectId (commands::Selection). A
  // rename that minted a new id would silently drop it; the in-place rename keeps it.
  ace::commands::Selection selection;
  selection.select(first_id);
  REQUIRE(selection.contains(first_id));

  dispatch(state, Command{"rename_camera", [&registry, first_id](arbc::Document& doc) {
                            ace::scene::rename_camera(doc, registry, first_id, "renamed");
                          }});

  CHECK(selection.contains(first_id)); // the selection still names the (same) camera
  const std::vector<Camera> cams = ace::scene::cameras(state.document());
  REQUIRE(cams.size() == 1);
  CHECK(cams[0].id == first_id);
  CHECK(cams[0].name == "renamed");
}

TEST_CASE("rename is one journal entry, undone/redone in place on the same ObjectId") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "rename_undo");
  const arbc::Registry registry = camera_registry();
  const arbc::Journal& journal = state.document().journal();

  dispatch(state, add_camera_command(registry, "first", Resolution{800, 600},
                                     arbc::Affine::translation(3.0, 4.0)));
  const arbc::ObjectId first_id = ace::scene::cameras(state.document())[0].id;

  const std::size_t depth_before = journal.depth();
  dispatch(state, Command{"rename_camera", [&registry, first_id](arbc::Document& doc) {
                            ace::scene::rename_camera(doc, registry, first_id, "renamed");
                          }});
  CHECK(journal.depth() == depth_before + 1); // exactly ONE undo unit (down from two)
  CHECK(ace::scene::cameras(state.document())[0].name == "renamed");

  // Undo restores the OLD name on the SAME object — the camera is still present at its
  // ObjectId (not removed, unlike the add-undo path).
  const auto undone = ace::commands::undo(state);
  CHECK(undone.moved);
  const std::vector<Camera> after_undo = ace::scene::cameras(state.document());
  REQUIRE(after_undo.size() == 1);
  CHECK(after_undo[0].id == first_id);
  CHECK(after_undo[0].name == "first");

  // Redo re-applies the new name in place.
  const auto redone = ace::commands::redo(state);
  CHECK(redone.moved);
  const std::vector<Camera> after_redo = ace::scene::cameras(state.document());
  REQUIRE(after_redo.size() == 1);
  CHECK(after_redo[0].id == first_id);
  CHECK(after_redo[0].name == "renamed");
}

TEST_CASE("a camera is bound editable and its state calls route to it (tripwire zero)") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "editable");
  const arbc::Registry registry = camera_registry();
  arbc::Document& doc = state.document();

  dispatch(state, add_camera_command(registry, "first", Resolution{800, 600},
                                     arbc::Affine::translation(3.0, 4.0)));
  const arbc::ObjectId camera_id = ace::scene::cameras(doc)[0].id;

  // add_camera auto-binds the editable content (Document::add_content) — no host wiring.
  CHECK(doc.editable_binding().bound(camera_id));

  // A full add->rename->undo->redo cycle must land every state call on the camera that
  // owns the handle: the misrouting tripwire stays zero (Constraint 6).
  dispatch(state, Command{"rename_camera", [&registry, camera_id](arbc::Document& d) {
                            ace::scene::rename_camera(d, registry, camera_id, "renamed");
                          }});
  ace::commands::undo(state);
  ace::commands::redo(state);
  CHECK(doc.editable_binding().unrouted_state_calls() == 0);
}

TEST_CASE("the editable facet is inert on empty / out-of-range state handles") {
  // Drive the `arbc::Editable` store's guard branches directly (the writer/drain
  // contract's null + stale-handle cases the Document path never reaches): a no-state
  // handle and a has_state() handle whose slot is past the table are both no-ops.
  CameraContent cam("guarded", Resolution{320, 240});

  const arbc::StateHandle none{};
  REQUIRE_FALSE(none.has_state());
  CHECK(cam.state_cost(none) == 0); // no state -> zero cost
  cam.retain(none);                 // no-op, no throw
  cam.release(none);                // no-op, no throw

  // A live in-range handle carries a real (non-guard) cost.
  const arbc::StateHandle base = cam.capture();
  REQUIRE(base.has_state());
  CHECK(cam.state_cost(base) > 0);

  // A has_state() handle whose slot is past the table is ignored by release.
  const arbc::StateHandle out_of_range{arbc::SlotIndex{9999}};
  REQUIRE(out_of_range.has_state());
  cam.release(out_of_range); // no-op, no throw

  // Releasing the never-retained base underflow-guards to a no-op (refcount already 0),
  // so the version stays live.
  cam.release(base);
  CHECK(cam.camera_name() == "guarded");

  // Restoring a no-state base clears the accessors (the base-invalidated read path).
  cam.restore(none);
  CHECK(cam.camera_name().empty());
  CHECK(cam.resolution() == Resolution{});
}

TEST_CASE("renaming after an undo recycles a freed version slot") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "rename_churn");
  const arbc::Registry registry = camera_registry();
  arbc::Document& doc = state.document();

  dispatch(state, add_camera_command(registry, "v0", Resolution{800, 600},
                                     arbc::Affine::translation(1.0, 2.0)));
  const arbc::ObjectId id = ace::scene::cameras(doc)[0].id;

  const auto rename = [&](const char* to) {
    dispatch(state, Command{"rename_camera", [&registry, id, to](arbc::Document& d) {
                              ace::scene::rename_camera(d, registry, id, to);
                            }});
  };

  rename("v1");               // mints a 2nd version; the 1st is the journal `before`
  ace::commands::undo(state); // back to "v0"; "v1" now lives only on the redo stack
  rename("v2");               // truncates the redo entry -> "v1"'s version is released and
                              // drained (drain_between_transactions), freeing its slot
  rename("v3");               // this mint must REUSE the recycled slot, not grow the table

  const std::vector<Camera> cams = ace::scene::cameras(doc);
  REQUIRE(cams.size() == 1);
  CHECK(cams[0].id == id); // the same object across all the churn
  CHECK(cams[0].name == "v3");
  CHECK(cams[0].resolution == Resolution{800, 600}); // resolution preserved throughout
}

TEST_CASE("rename_camera on a non-camera content id is a no-op") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "rename_noncam");
  const arbc::Registry registry = camera_registry();
  arbc::Document& doc = state.document();

  // A solid cell (not a camera): resolving it and dynamic_cast'ing to CameraContent
  // fails, so the rename declines rather than editing an unrelated content.
  const arbc::ObjectId cell =
      doc.add_content(std::make_shared<arbc::SolidContent>(arbc::Rgba{0.0F, 0.0F, 0.0F, 1.0F}));
  CHECK_FALSE(ace::scene::rename_camera(doc, registry, cell, "nope"));
}

TEST_CASE("rename_camera on an unknown id is a no-op") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "rename_miss");
  const arbc::Registry registry = camera_registry();
  CHECK_FALSE(ace::scene::rename_camera(state.document(), registry, arbc::ObjectId{999}, "nope"));
}

TEST_CASE("new_shot_from_view snapshots the viewport framing into a shot") {
  // A viewport camera: 2x uniform scale, panned by (30,40) device px (composition ->
  // device). The shot's frame is the INVERSE (device -> composition), and its
  // resolution is the pane size.
  const arbc::Affine viewport{2.0, 0.0, 0.0, 2.0, 30.0, 40.0};
  const ace::interact::ShotFraming shot = ace::interact::new_shot_from_view(viewport, 1920, 1080);
  CHECK(shot.width == 1920);
  CHECK(shot.height == 1080);
  // inverse of a scale-2 + translate: 0.5 linear, translation = -t/scale.
  CHECK(shot.frame.a == 0.5);
  CHECK(shot.frame.d == 0.5);
  CHECK(shot.frame.tx == -15.0);
  CHECK(shot.frame.ty == -20.0);

  // Round-trip identity: promoting a shot then recovering its camera reproduces the
  // framing (what editor.cameras.export will derive its render viewport from).
  const std::optional<arbc::Affine> recovered = shot.frame.inverse();
  REQUIRE(recovered.has_value());
  CHECK(recovered->a == viewport.a);
  CHECK(recovered->tx == viewport.tx);
}

TEST_CASE("new_shot_from_view on a degenerate pane yields an identity frame") {
  const ace::interact::ShotFraming shot =
      ace::interact::new_shot_from_view(arbc::Affine::identity(), 0, 720);
  CHECK(shot.width == 0);
  CHECK(shot.frame.a == 1.0); // identity — nothing to promote
  CHECK(shot.frame.tx == 0.0);
}

TEST_CASE("new_shot_from_view on a non-invertible camera keeps an identity frame") {
  // A zero-scale (collapsed) viewport camera has no inverse: the pane is valid but
  // there is no framing to promote, so the frame stays identity while the resolution
  // still reflects the pane.
  const ace::interact::ShotFraming shot =
      ace::interact::new_shot_from_view(arbc::Affine::scaling(0.0, 0.0), 800, 600);
  CHECK(shot.width == 800);
  CHECK(shot.height == 600);
  CHECK(shot.frame.a == 1.0);
  CHECK(shot.frame.tx == 0.0);
}

TEST_CASE("dispatching add_camera is undoable: undo removes the camera, redo restores it") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "undo");
  const arbc::Registry registry = camera_registry();
  const arbc::Journal& journal = state.document().journal();

  const std::size_t depth_before = journal.depth();
  dispatch(state, add_camera_command(registry, "hero", Resolution{1920, 1080},
                                     arbc::Affine::translation(1.0, 2.0)));
  CHECK(ace::scene::cameras(state.document()).size() == 1);
  CHECK(journal.depth() > depth_before); // the create advanced the journal

  // A single undo detaches the camera's layer: it leaves the composition membership
  // `cameras()` reads, so the camera disappears (the observable D15 contract).
  const auto undone = ace::commands::undo(state);
  CHECK(undone.moved);
  CHECK(ace::scene::cameras(state.document()).empty());

  // A single redo re-attaches it.
  const auto redone = ace::commands::redo(state);
  CHECK(redone.moved);
  REQUIRE(ace::scene::cameras(state.document()).size() == 1);
  CHECK(ace::scene::cameras(state.document())[0].name == "hero");
}

TEST_CASE("a camera contributes zero pixels: render is byte-identical with and without it") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "render");
  const arbc::Registry registry = camera_registry();
  arbc::Document& doc = state.document();

  // A visible solid cell so the frame is non-trivial.
  const arbc::ObjectId cell =
      doc.add_content(std::make_shared<arbc::SolidContent>(arbc::Rgba{0.0F, 0.5F, 0.0F, 1.0F}));
  arbc::ObjectId comp_id;
  {
    const arbc::DocStatePtr state_ptr = doc.pin();
    const arbc::CompositionRecord* rec = nullptr;
    REQUIRE(state_ptr->find_first_composition(comp_id, rec));
  }
  doc.attach_layer(comp_id, doc.add_layer(cell, arbc::Affine::identity()));

  const ace::render::Srgb8Image before = ace::render::render_document_srgb8(doc, 64, 64);
  REQUIRE(before.pixels.size() == 64u * 64u * 4u);

  dispatch(state, add_camera_command(registry, "obs", Resolution{1920, 1080},
                                     arbc::Affine::translation(5.0, 5.0)));
  REQUIRE(ace::scene::cameras(doc).size() == 1);

  const ace::render::Srgb8Image after = ace::render::render_document_srgb8(doc, 64, 64);
  CHECK(after.pixels == before.pixels); // the camera drew nothing (Constraint 5)
}

TEST_CASE("the camera codec round-trips every escape class in a name") {
  const arbc::Registry registry = camera_registry();
  const arbc::KindCodec* codec = registry.codec(CameraContent::kind_id);
  REQUIRE(codec != nullptr);

  // Every string metacharacter the hand-rolled JSON escaper/unescaper must handle,
  // plus a raw control byte (\u00xx) and non-ASCII UTF-8.
  const std::vector<std::string> names = {
      "",           "plain",    "a\"b",     "a\\b",     "tab\there",
      "nl\nhere",   "cr\rhere", "bs\bhere", "ff\fhere", std::string("ctrl\x01\x1f", 7),
      "unicode π ✓"};
  for (const std::string& name : names) {
    const CameraContent camera(name, Resolution{640, 480});
    const auto text = codec->serialize(camera);
    REQUIRE(text.has_value());
    const auto content = codec->deserialize(*text, {}, arbc::ObjectId{});
    REQUIRE(content.has_value());
    const auto* reloaded = dynamic_cast<const CameraContent*>(content->get());
    REQUIRE(reloaded != nullptr);
    CHECK(reloaded->camera_name() == name);
    CHECK(reloaded->resolution() == Resolution{640, 480});
  }
}

TEST_CASE("the camera codec surfaces malformed input as an error value") {
  const arbc::Registry registry = camera_registry();
  const arbc::KindCodec* codec = registry.codec(CameraContent::kind_id);
  REQUIRE(codec != nullptr);

  // serialize refuses a non-camera content (the dynamic_cast guard).
  const arbc::SolidContent solid(arbc::Rgba{0.0F, 0.0F, 0.0F, 1.0F});
  CHECK_FALSE(codec->serialize(solid).has_value());

  // deserialize rejects every malformed shape — a value, never a throw.
  const std::vector<std::string> bad = {
      "not-json",                                         // not an object
      "{}",                                               // empty object
      R"({"name":"a"})",                                  // missing width/height
      R"({"name":"a","width":10})",                       // missing height
      R"({"width":10,"height":20})",                      // missing name
      R"({"name":"a","width":10,"height":20,"extra":1})", // unknown field
      R"({"name":"a","width":"ten","height":20})",        // width not a number
      R"({"name":"unterminated)",                         // unterminated string
      R"({"name":"bad\x","width":1,"height":1})",         // bad escape
      R"({"name":"bad\uZZZZ","width":1,"height":1})",     // bad \u hex
      R"({"name":"a","width":9999999999,"height":1})",    // width overflow guard
  };
  for (const std::string& params : bad) {
    INFO("params = " << params);
    CHECK_FALSE(codec->deserialize(params, {}, arbc::ObjectId{}).has_value());
  }
}

TEST_CASE("the camera codec decodes JSON \\u escapes into UTF-8 on load") {
  const arbc::Registry registry = camera_registry();
  const arbc::KindCodec* codec = registry.codec(CameraContent::kind_id);
  REQUIRE(codec != nullptr);
  // Literal backslash-u escapes (not raw UTF-8): ASCII 1-byte (U+0041 'A'), Latin-1
  // 2-byte (U+00E9 'é'), CJK 3-byte (U+4E2D '中') — the decode path an external or
  // canonicalized producer can emit even though our serializer emits raw UTF-8.
  const auto content = codec->deserialize(
      "{\"name\":\"\\u0041\\u00e9\\u4e2d\",\"width\":2,\"height\":3}", {}, arbc::ObjectId{});
  REQUIRE(content.has_value());
  const auto* cam = dynamic_cast<const CameraContent*>(content->get());
  REQUIRE(cam != nullptr);
  CHECK(cam->camera_name() == "Aé中"); // "Aé中" as UTF-8
  CHECK(cam->resolution() == Resolution{2, 3});
}

TEST_CASE("CameraContent is a non-rendering static observer, and its factory constructs") {
  const CameraContent cam("obs", Resolution{10, 20});
  REQUIRE(cam.bounds().has_value());
  CHECK(cam.bounds()->empty()); // culled: contributes zero pixels
  CHECK(cam.stability() == arbc::Stability::Static);
  CHECK_FALSE(cam.time_extent().has_value());

  // The factory is the codec's plugin-present witness (`builtin_codecs`): it must
  // construct a CameraContent from a (here ignored) config.
  const arbc::Registry registry = camera_registry();
  const arbc::ContentFactory* factory = registry.factory(CameraContent::kind_id);
  REQUIRE(factory != nullptr);
  const auto made = (*factory)("");
  REQUIRE(made.has_value());
  CHECK(dynamic_cast<const CameraContent*>(made->get()) != nullptr);
}

TEST_CASE("register_camera_kind is idempotent") {
  arbc::Registry registry;
  arbc::register_builtin_kinds(registry);
  ace::scene::register_camera_kind(registry);
  ace::scene::register_camera_kind(registry); // duplicate: first-wins, no throw
  CHECK(registry.codec(CameraContent::kind_id) != nullptr);
}

TEST_CASE("cameras round-trip through save_project -> load_document with the codec") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  AppState state = session_with_composition(scratch, fs, "persist");

  // Use the SESSION's registry (seeded with the camera kind in AppState's ctor) so
  // the author-time token matches the save-side bridge (D-model-2). One name carries
  // JSON metacharacters + non-ASCII to exercise the codec's escaping both ways.
  const arbc::Registry& registry = state.registry();
  const arbc::Affine frame_a = arbc::Affine::translation(3.0, 4.0);
  const arbc::Affine frame_b{2.0, 0.0, 0.0, 2.0, 11.0, 22.0};
  dispatch(state,
           add_camera_command(registry, "hero \"quote\"\n\tπ", Resolution{1920, 1080}, frame_a));
  dispatch(state, add_camera_command(registry, "wide", Resolution{3840, 2160}, frame_b));

  // Rename the first camera in place, then persist: the codec's `serialize` reads the
  // LIVE base state, so the saved bytes must carry the post-rename name (Constraint 4/7).
  // The new name still carries JSON metacharacters + non-ASCII to exercise escaping.
  const arbc::ObjectId first_id = ace::scene::cameras(state.document())[0].id;
  dispatch(state, Command{"rename_camera", [&registry, first_id](arbc::Document& doc) {
                            ace::scene::rename_camera(doc, registry, first_id, "renamed\t\"✓\"");
                          }});

  const auto saved = ace::commands::save_project(state, fs);
  REQUIRE(saved.has_value());

  // Reload the published bytes into a fresh document through the real codec path.
  const auto bytes = fs.read_file(state.layout().canonical);
  REQUIRE(bytes.has_value());
  const arbc::Registry load_registry = camera_registry();
  arbc::Document reloaded;
  arbc::KindBridge bridge;
  const auto loaded = arbc::load_document(*bytes, reloaded, bridge, load_registry);
  REQUIRE(loaded.has_value());

  const std::vector<Camera> cams = ace::scene::cameras(reloaded);
  REQUIRE(cams.size() == 2);
  CHECK(cams[0].name == "renamed\t\"✓\""); // the POST-RENAME name survived the roundtrip
  CHECK(cams[0].resolution == Resolution{1920, 1080});
  CHECK(cams[0].frame.tx == 3.0);
  CHECK(cams[0].frame.ty == 4.0);
  CHECK(cams[1].name == "wide");
  CHECK(cams[1].resolution == Resolution{3840, 2160});
  CHECK(cams[1].frame.a == 2.0);
  CHECK(cams[1].frame.tx == 11.0);
}

// --- editor.cameras.reopen_codec: cameras survive a COLD reopen as live kinds -------

TEST_CASE("reopen from canonical WITH the callback restores cameras as live CameraContent") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;

  arbc::ObjectId first_id;
  const arbc::Affine frame_a = arbc::Affine::translation(3.0, 4.0);
  const arbc::Affine frame_b{2.0, 0.0, 0.0, 2.0, 11.0, 22.0};
  const std::filesystem::path root =
      persist_and_shed_workspace(scratch, fs, "reopen_live", [&](AppState& state) {
        const arbc::Registry& reg = state.registry();
        dispatch(state, add_camera_command(reg, "hero \"π\"", Resolution{1920, 1080}, frame_a));
        dispatch(state, add_camera_command(reg, "wide", Resolution{3840, 2160}, frame_b));
        first_id = ace::scene::cameras(state.document())[0].id;
      });

  // Reopen through the rebuild-from-canonical path WITH the camera-kind callback: the
  // transient load registry learns org.arbc.camera, so each record decodes to a live
  // CameraContent (the assertion that FAILS against pre-callback code, Constraint 2).
  auto reopened = ace::project::open_project(fs, root, ace::scene::register_camera_kind);
  REQUIRE(reopened.has_value());
  REQUIRE(reopened.value().rebuilt_from_canonical); // the load path, not the map fast path
  const arbc::Document& doc = *reopened.value().document;

  const std::vector<Camera> cams = ace::scene::cameras(doc);
  REQUIRE(cams.size() == 2);     // layer order preserved
  CHECK(cams[0].id == first_id); // ObjectId round-trips
  CHECK(cams[0].name == "hero \"π\"");
  CHECK(cams[0].resolution == Resolution{1920, 1080});
  CHECK(cams[0].frame.tx == 3.0);
  CHECK(cams[0].frame.ty == 4.0);
  CHECK(cams[1].name == "wide");
  CHECK(cams[1].resolution == Resolution{3840, 2160});
  CHECK(cams[1].frame.a == 2.0);
  CHECK(cams[1].frame.tx == 11.0);

  // The reopened content is the real typed kind, NOT a degraded placeholder (a
  // placeholder would fail this cast — cf. the WITHOUT-callback test below).
  const arbc::Content* content = doc.resolve(cams[0].id);
  REQUIRE(content != nullptr);
  CHECK(dynamic_cast<const CameraContent*>(content) != nullptr);
}

TEST_CASE("reopen from canonical WITHOUT the callback degrades a camera to a placeholder") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;

  arbc::ObjectId cam_id;
  const std::filesystem::path root =
      persist_and_shed_workspace(scratch, fs, "reopen_degrade", [&](AppState& state) {
        dispatch(state, add_camera_command(state.registry(), "hero", Resolution{1920, 1080},
                                           arbc::Affine::translation(3.0, 4.0)));
        cam_id = ace::scene::cameras(state.document())[0].id;
      });

  // Reopen with the default-empty callback (Constraint 6): the load registry never
  // learns org.arbc.camera, so the record decodes to a non-typed placeholder. Pins that
  // the callback — not some incidental change — is what restores the kind.
  auto reopened = ace::project::open_project(fs, root);
  REQUIRE(reopened.has_value());
  REQUIRE(reopened.value().rebuilt_from_canonical);
  const arbc::Document& doc = *reopened.value().document;

  CHECK(ace::scene::cameras(doc).empty()); // cameras() no longer recognizes it
  // The record still resolves (ObjectId round-trips) but decodes to a non-typed
  // fallback (`arbc::PlaceholderContent`), NOT the live CameraContent kind.
  const arbc::Content* content = doc.resolve(cam_id);
  REQUIRE(content != nullptr);
  CHECK(dynamic_cast<const CameraContent*>(content) == nullptr);
}

// --- editor.cameras.workspace_reopen_slab: a saved camera project reopens WITH the
// workspace present without hitting the aborting map fast path (A15) ------------------
//
// Supersedes the deferred NOTE this block replaced: the fast-path-with-a-camera case
// `reopen_codec` could not land (it aborts against the pinned lib's `Model::rebuild_counts`,
// arbc `model.cpp:771`) is now a live, passing case — the editor-side open-path policy
// forces rebuild-from-canonical for editor-kind sessions, so the map path is never taken
// for a camera-bearing workspace.

TEST_CASE("a saved camera project reopens via forced rebuild with the workspace present") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;

  arbc::ObjectId first_id;
  const arbc::Affine frame_a = arbc::Affine::translation(3.0, 4.0);
  const arbc::Affine frame_b{2.0, 0.0, 0.0, 2.0, 11.0, 22.0};
  const std::filesystem::path root =
      persist_keeping_workspace(scratch, fs, "reopen_fastpath", [&](AppState& state) {
        const arbc::Registry& reg = state.registry();
        dispatch(state, add_camera_command(reg, "hero \"π\"", Resolution{1920, 1080}, frame_a));
        dispatch(state, add_camera_command(reg, "wide", Resolution{3840, 2160}, frame_b));
        first_id = ace::scene::cameras(state.document())[0].id;
      });

  // The crash-durable workspace file is present, so D-open-3 would prefer the map fast
  // path — which aborts on a checkpointed camera's non-inert StateHandle (arbc
  // model.cpp:771). The editor-kind callback + the existing canonical force
  // rebuild-from-canonical instead (A15): the call RETURNS (no abort — the case that
  // faults against today's unguarded fast path), reports the rebuild route, and restores
  // live typed cameras identically to the shed-workspace guarantee above.
  REQUIRE(fs.exists(ace::project::project_layout(root).workspace_file));
  auto reopened = ace::project::open_project(fs, root, ace::scene::register_camera_kind);
  REQUIRE(reopened.has_value());
  REQUIRE(reopened.value().rebuilt_from_canonical); // policy skipped the map fast path
  const arbc::Document& doc = *reopened.value().document;

  const std::vector<Camera> cams = ace::scene::cameras(doc);
  REQUIRE(cams.size() == 2);     // layer order preserved
  CHECK(cams[0].id == first_id); // ObjectId round-trips
  CHECK(cams[0].name == "hero \"π\"");
  CHECK(cams[0].resolution == Resolution{1920, 1080});
  CHECK(cams[0].frame.tx == 3.0);
  CHECK(cams[0].frame.ty == 4.0);
  CHECK(cams[1].name == "wide");
  CHECK(cams[1].resolution == Resolution{3840, 2160});
  CHECK(cams[1].frame.a == 2.0);
  CHECK(cams[1].frame.tx == 11.0);

  // The reopened content is the real typed kind, NOT a degraded placeholder.
  const arbc::Content* content = doc.resolve(cams[0].id);
  REQUIRE(content != nullptr);
  CHECK(dynamic_cast<const CameraContent*>(content) != nullptr);
}

TEST_CASE("without the callback a present-workspace project keeps the map fast path") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  // Camera-free so the map path does not fault (no non-inert StateHandle in the map).
  const std::filesystem::path root =
      persist_keeping_workspace(scratch, fs, "fastpath_nocb", [](AppState&) {});

  // No editor-kind callback → the fail-safe signal is absent → the workspace map fast
  // path is taken verbatim (rebuilt_from_canonical == false). Pins that the rebuild is
  // driven by the callback signal, not an unconditional change (Constraint 3).
  auto reopened = ace::project::open_project(fs, root);
  REQUIRE(reopened.has_value());
  CHECK_FALSE(reopened.value().rebuilt_from_canonical);
}

TEST_CASE("a camera-free editor session still forces rebuild (the fail-safe cost)") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root =
      persist_keeping_workspace(scratch, fs, "fastpath_freecb", [](AppState&) {});

  // WITH the callback but NO camera: the fail-safe policy cannot know the workspace is
  // camera-free without mapping it (which might abort), so it conservatively rebuilds
  // anyway. This pins the deliberate false-positive / durability cost of D-slab-2 so the
  // trade-off is visible and regression-locked (Constraint 1).
  auto reopened = ace::project::open_project(fs, root, ace::scene::register_camera_kind);
  REQUIRE(reopened.has_value());
  CHECK(reopened.value().rebuilt_from_canonical);
}

TEST_CASE("a never-saved camera-free project falls back to the map path even with the callback") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  const std::filesystem::path root = scratch.root / "fastpath_nocanon";
  {
    // create_project mints a workspace but writes NO project.arbc (publish is save's) —
    // released here so the checkpointed workspace file stays on disk with no canonical.
    AppState state = session_with_composition(scratch, fs, "fastpath_nocanon");
  }
  const ace::project::ProjectLayout layout = ace::project::project_layout(root);
  REQUIRE(fs.exists(layout.workspace_file));
  REQUIRE_FALSE(fs.exists(layout.canonical));

  // WITH the callback but NO canonical to rebuild from: forcing rebuild would only return
  // NoProject, so the policy falls back to the map fast path (Constraint 4 / D-slab-3).
  // The project is camera-free, so that map opens cleanly (rebuilt_from_canonical == false).
  auto reopened = ace::project::open_project(fs, root, ace::scene::register_camera_kind);
  REQUIRE(reopened.has_value());
  CHECK_FALSE(reopened.value().rebuilt_from_canonical);
}

TEST_CASE("a camera reopened from canonical is fully operable (rename preserves ObjectId)") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;

  arbc::ObjectId original_id;
  const std::filesystem::path root =
      persist_and_shed_workspace(scratch, fs, "reopen_operable", [&](AppState& state) {
        dispatch(state, add_camera_command(state.registry(), "hero", Resolution{1920, 1080},
                                           arbc::Affine::translation(1.0, 2.0)));
        original_id = ace::scene::cameras(state.document())[0].id;
      });

  auto reopened = ace::project::open_project(fs, root, ace::scene::register_camera_kind);
  REQUIRE(reopened.has_value());
  AppState state(std::move(*reopened));

  const arbc::ObjectId cam_id = ace::scene::cameras(state.document())[0].id;
  CHECK(cam_id == original_id); // the persisted identity survived reopen

  // rename resolves + dynamic_casts to CameraContent, then edits in place: it takes
  // effect ONLY on a live kind, proving the reopened object is the real editable camera
  // (a placeholder would fail the cast). cameras.manip / cells.selection address it too.
  dispatch(state, Command{"rename_camera", [&](arbc::Document& doc) {
                            ace::scene::rename_camera(doc, state.registry(), cam_id, "renamed");
                          }});
  const std::vector<Camera> cams = ace::scene::cameras(state.document());
  REQUIRE(cams.size() == 1);
  CHECK(cams[0].id == original_id); // rename kept the ObjectId (in-place edit)
  CHECK(cams[0].name == "renamed");
}

TEST_CASE("reopen through the callback path is a clean no-op for a camera-free project") {
  ScratchDir scratch;
  ace::platform::NativeFileSystem fs;
  // No cameras authored — just the composition session_with_composition adds.
  const std::filesystem::path root =
      persist_and_shed_workspace(scratch, fs, "reopen_empty", [](AppState&) {});

  // The callback runs harmlessly (registers the kind; nothing decodes to it) — a
  // camera-free project reopens cleanly through the callback path (Constraint 4).
  auto reopened = ace::project::open_project(fs, root, ace::scene::register_camera_kind);
  REQUIRE(reopened.has_value());
  REQUIRE(reopened.value().rebuilt_from_canonical);
  CHECK(ace::scene::cameras(*reopened.value().document).empty());
}
