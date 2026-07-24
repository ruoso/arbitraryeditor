# editor.project.reopen_degradation_notice — Surface unbindable-content count as a one-shot open notice

## TaskJuggler entry

- **Task:** `editor.project.reopen_degradation_notice`
  (`tasks/00-editor.tji:168-173`, under `task project "Project & app state"` at `:93`).
- **Effort:** `0.5d` · `allocate team`.
- **Depends:** `editor.cameras.reopen_slab_adopt` (`.tji:171`;
  `tasks/refinements/cameras/reopen_slab_adopt.md`, **Done** 2026-07-23) — the leaf
  that produced the value this task surfaces.
- **Note (`.tji:172`):** "Read `OpenedProject::unbindable_content_records` through the
  existing `dock::ProjectGateway` POD seam and show a one-shot notice on open naming
  how many objects the unsaved workspace could not recover, with an ImGui Test Engine
  e2e driving it headless by widget id. Source-of-debt:
  `tasks/refinements/cameras/reopen_slab_adopt.md`. Design: `docs/01-architecture.md`
  A19."
- **Milestone:** `m9_editor` (`tasks/99-milestones.tji:6-8`) via its `editor.project`
  container dependency — no separate wiring needed.
- **Back-link:** the closer appends `complete 100` after `allocate team` and ends the
  `.tji` note with `Refinement: tasks/refinements/editor/reopen_degradation_notice.md`
  (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji` here.

## Effort estimate

**0.5 day**, as budgeted in the `.tji`. Roughly: 0.1d to carry the count onto
`commands::AppState` and expose it through one new `dock::ProjectGateway` virtual
(both are one-liner mirrors of the shipped `rebuilt_from_canonical` / `is_dirty`
pattern); 0.2d for the one-shot notice modal on `Dockspace` and its draw routine
(a trimmed copy of `draw_gc_modal`); 0.2d for the three test lanes (L1 carry unit,
L4 gateway unit, ImGui Test Engine e2e) and the D25 doc delta. **No new component,
no new DAG edge, no new external dependency, no libarbc fork, no pin bump.**

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cameras.reopen_slab_adopt`** (`tasks/refinements/cameras/reopen_slab_adopt.md`,
  **Done** 2026-07-23) — shipped the value this task consumes:
  `project::OpenedProject::unbindable_content_records` (`src/project/ace/project/project.hpp:141`,
  set on the kept-map path at `src/project/project_open.cpp:225`) and the **A19** row
  (`docs/01-architecture.md:381`), which settles that the workspace-map reopen restores
  the record graph only and binds no `Content` for any kind. Its own Acceptance criteria
  explicitly deferred this UI surface: *"deferred to `editor.project.reopen_degradation_notice`
  … read `OpenedProject`'s unbindable-content count through the existing `dock::ProjectGateway`
  POD seam and show a one-shot notice on open."* The count is fully pinned at L1 already —
  `project_open_test.cpp:375` asserts it is **1** on the never-saved lossy path and
  `:320`/`:355` assert **0** on the rebuild and content-free paths. **Unchanged here.**
- **`editor.project.app_state`** — `commands::AppState` (`src/commands/ace/commands/app_state.hpp:66`)
  holds the one in-process session (A7) and already carries `opened.rebuilt_from_canonical`
  onto a member (`app_state.cpp:53`, accessor `app_state.hpp:107`, field `:152`). This task
  adds a **sibling** scalar field beside it. **Extended, minimally.**
- **`editor.project.exec_new` / `editor.project.open_ui`** — the `dock::ProjectGateway`
  abstract seam (`src/dock/ace/dock/dock.hpp:80-228`, A12/A13/D22) and its L4 impl
  `AppProjectGateway` (`src/app/ace/app/project_gateway.hpp:34`). The rail's open verbs
  spawn siblings (D19/A7); the **session-query** verbs (`is_dirty`, `can_undo`, `clean_up`)
  act on this process's own `AppState`. This task adds one more session-query verb to that
  family. **Extended, minimally.**
- **`editor.project.gc`** — established the **one-shot confirm-modal** shape this notice
  copies: `Dockspace::open_gc_modal(GcSummary)` + `gc_modal_open()` + `close_gc_modal()`
  (`dock.hpp:317-323`) and `draw_gc_modal` with stable `###`-id buttons (`dock.cpp:93-117`),
  driven headless by `tests/gc_ui_e2e_test.cpp`. **Pattern reused, not modified.**

**Pending (owned here):** the `AppState` count field + accessor; the one new
`ProjectGateway` virtual and its L4 override; the `Dockspace` notice state + draw
routine; the three test lanes; the **D25** doc delta. No downstream leaf is blocked.

## What this task is

The predecessor `reopen_slab_adopt` established that the editor can produce exactly one
**lossy** reopen: a project that was **never saved** (so it has no canonical `project.arbc`
to rebuild from) but whose crash-durable `workspace/` held cells or cameras. At the pinned
libarbc v0.3.0 that project reopens **successfully but empty** — `arbc::Document::open` runs
no factory, so it restores the layer/record graph and binds no `Content` for any kind
(A19). The count of records the map could not bind rides back on
`OpenedProject::unbindable_content_records`, non-zero only on that fallback. Today nothing
reads it: an emptied never-saved project presents to the user as *"my work was never
saved,"* silently.

This task **surfaces that count as a one-shot notice on open** so the loss is announced
rather than silent. Concretely, four small pieces:

1. **Carry** the count from the bootstrap `OpenedProject` onto the in-process
   `commands::AppState` — a scalar field mirroring `rebuilt_from_canonical_`. The dock
   open verbs spawn *sibling* processes and never touch this value; the count is a
   **bootstrap-time** fact of *this* process's own session (`open_or_create_app_state`,
   `app_state.cpp:154`), so `AppState` is the only place it can live for the UI to read.
