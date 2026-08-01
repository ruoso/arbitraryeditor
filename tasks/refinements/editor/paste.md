# editor.import.paste — Paste / clipboard bitmap → owned read-only image

## TaskJuggler entry

- **Task:** `editor.import.paste` (`tasks/00-editor.tji:709-714`, under
  `task import "Import & assets"` at `:701`).
- **Effort:** `1.5d` (`:710`) · `allocate team` (`:711`).
- **Depends:** `!image` = `editor.import.image` (`:712`) — the borrowed-image
  import path, **Done** 2026-07-31 (`tasks/refinements/editor/image.md` Status).
  The dependency is satisfied; paste reuses almost all of its machinery.
- **Note (`.tji:713`):** "A pasted bitmap has no source file to borrow, so the
  project MINTS it as an owned read-only image in `assets/`; otherwise it behaves
  like an import. Design: D11/D12."
- **Back-link:** the `.tji` note currently ends
  `Refinement: tasks/refinements/import_paste.md` (the flat interim path). This
  refinement lands at **`tasks/refinements/editor/paste.md`** per the
  orchestrator's area = first-dot-segment (`editor`) assignment
  (`tasks/refinements/README.md:9-18`); the closer updates the note back-link to
  the real path and adds `complete 100` after `allocate team`
  (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji` here.
- **Downstream:** `editor.import.paste` gates `editor.import.consolidate`
  (`:720-724`, `depends editor.import.paste, editor.import.nested`) and is a named
  dependency of `editor.packaging.package` (`:737-740`). It is the second of the
  three D12 import paths; it feeds the `editor.import` gather the final
  `milestones` milestone depends on.

## Effort estimate

**1.5 days.** Paste is `editor.import.image`'s twin — the whole *insert* tail
(`image_config` → `probe_bounds` → `place_at_native_scale` →
`insert_cell_command` → `run_edit` → select), the `org.arbc.image` registration
and its `arbc-plugin-image-impl` link edge (A29), the immediate-render config
frame, and the classifier/Inspector/Layers readouts all shipped with
`editor.import.image`. Paste changes exactly two things: the **byte source**
(clipboard, not a file) and the **asset provenance** (owned in `assets/`, not
borrowed external).

- **`project::mint_owned_asset`** (L1) — content-address the clipboard bytes,
  write them write-if-absent into the live project's `assets/` through the
  shipped `FilesystemAssetSink`, return the project-relative owned URI + bytes.
  ~0.3d incl. Catch2.
- **`scene::classify_detail` owned/borrowed test** (L1) — a project-relative
  `assets/` URI reports `borrowed=false`; an external absolute URI keeps
  `borrowed=true`. ~0.2d incl. Catch2.
- **`Clipboard` seam** (L4) — abstract `Clipboard` + SDL-backed `SdlClipboard`
  over `SDL_GetClipboardData` + a scriptable fake, mirroring `FileDialog` (A12).
  ~0.3d.
- **Gateway `paste_image()` verb** (L4) — read the clipboard, mint, and share
  `insert_image`'s tail (refactor the file/bytes source out of the shared body).
  ~0.3d.
- **Ctrl+V chord + "Paste image" affordance** (L3) beside the undo/redo chords
  and "Place image…". ~0.1d.
- **Tests + threading scope** (Catch2 · golden reuse · e2e · TSan anchor).
  ~0.3d.

**No new editor component, no new §8 DAG edge, no new build/link edge** (the A29
`ace_commands → arbc-plugin-image-impl` edge is reused — paste registers no new
kind), **no `check_levels` edit, no libarbc fork, no pin bump.** The one new
architectural fact — the owned-image-from-raw-bytes mint path — is recorded as
the **A30** doc delta.

## Inherited dependencies

**Settled (consumed as-is from `editor.import.image`, `tasks/refinements/editor/image.md`):**

- **`commands::register_image_kind` / `image_config`**
  (`src/commands/ace/commands/image_import.hpp:14,23,29-30`,
  `src/commands/image_import.cpp:15-32`) — the `org.arbc.image` factory
  registration (A29 link edge `ace_commands → arbc-plugin-image-impl`) and the
  `image_config(authored_uri, resolved_uri, encoded_bytes)` frame builder. **Both
  reused verbatim** — paste registers no new kind and builds the same frame,
  differing only in which URI it passes.
- **`interact::place_at_native_scale`**
  (`src/interact/ace/interact/interact.hpp`) — the pure L1 1:1 native-px→units
  affine centered on the composition-space image of the device point; degrades to
  identity on a non-invertible camera. **Reused verbatim** (paste places at the
  focused-pane center, `device_point == nullopt`).
- **`scene::add_cell` / `probe_bounds` / `insert_cell_command`**
  (`src/scene/ace/scene/cell.hpp:134-137,110-111`,
  `src/commands/ace/commands/cells.hpp:45`) — the A16 Registry-driven insert
  through `run_edit` → `CanvasView::apply_edit` (one transaction = one journal
  entry = one undo, A13/D15). **Reused verbatim.**
- **`scene::classify_detail` / `CellDetail`** (`src/scene/cell.cpp:107-126`,
  `src/scene/ace/scene/cell.hpp:198-214`) — tags a non-empty `external_asset_ref()`
  as `DetailSource::ReferencedImage` and fills `native_pixels` from `bounds()`.
  **Extended here** with the one owned/borrowed distinction (see Constraint 3).
- **`AppProjectGateway::insert_image`** (`src/app/project_gateway.cpp:370-405`) —
  the shipped verb: `borrow_asset_file` → `image_config` → `probe_bounds` →
  `place_at_native_scale` → `insert_cell_command` → `run_edit` → select. **Its
  tail is factored out and shared with `paste_image`** (the byte-source varies).
  `drop_device_point` (`:357-368`) and the `nullopt` → focused-center fallback
  (`:387-388`) are reused.
- **`FileDialog` / `SdlFileDialog` seam** (`src/app/ace/app/file_dialog.hpp`,
  `src/app/file_dialog.cpp:33-53`, A12) — the async native-dialog pattern
  (abstract interface, SDL backing, scriptable fake). **`Clipboard` mirrors it.**
- **`FilesystemAssetSink` / `ProjectLayout`** (`src/project/ace/project/save.hpp:60-69`,
  `src/project/save.cpp:84-105`; `src/project/ace/project/project.hpp:82,85,99`) —
  the write-if-absent, content-addressed, never-deletes `assets/` sink and the
  `assets_dir = <root>/assets` layout. **`mint_owned_asset` writes through it.**
- **`FilesystemAssetSource` reopen resolution** (`src/project/project_open.cpp:316-318`,
  A4.1a) — resolves a cell's `params.source` URI against the canonical project
  root on reopen; a project-relative `assets/images/<xx>/<hash>` URI resolves to
  `<root>/assets/images/<xx>/<hash>`. **Reused unchanged.**

**Pending (owned here):** the `project::mint_owned_asset` helper, the
`classify_detail` owned/borrowed test, the `Clipboard` seam, the gateway
`paste_image` verb + the `insert_image` tail refactor, the Ctrl+V chord + "Paste
image" affordance, and their tests. The A30 doc delta records the owned-mint
seam. Nothing downstream is blocked on an unwritten predecessor.

## What this task is

Turn an image on the system clipboard (Ctrl+V, or a "Paste image" affordance)
into an **owned, read-only `org.arbc.image` cell** placed at the focused-pane
center at **1:1 native px → composition units**. Unlike a dropped file, a pasted
bitmap has **no source file to borrow** (D11/D12/§8), so the project **mints an
owned asset**: the clipboard's encoded image bytes are content-addressed and
written into the project's `assets/`, and the cell references them by a
project-relative owned URI. Everything downstream of "here are the bytes and a
URI" is `editor.import.image`'s tail, reused. Concretely, four pieces:

1. **Read an encoded image off the clipboard** (L4). A `Clipboard` seam mirroring
   `FileDialog` — abstract `Clipboard::read_image() -> optional<{bytes, mime}>`,
   SDL-backed `SdlClipboard` over `SDL_GetClipboardData`, a scriptable fake for
   tests. v1 accepts an **encoded** image (prefer `image/png`, then any
   imdec-decodable mime); paste is symmetric with import — encoded bytes in.

2. **Mint the owned asset** (L1 `project`). `mint_owned_asset(fs, layout, bytes,
   ext)` content-addresses the bytes (the raster codec's `hash_tile` /
   `tile_blob_uri` model), writes them **write-if-absent** into the live
   project's `assets/` through the shipped `FilesystemAssetSink` (dedup by content
   name), and returns the **project-relative owned URI**
   (`assets/images/<xx>/<hash>`) + the bytes. This is the one genuinely new L1
   verb, and the A30 seam.

3. **Insert exactly like an import** (L4 → L1). `image_config(owned_uri,
   owned_uri, bytes)` (bytes embedded → renders immediately) → `probe_bounds` →
   `place_at_native_scale(camera, focused_center, bounds)` → `insert_cell_command`
   through `run_edit`. One journal entry, one undo, the new cell selected — the
   `insert_image` tail, shared.

4. **Two triggers → one verb** (L3/L4). A **Ctrl+V** chord (beside the undo/redo
   chords) and a **"Paste image"** affordance (beside "Place image…") both call
   ONE gateway verb `paste_image()`, which reads the clipboard and — if an image
   is present — mints and inserts at the focused-pane center. An **empty /
   no-image clipboard is a graceful no-op** (nothing pasteable), never an error.

The minted cell is **owned + read-only**: read-only by construction
(`org.arbc.image` overrides no `editable()`), and owned because its bytes live in
the project's `assets/` under a project-relative URI — so `classify_detail`
reports `borrowed=false` (the D11 "bytes where?" axis), and GC roots the blob off
the live cell's reference.

**Not in scope, by WBS split:** copying an owned image blob into a **fresh root's
`assets/` on Save As / Consolidate** — deferred to `editor.import.owned_asset_republish`
(the image codec writes only the URI, never routing bytes through the sink, so a
copy-to-new-root needs an editor-side republish step; in-place Save→reopen
round-trips fully without it). Also out: **raw-RGBA-only clipboards** (need an
encoder the editor does not vendor — a graceful no-op in v1, the
encoder/raw-RGBA question routed to the parking lot); placing another `.arbc`
(`editor.import.nested`); Consolidate + relink (`editor.import.consolidate`).

## Why it needs to be done

D11 classifies every brought-in pixel on two axes — **editable?** and **bytes
where?** — and names the pasted bitmap the **owned + read-only** corner:
"a pasted bitmap (no source file) is **owned + read-only** (the project mints
it)" (`docs/00-design.md:309-311`, D11 `:478`). D12 is the import spec:
"paste/clipboard → owned read-only" (`:479`). §8 spells out the behavior:
"**Paste / clipboard** (no source file) → **owned** read-only image; the project
mints the asset, otherwise it behaves like an import" (`:323-324`), and its
on-disk rule: "Owned bytes in `assets/` (painted tiles *and* consolidated/pasted
images) are content-addressed, dedup'd, and **garbage-collected**" (`:342-345`,
D13 `:480`). Today `editor.import.image` can bring in a **borrowed** photo, but
there is no path for the far more common "I copied an image in a browser and want
it here" gesture — and the borrowed path is exactly wrong for it (a clipboard
bitmap has no stable external file to point at). Paste is the owned mirror of the
borrowed import, and the smallest addition that completes the D12 import trio: it
reuses the entire insert tail and adds only a byte source and a mint. It also
unblocks `editor.import.consolidate` (whose `depends` names it) and is a gate on
`editor.packaging.package`.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **D11 — Two asset axes** (`docs/00-design.md:478`; body `:309-311`): the pasted
  bitmap is **owned + read-only** — "the project mints it". This leaf produces
  that corner; the classifier already surfaces both axes once told owned≠borrowed.
- **D12 — Import paths** (`:479`): "paste/clipboard → owned read-only" — this
  leaf's one-clause specification.
- **§8 — Import & assets** (`:296-352`): "**Paste / clipboard** (no source file)
  → **owned** read-only image; the project mints the asset, otherwise it behaves
  like an import" (`:323-324`); "Owned bytes in `assets/` (painted tiles *and*
  consolidated/pasted images) are content-addressed, dedup'd, and
  garbage-collected" (`:342-345`); "**Formats:** whatever the library decodes
  (PNG/JPG/… via imdec)" (`:352`) — the format statement covers the *decodable*
  encoded bytes, matching v1's encoded-clipboard scope.
- **D13 — Assets, GC & portability** (`:480`): owned bytes in `assets/`
  content-addressed, dedup'd, GC'd via explicit "Clean up" + on close, roots =
  all open docs; borrowed files never GC'd. This leaf mints an **owned** asset —
  content-addressed, GC-rooted by the live cell — the opposite of the borrowed
  import.
- **D15 — Undo = library transactions** (`:482`): the paste is one transaction =
  one undo step; the new cell is scene data.

**Governing architecture rows:**

- **A16** (`docs/01-architecture.md:433`) — cell insert is Registry-driven, no
  allowlist; content minted only via `registry.factory(id)`; placement arrives as
  a finished `arbc::Affine`. Paste is a different asset SOURCE through the same
  seam, not a new insert verb.
- **A13** (`:430`) — the canonical dump `project::save_project` writes owned
  tiles/assets to `assets/` through a `project`-side `arbc::AssetSink`. The mint
  helper writes through that same sink, in `project`.
- **A23** (`:440`) — ONE `arbc::RasterTileStore` per `Document`; "any raster-cell
  insert, **paste** or import mints" owned bytes; the write-if-absent sink and the
  undo-orphans-a-blob-GC-reclaims lifecycle. Paste's owned-image blob follows the
  identical lifecycle (a different content-address family, `assets/images/…`).
- **A29** (`:446`) — the `org.arbc.image` kind + its `arbc-plugin-image-impl`
  link edge + the codec gated on registration + `FilesystemAssetSource` reopen
  resolution. **Reused wholesale** — paste registers no new kind.
- **A30** (this task's doc delta, `docs/01-architecture.md:447`) — the
  owned-image-from-raw-bytes mint path: content-address clipboard bytes →
  write-if-absent into `assets/` via the `project` sink → project-relative owned
  URI → same A16/A29 insert seam → `classify_detail` reports owned; Save
  As/Consolidate blob-copy deferred. This is the one architectural fact this leaf
  adds.
- **A12** (`docs/01-architecture.md`) — the native-dialog seam pattern. The
  `Clipboard` seam is the exact `FileDialog` mould (abstract, SDL-backed, fake
  for tests); **no doc delta** for the seam shape.
- **§8 levelization** (`:328-346`) — L1 core (`project`/`scene`/`interact`/
  `commands`) forbidden ImGui/GL/SDL. `project` is the only L1 component naming
  libarbc (`:334`) and already owns the `AssetSink → assets/` write (A13), so the
  mint helper hosts there with `commands → project → libarbc` all already
  declared — **no new DAG edge**. The `Clipboard` seam + Ctrl+V + affordance are
  L4/L3 (SDL/ImGui). **No `scripts/check_levels.py` edit.**
- **§9** — the universal DoD (`:348-375`) this leaf's Acceptance criteria
  instantiate.

**libarbc API surface** (pinned **v0.4.1**, `CMakeLists.txt:23`):

- `arbc::image::image_config(authored, resolved, bytes)` / `make_image_content` /
  `ImageContent::kind_id = "org.arbc.image"` (under
  `build/*/_deps/arbc-src/plugins/image/`) — reused via the shipped
  `commands::image_config` / `register_image_kind` wrappers.
- `arbc::AssetSink::put(resolved_uri, bytes)` — the write-if-absent contract the
  `FilesystemAssetSink` implements (`src/project/save.cpp:84-105`).
- `arbc::FilesystemAssetSource` — reopen-side URI resolution
  (`src/project/project_open.cpp:316-318`).
- The raster content-address model (`build/*/_deps/arbc-src/src/runtime/codec_raster.cpp`,
  `tile_blob.cpp:81-106`: `hash_tile` = `to_hex(sha256(…))`, `tile_blob_uri` =
  `assets/tiles/<xx>/<hash>`) — the pattern `mint_owned_asset` mirrors for
  `assets/images/…`. The image codec (`codec_image.cpp:91-93`) writes
  `params.source` = the authored URI verbatim and does **not** route bytes through
  the sink — the fact that forces eager mint at paste time and defers Save As
  blob-copy.

**Editor seams this leaf extends:**

- **L1 `project`** — `src/project/ace/project/import_asset.hpp` /
  `import_asset.cpp` (`borrow_asset_file` sits here; `mint_owned_asset` joins it);
  `src/project/save.hpp:60-69` (`FilesystemAssetSink`);
  `src/project/ace/project/project.hpp:82,85,99` (`ProjectLayout`).
- **L1 `scene`** — `src/scene/cell.cpp:107-126` (`classify_detail`; add the
  owned/borrowed URI test).
- **L4 `app`** — `src/app/project_gateway.cpp:370-405` (`insert_image`, factor
  the tail; add `paste_image` / `can_paste_image`); `src/app/shell.cpp:387-393`
  (undo/redo chords → add Ctrl+V); new `src/app/ace/app/clipboard.hpp` +
  `clipboard.cpp` (mirror `file_dialog.*`).
- **L3 `dock`** — `src/dock/dock.cpp:219-223` ("Place image…"; "Paste image"
  joins it); `src/dock/ace/dock/dock.hpp:250-261` (the `ProjectGateway` virtual
  seam — add `can_paste_image` / `paste_image`).

**Predecessor / sibling refinements:** `tasks/refinements/editor/image.md`
(the twin — read first), `tasks/refinements/editor/gc.md` (owned-asset lifecycle
+ the `editor.import.consolidate` split precedent), `tasks/refinements/editor/save_as.md`
(the republish-not-byte-copy model relevant to the deferred task).

**Test rigs:** Catch2 units join `ace_tests` (headless, GL-free; goldens under
`tests/goldens/` compared byte-exact via `ace_test::compare_golden`). The e2e
joins `ace_shell_test` (ImGui Test Engine, offscreen software-GL, driven by
widget id, state through `E2EState`; closest analog
`tests/image_import_e2e_test.cpp`). Threading anchor `tests/canvas_host_test.cpp`;
`asan`/`tsan` presets; residual Mesa leaks via `tests/lsan.supp`; coverage
`diff-cover --fail-under=90`. The checked-in fixture `tests/fixtures/photo_12x8.ppm`
(added by `editor.import.image`) is reused as the "clipboard bytes".

## Constraints / requirements

1. **Levelization (`check_levels` clean) — the primary structural assertion.**
   `mint_owned_asset` is **L1 `project`** (it names `arbc::AssetSink`, which
   `project` already may, and writes to `assets/` through the shipped sink). The
   `classify_detail` owned/borrowed test is **L1 `scene`** over URI strings. The
   `Clipboard` seam, the Ctrl+V chord, the "Paste image" chrome and the gateway
   `paste_image` glue are **L4/L3**. **No new editor component, no new §8 DAG
   edge, no new build/link edge** (A29's `ace_commands → arbc-plugin-image-impl`
   is reused; paste registers no kind so `project` links no image archive), **no
   `scripts/check_levels.py` edit.** Nothing in the L1 core gains an ImGui/GL/SDL
   include.

2. **1:1 native px → composition units, reusing `place_at_native_scale` (D12/§8).**
   The paste places at the **focused-pane center** (`device_point == nullopt` →
   the shipped fallback) at unit scale — a pasted 4000-px bitmap is 4000
   composition units wide regardless of zoom, exactly as the borrowed import. No
   new placement math.

3. **Owned and read-only by construction, provenance surfaced (D11/D13).** The
   asset is **owned**: `mint_owned_asset` writes the bytes into the live project's
   `assets/` (content-addressed, write-if-absent, dedup) and the cell references
   the **project-relative owned URI**. The minted content overrides no
   `editable()`, so it is read-only. `classify_detail` must report the pasted cell
   as `DetailSource::ReferencedImage` + **`borrowed=false`** — the one behavioral
   extension: a project-relative `assets/` URI ⇒ owned; an external absolute URI ⇒
   borrowed (the shipped import case, re-pinned). So the Layers panel shows
   "owned" for a pasted bitmap and "borrowed" for an imported photo (the D11
   "bytes where?" axis).

4. **Content-addressed, dedup'd, GC-rooted (D13/A23).** Identical clipboard bytes
   pasted twice yield the **same owned URI** and write the blob once (write-if-absent
   dedup). The live cell's reference **roots** the blob: a GC pass
   (`editor.project.gc`) does not reclaim it while the cell exists, and reclaims
   it once the cell is gone (paste→undo→save→GC) — the A23 tile lifecycle (the
   sink never deletes; GC is the reaper).

5. **Paste is ONE undoable action through the single-writer seam (A13/D15).** The
   whole paste — read clipboard, mint, build config, probe, place, insert —
   resolves to one `insert_cell_command` dispatched inside `run_edit`
   (`CanvasView::apply_edit`), so it is ONE transaction = one journal entry = one
   undo, with the new cell selected. The **mint is not undoable** (writing a blob
   to `assets/` is not a document edit — like tiles, an orphan the GC reaps); only
   the cell insert is journaled. A clipboard with no decodable image opens no
   transaction and leaves the `Document` untouched.

6. **Renders immediately and round-trips in-place.** The config embeds the bytes,
   so the paste decodes and renders at once (no async wait). On **in-place Save**
   the image codec persists `params.source` = the owned URI (the blob is already
   at `<root>/assets/…` from the eager mint); on **reopen** the registered codec
   reconstructs the cell and the installed `FilesystemAssetSource`
   (`project_open.cpp:316-318`) resolves the project-relative owned URI. **Save As
   / Consolidate into a fresh root** does NOT yet copy the owned blob — deferred to
   `editor.import.owned_asset_republish` (Constraint stated, not asserted here).

7. **Encoded clipboard images only in v1; empty/raw is a graceful no-op.**
   `Clipboard::read_image` returns an encoded image (prefer `image/png`, then any
   imdec-decodable mime). A clipboard with **no image**, or with **only raw
   pixels** (no encoded form), yields `nullopt` and `paste_image()` is a no-op
   (no cell, no error dialog, no crash) — re-encoding raw RGBA needs an encoder
   the editor does not vendor (parking-lot).

8. **Two triggers, one testable verb.** The Ctrl+V chord and the "Paste image"
   affordance both call `paste_image()`; the e2e drives it with a scripted
   `Clipboard` fake (no real OS clipboard exists headless) — the `FileDialog`
   fake precedent.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella. `diff-cover --fail-under=90` on changed lines; tests ship with the task.

- **Levelization (`check_levels` clean) — the primary structural assertion.** No
  new component, no new §8 DAG edge, **no new build/link edge, no
  `scripts/check_levels.py` edit**: `mint_owned_asset` is L1 `project`, the
  `classify_detail` test is L1 `scene`, `Clipboard`/Ctrl+V/"Paste image" are
  L4/L3. Confirm `scripts/gate`'s level lint passes and no entry in
  `scripts/check_levels.py:21-60` changes. Paste factories no new kind, so
  `registry.factory("org.arbc.image")` is the A29 registration, unchanged.
- **L1 logic — Catch2 units** (`tests/paste_image_test.cpp`, new file in
  `ace_tests`; moulded on `tests/image_import_test.cpp`):
  - **`mint_owned_asset` content-addresses + dedups (Constraints 3-4):** minting
    the fixture bytes writes a blob under `layout.assets_dir` and returns a
    **project-relative** owned URI (not absolute, not `file://`, under `assets/`);
    minting the **same** bytes again returns the **same URI** and does not
    re-write (the sink's `contains` is true / `put` no-ops); minting **different**
    bytes returns a different URI. A minted URI resolves through a
    `FilesystemAssetSource` rooted at the project to the written bytes.
  - **`classify_detail` owned vs borrowed (Constraint 3):** a cell whose
    `external_asset_ref()` is a project-relative `assets/…` URI reports
    `DetailSource::ReferencedImage`, **`borrowed == false`**, and `native_pixels`
    filled; a cell whose ref is an external absolute path reports
    **`borrowed == true`** (the shipped import case, re-pinned). Table-driven over
    a few URI shapes.
  - **One-action-one-entry (Constraint 5):** inserting the minted owned cell via
    `insert_cell_command(registry,"org.arbc.image",config,placement)` adds ONE
    journal entry, selects the new cell, and a single undo removes it on its same
    `ObjectId`; a no-image paste opens no transaction and leaves the `Document`
    byte-identical.
  - **In-place round-trip (Constraint 6):** `mint_owned_asset` → insert →
    `save_project` (same root) → rebuild-from-canonical reopen restores a live
    `org.arbc.image` cell whose `external_asset_ref()` equals the owned URI and
    whose `bounds()` equals the fixture's native px; the blob resolves from
    `assets/`. Moulded on `tests/reopen_*` / `save_*` fixtures.
  - **GC roots the owned blob (Constraint 4):** a `gc_project_directory` dry-run
    over a project with the pasted cell does **not** list the owned blob as
    orphaned; after paste→undo→save the same blob **is** listed reclaimable. (Uses
    the shipped `project::gc_project` / `arbc::gc_project_directory` from
    `editor.project.gc`.) *If libarbc's GC mark does not walk image
    `params.source` refs, this test surfaces a real library gap — the implementer
    reports it to the parking lot; it is not deferred as an audit task.*
- **Rendered output — golden (REUSE, no new golden):** a pasted owned image and
  an imported borrowed image of the **same fixture** composite to the **same
  pixels** at 1:1 (provenance differs, pixels do not). Reuse
  `tests/goldens/import_image_64x64.rgba8` (added by `editor.import.image`): the
  fixture minted-and-inserted through the paste path, composited via
  `render::render_document_srgb8(doc, w, h, camera)`, matches it byte-exact. No
  new golden is committed (justified by §9 — this is not new rendered output; it
  pins that owning the bytes does not perturb the render).
- **UI e2e — ImGui Test Engine** (`tests/paste_image_e2e_test.cpp`, in
  `ace_shell_test`, driven by widget id, state through `E2EState`, offscreen
  software-GL; modelled on `tests/image_import_e2e_test.cpp`):
  - **Ctrl+V path:** with a scripted `Clipboard` fake returning the fixture's
    encoded bytes, the Ctrl+V chord adds one new cell **selected**, its
    `CellDetail` is `ReferencedImage` + **`borrowed == false`** with
    `native_pixels == fixture size`, placed 1:1 at the focused-pane center.
  - **"Paste image" affordance:** the affordance beside "Place image…" does the
    same through the same verb.
  - **Empty/no-image clipboard (Constraint 7):** the fake returns `nullopt`;
    `paste_image()` adds no cell, raises no error, does not crash.
  - **Owned provenance surfaces:** the Layers/Inspector readout shows **owned**
    (not borrowed) for the pasted cell (the shipped readout, driven through the
    new provenance value).
- **Threading — ASan/TSan.** The paste mutation rides `run_edit` →
  `CanvasView::apply_edit` on the single writer thread (A13); the clipboard read +
  mint run on the UI (event-pump) thread. One assertion appended to the
  `tests/canvas_host_test.cpp` TSan anchor confirms a paste (mint + insert + its
  decode) while the render thread drives the same `Document` is data-race-clean —
  the new cell publishes through the `pin()` seam. No new thread, no new lock.
  Residual Mesa leaks via `tests/lsan.supp`.
- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed
  lines; clang-format + build clean across presets. Tests ship with the task.

**Deferred WBS work.** **`editor.import.owned_asset_republish`** (new leaf, ~1.5d,
`allocate team`, `depends editor.import.paste`; closer registers in the WBS and
wires it into the milestone that gathers `editor.import`, mirroring
`editor.import.consolidate`): on **Save As / Consolidate into a fresh root**, copy
the document's referenced **owned image** blob(s) from the source `assets/` into
the target root's `assets/`, so a pasted image survives a copy to a new root (and
scratch→Save-As persistence). Needed because the image runtime codec writes only
`params.source` (never routing bytes through the sink), so `project::save_project`
against a different root leaves the target `assets/` missing the blob; in-place
Save→reopen of the same project is unaffected. `note` cites this refinement + A30.

## Decisions

- **D-paste-1 — Mint the owned asset EAGERLY at paste time via a new
  `project::mint_owned_asset` helper (content-address → write-if-absent into the
  live `assets/`); reuse the whole `editor.import.image` insert tail (A30 doc
  delta).** The clipboard bytes are hashed and written into the project's
  `assets/` through the shipped `FilesystemAssetSink` (the raster
  `hash_tile`/`tile_blob_uri` model), returning a project-relative owned URI; then
  `image_config(owned_uri, owned_uri, bytes)` → `place_at_native_scale` →
  `insert_cell_command` via `run_edit`, exactly as the borrowed import. *Rationale:*
  the mint is the ONLY new logic — everything else (kind registration, config
  frame, placement, insert/undo/select, classifier, Inspector/Layers) already
  ships; hosting the mint in `project` (which already owns the `AssetSink →
  assets/` write, A13, and names `arbc::`) adds no DAG edge and no link edge; eager
  content-address gives dedup and immediate on-disk presence so an in-place
  Save→reopen round-trips with zero codec change. *Alternative rejected:* mint the
  owned bytes LAZILY in the image codec at save time (route embedded bytes through
  `store_asset` like the raster codec) — a **libarbc `codec_image.cpp` change**,
  the exact fork A29 refused; it also would not help until the first save. *Alternative
  rejected:* copy the pasted bytes to a borrowed external temp file and reuse
  `borrow_asset_file` — makes a pasted bitmap **borrowed** (breakable, GC-exempt),
  directly contradicting D11's owned axis. **Doc delta: A30.**

- **D-paste-2 — Distinguish owned from borrowed in `scene::classify_detail` by
  the URI shape: a project-relative `assets/` URI ⇒ `borrowed=false`, an external
  absolute URI ⇒ `borrowed=true`.** The shipped classifier hard-codes
  `borrowed=true` for any non-empty `external_asset_ref()`; paste adds the owned
  case. *Rationale:* the D11 "bytes where?" axis is user-visible (Layers shows
  owned vs borrowed provenance), and after paste there are cells of both kinds; a
  URI-shape test is the natural, cheap discriminator that matches the tile
  convention (owned assets are project-relative under `assets/`, `tile_blob_uri`)
  and the D13 relativized-owned story — a pasted image is already in the
  consolidated form. *Alternative rejected:* carry an explicit owned/borrowed flag
  on the cell — the library's `Content` exposes only `external_asset_ref()`; adding
  a flag is a per-kind field the generic classifier (A16) avoids, and the URI
  already encodes the answer. *Alternative rejected:* leave paste reporting
  `borrowed=true` — misclassifies owned pixels, breaks the D11 provenance readout,
  and would make GC/consolidate reason wrongly about the cell. **No doc delta
  beyond A30** (which records the classifier extension).

- **D-paste-3 — Clipboard is an L4 `Clipboard` seam mirroring `FileDialog`
  (A12); Ctrl+V + a "Paste image" affordance funnel through ONE gateway verb
  `paste_image()`.** An abstract `Clipboard::read_image() -> optional<{bytes,
  mime}>`, an SDL `SdlClipboard` over `SDL_GetClipboardData`, and a scriptable
  fake for tests; the Ctrl+V chord (beside the undo/redo chords in `dock.cpp`) and
  the affordance (beside "Place image…") both call `paste_image()`. *Rationale:*
  it is the exact `FileDialog` inversion this stream already uses (A12) — one verb
  keeps the chord and the affordance behaviorally identical and gives the headless
  e2e a single driveable entry (no real OS clipboard exists offscreen). D18 leaves
  no menu bar, so "Paste image" joins the existing Insert/Place affordances.
  *Alternative rejected:* read the clipboard inline in the ImGui/SDL loop with no
  seam — strands the paste logic where no e2e can reach it and hard-wires SDL into
  the trigger. *Alternative rejected:* route through ImGui's own clipboard
  callback — it is scoped to text; image mime types need `SDL_GetClipboardData`.
  **No doc delta** (A12 charters the seam; A30 notes it).

- **D-paste-4 — v1 accepts ENCODED clipboard images only; a raw-RGBA-only or
  empty clipboard is a graceful no-op.** `read_image` prefers `image/png`, then
  any imdec-decodable mime; those encoded bytes are the asset, decoded exactly
  like an imported PNG. *Rationale:* paste "behaves like an import" (§8) — import
  takes encoded bytes and imdec decodes them; keeping paste symmetric avoids
  pulling an image **encoder** into the editor (imdec is decode-only), and most
  applications place PNG on the clipboard. A no-op (not an error) on an
  unsupported clipboard is the least surprising behavior. *Alternative rejected:*
  encode raw RGBA to PNG at paste — needs an encoder dependency the editor does
  not vendor and whose choice is a design/dependency judgment, routed to the
  parking lot rather than decided here. *Alternative rejected:* accept raw RGBA as
  the asset bytes directly — `org.arbc.image` decodes via imdec, which does not
  accept headerless raw pixels; it would be an undecodable frame. **No doc delta**
  (§8's "whatever the library decodes" already scopes formats).

- **D-paste-5 — Save As / Consolidate owned-blob copy is a named follow-up
  (`editor.import.owned_asset_republish`), not this leaf.** In-place Save→reopen
  round-trips fully (the blob is already in the live `assets/`); copying the owned
  blob into a **fresh** root's `assets/` is a distinct, editor-side republish step
  the image codec does not perform (it writes only the URI). *Rationale:* it is
  concrete, agent-implementable `project`-layer work (walk the document's owned
  image refs, `put` their blobs into the target sink) but genuinely separate from
  the paste gesture, and scoping it out keeps this leaf at 1.5d while shipping the
  primary paste→save→reopen lifecycle. *Alternative rejected:* fold it into this
  leaf — bloats a 1.5d task with save-path surgery and its own Save-As/Consolidate
  test matrix. *Alternative rejected:* fold it into `editor.import.consolidate` —
  consolidate is borrowed→owned within one project; the republish is owned→fresh-root
  on copy, a different trigger and code path. **Registered as a WBS leaf** (see
  Acceptance criteria).

## Open questions

**Routed to the parking lot (human-judgment, not a WBS leaf):** whether v1's
encoded-clipboard-only scope needs to grow to **raw-RGBA clipboards**, and if so
which **image encoder** to vendor (imdec is decode-only; the editor vendors no
encoder). This is a product + dependency decision — the most defensible v1 call
(encoded-only, graceful no-op on raw) is made under D-paste-4; the growth question
is surfaced in the return summary for the parking lot, not encoded as a
re-examination WBS task.

All implementation-level questions are decided: the mint lives in `project`
(D-paste-1), owned/borrowed is a URI-shape test (D-paste-2), the clipboard is the
A12 seam (D-paste-3), and the Save-As blob-copy is a named follow-up (D-paste-5).

## Status

**Done** — 2026-07-31.

- `src/project/ace/project/import_asset.{hpp,cpp}` — `mint_owned_asset` helper + `OwnedAsset` struct; content-addresses clipboard bytes, writes write-if-absent into live `assets/images/`, returns project-relative owned URI (A30 seam, D-paste-1).
- `src/scene/cell.cpp` — `classify_detail` extended with owned/borrowed URI-shape split: a project-relative `assets/` URI ⇒ `borrowed=false`; an external absolute URI ⇒ `borrowed=true` (D-paste-2).
- `src/app/ace/app/clipboard.hpp` + `src/app/clipboard.cpp` — `Clipboard` abstract seam + `SdlClipboard` over `SDL_GetClipboardData`, mirroring `FileDialog` (A12, D-paste-3).
- `src/app/ace/app/project_gateway.hpp` + `src/app/project_gateway.cpp` — `paste_image` / `can_paste_image` / `set_clipboard` added; `insert_image` tail factored into shared `insert_image_bytes` body (D-paste-1, Constraint 5).
- `src/app/shell.cpp` — `SdlClipboard` wired; Ctrl+V chord added beside undo/redo (D-paste-3).
- `src/dock/ace/dock/dock.hpp` + `src/dock/dock.cpp` — `can_paste_image` / `paste_image` seam + "Paste image" affordance beside "Place image…" + Ctrl+V chord (D-paste-3, Constraint 8).
- `tests/paste_image_test.cpp` — Catch2 units: mint content-address/dedup/resolve, classify owned-vs-borrowed table, one-action-one-entry, in-place save→reopen round-trip; golden REUSE of `import_image_64x64.rgba8` (no new golden).
- `tests/paste_image_e2e_test.cpp` — ImGui Test Engine e2e: Ctrl+V path, "Paste image" affordance, empty-no-op, owned-provenance readout.
- `tests/canvas_host_test.cpp` — `import_paste` TSan anchor added.
- `CMakeLists.txt` — two new test files wired into `ace_tests` and `ace_shell_test`.
- **Deferred (parking lot):** libarbc v0.4.1 GC never sees `assets/images/` blobs — GC-roots-owned-blob unit not landed; see `arbc-gc-blind-to-owned-images` memory + parking-lot entry. Raw-RGBA clipboard / encoder question also surfaced to parking lot.
- **Tech-debt registered:** `editor.import.owned_asset_republish` (~1.5d) — copy owned image blobs into a fresh root's `assets/` on Save As / Consolidate; wired under `editor.import` (gates `milestones.m9_editor`).
