# editor.panels.history — History panel: journal entry list + click-to-jump

## TaskJuggler entry

`tasks/00-editor.tji:267-272` — `task history` under `editor.panels` (siblings
`layers`, `overview`, `color`). Effort `2.5d`, `allocate team`, `depends
editor.project.undo, editor.dock.view_registry` (`:270`). The `note` (`:271`)
states: render `journal().entry_at(i).name` as an **ordered list** with the
`cursor()` position marked, `"Undo <name>"` / `"Redo <name>"` labels, and
**click-an-entry to jump** (loop `undo()`/`redo()` to reach the target cursor —
**single-step navigation only**). It fixes the level — **L3 `views`** — cites
**Design D15/D18/D21**, and names its source-of-debt (`tasks/refinements/editor/undo.md`).

The `.tji` note back-link is added when this refinement lands (per the ritual in
`tasks/refinements/README.md:57-68`, exactly as `undo.md` / `save_as.md` did) —
`Refinement: tasks/refinements/editor/history.md`, appended after the design-doc
citations. The task itself was pre-registered into the WBS by
`editor.project.undo`'s closer (undo.md:285-296), so this leaf writes only the
refinement; the closer marks `complete 100` and wires nothing new.

Downstream dependents: none block on this leaf. It completes the **Review**
built-in workspace preset (D21) — a `History·Export` tab-stack beside a Canvas —
by giving the already-registered `History` view *type* a real body in place of its
labeled placeholder.

## Effort estimate

**2.5 days** (from the `.tji`) — half a day over `undo`'s `2d` because this leaf is
UI surface, not wiring. The data path is already shipped and trivial to read: the
document journal exposes `depth()` / `cursor()` / `entry_at(i).name`
(`arbc::Journal`, reached by `state.document().journal()`), and the two navigation
verbs `commands::undo(AppState&)` / `commands::redo(AppState&)` already exist
(undo.md, `src/commands/ace/commands/app_state.hpp:146-151`). The work is one L3
`views` panel body — an ordered `ImGui::Selectable` list with the current head
highlighted, redoable (future) entries dimmed, a synthetic "base" (pre-first-edit)
row, `"Undo <name>"` / `"Redo <name>"` affordance labels, and a click handler that
navigates to a row by looping the existing verbs — plus its registration in the L4
shell and a thorough real-journal ImGui Test Engine e2e. **No new component, no new
DAG edge, no new L1 surface** (Decision D-history-1).

## Inherited dependencies

**Settled (from `editor.project.undo`, `tasks/refinements/editor/undo.md`):**

- **The journal is the model — reached read-only through the shipped seam.** The
  document-wide journal (`arbc::Journal`) is fully built and already read from
  editor code today: `dispatch` reads `doc.journal().depth()`
  (`src/commands/app_state.cpp:31`). Inspection methods `depth()` (`journal.hpp:90`,
  stored undoable+redoable count), `cursor()` (`:92`, applied-entry count in
  `[0, depth()]`), and `const JournalEntry& entry_at(std::size_t)` (`:97`, the
  history-inspection seam) — with `JournalEntry::name` (`journal_entry.hpp:44-50`) —
  are exactly what this panel renders. The journal is **never mutated** by
  navigation (undo/redo are forward publishes, undo.md:143-150), so the panel is a
  pure reader/navigator.
- **The two navigation verbs.** `commands::undo(AppState&)` /
  `commands::redo(AppState&)` (`app_state.hpp:146,151`) drive `journal().undo()` /
  `.redo()` a single step and return `UndoOutcome { bool moved; std::uint64_t
  revision; bool can_undo; bool can_redo; }` (`:133-138`). `moved == false` is the
  honest end-stop signal (empty journal / at the tip / a rare writer-path failure —
  cursor stays put; D-undo-1). Click-to-jump loops these to reach a target cursor
  (Constraint 4 / D-history-5).
