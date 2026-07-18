# editor.project.gc — Clean up (GC): reclaim orphaned owned assets

## TaskJuggler entry

`tasks/00-editor.tji:143-148` — `task gc "Clean up (GC) + consolidate + relink"`
under `editor.project`. Effort `2d`, `allocate team`, `depends !save` (i.e.
`editor.project.save`). Like `save_as`/`exec_new` the block has **no**
`complete 100` yet. The `note` (`:147`) cites **Design D13** and back-links a
**flat** landing path (`tasks/refinements/gc_consolidate.md`); the real landing
path is `tasks/refinements/editor/gc.md`, matching the existing `editor/`
refinement set — the closer fixes the note back-link per the ritual in
`tasks/refinements/README.md:57-68`, exactly as `open.md` / `app_state.md` /
`save.md` / `save_as.md` did.

**Scope split (this refinement's central call — see Decisions D-gc-4).** The
`.tji` title bundles three verbs — *Clean up (GC)* **+** *Consolidate* **+**
*relink*. This leaf ships **Clean up (GC) only**. *Consolidate*
(borrowed→owned + relativize URIs) and *relink* (locate a moved borrow) operate
on **borrowed/referenced-URI cells that do not exist in the editor yet** — they
arrive with `editor.import.paste` / `editor.import.nested`, on which `gc` does
**not** depend (`depends !save` alone). Both are therefore split to a named
future task, **`editor.import.consolidate`** (registered under Acceptance
criteria; the closer wires it and, if it wishes, trims the `gc` task title to
"Clean up (GC)"). This is a WBS **sequencing** change (closer owns the `.tji`),
not a design-doc change: D13/§8 still describe all three verbs — this refinement
only sequences them across leaves so each lands when its inputs exist.

Downstream: `gc` is a dependency of the M9E editor rollup leaf
(`tasks/00-editor.tji:331`) and, transitively, of milestone **M9E**
(`tasks/99-milestones.tji:6-8`).

## Effort estimate

**2 days** (from the `.tji`), for the **Clean up (GC)** half plus its full DoD
matrix. The library does the heavy lifting — the mark-and-sweep is a shipped,
already-tested libarbc entry (`arbc::gc_project_directory`,
`build/dev/_deps/arbc-src/src/runtime/arbc/runtime/asset_gc.hpp:113`, tests at
`build/dev/_deps/arbc-src/tests/asset_gc.t.cpp`). This leaf is thin wiring in
four small places plus the tests, which dominate the two days:

- **L1 `project`** — a new `gc.hpp`/`gc.cpp` beside `save.hpp` (`save.hpp:57-58`
  already names *"reclaiming orphaned blobs is `editor.project.gc`"* as the home):
  `gc_project(layout, dry_run)` guards on canonical presence, calls
  `arbc::gc_project_directory(layout.root, dry_run)`, and maps its report/error
  onto `platform::Result`.
- **L1 `commands`** — `gc_project(AppState&, dry_run)` beside `save_project`
  (`app_state.hpp:169`): pull the layout, call `project::gc_project`; **not** a
  transaction (no revision bump, no `mark_saved`).
- **L3 `dock`** — a `ProjectGateway::clean_up(preview)` virtual + a
  **Clean up…###gc** rail entry beside **Save As…** (`dock.cpp:112`) that runs a
  dry-run, shows a confirm modal, then commits (D15 "confirmed op").
- **L4 `app`** — `AppProjectGateway::clean_up(...)` drives the `commands`
  orchestrator; `Shell::shutdown()` (`src/app/shell.cpp:163`) runs a silent
  on-close sweep.

**No new component, no new DAG edge, no new library machinery, no new
`FileSystem` primitive, and — unlike `save` — no new `nlohmann_json` link dep**
(the library's GC surface names no JSON type by design, `asset_gc.hpp:13-14`).
No doc delta.

## Inherited dependencies

**Settled (from `editor.project.save`).** The write side this leaf reclaims
against already ships and is trusted:

- `project::save_project(fs, layout, doc, registry) -> Result<SaveOutcome>`
  (`src/project/ace/project/save.hpp:84`, impl `src/project/save.cpp:96-147`)
  routes owned bytes through the content-addressed `FilesystemAssetSink`
  (`save.hpp:58`), which is **write-if-absent and NEVER deletes** — *"a save
  cannot prove a blob dead"* (`asset_gc.hpp:4-7`, `save.hpp:57-58`). So the
  `assets/` directory only ever grows; **GC is the reclamation half this leaf
  supplies.** Blobs land under `<root>/assets/tiles/` with a two-hex fan-out, the
  exact subtree `FilesystemAssetReaper` enumerates.
- `ProjectLayout` and `project_layout(root)` (`src/project/ace/project/project.hpp:55-66`,
  impl `src/project/project_open.cpp:105-116`): `root`, `canonical` =
  `<root>/project.arbc` (`:57`), `assets_dir` = `<root>/assets` (`:58`),
  `workspace_dir`, `workspace_file` = `<root>/workspace/document.arbcws`,
  `exports_dir`, `gitignore`. GC needs only `root` (the library resolves
  `assets/tiles/` against it the same way save/load do, `asset_gc.hpp:108`).
- `AppState` (`src/commands/ace/commands/app_state.hpp:35`): `document()` (`:44`),
  `layout()` (`:47`), `registry()` (`:49`), the dirty model `is_dirty()` (`:68`) /
  `mark_saved()` (`:75`) — the invariants a GC must leave **unchanged** (GC is not
  a document edit).

**Settled (from `editor.project.open` / `open_ui` / A12/A13).** The rail-action
seam: project verbs are dependency-inverted behind `dock::ProjectGateway`
(`src/dock/ace/dock/dock.hpp:38-96`), declared by L3 `dock`, implemented by L4
`app::AppProjectGateway` (`src/app/ace/app/project_gateway.hpp:32`), wired via
`Dockspace::set_project_gateway(...)` (`dock.hpp:131-132`, `src/app/shell.cpp:262`).
`save`/`save_as` added `save()`/`is_dirty()`/`save_as()` here; **Clean up follows
the identical pattern** — a new pure virtual + a rail entry in
`draw_project_section` (`src/dock/dock.cpp:87-160`) + an L4 override. Fake-gateway
injection point: `ShellOptions::project_gateway` (`src/app/ace/app/shell.hpp:50`).

**Settled (from libarbc, fetched under `build/dev/_deps/arbc-src/`).** The
mark-and-sweep GC is fully built (see Inputs). This leaf adds **no** library
code; it selects the root and drives the shipped entry.

**Pending (this leaf owns them).** L1 `project::gc_project`; L1
`commands::gc_project`; the `dock::ProjectGateway::clean_up()` virtual + rail
entry + confirm modal; the L4 `AppProjectGateway::clean_up()` override; the
silent on-close sweep in `Shell::shutdown()`.

## What this task is

Realize D13's **Clean up** verb: reclaim the owned asset bytes in `assets/` that
no longer belong to the project. Because the save sink never deletes, `assets/`
accumulates orphans across edits (every re-hash on a changed tile, every
superseded save leaves its old blob behind); Clean up is the explicit
mark-and-sweep that reclaims them. Per §8 the editor is *"the host that runs the
library's mark-sweep (`gc_project_directory`) on an explicit 'Clean up / compact'
**and on close**"* (`docs/00-design.md:340-343`). Concretely:

- In **L1 `project`** add `gc_project(const ProjectLayout& layout, bool dry_run)
  -> platform::Result<GcOutcome>` (new `src/project/ace/project/gc.hpp` +
  `src/project/gc.cpp`): **no-op** (return an empty `GcOutcome`) when
  `layout.canonical` is absent (no canonical root → the library would treat
  *zero* roots as *reclaim everything*, `asset_gc.hpp:111-112`); otherwise call
  `arbc::gc_project_directory(layout.root, dry_run)` and map `arbc::GcReport` →
  `project::GcOutcome` and `arbc::GcError` → `project::GcError`. Errors are
  values; on any GC error **nothing is deleted** (fail-safe, `asset_gc.hpp:69-72`).
- In **L1 `commands`** add `gc_project(AppState& state, bool dry_run) ->
  platform::Result<project::GcOutcome>` (beside `save_project`,
  `app_state.hpp:169`): pull `state.layout()`, call `project::gc_project`. It
  dispatches **no transaction**, bumps **no** document revision, and does **not**
  call `mark_saved` — GC is a maintenance op over on-disk blobs, not a document
  edit (D15, D13).
- On the **rail**, a **Clean up…###gc** `Selectable` beside **Save As…**
  (`dock.cpp:112`), wired through a new `dock::ProjectGateway::clean_up(bool
  preview)`. The action opens a confirm modal (D15 "confirmed op"): a `preview`
  (dry-run) run surfaces the reclaim counts, the user commits, then a real sweep
  runs. The L4 `AppProjectGateway::clean_up()` drives the `commands` orchestrator
  against the one in-process `AppState`.
- On **close**, `Shell::shutdown()` (`src/app/shell.cpp:163`) runs a **silent**,
  best-effort `commands::gc_project(state, /*dry_run=*/false)` before teardown
  (§8 "and on close").

The sweep roots on **this process's one canonical document** (`project.arbc`),
per process-per-project (D19/A7 *"GC root-set is that document"*). It touches
`assets/` **only** — never `workspace/`, never borrowed files (library contract +
D13).

**Explicitly out of scope (D-gc-4):** *Consolidate* (copy borrowed files into
`assets/` + relativize their URIs for a portable bundle) and *relink* (locate a
moved borrow), which require borrowed-URI cells the editor cannot yet produce —
split to the named future task `editor.import.consolidate`.

## Why it needs to be done

`save`'s content-addressed sink is deliberately grow-only (`save.hpp:57-58`,
`asset_gc.hpp:3-7`): it cannot prove a blob dead, so it never deletes, and
`assets/` grows without bound as the user edits and re-saves. D13 makes the
reclamation an **explicit, host-driven** op — *"GC'd via explicit 'Clean up' +
on close, roots = all open docs"* (`docs/00-design.md:474`) — and A7 assigns the
host role to the editor (*"the app owns exactly one `Document` … GC root-set is
that document"*, `docs/01-architecture.md:257`). The architecture doc already
names the mechanism: *"'clean up' → `gc_project_directory`"*
(`docs/01-architecture.md:109`). Until this leaf lands, a long-lived project's
`assets/` only ever grows — the portable core D16/§9 promises (`project.arbc` +
`assets/`) bloats with every dead blob, and the "compact my project" gesture has
no home. The library's own parking lot flags that *no user-facing GC affordance
exists yet* — this leaf is that affordance.

## Inputs / context

**Design docs (normative — the constitution).**

- `docs/00-design.md` **D13** (`:474`) — the governing row: *"A project is a
  directory (§9); owned bytes in `assets/` … content-addressed, dedup'd, GC'd via
  explicit 'Clean up' + on close, roots = all open docs; borrowed files **and
  `workspace/`** never GC'd. 'Consolidate' copies borrows into `assets/` +
  relativizes URIs. Missing borrow → placeholder + relink."* This leaf realizes
  the first sentence; the last two sentences are `editor.import.consolidate`.
- `docs/00-design.md` **§8 "Import & assets" — "On disk & GC"** (`:336-344`):
  *"Owned bytes in `assets/` … are content-addressed, dedup'd, and
  garbage-collected: the editor is the host that runs the library's mark-sweep
  (`gc_project_directory`) on an explicit 'Clean up / compact' **and on close**,
  with all open documents as roots — safe for the single-editor case; the
  cross-process case stays a host policy we do not invite (parking-lot). GC
  touches `assets/` **only** — never the workspace or borrowed files."* The
  normative behavior spec.
- `docs/00-design.md` **§8 two-axis / owned-vs-borrowed** (`:293-310`): *"**owned**
  (inside the project's `assets/`; the project stores, dedups and GCs them) or
  **borrowed** (an external file pointed at by URI; never stored, never GC'd) …
  **GC touches only owned bytes** — a borrowed file is never the project's to
  delete."* Fixes what GC may and may not reap.
- `docs/00-design.md` **D15** (`:476`, prose `:371-372`) — *"GC is a confirmed op
  (not undoable); consolidate is reversible."* Fixes the confirm-before-sweep flow
  and pins that GC is **not** a journaled transaction (so it must not bump the
  revision or engage undo).
- `docs/00-design.md` **D19** (`:480`) / **A7** (`docs/01-architecture.md:257`) —
  process-per-project; *"GC root-set is that document."* One process, one
  document, one canonical `project.arbc` → the root.
- `docs/00-design.md` **§9 "Files & the project directory"** (`:374-412`, layout
  `:379-385`) / **D16** (`:477`) — `project.arbc` + `assets/` are the portable
  core; `workspace/` is machine-local scratch. GC keeps the portable core minimal
  and never touches `workspace/`.
- `docs/00-design.md` **D11/D12** (`:472-473`) — the owned/borrowed asset axes
  and import paths that *Consolidate*/*relink* (the deferred half) act on.
- `docs/01-architecture.md` **§8 levelization** (`:144-179`): `project` L1
  (deps `base, platform, libarbc`; ImGui/GL/SDL-free), `commands` L1 (deps `base,
  project, scene`). **§9 DoD** (`:181-208`). **A12/A13** (`:262-263`, the
  `ProjectGateway` seam). Line `:109` — *"'clean up' → `gc_project_directory`."*

**Library API surface (fetched under `build/dev/_deps/arbc-src/`).** The
mark-and-sweep, in `build/dev/_deps/arbc-src/src/runtime/arbc/runtime/asset_gc.hpp`
(`namespace arbc`) — this leaf's one library dependency:

- `expected<GcReport, GcError> gc_project_directory(const std::filesystem::path&
  project_dir, bool dry_run)` (`:113`, contract `:107-114`) — the convenience
  entry: *"scan `project_dir` for `*.arbc`, union their marks, resolve the default
  `assets/tiles/` base against the directory … and sweep that shared store."* **A
  document NOT in `project_dir` is not a root** — caller-completeness (`:111-112`).
  Takes a **raw `std::filesystem::path`, not the `platform::FileSystem` seam**
  (see Decision D-gc-2 / Constraint 8).
- `struct GcReport { uint64_t scanned, referenced, deleted, bytes_reclaimed; }`
  (`:60-66`, `operator==` defaulted) — counters only, never timings; in `dry_run`,
  `deleted`/`bytes_reclaimed` are what a run *would* remove.
- `struct GcError { enum class Kind { MarkFailed, EnumerateFailed, RemoveFailed };
  ReaderError mark; AssetReaperError reap; }` (`:73-81`) — **fail-safe**: a
  `MarkFailed` returns before the sweep, an `EnumerateFailed` before the first
  unlink, so *"GC has deleted NOTHING"* on those (`:69-72`); a `RemoveFailed` is a
  partial run (a strict subset of orphans gone).
- The granular halves `collect_referenced_tiles(std::string_view document_json)`
  (`:95`) and `sweep_tile_store(referenced, AssetReaper&, dry_run)` (`:103`) +
  `class FilesystemAssetReaper` (`:42`) — the pieces the convenience entry
  composes; **not used directly** here (Decision D-gc-2 rejects rooting on the
  live document). The library *"names NO JSON type"* in this header (`:13-14`) —
  so wiring GC pulls **no** `nlohmann::json` (unlike `save`'s `builtin_codecs`).
- No `consolidate` / `relink` symbols exist in the libarbc public headers — those
  are **editor-level** concepts (Decision D-gc-4). Library reference:
  `build/dev/_deps/arbc-src/tests/asset_gc.t.cpp`,
  `build/dev/_deps/arbc-src/tasks/refinements/serialize/asset_gc.md`.

**Source seams this leaf extends.**

- `src/project/ace/project/save.hpp:57-58` — the sink *"NEVER deletes: reclaiming
  orphaned blobs is `editor.project.gc`"*; `src/project/ace/project/project.hpp:55-66`
  — `ProjectLayout` / `project_layout`. New `src/project/ace/project/gc.hpp` +
  `src/project/gc.cpp` land beside `save.*` in the same `project` component.
- `src/commands/ace/commands/app_state.hpp:169` (`save_project`), `:35` (`AppState`,
  `layout()` `:47`, `is_dirty()` `:68`, `mark_saved()` `:75`) — `commands::gc_project`
  joins beside `save_project`.
- `src/dock/ace/dock/dock.hpp:38-96` — `ProjectGateway` gains `virtual GcSummary
  clean_up(bool preview) = 0;` (`GcSummary` a dock-local POD, Decision D-gc-5);
  `src/dock/dock.cpp:87-160` (`draw_project_section`) gains a **Clean up…###gc**
  `Selectable` next to **Save As…** (`:112`) + a confirm modal.
- `src/app/ace/app/project_gateway.hpp:32` / `src/app/project_gateway.cpp` —
  `AppProjectGateway::clean_up()` reuses the held `AppState`; `src/app/shell.cpp:163`
  (`Shell::shutdown()`) gains the silent on-close sweep; `set_project_gateway`
  wiring at `:262`.
- `src/platform/ace/platform/filesystem.hpp:18` — the `FileSystem` seam (`exists`,
  `list_directory`, `read_file`, `write_file`, `make_directories`,
  `atomic_replace`) has **no delete primitive**; the library's GC owns deletion via
  raw `std::filesystem` (Decision D-gc-2 / Constraint 8).

**Test rigs.** `tests/platform_test.cpp` `ScratchDir`; `ace_tests` (headless
Catch2, `CMakeLists.txt` target `~:194`, `add_test ~:199`, `ACE_GOLDEN_DIR ~:198`,
Catch2 via libarbc `:16-17`) for the new `tests/project_gc_test.cpp`;
`ace_shell_test` (ImGui Test Engine, target `~:206-214`, e2e list `~:207-213`) for
the new `tests/gc_ui_e2e_test.cpp`, mirroring `tests/save_ui_e2e_test.cpp`
(`IM_REGISTER_TEST(engine, "save", …)`, `ctx->ItemClick(rail_ref("###…"))`, stub
gateway overriding verbs); `tests/app_project_gateway_test.cpp` for the L4 gateway
unit; the golden `tests/goldens/render_probe_64x64.rgba8` + `tests/golden_support.hpp`
+ `render_document_srgb8` (`src/render/ace/render/render.hpp`) for the
GC-preserves-the-canonical round-trip.

## Constraints / requirements

1. **The sweep roots on this process's one canonical document
   (`layout.canonical`), via `arbc::gc_project_directory(layout.root, dry_run)`
   (Decision D-gc-2).** Process-per-project (D19/A7) makes the on-disk
   `project.arbc` the authoritative root; `gc_project_directory` scans `root` for
   `*.arbc`, marks their referenced tiles, and reclaims the rest of
   `assets/tiles/`. No live-document serialization, no publish.

2. **No-canonical guard (data-safety).** `project::gc_project` returns an empty
   `GcOutcome{0,0,0,0}` **without sweeping** when `layout.canonical` does not exist
   (a freshly `create_project`'d, never-saved project). Reason: with zero `*.arbc`
   roots the library's referenced set is empty and it would reclaim **every** blob
   in `assets/` (`asset_gc.hpp:111-112`). (In that state `assets/` is in fact empty
   — the sink only runs on save — but the guard is the belt-and-braces contract.)

3. **GC is not a document edit (D13/D15).** `commands::gc_project` dispatches no
   transaction, bumps no revision, and does not call `mark_saved`. A Clean up
   leaves `AppState::is_dirty()`, `document().pin()->revision()`, and `layout()`
   **unchanged**. GC is "not undoable" (D15) precisely because it never enters the
   journal.

4. **Touches `assets/` only — never `workspace/`, never borrowed files.**
   Guaranteed by the library (`asset_gc.hpp` enumerates only the resolved
   `assets/tiles/` subtree) and asserted by a test that seeds `workspace/` and
   confirms it is byte-unchanged after a sweep. Borrowed files are external (not in
   `assets/`) and structurally unreachable by the reaper.

5. **Explicit Clean up is a confirmed, previewed op (D15).** The rail action runs
   `clean_up(preview=true)` (a `dry_run`), surfaces the reclaim counts (files +
   bytes) in a modal, and only sweeps for real (`preview=false`) on user commit;
   Cancel sweeps nothing. An irreversible delete is never one un-previewed click.

6. **On-close GC is silent and best-effort.** `Shell::shutdown()` runs
   `commands::gc_project(state, dry_run=false)` once, before teardown, ignoring its
   result value (an error never blocks or slows shutdown beyond the sweep itself).
   It does **not** prompt (a mandated-automatic op must not nag). Because the sweep
   roots on the on-disk canonical and touches no live state, it is
   shutdown-order-robust and does not require the `Document` to still be alive.

7. **Errors are values, never throws (house style).** `project::gc_project` maps
   `arbc::GcError` → a `project::GcError` value on the `Result`; on `MarkFailed` /
   `EnumerateFailed` **nothing is deleted** (fail-safe), on `RemoveFailed` a strict
   subset of orphans is gone and the error is reported. Neither the explicit nor the
   on-close path throws.

8. **Levelization — no new component, no new DAG edge, no lint edit, no doc
   delta, no new link dep.** `gc.hpp`/`gc.cpp` sit in `project` (L1) and include
   only `<arbc/...>`, `<ace/platform/...>`, `<ace/project/...>`, and std — all
   whitelisted (`scripts/check_levels.py`, `EXTERNAL_ALLOWED["arbc"]` includes
   `project`); `commands::gc_project` adds no non-whitelisted include and reaches
   `project`/`platform` through `commands`'s existing closure (the one that already
   hosts `save_project`) — **no new edge**. The `dock` virtual returns a
   **dock-local `GcSummary` POD** (Decision D-gc-5) so `dock` gains no `project`/
   `arbc` include edge; the override + modal wiring stay in L4 `app`.
   `project`/`commands` stay ImGui/GL/SDL-free. The GC library surface names no
   JSON type, so **no `nlohmann_json` link dep** is added (contrast `save.md`).
   `check_levels` stays green with **zero** `ALLOWED`/`EXTERNAL_ALLOWED` edits.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`, `:200-203`);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella (the 90% diff-coverage gate is enforced in CI). Specifically:

- **Levelization (`check_levels` clean).** `python3 scripts/check_levels.py`
  passes with **no edit**: `gc.hpp`/`gc.cpp` in `project` include only
  `<arbc/...>`, `<ace/platform/...>`, `<ace/project/...>`, std; `commands::gc_project`
  adds no non-whitelisted include; the `dock` `GcSummary` is dock-local (no
  `dock → project`/`arbc` edge); the override + confirm modal + on-close sweep stay
  in `app` (L4). No new component, no new DAG edge, no `nlohmann_json` dep. Primary
  structural assertion.

- **L1 logic — Catch2 unit (the bulk), in `tests/project_gc_test.cpp` joined to
  `ace_tests`**, reusing `ScratchDir`, header-comment `editor.project.gc`,
  sentence-style `TEST_CASE`s. Because the editor cannot yet mint owned tiles
  (paint is not a dependency), the fixtures are **hand-authored on-disk state** —
  a minimal `project.arbc` whose content body carries a `params.blobs` hash list,
  plus blob files written directly under `<root>/assets/tiles/` (the reaper reads
  the on-disk canonical's *text*, `asset_gc.hpp:84-90`, so no live `Document` with
  tiles is needed):
  - **Reclaim orphans:** a `project.arbc` referencing `hash_A` + blobs `hash_A`,
    `hash_B` on disk → `gc_project(layout, dry_run=true)` returns
    `GcOutcome{scanned=2, referenced=1, deleted=1, bytes_reclaimed=size(hash_B)}`
    and **both** blobs remain; then `dry_run=false` deletes `hash_B`, keeps
    `hash_A`, and reports the same counts. Dry-run and commit compute the identical
    plan.
  - **No-canonical guard (Constraint 2):** no `project.arbc` present →
    `gc_project` returns `GcOutcome{0,0,0,0}` and `assets/` is byte-unchanged (no
    sweep).
  - **Fail-safe (Constraint 7):** an unparseable / non-hash-`blobs` `project.arbc`
    → a `project::GcError` value (mapped from `MarkFailed`), and **every** blob on
    disk is untouched.
  - **`assets/`-only (Constraint 4):** seed `<root>/workspace/…` files → after a
    real sweep, `workspace/` is byte-unchanged.
  - **`commands::gc_project` invariants (Constraint 3):** over a real `AppState`,
    a Clean up leaves `is_dirty()`, `document().pin()->revision()`, and `layout()`
    unchanged; the returned `GcOutcome` matches the `project`-level result.

- **Rendered output — golden (GC never damages the portable core), in
  `tests/project_gc_test.cpp`.** Reuse `tests/goldens/render_probe_64x64.rgba8`:
  `create_project` → build the probe content → `save_project` → `gc_project(…,
  false)` → `open_project` the same root (rebuild-from-canonical) →
  `render_document_srgb8` → **byte-exact** compare against the golden. (The probe
  hands no bytes to the sink, so GC reclaims nothing here; the assertion pins that
  a sweep leaves the canonical + its render intact. No new golden committed; a
  reclaim-a-real-painted-blob render golden rides `editor.import.consolidate` /
  `editor.paint.*`, which own the tile store.)

- **UI e2e — ImGui Test Engine (Clean up + confirm), in `tests/gc_ui_e2e_test.cpp`
  joined to `ace_shell_test`.** Inject a **fake `ProjectGateway`** (recording
  `clean_up(preview)` calls, returning a scripted `GcSummary`) via
  `ShellOptions::project_gateway` (mirroring `tests/save_ui_e2e_test.cpp`): drive
  **Clean up…###gc**, assert `clean_up(preview=true)` was invoked and the confirm
  modal is shown with the reclaim counts; click **###gc_confirm**, assert
  `clean_up(preview=false)` was invoked; a separate run clicks **###gc_cancel** and
  asserts the real sweep was **not** invoked. (+ a screenshot baseline of the
  confirm modal where it adds signal.)

- **L4 gateway unit — headless, extending `tests/app_project_gateway_test.cpp`.**
  `AppProjectGateway::clean_up(preview)` over a real `ScratchDir` project seeded
  with an orphan blob: `preview=true` reports the orphan (nothing deleted),
  `preview=false` deletes it; the `arbc::GcReport` → `project::GcOutcome` →
  `dock::GcSummary` mapping is asserted (files + bytes).

- **Threading (ASan/TSan) — the on-close sweep.** A scenario under the `asan` and
  `tsan` presets: boot → dispatch a command → **close** (`Shell::shutdown()` runs
  the silent `gc_project` sweep over `assets/` while the `HousekeepingThread` may
  still be checkpointing `workspace/`) → clean teardown. Must be sanitizer-clean.
  Non-N/A scope: this is the first on-close filesystem sweep during shutdown; the
  sweep and the checkpointer operate on **disjoint directories** (`assets/` vs
  `workspace/`) and the sweep reads no live `Document` state, so there is no shared
  mutable state — the test pins that.

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`, `coverage`
  preset), including the dry-run/commit branches, the no-canonical guard, the
  `MarkFailed`/`EnumerateFailed` fail-safe and `GcOutcome`/`GcError` mapping, the
  orchestrator, and the modal confirm/cancel paths. Tests ship with the task.

- **Format/build.** `clang-format --dry-run --Werror` clean; `dev` and `release`
  presets build; `scripts/gate` green.

- **Named future task (closer registers in WBS).** **`editor.import.consolidate`**
  — *"Consolidate project (borrowed → owned + relativize URIs) + relink missing
  borrows."* When a project holds **borrowed** cells (external files referenced by
  URI, minted by `editor.import.paste` / `editor.import.nested`), Consolidate
  copies those files into `assets/` and rewrites their URIs to project-relative
  (a **reversible** scene transaction, D15), producing a self-contained portable
  bundle; relink surfaces a **locate-file / reveal** affordance for a moved borrow
  (the missing-borrow → placeholder half already ships from the library's load,
  `src/project/ace/project/project.hpp:75-77`, D-open-6). Effort ~2.5d;
  `depends editor.import.paste, editor.import.nested` (the borrowed-cell +
  referenced-URI machinery; the closer adds the list/cells-panel leaf that hosts
  the relink affordance); `note` citing D11/D12/D13 + design §8 (Import & assets:
  Consolidate, missing/moved → placeholder + relink) + D15 (consolidate reversible
  → a dispatched transaction). Wired into milestone **M9E** via the `editor.import`
  group. Concrete and agent-implementable **once the import leaves land** — its
  inputs (borrowed-URI cells, a library or editor URI-rewrite path) do not exist
  today, which is exactly why it cannot ride this leaf.

## Decisions

**D-gc-1 — Clean up lives in `project` (L1) as `gc_project`, thin-wired through a
`commands` orchestrator + the `ProjectGateway` seam, mirroring the `save` split.**
`project::gc_project` is pure reclamation-over-a-layout (headless
Catch2-testable); `commands::gc_project` composes it onto `AppState`;
`AppProjectGateway::clean_up()` drives the rail + confirm modal + on-close sweep.
*Rationale:* §7/§8 name `project` as *"libarbc Document, project-dir
open/save/**gc**"* and `save.hpp:57-58` already reserves the home; this is the
exact shape `save`/`save_as` proved out (L1 `project` primitive + L1 `commands`
orchestrator + L3/L4 gateway), so it needs no new seam. *Alternative rejected:*
inlining the library call in the L4 gateway — pushes I/O logic up into the
SDL-only level (harder to unit-test, against §9's "L1 logic is the bulk") and
diverges from the `save` layering; a new `persist`/`maintenance` component — a
gratuitous DAG node for one function with one internal call site.

**D-gc-2 — Root the sweep on the on-disk canonical `project.arbc` via
`arbc::gc_project_directory(layout.root, dry_run)` (guarded for zero roots), never
on a serialized live document and never publishing.** *Rationale:* process-per-project
(D19/A7) makes the on-disk canonical *this one document's* authoritative snapshot,
and the library's convenience entry marks exactly it — reclaiming precisely the
orphans accumulated across prior saves. It is **safe** even for a dirty session:
the library's model (`asset_gc.hpp:16-21`) is that a still-**resident** document's
blobs are repaired by the sink's write-if-absent on the next save (the pool tile,
in `workspace/`, is the source of truth, not the blob), and the portable core
(`project.arbc` + the `assets/` it references) is always kept complete — so neither
same-machine reopen (which reads `workspace/`) nor a cross-machine move (which
carries the canonical + its referenced assets) loses data. It is also the **only**
root testable without `editor.paint`: the mark walk reads the on-disk canonical's
*text* (`asset_gc.hpp:84-90`), so a hand-authored `project.arbc` + blob files fully
exercise it; a live-document root would require a `Document` holding owned tiles,
which needs paint (not a dependency). *Alternatives rejected:* (a) rooting on the
**live in-memory document** via `collect_referenced_tiles(serialized snapshot)` +
`sweep_tile_store` — strictly the library's Constraint-5 ideal (*"name every open
doc's current serialized state"*), but it buys nothing over the canonical root
except closing a self-healing dirty-divergence window (an undo-after-save that the
next save repairs), while forcing a live tile-bearing `Document` into every test
(→ a `editor.paint` dependency this leaf does not have) and adding a serialize
step; (b) **Save-then-GC** (publish `project.arbc`, then sweep) — makes the
canonical authoritative but re-publishes as a GC side effect, violating D13 (*"GC
touches `assets/` only"*) and D15 (*"GC is not undoable / a document edit"*) and
marking the session clean surprisingly. The **zero-roots guard** (Constraint 2) is
the one correction the convenience entry needs: with no `*.arbc` its referenced set
is empty and it would sweep everything, so `gc_project` no-ops when `canonical` is
absent. *This is why no doc delta is required:* the root choice is an
implementation decision *inside* L1 `project` that delivers D13's promised
behavior; it introduces no new dependency, seam, or behavioral deviation.

**D-gc-3 — Explicit Clean up is a previewed, confirmed op; on-close GC is silent.**
The rail action runs a `dry_run` preview → confirm modal with reclaim counts →
committed sweep; `Shell::shutdown()` runs a silent, best-effort `dry_run=false`
sweep. *Rationale:* D15 fixes GC as *"a confirmed op (not undoable)"* — an
irreversible delete earns a preview + explicit commit — while §8 fixes that GC
*also* runs *"on close"*, where a nag on every quit would be hostile for a
mandated-automatic op. The dry-run/commit split falls straight out of the library's
`dry_run` flag (`asset_gc.hpp:101-102`), which computes the identical plan without
deleting. *Alternative rejected:* immediate delete on the rail click (irreversible,
un-previewed — violates D15's "confirmed"); a confirm prompt on close (nags on a
maintenance op the design mandates run automatically).

**D-gc-4 — Ship Clean up (GC) only; split Consolidate + relink to
`editor.import.consolidate`.** *Rationale:* Consolidate and relink act on
**borrowed / referenced-URI cells**, which no editor code path can yet produce —
they arrive with `editor.import.paste` / `editor.import.nested`, and `gc` depends
only on `!save`. libarbc exposes **no** consolidate/relink API (verified: no such
symbols in the public headers) — they are editor-level, and Consolidate is a
**reversible URI-rewrite transaction** (D15) over a borrowed-cell model the import
leaves define. Building either now means machinery against a document shape nothing
generates, testable only via hand-authored fixtures and near-certain to mismodel
the cell/URI contract before import fixes it. Splitting is a WBS **sequencing**
call (the closer owns the `.tji`), not a design change: D13/§8 still describe all
three verbs; this leaf sequences them so each lands when its inputs exist. The GC
half fully realizes D13's first sentence + §8's "runs `gc_project_directory` on
explicit Clean up and on close." *Alternative rejected:* implement Consolidate now
against fabricated borrowed cells — guesses the import cell/URI model prematurely,
has no end-to-end test path, and would very likely need reworking once
`editor.import.*` lands.

**D-gc-5 — The `ProjectGateway::clean_up` return type is a dock-local `GcSummary`
POD, not `project::GcOutcome` / `arbc::GcReport`.** L4 `app` maps
`arbc::GcReport` → `project::GcOutcome` → `dock::GcSummary { reclaimed_files,
reclaimed_bytes, ran }`. *Rationale:* the gateway is declared in L3 `dock`; a
`project`/`arbc` return type would add a `dock → project` (or `dock → arbc`)
include edge for a two-field report, i.e. a `check_levels` DAG change (a doc
delta) for no real coupling benefit. A dock-local POD keeps the seam
edge-neutral, and the type-mapping (arbc → project → dock) lives in L4 `app`,
which already sees all three. *Alternative rejected:* returning
`arbc::GcReport` straight through the gateway — legal (`arbc` is whitelisted for
`dock`) but couples the UI seam to a runtime library type and blurs the "dock
speaks its own vocabulary" line the codebase keeps; returning `bool` — loses the
reclaim counts the confirm modal needs to show.

## Open questions

_None blocking — all decided against the constitution._ D13/§8 fix Clean up as
the host running `gc_project_directory` over `assets/` on explicit action + on
close, roots = this one document; D19/A7 fix the single-document root; D15 fixes
the confirmed-op flow and the not-a-transaction rule; §8 fixes the L1 homes and
the no-new-edge levelization. The one genuine degree of freedom — on-disk-canonical
root vs live-document root — is settled defensibly (D-gc-2: reuse the library
convenience entry the design names, testable without a paint dependency) and pinned
by the reclaim-orphans unit + the GC-preserves-the-canonical golden. **Doc delta:**
none — GC is D13/§8/A7-mandated, uses the shipped library API and the A13
`ProjectGateway` seam, and adds no new dependency, component, DAG edge, or
behavioral deviation. **Parking-lot items (human judgment, not WBS tasks):** (1)
whether the mandated on-close GC should be user-configurable / opt-out — a UX call
(silent deletion on every close may surprise some users), not agent-mechanical;
this leaf implements the design-stated silent on-close sweep. (2) The design's own
already-acknowledged cross-process GC root-completeness hazard (§8: *"the
cross-process case stays a host policy we do not invite (parking-lot)"*) — a
non-issue for this leaf under process-per-project (A7: one process, one document),
recorded here for continuity.

## Status

**Done** — 2026-07-18.

- `src/project/ace/project/gc.hpp` + `src/project/gc.cpp` — L1 `gc_project(layout, dry_run)`: no-canonical guard, drives `arbc::gc_project_directory`, maps `GcReport→GcOutcome` / `GcError→GcError`.
- `src/commands/ace/commands/app_state.hpp` + `src/commands/app_state.cpp` — `commands::gc_project` (not-a-transaction; no revision bump, no `mark_saved`).
- `src/dock/ace/dock/dock.hpp` + `src/dock/dock.cpp` — dock-local `GcSummary` POD; `ProjectGateway::clean_up(preview)` virtual; **Clean up…###gc** rail entry beside Save As…; confirm modal (`###gc_confirm` / `###gc_cancel`).
- `src/app/ace/app/project_gateway.hpp` + `src/app/project_gateway.cpp` — `AppProjectGateway::clean_up` (arbc→project→dock mapping).
- `src/app/shell.cpp` — silent best-effort on-close sweep in `run_editor` before teardown.
- `CMakeLists.txt` — wired new test targets + SDL X11 feature flags (`SDL_X11_XCURSOR/XINPUT/XFIXES/XRANDR/XSCRNSAVER OFF`) to unblock CI container configure.
- `tests/project_gc_test.cpp` — Catch2 units: reclaim/dry-run, no-canonical guard, fail-safe, assets-only, commands invariants, GC-preserves-canonical golden.
- `tests/gc_ui_e2e_test.cpp` — ImGui Test Engine e2e: preview→confirm-modal→commit, cancel-sweeps-nothing.
- `tests/app_project_gateway_test.cpp` — L4 `clean_up` preview/commit unit (orphan-blob fixture).
- `tests/{save_ui,save_as_ui,undo_ui,open_ui}_e2e_test.cpp` — inert `clean_up` overrides on existing fake gateways.
- Tech-debt follow-up registered: `editor.import.consolidate` (Consolidate borrowed→owned + relink; depends `editor.import.paste`, `editor.import.nested`; ~2.5d; wired under `editor.import` → M9E).
