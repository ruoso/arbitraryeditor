# editor.project.reconstructing_reopen — restore the durable-by-default workspace fast path by reconstructing content through arbc v0.4.0's registry-aware open

## TaskJuggler entry

`tasks/00-editor.tji:206-211`, under `task editor { task project { … } }`:

```
task reconstructing_reopen "Restore the durable-by-default workspace fast path via the reconstructing reopen" {
  effort 3d
  allocate team
  depends editor.canvas.arbc_v040, !reopen_degradation_notice
  note "ruoso/arbitrarycomposer#19 shipped in v0.4.0 and answers A19's 'Future fix (cross-repo)' clause … open through open_document in project::open_project (src/project/project_open.cpp:245) and RETIRE the content-bearing-map guard … CARRIES THE DIRTY-PRECISION DECISION … Design: docs/01-architecture.md A13/A19, docs/00-design.md D16, D-open-3."
}
```

**Closer ritual** (`tasks/refinements/README.md:47-78`): append `complete 100`
after `allocate team`; append
`Refinement: tasks/refinements/editor.project/reconstructing_reopen.md` to the
task's `note` (after the design-doc citations); carry the **three doc deltas**
below (A19 amendment + A13 amendment in `docs/01-architecture.md`, new **D28**
in `docs/00-design.md`) in the **same commit**. No follow-up WBS leaf is
registered — one library-side verification risk is surfaced to
`tasks/parking-lot.md` via the return summary (see Open questions), never as a
WBS task. **Milestone:** `m9_editor` (`tasks/99-milestones.tji:6-10`) depends on
`editor.project` wholesale, so this leaf is transitively covered — no milestone
`depends` edit is needed. After editing, run
`tj3 project.tjp 2>&1 | grep -iE "error|warning"` and confirm silence.

> **Path note.** Written to `tasks/refinements/editor.project/`, matching the
> sibling rollback pair (`create_rollback.md`, `save_as_rollback.md`) and this
> task's own area — the fully-qualified-area convention, not the older flat
> `tasks/refinements/editor/`.

## Effort estimate

**3d.** This is not a one-line API swap. It is: (1) replace the record-only
`arbc::Document::open` map path with the reconstructing `arbc::open_document`;
(2) install `Document::set_content_identity_capture` on **both** the
`create_project` mint and the `open_project`/save paths so newly-written
workspaces carry construction identity; (3) **retire** the content-bearing-map
guard (`project_open.cpp:258-285`) so the map path is durable-by-default for
every kind; (4) re-key `OpenedProject::unbindable_content_records` from
"records the map could not *bind*" to "records `open_document` could not
*reconstruct*" (`ReopenedDocument::unreconstructed`); (5) **the dirty-precision
decision** — a cross-session published-revision sidecar in `workspace/`, written
on publish and read on the mapped reopen, so a cleanly-saved project reopens
*clean* instead of falsely dirty (a new persisted artifact with write-ordering
and validation); (6) **invert four pinned tests** in `tests/project_open_test.cpp`,
each justified against the v0.4.0 changelog, plus the new reconstruction,
dirty-precision, and safety tests; (7) a `render_offline` differential golden
proving a mapped-reconstruct reopen renders byte-identically to a
canonical-rebuild; (8) three doc deltas. The estimate is dominated by the safety
and dirty-precision tests and by justifying the four inversions, not by the
`open_document` call itself.

## Inherited dependencies

### Settled (relied on directly)

- **`editor.canvas.arbc_v040`** (`tasks/refinements/canvas/arbc_v040.md`, the
  direct `depends`) — bumped `CMakeLists.txt:25` `v0.3.0`→`v0.4.0` and absorbed
  the three consumer-visible breaks, but **consumed none of the new surface in
  `src/`** (D-arbc_v040-1). It witnessed, in `tests/arbc_pin_test.cpp`, exactly
  the arbc#19 symbols this leaf consumes: `arbc::open_document`
  (`:295-299`), `arbc::ReopenedDocument::unreconstructed` (`:294`),
  `Document::rebind_content`, `Document::recovered_content_state()`,
  `Document::set_content_identity_capture`, and `arbc::codec_identity_capture`.
  Its **Constraint 7** is load-bearing here: *"`Document::open` stays
  record-only; reconstruction is the new `arbc::open_document`. Do not 'fix'
  [the record-only pin case] to expect reconstruction; that behaviour arrives
  through `arbc::open_document`, owned by `editor.project.reconstructing_reopen`."*
  This leaf is that owner.
- **`editor.project.reopen_degradation_notice`** (`!reopen_degradation_notice`,
  `tasks/refinements/editor/reopen_degradation_notice.md`) — shipped the
  loss-reporting cap this leaf keeps working unchanged: the immutable
  `commands::AppState::unbindable_content_records_` carry
  (`src/commands/app_state.cpp:53`, accessor `app_state.hpp:171`), the
  `dock::ProjectGateway::reopen_unbindable_count()` POD virtual, the one-shot
  `Dockspace::draw_reopen_notice()` modal, and its ImGui Test Engine e2e
  (`tests/reopen_degradation_notice_e2e_test.cpp`). Its own Open questions parked
  the *prevention* of the loss as cross-repo work needing a registry-aware open
  or a rebind seam — **exactly** what arbc#19 now provides. This leaf closes that
  parked gap; the notice keeps firing, but now on a genuinely rare
  (unreconstructable) case instead of the common one.