- **The library exposes single-step navigation only** (undo.md:290-294): there is no
  random-access journal seek, so "jump to entry `i`" is a bounded loop of `undo()` /
  `redo()` calls — the mechanism the `.tji` note prescribes.
- **Undo/redo are writer-thread, synchronous (A4).** Today the UI thread *is* the
  writer thread (synchronous `dispatch`; off-thread submission is the future
  `editor.canvas.frame_sync`), so both reading the journal and looping the verbs
  happen inline in the draw loop — the same discipline as the `Ctrl+Z` chord
  (`src/dock/dock.cpp:170-181`, called from `Dockspace::draw()`).
- **Navigation does not touch the dirty baseline (D-undo-4).** `undo`/`redo` never
  call `mark_saved`; jumping through the panel inherits that — the panel is inert to
  the dirty indicator (Constraint 5).

**Settled (from `editor.dock.view_registry`, `tasks/refinements/editor/view_registry.md`):**

- **The `History` view type is already in the L1 catalog.** `enum class ViewType {
  Canvas, Layers, Inspector, Overview, Color, History, Assets, Export }`
  (`src/dockmodel/ace/dockmodel/view_registry.hpp:19`; `k_view_type_count = 8`) — a
  **singleton**, project-level view (D19). This leaf adds no catalog entry.
- **A panel supplies its body through the `views` draw-dispatch seam.**
  `using ViewBody = std::function<void(std::string_view view_id)>;` and
  `void register_view_body(dockmodel::ViewType, ViewBody)`
  (`src/views/ace/views/views.hpp:31,37`) — a per-type hook the downstream panel
  leaf fills in without touching the L1 catalog (D-view-registry-5). Passing an
  empty `std::function` restores the labeled placeholder. `draw_view(view_id)`
  (`:43`) parses the id and dispatches; `dock` calls it per open id in the frame
  loop. The dockspace owns the enclosing `Begin`/`End` + the tab ✕; the body draws
  into the current window.
- **The open-set is the `DockLayout`, not a shadow.** A view never keeps its own
  "am I open" flag (D-view-registry-2); likewise this panel keeps **no shadow copy**
  of the journal — it reads it fresh each frame (D-history-6).

**Settled (from `editor.project.app_state` / `save.md` / `save_as.md`):**

- **The one owned session and where it is captured.** `ace::commands::AppState`
  holds the single `arbc::Document` for the process lifetime (A7), reached by
  `state.document()` (`app_state.hpp:44-45`). The L4 shell already binds it as
  `ace::commands::AppState& app_state` (`src/app/shell.cpp:204`) and registers view
  bodies right there — the Canvas body captures `&probe`
  (`src/app/shell.cpp:217-218`) and is cleared on exit (`:263`). The History body
  registration slots into the same place, capturing `&app_state` (D-history-3).

**Pending (do not block this leaf):**

- **Off-thread edit submission** (`editor.canvas.frame_sync`) — when edits move off
  the UI thread, a UI-thread journal read may need a snapshot/guard; that safety is
  that leaf's concern (Constraint 6). Today, single-writer synchronous, no race.
- **The other placeholder panels** (`layers`, `overview`, `color`, `inspector`,
  `assets`, `export`) — each owns its own body leaf; this leaf ships only History.

## What this task is

Build the **History panel** — the dockable L3 `views` view that makes the undo
journal visible and navigable (D18 "History is a view"; the `History·Export` half
of the D21 Review preset). Each frame the body reads the document journal
(`state.document().journal()` → `depth()`, `cursor()`, `entry_at(i).name`) and
renders an **ordered list** of the coalesced scene steps: a synthetic **base**
row (the pre-first-edit state, cursor `0`) at the top, then one row per journal
entry in chronological order. The row at `cursor()-1` is the **current head**
(highlighted); rows at index `≥ cursor()` are **redoable/future** (dimmed). The
panel surfaces the `"Undo <name>"` / `"Redo <name>"` affordance labels (from
`entry_at(cursor-1).name` / `entry_at(cursor).name`, the same labels the `Ctrl+Z`
chord acts on). **Clicking a row jumps** the session to that point in history by
looping the existing `commands::undo` / `commands::redo` verbs until
`journal().cursor()` reaches the row's target (single-step navigation only). It is
a pure **reader/navigator** — it never mutates the journal directly and adds no
new L1 surface.

