# editor.import.consolidate — Consolidate project (borrowed → owned + relativize URIs) + relink missing borrows

## TaskJuggler entry

`tasks/00-editor.tji:729-734` — `task consolidate "Consolidate project (borrowed
-> owned + relativize URIs) + relink missing borrows"` under `editor.import`.
Effort `2.5d`, `allocate team`, `depends editor.import.paste,
editor.import.nested`. The block has **no** `complete 100` yet. The `note`
(`:733`) cites **D11/D12/D13**, **§8** (Import & assets: Consolidate,
missing/moved → placeholder + relink) and **D15** (consolidate reversible → a
dispatched transaction), and back-links a **Source-of-debt**
(`tasks/refinements/editor/gc.md`) but not yet a landing path — this task was
split out of `editor.project.gc` (see that refinement's **D-gc-4**). Per the
ritual in `tasks/refinements/README.md:47-68` the closer appends
`Refinement: tasks/refinements/editor/import.consolidate.md` to the `note` when
this leaf lands, exactly as `open.md` / `save.md` / `gc.md` did.

Downstream: `consolidate` sits under the `editor.import` group, which milestone
**M9E** depends on directly (`tasks/99-milestones.tji:6-8`), so it is wired into
M9E by membership — no extra `depends` edge is needed.

## Effort estimate

**2.5 days** (from the `.tji`), for the **Consolidate (borrowed images)** verb
plus **relink** (locate-file), plus the full DoD matrix. The heavy lifting is
already shipped: the byte-copy primitive (`project::mint_owned_asset`), the
owned/borrowed URI-shape classifier (`scene::classify_detail`), the single-writer
dispatch seam (`commands::dispatch` inside `run_edit`), and the confirm-modal +
`ProjectGateway` rail pattern (from `gc`). This leaf is thin wiring in four
familiar places plus the tests, which dominate the estimate:

- **L1 `commands`** — a new `consolidate_project(AppState&, bool preview)`
  orchestrator beside `gc_project` (`app_state.hpp:478`) that enumerates borrowed
  image cells, copies their bytes into `assets/` via `mint_owned_asset` (on
  commit), and dispatches **one** batched reversible URI-rewrite `Command`; plus a
  `relink_cell(AppState&, ObjectId, path)` single-cell verb.
- **L1 `commands/cells`** — a `consolidate_cells_command` (batched
  delete+re-insert with rewritten owned URIs) and a `relink_cell_command`
  (single-cell re-point), modelled on the existing batch verbs
  (`remove_cells_command`, `transform_cells_command`, `cells.hpp:104-144`).
- **L3 `dock`** — a `ProjectGateway::consolidate(bool preview)` virtual returning
  a dock-local `ConsolidateSummary` POD + a **Consolidate project…###consolidate**
  `Selectable` beside **Clean up…###gc** (`dock.cpp:320`) with a preview/confirm
  modal cloning `draw_gc_modal` (`dock.cpp:57`).
- **L4 `app`** — `AppProjectGateway::consolidate(...)` drives the `commands`
  orchestrator; a per-borrow **Relink…** affordance in the inspector Source block
  (`inspector_panel.cpp:110`) using the `FileDialog` seam.

**No new component, no new DAG edge, no new library machinery, no new external
dependency, and no doc delta** — the scope split (images now; nested `.arbc`
bundling and OS "reveal" later) is a WBS **sequencing** call the closer owns, not
a design change (D-consolidate-3/4).

## Inherited dependencies

**Settled (from `editor.import.image`, `editor.import.paste`,
`editor.import.nested`).** Everything Consolidate reads and rewrites already
ships and is trusted:

- **The borrowed URI is stored verbatim, un-relativized, precisely so Consolidate
  can fix it later.** `project::borrow_asset_file` (`src/project/import_asset.cpp:27`)
  normalizes an imported file to an absolute-path URI and embeds its bytes; its
  header comment states the contract outright: *"NOT copied into `assets/` and NOT
  relativized here — Consolidate does both later, reversibly (D13)"*
  (`src/project/ace/project/import_asset.hpp:19-20`). Image cells carry that URI in
  the opaque config `params.source` (`image_config(authored, resolved, bytes)`,
  `src/app/project_gateway.cpp:377`; library shape
  `build/dev/_deps/arbc-src/plugins/image/arbc/kind_image/image_content.hpp:36`).
  Nested cells carry it in `NestedContent::d_ref`
  (`.../kind_nested/nested_content.hpp:85`).
- **The owned/borrowed discriminator is a shipped URI-shape test.**
  `scene::is_owned_asset_ref` (`src/scene/cell.cpp:112`) — a project-relative
  `assets/` URI is **owned**, an external absolute / `file://` URI is **borrowed**
  — drives `scene::classify_detail` (`src/scene/cell.cpp:117-135`), which sets
  `CellDetail.borrowed` (`:135`). The comment at `cell.cpp:130` explicitly names
  *"(post-Consolidate) is owned"* as the target transition. **Consolidate's
  read-side is exactly `detail.borrowed == true`.** This is the D-paste-2
  discriminator, reused verbatim by D-nested-5.
- **The byte-copy-into-`assets/` primitive already exists.**
  `project::mint_owned_asset(fs, layout, bytes, ext)` (`src/project/import_asset.cpp:50`)
  content-addresses bytes (`arbc::hash_tile`), builds a project-relative owned URI
  `assets/images/<xx>/<hash>[.ext]` (`k_owned_images_base = "assets/images/"`,
  `:23`), and writes **write-if-absent** through `FilesystemAssetSink`
  (`src/project/save.cpp:87-105`), deduping by content name. This is the exact call
  `paste_image` (`src/app/project_gateway.cpp:470-489`) already makes; Consolidate
  runs it per borrowed image (feeding it the bytes `borrow_asset_file` already
  embedded). Consolidated images thus become **owned** images under
  `assets/images/`, identical in provenance to pasted ones (A30).
- **The single-writer dispatch seam.** `commands::dispatch(AppState&, const
  Command&) -> DispatchOutcome` (`src/commands/ace/commands/app_state.hpp:335`) is
  the one-command-one-journal-entry seam; every edit is wrapped in
  `run_edit([&]{ dispatch(app_state_, command); })`
  (`src/app/project_gateway.cpp:399`), which posts onto the document's one writer
  thread via `CanvasView::apply_edit` (A4.1b, `docs/01-architecture.md:189-225`;
  `run_edit` seam `src/app/ace/app/project_gateway.hpp:295`). Batched multi-object
  edits fold into **one** entry / one undo — the shipped `remove_cells_command` /
  `transform_cells_command` pattern (`src/commands/ace/commands/cells.hpp:104-144`).
- **The rail + confirm-modal + gateway pattern.** `editor.project.gc` proved the
  exact shape Consolidate reuses: a `ProjectGateway` pure virtual returning a
  dock-local POD (`GcSummary`, `src/dock/ace/dock/dock.hpp:34,209`), a `Selectable`
  in the project overflow menu (`src/dock/dock.cpp:320`, beside **Save As…**
  `:306`), a preview→confirm→commit modal (`draw_gc_modal`, `dock.cpp:57`), and an
  L4 override mapping the runtime report to the dock POD
  (`src/app/project_gateway.cpp:197`).
- **The file-picker seam** (for relink). `FileDialog` / `SdlFileDialog`
  (`src/app/ace/app/file_dialog.hpp:18,36`), async `show(Callback)`, instantiated
  at `src/app/shell.cpp:493` — the same seam image/nested import use for their
  pickers.
- **The inspector Source block** where a per-borrow **Relink…** button lands:
  `src/app/inspector_panel.cpp:110` renders `scene::describe_detail_source`;
  `src/app/layers_panel.cpp:29` surfaces owned-vs-borrowed per row off
  `detail.borrowed`.
- **The missing-borrow → placeholder half already ships from the library's load**
  (D-open-6, `src/project/ace/project/project.hpp:75-77`): a broken borrow resolves
  to a placeholder and the rest of the project keeps working (§8, `00-design.md:333-335`).
  Relink supplies the recovery half — re-pointing the URI.

**Settled (from libarbc, fetched under `build/dev/_deps/arbc-src/`).** The
primitives Consolidate composes, none of which is a `consolidate`/`relink`
entry point (verified: no such symbols exist in the public headers):

- Read the borrowed URI: `arbc::Content::external_asset_ref()`
  (`.../contract/arbc/contract/content.hpp:704`) for images,
  `external_composition_ref()` (`.../kind_nested/nested_content.hpp:186`) for
  nested — both **read-only**, no write channel.
- Copy bytes into `assets/`: `arbc::SaveContext::store_asset(relative_uri, bytes)`
  (`.../serialize/arbc/serialize/save_context.hpp:154`) + `arbc::hash_tile` /
  `arbc::tile_blob_uri` — exactly what `mint_owned_asset` already wraps.
- Re-resolve on reopen: `arbc::FilesystemAssetSource` resolves both owned-relative
  and borrowed-absolute URIs; `install_asset()` (`content.hpp:706`) is the
  late-arrival bytes channel.

**Pending (this leaf owns them).** L1 `commands::consolidate_project` +
`commands::relink_cell`; L1 `commands/cells` `consolidate_cells_command` +
`relink_cell_command`; the `dock::ProjectGateway::consolidate()` virtual +
`ConsolidateSummary` POD + rail entry + preview/confirm modal; the L4
`AppProjectGateway::consolidate()` override and the inspector **Relink…**
affordance.

## What this task is

Realize D13's **Consolidate** verb and §8's **relink**: turn a project whose
pixels are *borrowed* (external files pointed at by absolute URI) into a
*self-contained, movable bundle*, and give a broken borrow a recovery path.
Concretely, for **borrowed image cells** (`detail.borrowed == true`):

- **Consolidate (commit)** copies each borrowed image's bytes into the project's
  own `assets/images/` via `mint_owned_asset` (content-addressed, write-if-absent,
  dedup'd) and **rewrites** the cell's URI from the external absolute path to the
  project-relative owned URI — as a **single reversible library transaction** (one
  journal entry, one undo; D15/§9). Because the library exposes no in-place
  config/ref setter (image `params.source` is opaque, nested ref is immutable), a
  URI rewrite is a **delete + re-insert** of the cell preserving its transform and
  z-order (D-consolidate-2) — which is exactly why consolidate is naturally
  reversible via the journal. The result is a bundle that renders byte-identically
  and survives being moved to a new machine with the original sources gone.
- **Consolidate (preview)** computes the identical plan without writing or
  dispatching — it reports the count of borrowed images and total bytes to copy
  (plus the count of borrowed **nested** references left external, D-consolidate-3)
  so the user confirms the scope before the copy runs (D-consolidate-5).
- **Relink** surfaces a per-borrow **Locate file…** affordance in the inspector
  that re-points a moved or missing borrow's URI to a user-picked path — dispatched
  through the **same** reversible URI-rewrite command (`relink_cell_command`).
  Relink works for both image and nested borrows (it is a pure re-point, no
  bundling), and complements the placeholder the library already shows for a broken
  borrow (D-open-6).

The rail exposes **Consolidate project…** beside **Clean up…** in the project
overflow menu, gated behind a preview/confirm modal. The verb is dispatched on
the document's one writer thread like any edit (A4.1b), and the copied blobs live
under `assets/images/` exactly as pasted-owned images do (A30).

**Explicitly out of scope, split to named future tasks (D-consolidate-3/4):**
(1) *borrowed nested `.arbc`* consolidation — a recursive bundle copy of a nested
project's transitive owned+borrowed assets — split to
`editor.import.consolidate_nested`; (2) *reveal in file manager* — an OS
"show the file" affordance needing a new cross-platform `platform` seam — split to
`editor.import.reveal_in_file_manager`.

## Why it needs to be done

By default nothing is duplicated: borrows stay external (§8, `00-design.md:337-340`),
and `borrow_asset_file` deliberately leaves the URI absolute and un-relativized,
naming Consolidate as the thing that fixes it (`import_asset.hpp:19-20`). Until
this leaf lands, a project that imports a photo is **not portable** — move the
directory or the source file and the borrow breaks to a placeholder, with no way
to bring the pixels in-project or re-point the reference. D13 promises the
*"self-contained, movable bundle (the NLE 'collect files' move)"* and the
*"relink ('locate file')"* recovery; `editor.project.gc` shipped the reclamation
half (Clean up) but explicitly split Consolidate + relink here because they act on
borrowed cells the editor could not yet produce — cells that
`editor.import.image` / `paste` / `nested` now mint (D-gc-4). This leaf closes
the portability story: owned bytes are GC'd (shipped), borrowed bytes can be
*made* owned (this leaf), and a broken borrow can be recovered (this leaf).

## Inputs / context

**Design docs (normative — the constitution).**

- `docs/00-design.md` **D13** (`:480`) — the governing row: *"owned bytes in
  `assets/` (painted tiles + consolidated/pasted files) content-addressed,
  dedup'd, GC'd … borrowed files **and `workspace/`** never GC'd. **'Consolidate'
  copies borrows into `assets/` + relativizes URIs.** Missing borrow → placeholder
  + relink."* This leaf realizes the two bold sentences (`gc` realized the first).
- `docs/00-design.md` **§8 "Import & assets"** — *"**Consolidate** moves a
  reference from borrowed → owned"* (`:311`); the **Portability — Consolidate**
  subsection (`:337-340`): *"'Consolidate project' copies borrowed files into
  `assets/` and relativizes their URIs, producing a self-contained, movable bundle
  (the NLE 'collect files' move)"*; the **Missing / moved references** subsection
  (`:333-335`): *"A broken borrow shows a placeholder and a **relink** ('locate
  file') / reveal affordance … Only *borrowed* pixels can break."* The normative
  behavior spec for both verbs.
- `docs/00-design.md` **D15** (`:482`) / **§9 Undo/redo** (`:371-378`) — *"consolidate
  is reversible"*, and *"Every scene edit (place, move, scale, rotate, reorder,
  paint, **import**, delete, resolution change) is a **library transaction** (doc
  14), so undo/redo is the document's journal."* Consolidate is an import-class
  edit; its reversibility is delivered — as §9 delivers all reversibility — by
  being a dispatched, journalled transaction. (There is no separate D/A row
  stating "consolidate is a dispatched transaction" verbatim; it is the direct
  application of §9's rule, which is why no doc delta is needed —
  D-consolidate-1.)
- `docs/00-design.md` **D11/D12** (`:478-479`) — the two asset axes (editable? ·
  bytes-where?) and import paths: *"consolidate = borrowed→owned"* (D11); the
  owned/borrowed provenance Consolidate flips.
- `docs/01-architecture.md` **A30** (`:447`) — the owned-image write path
  Consolidate reuses: *"`project::mint_owned_asset(fs, layout, bytes, ext)` hashes
  the bytes … `put`s them write-if-absent through `FilesystemAssetSink` … and
  returns the project-relative owned URI + the bytes"*, and *"a project-relative
  `assets/` URI ⇒ `borrowed=false` (owned), an external absolute URI ⇒
  `borrowed=true` (borrowed)"*. A30 also defers *fresh-root* owned-blob copy to
  `editor.import.owned_asset_republish` — orthogonal to this leaf (which copies
  into the **current** root; see Constraints §7).
- `docs/01-architecture.md` **§8 levelization** (`:310-346`, table `:328-342`):
  `commands` L1 (deps `base, project, scene`), `project` L1 (deps `base, platform,
  libarbc`), `scene` L1 (deps `base, project, libarbc`); *"All of L1 is the
  testable core and none of it may `#include <imgui.h>` (or GL/SDL)"* (`:344-346`).
  **A16** (`:433`, `one_action_one_entry`) — one command = one journal entry = one
  undo, dispatched inside `CanvasView::apply_edit`. **A4.1b** (`:189-225`) — the
  writer thread owns every `transact`/`journal().undo()`. **§9 DoD** (`:348-372`) /
  **A8/A9** (`:425-426`) — the universal testing checklist.

**Library API surface (fetched under `build/dev/_deps/arbc-src/`).** No
`consolidate`/`relink`/`resolve_source`/`copy_asset` symbol exists — Consolidate
is an editor-level composition of: `external_asset_ref()` (read the borrow,
`content.hpp:704`), `SaveContext::store_asset` (copy bytes, `save_context.hpp:154`,
wrapped by `mint_owned_asset`), `FilesystemAssetSource` (re-resolve on reopen),
and the delete+re-attach create verbs (`scene::add_cell` / `add_nested_reference`,
`src/scene/ace/scene/cell.hpp:134,169`). The image URI lives in the opaque
`params.source` (`image_content.hpp:36`, authored via
`image_config(authored, resolved, bytes)`, `image_content.hpp:488`); the nested
ref is **immutable** (`NestedContent` has no setter, `nested_content.hpp:296`) —
so a rewrite is delete + re-create, not in-place mutation (D-consolidate-2).

**Source seams this leaf extends.** `src/commands/ace/commands/app_state.hpp:335`
(`dispatch`), `:478` (`gc_project`, the orchestrator sibling);
`src/commands/ace/commands/cells.hpp:45,63,117,144` (the create/remove/transform
verbs to model the batch on); `src/project/import_asset.cpp:50`
(`mint_owned_asset`); `src/scene/cell.cpp:112,117-135` (the borrowed classifier);
`src/dock/dock.cpp:320,57` + `src/dock/ace/dock/dock.hpp:34,209` (rail + modal +
gateway); `src/app/project_gateway.cpp:197,399,470-489`
(`clean_up`/`run_edit`/`paste_image` moulds) + `src/app/ace/app/project_gateway.hpp:295`;
`src/app/inspector_panel.cpp:110` (relink surface); `src/app/file_dialog.hpp:18`
(picker).

**Test rigs.** `tests/import_asset_test.cpp` / `tests/paste_image_test.cpp`
patterns + `ScratchDir`; `ace_tests` (headless Catch2) for the new
`tests/consolidate_test.cpp`; `ace_shell_test` (ImGui Test Engine) for
`tests/consolidate_e2e_test.cpp`, mirroring `tests/gc_ui_e2e_test.cpp` (stub
`ProjectGateway`, `ctx->ItemClick(rail_ref("###…"))`); the existing golden
`tests/goldens/import_image_64x64.rgba8` + `render_document_srgb8`
(`src/render/ace/render/render.hpp`) for the pixels-unchanged / self-contained
round-trip; the `import_paste` TSan anchor pattern in `tests/canvas_host_test.cpp`.

## Constraints / requirements

1. **Consolidate operates only on `detail.borrowed == true` image cells.** The
   read-side is `scene::classify_detail` (`cell.cpp:117-135`); owned cells
   (project-relative `assets/` URI) and already-consolidated cells are skipped.
   Running Consolidate twice is a no-op on the second pass (idempotent).

2. **The copy uses the shipped owned-image path — `mint_owned_asset` into
   `assets/images/`, write-if-absent, content-addressed, dedup'd.** Two borrowed
   cells pointing at the same file produce **one** blob (mint dedup). Consolidated
   images are indistinguishable in provenance from pasted-owned images (A30): a
   project-relative `assets/images/<xx>/<hash>` URI, `borrowed=false` after the
   rewrite.

3. **The URI rewrite is ONE reversible library transaction (D15/§9).** All borrowed
   images are rewritten in a single `commands::dispatch` → one journal entry → one
   undo, via the batch pattern (`cells.hpp:104-144`). Undo restores every original
   borrowed URI (and cell) verbatim from the journal; a single Ctrl+Z reverts the
   whole Consolidate. The transaction preserves each cell's **transform and
   z-order**; because the rewrite is delete+re-insert, forward ObjectIds change
   (D-consolidate-2) — undo restores the originals.

4. **The byte copy happens on commit, before dispatch, on the calling thread; only
   the transaction posts to the writer.** `mint_owned_asset` writes to `assets/`
   (idempotent, write-if-absent) up front; then the URI-rewrite `Command` is
   dispatched inside `run_edit` onto the one writer thread (A4.1b). Preview writes
   nothing and dispatches nothing (the document's revision is unchanged after a
   preview).

5. **Consolidate is previewed and confirmed (D-consolidate-5).** The rail action
   runs `consolidate(preview=true)`, shows a modal with the files + bytes to copy
   (and the nested-left-external count), and only copies + dispatches on user commit
   (`preview=false`); Cancel does nothing. Reversibility is the safety net, but the
   preview reports the "collect files" scope up front.

6. **Relink is a single-cell reversible re-point.** `relink_cell(state, cell,
   new_path)` rewrites one borrowed cell's URI to a user-picked absolute path via
   the same `relink_cell_command` (one journal entry, undoable). It works for image
   and nested borrows alike. It does **not** copy bytes (it re-points the borrow);
   Consolidate is the separate verb that owns.

7. **Consolidate copies into the CURRENT root's `assets/` only.** It does not touch
   `workspace/`, does not publish `project.arbc`, and does not itself save. Carrying
   the now-owned blobs to a *different* root on Save As is a separate concern owned
   by `editor.import.owned_asset_republish` (A30 deferral); an in-place
   Save→reopen round-trips the consolidated bundle without it (owned URIs re-resolve
   through `FilesystemAssetSource`).

8. **Levelization — no new component, no new DAG edge, no lint edit, no doc delta,
   no new link dep.** `commands::consolidate_project` / `relink_cell` and the
   `commands/cells` verbs sit in `commands` (L1) and reach `project`
   (`mint_owned_asset`) and `scene` (`classify_detail`, `add_cell`) through
   `commands`'s existing closure — the one that already hosts `save_project` /
   `gc_project` — so **no new edge**. The `dock` virtual returns a **dock-local
   `ConsolidateSummary` POD** (D-consolidate-6) so `dock` gains no `commands`/
   `project`/`arbc` include edge; the override + modal + relink UI stay in L4 `app`.
   `commands`/`project`/`scene` stay ImGui/GL/SDL-free. `check_levels` stays green
   with **zero** `ALLOWED`/`EXTERNAL_ALLOWED` edits.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`, `:359-372`;
A9 `:426`); `scripts/gate` green (check_levels · clang-format · build · ctest) is
the umbrella (the ≥90% diff-coverage gate is enforced in CI). Specifically:

- **Levelization (`check_levels` clean).** `python3 scripts/check_levels.py`
  passes with **no edit**: the new `commands` functions add no non-whitelisted
  include; `mint_owned_asset` / `classify_detail` are reached through `commands`'s
  existing `project`/`scene` closure (no new edge); the `dock`
  `ConsolidateSummary` is dock-local (no `dock → commands`/`project`/`arbc` edge);
  the override + modal + inspector relink stay in `app` (L4). No new component, no
  new DAG edge, no new link dep. Primary structural assertion.

- **L1 logic — Catch2 unit (the bulk), in `tests/consolidate_test.cpp` joined to
  `ace_tests`**, reusing `ScratchDir`, header-comment `editor.import.consolidate`,
  sentence-style `TEST_CASE`s. Fixtures build a real `AppState` and import borrowed
  images through the shipped `borrow_asset_file` path (a `tests/fixtures/*.ppm`),
  so the borrowed cells are genuine:
  - **Preview computes without writing (Constraint 4/5):** a project with 2 borrowed
    image cells + 1 owned cell → `consolidate_project(state, preview=true)` reports
    `{borrowed_images=2, bytes_to_copy=…, nested_left=0}`, writes **nothing** to
    `assets/`, dispatches **nothing** (`document().pin()->revision()` unchanged).
  - **Commit copies + rewrites reversibly (Constraint 2/3):** `preview=false` →
    each borrowed image now carries a project-relative `assets/images/…` URI
    (`classify_detail → borrowed=false`), the blobs exist under `assets/images/`,
    exactly **one** journal entry was added, and a single undo restores both
    original borrowed URIs (and revision round-trips). Transform + z-order are
    unchanged across the round-trip.
  - **Idempotence (Constraint 1):** a second `consolidate_project` reports
    `borrowed_images=0` and dispatches nothing.
  - **Dedup (Constraint 2):** two borrowed cells referencing the **same** file →
    one blob under `assets/images/` after commit.
  - **Owned + nested untouched (Constraint 1, D-consolidate-3):** an owned (pasted)
    cell is unchanged; a borrowed nested `.arbc` cell is left external and surfaced
    in the summary's `nested_left` count (not rewritten).
  - **Relink (Constraint 6):** `relink_cell(state, cell, new_path)` on a borrowed
    cell rewrites the URI to `new_path`, adds one journal entry, and undo restores
    the prior URI; a relink of a *missing* borrow re-resolves the cell.

- **Rendered output — golden (Consolidate never changes pixels; the bundle is
  self-contained), in `tests/consolidate_test.cpp`.** **Reuse**
  `tests/goldens/import_image_64x64.rgba8` (owning the bytes must not perturb
  pixels — exactly the paste-reuse rationale): build a doc with a borrowed image →
  `render_document_srgb8` → **byte-exact** vs the golden; `consolidate_project(…,
  false)` → render again → **byte-exact** vs the same golden. Then the
  **self-containment** assertion: `save_project` → copy the root to a fresh
  directory → **delete the original borrowed source file** → `open_project` the
  copy → render → **byte-exact** vs the golden (proves the consolidated bundle
  resolves with the external source gone). No new golden committed.

- **UI e2e — ImGui Test Engine, in `tests/consolidate_e2e_test.cpp` joined to
  `ace_shell_test`.** Inject a **fake `ProjectGateway`** (recording
  `consolidate(preview)` calls, returning a scripted `ConsolidateSummary`) via
  `ShellOptions::project_gateway` (mirroring `tests/gc_ui_e2e_test.cpp`): drive
  **Consolidate project…###consolidate**, assert `consolidate(preview=true)` was
  invoked and the confirm modal shows the files/bytes counts; click
  **###consolidate_confirm**, assert `consolidate(preview=false)` was invoked; a
  separate run clicks **###consolidate_cancel** and asserts the real consolidate was
  **not** invoked. A relink e2e drives the inspector **Relink…###relink** button
  with a stub `FileDialog` returning a path and asserts a `relink_cell` dispatch. (+
  a screenshot baseline of the consolidate confirm modal.)

- **Threading (ASan/TSan) — the copy→dispatch handoff.** A scenario under the
  `asan` and `tsan` presets: boot → import a borrowed image → **consolidate**
  (byte copy on the calling thread, then the URI-rewrite transaction posts to the
  writer thread) → undo → clean teardown; must be sanitizer-clean. Scope: the
  `mint_owned_asset` write completes before the `run_edit` dispatch, and the
  transaction runs writer-side (A4.1b), so the copy and the edit touch disjoint
  state — the test pins that (mirrors the `import_paste` anchor in
  `tests/canvas_host_test.cpp`).

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`, `coverage`
  preset), including the preview/commit branches, the idempotent/skip paths, the
  dedup path, the `ConsolidateSummary` mapping, the relink verb, and the modal
  confirm/cancel paths. Tests ship with the task.

- **Format/build.** `clang-format --dry-run --Werror` clean; `dev` and `release`
  presets build; `scripts/gate` green.

- **Named future tasks (closer registers in WBS; both wired under `editor.import`
  → milestone M9E).**
  1. **`editor.import.consolidate_nested`** — *"Consolidate a borrowed nested
     `.arbc` into a self-contained bundle (recursive asset copy + ref rewrite)."*
     Effort ~2.5d; `depends editor.import.consolidate, editor.import.nested`; `note`
     citing D11/D12/D13 + §8 (Portability — Consolidate) + D15. A nested document
     resolves its own owned tiles and borrows relative to its original location, so
     a self-contained copy must recursively copy the nested project's transitive
     owned + borrowed assets (arbitrary depth) and rewrite refs — no library bundle
     seam exists, and copying just the `.arbc` file ships a broken bundle. Concrete
     and agent-implementable, but materially larger than the image case and
     orthogonal to it; deferred so v1 ships the correct, common image path.
  2. **`editor.import.reveal_in_file_manager`** — *"Reveal a borrowed file in the
     OS file manager."* Effort ~1d; `depends editor.import.consolidate`; `note`
     citing §8 (relink / **reveal** affordance). Needs a new cross-platform
     `platform` seam (xdg-open / `open` / `explorer /select`) extending
     `ProcessLauncher::spawn_detached` (`src/platform/ace/platform/process_launcher.hpp:19`,
     no reveal wrapper today); orthogonal to the reversible-transaction core.

## Decisions

**D-consolidate-1 — Consolidate/relink are L1 `commands` orchestrators over the
shipped `project::mint_owned_asset` (byte copy) + new reversible `commands/cells`
URI-rewrite verbs, thin-wired through a `ProjectGateway` seam + confirm modal,
mirroring the `save`/`gc` split (D-gc-1).** `commands::consolidate_project(state,
preview)` enumerates borrowed image cells (`scene::classify_detail`), copies bytes
(`mint_owned_asset`) on commit, and dispatches one batched URI-rewrite `Command`;
the rail + modal + `AppProjectGateway::consolidate` override live in L3/L4.
*Rationale:* §7 names `project` as *"open/save/gc"* and `commands` as *"actions →
transactions · undo"*; every building block ships, so this needs no new seam — the
exact shape `gc` proved (L1 orchestrator + gateway virtual + confirm modal).
*Alternative rejected:* a single libarbc `consolidate` entry point — none exists,
and the URI-rewrite-as-scene-transaction is inherently editor-level (D15); inlining
the copy + dispatch in the L4 gateway — pushes I/O and transaction logic into the
SDL-only level, against §9's "L1 logic is the bulk" and the `gc`/`save` layering.
*No doc delta:* Consolidate is an import-class scene edit, and §9 already makes
every scene edit a dispatched library transaction — this leaf applies that rule, it
does not amend it.

**D-consolidate-2 — A URI rewrite is a delete + re-insert of the cell (transform +
z-order preserved), folded into ONE library transaction — this is precisely why
consolidate is reversible.** The library exposes no in-place config/ref setter: the
image URI is in the opaque `params.source` (`image_content.hpp:36`) and the nested
ref is immutable (`NestedContent` has no setter, `nested_content.hpp:296`). So each
rewrite removes the borrowed cell and re-attaches an owned-URI cell (via
`scene::add_cell` / `add_nested_reference`) inside one `transact`, giving one
journal entry / one undo (A16). *Rationale:* it reuses the shipped batch-transaction
pattern (`remove_cells`/`transform_cells`, `cells.hpp:104-144`) with no new library
machinery, and reversibility falls straight out of the journal (D15/§9). *Consequence
(documented):* forward ObjectIds churn across a Consolidate (a delete+re-add mints
new ids); undo restores the originals verbatim from the journal, so the operation is
fully reversible — selection/history references to a consolidated cell's *old* id
are stale only in the forward direction, which any structural edit already implies.
*Alternative rejected:* an in-place library `update_content_config` / mutable-ref
seam that would preserve ObjectIds — it does not exist, is cross-repo library work,
and is recorded in the parking lot; building against a fabricated seam is not
agent-implementable here.

**D-consolidate-3 — v1 consolidates borrowed IMAGES only; borrowed nested `.arbc`
consolidation is split to `editor.import.consolidate_nested`.** *Rationale:* a
nested document resolves its own owned tiles and its own borrows relative to its
original on-disk location; copying just the referenced `.arbc` into the parent's
`assets/` **breaks** that resolution, so a correct nested consolidation must
recursively copy the nested project's transitive owned + borrowed assets at
arbitrary depth — a materially larger operation with no library bundle seam.
Shipping the naive `.arbc`-file copy would produce a *broken* bundle, which is worse
than leaving the nested borrow external and honestly reporting it. v1 detects
borrowed nested cells and surfaces them in the summary's `nested_left` count so the
bundle's completeness is truthful. This is a WBS **sequencing** call the closer owns
(D13/§8 still describe consolidating all borrows), not a design change. *Alternative
rejected:* consolidate nested now via a shallow `.arbc` copy — ships a bundle that
fails to resolve once the original source tree is gone, defeating the entire
portability purpose.

**D-consolidate-4 — Relink (locate-file) ships in v1 as a per-borrow inspector
affordance dispatching the SAME reversible URI-rewrite command; "reveal in file
manager" is split to `editor.import.reveal_in_file_manager`.** Relink re-points a
moved/missing borrow's URI to a user-picked path via `relink_cell_command` (one
journal entry, undoable), reusing the `FileDialog` seam; it works for image and
nested borrows alike because it is a pure re-point, no bundling. It complements the
placeholder the library already renders for a broken borrow (D-open-6,
`project.hpp:75-77`). *Rationale:* locate-file is the load-bearing recovery path and
reuses machinery this leaf already builds; "reveal" is a convenience that needs a
*new* cross-platform `platform` seam (no `xdg-open`/`open`/`explorer` wrapper
exists today), which is orthogonal to the reversible-transaction core and cheaply
split. *Alternative rejected:* folding reveal in now — adds per-OS platform code +
tests to a task whose core is the transaction model, and the Linux-only v1 CI (per
`ci.md`) would exercise only one of the three OS paths anyway.

**D-consolidate-5 — Consolidate is a previewed, confirmed op (reusing the `gc`
modal shape), and reversible via the journal.** The rail runs a `preview=true` plan
→ a modal reporting files + bytes to copy (and nested-left-external) → a committed
copy + dispatch on user confirm. *Rationale:* although consolidate is *reversible*
(unlike GC, which D15 fixes as a confirmed-because-irreversible op), the "collect
files" copy can duplicate many megabytes, so a preview of the scope is good UX; undo
is the safety net if the user reconsiders after committing. Reusing `draw_gc_modal`'s
preview→confirm→commit shape keeps the rail consistent. *Alternative rejected:*
one-click immediate consolidate with no preview — hides how much the "collect files"
move will copy; a mandatory *irreversible* confirm like GC — wrong, consolidate is
undoable, so the modal is informational, not a point-of-no-return.

**D-consolidate-6 — `ProjectGateway::consolidate` returns a dock-local
`ConsolidateSummary` POD, not a `commands`/`project`/`arbc` type (mirrors
D-gc-5).** L4 `app` maps the `commands` outcome → `dock::ConsolidateSummary {
consolidated_files, copied_bytes, nested_left, ran }`. *Rationale:* the gateway is
declared in L3 `dock`; a `commands`/`project`/`arbc` return type would add a `dock →`
core include edge (a `check_levels` DAG change / doc delta) for a small report.
A dock-local POD keeps the seam edge-neutral, and the type-mapping lives in L4 `app`,
which already sees all layers. *Alternative rejected:* returning the runtime/library
report straight through the gateway — couples the UI seam to a core type and blurs
the "dock speaks its own vocabulary" line the codebase keeps; returning `bool` —
loses the counts the confirm modal must show.

## Open questions

_None blocking — all decided against the constitution._ D13/§8 fix Consolidate as
"copy borrows into `assets/` + relativize URIs → self-contained bundle" and relink
as the locate-file recovery; D15/§9 fix its reversibility as a journalled scene
transaction; A30 fixes the owned-image write path (`mint_owned_asset`); §8
levelization fixes the L1 homes and the no-new-edge shape. The one genuine degree of
freedom — how to rewrite an immutable URI reversibly — is settled defensibly
(D-consolidate-2: delete+re-insert in one transaction, the shipped batch pattern)
and pinned by the reversibility unit + the pixels-unchanged / self-contained golden.
**Doc delta:** none — Consolidate/relink are D13/§8/D15-mandated, compose shipped
library primitives and the A13/A16 seams, and add no new dependency, component, DAG
edge, or behavioral deviation. **Parking-lot items (human judgment / cross-repo, not
WBS tasks):** (1) a libarbc in-place `update_content_config` / mutable-ref seam that
would let a consolidate/relink rewrite preserve ObjectIds instead of delete+re-add —
cross-repo library work, not agent-implementable in this repo. (2) The known
`arbc-gc-blind-to-owned-images` library gap (v0.4.1 GC scans only `assets/tiles/`,
never `assets/images/`): a consolidate-then-undo leaves the minted owned-image copy
under `assets/images/` unreclaimable by the current GC — a pre-existing library
limitation shared with pasted-owned images (bytes are small, correctness unaffected),
recorded for continuity, not a blocker for this leaf.

## Status

_pending implementation_
