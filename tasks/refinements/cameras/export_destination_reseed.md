# editor.cameras.export_destination_reseed — Re-seed Export destination when service is pointed at a new project

## TaskJuggler entry

- **Task:** `editor.cameras.export_destination_reseed` — *"Re-seed Export
  destination when service is pointed at a new project"*
  (`tasks/00-editor.tji:398-403`), under `task cameras` (`tasks/00-editor.tji:306`).
- **Effort:** `0.5d`.
- **Depends:** `!contact_sheet` — `editor.cameras.contact_sheet`, `complete 100`
  (`tasks/00-editor.tji:384-390`).
- **Note (`.tji:402`):** *"The Export panel seeds its destination path (exports/
  under the project directory) once when the ExportService is first created; on
  project reopen the panel retains the previous project's destination because the
  panel keyed its reset on the service pointer address, not a stable identity.
  Re-seed the destination whenever the service instance id changes, so the path
  always reflects the live project. Source-of-debt:
  tasks/refinements/editor/contact_sheet.md (fixer follow-up). Design:
  docs/01-architecture.md A20."*
- **Back-link:** this document, `tasks/refinements/cameras/export_destination_reseed.md`;
  the closer appends `Refinement:
  tasks/refinements/cameras/export_destination_reseed.md` to the `.tji` note.
  *(Layout note for the closer: the camera area is split three ways across
  `tasks/refinements/editor/`, `tasks/refinements/editor.cameras/` and
  `tasks/refinements/cameras/`. This leaf is filed under `cameras/` as the
  orchestrator directed, alongside its sibling `caption_latin1.md`; normalizing
  the split is out of scope.)*
- **Downstream dependents:** none. This closes a debt; nothing waits on it.
- **Milestone:** `m9_editor` (`tasks/99-milestones.tji:6-8`), reached through the
  `editor.cameras` container dependency — no new milestone wiring is required,
  and this leaf registers no follow-up.

## Effort estimate

**0.5 days**, unchanged from the `.tji`. The scope is narrow and the production
mechanism is already shipped (see **Inherited dependencies** and **Decisions**):
the work is a small amount of dedicated test code plus verification.

- **~0.1d — a Catch2 unit** pinning the identity property the re-seed rests on:
  distinct `ExportService` instances never share an `instance()` id.
- **~0.3d — the ImGui Test Engine e2e** that pins the destination-follows-live-project
  invariant across a service change, built on the shipped `export_e2e_test.cpp`
  harness.
- **~0.1d — verification and the refinement Status block.** `scripts/gate` green;
  confirm no production code change is required (or apply a minimal one only if
  the e2e reveals a gap this analysis did not — see **D-reseed-1**).

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cameras.contact_sheet`** (`tasks/refinements/editor/contact_sheet.md`,
  Done 2026-07-23, commit `016b63e`) — the fixer follow-up's **source of debt**.
  contact_sheet, while unblocking its own multi-session shell-test, converted the
  Export panel's reset key from the service **pointer address** to a monotonic
  **`ExportService::instance()`** counter:
  - `ExportService::instance()` — a never-repeating id handed out once per
    constructed service (`src/commands/ace/commands/export.hpp:312-320,360`),
    backed by a process-wide atomic monotonic counter
    (`src/commands/export.cpp:44-49,385-386`). Its own comment states the reason:
    *"the service ADDRESS cannot answer that: once a service is destroyed, the
    next one is free to land on the same bytes … and a pointer comparison then
    reports 'same panel' while the ticked ids, the destination and the project
    underneath have all changed. A monotonic counter is immune to that reuse."*
  - The panel keys its reset on `service.instance()` — `if (panel.owner !=
    service.instance()) { panel = ExportPanel{}; panel.owner = service.instance();
    }` (`src/views/views.cpp:315-317`, `ExportPanel::owner` at `:45-50`). The full
    `panel = ExportPanel{}` reset clears `destination_seeded` back to `false`
    (`:63`), which is what makes the **destination** (not just the ticks) re-seed on
    the very next frame (`:319-323`).
  This leaf **does not widen** that mechanism; it pins it with the test that
  contact_sheet's own suite did not carry (its e2e asserted contact-sheet
  composition, and the `instance()` change rode along untested for the
  destination invariant specifically — see **Why it needs to be done**).
- **`editor.cameras.export`** (`tasks/refinements/editor.cameras/export.md`,
  Done 2026-07-23, commit `4f1319c`) — every seam under test:
  - The Export panel body `views::draw_export`
    (`src/views/views.cpp:312-323` reset+seed, `:387` `###export_destination`
    InputText, `:405-411` the run gate and `ExportOptions.destination`, `:421-422`
    the dispatch) and its file-static state `g_export_panel` (`:38-65`) —
    **D-export-9**. `ViewType::Export` is a catalog **singleton**, so its panel is
    one process-wide instance that must recognize a new service drawing it.
  - The destination default: `ExportPanel::destination` is seeded from
    `state.layout().exports_dir` (`views.cpp:319-323`) — **D-export-10**, D16's
    *"exports/ under the project directory"*. `ProjectLayout::exports_dir` is
    `<root>/exports` (`src/project/ace/project/project.hpp:83`,
    `src/project/project_open.cpp:162`).
  - The user override rides the shipped `app::FolderDialog` seam
    (`export.hpp:326,331,335`, bound at `src/app/shell.cpp:399-403`) — orthogonal
    to the seed and untouched here.
