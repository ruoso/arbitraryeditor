# editor.project.save — Save = re-dump project.arbc + assets; dirty state

## TaskJuggler entry

`tasks/00-editor.tji:122-127` — `task save` under `editor.project`. Effort
`1.5d`, `allocate team`, `depends !app_state` (`:125`, i.e.
`editor.project.app_state`). The `note` (`:126`) cites **Design D16** and
**design §9** and names this refinement. (The `.tji` note back-links to the flat
`tasks/refinements/project_save.md`; the real landing path is
`tasks/refinements/editor/save.md`, matching the existing `editor/` refinement
set — the closer fixes the note back-link per the ritual in
`tasks/refinements/README.md:57-68`, exactly as `app_state.md` / `open.md` did.)

Downstream dependents: `editor.project.gc` (`:134-138`, `depends !save` — Clean
up / Consolidate / relink runs over the `assets/` this leaf publishes) and the
new `editor.project.save_as` this refinement registers (publish + copy the
directory + exec a sibling on the copy). Every asset-owning edit leaf
(`editor.paint.*`, `editor.import.*`) assumes a working publish path for the
owned bytes it produces.

## Effort estimate

**1.5 days** (from the `.tji`). The library does the serialize heavy lifting
(`arbc::save_document` / `capture_snapshot` / `serialize_snapshot` already exist,
`build/dev/_deps/arbc-src/src/runtime/arbc/runtime/document_serialize.hpp:147-181`);
this leaf is thin wiring in three small places: (1) the L1 `project` dump
function — build the codec table + a `KindBridge`, capture on the writer thread,
write the JSON to `project.arbc` through `FileSystem::atomic_replace`, and route
owned bytes to `assets/` through a `project`-side `arbc::AssetSink`; (2) the L1
`commands` dirty model — a `saved_revision_` baseline on `AppState` + `is_dirty()`
+ a `save_project(state, fs)` entry that publishes and marks clean; (3) the
Save button + dirty indicator on the rail, wired through two new `ProjectGateway`
methods. No new component, no new library machinery, no off-thread driver.

## Inherited dependencies

**Settled (from `editor.project.app_state`).** The process owns exactly one
`AppState` (`src/commands/ace/commands/app_state.hpp:32`), from which this leaf
reads everything it needs to publish:

- `AppState::document()` (`app_state.hpp:41-42`) — the live workspace-backed
  writer `Document` to capture from.
- `AppState::layout()` (`app_state.hpp:44`) → `ProjectLayout` whose
  `canonical` = `<root>/project.arbc` (**the dump target**,
  `src/project/ace/project/project.hpp:57`) and `assets_dir` = `<root>/assets`
  (`project.hpp:58`).
- `AppState::registry()` (`app_state.hpp:46-47`) — the **persistent seeded
  `arbc::Registry`** (`register_builtin_kinds` at construction,
  `src/commands/app_state.cpp:16`). D-open-7 / D-app_state-2 hoisted this here
  *precisely* "reused by `save`/export": save builds its `CodecTable` off it, so
  it need not rebuild one.
- `AppState::rebuilt_from_canonical()` (`app_state.hpp:55`) — whether the session
  was rebuilt from `project.arbc` at open (clean) vs mapped from a possibly-drifted
  workspace. The dirty baseline reads this at construction.
- `dispatch(...)` already reads `document().journal().depth()` and
  `document().pin()->revision()` (`src/commands/app_state.cpp:26-27`) — the same
  revision signal the dirty model compares against.

**Settled (from `editor.project.open`).** `ProjectLayout` and the `platform`
error channel: `open_project`/`create_project` return `platform::Result<OpenedProject>`
(`project.hpp:99-109`), errors are values on `platform::Result<T>`
(`src/platform/ace/platform/result.hpp:14`). `create_project` **explicitly does
not write `project.arbc`** — "the canonical dump is `editor.project.save`'s
publish step" (`project.hpp:104-109`, D-open-4). The load direction constructs a
transient `KindBridge` + `FilesystemAssetSource` internally
(`src/project/project_open.cpp:87-89`) — the **write direction is the symmetric
gap this leaf fills**. The `FileSystem` seam already exposes the atomic publish:
`atomic_replace(path, content)` (`src/platform/ace/platform/filesystem.hpp:45`,
D16) and `make_directories` (`:40`).