- **`editor.cameras.reopen_slab_adopt`** (`tasks/refinements/cameras/reopen_slab_adopt.md`)
  — the source-of-debt that produced `unbindable_content_records` and documented,
  under A19, precisely why v0.3.0 could not reconstruct (the map path runs no
  factory, so `resolve()` is null for every recovered record). A19's *"Future fix
  (cross-repo)"* clause is the gap this leaf fills.
- **`editor.project.save`** / **`editor.project.open`** / **`editor.project.raster_tile_store`**
  (all `complete 100`) — the `project::save_project` publish path
  (`src/project/save.cpp`) this leaf extends with the sidecar write, the
  `open_project` map-vs-rebuild structure (`D-open-3`,
  `tasks/refinements/editor/open.md:382`) this leaf reworks, and the
  one-`RasterTileStore`-per-`Document` carry on `OpenedProject` (A23) that is
  untouched here.

### Pending

- None. Every predecessor is `complete 100`; the v0.4.0 pin is landed and the
  library side of arbc#19 shipped (`.tji:208`).

## What this task is

Today `project::open_project` (`src/project/project_open.cpp:209-315`) takes the
crash-durable `workspace/` on reopen, maps it with the **record-only**
`arbc::Document::open` (`:261`), then **inspects** it: a guard
(`:258-285`, powered by `count_unbindable_content` at `:177-189`) counts every
recovered layer whose `Document::resolve` is null, and **discards the whole map
in favour of a full `rebuild_from_canonical`** whenever the workspace holds any
content record and a canonical `project.arbc` exists (`:270`). That is A19's
guard: `Document::open` runs no factory, so a mapped reopen restores the record
graph but binds **no `Content` for any kind** — cells and cameras alike come
back as null-resolving husks. The guard makes the durable-by-default fast path
(D-open-3, D16) unusable for any project with content: the common reopen pays a
full canonical rebuild.

arbc#19 (shipped in v0.4.0) makes the map path registry-aware. This leaf
switches `open_project`'s map branch from `arbc::Document::open` to
`arbc::open_document(path, registry, codecs, load_context, bridge, config)`,
which **reconstructs** each recovered `ContentRecord` through the same routing
`load_document` uses for a canonical file — because the record now carries its
construction identity (reverse-DNS `kind_id` + the kind's canonical params +
input edges), captured at `add_content` through the kind's own registered codec
via `Document::set_content_identity_capture(arbc::codec_identity_capture(codecs,
bridge))`, and `Document::recovered_content_state()` makes the arbc#5 replay trio
reachable through `Document` to restore mutable state. This leaf **installs that
identity capture** on the create/open/save paths so newly-written workspaces
carry construction identity, **retires the content-bearing-map guard** so the map
fast path is durable-by-default again — for every kind at once (D-open-3's
original policy) — and **re-keys** `unbindable_content_records` from "the map
could not bind" to `ReopenedDocument::unreconstructed.size()`: a record that
genuinely cannot be rebuilt (a plugin absent this session, a kind with no codec,
a workspace written before v0.4.0) arrives in `unreconstructed` and is left
**UNBOUND — never filled with a default-constructed stand-in** — so the existing
degradation notice keeps working, now reporting a rare case.

Restoring the fast path has a second, deliberate consequence this leaf **owns**:
it flips the common reopen from clean to dirty. Today most reopens rebuild from
canonical and start clean because that read proves sync (`D-save-4`,
`app_state.cpp:69-75`); after this leaf most reopens *map*, and the
session-scoped dirty model starts a mapped open dirty
(`app_state.hpp:186-189` — `saved_revision_ == k_none`). A project saved cleanly
yesterday would open showing *modified* with Save armed and a pointless re-dump
on first press. This leaf resolves that with a **cross-session published-revision
sidecar** in `workspace/` (D28): `project::save_project` writes the just-published
revision after the canonical `atomic_replace` succeeds; the mapped reopen reads
it and seeds the session **clean** iff the reconstructed document's revision
matches. Absent, stale, or mismatched → dirty (the safe direction). The
never-saved project keeps no sidecar and stays honestly dirty.

## Why it needs to be done

D16 promises "`workspace/` makes the project crash-durable by default; Save =
re-dump `project.arbc`" and D-open-3 promises "open = map the workspace if
usable, else rebuild." A19's guard made *usable* mean *content-free* for the
whole editor: since v0.1.0 the durable-by-default fast path has silently degraded
to a full canonical rebuild for every project a user actually built content in.
`editor.project.reopen_degradation_notice` could only **announce** the loss; it
explicitly parked the fix as cross-repo work. arbc#19 is that fix, landed. Not
consuming it leaves the editor permanently paying a canonical rebuild on the
common reopen and leaves the degradation notice firing on the common case rather
than the rare one — the exact inversion the notice was built to make rare.