- **`editor.cameras.model`** — `AppState::layout()`
  (`src/commands/ace/commands/app_state.hpp:78`) is the authoritative,
  per-session project layout the seed reads.

**Pending (owned here):**

- The Catch2 identity unit, the two-session e2e, and the refinement doc. No
  production seam is added; **no doc delta** (see **D-reseed-2**).

## What this task is

The Export panel's destination field must always show the **live** project's
`exports/` directory. It seeds that field once per drawing service, latched by
`ExportPanel::destination_seeded`, and resets the latch whenever a **different**
service draws it. contact_sheet already made that "different service" test key on
the monotonic `ExportService::instance()` id rather than the service's raw
address (`views.cpp:315-317`), which is what makes the reset — and therefore the
destination re-seed — fire correctly when one session's service is torn down and
a new session's service is built in the same process (the multi-session
`ace_shell_test` binary, where the panel is file-static and shared).

This leaf **pins that invariant with a dedicated regression test** and confirms
the mechanism is complete. The bug the `.tji` note names — *"the panel keyed its
reset on the service pointer address, not a stable identity"* — was the state
**before** contact_sheet; contact_sheet's inline fix already applies the note's
prescription (*"Re-seed the destination whenever the service instance id
changes"*). What the fix shipped without is a test that specifically asserts the
destination follows the project across a service change, so the invariant can
silently regress under a future edit (someone reverting `instance()` to a
pointer, making `instance()` constant, or breaking the `destination_seeded`
reset). This task closes that gap.

## Why it needs to be done

- **The debt is registered and unshipped.** contact_sheet's Status
  (`tasks/refinements/editor/contact_sheet.md:772,774,782`) records the inline
  `instance()` fix *and* registers this leaf as the follow-up. Until it lands the
  `editor.cameras` container — and thus `m9_editor` — stays incomplete.
- **The fix shipped without targeted coverage.** No existing test asserts the
  destination re-seed. `tests/export_e2e_test.cpp` and
  `tests/contact_sheet_e2e_test.cpp` each build a **single** session and assert
  writes land in **that** session's `exports_dir`
  (`export_e2e_test.cpp:327-334,421`; `contact_sheet_e2e_test.cpp:305,399`); the
  cross-session re-seed is exercised only **implicitly** (each case passes because
  the file-static panel happens to reset between cases) and is asserted **nowhere**.
  A revert of the `instance()`-keying would not deterministically fail any current
  test — the pointer-reuse false-match depends on the allocator handing a new
  service the freed bytes of the old one, which is nondeterministic. That is
  exactly the silent-regression hole a targeted test closes.
- **The invariant is load-bearing for a plausible future.** Today a project change
  is a detached sibling `exec` — *"this process's one Document is never swapped"*
  (`src/app/ace/app/project_gateway.hpp:21-33`), and `save_project_as` *"never
  re-points `layout_`"* (`src/commands/ace/commands/app_state.hpp:284-285`) — so
  the real app's service↔project mapping is 1:1 per process and `instance()` is a
  faithful project proxy. The multi-session **test** binary is the one place a
  stable-static panel meets more than one project, and it is the place the bug
  first bit. Pinning the invariant there protects it before any in-process reopen
  ever ships.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **`docs/01-architecture.md` A20** (`:382`) — the export pipeline's structure.
  Load-bearing here: *"The destination defaults to `ProjectLayout::exports_dir`
  and is overridden through the **shipped** `app::FolderDialog` folder seam (A12)
  injected as a callable … the user picks a directory, never a filename."* The
  destination **default** is A20's; the re-seed's job is to keep that default
  pointing at the **live** project. A20 says nothing about the panel's reset
  mechanism (it is below A20's granularity), so **no A-row amendment is needed**
  (**D-reseed-2**).
- **`docs/00-design.md` D16** — export destination defaults to `exports/` under
  the project directory. The invariant under test is literally "D16, but for the
  right project after a service change."
- **`docs/00-design.md` D23** — refuse rather than guess. The panel already
  refuses an empty destination in its run gate (`views.cpp:405-407`); re-seeding
  keeps a **correct** non-empty default present rather than leaving a stale one.

**Editor seams under test (no production change expected — see D-reseed-1):**

- `src/views/views.cpp:38-65` — `ExportPanel` (file-static `g_export_panel`,
  `owner`, `destination`, `destination_seeded`) and its comment
  (*"reset whenever the panel is drawn against a different `ExportService`"*).
- `src/views/views.cpp:312-323` — `draw_export`'s reset (`panel = ExportPanel{}`
  keyed on `panel.owner != service.instance()`) and the `!destination_seeded`
  seed from `state.layout().exports_dir`.
- `src/commands/ace/commands/export.hpp:312-320,360` — `instance()` and its
  rationale comment; `:304-307` the ctor.
- `src/commands/export.cpp:44-49,385-386` — `next_instance()` (process-wide
  atomic monotonic counter, starts at 1, never reused) and the ctor init
  `instance_(next_instance())`.
- `src/commands/ace/commands/app_state.hpp:78` — `AppState::layout()`;
  `:284-285` — `save_project_as` never re-points `layout_` (the 1:1 evidence).
- `src/app/ace/app/project_gateway.hpp:21-33` — the sibling-`exec` reopen model
  (the Document is never swapped in-process).
- `src/app/shell.cpp:384-403` — the one bootstrap wiring of the process's single
  `ExportService` and its destination picker.

**Test rigs this leaf builds on:**

- `tests/export_e2e_test.cpp` — the real-`AppProjectGateway` + real `CanvasView` +
  `ScratchDir` + `WriterSession` harness (`:79-94` `ScratchDir`, `:197-208`
  `E2EState`, `:210-220` `pump_until`, `:241-336` session setup and
  `IM_REGISTER_TEST(engine, "cameras", "export_panel")`, `:352-360` `run_and_wait`,
  `:527-550` the destination-override phase). The new e2e reuses this harness and
  leaves `export_panel` untouched.
- `tests/export_test.cpp` — the headless Catch2 home for the identity unit
  (`options.destination` fixtures at `:263,345`), added to `ace_tests`.
- `CMakeLists.txt:270-291` — `ace_shell_test` sources (the new e2e joins beside
  `tests/export_e2e_test.cpp` at `:290`); `:252` region — `ace_tests` sources for
  the Catch2 unit.

**Predecessor refinements:**

- `tasks/refinements/editor/contact_sheet.md` — the source of debt; its Status
  (`:772,774,782`) records the inline `instance()` fix and this follow-up.
- `tasks/refinements/editor.cameras/export.md` — **D-export-9** (the Export panel
  is stateful, not a one-shot op) and **D-export-10** (destination defaults to
  `exports_dir`, override via the folder seam).

## Constraints / requirements

1. **The destination reflects the live project after a service change.** When a
   new `ExportService` (a new `instance()`) draws the file-static Export panel,
   the panel's `destination` re-seeds from **that** service's session
   `state.layout().exports_dir` — never the previous session's. This is the
   invariant under test; it must be **asserted**, not merely exercised.
2. **The identity the re-seed keys on is stable and unique.** Two distinct
   `ExportService` objects — even one built on a destroyed one's address — never
   report the same `instance()`. Pinned by a headless unit so a regression to a
   constant/reused id fails deterministically, independent of the allocator.
3. **No production behavior change unless the test reveals a gap.** This analysis
   concludes the mechanism (`views.cpp:315-323`) is already correct and complete
   (**D-reseed-1**). If the e2e nonetheless fails, the fix is the **minimal** edit
   to `draw_export`'s reset/seed — not a redesign, and not a new key: the `.tji`
   note endorses `instance()` as the identity.
4. **The seed source stays `state.layout().exports_dir`.** No new destination
   policy, no clock, no filesystem probe — the seed is D16/D-export-10's default,
   read from the live `AppState` passed to `draw_export`.
5. **The user's override is not clobbered mid-session.** Within one service's
   life the panel seeds **once** (`destination_seeded` latch); a re-seed happens
   only on a service change. A test must not assert re-seeding while the same
   service keeps drawing — that would be the wrong invariant and would fight the
   folder-picker override (`views.cpp:393-395`, D-export-10).
6. **Errors stay values.** Nothing in the tested path throws or asserts on
   session boundaries; the panel is a pure UI-thread reader of `layout()`.
7. **No new component, no new DAG edge, no new external, no new thread.**
   `scripts/check_levels.py` is unmodified; `src/base/**`, `src/render/**`,
   `src/gl/**`, `src/dock/**`, `src/commands/**` and (per D-reseed-1)
   `src/views/**` and `src/app/**` production sources are byte-unchanged. The work
   is test-only.
8. **The catalogued Export view is untouched.** No new view, rail item, widget, or
   `ProjectGateway` virtual — the panel already has everything under test.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the umbrella.

**Levelization (`check_levels` clean) — the structural assertion:**

- `scripts/check_levels.py` is **unmodified**; no component, edge, or external is
  added. Test files only; production sources byte-unchanged (**Constraint 7**).

**L1 logic — Catch2 unit:** added to `tests/export_test.cpp` (in the `ace_tests`
list, `CMakeLists.txt:252` region), reusing its fake-filesystem/stub-renderer
scaffolding. Case `TEST_CASE("export: distinct services carry distinct
instance ids")`:

- Construct two `ExportService` objects; assert `a.instance() != b.instance()`
  and both are non-zero (`export.hpp:320`, `export.cpp:46-49`). **Anti-vacuity /
  address-reuse guard:** construct a service on the heap, record its
  `instance()`, destroy it, construct a second service on the heap, and assert the
  second `instance()` **differs from the first even if the two objects share an
  address** (assert the addresses matched where they do, and the ids still
  differ) — the exact reuse the pointer key fell to. This pins the property the
  whole re-seed rests on (**Constraint 2**), deterministically and headlessly.

**UI e2e — ImGui Test Engine (the primary regression):** new
**`tests/export_destination_reseed_e2e_test.cpp`**, added to the `ace_shell_test`
source list beside `tests/export_e2e_test.cpp` (`CMakeLists.txt:290`), registered
**`IM_REGISTER_TEST(engine, "cameras", "export_destination_reseed")`**, built on
the `export_e2e_test.cpp` harness (real `AppProjectGateway` + real `CanvasView`,
`WriterSession`, `NoopFolderDialog`, `pump_until`/`run_and_wait`), asserting on
**model state and on-disk files, never on pixels**, and leaving the shipped
`export_panel` and `contact_sheet_panel` tests untouched. Two projects, one
process:

1. **Baseline (single session).** Build session **A** over `ScratchDir` A, open
   the Export view, mint a camera, tick it, and run **without** overriding the
   destination → the report's item path is under `A/exports`
   (`state.layout().exports_dir`), matching the seeded default. Confirms the seed
   is A's, not empty, not the process CWD.
2. **The re-seed across a service change (the pin).** Build session **B** over a
   **second** `ScratchDir` B with its **own** `ExportService` (new `instance()`),
   register the Export body to draw B's service+state, and pump one frame so the
   file-static `g_export_panel` is redrawn by B's service. Then mint+tick a camera
   and run **without** overriding the destination → the report's item path is
   under `B/exports`, and **no** file is written under `A/exports`. **This is the
   law:** if the panel had retained A's destination (the bug), B's export would
   target `A/exports`; asserting the file lands in `B/exports` and **not**
   `A/exports` fails deterministically on any re-seed regression.
3. **Override survives within a session (Constraint 5 anti-vacuity).** Still in
   session B, override the destination via the `###export_destination` field to
   `B/elsewhere`, run again → the file lands in `B/elsewhere`, proving the re-seed
   fired **once** on the service change and did **not** clobber the user's
   in-session override on subsequent frames.
4. **The document is untouched.** `scene::cameras()` counts and each camera's
   `resolution`/`frame` are unchanged by drawing or exporting across both sessions
   (D15, pure reader).

*(Harness note for the implementer: the two sessions share the file-static
`g_export_panel` by construction — that sharing is the mechanism under test.
Re-register `ViewType::Export`'s body between sessions to point `draw_export` at
the second service+state, exactly as the shell would after a new bootstrap, and
pump a frame before asserting. The `instance()` values differ by construction, so
the reset fires; the test proves the destination follows it.)*

**Rendered output — golden:** none. This leaf renders nothing new; the export
path and its `render_offline` golden are the predecessor's
(`tests/goldens/export_camera_64x64.{rgba8,png}`), reached unchanged.

**Threading (ASan/TSan) — explicitly scoped:** this leaf adds **no new thread**
and no new shared state. The re-seed is a UI-thread-only read of `layout()` and a
write to the file-static, UI-thread-only `g_export_panel` (`views.cpp:33,38-43`);
no worker touches it. The new e2e runs in the existing offscreen software-GL
ASan lane (§9.1) with **no new `tests/lsan.supp` suppression**. No new TSan anchor
is warranted — the predecessor's export TSan anchor (`tests/canvas_host_test.cpp`,
extended by contact_sheet) already covers the job's threading, which this leaf
does not touch.

