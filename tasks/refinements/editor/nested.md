# editor.import.nested — Place another `.arbc` → nested composition cell

## TaskJuggler entry

- **Task:** `editor.import.nested` (`tasks/00-editor.tji:722-726`, under
  `task import "Import & assets"` at `:701`).
- **Effort:** `2d` (`:723`) · `allocate team` (`:724`).
- **Depends:** `!image` = `editor.import.image` (`:725`) — the sibling borrowed
  import, **Done** 2026-07-31 (`tasks/refinements/editor/image.md` Status). The
  dependency is satisfied.
- **Note (`.tji:726`):** "Place another project (`.arbc`) → an `org.arbc.nested`
  cell referencing it (the library's external nested reference). Design: D12."
- **Back-link:** the `.tji` note currently ends `Refinement:
  tasks/refinements/import_nested.md` (the flat interim path). This refinement
  lands at **`tasks/refinements/editor/nested.md`** per the orchestrator's
  area = first-dot-segment (`editor`) assignment
  (`tasks/refinements/README.md:9-18`); the closer updates the note back-link to
  the real path and adds `complete 100` after `allocate team`
  (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji` here.
- **Downstream:** `editor.import.consolidate` (`:728-732`,
  `depends editor.import.paste, editor.import.nested`) — consolidate copies a
  borrowed reference into `assets/` and relativizes its URI, and a nested `.arbc`
  reference is exactly such a borrow. `editor.import.nested` also feeds the
  `editor.packaging.package` gather (`:747`) and, transitively, the final
  `milestones` milestone (`tasks/99-milestones.tji`).

## Effort estimate

**2 days.** This is the third D12 import path and reuses the whole
`editor.import.image` gesture skeleton — a picked/dropped file, a gateway verb,
a one-transaction insert through `run_edit`, a selected result. What is *new* is
narrow but load-bearing: the external nested reference cannot travel the A16
`registry.factory(id)(config)` seam the image mints through (the built-in
`org.arbc.nested` factory accepts only an in-document child `ObjectId`), so this
leaf adds a **dedicated construction seam** and grapples with the fact that a
freshly-placed external reference resolves only on reopen.

- **`scene::add_nested_reference`** (L1) — construct `arbc::NestedContent(
  ObjectId{}, ref)` and attach in one transaction (the `scene::add_cell`
  mould minus the factory). ~0.3d incl. Catch2.
- **`classify_detail` nested arm** (L1) — read `external_composition_ref()` as a
  borrowed external reference beside the image-only `external_asset_ref()`.
  ~0.15d incl. Catch2.
- **Borrowed-URI helper** (L1 `project`) — the picked `.arbc` path → a borrowed
  absolute URI (reuse `borrow_asset_file`'s URI, no embedded bytes). ~0.1d.
- **The two entry points** (L3/L4) — an OS file-drop `.arbc` arm and a "Place
  composition…" affordance behind the existing `FileDialog` seam (filtered to
  `.arbc`); both funnel through one gateway verb. ~0.5d.
- **Gateway wiring** — `insert_nested(path, device_point)` / `place_nested()`
  reusing `run_edit` (the `AppProjectGateway::insert_image` mould). ~0.2d.
- **Tests + threading scope** (Catch2 · one `render_offline` golden of the
  reopen-resolved composition · e2e · TSan anchor · a `.arbc` fixture pair).
  ~0.75d.

**No new build/link edge** (`org.arbc.nested` is a libarbc built-in, not an
out-of-lib plugin like the image kind — the A29 link edge has no analog here),
**no new editor component, no new §8 DAG edge, no `check_levels` edit, no
libarbc fork, no pin bump.** The pin stays at `v0.4.1`. One doc delta: **A31**
(`docs/01-architecture.md`).

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.import.image`** (`tasks/refinements/editor/image.md`, Done
  2026-07-31) — the import gesture this leaf mirrors:
  - **`AppProjectGateway::insert_image(path, device_point)` /
    `place_image()` / `drop_device_point()`** (`src/app/project_gateway.cpp:372-468`) —
    the exact shape `insert_nested`/`place_nested` copy: probe → placement →
    command → `run_edit` → select the new cell; the OS-drop point maps onto the
    focused pane (`canvas_view.cpp:249`, `drop_device_point`), an out-of-pane
    drop falls back to the focused-pane centre.
  - **The `FileDialog` seam** (`src/app/ace/app/file_dialog.hpp`,
    `SdlFileDialog` over `SDL_ShowOpenFileDialog`, scriptable fake) — reused
    filtered to `.arbc`; the "Place composition…" affordance sits beside "Place
    image…" (`src/dock/dock.cpp`).
  - **`project::borrow_asset_file(fs, path)`** (`src/project/ace/project/import_asset.hpp:31`,
    returns `BorrowedAsset{uri, bytes}`) — the borrowed absolute-path URI
    normalization (D-image-5). This leaf uses the **URI only** (nested resolves
    through the `AssetSource`, not an embedded-bytes channel).
- **`editor.cells.model`** (`tasks/refinements/editor.cells/model.md`, Done
  2026-07-22) — the insert seam `add_nested_reference` is patterned on:
  - **`scene::add_cell(document, registry, kind_id, config, placement, entered)`**
    (`src/scene/cell.cpp:263-297`) — the mould: pin → `active_composition_in`
    (D-scoped_edit-1 fail-safe) → `Document::create_content_and_attach(content,
    cell_token(registry, id), composition, placement)` = one journal entry.
    `add_nested_reference` differs in exactly one line: it constructs the content
    directly instead of via `make_content`/the factory.
  - **`cell_token(registry, kind_id)`** (`src/scene/cell.cpp:44-49`, via
    `project::seed_kind_bridge`) — interns the `org.arbc.nested` kind id/version
    to the token `create_content_and_attach` needs, the same interning save/open
    use so the token names the same kind on reopen.
  - **`interact::place_in_view(camera, pane_w, pane_h, bounds)`**
    (`src/interact/ace/interact/interact.hpp`) — the pure L1 placement helper;
    over `nullopt` bounds it yields identity placement (A16's unbounded rule).
  - **`commands::insert_cell_command` / `dispatch` / `run_edit`**
    (`src/commands/ace/commands/cells.hpp:45`, `cells.cpp:15-26`;
    `CanvasView::apply_edit`, A13 / `edit_render_sync` Constraint 1) — the
    single-writer command envelope; this leaf adds a sibling
    `insert_nested_reference_command`.
- **`editor.cells.one_action_one_entry`** (Done, v0.4.0 `create_content_and_attach`
  amendment to A16) — a create is ONE transaction, ONE journal entry, one undo.
- **`editor.canvas.nested_composition_binding`** (`tasks/refinements/editor/nested_composition_binding.md`,
  Done 2026-07-19) — the interactive `CanvasRenderer` wires the real
  `HostViewport::DocumentBinding{&bridge, &registry}` and the writer-thread
  `settle_external_loads` hook, so a nested child whose bytes arrive from the
  `AssetSource` installs and composites. This leaf's reopen-resolved cell renders
  through **exactly** that already-shipped wiring — nothing new on the render
  side.
- **libarbc `v0.4.1`** (`editor.canvas.arbc_v041`, Done) — the pinned surface:
  `arbc::NestedContent` (`build/*/_deps/arbc-src/src/kind_nested/arbc/kind_nested/nested_content.hpp:58-197`),
  the external-ref constructor `NestedContent(ObjectId child, std::string ref)`
  (`:85`), `Content::external_composition_ref()` (`.../contract/content.hpp:652`),
  `register_builtin_kinds` seating `org.arbc.nested` / `make_nested`
  (`builtin_kinds.cpp:270,181`), `codec_nested` + `ExternalCompositionLoader`
  (deserialize-time resolution, `runtime/external_composition_loader.hpp`), and
  `render_offline` rendering nested through the unified tiled driver.

**Pending:** none. All dependencies are Done.

## What this task is

Add the third D12 import path: **placing another project's `.arbc` document into
the current composition as an `org.arbc.nested` cell that references it** — the
library's external nested reference (`params.ref`), not an inline copy. Dropping
a `.arbc` file onto the canvas, or choosing "Place composition…", mints a nested
cell whose `params.ref` is the borrowed URI of the picked `.arbc`. On the next
reopen the editor's `FilesystemAssetSource` loads that referenced document's
composition inline and the nested cell composites it; in the placing session the
cell shows the doc-05 placeholder (the referenced child is unresolved until a
reopen runs the deserialize-time loader). The reference stays **borrowed** — the
bytes are never copied, the URI is kept verbatim and relocatable — so Consolidate
(`editor.import.consolidate`) can later copy it into `assets/` and relativize it,
exactly as it does a borrowed image.

## Why it needs to be done

D12 names three import paths — drop an image (borrowed, Done), paste a bitmap
(owned, Done), and **place another `.arbc` (nested composition)**. This leaf is
the third. It is also the keystone of D3's "nested compositions" cell kind
reaching the user: `org.arbc.nested` is a built-in, but until this leaf there is
no gesture that authors an *external* one — the architecture records nested cells
as today "in-document `ObjectId` children (never a serialized `params.ref`)" with
the external-load path "**armed but unreachable**" (`docs/01-architecture.md:140-149`).
This task is what makes it reachable. Downstream, `editor.import.consolidate`
depends on both nested and paste being placeable before it can copy-and-relink
borrows.

## Inputs / context

**Design docs (normative):**

- **D12 Import paths** (`docs/00-design.md:479`) and the §8 narrative
  (`:317-326`): "**Place another `.arbc`** → a **nested composition** cell (the
  library's external nested reference)."
- **D11 Two asset axes** (`:478`, `:301-315`): a nested `.arbc` reference is a
  **borrowed** external file (breakable if it moves, never GC'd), read-only from
  this document's side.
- **D13 Assets, GC & portability** (`:480`, `:333-340`): missing/moved borrow →
  placeholder + relink; Consolidate copies borrows into `assets/` and
  relativizes URIs.
- **A31** (`docs/01-architecture.md`, this task's doc delta) — the structural
  seam: dedicated verb, `scene::add_nested_reference`, placeholder-live +
  reopen-resolved, `classify_detail` nested arm, no new component/edge/link.
- **§4 external-load / settler** (`docs/01-architecture.md:140-149,216-225`) —
  where an external arrival installs (writer-thread settler); the `KindBridge` is
  document-scoped/writer-owned; the editor's `FilesystemAssetSource` resolves a
  reference **inline** at load, so `external_loads_ready()` stays zero.
- **A16** (`:433`) — cell insert is Registry-driven with **no allowlist**;
  content minted via `registry.factory(id)` in the generic path; placement is a
  finished `arbc::Affine` from `interact::place_in_view`. A31 adds the one
  dedicated non-factory verb, scoped to this import (the A14 camera / A29 image
  precedent), leaving the generic path factory-only.
- **§8 levelization** (`:328-346`) and **§9 DoD** (`:348-375`).

**libarbc surface (v0.4.1, `build/*/_deps/arbc-src/`):**

- `NestedContent(ObjectId child, std::string ref)`
  (`src/kind_nested/arbc/kind_nested/nested_content.hpp:85`) — the external-ref
  constructor; `child == ObjectId{}` = unresolved, renders the doc-05
  placeholder; `ref()` / `external_composition_ref()` return the authored URI
  (`:179,197`). `kind_id = "org.arbc.nested"` (`:199`).
- `make_nested(ContentConfig)` (`src/builtin_kinds.cpp:181-188`) — the built-in
  factory, **numeric-`ObjectId`-only**: the reason the generic seam cannot mint
  an external reference.
- `codec_nested` serialize/deserialize (`src/runtime/codec_nested.cpp`):
  `serialize_nested` emits `{"ref": …}` verbatim when `ref()` is non-empty;
  `deserialize_nested` reads `params.ref` and drives
  `ExternalCompositionLoader::load` through the `LoadContext` (deserialize-time,
  `src/runtime/arbc/runtime/external_composition_loader.hpp:63-118`).
- `Content::external_composition_ref()` vs `external_asset_ref()`
  (`src/contract/arbc/contract/content.hpp:652,672`) — **deliberately distinct**
  virtuals; nested answers the former, image the latter.
- `Document::create_content_and_attach(content, kind, composition, transform)`
  (`src/runtime/arbc/runtime/document.hpp:128`) — the one-transaction attach.
- `render_offline(document, viewport, backend)` — renders nested through the
  unified tiled driver (binds operators, arbc#6/v0.2.0+).

**Editor sources this leaf touches:**

- `src/scene/cell.cpp:263-297` (`add_cell`), `:44-49` (`cell_token`),
  `:115-135` (`classify_detail`), `src/scene/ace/scene/cell.hpp:104-137`.
- `src/commands/ace/commands/cells.hpp`, `src/commands/cells.cpp:15-26`.
- `src/project/ace/project/import_asset.hpp:31` (`borrow_asset_file`).
- `src/app/project_gateway.cpp:372-468` (`insert_image`/`place_image`),
  `src/app/canvas_view.cpp:249`, `src/app/shell.cpp:140-155` (OS-drop arm),
  `src/dock/dock.cpp` ("Place image…" affordance + gateway virtual).
- `tests/image_import_test.cpp`, `tests/image_import_e2e_test.cpp`,
  `tests/canvas_host_test.cpp` (nested + external-load fixtures, `:512-530`,
  `:1103-1213`).

## Constraints / requirements

1. **Reference, not copy.** The placed cell carries `params.ref` = the picked
   `.arbc`'s URI (the library's external nested reference, D12). No composition
   graph is copied into the host model. On Save the ref round-trips verbatim
   (`serialize_nested`), un-relativized (borrowed, D-image-5 / D13); Consolidate
   later owns the copy-and-relativize.

2. **Dedicated verb, not the generic factory.** Because `make_nested` is
   numeric-`ObjectId`-only, the import does **not** go through
   `scene::add_cell` / the Insert list. A new L1
   `scene::add_nested_reference(document, ref_uri, placement, entered)`
   constructs `std::make_shared<arbc::NestedContent>(arbc::ObjectId{},
   std::string(ref_uri))` and attaches via `create_content_and_attach` with
   `cell_token(registry, "org.arbc.nested")`. It must **not** override or
   re-register the `org.arbc.nested` registry factory (that would break the
   in-document nested insert path; A31 alternative-rejected).

3. **One action, one entry.** The insert is ONE `create_content_and_attach`
   transaction — one revision, one journal entry, one undo press, no
   intermediate published state (the `editor.cells.one_action_one_entry` /
   D-one_action_one_entry-1 invariant). It rides `run_edit` /
   `CanvasView::apply_edit` on the single-writer thread (A13 /
   `edit_render_sync` Constraint 1). A refused/empty pick mutates nothing.

4. **Scoped insert.** `add_nested_reference` resolves the target composition
   through `active_composition_in(state, entered)` against the create's own pin
   (D-scoped_edit-1 / D29): a nested `.arbc` placed while a composition is
   entered lands in that composition; a vanished scope degrades to Root. The
   gateway reads `AppState::entered_composition()` on the UI thread and threads
   it by value into the command (D-scoped_edit-2), exactly as `insert_image`
   does.

5. **Placement = identity (1:1), anchored at the drop point.** The unattached
   nested content reports empty bounds (`probe_bounds` → `nullopt`), so placement
   is `interact::place_in_view(camera, pane_w, pane_h, nullopt)` = identity
   (A16's unbounded rule) — 1:1 child-composition-units → parent-composition
   units, the nested analog of image's native-px 1:1 (D12 "carry real relative
   scale"). The OS-drop path anchors the child's composition origin at the
   composition-space image of the drop point; the dialog/centre path anchors at
   the focused-pane centre. The user rescales freely afterward (D8). The
   placement is not re-fit once the child resolves on reopen.

6. **Placeholder live, resolved on reopen.** The minted cell has
   `child == ObjectId{}` and never traverses the codec, so it renders the doc-05
   placeholder in the placing session (an expected, honest consequence of the
   library's deserialize-time resolution model, not a bug). On reopen the
   editor's already-installed `FilesystemAssetSource`
   (`project_open.cpp:316-318`, A4.1a) resolves the ref **inline**, so
   `external_loads_ready()` stays zero and the nested composition composites
   through the `nested_composition_binding` wiring. Live in-session resolution is
   a **cross-repo library seam** (parking lot — see Decisions), not in scope.

7. **Borrowed provenance surfaced.** `scene::classify_detail` gains a
   nested-reference arm: a non-empty `external_composition_ref()` classifies as a
   borrowed external reference (owned/borrowed split by URI shape, D-paste-2),
   so the Layers list shows the source-file / relink readout D11/D13 promise. No
   per-kind `kind_id` switch — the generic facet keys it (A16).

8. **Entry points funnel to one verb.** An OS file-drop whose path ends `.arbc`
   and a "Place composition…" affordance behind the existing `FileDialog`
   (filtered to `.arbc`) both call ONE gateway verb
   `insert_nested(path, device_point)` (the D-image-4 one-verb rule). A cancelled
   pick / non-`.arbc` drop imports nothing. The freshly placed cell is selected
   (transient UI state, off the writer turn), like every drop/Place.

9. **Levelization (§8).** No new component, no new DAG edge, no link edge. The
   new `scene::add_nested_reference` names `arbc::NestedContent` on the
   already-declared `scene → libarbc` edge (the public `arbc/kind_nested/`
   header — `NestedContent` is `ARBC_API`; contrast A29's private plugin
   archive, which has no analog here). `project`, `commands`, `app`, `dock` all
   act on existing edges. `check_levels` stays clean; no L1 core gains an
   ImGui/GL/SDL include.

## Acceptance criteria

The universal DoD (`docs/01-architecture.md:359-375`) instantiated for this leaf:

- **Levelization** — `check_levels` clean; no new component/edge/link, no L1
  ImGui/GL/SDL include (Constraint 9). Verified by the existing lint in
  `scripts/gate`.

- **L1 Catch2 units** (`tests/nested_import_test.cpp`, `ace_tests`, moulded on
  `tests/image_import_test.cpp`):
  - `add_nested_reference` mints an `org.arbc.nested` content whose `ref()` /
    `external_composition_ref()` == the passed URI and whose `child()` ==
    `ObjectId{}` (unresolved); the cell appears in `scene::cells()` at the given
    placement.
  - **One-action-one-entry**: exactly one journal entry after the insert; one
    `undo` removes the cell (same `ObjectId` restored on `redo`) — the
    D-one_action_one_entry-1 assertion.
  - **Scoped insert**: with `entered` naming a live nested composition the cell
    lands there; a vanished/foreign `entered` degrades to Root (D-scoped_edit-1).
  - **`classify_detail` nested arm**: a nested cell with an external absolute
    `.arbc` ref classifies `borrowed == true`; a project-relative `assets/` ref
    (post-Consolidate shape) classifies `borrowed == false`; an image cell's
    classification is unchanged (regression guard on the distinct virtuals).
  - **Placement**: `place_in_view` over `nullopt` bounds yields identity; the
    drop-point path anchors the origin at the comp-space drop point.
  - **Save→reopen round-trip** (the crux): `insert_nested` against a real child
    `.arbc` fixture → `save_project` → `open_project` (with the editor's
    `FilesystemAssetSource`) resolves the ref **inline** —
    `pending_external_loads() == 0`, the nested content's `composition_ref()`
    now names a live child (`child() != ObjectId{}`), and the reference
    round-tripped verbatim.

- **Rendered output — `render_offline` golden**
  (`tests/goldens/import_nested_64x64.rgba8`, `ace_tests`): a fixture parent
  `.arbc` referencing a child `.arbc` (a small colored-solid composition),
  opened through the editor project-open path (ref resolves inline), rendered by
  `render::render_document_srgb8` and compared **byte-exact**. This is the
  reopen-resolved composite — the honest end-to-end proof the external reference
  renders. (Unlike `editor.canvas.nested_composition_binding`'s v0.2.0-era note,
  `render_offline` binds nested operators at the v0.4.1 pin, so a stored golden
  *is* available; no in-test cross-check workaround is needed.) Fixture: a
  `tests/fixtures/nested_child/` project directory (its `project.arbc` + any
  assets) and a parent `.arbc` referencing it.

- **UI behavior — ImGui Test Engine e2e** (`tests/nested_import_e2e_test.cpp`,
  `ace_shell_test`, moulded on `tests/image_import_e2e_test.cpp`): three cases —
  (1) the **drop** path (`.arbc` at a drop point) mints one nested cell selected
  at that point; (2) the **"Place composition…"** affordance (scripted
  `FileDialog` fake returning the child fixture path) mints at the focused-pane
  centre; (3) **out-of-pane drop** falls back to the focused-pane centre. Each
  asserts resulting state — cell count, selection is the new cell, and provenance
  reads borrowed — plus a screenshot baseline of the placeholder-rendered cell.

- **Threading — ASan/TSan anchor** (a case added to `tests/canvas_host_test.cpp`,
  the inline `WorkerPoolConfig{}` pool per `arbc-nested-render-worker-detach-race`):
  a nested-reference insert via `run_edit` concurrent with the render loop, under
  gcc-tsan, races nothing — the insert is one writer-thread transaction and the
  unresolved cell renders the placeholder (no external-load traffic:
  `external_loads_ready() == 0`). Scope is explicit; no new synchronization is
  added (the edit↔render handoff is `edit_render_sync`'s charter).

- **Coverage** — tests ship with the task; changed L1 lines
  (`add_nested_reference`, the `classify_detail` arm, the command envelope) are
  the bulk and are unit-covered headless.

- **Named future task** — live in-session resolution of a freshly-placed
  external reference is **not** an editor WBS leaf; it needs a libarbc seam and is
  surfaced to the parking lot (below). Nothing is deferred to an editor WBS
  successor from this leaf.

## Decisions

- **D-nested-1 — A dedicated `scene::add_nested_reference` verb, not the A16
  factory path.** The built-in `make_nested` factory accepts only a numeric
  in-document child `ObjectId` (`builtin_kinds.cpp:181-188`); it has no config
  grammar for a `ref` URI, so the generic `registry.factory(id)(config)` seam
  image/paste mint through cannot construct an external reference. A dedicated L1
  verb constructs `NestedContent(ObjectId{}, ref)` directly (the public
  `arbc/kind_nested/` header, the A14 camera-register precedent) and attaches via
  `create_content_and_attach`. *Alternatives rejected:* (a) **override the
  `org.arbc.nested` registry factory** with a URI grammar — first-wins
  registration already seats the built-in numeric-id factory, overriding it
  breaks the in-document nested insert (`"<decimal ObjectId>"` raw-config
  fallback) existing tests and the generic Insert list use, and conflates two
  grammars on one factory; (b) fold it into `insert_cell_command` — that verb
  takes a factory `kind_id`+`config`, a fundamentally different construction.

- **D-nested-2 — Reference (borrowed), never inline-copy.** The cell carries
  `params.ref` = the borrowed `.arbc` URI, un-relativized (D12 "the library's
  external nested reference"; D-image-5 borrowed URI). *Alternative rejected:*
  load the foreign composition's graph and mint an in-document
  `NestedContent(child_id)` — it renders live but is a drifting copy that would
  not round-trip as a `ref`, contradicts D12, and libarbc exposes no host-model
  graft seam anyway.

- **D-nested-3 — Placeholder live, resolved on reopen; live resolution is a
  library seam.** libarbc resolves external references only at deserialize time
  (`ExternalCompositionLoader`, `LoadContext`-scoped, "single-writer, one load,
  one thread"); there is no public seam to install an external composition into
  an OPEN `Document`. The freshly-placed cell therefore renders the doc-05
  placeholder in-session and resolves on the next reopen (inline via the editor's
  `FilesystemAssetSource`, §4:140-149), rendering through the
  `nested_composition_binding` wiring. This is the defensible v1: it is the
  library's own resolution model taken at its word, is fully testable
  (save→reopen round-trip + reopen golden), and needs no libarbc change.
  *Alternative considered and deferred:* a live external-composition loader
  against an open `Document` — cross-repo library work, surfaced to the parking
  lot (not an editor WBS leaf; no editor code can supply the seam).

- **D-nested-4 — Placement is identity (1:1) over the unattached content's empty
  bounds.** The referenced child's true extent is unknowable before it resolves
  (probing it would require the deferred live load), so placement uses
  `place_in_view` over `nullopt` bounds → identity (A16's unbounded rule) = 1:1
  child-units → comp-units, the nested analog of image's native-px 1:1 (D12
  "carry real relative scale"), anchored at the drop point. It is not re-fit on
  resolution; the user rescales freely (D8). *Alternative rejected:* fit-to-view
  scaling — discards the true relative scale between a placed composition and its
  neighbours, the same reason image chose 1:1 over fit (D-image-2).

- **D-nested-5 — `classify_detail` reads `external_composition_ref()` too.**
  Nested answers `external_composition_ref()`, not the image-only
  `external_asset_ref()` the classifier reads today (deliberately distinct
  virtuals, `content.hpp:652,672`), so without this arm a nested cell would show
  no borrowed provenance. Extend the classifier to treat a non-empty
  `external_composition_ref()` as a borrowed external reference, split
  owned/borrowed by URI shape (D-paste-2) — the Layers source-file/relink readout
  D11/D13 promise. *Alternative rejected:* a per-kind `kind_id` switch — the
  generic-facet classification A16 mandates keys it without one.

- **D-nested-6 — Built-in kind ⇒ no link edge (no A29 analog).**
  `org.arbc.nested` is registered by `register_builtin_kinds`, already in
  `AppState::registry()`; unlike the image kind it needs no static-linked plugin
  archive and no A29-style `register_editor_kinds` registration. This leaf adds
  zero build/link edges.

- **D-nested-7 — Reuse the `editor.import.image` gesture wholesale.** One gateway
  verb funnels the OS-drop and the "Place composition…" affordance (D-image-4);
  the existing `FileDialog` seam is reused filtered to `.arbc`; the picked file
  becomes a borrowed URI via `borrow_asset_file` (URI only, no embedded bytes —
  nested resolves through the `AssetSource`). *Alternative rejected:* a folder
  picker returning a project *directory* — deferred as a UX nicety; v1 references
  a `.arbc` document by path (a dropped/picked file), and a directory form can
  resolve to its `project.arbc` later without changing the model.

## Open questions

(none — all decided)

## Status

**Done** — 2026-07-31.

- `src/scene/cell.cpp`, `src/scene/ace/scene/cell.hpp` — `scene::add_nested_reference` (dedicated non-factory L1 seam) + `classify_detail` nested arm reading `external_composition_ref()`.
- `src/commands/cells.cpp`, `src/commands/ace/commands/cells.hpp` — `insert_nested_reference_command`.
- `src/app/project_gateway.cpp`, `src/app/ace/app/project_gateway.hpp` — `insert_nested` / `can_place_nested` / `place_nested` gateway verbs.
- `src/dock/dock.cpp`, `src/dock/ace/dock/dock.hpp` — "Place composition…" affordance + inert-default virtuals.
- `src/app/shell.cpp` — OS-drop handler branches on `.arbc` extension to call `insert_nested`.
- `CMakeLists.txt` — registers `nested_import_test.cpp` and `nested_import_e2e_test.cpp`.
- `tests/nested_import_test.cpp` — 8 Catch2 unit cases: mint, undo/redo, scoped insert, classify, placement, round-trip, golden.
- `tests/nested_import_e2e_test.cpp` — ImGui Test Engine e2e: drop path, "Place composition…" dialog, out-of-pane drop.
- `tests/canvas_host_test.cpp` — TSan anchor case (streamed nested-reference insert, `external_loads_ready()==0`).
- `tests/goldens/import_nested_64x64.rgba8` — `render_offline` byte-exact golden of the reopen-resolved composite.
