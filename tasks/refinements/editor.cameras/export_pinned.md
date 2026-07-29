# editor.cameras.export_pinned — Pin the document once per batch export so every item renders one state

## TaskJuggler entry

- **Task:** `editor.cameras.export_pinned` — *"Pin the document once per batch
  export so every item renders one state"* (`tasks/00-editor.tji:511-516`), under
  `task cameras "Cameras"` (`tasks/00-editor.tji:412`), inside `task editor`.
- **Effort:** `1d` · `allocate team`.
- **Depends:** `editor.canvas.arbc_v040` (`complete 100`,
  `tasks/00-editor.tji:346-351`; the pin is `ARBC_GIT_TAG "v0.4.0"`,
  `CMakeLists.txt:25`) and `!export_destination_reseed`
  (`editor.cameras.export_destination_reseed`, `complete 100`,
  `tasks/00-editor.tji:504-510`).
- **Note (`.tji:515`, abridged):** *"ruoso/arbitrarycomposer#27 shipped
  `render_offline(document, pinned, viewport, backend)` in v0.4.0
  (`arbc/runtime/offline.hpp:42`) beside the pin-per-call overload … Take the pin
  ONCE for the batch and pass it to every item. A `DocStatePtr` retains its
  version, so there is no 'that revision is gone' error case … THE REPORTING FIELD
  THEN CHANGES MEANING and must not be left as-is … Either retire it with its
  views.cpp notice, or keep it and reword the notice to say what is now true …
  Cancellation stays honest at ITEM granularity (Constraint 10, export.cpp:250) —
  unchanged … tests/export_test.cpp's revision cases are where the new invariant is
  pinned: an edit committed mid-batch must leave every item byte-identical to a
  batch with no concurrent edit. Design: docs/00-design.md D14,
  docs/01-architecture.md A20."*
- **Back-link:** this refinement lands at
  `tasks/refinements/editor.cameras/export_pinned.md`. **The closer** appends
  `Refinement: tasks/refinements/editor.cameras/export_pinned.md` to the `.tji`
  note (after the `Design:` citations) and adds `complete 100` immediately after
  `allocate team`. **Do not** hand-edit the `.tji` here.
  *(Layout note for the closer: the camera area is split three ways across
  `tasks/refinements/editor/`, `tasks/refinements/editor.cameras/` and
  `tasks/refinements/cameras/`. This leaf is filed under `editor.cameras/`
  alongside its direct predecessor `export.md`, whose decisions it amends;
  normalizing the split is out of scope.)*
- **Downstream dependents:** none declared. `editor.packaging.package`
  (`tasks/00-editor.tji`) gates on `editor.cameras.export`, not on this leaf; this
  task hardens a coherence property of a feature already shipped and gated.
- **Milestone:** `m9_editor` (`tasks/99-milestones.tji:6-8`), reached through the
  `editor.cameras` container dependency. No new milestone wiring is required; this
  leaf registers no follow-up.

## Effort estimate

**1 day**, unchanged from the `.tji`. Every seam is shipped: the pinned
`render_offline` overload exists and is guarded at the pin
(`tests/arbc_pin_test.cpp:333-344`), the export kernel already injects its render
step as an opaque callable (A20), and the report already carries the revision
fields. The work is threading one `DocStatePtr` through the existing injection and
truing up one repurposed field plus its notice.

- **~0.4d — thread the batch pin.** `commands` gains a `PinFn` seam and a leading
  `DocStatePtr` argument on `RenderFn`; `run_export` takes the pin once and passes
  it to every item and to the contact-sheet phase; L2 `render` gains a pinned
  render entry point over the 4-arg `render_offline`; L4 `app` binds the pin
  provider and forwards the pin through the injected renderer.