2. **Expose** it through **one** new `dock::ProjectGateway` virtual —
   `reopen_unbindable_count() const -> std::size_t` — in the A13 session-query family
   alongside `is_dirty()`. The L4 `AppProjectGateway` (which holds the `AppState`) returns
   the carried count; every other gateway impl inherits an inert `0` default.
3. **Present** it as a one-shot, dismissible modal on `Dockspace`, drawn like the Clean up
   confirm but with a single **Dismiss**, naming N objects, shown at most once per session.
4. **Record** the new user-visible behavior as **D25** (`docs/00-design.md`), the same-commit
   doc delta.

## Why it needs to be done

**A19 promises the loss is announced; nothing announces it yet.** A19 closes with *"…so
the loss is announced rather than silent"* and the predecessor's Open-questions named the
never-saved residual as *"now reported rather than silent"* only at the L1 value level — the
count exists but has no consumer. D16 tells the user `workspace/` makes a project
"crash-durable by default"; when that promise cannot be kept for content (A19), a silent
empty project is the worst possible presentation of the failure — it reads as data the user
never created, not data the tool could not recover. Converting the already-computed count
into a visible, honest notice is the whole point of having computed it.

**It is the last open piece of the reopen-degradation thread.** `reopen_slab_adopt` did the
hard part — verified the binding gap, corrected the guard, and produced the count. This leaf
is the small, agent-implementable UI cap that the predecessor deliberately split off (a UI
widget belongs in an e2e-tested `views`/`dock` leaf, not in a headless L1 open-path change).

## Inputs / context

**Governing design docs (normative — the constitution):**

