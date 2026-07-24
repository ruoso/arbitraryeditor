# editor.project.raster_tile_store — Thread RasterTileStore/TileDecodeDispatch through save/open for raster memoisation

## TaskJuggler entry

- **Task:** `tasks/00-editor.tji:169-175`, `task raster_tile_store` inside
  `task project` (`tasks/00-editor.tji`, the "Project lifecycle" container under
  `task editor`).
- **Effort:** `1.5d` · `allocate team`.
- **Depends:** `editor.cells.model` — refinement
  `tasks/refinements/editor.cells/model.md`, **Done** 2026-07-22. That leaf is
  the first that puts raster cells into a saved project, which is what makes
  this one load-bearing rather than speculative.
- **Note (`.tji:173`):** *"Thread arbc::RasterTileStore (and TileDecodeDispatch)
  through project::save_project -> arbc::builtin_codecs(registry, &tiles)
  (document_serialize.hpp:134) and project::open_project ->
  arbc::load_document(..., tiles, decode) (:218) so raster-cell tiles are
  memoized across saves instead of re-hashed on every save. save.cpp:120 passes
  null tiles and project_open.cpp:97-98 passes none -- correct but O(all tiles)
  per save. editor.cells.model is the first leaf to put raster cells in a saved
  project, making this load-bearing. Source-of-debt:
  tasks/refinements/editor.cells/model.md. Design: docs/01-architecture.md
  A4/A14."*
- **Milestone:** `m9_editor` via `editor.project`
  (`tasks/99-milestones.tji:6-8`).