## Why it needs to be done

D15 makes undo a first-class capability and `editor.project.undo` wired the
keyboard chord, but a chord alone gives no *visibility*: the user cannot see how
many steps exist, what each one was, or where the cursor sits, and cannot jump
more than one step at a time without mashing `Ctrl+Z`. D18 lists History among the
dockspace's first-class views, and D21's **Review** preset pairs it with Export —
so until this body lands, the Review workspace opens a labeled *placeholder* where
its History pane should be. This leaf turns the journal `editor.project.undo`
exposed into the visible, click-navigable history the design promises. It is a
`editor.project.undo` dependent because it renders that journal and loops those
verbs, and a `editor.dock.view_registry` dependent because it plugs into the
`register_view_body` seam. It sits under the single `editor` milestone
(`tasks/99-milestones.tji:6`, `m9_editor`, whose `depends` names `editor.panels`).

## Inputs / context

**Design constitution (normative):**

- **D15** (`docs/00-design.md:476`) — *Undo = library transactions.* The journal
  holds only **scene** transactions with continuous gestures **coalesced to one
  step**; transient viewport pan/zoom is **not** on the journal. **Consequence for
  this panel:** it renders the journal *as-is* — one row per coalesced entry — and
  therefore shows scene steps only; the transient-vs-scene filtering is enforced
  upstream at transaction-creation time, never by the panel (Constraint 3). GC is
  not a transaction (absent from the list); consolidate is (present).
- **D18** (`docs/00-design.md:479`, and §10 `:429` naming History) — *Uniform
  dockspace.* History is a first-class relocatable view; the tool rail's launcher
  can open it into any container. This leaf makes the view non-placeholder.
- **D21** (`docs/00-design.md:482`) — *Saved workspace presets.* The **Review**
  built-in draws a `History·Export` tab-stack beside a Canvas; the preset already
  references `ViewType::History` (owned by `editor.dock.workspaces`). This leaf
  fills its body — no preset change here.

**Architecture constitution:**

- **§8** (`docs/01-architecture.md:144-179`) — the levelization DAG. `views` is
  **L3** (`:173`), may depend on `scene, interact, commands, render, dockmodel,
  imgui`, and is one of the two ImGui layers. `dock` is L3 too but depends on
  `views` (`:174`) — so **`views → dock` is illegal** (a cycle), which is why this
  panel reaches the session through `views → commands`, not `dock::ProjectGateway`
  (Decision D-history-2). Enforced by `scripts/check_levels.py`: `ALLOWED["views"]`
  includes `commands` (`:31`); `EXTERNAL_ALLOWED["arbc"]` includes `views`
  (`:46-47`), so a direct `<arbc/…>` journal include in `views` is legal;
  `ALLOWED["dock"] = {dockmodel, views}` (`:32`) confirms the non-edge.
- **§9** (`docs/01-architecture.md:181-208`) and **§9.1** (`:210-245`) — the
  universal DoD and the clang-asan offscreen-GL lane the e2e runs under. **A11**
  (`:261`) — `dockmodel` owns the headless view catalog (History already in it).

**Library API (v0.1.0, fetched under `build/dev/_deps/arbc-src/`; doc 14
`docs/design/14-data-model-and-editing.md`):**

- `arbc::Journal` — `src/model/arbc/model/journal.hpp`. Read seam this panel uses:
  `std::size_t depth()` (`:90`), `std::size_t cursor()` (`:92`, applied count in
  `[0, depth()]`), `const JournalEntry& entry_at(std::size_t)` (`:97`),
  `bool can_undo()` / `bool can_redo()` (`:86-87`). Navigation (looped by
  click-to-jump): `bool undo()` / `bool redo()` (`:83-84`, WRITER-THREAD ONLY,
  return `false` at the end-stop). Reached by `arbc::Document::journal()` →
  `Journal&` (`src/runtime/arbc/runtime/document.hpp:184-185`).