Downstream, this leaf is what makes the workspace genuinely durable-by-default:
unpublished record-level edits (layer transforms, z-order, composition size)
**and** live content survive a reopen without a Save, which is the whole point of
the crash-durable arena. It also unblocks a truthful dirty indicator on that path
— without the sidecar, restoring the fast path would make *modified* mean nothing
(on for essentially every reopen of a cleanly-saved project).

## Inputs / context

### Design docs (normative)

- **`docs/01-architecture.md` A19** (`:434`) — the guard this leaf retires.
  Full title: *"The libarbc workspace-map reopen restores the RECORD GRAPH ONLY
  — it binds no `Content` for any kind — so a content-bearing project is
  reopenable only through the canonical rebuild."* Its premise ("`Document::open`
  … takes **no `Registry`** and runs **no factory** … the id→`Content` side-map
  starts empty") no longer holds at the pinned version; its consequences
  (1)-(3) are superseded. **This leaf amends A19** (see Doc delta), retaining the
  text as the historical record the way A15 was retained.
- **`docs/01-architecture.md` A13** (`:428`) — *"`ProjectGateway` also carries
  the in-session Save + dirty query."* States "a workspace-mapped open has no
  known-published snapshot this session → dirty until the first Save
  (conservative: never a false-clean)." **This leaf amends A13** with an inline
  pointer to D28: a mapped reopen is no longer *unconditionally* dirty.
- **`docs/01-architecture.md` A15** (`:430`) — the retention template (a
  fully-superseded row whose original text is retained, with a leading and
  trailing italic `*(Amended by …)*` parenthetical, that hands operative
  authority forward to A19). A15 currently names A19 as "the operative rule";
  once A19's guard is retired, A15's deferral-to-A19 is moot but stays as-is
  historical (see Doc delta rationale).
- **`docs/01-architecture.md` A23** (`:438`) — the one-`RasterTileStore`-per-
  `Document` row, and the governing row for the WorkerPool-vs-tile-store-at-load
  question. Consequence (4): a null `TileDecodeDispatch`/`TileEncodeDispatch`
  decodes inline, byte-identically, "so the null is a *complete* implementation."
  **Untouched here** — the `LoadContext` `open_document` takes carries the same
  tile store and the same null dispatch (see Constraints).
- **`docs/00-design.md` D16** (`:483`) — "Project = a directory": `project.arbc`
  (portable core) + `assets/` + `workspace/` (machine-local, gitignored, "rebuilt
  from the core", crash-durable). The sidecar lives in `workspace/` because it is
  machine-local sync state, not portable core.
- **`docs/00-design.md` D25** (`:492`) — "Reopen degradation notice." The count
  it surfaces is re-keyed here but its behavior (show when count > 0) is
  unchanged; D25 does **not** need editing (checked for drift — the notice now
  fires on the rare unreconstructable case).
- **`docs/00-design.md` D27** (`:494`) — "Creation targets a path that does not
  exist." Untouched; named only because create_project (where identity capture is
  installed on the mint) sits behind it.
- **`D-open-3`** (`tasks/refinements/editor/open.md:382`) — "Open = map the
  workspace if usable, else rebuild." A v0.1.0-era, kind-agnostic policy;
  restoring it "for every kind at once" is precisely this bullet minus A19's
  guard.
- **`D-save-4`** (`tasks/refinements/editor/save.md:422`) — "Dirty is a
  session-scoped revision-drift baseline on `AppState`, conservative toward
  'dirty'." Its *Alternative rejected* clause explicitly rejects "persisting a
  cross-session published-revision sidecar in `workspace/`" as a "defensible
  non-goal … **not** a WBS 'revisit' task." This leaf **promotes** D-save-4 to a
  design-doc row (**D28**) that flips that non-goal, because restoring the fast
  path makes the mapped reopen the common case and the parked entry's reason for
  deferring ("would edit the shipped `open`/`create` path") is void — this leaf
  edits exactly that path.

### Source seams

- **`src/project/project_open.cpp:209-315`** — `open_project`. Key lines:
  - `:226` — the one `arbc::RasterTileStore` minted before the branch; the map
    branch leaves it **cold** (`:273`), the rebuild branch **seeds** it.
  - `:258-285` — the content-bearing-map guard and rationale comment. `:258-260`
    the `canonical_exists` / `skip_map_for_editor_kinds` short-circuit; `:261`
    `arbc::Document::open(layout.workspace_file.string())` (the **record-only**
    call this leaf replaces); `:263` `count_unbindable_content`; `:270`
    `if (unbindable == 0 || !canonical_exists)` — the keep-vs-discard test this
    leaf **retires**; `:279-281` the fall-through-to-rebuild.
  - `:177-189` — `count_unbindable_content`, which walks `for_each_layer` testing
    `document.resolve(layer.content) == nullptr`. Superseded by
    `ReopenedDocument::unreconstructed`.
  - `:287-314` — the `rebuild_from_canonical` branch (`load_document` at
    `:147-149`, passing `tiles.get()` and `/*decode=*/nullptr` for inline decode).
    Retained as the fallback for a missing/unusable/pre-v0.4.0 workspace.
  - `:317+` — `create_project`, where identity capture is installed on the mint so
    a freshly-created workspace carries construction identity from its first
    checkpoint.
