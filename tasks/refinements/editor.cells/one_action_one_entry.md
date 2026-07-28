# editor.cells.one_action_one_entry — One user-visible action is one journal entry

## TaskJuggler entry

`task one_action_one_entry` under `task cells` in
[`tasks/00-editor.tji`](../../00-editor.tji) (lines 552-557). Title:
*"One user-visible action is one journal entry for create and multi-delete."*

## Effort estimate

**1.5d**, `allocate team`. No new component, no new thread, no new external
dependency, no libarbc fork, no pin bump — the two atomic verbs it consumes
already shipped in the v0.4.0 pin this leaf depends on. The work is: swap two
create call sites onto one library verb, replace one N-dispatch delete loop
with one batch call, reconcile the now-false source/doc prose the two-entry
shape left behind, and — the load-bearing part of the budget — re-cut the
tests that today pass on **both** the old and the new journal shape so they
assert entry **count**, not just final state.

## Inherited dependencies

**Settled:**

- `editor.canvas.arbc_v040` (Done 2026-07-28,
  `tasks/refinements/canvas/arbc_v040.md`) — pins libarbc **v0.4.0**
  (`CMakeLists.txt:25`) and lands the surface this leaf is the sole consumer
  of: `Document::create_content_and_attach` and
  `Document::remove_contents(std::span<const Removal>)`
  (arbc#20). That refinement deliberately **consumed none of it** in `src/`;
  its `tests/arbc_pin_test.cpp` `static_assert`s only witness the two symbols'
  existence/shape. Consuming them is this leaf's whole job.
- `editor.cells.remove` (Done 2026-07-23,
  `tasks/refinements/editor.cells/remove.md`) — ships the single-object delete
  path (`scene::remove_cell` → `remove_cell_command` → the `delete_selection`
  loop) whose journal granularity this leaf corrects. Its **Constraint 5**
  (leave-the-rest-untouched), **Constraint 12** (root composition only),
  **Constraint 3** (one `Command` = one libarbc transaction), and
  **D-cells_remove-2** (the *accepted* N-entry asymmetry) are the exact prior
  decisions this task revisits.
- `editor.cells.model` (Done 2026-07-22,
  `tasks/refinements/editor.cells/model.md`) — ships `scene::add_cell` and
  **D-cells_model-7**, which *accepted* the two-entry create as a libarbc shape
  rather than an editor bug ("Do not paper over this with a bespoke single-entry
  path"). The library now offers the non-bespoke single-entry verb, so the
  premise of that acceptance is retired.
- `editor.cameras.model` — ships `scene::add_camera`, which **mirrors** the
  cell two-entry create (`src/scene/camera.cpp:516-526`); it collapses on the
  same verb here.

**Pending:** none. Every seam is shipped.

## What this task is

Two library verbs shipped in v0.4.0 make a user-visible create and a
user-visible multi-delete each cost exactly **one** journal entry — one undo
press, one atomic publish, no intermediate published state. This leaf routes
the editor's create and delete paths through them:

- **CREATE.** `Document::add_content` self-commits (it is the only call that
  binds a `Content` vtable), so a placed-object create is **two** entries —
  content first, then a second `transact` for `add_layer` + `attach_layer` —
  and passes through a published state in which a content exists attached to
  nothing (`src/scene/cell.cpp:305-313`, `src/scene/camera.cpp:516-526`).
  `Document::create_content_and_attach` does everything `add_content` +
  `add_layer` + `attach_layer` did, in the same order, **inside one
  transaction** → one entry. Both `scene::add_cell` and `scene::add_camera`
  route through it.
- **DELETE.** `editor.cells.remove` ships the correct single-object verb, but a
  multi-select delete of N objects dispatches `remove_cell_command` N times
  (`src/commands/cells.cpp:75-83`) → N entries, N undo presses to reverse
  something the user did **once**. `Document::remove_contents(span<Removal>)`
  tears down N contents-and-layers in one transaction → one entry, one undo.

This changes journal **granularity** only. *What* is created and *what* is
removed — and Constraint 5's leave-the-rest-untouched rule — are unchanged.

## Why it needs to be done

Undo is the editor's whole safety model (D15; D-cells_remove-4 makes it the
substitute for a delete confirmation dialog). An undo unit that does not match a
user action breaks that model in two concrete ways the user hits immediately:

- **Multi-select delete.** Select three cells, press Delete, press Ctrl+Z once
  expecting them all back — one comes back. The user must count their own
  deletions to know how many times to undo.
- **Create.** Insert a cell, press Ctrl+Z once expecting it gone — the layer
  detaches (it *looks* gone) but an orphan content lingers in the record set,
  and a second undo is needed to actually reverse the create.

This is registered tech-debt: the parking-lot items *"arbc Document lacks
atomic create-content-and-attach"* and *"arbc batch removal verb"*
(triage 2026-07-28) are the cross-repo asks that landed as arbc#20. The debt
survived this long precisely because **the existing tests pass on both the
two-entry and the one-entry shape** — they assert final state, never entry
count (see Constraint 3 / the proof obligation).

## Inputs / context

**Design docs (normative):**

- `docs/00-design.md` **D15** (line 482) — *Undo = library transactions;
  continuous gestures coalesce to one step.* One user-visible action = one
  journal entry is the direct reading of "coalesce to one step." This leaf
  realizes D15 for create and multi-delete; **no D15 edit is needed** — it is
  already aligned.
- `docs/01-architecture.md` **A16** (line 431) — cell insert, whose prose
  currently asserts *"`Document::add_content` (self-committing, so a create is
  **two** journal entries, as for cameras) → one `transact` …"*. That decided
  behavior is what this leaf changes, so A16 gets a same-commit amendment
  (see Decisions / doc delta).