- `JournalEntry { std::string name; CoalesceKey coalesce_key; … }`
  (`journal_entry.hpp:44-50`) — `name` is the row label; a gesture is already
  coalesced to one entry, so one entry = one row.

**Existing seams (extend, do not fork):**

- `src/views/ace/views/views.hpp` / `src/views/views.cpp` — the `ViewBody` /
  `register_view_body` / `draw_view` dispatch (`views.hpp:31,37,43`,
  `views.cpp:44,60`). The new `draw_history` body is declared here and implemented
  in `views.cpp` (which gains a `<ace/commands/app_state.hpp>` include for
  `AppState` and a `<arbc/model/journal.hpp>` include for the read — both
  check_levels-allowed for `views`).
- `src/commands/ace/commands/app_state.hpp` — `AppState::document()`
  (`:44-45`), `commands::undo` / `commands::redo` (`:146,151`), `UndoOutcome`
  (`:133-138`). **Read-only consumer; nothing added here.**
- `src/app/shell.cpp:204,217-218,263` — the `AppState&` bind, the Canvas body
  registration to mirror, and the on-exit clear.
- `tests/undo_test.cpp` / `tests/commands_test.cpp` — the `ScratchDir` +
  `create_project` + `add_solid_content` fixture for building a **real** seeded
  `AppState` (with real journal entries) inside the e2e.
- `tests/undo_ui_e2e_test.cpp` / `tests/view_registry_e2e_test.cpp` — the ImGui
  Test Engine rig in `ace_shell_test`: drive-by-view-id, `ItemClick`,
  frame-pump-to-drain, state readback. The new e2e reuses this rig but over a
  **real** `AppState` (not a fake gateway), since the panel reads a real journal.

## Constraints / requirements

1. **Pure reader/navigator — never mutate the journal directly.** The body reads
   `depth()` / `cursor()` / `entry_at(i).name` and, to move, calls **only** the
   shipped `commands::undo` / `commands::redo`. No editor-side entry list, no
   snapshot, no direct `journal().undo()` from `views` (the verbs own the
   writer-path call). History is never mutated (undo.md D15 contract).
2. **No new L1 surface, no new component, no new DAG edge (§8).** The panel lives
   entirely in L3 `views`; it uses the existing `views → commands` edge
   (`check_levels.py:31`) and the check_levels-allowed `arbc`-in-`views` include
   (`:46-47`). It does **not** reach `dock::ProjectGateway` (`views → dock` is a
   cycle, `:32`) and adds **no** `commands` / `dockmodel` surface. `check_levels`
   stays clean with **no edit** to its maps.
3. **Render the journal as-is (D15).** One row per journal entry, in chronological
   order; a synthetic base row (cursor `0`) for the pre-first-edit state. The panel
   does **not** filter — coalescing and the transient-vs-scene line are enforced
   upstream, so the list already contains exactly the coalesced scene steps. GC
   ops (not transactions) never appear; consolidate (a transaction) does.
4. **Click-to-jump = a bounded single-step loop to the target cursor.** Clicking
   entry index `i` targets cursor `i+1` (entry `i` and all before it applied);
   clicking the base row targets cursor `0`. Navigate by looping: while
   `journal().cursor() > target` call `commands::undo(state)`; while `<` call
   `commands::redo(state)`; **stop early** if a verb returns `moved == false`
   (defensive end-stop). Clicking the current head is a **no-op** (target ==
   cursor). No random-access seek (the library exposes single-step only).
5. **Inert to the dirty baseline (D-undo-4).** Because the verbs never call
   `mark_saved`, jumping leaves `is_dirty()` deriving from revision drift exactly as
   `Ctrl+Z` does. The panel adds no dirty-state logic.