**Coverage:**

- ≥90% diff coverage on changed lines (`diff-cover --fail-under=90`);
  clang-format and build clean. Because the changed lines are almost entirely
  test code, the diff-coverage ratio is dominated by the tests exercising
  themselves; any residual production line (only if D-reseed-1's minimal-fix
  branch is taken) is covered by phase 2 of the e2e.

**Doc delta (same commit):**

- **None.** A20 already states the destination defaults to
  `ProjectLayout::exports_dir`; the `instance()`-keyed re-seed is an
  implementation detail below A20's granularity, and the invariant "the default
  reflects the live project" is a direct consequence of A20 + D16 + D-export-10,
  not a new decision (**D-reseed-2**). contact_sheet's A20 amendment and A21
  already record the `ExportService` changes of that commit; this leaf changes no
  decided behavior.

**Deferred WBS work (closer registers in the WBS):**

- **None.** This leaf closes the debt registered by
  `tasks/refinements/editor/contact_sheet.md` and spawns no follow-up. (One
  human-judgement item is surfaced to the parking lot in the return summary, not
  the WBS.)

## Decisions

- **D-reseed-1 — The production mechanism is already correct; this leaf's
  deliverable is the regression test that pins it, not a re-fix.**
  contact_sheet (`016b63e`) already keys the panel reset on the monotonic
  `ExportService::instance()` (`views.cpp:315-317`), and the full `panel =
  ExportPanel{}` reset clears `destination_seeded`, so the destination re-seeds on
  the next frame from the new session's `state.layout().exports_dir`
  (`views.cpp:319-323`). `next_instance()` is a process-wide atomic monotonic
  counter, never reused (`export.cpp:44-49`), so it is immune to the address-reuse
  false-match that the pointer key fell to (`export.hpp:314-319`). The `.tji`
  note's prescription — *"Re-seed the destination whenever the service instance id
  changes"* — is therefore already satisfied in code; what was never written is a
  test that **asserts** it.
  *Rationale:* the note describes the **pre-**contact_sheet state (git confirms
  `4f1319c` used a pointer comparison, replaced by `016b63e` at `views.cpp:315,317`).
  A refinement must reflect the code as it is: turning this leaf into a redundant
  re-application of a shipped fix would be busywork, while the real, unfilled gap
  is coverage. The task remains a genuine, closable WBS leaf because "add the
  regression test that would have caught this class of bug" is concrete,
  agent-implementable work with a deliverable that fails before and passes after.
  *Guard:* the e2e is written to **fail** if the mechanism is broken (destination
  leaks across a service change). Should it fail against the current tree —
  contradicting this analysis — the fix is the minimal edit to `draw_export`'s
  reset/seed, keeping the `instance()` key. Either way the leaf ships a test that
  pins the invariant.
  *Alternative rejected:* **re-key the re-seed on a project/layout identity
  instead of `instance()`.** More "principled" in the abstract (the destination
  tracks the *project*, not the *service object*), but the shipped app makes
  service↔project 1:1 per process (`project_gateway.hpp:21-33`,
  `app_state.hpp:284-285`), so `instance()` is a faithful proxy today; the `.tji`
  note explicitly endorses `instance()`; and introducing a second identity would
  add a seam (a project id on `AppState` or a comparison the panel does not have)
  for a distinction no shipping code can observe. Reuse the existing seam (§ bias).
  **No production change (test-only). No doc delta.**

