# editor.cameras.export — Export via cameras (render_offline); PNG; batch

## TaskJuggler entry

- **Task:** `editor.cameras.export`, `tasks/00-editor.tji:377-382`, inside
  `task cameras "Cameras"` (`tasks/00-editor.tji:306`), inside `task editor`.
- **Effort:** `3d` · `allocate team`.
- **Depends:** `!model` (i.e. `editor.cameras.model`, `tasks/00-editor.tji:307-313`,
  `complete 100`). That is the leaf's **only** declared dependency, but every camera leaf
  after it has landed too (`manip`, `look_through`, `frame_selection`,
  `new_shot_from_view`, `mint_from_focused_canvas`, `reopen_slab_adopt`), so the seams
  below are all shipped, not promised.
- **Note (`.tji:381`):** *"A camera IS the export spec: pick camera(s) -> library
  render_offline each at its resolution -> file(s). PNG (working->sRGB8); batch (one file
  per camera) + contact sheet fall out; transparent/filled bg, N-times scale multiplier;
  async with progress. Design: D14. Refinement: tasks/refinements/export.md. Decided
  (pre-exec 2026-07-19): default background TRANSPARENT (preserves alpha; filled bg is a
  deliberate opt-in); N× multiplier default 1×; on filename collision OVERWRITE in place
  (batch export is idempotent, mirroring Save's re-dump — no auto-suffix, no prompt);
  contact sheet = uniform grid, each camera rendered then scaled to fit a common tile box
  preserving aspect, ceil(sqrt(N)) columns, thin gutters, a small per-tile camera-name
  caption, on the chosen background. NOTE: render_offline binds nested-composition
  operators as of libarbc v0.2.0 (ruoso/arbitrarycomposer#6, fixed) — nested children
  export correctly with the v0.2.0 pin (editor.canvas.arbc_v020)."*
- **Back-link:** this refinement lands at `tasks/refinements/editor.cameras/export.md`.
  The `.tji` note still carries the interim flat path `Refinement:
  tasks/refinements/export.md`, which has never existed; the closer **replaces** it with
  `Refinement: tasks/refinements/editor.cameras/export.md` and adds `complete 100` after
  `allocate team` (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji`
  here. *(Layout note for the closer: the eight sibling camera refinements live under
  `tasks/refinements/cameras/`, while `editor.cells/` and `editor.dock/` use the
  dotted-prefix form. This file follows the dotted-prefix form the task brief specified;
  if the closer prefers the `cameras/` convention its siblings use, moving the file and
  the note together is a rename with no content change.)*
- **Downstream dependents:** `editor.packaging.package` (`tasks/00-editor.tji:515-520`)
  declares `depends … editor.cameras.export …` at `:518` — this leaf is one of the
  eighteen feature gates on the shippable editor. `editor.panels.overview` (`.tji:439-441`)
  calls the overview an *"export/shot map"* but does not declare the edge.
- **Milestone:** already wired into `m9_editor` (`tasks/99-milestones.tji:8`) through the
  `editor.cameras` container dependency. The one follow-up this refinement registers
  (`editor.cameras.contact_sheet`) inherits the same wiring.

## Effort estimate

**3 days**, unchanged from the `.tji`, but **reallocated**: the contact sheet moves to a
named follow-up (`editor.cameras.contact_sheet`, 1.5d — see Acceptance criteria) and the
recovered budget pays for the one genuinely new thing this leaf must stand up, an image
**encoder** and the async job that drives it. Everything else is assembly of shipped parts.

- **The render-through-camera derivation is shipped, unit-tested, and explicitly
  reserved for this leaf.** `interact::viewport_camera_for_shot(const arbc::Affine& frame,
  int native_w, int native_h, int out_w, int out_h)`
  (`src/interact/ace/interact/interact.hpp:168-169`) is documented at `:156-167` as *"the
  comp -> device render camera that renders the shot's exact crop at an arbitrary
  `(out_w, out_h)` … at `out == k*native` it is that native camera scaled by `k` (the
  pane-fit preview and export's D14 N× multiplier) … `editor.cameras.export` reuses this
  verbatim (the sibling leaf simply consumes it)."* **The N× multiplier is therefore not
  new code at all** — it is `out = k · native` through a function that already ships.
- **The offline render is shipped and GL-free.**
  `render::render_document_srgb8(const arbc::Document&, int width, int height,
  const arbc::Affine& camera)` (`src/render/ace/render/render.hpp:37-38`, impl
  `src/render/render.cpp:22-43`) is the *only* `arbc::render_offline` call site in `src/`
  (`render.cpp:28`) and already does the D10 tail — `CpuBackend::convert` un-premultiplies
  and sRGB-encodes, *"never hand-rolled"* (`render.hpp:25-31`). It runs on the plain
  headless lane.
- **The camera read-out is shipped.** `scene::cameras(const arbc::Document&)`
  (`src/scene/ace/scene/camera.hpp:134`) returns `std::vector<scene::Camera>`
  (`:123-129`: `id`, `layer`, `name`, `resolution`, `frame`) in layer order over the
  lock-free `pin()` seam.
- **The destination directory is shipped and is currently written by nothing.**
  `project::ProjectLayout::exports_dir` (`src/project/ace/project/project.hpp:83`,
  populated `src/project/project_open.cpp:162`, created by the scaffold at `:271`).
- **The two platform faculties this leaf needs were built for it, by name.**
  `platform::Threads::spawn` is documented *"for editor-owned auxiliary threads only (e.g.
  **the later async export-with-progress**)"* (`src/platform/ace/platform/threads.hpp:19-21`),
  and `platform::FileSystem`'s charter names *"write export output"*
  (`src/platform/ace/platform/filesystem.hpp:13-17`).
- **The view slot is shipped and empty.** `dockmodel::ViewType::Export`
  (`src/dockmodel/ace/dockmodel/view_registry.hpp:19`, catalog entry
  `src/dockmodel/view_registry.cpp:24` — slug `"export"`, title `"Export"`, single-instance)
  is already wired into the built-in **Review** preset
  (`src/dockmodel/workspaces.cpp:278`) and draws the generic placeholder
  (`src/views/views.cpp:167-173`). This leaf installs its real body through the shipped
  `views::register_view_body` seam (`src/views/ace/views/views.hpp:108,114`), exactly as
  `draw_history` does (`src/app/shell.cpp:279-281`).
- **What is genuinely new**, and where the three days go: a vendored PNG encoder plus its
  containment rule (~0.5d), the L1 export kernel — plan, filenames, run loop, report
  (~0.75d), the async service with published progress and cancel (~0.5d), the L2 filled-
  background composite (~0.25d), the L3 panel (~0.5d), and the test surface — Catch2 units,
  two goldens, an e2e, a TSan case (~0.5d).

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cameras.model`** (`tasks/refinements/cameras/model.md`, Done 2026-07-18) —
  D-model-1: a shot camera is **one `org.arbc.camera` `arbc::Content` + one `Layer`**;
  the **frame** is the binding layer's `Affine` (device → composition), the **resolution**
  (W×H device px) and **name** are the content's `Editable` state; the kind is
  **non-rendering** (`bounds()` empty, zero pixels in the composite). Its Constraint 6
  (`model.md:247-253`) and Acceptance section (`:286-290`) both hand **this leaf** the
  inverse derivation (frame + resolution → `arbc::Viewport`) and the
  render-through-camera-at-its-own-resolution golden, deliberately out of scope there.
  A14 (`docs/01-architecture.md:376`) is the doc row.
- **`editor.cameras.look_through`** (`tasks/refinements/editor/look_through.md`) — shipped
  `interact::viewport_camera_for_shot` and proved with a `render_offline` golden
  (`tests/look_through_test.cpp:170-201`, `tests/goldens/look_through_shot_64x64.rgba8`)
  that a shot's preview is byte-convergent to the offline render through the same `Affine`.
  **That golden is this leaf's correctness anchor**: export is the same derivation at the
  shot's own resolution instead of a pane-fit one.
- **`editor.cameras.frame_selection` / `new_shot_from_view` / `mint_from_focused_canvas`**
  — D23 (`docs/00-design.md:490`) and `interact::k_max_mint_resolution = 8192`
  (`interact.hpp:128`). The clamp exists **because of this leaf**: *"A composition-scale
  selection must not mint a terapixel camera whose export (D14) would allocate terabytes"*
  (`interact.hpp:123-127`). Export inherits that bound for minted cameras but must impose
  its own for the N× multiplier, which multiplies past it.
- **`editor.canvas.writer_thread`** (`tasks/refinements/editor/writer_thread.md`, A4.1b at
  `docs/01-architecture.md:157-193`) — `ace::writer::WriterThread`
  (`src/writer/ace/writer/writer_thread.hpp:40-109`) is the document's single writer
  identity. Its posting inventory (`:172-182`) lists **writes** only; `pin()`, `resolve()`,
  `for_each_content()` stay unposted. `save_project`'s split — *"So save posts its cheap
  half and serializes off-thread"* (`docs/01-architecture.md:182`,
  `src/project/ace/project/save.hpp:82,93-101`) — is the shape this leaf copies.
- **`editor.canvas.history_published_reads`** (A18, `docs/01-architecture.md:380`) — the
  published-immutable-snapshot pattern for handing writer-side/worker-side state to the UI
  thread: `std::atomic<std::shared_ptr<const T>>`, a self-contained value, `load()`
  any-thread. This leaf's progress readout reuses it verbatim.
- **`editor.project.save` / `save_as`** (A13, `docs/01-architecture.md:375`) — the
  precedent that an L1 `commands` verb legitimately performs filesystem I/O through
  `platform::FileSystem&`, and that `ace_commands` already links `ace_platform`
  transitively (`CMakeLists.txt:160-163` links `PUBLIC`; `:180` `project DEPENDS base
  platform`; `:190` `commands DEPENDS base project scene`).
- **`editor.canvas.arbc_v030`** — the pin is `ARBC_GIT_TAG "v0.3.0"` (`CMakeLists.txt:25`).
  The `.tji` note's nested-composition caveat is already discharged: `render_offline` binds
  nested-composition operators as of v0.2.0 (arbc#6).

**Pending (owned here):**

- There is **no image encoder anywhere in the editor build.** libarbc is decode-only for
  foreign formats and says so: *"The host keeps, **encodes**, or muxes the surface"*
  (`arbc/runtime/offline_sequence.hpp:68`). `imgui_test_engine` vendors
  `imstb_image_write.h` for its screenshot tool, but it is reachable only from targets
  linking `imgui_test_engine` — i.e. L3/L4 under the A8 seam — and its `stbi_*` symbols
  are static to `imgui_capture_tool.cpp`. **Introducing the encoder is this leaf's job**
  (D-export-3, doc delta A20).
- No component owns "a rendered image written to a user-chosen file". This leaf decides
  where that lives (D-export-1).
- D14's *"Heavy renders run async with progress"* (`docs/00-design.md:367`) is the entire
  design surface for the job model — no D-row or A-row describes a progress surface,
  cancellation, or what happens when the document changes mid-export. D-export-7 and
  D-export-8 decide those.

## What this task is

Turn a saved shot camera into a file on disk. A camera already carries a resolution and a
framing, so there is no export *spec* to invent (D14): the user ticks one or more cameras
in the **Export** view, optionally sets an N× scale multiplier and a filled background
colour, and presses Export. For each ticked camera the editor derives the
composition→device render camera with `interact::viewport_camera_for_shot`, renders the
document offline at `N ×` the camera's own resolution through the shipped
`render::render_document_srgb8`, encodes the resulting straight-alpha sRGB8 image as a
PNG, and writes it to `<project>/exports/<camera name>.png` (or a directory the user
picked). Several ticked cameras is a **batch** — one file per camera, no extra machinery.
The whole job runs on one auxiliary thread spawned through `platform::Threads`, publishes
a per-item progress snapshot the UI thread reads lock-free, and can be cancelled between
items.

The **contact sheet** — every camera tiled into one image with per-tile captions — is
deliberately **not** in this leaf; it is registered as `editor.cameras.contact_sheet`
(1.5d) and builds on the identical plan/render/encode kernels. See Acceptance criteria.

## Why it needs to be done

- **It is the last unimplemented half of the camera premise.** D2 (`docs/00-design.md:469`)
  says *"Export = render-through-camera"*; §2 line 67-70 says *"Export = render each camera
  to a file at its resolution, through `render_offline(document, camera, backend)`"*. Six
  camera leaves have shipped the mint, the manipulation, the persistence, the reopen and
  the live preview — and none of them produce a file. Today the editor can compose,
  frame and save a project but cannot emit a single pixel a user can hand to anyone.
- **`editor.packaging.package` is gated on it** (`tasks/00-editor.tji:518`). It is one of
  eighteen leaves that define "feature-complete editor".
- **`exports/` is a promise the code already makes and never keeps.**
  `project::project_layout` computes `<root>/exports` (`project.hpp:83`) and the scaffold
  creates the directory on every new project (`project_open.cpp:271`). Nothing has ever
  written into it.
- **The Export view is a shipped placeholder.** `ViewType::Export` is in the catalog
  (`view_registry.cpp:24`), is one of two panes in the built-in **Review** workspace preset
  (`workspaces.cpp:278`, D21 at `docs/00-design.md:488`), and draws the generic "no body
  registered" placeholder. A user who applies Review today gets an empty box — the same
  class of shipped lie `editor.cameras.new_shot_from_view` closed at
  `camera_inspector.cpp:41`.
- **§9's own testing table already assigns export a golden** (`docs/01-architecture.md:300`:
  *"Rendered output | **export**, canvas composition | golden compare, reusing libarbc's
  byte-exact `render_offline`"*) and names export in the e2e chain (`:301`). The DoD for
  this leaf was written before the leaf was.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **`docs/00-design.md:481` — D14 "Export via cameras"**, verbatim: *"No separate export
  spec — a camera **is** the spec. Export = pick camera(s) → render each at its resolution
  (`render_offline`) → file(s); batch (one file per camera) + contact-sheet fall out;
  ad-hoc crop = frame a camera. v1 PNG (sRGB8); knobs: transparent/filled bg, N× scale
  multiplier; async for heavy renders."*
- **`docs/00-design.md:355-367` — §9 "Export — a camera *is* the export spec"**, the prose
  D14 compresses. Load-bearing sentences: *"pick camera(s) → render each at its resolution
  → file(s)"* (`:360`); *"batch writes one file per camera (named by camera)"* (`:361`);
  *"v1 format: **PNG** (working→sRGB8 encode); EXR/TIFF float formats ride the wider-gamut
  future (D10)"* (`:364-365`); *"Knobs: transparent-vs-filled background, an optional **N×
  scale multiplier** (render this camera above its set resolution)"* (`:365-367`);
  *"Heavy renders run async with progress."* (`:367`).
- **`docs/00-design.md:385-391`, `:416-418` — the project directory.** `exports/` is
  *"rendered camera outputs (PNG …), by default"* (`:390`); *"exports default into
  `exports/` but any destination is fine"* (`:418`).
- **`docs/00-design.md:432`, `:485` (D18), `:488` (D21)** — Export is a first-class **view**
  in the uniform dockspace catalog and is one of the two panes in the immutable **Review**
  preset.
- **`docs/00-design.md:469` (D2), `:534`** — cameras are the only observer primitive;
  `| Camera (export) | Viewport + render_offline(document, viewport, backend) |`.
- **`docs/00-design.md:476` (D9), `:490` (D23)** — frame ≠ resolution; a camera's
  resolution is its export resolution; minted resolutions are clamped to
  `k_max_mint_resolution` precisely so an export cannot allocate terabytes.
- **`docs/00-design.md:277-279` (D10)** — the editor is the *invisible translator*; the
  library owns the colour transfer. `render_document_srgb8` already discharges this; the
  filled-background composite must not undo it (Constraint 5).
- **`docs/00-design.md:482` (D15)** — export is **not** a scene edit: it mutates nothing,
  so it is not a transaction, not undoable, and posts nothing to the writer.

**Governing architecture rows:**

- **`docs/01-architecture.md:256-291` — §8 levelization.** The direct-dependency table
  (`:273-287`) and *"All of L1 is the testable core"* (`:289-291`). Machine source of
  truth: `scripts/check_levels.py:21-40` (`ALLOWED`) and `:45-57` (`EXTERNAL_ALLOWED`).
  Note `closure()` at `:61-69` is **transitive** — `commands`' closure already contains
  `platform` through `project`, which is why `commands::save_project` may already take a
  `platform::FileSystem&`.
- **`docs/01-architecture.md:293-320` — §9 testing/DoD**, especially `:300` (export →
  golden via `render_offline`) and `:301` (export named in the e2e chain). §9.1 (`:322-357`)
  is the offscreen software-GL ASan lane the e2e runs in.
- **`docs/01-architecture.md:365` (A3)** — the `PlatformServices` seam: *"file/directory
  access, threading spawn, and clock behind a thin interface … the later web impl = File
  System Access API / OPFS + Emscripten pthreads."* Every byte this leaf writes and every
  thread it spawns must go through it.
- **`docs/01-architecture.md:374` (A12)** — SDL is L4-only; the native folder dialog lives
  behind an app-level seam. `src/app/ace/app/folder_dialog.hpp:18-27` is the shipped
  `FolderDialog` abstraction (`show(Callback)`, async).
- **`docs/01-architecture.md:375` (A13)** — an L1 `commands` verb doing filesystem I/O
  through `platform::FileSystem` is the established shape.
- **`docs/01-architecture.md:376` (A14)** — the camera kind. **`:378` (A16)** — the
  header-only-PRIVATE-link idiom and the "exchange POD across a seam" pattern.
  **`:380` (A18)** — the published immutable snapshot for cross-thread reads.
- **`docs/01-architecture.md:157-193` (A4.1b)** — the writer thread's posting inventory
  (`:172-182`): reads (`pin()`, `resolve()`, `for_each_content()`) stay **unposted**;
  `:182` *"So save posts its cheap half and serializes off-thread."*

**libarbc API surface** (fetched at tag `v0.3.0`, `CMakeLists.txt:25`; headers under
`build/dev/_deps/arbc-src/src/`):

- `arbc::render_offline(const Document&, const Viewport&, Backend&) ->
  expected<std::unique_ptr<Surface>, SurfaceError>` —
  `arbc/runtime/offline.hpp:20-21`. Errors are values.
- `arbc::Viewport{int width; int height; Affine camera; ObjectId anchor{};}` —
  `arbc/compositor/compositor.hpp:16-36`; a default-invalid `anchor` sources the root
  composition.
- `arbc::CpuBackend::make_surface` / `convert` and `arbc::k_fast_rgba8srgb` — already
  consumed at `src/render/render.cpp:24-40`.
- `arbc/media/pixel_traits.hpp:56` (`unorm8_encode`), `:197-210` (the straight-alpha
  sRGB8 ↔ premultiplied-linear pair) — the library-owned transfer functions the filled
  background must be linearized through (Constraint 5).
- **No image-file encoder exists.** `arbc/runtime/tile_encode_dispatch.hpp` is the
  zstd tile-store save path, unrelated. `arbc/runtime/offline_sequence.hpp:68` states the
  contract: encoding is the host's job.

**Editor seams this leaf extends:**

- `src/interact/ace/interact/interact.hpp:156-169` —
  `viewport_camera_for_shot(frame, native_w, native_h, out_w, out_h)`, with the
  export-reserved comment at `:166-167` and the N× clause at `:163-165`.
- `src/render/ace/render/render.hpp:19-23` (`struct Srgb8Image`), `:37-38`
  (`render_document_srgb8`); impl `src/render/render.cpp:22-43`.
- `src/scene/ace/scene/camera.hpp:26-31` (`Resolution`), `:123-129` (`struct Camera`),
  `:134` (`cameras(const arbc::Document&)`).
- `src/project/ace/project/project.hpp:77-85` (`ProjectLayout`, `exports_dir` at `:83`),
  `:87` (`project_layout`).
- `src/platform/ace/platform/filesystem.hpp:18-47` — `write_file(path, string_view)`
  (`:35-36`), `make_directories` (`:40`), `atomic_replace` (`:45-46`), `exists` (`:22`).
  The API is `std::string_view`-based, so a PNG blob crosses as a `string_view` over its
  bytes.
- `src/platform/ace/platform/threads.hpp:11-32` — `Threads::spawn(std::function<void()>)
  -> std::unique_ptr<JoinHandle>`; `JoinHandle::join/detach/joinable`.
- `src/views/ace/views/views.hpp:108,114` — `ViewBody` /
  `register_view_body(dockmodel::ViewType, ViewBody)`; the `draw_history` precedent at
  `src/views/views.cpp` and the shell wiring at `src/app/shell.cpp:279-281` (install) and
  `:387` (clear-before-teardown — the seam is process-global).
- `src/app/ace/app/folder_dialog.hpp:18-27` (`FolderDialog::show`), `:36-51`
  (`SdlFolderDialog`); test fakes `tests/app_project_gateway_test.cpp:69`
  (`ScriptedFolderDialog`) and the per-e2e `NoopFolderDialog`
  (e.g. `tests/frame_selection_e2e_test.cpp:92`).
- `src/app/shell.cpp:210-213,245-246` — the writer-first / canvas-then-writer-stop
  teardown order the export thread's join must slot into.
- `CMakeLists.txt:153-167` (`ace_component`, links `PUBLIC`), `:187` (the
  nlohmann-header-only-**PRIVATE** idiom this leaf copies for the vendored encoder),
  `:190` (`commands`), `:194` (`render`), `:224-253` (`ace_tests`), `:260-292`
  (`ace_shell_test` + the offscreen-GL environment).

**Predecessor refinements:**

- `tasks/refinements/cameras/model.md` — D-model-1..5; Constraint 6 (`:247-253`) and
  Acceptance (`:286-290`) hand this leaf the render-through-camera derivation and its
  golden; `:126-131` warns the model leaf must not pre-empt export on `render_offline`.
- `tasks/refinements/editor/look_through.md` — D-look_through-3/4: the pane-fit wrapper
  and the byte-convergence proof against `render_offline`.
- `tasks/refinements/cameras/frame_selection.md:663-664` — the `k_max_mint_resolution`
  clamp exists *"whose export (`editor.cameras.export`, D14) would allocate terabytes"*.
- `tasks/refinements/editor/writer_thread.md` — D-writer_thread-3/6: sync-by-default
  posting, drain-then-join teardown enclosing the document's lifetime.
- `tasks/refinements/editor/save.md` — the capture-on-writer / serialize-off-writer split.

**Test rigs:**

- `tests/golden_support.hpp:17,26-27,36` — `read_file_bytes`, `write_file_bytes`,
  `compare_golden(name, bytes)` (byte-exact; dumps `<name>.actual` on mismatch).
  `ACE_GOLDEN_DIR` = `tests/goldens` (`CMakeLists.txt:251-252`).
- `tests/goldens/*.rgba8` — raw straight-alpha sRGB8 RGBA blobs, named
  `<leaf>_<what>_<W>x<H>.rgba8` (e.g. `look_through_shot_64x64.rgba8`).
- `tests/look_through_test.cpp:170-201` — the closest existing analogue of an export
  golden. `tests/camera_manip_test.cpp:317-354` — a `render_offline` golden pinning a
  camera-geometry law.
- `tests/platform_test.cpp:182` (`FakeFileSystem`), `:225` (`FakePlatformServices`) — the
  fakes the L1 export unit tests drive.
- `tests/writer_session.hpp:33-63` (`ace::testing::WriterSession`) — the headless
  writer-lifetime fixture.
- `tests/canvas_host_test.cpp` — where the real-`WorkerPool` TSan cases live.
- `tests/frame_selection_e2e_test.cpp`, `tests/look_through_e2e_test.cpp`,
  `tests/save_as_ui_e2e_test.cpp` (the async-dialog verb shape) — the e2e harness
  patterns.

## Constraints / requirements

1. **A camera is the whole spec — the exporter invents no framing.** The render camera is
   `interact::viewport_camera_for_shot(cam.frame, cam.resolution.width,
   cam.resolution.height, out_w, out_h)` **verbatim** (`interact.hpp:168-169`). The
   exporter must not re-derive, re-fit, letterbox, or aspect-correct anything: at `N = 1`,
   `out == native` and the function reproduces the exact camera the shot was minted from,
   which is what makes an export reproduce what `look_through` previews. No new geometry
   helper is added to `interact`.

2. **The N× multiplier is `out = N · native`, not a resample.** `out_w = N *
   cam.resolution.width`, `out_h = N * cam.resolution.height`, and the *same* function
   derives the camera — `interact.hpp:163-165` already states this is what `k` means. The
   rendered pixels are genuinely composed at the higher resolution; nothing is scaled after
   the fact. `N` is an integer ≥ 1, default 1 (the `.tji` pre-exec decision).

3. **N× must be bounded, because the mint clamp does not cover it.**
   `k_max_mint_resolution = 8192` bounds a *minted* camera (`interact.hpp:123-128`), but a
   user may type any W×H in the resolution inspector and then multiply it. The plan must
   **refuse** (as a value, not an abort) any item whose `out_w * out_h * 4` exceeds a
   named byte budget, reporting the camera and the requested size. The budget is a single
   named constant in `commands` with a stated derivation (D-export-4); the refusal is
   per-item, so one oversized camera never kills a batch.

4. **Errors are values, end to end.** `render_offline` returns
   `expected<…, SurfaceError>`; `render_document_srgb8` already degrades to an empty image
   on the error path (`render.hpp:29-30`); `FileSystem::write_file` returns
   `std::error_code`. Every failure lands in the per-item `ExportReport` entry with a
   message. Nothing throws, nothing aborts, and a failed item never stops the batch.

5. **The filled background is composited in the linear working space, not in sRGB8.**
   D10 makes the editor the invisible translator; blending straight-alpha sRGB8 bytes
   directly would produce gamma-incorrect edges. The composite therefore happens in
   `render` (which may include `arbc/`), over the working-space surface **before**
   `CpuBackend::convert`, using the library's own transfer helpers
   (`arbc/media/pixel_traits.hpp:56,197-210`) for the sRGB→linear direction. `commands`
   never touches a pixel except to hand a finished `Srgb8Image` to the encoder.
   **Transparent is the default** (the `.tji` pre-exec decision: it preserves alpha; a
   filled background is a deliberate opt-in), and with it the export path is byte-identical
   to the shipped `render_document_srgb8`.

6. **All bytes and all threads go through the L0 platform seam.** File writes use
   `platform::FileSystem` (`write_file` for the payload, `make_directories` for the
   destination); the job thread comes from `platform::Threads::spawn`. No `<fstream>`, no
   `std::thread`, no `FILE*` — the encoder is compiled with `STBI_WRITE_NO_STDIO` so it
   *cannot* open a file (D-export-3). This is what keeps A3's WASM port a port.

7. **Export writes nothing to the document and posts nothing to the writer.** It is not a
   transaction (D15): no `transact`, no `commit`, no journal entry, no undo step, and no
   `WriterThread::submit`. It reads through `pin()` / `resolve()` / `for_each_content()`,
   which A4.1b's inventory (`docs/01-architecture.md:172-182`) explicitly leaves unposted.

8. **The export thread is joined before the `Document` is released.** The shell's teardown
   order is writer-first-in, writer-last-out with the canvas nested inside
   (`src/app/shell.cpp:210-213,245-246`). The export job's `JoinHandle` must be joined
   **inside** that scope — before `canvas.destroy()` is fine, before the document release
   is mandatory. A cancelled job still joins; `detach()` is never called.

9. **`check_levels` stays clean and the encoder is *contained*.** `commands` gains no new
   entry in `ALLOWED` — its closure already reaches `platform` through `project`
   (`check_levels.py:61-69`). The vendored encoder gets its **own** `EXTERNAL_ALLOWED`
   entry restricted to `commands`, so the containment is CI-enforced rather than
   conventional (the editor's analogue of libarbc's "codec line",
   `plugins/imdec/CMakeLists.txt:11-17`). No component gains an ImGui/GL/SDL include it did
   not already have; `base`, `platform`, `gl`, `dockmodel` and `writer` stay libarbc-free.

10. **Progress crosses threads as an immutable published value (A18), never as a mutable
    shared struct or a mutex.** The worker builds a self-contained `ExportProgress`
    (`done`, `total`, `current_name`, `state`) and stores it into an
    `std::atomic<std::shared_ptr<const ExportProgress>>`; the UI thread `load()`s once per
    frame. A frame's readout is therefore always one coherent generation. Cancellation is a
    separate `std::atomic<bool>` checked **between** items — a single `render_offline` call
    is not interruptible and must not be pretended otherwise.

11. **Filenames are derived, deterministic, and cannot escape the destination.** The stem
    is the camera's name sanitized to a portable set; a name that sanitizes to nothing
    falls back to a positional stem. Path separators, `..`, drive-letters, control
    characters, trailing dots/spaces and the Windows reserved device names are all
    eliminated **in L1**, so a hostile or careless camera name is structurally incapable of
    writing outside the chosen directory. Within one plan, duplicate stems are
    disambiguated deterministically; a collision with an **existing file on disk**
    overwrites in place, per the `.tji` pre-exec decision (batch export is idempotent,
    mirroring Save's re-dump — no auto-suffix, no prompt).

12. **The UI surface is the already-catalogued Export view, driven by widget id.** The
    panel body installs through `views::register_view_body(ViewType::Export, …)` and is
    **cleared before the state it captures is destroyed** (`src/app/shell.cpp:387` — the
    seam is process-global). Every interactive widget carries an explicit `###id` so the
    ImGui Test Engine can drive it. Export adds **no rail item and no `ProjectGateway`
    virtual**: it is a panel with persistent state (a camera tick-list, options, a live
    progress readout), not a one-shot confirmed op like `Insert Cell` or `Clean up`.

13. **No new external dependency beyond the one vendored header, and no libarbc fork.**
    The encoder is a checked-in single header under the editor's own `third_party/`, not a
    `FetchContent` — which is also what makes a byte-exact `.png` golden legitimate
    (D-export-11).

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate` green
(check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.**
  - `scripts/check_levels.py` gains **one** `EXTERNAL_RE` pattern and **one**
    `EXTERNAL_ALLOWED` entry (`"stb_write": {"commands"}`); `ALLOWED` (`:21-40`) is
    **unchanged** — no new component, no new DAG edge. A test-visible proof that the rule
    bites: temporarily adding the include to any other component under `src/` must make
    `check_levels` fail (asserted by inspection in review, not in CI).
  - `src/base/**` gains `ace/base/image.hpp` and remains free of `arbc/`, ImGui, GL and
    SDL includes (D-export-2).
  - `src/commands/**` gains `ace/commands/export.hpp` + `export.cpp` + `png_encode.cpp`
    and includes `<ace/platform/filesystem.hpp>` / `<ace/platform/threads.hpp>` — already
    legal through `commands`' transitive closure and already precedented by
    `commands::save_project`.
  - `src/render/**` gains one function and no new external.
  - `src/views/**` gains `draw_export`; `src/dock/**` is **unmodified**.

- **L1 logic — Catch2 unit.** New `tests/export_test.cpp`, added to the `ace_tests` source
  list (`CMakeLists.txt:224-246`), naming `TEST_CASE("export: …")` after
  `tests/frame_selection_test.cpp`. It pins the laws the rest of the leaf rests on:
  - **The plan is a total, injective function from ticked cameras to paths.** N ticked
    cameras produce exactly N items, in `scene::cameras()` order, with N *distinct*
    absolute paths, all under the destination directory.
  - **Filename sanitization is closed under hostile input** (Constraint 11): a table-driven
    case over `"../../etc/passwd"`, `"a/b"`, `"C:\\x"`, `"  "`, `""`, `"...."`,
    `"CON"`, `"aux.png"`, a name with an embedded NUL and a name with a control character
    — each yields a stem containing no separator, no `.` run, no reserved device name, and
    `path.parent_path() == destination` for every one. An all-invalid name falls back to
    the positional stem.
  - **Within-plan duplicate stems disambiguate deterministically** (D-export-6): two
    cameras both named `"Hero"` yield `Hero.png` and `Hero-2.png` in camera order, stably
    across runs; three yield `-2`/`-3`. Asserted **not** to renumber the first item.
  - **The N× multiplier is a resolution multiply, not a resample** (Constraint 2): for
    `cam.resolution == {320,200}` and `N == 3`, the item's `(width,height) == (960,600)`
    and its camera equals `viewport_camera_for_shot(frame,320,200,960,600)` componentwise.
    Anti-vacuity: it is asserted **≠** `viewport_camera_for_shot(frame,320,200,320,200)`.
  - **The byte budget refuses per item, not per batch** (Constraint 3): a plan over three
    cameras where the middle one exceeds the budget yields three items, the middle marked
    refused with its requested size in the message, the other two intact and renderable.
  - **An empty tick-list is refused, not guessed** (D23's refuse-rather-than-guess rule
    read across): `plan_export` over zero selected cameras returns an empty plan carrying a
    reason, and `run_export` over it writes **no** file (asserted against a `FakeFileSystem`
    that records every write).
  - **`run_export` reports every outcome as a value** (Constraint 4): driven with a
    `FakeFileSystem` whose `write_file` returns `std::errc::no_space_on_device` for one
    path, the report marks that item failed with the error text and the remaining items
    succeeded; nothing throws.
  - **`encode_png` produces a structurally valid PNG** (D-export-11): a test-local chunk
    walker asserts the 8-byte signature, an `IHDR` whose width/height match the image and
    whose bit-depth/colour-type are `8`/`6` (RGBA), a non-empty `IDAT`, a terminating
    `IEND`, and a **correct CRC32 on every chunk**. Degenerate inputs (0×0, an image whose
    `pixels.size() != w*h*4`) return an empty byte vector rather than a malformed file.
  - **Progress is monotone and terminal** (Constraint 10): driving `run_export` with a
    stub renderer over 5 items, the sequence of published snapshots has non-decreasing
    `done`, `total == 5` throughout, and ends in exactly one terminal state
    (`Finished` / `Cancelled` / `Failed`).
  - **Cancel takes effect between items and leaves complete files** (D-export-7): a stub
    renderer that sets the cancel flag on its 2nd call yields a report with 2 written
    items, state `Cancelled`, and no third write recorded on the `FakeFileSystem`.

- **Rendered output — golden.** Two golden files, both new, both under `tests/goldens/`,
  added as cases in `tests/export_test.cpp`:
  - **`export_camera_64x64.rgba8`** — the pre-encode pixels. Build a fixture document (the
    `tests/look_through_test.cpp:170-201` recipe), add a camera whose resolution is 64×64,
    derive the render camera through `viewport_camera_for_shot`, call
    `render::render_document_srgb8`, and `compare_golden` byte-exactly. **This is the leaf's
    render-through-camera-at-its-own-resolution golden, the one `cameras/model.md:286-290`
    reserved for it.** Cross-check in the same case: the identical bytes come back through
    the export path's renderer callable, so the export pipeline is proven to add no pixel
    of its own at `N = 1` with a transparent background.
  - **`export_camera_64x64.png`** — the encoded bytes, byte-exact. Legitimate because the
    encoder is a **checked-in** header, not a fetched dependency (Constraint 13), and
    because the encode TU pins the compression level and filter mode explicitly rather than
    inheriting stb's mutable globals (D-export-3). Regenerating this golden is a
    deliberate act tied to a vendored-header bump; the `.rgba8` golden above is what
    catches a *rendering* regression, so a stale `.png` can never mask one.
  - **`export_filled_bg_64x64.rgba8`** — the filled-background composite (Constraint 5).
    Anti-vacuity: the case additionally asserts that a naive sRGB8-space blend of the same
    inputs is **different** from the golden at a partially-transparent edge pixel, so an
    implementation that composites in gamma space cannot pass.

- **UI e2e — ImGui Test Engine.** New `tests/export_e2e_test.cpp`, added to the
  `ace_shell_test` source list (`CMakeLists.txt:260-282`), registered
  `IM_REGISTER_TEST(engine, "cameras", "export_panel")`, built on
  `tests/frame_selection_e2e_test.cpp`'s harness (real `AppProjectGateway` + real
  `CanvasView` over a `ScratchDir` project, `NoopFolderDialog`) and asserting on **model
  state and on-disk files, never on pixels**. Phases:
  1. Open the Export view through the rail's view launcher; with **no** cameras the panel
     shows the empty state and `###export_run` is **disabled**.
  2. Mint two cameras (`###new_shot_from_view` twice, with a canvas nav between them, the
     `new_shot_from_view_e2e_test.cpp` recipe). The panel now lists `###export_cam_0` and
     `###export_cam_1` with the camera names; both start unticked and `###export_run` is
     still disabled (Constraint: refuse rather than guess).
  3. Tick `###export_cam_0`, click `###export_run`, `pump_until` the published state is
     terminal. Assert **exactly one** file exists at `<scratch>/exports/Camera 1.png`,
     that its first 8 bytes are the PNG signature, and that its `IHDR` width/height equal
     the camera's resolution.
  4. Tick both, set `###export_scale` to 2, run again. Two files; `Camera 1.png` is
     **overwritten in place** (no `Camera 1-2.png` appears — the `.tji` overwrite decision)
     and its `IHDR` now reads `2 ×` the camera resolution.
  5. Rename `Camera 2` to `Camera 1` through the inspector, run again: the on-disk result
     is `Camera 1.png` **and** `Camera 1-2.png` (within-plan disambiguation, D-export-6).
  6. Tick both, enable `###export_bg_filled` and pick an opaque colour; run; assert both
     files' pixels are fully opaque — read back through the same chunk walker's `IHDR`
     plus a re-render comparison against the filled-background path (no decoder is
     introduced; the assertion is on the *renderer* output the panel fed the encoder,
     surfaced by the report).
  7. **Progress and cancel are observable**: with both cameras ticked at a large `N`,
     `###export_cancel` is enabled while running and disabled when idle; clicking it drives
     the published state to `Cancelled`, `###export_status` reflects it, and the already
     written file is a complete, signature-valid PNG.
  8. **The panel does not touch the document** (Constraint 7): `scene::cameras()` count,
     every camera's `resolution`/`frame`, `commands::Selection` and the journal depth are
     all unchanged across every phase above, and `Ctrl+Z` after an export undoes the
     *rename*, not an export.
  9. **Destination override** goes through the shipped seam: with a `ScriptedFolderDialog`
     returning a second scratch directory, `###export_browse` updates `###export_destination`
     and the next run writes there, leaving `<scratch>/exports/` untouched.

- **Threading (ASan/TSan) — explicitly scoped.** This leaf adds the editor's **second**
  long-lived auxiliary thread, so the coverage is named rather than inherited:
  - A new case in `tests/canvas_host_test.cpp` (where the real-`WorkerPool` cases live)
    runs a real `WriterThread` + a real `CanvasHost` over one document while an export job
    renders the same document on a `platform::Threads`-spawned thread, with UI-thread
    edits (`add_camera`, `rename_camera`) landing throughout. It asserts TSan-clean and
    that every produced file is signature-valid. This is the direct test of Constraint 7's
    claim that export is a pure reader.
  - A Catch2 case pins the **teardown order** (Constraint 8): a job still running when the
    session tears down is joined before the document is released; the test fails (under
    ASan, as a use-after-free) if the join is moved out of that scope.
  - A Catch2 case pins the **published-progress** discipline (Constraint 10): a reader
    thread `load()`ing the snapshot continuously while the worker publishes is TSan-clean,
    and a snapshot held across later publishes keeps its values (immutability, A18).
  - The new e2e runs in the existing offscreen software-GL ASan lane
    (`docs/01-architecture.md` §9.1) with **no new `tests/lsan.supp` suppression**. The
    vendored encoder allocates through `STBIW_MALLOC`/`STBIW_FREE`; any leak it introduces
    is an editor leak and must be fixed, not suppressed.

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed lines;
  clang-format + build clean. The vendored `third_party/stb/stb_image_write.h` is
  **excluded** from clang-format and from diff coverage as vendored third-party source —
  the same treatment libarbc gives `plugins/imdec/third_party/imdec.h`. Tests ship with the
  task.

- **Doc delta (same commit).** One new row: **`docs/01-architecture.md` A20** — the export
  pipeline's structure (the L1 `commands` kernel with an injected renderer, the
  `base::Srgb8Image` hoist, the vendored-and-contained PNG encoder, the
  `platform::Threads` job with an A18-published progress snapshot, and the Export view
  body). See D-export-1, D-export-2, D-export-3, D-export-7. **No `docs/00-design.md`
  change**: D14 already decides format, knobs, batch and async; D18/D21 already give Export
  a view slot; D16 already puts outputs in `exports/`. The contact-sheet clause of D14
  stays as written and is satisfied by the follow-up below, not contradicted here.

- **Deferred WBS work.** One named follow-up, for the closer to register mechanically:
  - **`editor.cameras.contact_sheet`** — *"Contact sheet: all cameras tiled into one PNG"*,
    **1.5d**, `allocate team`, `depends !export`, under `task cameras`
    (`tasks/00-editor.tji:306`), already wired into `m9_editor`
    (`tasks/99-milestones.tji:8`) through the `editor.cameras` container dependency.
    Scope: a `contact_sheet` toggle on the Export panel that renders the *same*
    `ExportPlan` and then tiles the results into one additional PNG — uniform grid,
    `ceil(sqrt(N))` columns, each tile scaled to fit a common tile box preserving aspect,
    thin gutters, a small per-tile camera-name caption, on the chosen background (the
    `.tji:381` pre-exec decision, carried forward verbatim). Two pieces make it a leaf of
    its own rather than a clause of this one: (a) the **caption** needs glyphs, and the L1
    core has no text renderer — ImGui's font is L3-only and unavailable to a headless job —
    so it ships a small embedded fixed-width bitmap glyph table plus a pure blit, which is
    self-contained, deterministic and golden-testable; (b) the **tile downscale** is a
    resample and must run in the linear working space for the same D10 reason Constraint 5
    gives, i.e. in L2 `render` over `arbc/media/image_resampler.hpp`, **not** as an sRGB8
    box filter in `commands`. Pure grid geometry (`contact_sheet_layout`) is L1 Catch2;
    the composed sheet gets an `.rgba8` golden. Source-of-debt:
    `tasks/refinements/editor.cameras/export.md`. Design: `docs/00-design.md` D14, `:362`;
    `docs/01-architecture.md` A20.
  - Everything else out of scope already has a scheduled owner or a standing decision:
    **EXR/TIFF float output** is deferred by D14 itself (`docs/00-design.md:364-365`, *"ride
    the wider-gamut future (D10)"*) and needs no task until D10's wider-gamut work is
    scheduled; **export from the overview shot-map** is `editor.panels.overview`
    (`.tji:439-441`, which already calls itself an *"export/shot map"*); **a rail item or
    keyboard chord for export** waits on §11's input map, still unwritten and deliberately
    not pre-empted (the same call `D-frame_selection-8` made); **a native save-**file**
    dialog** is not needed — this leaf reuses the shipped `FolderDialog` folder seam
    (A12) and a derived filename, which is exactly what D14's "named by camera" asks for.

## Decisions

- **D-export-1 — The export pipeline is an L1 `commands` kernel driven by an *injected*
  renderer callable; L4 binds the renderer, L3 draws the panel. No new component, no new
  DAG edge.**
  `commands` (L1) owns `ExportOptions`, `ExportItem`, `ExportPlan`, `plan_export`,
  `sanitize_stem`, `encode_png`, `run_export` and the `ExportService` that wraps them in a
  thread; the actual `render_offline` arrives as
  `using RenderFn = std::function<base::Srgb8Image(const arbc::Affine& camera, int w,
  int h, std::optional<Rgba8> background)>`, which L4 `app` binds to
  `render::render_document_srgb8` (or its filled-background sibling) at shell bootstrap.
  *Rationale:* three forces converge. (i) **The DAG forbids the direct edge and should
  keep forbidding it** — `commands`' declared deps are `base, project, scene`
  (`check_levels.py:31`); `render` is L2 and sits *above* `commands`, so a
  `commands → render` edge would invert the level order, not merely widen it. (ii) **§9
  says L1 logic is "the bulk"** (`docs/01-architecture.md:299`), and the interesting parts
  of export — which cameras, at what size, to which paths, refused how, reported how — are
  exactly that kind of logic. Injecting the one impure step keeps all of it in headless
  Catch2 reach with a stub renderer, which is what makes the plan/refusal/cancel/progress
  matrix above testable at all. (iii) **The pattern is already the house style**: A18
  (`:380`) hands `CanvasHost` an opaque `std::function<void()>` post-edit hook precisely so
  `render` never sees `commands`; A12/A13/A16 invert the SDL dialog and the insert schema
  the same way. `commands` also already takes a `platform::FileSystem&`
  (`commands::save_project`, A13), so the I/O half needs no argument at all.
  *Alternative rejected:* **put the whole pipeline in L2 `render`.** Legal —
  `render`'s closure reaches `scene`, `project` and (transitively) `platform`, and its
  tests are headless — and it would need no injection. But it moves the plan, the filename
  policy, the batch loop, the report and the job model into the component chartered
  *"HostViewport/InteractiveRenderer glue · frame-sync · tile→GL"*
  (`docs/01-architecture.md:266`), directly against §9's "L1 is the bulk", and it puts a
  user-facing policy surface in the component whose other job is a real-time frame loop.
  *Alternative rejected:* **a new L1 `exporter` component.** Clean on paper, and `writer`
  is a recent precedent for adding one — but `writer` earned it by being *structurally*
  different (libarbc-free by construction, so the DAG had to say so). An export kernel is
  ordinary `commands` material with the same dependency set `commands` already has; adding
  a component to hold one header is churn that every future reader has to justify.
  **No new DAG edge. Doc delta: A20.**

- **D-export-2 — `Srgb8Image` moves down to L0 `base` as `ace::base::Srgb8Image`;
  `ace::render::Srgb8Image` stays as a type alias.**
  `struct Srgb8Image {int width; int height; std::vector<std::uint8_t> pixels;}`
  (`src/render/ace/render/render.hpp:19-23`) names no libarbc type and no GL type — it is
  a value type, which is precisely `base`'s charter (`docs/01-architecture.md:270`, *"value
  types"*). Moving it to `ace/base/image.hpp` and leaving
  `namespace ace::render { using Srgb8Image = base::Srgb8Image; }` lets `commands` name the
  image the renderer produces without a `commands → render` edge, and leaves every shipped
  call site (`src/render/render.cpp`, `src/app/canvas_view.cpp`, nine test files) compiling
  unchanged.
  *Rationale:* the alternative to a shared type is a duplicated one, and a duplicated pixel
  buffer type is the kind of thing that silently diverges (stride, alpha convention,
  channel order) between the component that fills it and the component that encodes it —
  the exact class of bug a byte-exact golden exists to catch, made undetectable by
  construction. The move is mechanical, adds no dependency to `base`, and keeps
  `base` free of `arbc/` (the `arbc` `EXTERNAL_ALLOWED` list at `check_levels.py:49-50`
  excludes `base`, and this change does not touch that).
  *Alternative rejected:* **a `commands`-local image POD plus an L4 conversion.** It costs
  a full-image copy per item for a purely bookkeeping reason and lets the two definitions
  drift.
  *Alternative rejected:* **have the injected callable return already-encoded PNG bytes**,
  so `commands` never names an image at all. Tempting — but it moves the encoder to L4,
  puts the one genuinely tricky pure function (encode) outside the L1 Catch2 bulk, and
  makes the pre-encode `.rgba8` golden unreachable from the export path.
  **No new DAG edge. Doc delta: A20.**

- **D-export-3 — PNG is encoded by a *vendored* `stb_image_write.h`, compiled into exactly
  one TU inside `ace_commands`, with `STBI_WRITE_NO_STDIO`, emitting bytes through
  `stbi_write_png_to_func`; containment is enforced by a new `check_levels`
  `EXTERNAL_ALLOWED` entry.**
  The header lands at `third_party/stb/stb_image_write.h`; `src/commands/png_encode.cpp`
  is the single TU defining `STB_IMAGE_WRITE_IMPLEMENTATION` and
  `STBI_WRITE_NO_STDIO`, and it sets `stbi_write_png_compression_level` and
  `stbi_write_force_png_filter` explicitly rather than inheriting stb's mutable globals.
  CMake: `target_include_directories(ace_commands PRIVATE "${CMAKE_SOURCE_DIR}/third_party")`
  — the **exact** header-only-**PRIVATE** idiom `CMakeLists.txt:182-187` already documents
  for nlohmann on `ace_project` (*"so only src/project/save.cpp sees it — no downstream
  component inherits JSON"*). `scripts/check_levels.py` gains
  `EXTERNAL_RE["stb_write"]` and `EXTERNAL_ALLOWED["stb_write"] = {"commands"}`.
  *Rationale:* (i) **Nothing in the tree can be reused.** libarbc is decode-only and says
  encoding is the host's job (`arbc/runtime/offline_sequence.hpp:68`); the only PNG writer
  in the build ships inside `imgui_test_engine`, reachable only from targets that link it —
  i.e. L3/L4 under the A8 seam — with its `stbi_*` symbols static to
  `imgui_capture_tool.cpp`. Reaching for it would drag an ImGui-tier dependency into L1,
  which is the one thing §8 exists to prevent. (ii) **The dependency category is
  pre-sanctioned.** libarbc's own codec line already names *"the ONE vendored stb-class
  decode dependency"* and pre-clears its licence class
  (`plugins/imdec/third_party/imdec.h:15-17`, public domain — *"no doc-10 dependency
  decision"*). `stb_image_write.h` is that same class (public domain / MIT dual), and this
  is the editor's mirror-image *encode* line. (iii) **`STBI_WRITE_NO_STDIO` makes A3
  structural, not aspirational** — with no `FILE*` entry points compiled in, the encoder
  *cannot* bypass `platform::FileSystem`, so the WASM port has one seam and no leaks around
  it. (iv) **Vendoring rather than fetching pins the bytes**, which is what makes a
  byte-exact `.png` golden a real assertion instead of a version-drift tripwire.
  *Alternative rejected:* **hand-roll a PNG writer over stored (uncompressed) deflate
  blocks.** ~120 lines, no dependency, fully testable — and it produces files ≈ the raw
  pixel size. A 4K hero would land at ~33 MB instead of a few MB. The deliverable of this
  leaf *is* the file; shipping a pathologically large one to avoid a public-domain header
  is a bad trade.
  *Alternative rejected:* **hand-roll a real deflate (fixed-Huffman + LZ77).** Several
  hundred lines of compression code the editor would own, test and debug forever, for a
  problem that is not the editor's business.
  *Alternative rejected:* **FetchContent zlib and write the PNG container ourselves.** Two
  moving parts instead of one, a compiled C dependency added to the editor's build matrix,
  and the container code still has to be written and tested.
  **Doc delta: A20.**

- **D-export-4 — Oversized items are refused per item against a named byte budget, and the
  budget is stated, not implied.**
  `commands` carries `inline constexpr std::int64_t k_max_export_bytes` with its derivation
  in the comment (the RGBA8 output buffer plus libarbc's working-space `rgba32f` target —
  i.e. `w*h*(4 + 16)` bytes live at once — bounded so a single item cannot exhaust a
  workstation's RAM). An item whose requested `out_w × out_h` exceeds it is marked refused
  in the plan, with the camera name and the requested size in the message; the rest of the
  batch proceeds.
  *Rationale:* `k_max_mint_resolution = 8192` bounds *minted* cameras only
  (`interact.hpp:123-128`) — the resolution inspector lets a user type any W×H
  (`editor.cameras.manip`), and the N× multiplier then multiplies it. Without a budget,
  "8192×8192 at 8×" is a 65536×65536 render: a 17 GB working-space target that the process
  simply dies on. Refusing as a *value* keeps Constraint 4 whole and tells the user which
  camera and how big.
  *Alternative rejected:* **clamp N down silently to fit.** It would export something other
  than what the user asked for, and D23's minting rule already establishes the house
  position — refuse rather than guess.
  *Alternative rejected:* **rely on `render_offline`'s `SurfaceError`.** It reports a
  failure only after the allocation is attempted; the failure mode we are guarding against
  is the one that kills the process before any value is returned.
  **No doc delta** (an implementation bound, not a design decision; the design decision it
  serves is D23's existing clamp rationale).

- **D-export-5 — The filled background is composited in the linear working space inside L2
  `render`; `commands` never touches a pixel.**
  `render` gains
  `render_document_srgb8_over(const arbc::Document&, int w, int h, const arbc::Affine&
  camera, std::array<std::uint8_t,4> background)`, which builds the working-space target,
  fills it with the *linearized, premultiplied* background using the library's own transfer
  helpers (`arbc/media/pixel_traits.hpp:56,197-210`), renders over it, and hands the result
  to the same `CpuBackend::convert` tail the existing function uses.
  *Rationale:* D10 (`docs/00-design.md:277-279`) makes the editor the invisible translator
  and `render.hpp:25-31` already commits to *"never hand-rolled"* colour. Blending
  straight-alpha sRGB8 bytes with `src·a + bg·(1-a)` is the classic gamma-incorrect
  composite — visible as dark halos on every antialiased edge — and it would be *invisible
  to a golden written from the same wrong code*, which is why the golden case above
  explicitly asserts the naive blend differs. Putting the composite where the working-space
  surface already lives costs one function and keeps `commands` a pure
  plan/encode/IO/report component. **Transparent stays the default** (the `.tji` pre-exec
  decision), and on that path the export renderer is *byte-identical* to the shipped
  `render_document_srgb8` — so the common case adds no new rendering surface at all.
  *Alternative rejected:* **composite in `commands` over the `Srgb8Image`.** It is where
  the rest of the export logic lives and would need no new `render` entry point — but it
  puts colour math in the component that D10 says must not own any, and gets it wrong.
  **No new DAG edge. Doc delta: A20** (the row records where the composite lives and why).

- **D-export-6 — Filenames are sanitized camera names, disambiguated within the plan with a
  `-<n>` suffix, and overwrite in place on disk.**
  Stem = the camera name reduced to a portable set (alphanumerics, space, `-`, `_`, `.`),
  with separators, `..`, control characters and drive-letter colons removed, trailing dots
  and spaces trimmed, Windows reserved device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1-9`,
  `LPT1-9`) prefixed, and an empty result replaced by `camera-<index>`. Within one plan, the
  2nd and later occurrences of an identical stem get `-2`, `-3`, … **The first occurrence is
  never renumbered.** An existing file at the resulting path is overwritten.
  *Rationale:* the `.tji` pre-exec decision settles the *disk* case explicitly — *"on
  filename collision OVERWRITE in place (batch export is idempotent, mirroring Save's
  re-dump — no auto-suffix, no prompt)"* — and that is preserved verbatim. It does **not**
  settle the *within-batch* case, which is a different problem: camera names are free text
  and `rename_camera` (`scene/camera.hpp:165-166`) imposes no uniqueness, so two cameras
  can legitimately both be `"Hero"`. Without disambiguation the batch would silently emit
  one file for two cameras — a data-loss surprise, not idempotence. The suffix rule keeps
  `plan_export` a total injective function (the property the unit test asserts) while
  leaving the single-camera and all-distinct-names cases — i.e. every normal export —
  byte-identical to what the `.tji` decided. Sanitizing in **L1** rather than trusting the
  filesystem is what makes "a camera name cannot write outside the destination" a headless
  Catch2 assertion instead of a platform behaviour.
  *Alternative rejected:* **suffix every file (`Hero-1.png`, `Hero-2.png`) whenever any
  duplicate exists.** Uniform, but it renames the file a user already has for a reason that
  has nothing to do with them.
  *Alternative rejected:* **disambiguate with the camera's `ObjectId`.** Stable and unique,
  but it puts an opaque number in a filename D14 says is *"named by camera"*.
  **No doc delta.**

- **D-export-7 — The job runs on one `platform::Threads` thread, publishes progress as an
  A18 immutable snapshot, and cancels between items.**
  `commands::ExportService` holds the installed `RenderFn`, an
  `std::atomic<std::shared_ptr<const ExportProgress>>`, an `std::atomic<bool> cancel_`, and
  the `platform::JoinHandle` of the in-flight job. `start(plan, options)` spawns; the worker
  publishes a fresh snapshot before and after each item; the UI thread `load()`s once per
  frame; `cancel()` sets the flag, which the worker checks between items.
  *Rationale:* (i) `platform::Threads` was built for this and names it —
  *"for editor-owned auxiliary threads only (e.g. **the later async export-with-progress**)"*
  (`threads.hpp:19-21`) — and it is the seam A3 maps to Emscripten pthreads. (ii) The
  `WriterThread` is explicitly **not** the right home: A4.1b's inventory
  (`docs/01-architecture.md:172-182`) posts *writes*, and the save precedent (`:182`) is
  *"posts its cheap half and serializes off-thread"*. An export is all heavy read; queuing
  a multi-second render behind the writer's FIFO would stall every edit — the exact failure
  A18 rejects as *"couples frame rate to edit latency"*. (iii) The published-snapshot shape
  is A18's, unchanged: a self-contained value means a frame's `done`/`total`/`current_name`
  are always one generation, without a lock and without the UI thread ever reading a
  half-written struct. (iv) Cancellation is honest at item granularity because
  `render_offline` exposes no cancellation hook; pretending otherwise would mean either
  lying in the UI or forking the library.
  *Alternative rejected:* **run on libarbc's shared `WorkerPool`.** A5 gives the pool to the
  renderers; borrowing it for a multi-second offline render would starve the interactive
  canvases it exists to feed, and the pool's concurrent-submitter contract is exactly what
  `multi_canvas.md`'s parked question says not to disturb.
  *Alternative rejected:* **render synchronously on the UI thread with a progress modal.**
  Simplest to write and it makes coherence trivial — but D14 says *"Heavy renders run async
  with progress"*, and a frozen window during a 4K batch is the thing that sentence forbids.
  **Doc delta: A20.**

- **D-export-8 — The batch is not serialized against the writer; the report records the
  document revision at start and end and flags a change.**
  `ExportReport` carries `start_revision`, `end_revision` and
  `document_changed_during_export` (`start != end`). No lock, no writer hold, no edit
  blocking.
  *Rationale:* export is a reader (Constraint 7) and A4.1 guarantees reads are lock-free via
  `pin()`. But `render_offline` pins the *current* version per call, so an edit landing
  mid-batch can make item 3 reflect a document item 1 did not — an incoherence with three
  possible answers. Blocking edits for the duration contradicts D14's async promise and A4's
  *"the UI thread stays responsive"*. Freezing the version for the whole batch is not
  available: the library offers no way to render an arbitrary retained version offline
  (`render_offline` takes a `const Document&`, not a pin, `offline.hpp:20-21`), and
  inventing one means a library change. Recording and reporting it is the honest third
  option — it costs two `revision()` reads, it is a testable observable, and it turns a
  silent incoherence into a stated one. In practice the common case (press Export, wait) has
  `start == end` and the flag never appears.
  *Alternative rejected:* **snapshot the document up front** (`capture_snapshot` on the
  writer, render from the snapshot). It would give exact coherence and reuses the save
  precedent — but `capture_snapshot` produces a serialization snapshot, not a renderable
  `Document`, so it would mean loading a second document from it: a full parse and rebuild
  per export, for a coherence property the user has to work to violate.
  *Alternative rejected:* **say nothing.** A batch that silently mixes two document states
  is precisely the class of quiet wrongness `A19`'s *"the loss is announced rather than
  silent"* rejects elsewhere in this tree.
  **No doc delta** (an observable in the report; the structure it lives in is A20's).

- **D-export-9 — The UI is the already-catalogued Export *view*, not a rail action and not a
  `ProjectGateway` verb.**
  `views::draw_export(commands::ExportService&, commands::AppState&, std::string_view
  view_id)` is installed by the shell through `views::register_view_body(ViewType::Export,
  …)` (`views.hpp:114`) and cleared before teardown (`shell.cpp:387`), exactly as
  `draw_history` is (`shell.cpp:279-281`).
  *Rationale:* (i) **The slot exists and is empty.** `ViewType::Export` has been in the
  catalog since `editor.dock.view_registry` (`view_registry.cpp:24`) and is half of the
  built-in **Review** preset (`workspaces.cpp:278`, D21) — a user who applies Review today
  gets a placeholder box. (ii) **Export has persistent state**: a tick-list of cameras,
  three options, a destination, and a live progress readout. A16 rejected an Insert *panel*
  because *"inserting is a one-shot confirmed op"*; export is the opposite — the same
  reasoning, applied to the other case, lands on a panel. (iii) **`views` may already reach
  everything it needs** (`check_levels.py:34`: `views → {scene, interact, commands, render,
  dockmodel}`), so no `ProjectGateway` marshalling and no dock-local POD is required; the
  A12/A13/A16 inversion exists because `dock` may *not* see `commands`, and this panel is
  not in `dock`.
  *Alternative rejected:* **an `Export…` rail item opening a modal**, matching `Insert
  Cell…` / `Clean up…`. It fits the existing rail pattern — but it needs a `ProjectGateway`
  virtual pair plus dock-local PODs for the options and the progress, purely to route around
  a seam that does not apply, and it leaves the catalogued Export view still empty.
  **Doc delta: A20.**

- **D-export-10 — The destination defaults to `ProjectLayout::exports_dir` and is overridden
  through the shipped `app::FolderDialog` seam; no save-file dialog is introduced.**
  The panel shows the resolved destination and a `###export_browse` button; L4 installs a
  `std::function<void(std::function<void(std::optional<std::filesystem::path>)>)>` on the
  service, bound to the existing `FolderDialog::show`.
  *Rationale:* D16 already says *"exports default into `exports/` but any destination is
  fine"* (`docs/00-design.md:418`), and D14 says files are *"named by camera"* — so the user
  chooses a **directory**, never a filename, and the folder picker A12 already ships
  (`folder_dialog.hpp:18-27`, `SdlFolderDialog` at `:36-51`) is the exactly-right seam.
  Adding an `SDL_ShowSaveFileDialog` sibling would introduce a second SDL dialog surface, a
  second async callback shape and a second WASM mapping to serve a filename the design says
  is derived, not chosen. Reusing the seam also means the test fakes exist already
  (`ScriptedFolderDialog`, `NoopFolderDialog`).
  **No new seam. No doc delta** (A12 already covers the dialog; A20 records the injection
  point).

- **D-export-11 — Two goldens: an `.rgba8` on the pre-encode pixels and a byte-exact `.png`
  on the encoded bytes, plus a structural chunk-walk assertion.**
  *Rationale:* they catch different regressions and neither substitutes for the other. The
  `.rgba8` golden is the §9-mandated *"golden compare, reusing libarbc's byte-exact
  `render_offline`"* (`docs/01-architecture.md:300`) and is the one
  `cameras/model.md:286-290` reserved for this leaf — it fails when the *rendering* changes.
  The `.png` golden fails when the *encoding* changes, which is invisible to the first; it
  is legitimate only because D-export-3 **vendors** the encoder and pins its compression
  settings, so a regenerate is a deliberate act tied to a header bump rather than a
  version-drift flake. The chunk walk is the anti-vacuity layer: it proves the bytes are a
  well-formed PNG (signature, `IHDR` geometry and colour type, non-empty `IDAT`, `IEND`,
  correct CRC32 per chunk) rather than merely equal to a previously recorded blob, so a
  golden regenerated from a broken encoder cannot pass.
  *Alternative rejected:* **decode the PNG back and compare pixels.** The strongest possible
  check — and it would require introducing a *decoder* to the editor purely for tests, i.e.
  a second vendored dependency to validate the first.
  **No doc delta.**

- **D-export-12 — The contact sheet is deferred to a named follow-up rather than shipped
  here.**
  *Rationale:* D14 calls it a thing that *"falls out for free"*, and the plan/render/encode
  half genuinely does — but the pre-exec decision at `.tji:381` also specifies *"a small
  per-tile camera-name caption"*, and there is no text rendering anywhere below L3: ImGui's
  font atlas is unavailable to a headless L1 job, so captions mean an embedded bitmap glyph
  table. That, plus a tile resample that must run in the linear working space for D-export-5's
  reason, is a real 1.5d of work with its own goldens. Splitting keeps this leaf at its
  estimated 3d and makes the follow-up a clean additive layer over the *identical*
  `ExportPlan` — no rework, no seam change, no re-decision. The `.tji` title is
  correspondingly narrowed by the closer to *"Export via cameras (render_offline); PNG;
  batch"*, with the contact sheet a scheduled sibling rather than a dropped promise.
  *Alternative rejected:* **ship it here at a raised effort (4.5d).** Defensible, but it
  makes the batch-export gate that `editor.packaging.package` waits on hostage to a bitmap
  font, and it bundles two independently testable output modes into one reviewable unit.
  *Alternative rejected:* **ship a contact sheet without captions.** It contradicts a
  decision already recorded in the `.tji`, and an uncaptioned grid of crops is materially
  less useful as the "shot map" D6 wants it to be.
  **No doc delta** (D14's contact-sheet clause stands; the follow-up satisfies it).

## Open questions

(none — all decided.)

One item is surfaced to the orchestrator for `tasks/parking-lot.md` rather than encoded as a
WBS task: **whether `render_offline` should accept a retained document version (or a pin)**
so a batch export can be *exactly* coherent instead of merely honest about incoherence
(D-export-8). Today `render_offline(const Document&, const Viewport&, Backend&)`
(`arbc/runtime/offline.hpp:20-21`) pins the current version per call, and the editor has no
host-side fix that is not a full second document load. Whether libarbc should offer a
version-addressed offline render is a **library** design judgement for the
`arbitrarycomposer` maintainer, on nothing's critical path — an upstream-issue candidate,
not editor work.

## Status

**Done** — 2026-07-23.

- `src/base/ace/base/image.hpp`: new `base::Srgb8Image` value type (L0), so `commands` can name the rendered image without a `commands → render` DAG edge (D-export-2).
- `src/commands/ace/commands/export.hpp`, `src/commands/export.cpp`, `src/commands/png_encode.cpp`: L1 export kernel — `ExportOptions`, `ExportItem`, `ExportPlan`, `plan_export`, `sanitize_stem`, `encode_png`, `run_export`, `ExportService`; injected `RenderFn` (D-export-1); per-item byte-budget refusal (D-export-4); A18-published progress + between-item cancel (D-export-7); D-export-8 revision flag in `ExportReport`.
- `third_party/stb/stb_image_write.h` (v1.16, public-domain) + `third_party/.clang-format`: vendored PNG encoder, contained to `commands` via `scripts/check_levels.py` `EXTERNAL_ALLOWED` entry (D-export-3, Constraint 9).
- `src/render/ace/render/render.hpp`, `src/render/render.cpp`: `Srgb8Image` alias (`render::Srgb8Image = base::Srgb8Image`) + `render_document_srgb8_over` for linear-working-space filled-background composite before `CpuBackend::convert` (D-export-5, Constraint 5).
- `src/views/ace/views/views.hpp`, `src/views/views.cpp`: `draw_export` panel body — camera tick-list, N× scale, destination, bg-fill option, live A18 progress readout — wired into `ViewType::Export` (D-export-9).
- `src/app/shell.cpp`: `register_view_body(ViewType::Export, …)` + export thread `JoinHandle` joined inside the writer-first/writer-last-out teardown scope (Constraint 8).
- `scripts/check_levels.py`: `EXTERNAL_RE["stb_write"]` + `EXTERNAL_ALLOWED["stb_write"] = {"commands"}` — containment CI-enforced, not conventional (Constraint 9).
- `CMakeLists.txt`: `ace_commands` gains `png_encode.cpp` with private `third_party/` include; `ace_tests` and `ace_shell_test` gain new test files.
- `tests/export_test.cpp` (17 cases): plan injectivity, hostile-name sanitization table, within-plan dedup, overwrite-in-place, N× multiplier, per-item byte budget, empty-refusal, error-as-value, D-export-8 revision flag, progress monotonicity, cancel-between-items, PNG chunk-walk + CRC32, teardown-order join, A18 published-progress (TSan-fixed: reader accumulates into `std::atomic<int> torn`, main thread asserts after `join()`).
- `tests/export_e2e_test.cpp` (9-phase `IM_REGISTER_TEST(engine,"cameras","export_panel")`): tick/untick, unwritable destination, Ctrl+Z undo, cancel, destination override; `run_and_wait` hardened to wait on `!service.running()`.
- `tests/goldens/export_camera_64x64.{rgba8,png}`, `tests/goldens/export_filled_bg_64x64.rgba8`: render-through-camera golden (the model.md-reserved one), PNG encoding golden, filled-background linear-composite golden.
- `tests/canvas_host_test.cpp`: TSan anchor case — concurrent export reader + real `WriterThread` edits under `platform::Threads`.
- `docs/01-architecture.md`: A20 row (export pipeline structure) already present in tree.
