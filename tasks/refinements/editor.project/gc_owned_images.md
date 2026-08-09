# editor.project.gc_owned_images — land the GC-roots-owned-blob case the paste leaf could not satisfy

## TaskJuggler entry

`tasks/00-editor.tji:219-224`, under `project editor { task project { … } }`:

```
task gc_owned_images "Land the GC-roots-owned-blob case the paste leaf could not satisfy" {
  effort 0.5d
  allocate team
  depends editor.canvas.arbc_v070, editor.import.paste
  note "ruoso/arbitrarycomposer#30 shipped in v0.5.0: `gc_project_directory` now marks `params.source` URIs alongside `params.blobs` hashes and sweeps `assets/images/` alongside `assets/tiles/`, both halves together. `editor.import.paste` shipped with its GC-roots-owned-blob acceptance criterion DELIBERATELY NOT LANDED because neither half was satisfiable against v0.4.1 — that is the debt this leaf pays. Land the deferred Catch2 case: a pasted owned image is ROOTED while referenced (a Clean-Up leaves its blob alone), and a paste->undo->save->Clean-Up cycle RECLAIMS the orphan. … Design: docs/00-design.md D13; docs/01-architecture.md A23."
}
```

**Closer ritual** (`tasks/refinements/README.md:47-78`): append `complete 100`
after `allocate team`; append
`Refinement: tasks/refinements/editor.project/gc_owned_images.md` to the task's
`note` (after the design-doc citations). No doc delta rides this leaf — D13, A23
and A30 already describe the owned-blob GC lifecycle this leaf only pins with
tests. **No follow-up WBS leaf is registered** — this is a terminal, test-only
leaf. **Milestone:** `m9_editor` (`tasks/99-milestones.tji:11-13`) depends on
`editor.project` wholesale, so this leaf is transitively covered — no milestone
`depends` edit is needed. After editing, run
`tj3 project.tjp 2>&1 | grep -iE "error|warning"` and confirm silence.

> **Path note.** Written to `tasks/refinements/editor.project/`, matching every
> other `editor.project.*` refinement (`reconstructing_reopen.md`,
> `create_rollback.md`, `save_as_rollback.md`) and their `Refinement:`
> back-links — the fully-qualified-area convention, not the older flat
> `tasks/refinements/editor/`.

## Effort estimate