- **D-reseed-2 — No doc delta.**
  A20 governs the destination default (`ProjectLayout::exports_dir`) but says
  nothing about the panel's reset mechanism; the re-seed is below its granularity,
  and the invariant "the default reflects the live project" already follows from
  A20 + D16 + D-export-10. contact_sheet's commit already carried the A20
  amendment and A21 for the `ExportService` changes it made. This leaf changes no
  decided behavior and adds no seam, so it amends nothing.
  *Rationale:* the same-commit doc rule (`README.md:47-96`) triggers on a change
  to **decided behavior** or a **new seam/dependency/deviation**; a
  behavior-preserving regression test is none of those.
  *Alternative rejected:* **add a one-clause A20 amendment noting the
  `instance()`-keyed re-seed.** Over-documentation — it would pull an
  implementation detail up into the constitution for no future refinement to
  depend on.

- **D-reseed-3 — The instrument split: a headless Catch2 unit for the identity
  property, an ImGui e2e for its consumption.**
  The re-seed decision lives inside `draw_export` (L3 `views`, requires a live
  ImGui frame), so it is not headless-unit-testable without extracting a
  three-line `!=` branch into L1 — which would be altitude-wrong for a bare
  comparison and would invent an L1 seam with one caller. The correct instrument
  for the **consumption** (does the panel actually re-seed?) is the ImGui Test
  Engine e2e. The **identity property** the consumption relies on (`instance()` is
  unique and reuse-immune) *is* headless L1-adjacent logic and gets a cheap Catch2
  unit, which pins the fix's foundation deterministically without depending on the
  allocator reproducing an address collision.
  *Rationale:* §9 assigns L1 logic to Catch2 and UI behavior to the Test Engine;
  this split honors both without manufacturing an L1 component for a `!=`.
  *Alternative rejected:* **e2e only.** Cheaper to write, but a pure e2e cannot
  deterministically reproduce the address-reuse that motivated `instance()`, so it
  under-protects the exact property that failed; the unit closes that.
  *Alternative rejected:* **extract `should_reseed(uint64, uint64)` to L1 for a
  unit test.** A new seam with one call site to test a comparison — the
  simpler-abstraction bias rejects it.

