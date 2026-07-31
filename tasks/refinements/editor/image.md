# editor.import.image — Drop / place an image → borrowed cell (1:1 px→units)

## TaskJuggler entry

- **Task:** `editor.import.image` (`tasks/00-editor.tji:702-706`, under
  `task import "Import & assets"` at `:701`).
- **Effort:** `2d` (`:703`) · `allocate team` (`:704`).
- **Depends:** `editor.cells.model` (`:705`) — the Registry-driven cell model,
  **Done** 2026-07-22 (`tasks/refinements/editor.cells/model.md` Status). The
  dependency is satisfied.
- **Note (`.tji:706`):** "Drop or Place an image file → a borrowed (referenced)
  read-only `org.arbc.image` cell at the drop point, sized native px →
  composition units 1:1 so pixel dimensions carry real relative scale. Design:
  D11/D12."
- **Back-link:** the `.tji` note currently ends `Refinement:
  tasks/refinements/import_image.md` (the flat interim path). This refinement
  lands at **`tasks/refinements/editor/image.md`** per the orchestrator's
  area = first-dot-segment (`editor`) assignment
  (`tasks/refinements/README.md:9-18`); the closer updates the note back-link to
  the real path and adds `complete 100` after `allocate team`
  (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji` here.
- **Downstream:** `editor.import.image` is the direct predecessor of the whole
  rest of the import stream — `editor.import.paste` (`:708-712`,
  `depends !image`), `editor.import.nested` (`:714-718`, `depends !image`) and,
  transitively, `editor.import.consolidate` (`:720-724`) — and of the
  non-destructive-stack leaf `editor.paint.retouch_stack` (`:692-696`,
  `depends editor.import.image`), which offers "add a retouch layer above" a
  read-only photo. It is the first realization of the D11 **borrowed + read-only**
  corner; it feeds the `editor.import` gather, which the final `milestones`
  milestone depends on (`tasks/99-milestones.tji:8`).

## Effort estimate

**2 days.** Most of the borrowed-image *model* already exists — the generic
`external_asset_ref()` classifier, the `native_pixels` grid, the Inspector /
Layers / resolution readouts, and the whole `add_cell` → `insert_cell_command`
→ `run_edit` insert seam all shipped with `editor.cells.model` and its
downstream leaves. The greenfield is the *import gesture* and the one thing that
distinguishes an image from a schema-inserted cell: it comes from a file and is
placed at native scale.

- **Register the kind** (link `arbc-plugin-image-impl`, one line in
  `register_editor_kinds`; A29 doc delta). ~0.2d.
- **`interact::place_at_native_scale`** — the pure L1 1:1 native-px→units
  placement affine at the drop point (the seam `interact::place_in_view`'s note
  already reserves, `interact.hpp:143-146`). ~0.2d incl. Catch2.
- **The image-config helper** (L1) — read the file bytes, normalize the path to
  a borrowed URI, assemble the kind's `image_config` frame. ~0.2d incl. Catch2.
- **The two entry points** (L3/L4) — an OS file-drop (`SDL_EVENT_DROP_FILE`) and
  a "Place image…" affordance behind a native `FileDialog` seam mirroring the
  shipped `FolderDialog`; both funnel through **one** app import verb. ~0.6d.
- **Gateway wiring** — `insert_image(path, device_point)` reusing
  `insert_cell_command` through `run_edit` (the `AppProjectGateway::insert_cell`
  mould). ~0.2d.
- **Tests + threading scope** (Catch2 · one golden · e2e · TSan anchor). ~0.6d.

**One new build/link edge** (`ace_commands → arbc-plugin-image-impl`) recorded as
**A29**; **no new editor component, no new §8 DAG edge, no `check_levels` edit,
no libarbc fork, no pin bump** (the kind is a v0.4.1 archive already fetched
under `build/*/_deps/arbc-src/plugins/image/`). The pin stays at `v0.4.1`.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cells.model`** (`tasks/refinements/editor.cells/model.md`, Done
  2026-07-22) — the Registry-driven cell model this leaf builds directly on. It
  ships everything the *insert* needs; this leaf adds only the file→config and
  the native-scale placement:
  - **`scene::add_cell(document, registry, kind_id, config, placement, entered)
    -> arbc::expected<arbc::ObjectId, std::string>`** (`src/scene/ace/scene/cell.hpp:134-137`)
    — mints content **only** via `registry.factory(kind_id)` (never a concrete
    arbc type, A16) in ONE libarbc transaction (`create_content_and_attach`),
    returning the kind's own error string with the `Document` untouched on
    factory failure. **This is the verb an image import calls, unchanged**, with
    `kind_id = "org.arbc.image"`.
  - **`scene::probe_bounds(registry, kind_id, config)`**
    (`src/scene/ace/scene/cell.hpp:110-111`) — the content's extent a placement
    helper needs BEFORE `add_cell` mints anything (it builds a throwaway content
    and destroys it). For an image, the factory decodes the config's embedded
    bytes, so `probe_bounds` returns the photo's native pixel `Rect`.
  - **`commands::insert_cell_command(const arbc::Registry&, std::string kind_id,
    std::string config, ...)`** (`src/commands/ace/commands/cells.hpp:45`) — the
    `commands::Command` wrapper `dispatch`/`run_edit` runs; keeps `AppState`
    revision/dirty bookkeeping (A13). **Reused verbatim with the image kind — no
    new command.**
  - **`scene::CellDetail{ DetailSource source; std::optional<std::pair<int,int>>
    native_pixels; bool borrowed; }`** (`src/scene/ace/scene/cell.hpp:198-214`)
    and **`scene::classify_detail`** (`src/scene/cell.cpp:107-126`): a non-empty
    generic `external_asset_ref()` is classified `DetailSource::ReferencedImage`
    + `borrowed = true`, `native_pixels` filled from `bounds()`
    (`cell.cpp:114-118,126`), **never a `kind_id` switch** (A16 / D-resolution-2).
    So the moment this leaf mints a borrowed image cell, the Inspector's native
    resolution (`src/app/inspector_panel.cpp:62-64`), the Layers owned-vs-borrowed
    provenance and the resolution-health readouts all light up **for free** — no
    per-kind editor code.
  - **`interact::place_in_view(const arbc::Affine& view, int pane_w, int pane_h,
    ...)`** (`src/interact/ace/interact/interact.hpp:155`) — the *fill-fraction*
    placement the modal insert uses. Its note **explicitly reserves this leaf's
    seam**: "`editor.import.image` a native-px→units 1:1 affine, with no change
    to `scene`" (`interact.hpp:143-146`). This leaf writes the sibling helper,
    not a variant of `place_in_view`.
  - The **insert wiring end-to-end** to copy: `AppProjectGateway::insert_cell`
    (`src/app/project_gateway.cpp:311-351`) probes bounds (`:334-338`), computes
    a placement (`:340-341`), builds the command (`:345-347`) and runs it inside
    `run_edit` (`:350`, → `CanvasView::apply_edit`, the single-writer /
    `edit_render_sync` seam). A drop swaps `place_in_view` for the native-scale
    affine and reuses the rest.
- **`editor.foundation.app_shell`** (Done) — the SDL event pump
  (`src/app/shell.cpp:140-146`, today only `SDL_EVENT_QUIT` /
  `WINDOW_CLOSE_REQUESTED`, else forwarded to ImGui) this leaf extends with a
  `SDL_EVENT_DROP_FILE` arm; and the ImGui Test Engine e2e harness (`ace_shell_test`).
- **`editor.project.open_ui` / `open`** (Done) — the **`FolderDialog`** seam
  (`src/app/ace/app/folder_dialog.hpp`, abstract interface + SDL-backed
  `SdlFolderDialog` wrapping `SDL_ShowOpenFolderDialog`, with a scriptable fake
  the gateway tests inject; A12). This leaf mirrors it as a `FileDialog` /
  `SdlFileDialog` (`SDL_ShowOpenFileDialog`) for "Place image…".
- **`editor.project.reconstructing_reopen`** (Done, A19/A4.1a) — the reopen path
  installs an `arbc::FilesystemAssetSource` on the `LoadContext`
  (`src/project/project_open.cpp:316-318`), the resolver a borrowed image's URI
  re-fetches through on reopen once the image codec is present.

**Pending (owned here):** the kind registration (A29), the L1 native-scale
placement helper + image-config helper, the two UI entry points and their
`FileDialog` seam, and the gateway import verb + its tests. Nothing downstream
is blocked on an unwritten predecessor.

## What this task is

Turn a file the user brings in — by dragging it onto the canvas, or by a
"Place image…" affordance — into a **borrowed, read-only `org.arbc.image` cell**
placed at the drop point, **sized native px → composition units 1:1** so a
4000-px photo genuinely dwarfs a 200-px sketch (D12/§8). Concretely, four
pieces, only two of which are new L1 logic:

1. **Register `org.arbc.image`.** It ships out-of-lib and is excluded from
   `register_builtin_kinds`, so the editor's registry cannot factory it today.
   Static-link `arbc-plugin-image-impl` and add one `registry.add(...)` to
   `register_editor_kinds`, exactly as the camera kind is registered
   (A29 doc delta). This both enables `add_cell("org.arbc.image", …)` and gates
   the runtime serialize codec so borrowed images round-trip on save/reopen.

2. **Read the file → the kind's config frame** (L1). Read the dropped file's
   bytes, normalize its absolute path to a **borrowed** URI (external, not
   relativized — consolidate relativizes later, D13), and assemble the image
   kind's opaque `image_config(authored_uri, resolved_uri, encoded_bytes)`
   frame. The bytes are embedded so the image decodes and renders *immediately*
   at import; on save the codec writes only the URI (borrowed).

3. **Place at native scale** (L1). A pure `interact::place_at_native_scale`
   turns the drop's device point + the image's native-pixel bounds into a
   **unit-scale** `arbc::Affine` (1 native px = 1 composition unit) centered on
   the composition-space image of the drop point (device→comp via
   `camera.inverse()`). This is *not* `place_in_view` — fit-to-camera would
   discard the true relative scale between imports (D12/§8: "Sizing is always
   1:1 on drop").

4. **The two entry points → one import verb** (L3/L4). An `SDL_EVENT_DROP_FILE`
   arm in the shell delivers `(path, drop_point)`; a "Place image…" affordance
   beside the Insert Cell entry opens a native `FileDialog` and places at the
   view center. Both funnel into ONE testable app verb
   `insert_image(path, device_point)` that runs pieces 2+3 + `insert_cell_command`
   through `run_edit`.

The minted cell is **read-only by construction** — `org.arbc.image` overrides no
`editable()` facet — so attempting to paint it is what later triggers the
"add a retouch layer above" offer (`editor.paint.retouch_stack`, separate leaf).

**Not in scope, by WBS split:** a pasted bitmap with no source file → **owned**
read-only image (`editor.import.paste`, `depends !image`); placing another
`.arbc` → a nested composition cell (`editor.import.nested`); Consolidate
(borrowed→owned + relativize URIs) and the **missing/moved-borrow placeholder +
relink** affordance (`editor.import.consolidate`, D13); the retouch layer
(`editor.paint.retouch_stack`). This leaf ships the borrowed-import gesture and
the round-trip that keeps it alive; broken-borrow recovery is consolidate's.

## Why it needs to be done

D11 classifies every brought-in pixel on two axes — **editable?** and **bytes
where?** — and names the imported photo as the **borrowed + read-only** corner
(`docs/00-design.md:478`). D12 is the import spec: "Drop image file → borrowed
read-only cell (native px→comp units 1:1 at drop point)" (`:479`), and §8 spells
out *why 1:1*: "sized native px → composition units 1:1 so pixel dimensions
carry real relative scale … fit-to-camera would discard the true relative scale
between imports" (`docs/00-design.md` §8, Import paths). Today the editor can
insert every *built-in* kind through the modal but cannot bring in a photo at
all — `org.arbc.image` is not even registered, so a hypothetical image layer
round-trips as a placeholder. This leaf is the first of the three import paths
and the gate for the rest of the import stream and the retouch stack: without a
borrowed image cell there is nothing to paste-as-owned beside, nothing to
consolidate, and nothing to retouch. It is also the smallest possible addition
that unlocks all of that, because the model already understands a borrowed
external-ref cell generically — this leaf just *produces* one.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **D11 — Two asset axes** (`docs/00-design.md:478`, verbatim): *"Every
  brought-in pixel is classified by **editable?** (painted raster vs referenced
  image) and **bytes where?** (owned in `assets/` vs borrowed external file).
  Photo = borrowed+read-only; … The list surfaces both."* This leaf produces the
  photo = borrowed + read-only cell; `classify_detail` already surfaces both axes.
- **D12 — Import paths** (`:479`, verbatim): *"Drop image file → borrowed
  read-only cell (native px→comp units 1:1 at drop point); paste/clipboard →
  owned read-only; place `.arbc` → nested composition. …"* The first clause **is**
  this leaf's specification; the other two are its siblings.
- **§8 — Import & assets → Import paths** (drag-drop discussion): *"Drop / Place
  an image file → a **borrowed** read-only cell at the drop point, sized **native
  px → composition units 1:1** … the user rescales freely. Sizing is **always
  1:1** on drop — fit-to-camera would discard the true relative scale between
  imports."* And: read-only photos can't be painted; formats are "whatever the
  library decodes (PNG/JPG/… via imdec)"; **only borrowed pixels can break**
  (missing/moved → placeholder + relink, which is consolidate's).
- **D13 — Assets, GC & portability** (`:480`, verbatim): *"…borrowed files **and
  `workspace/`** never GC'd. 'Consolidate' copies borrows into `assets/` +
  relativizes URIs. Missing borrow → placeholder + relink."* This leaf keeps the
  import **borrowed** (URI external, un-relativized) and mints nothing in
  `assets/`; consolidate/relink is `editor.import.consolidate`.

**Governing architecture rows:**

- **A16** (`docs/01-architecture.md:433`) — cell insert is Registry-driven, no
  allowlist; content minted **only** via `registry.factory(id)`;
  `scene::add_cell(document, registry, kind_id, config, placement)` is the seam.
  A16 **names this leaf explicitly**: placement "arrives as a finished
  `arbc::Affine` … the seam `editor.panels.overview` later swaps a drag-derived
  affine into and **`editor.import.image` a native-px→units 1:1 affine, with no
  change to `scene`**." This leaf is A16's promised import realization: a config
  + a 1:1 affine through the existing seam.
- **A29** (this task's doc delta, `docs/01-architecture.md:446`) — the editor
  gains `org.arbc.image` by static-linking `arbc-plugin-image-impl` into
  `ace_commands` and registering its factory in `register_editor_kinds`, gating
  the runtime image codec so borrowed images round-trip; the decode dep stays
  PRIVATE to that archive. This is the one architectural fact this leaf adds.
- **A19 / A4.1a** (`docs/01-architecture.md`) — reopen reconstructs each
  `ContentRecord` through its registered codec, and the load path installs an
  `arbc::FilesystemAssetSource` on the `LoadContext` (`project_open.cpp:316-318`)
  that resolves the borrowed URI inline; so a registered image kind survives
  save→reopen with its pixels re-fetched from the external file.
- **A12** (`docs/01-architecture.md`) — the native-dialog seam (L3-declared,
  L4-implemented, POD-crossed, tests inject a fake). The `FileDialog` this leaf
  adds is the exact `FolderDialog` pattern; **no doc delta** (A12 charters it).
- **A13 / edit_render_sync** — every mutation rides `run_edit` →
  `CanvasView::apply_edit` on the single writer thread. The import is one such
  mutation.
- **§8 levelization** (`:310-346`) — the L1 core (`project`/`scene`/`interact`/
  `commands`/`dockmodel`) is forbidden ImGui/GL/SDL (`scripts/check_levels.py`);
  only `views`/`dock`/`app` (L3/L4) see ImGui/SDL. This leaf keeps the
  file→config + placement math in L1 and the drop handler / dialog / label in
  L4. The image kind's header is under `arbc/`, covered by the existing
  `EXTERNAL_ALLOWED["arbc"] ⊇ {commands}` (`check_levels.py:49-51`); `imdec` is
  PRIVATE to the archive and enters no editor TU — **so `check_levels.py` needs
  no edit**.
- **§9** — the universal DoD (`:348-375`) this leaf's Acceptance criteria
  instantiate.

**libarbc API surface** (pinned **v0.4.1**, `CMakeLists.txt:23`; the kind under
`build/*/_deps/arbc-src/plugins/image/`):

- `arbc::image::ImageContent` (`plugins/image/arbc/kind_image/image_content.hpp:344`):
  `static constexpr const char* kind_id = "org.arbc.image"` (`:346`);
  **read-only / borrowed by construction** — `editable()` is *not* overridden
  (`:411`), `external_asset_ref()` returns the authored URI (`:412`);
  `bounds()` (`:382`) is the decoded master's native `Rect` (empty when
  unavailable).
- `arbc::image::make_image_content(ContentConfig) -> expected<unique_ptr<Content>,
  std::string>` (`:484`) — the factory the registration installs.
- `arbc::image::image_config(authored_uri, resolved_uri, encoded_bytes)
  -> std::string` (`:488`) — assembles the opaque `"<authored>\n<resolved>\n<bytes>"`
  frame (`:467-483`); **empty bytes** = asset unavailable → a content with the
  URI kept and no pixels (a *condition of the environment*, never a read error),
  and only a **malformed frame** (a caller bug) is an error value.
- `arbc::register_builtin_kinds` **excludes** `org.arbc.image`
  (`arbc/api/arbc/builtin_kinds.hpp:34-35`); the loadable plugin entry is
  `plugins/image/image_plugin.cpp:16-20`; the impl STATIC archive is
  `arbc-plugin-image-impl` (`plugins/image/CMakeLists.txt:24`, linkable without
  `dlopen`, decode dep `arbc-plugin-imdec` PRIVATE). The serialize codec
  (`arbc/runtime/codec_image.cpp`) is gated on the factory being registered
  (`arbc/runtime/document_serialize.hpp:112`).
- `arbc::Affine` (`arbc/base/transform.hpp`): `apply(Vec2)`, `inverse() ->
  optional<Affine>`, `max_scale()`, `map_rect(Rect)` — the composition the
  placement helper uses.

**Editor seams this leaf extends:**

- **L1 `interact`** — `src/interact/ace/interact/interact.hpp` / `interact.cpp`:
  the new `place_at_native_scale` sits beside `place_in_view` (`interact.hpp:155`,
  whose note `:143-146` reserves it); device→comp is `camera.inverse().apply(pt)`
  (the `brush_footprint` idiom, `interact.cpp:68,77`).
- **L1 `commands`** — `src/commands/app_state.cpp:29` (`register_editor_kinds`,
  registration point) `:57,64`; the image-config helper (the one editor
  component that links the image archive, A29); `insert_cell_command`
  (`cells.hpp:45`). **L1 `project`** owns the file byte-read + path→URI
  normalization (its URI/file turf).
- **L4 `app`** — `src/app/shell.cpp:140-146` (add the `SDL_EVENT_DROP_FILE`
  arm); `Presenter::camera` (`src/app/ace/app/canvas_view.hpp:246`, comp→device);
  the canvas pane origin (`canvas_view.cpp:247`) for window→pane-local mapping;
  `AppProjectGateway::insert_cell` (`project_gateway.cpp:311-351`) mould →
  `insert_image`; the new `FileDialog` (mirror `folder_dialog.hpp`).
- **L3 `dock`** — the "Insert Cell…" affordance (`src/dock/dock.cpp:211`, modal
  `:136-181`; there is **no menu bar**, D18, `dock.cpp:371`) — the "Place image…"
  entry joins it, reaching L4 through a gateway virtual (the A12 inversion).

**Predecessor / sibling refinements:** `tasks/refinements/editor.cells/model.md`,
`tasks/refinements/editor/resolution.md`, `tasks/refinements/editor/open.md`,
`tasks/refinements/editor/open_ui.md`, `tasks/refinements/editor/reconstructing_reopen.md`.

**Test rigs:** Catch2 units join `ace_tests` (headless, GL-free;
`CMakeLists.txt:254+`, goldens under `tests/goldens/` compared byte-exact via
`ace_test::compare_golden`, `tests/golden_support.hpp:36`, against the offline
path `render::render_document_srgb8`; example `tests/render_probe_test.cpp:40-50`).
The e2e joins `ace_shell_test` (ImGui Test Engine, offscreen software-GL, driven
by widget id, state through `E2EState`; `CMakeLists.txt:322-362`; closest analog
`tests/cells_insert_e2e_test.cpp`). Threading anchor `tests/canvas_host_test.cpp`;
`asan`/`tsan` presets; residual Mesa leaks via `tests/lsan.supp`; coverage
`diff-cover --fail-under=90`. A small checked-in image fixture (a few-KB PNG
under `tests/fixtures/`) is added for the decode/golden/e2e cases.

## Constraints / requirements

1. **Levelization (`check_levels` clean) — the primary structural assertion.**
   The file→config and the placement math are **L1** (`project` byte-read /
   path→URI; `commands` config-frame + registration; `interact`
   `place_at_native_scale` over primitives + `arbc::Affine`). The drop handler,
   the `FileDialog`, the "Place image…" chrome and the window→pane mapping are
   **L4/L3** (see SDL/ImGui). **No new editor component, no new §8 DAG edge, no
   `scripts/check_levels.py` edit** — `arbc/kind_image/*.hpp` is under the
   existing `EXTERNAL_ALLOWED["arbc"] ⊇ {commands}` and `imdec` stays PRIVATE to
   `arbc-plugin-image-impl` (enters no editor TU). Nothing in the L1 core gains
   an ImGui/GL/SDL include. The one new fact is the **build/link** edge
   `ace_commands → arbc-plugin-image-impl` (A29).

2. **1:1 native px → composition units, camera-independent (D12/§8).**
   `place_at_native_scale` yields a **unit-scale** affine: the cell's placed
   composition extent equals its native pixel dimensions, *regardless of zoom* —
   a 4000-px photo is 4000 composition units wide whether dropped zoomed-in or
   zoomed-out. It is **not** `place_in_view` (no fill-fraction scaling). The drop
   point sets only the **translation** (the image is centered on the
   composition-space image of the drop point); a non-invertible camera degrades
   to identity placement, never a NaN.

3. **Borrowed and read-only by construction (D11/D13).** The authored URI is the
   dropped file's absolute path as a **borrowed** external reference — **not**
   copied into `assets/`, **not** relativized (consolidate does that later,
   D13). The minted content overrides no `editable()`, so it is read-only;
   `classify_detail` tags it `ReferencedImage` + `borrowed` off the generic
   facet (`cell.cpp:114-118`), never a `kind_id` switch (A16). This leaf writes
   no new provenance code — it just produces a cell the shipped classifier reads.

4. **Import is ONE undoable action through the single-writer seam (A13/D15).**
   The whole import — build config, probe bounds, place, mint — resolves to one
   `insert_cell_command` dispatched inside `run_edit` (`CanvasView::apply_edit`),
   so it is ONE libarbc transaction = one journal entry = one undo press
   (D-one_action_one_entry-1), with the new cell selected. A factory failure
   (unreadable/undecodable file surfaced as an empty-bytes or malformed frame)
   returns the kind's own error with the `Document` untouched — no half-import,
   no orphan transaction (A16 error-as-value).

5. **The image decodes and renders immediately, and round-trips.** The config
   embeds the file bytes, so `bounds()`/render work at import with no async
   asset wait. On **save** the gated codec writes `params.source` (the URI, not
   the bytes — borrowed); on **reopen** (rebuild-from-canonical) the registered
   codec reconstructs the cell and the installed `FilesystemAssetSource`
   (`project_open.cpp:316-318`, A4.1a) re-resolves the URI. A borrowed file that
   has since moved is the **missing-borrow** case — placeholder + relink, owned
   by `editor.import.consolidate` (D13), out of scope here; this leaf asserts the
   *present-file* round-trip only.

6. **Two entry points, one testable verb.** The `SDL_EVENT_DROP_FILE` handler
   and the "Place image…" dialog both call a single app verb
   `insert_image(path, device_point)`; the drop supplies the cursor's
   pane-local device point, the dialog supplies the focused canvas center. A
   drop whose window point is not over a canvas pane falls back to the focused
   canvas center (graceful, documented) rather than being silently lost. The
   e2e drives `insert_image` directly with a fixture path and a scripted
   `FileDialog` fake (no real OS drop/dialog exists headless) — the
   `folder_dialog` fake precedent.

7. **`org.arbc.image` in the generic Insert list is an accepted consequence, not
   a filter (A16).** Once registered the kind appears in `insert_schemas` with
   the **raw-config fallback** field, like every kind lacking a grammar adapter.
   It is **not** hidden — A16 forbids an allowlist. A hand-typed frame yields the
   kind's own error (errors are values, `Document` untouched — the `org.arbc.fade`
   precedent); the ergonomic path is the drop/Place gesture. No adapter and no
   filter are added.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella. `diff-cover --fail-under=90` on changed lines; tests ship with the task.

- **Levelization (`check_levels` clean) — the primary structural assertion.** No
  new component, no new §8 DAG edge, **no `scripts/check_levels.py` edit**: the
  L1 helpers take primitives + `arbc::Affine` and `arbc/kind_image/*` is under
  the existing `EXTERNAL_ALLOWED["arbc"] ⊇ {commands}`; `imdec` is PRIVATE to
  `arbc-plugin-image-impl` and appears in no editor TU; only `src/app/` (L4)
  holds SDL/ImGui and the drop/dialog code. Confirm `scripts/gate`'s level lint
  passes and no entry in `scripts/check_levels.py:21-60` changes. The A29 build
  edge (`ace_commands → arbc-plugin-image-impl`) is a CMake fact, verified by the
  editor binary linking and by `registry.factory("org.arbc.image") != nullptr`.
- **L1 logic — Catch2 units** (`tests/image_import_test.cpp`, new file in
  `ace_tests`; `TEST_CASE("image_import: …")`, moulded on
  `tests/cell_model_test.cpp` / `tests/interact_test.cpp`):
  - **`place_at_native_scale` is unit-scale and camera-independent
    (Constraint 2):** a `content_bounds` of W×H under a known camera yields an
    affine with `max_scale() == 1` (no shear/rotation) whose placed rect has
    composition extent exactly W×H; **doubling the camera zoom leaves the placed
    composition extent unchanged** (1:1 is not fill-to-view); the placed rect is
    centered on `camera.inverse().apply(device_point)`. A non-invertible / zero-
    area camera yields identity placement — no NaN, no div-by-zero. Use integer
    sizes for byte-exact assertions.
  - **Image-config helper builds a valid frame + decodes to native px:** reading
    the checked-in fixture builds a well-formed `image_config` frame (two
    newlines; the absolute-path URI in both fields; the fixture bytes after the
    second `\n`), and `registry.factory("org.arbc.image")` over that frame yields
    a content whose `bounds()` equals the fixture's native dimensions. A missing
    file yields a **value** error (or an empty-bytes frame → a content with the
    URI and no pixels, the D13 unavailable case), **never** a throw.
  - **Registration + classification:** after `register_editor_kinds`,
    `registry.factory("org.arbc.image") != nullptr` and `"org.arbc.image"` is in
    `registry.ids()`; `scene::classify_detail` on the minted cell reports
    `DetailSource::ReferencedImage`, `borrowed == true`, and
    `native_pixels == {W,H}` (the shipped classifier, re-pinned here).
  - **One-action-one-entry (Constraint 4):** dispatching
    `insert_cell_command(registry,"org.arbc.image",config,placement)` adds ONE
    journal entry, selects the new cell, and a single undo removes it on its same
    `ObjectId` (D15); a factory failure opens no transaction and leaves the
    `Document` byte-identical.
  - **Present-file round-trip (Constraint 5):** `save_project` then a
    rebuild-from-canonical reopen restores a live `org.arbc.image` cell whose
    `external_asset_ref()` equals the authored URI and whose `bounds()` equals
    the native px (codec gated on registration; `FilesystemAssetSource` resolves
    the URI). Moulded on `tests/reopen_*`/`save_*` fixtures.
- **Rendered output — golden** (`ace_tests`): a new composition golden
  `tests/goldens/import_image_<w>x<h>.rgba8` — the fixture imported at 1:1 and
  composited through `render::render_document_srgb8(doc, w, h, camera)` matches
  byte-exact (the vendored imdec decode is deterministic; the render path is
  byte-exact — a tolerance is **not** used). This pins that the borrowed image
  actually renders at native scale, and that the 1:1 placement lands where the
  drop point says. (Golden justified by §9: this is new rendered output — a
  composited photo — not covered by any existing golden.)
- **UI e2e — ImGui Test Engine** (`tests/image_import_e2e_test.cpp`, in
  `ace_shell_test`, driven by widget id, state through `E2EState`, offscreen
  software-GL; modelled on `tests/cells_insert_e2e_test.cpp`):
  - **Drop path:** invoke `insert_image(fixture_path, drop_point)` (the verb the
    `SDL_EVENT_DROP_FILE` arm calls); assert one new cell appears **selected**,
    its `CellDetail` is `ReferencedImage` + `borrowed` with
    `native_pixels == fixture size`, and its placement is 1:1 at the drop point
    (placed composition extent == native px, centered on the drop point mapped
    through the live camera).
  - **Place path:** the "Place image…" affordance beside "Insert Cell…" opens the
    `FileDialog` seam (a scripted fake returns the fixture) and places the image
    at the focused canvas center; the same detail/placement assertions hold.
  - **Read-only:** the imported cell reports no editable facet (a Brush attempt
    over it does not paint — the hook `editor.paint.retouch_stack` later drives),
    and the Inspector shows its native resolution + borrowed provenance (the
    shipped readouts, exercised through the new cell).
  - **Out-of-pane drop (Constraint 6):** a drop whose point is not over a canvas
    pane falls back to the focused canvas center rather than being lost.
- **Threading — ASan/TSan.** The import mutation rides `run_edit` →
  `CanvasView::apply_edit` on the single writer thread (A13); the drop/dialog
  callback fires on the UI (event-pump) thread (the `folder_dialog` TSan
  precedent — SDL delivers the result on the pumping thread, no cross-thread
  sync). One assertion appended to the `tests/canvas_host_test.cpp` TSan anchor
  confirms an import (new cell + its decode) while the render thread drives the
  same `Document` is data-race-clean — the new cell and its content publish
  through the `pin()` seam. No new thread, no new lock. Residual Mesa leaks via
  `tests/lsan.supp`.
- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed
  lines; clang-format + build clean across presets. Tests ship with the task.

**Deferred WBS work.** **None new as a WBS leaf.** Every adjacent surface already
has a shipped or scheduled owner: paste→owned is `editor.import.paste`
(`depends !image`), nested `.arbc` is `editor.import.nested`, Consolidate +
**missing/moved-borrow placeholder + relink** is `editor.import.consolidate`
(D13; `depends paste, nested`), and the read-only-photo retouch layer is
`editor.paint.retouch_stack` (`depends editor.import.image`). This leaf
cross-references them; it registers no successor of its own. The one design fact
it introduces — registering the out-of-lib image kind — is recorded as the A29
doc delta, not a follow-up task.

## Decisions

- **D-image-1 — Import mints through the EXISTING A16 seam
  (`add_cell` / `insert_cell_command` with `kind_id = "org.arbc.image"`); no new
  `scene` verb, no new command.** The import is a config + a placement affine
  through the same Registry-driven insert every built-in kind already uses; A16
  reserves exactly this ("`editor.import.image` a native-px→units 1:1 affine,
  with no change to `scene`", `interact.hpp:143-146`). The only new L1 verbs are
  the pure `place_at_native_scale` (a sibling to `place_in_view`) and the
  file→config helper. *Rationale:* the model already understands a borrowed
  external-ref cell generically (`classify_detail`, `native_pixels`, the
  Inspector/Layers/resolution readouts) — reusing `add_cell` means all of that
  lights up for free and the import inherits one-action-one-entry, selection and
  undo with zero new machinery. *Alternative rejected:* a bespoke
  `scene::add_image` verb that names `arbc::image` — duplicates `add_cell`,
  hard-codes a kind into `scene` (the allowlist A16 forbids), and forks the
  transaction/undo path. **No doc delta beyond A29.**

- **D-image-2 — Place at 1:1 native scale, NOT `place_in_view`; the drop point
  sets only the translation.** `place_at_native_scale` returns a unit-scale
  affine (placed extent == native px, camera-independent) centered on the
  composition-space image of the drop point. *Rationale:* D12/§8 are explicit —
  "Sizing is **always 1:1** on drop … fit-to-camera would discard the true
  relative scale between imports"; a 4000-px photo must genuinely dwarf a 200-px
  sketch, which `place_in_view`'s fill-to-view scaling destroys. Centering on the
  drop point (rather than top-left anchoring) matches "at the drop point" as
  "the thing lands where you dropped it" and is symmetric for the dialog's
  view-center fallback. *Alternative rejected:* reuse `place_in_view` — directly
  contradicts D12 (discards relative scale). *Alternative rejected:* top-left
  corner at the drop point — less predictable for a drop gesture and asymmetric
  with the centered view-center Place; centering is the more forgiving default.
  **No doc delta** (D12/§8/A16 already specify 1:1).

- **D-image-3 — Register `org.arbc.image` by static-linking
  `arbc-plugin-image-impl` into `ace_commands`, in `register_editor_kinds`
  (A29 doc delta).** The kind ships out-of-lib and is excluded from
  `register_builtin_kinds`, so it must be registered for `add_cell` to factory
  it and for the runtime serialize codec to round-trip it. A static link (not
  `dlopen`) is deterministic and testable; the decode dep stays PRIVATE to the
  archive. *Rationale:* it mirrors `scene::register_camera_kind` exactly (A14),
  gates the codec (`document_serialize.hpp:112`), and adds the borrowed-image
  capability with one link line + one `registry.add`. *Alternative rejected:*
  `dlopen` the loadable MODULE at runtime — a dependency on installed-artifact
  layout and a new headless failure mode (the offline/e2e job can't find the
  `.so`), the regression A27 chose build-embedding to avoid. *Alternative
  rejected:* fork libarbc to fold the kind into `register_builtin_kinds` — drags
  the stb-class decode into `libarbc`, the "codec line" containment the library
  forbids. **Doc delta: A29.**

- **D-image-4 — Two entry points (OS file-drop + "Place image…") funnel through
  ONE app import verb `insert_image(path, device_point)`; the file picker is a
  `FileDialog` seam mirroring `FolderDialog`.** The `SDL_EVENT_DROP_FILE` arm
  (drop point) and the "Place image…" affordance beside Insert Cell (view
  center) both call the same verb; the picker is the A12 dialog pattern
  (abstract `FileDialog`, SDL `SdlFileDialog` over `SDL_ShowOpenFileDialog`,
  scriptable fake for tests). *Rationale:* D18 leaves no menu bar, so "Place"
  joins the existing Insert affordance; one verb keeps the drop and dialog paths
  behaviourally identical and gives the headless e2e a single, driveable entry
  (no real OS drop/dialog exists offscreen) — the exact `folder_dialog` fake
  precedent. *Alternative rejected:* handle the drop entirely in the SDL loop
  with no shared verb — strands the import logic where no e2e can reach it, and
  the Place path would re-implement it. *Alternative rejected:* drop-only (no
  Place affordance) — the note says "Drop **or** Place"; a keyboard/menu path is
  needed where drag-drop is unavailable. **No doc delta** (A12 charters the
  dialog seam; A29 covers the kind).

- **D-image-5 — Borrowed URI = the file's absolute path, un-relativized; bytes
  embedded at import for immediate decode; save writes only the URI.** The
  authored + resolved URI is the dropped file's absolute path as an external
  (borrowed) reference; the config embeds the bytes so the photo decodes and
  renders at once; the gated codec persists only the URI. *Rationale:* D11/D13
  make the imported photo borrowed — its bytes are **not** the project's, so they
  are not copied into `assets/` and not relativized here (consolidate does both
  later). Embedding the bytes avoids an async asset-arrival round trip at import;
  on reopen the installed `FilesystemAssetSource` re-fetches. *Alternative
  rejected:* copy the file into `assets/` at import (own it) — that is the
  **paste** semantics (`editor.import.paste`), and it discards the D11 borrowed
  axis and the portability story consolidate depends on. *Alternative rejected:*
  relativize the URI at import — premature; a borrowed file lives anywhere,
  relativization is consolidate's reversible transaction (D13/D15). **No doc
  delta** (D11/D13 specify borrowed).

- **D-image-6 — `org.arbc.image` appears in the generic Insert list with the
  raw-config fallback; it is not filtered out.** Once registered the kind is one
  of `registry.ids()`, so `insert_schemas` offers it with the raw-config field,
  like any adapter-less kind. *Rationale:* A16 forbids a kind allowlist — "no
  filter by id, by metadata, or by 'is it visual'"; the raw-config path yields
  the kind's own error on a hand-typed frame (errors are values, `Document`
  untouched — the `org.arbc.fade` precedent), and the ergonomic path is the
  drop/Place gesture. *Alternative rejected:* special-case-hide the image kind
  from the modal — a direct A16 violation, and the machinery to special-case one
  id is the allowlist by another name. *Alternative rejected:* write a grammar
  adapter so "Image" in the modal opens a file picker — a second, redundant
  import entry point that duplicates D-image-4's verb behind the modal;
  deferring it costs nothing (the drop/Place paths cover the need). **No doc
  delta** (A16 already settles it).

## Open questions

(none — all decided.)

No item is routed to the parking lot: the one design fact (registering the
out-of-lib image kind) is decided and recorded as A29, and every adjacent
capability (paste, nested, consolidate/relink, retouch) is an already-scheduled
sibling WBS leaf rather than a deferral of this task.

## Status

**Done** — 2026-07-31.

- `src/interact/ace/interact/interact.hpp` + `src/interact/interact.cpp`: added `place_at_native_scale` — pure L1 1:1 native-px→units affine centered on the composition-space image of the drop point; degrades to identity on non-invertible camera, never NaN.
- `src/project/ace/project/import_asset.hpp` + `src/project/import_asset.cpp`: added `borrow_asset_file` — reads file bytes, normalizes absolute path to a borrowed URI, assembles the `image_config` frame for immediate decode.
- `src/commands/ace/commands/image_import.hpp` + `src/commands/image_import.cpp` + `src/commands/app_state.cpp`: `register_image_kind` / `image_config` helper (A29 link edge `ace_commands → arbc-plugin-image-impl`); wired into `register_editor_kinds`.
- `src/dock/ace/dock/dock.hpp` + `src/dock/dock.cpp`: "Place image…" affordance beside "Insert Cell…"; gateway virtual seam to L4.
- `src/app/ace/app/file_dialog.hpp` + `src/app/file_dialog.cpp`: `FileDialog` / `SdlFileDialog` seam mirroring `FolderDialog` (A12).
- `src/app/project_gateway.cpp` + `src/app/canvas_view.cpp` + `src/app/shell.cpp`: `insert_image` / `place_image` / `drop_device_point` / `focused_pane_rect` / SDL `SDL_EVENT_DROP_FILE` arm; SDL/OS glue marked `GCOVR_EXCL`.
- `CMakeLists.txt`: A29 link edge, `ACE_FIXTURE_DIR`; `docs/01-architecture.md`: A29 row.
- `tests/image_import_test.cpp`: 10 Catch2 unit cases (placement, config-frame, registration, classification, one-action-one-entry, round-trip, `place_at_native_scale` degenerate paths).
- `tests/image_import_e2e_test.cpp`: 3 ImGui Test Engine e2e cases (drop path, Place path, `focused_pane_rect == nullopt`); `tests/canvas_host_test.cpp`: TSan anchor addition.
- `tests/goldens/import_image_64x64.rgba8`: render_offline golden; `tests/fixtures/photo_12x8.ppm`: Netpbm P6 decode fixture.