- **Back-link:** on completion the closer appends `complete 100` after
  `allocate team` and ends the `.tji` note with
  `Refinement: tasks/refinements/editor/raster_tile_store.md`
  (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji` here.

## Effort estimate

`1.5d`, decomposing roughly as: **0.3d** the `project` seam — mint the store in
`project::open_project` / `create_project`, carry it on `OpenedProject`, pass it
to `load_document`, add the trailing `arbc::RasterTileStore*` parameter to
`save_project`/`save_project_as`, switch `save.cpp:119` to the two-argument
`builtin_codecs`, and set `ctx.set_storage_format(doc.storage_format())`;
**0.3d** the `commands` wiring — the `AppState` member, its accessor, its
declaration-order/lifetime constraint, and the two call sites in
`app_state.cpp`; **0.7d** the tests, which are the bulk (a raster-bearing
fixture, the four incrementality cases, the byte-identity case, the
storage-format case, the Save-As case, and the ASan lifetime case); **0.2d** the
A23 doc delta, `clang-format`, and `scripts/gate`. **No new component, no new
DAG edge, no new external dependency, no libarbc fork, no pin bump.**

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cells.model`** (`tasks/refinements/editor.cells/model.md`, Done
  2026-07-22) — cell insert is `Registry`-driven with no kind allowlist (A16),
  so `org.arbc.raster` cells enter a project through `scene::add_cell` and their
  durable state rides the generic codec path (D-cells_model-9). This leaf's test
  fixture builds its raster cells through exactly that entry point. It is also
  the source-of-debt: `model.md:503-511` names this task verbatim.
- **`editor.project.save`** (`tasks/refinements/editor/save.md`, Done) —
  `project::save_project(fs, layout, doc, registry, post_writer)` in L1
  `project`; capture on the writer thread, serialize + atomic publish off it
  (D-save-1, D-writer_thread-7); owned bytes through the content-addressed
  `FilesystemAssetSink` (D-save-2); the transient `KindBridge` (D-save-3).
  **D-save-2 explicitly deferred this work** — *"save uses
  `builtin_codecs(registry)` now, the tile-store overload when painting
  lands … wired by `editor.paint.*`, which **owns** the tile store (a store this
  leaf does not have)"*. This leaf supersedes the *ownership* half of that
  projection (see D-raster_tile_store-1) while honoring its intent.
- **`editor.project.open`** (`tasks/refinements/editor/open.md`, Done) — Open =
  map the crash-durable `workspace/` when usable, else rebuild from the
  canonical `project.arbc` via `Document::create` + `load_document` +
  `checkpoint` (D-open-3); `Registry`/`KindBridge`/`AssetSource` are transient to
  the load, persistent kind-registry ownership is `app_state`'s (D-open-7);
  errors are values (D-open-6). **Constraint 7 (`open.md:285-291`) pins
  "synchronous only; no editor-owned thread, no `WorkerPool` … inline decode,
  null `TileDecodeDispatch`"** — this leaf keeps that constraint intact
  (D-raster_tile_store-4).
- **`editor.project.app_state`** — `commands::AppState` is the process's single
  project session (A7) and already owns the `Document`, the persistent
  `Registry`, the document-scoped `KindBridge` and the `HistoryPublisher`
  through a `unique_ptr` for exactly the non-movable-member reason this store
  reproduces (`src/commands/ace/commands/app_state.hpp:158-181`).
- **`editor.canvas.arbc_v030`** — the pin is libarbc **v0.3.0**
  (`CMakeLists.txt:25`), which is where `RasterTileStore`, `TileDecodeDispatch`
  and the memoizing `builtin_codecs` overloads live. No pin bump.

**Pending (explicitly out of scope, must not be assumed):**

- **`editor.paint.*`** — no painting tool exists yet. The one "a user touched
  one tile" test case reaches the pinned library's raster paint entry point
  directly, exactly as libarbc's own
  `tests/raster_tile_store_golden.t.cpp:249-289` does. This leaf ships no UI.
- **Autosave / gesture-cadence save** — the memo is the precondition libarbc
  names for it (`raster_tile_store.hpp:112-116`), but nothing in the WBS
  schedules it and D16 makes `workspace/` crash-durable by default, so save
  stays an explicit publish step.

## What this task is

Give the editor **one `arbc::RasterTileStore` per `Document`**, born with the
`Document` inside L1 `project`, carried on the existing `OpenedProject` POD into
`commands::AppState` for the process's life, and bound into both directions of
serialization: the save side switches `arbc::builtin_codecs(registry)` to
`arbc::builtin_codecs(registry, tiles)` so an untouched tile is a memo hit
instead of a re-hash, and the load side passes the store to
`arbc::load_document(...)` so a rebuild-from-canonical open **seeds** the memo
with the names it just read off disk. Along the way the save path starts
honoring the document's authored storage format
(`ctx.set_storage_format(doc.storage_format())`), which is not a separate
improvement but the precondition that makes the memo hit at all — and which
today silently re-authors a 32f painting at 16f.

The `TileDecodeDispatch` half of the `.tji` title is resolved **deliberately as
null**, not dropped: a worker-backed dispatch needs an `arbc::WorkerPool` the L1
open path may not reach and which does not exist at open time, and a null
dispatch is byte-identical to the inline serial load. That narrowing is stated,
justified against A4 and `open.md` Constraint 7, and recorded in the A23 doc
delta (D-raster_tile_store-4) — it is the whole of the deviation from the task
line as written.

## Why it needs to be done

`editor.cells.model` made raster cells insertable, so a project can now hold
real painted tiles; `editor.project.save` re-emits the whole canonical snapshot
on every Save. Without a memo the raster codec **re-hashes every tile in the
document on every save** — SHA-256 over the storage-format bytes of the entire
painting, whether or not a single pixel moved. libarbc calls this out as the
difference between an incremental save and a lie
(`arbc/runtime/raster_tile_store.hpp:112-116`) and ships the memo as the fix;
the editor simply is not using it. D13's promise — owned bytes
*"content-addressed, dedup'd"* — is half-kept today: the `FilesystemAssetSink`
already skips the **write** for a blob it has (`save.cpp:70-98`), but the
**hash** that produces the blob's name is recomputed regardless. The sink's
`contains()` check is downstream of the cost it is supposed to avoid.

Downstream, every save-adjacent leaf inherits the fix for free (`Save`, `Save
As`, the on-close GC's post-save state), and the future gesture-cadence autosave
becomes possible at all rather than becoming a per-keystroke full-document
rehash. The seeding half matters symmetrically: without it, the very first save
after any reopen pays the full O(all tiles) cost even though the process just
read every one of those hashes off disk.

## Inputs / context

**Governing design docs (normative):**

- `docs/01-architecture.md` **A4** (`:366`) — *"Adopt the library's concurrency
  verbatim — single-writer cache, leaf-only dispatch, **shared pool**,
  per-doc housekeeping. UI thread submits edits, never touches the cache."*
  This is what forbids minting a second `WorkerPool` to back a parallel tile
  dispatch (D-raster_tile_store-4).
- `docs/01-architecture.md` **A14** (`:376`) — persistence rides the **existing
  snapshot seam** (`project::save_project` → `capture_snapshot` /
  `serialize_snapshot` over the `Registry` codec table, A13). The memo attaches
  to that same seam; no new persistence path appears.
- `docs/01-architecture.md` **§8** (`:255-292`) — `project` is **L1**, may depend
  on `base, platform, libarbc`; `commands` is **L1**, may depend on
  `base, project, scene`; `render` is **L2**. All of L1 is ImGui/GL/SDL-free.
- `docs/01-architecture.md` **§9** (`:293-324`) — the layered DoD this leaf's
  Acceptance criteria instantiate.
- `docs/01-architecture.md` **A23** — *the doc delta this refinement adds*
  (`docs/01-architecture.md:385`): one store per `Document`, minted in
  `project`, released before the `Document`, dispatches null.
- `docs/00-design.md` **D13** (`:480`) — *"owned bytes in `assets/` (painted
  tiles + consolidated/pasted files) **content-addressed, dedup'd**"*.
- `docs/00-design.md` **D16** (`:483`) — *"**Save = re-dump `project.arbc`**"*;
  the workspace is the durability mechanism, save is a publish.

**Editor call sites this leaf edits (verified at HEAD):**

- `src/project/save.cpp:102-105` — `save_project(fs, layout, doc, registry,
  post_writer)`, the enclosing signature.
- `src/project/save.cpp:119` — `const arbc::CodecTable codecs =
  arbc::builtin_codecs(registry);` — **the save seam** (the `.tji`'s
  "save.cpp:120", drifted by one).
- `src/project/save.cpp:123-125` — `FilesystemAssetSink sink(fs); arbc::SaveContext
  ctx(layout.canonical.string()); ctx.set_asset_sink(&sink);` — **no
  `set_storage_format` call today**; this is where it lands.
- `src/project/save.cpp:191` — `save_project_as` delegates to `save_project`, so
  one change covers both publish directions.
- `src/project/ace/project/save.hpp:98-101, 115-119` — the two public
  declarations; `WriterPost` at `:82`; `SaveOutcome` at `:46-49`.
- `src/project/project_open.cpp:79-81` — `rebuild_from_canonical(layout,
  canonical_bytes, register_extra_kinds)`, the file-local helper.
- `src/project/project_open.cpp:108-109` — `arbc::load_document(canonical_bytes,
  *document, bridge, registry, layout.canonical.string(), &assets);` — **the
  load seam**, stopping at `assets` and defaulting both `tiles` and `decode` to
  `nullptr` (the `.tji`'s "project_open.cpp:97-98", drifted by ~11 lines).
- `src/project/project_open.cpp:167-169` — `open_project(fs, root,
  register_extra_kinds)`, the only caller of `rebuild_from_canonical`
  (`:250-251`).
- `src/project/project_open.cpp:261-297` — `create_project`, which never calls
  `load_document` (a fresh project has no tiles to seed) but must still mint an
  empty store so `OpenedProject::tiles` is never null.
- `src/project/ace/project/project.hpp:124-142` — `struct OpenedProject`
  (`document`, `layout`, `rebuilt_from_canonical`,
  `unbindable_content_records`); `:168-170` `open_project`; `:176-177`
  `create_project`.
- `src/commands/ace/commands/app_state.hpp:66` — `class AppState`; `:70-73`
  move-only with **defaulted moves**; `:158-181` the member block, whose
  `history_` comment (`:175-179`) is the exact precedent for a
  `unique_ptr`-held non-movable member.
- `src/commands/app_state.cpp:152-171` — `open_or_create_app_state`, which
  constructs `AppState(std::move(*opened))` / `AppState(std::move(*created))`;
  `:177-178` the `project::save_project` call; `:213-214` the
  `project::save_project_as` call.
- `CMakeLists.txt:25` — `ARBC_GIT_TAG "v0.3.0"` (the pin);
  `CMakeLists.txt:180-187` — `ace_component(project DEPENDS base platform LIBS
  arbc::arbc)` plus the documented PRIVATE `nlohmann_json` link;
  `CMakeLists.txt:232-263` — the `ace_tests` source list and `add_test`.

**Pinned libarbc API (v0.3.0, all in exported public headers):**

- `arbc/runtime/document_serialize.hpp:134` —
  `CodecTable builtin_codecs(const Registry&, RasterTileStore* tiles);`, doc'd:
  *"`nullptr` yields exactly `builtin_codecs(registry)` — correct, and still
  saving correct pixels, but re-hashing every tile on every save rather than
  only the ones the user actually touched."*
- `arbc/runtime/document_serialize.hpp:217-220` —
  `load_document(bytes, doc, bridge, registry, base_uri, assets, tiles, decode)`;
  `:204-216` doc'd: the load **seeds** the memo, and *"A null dispatch loads the
  decode INLINE — byte-identical to the serial load and the offline default."*
- `arbc/runtime/document_serialize.hpp:143-145, 149` — `capture_snapshot` **must
  run on the writer thread**; `serialize_snapshot` is thread-safe over the
  immutable snapshot. This is already how `save.cpp` is built (D-writer_thread-7).
- `arbc/runtime/raster_tile_store.hpp:58-122` — the full interface. Non-copyable
  and **non-movable** (it holds a `mutable std::mutex`, `:145`). Every method is
  mutex-guarded; `tiles_hashed()` is a relaxed atomic. `:36-41` states the
  concurrency contract (Constraint 10: the save runs off the editing thread on a
  pinned snapshot; the memo's retains can never resurrect a dead slot).
  `:141` — the memo holds **owning `BlockRef` pins**: *"THE validity token:
  while this lives, the slot cannot be recycled."*
- `arbc/runtime/builtin_codecs.hpp:154` — *"The host owns one `RasterTileStore`
  per `Document`."* (This header is **not** installed; the overloads the editor
  needs are re-declared in the exported `document_serialize.hpp`. Do not include
  it — it compiles only in a build-tree consume.)
- `arbc/runtime/codec_raster.cpp:117-126, 223-227` — **the codec brackets the
  pass**: `memo.begin_pass(ctx.storage_format())` … `memo.end_pass()`, with a
  local non-memoizing store as the null fallback. **The host never calls
  `begin_pass`/`end_pass`.**
- `arbc/runtime/codec_raster.cpp:430-438` — the load-side `tiles->seed(pool,
  ref, storage, hash)` loop, where `storage` is the `LoadContext`'s format.
- `arbc/runtime/document_serialize.cpp:431` — arbc's own ctx-less `save_document`
  does `ctx.set_storage_format(doc.storage_format())`; `:755-759` — a successful
  load carries the file's authored format onto the `Document`.
- `arbc/runtime/document.hpp:304-305` — `PixelFormat storage_format() const
  noexcept` / `set_storage_format`; default `Rgba16fLinearPremul` (`:509`).
- `arbc/media/pixel_format.hpp:18-22` — `Rgba32fLinearPremul`,
  `Rgba16fLinearPremul`, `Rgba8Srgb` (only the first two are permitted *storage*
  formats).
- `arbc/runtime/tile_decode_dispatch.hpp:80-135` — default ctor = inline
  executor; `TileDecodeDispatch(WorkerPool&, const void* owner)` = worker-backed,
  *"`pool` must outlive the dispatch."*

**Test patterns to copy (libarbc's own, same pinned tree):**

- `tests/raster_tile_store_golden.t.cpp:176-185` — the canonical host protocol:
  own the store, `ctx.set_storage_format(...)`, `builtin_codecs(registry,
  &tiles)`, save.
- `tests/raster_tile_store_golden.t.cpp:249-289` — *"a re-save after one dab
  writes and hashes exactly the touched tiles"*: `CHECK(sink.blobs_written() -
  written_before == 1); CHECK(tiles.tiles_hashed() - hashed_before == 1);` —
  note the `doc.drain()` after the commit, without which the published version's
  `BlockSlotRef` identity has not settled and the memo cannot hit.
- `tests/raster_tile_store_golden.t.cpp:361-365` — the seed witness:
  `CHECK(resink.blobs_written() == 0); CHECK(ltiles.tiles_hashed() == 0);`
- `tests/raster_tilewise_load.t.cpp:553` — the negative control: a **fresh**
  memo really does re-hash (`CHECK(resave_memo.tiles_hashed() == 4U);`).

**Editor tests that must stay green:** `tests/project_save_test.cpp`,
`tests/project_open_test.cpp`, `tests/save_as_test.cpp`,
`tests/project_gc_test.cpp`, `tests/commands_test.cpp` (all in `ace_tests`,
`CMakeLists.txt:232-263`); `tests/save_ui_e2e_test.cpp`,
`tests/save_as_ui_e2e_test.cpp`, `tests/open_ui_e2e_test.cpp` (in
`ace_shell_test`, `CMakeLists.txt:270-297`).

## Constraints / requirements

1. **One store per `Document`, minted with it.** `project::open_project` and
   `project::create_project` each mint an `arbc::RasterTileStore` and return it
   on `OpenedProject`; `OpenedProject::tiles` is **never null** on success. No
   other code path constructs one. (A23.)
2. **Declaration order is a lifetime rule.** The store pins `BlockRef`s into the
   `Document`'s `BigBlockPool` (`raster_tile_store.hpp:141`), so in both
   `OpenedProject` and `AppState` the store member is declared **after**
   `document`/`document_` — reverse-destruction order releases the pins before
   the pool they point into. `AppState`'s defaulted **move assignment** would
   assign `document_` before `tiles_` and therefore free the pool while the old
   memo still holds pins; the implementation must either `= delete` move
   assignment (the move **constructor**, which `open_or_create_app_state`'s
   return-by-value needs, is unaffected and must be kept) or define it to reset
   `tiles_` first. Whichever is chosen, carry the reason in a comment.
3. **Held through `std::unique_ptr`.** `RasterTileStore` is non-copyable *and*
   non-movable (mutex member), while `OpenedProject` and `AppState` are moved.
   Use `std::unique_ptr<arbc::RasterTileStore>`, the idiom `history_` already
   documents (`app_state.hpp:175-179`).
4. **The save seam.** `project::save_project` and `project::save_project_as`
   gain a **trailing** `arbc::RasterTileStore* tiles = nullptr` parameter;
   `save.cpp:119` becomes `arbc::builtin_codecs(registry, tiles)`. The default
   keeps every existing call site and test source-compatible, and a null store
   is libarbc's documented correct-but-non-memoizing behaviour
   (`document_serialize.hpp:134`) — not a silent degradation the editor invents.
   `save_project_as` forwards its pointer to `save_project` unchanged
   (`save.cpp:191`).
5. **The storage-format precondition.** `save.cpp` adds
   `ctx.set_storage_format(doc.storage_format())` beside the existing
   `set_asset_sink` (`:123-125`), mirroring arbc's own `save_document`
   (`document_serialize.cpp:431`). Without it the pass runs at `SaveContext`'s
   default while the memo was seeded at the document's authored format, so every
   entry misses — and, independently, a 32f-authored project is silently
   re-authored at 16f, renaming every blob in `assets/`. The editor still
   **authors** nothing: a new `Document` keeps libarbc's `Rgba16fLinearPremul`
   default, so shipped behaviour for editor-created projects is byte-unchanged.
6. **The load seam.** `rebuild_from_canonical` takes the store pointer from its
   caller and passes it as `load_document(..., &assets, tiles, /*decode=*/nullptr)`.
   It must **not** mint its own — the store must outlive the call and belong to
   the `Document` (contrast the transient `Registry`/`KindBridge`, D-open-7).
7. **No dispatch, no thread, no pool.** `TileDecodeDispatch` and
   `TileEncodeDispatch` stay `nullptr`. `open.md` Constraint 7 ("synchronous
   only; no editor-owned thread, no `WorkerPool`") is **kept, not amended**, and
   A4's shared-pool clause is not touched. No `#include` of
   `tile_decode_dispatch.hpp` appears in the editor.
8. **The host never brackets the pass.** `begin_pass`/`end_pass` are the codec's
   (`codec_raster.cpp:117-126, 223-227`). Editor code that calls them is wrong
   and will corrupt the pass accounting.
9. **Levelization (§8).** `project` gains
   `#include <arbc/runtime/raster_tile_store.hpp>` in its `.cpp`s and a
   `namespace arbc { class RasterTileStore; }` forward declaration in
   `project.hpp`/`save.hpp` (pointer/`unique_ptr`-to-incomplete is fine for the
   declarations; the *definition* is needed where the `unique_ptr` is destroyed,
   so `OpenedProject`/`AppState` need an out-of-line destructor or the full
   include in their TU). `commands` already names `arbc::` types
   (`app_state.hpp:158-165`). **No new component, no new DAG edge, no new link
   dependency** — `raster_tile_store.hpp` is in arbc's exported
   `PUBLIC_HEADERS` and pulls no JSON. Do **not** include
   `arbc/runtime/builtin_codecs.hpp`: it is not installed and only resolves by
   accident of the FetchContent build-tree include path.
10. **Behaviour, not just plumbing.** The observable claim is
    `RasterTileStore::tiles_hashed()` — *"Advances by exactly one per tile
    actually hashed, and not on a memo hit"* (`raster_tile_store.hpp:112-118`).
    Expose it: `AppState` gets a `tiles()` accessor (const + non-const as
    needed) so tests and any future diagnostics can read the counter. Blob
    counts alone are **not** sufficient — the sink already dedups writes, so a
    write-count-only assertion passes today with zero memoisation.
11. **Bytes on disk are unchanged.** Memoisation changes *how much work* a save
    does, never *what it writes*. A memoised save must produce a
    byte-identical `project.arbc` and an identical `assets/` blob set to a
    non-memoised one for the same document state. This is an assertion, not a
    hope (Acceptance criteria).
12. **Cold-memo paths stay correct.** A workspace-mapped open (D-open-3's fast
    path) never runs `load_document`, so its memo starts empty and the first
    save of that session hashes every tile; subsequent saves are incremental.
    `create_project` likewise starts cold. Both are correct and must be pinned
    by a test rather than left as folklore.
13. **No UI, no new user-visible behaviour, no new chrome.** Nothing on
    `dock::ProjectGateway` changes; `SaveOutcome` gains no field.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella.

- **Levelization (`check_levels` clean).** No new component and no new DAG edge:
  `project` stays `base, platform, libarbc`, `commands` stays `base, project,
  scene`. `grep -rn "imgui\|SDL\|<GL" src/project src/commands` stays empty;
  `grep -rn "builtin_codecs.hpp\|tile_decode_dispatch.hpp\|WorkerPool" src/project
  src/commands` returns nothing (the overloads come from the exported
  `document_serialize.hpp`, and no pool is reachable from L1). `CMakeLists.txt:25`
  stays at `v0.3.0` and `ace_project`'s link line at `:180-187` is unmodified.

- **L1 logic — Catch2 unit (the bulk), in a new `tests/raster_tile_store_test.cpp`**
  joined to `ace_tests` (`CMakeLists.txt:232-255`; no other CMake change). Cases,
  each asserting on `AppState::tiles()->tiles_hashed()` and
  `SaveOutcome::assets_written`:
  1. *"a re-save of an untouched raster project hashes zero tiles and writes
     zero blobs"* — insert an `org.arbc.raster` cell via `scene::add_cell`,
     save, record the counters, save again; both deltas are `0`. This is the
     headline claim and **fails today**.
  2. *"a save after one dab hashes exactly the touched tiles"* — paint into one
     tile through the pinned raster content's paint entry point, `doc.drain()`,
     save; `tiles_hashed()` advances by exactly the touched-tile count and
     `assets_written` by the same. Mirrors
     `tests/raster_tile_store_golden.t.cpp:249-289`.
  3. *"a rebuild-from-canonical reopen seeds the memo: the next save hashes
     nothing"* — save, drop `workspace/` to force D-open-3's rebuild branch (the
     manoeuvre `tests/project_open_test.cpp` already performs), reopen, save;
     `tiles_hashed() == 0` and `assets_written == 0`. Mirrors
     `raster_tile_store_golden.t.cpp:361-365`.
  4. *"a workspace-mapped open leaves the memo cold"* — reopen without dropping
     `workspace/`; the first save hashes every tile, the second hashes none.
     Pins Constraint 12 so the degradation is documented behaviour, not a
     regression someone later "fixes" by mistake.
  5. *"a memoised save is byte-identical to a non-memoised save"* — same
     document state saved into two directories, one with the store and one with
     `tiles = nullptr`; `project.arbc` bytes compare equal and the `assets/`
     blob-name sets are equal. Constraint 11; this is the leaf's *correctness*
     assertion and stands in for a rendered golden.
  6. *"save re-emits the document's authored storage format"* —
     `doc.set_storage_format(Rgba32fLinearPremul)`, save, reopen; the reloaded
     document reports `Rgba32fLinearPremul`. Constraint 5; **fails today**
     (the save silently downgrades to `Rgba16fLinearPremul`).
  7. *"Save As to a fresh directory writes every blob but re-hashes none"* —
     the destination's `assets/` is empty so the sink writes everything, while
     the shared memo means `tiles_hashed()` does not advance. Pins that the
     memo keys on pool-slot identity, not on destination.
  8. *"an AppState carrying a seeded memo destroys and moves cleanly"* — build,
     save, move-construct, destroy; the ASan lane is the assertion. Pins
     Constraint 2's release-before-the-`Document` ordering.

- **L1 wiring — Catch2, extending `tests/commands_test.cpp`.** One case:
  `open_or_create_app_state` yields an `AppState` whose `tiles()` is non-null on
  **both** the create branch and the open branch (Constraint 1).

- **Regression witnesses — existing suites unchanged.**
  `tests/project_save_test.cpp`, `tests/project_open_test.cpp`,
  `tests/save_as_test.cpp` and `tests/project_gc_test.cpp` must pass **with no
  edits**, which is the proof that the trailing defaulted `tiles` parameter is
  source-compatible and that memoisation is behaviour-preserving.

- **Rendered output — golden N/A (justified).** This leaf renders nothing and
  changes no pixels; the stronger claim — that the *bytes it writes* are
  identical with and without the memo — is asserted directly by case 5, and
  every existing golden under `tests/goldens/` is unchanged.

- **UI e2e — none new (justified); existing e2e are the wiring gate.** No user-
  visible behaviour and no `ProjectGateway` change (Constraint 13). The existing
  `tests/save_ui_e2e_test.cpp`, `tests/save_as_ui_e2e_test.cpp` and
  `tests/open_ui_e2e_test.cpp` in `ace_shell_test` must stay green — they are
  what proves the new parameter is threaded correctly through L4.

- **Threading (ASan/TSan).** The new unit test exercises `save_project` with a
  **non-empty `WriterPost`** so libarbc's writer-thread contract runs with a live
  memo attached: `capture_snapshot` on the writer thread, `serialize_snapshot`
  (and therefore `begin_pass`/`hash_of`/`record`/`end_pass`) on the caller,
  over the pinned snapshot — arbc's Constraint 10
  (`raster_tile_store.hpp:36-41`). The whole file runs in the offscreen ASan
  lane (§9.1) and under the TSan configuration; case 8 is the pin-lifetime
  witness. No new thread is created (Constraint 7).

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on the changed
  lines of `src/project/save.cpp`, `src/project/project_open.cpp`,
  `src/project/ace/project/{project,save}.hpp`, `src/commands/app_state.cpp` and
  `src/commands/ace/commands/app_state.hpp`. Tests ship with the task.
  clang-format + build clean.

- **Doc delta.** `docs/01-architecture.md` **A23** lands in the same commit
  (`tasks/refinements/README.md:47-68`).

- **Deferred WBS work: none.** This leaf is terminal for the save/load
  memoisation seam. The one thing it does not do — a worker-backed parallel tile
  dispatch — is **not** a WBS leaf, because no in-repo implementer can build it
  without an architectural change A4 forbids (D-raster_tile_store-4); it goes to
  `tasks/parking-lot.md`, along with the question of whether the editor should
  author `Rgba32fLinearPremul` for new projects (D-raster_tile_store-5).

## Decisions

- **D-raster_tile_store-1 — The store is minted with the `Document` inside
  `project` and carried on `OpenedProject`; `commands::AppState` owns it for the
  process's life.** `open_project` and `create_project` each construct a
  `std::unique_ptr<arbc::RasterTileStore>` and return it on the existing
  `OpenedProject` POD (`project.hpp:124-142`); `AppState`'s existing
  `AppState(OpenedProject&&)` constructor (`app_state.cpp:159-169`) takes it
  over, so `open_or_create_app_state`'s signature does not change at all.
  *Rationale:* libarbc's rule is *"the host owns one `RasterTileStore` per
  `Document`"* (`builtin_codecs.hpp:154`); minting the store in the same
  function that mints the `Document` makes "one per `Document`" **structural** —
  there is no code path that produces one without the other — rather than a
  convention every future caller must remember. It also puts the store where the
  seeding has to happen (inside `open_project`, before `load_document` returns)
  without adding a parameter. `AppState` is the natural long-lived owner on
  precisely the rationale that already put the persistent `Registry` and the
  document-scoped `KindBridge` there (A7, D-open-7, D-writer_thread-9).
  *Alternative rejected:* `AppState` mints the store and passes it **into**
  `open_project` as an in-parameter — the same end state, but the store's
  existence becomes a caller obligation, so a second `open_project` consumer (a
  future import or probe path) can legally produce a memo-less `Document`, and
  the invariant degrades from structural to remembered.
  *Alternative rejected:* construct a store per save inside `save_project` — no
  plumbing at all and trivially correct, but a memo that dies with the pass
  memoizes nothing; cross-save persistence *is* the feature.
  *Alternative rejected:* honour `D-save-2`'s projection and let `editor.paint.*`
  own the store — painting is not the only producer of raster tiles (insert,
  paste and import all mint them, and `editor.cells.model` already does), so a
  tool-owned memo would simply be absent from most saves, including every
  headless one. **Doc delta: A23.**

- **D-raster_tile_store-2 — The save seam is a trailing defaulted
  `arbc::RasterTileStore* tiles = nullptr` on `save_project`/`save_project_as`,
  switching `save.cpp:119` to the two-argument `builtin_codecs`.** *Rationale:*
  the raster codec binds the store **by closure** at codec-table construction
  (`codec_raster.cpp:449-461`), so the table must be built with the pointer —
  there is no post-hoc attach. A raw pointer with a `nullptr` default matches the
  library's own signature and its documented degradation (*"`nullptr` yields
  exactly `builtin_codecs(registry)` — correct … but re-hashing every tile"*,
  `document_serialize.hpp:134`), so every existing call site and all four
  shipped project test files keep compiling and passing unmodified — which is
  itself the regression witness that the change is behaviour-preserving.
  *Alternative rejected:* a required (non-defaulted) parameter — marginally
  safer against a forgotten call site, but it churns every existing test for no
  behavioural gain and makes the null case, which libarbc explicitly supports,
  feel like an error state.
  *Alternative rejected:* a `SaveOptions` struct — the right move at three or
  more knobs; at one it is ceremony (§ *"the simpler abstraction with one or two
  call sites today"*). **No further doc delta required beyond A23.**

- **D-raster_tile_store-3 — `save_project` sets
  `ctx.set_storage_format(doc.storage_format())`; the editor still authors no
  format.** *Rationale:* the memo is keyed on the on-disk storage format —
  the codec calls `memo.begin_pass(ctx.storage_format())`
  (`codec_raster.cpp:117`) and the load seeds with the `LoadContext`'s format
  (`:430-438`) — so a save that runs at `SaveContext`'s own default while the
  memo was seeded at the document's authored format misses **every** entry, and
  the whole feature silently no-ops on exactly the projects that came from disk.
  Independently of memoisation this is a latent data bug: a load installs the
  file's authored format onto the `Document` (`document_serialize.cpp:755-759`),
  so today a 32f-authored project re-saves at 16f — a precision downgrade that
  renames every blob in `assets/`. Copying arbc's own idiom
  (`document_serialize.cpp:431`) fixes both with one line. Leaving the
  `Document`'s own default alone (`Rgba16fLinearPremul`, `document.hpp:509`)
  means editor-created projects are byte-identical to today.
  *Alternative rejected:* have the editor author `Rgba32fLinearPremul` on every
  new `Document` for a lossless round-trip of the rgba32f working space — the
  quality-vs-file-size call libarbc deliberately left to the host; it changes
  every shipped project's bytes and is a product decision, not a plumbing one
  (parked, see Open questions). **Doc delta: A23** (the format-honouring clause).

- **D-raster_tile_store-4 — `TileDecodeDispatch` (and `TileEncodeDispatch`) stay
  `nullptr`; `open.md` Constraint 7 is kept, not amended.** *Rationale:* a
  worker-backed dispatch requires an `arbc::WorkerPool`
  (`tile_decode_dispatch.hpp:95-99`, *"`pool` must outlive the dispatch"*), and
  the editor has exactly one — owned by `ace::render::CanvasHost`
  (`src/render/ace/render/canvas_host.hpp:64-70`), which is **L2** and therefore
  unreachable from L1 `project`/`commands` under §8, and which does not exist
  yet when `open_or_create_app_state` runs. Minting a second pool inside
  `project` would contradict A4's *shared pool* clause and add the editor-owned
  thread `open.md` Constraint 7 forbids. A null dispatch decodes **inline,
  byte-identically** to the serial load (`document_serialize.hpp:212`), so the
  null is a complete implementation of the seam at the editor's current
  concurrency shape — and the O(all tiles) cost the `.tji` note names lives
  entirely in the **hash**, which the memo removes, not in the decode, which
  runs at most once per process and only on the rebuild-from-canonical branch.
  This narrows the `.tji` title's "and TileDecodeDispatch" to an explicit,
  justified null rather than dropping it silently.
  *Alternative rejected:* thread a `TileDecodeDispatch*` parameter through
  `open_project` anyway, defaulted to null, so the seam exists for a future
  caller — a parameter with zero call sites, untestable and uncovered, for a
  consumer that cannot legally exist until the pool question is answered.
  *Alternative rejected:* construct a default (inline) `TileDecodeDispatch` and
  pass it — byte-identical to null by the library's own statement, so it buys an
  include, a lifetime obligation and a line of code for no observable change.
  **Doc delta: A23** (clause 4).

- **D-raster_tile_store-5 — `tiles_hashed()` is the acceptance witness; blob
  counts alone are not.** `AppState` exposes the store so tests read the counter
  directly. *Rationale:* libarbc names this precisely — *"the STRONGER of the
  two incremental-save witnesses: write-if-absent alone would give the right
  blob count while still re-hashing the whole document on every save"*
  (`raster_tile_store.hpp:112-116`). The editor's `FilesystemAssetSink` already
  dedups writes (`save.cpp:70-98`), so an `assets_written`-only assertion passes
  today with zero memoisation — it would test nothing. Both counters are
  asserted; the hash counter is the one that can fail.
  *Alternative rejected:* surface the counter on `SaveOutcome` as a new field —
  a public API widened for a test's benefit, when the store is already reachable
  through its owner. **No doc delta required.**

- **D-raster_tile_store-6 — The store is `unique_ptr`-held, declared after the
  `Document`, and `AppState`'s move **assignment** is constrained.**
  *Rationale:* `RasterTileStore` holds a `std::mutex` so it is neither copyable
  nor movable, while both `OpenedProject` and `AppState` are moved — the exact
  situation `history_`'s comment already documents
  (`app_state.hpp:175-179`). More sharply, the memo holds **owning `BlockRef`
  pins** into the `Document`'s pool (`raster_tile_store.hpp:141`), so releasing
  it after the `Document` is use-after-free. Declaration order handles
  destruction; move **assignment** does not (a defaulted one assigns `document_`
  first, freeing the pool while the old memo still pins into it), so it is
  either deleted or defined to reset the store first. The move **constructor**
  is kept — `open_or_create_app_state` returns by value.
  *Alternative rejected:* rely on the fact that A7 means an `AppState` is never
  move-assigned in practice — true today, and a silent use-after-free the first
  time it stops being true. **Doc delta: A23** (the release-before-the-`Document`
  clause).

- **D-raster_tile_store-7 — A cold memo on the workspace-map path is accepted
  and pinned by a test, not worked around.** D-open-3's fast path maps
  `workspace/` and never calls `load_document`, so nothing seeds the memo and
  the first save of such a session re-hashes the whole document once; every save
  after it is incremental. *Rationale:* seeding the memo from a mapped workspace
  would mean hashing every tile at map time — paying the exact cost the memo
  exists to avoid, eagerly, on a path where the user may never save. Correct,
  bounded (once per process), and cheaper than the alternative.
  *Alternative rejected:* eagerly hash the mapped document at open to warm the
  memo — moves the cost from a save the user asked for to an open they did not.
  **No doc delta required.**

### Named future task (closer registers in WBS)

(none — this leaf is terminal for the save/load memoisation seam. The two
unresolved items are human-judgment calls, not implementable leaves; see Open
questions.)

## Open questions

(none — all decided.)

Two items are surfaced for `tasks/parking-lot.md` (the human-review queue),
**not** as WBS leaves — neither is work an in-repo implementer can close:

1. **Should the editor ever run a worker-backed parallel tile decode/encode, and
   where would the pool come from?** D-raster_tile_store-4 keeps the dispatches
   null because the only `arbc::WorkerPool` is L2 `render::CanvasHost`'s and does
   not exist at open time, and A4 says the editor adopts the library's
   concurrency *verbatim* with a **shared** pool. Answering it means deciding
   whether the pool's lifetime moves below `render` (an A4/A5 amendment) or
   whether cold-open latency on large raster projects is simply accepted. That
   is a design judgment, not a kernel to write.
2. **Should new editor projects author `Rgba32fLinearPremul` instead of
   libarbc's `Rgba16fLinearPremul` default?** D-raster_tile_store-3 fixes the
   editor to *honour* the authored format but deliberately authors nothing.
   Choosing 32f is lossless against the rgba32f working space; it also roughly
   doubles `assets/` and changes every shipped project's bytes. libarbc left the
   call to the host on purpose (`raster_tile_store_golden.t.cpp:378-379`,
   *"Lossy from an rgba32f working space, by the user's authored choice"*), and
   it interacts with D10's colour boundary and D13's portability story — a
   product/quality decision.

**No further doc delta required beyond A23:** no new dependency, no new
component, no new DAG edge, and the one deviation from a decided behaviour
(`D-save-2`'s projected ownership by `editor.paint.*`) is recorded there.

## Status

**Done** — 2026-07-24.

- `src/project/ace/project/project.hpp` — `OpenedProject` gains `std::unique_ptr<arbc::RasterTileStore> tiles`; forward-declares `arbc::RasterTileStore`; out-of-line destructor ensures the full type is seen in the TU where `unique_ptr` is destroyed.
- `src/project/ace/project/save.hpp` — `save_project` and `save_project_as` gain a trailing `arbc::RasterTileStore* tiles = nullptr` parameter (default keeps all existing call sites source-compatible).
- `src/project/project_open.cpp` — `open_project` and `create_project` each mint a `std::unique_ptr<arbc::RasterTileStore>` and return it on `OpenedProject`; `rebuild_from_canonical` receives and passes `tiles` to `arbc::load_document(...)` to seed the memo; includes `<arbc/runtime/raster_tile_store.hpp>`.
- `src/project/save.cpp` — `builtin_codecs(registry)` → `builtin_codecs(registry, tiles)`; adds `ctx.set_storage_format(doc.storage_format())` alongside `set_asset_sink`; `save_project_as` does **not** forward the store (destination-scoping rule; A23 clause 2 corrected).
- `src/commands/ace/commands/app_state.hpp` — `AppState` gains `std::unique_ptr<arbc::RasterTileStore> tiles_` declared after `document_`; `tiles()` accessor (const + non-const); move assignment deleted (release ordering); includes the forward decl.
- `src/commands/app_state.cpp` — `AppState(OpenedProject&&)` takes over `tiles_`; `save_project` and related call sites pass `tiles_.get()`.
- `docs/01-architecture.md` — A23 updated: one store per `Document`, minted in `project`, released before the `Document`, dispatches null, destination-scoping rule for Save As.
- `CMakeLists.txt` — `tests/raster_tile_store_test.cpp` added to `ace_tests` source list.
- `tests/raster_tile_store_test.cpp` (new) — 8 Catch2 cases: untouched re-save hashes 0 blobs/tiles; one-dab hashes exactly 1; rebuild-from-canonical seeds the memo; workspace-map leaves it cold; memoised == non-memoised bytes+blob-set; authored storage format survives round-trip; Save As publishes a complete copy; seeded-memo move/destroy (ASan lifetime witness). Plus 2 `static_assert`s pinning move-ctor-kept/move-assign-deleted.
- `tests/commands_test.cpp` — one new case: `open_or_create_app_state` always carries a non-null `tiles()` on both create and open branches.
- `tasks/parking-lot.md` — two entries appended: worker-backed parallel tile decode/encode (D-raster_tile_store-4) and `Rgba32fLinearPremul` authoring question (D-raster_tile_store-3/5).

**Deviation:** `save_project_as` takes no store; a memo hit is resolved inside the memo and never reaches the `AssetSink`, so forwarding the session memo into a fresh root would name tile blobs never written there. Case 7 became the regression witness that the copy is complete and the session memo is intact. A23 clause 2 documents the destination-scoping rule.
