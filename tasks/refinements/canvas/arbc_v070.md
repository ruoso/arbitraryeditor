# editor.canvas.arbc_v070 — Bump libarbc pin v0.4.1 → v0.7.0 (absorbing v0.5.0 and v0.6.0 unconsumed)

## TaskJuggler entry

- **Task:** `editor.canvas.arbc_v070` (`tasks/00-editor.tji:417-422`).
- **Effort:** `2.5d` · `allocate team`.
- **Depends:** `!arbc_v041` (the immediately-preceding pin in the
  v0.2.0/v0.3.0/v0.4.0/v0.4.1 series).
- **Note (`.tji:421`, abridged):** bump `ARBC_GIT_TAG` `v0.4.1`→`v0.7.0`
  (`CMakeLists.txt:25`), **skipping v0.5.0 and v0.6.0 as pins** and absorbing all
  three releases at once. v0.6.0 is the only breaking one (a required `BlendMode`
  parameter on `Backend::composite*` fails four sites to compile); v0.5.0 is
  additive-except-one-enumerator; v0.7.0 is one purely-additive issue the editor
  does not yet consume. This leaf **replaces** the retired `arbc_router_split_pin`
  placeholder. Every consumer of the new surface is a downstream leaf that
  already exists (`depends editor.canvas.arbc_v070` / `!arbc_v070`).
- **Back-link:** this refinement lands at
  `tasks/refinements/canvas/arbc_v070.md`. **The closer** appends
  `Refinement: tasks/refinements/canvas/arbc_v070.md` to the `.tji` note and adds
  `complete 100` after `allocate team`. **Do not** hand-edit the `.tji` here.