- `docs/01-architecture.md` **A17** (line 432) — hit-testing / `pick_targets`.
  The `.tji` note cites it; it governs how the **selection** feeding a
  multi-delete is assembled, not the journal granularity this leaf changes.
  The delete continues to resolve its removal targets from `scene::cells` +
  `scene::cameras` inside the edit (D-cells_remove-3), **not** from
  `interact::pick_targets` — that boundary is untouched here.

**Library surface (v0.4.0, via FetchContent — `arbc/runtime/document.hpp`):**

- `add_content(std::shared_ptr<Content>, std::uint64_t kind = 0)` (`:108`) —
  self-commits; the vtable-binding call; the source of the two-entry create.
- `struct Placed { ObjectId content; ObjectId layer; };` (`:124-127`) and
  `Placed create_content_and_attach(std::shared_ptr<Content> content,
  std::uint64_t kind, ObjectId composition, const Affine& transform,
  double opacity = 1.0)` (`:128-130`). Doc comment (`:110-123`): *"ONE
  user-visible action, ONE journal entry (issue #20) … Everything `add_content`
  does still happens, in the same order and inside the one transaction."*
- `struct Removal { ObjectId content; ObjectId composition; ObjectId layer; };`
  (`:146-150`) and `void remove_contents(std::span<const Removal> removals)`
  (`:151`). Doc comment (`:132-145`): *"Delete N contents and their referencing
  layers as ONE user-visible action — one transaction, one publish, one journal
  entry, one undo press (issue #20) … An empty span is a no-op that publishes
  nothing."* Binding rows are retained exactly as the single-object form
  retains them.
- `void remove_content(ObjectId, ObjectId composition, ObjectId layer)`
  (`:175`) — the current single-object verb; equivalent to `remove_contents`
  over a one-element span.

**Editor call sites this leaf edits:**

- `src/scene/cell.cpp:284-315` — `add_cell`; the self-commit comment at
  `:305-307`, the two-entry sequence at `:308-313`.
- `src/scene/camera.cpp:501-528` — `add_camera`; the (now-false) *"libarbc
  exposes no atomic content+layer+attach for a vtable content"* comment at
  `:516-520`, the two-entry sequence at `:521-526`.
- `src/scene/cell.cpp:317-353` — `remove_cell` (single-object); one library
  transaction at `:349-351`. Resolves root composition internally via the
  file-local `root_composition` helper (D-cells_remove-8).
- `src/commands/cells.cpp:70-88` — `delete_selection`; the N-dispatch loop at
  `:75-83`, the eager `selection().clear()` at `:86` (D-cells_remove-7).
  `selected_removals` at `:27-61`; the editor's own
  `commands::Removal{content, layer}` built at `:56`
  (`src/commands/ace/commands/cells.hpp:46`).
- `src/commands/cells.cpp:63-68` — `remove_cell_command`.
- `src/app/ace/app/project_gateway.hpp:158` and `src/app/project_gateway.cpp`
  (~`:355`) — the L4 gateway comment describing "one `remove_cell_command`
  per object," and the writer-thread-only guard.

**Journal / dispatch plumbing:**

- `src/commands/app_state.cpp:101-114` — `dispatch` computes
  `journal_entries_added` by diffing `doc.journal().depth()` before/after
  `command.apply`, and reads `doc.pin()->revision()`. This is the observation
  point every entry-count assertion goes through.

**Existing tests (the proof surface):**

- `tests/cell_model_test.cpp:396-427` — asserts `journal_entries_added == 2`
  for an insert (the primary create-count test); `:429-442` refused insert
  `== 0`; `:318-345` failing factory leaves `depth()` / revision unchanged.
- `tests/camera_model_test.cpp` — the `add_camera_command` + undo path (mirror
  of the cell create; today two entries, undo-once detaches the layer so the
  camera leaves `cameras()` while an orphan content lingers).
- `tests/cells_remove_test.cpp:154-184` — one delete `== 1`, contrast-pinned
  against insert `== 2`; `:244-281` — a 3-object delete `== 3` entries and
  **3 undos** in reverse order, with the *accepted-asymmetry* comment at
  `:261-262`; `:314-343` mixed cell+camera `== 2`; `:347-377` stale-id skipped;
  `:379-399` empty selection `== 0`; `:550-592` round-trip byte-invariance.
- `tests/cells_remove_e2e_test.cpp:125-319` — ImGui e2e (rail + Delete/Backspace
  chords, Ctrl+Z restores the same `ObjectId`), asserting model state, not
  entry count; `:323-391` headless gateway `delete_selected() == 2`.
- Byte-invariance golden reused (no new golden file):
  `tests/goldens/cells_insert_nested_64x64.rgba8`.

## Constraints / requirements

1. **Levelization stays clean.** All edits live in L1 `scene` and L1
   `commands`; the library verbs are already-declared `scene → arbc` /
   `commands → arbc` calls. No new component, no new DAG edge, no ImGui/GL/SDL
   in the L1 core. `check_levels` stays clean.
2. **One user-visible action = one journal entry.** After this leaf, a cell
   create, a camera create, and an N-object multi-select delete each add
   **exactly 1** to `journal().depth()`. An empty-selection delete adds **0**
   (`remove_contents` over an empty span publishes nothing). A refused/failed
   create adds **0** and leaves the document byte-untouched (errors are values —
   the create still validates the factory and the root composition *before*
   the library call, D-cells_model unchanged).
3. **The proof is entry COUNT, not final state.** Every new/changed assertion
   must pin `journal().depth()` delta (or `journal_entries_added`) and the
   **undo-wholeness** of one Ctrl+Z, because the current tests pass on both the
   two-entry and one-entry shapes by only checking final `cells()`/`cameras()`
   membership. The two cases that fail *today* and must pass *after*:
   undo-once-removes-a-created-cell-whole (no orphan content; `depth()` back to
   baseline) and undo-once-restores-all-N of a multi-delete.
4. **One `Command` = one libarbc transaction (remove Constraint 3, restored).**
   `delete_selection` dispatches **one** `remove_cells_command` wrapping one
   `remove_contents` call, not a loop of N commands. This does not weaken the
   contract — it repairs it; the N-dispatch loop is what strained it.
5. **Leave-the-rest-untouched (remove Constraint 5) and root-composition-only
   (remove Constraint 12) are unchanged.** The set of objects removed is
   identical to today; only the journal granularity changes. Stale/mismatched
   `(content, layer)` pairs are skipped, not errors; every removal names the
   root composition.
6. **Placement / opacity semantics are preserved.** `create_content_and_attach`
   is fed the same finished `arbc::Affine` the two-entry path fed `add_layer`,
   and the same default opacity (`add_layer` used the library default; the verb
   defaults `opacity = 1.0`). A camera is non-rendering (A14), so opacity is
   inert for it. Returned `Placed.content` is what `add_cell`/`add_camera`
   return today; `Placed.layer` is available but callers still need only the
   content id.
7. **Writer-thread identity is unchanged.** `create_content_and_attach` and
   `remove_contents` are both **WRITER-THREAD ONLY**, exactly as `add_content`
   and `remove_content` were. Both paths still run inside
   `CanvasView::apply_edit` (A4.1 writer identity). Collapsing N dispatches to
   one reduces writer round-trips; it introduces no new shared state.
8. **No now-false prose left in the tree.** The two-entry / "no atomic
   content+layer+attach exists" comments (`src/scene/cell.cpp:305-307`,
   `src/scene/camera.cpp:516-520`, `src/commands/ace/commands/cells.hpp:31`,
   `:66`, `:89`, `src/app/ace/app/project_gateway.hpp:158`) and the
   accepted-asymmetry comment (`tests/cells_remove_test.cpp:261-262`) are
   reconciled with the shipped verbs. A16's two-entry sentence gets a
   same-commit doc amendment.
9. **No new external dependency, no libarbc fork, no pin bump.** Everything is
   in the v0.4.0 surface `editor.canvas.arbc_v040` already pinned.

## Acceptance criteria

Instantiating the universal DoD (`docs/01-architecture.md` §9) for this leaf:

- **Levelization.** `python3 scripts/check_levels.py` clean. No component
   added, no edge added — the change is call-site-only within `scene` and
   `commands`, both of which already depend on `arbc`.
- **L1 Catch2 (the bulk):**
  - `tests/cell_model_test.cpp` — flip the create-count case (`:396-427`) from
    `journal_entries_added == 2` to `== 1`, and **add** the undo-wholeness case
    that fails today: after one insert, `journal().depth()` is baseline `+ 1`;
    after **one** undo, `depth()` is back to baseline **and no orphan content
    remains** (the content record is gone, not merely detached — assert via
    `cells()` empty *and* a record-level check / redo-restores-identity
    round-trip). The refused (`== 0`) and failing-factory
    (`depth()`/revision unchanged) cases stay green unchanged.
  - `tests/camera_model_test.cpp` — the mirror: `add_camera_command` adds
    `+ 1`, and one undo reverses the camera create whole (no orphan content),
    contrast-pinned the same way. Sweep every other `add_camera`/`add_cell`
    entry-count or `depth()` assertion in the suite for the 2→1 flip (e.g.
    `tests/history_publish_test.cpp` create sites).
  - `tests/cells_remove_test.cpp` — flip the 3-object case (`:244-281`) from
    `== 3` entries / 3 undos to `== 1` entry / **one** undo restoring all three
    (rename it away from "accepted asymmetry"); the single delete (`:154-184`)
    stays `== 1` but is now contrast-pinned against the insert's **1**; mixed
    cell+camera (`:314-343`) flips `== 2` → `== 1`; stale-id (`:347-377`) stays
    one entry over the live subset (a batch of the live removals, not N);
    empty selection (`:379-399`) stays `== 0`; the round-trip byte-invariance
    render case (`:550-592`) stays green (granularity does not touch pixels).
  - New L1 verb `scene::remove_cells(document, std::span<const commands::Removal>)`
    (or a scene-local `Removal`) gets its own case: it validates each removal
    against the live pin (stale skip, as `remove_cell` did), resolves the root
    composition once, builds `arbc::Document::Removal{content, composition,
    layer}`, calls `remove_contents` once, and returns the validated count.
  - Naming follows the established convention
    (`TEST_CASE("cells remove: …")` / `"cells model: …"`, per
    `tests/cells_remove_test.cpp` and `tests/cell_model_test.cpp:396`).
- **Rendered output (golden).** No new golden file — reuse
  `tests/goldens/cells_insert_nested_64x64.rgba8` as the known-good render in
  a Catch2 byte-invariance case proving that collapsing create/delete to one
  entry leaves composited pixels byte-identical (the sibling refinements'
  established pattern; a mint's *granularity* is invisible to
  `render_offline`).
- **UI e2e (ImGui Test Engine, headless).**
  - `tests/cells_remove_e2e_test.cpp` — extend the delete case: select **N**
    objects (rail + Delete chord), then a **single** Ctrl+Z restores **all N**
    (asserting `scene::cells(...).size()` returns to N and the same
    `ObjectId`s), where today one Ctrl+Z would restore one. Drive the existing
    `Delete Selected###delete_selected` id.
  - `tests/cells_insert_e2e_test.cpp` — extend the insert case: after inserting
    a cell, **one** Ctrl+Z leaves the composition empty *and* one Ctrl+Y
    restores it whole (round-trip proves the single-entry reversal), using the
    existing `Insert Cell…###insert_cell` / `Insert###insert_confirm` ids.
- **Threading (ASan/TSan).** The writer-thread edit shape is unchanged (one
  `apply_edit` closure per action, no new shared state), so scope is the
  existing `tests/canvas_host_test.cpp` edit cases; extend/confirm one case
  drives `commands::delete_selection` (batch) and an insert under the
  `WriterThread` clean under TSan — the batch reduces, never adds,
  cross-thread traffic.
- **Coverage.** ≥90% diff coverage on the changed lines — ships with the task.
- **Doc delta.** The A16 amendment (below) rides in the closer's commit.

No deferred follow-up tasks — the surface is fully consumed here. **(none —
no named-future-task to register.)**

## Decisions

- **D-one_action_one_entry-1 — CREATE collapses onto `create_content_and_attach`;
  both `scene::add_cell` and `scene::add_camera` route through it.** Each
  resolves and guards the root composition first (unchanged — an invalid/absent
  composition still short-circuits before any library call), then calls the one
  verb instead of `add_content` + a `transact`. One journal entry, one undo,
  no intermediate attached-to-nothing published state. *Rationale:* this is
  precisely the "non-bespoke single-entry path" D-cells_model-7 said to wait
  for rather than hand-roll; the library now provides it, in the same order
  `add_content` did its work. *(Doc delta: A16 amendment.)* *Alternative
  rejected:* keep the two-entry create and hand-fold undo — exactly the bespoke
  atomicity D-cells_model-7 forbade, and now pointless.
- **D-one_action_one_entry-2 — DELETE routes through `remove_contents` in ONE
  `remove_cells_command`.** `delete_selection` resolves its removals
  (`selected_removals`, unchanged: from `scene::cells` + `scene::cameras`, not
  `pick_targets`, D-cells_remove-3), then dispatches a single command wrapping
  one `remove_contents` over the whole span. One transaction, one entry, one
  undo for N objects. *Rationale:* this **overturns D-cells_remove-2's accepted
  N-entry asymmetry** — that decision explicitly named "the symmetric upstream
  ask" (a batch verb) as the fix, which arbc#20 delivered. It also **restores**
  remove Constraint 3 (one `Command` = one transaction): the N-dispatch loop
  was the strain on that contract, not this. *Alternative rejected:* keep the
  loop but coalesce N journal entries host-side — the library exposes no undo
  coalescing hook, and faking it would re-introduce the bespoke-atomicity smell.
- **D-one_action_one_entry-3 — a new L1 `scene::remove_cells` owns the
  composition resolution and per-removal validation; the editor's
  `commands::Removal{content, layer}` stays a two-field pair.** The batch verb
  resolves the root composition once (root-only, remove Constraint 12), builds
  `arbc::Document::Removal{content, composition, layer}` per validated removal,
  skips stale/mismatched pairs against the live pin exactly as `remove_cell`
  did (Constraint 5), then makes one `remove_contents` call and returns the
  validated count. *Rationale:* keeping composition resolution in `scene`
  (matching `remove_cell`'s file-local `root_composition`, D-cells_remove-8)
  means `commands::Removal` and `selected_removals` are untouched — the ripple
  stops at the scene verb. *Alternative rejected:* widen `commands::Removal` to
  carry composition and build the `arbc::Document::Removal` span in `commands`
  — leaks the root-composition concern up a level and churns `selected_removals`
  for no gain (every removal shares the one root composition).
- **D-one_action_one_entry-4 — retire the single-object `scene::remove_cell` +
  `remove_cell_command`; unify all deletes on the batch path.** A one-element
  `remove_contents` is byte-identical in effect to `remove_content` (the
  library doc states binding rows are retained the same way), so once the loop
  is gone the single-object verb has no live caller (grep confirms only
  `delete_selection`, the gateway comment, and tests reference it). One
  deletion path, no dead code, one thing to test. *Rationale:* two parallel
  deletion paths that must be kept behaviorally identical is a maintenance trap;
  the batch verb subsumes the single case exactly. *Alternative rejected:* keep
  `remove_cell` for "symmetry with `add_cell`" — symmetry that nothing calls is
  dead weight; `add_cell` keeps its callers, `remove_cell` would not.
- **D-one_action_one_entry-5 — the proof obligation is entry COUNT plus
  undo-wholeness, not final state.** Every touched test asserts the
  `journal().depth()` delta and that a single undo fully reverses the action.
  *Rationale:* the debt survived because the landed tests assert only final
  `cells()`/`cameras()` membership, which is satisfied by both the old and new
  shapes (a two-entry create's first undo *detaches* the layer, so `cells()` is
  already empty — the orphan content is invisible to a membership check). Only
  a count assertion and an orphan/round-trip check distinguish the shapes.
- **D-one_action_one_entry-6 — reconcile the now-false prose in the same
  commit.** The two-entry / "no atomic content+layer+attach exists" comments
  and the "accepted asymmetry" test comment are edited to describe the shipped
  verbs. *Rationale:* leaving them is a lie in the tree that will mislead the
  next reader into re-deriving a constraint the library has removed; the §8/§9
  same-commit discipline applies to prose that asserts decided behavior.

### Doc delta — A16 amendment (`docs/01-architecture.md`)

A16's sentence *"`Document::add_content` (self-committing, so a create is two
journal entries, as for cameras) → one `transact` for `add_layer` +
`attach_layer`"* states a behavior this leaf changes, so A16 gains a
same-commit `*(Amended by `editor.cells.one_action_one_entry`: …)*`
parenthetical (matching the amendment style already on A13/A14/A18/A20)
recording that v0.4.0's `create_content_and_attach` collapses the two-entry
create into one journal entry and one undo press with no intermediate
attached-to-nothing state, that `scene::add_cell` and `scene::add_camera` both
route through it (the no-allowlist / factory-only / placement-as-finished-Affine
shape otherwise unchanged), and that the symmetric `remove_contents` batch verb
makes an N-object delete one entry and one undo too. D15 is already aligned and
needs no edit.

## Open questions

(none — all decided.)

## Status

**Done** — 2026-07-28.

- CREATE collapsed: `scene::add_cell` and `scene::add_camera` now call
  `Document::create_content_and_attach` (one journal entry, no intermediate
  attached-to-nothing published state), retiring the two-step
  `add_content` + `transact`/`add_layer`/`attach_layer` sequences in
  `src/scene/cell.cpp` and `src/scene/camera.cpp`.
- DELETE unified: a new L1 `scene::remove_cells(document, span<CellRemoval>)`
  resolves composition once, validates each removal against the live pin (stale
  skip), builds `arbc::Document::Removal` rows, and issues one
  `remove_contents` call; `delete_selection` now dispatches a single
  `remove_cells_command` instead of the N-command loop in
  `src/commands/cells.cpp`.
- Single-object `scene::remove_cell` + `remove_cell_command` retired
  (D-one_action_one_entry-4); all deletes route through the batch verb.
- Now-false prose reconciled: two-entry comments in `src/scene/cell.cpp`,
  `src/scene/camera.cpp`, `src/commands/ace/commands/cells.hpp`,
  `src/app/ace/app/project_gateway.hpp`, and the accepted-asymmetry comment
  in `tests/cells_remove_test.cpp` updated or removed.
- A16 amended in `docs/01-architecture.md`: records that
  `create_content_and_attach` collapses the two-entry create and
  `remove_contents` makes an N-object delete one entry and one undo.
- Tests flipped: `tests/cell_model_test.cpp`, `tests/camera_model_test.cpp`,
  `tests/cells_remove_test.cpp` assert `journal_entries_added == 1` for
  create and N-delete; undo-wholeness probes (orphan-free create undo,
  single-Ctrl+Z restores all N deleted objects) added.
- E2e extended: `tests/cells_remove_e2e_test.cpp` multi-select single-Ctrl+Z
  restore; `tests/cells_insert_e2e_test.cpp` Ctrl+Z/Ctrl+Y round-trip;
  `tests/frame_selection_e2e_test.cpp`, `tests/new_shot_from_view_e2e_test.cpp`,
  `tests/app_project_gateway_test.cpp`, `tests/canvas_host_test.cpp` updated
  for the batch-verb shape and one-entry counts.