- **A19 — the workspace-map reopen restores the record graph only; binds no `Content`**
  (`docs/01-architecture.md:381`). The `.tji`-named governing row. Establishes the value
  (`OpenedProject` "reports the count of content records the map could not bind — a value,
  not an error — so the loss is announced rather than silent") and that it is non-zero only
  on the canonical-absent fallback. This leaf is the "announced" half.
- **A12 / A13 — the `dock::ProjectGateway` seam** (`docs/01-architecture.md:374-375`). A12
  declares the abstract gateway `dock` owns with its L4 `app` impl; A13 adds the **in-session
  Save + dirty query** (`is_dirty()`, `save()`) as session-state verbs on that *same* seam,
  reading `AppState` — the exact family the new `reopen_unbindable_count()` joins. No new A-row
  is needed: this is a method on an existing seam, not a new architectural seam.
- **§8 levelization** (`docs/01-architecture.md:256-291`) and `scripts/check_levels.py:21-53`:
  `dock` is **L3** and may include neither `ace/commands` nor `ace/scene`; a `std::size_t`
  crosses the seam as a primitive with **no new include edge** (the `can_delete()`/
  `delete_selected()` precedent, D-cells_remove-6). `app` is **L4** and already includes both
  `dock` and `commands`. **§9 DoD** (`:293-357`).
- **D16** (`docs/00-design.md:483`) — `workspace/` makes the project "crash-durable by
  default"; the promise A19 cannot keep for content, and the reason the degradation deserves a
  visible signal.
- **D25 (new, this leaf)** — the reopen-degradation-notice UX row; see Decisions
  D-reopen_degradation_notice-4.

**Editor seams this leaf extends (real paths + line numbers):**

- `src/project/ace/project/project.hpp:124-142` — `OpenedProject`, whose
  `unbindable_content_records` (`:141`) is the value read at bootstrap. **Unchanged**;
  consumed only.
- `src/commands/app_state.cpp:51-81` — the `AppState` constructor. Its initializer list
  (`:52-54`) copies `opened.rebuilt_from_canonical` into `rebuilt_from_canonical_` but drops
  `unbindable_content_records`; this leaf adds the sibling copy. `:70-76` shows the sibling
  scalar `rebuilt_from_canonical_` in use.
- `src/commands/ace/commands/app_state.hpp:104-107` (the `rebuilt_from_canonical()` accessor
  to mirror), `:152` (the `rebuilt_from_canonical_` field to sit beside). Note `saved_revision_`
  (`:154`) is **atomic** because it mutates on the writer thread; the new count is
  **immutable after construction**, so it is a plain `std::size_t` — no atomic (Constraint 5).
- `src/commands/app_state.cpp:147-165` — `open_or_create_app_state`, the bootstrap that
  constructs `AppState(std::move(*opened))` at `:158`, where the count is currently lost.
- `src/dock/ace/dock/dock.hpp:80-228` — `ProjectGateway`; the new virtual joins the
  session-query verbs (`is_dirty()` `:115`, `can_undo()` `:145`). Inert-default idiom to copy:
  `can_delete()` (`:187`), `insert_kinds()` (`:157`). `Dockspace` state to extend near the GC
  modal state (`:317-323`) and the `draw()` fan-out (`:253`, `:263`).
- `src/dock/dock.cpp:86-117` — `draw_gc_modal`, the exact one-shot-modal precedent (OpenPopup
  guard `:95-96`, `BeginPopupModal` `:100`, count text `:101-104`, `###`-id buttons `:105-114`)
  the notice modal is trimmed from. `draw_project_section` (`:243-325`) is where the rail's
  per-frame draw already renders open feedback (`project_feedback_`, `:320-322`).
- `src/app/ace/app/project_gateway.hpp:34-53` and `src/app/project_gateway.cpp` — the L4
  `AppProjectGateway` that holds `app_state_` (`:152`); the new override returns
  `app_state_.unbindable_content_records()`, alongside its existing `is_dirty()` override.
- `src/app/shell.cpp:215-226,296,363,412-414` — bootstrap: `open_or_create_app_state` posted
  to the writer thread (`:215-217`), `AppState& app_state` bound (`:226`), `Dockspace`
  constructed (`:296`), gateway wired via `set_project_gateway` (`:363`), and the per-frame
  `dockspace.draw()` installed (`:412-414`) — the frame loop where the notice first fires.

**Test seams:** `tests/commands_test.cpp` (L1 `commands`/`AppState` units),
`tests/app_project_gateway_test.cpp` (L4 gateway units, over a real `ScratchDir`),
`tests/gc_ui_e2e_test.cpp` (the ImGui Test Engine modal-driving pattern to mirror:
`FakeGateway : ProjectGateway`, `set_project_gateway`, `IM_REGISTER_TEST`,
`ctx->ItemClick("…/###id")`, `ctx->ItemExists`, `ctx->Yield`), `tests/project_open_test.cpp:320,355,375`
(the L1 witnesses that the count is 0/0/1 across paths).

**Predecessor refinements:** `tasks/refinements/cameras/reopen_slab_adopt.md`,
`tasks/refinements/editor/gc.md`, `tasks/refinements/editor/open_ui.md`.

## Constraints / requirements

1. **The count is carried, not recomputed.** `AppState` gains one `std::size_t
   unbindable_content_records_` field, copied from `opened.unbindable_content_records` in the
   constructor initializer list beside `rebuilt_from_canonical_` (`app_state.cpp:52-54`), with
   an accessor `unbindable_content_records() const` beside `rebuilt_from_canonical()`
   (`app_state.hpp:107`). No open-path logic changes; the value is produced by the predecessor
   and only ferried.

2. **One new `ProjectGateway` virtual, a primitive, no new POD, no new edge.**
   `virtual std::size_t reopen_unbindable_count() const { return 0; }` on `dock::ProjectGateway`,
   in the A13 session-query family with an **inert `0` default** (so the gateway fakes of
   unrelated suites need no churn — the `can_delete`/`insert_kinds` idiom). A single
   `std::size_t` needs no dock-local POD (D-cells_remove-6) and adds no `dock → commands`
   include. `AppProjectGateway` overrides it to return `app_state_.unbindable_content_records()`.
   The `.tji`'s "POD seam" phrasing is honored by *reusing* the existing seam; the payload is a
   scalar, matching the leanest verbs already on it (Decision D-reopen_degradation_notice-2).

3. **One-shot, dismissible, only when N > 0.** The notice is shown at most once per session,
   dismissed by a single **Dismiss** button, and never opened when the count is zero (a clean
   map reopen, a rebuild-from-canonical, or a fresh `create_project` — all report 0). The
   one-shot latch is **UI-ephemeral state on `Dockspace`** (a `bool` seen-flag), *not* on the
   gateway or `AppState` (which hold the durable session fact) — the same separation the GC
   modal keeps between `gc_modal_open_` (dock state) and the session's real GC data. The count
   text names N ("This project's unsaved workspace held N object(s) that could not be
   recovered.").

4. **Modal shape mirrors `draw_gc_modal`, with stable `###` widget ids for the e2e.** A
   centered `BeginPopupModal` drawn every frame from the rail so the popup stays balanced,
   opened once via an `OpenPopup` guard when the seen-flag is unset and the count is positive,
   with a `Dismiss###reopen_notice_dismiss` button that closes it and sets the seen-flag. The
   popup id and button id are slash-free `###` ids so the e2e drives them by stable widget id
   (Constraint mirrors `dock.cpp:92,105,111`).

5. **The carried count is immutable after construction — a plain scalar, no atomic.** Unlike
   `saved_revision_` (writer-thread-mutated, hence atomic), the unbindable count is set once in
   the `AppState` constructor and never written again; the UI thread reads it. A plain
   `std::size_t` with no synchronization is correct and is what the ASan/TSan lanes must
   confirm (no new concurrency seam).

6. **Levelization — no new component, no new edge, no new dependency, no pin bump.** The change
   touches L1 `commands` (a field), L3 `dock` (a virtual returning a primitive + `Dockspace`
   UI state), and L4 `app` (an override the impl already has the `AppState` to serve).
   `grep -rn "ace/commands" src/dock/` stays empty; `check_levels` stays clean with **no
   `scripts/check_levels.py` edit**; `CMakeLists.txt:25` stays at `v0.3.0`.

7. **Doc delta (D25) rides the same commit.** The notice is a new user-visible affordance with
   no covering `docs/00-design.md` row; A19 records the *value* and the architecture, but the
   *UX* (a one-shot dismissible modal naming the count) belongs in the D-series. **D25** lands
   in the same commit (`tasks/refinements/README.md:49`).

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate` green
(check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean).** No new component, no new DAG edge, no
  `scripts/check_levels.py` edit; `grep -rn "ace/commands\|ace/scene" src/dock/` empty;
  `CMakeLists.txt:25` unchanged at `v0.3.0` (Constraint 6).

- **L1 logic — Catch2 unit (the carry), in `tests/commands_test.cpp`.** Construct an
  `AppState` from an `OpenedProject{unbindable_content_records = N}` and assert
  `state.unbindable_content_records() == N`; assert it is **0** for an `OpenedProject` with
  `rebuilt_from_canonical = true` and for one left default (the `create_project` shape). This
  pins the ferry and that a clean/rebuilt/created session never reports a phantom loss. (The
  underlying count values 0/0/1 across open paths are already pinned upstream at
  `tests/project_open_test.cpp:320,355,375` — not re-asserted here.)

- **L4 gateway unit — headless, extending `tests/app_project_gateway_test.cpp`.**
  `AppProjectGateway::reopen_unbindable_count()` over a real `AppState`: returns the session's
  carried count (assert the value round-trips through the gateway seam), and the inert base-class
  default returns `0` for an unwired/fake gateway. This is the L3→L4 inversion asserted headless
  (A12/A13), mirroring the leaf's `is_dirty()` coverage.

- **UI e2e — ImGui Test Engine (the one-shot notice), in
  `tests/reopen_degradation_notice_e2e_test.cpp` joined to `ace_shell_test`.** Inject a **fake
  `ProjectGateway`** overriding `reopen_unbindable_count()` to return a scripted `N > 0`
  (mirroring `tests/gc_ui_e2e_test.cpp`'s `FakeGateway` + `set_project_gateway`): drive one
  frame, assert the notice modal is shown (`ctx->ItemExists("<popup>/###reopen_notice_dismiss")`),
  click **###reopen_notice_dismiss**, `Yield`, and assert it is **gone**; then run further
  frames and assert it does **not** reappear (the one-shot latch, Constraint 3). A second run
  with a fake reporting `reopen_unbindable_count() == 0` asserts the modal is **never** shown.
  (+ a screenshot baseline of the notice where it adds signal.)

- **Rendered output — golden N/A (justified).** No code path here materializes a `render_offline`
  frame; the change is a scalar carry plus a text modal drawn with ImGui. The committed goldens
  must return **byte-identical** — a mismatch is a finding that blocks the leaf, never a baseline
  to regenerate (`gc.md` / `arbc_v030.md` D-arbc_v030-7 precedent). No new golden.

- **Threading (ASan/TSan).** No new concurrency seam: the count is written once in the
  `AppState` constructor (writer/bootstrap thread) and read on the UI thread, with no interleaving
  (Constraint 5). The new e2e runs in the existing `ace_shell_test` ASan/TSan lanes; that the
  plain (non-atomic) scalar read is race-free is the property those lanes confirm.

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed lines — the carry,
  the accessor, the override, and the modal's open/dismiss/never-reopen branches. Tests ship
  with the task. clang-format + build clean.

## Decisions

- **D-reopen_degradation_notice-1 — Carry the count onto `AppState`, not re-read it at the rail.**
  The dock's open verbs (`open_project`/`open_recent`) spawn *sibling processes* and return a bare
  `bool` — they never see an `OpenedProject`. The count for *this* window's project is produced
  once, at bootstrap, by `project::open_project` and consumed into `AppState`
  (`open_or_create_app_state`, `app_state.cpp:154-158`), where it is currently dropped. So the
  only correct source for the notice is the in-process session, exactly as `is_dirty()` and
  `rebuilt_from_canonical()` already are. Carrying it as a scalar field beside
  `rebuilt_from_canonical_` is a one-line change with an established pattern.
  *Alternative rejected — seed the count directly onto `Dockspace` from the shell at bootstrap
  (shell.cpp:296/363 has `app_state` in scope).* It works, but it bypasses the `ProjectGateway`
  seam the `.tji` names and the A12/A13 inversion every other session query already uses,
  scattering session-state reads across two mechanisms. Routing through the gateway keeps one
  seam and one testable inversion.

- **D-reopen_degradation_notice-2 — One `std::size_t` gateway virtual, no dock-local POD.** A19's
  degradation is a single count; the seam already carries lone primitives for
  `can_delete()`/`delete_selected()`/`frame_selection()` precisely because "two primitives carry
  everything the rail and the e2e need, so this needs not even a dock-local POD" (D-cells_remove-6,
  `dock.hpp:181-186`). A `GcSummary`-style POD would be ceremony for one number. The `.tji`'s "POD
  seam" is satisfied by *reusing* the existing gateway (the seam), not by minting a struct.
  *Alternative rejected — an `OpenSummary` POD mirroring `GcSummary`.* Justified only if the notice
  needed more than a count (e.g. per-kind breakdown); it does not, and `project` may not name a
  `scene` type to produce a per-kind split anyway (A19's kind-agnostic count is deliberately the
  only thing L1 can honestly report).

- **D-reopen_degradation_notice-3 — A one-shot modal, not the sticky inline `project_feedback_`
  string.** The rail's `project_feedback_` (`dock.hpp:355`, drawn at `dock.cpp:320-322`) is
  sticky-until-next-action feedback for *rail actions the user just took* (a bad Open selection, a
  failed Save). A reopen degradation is a **passive** condition of the session at startup — the user
  took no action — and it is a data-loss report worth a deliberate acknowledgement, which is what a
  dismissible modal gives. The Clean up confirm (D-gc-3, a count-bearing one-shot modal) is the exact
  precedent; the notice is that modal with the confirm/cancel pair collapsed to a single Dismiss
  (there is nothing to confirm — the loss already happened).
  *Alternative rejected — pre-seed `project_feedback_` with the notice text at bootstrap.* Cheaper,
  but it (a) mixes a passive startup condition into the action-feedback channel, (b) is easy to miss
  (a small line in the rail's Project section), and (c) has no natural dismiss — it would linger or
  be cleared by the user's first unrelated action, neither of which fits a data-loss report.

- **D-reopen_degradation_notice-4 — Doc delta: add D25 to `docs/00-design.md`.** The notice is a new
  user-visible affordance, and `docs/00-design.md` (D-series) is the constitution for UI/UX. A19
  records the *value and the architecture* (an architecture row); it does not, and should not,
  describe the modal. No existing D-row covers a reopen-degradation surface, so the honest home is a
  new **D25** row recording the one-shot dismissible notice, the count-naming text, and that it fires
  only on the never-saved lossy reopen. Same-commit rule.
  *Alternative rejected — no doc delta, "A19 already covers it."* A19 is an `A`-row (structure), and
  its "announced rather than silent" clause names the *intent*, not the *UX*. Shipping a new modal with
  no D-row leaves the design docs unable to answer "how does the editor tell the user about a lossy
  reopen?" — precisely the gap the D-series exists to close.
  *Alternative rejected — an `A`-row for the new gateway virtual.* The virtual is a method on the
  existing A12/A13 seam in its established session-query family, adds no component and no DAG edge;
  the gc/insert/delete/frame leaves all added gateway virtuals under A12/A13 without new `A`-rows, and
  this follows them.

- **D-reopen_degradation_notice-5 — The one-shot latch lives on `Dockspace`, not the session.** The
  count is a durable session fact (the loss really happened, and re-querying it always returns the
  same number). "Have I shown the notice yet?" is *ephemeral presentation state*, so it belongs with
  the other transient modal flags on `Dockspace` (`gc_modal_open_`, `new_project_modal_open_`), never
  on `AppState`/the gateway. This keeps the gateway a pure reporter and makes the e2e's
  "dismiss → never reappears" assertion a property of the layer that owns presentation.

## Open questions

(none — all decided.) One item is carried forward from the predecessor to the **parking lot** as
cross-repo work, **not** a WBS leaf — no in-repo implementer can close it:

- **The library seam that would prevent the loss entirely (cross-repo, `ruoso/arbitrarycomposer`).**
  This leaf *announces* the degradation; *preventing* it needs `arbc::Document::open` to bind content
  (a registry-aware open or a public rebind seam), which A19 and `reopen_slab_adopt`'s Open-questions
  already surfaced to the parking lot. Until that lands, the never-saved-with-content reopen remains
  lossy and this notice is the honest close. No new editor-side WBS task (the same disposition the
  parking lot already records).

No named future WBS task: this leaf is the terminal UI cap of the reopen-degradation thread.

## Status

**Done** — 2026-07-24.

- `src/commands/ace/commands/app_state.hpp` + `src/commands/app_state.cpp`: added `unbindable_content_records_` field and `unbindable_content_records()` accessor to `commands::AppState`, ferrying the count from `OpenedProject` at bootstrap alongside the existing `rebuilt_from_canonical_`.
- `src/dock/ace/dock/dock.hpp`: added `virtual std::size_t reopen_unbindable_count() const { return 0; }` to `dock::ProjectGateway` (A13 session-query family); added `reopen_notice_shown_` latch and `draw_reopen_notice()` declaration to `Dockspace`.
- `src/dock/dock.cpp`: implemented `draw_reopen_notice()` as a one-shot dismissible modal mirroring `draw_gc_modal`; modal triggered via `Dockspace::draw()` when count > 0 and not yet shown; `Dismiss###reopen_notice_dismiss` button clears the latch.
- `src/app/ace/app/project_gateway.hpp` + `src/app/project_gateway.cpp`: `AppProjectGateway` overrides `reopen_unbindable_count()` to return `app_state_.unbindable_content_records()`.
- `CMakeLists.txt`: added `tests/reopen_degradation_notice_e2e_test.cpp` to `ace_shell_test`.
- `tests/commands_test.cpp`: L1 unit — `AppState carries the reopen unbindable-content count off the OpenedProject` (3 sections: lossy, clean, default).
- `tests/app_project_gateway_test.cpp`: L4 unit — `AppProjectGateway::reopen_unbindable_count reports the session's carried count` (3 sections incl. inert base-class default via `InertGateway`).
- `tests/reopen_degradation_notice_e2e_test.cpp` (new): ImGui Test Engine e2e — `reopen-notice e2e: a lossy reopen announces itself once and never reappears` + `reopen-notice e2e: a session that lost nothing is never shown the notice`.
- `docs/00-design.md`: D25 doc delta (reopen-degradation-notice UX row) present and matching the shipped modal string verbatim.