**0.5d** — a **test-only** leaf. The library capability landed in the pin bump
(arbc#30, absorbed by `editor.canvas.arbc_v070`), the editor call site is the
same unchanged two-arg `arbc::gc_project_directory` (`src/project/gc.cpp:80-81`),
and the owned-image write path already mints the URI the mark walk harvests
(`src/project/import_asset.cpp:50-78`). The entire editor-side loop is closed;
what is missing is coverage. The cost is one new fixture helper that authors an
owned image through the real mint verb, four Catch2 cases in the existing
`tests/project_gc_test.cpp`, and the re-read of two consequences the pin leaf
flagged (referenced-set no longer tiles-only; malformed `params.source` now
fails the mark walk). No new production behaviour is expected; the one defensible
production touch is refreshing `gc.cpp`'s now-incomplete "referenced tiles"
comment (Constraint 7).

## Inherited dependencies

### Settled (relied on directly)

- **`editor.canvas.arbc_v070`** (`complete 100`, `tasks/00-editor.tji:417-420`) —
  bumped the libarbc pin to v0.7.0, absorbing v0.5.0's **arbc#30**:
  `gc_project_directory` now marks `params.source` URIs alongside `params.blobs`
  hashes and sweeps `assets/images/` alongside `assets/tiles/`, **both halves
  together**, with its **two-arg signature unchanged**
  (`asset_gc.hpp:179-180`), so `gc.cpp:80-81` compiled untouched and gained the
  behaviour for free. That pin leaf's scope was inert-and-verified only; it
  **explicitly deferred the substantive consumption to this leaf**
  (`tasks/refinements/canvas/arbc_v070.md:141`, "Consuming the GC image
  mark/sweep (#30) — `editor.project.gc_owned_images`"), and flagged the two
  re-reads this leaf owns (`arbc_v070.md`, D-arbc_v070-5): the referenced set is
  no longer the same set, and a malformed `params.source` now yields a `GcError`
  where it previously swept.
- **`editor.import.paste`** (`complete 100`, `tasks/00-editor.tji:752-755`) —
  minted the owned-image path: `project::mint_owned_asset(fs, layout, bytes,
  ext)` content-addresses clipboard bytes and writes them **write-if-absent**
  into the live project's `assets/images/<xx>/<hash>[.ext]`
  (`src/project/import_asset.cpp:23,50-78`), and the cell carries that URI as
  `params.source`. Its **GC-roots-owned-blob acceptance criterion was
  deliberately NOT landed** because neither library half was satisfiable at
  v0.4.1 (`tasks/refinements/editor/paste.md:396-400`, the italic escape clause;
  Status at `paste.md:558`) — **this leaf pays exactly that debt.**
- **`editor.project.gc`** (`complete 100`, `tasks/00-editor.tji:144-146`) —
  shipped `project::gc_project(const ProjectLayout&, bool dry_run)`
  (`src/project/gc.cpp:63`), the `GcOutcome`/`GcError` types and mapping
  (`src/project/ace/project/gc.hpp:23-40`), the no-canonical guard
  (`gc.cpp:64-72`), the `commands::gc_project` composition, the Clean-Up
  preview→confirm→commit UX, and the whole test harness this leaf extends
  (`tests/project_gc_test.cpp`, `tests/gc_ui_e2e_test.cpp`).

### Pending

- None. All three predecessors are `complete 100`.

## What this task is

Land the two Catch2 cases `editor.import.paste` predicted but could not ship,
plus the two edge cases the task note and library policy call out, against the
now-capable library. Concretely: prove that a **referenced** owned image is
**rooted** — a Clean-Up over a project holding a pasted owned image leaves its
`assets/images/` blob on disk (the data-loss-critical direction) — and that an
**orphaned** owned image is **reclaimed** — a `paste→undo→save→Clean-Up` cycle
removes the blob. Add the two boundary cases: owned images and tiles are rooted
**together in one pass** (the union that arbc#30 delivered), and an image whose
`params.source` points **outside the project's owned subtree** is never
enumerated and never reclaimable (correct, per the note). Re-read — not assume —
the two v0.5.0 consequences (the referenced set is no longer tiles-only; a
malformed `params.source` now fails the mark walk) and confirm the editor's
report/error mapping still reads sensibly. No production behaviour changes.

## Why it needs to be done

The rooted half is the one whose absence would mean **data loss**: a Clean-Up
that swept a live owned image's blob would leave a cell pointing at nothing after
reopen. It was unsatisfiable in the safe direction before v0.5.0 precisely
because the old reaper ignored `assets/images/` entirely — nothing was ever
wrongly deleted *because nothing under `assets/images/` was ever looked at*
(memory `arbc-gc-blind-to-owned-images`; the "gap 1 masking gap 2" note). Now
that both halves ship together, the editor is exposed to the failure mode the
deferred test guards against, and that guard must exist as an executing test —
not as prose in a Status block. Downstream, `editor.import.consolidate` and
`editor.import.owned_asset_republish` both move owned image blobs and rely on the
GC lifecycle being pinned truthfully here.

## Inputs / context

### Design docs (normative)

- **`docs/00-design.md` D13** (`:480`) — Assets, GC & portability: owned bytes in
  `assets/` (painted tiles **and** pasted/consolidated files) are
  content-addressed, dedup'd, and GC'd via explicit "Clean up" + on close; roots
  = all open docs; **borrowed files and `workspace/` are never GC'd**. This leaf
  pins the owned-file half of that rule for pasted images.
- **`docs/01-architecture.md` A23** (`:440`) — the tile lifecycle clause this
  leaf reuses: *the sink never deletes; GC is the reaper* — undo orphans a blob
  the sink leaves in place, and only a GC pass reclaims it. Owned images inherit
  the identical lifecycle.
- **`docs/01-architecture.md` A30** (`:447`) — the pasted-bitmap-becomes-owned
  decision: the owned blob is **GC-rooted by the live cell's reference**
  (`gc_project_directory` marks `params.source`, so GC reclaims it *only once the
  cell is gone* — "the A23 tile lifecycle, undo orphans a blob the sink never
  deletes"). This leaf is the test that instantiates A30's GC promise.

### Source seams

- `src/project/gc.cpp:63` — `platform::Result<GcOutcome> gc_project(const
  ProjectLayout& layout, bool dry_run)`; `:64-72` no-canonical guard **and** the
  referenced-set comment (still worded "union their referenced **tiles**",
  now-incomplete — Constraint 7); `:80-81` the unchanged two-arg
  `arbc::gc_project_directory(layout.root, dry_run)` call; `:82-85` the
  `GcReport → GcOutcome` / `GcError → project::GcError` mapping.
- `src/project/ace/project/gc.hpp:23-30` `GcOutcome{scanned, referenced,
  deleted, bytes_reclaimed}`; `:36-40` `GcError` enum; `:58` `gc_project` decl.
- `src/app/project_gateway.cpp:197-211` — the `GcOutcome → dock::GcSummary`
  report/error surface (where a malformed-source `GcError` reaches the UI as
  `ran=false` / nothing-reclaimed); `:470-489` `paste_image()` →
  `mint_owned_asset(...)` → `insert_image_bytes(owned.uri, owned.bytes, …)`.
- `src/project/import_asset.cpp:23` `k_owned_images_base = "assets/images/"`;
  `:50-78` `mint_owned_asset` (`:61` `arbc::hash_tile`, `:62`
  `arbc::tile_blob_uri`, `:73-76` the `FilesystemAssetSink`/`SaveContext`
  write-if-absent); decl `src/project/ace/project/import_asset.hpp:41-50,62-63`.
- `src/commands/image_import.cpp:31` — `image_config(...)` forwards the owned URI
  to the codec; the library sets `params["source"]` at
  `_deps/arbc-src/src/runtime/codec_image.cpp:93` and reads it back **leniently**
  (malformed → treated as absent) at `codec_image.cpp:149,177`.
- **libarbc `asset_gc.hpp`** (FetchContent, under
  `build/dev/_deps/arbc-src/src/runtime/arbc/runtime/`): `gc_project_directory`
  two-arg at `:179-180`; `GcRoots{ tiles, assets }` at `:140-143` (the `assets`
  set is authored `params.source` URIs, `file://` stripped);
  `collect_referenced_assets` at `:151`; `GcReport` at `:97-104`; `GcError{ Kind,
  mark, reap }` at `:110-119`. **Note:** `gc.cpp`'s inline `asset_gc.hpp:69-72` /
  `:84-90` citations are stale against this checkout (the structs moved to
  `:97-104` / `:110-119`) — do not "fix" the code from the stale comment; the
  behaviour is what the two-arg entry does.

### Existing test surface this leaf edits or pins

- `tests/project_gc_test.cpp` — the file this leaf extends. Existing cases at
  `:129` (reclaim, dry-run == commit), `:157` (no-canonical guard), `:173`
  (fail-safe on unparseable canonical), `:188` (`assets/`-only), `:211`
  (`commands::gc_project` invariants), `:242` (canonical-preserving golden).
  Harness in the anon namespace: `ScratchDir` `:52-63`, `tile_hash` `:68`,
  `write_canonical(root, blobs)` `:73-86` **(authors `params.blobs` only —
  no `params.source`)**, `write_blob(root, hash, content)` `:90-96` **(writes
  `assets/tiles/<xx>/<hash>` only)**, `blob_exists` `:98-100`,
  `build_saveable_probe` `:109-119`, `builtin_registry` `:121-125`. **Gap this
  leaf fills:** no helper authors `params.source` or writes under
  `assets/images/`, and no GC test runs against a pasted owned image.
- `tests/paste_image_test.cpp` — the owned-image authoring harness to reuse:
  `session_with_composition` `:87-95`, `fixture_bytes` `:98-102`, the fixture
  `photo_12x8.ppm`, and the canonical round-trip proving `params.source`
  persistence at `:249-302`. This is where the real mint-through-`mint_owned_asset`
  path is already exercised — but it never runs `gc_project`.
- `tests/gc_ui_e2e_test.cpp` — the Clean-Up preview→confirm→commit e2e; owned
  images route through the same L1 `gc_project` + `dock::GcSummary`, so it needs
  no new case (Acceptance / UI).

## Constraints / requirements

1. **Test-only.** No new production behaviour. The library owns the mark walk
   (`collect_referenced_assets` over `params.source`) and the sweep
   (`assets/images/`); the editor already writes the URI it harvests. Do **not**
   add an editor-side owned-image root collector — it would be a second,
   drifting source of truth.
2. **Author owned images through the real mint verb.** The new fixture helper
   mints via `project::mint_owned_asset(fs, layout, bytes, ext)` (reusing
   `paste_image_test`'s fixture bytes), producing the exact
   `assets/images/<xx>/<hash>[.ext]` blob and `params.source` URI paste ships —
   not a hand-authored synthetic blob (D-gc_owned_images-2).
3. **Both directions.** Rooted-while-referenced (Clean-Up leaves the blob) **and**
   `paste→undo→save→Clean-Up` reclaims the orphan; dry-run and commit compute one
   plan, mirroring the existing tiles case at `project_gc_test.cpp:129`.
4. **Respect the over-approximate matching policy.** Upstream matches an authored
   URI against an on-disk key over-approximately on purpose (a shared basename
   roots a blob), because retaining an orphan is a leak while missing a live
   reference is data loss. Assert the **safe direction and presence/absence**,
   never exact reclaim counts on colliding basenames (D-gc_owned_images-3;
   memory `arbc-gc-blind-to-owned-images`).
5. **The outside-the-subtree case is its own case.** A referenced image whose
   URI points outside the project's owned subtree (an external absolute URI) is
   never enumerated and can never be reclaimed — correct, and worth pinning:
   `assets/` byte-unchanged, the cell still resolves on reopen.
6. **Levelization unchanged.** No new component, no new DAG edge, no
   `scripts/check_levels.py` edit; the L1 `project` core gains no ImGui/GL/SDL
   include. `project` already legally names `arbc::` (§8).
7. **Re-read the two v0.5.0 consequences** the pin leaf flagged: (a) the
   referenced set at `gc.cpp:64-72` is no longer tiles-only — confirm the
   no-canonical guard still fires and a never-saved project still no-ops; and (b)
   a malformed `params.source` now yields `GcError(MarkFailed)`/`ran=false` where
   it previously swept — confirm `project_gateway.cpp:197-211` maps it to a
   sensible "nothing reclaimed", nothing deleted. Refreshing the now-incomplete
   "referenced tiles" comment at `gc.cpp:74-76` to name the owned-image half is
   the one in-scope production touch (a comment, not behaviour).

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate`
green (check_levels · clang-format · build · ctest) is the umbrella, and
`diff-cover --fail-under=90` must pass on the changed lines (here almost entirely
test code, exercised directly by the new cases).

### Levelization (the primary structural assertion)

`scripts/check_levels.py` stays byte-unmodified. No new component, no new DAG
edge, no new external dependency, no link-line change. The leaf touches only
`tests/project_gc_test.cpp` (plus the `gc.cpp:74-76` comment).

### L1 logic (the bulk) — Catch2 in `tests/project_gc_test.cpp`

Sentence-style `TEST_CASE`s, matching the file's convention:

- **`gc_project roots a referenced owned image, leaving its assets/images/ blob
  untouched`** — a canonical carrying a cell with a minted `params.source`, the
  matching blob under `assets/images/<xx>/<hash>`; a `dry_run` and a commit both
  list **zero** owned-image reclaims and the blob still exists after the commit.
  *This is the data-loss-critical direction.*
- **`gc_project reclaims an orphaned owned image after paste→undo→save→Clean-Up`**
  — the owned blob is present but the saved canonical no longer references it
  (the undo dropped the cell before the save); dry-run and commit compute one
  plan and the blob is gone after commit. Assert presence/absence, not an exact
  byte count on a colliding basename (Constraint 4).
- **`gc_project roots owned images and tiles together in one pass`** — a
  canonical referencing one live tile (`params.blobs`) and one live owned image
  (`params.source`), plus one orphan of each kind; after the sweep **both live
  blobs survive and both orphans are gone**. Guards the union arbc#30 delivered
  against a regression that marks one store while sweeping the other's live blob.
- **`gc_project never enumerates an image referenced outside the owned subtree`**
  — a cell whose `params.source` is an external absolute URI; `assets/` is
  byte-unchanged after a real sweep and the reference still resolves. (Correct
  and worth its own case, per the note.)
- **Guarded (Constraint 7b):** extend the existing fail-safe case, or add
  **`gc_project fails safe on a malformed params.source, deleting nothing`** — a
  canonical whose source URI is unparseable now surfaces `project::GcError`
  (`MarkFailed`) with no blob touched. Assert the safe direction only, not the
  message.

The new fixture helper (`write_owned_image` / `mint_owned_image`, D-gc_owned_images-2)
authors the blob and `params.source` through `mint_owned_asset` reusing
`paste_image_test`'s fixture bytes.

### Rendered output (golden) — justified omission

No new golden. Owned-image reclamation touches only `assets/`; no pixel moves.
The existing canonical-preserving golden
(`project_gc_test.cpp:242`, `tests/goldens/render_probe_64x64.rgba8`) already
proves a real sweep leaves the renderable document byte-exact.

### UI behavior (ImGui Test Engine e2e) — justified reuse

No new e2e. Owned images route through the same L1 `gc_project` and the same
`dock::GcSummary{reclaimed_files, reclaimed_bytes, ran}` POD the existing
`tests/gc_ui_e2e_test.cpp` preview→confirm→commit flow already drives; a
Clean-Up reclaiming an owned image differs only in the counts fed to that POD,
which the L1 cases above assert directly.

### Threading (ASan/TSan) — no new scope

`gc_project` runs synchronously on the calling thread with no `WorkerPool` and no
cross-thread handoff; the on-close silent-GC path is unchanged. No new sanitizer
scope; the extended `project_gc_test` runs under the existing CI sanitizer
configuration.

### Coverage

`diff-cover --fail-under=90` on the changed lines — the new cases and helper are
the changed lines and exercise themselves; the `gc.cpp` comment refresh is
non-executable.

### Deferred follow-ups

**None as a WBS leaf** — this is a terminal, test-only leaf. The Save-As /
Consolidate republish of owned blobs into a fresh root is already the separate
`editor.import.owned_asset_republish` leaf (GC over a fresh root is not this
leaf's concern). The malformed-`params.source` **user-message wording** is a
product/UX judgment, not agent-implementable work — routed to the parking lot via
the return summary, never encoded as a WBS "revisit" task (see Open questions).

## Decisions

- **D-gc_owned_images-1 — Test-only; no production code change (bar a stale
  comment).** *Rationale:* arbc#30 delivered the mark-over-`params.source` and
  sweep-of-`assets/images/` in the library; `gc.cpp:80-81` already calls the
  unchanged two-arg entry, and `mint_owned_asset` already writes the URI the mark
  walk harvests — the whole editor-side loop is closed, so the only missing thing
  is coverage. *Alternative rejected:* add an editor-side owned-image root
  collector to feed the library — redundant and a second source of truth; the
  library's `collect_referenced_assets` (`asset_gc.hpp:151`) owns the mark walk.
  **No doc delta.**
- **D-gc_owned_images-2 — Author fixtures through the real `mint_owned_asset`,
  not a synthetic hand-authored `assets/images/` blob.** *Rationale:* the whole
  gap was that owned images use a different key scheme (`params.source` +
  `assets/images/<xx>/<hash>[.ext]`) than tiles (`params.blobs` +
  `assets/tiles/`); a test that hand-authors an arbitrary blob could pass against
  a scheme paste never emits and miss a real drift. Minting through the shipped
  verb (reusing `paste_image_test`'s fixture bytes) pins the exact bytes and URI
  paste writes. *Alternative rejected:* extend `write_canonical` to inject a
  `params.source` array by hand — cheaper, but it re-encodes the scheme
  assumption the test exists to verify. **No doc delta.**
- **D-gc_owned_images-3 — Assert the safe direction and presence/absence, never
  exact reclaim counts on colliding basenames.** *Rationale:* upstream matches an
  authored URI against an on-disk key over-approximately on purpose (a shared
  basename roots a blob) because retaining an orphan is a leak while missing a
  live reference is data loss; an exact-count assertion on a basename collision
  tests against stated library policy, not a bug (memory
  `arbc-gc-blind-to-owned-images`; the task note). *Alternative rejected:* force
  distinct basenames to make counts exact — that hides the policy the test should
  respect rather than exercising it. **No doc delta.**
- **D-gc_owned_images-4 — The malformed-`params.source` consequence is verified
  safe-direction only; its user-message quality is parking-lot, not this leaf.**
  *Rationale:* a malformed source now yields `GcError(MarkFailed)`/`ran=false`
  where v0.4.1 swept — nothing is wrongly deleted, so the behaviour is
  correct-if-terse and the test can pin exactly that. Whether the confirm modal
  should distinguish "your project has a bad image reference" from a generic
  "couldn't clean up" is a product/UX call; encoding it as a WBS "revisit" task
  would be an un-closable "decide X later" leaf. *Alternative rejected:* file a
  WBS leaf to improve the message — surfaced to the parking lot instead. **No doc
  delta.**

## Open questions

`(none — all decided)`. One item is routed to the parking lot (not a WBS task,
per D-gc_owned_images-4): **the confirm-modal wording when a Clean-Up aborts on a
malformed `params.source`** — should the user see "couldn't clean up: a project
image reference is malformed" rather than a generic terse failure? A product/UX
judgment surfaced in the return summary; `editor.canvas.arbc_v070` already
flagged the same message question there.

## Status

**Done** — 2026-08-09.

- `src/project/gc.cpp` — refreshed the now-incomplete "referenced tiles" comment at `:74-76` to name the owned-image half (Constraint 7; comment only, no behaviour change).
- `tests/project_gc_test.cpp` — added `mint_owned_image` fixture helper (via real `project::mint_owned_asset`), `owned_blob_exists`, `fixture_bytes`, and `asset_tree`; extended `write_canonical` to emit `params.source` layers; added five new `TEST_CASE`s covering all acceptance criteria.
- Five new Catch2 cases: `gc_project roots a referenced owned image` (data-loss-critical, blob untouched); `gc_project reclaims an orphaned owned image after paste→undo→save→Clean-Up`; `gc_project roots owned images and tiles together in one pass`; `gc_project never enumerates an image referenced outside the owned subtree`; `gc_project fails safe on a malformed params.source, deleting nothing`.
- All 11 `gc_project` cases pass (6 existing + 5 new). Owned blobs minted through `mint_owned_asset`, not hand-authored (D-gc_owned_images-2); presence/absence asserted, not exact counts on colliding basenames (D-gc_owned_images-3).
- Re-reads confirmed: no-canonical guard still fires on unsaved project (Constraint 7a); malformed `params.source` maps through `project_gateway.cpp:197-211` to `ran=false`/nothing-reclaimed (Constraint 7b) — both sensible, no production change.
- No WBS follow-up registered — terminal test-only leaf. Malformed-source UX wording routed to parking lot (D-gc_owned_images-4).
