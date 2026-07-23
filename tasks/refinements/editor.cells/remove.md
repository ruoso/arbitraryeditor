# editor.cells.remove — Delete selected cell(s) via Document::remove_content + Delete chord

## TaskJuggler entry

- **Task:** `editor.cells.remove` (`tasks/00-editor.tji:348-353`, under
  `task cells "Cells & manipulation"` at `:321`).
- **Effort:** `1d` · `allocate team`.
- **Depends:** `!selection` (`editor.cells.selection`, `:351`), which is
  `complete 100` (`:332`) — the dependency is satisfied.
- **Note (`.tji:352`):** "Delete the selected cell(s) via
  `arbc::Document::remove_content(content, composition, layer)`
  (`document.hpp:131` — **atomic, one journal entry, undoable**) with the **Delete
  chord** and a **rail/inspector affordance** plus an **e2e**. Insert without
  delete is the gap `editor.cells.model` opens; **the library verb already
  exists, so this is mechanical**. Source-of-debt:
  `tasks/refinements/editor.cells/model.md`. Design: `docs/00-design.md` D7/D15."
- **Back-link:** the `.tji` note currently ends
  `Source-of-debt: tasks/refinements/editor.cells/model.md. Design:
  docs/00-design.md D7/D15.` — it carries **no** `Refinement:` pointer yet (this
  leaf was registered as tech debt by `editor.cells.model`, not written ahead as a
  flat interim path). This refinement lands at
  **`tasks/refinements/editor.cells/remove.md`** per the area-subdir layout
  (`tasks/refinements/README.md:9-18`); the closer appends the back-link to the
  note and adds `complete 100` after `allocate team`
  (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji` here.
- **Source of debt:** registered by `editor.cells.model` as a deferred WBS leaf
  (`tasks/refinements/editor.cells/model.md:513-518`): *"Delete the selected
  cell(s) via `arbc::Document::remove_content(content, composition, layer)`
  (`document.hpp:131` — atomic, **one** journal entry, undoable), with the Delete
  chord and a rail/inspector affordance plus an e2e. Insert without delete is the
  gap this leaf opens; the library verb already exists, so it is mechanical."*
- **Downstream dependents:** none in the `.tji` — nothing `depends` on this leaf.
  It closes an asymmetry rather than opening a seam. The two `ProjectGateway`
  virtuals it adds are what `editor.panels.inspector` (`:358`) and
  `editor.panels.layers` (`:364`) will reuse for their own delete affordances.

## Effort estimate

**1 day.** Every seam this leaf needs is shipped; nothing is greenfield.

- The **library verb exists and is exactly right**:
  `arbc::Document::remove_content(content, composition, layer)`
  (`build/dev/_deps/arbc-src/src/runtime/arbc/runtime/document.hpp:131`, doc
  comment `:109-130`) composes `detach_layer` + `remove(layer)` + `remove(content)`
  into ONE transaction — atomic, revision +1, **one journal entry**, one damage
  flush, "undoable BY CONSTRUCTION, through the journal alone". The editor writes
  no inverse and no snapshot.
- The **command seam is shipped and has a mould**: `commands::Command` /
  `dispatch` (`src/commands/ace/commands/app_state.hpp:110-126`) with
  `insert_cell_command` (`src/commands/ace/commands/cells.hpp:35-36`, impl
  `src/commands/cells.cpp:10-23`) as the one concrete verb to mirror, in the same
  `cells.{hpp,cpp}` pair.
- The **selection is shipped and already carries both ids we need**:
  `commands::Selection` (`src/commands/ace/commands/selection.hpp:18-62`) keys off
  the *content* `ObjectId`, and `scene::Cell{id, layer, …}`
  (`src/scene/ace/scene/cell.hpp:124-137`) / `scene::Camera`
  (`src/scene/ace/scene/camera.hpp:123-129`) supply the placing layer.
- The **stale-id safety net is shipped and was built for this leaf**:
  `Selection::prune` (`selection.hpp:51-57`) is called once per frame at
  `src/app/canvas_view.cpp:388-389`, and its doc comment names "a delete"
  explicitly (D-selection-7).
- The **gateway inversion and both UI homes are shipped**:
  `dock::ProjectGateway` (`src/dock/ace/dock/dock.hpp:80`), the rail's Insert
  section (`src/dock/dock.cpp:196-203`, called `:357`), and the chord handler
  beside undo/redo (`src/dock/dock.cpp:301-313`, called `:455`).
- The **look-through fail-safe is already written**: a canvas looking through a
  camera that no longer exists falls back to the free viewport
  (`src/app/canvas_view.cpp:100-122`), so deleting a camera needs no new
  bookkeeping.

New code is one L1 `scene` verb, one L1 `commands` resolver + command + facade,
two `ProjectGateway` virtuals, one rail item, and one chord — plus the tests. No
new component, no new DAG edge, no new external dependency, no libarbc change, no
doc delta.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cells.selection`** (`tasks/refinements/editor.cells/selection.md`) —
  the project-level `commands::Selection` keyed by content `ObjectId`
  (D-selection-1), `interact::PickTarget{id, layer, kind, placement, extent}`
  (`src/interact/ace/interact/pick.hpp:50-62`), and above all **D-selection-7**:
  `prune` exists because *"undo, GC, and the imminent `editor.cells.remove` can
  all delete a selected object"*. Its companion contract — *"a redo that restores
  the same object does NOT restore the selection — documented, not a bug"* —
  is the undo-after-delete behaviour this leaf inherits and pins with a test
  rather than re-litigates.
- **`editor.cells.model`** (`tasks/refinements/editor.cells/model.md`) — the
  `commands::Command`/`dispatch` shape for a scene edit (D-cells_model-7), the
  `scene::add_cell` transaction mould (`src/scene/cell.cpp:284-315`), the
  Registry-driven no-allowlist principle (A16), and the Constraint-5 rule that
  **every UI-driven mutation runs inside `CanvasView::apply_edit`**
  (`src/app/project_gateway.cpp:243-245`).
- **`editor.project.undo`** — `commands::undo`/`redo`
  (`src/commands/ace/commands/app_state.hpp:147,152`) are libarbc journal
  navigation, never an editor stack (D-undo-1); `can_undo()`/`can_redo()` gate the
  chord so a no-op is never dispatched (D-undo-3). This leaf copies that gating
  shape verbatim.
- **`editor.dock.tool_rail` / `editor.project.entry`** — the A12/A13 dependency
  inversion: `dock` may not include `ace/commands` or `ace/scene`
  (`scripts/check_levels.py:21-37`), so every session-acting rail verb crosses
  `ProjectGateway` (`dock.hpp:80-172`). Delete is the seventh such verb after
  Save / Save As / Clean up / Undo / Redo / Insert.
- **`editor.canvas.single_writer`** — `CanvasView::apply_edit`
  (`src/app/ace/app/canvas_view.hpp:78` → `src/render/ace/render/canvas_host.hpp:104`)
  is the writer-identity seam; `remove_content` is documented **WRITER-THREAD
  ONLY** (`document.hpp:130`), so it must ride the shipped edit runner
  (`src/app/project_gateway.cpp:112-122`).

**Pending (owned here):** nothing. Every predecessor is `complete 100`; this leaf
adds no dependency of its own and blocks nothing downstream.

## What this task is

Make a placed object leave the composition. `editor.cells.model` shipped insert
with no inverse; this leaf ships the inverse, on the project-level selection, with
two user affordances and the undo behaviour the library already guarantees.

1. **L1 `scene`** — `remove_cell(arbc::Document&, arbc::ObjectId content,
   arbc::ObjectId layer) -> bool` in `src/scene/ace/scene/cell.hpp` +
   `src/scene/cell.cpp`, the declared inverse of `add_cell`
   (`cell.hpp:120-122`). It resolves the root composition through the file-local
   `root_composition(const arbc::DocRoot&)` helper already in that TU
   (`src/scene/cell.cpp:138-145`), verifies `layer` is a live member, and calls
   `Document::remove_content(content, composition, layer)`. Returns `false`
   (mutating nothing) for a stale or non-member id.
2. **L1 `commands`** — in `src/commands/ace/commands/cells.hpp` +
   `src/commands/cells.cpp`, beside `insert_cell_command`:
   - `struct Removal { arbc::ObjectId content; arbc::ObjectId layer; };`
   - `std::vector<Removal> selected_removals(const arbc::Document&, const
     arbc::Registry&, const Selection&)` — the pure resolver: walk
     `scene::cells(document, registry)` (`cell.hpp:147`) and
     `scene::cameras(document)` (`camera.hpp:134`), keep the ones the selection
     contains, in **selection order**, skipping ids with no live target.
   - `Command remove_cell_command(arbc::ObjectId content, arbc::ObjectId layer,
     bool& removed)` — exactly one `scene::remove_cell`, therefore exactly one
     journal entry, keeping `dispatch`'s one-command-one-transaction contract
     (`app_state.hpp:103-121`) intact.
   - `DeleteOutcome delete_selection(AppState&)` → `{removed,
     journal_entries_added}` — resolve, dispatch one command per removal, then
     `state.selection().clear()`. Plus `bool can_delete(const AppState&)`.
3. **L3 `dock`** — two new `ProjectGateway` virtuals mirroring `can_undo`/`undo`
   (`dock.hpp:143-146`), non-pure with inert defaults exactly as `insert_kinds`
   (`dock.hpp:157`) so the gateway fakes in the existing suites need no churn:
   `virtual bool can_delete() const { return false; }` and
   `virtual std::size_t delete_selected() { return 0; }`. A rail item
   `Delete Selected###delete_selected`, disabled when `!can_delete()`, added to
   `draw_insert_section` (`src/dock/dock.cpp:196-203`, retitled **Edit** since it
   now holds both halves). A `handle_delete_shortcut(ProjectGateway&)` beside
   `handle_undo_shortcuts` (`dock.cpp:301-313`), called from the same per-frame
   block (`dock.cpp:449-456`).
4. **L4 `app`** — `AppProjectGateway::can_delete/delete_selected` in
   `src/app/ace/app/project_gateway.hpp` + `src/app/project_gateway.cpp`,
   overriding into `commands::can_delete` / `commands::delete_selection` with the
   mutation wrapped in `run_edit` (`project_gateway.cpp:112-122`) exactly as
   `insert_cell` does at `:245`.

It deliberately does **not** ship: a confirmation modal (D-cells_remove-4), a
delete of anything outside the root composition (`add_cell` inserts only there
too; entered/isolated nested scope is `editor.panels.layers`, `.tji:364`), asset
reclamation (owned bytes are `editor.project.gc`'s, D13), a Cut/Copy/Paste
clipboard (`editor.import.*`), or a History-panel entry label beyond the command
name.

## Why it needs to be done

`editor.cells.model` made the composition growable and left it un-shrinkable: the
only way to remove a cell today is `Ctrl+Z` back past its insert, which also
undoes everything after it. `grep -rn remove_content src/ tests/` returns nothing
— the library verb has never been called from this tree. A compositor whose
objects can only accumulate is not usable for the first real editing session, and
every downstream leaf that lets a user *make* something (`editor.cells.gizmo`,
`editor.paint.*`, `editor.import.*`) increases the cost of not being able to
unmake it.

It is also the last piece of the selection model's promise. `editor.cells.selection`
built `Selection::prune` **specifically** for this leaf
(`selection.md:701-714`, `selection.hpp:51-57`), and that machinery is currently
exercised only by undo and GC. Shipping delete turns a defensive guard into a
tested path.

Finally it closes the D15 story for structural edits: place *and* remove are both
library transactions, so the journal is the complete history of the scene.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **D7 — Manipulation model** (`docs/00-design.md:474`): *"Cells and cameras share
  **one shape** (affine placement + a resolution number) and **one select tool**,
  differing only in direction… Cells grabbed by body, cameras by border/label with
  click-through interiors."* This is what makes a selected **camera** deletable by
  the same verb (D-cells_remove-1).
- **D15 — Undo = library transactions** (`:482`): *"Scene edits are transactions
  (doc 14); continuous gestures coalesce to one step… **GC is a confirmed op (not
  undoable)**; consolidate is reversible."* The named exception is the argument
  that an *undoable* op needs no confirm (D-cells_remove-4).
- **§9 Undo/redo** (`:369-378`), which names this operation explicitly at `:371`:
  *"Every scene edit (place, move, scale, rotate, reorder, paint, import,
  **delete**, resolution change) is a **library transaction** (doc 14), so
  undo/redo is the document's journal, not an editor reimplementation."*
- **D13 — Assets, GC & portability** (`:480`) and **§8** (`:313`): *"GC touches
  only owned bytes."* Deleting a cell does **not** reclaim `assets/` bytes;
  Clean up does. The refinement pins that as a non-goal, not an omission.
- **D19 — Project-scoped state** (`:486`): one project-level selection shared by
  every canvas and panel — so Delete is a project verb, not a canvas verb.
- **D20 — Tool rail modal set** (`:487`): *"**Import** is **not** a modal tool …
  surfaces as a rail **action** … never a mode."* The precedent that a one-shot
  verb is a rail **action** and never a fifth `ToolId`.
- **§11 open item — the full input map** (`:499-500`): *"The complete
  keyboard/shortcut set and tool palette (the pieces are decided; the map isn't
  written)."* The standing licence under which D-undo-3 minted `Ctrl+Z` and
  D-selection-9 minted `Ctrl+A`/`Escape`; `Delete`/`Backspace` lands under it.

**Governing architecture rows:**

- **A8** (`docs/01-architecture.md:299`) + **§8** (`:186-220`) — the DAG. Relevant
  edges: `commands → {base, project, scene}`, `interact → {base, scene}`,
  `dock → {dockmodel, views}`. `commands` may **not** depend on `interact`
  (`scripts/check_levels.py:28`), which is why the removal resolver reads
  `scene::cells`/`scene::cameras` rather than `interact::pick_targets`.
- **A9** (`:300`) + **§9** (`:222-249`) — the layered DoD this leaf's Acceptance
  criteria instantiate.
- **A12/A13** (`:303-304`) — the `ProjectGateway` inversion; **A16** (`:307`)
  extended it with the two insert virtuals. Delete adds two more of the same
  shape; the row already says "extends A12/A13", so no new row is needed.
- **A14** (`:305`) — a camera **is** a `Content` + a `Layer` of kind
  `org.arbc.camera`, identical in shape to a cell. That is what makes
  `remove_content` kind-agnostic here.
- **A17** (`:308`) — `interact::pick_targets` merges cells and cameras into one
  z-ordered target list; the selection can therefore hold either.
- **A4/A4.1** (`:295`, `:84-123`) — writes are bound to one writer *identity*;
  `remove_content` is WRITER-THREAD ONLY.

**libarbc API surface** (fetched at tag `v0.2.0`, `CMakeLists.txt:25`; headers
under `build/dev/_deps/arbc-src/src/`):

- `arbc::Document` (`runtime/arbc/runtime/document.hpp`):
  **`remove_content(ObjectId content, ObjectId composition, ObjectId layer)`
  `:131`** (contract `:109-130`) — one transaction, revision +1, **one journal
  entry**, atomic, undoable by construction, binding row **retained** while the
  journal holds the removal, WRITER-THREAD ONLY; `add_content` `:107`
  (self-commits — this is why a *create* is two entries and a *delete* is one);
  `transact` `:179`; `journal()` `:185-186`; `pin()` `:262`.
- `arbc::Journal` (`model/arbc/model/journal.hpp`): `undo()` `:83`, `redo()`
  `:84`, `can_undo()` `:86`, `can_redo()` `:87`, `depth()` `:90`.
- `arbc::Model::Transaction` (`model/arbc/model/model.hpp`): `detach_layer`
  `:524`, `remove` `:542`, `coalesce(CoalesceKey)` `:547` — the three the library
  verb composes internally, and the coalesce hook `remove_content` deliberately
  does **not** expose (see D-cells_remove-2).
- Upstream reference impl: `arbitrarycomposer/src/runtime/document.cpp:128-153`.

**Editor seams this leaf extends:**

- `scene::add_cell` — `src/scene/ace/scene/cell.hpp:120-122`, impl
  `src/scene/cell.cpp:284-315`; the root-composition helper it uses is
  `src/scene/cell.cpp:138-145`.
- `scene::Cell{id, layer, kind_id, placement, content_bounds}` —
  `src/scene/ace/scene/cell.hpp:124-137`; `scene::cells` `:147`.
- `scene::Camera{id, layer, …}` — `src/scene/ace/scene/camera.hpp:123-129`;
  `scene::cameras` `:134`.
- `commands::Command`/`DispatchOutcome`/`dispatch` —
  `src/commands/ace/commands/app_state.hpp:110-126`, impl
  `src/commands/app_state.cpp:55-65`; `AppState::registry()` `:50`/`:93`,
  `AppState::selection()` `:53`/`:94`, `is_dirty()` `:69`.
- `commands::insert_cell_command` + `InsertCellOutcome` —
  `src/commands/ace/commands/cells.hpp:21-24,35-36`, impl
  `src/commands/cells.cpp:10-23`.
- `commands::Selection` — `src/commands/ace/commands/selection.hpp:18-62`
  (`items()` `:24`, `primary()` `:28`, `clear()` `:39`, `prune()` `:57`), impl
  `src/commands/selection.cpp:7-73`; pruned per frame at
  `src/app/canvas_view.cpp:388-389`.
- `interact::PickTarget`/`PickKind` — `src/interact/ace/interact/pick.hpp:43-62`;
  `pick_targets` `:182-183`.
- `dock::ProjectGateway` — `src/dock/ace/dock/dock.hpp:80`; `clean_up` `:133`,
  `undo/redo/can_undo/can_redo` `:143-146`, `insert_kinds` `:157`, `insert_cell`
  `:170-172`. Dock-local POD precedent: `GcSummary` `:34`, `InsertKindSpec` `:57`.
- Rail + chords — `src/dock/dock.cpp:196-203` (`draw_insert_section`, called
  `:357`), `:301-313` (`handle_undo_shortcuts`, called `:455`), `:325-389`
  (`draw_tool_rail`); the GC confirm modal precedent `:88-113`.
- Gateway impl — `src/app/project_gateway.cpp:92-110` (undo/redo), `:112-122`
  (`run_edit`), `:128,:130` (`can_undo`/`can_redo`), `:209-247` (`insert_cell`,
  with the single `dispatch` call site at `:245`).
- Look-through fail-safe on a vanished camera — `src/app/canvas_view.cpp:100-122`.
- Pane-scoped selection chords — `src/app/canvas_view.cpp:450-459`.

**Predecessor refinements:** `tasks/refinements/editor.cells/model.md`,
`tasks/refinements/editor.cells/selection.md`, `tasks/refinements/editor/undo.md`.

**Test rigs:** `ace_tests` source list `CMakeLists.txt:219-234`; `ace_shell_test`
source list `CMakeLists.txt:249-261`; goldens under `tests/goldens/` compared via
`ace_test::compare_golden` (`tests/golden_support.hpp:36-46`), rendered by
`render::render_document_srgb8` (`src/render/render.cpp:22`); `ACE_GOLDEN_DIR`
injected at `CMakeLists.txt:240-241`.

## Constraints / requirements

1. **Levelization (`check_levels` clean).** No new component and no new DAG edge.
   `commands` gains a `scene` include only (already legal,
   `scripts/check_levels.py:28`) and gains **no** `ace/interact` include — the
   removal resolver must not reach for `interact::pick_targets`. `dock` gains
   **no** `ace/scene` or `ace/commands` include: the two new verbs cross as
   `ProjectGateway` virtuals over primitives (`bool`, `std::size_t`), needing not
   even a POD. Nothing in `src/scene/`, `src/commands/`, or `src/interact/` gains
   an ImGui/GL/SDL include.
2. **The library verb is the only deletion mechanism.**
   `arbc::Document::remove_content` (`document.hpp:131`) is called; the editor
   does **not** hand-compose `transact` + `detach_layer` + `remove` + `remove`,
   which would silently re-implement the library's Decision-1 binding-retention
   invariant (`document.hpp:120-130`) and drift from it.
3. **One `Command` performs exactly one libarbc transaction.** The
   `dispatch` contract (`app_state.hpp:103-121`) says a command is one journal
   entry; `remove_cell_command` deletes exactly one object so the contract holds
   literally. A multi-object delete is N *commands*, not one command doing N
   transactions.
4. **Every UI-driven delete runs inside `CanvasView::apply_edit`.** The gateway
   override wraps its work in `run_edit` (`project_gateway.cpp:112-122`), matching
   Constraint 5 of `model.md`. `remove_content` is WRITER-THREAD ONLY
   (`document.hpp:130`); a bare `dispatch` from the UI thread is forbidden.
   Headless gateway tests with no live canvas get the direct-invoke default runner.
5. **Errors are values; a delete never throws and never half-mutates.** A selected
   id with no live target (already deleted, undone away, a camera GC'd out from
   under the selection) is **skipped**, not an error: `selected_removals` omits it
   and `scene::remove_cell` returns `false` having opened no transaction. Deleting
   an empty selection is a no-op with zero journal entries and an unchanged
   revision.
6. **Targets are resolved on the writer thread against the live document**, inside
   `delete_selection`, never from a UI-side snapshot taken frames earlier — an
   undo/redo between the click and the dispatch can invalidate a layer id.
7. **The selection is emptied by the delete itself**, not left to the next frame's
   `prune`. `delete_selection` calls `Selection::clear()` after dispatching, so the
   post-condition is observable in a headless Catch2 test with no frame pump. The
   per-frame `prune` at `canvas_view.cpp:388-389` remains the belt-and-braces guard
   for the undo/GC paths and is not removed.
8. **Delete does not restore on redo-of-nothing and does not touch the dirty
   baseline.** `mark_saved` is never called on the edit path (D-undo-4); a delete
   advances the revision and therefore re-dirties the session (A13).
9. **The chord must not fire while the user is typing.** `Delete`/`Backspace` are
   bare keys, unlike `Ctrl+Z`. The handler is gated on
   `!ImGui::GetIO().WantTextInput` **and** `ImGui::GetTopMostPopupModal() ==
   nullptr`, so the Insert Cell modal's `###insert_field`, the workspace-name
   field, and any future rename box keep the key.
10. **No confirmation dialog.** A delete is journaled and undoable; the confirm
    modal exists for GC precisely because GC is not (D15, `docs/00-design.md:482`).
11. **Deleting a camera is fail-safe downstream, not specially handled here.** A
    canvas looking through the deleted camera falls back to the free viewport by
    the shipped code path (`src/app/canvas_view.cpp:100-122`); this leaf adds no
    look-through fix-up and must not regress that fallback.
12. **Root composition only.** `scene::remove_cell` deletes from the root
    composition (`cell.cpp:138-145`), symmetric with `scene::add_cell`, which only
    inserts there. Entered/isolated nested scope is `editor.panels.layers`'
    (`.tji:364`).

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate`
green (check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.**
  Asserted by inspection as well as by the lint: `src/commands/` gains an
  `ace/scene/camera.hpp` include and **no** `ace/interact/` include;
  `src/dock/dock.hpp` and `src/dock/dock.cpp` gain no `ace/scene` or
  `ace/commands` include (the two new virtuals traffic in `bool`/`std::size_t`
  only); `src/scene/` gains no ImGui/GL/SDL include. No entry in
  `scripts/check_levels.py:21-37` changes.

- **L1 logic — Catch2 unit** (`tests/cells_remove_test.cpp`, new file added to the
  `ace_tests` source list at `CMakeLists.txt:219-234`; naming follows
  `tests/cell_model_test.cpp:396` and `tests/selection_test.cpp:527`, i.e.
  `TEST_CASE("cells remove: …")`):
  - **One entry per delete (Constraint 3, D15):** dispatching
    `remove_cell_command` for one cell adds **exactly 1** to
    `journal().depth()` and bumps `pin()->revision()`; the cell leaves
    `scene::cells()`. Contrast-pinned against the insert's 2
    (`cell_model_test.cpp:396`) in the same case.
  - **Undo restores on the SAME `ObjectId`; redo removes it again
    (D15/D-cells_remove-2):** after `commands::undo`, `scene::cells()` lists the
    cell with its original id, layer, and placement; after `commands::redo` it is
    gone again.
  - **Undo does NOT restore the selection (D-selection-7):** the selection stays
    empty across the undo — pinning selection.md's documented behaviour at the leaf
    that makes it reachable.
  - **N objects ⇒ N entries ⇒ N undos (D-cells_remove-2):** deleting a 3-object
    selection adds exactly 3 entries, and it takes 3 `undo()` calls to restore all
    three, in reverse order. This test is the record of the accepted asymmetry.
  - **A selected camera is deleted by the same verb (D-cells_remove-1):** a
    selection holding an `org.arbc.camera` content id removes it from
    `scene::cameras()` with one entry, and `scene::cells()` is untouched.
  - **A mixed cell+camera selection deletes both, in selection order.**
  - **A stale id is skipped, not an error (Constraint 5):** a selection holding an
    id with no live target deletes the live members only, adds entries only for
    them, and throws nothing.
  - **An empty selection is a no-op:** `can_delete` is false, `delete_selection`
    returns `{0, 0}`, `journal().depth()` and `pin()->revision()` are unchanged.
  - **The selection is empty afterwards (Constraint 7):** `empty()` is true and
    `primary()` is the invalid `ObjectId{}` immediately after
    `delete_selection`, with no frame pump.
  - **Dirty bookkeeping (Constraint 8, A13):** a delete on a freshly-saved session
    makes `is_dirty()` true; a subsequent `undo` leaves it true (undo never marks
    clean, D-undo-4).
  - **The resolver is pure and order-preserving:** `selected_removals` over a
    known document returns `{content, layer}` pairs matching `scene::cells` /
    `scene::cameras`, in selection order, with no duplicates.

- **Rendered output — golden.** **No new golden file.** The assertion is
  round-trip byte-*in*variance, which is stronger than a new baseline: in
  `tests/cells_remove_test.cpp`, render the probe document at 64×64 via
  `render::render_document_srgb8` (`src/render/render.cpp:22`) → insert a cell →
  render again and assert the bytes **differ** (so the test cannot pass vacuously)
  → delete it → assert the bytes are **byte-identical** to the first render. The
  existing `tests/goldens/cells_insert_nested_64x64.rgba8`
  (`ace_test::compare_golden`, `tests/golden_support.hpp:36-46`) anchors the
  middle render as a known-good image; nothing new lands in `tests/goldens/`.

- **UI e2e — ImGui Test Engine** (`tests/cells_remove_e2e_test.cpp`, new file
  added to the `ace_shell_test` source list at `CMakeLists.txt:249-261`; modelled
  on `tests/cells_insert_e2e_test.cpp:165` and `tests/undo_ui_e2e_test.cpp:98`,
  reusing their `ScratchDir`, `pump_until`, and gateway/edit-runner wiring;
  registered as `IM_REGISTER_TEST(engine, "cells", "delete_rail_and_chord")`).
  Assertions are on model state, never pixels:
  - the rail's `Delete Selected###delete_selected` item is **disabled** with an
    empty selection and **enabled** once a cell is selected;
  - clicking it removes exactly that cell from `scene::cells()` and leaves
    `selection().empty()`;
  - pressing `Delete` with a second cell selected removes it too, and `Backspace`
    works identically on a third;
  - `Ctrl+Z` restores the last-deleted object and the selection stays empty;
  - **the text-input guard (Constraint 9):** with the Insert Cell modal open and
    the cursor in `###insert_field`, typing a `Delete` keypress edits the field and
    deletes **nothing** — `scene::cells().size()` is unchanged. This is the
    sharpest regression this leaf can ship, so it is a named e2e assertion rather
    than a comment;
  - deleting the camera a canvas is looking through leaves the pane rendering
    through the free viewport, not a stale frame or a crash (Constraint 11).

- **Threading (ASan/TSan).** One case appended to `tests/canvas_host_test.cpp` on
  the real-pool `CanvasHost` (`default_interactive_pool_config()`, the
  D-edit_render_sync-3 anchor): drive a `remove_content` through
  `CanvasHost::apply_edit` (`src/render/ace/render/canvas_host.hpp:104`) while the
  render thread walks the same document over the lock-free `pin()` seam, and assert
  the frames keep arriving. This pins that erasing a `ContentRecord` races nothing
  against the v0.2.0 copy-on-write binding read (A4/A4.1) — the one genuinely new
  threading surface, since every prior edit only *added* records. Runs in the
  existing ASan and TSan lanes; no new lane, no new suppression.

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed
  lines; clang-format + build clean. Tests ship with the task.

- **Deferred WBS work.** **None.** Every out-of-scope item already has a scheduled
  owner: the inspector's own Delete button is `editor.panels.inspector`
  (`.tji:358`, which builds the selection-keyed property sheet and reuses the two
  `ProjectGateway` virtuals this leaf ships — a two-line addition there, not a
  task of its own); a Layers-list delete is `editor.panels.layers` (`:364`);
  deleting inside an entered nested scope is `editor.panels.layers` (`:364`);
  reclaiming a deleted cell's owned bytes is `editor.project.gc` (shipped, D13);
  Cut/Copy/Paste is `editor.import.*` (`:414`+). The two items with no editor-side
  owner are library-shaped and go to `tasks/parking-lot.md` as upstream-issue
  candidates, not WBS leaves (see Open questions).

## Decisions

- **D-cells_remove-1 — Delete acts on the WHOLE project selection: cells **and**
  cameras, with no kind filter.**
  `selected_removals` resolves against both `scene::cells` and `scene::cameras`
  and calls the same `remove_content` for either.
  *Rationale:* D7 (`docs/00-design.md:474`) makes cells and cameras "one shape …
  one select tool"; A14 makes a camera a `Content`+`Layer` structurally identical
  to a cell; A17's `pick_targets` already merges them into one selection, so a
  selection *can* hold a camera today. `remove_content` is kind-agnostic. There is
  no camera-delete leaf anywhere in the WBS, so a cells-only Delete would leave
  cameras permanently undeletable. The one integration risk — a canvas looking
  through a deleted camera — is already fail-safed
  (`src/app/canvas_view.cpp:100-122`, "a selection whose shot is gone (GC'd/deleted,
  `cameras()` no longer lists it) … falls back to the free viewport").
  *Alternative rejected:* filter to `PickKind::Cell` because the task is named
  `cells.remove`. That reintroduces exactly the hard-coded kind discrimination A16
  and D-cells_model-8 forbid elsewhere, and it makes Delete's behaviour depend on
  something the user cannot see in the selection outline.
  **No doc delta required.**

- **D-cells_remove-2 — One `Document::remove_content` per object; an N-object
  delete is N journal entries and N undo steps, and that asymmetry is pinned by a
  test rather than engineered away.**
  *Rationale:* `remove_content` is a self-committing `Document` wrapper
  (`document.hpp:131`, impl `arbitrarycomposer/src/runtime/document.cpp:128-153`)
  with no place to stamp a coalesce key — `AppState::next_gesture_key()`
  (`app_state.hpp:88`) can only be applied to a caller-held `transact()`. Accepting
  the library's journal granularity is precisely the call this project already made
  for the *create* side: D-cells_model-7 accepted two entries per insert rather than
  inventing a bespoke atomic composition, and registered the missing
  `create_content_and_attach` as an upstream-issue candidate
  (`tasks/parking-lot.md:92`). The batch verb is the symmetric upstream ask.
  *Alternative rejected:* hand-compose the three teardowns
  (`txn.detach_layer` + `txn.remove(layer)` + `txn.remove(content)`,
  `model.hpp:524,542`) for every target inside one `Document::transact("delete")`,
  which would give one undo step for a multi-delete. It re-implements the library
  verb's body in the editor and thereby takes on its Decision-1 invariant — the
  content binding row must NOT be unbound while the journal holds the removal
  (`document.hpp:120-130`) — as an undocumented dependency that a library change
  could silently break. The `.tji` note also names `remove_content` as the
  mechanism; substituting a private re-implementation is redesign, not refinement.
  *Alternative rejected:* one `Command` whose `apply` loops N `remove_content`
  calls. That breaks `dispatch`'s stated one-command-one-transaction contract
  (`app_state.hpp:103-121`) for no gain — the journal entry count is identical.
  **No doc delta required.**

- **D-cells_remove-3 — Removal targets are resolved inside the edit, from
  `scene::cells` + `scene::cameras`, never from `interact::pick_targets` and never
  from a UI-side snapshot.**
  *Rationale:* two independent forces converge. Structurally, `commands` may not
  depend on `interact` (`scripts/check_levels.py:28`), so the resolver physically
  cannot use `pick_targets`; `commands → scene` is legal and both accessors already
  return the `{content, layer}` pair the library verb needs. Temporally, a layer id
  captured at click time can be invalidated by an interleaved undo/redo, and
  `remove_content` names a specific `(composition, layer)` — a stale layer would
  detach the wrong member. Resolving inside `delete_selection`, on the writer
  thread, against the same pinned generation, removes the window entirely.
  *Alternative rejected:* pass `std::vector<interact::PickTarget>` down from L4,
  which the canvas already assembles each frame. It would be free at the call site
  but pushes the resolution logic above the Catch2 line and makes the correctness
  of a mutation depend on how fresh a UI-thread cache is.
  **No doc delta required.**

- **D-cells_remove-4 — No confirmation dialog; undo is the safety net.**
  Delete fires immediately from both affordances.
  *Rationale:* D15 (`docs/00-design.md:482`) marks exactly one editor operation as
  needing confirmation — *"GC is a confirmed op (**not undoable**)"* — and the
  shipped `draw_gc_modal` (`src/dock/dock.cpp:88-113`) exists for that reason. A
  delete is a journaled transaction, so the stated precondition for a confirm does
  not hold. `docs/00-design.md:371` lists `delete` among the ordinary transactional
  edits alongside move and paint, none of which confirm.
  *Alternative rejected:* a confirm modal above some object-count threshold. A
  threshold is an invented rule with no design-doc basis, and it makes the verb's
  behaviour non-uniform — the thing D7's "one shape" principle exists to prevent.
  **No doc delta required.**

- **D-cells_remove-5 — The chord is `Delete` **or** `Backspace`, handled GLOBALLY
  in L3 `dock` beside `handle_undo_shortcuts`, gated on `can_delete()` plus
  `!io.WantTextInput` and no top-most modal popup.**
  Implemented as `handle_delete_shortcut(ProjectGateway&)` at
  `src/dock/dock.cpp:301-313`, called from the same per-frame block (`:449-456`).
  *Rationale:* Delete is a *project* verb over a *project-level* selection (D19),
  so it must work from wherever the selection is visible — the canvas today, the
  Layers list and Overview once `editor.panels.*` ship. Handling it beside
  undo/redo reuses the one seam the rail item also uses (`can_delete`/
  `delete_selected`), needs no new `CanvasView` plumbing and no new
  `views::CanvasInput` field, and matches D-undo-3's gating shape exactly.
  `Backspace` is included because macOS's Delete key reports as `ImGuiKey_Backspace`;
  omitting it would make the chord unreachable on a MacBook keyboard.
  `docs/00-design.md:499-500` leaves the input map open, which is the licence
  D-undo-3 and D-selection-9 both used.
  *Alternative rejected:* pane-scope it on `in.hovered` beside `Escape`/`Ctrl+A`
  (`src/app/canvas_view.cpp:450-459`, D-selection-9). D-selection-9's reason for
  pane-scoping was to keep `Escape` out of a modal's way — a collision Delete does
  not have, since its only real conflict is text editing, which `WantTextInput`
  addresses precisely and completely. Pane-scoping would also make Delete silently
  dead over the Layers list, which D19 makes a first-class selection surface.
  *Alternative rejected:* leaving the chord global but ungated. `IsKeyChordPressed`
  reads global key state, so a bare `Delete` would be stolen from every
  `InputText` in the app — including the Insert Cell modal's config field. The
  guard is two conditions and is covered by a named e2e assertion.
  **No doc delta required.**

- **D-cells_remove-6 — The rail affordance is an *action* in a retitled **Edit**
  section, disabled (not hidden) when the selection is empty; the gateway carries
  `can_delete()` + `delete_selected()`, mirroring `can_undo()` + `undo()`.**
  `draw_insert_section` (`src/dock/dock.cpp:196-203`) becomes `draw_edit_section`
  holding `Insert Cell…###insert_cell` and `Delete Selected###delete_selected`.
  The virtuals are **non-pure with inert defaults** (`can_delete → false`,
  `delete_selected → 0`), following `insert_kinds` (`dock.hpp:154-157`).
  *Rationale:* D20 (`docs/00-design.md:487`) settles that a one-shot verb is a rail
  **action**, never a fifth `ToolId` — the same ruling it gives Import. Disabled
  rather than hidden keeps the rail's fixed geometry ("home base",
  `docs/00-design.md:452-456`) and makes the gate discoverable. Inert defaults mean
  the six gateway fakes across the existing e2e suites need no churn, exactly the
  argument `dock.hpp:154-156` makes for the insert virtuals.
  *Alternative rejected:* a fifth modal tool (`ToolId::Delete`). D20 forbids it —
  Delete is not a pointer mode.
  *Alternative rejected:* returning a richer outcome struct (a dock-local
  `DeleteSummary` POD like `GcSummary`). Two primitives carry everything both the
  rail and the e2e need; a POD would be a seam with one field.
  **Covered by A12/A13/A16 — no new architecture row.**