**Settled (from `editor.project.open_ui` / A12).** The rail's project affordances
are dependency-inverted behind `dock::ProjectGateway`
(`src/dock/ace/dock/dock.hpp:38`, methods `:44-62`), declared/owned by L3 `dock`,
implemented by L4 `app::AppProjectGateway` (`src/app/ace/app/project_gateway.hpp:25`),
wired via `Dockspace::set_project_gateway(...)` (`dock.hpp:131-132`, shell
`src/app/shell.cpp:254`). `open_ui`'s Constraint 8 **explicitly deferred Save /
Save As / the dirty indicator to this leaf**.

**Pending (this leaf owns them).** The `project` dump function + a `project`-side
content-addressed `AssetSink`; the `AppState` dirty baseline + `is_dirty()` + the
`commands` save entry; the two `ProjectGateway` methods (`save`/`is_dirty`) + the
rail Save button + dirty indicator + the L4 gateway impl; and the split-out
`editor.project.save_as` leaf.

## What this task is

Turn the design's **Save** verb and **dirty indicator** into working code. Per
D16 / §9 the workspace is durable by default (crash-consistent checkpoints), so
**Save is a *publish* step, not a race against a crash**: it re-emits the
canonical, portable `project.arbc` (+ the project's owned `assets/` bytes) from
the live workspace `Document`. Concretely:

- In the **L1 `project`** component (§7: "libarbc Document, project-dir
  open/**save**/gc") add `save_project(fs, layout, doc, registry)` — capture the
  document on the writer thread (`arbc::capture_snapshot`), serialize to canonical
  `.arbc` bytes (`arbc::serialize_snapshot` / `save_document` over
  `builtin_codecs(registry)`), route any owned asset bytes to `<root>/assets/`
  through a `project`-side content-addressed `arbc::AssetSink`, and **atomically
  publish** the JSON to `<root>/project.arbc` via `FileSystem::atomic_replace`
  (D16). Errors are values.
- In the **L1 `commands`** component model **dirty state** as workspace-vs-snapshot
  drift: `AppState` gains a `saved_revision_` baseline and `is_dirty()`; a
  `commands::save_project(state, fs)` entry publishes through the `project`
  function and, on success, marks the session clean at the current revision.
- On the **rail** surface a **Save** action and a **dirty indicator**, wired
  through two new `dock::ProjectGateway` methods (`save`/`is_dirty`) whose L4
  `AppProjectGateway` impl drives the one in-process `AppState` (A13).

This leaf ships **Save + dirty**. It does **not** ship **Save As** (publish +
copy the directory + exec a sibling on the copy) — that is the new
`editor.project.save_as` leaf this refinement registers. It introduces **no new
library machinery** (D16's "how this maps onto libarbc": canonical save *is*
`project.arbc` JSON + `assets/`, already the library's interchange format).

## Why it needs to be done

`editor.project.open`/`create_project` deliberately never write `project.arbc`
(D-open-4) — a freshly created or edited project has a durable workspace but **no
portable, diffable, version-controllable snapshot** until something publishes one.
That "something" is this leaf. `open_ui` put New/Open/Recent on the rail but
explicitly left "Save / Save As / a dirty indicator" as
`editor.project.save`'s. And every asset-producing leaf downstream
(`editor.paint.*`, `editor.import.*`, and `editor.project.gc` which reaps
`assets/`) assumes a publish path exists for the owned bytes it mints. Without
Save the editor can edit durably but can never produce the portable bundle D16
promises — the whole point of "project.arbc + assets/ is the portable core"
(§9).

## Inputs / context

**Design docs (normative — the constitution).**

- `docs/00-design.md` **D16** (`:477`) — *"A project is a **directory** … Save =
  re-dump `project.arbc`. Portable core = `.arbc` + `assets/`; `workspace/` is
  machine-local scratch, rebuilt from the core."* The governing row.
- `docs/00-design.md` **§9 — "Data file vs. its dump"** (`:396-412`): *"`workspace/`
  is the **live data file** … so **the project is durable by default** (no 'lost
  work since last save'); `project.arbc` is the **dump** — the portable, canonical,
  content-addressed snapshot … So **Save = re-dump the canonical `project.arbc`**
  (+ owned assets) from the live workspace: **a publish step, not a race against a
  crash.**"* And the verb list (`:410-412`): *"New / Open … / Save (re-dump the
  snapshot) / Save As (copy the directory); recent projects; a **dirty indicator**
  reflects **workspace-vs-snapshot drift**."*
- `docs/00-design.md` **§9 "Portable core vs. local scratch"** (`:405-408`) — the
  atomic-publish + gitignore framing; `workspace/` excluded from VCS.
- `docs/00-design.md` **§8 "On disk & GC"** (`:336-344`) — owned bytes in
  `assets/` are content-addressed, dedup'd, GC'd (roots = this one document); the
  save-side `AssetSink` writes exactly those. GC itself is `editor.project.gc`.
- `docs/00-design.md` **"How this maps onto libarbc"** (`:501-520`) — *"Canonical
  save (Save) → `project.arbc` JSON + `assets/` — the doc-08 interchange /
  version-control format."* No new library machinery.
- `docs/01-architecture.md` **§7** (`:107-110`, `:126`) — *"Save → serialize to
  `project.arbc` + `assets/`"*; `project` is *"libarbc Document, project-dir
  open/save/gc"* (L1).
- `docs/01-architecture.md` **A4/§4** (`:61-82`, log `:254`) — single-writer;
  `capture_snapshot` runs on the writer thread; the background `HousekeepingThread`
  checkpoints the workspace concurrently. Save is synchronous on the UI/writer
  side (no off-UI-thread driver here — that is `editor.canvas.frame_sync`).
- `docs/01-architecture.md` **A7/D19** (log `:257`, `:480`) — one process, one
  `Document`; the one `AppState` is the natural single owner of the dirty baseline.
- `docs/01-architecture.md` **§8 / A8** (`:162-179`, log `:258`) — `project` is
  L1, may depend on `base`/`platform`/**libarbc**, ImGui/GL/SDL-free; `commands`
  L1 (whitelisted `<arbc/...>`, `scripts/check_levels.py:47`); only L3 `dock`
  sees ImGui, only L4 `app` sees SDL.
- `docs/01-architecture.md` **§9 / A9** (`:181-208`) — the layered DoD; the *L1
  logic* row is the bulk (Catch2), rendered output → golden, UI → ImGui Test
  Engine, threading → ASan/TSan.
- `docs/01-architecture.md` **A12** (log `:262`) — the `ProjectGateway` seam this
  leaf extends; **A13** (log `:263`, this leaf's doc delta) — Save + dirty join
  that seam.

**Library API surface (fetched under `build/dev/_deps/arbc-src/`).**

- `<arbc/runtime/document_serialize.hpp>` — the write path:
  `ContentSnapshot capture_snapshot(const Document&, const KindBridge&)` (`:147`,
  **writer-thread only**); `expected<std::string, SerializeError>
  serialize_snapshot(const ContentSnapshot&, const CodecTable&, SaveContext&)`
  (`:157`) and the sink-less overload (`:165`); the convenience
  `save_document(doc, bridge, codecs, ctx)` / `save_document(doc, bridge)`
  (`:170-181`); `CodecTable builtin_codecs(const Registry&)` (`:127`) and the
  `RasterTileStore*` overloads (`:134,141`); `class KindBridge` (`:45`).
- `<arbc/serialize/save_context.hpp>` — `class SaveContext` (base URI + installed
  `AssetSink` + storage format; `set_asset_sink`, `:122`) and `class AssetSink`
  (`:75`): `expected<bool, AssetSinkError> put(resolved_uri, bytes)` (`:83`, the
  bool = "actually written" vs already-present dedup), `bool contains(...)`
  (`:94`), `blobs_written()` (`:98`). `AssetSinkError{ NoSink, WriteFailed }`
  (`:52-58`). A sink-less save of a document that holds owned tile bytes fails
  loudly with `SerializeError::Kind::AssetSinkMissing` (`:159-165`) — never a
  silent pixel drop.
- `<arbc/runtime/document.hpp>` — `pin()` → `DocStatePtr` (`:261`) whose
  `revision()` (`model.hpp:49`) is the drift signal; `journal()` (`:184`);
  `checkpoint()` / `workspace_backed()` (`:219,222`) — workspace durability,
  distinct from this canonical publish.

**Source seams this leaf extends.**

- `src/project/ace/project/project.hpp` / `src/project/project_open.cpp:76-98` —
  the `project` component; the load path's transient `KindBridge` +
  `FilesystemAssetSource` (`project_open.cpp:87-89`) is the exact mirror the new
  `save_project` + `FilesystemAssetSink` follow. `ProjectLayout` at `project.hpp:53-63`.
- `src/commands/ace/commands/app_state.hpp:32-63` — `AppState` gains the dirty
  baseline + `is_dirty()`; `commands::save_project` joins the file. `commands`
  already includes `<arbc/runtime/document.hpp>` (`:9`) and depends on `project`.
- `src/dock/ace/dock/dock.hpp:38-62` — `ProjectGateway` gains `save`/`is_dirty`;
  `src/dock/dock.cpp` (rail Project section, `:87-107`, `:178`) gains the Save
  button + dirty indicator.
- `src/app/ace/app/project_gateway.hpp:25` / `src/app/project_gateway.cpp` —
  `AppProjectGateway` gains the `AppState&` + `FileSystem&` collaborators and the
  two overrides; `src/app/shell.cpp:204,239-254` wires them (the shell already
  holds `AppState& app_state` and a `NativeFileSystem`).

**Test rigs.**

- `tests/platform_test.cpp` — the `ScratchDir` temp-dir helper (reused by
  `project_open_test.cpp`, `commands_test.cpp`).
- `ace_tests` (headless Catch2, `CMakeLists.txt:177-191`) — the new
  `tests/project_save_test.cpp` and the dirty units join here.
- `ace_shell_test` (`CMakeLists.txt:198-205`) — the Save/dirty ImGui Test Engine
  e2e joins here, alongside `app_project_gateway_test.cpp`.
- `tests/goldens/render_probe_64x64.rgba8` — the existing load-fidelity golden,
  reused for the save→reload→render round-trip.

## Constraints / requirements

1. **No new component, no new DAG edge, no lint edit.** `save_project` +
   `FilesystemAssetSink` land in the existing `project` L1 component (new includes
   `<arbc/serialize/save_context.hpp>` / `<arbc/runtime/document_serialize.hpp>`
   are `<arbc/...>`, whitelisted for `project`, `scripts/check_levels.py`); the
   dirty model in `commands` (already `<arbc/...>`-whitelisted, already depends on
   `project`); the two virtuals in `dock`; the impl in `app` (L4, sees
   everything). `project` / `commands` stay ImGui/GL/SDL-free
   (`docs/01-architecture.md:176-179`). arbc links transitively via `project`
   (`CMakeLists.txt:141`) — no CMake edge added.

2. **Save is a publish, not the durability mechanism (D16/§9).** The canonical
   dump goes to `layout.canonical` (`project.arbc`); the workspace and its
   `checkpoint()` are untouched by save (durability is already the workspace's
   job). Save never blocks on, nor replaces, the `HousekeepingThread`.

3. **Atomic publish (D16).** `project.arbc` is written through
   `FileSystem::atomic_replace` (temp sibling + rename), so a crash or concurrent
   reader sees either the whole old file or the whole new one — never a
   half-written snapshot. `assets/` (and the dir) are ensured via
   `make_directories` before writing.

4. **Capture on the writer thread; serialize off the immutable snapshot (A4).**
   `capture_snapshot` runs synchronously in the UI/writer context (single-writer,
   A4); `serialize_snapshot` then reads only the pinned immutable `ContentSnapshot`.
   No new thread, no off-UI-thread submit (that is `editor.canvas.frame_sync`).

5. **Owned bytes go through a content-addressed `AssetSink` into `assets/`;
   dedup is honored.** `FilesystemAssetSink` writes each blob under its resolved
   content-addressed URI beneath `layout.assets_dir` via the injected `FileSystem`,
   returns `false` from `put` when `contains` already holds it (the "+ changed
   owned tiles" incremental behavior — an untouched blob is neither re-hashed nor
   re-written), and surfaces I/O faults as `AssetSinkError::WriteFailed`. Deletion
   of orphaned blobs is **never** a side effect of save (that is GC,
   `editor.project.gc`). The `RasterTileStore`-backed codec table
   (`builtin_codecs(registry, tiles)`) is wired **when a tile store exists** —
   `editor.paint.*` introduces it; this leaf uses `builtin_codecs(state.registry())`,
   correct for every kind the editor can currently represent (solid/probe), and
   the sink is ready for the owned-asset kinds that follow.

6. **Dirty = session revision-drift, conservative, never a false-clean
   (D16/A13).** `AppState` holds `std::optional<std::uint64_t> saved_revision_`.
   At construction: a `rebuilt_from_canonical()` session is **clean** (baseline =
   current `document().pin()->revision()`, since the workspace was just built from
   `project.arbc`); a fresh `create_project` or a workspace-mapped open is
   **dirty** (baseline `nullopt` — no known-published snapshot this session).
   `is_dirty()` = `!saved_revision_ || *saved_revision_ != document().pin()->revision()`.
   A successful publish sets the baseline to the current revision. The model is
   **session-scoped and conservative**: it never claims clean when unpublished
   edits may exist (a re-dump is cheap and idempotent, so an occasional
   false-dirty on a mapped-workspace reopen is harmless; the inverse — telling the
   user their edits are in `project.arbc` when they are not — is not). Persisting a
   cross-session published-revision so a mapped reopen of an already-published
   project reads clean is a deliberate **non-goal** (see Decisions) and would touch
   the shipped open path.

7. **Errors are values, never throws (house style).** A new
   `project::SaveError { SerializeFailed, AssetWriteFailed, IoError }` +
   `make_error_code` rides `platform::Result<T>` (mirroring `OpenError`,
   `project.hpp:70-81,133-136`); `save_document`'s `SerializeError` and the
   sink's `AssetSinkError` map into it; a failed publish returns cleanly and the
   session stays dirty (the workspace remains durable regardless).

8. **Save acts on the in-process session (A13), unlike New/Open/Recent.**
   `dock::ProjectGateway` gains `virtual bool save() = 0;` and
   `virtual bool is_dirty() const = 0;`; the L4 `AppProjectGateway` implements them
   against the one `AppState` it now holds (`save()` → `commands::save_project`,
   `is_dirty()` → `app_state.is_dirty()`). No new component or DAG edge; the rail's
   includes stay within `dock`; the L1 core never sees ImGui/SDL.

9. **Scope excludes Save As.** Copying the directory + exec-a-sibling-on-the-copy
   is `editor.project.save_as` (registered below). This leaf never mints a second
   `Document`, never copies the bundle, never execs.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella.

- **Levelization (`check_levels` clean).** `python3 scripts/check_levels.py`
  passes **with no edit**: new `project` code includes only `<arbc/...>`,
  `<ace/platform/...>`, and std; `commands` gains no non-whitelisted include;
  `dock` gains two pure-virtual signatures (no new include); the impl + wiring
  stay in `app` (L4). No new component, no new DAG edge. Primary structural
  assertion.
- **L1 logic — Catch2 unit (the bulk).** A new `tests/project_save_test.cpp`
  (headless, joined to `ace_tests`, `CMakeLists.txt:181`), reusing `ScratchDir`,
  header-comment `editor.project.save`, sentence-style `TEST_CASE`s:
  - **Publish:** `save_project` on a `create_project`'d probe/solid document
    writes `<root>/project.arbc` (`fs.exists`, non-empty, parses as canonical
    `.arbc`); `assets/` exists. Returns a `SaveOutcome` (revision + assets_written).
  - **Atomic replace:** a pre-existing `project.arbc` is replaced whole by a
    second save (old content never observed truncated; the temp sibling does not
    survive) — asserted through the `FileSystem` seam.
  - **`FilesystemAssetSink` dedup (the "+ changed owned tiles" behavior):** `put`
    of synthetic bytes writes a content-addressed blob under `assets/` and
    increments `blobs_written()`; `contains` is then true; a second `put` of the
    same bytes returns `false` and leaves `blobs_written()` unchanged; an
    unwritable target surfaces `AssetSinkError::WriteFailed`.
  - **Error values:** a save whose publish/asset write cannot happen (unwritable
    root) surfaces `SaveError::IoError`/`AssetWriteFailed` via `Result` — none
    thrown.
  - **Dirty model (in `tests/project_save_test.cpp` or extending
    `tests/commands_test.cpp`):** a fresh `create_project` `AppState` is
    `is_dirty() == true`; after `commands::save_project(state, fs)` it is
    `is_dirty() == false` and `project.arbc` exists; dispatching a
    revision-bumping `Command` flips it back to `true`; an `AppState` whose
    `rebuilt_from_canonical()` is true is `is_dirty() == false` at construction.
- **Rendered output — golden (round-trip fidelity).** The dump's faithfulness is
  pinned by **reusing** `tests/goldens/render_probe_64x64.rgba8`: `create_project`
  → build the probe content → `save_project` → `open_project` a fresh session
  from the same root forcing rebuild-from-canonical (delete/ignore the workspace)
  → `render_document_srgb8` (`src/render/ace/render/render.hpp`, from `open.md`)
  the reloaded document → **byte-exact** compare against the existing golden. No
  new golden committed (the probe is the only rendered content this leaf can
  represent; a painted-raster tile-store round-trip golden rides `editor.paint.*`).
- **UI e2e — ImGui Test Engine (Save + dirty).** A headless e2e joined to
  `ace_shell_test` (`CMakeLists.txt:201-203`) injects a **fake `ProjectGateway`**
  (scriptable `is_dirty()`, recording `save()`) into the `Dockspace` (mirroring
  `open_ui_e2e_test.cpp`'s injected-gateway pattern), drives the rail's **Save**
  button by stable widget id, and asserts (a) `save()` was invoked and (b) the
  **dirty indicator** is drawn when `is_dirty()` is scripted true and gone when
  false (+ a screenshot baseline of the two rail states where it adds signal).
- **Threading (ASan/TSan) — capture vs. the background checkpointer.** A scenario
  under the ASan and TSan presets: boot → dispatch a command → **save** (
  `capture_snapshot` on the writer thread, `serialize_snapshot` off the pinned
  snapshot, `atomic_replace` the file) → shutdown, exercising the writer-thread
  capture racing the `HousekeepingThread`'s checkpoints over the workspace, and a
  clean `AppState`/`Document` teardown. Must be sanitizer-clean. Non-N/A scope
  (this leaf is the first to *read the document for serialization* concurrently
  with housekeeping).
- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`, `coverage`
  preset), including the save success + `SaveError` branches, the sink
  put/contains/dedup + `WriteFailed` paths, and the dirty
  clean/dirty/rebuilt-from-canonical transitions.
- **Format/build.** `clang-format --dry-run --Werror` clean; `dev` and `release`
  presets build; `scripts/gate` green.
- **Named future task (closer registers in WBS).** `editor.project.save_as` —
  **Save As = publish + copy the project directory to a new location and exec a
  sibling editor on the copy** (process-per-project, A7/D19): dump the current
  state, recursively copy `project.arbc` + `assets/` to the chosen target
  (excluding `workspace/`, which the sibling regenerates), write the
  `workspace/`-excluding `.gitignore`, then hand the new directory to
  `commands::open_another_project` (`editor.project.exec_new`). Effort ~1d;
  `depends !save, editor.project.exec_new`; `note` citing D16 / §9 (Save As =
  copy the directory) + A7/D19; wired into milestone **M9E** via the
  `editor.project` group. Concrete, agent-implementable; splits the "Save As" half
  of `open_ui`'s deferral out of this leaf.

## Decisions

- **D-save-1 — The canonical dump lives in `project` (L1) as
  `save_project(fs, layout, doc, registry)`; the rest is thin wiring.** §7 names
  `project` as "libarbc Document, project-dir open/**save**/gc", it is the one L1
  component that already depends on `platform` (for `FileSystem`) and links arbc
  (for `save_document`), and its load path already owns the mirror machinery
  (`project_open.cpp` transient bridge + asset source). *Rationale:* the dump is
  pure I/O + serialization, headless and Catch2-testable; symmetry with the load
  direction keeps both halves in one component. *Alternative rejected:* putting
  save in `commands` (it is not I/O — §8 scopes `commands` to
  actions→transactions), or a new `persist` component (a gratuitous DAG node for
  one function with one internal call site).

- **D-save-2 — A `project`-side `FilesystemAssetSink` writes owned bytes to
  `assets/`; save uses `builtin_codecs(registry)` now, the tile-store overload
  when painting lands.** The sink mirrors `project_open.cpp`'s
  `FilesystemAssetSource`: content-addressed `put`/`contains`/`blobs_written` over
  the injected `FileSystem` beneath `layout.assets_dir`, honoring dedup (an
  unchanged blob is neither re-hashed nor re-written — the "+ changed owned tiles"
  incrementality). *Rationale:* the write-side asset seam (`SaveContext` +
  `AssetSink`) is exactly how the library hands finished asset bytes to the host
  (`save_context.hpp:75-98`); building it now means owned-asset kinds "just save"
  the moment they exist. Save wires `builtin_codecs(state.registry())` — correct
  for every kind the editor can represent today (solid/probe hand no bytes to the
  sink) and ready for the sink; the `RasterTileStore`-backed table
  (`builtin_codecs(registry, tiles)`) is wired by `editor.paint.*`, which
  **owns** the tile store (a store this leaf does not have). *Alternative
  rejected:* the sink-less `save_document(doc, bridge)` overload only (it fails
  loudly the moment a raster layer exists — `AssetSinkMissing`,
  `save_context.hpp:159-165` — so shipping without a sink would strand painting);
  deferring the whole asset path to `editor.paint` (leaves save architecturally
  half-built and re-opens the `project` write path later).

- **D-save-3 — A transient `KindBridge` inside `save_project`, symmetric with the
  load path; the persistent `Registry` is `AppState`'s (D-open-7/D-app_state-2).**
  `capture_snapshot` needs a `KindBridge`; the shipped load path constructs one
  transiently from the builtin registry (`project_open.cpp:87`), so save does the
  same. The `CodecTable` is built off the **persistent** `state.registry()`
  (D-app_state-2 hoisted it here "reused by save"). *Rationale:* the builtin
  registry makes the token↔kind mapping deterministic, so a fresh bridge resolves
  the document's tokens identically to load — and the save→reload→render golden
  (Acceptance) is the arbiter that pins it byte-exact. Keeping the bridge transient
  avoids adding state to the shipped `AppState`. *Alternative rejected:* owning a
  persistent `KindBridge` on `AppState` (unnecessary state today; if plugin kinds
  ever break the deterministic symmetry that is `editor.*plugins`' concern, not a
  reason to hoist state now).

- **D-save-4 — Dirty is a session-scoped revision-drift baseline on `AppState`,
  conservative toward "dirty".** `saved_revision_` is set clean at a
  `rebuilt_from_canonical` open and on each publish, and is `nullopt` (dirty) for
  a fresh create or a workspace-mapped open; `is_dirty()` compares it to the live
  revision. *Rationale:* D16 defines the indicator as "workspace-vs-snapshot
  drift", and the cheapest faithful signal is the document revision the writer
  already bumps per transaction (`app_state.cpp:26-27`). A workspace-mapped reopen
  genuinely *may* have drifted from `project.arbc` (that is exactly the
  crash-durability case D16 celebrates — edits in the workspace not yet published),
  and we cannot prove otherwise without reloading the canonical, which
  `open_project` deliberately avoids. So "dirty until you publish this session
  (unless we rebuilt from canonical)" is the honest conservative call: it never
  reports a **false clean**. *Alternative rejected:* optimistic-clean when a
  `project.arbc` merely *exists* (risks telling the user unpublished workspace
  edits are safely in the snapshot — the one error that loses work); persisting a
  cross-session published-revision sidecar in `workspace/` (would edit the shipped
  `open`/`create` path and add I/O for a precision the durable-workspace +
  idempotent-re-dump model does not need — a defensible non-goal, recorded here,
  **not** a WBS "revisit" task).

- **D-save-5 — Save + the dirty indicator join the existing `ProjectGateway`
  (A13), not new app chrome or a new seam.** Two methods (`save`/`is_dirty`) on the
  L3-declared `dock::ProjectGateway`, implemented in L4 `AppProjectGateway` against
  the one in-process `AppState`. *Rationale:* §9's verb list groups Save with
  New/Open/Save As as the front-door verbs, and the rail (D22, home base) is where
  they live; the gateway is the established seam by which L3 `dock` reaches the L4
  session without an illegal `dock → commands` edge. Save differs from the entry
  actions only in *what* it drives (the in-process session, not a sibling `exec`) —
  same seam, same dependency inversion, no new component or DAG edge. *Alternative
  rejected:* drawing Save/dirty as bespoke L4 shell chrome outside `dockspace.draw()`
  (splits project affordances across two surfaces and bypasses the home-base rail);
  a *new* `SessionGateway` seam (ceremony for two methods that fit the existing
  one). This is the A13 doc delta.

## Open questions

- _None — all decided against the constitution._ D16/§9 fix Save as an atomic
  re-dump of `project.arbc` + `assets/` from the durable workspace and the dirty
  indicator as workspace-vs-snapshot drift; §7/§8 fix the home in `project` (L1,
  no new edge) and the test model; A4 fixes writer-thread capture + synchronous
  save (the off-thread driver is `frame_sync`); A7/D19 fix the one `AppState` as
  the dirty owner; A12/A13 fix the `ProjectGateway` as the Save/dirty seam. The
  library write path is concrete in the fetched surface
  (`capture_snapshot`/`serialize_snapshot`/`save_document`/`SaveContext`/`AssetSink`).
  The one product-polish degree of freedom — precise cross-session dirty on a
  mapped-workspace reopen — is settled defensibly (conservative session-scoped
  model, D-save-4) and is **not** a WBS task; it is surfaced to the parking lot for
  the human record. **Doc delta:** `docs/01-architecture.md` **A13** (Save + dirty
  extend `ProjectGateway`) rides this task's commit; no new dependency, no new
  component, no new DAG edge, no deviation from a decided behavior.

## Status

**Done** — 2026-07-18.

- `src/project/ace/project/save.hpp` + `src/project/save.cpp`: L1 `save_project` function, `SaveError`/`SaveOutcome` types, and `FilesystemAssetSink` (content-addressed, dedup-honoring, `FileSystem`-routed per Constraint 5/A3 WASM seam).
- `src/commands/ace/commands/app_state.hpp` + `src/commands/app_state.cpp`: `saved_revision_` dirty baseline, `is_dirty()`, `mark_saved()`, and `commands::save_project` entry point added to `AppState`.
- `src/dock/ace/dock/dock.hpp` + `src/dock/dock.cpp`: two pure-virtual `save()`/`is_dirty()` methods on `dock::ProjectGateway`; rail Save button (`###save_project`) and dirty indicator (`###dirty_indicator`) added to the Project section.
- `src/app/ace/app/project_gateway.hpp` + `src/app/project_gateway.cpp` + `src/app/shell.cpp`: `AppProjectGateway` gains `AppState&` collaborator, `save()`/`is_dirty()` overrides; shell wiring updated.
- `tests/project_save_test.cpp`: Catch2 units — publish, atomic-replace, `FilesystemAssetSink` dedup + `WriteFailed`, `SaveError::IoError`/`SerializeFailed`, dirty clean/dirty/rebuilt-from-canonical transitions, publish-failure-stays-dirty, golden round-trip (`render_probe_64x64.rgba8`).
- `tests/app_project_gateway_test.cpp`: real-gateway `save`/`is_dirty` unit added.
- `tests/save_ui_e2e_test.cpp`: ImGui Test Engine e2e — fake gateway injection, Save button drive, dirty indicator presence/absence assertions.
- `tests/open_ui_e2e_test.cpp`: interface update for gateway's new `save`/`is_dirty` virtuals.
- `CMakeLists.txt`: both new test files registered; `PRIVATE nlohmann_json::nlohmann_json` added to `ace_project` (header-only, no levelization edge — the `FilesystemAssetSink`/`builtin_codecs` path requires `arbc::CodecTable` whose header names `nlohmann::json`; `check_levels` stays green).
- `docs/01-architecture.md`: A13 doc delta (Save + dirty extend `ProjectGateway`) recorded.
- **Deviation flagged:** Constraint 1 said "no CMake edge added," but `FilesystemAssetSink` + `builtin_codecs(registry)` (D-save-2) requires `nlohmann::json` transitively through `arbc/serialize/codec.hpp`; added `PRIVATE nlohmann_json::nlohmann_json` — header-only, invisible to `check_levels`, no new levelization component. Confirmed green across all matrix legs.