- **~0.1d — repurpose the coherence field + reword the notice** (D-pinned-2).
- **~0.4d — tests.** The byte-coherence Catch2 case (an edit committed mid-batch
  leaves every item byte-identical to an edit-free batch), the update to the
  existing revision case (`export_test.cpp:534`), a TSan case exercising a
  concurrent writer against the held pin, and confirming the 1:1 export goldens
  stay byte-identical.
- **~0.1d — the A20 doc delta, the Status block, and `scripts/gate` green.**

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.canvas.arbc_v040`** (`tasks/refinements/canvas/arbc_v040.md`, Done
  2026-07-28) — the pin bump that landed the surface this leaf consumes:
  - The **caller-pinned overload** `arbc::render_offline(const Document&, const
    DocStatePtr& state, const Viewport&, Backend&)`
    (`arbc/runtime/offline.hpp:42-45`), issue #27, whose header comment
    (`offline.hpp:23-45`) states the exact rationale this leaf realizes: *"The host
    pins once, renders N frames against it, and drops the pin; the writer is never
    blocked and no second document exists … A null pin renders nothing and returns
    `SurfaceError::UnsupportedFormat`."* arbc_v040 **left this unconsumed in
    `src/`** by design (D-arbc_v040-1) and reserved it to this leaf.
  - The **existence guard**: `tests/arbc_pin_test.cpp:333-344` is a compile-time
    `static_assert` witnessing the 4-arg overload's signature (tagged
    `editor.cameras.export_pinned`, `.tji:487`). It pins the *shape*; this leaf
    adds the *behaviour*.
  - The **1:1 export goldens are byte-stable at v0.4.0** (D-arbc_v040-2): the
    `.tji`-predicted re-baseline of `export_camera_64x64.png` was *disproven* by
    the trial and did not move; `export_camera_64x64.{rgba8,png}` and
    `export_filled_bg_64x64.rgba8` stayed byte-identical because a 1:1 camera
    export composites at an integral phase where the tiled and untiled paths agree.
    This leaf inherits that fact as a **constraint**: switching to the pinned
    overload is a coherence change, not a pixel change, so an export golden that
    moves here is suspect (Constraint 6).
- **`editor.cameras.export`** (`tasks/refinements/editor.cameras/export.md`, Done
  2026-07-23) — the kernel this leaf amends. Every seam is shipped:
  - **The injected render step** — `using RenderFn = std::function<Srgb8Image(const
    arbc::Affine& camera, int width, int height, const std::optional<Rgba8>&
    background)>` (`src/commands/ace/commands/export.hpp:197-198`), bound at L4 to
    `render::render_document_srgb8` / `_over` (`src/app/shell.cpp:462-471`). This is
    A20's L1→L2 inversion: `commands` never calls `render`, so the pin must ride
    through this callable, not around it.
  - **The report** — `ExportReport` (`export.hpp:173-194`) carries
    `start_revision` (`:191`), `end_revision` (`:192`) and
    `document_changed_during_export` (`:193`), whose own comment (`:185-190`)
    predates this leaf and describes the pin-per-call world.
  - **The revision read** — `using RevisionFn = std::function<std::uint64_t()>`
    (`export.hpp:205-206`, comment `:204`: *"the injected any-thread
    document-revision read (`document.pin()->revision()`)"*), bound at
    `shell.cpp:473`.
  - **The runner** — `ExportRunner { render; filesystem; publish; cancel;
    revision; }` (`export.hpp:276-284`); `run_export(plan, options, runner)`
    (`:290-291`).
  - **D-export-8** — the coherence decision this leaf supersedes: *"`render_offline`
    pins the current version per call, so an edit landing mid-batch can make item 3
    reflect a document item 1 did not … Recording and reporting it is the honest
    third option … it turns a silent incoherence into a stated one"*
    (`export.md:829-853`). export.md's own Open Question (`:936-944`) named exactly
    this successor: *"whether `render_offline` should accept a retained document
    version (or a pin) so a batch export can be exactly coherent"* — libarbc #27
    answered it, and this leaf consumes the answer.
  - **D-export-7 / Constraint 10** — the async job and its **item-granularity
    cancel** (`export.cpp:250-256`, `:327-330`): a started item always finishes;
    cancel is checked between items. **Unchanged here** — a pin held across a
    cancelled batch simply releases earlier.
- **`editor.cameras.export_destination_reseed`**
  (`tasks/refinements/cameras/export_destination_reseed.md`, Done 2026-07-24) —
  the declared `!` predecessor. Test-only, unrelated to the render pinning model
  (it hardens the destination-path re-seed); consumed only as the ordering
  predecessor.

**Pending (owned here):**

- The `PinFn` seam and the leading `DocStatePtr` on `RenderFn`; the pinned L2
  render entry point; the L4 binding; the repurposed field + reworded notice; the
  byte-coherence, revised-revision, and TSan tests; and the **A20 doc delta**
  (D-pinned-3).

## What this task is

Batch export currently renders each camera with its own `render_offline` call,
and each call independently pins whatever document version is *current when it
runs* (`export.cpp:270-271` → the 2-arg overload at `render.cpp:30,52` →
`offline.hpp:20-21`). So a twelve-camera batch pins twelve times, and a writer
commit that lands between item 1 and item 3 makes item 3 reflect a document state
item 1 did not — an incoherent output. export.md's D-export-8 could only *report*
that (two `revision()` reads, a `document_changed_during_export` flag) because the
library offered no way to render a retained version offline.

libarbc v0.4.0 issue #27 removed that limitation: `render_offline(document,
pinned, viewport, backend)` renders against a `DocStatePtr` the caller pinned, and
a `DocStatePtr` *retains* its version. This leaf takes the pin **once** for the
whole batch — before the first item — holds it across every item and the
contact-sheet phase, and drops it at the end. The output is now a faithful
snapshot of the document as it stood when Export was pressed, regardless of
concurrent edits, and the writer is never blocked (the pin is a cheap lock-free
read; A4.1 / issue #23).

Because the batch is now coherent by construction,
`document_changed_during_export` stops meaning *"your output might be
incoherent"*. It is repurposed to an informational note — *the live document
advanced past the version you exported* — and its user-facing notice is reworded
to say that, because a stale warning telling the user their coherent export might
be incoherent is worse than none (`.tji:515`).

## Why it needs to be done

- **It closes a registered debt with a now-available library fix.** export.md
  surfaced the version-addressed offline render to the parking lot as a *library*
  judgement (`export.md:936-944`); the parking-lot triage (2026-07-28) promoted
  the shipped #27 overload into this WBS leaf. The capability exists, is guarded at
  the pin, and is unconsumed — this leaf is the consumer.
- **The incoherence is real, not theoretical.** D14 promises async export *with a
  responsive UI* — which means the writer keeps accepting edits while a multi-frame
  batch renders. Every such edit that lands mid-batch is a chance for the D-export-8
  incoherence to occur. Reporting it was the honest interim; enforcing coherence is
  the correct end state now that the library supports it.
- **A stale reporting field is a live defect.** Leaving
  `document_changed_during_export` and its notice *"The document changed during
  this export."* (`views.cpp:461`) unchanged after the batch becomes coherent would
  tell users their coherent export is suspect — precisely the quiet wrongness A19's
  *"the loss is announced rather than silent"* rejects, inverted into a false
  alarm. The field must change meaning in the same commit as the pin.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **`docs/01-architecture.md` A20** (`:435`) — the export pipeline's structure:
  the L1 `commands` kernel driven by an *injected* renderer, the L1→L2 inversion,
  the `platform::Threads` job, and specifically the clause this leaf reverses:
  *"Because `render_offline` pins the current version per call, batch coherence is
  **reported, not enforced**: the report carries the start/end `revision()` and a
  `document_changed_during_export` flag."* **This leaf amends A20** (D-pinned-3).
- **`docs/00-design.md` D14** (`:481`, §9 prose `:355-367`) — *"pick camera(s) →
  render each at its resolution (`render_offline`) → file(s) … Heavy renders run
  async with progress."* Unchanged: this leaf keeps the async job and its progress;
  it only makes the render version-stable across the batch. No `D`-row change.

**Library surface (via FetchContent, pinned at v0.4.0):**

- `arbc/runtime/offline.hpp:42-45` — `render_offline(const Document&, const
  DocStatePtr& state, const Viewport&, Backend&)`; comment `:23-45` (pin once,
  render N, drop; null pin → `SurfaceError::UnsupportedFormat`).
- `arbc/runtime/offline.hpp:20-21` — the incumbent 2-arg overload the editor uses
  today; **kept** for `render_probe_srgb8` and any non-export caller (Constraint 4).
- `arbc/runtime/document.hpp:375` — `DocStatePtr pin() const;` (*"Pin the current
  version for rendering"*; lock-free, any thread). `DocStatePtr` is
  `std::shared_ptr<const DocRoot>` (`arbc/model/model.hpp:199`); `pin->revision()`
  is already called throughout `commands` (`src/commands/app_state.cpp:94,109`),
  so no new include or level edge is introduced by holding one in `commands`.

**Editor seams this leaf modifies:**

- `src/commands/ace/commands/export.hpp:197-198` — `RenderFn` (gains a leading
  `const arbc::DocStatePtr& pin`); `:205-206` `RevisionFn` (kept, now the *live
  end* read); `:276-284` `ExportRunner` (gains a `PinFn pin` member); `:304-374`
  `ExportService` (gains `set_pin`, beside `set_renderer`/`set_revision` at
  `:323-326`); `:173-194` `ExportReport` and its `:185-190` comment (reworded).
- `src/commands/export.cpp:219-222` — start-revision read (now from the pin);
  `:270-271` the per-item `runner.render(...)` call (gains the pin argument);
  `:374-378` end-revision + flag (repurposed); `:250-256`,`:327-330` the
  item/tile cancel checks (**unchanged**).
- `src/render/ace/render/render.hpp:40-41,62-63` and `src/render/render.cpp:24-45,
  47-86` — `render_document_srgb8` / `_over`; add pinned entry points calling the
  4-arg overload. `render.cpp:90` (`render_probe_srgb8`) stays on the 2-arg path.
- `src/app/shell.cpp:462-471` — the `set_renderer` closure (forwards the pin);
  `:473` `set_revision` (kept); add `set_pin([&app_state] { return
  app_state.document().pin(); })`.
- `src/views/views.cpp:458-462` — the `document_changed_during_export` notice
  (reworded, D-pinned-2).

**Test rigs this leaf builds on:**

- `tests/export_test.cpp` — the headless Catch2 home; already links real `render`
  for its goldens (`:671` render-through-camera golden, `:707` filled-background
  golden) and stubs the runner for the logic matrix. Existing revision cases:
  `:486` *"run_export reports every outcome as a value"* (constant revision) and
  `:534` *"a mid-batch document edit is reported, not silently mixed in"*
  (post-incrementing revision → flag true). The `:534` case's **meaning** changes
  here; a new byte-coherence case is added.
- `tests/canvas_host_test.cpp` — the export/threading TSan anchor (extended by
  contact_sheet); the concurrent-writer-against-held-pin case joins it.
- `tests/arbc_pin_test.cpp:333-344` — the #27 existence witness (unchanged;
  behaviour now lives here).
- Golden set: `tests/goldens/export_camera_64x64.{rgba8,png}`,
  `tests/goldens/export_filled_bg_64x64.rgba8` — must stay byte-identical
  (Constraint 6).
- `CMakeLists.txt:252` region (`ace_tests`), `:270-291` (`ace_shell_test`).

**Predecessor refinements:**

- `tasks/refinements/editor.cameras/export.md` — D-export-1 (the injection),
  D-export-7 (the async job / item cancel), **D-export-8** (the coherence model
  this leaf supersedes), and the Open Question (`:936-944`) that named this leaf.
- `tasks/refinements/canvas/arbc_v040.md` — D-arbc_v040-1 (surface reserved),
  D-arbc_v040-2 (export goldens byte-stable).

## Constraints / requirements

1. **One pin for the whole batch.** `run_export` obtains a single
   `arbc::DocStatePtr` before the first item (via the injected `PinFn`), holds it
   across **every** item **and** the contact-sheet phase, and drops it when the
   function returns. Every render — item and sheet — goes through the 4-arg
   `render_offline(document, pin, viewport, backend)`. No item re-pins.
2. **The pin rides through the injection; the L1→L2 inversion is preserved.**
   `commands` holds the `DocStatePtr` as a value and passes it as the leading
   `RenderFn` argument; it never calls `render`. A20's opaque-`std::function`
   inversion is unchanged — the pin is data, not a render dependency.
3. **`document_changed_during_export` is repurposed, not left stale.** With one pin
   the batch is coherent, so the field becomes an informational *"the live document
   advanced past the exported version"* note. `start_revision` is the **pinned**
   version; `end_revision` is the **live** version at completion; the flag is
   `end != start`. The `views.cpp:461` notice is reworded to say what is now true
   (D-pinned-2). A stale coherence warning must not survive this commit.
4. **The 2-arg render path survives.** `render_probe_srgb8` and any non-export
   caller keep the pin-per-call overload; only the export render entry points move
   to the 4-arg overload. A null pin (a runner without `PinFn`, e.g. a pure-stub
   test) must not crash: the pinned render returns an empty image (the library's
   `UnsupportedFormat` on a null pin), reported as a failed item.
5. **Cancellation stays honest at item granularity.** The between-item /
   between-tile cancel checks (`export.cpp:250-256`,`:327-330`) are unchanged; a
   pin held across a cancelled batch just releases earlier. No finer cancellation
   is invented (`render_offline` still exposes no cancel hook).
6. **The 1:1 export goldens do not move.** `export_camera_64x64.{rgba8,png}` and
   `export_filled_bg_64x64.rgba8` stay **byte-identical**: at an integral 1:1 phase
   the pinned and pin-per-call overloads render the same version to the same bytes.
   A moved export golden here is a regression to investigate, not a re-baseline —
   the same "justify every diff" discipline arbc_v040 established (D-arbc_v040-2).
7. **No new component, no new DAG edge, no new external, no new thread.**
   `scripts/check_levels.py` is unmodified. `commands` already depends on `arbc`
   (external) and holds `arbc::Document`/`Affine`/`ObjectId`; a `DocStatePtr` is
   within that dependency. `render` already calls `render_offline`; the 4-arg call
   is within its `arbc` dependency. The pin flows L4→L1→L2 through the existing
   injection.
8. **Errors stay values.** A failed render (including the null-pin path) lands in
   the per-item `ExportReport` entry with a message; a failed item never stops the
   batch (D-export-4). The pin read is lock-free and does not throw.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the umbrella.

**Levelization (`check_levels` clean) — the structural assertion:**

- `scripts/check_levels.py` is **unmodified**; no component, edge, or external is
  added (Constraint 7). The `DocStatePtr` is threaded through `commands`'s existing
  `arbc` dependency and `render`'s existing `render_offline` call; the L1→L2
  inversion (`commands` never includes `render`) is preserved (Constraint 2).

**L1 logic — Catch2 units** in `tests/export_test.cpp` (`ace_tests`,
`CMakeLists.txt:252` region), reusing its real-`render` + stub-runner scaffolding:

- **The batch-coherence invariant (the pin's whole point).** A new case
  `TEST_CASE("export: a batch renders one frozen version even under a concurrent
  edit")`: build a real `arbc::Document` with **two** cameras over distinct,
  visibly different content; run a batch whose injected renderer, **between** item
  1 and item 2, commits a transaction that changes the composition; assert every
  produced item is **byte-identical** to the corresponding item of an
  **edit-free** batch over the same document. This is the `.tji`'s stated
  invariant: *"an edit committed mid-batch must leave every item byte-identical to
  a batch with no concurrent edit."*
- **Anti-vacuity guard (the test has teeth).** In the same case, assert that the
  mid-batch edit **would** change the output under a re-pinning render — i.e. a
  fresh 2-arg `render_offline` of item 2's camera *after* the commit differs from
  item 2's pinned bytes. Without this, a coherence test passes vacuously on an
  inert edit; with it, the byte-identity above is a real assertion that the pin,
  not luck, froze the version.
- **The repurposed reporting field.** Update `export_test.cpp:534` (*"a mid-batch
  document edit is reported, not silently mixed in"*) to the new meaning:
  `start_revision` is the pinned version, `end_revision` the live version at
  completion, `document_changed_during_export == (end != start)` — now an
  **informational** flag, asserted alongside the byte-coherence of the *output*
  (the flag firing does **not** imply a mixed output). The `:486` constant-revision
  case (`start == end`, flag false) stays valid unchanged.

**Rendered output — goldens (byte-exact, `render_offline`):**

- `tests/goldens/export_camera_64x64.{rgba8,png}` and
  `export_filled_bg_64x64.rgba8` are re-run through the **pinned** export path and
  asserted **byte-identical** to the shipped goldens (Constraint 6). No new golden
  and no re-baseline; the assertion is that the pin changed coherence, not pixels.
  A diff here fails the build and is investigated against the v0.4.0 changelog
  before any baseline is touched.

**UI e2e — ImGui Test Engine:**

- The reworded notice is asserted in the existing Export-panel e2e lane
  (`tests/export_e2e_test.cpp`, the `cameras/export_panel` harness): drive a batch
  with a concurrent edit so the informational flag is set, and assert the panel
  shows the **new** wording (an informational note about the live document moving
  on), **not** the retired *"The document changed during this export."* coherence
  warning. A pixel screenshot is not warranted — the assertion is on the surfaced
  string and the report state, not on layout.

**Threading (ASan/TSan) — explicitly scoped:**

- The export job holds a `DocStatePtr` on the export thread while the writer
  commits new versions on the writer thread — the lock-free pinned-read contract
  (A4.1 / issue #23; `Content::bounds()` any-thread, which arbc_v040's
  D-arbc_v040-5 re-anchored as a stated guarantee). A TSan case joins the export
  anchor in `tests/canvas_host_test.cpp`: spawn the export job over a held pin,
  commit writer transactions concurrently for the batch's duration, and assert
  (a) **no** TSan data-race report, and (b) the exported bytes match a
  no-concurrent-writer run (the pin isolates the reader). No new
  `tests/lsan.supp` suppression; the job adds no new shared mutable state beyond
  the `shared_ptr` refcount the library owns.

**Coverage:**

- ≥90% diff coverage on changed lines (`diff-cover --fail-under=90`);
  clang-format and build clean. The changed production lines (the pin threading,
  the repurposed flag, the render entry point, the shell binding) are exercised by
  the byte-coherence case, the revised revision case, and the e2e.

**Doc delta (same commit):**

- **`docs/01-architecture.md` A20** — an inline amendment parenthetical (matching
  the contact_sheet amendment style already on A20) recording that as of v0.4.0 the
  batch takes one `DocStatePtr` pin and renders every item and the contact sheet
  against it through #27's 4-arg `render_offline`, so coherence is **enforced, not
  reported**, and `document_changed_during_export` is repurposed to an
  informational live-vs-pinned note with a reworded notice (D-pinned-3). **Written
  ahead of implementation in this refinement's commit line; the closer confirms it
  rides the implementation commit.**

**Deferred WBS work (closer registers in the WBS):**

- **None.** This leaf closes the parking-lot debt promoted into it and spawns no
  follow-up. `render_probe_srgb8` and the 2-arg overload are retained, not
  migrated, so no cleanup task is left behind.

## Decisions

- **D-pinned-1 — One `DocStatePtr` per batch, threaded through the existing
  injected `RenderFn`; `commands` never calls `render`.**
  `ExportRunner` gains a `PinFn pin` (`std::function<arbc::DocStatePtr()>`) and
  `RenderFn` gains a leading `const arbc::DocStatePtr&`. `run_export` calls
  `runner.pin()` once before the item loop, holds the result across both phases,
  and passes it to every `runner.render(pin, camera, w, h, background)`. L2 `render`
  gains a pinned entry point over `arbc::render_offline(document, pin, viewport,
  backend)`; L4 binds `set_pin([&]{ return app_state.document().pin(); })` and the
  `set_renderer` closure forwards the pin.
  *Rationale:* the pin is pure data (a `shared_ptr<const DocRoot>` that *retains*
  its version), so holding it in `commands` costs nothing structural and adds no
  DAG edge — `commands` already depends on `arbc` and calls `pin->revision()`. The
  impure step (the actual render) stays behind A20's opaque callable, so the
  plan/refusal/cancel/progress matrix stays headless-testable with a stub renderer
  (the stub simply ignores the pin). One pin held across the whole `run_export`
  makes the contact sheet coherent with the items *and* with each other, for free.
  *Alternative rejected:* **pin per call (the status quo).** It is what D-export-8
  could only *report*; #27 exists precisely to retire it, and leaving it in place
  keeps a real incoherence the library now lets us eliminate.
  *Alternative rejected:* **snapshot the document up front** (`capture_snapshot`,
  render from a reloaded document). D-export-8 already rejected this: a snapshot is
  a serialization artifact, not a renderable `Document`, so it means a full
  parse-and-rebuild per export — a `DocStatePtr` is the cheap, in-model answer the
  library was already holding.
  *Alternative rejected:* **block the writer for the batch.** Contradicts D14's
  async promise and A4's *"the UI thread stays responsive"*; a dozen 4K frames
  would freeze editing for minutes. The pin blocks nothing.
  **Doc delta: A20** (D-pinned-3).

- **D-pinned-2 — Keep `document_changed_during_export`, repurpose it to an
  informational live-vs-pinned note, and reword the notice.**
  `start_revision` is the pinned version (`pin->revision()` in production; the
  injected `RevisionFn` remains the fallback when a stub runner supplies no pin, so
  the existing headless cases keep working). `end_revision` is the **live**
  document revision at completion (`runner.revision()`). The flag stays `end !=
  start` but now means *"the on-screen document advanced past what you exported"*,
  not *"your output might be incoherent"*. The `views.cpp:461` notice is reworded
  to state that (e.g. *"Exported the document as it was when the export started; it
  has changed since."*).
  *Rationale:* the field is genuinely useful under the new model — it tells a user
  who edited after pressing Export that those edits are **not** in the output (the
  batch was pinned before them), which is real information, not noise. Keeping the
  plumbing and changing the meaning is minimal churn and preserves the existing
  test/notice wiring. The `.tji` is explicit that a stale warning is *worse than
  none*, so the reword is mandatory in this commit.
  *Alternative rejected:* **retire the field and its notice entirely.** Cleaner in
  the abstract, but it discards a real signal (your export predates your latest
  edits) and deletes a report field other readers may already consume; repurposing
  keeps the signal and the wiring at lower churn. The interpretation change is
  captured in A20 and in the updated test, so the field's meaning is not silently
  shifted.
  *Alternative rejected:* **leave the notice as-is.** The exact defect the `.tji`
  forbids — a coherent export flagged as possibly incoherent.
  **Doc delta: A20** (D-pinned-3).

- **D-pinned-3 — The doc delta is an inline A20 amendment, not a new A-row.**
  A20 already owns the export kernel, the injected renderer, and the very clause
  being reversed (*"coherence is reported, not enforced"*). This leaf adds no
  component, no seam, no dependency and no thread — it threads one more value
  through A20's own injection and reverses one of A20's own clauses. That is the
  textbook case for an inline amendment parenthetical, matching how
  `editor.cameras.contact_sheet` amended A20 in place.
  *Rationale:* the same-commit doc rule (`README.md:47-96`) triggers on a
  **deviation from a stated decision** — A20's "reported, not enforced" — which
  this is, so a delta is required; but the deviation is *within* A20's mechanism,
  so it belongs *in* A20.
  *Alternative rejected:* **a new A-row (A24).** `editor.cameras.contact_sheet`
  earned A21 because it introduced a *materially different* composition mechanism
  (copy-not-filter, an embedded glyph table). This leaf introduces no new
  mechanism — it is A20's kernel rendering against a pin instead of the live
  version — so a new row would fragment one decision across two rows for no future
  refinement to depend on separately.

- **D-pinned-4 — Keep the 2-arg `render_offline` path for non-export renderers.**
  Only the export render entry points move to the 4-arg overload;
  `render_probe_srgb8` (`render.cpp:90`) and the interactive/offline callers keep
  the incumbent overload. A null pin (a runner without a `PinFn`) flows to the
  library's `UnsupportedFormat`, surfaced as a failed item — never a crash.
  *Rationale:* the probe and other callers have no batch and no coherence
  requirement; migrating them would be churn with no benefit and would need a pin
  they have no natural source for. Scoping the change to export keeps the blast
  radius at exactly the seam that needs it.
  *Alternative rejected:* **migrate every `render_offline` call to the pinned
  overload.** Uniform but pointless — it forces a pin on callers that render one
  frame of the current version, which is exactly what the 2-arg overload is for.

## Open questions

`(none — all decided.)`

## Status

**Done** — 2026-07-29.

- Added `render_document_srgb8_pinned` / `_over_pinned` entry points over libarbc #27's 4-arg `render_offline` in `src/render/ace/render/render.hpp` and `src/render/render.cpp`; refactored shared sRGB8 tails.
- `RenderFn` gains a leading `DocStatePtr`; added `PinFn`/`ExportRunner::pin`/`ExportService::set_pin`; `run_export` takes one pin for the whole batch in `src/commands/ace/commands/export.hpp` and `src/commands/export.cpp`.
- L4 binds `set_pin` and forwards the pin into pinned render entry points in `src/app/shell.cpp`; extracted production closure into shared `ace::app::make_export_renderer(AppState&)` factory in new `src/app/ace/app/export_wiring.hpp` and `src/app/export_wiring.cpp`.
- Reworded the `document_changed_during_export` notice to informational (D-pinned-2) with stable `###export_changed_notice` id in `src/views/views.cpp`.
- `docs/01-architecture.md` A20 amended inline: coherence now enforced (not reported), `document_changed_during_export` repurposed to live-vs-pinned note (D-pinned-3).
- Catch2 unit `export: a batch renders one frozen version even under a concurrent edit` (byte-coherence + anti-vacuity re-pin); revised `:534` reporting-field case; pinned-path byte-identity + null-pin assertions added to both goldens in `tests/export_test.cpp`.
- TSan case `canvas_host: a batch export holds ONE pin across a concurrent writer, byte-identical to a quiet batch`; migrated export/sheet anchors to pinned path in `tests/canvas_host_test.cpp`.
- ImGui e2e phase 6b in `tests/export_e2e_test.cpp`: clean batch → no note; concurrent-edit batch → informational note surfaced; retired warning string grep-clean.
- Signature updates in `tests/contact_sheet_test.cpp`, `tests/contact_sheet_e2e_test.cpp`, `tests/export_destination_reseed_e2e_test.cpp`; `render::name()` pinned in `tests/render_probe_test.cpp`.
</content>
</invoke>