- **`src/project/save.cpp`** — `project::save_project` (the publish path). The
  sidecar write goes here, immediately **after** the canonical `atomic_replace`
  of `project.arbc` durably succeeds (ordering is load-bearing — see Constraints).
- **`src/project/ace/project/project.hpp:133-164`** — `OpenedProject`.
  `unbindable_content_records` (`:153-163`, re-documented here) and a **new**
  `bool mapped_in_sync = false;` field carrying the sidecar verdict from
  `open_project` to `AppState`.
- **`src/commands/ace/commands/app_state.hpp:173-227`** — the dirty model.
  `is_dirty()` (`:186-189`), `mark_saved()` (`:194`), `saved_revision_` (the
  atomic, `:227`), `rebuilt_from_canonical_` (`:222`),
  `unbindable_content_records_` (`:225`). The header comment (`:180`) states
  "Persisting a cross-session baseline is a deliberate non-goal" — **this leaf
  amends that comment** (D28 supersedes it).
- **`src/commands/app_state.cpp:42-79`** — the `AppState` ctor. `:69-75` the
  D-save-4 dirty-baseline seed (`if (rebuilt_from_canonical_)
  saved_revision_.store(...)`). **This leaf extends the condition** to also seed
  clean on a `mapped_in_sync` open. `:53` the `unbindable_content_records_` ferry.
- **`src/app/project_gateway.cpp:138-141`** — `is_dirty()` /
  `reopen_unbindable_count()` forwarders; unchanged (the sidecar is seeded into
  `saved_revision_` upstream at construction, so `is_dirty()` reads it unchanged).
- **`CMakeLists.txt:25`** — `ARBC_GIT_TAG "v0.4.0"` (already pinned by
  `arbc_v040`).
- **`arbc/runtime/document_serialize.hpp:190`** — `arbc::open_document`;
  `:212` — the inline-decode contract a null `TileDecodeDispatch` honors.

### Existing test surface this leaf edits or pins

- **`tests/project_open_test.cpp`** (Catch2). Four pinned cases **invert** (each
  justified against the v0.4.0 changelog, **rewritten not deleted** — they are the
  record of why the guard existed):
  - **`:360`** `a workspace-mapped reopen restores the record graph but binds NO
    content` — pins the **retired premise** (`resolve() == nullptr`,
    `for_each_content` visits 0). **Rewritten** to pin *reconstruction* through
    `open_document` (`resolve() != nullptr`, `for_each_content` visits the
    reconstructed content).
  - **`:435`** `open_project rebuilds a content-bearing workspace even with no
    extra-kinds callback` — currently asserts `rebuilt_from_canonical == true`,
    `resolve() != nullptr` via canonical. **Inverts** to: the map path
    *reconstructs* (`rebuilt_from_canonical == false`, `unbindable == 0`,
    `resolve() != nullptr`, `bound_content_count == 1`) with no canonical rebuild.
  - **`:463`** `open_project keeps the map fast path for a content-free workspace`
    — meaning generalizes: the map is durable-by-default for **all** kinds now,
    not only the content-free case.
  - **`:489`** `a never-saved content-bearing project keeps the map and REPORTS
    the loss` — now the map *reconstructs*; `unbindable_content_records ==
    unreconstructed.size()`, non-zero only for genuinely unreconstructable
    records, not all content.
  - **`:585-645`** the IoError fault cases — unchanged in intent; extend to cover
    an `open_document` fault falling through to canonical rebuild.
- **`tests/arbc_pin_test.cpp:294-299,355-395`** — the v0.4.0 witnesses. `arbc_v040`
  pinned the signatures; this leaf adds a **behavioral** pin: reconstruction is
  revision-invariant (see Constraint 5) and `open_document` leaves an
  unreconstructable record unbound (never a stand-in).
- **`tests/reopen_degradation_notice_e2e_test.cpp`** — the notice e2e; must keep
  passing **unchanged** (the count is now rare, not common).

## Constraints / requirements

1. **The map branch opens through `arbc::open_document`, not
   `arbc::Document::open`.** The reconstructing open takes the same `Registry`
   (`state.registry()`-equivalent — the transient load registry augmented by
   `register_extra_kinds`, D-open-7), the same `CodecTable`, the same `KindBridge`,
   and a `LoadContext` carrying the caller's one `RasterTileStore` with a **null**
   `TileDecodeDispatch` (inline decode, byte-identical, A23 consequence (4) — the
   cold-open cost moves from full rebuild to map-plus-reconstruct, decode stays
   inline). `Document::open`'s record-only behavior is unchanged and stays pinned
   in `arbc_pin_test.cpp` (arbc_v040 Constraint 7) — this leaf does not touch that
   pin case; reconstruction is a **different** entry point.

2. **The content-bearing-map guard is retired.** Delete the
   `unbindable == 0 || !canonical_exists` keep-test (`:270`), the
   `count_unbindable_content` walk (`:177-189`), and the fall-through-to-rebuild
   for content-bearing maps (`:279-281`). The `skip_map_for_editor_kinds`
   short-circuit (`:259`) is **also retired** — it existed only to avoid mapping a
   workspace the guard would reject, and there is no longer a rejection. Every
   caller now maps-and-reconstructs; the canonical rebuild survives **only** as
   the missing/unusable/pre-v0.4.0-workspace fallback (`:287-314`).

