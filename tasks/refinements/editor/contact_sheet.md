# editor.cameras.contact_sheet — Contact sheet: all cameras tiled into one PNG

## TaskJuggler entry

- **Task:** `editor.cameras.contact_sheet` — *"Contact sheet: all cameras tiled
  into one PNG"* (`tasks/00-editor.tji:384-389`), under `task cameras`
  (`tasks/00-editor.tji:306`).
- **Effort:** `1.5d`.
- **Depends:** `!export` — `editor.cameras.export`, `complete 100`
  (`tasks/00-editor.tji:377-383`).
- **Note (`.tji:388`):** *"Tile the same ExportPlan into one additional PNG:
  uniform grid, ceil(sqrt(N)) columns, each tile scaled to fit a common tile box
  preserving aspect (linear-working-space resample via
  arbc/media/image_resampler.hpp in L2 render, not an sRGB8 box filter), thin
  gutters, a small per-tile camera-name caption via an embedded fixed-width
  bitmap glyph table (ImGui font is L3-only, unavailable to a headless job).
  Chosen background matches the Export panel bg option. Source-of-debt:
  tasks/refinements/editor.cameras/export.md. Design: docs/00-design.md D14,
  :362; docs/01-architecture.md A20."*
- **Back-link:** this document, `tasks/refinements/editor/contact_sheet.md`;
  the closer appends `Refinement: tasks/refinements/editor/contact_sheet.md`
  to the `.tji` note.
  *(Layout note for the closer: the predecessor sits at
  `tasks/refinements/editor.cameras/export.md` while the other camera leaves sit
  at `tasks/refinements/cameras/`. This leaf is filed at
  `tasks/refinements/editor/contact_sheet.md` as the orchestrator directed; the
  three-way split of the camera area across `editor/`, `editor.cameras/` and
  `cameras/` is pre-existing and not this leaf's to normalize.)*
- **Downstream dependents:** none scheduled. `editor.panels.overview`
  (`.tji:439-441`) will offer export from the shot map and reuses this plan
  shape, but does not depend on it.
- **Milestone:** `m9_editor` (`tasks/99-milestones.tji:8`), reached through the
  `editor.cameras` container dependency — no new milestone wiring is required
  for this leaf, and the one follow-up it registers inherits the same wiring.

## Effort estimate

**1.5 days**, unchanged from the `.tji`. The budget is the one the predecessor
reserved (`tasks/refinements/editor.cameras/export.md:45-47`), and this leaf
spends it differently than the predecessor projected — the caption half is
confirmed as the expensive half, the resample half evaporates (Decision
**D-sheet-1**), and the recovered time goes into the composition laws and their
goldens.

- **The render, encode, write, progress, cancel and report machinery is
  shipped.** `commands::ExportService` already owns the thread
  (`src/commands/ace/commands/export.hpp:248-307`), the A18-published progress
  snapshot (`src/commands/export.cpp:342-344`), the between-item cancel
  (`export.cpp:223-226`), the PNG encoder (`src/commands/png_encode.cpp:53-79`)
  and the `platform::FileSystem` write (`export.cpp:257-258`). This leaf adds
  a second **phase** to that job, not a second job.
- **The render seam is shipped and already parameterized by output size.**
  `RenderFn` takes `(camera, width, height, background)`
  (`export.hpp:169-170`) and `ShotCameraFn` derives the render camera for *any*
  output box (`export.hpp:173-175`, bound to
  `interact::viewport_camera_for_shot` at `src/app/shell.cpp:395`). Asking for a
  240×135 render of a 3840×2160 camera is the same call the shipped N× path
  makes in the other direction (`export.cpp:141-142,165-166`).
- **What is genuinely new**, and where the 1.5 days go:
  - **~0.5d — the embedded glyph table and the text blit.** 95 ASCII cells
    hand-authored as `constexpr` rows, plus `text_width` / `fit_text` /
    `draw_text` with integer scaling, truncation, clipping and a fallback box.
    This is the piece with no prior art in the tree
    (`docs/01-architecture.md:247` reserves `assets/ icons, fonts (bundled)`
    and the directory does not exist; there is no `font`/`glyph` symbol anywhere
    under `src/`).
  - **~0.4d — layout + composition.** `contact_sheet_layout` grid geometry,
    per-tile aspect fit, the sheet allocation and the clipped `blit`.
  - **~0.6d — the tests.** Sixteen Catch2 cases, one `.rgba8` golden, one new
    ImGui Test Engine e2e, and the extension of the existing TSan anchor.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cameras.export`** (`tasks/refinements/editor.cameras/export.md`,
  Done 2026-07-23) — every seam this leaf builds on:
  - `ExportOptions` / `ExportItem` / `ExportPlan` / `plan_export`
    (`export.hpp:76-113,213-214`), extended additively here.
  - The injected `RenderFn` / `ShotCameraFn` / `RevisionFn` / `ProgressFn`
    (`export.hpp:169-186`) and the `ExportRunner` bundle (`export.hpp:222-230`)
    — **D-export-1**'s inversion, which this leaf reuses verbatim rather than
    widening.
  - `base::Srgb8Image` in L0 (`src/base/ace/base/image.hpp:23-27`, straight-alpha
    sRGB8 RGBA, tightly packed `w*h*4`) and `base::Rgba8` (`:34-41`) —
    **D-export-2**. Both are exactly the types a sheet composer needs.
  - `encode_png` (`export.hpp:219`, `png_encode.cpp:53-79`) with the compression
    level and filter pinned (`png_encode.cpp:33-34`) — **D-export-3**. No new
    vendored header is needed here.
  - The A18 progress/cancel/report model — **D-export-7**
    (`export.hpp:127-166,248-307`).
  - `sanitize_stem` + `take_unique_stem` (`export.hpp:201`, `export.cpp:24-52`,
    `:70-104`) — **D-export-6**'s hostile-name closure and within-plan
    disambiguation, reused for the sheet's own filename.
  - `k_max_export_bytes` (`export.hpp:73`) — **D-export-4**'s refuse-as-a-value
    budget, applied here to the sheet buffer.
  - The Export view body `views::draw_export`
    (`src/views/ace/views/views.hpp:163-164`, `src/views/views.cpp:303-433`) and
    its panel state (`views.cpp:44-82`) — **D-export-9**. This leaf adds three
    widgets to that body; it introduces no new view, no rail item and no
    `ProjectGateway` virtual.
  - The L4 binding block (`src/app/shell.cpp:384-407`) and the
    cancel-then-join-before-document-release teardown (`shell.cpp:441-442`) —
    **Constraint 8** of the predecessor, inherited unchanged.
- **`editor.cameras.model`** — `scene::cameras(const arbc::Document&)` in layer
  order, each `scene::Camera` carrying `frame`, `resolution` and `name` (A14,
  `docs/01-architecture.md:376`). The sheet reads the same list the batch does.
- **`editor.cameras.rename_stable_id`** — camera names are free text with **no
  uniqueness constraint**, which is why the predecessor needed within-plan stem
  disambiguation (**D-export-6**) and why this leaf must not assume the sheet's
  own stem is free.

**Pending (owned here):**

- The contact-sheet plan, layout, composition and caption rendering; the two new
  `ExportOptions` fields plus the tile-size knob; the sheet's slot in the report;
  the three Export-panel widgets; and the doc delta (**A21**, plus a one-clause
  amendment to A20).

## What this task is

The Export panel gains a second output mode. With **Contact sheet** ticked, the
same run that writes one PNG per ticked camera also writes **one additional
PNG** — every ticked camera tiled into a uniform grid, `ceil(sqrt(N))` columns,
each camera fitted into a common square tile box preserving its own aspect,
separated by thin gutters, each tile carrying a small camera-name caption, all on
the same background the panel's background option selects. The two outputs are
independently toggleable, so a user can ask for the sheet alone (the fast "shot
map" of D6) without paying for N full-resolution renders.

The sheet is composed **entirely in L1 `commands`** on `base::Srgb8Image`
values, and it is composed by **copying pixels, never by filtering or blending
them**. That is possible because a tile is not a downscale of a full-resolution
render — it is a render *at* the tile's own resolution, through the already
injected `RenderFn`, which is the same operation the shipped N× multiplier
performs in the opposite direction. See **D-sheet-1**; it is the one place this
refinement departs from the mechanism the `.tji` note and A20's closing sentence
projected, and it carries a doc delta.

Captions are drawn from an **embedded fixed-width bitmap glyph table** compiled
into `commands`. ImGui's font atlas is L3-only (`docs/01-architecture.md:266`,
§8: `dock · views` is *"the ONLY layer that sees ImGui"*) and this job is
headless L1, so there is no font to borrow; the table is 95 ASCII cells of
`constexpr` data, not a new external dependency.

## Why it needs to be done

- **D14 promises it.** *"batch writes one file per camera (named by camera), and
  a **contact sheet** (all cameras tiled into one image) falls out for free"*
  (`docs/00-design.md:359-361`; the D14 row at `:481`). The predecessor
  deliberately deferred it rather than dropping it (**D-export-12**,
  `tasks/refinements/editor.cameras/export.md:912-930`), and A20's closing
  sentence names this leaf as the follow-up that satisfies the clause
  (`docs/01-architecture.md:382`). Until it lands, D14's export section is
  partially unimplemented.
- **D6's shot map has no cheap realization today.** Seeing all cameras at once
  currently means exporting N full-resolution PNGs and opening them in something
  else. A tile-resolution sheet is seconds of work for a project that would take
  minutes at native resolution.
- **The follow-up is on `m9_editor`'s critical path.** It is wired into the
  milestone through the `editor.cameras` container (`tasks/99-milestones.tji:8`);
  an unshipped leaf holds the container incomplete.
- **The seam is at its cheapest right now.** `ExportOptions`, `ExportReport`
  and the panel were designed one commit ago with this extension named in their
  own comments; every field added here is additive and every existing test keeps
  passing under the defaults.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **`docs/00-design.md` D14** (`:481`) — *"No separate export spec — a camera
  **is** the spec. Export = pick camera(s) → render each at its resolution
  (`render_offline`) → file(s); batch (one file per camera) + contact-sheet fall
  out … v1 PNG (sRGB8); knobs: transparent/filled bg, N× scale multiplier; async
  for heavy renders."*
- **`docs/00-design.md` §9 prose** (`:355-367`) — the paragraph the `.tji` cites
  as `:362`. Load-bearing for this leaf: *"a **contact sheet** (all cameras tiled
  into one image) falls out for free"* (`:360-361`), *"v1 format: **PNG**
  (working→sRGB8 encode)"* (`:362`), *"Knobs: transparent-vs-filled background,
  an optional **N× scale multiplier** (render this camera above its set
  resolution)"* (`:363-365`), *"Heavy renders run async with progress"*
  (`:365`).
- **`docs/00-design.md` D10** — the editor as the invisible colour translator.
  The reason the predecessor's **Constraint 5** forbids an sRGB8-space blend and
  the reason **D-sheet-2** eliminates blending altogether rather than moving it.
- **`docs/00-design.md` D15** — export is a pure reader: no transaction, no
  journal entry, not undoable.
- **`docs/00-design.md` D23** — refuse rather than guess. Applied here to an
  empty tick-list, an over-budget sheet, and both output toggles off.

**Governing architecture rows:**

- **`docs/01-architecture.md` A20** (`:382`) — the export pipeline's structure:
  the L1 `commands` kernel with an injected renderer, the `base::Srgb8Image`
  hoist, the one vendored encoder contained by `EXTERNAL_ALLOWED["stb_write"] =
  {"commands"}`, the `platform::Threads` job with A18-published progress, the
  Export view body. Its closing sentence names *"the contact sheet (D14's tiled
  variant, which needs an embedded glyph table for its captions and a
  working-space tile resample) is the additive follow-up
  `editor.cameras.contact_sheet`"* — the glyph-table half stands, the
  tile-resample half is amended by **D-sheet-1** / **A21**.
- **`docs/01-architecture.md` A18** (`:380`) — the published-immutable-snapshot
  discipline this leaf's second progress phase obeys.
- **`docs/01-architecture.md` A14** (`:376`) — cameras as an editor-defined
  `org.arbc.camera` kind; `scene::cameras()` in L1 `scene`; resolution + name are
  the content's serialized state.
- **`docs/01-architecture.md` §8** (`:256-292`) — the DAG. `commands` is **L1**
  with deps `base, project, scene`; `render` is **L2**; `commands → render` is
  *"not merely undeclared but **level-inverting**"* (A20's words). `base` is L0,
  *"value types, ids, small helpers"* (`:233`, `:276`).
- **`docs/01-architecture.md` §9** (`:294-320`) — the layered DoD this leaf's
  Acceptance criteria instantiate; §9.1 (`:322-357`) — the offscreen software-GL
  ASan lane.

**Editor seams this leaf extends:**

- `src/commands/ace/commands/export.hpp` — `k_max_export_bytes` (`:73`),
  `ExportOptions` (`:76-89`), `ExportItem` (`:93-106`), `ExportPlan`
  (`:108-113`), `ExportState` (`:116-122`), `ExportProgress` (`:127-132`),
  `ExportItemResult` (`:136-148`), `ExportReport` (`:150-166`), `RenderFn`
  (`:169-170`), `ShotCameraFn` (`:173-175`), `sanitize_stem` (`:201`),
  `plan_export` (`:213-214`), `encode_png` (`:219`), `ExportRunner` (`:222-230`),
  `run_export` (`:234-235`), `ExportService` (`:248-307`).
- `src/commands/export.cpp` — `take_unique_stem` (`:24-52`), `all_opaque`
  (`:56-66`), `sanitize_stem` (`:70-104`), `plan_export` (`:106-174`) with its
  budget refusal (`:149-160`) and render-camera derivation (`:165-166`),
  `run_export` (`:176-280`) with the publish lambda (`:181-191`), cancel check
  (`:223-226`), render (`:240-241`), encode (`:250`) and write (`:257-258`),
  `ExportService` (`:284-368`).
- `src/commands/png_encode.cpp:53-79` — `encode_png`.
- `src/base/ace/base/image.hpp:23-27,34-41` — `Srgb8Image`, `Rgba8`.
- `src/platform/ace/platform/filesystem.hpp:35-36,40` — `write_file`,
  `make_directories`.
- `src/views/views.cpp:44-82` (`ExportPanel` state), `:303-433` (the body), with
  the option block at `:349-360`, the run gate at `:380-381` and the dispatch at
  `:383-395`.
- `src/app/shell.cpp:384-407` (bindings), `:441-442` (teardown join).
- `scripts/check_levels.py` — `ALLOWED` (`:21-40`), `EXTERNAL_ALLOWED`
  (`stb_write` → `{"commands"}`). **Neither changes.**
- `CMakeLists.txt:191` (the vendored-encoder note), `:198` (the private
  `third_party/` include on `ace_commands`), `:252` (`ace_tests` sources), `:289`
  (`ace_shell_test` sources), `:277-278` (`ACE_GOLDEN_DIR`).

**Predecessor refinements:**

- `tasks/refinements/editor.cameras/export.md` — Decisions D-export-1 … 12,
  Constraints 1-13, and the follow-up registration at `:592-610` that this
  document answers.
- `tasks/refinements/cameras/model.md:286-290` — the render-through-camera
  golden reservation, already spent by the predecessor.

**Test rigs:**

- `tests/export_test.cpp` (879 lines, 17 cases) — the fake-filesystem +
  stub-renderer harness, the `walk_png`/`PngInfo` chunk walker (`:626`), the
  golden case (`:658`), the anti-vacuity naive-blend check (`:712-734`).
- `tests/export_e2e_test.cpp` (639 lines) —
  `IM_REGISTER_TEST(engine, "cameras", "export_panel")` (`:335`), the real
  `AppProjectGateway` + `CanvasView` + `ScratchDir` harness, `run_and_wait`, the
  teardown join (`:624-633`).
- `tests/canvas_host_test.cpp:1639` — the export TSan anchor.
- `tests/look_through_test.cpp:170-201` — the document-fixture recipe the
  goldens use.
- `tests/goldens/` — `export_camera_64x64.{rgba8,png}`,
  `export_filled_bg_64x64.rgba8`.

## Constraints / requirements

1. **A tile is a render, not a resample.** Each tile is produced by one call to
   the already-injected `RenderFn` at the tile's fitted pixel size, with the
   render camera from the already-injected `ShotCameraFn` at that same size. No
   image filter, no mip chain, no `arbc/media/image_resampler.hpp` include, no
   new L2 entry point. (**D-sheet-1**.)
2. **Composition copies; it never blends.** The sheet is allocated filled with
   the chosen background (`Rgba8{0,0,0,0}` when transparent), and each tile's
   pixels are **written**, not composited, into a rect that no other tile
   touches. Every tile carries the *same* background the sheet is filled with, so
   `replace` and `over` are provably identical here — which is why no colour
   arithmetic of any kind appears in `commands`. (**D-sheet-2**.)
3. **Captions are opaque pixel writes from an embedded table.** Glyphs are 1-bit
   cells scaled by an integer factor (nearest-neighbour replication), drawn as
   opaque white with an opaque black shadow at `(+scale, +scale)`. No
   antialiasing, no alpha ramp, no luminance analysis of the background.
   (**D-sheet-4**.)
4. **The caption never leaves its tile column.** `fit_text` truncates to the tile
   width and `draw_text` clips to the target image; a name longer than the tile
   can hold is truncated with a trailing `...`, never overdrawn into the
   neighbouring tile or off the sheet.
5. **Both output modes are independently controlled, and neither is guessed.**
   `ExportOptions` gains `write_items` (default `true`, preserving every shipped
   behaviour) and `contact_sheet` (default `false`). With both off, or with an
   empty tick-list, the run is **refused with a reason** (D23), and `Export` is
   disabled in the panel rather than silently reinterpreted.
6. **The N× multiplier does not apply to the sheet.** `scale` means *"render this
   camera above its **set resolution**"* (`docs/00-design.md:363-365`); the sheet
   has no camera resolution. Sheet size is governed solely by `tile_edge`.
   (**D-sheet-5**.)
7. **The sheet is planned over the ticked set, not over the batch's surviving
   subset.** A camera the batch refuses for exceeding `k_max_export_bytes` at N×
   still appears in the sheet — at tile resolution it costs a few hundred
   kilobytes — because the sheet's job is to be a *complete* map of what was
   asked for.
8. **The sheet is bounded by the same budget, and refuses as a value.**
   `sheet_w * sheet_h * 4` over `k_max_export_bytes` yields a plan with a
   `reason` and no tiles; the batch phase still runs. `tile_edge` is clamped to
   `[k_contact_tile_min, k_contact_tile_max]` at plan time.
9. **Errors are values end to end.** Nothing throws, nothing aborts, nothing
   asserts on user input. A render that returns an empty or wrongly-sized image
   leaves its tile at background and records a message.
10. **Cancel leaves no partial sheet.** The cancel flag is checked **between
    tiles**; a cancelled sheet is not encoded and not written. A half-composed
    grid on disk would be indistinguishable from a complete one and is a lie.
11. **Progress counts every render across both phases.** `total` =
    (`write_items` ? items : 0) + (`contact_sheet` ? tiles : 0); the published
    snapshot stays monotone in `done` with exactly one terminal state, per A18
    and the predecessor's **Constraint 10**.
12. **The sheet's filename cannot silently steal a camera's file.** The sheet
    stem is `contact-sheet`, run through the same `sanitize_stem` /
    `take_unique_stem` machinery **seeded with the batch plan's stems**, so a
    camera literally named `contact-sheet` keeps `contact-sheet.png` and the
    sheet becomes `contact-sheet-2.png`. A collision with a pre-existing file on
    disk still **overwrites in place**, matching **D-export-6**.
13. **Pure reader (D15).** No `transact`, no commit, no journal entry, no
    `WriterThread::submit`, no undo entry. Reads go through `pin()` /
    `scene::cameras()` exactly as the batch phase does.
14. **No new thread, no new component, no new external dependency, no new DAG
    edge.** The sheet is a second phase of the existing `ExportService` job.
    `scripts/check_levels.py`'s `ALLOWED` and `EXTERNAL_ALLOWED` are both
    **unchanged**; `src/base/**`, `src/render/**` and `src/dock/**` are
    untouched.
15. **The UI stays the catalogued Export view.** Three widgets driven by `###`
    id inside the shipped `views::draw_export` body. No new view, no rail item,
    no `ProjectGateway` virtual.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella.

**Levelization (`check_levels` clean) — the structural assertion:**

- `scripts/check_levels.py` is **unmodified**. `ALLOWED` (`:21-40`) and
  `EXTERNAL_ALLOWED` both keep their shipped contents — this leaf adds no
  component, no dependency edge and no external.
- `src/commands/**` gains `ace/commands/contact_sheet.hpp`, `contact_sheet.cpp`
  and `glyphs.cpp`, including only `<ace/base/image.hpp>`,
  `<ace/commands/export.hpp>`, `<ace/platform/…>`, `<ace/scene/…>` and the
  standard library. **No `<arbc/media/…>` include, no `render` include** — the
  positive proof of **D-sheet-1**, checkable by `grep` and asserted in review.
- `src/base/**`, `src/render/**`, `src/gl/**` and `src/dock/**` are byte-unchanged.
- `src/views/views.cpp` gains three widgets inside the existing `draw_export`
  body; `src/app/shell.cpp` is unchanged (the bindings it installs already cover
  both phases).
- Review-time inspection proof: temporarily adding `#include
  <arbc/media/image_resampler.hpp>` to `src/commands/contact_sheet.cpp` must make
  `check_levels` fail on the libarbc rule for `commands`.

**L1 logic — Catch2 unit (the bulk):** new **`tests/contact_sheet_test.cpp`**,
added to the `ace_tests` source list beside `tests/export_test.cpp`
(`CMakeLists.txt:252`), cases named `TEST_CASE("contact_sheet: …")`, reusing
`export_test.cpp`'s fake-filesystem and stub-renderer harness. Named laws:

- **The grid is `ceil(sqrt(N))` columns with the rows that follow.** Table-driven
  over `N = 1..17`: `cols == ceil(sqrt(N))`, `rows == ceil(N/cols)`,
  `rows*cols >= N`, and `(rows-1)*cols < N` (no wholly empty trailing row).
- **Tile boxes tile the sheet without overlap and stay inside it.** Every tile
  rect is within `[0,width) × [0,height)`; no two tile rects (box + caption
  strip) intersect; gutters separate them by exactly `k_contact_gutter`.
- **Each tile preserves its camera's aspect inside the common box.** Cameras at
  16:9, 1:1 and 9:16 with a `tile_edge` of 96: each fit's long edge equals
  `tile_edge`, the fitted aspect matches the camera's within one pixel, both
  dimensions are `>= 1`, and the image origin is the centred offset.
- **The render camera comes from the injected `ShotCameraFn` at the FITTED
  size.** For a 16:9 camera, `tile.render_camera ==
  shot_camera(frame, res.w, res.h, fit_w, fit_h)` componentwise; **anti-vacuity**:
  asserted **≠** the camera derived at `(res.w, res.h)` and **≠** the camera
  derived at `(tile_edge, tile_edge)`.
- **Composition never blends — a tile rect is byte-identical to its render.**
  Stub renderer emits a known per-pixel gradient keyed by camera index; after
  `compose_contact_sheet`, `memcmp` of each tile's rows against that render is
  zero. This is the law that makes **D-sheet-2** an assertion rather than a
  claim: any blend, filter or premultiply anywhere in the path breaks it.
- **The sheet fill is exactly the chosen background, transparent by default.**
  Gutter pixels, letterbox pixels inside a non-square tile's box, and the empty
  slots of a partially-filled last row are all exactly `{0,0,0,0}` with the
  default options, and exactly the chosen `Rgba8` when `background` is set.
- **Captions write only glyph and shadow pixels.** A caption strip for a known
  name contains exactly three distinct colours (background, opaque white, opaque
  black); **anti-vacuity**: the strip is asserted **not** uniformly background,
  and the white pixel count matches the table's set-bit count for that string at
  that scale.
- **A caption too wide for the tile is truncated, never overdrawn.** A 200-char
  name at `tile_edge = 64`: the drawn text ends with `...`, `text_width(fit) <=
  tile_edge`, and no pixel outside the tile's column differs from background.
- **Unmapped bytes render as one fallback box, not one per byte.** `"Café"`
  (UTF-8, 5 bytes) draws `C`, `a`, `f` and **one** box glyph;
  `text_width` agrees with what was drawn.
- **An empty tick-list is refused, not guessed.** `plan_contact_sheet` over zero
  cameras yields no tiles and a non-empty `reason`; the run writes **no** file
  (asserted against the recording `FakeFileSystem`).
- **An oversized sheet is refused as a value, and the batch still runs.**
  `tile_edge = k_contact_tile_max` with enough cameras to exceed
  `k_max_export_bytes`: the sheet is refused with the requested dimensions in the
  message, `report.contact_sheet->refused == true`, and every batch item is still
  written.
- **Cancel between tiles writes no partial sheet.** Stub setting the flag on its
  second tile render: state `Cancelled`, no sheet path recorded by the fake
  filesystem, and any previously written batch items intact.
- **The sheet stem yields to a camera of the same name.** A camera named
  `contact-sheet` plus `write_items = true`: the batch keeps
  `contact-sheet.png`, the sheet takes `contact-sheet-2.png`; with
  `write_items = false` the sheet takes `contact-sheet.png`.
- **Progress counts both phases and stays monotone and terminal.** Three cameras
  with both outputs on: `total == 6` throughout, `done` non-decreasing, exactly
  one terminal state, and `current_name` naming the camera being rendered in
  either phase.
- **Layout and bytes are deterministic.** The same document, tick-list and
  options run twice produce an identical `ContactSheetPlan` (field-by-field) and
  byte-identical sheet pixels.
- **Existing export behaviour is unchanged under the defaults.** The 17 shipped
  cases in `tests/export_test.cpp` pass untouched — `write_items = true`,
  `contact_sheet = false` is the shipped path exactly.

**Rendered output — golden:**

- **`tests/goldens/contact_sheet_3cam.rgba8`** — the composed sheet's raw
  `Srgb8Image` pixels, byte-exact via `compare_golden`. Fixture built by the
  `tests/look_through_test.cpp:170-201` recipe with three cameras of distinct
  aspects (64×64, 96×54, 32×64) named `Hero`, `Wide` and `A very long camera name
  that will not fit`, `tile_edge = 96` → 2 columns × 2 rows with one empty slot,
  rendered through the **real** `render::render_document_srgb8` bound exactly as
  `src/app/shell.cpp:385-394` binds it. This golden pins, in one artifact: the
  grid geometry, the aspect fit, the tile blit, the background fill of the empty
  slot, the caption glyphs, the shadow, and the truncation ellipsis.
- **Anti-vacuity within the same case:** the golden is asserted **not** uniformly
  background; the empty fourth slot **is** exactly background; and each occupied
  tile rect is asserted byte-identical to an independent direct render of that
  camera at its fitted size — so a sheet regenerated from a broken composer
  cannot pass by agreeing with itself.
- **No second `.png` golden.** `tests/goldens/export_camera_64x64.png` already
  pins the encoder byte-for-byte (**D-export-11**); the sheet reaches disk through
  the identical `encode_png` call, so a second encoded golden would re-assert the
  encoder and nothing else. The e2e's chunk walk covers the sheet's on-disk
  structure instead.

**UI e2e — ImGui Test Engine:** new **`tests/contact_sheet_e2e_test.cpp`**, added
to the `ace_shell_test` source list (`CMakeLists.txt:289`), registered
**`IM_REGISTER_TEST(engine, "cameras", "contact_sheet_panel")`**, built on
`tests/export_e2e_test.cpp`'s harness (real `AppProjectGateway` + real
`CanvasView` over a `ScratchDir` project, `NoopFolderDialog`, `run_and_wait`
waiting on `!service.running()`), asserting on **model state and on-disk files,
never on pixels**, and leaving the shipped `export_panel` test untouched. Phases:

1. Open the Export view through the rail's view launcher; mint three cameras
   (`###new_shot_from_view`, with canvas navigation between) and give them
   distinct resolutions through the camera inspector. `###export_contact_sheet`
   is present and **unticked**; `###export_items` is present and **ticked**.
2. Tick all three cameras, run with the defaults → exactly three files, **no**
   `contact-sheet.png` on disk. The new mode is genuinely opt-in.
3. Tick `###export_contact_sheet`, untick `###export_items`, run → exactly **one**
   new file, `<scratch>/exports/contact-sheet.png`; its first 8 bytes are the PNG
   signature and its `IHDR` width/height equal the dimensions the in-test
   `plan_contact_sheet` computes for the same inputs. No per-camera file is
   added or modified.
4. Set `###export_tile` to 128 and rerun → the `IHDR` dimensions change
   accordingly (**anti-vacuity**: the knob is live, not decorative), the sheet is
   **overwritten in place**, and no `contact-sheet-2.png` appears.
5. Tick `###export_items` as well and rerun → three camera files **and** the
   sheet, from one dispatch, with `###export_status` reaching a terminal state
   once.
6. Enable `###export_bg_filled` with an opaque colour and rerun → the written
   sheet is fully opaque, asserted by recomposing the sheet in-test through the
   same public `compose_contact_sheet` and comparing (no PNG decoder is
   introduced, per **D-export-11**).
7. Rename a camera to `contact-sheet` through the inspector and rerun → both
   `contact-sheet.png` (the camera) and `contact-sheet-2.png` (the sheet) exist.
8. Untick both `###export_items` and `###export_contact_sheet` → `###export_run`
   is **disabled**; re-tick one → enabled again.
9. **Cancel during the sheet phase** — many cameras at `k_contact_tile_max`:
   `###export_cancel` is enabled while running; clicking drives the state to
   `Cancelled`, `###export_status` reflects it, and **no** sheet file exists (or,
   if one existed from a prior phase, its bytes are unchanged).
10. **The panel does not touch the document** — `scene::cameras()` count, every
    camera's `resolution`/`frame`, `commands::Selection` and journal depth are
    unchanged across every phase; `Ctrl+Z` after a run undoes the **rename**, not
    an export (D15 / Constraint 13).

**Threading (ASan/TSan) — explicitly scoped:** this leaf adds **no new thread**;
it lengthens the existing export job and adds a second publishing phase, so the
coverage is an extension of the shipped anchors rather than a new lane.

- `tests/canvas_host_test.cpp:1639` (the export TSan anchor) gains a variant
  running with `contact_sheet = true` and `write_items = false`: the sheet phase
  performs N additional lock-free reads of the live document while a real
  `WriterThread` commits `add_camera` / `rename_camera` edits from the UI thread.
  Asserts TSan-clean and a signature-valid sheet on disk — the direct test of
  Constraint 13's pure-reader claim across the new phase.
- A Catch2 case in `tests/contact_sheet_test.cpp` pins that a job **still in its
  sheet phase** at teardown is cancelled and joined before the document is
  released — fails under **ASan as a use-after-free** if the join in
  `src/app/shell.cpp:441-442` stops covering the longer job.
- The A18 published-progress case is extended to span both phases: a reader
  thread `load()`ing continuously while the worker publishes across the item→tile
  transition is TSan-clean, and a snapshot held across the transition keeps its
  values.
- The new e2e runs in the existing offscreen software-GL ASan lane (§9.1) with
  **no new `tests/lsan.supp` suppression**.

**Coverage:**

- ≥90% diff coverage on changed lines (`diff-cover --fail-under=90`);
  clang-format and build clean. Tests ship with the task.
- The glyph table is `constexpr` data, not branching code — gcov emits no arcs
  for it, so it neither inflates nor deflates the diff-coverage ratio. If the
  tool does attribute lines to it, `src/commands/glyphs.cpp` gets the same
  exclusion treatment `third_party/` already receives, and the note says why.

**Doc delta (same commit):**

- **`docs/01-architecture.md` A21** — a new row for the contact-sheet structure:
  tiles are rendered at tile resolution through the shipped injected `RenderFn`
  rather than downsampled, so composition is a pure copy in L1 `commands` with no
  resampler, no libarbc media include and no new L2 entry point; captions come
  from an embedded ASCII bitmap glyph table compiled into `commands`, drawn as
  opaque white with a black shadow; the sheet is a second phase of the existing
  `ExportService` job sharing its progress, cancel and report. See **D-sheet-1**,
  **D-sheet-2**, **D-sheet-4**.
- **`docs/01-architecture.md` A20** — a one-clause inline amendment appended to
  its closing sentence, in the house `*(Amended by …)*` form, so A20 stops
  asserting the working-space tile resample it projected. The rest of A20 is
  untouched.
- **No `docs/00-design.md` change.** D14 already promises the contact sheet and
  names nothing about how it is composed; the two new panel widgets sit inside
  D14's own knob set and D-export-9's view.

**Deferred WBS work (closer registers in the WBS):**

- **`editor.cameras.caption_latin1`** — *"Contact-sheet captions: Latin-1 camera
  names"*, **0.5d**, `allocate team`, `depends !contact_sheet`, under
  `task cameras` (`tasks/00-editor.tji:306`), already wired into `m9_editor`
  (`tasks/99-milestones.tji:8`) through the `editor.cameras` container
  dependency. Scope: decode the camera name from UTF-8 to code points inside
  `commands`, extend the embedded glyph table with the printable Latin-1 range
  (U+00A0–U+00FF), drive `text_width` / `fit_text` / `draw_text` off code points
  instead of bytes, and keep the fallback box for anything outside the table.
  Motivation: camera names are free text with no charset constraint
  (`editor.cameras.rename_stable_id`), and D3/D23's arbitrariness principle
  argues against silently boxing a name the user typed — `"Café"` rendering as
  `Caf□` is a visible defect, just a bounded one. Tests: table-driven Catch2
  cases over accented names and malformed UTF-8 (which must still produce boxes,
  never read out of bounds), plus one golden with an accented caption.
  Source-of-debt: `tasks/refinements/editor/contact_sheet.md`.
- Everything else out of scope already has a scheduled owner or a standing
  decision: **export from the overview shot map** is `editor.panels.overview`
  (`.tji:439-441`); **EXR/TIFF float output** is deferred by D14 itself
  (`docs/00-design.md:362-363`) pending D10's wider-gamut work; **a rail item or
  keyboard chord for export** waits on §11's input map, the same call
  `D-frame_selection-8` and **D-export-9** made; **multi-page sheets for very
  large N** are not registered — Constraint 8's budget refusal is the honest
  bound, and no paging behaviour has a user asking for it.

## Decisions

- **D-sheet-1 — A tile is rendered AT tile resolution through the shipped
  `RenderFn`; there is no downscale and no resampler.**
  For each camera, the fitted tile size `(fit_w, fit_h)` is computed first, then
  the tile is produced by exactly one `render(shot_camera(frame, res.w, res.h,
  fit_w, fit_h), fit_w, fit_h, background)` call — the same call
  `export.cpp:165-166,240-241` already makes for the batch, with a smaller output
  box instead of a larger one.
  *Rationale:* (i) **It is the operation D14 already defines.** The N× multiplier
  is *"render this camera above its set resolution"* (`docs/00-design.md:363-365`)
  and the predecessor's **Constraint 2** insists it is *"a resolution multiply,
  not a resample"*. Rendering below the set resolution is the identical
  operation; treating one direction as a render and the other as a filter would
  be an arbitrary asymmetry. (ii) **It produces a better image.** The compositor
  samples each source raster with libarbc's own working-space resampler at the
  requested output scale; a render-then-decimate path samples the sources at full
  resolution and then applies a *second*, editor-authored filter on top. One
  correct resample beats two. (iii) **It removes the whole reason the resample was
  scary.** `arbc/media/image_resampler.hpp` is a per-pixel tap kernel over
  `arbc::WorkingPixel` — `decimate_half_band` is a **2:1 half-band decimator**
  (`:105`) and `sample_bicubic` is a **magnifier** (`:162`); an arbitrary-ratio
  downscale means an editor-authored mip chain plus a bicubic tail, with its own
  quality decisions and its own goldens. And because the working-space surface
  never escapes `render.cpp` (it is allocated, composited and converted inside
  `render_document_srgb8_over`, `render.cpp:63-80`), consuming it would require a
  **new L2 entry point** that fuses render+resample+convert. (iv) **It is
  cheaper where it matters.** A sheet-only run of twelve 4K cameras renders
  twelve ~250×140 frames instead of twelve 8.3-megapixel frames with their
  `w*h*20`-byte working buffers — seconds instead of minutes, and
  `k_max_export_bytes` can never refuse a tile. When both outputs are requested,
  the marginal cost of the tile renders is negligible against the native ones.
  *Alternative rejected:* **render at native, resample in linear working space in
  L2 `render`** — the mechanism the `.tji` note and A20's closing sentence
  projected. It is not wrong, but it buys a worse image for more code: a new L2
  entry point, an editor-owned mip-chain filter, a second golden to pin the
  filter, and a saving that only materializes when the user asked for the
  full-resolution files anyway. Its stated motivation — *"not an sRGB8 box
  filter"* — is satisfied more completely by having **no filter at all**.
  *Alternative rejected:* **downsample the `Srgb8Image` in `commands` with a box
  filter.** This is the thing the `.tji` explicitly forbids, and correctly: a
  gamma-space average is wrong in a way a golden written from the same wrong code
  cannot catch (**D-export-5**'s reasoning).
  **No new DAG edge, no new external, no new L2 entry point. Doc delta: A21, and
  a one-clause amendment to A20.**

- **D-sheet-2 — Composition is a clipped pixel COPY, never a blend, and that is
  provable rather than merely intended.**
  The sheet is allocated filled with the background (`{0,0,0,0}` when
  transparent), tile rects do not overlap, and each tile's rows are written into
  its rect with `std::copy`. Every tile was rendered with the *same* `background`
  option the sheet is filled with, so the destination under a tile is either
  fully transparent (transparent mode → `over` degenerates to `replace`) or
  exactly the tile's own opaque backing (filled mode → `over` degenerates to
  `replace`). In both cases `replace ≡ over`, so the correct composite is
  achieved with zero colour arithmetic.
  *Rationale:* D10 makes the editor the invisible translator and **D-export-5**
  established that a gamma-space `over` is the specific error a self-generated
  golden cannot catch. The strongest available answer is not "blend correctly in
  L1" (which would need transfer functions in `commands`) nor "move the blend to
  L2" (which would need the resample of D-sheet-1) but **eliminate the blend**.
  The law is directly testable — the byte-identity of each tile rect against an
  independent render of the same camera — and any filter, premultiply or blend
  introduced later breaks that test immediately.
  *Alternative rejected:* **alpha-composite tiles over the sheet in `commands`.**
  Unnecessary given the background invariant, and it would reintroduce exactly
  the gamma-space blend Constraint 5 forbids.
  **No doc delta beyond A21.**

- **D-sheet-3 — The layout is fully determined by one knob, `tile_edge`.**
  `cols = ceil(sqrt(N))`, `rows = ceil(N/cols)`; the tile box is a `tile_edge ×
  tile_edge` square; the cell is the box plus a caption strip below it; the
  gutter is a fixed `k_contact_gutter = 8`; `sheet_w = g + cols*(tile_edge + g)`
  and `sheet_h = g + rows*(cell_h + g)`. `tile_edge` defaults to 256 and is
  clamped to `[32, 1024]`. The aspect fit uses deterministic integer rounding
  (long edge = `tile_edge`; short edge = `max(1, (tile_edge*short + long/2) /
  long)`) and the fitted image is centred with a floor-divided offset.
  *Rationale:* the knob the user actually cares about is *how big is each
  thumbnail* — that is what governs legibility of both the image and its caption.
  Deriving the sheet size from it keeps every other number a pure function of
  `(N, tile_edge)`, which is what makes the layout Catch2-testable without a
  document at all. A fixed gutter avoids a second knob whose only effect is
  cosmetic.
  *Alternative rejected:* **a target sheet width, with the tile size derived.**
  Closer to how print contact sheets are specified, but it makes tile size — and
  therefore caption legibility — a non-obvious function of N, so adding one
  camera silently shrinks every thumbnail.
  *Alternative rejected:* **tile box sized from the largest camera's resolution.**
  One 8K camera would blow the sheet up for everyone else.
  **No doc delta.**

- **D-sheet-4 — Captions come from an in-tree `constexpr` ASCII bitmap glyph
  table, drawn opaque white with a black shadow.**
  A 5×7 glyph in a 6×8 cell, 95 cells covering U+0020–U+007E, stored as
  `constexpr std::array<std::uint8_t, 95*8>` (one byte per row, low five bits
  left-to-right) in `src/commands/glyphs.cpp` — 760 bytes of data. Scaling is
  integer nearest-neighbour pixel replication at `scale = clamp(tile_edge/128, 1,
  4)`. Each set bit writes an opaque black pixel at `(+scale, +scale)` first, then
  an opaque white pixel at the glyph position. Bytes outside the table — including
  every byte of a multi-byte UTF-8 sequence — collapse into a **single** fallback
  hollow box per unmapped run.
  *Rationale:* (i) ImGui's font atlas is structurally unreachable — §8 makes
  `views`/`dock` *"the ONLY layer that sees ImGui"* (`:266`) and this is an L1
  headless job. (ii) A table is not a dependency: A20's ledger says the editor
  vendors **one** encode dependency, and that stays true — no `stb_easy_font`, no
  TTF, no rasterizer, no new `EXTERNAL_ALLOWED` entry, no `assets/` directory to
  ship and locate at runtime. (iii) The white-plus-shadow pair is legible on any
  background — light, dark, mid-grey, or transparent viewed over unknown backing —
  **without analysing the background at all**, which keeps colour math out of
  `commands` entirely and keeps the caption a pure pixel write under D-sheet-2.
  (iv) 1-bit glyphs mean no alpha ramp, so a caption cannot introduce the blend
  D-sheet-2 eliminated.
  *Alternative rejected:* **choose black or white text by background luminance.**
  Cleaner output, but it needs an sRGB→linear transfer inside `commands` (colour
  math D10 says the editor should not be doing by hand), it still fails on
  mid-grey, and it has no answer at all for a transparent background.
  *Alternative rejected:* **vendor `stb_easy_font` or a TTF rasterizer.** A second
  external dependency, and both emit geometry or antialiased coverage — a
  rasterizer's grey edge pixels are an alpha blend, reintroducing the exact
  problem D-sheet-2 removed, for captions measured in single-digit pixels.
  *Alternative rejected:* **no captions.** Contradicts a decision already recorded
  in the `.tji` and rejected once by **D-export-12**; an uncaptioned grid of crops
  is materially worse as D6's shot map.
  **No new external dependency. Doc delta: A21.**

- **D-sheet-5 — The sheet is a second PHASE of the existing job, with two
  independent output toggles, and the N× multiplier does not reach it.**
  `ExportOptions` gains `bool write_items = true`, `bool contact_sheet = false`
  and `int tile_edge = 256`; `ExportReport` gains
  `std::optional<ExportItemResult> contact_sheet`; `run_export` runs the item
  loop (when `write_items`) then the sheet (when `contact_sheet`), publishing into
  the same `ExportProgress` with `total` covering both phases. The panel gains
  `Per-camera files###export_items`, `Contact sheet###export_contact_sheet` and
  `Tile size###export_tile`, and `###export_run` is disabled when both outputs are
  off.
  *Rationale:* (i) **Additive by construction** — the defaults reproduce the
  shipped behaviour exactly, so all 17 `export_test.cpp` cases and all 11
  `export_e2e_test.cpp` phases keep passing unmodified, which is the cheapest
  possible proof that this leaf broke nothing. (ii) **One job, one progress, one
  cancel, one report** — a second `ExportService` would duplicate the A18
  publication, the teardown join (`shell.cpp:441-442`) and the TSan anchor for no
  behavioural gain, and would let two renders of the same document race for the
  same destination. (iii) **The sheet-alone case is the common one** — D6's shot
  map is a browsing artifact, not a delivery artifact, so making the per-camera
  files optional is what makes the fast path fast. (iv) `scale` is defined
  against a camera's *set resolution* (`docs/00-design.md:363-365`); the sheet has
  none, and letting one control mean two things is how knobs become folklore.
  *Alternative rejected:* **a separate "Contact sheet" run button.** Two dispatch
  paths, two progress readouts, and the natural "give me both" request becomes two
  clicks and two documents-worth of renders.
  *Alternative rejected:* **apply N× to the sheet as a supersample.** It would
  contradict D-sheet-1's no-resample invariant (a supersample is only useful if
  you downsample afterwards) and overload a control D14 defines precisely.
  **No doc delta beyond A21.**

- **D-sheet-6 — The sheet's filename yields to a camera's, and overwrites in
  place otherwise.**
  The stem is `contact-sheet`, run through the shipped `sanitize_stem` /
  `take_unique_stem` pair **seeded with the batch plan's already-taken stems**, so
  a camera named `contact-sheet` keeps `contact-sheet.png` and the sheet becomes
  `contact-sheet-2.png`. A pre-existing file of that name on disk is overwritten.
  *Rationale:* **D-export-6** already settled both halves of this — within-plan
  collisions disambiguate (because losing one of two files is data loss, not
  idempotence) and disk collisions overwrite (because batch export is idempotent,
  mirroring Save's re-dump). The only new question is precedence, and the camera
  must win: a camera's file is named by the user's own data, while the sheet's
  name is the editor's choice and is therefore the one that can move.
  *Alternative rejected:* **reserve `contact-sheet` first and renumber the
  camera.** Renames the user's file for the editor's convenience.
  *Alternative rejected:* **timestamp the sheet.** Breaks idempotence — the whole
  reason **D-export-6** chose overwrite — and would need a clock in L1.
  **No doc delta.**

- **D-sheet-7 — A cancelled sheet is not written; a refused sheet is a value.**
  Cancel is checked between tile renders; on cancel the sheet is abandoned before
  `encode_png`. An over-budget sheet (`sheet_w*sheet_h*4 > k_max_export_bytes`)
  yields a plan with a `reason`, no tiles, and a `refused` result — while the
  batch phase still runs and writes its files.
  *Rationale:* a half-composed grid is byte-valid PNG and visually
  indistinguishable from a complete one, so writing it would be the one failure
  mode the user cannot detect. That is a stronger reason than the batch had for
  keeping partial output (**D-export-7** keeps already-written per-camera files
  precisely *because* each is individually complete and self-describing). The
  refusal follows **D-export-4** and D23 verbatim: name the resource, name the
  requested size, refuse the one thing, keep the rest of the run.
  **No doc delta.**

## Open questions

`(none — all decided.)`

One item is surfaced to the orchestrator for `tasks/parking-lot.md` rather than
the WBS, because it is a human design judgement and not agent-implementable work:
**whether the editor should eventually ship a real bundled font** (the `assets/
icons, fonts (bundled)` slot `docs/01-architecture.md:247` reserves and nothing
has ever filled) for offline text rendering — captions, watermarks, slates —
instead of an embedded bitmap table. That is a new-external-dependency and
product-scope call, it is on nothing's critical path, and **D-sheet-4** makes the
defensible interim choice. `editor.cameras.caption_latin1` extends the table
within the existing decision and does not presume the answer.

## Status

**Done** — 2026-07-23.

- `src/commands/ace/commands/contact_sheet.hpp` — `ContactSheetPlan`, `plan_contact_sheet`, `compose_contact_sheet` (L1 commands; no render or libarbc-media include).
- `src/commands/contact_sheet.cpp` — grid layout (`ceil(sqrt(N))` columns, gutter geometry, aspect fit with integer rounding), tile blit (`std::copy` rows, never blend), budget refusal as a value, cancel-between-tiles, sheet filename via `take_unique_stem` seeded with batch stems.
- `src/commands/glyphs.cpp` — embedded 5×7 constexpr ASCII bitmap glyph table (U+0020–U+007E, 760 bytes); `text_width`, `fit_text`, `draw_text` with integer-factor nearest-neighbour scaling, shadow, truncation ellipsis, single-fallback-box for unmapped multi-byte UTF-8 runs.
- `src/commands/ace/commands/export.hpp` — `ExportOptions` gains `write_items`, `contact_sheet`, `tile_edge`; `ExportReport` gains `std::optional<ExportItemResult> contact_sheet`; `ExportService` carries a monotonic `instance()` id (fixes pointer-reuse reset bug surfaced in shell-test).
- `src/commands/export.cpp` — `run_export` extended with a second phase (sheet) sharing progress, cancel and report with the item phase; `ExportService` atomic instance counter.
- `src/views/views.cpp` — Export panel gains `Per-camera files###export_items`, `Contact sheet###export_contact_sheet`, `Tile size###export_tile`; run button disabled when both outputs off; panel reset keyed on `service.instance()` instead of pointer address.
- `tests/contact_sheet_test.cpp` — 17 Catch2 `TEST_CASE("contact_sheet: …")` cases covering all 16 named laws (grid geometry, tile aspect, composition copy, caption pixels, truncation, UTF-8 fallback, budget refusal, cancel, stem yield, progress, determinism, defaults).
- `tests/contact_sheet_e2e_test.cpp` — `IM_REGISTER_TEST(engine, "cameras", "contact_sheet_panel")` 10-phase ImGui Test Engine e2e (opt-in, sheet-only, tile-size knob, overwrite-in-place, both outputs, filled-bg verify, stem-collision, run-disabled, cancel, D15 no-undo).
- `tests/goldens/contact_sheet_3cam.rgba8` — byte-exact composed sheet for three cameras (64×64 Hero, 96×54 Wide, 32×64 long-name), `tile_edge=96`, 2×2 grid; anti-vacuity checks on tile rects and empty slot.
- `tests/canvas_host_test.cpp` — TSan anchor extended with a `contact_sheet=true, write_items=false` variant.
- `CMakeLists.txt` — `contact_sheet.cpp`, `glyphs.cpp`, `contact_sheet_test.cpp`, `contact_sheet_e2e_test.cpp` added to source lists.
- `docs/01-architecture.md` — A21 (contact-sheet structure: render-at-tile-size, copy-compose, embedded glyph table, second ExportService phase) + A20 one-clause amendment retiring the projected working-space resample.
- One acceptance criterion noted unsatisfiable as written (`#include <arbc/media/image_resampler.hpp>` in `src/commands/` passes `check_levels` because `commands` is in `EXTERNAL_ALLOWED["arbc"]`); the substantive invariant holds and is grep-verified (no `arbc/media`, no `image_resampler`, no `ace/render` in `src/commands/`).
- Follow-up tasks registered: `editor.cameras.caption_latin1`, `editor.cameras.export_destination_reseed`.