- **Source of debt:** `tasks/parking-lot.md` triage 2026-08-08 (the v0.5.0/v0.6.0
  issue set #28–#38) and triage 2026-08-09 (arbc#39 → v0.7.0); it also retires the
  three-times-re-deferred `tasks/refinements/canvas/arbc_router_split_pin.md`
  placeholder, whose promised release IS v0.5.0.
- **Downstream dependents (all pre-existing leaves, none registered here):**
  `editor.canvas.arbc_router_attach_split` (#28), `editor.canvas.nested_real_pool_restore`
  (#29), `editor.project.gc_owned_images` (#30), `editor.cells.resample_apply` (#31),
  `editor.import.nested_live_resolve` (#32), `editor.project.undo_reconcile` →
  `editor.import.consolidate` (#34), `editor.cameras.export_unresolved_report`
  (#35), `editor.cells.insert_offer` (#37 + arbc#39), plus `editor.cells.insert_schema`
  is already `complete 100`. **One new follow-up is registered:**
  `editor.cells.objectid_field_picker` (see Acceptance criteria).
- **Milestone:** M9E (`m9_editor` depends on `editor.canvas`; this leaf is
  transitively covered — no new milestone edge).

## Effort estimate

**Two and a half days** — the largest pin in the series, because it absorbs
**three releases in one bump** and, unlike every prior pin, carries **real `src/`
edits** (four compile-fix sites plus one new enumerator arm) rather than being
inert. The mechanical bump is again one string at `CMakeLists.txt:25` — the only
place in built code that names the version (no `arbc_version` symbol exists; a
`<NAME>_VERSION` cache var is invisible after `FetchContent_MakeAvailable`; no test
asserts a library version string). The budget goes into:

1. **An empirical trial configure+build+ctest against `v0.7.0`** (tagged
   2026-08-09; the release exists, so this is a *real* bump, not a gated one like
   `arbc_router_split_pin`) — characterizing which sites fail to compile and
   whether any golden moves, before committing.
2. **Four compile-fix edits** — the three `Backend::composite` call sites plus the
   one pin-test `static_assert` whose member-function-pointer type moved.
3. **One new enumerator arm** — `InsertFieldType::ObjectId` and its `map_field_type`
   case, so the v0.5.0 `KindInsertField::Type::ObjectId` hint survives instead of
   silently collapsing to `Text`.
4. **Retargeting `tests/arbc_pin_test.cpp`** with the full v0.5.0 + v0.6.0 + v0.7.0
   witness surface the downstream leaves lean on, and updating the `composite_windowed`
   signature witness.
5. **Two verification tests that need no `src/` change** — the per-layer blend
   round-trip (free via libarbc's snapshot stash) and the nested labelled-field
   rendering.

Where the budget does **not** go: any `Backend` subclass (the editor uses
`arbc::CpuBackend`), any blend-control UI (D10 forbids it for v1), any consumption
of the damage-sink / resample / external-composition / undo-reconcile surface (each
its own downstream leaf), any golden re-baseline (every one is expected
byte-identical), and any doc delta (no new seam, dependency, or deviation).

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.canvas.arbc_v041`** (`complete 100`) — the immediate predecessor pin.
  It carried no refinement by design (purely additive, nothing to absorb); it
  established `v0.4.1` as the base tag this leaf moves off.
- **`editor.canvas.arbc_v040`** (Done 2026-07-28,
  `tasks/refinements/canvas/arbc_v040.md`) — the first **non-additive** pin, and
  the direct model for this one. It set the disciplines this leaf reuses verbatim:
  `ARBC_GIT_TAG` is `CACHE` **without** `FORCE` (`CMakeLists.txt:25`); the staleness
  hazard is closed **by the compiler** via unevaluated `static_assert` witnesses in
  `tests/arbc_pin_test.cpp`; a golden that moves for a reason the changelog does not
  name is a **regression, not a re-baseline** (D-arbc_v040-2); and it added the
  `composite_windowed`/`clear_rect` `Backend`-virtual witnesses at
  `tests/arbc_pin_test.cpp:346-355` that this leaf's `BlendMode` growth updates.
- **`editor.canvas.arbc_v030` / `arbc_v020`** (`complete 100`) — earlier additive
  pins; `arbc_v030` set the golden-**invariance** stance this leaf lands back on
  (v0.6.0's `BlendMode::Normal` is byte-exact source-over, so no golden moves).
- **`editor.cells.insert_schema`** (`complete 100`) — replaced the editor's own
  grammar adapters with the library's `Registry::insert_schema(id)` and the
  no-allowlist `insert_schemas` enumeration (`src/scene/cell.cpp:197-214`,
  one `KindInsertSchema` per `registry.ids()` entry). That enumeration is what
  makes v0.5.0's new `org.arbc.nested` schema appear automatically — no code needed
  here beyond the `ObjectId` enumerator.

**Pending (owned here):** nothing external. Unlike `arbc_router_split_pin` (which
was gated on a release that did not yet exist), **v0.7.0 is tagged** — this leaf is
READY. Everything it owns is in-repo: the tag string, four compile fixes, one
enumerator, the pin-test retarget, and two verification tests.

## What this task is

libarbc shipped three releases the editor never pinned: v0.5.0 (2026-08-08,
issues #28–#34), v0.6.0 (same week, #35–#38), and v0.7.0 (2026-08-09, arbc#39).
Because nothing here ever consumed v0.5.0 or v0.6.0 as a separate pin, bumping
through them one at a time would mean absorbing two churn passes for no gain. This
leaf bumps `CMakeLists.txt:25` straight to `v0.7.0` and absorbs all three at once.

The work, in dependency order:

1. **Bump the pin.** `CMakeLists.txt:25` `"v0.4.1"` → `"v0.7.0"`, keeping `CACHE`
   without `FORCE`.
2. **Fix the four v0.6.0 compile breaks.** `Backend::composite`/`composite_clipped`/
   `composite_windowed` each gained a **required** `BlendMode blend` immediately
   after `opacity` (required, not defaulted, on purpose: a default argument on a
   virtual binds statically and would let backend and caller silently disagree).
   The editor implements no `Backend` but calls the **base `composite`** at three
   sites, and the pin test asserts `composite_windowed`'s member-function-pointer
   type. All four pass `1.0` opacity and want plain source-over, so each takes
   `arbc::BlendMode::Normal` — which the release states is premultiplied source-over
   **exactly**, its own kernel arm — so **no golden may move**.
3. **Add the v0.5.0 `ObjectId` enumerator arm.** `KindInsertField::Type` gained an
   `ObjectId` value; `scene::map_field_type` (`src/scene/cell.cpp:32-42`) switches
   with no `default` and the build sets no `-Wswitch`, so an unmapped value silently
   falls through to `InsertFieldType::Text`, discarding the one bit the new type
   carries. Add the case and a matching editor-side `InsertFieldType::ObjectId` so
   the hint survives.
4. **Retarget the pin guard.** Extend `tests/arbc_pin_test.cpp` with unevaluated
   `static_assert` witnesses naming the v0.5.0 + v0.6.0 + v0.7.0 surface the
   downstream leaves inherit, and update the `composite_windowed` signature witness.
5. **Verify the two free-behaviour absorptions.** The per-layer `BlendMode`
   round-trip (v0.6.0) and the nested labelled `child` field (v0.5.0 #33) need no
   `src/` code, but each gets a test that pins the property so a later change cannot
   silently break it.

**Out of scope, by charter** (each owned by a named downstream leaf that already
`depends` on this pin):

- **Constructing the viewport with both writer-thread installs deferred** (the
  damage-sink split, #28) — `editor.canvas.arbc_router_attach_split`.
- **Flipping nested e2es back to the real interactive pool** (#29) —
  `editor.canvas.nested_real_pool_restore`.
- **Consuming the GC image mark/sweep** (#30) — `editor.project.gc_owned_images`.
- **The resample verb** (#31) — `editor.cells.resample_apply`.
- **Live nested resolution via `install_external_composition`** (#32) —
  `editor.import.nested_live_resolve`.
- **The config-rewrite undo reconcile** (#34) — `editor.project.undo_reconcile` →
  `editor.import.consolidate`.
- **Surfacing the unresolved-region export report** (#35) —
  `editor.cameras.export_unresolved_report`.
- **Driving the insert dialog off `insert_offer` and the modality filter** (#37 +
  arbc#39) — `editor.cells.insert_offer`. Until it lands, the editor keeps
  enumerating `registry.ids()`, so `org.arbc.fade`/`org.arbc.crossfade` (now
  declaring themselves `Internal`) stay offered — a known, tolerated latency, not
  this leaf's bug.
- **Rendering an `ObjectId` field as a composition picker** — the *new* follow-up
  `editor.cells.objectid_field_picker` this leaf registers.

## Why it needs to be done

- **It unblocks a whole fan-out.** Nine downstream leaves `depends` on this pin;
  none can start until the library surface they name is on the tree. Isolating the
  bump in its own leaf, with `src/` edits limited to the mechanically-forced ones,
  keeps any library-side re-baseline attributable apart from an editor bug — the
  attribution hygiene every prior pin enforced.
- **v0.6.0 is a hard compile break.** Four sites will not compile against the new
  tag; the tree is red until they take `BlendMode::Normal`. That break cannot be
  deferred to a consumer leaf — it gates the build itself.
- **Two silent-correctness traps close here.** (a) The v0.5.0 `ObjectId` enumerator
  *compiles* against the no-`default` switch and silently degrades to `Text`; adding
  the arm preserves the hint. (b) A per-layer blend authored elsewhere must
  round-trip through the editor unmodified even though the editor exposes no blend
  control; a test pins that the save path cannot drop it.
- **It retires a re-deferred placeholder.** `arbc_router_split_pin` existed only to
  name "the release that splits the DamageRouter." v0.5.0 **is** that release, and
  it carries six other things the placeholder never scoped — the honest shape is a
  normal pin-bump leaf in this series, not a 0.5d placeholder pretending the rest is
  free.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **A1** (`docs/01-architecture.md:18-22`) — "The editor is a **native C++20
  application that links `arbc::arbc` directly** … a canvas holds real
  `HostViewport` / `Document` / `Backend` objects and shares memory with the
  renderer." This is why a library signature change is a *compile* event in the
  editor, and why serialization (and thus blend round-trip) is the library's job,
  not the editor's.
- **A5** (`docs/01-architecture.md:229-237`) — multi-canvas is N `HostViewport` /
  `InteractiveRenderer` over one `Document` sharing one `WorkerPool`, "no new
  locking." The damage-sink split this pin lands (consumed by
  `arbc_router_attach_split`) must not change that; this leaf keeps the default-`Config`
  construction, so fan-out semantics are unchanged at pin-land time.
- **A6** (`docs/01-architecture.md:246-250`, v0.4.0 amendment `:252-271`) —
  `CpuBackend` yields CPU tiles composited to GL textures; a pixel-modelling
  `Backend` lives behind the `Backend` seam, "no editor change." The `BlendMode`
  parameter grows `composite*` on that seam, but the editor implements no `Backend`,
  so it is **inert** — witnessed in the pin test, not the constitution.
- **D10** (`docs/00-design.md:477`, body §7 `:272-295`) — "Compositing stays
  **linear** (no v1 blend-space toggle)." The editor exposes **no** blend control in
  v1; it writes no blend field. A document authored elsewhere may carry one, which
  round-trips verbatim and renders per the library — this is faithful pass-through,
  not a user toggle, so D10 is not contradicted.
- **§8 levelization DAG** (`:310-346`) and **§9 testing/DoD** (`:348-411`) — a pin
  bump touches neither the component graph nor the level rules. `render` stays L2
  (`base, project, scene, gl, writer, libarbc`; GL, not ImGui); `scene` and
  `project` stay L1. `scripts/check_levels.py` stays clean.

**libarbc — what the bump brings (the surface this leaf absorbs or witnesses):**

- **v0.6.0, breaking:** `Backend::composite`/`composite_clipped`/`composite_windowed`
  each take a required `BlendMode blend` after `opacity`. `BlendMode::Normal` =
  premultiplied source-over, byte-exact.
- **v0.6.0, additive:** `LayerRecord::blend()` + `Model::Transaction::set_blend`
  (persisted by name, omitted when normal, unknown names preserved verbatim);
  `render_offline`'s trailing `OfflineRenderReport*` out-param, `SequenceRenderer::
  unresolved_contents()`, `count_unresolved_contents` (arbc/runtime/unresolved_contents.hpp);
  `KindMetadata::insertability` + `KindInsertability` + `Registry::insert_offer(id)`.
- **v0.5.0, additive-except-one-enumerator:** `KindInsertField::Type::ObjectId`;
  `org.arbc.nested`'s new insert schema (one labelled `child` field of type
  `ObjectId`, min 1, no default); `RasterContent::bounds()` now derived from the
  live tile table; `gc_project_directory` now marks `params.source` URIs and sweeps
  `assets/images/` (two-arg signature **unchanged**); `AssetReaper`'s three new
  defaulted virtuals (inert); plus the surface the consumer leaves name —
  `HostViewport::attach_damage_sink`/`detach_damage_sink`/`Config::install_damage_sink`,
  `Document::update_content_config`/`undo`/`redo`/`reconcile_content_bindings`/
  `set_content_reconstruct`, `codec_content_reconstruct`, `install_external_composition`,
  `Content::resampleable`/`Resampleable`, `GcRoots`/`collect_referenced_assets`.
- **v0.7.0, additive, unconsumed here:** `KindMetadata::modality`, the `KindModality`
  flag set over Visual/Audio (default Both), `intersects()`, and the widened
  `Registry::insert_offer(id, presentable = KindModality::Both)`. The editor does not
  call `insert_offer` yet (`editor.cells.insert_offer` will), so v0.7.0 adds only the
  tag retarget and pin-test witnesses.

**Editor call sites the bump touches:**

- `CMakeLists.txt:25` — `set(ARBC_GIT_TAG "v0.4.1" CACHE STRING …)`, the one string
  edited. Kept `CACHE` without `FORCE` (preserves the `-DARBC_GIT_TAG=<branch>` /
  `-DARBC_SOURCE_DIR=` co-dev overrides).
- `src/render/dim_scrim.cpp:93` — `backend.composite(frame, *scrim,
  arbc::Affine::identity(), 1.0)` (base `composite`, opacity `1.0`) → add
  `arbc::BlendMode::Normal`.
- `src/render/render.cpp:75` — `backend.composite(*filled, *frame,
  arbc::Affine::identity(), 1.0)` (base `composite`) → add `arbc::BlendMode::Normal`.
- `src/render/canvas_renderer.cpp:188` — `backend.composite(*scratch, *target,
  arbc::Affine::identity(), 1.0)` (base `composite`) → add `arbc::BlendMode::Normal`.
  (No `composite_clipped`/`composite_windowed` call exists anywhere in `src/`; only
  the base overload is called.)
- `tests/arbc_pin_test.cpp:349-352` — the `static_assert` on
  `&arbc::Backend::composite_windowed` asserting
  `void(Surface&, const Surface&, const Affine&, double, const Rect&, const Rect&)`;
  the inserted `BlendMode` after the `double` changes this type, so the witness must
  be updated in the same bump. New v0.5.0/v0.6.0/v0.7.0 witnesses append after the
  v0.4.0 block (`:283-355`); the file is already in the `ace_tests` source list
  (`CMakeLists.txt:219-237`), which already links `arbc::arbc`, so no build-system
  change.
- `src/scene/cell.cpp:32-42` — `map_field_type`, the no-`default` switch to extend.
  `src/scene/ace/scene/cell.hpp:48-52` — the `InsertFieldType` enum to grow.
- `src/scene/cell.cpp:197-214` — `insert_schemas`, one `KindInsertSchema` per
  `registry.ids()` entry (`:199`); the no-allowlist enumeration through which
  `org.arbc.nested`'s new schema appears **automatically**. Untouched; verified.
- `src/project/save.cpp:159-161,165,178` — `capture_snapshot` "copies the
  writer-owned content side-map **and unknown-field stash**"; `serialize_snapshot`
  owns record serialization end-to-end. The editor never reads or writes a `blend`
  field (grep of `src/project/` and `src/scene/` for `blend`/`BlendMode` is empty),
  so an unknown per-layer blend rides the stash and **cannot be dropped**. Read-only
  for this leaf beyond the round-trip test.
- `src/project/gc.cpp:63-86` — `gc_project(...)` calls the two-arg
  `arbc::gc_project_directory(layout.root, dry_run)` at `:80-81` (signature
  **unchanged**, compiles untouched, gains the image mark/sweep for free); the
  no-canonical guard reasoning at `:64-71`; error mapping `map_error` at `:45-55`,
  `GcError` enum at `src/project/ace/project/gc.hpp:36-39`, user surfacing at
  `src/app/project_gateway.cpp:197-211` (returns an empty `GcSummary{}`, `ran=false`,
  discarding the specific kind). Read-only; see Decisions on the malformed-source nuance.

**Harness:** `scripts/gate` reconfigures in place (`check_levels` · clang-format ·
build · ctest); the CI lanes (including `gcc-tsan`, `clang-asan`) are the acceptance
surface; committed goldens live under `tests/goldens/`.

## Constraints / requirements

1. **One leaf, three releases, straight to `v0.7.0`.** Bump `CMakeLists.txt:25`
   directly; do not land intermediate pins. Keep `CACHE` without `FORCE` — adding
   `FORCE` breaks the `-DARBC_GIT_TAG=` / `-DARBC_SOURCE_DIR=` overrides and is not
   acceptable.
2. **Fix exactly the four forced compile sites, and only those.** The three base
   `composite` calls (`dim_scrim.cpp:93`, `render.cpp:75`, `canvas_renderer.cpp:188`)
   and the `composite_windowed` `static_assert` (`arbc_pin_test.cpp:349-352`). Each
   takes `arbc::BlendMode::Normal`. If the implementer finds themselves adding a
   `Backend` subclass, a blend-control widget, or consuming the damage-sink /
   resample / external-composition / undo-reconcile / unresolved-report / insert-offer
   surface, the scope boundary has been crossed — those belong to the named
   downstream leaves.
3. **No golden may move.** `BlendMode::Normal` is byte-exact premultiplied
   source-over (its own kernel arm), and `RasterContent::bounds()` deriving from the
   live tile table changes nothing for documents that never resample. Every
   `tests/goldens/*` is expected **byte-identical**. A golden that moves is a
   **regression to investigate against the changelog**, not a re-baseline
   (D-arbc_v040-2 discipline landing on `arbc_v030` invariance).
4. **The `ObjectId` hint must survive `map_field_type`.** Add the
   `arbc::KindInsertField::Type::ObjectId` case and a matching
   `InsertFieldType::ObjectId` enumerator; do not let it collapse to `Text`. Do
   **not** build a composition picker over it here (that is
   `editor.cells.objectid_field_picker`); the L3 modal rendering the `ObjectId`
   field as a labelled text field is acceptable for this leaf.
5. **The per-layer blend must round-trip, with no editor `src/` change.** The editor
   writes no blend and delegates persistence to `serialize_snapshot`; the constraint
   is that a save→reopen preserves a blend the editor never touches. Satisfy it with
   a test, not a serializer edit — adding an editor-side blend field would duplicate
   the library's serialization and invite drift.
6. **The GC path stays green with the two-arg call unchanged.** `gc.cpp:80-81`
   compiles untouched and gains the image mark/sweep for free. Re-read (do not
   assume) the two consequences the note flags: the referenced set at `gc.cpp:64-71`
   is no longer the same set, and a malformed `params.source` now yields a `GcError`
   where it previously swept. Confirm the existing report/error mapping still reads
   sensibly to a user; the *substantive* consumption of the image mark/sweep is
   `editor.project.gc_owned_images`, not this leaf.
7. **The new pin-test witnesses must fail to *compile* against `v0.4.1`, not merely
   fail to pass.** No runtime version symbol exists and a CMake `VERSION_LESS` guard
   cannot fire (the cache var is not `FORCE`d and `gate` reconfigures in place).
   Name v0.5.0/v0.6.0/v0.7.0-only symbols as unevaluated `static_assert` operands
   (the `:255-267` / `:283-355` pattern) so a stale `build/` tree red-lights at the
   compiler.
8. **Levelization untouched.** No component added, no DAG edge added; the L1 core
   gains no ImGui/GL/SDL include; `scripts/check_levels.py` stays clean.
9. **No new sanitizer suppression.** No `tests/lsan.supp` entry; `gcc-tsan` and
   `clang-asan` stay green with the existing anchors unmodified (this pin changes no
   editor threading behaviour — the real-pool flip is `nested_real_pool_restore`'s
   work, and the `bounds()`-derivation TSan case belongs to `resample_apply`).

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate`
green (`check_levels` · clang-format · build · ctest) is the umbrella.

- **Levelization — `scripts/check_levels.py` clean.** No component or edge added; a
  pin bump cannot change the DAG. `render` stays L2, `scene`/`project` stay L1, no L1
  component gains an ImGui/GL/SDL include.
- **Pin-effectiveness + guarantee pin — Catch2, headless, `tests/arbc_pin_test.cpp`.**
  The existing behavioural cases pass with prose retargeted to `v0.7.0`. The
  `composite_windowed` `static_assert` (`:349-352`) is updated to the new
  `(…, double, arbc::BlendMode, const Rect&, const Rect&)` signature. **New
  unevaluated `static_assert` witnesses** name the version-only surface such that a
  `v0.4.1` tree **fails to compile**: (v0.6.0) `composite`/`composite_clipped`/
  `composite_windowed`'s `BlendMode` parameter, `LayerRecord::blend()`,
  `Model::Transaction::set_blend`, the `render_offline` `OfflineRenderReport*`
  out-param + `SequenceRenderer::unresolved_contents()` + `count_unresolved_contents`,
  `KindMetadata::insertability` + `KindInsertability` + `Registry::insert_offer(id)`;
  (v0.5.0) `KindInsertField::Type::ObjectId`, `attach_damage_sink`/`detach_damage_sink`/
  `Config::install_damage_sink`, `Document::update_content_config`/`undo`/`redo`/
  `reconcile_content_bindings`/`set_content_reconstruct`, `codec_content_reconstruct`,
  `install_external_composition`, `Content::resampleable`/`Resampleable`,
  `GcRoots`/`collect_referenced_assets`; (v0.7.0) `KindMetadata::modality`,
  `KindModality`, `intersects()`, the widened `Registry::insert_offer(id, presentable)`.
  This is the primary functional assertion, and pins *existence and shape* — behavioural
  consumption stays with each downstream leaf.
- **L1 logic — Catch2, headless.** A unit test for `scene::map_field_type` asserting
  `arbc::KindInsertField::Type::ObjectId` → `InsertFieldType::ObjectId` (the new arm;
  extend the existing `cell` model test). A `scene::insert_schemas` unit test
  asserting `org.arbc.nested` emits one entry with a single **labelled** `child`
  field of type `ObjectId`, min 1, no default, `raw_config == false` — the property
  the parked item closes. A `project`-level round-trip test constructing a `Model`
  with a per-layer blend via `Model::Transaction::set_blend`, saving via
  `save_project`, reopening, and asserting `LayerRecord::blend()` survives (pins
  Constraint 5). Existing `gc` tests pass unmodified.
- **Rendered output — goldens, invariance asserted.** Every `tests/goldens/*`
  (`export_camera_64x64.png` and the `.rgba8` set) **byte-identical and unmodified**;
  no `.rgba8` regenerated, no `.actual` produced. The three composite sites exercise
  `dim_scrim`, the filled background, and the isolation-dim scratch path, so a green
  golden run is the proof the `BlendMode::Normal` fixes are truly source-over. **No
  re-baseline is expected**; any movement is investigated against the changelog and
  treated as a regression unless the changelog names a cause.
- **UI e2e — ImGui Test Engine.** An e2e (or a screenshot-baselined assertion within
  an existing insert-dialog e2e) confirming the `org.arbc.nested` `child` field
  renders as a **labelled field**, not the raw-config box — "verify it renders
  labelled" is the whole point of the v0.5.0 #33 item. No other UI changes, so the
  remaining Test Engine suites pass unmodified.
- **Threading — ASan/TSan lanes.** `gcc-tsan` and `clang-asan` green with
  `tests/canvas_host_test.cpp`'s anchors passing **unmodified**; the pin changes no
  editor threading behaviour. No new `tests/lsan.supp` entry.
- **Coverage.** `diff-cover --fail-under=90` on the changed lines. The new executable
  lines are the single `map_field_type` `ObjectId` arm and the enumerator (covered by
  the unit test) and the three one-token composite edits (covered by the goldens);
  `CMakeLists.txt:25` and the compile-time witnesses are not code-covered by
  construction.
- **Doc delta — none.** This pin adds no seam, no external dependency, and deviates
  from no stated decision; A1/A5/A6 and D10 are unaffected (see Decisions). Signature
  growth is recorded in `tests/arbc_pin_test.cpp`, where the series records
  signatures — not in the constitution.
- **Deferred WBS work — one new leaf registered.**
  `editor.cells.objectid_field_picker` — "Render an `ObjectId` insert-schema field as
  a composition picker, not free text." Effort `1.5d`, `allocate team`,
  `depends editor.canvas.arbc_v070, editor.cells.insert_schema`, milestone **M9E**
  (transitively via `editor.cells`). Scope: the L3 insert modal renders an
  `InsertFieldType::ObjectId` field (today `org.arbc.nested`'s `child`) as a picker
  over existing compositions, resolving the chosen `ObjectId` back into the field's
  collected string, with an ImGui Test Engine e2e; it holds no kind allowlist (A16
  preserved). Source-of-debt: `tasks/refinements/canvas/arbc_v070.md`. The closer
  registers it in the WBS and wires it under `editor.cells`. Every other consumer of
  the v0.5.0/v0.6.0/v0.7.0 surface **already exists** as a leaf `depends` on this pin.

## Decisions

- **D-arbc_v070-1 — one leaf absorbs v0.5.0 + v0.6.0 + v0.7.0; no intermediate
  pins.** *Rationale:* (i) nothing in the editor ever consumed v0.5.0 or v0.6.0 as a
  standalone pin, so bumping through them would absorb the same churn in two or three
  passes for no gain; (ii) it retires the `arbc_router_split_pin` placeholder honestly
  — v0.5.0 *is* the DamageRouter-split release the placeholder named, plus six things
  it never scoped, so a normal pin-bump leaf is the truthful shape. *Alternative
  rejected:* three sequential pins (v0.5.0, then v0.6.0, then v0.7.0) — more commits,
  more re-baseline windows, no attribution benefit, and it keeps a stale placeholder
  alive. **No doc delta** (WBS shape; the closer owns the `.tji`).
- **D-arbc_v070-2 — the four composite sites take `BlendMode::Normal`, and no golden
  moves.** *Rationale:* the release states `BlendMode::Normal` is premultiplied
  source-over *exactly*, computed by its own kernel arm — the pixels are identical to
  the pre-blend path. The bump is a required-parameter break, not a pixel change, so
  the acceptance is golden **invariance** (`arbc_v030` stance), not the four justified
  re-baselines `arbc_v040` budgeted. *Alternative rejected:* treating this like
  `arbc_v040` and budgeting for golden churn — the wrong model here; it invites a
  reflexive re-baseline that would hide a real regression (a golden moving would mean
  `Normal` is *not* byte-exact source-over, which is a finding to escalate upstream).
  **No doc delta.**
- **D-arbc_v070-3 — the per-layer blend round-trip is verify-only; no editor
  serialization code.** *Rationale:* the editor links `arbc` directly (A1) and
  delegates all record serialization to `serialize_snapshot`, whose snapshot copies
  the "unknown-field stash" (`save.cpp:159-161`); the editor never reads or writes a
  blend field, so an unknown per-layer blend is preserved by construction. A test
  pins the property; a serializer edit would only add a second, drift-prone copy of a
  fact the library already owns. *Alternative rejected:* adding an editor-side blend
  read/write to the save path — duplicates the library, risks the two disagreeing,
  and would be the first step toward a blend-control UI D10 forbids in v1. **No doc
  delta:** D10 governs a *user toggle* and the *blend space*, neither of which this
  touches — faithful pass-through of a document-authored blend is A1's delegation,
  already true for any unknown field.
- **D-arbc_v070-4 — add the `ObjectId` enumerator now; defer the picker.**
  *Rationale:* `map_field_type`'s no-`default` switch silently maps the new value to
  `Text` and the build has no `-Wswitch` to catch it, so the one bit the type carries
  is lost unless the arm is added — a two-line fix that belongs with the pin that
  introduces the value. The *picker* over the field is separable L3 UI work with its
  own e2e, so it is a named follow-up rather than scope creep here. *Alternative
  rejected:* (a) render the picker now — crosses the pin's scope boundary into L3 UI;
  (b) leave the value mapping to `Text` and skip the enumerator — throws away exactly
  the hint v0.5.0 added, which is the debt. **No doc delta:** A16's no-allowlist rule
  is preserved (the enumeration still asks the registry about every id).
- **D-arbc_v070-5 — the GC bump is inert-and-verified here; the malformed-source
  nuance is `gc_owned_images`'s.** *Rationale:* `gc_project_directory`'s two-arg
  signature is unchanged (`asset_gc.hpp:179-180`), so `gc.cpp:81` compiles untouched
  and gains the image mark/sweep for free; the substantive consumption (owned-image
  roots, report surfacing) is `editor.project.gc_owned_images`. This leaf's obligation
  is to *re-read* the two consequences and confirm no regression: the empty-referenced-set
  guard at `gc.cpp:64-71` still fires (a never-saved project still no-ops without
  sweeping, because the canonical-existence check precedes the call), and a malformed
  `params.source` now surfaces as `ran=false` / nothing-reclaimed via
  `project_gateway.cpp:197-211` — safe (nothing wrongly deleted), if terse.
  *Alternative rejected:* improving the malformed-source error surfacing in this leaf
  — that is the consumer leaf's design space, and doing it here would entangle a pin
  bump with a UX change. **No doc delta.**

## Open questions

(none — all decided.)

Two items are surfaced for human review rather than encoded as WBS work:

- **Whether the insert-dialog path to mint `org.arbc.nested` is worth the picker at
  all**, given `editor.import.nested` is the primary nested-creation flow — a product
  judgment. This leaf takes the defensible default the `.tji` note already implies
  ("a composition picker … is a LATER call"): keep the field, render it labelled, and
  register `editor.cells.objectid_field_picker` as the follow-up. A human may instead
  decide the dialog nested-insert path is redundant and drop that follow-up.
- **Whether a malformed `params.source` deserves a distinct user-facing GC message**
  (rather than the current `ran=false` swallow at `project_gateway.cpp:208`) — a UX
  call that belongs to `editor.project.gc_owned_images`'s design space, noted so that
  leaf's author inherits the observation.

## Status

**Done** — 2026-08-09.

- `CMakeLists.txt:25` — pin bumped `v0.4.1` → `v0.7.0`, `CACHE` without `FORCE`.
- `src/render/dim_scrim.cpp`, `src/render/render.cpp`, `src/render/canvas_renderer.cpp` — three `backend.composite(…)` call sites each gained `arbc::BlendMode::Normal`; no golden moved.
- `src/scene/ace/scene/cell.hpp` — `InsertFieldType::ObjectId` enumerator added.
- `src/scene/cell.cpp` — `map_field_type` arm for `arbc::KindInsertField::Type::ObjectId` added; hint now survives instead of collapsing to `Text`.
- `tests/arbc_pin_test.cpp` — `composite_windowed` `static_assert` updated to new `BlendMode` signature; full v0.5.0/v0.6.0/v0.7.0 unevaluated witness block added; prose retargeted to `v0.7.0`; also fixed the `ArbcRenderOfflinePinned` witness for the v0.6.0 `OfflineRenderReport*` trailing out-param.
- `tests/cell_model_test.cpp` — unit tests for `map_field_type` `ObjectId` arm and `insert_schemas` labelled `child` field on `org.arbc.nested` added; stale v0.5.0 raw-config assertions updated.
- `tests/project_save_test.cpp` — per-layer blend `set_blend`→save→reopen round-trip test added.
- `tests/cells_insert_e2e_test.cpp` — e2e assertion that `org.arbc.nested` `child` field renders labelled (not raw-config box) added.
- Follow-up `editor.cells.objectid_field_picker` registered in WBS (1.5d, M9E via `editor.cells`).