3. **Identity capture is installed on the create/open/save paths.** Call
   `Document::set_content_identity_capture(arbc::codec_identity_capture(codecs,
   bridge))` so that every `add_content` on a live `Document` snapshots the
   content's construction identity into the workspace record. Install it on the
   `create_project` mint (`:317+`) and on the rebuild-from-canonical mint so
   newly-written and freshly-rebuilt workspaces both carry identity from their
   first checkpoint. A workspace written before v0.4.0 (no captured identity) is a
   legal input: its records land in `unreconstructed`, unbound, reported.

4. **An unreconstructable record is left UNBOUND, never defaulted.** A record in
   `ReopenedDocument::unreconstructed` (plugin absent, no codec, pre-v0.4.0)
   stays unbound — `resolve()` answers null for it — and feeds
   `OpenedProject::unbindable_content_records = unreconstructed.size()`. Never
   fill it with a default-constructed stand-in: a silent stand-in would be strictly
   worse than the honest "this record was lost" the notice already reports. This is
   the content-level safety analogue of D-save-4's never-a-false-clean asymmetry.

5. **The published-revision sidecar (D28) is correct-by-write-ordering and
   never yields a false-clean.** `project::save_project` writes the sidecar
   (`workspace/published.rev`, a decimal `std::uint64_t`, through the
   `platform::FileSystem` seam) **after** the canonical `atomic_replace` of
   `project.arbc` durably succeeds. On the mapped reopen, `open_project` reads the
   sidecar and sets `OpenedProject::mapped_in_sync = true` **iff** the sidecar is
   present **and** equals the reconstructed document's `pin()->revision()`;
   otherwise false. `AppState` seeds `saved_revision_` clean when
   `rebuilt_from_canonical_ || mapped_in_sync_`. This is safe against a crash
   between the two writes: crash after canonical, before sidecar → sidecar
   stale/absent → mismatch → **dirty** (a safe false-dirty). A false-clean would
   require the sidecar to name a revision the canonical does not hold, which the
   ordering forbids. **Reconstruction must be revision-invariant** for the
   comparison to hold: `open_document` binds objects to existing records
   (`rebind_content`) and replays recovered state — it must not advance the
   persisted document revision the way a journaled user edit does. Pin this
   invariant in `arbc_pin_test.cpp`; if it does not hold, the sidecar records and
   compares the post-reconstruction revision instead (the comparison stays
   well-defined either way, but the invariant is the expected arbc#19 contract).

6. **No vocabulary, POD-shape, or string regressions to the notice path.**
   `OpenError`'s enumerator set, `ProjectEntryOutcome`, the notice modal, and every
   user-visible string are byte-identical. `OpenedProject` gains one `bool`
   (`mapped_in_sync`); `unbindable_content_records` keeps its type and its
   accessor, only its *source* changes. `reopen_degradation_notice`'s e2e passes
   unchanged.

7. **Levelization stays clean.** No new component, no new DAG edge. `open_document`
   / `set_content_identity_capture` / `codec_identity_capture` are `libarbc`
   symbols already nameable in L1 `project` (`ALLOWED["project"] = {base,
   platform, libarbc}`, §8). The sidecar I/O goes through the existing
   `project → platform` edge (`platform::FileSystem`, WASM-swappable per A3), never
   a raw `std::filesystem`. `commands → project` (the `mapped_in_sync` field) is an
   existing edge. `scripts/check_levels.py` is unmodified and stays green; the L1
   core gains no ImGui/GL/SDL include.

## Acceptance criteria

The universal DoD (`docs/01-architecture.md` §9) instantiated for this leaf;
`scripts/gate` green is the umbrella. Tests are **headless Catch2** in
`tests/project_open_test.cpp` unless noted.

### Levelization

- `scripts/check_levels.py` stays clean with no edit — no new component, no new
  edge, no new external (`libarbc` and `platform` edges pre-exist). State this
  explicitly in the Status block.

### L1 logic (the bulk)

- **Rewrite** `TEST_CASE("a workspace-mapped reopen restores the record graph but
  binds NO content")` (`:360`) to `… reconstructs live content through
  open_document`: after the mapped reopen, `resolve(cell.id) != nullptr` and
  `for_each_content` visits the reconstructed content. Cite the changelog line in
  the test comment (record-only → registry-aware open).
- **Invert** `open_project rebuilds a content-bearing workspace even with no
  extra-kinds callback` (`:435`) to assert the map path reconstructs without a
  canonical rebuild: `rebuilt_from_canonical == false`, `unbindable == 0`,
  `resolve() != nullptr`, `bound_content_count == 1`.
- **Invert** `open_project keeps the map fast path for a content-free workspace`
  (`:463`) to pin the durable-by-default rule for **content-bearing** workspaces
  too (map kept, content reconstructed, no rebuild).
- **Invert** `a never-saved content-bearing project keeps the map and REPORTS the
  loss` (`:489`): the map reconstructs; `unbindable_content_records == 0` for a
  reconstructable content-bearing never-saved project, and the report is
  exercised by a **separate** unreconstructable case (below).
- **`open_project reconstructs cells and cameras live on a mapped reopen`** —
  save a project with a cell **and** a re-cropped, renamed camera; reopen via the
  map path; assert both `resolve()` live, the camera's framing/name/resolution
  match the pre-close state (durable-by-default: the reconstructed state is the
  last-checkpointed state, not construction defaults), and `bound_content_count`
  covers both. This test **encodes the durable-by-default requirement** — see Open
  questions for the verification risk it guards.
- **`open_project leaves an unreconstructable record UNBOUND and reports it`** —
  a workspace whose record names a `kind_id` with no registered codec this session
  (a pre-v0.4.0 / plugin-absent stand-in): assert the record is in
  `unreconstructed`, `resolve()` answers null for it, no default stand-in is
  bound, and `unbindable_content_records == 1`. Feeds the notice unchanged.
- **`open_project falls back to canonical rebuild when open_document faults`** —
  extend the `:585-645` fault family: an `open_document` failure falls through to
  `rebuild_from_canonical`, exactly as the old map fault did.
- **Dirty-precision cases** (D28):
  - **`a cleanly-saved workspace reopens CLEAN through the map path`** — save,
    reopen, assert `mapped_in_sync == true` and `AppState::is_dirty() == false`.
  - **`a workspace edited after its last save reopens DIRTY`** — save, edit
    (bump the revision), checkpoint, reopen; assert the sidecar mismatches and
    `is_dirty() == true`.
  - **`a pre-v0.4.0 / sidecar-absent workspace reopens DIRTY`** — no sidecar →
    `mapped_in_sync == false` → dirty (safe).
  - **`a never-saved created project reopens DIRTY`** — `create_project` writes
    no sidecar (no publish) → dirty, unchanged.
  - **`a crash between canonical publish and sidecar write never reads clean`** —
    simulate the canonical `atomic_replace` succeeding and the sidecar write
    failing/absent; assert the next reopen is **dirty**, never clean. The single
    most important safety test in this leaf — it pins D-save-4's asymmetry through
    the sidecar.
- **`arbc_pin_test.cpp`**: pin that `open_document` reconstruction is
  revision-invariant (Constraint 5) and leaves an unreconstructable record unbound.

### Rendered output (golden)

- **`reconstructed_reopen_64x64.rgba8`** — a byte-exact `render_offline` golden of
  a content-bearing project reopened via the **map-reconstruct** path, asserted
  **equal to** the same project reopened via a forced **canonical rebuild** (a
  differential the golden anchors). This proves reconstruction yields pixel-live
  content, not husks. Tolerance is not used — byte-exact, per §9.

### UI behavior (ImGui Test Engine e2e)

- **No new e2e; the existing `reopen_degradation_notice_e2e_test.cpp` must pass
  unchanged** — it is the proof the notice path is untouched. The count it drives
  is now sourced from `unreconstructed` but crosses the same `ProjectGateway`
  seam with the same primitive type. No new user-driven surface is added (the
  dirty indicator already exists; only its *value* on a mapped reopen changes,
  covered by the L1 `is_dirty()` cases above).

### Threading (ASan/TSan)

- The sidecar write happens inside the **posted save closure** (writer thread,
  like `mark_saved`); the sidecar read and the `mapped_in_sync` seed happen at
  **`AppState` construction** (bootstrap, single-threaded, before the render
  thread exists). `saved_revision_` stays the one editor-owned atomic
  (`D-writer_thread-12`); the sidecar only changes what it is **seeded** with at
  construction. Scope the existing ASan/TSan lanes over the save-closure sidecar
  write vs the UI-thread `is_dirty()` read to confirm no new race — the sidecar is
  on disk, not a shared in-memory field, so the surface is the atomic that already
  existed.

### Coverage

- ≥90% diff coverage on changed lines (the `open_document` call, the retired
  guard, the identity-capture install, the sidecar write/read, the `AppState`
  seed) — met by the reconstruction, unreconstructable, dirty-precision, and
  safety cases above; tests ship with the task.

### Deferred follow-ups

- **None as a WBS task.** The WorkerPool-below-render question the `.tji` note
  flags ("SEE ALSO … Worker-backed parallel tile decode/encode") is **already
  settled** — inline decode accepted for v1 (`tasks/parking-lot.md` "Decidable
  now", A23 consequence (4)); `open_document` takes the same `LoadContext` with
  the same null dispatch, so a reconstructing reopen decodes tiles inline and this
  leaf ships correctly without reopening A4/A5/A23. One **library-side
  verification risk** (camera mutable-state round-trip through the workspace arena
  — see Open questions) is surfaced to `tasks/parking-lot.md` via the return
  summary, never encoded as a WBS "revisit" task.

## Decisions

- **D-reconstructing_reopen-1 — Open the map path through `arbc::open_document`
  and retire the content-bearing-map guard; the canonical rebuild survives only as
  the missing/unusable/pre-v0.4.0 fallback.** *Rationale:* arbc#19 makes the map
  path registry-aware, which is exactly A19's *"Future fix (cross-repo)"* — the
  guard exists only because `Document::open` ran no factory, and that premise is
  gone. Retiring it restores D-open-3's original, kind-agnostic durable-by-default
  policy "for every kind at once," so the common reopen stops paying a full
  canonical rebuild. *Alternative rejected:* keep `Document::open` + the guard and
  add a *second* reconstructing pass only for editor kinds — reintroduces the
  kind-specific guard A19 spent its whole text arguing against, and pays two loads
  where one now suffices.

- **D-reconstructing_reopen-2 — An unreconstructable record is left unbound and
  reported, never default-constructed.** *Rationale:* the arbc#19 contract is
  reconstruct-or-leave-unbound; a default stand-in would silently present a
  wrong/empty object as if it were the user's content, which is strictly worse
  than the honest count the degradation notice already surfaces. This is the
  content-level twin of D-save-4's never-a-false-clean asymmetry. *Alternative
  rejected:* bind a placeholder Content so `resolve()` is never null — trades an
  honest, reported gap for a silent, unreported corruption.

- **D-reconstructing_reopen-3 — Resolve the clean→dirty flip with a cross-session
  published-revision sidecar in `workspace/`, not an accepted false-dirty.**
  *Rationale:* restoring the fast path makes the mapped reopen the **common** case;
  an accepted false-dirty would then arm *modified* and force a pointless re-dump
  on essentially every reopen of a cleanly-saved project, hollowing out the dirty
  indicator. The sidecar restores precision — clean when actually in sync, dirty
  otherwise — and this leaf already edits the open/create/save path the parked
  D-save-4 entry named as its reason for deferring, so that reason is void. Safety
  is preserved by write-ordering (canonical first, sidecar second): a crash between
  them yields a safe false-dirty, never a false-clean. *Alternative rejected:*
  accept an explicit false-dirty on every mapped reopen (D-save-4's shipped
  posture) — safe but degrades the now-common path permanently and makes the
  indicator meaningless; the `.tji` note names this exact regression as the thing
  "not to ship unexamined."

- **D-reconstructing_reopen-4 — The sidecar keys on the document revision and
  lives in `workspace/`, read/written through `platform::FileSystem`.**
  *Rationale:* `workspace/` is machine-local, gitignored scratch (D16, D-open-5) —
  the right home for a same-machine sync artifact that must not enter the portable
  core or VCS; the revision is the same scalar the dirty model already compares
  (`is_dirty()`), so the sidecar just persists cross-session what `saved_revision_`
  holds in-session; and the `platform::FileSystem` seam keeps the WASM port a swap
  (A3), unlike a raw `std::filesystem` write. *Alternative rejected:* re-read
  `project.arbc` on the mapped reopen to recover the published revision — negates
  the whole durable-by-default performance win by doing the canonical read the
  fast path exists to avoid.

- **D-reconstructing_reopen-5 — Install identity capture on create/open/save, not
  only open.** *Rationale:* a workspace only carries construction identity if it
  was captured at `add_content` on the live `Document`; installing the capture on
  the `create_project` mint and the rebuild-from-canonical mint (not just at open)
  is what makes newly-written and freshly-rebuilt workspaces reconstructable on
  their *next* reopen. Capturing only at open would leave the first session's
  workspace identity-less. *Alternative rejected:* rely on a one-time migration of
  existing workspaces — pre-v0.4.0 workspaces are a legal, reported
  (`unreconstructed`) input; a migration is unnecessary machinery for a case the
  fallback already handles honestly.

### Doc delta

Three edits, all riding the closer's commit (same-commit rule):

1. **`docs/01-architecture.md` A19 (`:434`) — trailing amendment.** Append an
   italic `*(Amended by editor.project.reconstructing_reopen: …)*` parenthetical
   in the A15 retention style: A19's premise (`Document::open` runs no factory, so
   the map binds no `Content`) is **retired** at the v0.4.0 pin — the map path now
   opens through `arbc::open_document`, which reconstructs each `ContentRecord`
   through its registered codec identity; consequences (1)-(3) are **superseded**
   (the editor still registers no `KindStateWalker`, but content now reconstructs
   for every kind, so the guard is gone and `unbindable_content_records` re-keys to
   `ReopenedDocument::unreconstructed`); the text above is retained as the
   historical record of why the guard was first written. Note that A15's
   deferral-to-A19 is thereby moot but A15 stays unedited (double-superseded
   historical; A19 remains the row A15 points to and now carries the retirement).

2. **`docs/01-architecture.md` A13 (`:428`) — inline pointer.** Append
   `*(Amended by editor.project.reconstructing_reopen: a mapped reopen is no
   longer unconditionally dirty — a cross-session published-revision sidecar (D28)
   lets an in-sync mapped reopen read clean; the conservative asymmetry (never a
   false-clean) is preserved by write-ordering. See D28.)*`.

3. **`docs/00-design.md` D28 (new, next id after D27) — promote D-save-4's
   cross-session precision.** Area title: *"Cross-session dirty precision on the
   durable-by-default map reopen."* Prose: the session-scoped dirty baseline
   (D-save-4) is extended with a `workspace/published.rev` sidecar written on
   publish (after the canonical `atomic_replace`) and read on the mapped reopen;
   an in-sync mapped reopen reads **clean**, an out-of-sync / sidecar-absent /
   pre-v0.4.0 reopen reads **dirty**; the never-a-false-clean asymmetry is
   preserved by write-ordering (canonical then sidecar). Supersedes the
   "cross-session baseline is a deliberate non-goal" clause in
   `app_state.hpp:180` and D-save-4's rejected-alternative, whose stated reason
   (would edit the shipped open/create path) is void for this leaf. Update the
   `app_state.hpp:173-180` header comment in the same commit to cite D28.

## Open questions

**One verification risk, surfaced to the parking lot, not encoded as a WBS task.**
A19 consequence (1) records that `scene::CameraContent`'s mutable state (its
per-instance affine-frame / resolution version table of heap `std::string`s,
`scene/camera.hpp:97-111`) is **codec-persisted into `project.arbc`**, not into
the workspace arena, and that the editor registers no `KindStateWalker`. arbc#19's
identity capture snapshots construction identity **at `add_content`** (creation
time). The durable-by-default requirement is that a camera *re-cropped or renamed
after creation but not yet Saved* reconstructs to its **last-checkpointed**
state on a mapped reopen, not to its construction defaults. Whether arbc#19's
capture-plus-replay actually round-trips a codec-persisted kind's post-creation
mutable state **through the workspace arena** (vs only through the canonical
`project.arbc` a mapped reopen never reads) is a library-side property this leaf
cannot settle from the editor. The most defensible call is taken in the leaf: the
`reconstructs cells and cameras live` test **encodes the requirement** (an
edited-then-checkpointed camera must reopen with its edit), and Constraint 4's
reconstruct-or-leave-unbound invariant means the safe failure mode is an unbound,
**reported** record — never a silent revert to stale state. If implementation
finds the camera's checkpointed mutable state does not survive a mapped reopen
(because it lives only in the codec/canonical path), that is a cross-repo
**library** gap — surfaced here for the parking lot's "Waiting on evidence" half
with the trigger "a mapped reconstruct reopen observed reverting an unsaved camera
edit" — implementable only against a future libarbc release, so **not** a WBS
leaf. Everything else is decided.