- **D-cells_remove-7 — `delete_selection` clears the selection itself rather than
  leaving it to the next frame's `prune`.**
  *Rationale:* it is the honest post-condition (the objects are gone, so no id can
  survive), it makes the behaviour assertable in a headless Catch2 test with no
  frame pump, and it means a delete driven from a surface with no canvas open — a
  future Layers-only layout — still empties the selection. `Selection::prune` at
  `canvas_view.cpp:388-389` stays exactly as shipped: it is the guard for undo and
  GC, which delete does not replace.
  *Alternative rejected:* rely on `prune` alone (D-selection-7's stated design).
  It is correct but only *eventually*, and only when a canvas is drawing; the unit
  test would have to pump a frame to observe it.
  **No doc delta required.**

- **D-cells_remove-8 — `scene::remove_cell` reuses the file-local
  `root_composition` helper in `src/scene/cell.cpp:138-145`; the duplicated helper
  is NOT hoisted to a public `scene` accessor in this leaf.**
  *Rationale:* the helper is duplicated in two TUs (`cell.cpp:138-145` and
  `camera.cpp:460-467`); `remove_cell` is a third caller *in a TU that already has
  one*, so it adds no new duplication. Hoisting would be a public API change to
  `scene` justified by nothing this leaf needs — the simpler abstraction with the
  call sites it actually has. If a fourth caller appears in a third TU, hoist then.
  *Alternative rejected:* add `scene::root_composition(const arbc::Document&)` to
  `scene.hpp` now. A speculative accessor, and a wider blast radius than a 1d leaf
  warrants.
  **No doc delta required.**

- **D-cells_remove-9 — Deleting a cell does not touch `assets/`; owned bytes are
  reclaimed by Clean up.**
  The delete is purely structural: the content record leaves the composition, and
  the library retains its binding row while the journal holds the removal
  (`document.hpp:120-130`), so nothing on disk changes.
  *Rationale:* D13 (`docs/00-design.md:480`, `:313`) makes `assets/` reclamation
  GC's job with the open documents as roots, and `editor.project.gc` already ships
  it as a confirmed op. Reclaiming on delete would also be *wrong*: an undoable
  delete whose bytes were destroyed is not undoable.
  *Alternative rejected:* eagerly sweep the deleted cell's owned bytes. It breaks
  undo and duplicates GC's root-set logic outside GC.
  **No doc delta required.**

## Open questions

(none — all decided.)

Two items are routed to `tasks/parking-lot.md` for human review rather than the
WBS (neither is an "audit" task; each is an upstream-issue candidate a human
decides whether to file against `ruoso/arbitrarycomposer`):

1. **A batch removal verb would collapse an N-object delete to one undo step.**
   `Document::remove_content` (`document.hpp:131`) self-commits and exposes no
   coalesce hook, so N selected objects cost N journal entries and N undo presses
   (D-cells_remove-2). A `remove_contents(std::span<const Removal>)` — or simply
   a `CoalesceKey` parameter on `remove_content` — would make a multi-select delete
   one undo unit. This is the exact mirror of the already-parked
   `create_content_and_attach` ask (`tasks/parking-lot.md:92`). Upstream-issue
   candidate; no editor-side WBS task until the API exists.
2. **Deleted content is retained in memory until the document closes.**
   `remove_content` deliberately keeps the content's binding row and live
   `Content*` alive while the journal holds the removal, and tears them down only at
   document close — or "once `runtime.removed_content_reclaim` lands, the moment the
   removal leaves history" (`document.hpp:123-130`). A long session that repeatedly
   inserts and deletes large rasters therefore grows monotonically even after
   history trims. The named library follow-up already exists upstream; the editor
   cannot fix it host-side. Upstream-issue candidate; no editor-side WBS task.

## Status

**Done** — 2026-07-23.

- `src/scene/ace/scene/cell.hpp` + `src/scene/cell.cpp`: added `scene::remove_cell(Document&, ObjectId content, ObjectId layer) -> bool`, the declared inverse of `add_cell`, reusing the file-local `root_composition` helper and calling `Document::remove_content` as the sole deletion mechanism.
- `src/commands/ace/commands/cells.hpp` + `src/commands/cells.cpp`: added `Removal` struct, `selected_removals(Document&, Registry&, Selection&)` pure resolver, `remove_cell_command`, `DeleteOutcome delete_selection(AppState&)`, and `bool can_delete(const AppState&)`; N-object delete dispatches N commands (one journal entry each, D-cells_remove-2).
- `src/dock/ace/dock/dock.hpp` + `src/dock/dock.cpp`: added `can_delete()`/`delete_selected()` non-pure virtuals to `ProjectGateway` (inert defaults); retitled Insert section to **Edit**; added `Delete Selected###delete_selected` rail action (disabled when `!can_delete()`); added `handle_delete_shortcut` gated on `!WantTextInput && no top-most modal`, called from the undo/redo per-frame block.
- `src/app/ace/app/project_gateway.hpp` + `src/app/project_gateway.cpp`: `AppProjectGateway::can_delete`/`delete_selected` overrides delegating to `commands::can_delete`/`delete_selection`, mutation wrapped in `run_edit` (Constraint 4).
- `CMakeLists.txt`: added `tests/cells_remove_test.cpp` to `ace_tests` source list and `tests/cells_remove_e2e_test.cpp` to `ace_shell_test` source list.
- `tests/cells_remove_test.cpp` (new): 14 `TEST_CASE("cells remove: …")` covering one-entry-per-delete, undo/redo, N-object N-entries, camera delete, mixed selection, stale-id skip, empty-selection no-op, post-delete selection empty, dirty bookkeeping, resolver order-preserving, and byte-invariance round-trip reusing `tests/goldens/cells_insert_nested_64x64.rgba8`.
- `tests/cells_remove_e2e_test.cpp` (new): `IM_REGISTER_TEST(engine, "cells", "delete_rail_and_chord")` — rail enable/disable, click-to-delete, `Delete`/`Backspace` chord, undo, text-input guard (Constraint 9), and camera-deletion look-through fallback.
- `tests/canvas_host_test.cpp`: TSan/ASan anchor — `remove_content` through `CanvasHost::apply_edit` concurrent with render-thread `pin()` walk.
