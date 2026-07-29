# editor.cells.resolution — Cell resolution display; resample-to-crisp; health badge

## TaskJuggler entry

- **Task:** `editor.cells.resolution` (`tasks/00-editor.tji:533-538`, under
  `task cells "Cells & manipulation"` at `:512`).
- **Effort:** `2d` (`:534`) · `allocate team` (`:535`).
- **Depends:** `!selection` (`editor.cells.selection`, `:536`), which is
  `complete 100` (`:523`) — the dependency is satisfied.
- **Note (`.tji:537`):** "Show a cell's native/working resolution separately from
  placement; 'resample to crisp' grows a raster's grid; a referenced image is
  source-limited (the file is the floor). The health badge flags a camera
  out-resolving a cell. Design: D8."
- **Back-link:** the `.tji` note currently ends `Design: D8. Refinement:
  tasks/refinements/resolution_ops.md` (the flat interim path). This refinement
  lands at **`tasks/refinements/editor/resolution.md`** per the orchestrator's
  area = first-dot-segment (`editor`) assignment (`tasks/refinements/README.md:9-18`);
  the closer updates the note back-link to the real path and adds `complete 100`
  after `allocate team` (`tasks/refinements/README.md:47-68`). **Do not** hand-edit
  the `.tji` here.
- **Downstream:** `editor.cells.resolution` is a `depends` of the M-editor
  milestone (`tasks/00-editor.tji:658`). It is the leaf `editor.cameras.manip`
  (Done) repeatedly names as the owner of the resolution-**health** readout it
  deferred: `tasks/refinements/editor/manip.md:333-334` ("the per-camera
  resolution-**health** read to `editor.cells.resolution` … it owns the health
  badge") and `:501-502`. It closes the D8 "placement is not resampling" story on
  the **cell** side, mirroring what `editor.cameras.manip` closed on the camera
  side (D9). The dense multi-property Inspector sheet that will compose this
  readout is a distinct scheduled leaf, `editor.panels.inspector` (`:564`).

## Effort estimate

**2 days.** The two shippable halves — the resolution *display* and the resolution
*health badge* — are pure reads plus pure math over seams that already exist; the
third named half, the resample-to-crisp *mutation*, has **no libarbc primitive** and
is deferred to a cross-repo verb (see Decisions D-resolution-5, Open questions).

- **Resolution display** reuses `scene::Cell::content_bounds` (`src/scene/ace/scene/cell.hpp:174`,
  D-selection-11) — for a raster or a referenced image, the content-space extent's
  dimensions **are** the native pixel grid (`arbc::Content::bounds()`,
  `.../contract/content.hpp:515`; raster `Rect{0,0,w,h}` in pixels, image the decoded
  master extent). No new library surface. ~0.3d incl. Catch2.
- **Provenance classification** (painted-raster vs referenced-image vs
  resolution-independent) reads two **generic** `arbc::Content` virtuals the editor
  already may call — `editable()` (`content.hpp:609`, non-null ⇒ owns a mutable grid)
  and `external_asset_ref()` (`content.hpp:672`, non-empty ⇒ borrowed external file) —
  never a `kind_id` string-switch (A16). ~0.3d incl. Catch2.
- **Resolution-health math** is a pure L1 `interact` helper over **primitive** inputs
  (native px, cell placement `Affine`, camera `Resolution`, camera frame `Affine`),
  beside the shot/frame helpers `editor.cameras.manip`/`editor.cameras.look_through`
  already landed (`src/interact/ace/interact/interact.hpp`). ~0.6d incl. Catch2.
- **First-cut inspector readout** extends the existing `ViewType::Inspector` body —
  `app::CameraInspector` (`src/app/ace/app/camera_inspector.hpp:25-31`, wired
  `src/app/shell.cpp:360-370`), whose own header (`:21-24`) reserves the per-camera
  health read for this leaf — to show a selected cell's native-vs-placed size and its
  health against the capturing camera(s). ~0.5d incl. e2e + screenshot baseline.

**No new component, no new DAG edge, no new libarbc surface consumed, no doc delta.**

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cells.selection`** (`tasks/refinements/editor.cells/selection.md`, Done
  2026-07-23) — the direct predecessor. It ships:
  - the project-level `commands::Selection` keyed by content `ObjectId`, read fresh
    each frame off `AppState` (`src/commands/ace/commands/app_state.hpp:53-54`), so
    "which cell's resolution do we show" is answered by the existing selection with
    **no new state** (D-selection-1/-2).
  - **`scene::Cell::content_bounds`** (`src/scene/ace/scene/cell.hpp:174`,
    **D-selection-11**) — `arbc::Content::bounds()` read off the **same** pinned
    snapshot that produced `placement`, `nullopt` = unbounded. Its doc comment
    (`cell.hpp:170-174`) names "the inspector's 'placed size'" as a consumer; this
    leaf is that consumer, and the field's dimensions double as the native pixel
    grid for a raster/image cell (D-resolution-1).
  - **`interact::PickTarget{id, layer, kind, placement, extent}`**
    (`src/interact/ace/interact/pick.hpp:50-62`) and the `interact→scene` assembly
    adapter `pick_targets` (`pick.hpp:182-183`), which merges cells and cameras into
    one z-ordered target list (A17) — the shape the health readout iterates.
  - **Open question 2** (`selection.md`, routed to `tasks/parking-lot.md`): reading
    `arbc::Content::bounds()` on the UI thread while the render thread walks the same
    document is safe *today* because every editor cell's extent is construction-fixed;
    it names **`editor.cells.resolution`'s "resample to crisp"** (a growing raster)
    as the future edit that would turn `bounds()` into a genuine read/write pair. This
    leaf **honours** that boundary: it ships **no** grid-growing mutation (D-resolution-5),
    so it introduces no such write; the read/write pair is `editor.cells.resample_apply`'s
    obligation when the upstream verb lands (Open questions).
- **`editor.cameras.manip`** (`tasks/refinements/editor/manip.md`, Done 2026-07-22)
  — the camera side of the same D7/D8/D9 story. It ships the camera model this leaf
  reads for the health comparison: `scene::Resolution{int width; int height;}`
  (`src/scene/ace/scene/camera.hpp:26-31`), `CameraContent::resolution()`
  (`camera.hpp:91`), `scene::Camera{id, layer, name, resolution, frame}`
  (`camera.hpp:123-129`) and `scene::cameras(const arbc::Document&)`
  (`camera.hpp:134`), all over the lock-free `pin()`. It also established the
  **first-cut `ViewType::Inspector` body** pattern (`app::CameraInspector`) this leaf
  extends, and (D-manip-6, `manip.md:493-513`) explicitly deferred the per-camera
  resolution-health readout, cross-object snapping, unified selection, and the dense
  property sheet to their scheduled sibling leaves — this leaf is the health-readout
  owner it named.
- **`editor.cameras.look_through`** (`tasks/refinements/editor/look_through.md`, Done)
  — `interact::viewport_camera_for_shot(frame, native_w, native_h, out_w, out_h)`
  (`src/interact/ace/interact/interact.hpp:104-118`, the comp↔device derivation) and
  the transient viewport camera the active canvas frames the composition through. The
  health math reuses the same "resolution is device pixels over a frame's covered
  composition region" relationship, over primitive values (D-resolution-3).
- **`editor.cells.insert_schema`** (`tasks/refinements/editor.cells/insert_schema.md`,
  Done 2026-07-28) — resolution is a first-class, user-specified, always-editable
  insert input owned by the kind's own schema (raster advertises `width`/`height`
  Integer fields, default 1024; D-insert_schema-6). This leaf provides the "always
  visible in the inspector thereafter" half of `docs/00-design.md:116-119` that
  insert_schema left to the inspector.

**Pending (owned here):** the generic provenance classification on `scene::Cell`,
the pure `interact` resolution-health helper, and the first-cut cell-resolution
inspector readout. Nothing downstream is blocked on an unwritten predecessor; the
one out-of-scope half (the resample mutation) is gated on a cross-repo library verb,
not on any editor leaf (Open questions).

## What this task is

Make a cell's **native/working resolution legible and its resolution-health
computable**, on the project selection, in the Inspector — the D8/§2 "resolution
health is computable, not a vibe." Concretely:

1. **L1 `scene`** — classify each cell's pixel provenance from **generic**
   `arbc::Content` signals, never its kind id. In `src/scene/ace/scene/cell.hpp`
   + `src/scene/cell.cpp`, `scene::Cell` gains a `CellDetail` describing whether the
   cell has a native pixel floor and where its bytes come from, derived during the
   same pinned `cells()` walk that already fills `content_bounds`:
   - `enum class DetailSource { PaintedRaster, ReferencedImage, ResolutionIndependent };`
     — `PaintedRaster` when `content->editable() != nullptr` (owns a mutable grid,
     D11 editable axis); else `ReferencedImage` when `!content->external_asset_ref().empty()`
     (borrowed external file, D11 bytes-where axis); else `ResolutionIndependent`
     (a solid/procedural/nested cell — no detail floor).
   - The **native pixel resolution** is the `content_bounds` dimensions for the first
     two (a raster/image whose content-space extent *is* its pixel grid); `nullopt`
     for `ResolutionIndependent` and for an unavailable image (empty bounds).
2. **L1 `interact`** — `interact::resolution_health(...)`, a pure helper beside the
   shot/frame math in `src/interact/ace/interact/interact.hpp` +
   `interact.cpp`. Given a cell's native pixel size `(nw, nh)`, its placement
   `arbc::Affine`, and a camera's `Resolution` + frame `arbc::Affine`, it returns the
   **sampling ratio** `r` (camera output pixels per composition unit over the cell's
   placed region, divided by the cell's native pixels per composition unit) and a
   verdict (`Crisp` when `r ≤ 1`, `Soft` when `r > 1` — the camera magnifies above
   native, "hero camera samples the photo at 1.4× — slightly soft",
   `docs/00-design.md:83-86`). It takes **primitive** values only (no `scene`/`camera`
   type), so it stays on the pure `interact→scene`-free policy side (D-resolution-3).
3. **L3 `views` / L4 `app`** — extend the `ViewType::Inspector` body
   (`app::CameraInspector`, renamed to reflect it now serves both objects, or a
   sibling `CellInspector` composed beside it) to show, for a selected **cell**: its
   **native resolution** (`nw × nh px`) separately from its **placed size**
   (`placement`-mapped `content_bounds`, in composition units, the D7/§6:234 "placed
   size in composition units"); a **health badge** per camera that captures it
   (worst-case ratio surfaced first); and the D8 provenance line —
   **"source-limited — no crisper detail exists"** for a `ReferencedImage`
   (`docs/00-design.md:242`, a terminal, fully-true statement) or the **detail-floor
   read** for a `PaintedRaster` (`"soft — sampled at 1.4× · detail floor 2048²"`).
   For a selected **camera**, it adds the per-camera health read of the cells it
   captures (the read `editor.cameras.manip` deferred here).

It deliberately does **not** ship: the resample-to-crisp **mutation** that grows a
raster's working grid (no libarbc verb exists — D-resolution-5, Open questions); a
dense multi-property Inspector sheet (`editor.panels.inspector`, `.tji:564`); the
Layers-list / Overview health chips (`editor.panels.layers`/`overview`); the brush
detail-floor readout (`editor.paint.paint_res`, `.tji:605`, which computes the same
health against the *brush* rather than a camera).

## Why it needs to be done

D8 (`docs/00-design.md:475`) and its backing §6 prose (`:237-244`) are the
constitution for the cell side of "placement is not resampling," and only the
*camera* half is implemented: `editor.cameras.manip` shipped the frame-vs-resolution
split and a first-cut camera resolution editor, and explicitly left the
resolution-**health** readout — the payoff that makes the split legible — to this
leaf (`manip.md:333-334,501-502`). Without it, a user scaling a cell up sees it get
soft with **no cue that it is soft, why, or what the fix is** — the exact failure the
"health badge" exists to prevent (`docs/00-design.md:239-244`). §2 states the
promise precisely — resolution health is *computable* for any camera over any cell
(`:83-86`) — and every input is already in hand: the cell's native detail
(`content_bounds`), its placement, and the camera's resolution + frame
(`scene::cameras`). This leaf turns that computable quantity into a readout, and
surfaces the D11 provenance (painted vs referenced) that decides whether the softness
is fixable (resample) or terminal (source-limited).

It is also the "always visible/editable in the inspector thereafter" half of the
insert-time resolution contract (`docs/00-design.md:116-119`) that
`editor.cells.model`/`editor.cells.insert_schema` left to the inspector.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **D8 — Cell scale ≠ resample** (`docs/00-design.md:475`, verbatim): *"Handle-drag
  changes **placement (affine)**, never resolution — non-destructive. Corners
  proportional-by-default (Shift free), edges 1D. Resampling is a separate explicit
  act: raster grows its grid; a referenced image is source-limited."* The backing §6
  prose (`:237-244`): *"Dragging handles changes the affine … it **never** touches
  stored pixels. Scaling a cell up is non-destructive — it just gets softer per unit
  area, and the **health badge** flags it and offers the explicit fix: **'resample to
  crisp'** for a painted raster (grow its working grid), or **'source-limited — no
  crisper detail exists'** for a referenced image (the file is the floor, nothing to
  resample to)."* The inspector spec (`:234-235`): *"position, placed **size in
  composition units**, rotation, (shear) — and separately the cell's **native
  resolution** with a resample control."*
- **§2 — Resolution independence, stated precisely** (`:76-86`): cells provide detail
  at their **own fixed native resolution**; cameras sample it at their output
  resolution; the composition has **no resolution**. Therefore *"**Resolution health**
  is … computable, not a vibe: for any camera, compare its pixel density over a cell's
  region against that cell's native detail ('hero camera samples the photo at 1.4× —
  slightly soft')."* This is the exact formula the `interact` helper implements.
- **D7 — Manipulation model** (`:474`): cells and cameras share one shape (affine
  placement + a resolution number) and one select tool; *"drag the extent, type the
  resolution; the two are always independent."* The health readout reads both the
  extent (placement) and the resolution (native px) and never conflates them.
- **D11 — Two asset axes** (`:478`): every brought-in pixel is classified by
  **editable?** (painted raster vs referenced image) and **bytes where?** (owned vs
  borrowed). These are the two generic axes the provenance classification reads —
  `editable()` and `external_asset_ref()` — so the classification is D11's, not a new
  taxonomy.
- **D4 — Paint storage** (`:471`) / **§4** (`:110-128`): cell-owned fixed resolution,
  a detail floor, resample-up as the escape hatch — the same detail-floor concept the
  health readout surfaces (the brush-side readout is `editor.paint.paint_res`).
- **D5 — Brush size** (`:472`, §4 `:148-153`): the `≈ 48 px on ‹retouch›` readout
  "doubles as the resolution-health cue" — the sibling readout against the brush;
  this leaf owns the camera-vs-cell readout, `editor.paint.paint_res` the brush one.
- **D9 — Camera frame ≠ resolution** (`:476`, §6 `:246-255`): the camera inspector
  carries *"a per-camera resolution-health read of the cells it captures"* — the read
  `editor.cameras.manip` deferred to this leaf.

**Governing architecture rows:**

- **A16** (`docs/01-architecture.md:431`) — Registry-driven cell insert with **no
  kind allowlist**; content minted only via `registry.factory(id)`. The load-bearing
  constraint for *this* leaf: the resolution readout and provenance classification
  must **not** switch on a concrete `kind_id`; they read generic `arbc::Content`
  virtuals (`editable()`, `external_asset_ref()`, `bounds()`) so a future plugin kind
  is classified automatically (D-resolution-2).
- **A17** (`:432`) — hit-testing split into an `interact` primitive policy core plus
  the one `interact→scene` adapter `pick_targets`, merging cells and cameras into one
  z-ordered target list; the shape the readout iterates.
- **A14** (`:305`) — a camera is a `Content` + a `Layer` (kind `org.arbc.camera`), so
  its `Resolution` is a first-class modeled value (`camera.hpp:26-31,91,127`) the
  health comparison reads.
- **§8** — the levelization DAG (`:308-344`; `scripts/check_levels.py:21-40`): the
  relevant edges are `scene → {base, project}`, `interact → {base, scene}`,
  `views → {scene, interact, commands, render, dockmodel, imgui}`; the L1 core takes
  no ImGui/GL/SDL include.
- **§9** — the universal DoD (`:346-373`) this leaf's Acceptance criteria instantiate.

**libarbc API surface** (fetched at tag `v0.4.0`, `CMakeLists.txt:25`; headers under
`build/dev/_deps/arbc-src/src/`):

- **Generic content signals (all pre-v0.4.0, no new pin):**
  `arbc::Content::bounds() → std::optional<Rect>`
  (`src/contract/arbc/contract/content.hpp:515`) — content-space extent, whose
  dimensions are the native pixel grid for raster/image; `Content::editable() →
  Editable*` (`content.hpp:609`, default `nullptr`) — non-null ⇒ owns a mutable grid
  (D11 editable axis); `Content::external_asset_ref() → std::string_view`
  (`content.hpp:672`, default empty) — non-empty ⇒ borrowed external file (D11 bytes
  axis). All three are read off the resolved `Content*` (`document.resolve(layer→content)`,
  `src/scene/cell.cpp:280-282`).
- **What confirms the native grid is the extent, and that there is no resize verb:**
  `org.arbc.raster` — `arbc::RasterContent`
  (`src/kind_raster/arbc/kind_raster/raster_content.hpp:297-362`): `bounds()`
  (`:310`) is `Rect{0,0,w,h}` in pixels; native grid is also readable via
  `store().base_table()->width()/height()` (`:345`, `TileTable::width/height`
  `:156-157`); mutation is **`paint` only** (`:339-341`) within the fixed grid; the
  grid size is set only at construction (`RasterContent(DecodedImage)` `:299`,
  `from_tiles` `:304`, `RasterStore::build_from_tiles` `:222`). **There is no
  `resample` / `set_resolution` / grow-the-grid verb** — unlike `CameraContent`,
  which has `set_camera_resolution` (`camera.hpp:177`). This is the gap D-resolution-5
  defers.
- `org.arbc.image` — `arbc::image::ImageContent`
  (`plugins/image/arbc/kind_image/image_content.hpp:344`): stores **no intrinsic
  size** (`:36-41`); `bounds()` (`:382`) reports the decoded master extent (empty when
  unavailable, no decode, no lock); `editable()` is **not** overridden (`:411`, so
  the generic default `nullptr` classifies it `ReferencedImage`); `external_asset_ref()`
  is overridden (`:412`). The file is the floor: no in-document size to grow.
- **Camera side:** `scene::Resolution` (`camera.hpp:26-31`), `CameraContent::resolution()`
  (`camera.hpp:91`), `scene::cameras` (`camera.hpp:134`); the comp↔device basis
  `interact::viewport_camera_for_shot` (`src/interact/ace/interact/interact.hpp:104-118`).
- The pin guard `tests/arbc_pin_test.cpp` needs **no change**: this leaf consumes only
  pre-v0.4.0 generic virtuals and pins no new library surface.

**Editor seams this leaf extends:**

- `scene::Cell{id, layer, kind_id, placement, content_bounds}`
  (`src/scene/ace/scene/cell.hpp:162-175`); `scene::cells`
  (`cell.hpp:185`, impl `src/scene/cell.cpp:244-289`, extent filled at `:280-282`).
- The `interact` math home: `src/interact/ace/interact/interact.hpp` (namespace at
  `:8`; the shot/frame/look-through helpers this leaf's health helper sits beside).
- The Inspector body seam: `app::CameraInspector`
  (`src/app/ace/app/camera_inspector.hpp:25-31`; its header at `:16-24` names the
  per-camera resolution-health readout as a scheduled sibling leaf's scope), wired via
  `ace::views::register_view_body(ViewType::Inspector, …)` at
  `src/app/shell.cpp:360-370`, cleared at teardown; the `dockmodel::ViewType::Inspector`
  slot (`src/dockmodel/ace/dockmodel/view_registry.hpp:19`).
- The selection read: `commands::AppState::selection()`
  (`src/commands/ace/commands/app_state.hpp:53-54`); `CanvasView` for the active
  viewport camera the readout compares against (`app::CameraInspector` already holds a
  `CanvasView&`, `camera_inspector.hpp:27`).

**Predecessor / sibling refinements:** `tasks/refinements/editor.cells/selection.md`,
`tasks/refinements/editor/manip.md`, `tasks/refinements/editor/look_through.md`,
`tasks/refinements/editor.cells/insert_schema.md`.

**Test rigs:** `ace_tests` (Catch2, headless; `tests/cell_model_test.cpp` is the
sibling to extend for the scene classification, `CMakeLists.txt` `ace_tests` source
list ~`:219-234`); a new `tests/resolution_test.cpp` for the `interact` health math;
`ace_shell_test` (ImGui Test Engine, offscreen software-GL, source list ~`:249-261`;
patterns `tests/cells_insert_e2e_test.cpp` + `tests/writer_session.hpp`, driven by
widget id via `IM_REGISTER_TEST` + `ctx->ItemClick`/`ctx->Yield`, state read through a
per-test `E2EState`); goldens/screenshots under `tests/goldens/*` via
`tests/golden_support.hpp`; `asan`/`tsan` presets, residual Mesa leaks via
`tests/lsan.supp`; coverage `diff-cover --fail-under=90`.

## Constraints / requirements

1. **Levelization (`check_levels` clean) — the primary structural assertion.** The
   provenance classification lands in **L1 `scene`** (reads generic `arbc::Content`
   virtuals on the already-resolved `Content*`; no new edge — `scene→{base,project}`
   and the existing `arbc` include suffice). The health math lands in **L1 `interact`**
   as a helper taking **primitive** `int` sizes + `arbc::Affine` + `arbc::Resolution`-
   free `(int,int)` — never a `scene::Cell`/`scene::Camera`, so the `interact→scene`
   edge is not exercised and no new edge appears (the look_through/manip
   primitive-values discipline). The inspector readout is **L3 `views`** / **L4 `app`**
   (both already see ImGui). **No new component, no new DAG edge, no `check_levels`
   edit**; the L1 core gains no ImGui/GL/SDL include.

2. **No kind allowlist — provenance is read from generic `arbc::Content` signals, never
   a `kind_id` string-switch (A16 / D-cells_model-8).** `DetailSource` is derived from
   `editable()` (`content.hpp:609`) and `external_asset_ref()` (`content.hpp:672`) —
   D11's two axes — so a future plugin kind that registers on the `Registry` is
   classified automatically (an editable kind ⇒ resamplable; an external-ref kind ⇒
   source-limited; otherwise resolution-independent). The one `kind_id` comparison the
   existing `cells()` walk makes (excluding `org.arbc.camera`, `cell.cpp:271-273`,
   A14's cells/cameras split) is unchanged; this leaf adds **no** new one.

3. **Native resolution and placed size are always shown as distinct numbers (D7/D8).**
   The readout shows native pixels (`content_bounds` dims, for raster/image) and the
   placed size in composition units (`placement`-mapped `content_bounds`) as two
   separate quantities; it never derives one from the other and never presents the
   placement scale as a resolution (the D8 "the two are always independent" rule).

4. **Resolution health is the computable §2 quantity, pure and camera-relative.** The
   `interact` helper returns the sampling ratio `r` = camera pixels-per-composition-
   unit over the cell's placed region ÷ the cell's native pixels-per-composition-unit,
   and a verdict (`Crisp` for `r ≤ 1`, `Soft` for `r > 1`). It is a pure function of
   its primitive inputs — no `Document`, no I/O, no frame state — so it is byte-
   deterministic and headless-testable. A `ResolutionIndependent` cell (no native
   floor) and an unavailable image (empty bounds) report **no** health verdict
   (health N/A), never a false "soft."

5. **This leaf ships NO grid-growing mutation.** The "resample to crisp" *action*
   (grow a raster's working grid, `docs/00-design.md:241`) has **no libarbc verb**
   (`raster_content.hpp` exposes construction + `paint` only; no resize) and cannot be
   done editor-side without naming the concrete `RasterContent` type (violating A16's
   no-allowlist) and owning kind-specific upsampling (violating levelization —
   `arbc::media`'s resampler is the kind's floor, not the editor's). The readout
   **states** the health and the provenance (a `ReferencedImage` is terminally
   "source-limited — no crisper detail exists", a fully-true shippable statement; a
   soft `PaintedRaster` shows its detail-floor read), but ships **no** resample button
   — no dead UI. The mutation is deferred to `editor.cells.resample_apply` behind a
   cross-repo library verb (Open questions). Because no grid grows here, the
   `bounds()`-read/write-pair `selection.md` Open question 2 anticipated does **not**
   arise (Constraint honoured, not introduced).

6. **The readout is a pure read over the live pinned snapshot; it opens no
   transaction and holds no lock beyond the frame read.** It reads `scene::cells`,
   `scene::cameras`, and the current `AppState::selection()` fresh each frame (as
   every canvas already does), never a UI-side snapshot cached across frames. A cell
   whose selection id has no live target simply shows nothing (the selection's own
   per-frame `prune` handles the stale id, D-selection-7).

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate`
green (check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.** No new
  component, no new DAG edge, no lint edit; `scene` gains no UI include and no new
  `arbc` need beyond the generic virtuals it already calls; `interact`'s health helper
  takes primitive `(int,int)` + `arbc::Affine` and reaches no `scene` type; nothing in
  the L1 core includes ImGui/GL/SDL. Confirm `scripts/gate`'s level lint passes and no
  entry in `scripts/check_levels.py:21-40` changes.
- **L1 logic — Catch2 units:**
  - **Provenance classification** (extend `tests/cell_model_test.cpp`, which already
    inserts raster/image/solid/nested cells and reads `cells()` back in z-order,
    `:357-401`): a `PaintedRaster` (an editable `org.arbc.raster`) classifies
    `PaintedRaster` with native px = its `content_bounds` dims; a referenced
    `org.arbc.image` classifies `ReferencedImage`; a factory-built `org.arbc.solid`
    and an `org.arbc.nested` classify `ResolutionIndependent` with native px =
    `nullopt`; an **unknown/plugin editable kind** classifies `PaintedRaster` (the
    generic `editable()` path, pinning the no-allowlist property — no `kind_id`
    switch). The classification is read off the same pinned `cells()` walk that fills
    `content_bounds`.
  - **Health math** (`tests/resolution_test.cpp`, new file in `ace_tests`;
    `TEST_CASE("resolution: …")`):
    - **Sampling ratio is camera-relative and correct:** a camera whose output
      samples a cell's placed region at exactly native density returns `r == 1.0`
      (`Crisp`); at 2× density returns `r == 2.0` (`Soft`); the design's `1.4×`
      example is reproduced from `(native, placement, camera resolution, frame)` and
      lands `Soft`. Use integer scales for byte-exact `r`; a documented tolerance only
      where a rotation makes `r` irrational.
    - **Independence (D8):** changing the cell's placement scale changes `r`
      (placement is the sampling density) while the native px input is untouched;
      changing native px changes `r` while placement is untouched — the helper never
      conflates them.
    - **N/A cases:** `native px == nullopt` (resolution-independent) and a degenerate
      (non-invertible placement/frame, non-positive size) yield **no** verdict, never
      a div-by-zero or a false `Soft`.
    - **Placed-size derivation:** the placed size the readout shows equals
      `placement`-mapped `content_bounds` (a pure `Affine::map_rect`), distinct from
      native px.
- **Rendered output — golden.** **No new composition golden** (this leaf composites
  nothing — it reads and computes; justified exception per §9). A **screenshot
  baseline** of the Inspector badge (`tests/goldens/resolution_badge_*.png` via the
  e2e capture, `tests/golden_support.hpp`) is added where it adds signal — pinning
  that a soft raster, a source-limited image, and a resolution-independent solid each
  render their distinct badge text.
- **UI e2e — ImGui Test Engine** (`tests/resolution_e2e_test.cpp`, in `ace_shell_test`,
  modelled on `tests/cells_insert_e2e_test.cpp` + `tests/writer_session.hpp`,
  offscreen software-GL, driven by widget id, state read through `E2EState`):
  - seed a document with a raster cell and a shot camera; select the raster; assert
    the Inspector shows its native resolution and placed size as **two distinct**
    values, and a health badge whose verdict matches `interact::resolution_health`
    for the capturing camera.
  - scale the raster's placement up (via the shipped gizmo/`set_layer_transform`) so
    the camera out-resolves it; assert the badge flips to **Soft** with the ratio, and
    the raster's **native resolution is unchanged** (placement is not resample, D8).
  - select a referenced `org.arbc.image` cell; assert the badge reads **"source-limited
    — no crisper detail exists"** and shows **no** resample affordance (D8 / D-resolution-5).
  - select a solid; assert the readout reports **resolution-independent** (no health
    verdict).
  - select a **camera**; assert the per-camera health read of the cells it captures
    appears (the read `editor.cameras.manip` deferred here).
- **Threading — ASan/TSan** (one case appended to `tests/canvas_host_test.cpp`'s
  real-pool sanitizer suite, joining the existing `bounds()`-read anchor
  `selection.md` established): the UI thread reads `scene::cells` (`content_bounds` +
  the new `editable()`/`external_asset_ref()` classification) and `scene::cameras`
  while the render thread `drive_once`s the same document over the lock-free `pin()`.
  Must be data-race-clean: the readout adds **no shared mutable state** and **no
  mutation** — `editable()`/`external_asset_ref()`/`bounds()` are const reads of
  construction-stable content state (this leaf grows no grid, so `bounds()` stays
  read-only, Constraint 5). Residual Mesa leaks via `tests/lsan.supp`.
- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed lines;
  clang-format + build clean across presets. Tests ship with the task.

**Deferred WBS work.** **None as a WBS leaf.** The resample-to-crisp **mutation** is
gated on a cross-repo libarbc verb that does not exist yet, so it is **not**
agent-implementable today and is registered in `tasks/parking-lot.md` with a trigger
(closer records under the M-editor milestone's watch), not as an orphan WBS leaf
(Open questions). Every other out-of-scope surface already has a scheduled owner: the
dense property sheet is `editor.panels.inspector` (`.tji:564`), the Layers/Overview
health chips are `editor.panels.layers`/`overview` (`.tji:570`/`:576`), and the
brush-relative detail-floor readout is `editor.paint.paint_res` (`.tji:605`) — none is
new work.

## Decisions

- **D-resolution-1 — A cell's native pixel resolution is its `content_bounds`
  dimensions (for a raster/image); the editor stores no separate resolution number on
  `scene::Cell`.** For `org.arbc.raster` and `org.arbc.image`, the content-space
  extent `arbc::Content::bounds()` reports (`content.hpp:515`; raster `Rect{0,0,w,h}`
  in pixels, `raster_content.hpp:310`; image the decoded master extent,
  `image_content.hpp:382`) numerically **is** the native pixel grid — 1 native pixel =
  1 content unit before placement. So the field `editor.cells.selection` already added
  (`Cell::content_bounds`, D-selection-11) carries it; a resolution-specific field
  would duplicate one number and drift from it. *Rationale:* the design states the
  composition has no resolution and detail lives at the cell's native grid
  (`docs/00-design.md:78-81`); `content_bounds` is that grid for the two finite-detail
  kinds, and reusing it keeps the two consumers (placed-size and native-resolution)
  reading one pinned value (D-selection-11's stated purpose). *Alternative rejected:*
  add a typed `Resolution` to `scene::Cell` populated by reaching into
  `RasterContent::store().base_table()->width()/height()` (`raster_content.hpp:345,156`)
  — it requires naming the concrete raster type (A16 allowlist), gives the same number
  `bounds()` already reports, and has no answer for image (which stores no size). **No
  doc delta required.**

- **D-resolution-2 — Pixel provenance is classified from the generic `arbc::Content`
  facets `editable()` and `external_asset_ref()` — D11's two axes — never from a
  `kind_id` string-switch.** `PaintedRaster` ⇐ `editable() != nullptr`;
  `ReferencedImage` ⇐ else `!external_asset_ref().empty()`; `ResolutionIndependent` ⇐
  otherwise. *Rationale:* A16 / D-cells_model-8 forbid hard-coded kind discrimination
  (remove.md D-cells_remove-1 restates it verbatim); D11 (`docs/00-design.md:478`)
  defines exactly these two axes — editable? and bytes-where? — and libarbc exposes
  each as a generic virtual (`editable()` `content.hpp:609`, non-null for a raster's
  mutable grid; `external_asset_ref()` `content.hpp:672`, non-empty for an image's
  borrowed file). Reading them classifies any plugin kind automatically: a future
  editable kind is resamplable, a future external-ref kind is source-limited. It also
  makes the D8 fork ("resample for a painted raster / source-limited for a referenced
  image") fall out of the classification rather than a kind table. *Alternative
  rejected:* `if (kind_id == "org.arbc.raster")` — the allowlist A16 forbids, and it
  would misclassify any plugin raster-like kind and any future editable kind. **No doc
  delta required** (a faithful implementation of D11, not a new decision).

- **D-resolution-3 — The health math is a pure L1 `interact` helper over primitive
  values, beside the shot/frame helpers; `views` supplies the `scene`/`camera` data.**
  `interact::resolution_health` takes `(int native_w, int native_h, const arbc::Affine&
  placement, int cam_w, int cam_h, const arbc::Affine& cam_frame)` and returns
  `{double ratio; Verdict verdict;}` — no `scene::Cell`/`scene::Camera`. *Rationale:*
  A17/§8 make `interact` the pure-math home and take primitive values (as
  look_through/manip did, D-manip-3) so it stays `scene`-decoupled and headless-
  testable — the bulk of the coverage; the §2 health quantity is exactly this kind of
  affine/resolution math. *Alternative rejected:* compute it in `views` (L3) — untestable
  without ImGui/GL and against the "L1 logic is the bulk" DoD; *or* thread a
  `scene::Cell`+`scene::Camera` into `interact` — needless coupling when only four
  primitive geometry inputs matter. **No doc delta required** (a new function in an
  existing L1 component).

- **D-resolution-4 — Softness is `ratio > 1` (any magnification above native), with
  the ratio surfaced so the user judges severity; there is no invented tolerance
  band.** `Crisp` for `r ≤ 1`, `Soft` for `r > 1`. *Rationale:* D8/§2 make the
  detail floor exactly the native grid — a camera sampling above native is
  reconstructing detail the cell does not hold ("softer per unit area",
  `docs/00-design.md:239-244`); `r == 1.4` is the design's own "slightly soft"
  example (`:85`). Showing the number lets the user decide whether 1.05× matters,
  which a hidden threshold would pre-judge. *Alternative rejected:* a
  `Soft`/`Very-soft` two-band threshold (e.g. `r > 1.5`) — an invented rule with no
  design basis; the raw ratio is more honest and testable. **No doc delta required.**

- **D-resolution-5 — This leaf ships the resolution DISPLAY and HEALTH BADGE now, and
  DEFERS the resample-to-crisp MUTATION to a cross-repo libarbc verb; it ships no dead
  resample button.** *Rationale:* the mutation has no primitive — `RasterContent`
  exposes construction + `paint` only, no resize (`raster_content.hpp:297-362`, vs
  `CameraContent::set_camera_resolution` `camera.hpp:177`) — and it is not
  editor-implementable without (a) naming the concrete `RasterContent` type, which A16
  forbids, and (b) owning kind-specific upsampling, which levelization forbids
  (`arbc::media`'s resampler `image_resampler.hpp` is the kind's floor, not the
  editor's). This is the same shape `editor.cells.model`/`remove` faced with atomic
  create/batch-remove: the editor waited for the library verb
  (`create_content_and_attach`/`remove_contents`, arbc#20) rather than re-implement it
  host-side. The *display* and *health* halves are fully shippable now and carry D8's
  user-facing value (the badge that flags softness and states the fix's availability);
  a `ReferencedImage`'s "source-limited — no crisper detail exists" is a terminal,
  fully-true statement that needs no mutation at all. Shipping a button that does
  nothing would be dead UI. *Alternative rejected:* implement resample by
  delete-cell + insert-new-resampled-cell — it mints a new `ObjectId`, drops paint
  history, and is a *replace*, not the non-destructive grid-grow D8 specifies.
  *Alternative rejected:* implement it host-side via `RasterStore::build_from_tiles`
  reading `TileTable::level_pixels` and `arbc::media` upsampling — names the concrete
  kind (A16) and pulls kind-owned resampling into the editor (levelization). **No doc
  delta required** (D8 stands as the target; the deferral is a sequencing item in
  `tasks/parking-lot.md`, not a design amendment).

- **D-resolution-6 — The readout is a first-cut extension of the existing
  `ViewType::Inspector` body, not the dense property sheet.** It extends
  `app::CameraInspector` (`camera_inspector.hpp:25`, wired `shell.cpp:360-370`) — the
  first-cut body `editor.cameras.manip` shipped — to serve a selected cell (native/
  placed size + health) as well as a camera (per-camera health of captured cells).
  *Rationale:* `manip.md` D-manip-6 already established this seam and explicitly
  deferred the health readout here (`manip.md:333-334,501-502`); the dense
  multi-property sheet remains `editor.panels.inspector` (`.tji:564`), which will
  compose this readout. Keeps the leaf self-contained under its `!selection`-only
  dependency. *Alternative rejected:* block on `editor.panels.inspector` — it is a
  later `depends editor.cells.selection` leaf, and the health readout is exactly the
  first-cut content the Inspector body is for. **No doc delta required** (a body on the
  existing `ViewType::Inspector`, no new view type).

## Open questions

(none — all decided.)

One item is routed to `tasks/parking-lot.md` for the human-review queue rather than
the WBS (it is a cross-repo upstream-issue candidate with a concrete trigger, not an
"audit" task and not agent-implementable today):

1. **libarbc has no content-resample verb — "resample to crisp" (grow a raster's
   working grid) cannot be built host-side.** `arbc::RasterContent` exposes
   construction + `paint` only (`raster_content.hpp:297-362`); there is no in-place
   resize, and doing it editor-side would require naming the concrete kind (A16
   no-allowlist) and owning kind-specific upsampling (levelization). The clean fix is
   an **upstream generic content-resample facet** — mirroring `arbc::Editable`, a
   `Resampleable` a kind opts into and the host discovers via a `Content` virtual
   (a referenced `org.arbc.image` returns none → "source-limited"; a raster resamples
   to a new `set_content_state` version) — so a host offers "resample to crisp"
   without naming the kind. **Human action:** file an upstream issue against
   `ruoso/arbitrarycomposer` for that facet/verb. **When the trigger fires** (a
   libarbc release adding it): bump the pin, then implement the editor-consumer leaf
   **`editor.cells.resample_apply`** (~1.5d) — wire a "resample to crisp" action in the
   Inspector through a `commands::Command` + the new verb (one journal entry,
   undoable, `ObjectId` preserved), gated on `DetailSource::PaintedRaster`, with the
   `bounds()`-read/write TSan case `selection.md` Open question 2 anticipated. That
   consumer is genuine agent-implementable work *once the API exists*, so it becomes a
   WBS leaf then, not now.

## Status

**Done** — 2026-07-28.

- `src/scene/ace/scene/cell.hpp` + `src/scene/cell.cpp` — added `DetailSource` enum and `CellDetail` struct; provenance classified from `editable()`/`external_asset_ref()` generics (no `kind_id` switch, A16); native-px derived from `content_bounds` dims in the pinned `cells()` walk.
- `src/interact/ace/interact/interact.hpp` + `src/interact/interact.cpp` — `interact::resolution_health(native_w, native_h, placement, cam_w, cam_h, cam_frame)` pure helper returning `{ratio, Verdict}` (`Crisp` / `Soft`); no `scene` types, no I/O, byte-deterministic.
- `src/app/ace/app/camera_inspector.cpp` — extended `ViewType::Inspector` body to show selected-cell native vs placed size, provenance line ("source-limited" / detail-floor), and per-camera health badge; also wired the per-camera capture-health read deferred by `editor.cameras.manip`.
- `tests/resolution_test.cpp` — new Catch2 L1 unit file; `resolution:*` cases covering sampling ratio correctness, independence of native px from placement (D8), N/A for `ResolutionIndependent` / degenerate geometry.
- `tests/resolution_e2e_test.cpp` — new ImGui Test Engine e2e covering raster Crisp→Soft on scale-up with native unchanged, source-limited image badge, resolution-independent solid, and per-camera capture-health.
- `tests/cell_model_test.cpp` — provenance-classification units (`cells() classifies…`) and probe-facet units; two test-local `Content` doubles.
- `tests/canvas_host_test.cpp` — appended real-pool TSan/ASan case asserting UI-thread `editable()`/`external_asset_ref()`/`bounds()` reads are data-race-clean under the lock-free `pin()`.
- `CMakeLists.txt` — registered `tests/resolution_test.cpp` and `tests/resolution_e2e_test.cpp` in their respective test targets.
- Resample-to-crisp mutation not shipped (no libarbc verb; D-resolution-5); "source-limited — no crisper detail exists" is the terminal shippable statement. Parking-lot entry added for the cross-repo upstream verb.
- Screenshot golden deferred per refinement §9 justified exception (the e2e asserts badge correctness through `scene`/`interact` seams, not software-GL byte equality).