## Status

**Done** — 2026-07-28.

- Switched `project::open_project`'s map branch from `arbc::Document::open` to `arbc::open_document` (`src/project/project_open.cpp`), reconstructing each recovered `ContentRecord` through its registered codec identity.
- Retired the content-bearing-map guard (`count_unbindable_content` walk + `skip_map_for_editor_kinds` short-circuit + `unbindable == 0 || !canonical_exists` keep-test) so the map fast path is durable-by-default for every kind at once (D-open-3 restored).
- Installed `Document::set_content_identity_capture(arbc::codec_identity_capture(codecs, bridge))` on `create_project` mint and `rebuild_from_canonical` mint (`src/project/project_open.cpp`) so newly-written and freshly-rebuilt workspaces carry construction identity.
- Re-keyed `OpenedProject::unbindable_content_records` from count-of-null-binds to `ReopenedDocument::unreconstructed.size()` (`src/project/ace/project/project.hpp`); added `bool mapped_in_sync` field ferrying the sidecar verdict.
- Wrote D28 (cross-session dirty precision): `project::save_project` now writes `workspace/published.rev` after canonical `atomic_replace` (`src/project/save.cpp`); `open_project` reads it and sets `mapped_in_sync = true` iff present and matching reconstructed revision; `AppState` ctor seeds `saved_revision_` clean when `rebuilt_from_canonical_ || mapped_in_sync_` (`src/commands/app_state.cpp`, `src/commands/ace/commands/app_state.hpp`).
- Added type-erased `project::install_content_identity_capture` helper (PRIVATE to `ace_project`); `nlohmann_json` linked PRIVATE to `ace_tests` so the arbc pin can call `open_document` directly (`CMakeLists.txt`).
- Levelization stays clean — `scripts/check_levels.py` unmodified, no new component, no new edge; `open_document`/`set_content_identity_capture`/`codec_identity_capture` are pre-existing `libarbc` symbols nameable in L1 `project`.
- Rewrote/inverted 4 pinned `project_open_test` cases; added reconstruction, unreconstructable-unbound, `open_document`-fault fallback, 5 dirty-precision (D28), cameras-live, and differential golden cases (`tests/project_open_test.cpp`, `tests/goldens/reconstructed_reopen_64x64.rgba8`).
- Added behavioral arbc pin in `tests/arbc_pin_test.cpp` (revision-invariant reconstruction + unreconstructable-record-unbound).
- Updated 4 `camera_model_test` + 1 `project_save_test` cases (retired guard legitimately inverts them map→reconstruct, justified in-comment) (`tests/camera_model_test.cpp`, `tests/project_save_test.cpp`).
- Amended `docs/01-architecture.md` A19 (trailing retirement parenthetical) and A13 (inline D28 pointer); added `docs/00-design.md` D28 (cross-session dirty precision).