## Open questions

`(none — all decided.)`

One item is surfaced to the orchestrator for `tasks/parking-lot.md` rather than
the WBS, because it is a human/architectural judgement and not
agent-implementable behavior work: **if the editor ever grows an in-process
project reopen** (swapping the `Document`/`AppState` under a surviving
`ExportService` instead of the current detached-sibling-`exec` model,
`project_gateway.hpp:21-33`), the destination re-seed would need to key on a
project/layout identity rather than the service `instance()` — because a reused
service would keep its id across the swap (**D-reseed-1**'s rejected alternative,
which is correct *only under that future*). That is a design call contingent on an
architecture decision no leaf owns today; **D-reseed-1** makes the defensible call
for the shipping model, and this note records the trigger that would reopen it.

## Status

**Done** — 2026-07-24.

- `tests/export_destination_reseed_e2e_test.cpp` (new) — ImGui Test Engine e2e
  `cameras/export_destination_reseed`: two `ScratchDir` projects, one shared
  file-static `g_export_panel`; asserts baseline export lands in `A/exports`,
  then that a service change re-seeds so `B/exports` is targeted (and nothing
  lands in `A/exports`), then that an in-session override to `B/elsewhere`
  survives subsequent frames (**Constraint 5** anti-vacuity).
- `tests/export_test.cpp` — Catch2 unit `export: distinct services carry
  distinct instance ids`: constructs two `ExportService` objects (one on the
  heap, destroy, construct second), asserts `instance()` values differ even when
  the heap address is reused (**Constraint 2**, deterministic address-reuse
  guard).
- `CMakeLists.txt` — registered the new e2e in the `ace_shell_test` source list
  (beside `tests/export_e2e_test.cpp`); the unit is in the existing `ace_tests`
  list.
- No production source was modified (**D-reseed-1** confirmed: the
  `instance()`-keyed reset at `views.cpp:315-317` was already correct; this leaf
  added the regression coverage that pins it).
- No doc delta (**D-reseed-2**: A20 + D16 + D-export-10 already cover the
  invariant; the `instance()` re-seed is below A20's granularity).
- Parking-lot entry appended for the in-process project reopen architectural
  question (see `tasks/parking-lot.md`).
</content>
</invoke>