6. **Writer-thread discipline (A4).** Reads and the navigation loop run inline in
   the draw loop on the UI thread, which today *is* the writer thread — the same
   scope as the `Ctrl+Z` chord and `dispatch`'s journal read. This leaf introduces
   **no new threading**; off-thread journal reads are deferred to
   `editor.canvas.frame_sync`.
7. **Head highlight + future dimming + labels.** The row at `cursor()-1` is drawn
   as the selected/highlighted head (or the base row when `cursor() == 0`); rows at
   index `≥ cursor()` are dimmed (redoable/future). Surface `"Undo <name>"`
   (`entry_at(cursor-1).name`, when `cursor > 0`) and `"Redo <name>"`
   (`entry_at(cursor).name`, when `cursor < depth`) as affordance labels.
8. **Registration is L4-owned and cleared on exit.** The shell registers the body
   capturing `&app_state` and clears it (`register_view_body(History, {})`) before
   teardown, mirroring the Canvas body (`src/app/shell.cpp:217-218,263`) — the
   `register_view_body` seam is process-global, so a stale capture must not outlive
   the session.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`, `:181-208`);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.**
  `python3 scripts/check_levels.py` passes with **no edit to its maps**: `views`
  includes `<imgui.h>`, `<ace/commands/app_state.hpp>` (edge `:31`), and
  `<arbc/model/journal.hpp>` (external-allowed `:46-47`) — and **not**
  `<ace/dock/...>` (the `views → dock` cycle, forbidden `:32`). No new component,
  no new DAG edge; the L1 `commands` / `dockmodel` core gains no surface and no
  ImGui/GL/SDL include.

- **L1 Catch2 units — none added (justified).** This leaf ships no L1 logic: it
  reuses the shipped journal read API (`entry_at`/`cursor`/`depth`) and the shipped
  `commands::undo`/`redo` verbs — both already covered headless by
  `tests/undo_test.cpp` (round-trip/LIFO, empty-journal safety, cursor/revision
  transitions). The only view-side arithmetic — applied = `i < cursor`, head =
  `i == cursor-1`, jump target = `i+1` — is trivial and lives in `views` (not
  linkable into the headless `ace_tests`); it is pinned end-to-end by the e2e below
  against a **real** journal. Manufacturing an L1 `dockmodel`/`commands` projection
  purely to host a unit test would add the shadow state Constraint 2 forbids
  (Decision D-history-1).

- **UI behavior — ImGui Test Engine e2e (`views`/`dock`).** A new
  `tests/history_e2e_test.cpp` (joined to `ace_shell_test`, offscreen software-GL,
  reusing the `undo_ui_e2e_test.cpp` / `view_registry_e2e_test.cpp` rig) builds a
  **real** `commands::AppState` (`ScratchDir` + `create_project`, mirroring
  `undo_test.cpp`), `dispatch`es N ≥ 3 fixture edits (`add_solid_content`),
  registers the History body over it, opens the History view, and drives **by
  stable view id** (`ctx->WindowInfo(id).ID != 0`):
  1. **List shape.** N entry rows + a base row render; row labels match
     `entry_at(i).name`; `cursor() == N`; the head row (`N-1`) is highlighted; no
     rows dimmed.
  2. **Jump back.** `ItemClick` an earlier entry `j` (`j < N-1`) → assert
     `state.document().journal().cursor() == j+1`, the revision **advanced**
     (forward publish), the head moved to `j`, and rows `> j+1` are dimmed.
  3. **Jump forward.** Click a later entry `k` (`k+1 > cursor`) → `cursor() == k+1`.
  4. **Head is a no-op.** Click the current head → `cursor()` unchanged, no journal
     growth.
  5. **Jump to base.** Click the base row → `cursor() == 0`, no head highlight, all
     rows dimmed, `can_redo()` true.
  6. **Labels.** `"Undo <name>"` / `"Redo <name>"` reflect
     `entry_at(cursor-1)` / `entry_at(cursor)` after a jump.
  A screenshot **baseline** via the existing `capture_pixels` rig may be captured
  for signal, but is **not** a byte-exact assertion (software-GL pixels are flaky).
  This e2e is the coverage authority for the leaf.

- **Rendered output — golden: N/A (justified).** The panel composes ImGui chrome
  over journal *metadata* (names, cursor), not a libarbc `Document` render, so there
  is no byte-exact `render_offline` golden to compare — it asserts
  cursor/revision/row state, not pixels (undo.md's rationale applies verbatim: the
  round-trip's *visual* correctness is the library's doc-14 contract, covered by
  libarbc's own tests). Canvas's render_probe golden is untouched.

- **Threading — ASan scope (explicit).** No new threading: the panel reads and
  loops the verbs on the UI/writer thread inside the draw loop (A4 single-writer;
  the same scope as the `Ctrl+Z` chord and `dispatch`'s journal read). No new TSan
  surface; the new e2e runs under the existing clang-asan offscreen lane (§9.1,
  `docs/01-architecture.md:210-245`) and must be ASan/LSan-clean. Off-thread journal
  reads are deferred to `editor.canvas.frame_sync`.

- **Coverage.** ≥90% diff coverage on the changed lines (`diff-cover --fail-under=90`
  under the `coverage` preset); the e2e ships **with** this task. Because the leaf's
  logic is view-side, the e2e's multi-scenario clicks (back/forward/head/base) are
  the diff-coverage authority.

- **Format/build.** clang-format clean; the new e2e registered in `CMakeLists.txt`
  under `ace_shell_test`; `scripts/gate` green.

- **No deferred WBS leaf from this task.** The panel is self-contained (read the
  journal, loop the shipped verbs). The other placeholder panels already exist as
  their own leaves (Inherited dependencies → Pending); this leaf registers no new
  WBS task and edits no `.tji` shape.

## Decisions

- **D-history-1 — A thin L3 `views` reader/navigator; no new L1 surface.** The body
  reads the journal directly (`entry_at`/`cursor`/`depth`) and loops the existing
  `commands::undo`/`redo`; it adds no component, no DAG edge, and no `commands` /
  `dockmodel` surface — honoring `editor.project.undo`'s established boundary that
  the journal *is* the model and undo/redo *are* the verbs. *Rejected:* a
  `commands::history_view` L1 projection returning a plain-value entry list — the
  levelization argument for it evaporates because `arbc`-in-`views` is
  check_levels-allowed (`:46-47`), so `views` reads the journal directly with no
  illegal include; a projection would only duplicate the journal into a second
  representation the note explicitly says isn't needed (Constraint 2). *Rejected:* a
  `commands::jump_to(AppState&, size_t)` verb — the `.tji` note prescribes **looping
  the existing** `undo`/`redo`, and the loop's edge cases (clamp, early end-stop,
  head no-op) are fully exercised by the e2e against a real journal; a new verb would
  add surface for a single call site.

- **D-history-2 — Reach the session through `views → commands`, NOT
  `dock::ProjectGateway`.** The gateway exists to *invert* the forbidden `dock →
  commands` edge for the rail/chord (A13); `views` needs no such inversion because
  `views → commands` is a legal edge (`check_levels.py:31`). *Rejected:* routing
  click-to-jump through the gateway virtuals for symmetry with the `Ctrl+Z` chord —
  it is structurally impossible: the gateway type lives in `dock`, and `views → dock`
  is a cycle the DAG forbids (`:32`). This also means the panel needs **no** new
  gateway virtuals (the four `undo`/`redo`/`can_undo`/`can_redo` stay a
  dock/rail/chord concern). A single dispatch path is preserved in practice — both
  the chord and the panel bottom out in `commands::undo`/`redo` on the one owned
  `Document`.

- **D-history-3 — The L4 shell registers the body capturing the one `AppState&`.**
  The shell already binds `AppState& app_state` (`shell.cpp:204`) and registers view
  bodies there (`:217-218`); the History registration slots in beside the Canvas one
  and is cleared on exit (`:263`) to avoid a dangling capture in the process-global
  seam. *Rejected:* a `views`-owned singleton or a second `AppState` handle — it
  would fracture the one-owner discipline (A7); the shell is the sole session owner
  and the natural composition root.

- **D-history-4 — Chronological top-to-bottom list with a synthetic base row; head
  highlighted, future dimmed.** Oldest-first (base at top, newest at bottom) is the
  conventional history-list reading order and lets the pre-first-edit anchor sit
  naturally above entry `0`, so "undo everything" is a click on a real row (there is
  no journal index for "before the first edit"). Applied vs. redoable is shown by
  highlight (head) and dimming (index `≥ cursor`). *Rejected:* newest-first — less
  conventional for a click-to-jump list and it buries the base anchor at the bottom.
  *Rejected:* omitting the base row — then the user could never undo the very first
  edit from the panel (only entry `0`+ are clickable), losing parity with looped
  `Ctrl+Z`.

- **D-history-5 — Click-to-jump is a bounded single-step loop, defensively
  end-stopped.** Target cursor = `clicked_index + 1` (or `0` for the base row); loop
  `undo`/`redo` toward it and stop if a verb reports `moved == false`. This matches
  the library's single-step-only navigation (undo.md:290-294) and is inherently safe
  against a target drifting out of `[0, depth]`. *Rejected:* computing a step count
  and asserting it lands exactly — the `moved` end-stop is the honest guard and needs
  no separate bounds assertion; clicking the head naturally loops zero times.

- **D-history-6 — Read the journal fresh each frame; keep no shadow copy.**
  Consistent with `D-view-registry-2` / `D-tool_rail-5` ("the model is the single
  source of truth"): the list is rebuilt from `depth()`/`cursor()`/`entry_at` every
  draw. *Rejected:* caching the entry list and invalidating on edit — needless state
  plus a staleness-bug surface (a jump, an external edit, or a coalesced gesture all
  mutate the journal); the read is cheap and always current.

No doc delta: **D15** already settles undo = the library journal, the coalesced
one-entry-per-step shape, and the transient-vs-scene line the list reflects; **D18**
already lists History as a dockspace view; **D21** already places it in the Review
preset; **A11** already homes the (already-registered) `History` catalog type in
`dockmodel`; and `editor.project.undo` (D-undo-1..4) already settled the navigation
verbs and the dirty-inert contract this panel inherits. This leaf renders and
navigates what the constitution settled — it amends nothing.

## Open questions

(none — all decided)

## Status

**Done** — 2026-07-18.

- `src/views/ace/views/views.hpp`: forward-declared `commands::AppState`; added `draw_history(commands::AppState&, std::string_view)` declaration.
- `src/views/views.cpp`: implemented `draw_history` body — fresh-each-frame ordered journal list (synthetic base row, head highlighted, future entries dimmed, `Undo/Redo <name>` affordance labels, click-to-jump via looped `commands::undo`/`redo`); added `<ace/commands/app_state.hpp>` and `<arbc/model/journal.hpp>` includes (both check_levels-allowed for `views`).
- `src/app/shell.cpp`: registered History view body capturing `&app_state` alongside the Canvas body; cleared on exit with `register_view_body(ViewType::History, {})`.
- `CMakeLists.txt`: added `history_e2e_test.cpp` to `ace_shell_test`.
- `tests/history_e2e_test.cpp`: ImGui Test Engine e2e — `history e2e: list shape + click-to-jump back/forward/head/base over a real journal` (real `AppState`, 3 seeded edits; drives by stable view id `history` + row ids `###entryN`/`###base`; asserts cursor/revision/depth across list-shape, jump-back, jump-forward, head no-op, jump-to-base, return-to-tip).
- No new L1 surface, no new DAG edge, no new component — pure L3 `views` reader/navigator. `check_levels` stays clean with no edit to its maps.
- No deferred WBS leaf; no tech-debt follow-up.
