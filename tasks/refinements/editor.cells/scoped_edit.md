# editor.cells.scoped_edit — Make insert/delete verbs honor the entered composition scope

## TaskJuggler entry

- **Task:** `editor.cells.scoped_edit` (`tasks/00-editor.tji:592-597`, under
  `task cells "Cells & manipulation"` at `:321`).
- **Effort:** `2.5d` · `allocate team`.
- **Depends:** `editor.panels.layers` (`:609`, `complete 100` at `:611`) and
  `!remove` (`editor.cells.remove`, `:571`, `complete 100` at `:574`). Both are
  satisfied. The delete side's live verb is no longer `remove.md`'s single-object
  `remove_cell` — `editor.cells.one_action_one_entry` (`:585`, `complete 100` at
  `:588`) **retired** it (D-one_action_one_entry-4) and replaced it with the batch
  `scene::remove_cells` + `create_content_and_attach` this leaf extends, so the
  effective predecessor for the verbs is `one_action_one_entry`; the `.tji depends`
  names `!remove` because the batch verb subsumes it.
- **Note (`.tji:596`):** "`scene::remove_cells` and `add_cell` currently hard-code
  `root_composition`; make them resolve the active composition
  (`AppState::entered_composition`) instead, so insert and delete operate on the
  entered nested scope when the user has entered one. The scope model and its
  `entered_composition` field ship with `editor.panels.layers`; this leaf owns the
  thin edit-plumbing that consumes it. `src/scene/ace/scene/cell.hpp:157` comment
  already flags this gap. Source-of-debt:
  `tasks/refinements/editor.panels/layers.md` (Deferred WBS). Design:
  `docs/00-design.md` D17, `docs/01-architecture.md` A17."
- **Back-link:** the `.tji` note carries **no** `Refinement:` pointer yet (this
  leaf was registered as tech debt by `editor.panels.layers`, not written ahead as
  a flat interim path). This refinement lands at
  **`tasks/refinements/editor.cells/scoped_edit.md`** per the area-subdir layout
  (`tasks/refinements/README.md:9-18`), beside its `editor.cells` siblings; the
  closer appends the back-link to the note and adds `complete 100` after
  `allocate team` (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the
  `.tji` here.
- **Source of debt:** registered by `editor.panels.layers` as a deferred WBS leaf
  (`tasks/refinements/editor.panels/layers.md`, "Deferred WBS work"): *"make the
  existing insert/delete verbs honor the entered composition: `scene::remove_cells`
  / `add_cell` resolve the **active** composition instead of hard-coding
  `root_composition` (the `src/scene/ace/scene/cell.hpp:157` comment already flags
  "Deleting from an entered/isolated nested scope is `editor.panels.layers`'" — that
  leaf provides the scope; the thin edit-plumbing that consumes it rides here)."*
- **Downstream dependents:** none in the `.tji` — nothing `depends` on this leaf.
  `editor.panels.overview` (`:616`) will insert and manipulate cells too, but it
  consumes the same scope-aware verbs this leaf produces (no new plumbing there).

## Effort estimate

**2.5 days.** Every seam this leaf needs is shipped; the work is threading one
optional through four levels and confining one assembly adapter, plus the tests
that pin scope honoring end to end.

- The **fail-safe scope resolver already exists**: `scene::active_composition(const
  arbc::Document&, std::optional<arbc::ObjectId> entered)`
  (`src/scene/ace/scene/cell.hpp:255-256`, impl `src/scene/cell.cpp:366-379`)
  returns `*entered` when it names a **live** composition and `root_composition`
  otherwise — the exact substitution the two edit verbs need, with the
  GC'd/undone-away/foreign fallback (D-look_through-7) already written and pinned.
- The **scope state is shipped**: `AppState::entered_composition()`
  (`src/commands/ace/commands/app_state.hpp:149-150`, field `:252`) is the
  project-level `std::optional<arbc::ObjectId>` (nullopt = Root) `editor.panels.layers`
  landed; the Layers panel is its sole mutator (`src/app/layers_panel.cpp:83`).
- The **composition-parameterised cell walk is shipped**: `scene::cells(const
  arbc::Document&, const arbc::Registry&, arbc::ObjectId composition)`
  (`src/scene/cell.cpp:336-346`) already re-roots the list to any composition — the
  reader the pick adapter and the removal resolver switch to while entered.
- The **edit verbs are shipped and root-only in exactly one spot each**:
  `scene::add_cell` forces `composition = root_composition(*state)` at
  `src/scene/cell.cpp:252`; `scene::remove_cells` at `:284`, with the membership
  gate that rejects nested-scope layers at `:296-304`. Each is a one-line change to
  `active_composition`.
- The **pick set has one chokepoint**: `interact::pick_targets(const arbc::Document&,
  const arbc::Registry&)` (`src/interact/pick_targets.cpp:16-39`, decl
  `src/interact/ace/interact/pick.hpp:264-265`) is A17's single `interact → scene`
  assembly adapter — the one place cells and cameras are merged into the pickable
  list, so confining it confines every pointer/keyboard/drag path at once.
- The **command + gateway moulds are shipped**: `insert_cell_command`
  (`src/commands/cells.cpp:14-27`), `selected_removals` (`:29-63`),
  `remove_cells_command` (`:65-79`), `delete_selection` (`:125-142`), and the
  `AppProjectGateway::insert_cell` / `delete_selected` overrides
  (`src/app/project_gateway.cpp:343-362`) already funnel through `run_edit`.
- The **canvas half is already wired**: `editor.canvas.isolation_scope` (commit
  `6bd4a33`) mirrors the same `entered_composition` down the render `set_scope`
  channel (`src/app/canvas_view.cpp:187-199`) and bakes the out-of-scope dim into
  the L2 frame (`src/render/dim_scrim.cpp:53-91`, D-isolation_scope-2). This leaf is
  the **interactive** half of the same rule — it never touches the render path.

New code is a defaulted `entered` parameter on two `scene` verbs, one `commands`
resolver and two commands, one `interact` adapter, three L4 call-site reads, and
the tests. No new component, no new DAG edge, no new external dependency, no
libarbc change. One doc delta (D29) turns D17's view-only isolation scope into an
editing-confinement promise.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.panels.layers`** (`tasks/refinements/editor.panels/layers.md`) — the
  project-level `AppState::entered_composition` (`std::optional<arbc::ObjectId>`,
  nullopt = Root, transient UI-thread session state, D-layers-3), the
  `scene::active_composition` fail-safe resolver (`cell.hpp:255`), the
  `scene::cells(document, registry, composition)` overload, and the breadcrumb
  (`composition_path`). This leaf consumes the scope the panel produces; it adds no
  new scope state and no new mutation of it.
- **`editor.cells.one_action_one_entry`**
  (`tasks/refinements/editor.cells/one_action_one_entry.md`) — the batch verbs this
  leaf extends: `scene::add_cell` on `Document::create_content_and_attach` (one
  journal entry, D-one_action_one_entry-1) and `scene::remove_cells(span<const
  CellRemoval>)` on `Document::remove_contents` (one entry for the whole batch,
  D-one_action_one_entry-2/-3). The single-object `remove_cell` +
  `remove_cell_command` are **retired** (D-one_action_one_entry-4); this leaf must
  not resurrect them.
- **`editor.cells.remove`** (`tasks/refinements/editor.cells/remove.md`) — the
  errors-are-values discipline (a stale/non-member removal is **skipped**, opens no
  transaction, Constraint 5), the `Selection`-driven delete on the project-level
  selection, and the two `ProjectGateway` delete virtuals whose signatures this leaf
  leaves untouched. Its **Constraint 12** ("Root composition only … entered/isolated
  nested scope is `editor.panels.layers`'") is exactly the constraint this leaf now
  lifts.
- **`editor.cells.model` / `editor.cells.selection`** — the `commands::Command` /
  `dispatch` one-command-one-transaction shape (`app_state.hpp`), the A16
  no-kind-allowlist principle, the `commands` may-not-include-`interact` rule
  (`scripts/check_levels.py`), and the project-level `commands::Selection`.
- **`editor.canvas.isolation_scope`**
  (`tasks/refinements/editor.canvas/isolation_scope.md`) — the **visual** half of
  the scope: the L2 render dim of the out-of-scope complement
  (D-isolation_scope-1/-2), the project-level scope fed to every canvas
  (D-isolation_scope-3), and the focus region = the wrapping nested cell's placed
  bounds (D-isolation_scope-4, `scene::composition_focus_quad`,
  `src/scene/focus_quad.cpp:84-127`). This leaf pairs its interactive lock with that
  visual lock so out-of-scope objects are dimmed **and** unpickable; it depends on
  no render code and does not regress the scrim.

**Pending (owned here):** nothing. Every predecessor is `complete 100`; this leaf
adds no dependency of its own and blocks nothing downstream.

## What this task is

Make the composition the user has **entered** the target of every structural edit.
`editor.panels.layers` shipped the scope (and re-rooted the list);
`editor.canvas.isolation_scope` shipped the dim. Today insert and delete still
hard-wire the root composition, so entering a nested cell changes what you *see* but
not what you *edit* — an insert lands in the root behind the scrim, a delete of an
in-scope cell silently no-ops (the root-only resolver never finds its layer). This
leaf closes that gap on both the edit and the interaction side.

1. **L1 `scene`** — thread `std::optional<arbc::ObjectId> entered` into the two
   verbs and resolve the target composition through `active_composition` instead of
   `root_composition`:
   - `add_cell(Document&, const Registry&, kind_id, config, placement,
     std::optional<arbc::ObjectId> entered)` — `src/scene/cell.cpp:252` becomes
     `composition = active_composition(document, entered)` (resolved off the
     already-pinned `*state` to avoid a redundant pin — a `DocRoot`-taking sibling of
     the fail-safe, or the inlined body). Everything else (factory-first,
     one-entry `create_content_and_attach`) is unchanged.
   - `remove_cells(Document&, span<const CellRemoval>, std::optional<arbc::ObjectId>
     entered)` — `src/scene/cell.cpp:284` becomes `composition =
     active_composition(...)`; the membership gate at `:296-304` then validates each
     removal against the **entered** composition, so an in-scope layer passes and a
     root layer is skipped (Constraint 5, unchanged mechanism).
2. **L1 `commands`** — thread the scope from `AppState::entered_composition()`:
   - `selected_removals(Document&, Registry&, Selection&, std::optional<arbc::ObjectId>
     entered)` walks `scene::cells(document, registry, active_composition(document,
     entered))` (`cells.cpp:38`) so a selected in-scope cell's layer is *found* while
     entered; without this the resolver silently drops it.
   - `insert_cell_command` and `remove_cells_command` gain an `entered` argument
     captured by value into their writer-thread closures and forwarded to the scene
     verb.
   - `delete_selection(AppState&)` reads `state.entered_composition()` at the same
     point it reads `state.selection()` (`cells.cpp:128-129`) and passes it to both
     `selected_removals` and `remove_cells_command`.
3. **L1 `interact`** — `pick_targets(Document&, Registry&, std::optional<arbc::ObjectId>
   entered = std::nullopt)`: when `entered` resolves to a **non-root** live
   composition, walk only `scene::cells(document, registry, active_composition(...))`
   and append **no** cameras; at Root it is byte-identical to today (all root cells +
   all cameras). One change confines click-select, marquee, gizmo-drag, select-all,
   and frame-selection.
4. **L4 `app`** — three UI-side reads of `entered_composition()` at the call sites:
   `AppProjectGateway::insert_cell` (`project_gateway.cpp:343`, into
   `insert_cell_command`); the canvas hit-test and nav-aid `pick_targets` builds
   (`canvas_view.cpp:265`, `:340`) and the frame-selection build
   (`project_gateway.cpp:376`) pass `state_.entered_composition()`. The gateway
   delete path is unchanged (the scope is read inside `delete_selection`).

It deliberately does **not** ship: any change to the render scrim or `set_scope`
channel (`editor.canvas.isolation_scope` owns them); a new `ProjectGateway` virtual
or any `dock` awareness of scope (the virtuals' signatures are untouched, Decision 5);
scoped **paint** or **import** (those verbs are not yet built; when they land they
consume the same scope-aware seam); an Overview-driven scoped insert
(`editor.panels.overview`, `:616`, reuses these verbs); and any persistence of the
scope (it is transient session state, D15/D-layers-3).

## Why it needs to be done

Entering a nested composition is currently a **lie of omission**: the canvas, list,
and overview show the child and dim everything else (D17), but the edit verbs still
act on the root. Three concrete breakages fall out of that split:

- An **insert while entered** mints a cell in the *root* composition — placed behind
  the scrim, invisible in the re-rooted list, and semantically in the wrong
  container. The user asked to add a cell *to the thing they entered*.
- A **delete while entered** silently does nothing: `selected_removals` walks the
  root-only `scene::cells(document, registry)` (`cells.cpp:38`), never finds the
  in-scope cell's layer, and returns an empty batch — or, if the layer id were found
  another way, `remove_cells`' root-membership gate (`cell.cpp:296-304`) would reject
  it. Either path is a no-op the user reads as a bug.
- **Pointer/keyboard/drag** can still reach *out-of-scope* objects: a click, marquee,
  or select-all grabs cells the scrim says are locked, and a gizmo drag then moves
  them — the visual isolation is contradicted the instant the user interacts.

D17 promised "isolation scope"; today it is isolation of the *view* only. This leaf
makes the scope mean what "isolation" says — you edit, and can only touch, what you
entered — closing the loop `editor.panels.layers` and `editor.canvas.isolation_scope`
opened.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **D17 — Nested scope** (`docs/00-design.md:484`): *"A nested-composition cell can
  be **expanded** (peek children, still editing parent) or **entered** (double-click
  → isolation scope: canvas/list/overview show the child, outside dims, breadcrumb
  climbs out). Select ≠ expand ≠ enter."* D17 fixes the **view** semantics of
  entering; it is silent on where an insert lands or what a delete removes. Turning
  "isolation scope" into an **editing**-confinement promise is the gap this leaf
  fills — recorded as **D29** (doc delta below), which extends D17.
- **D7 — Manipulation model** (`:474`): cells and cameras are "one shape … one select
  tool", which is why the pick confinement must reason about the *composition
  membership* of a target, not its kind.
- **D15 — Undo = library transactions** (`:482`): the scope is **transient session
  state** ("like scroll position"), never a transaction — entering, climbing, and
  the scope's fail-safe degradation add zero journal entries and never dirty the
  document. The edits themselves stay ordinary transactions.
- **D19 — Project-scoped state** (`:486`): the selection and the entered scope are
  project-level, shared by every canvas and panel — so the confinement is a project
  rule, uniform across surfaces, not a per-canvas one.
- **§9 Undo/redo** (`:369-378`): names `place` and `delete` among the transactional
  edits this leaf reroutes; their transaction shape is unchanged, only their target
  composition.

**Governing architecture rows:**

- **A17** (`docs/01-architecture.md:432`) — hit-testing lives in L1 `interact`,
  split into a primitive-only pick **policy** and **one** `interact → scene`
  **assembly** adapter, `pick_targets`. That single adapter is the chokepoint this
  leaf confines; the policy core (`click_selection`/`marquee`/`hit_cell`) is
  untouched, so the confinement is expressed once.
- **§8 levelization DAG** (`:308-344`) — relevant edges: `commands → scene`,
  `interact → scene` (already exercised by `pick_targets.cpp`, comment `:7-12`),
  `dock → {dockmodel, views}` (never `scene`/`commands`). The L1-core rule
  (`:342-344`): no `scene`/`interact`/`commands` file may include ImGui/GL/SDL.
- **A8/A9** (`:299-300`) — the UI-agnostic L1 core is the testable bulk (Catch2), and
  every leaf carries the layered DoD §9 instantiates.
- **A14** (`:305`) — a camera is a `Content` + a `Layer`; it is *not* a member of a
  nested composition, which is why cameras drop out of the pick set while entered
  (Decision 4).

**libarbc API surface** (headers under `build/dev/_deps/arbc-src/src/`; consumed via
FetchContent):

- `arbc::Document::create_content_and_attach(...)` and `remove_contents(span<const
  Removal>)` (`runtime/arbc/runtime/document.hpp`) — both take an explicit
  **`composition`** argument; this leaf changes *which* composition the editor hands
  them, nothing about the calls themselves. `Document::pin()` gives the lock-free
  snapshot every resolution reads. `find_composition` / `find_layer` /
  `for_each_layer_in` (the model accessors) back `active_composition` and the
  membership gate.

**Editor seams this leaf extends:**

- `scene::add_cell` — decl `src/scene/ace/scene/cell.hpp:126-128` (comment `:112-125`),
  impl `src/scene/cell.cpp:236-267`, root forced at `:252`.
- `scene::remove_cells` — decl `cell.hpp:162` (comment `:139-161`, the flagged gap at
  `:157`), impl `cell.cpp:269-316`, root forced at `:284`, membership gate `:296-304`.
- `scene::active_composition` — decl `cell.hpp:255-256`, impl `cell.cpp:366-379`.
- `scene::cells(...)` root overload `cell.cpp:318-334`; composition overload
  `cell.cpp:336-346`. `scene::CellRemoval` — `cell.hpp:134-137`.
- `commands::insert_cell_command` — `src/commands/cells.cpp:14-27`;
  `selected_removals` `:29-63` (root walk at `:38`); `remove_cells_command` `:65-79`;
  `delete_selection` `:125-142` (selection read at `:128-129`); `commands::Removal`
  (two-field) in `src/commands/ace/commands/cells.hpp`.
- `interact::pick_targets` — `src/interact/pick_targets.cpp:16-39` (cells `:23`,
  cameras `:24`/`:33-37`), decl `src/interact/ace/interact/pick.hpp:264-265`.
- `AppState::entered_composition()` — `src/commands/ace/commands/app_state.hpp:149-150`,
  field `:252`; `selection()` `:138-139`. Sole mutator: `src/app/layers_panel.cpp:83`.
- Canvas / gateway call sites: `AppProjectGateway::insert_cell`
  `src/app/project_gateway.cpp:343-347`; `delete_selected` `:353-362` (→
  `delete_selection` inside `run_edit` at `:360`); `frame_selection`
  `pick_targets` build `:376`. Canvas hit-test / nav-aid `pick_targets` builds
  `src/app/canvas_view.cpp:265`, `:340`; the shipped scope→render mirror `:187-199`.
- The visual companion (not touched): `scene::composition_focus_quad`
  `src/scene/ace/scene/cell.hpp:295-296` / `src/scene/focus_quad.cpp:84-127`;
  `composite_isolation_dim` `src/render/dim_scrim.cpp:53-91`; consumers
  `src/render/canvas_renderer.cpp:171-202`, `src/render/render.cpp:137-140`.

**Predecessor refinements:** `tasks/refinements/editor.panels/layers.md`,
`tasks/refinements/editor.cells/one_action_one_entry.md`,
`tasks/refinements/editor.cells/remove.md`,
`tasks/refinements/editor.canvas/isolation_scope.md`.

**Test rigs:** `ace_tests` source list `CMakeLists.txt:268-285`; `ace_shell_test`
source list `CMakeLists.txt:325-348`; existing scope suites
`tests/isolation_scope_test.cpp` (`:284`) and `tests/isolation_scope_e2e_test.cpp`
(`:347`); cell suites `tests/cell_model_test.cpp` (`:271`),
`tests/cells_remove_test.cpp` (`:276`). Goldens under `tests/goldens/` compared via
`ace_test::compare_golden`, rendered by `render::render_document_srgb8`
(`src/render/render.cpp:22`).

## Constraints / requirements

1. **Levelization (`check_levels` clean).** No new component, no new DAG edge.
   `scene` gains no new dependency — `active_composition` and the `cells` overload
   already live in the same TU. `interact` gains no `commands` include: the `entered`
   optional arrives as a plain `std::optional<arbc::ObjectId>` parameter from L4; the
   `interact → scene` edge is already declared and exercised
   (`pick_targets.cpp:7-12`). `commands` gains no `interact` include. `dock` gains
   no `scene`/`commands` include (Decision 5). Nothing in `src/scene/`,
   `src/interact/`, or `src/commands/` gains an ImGui/GL/SDL include. No entry in
   `scripts/check_levels.py` changes.
2. **The scope is resolved through `active_composition`, everywhere, with its
   fail-safe intact.** Every consumer (`add_cell`, `remove_cells`,
   `selected_removals`, `pick_targets`) routes the `entered` optional through
   `active_composition` (`cell.cpp:366-379`), so a scope naming a
   GC'd/undone-away/foreign composition degrades to **Root** editing — never a crash,
   never a phantom un-editable composition. This is the same fallback the render's
   `composition_focus_quad` and the Layers panel already use (D-look_through-7 /
   D-isolation_scope-4).
3. **Scope is threaded by value; no writer-thread read of live `AppState` scope
   state.** `entered_composition` is UI-thread session state (`app_state.hpp:141-148`).
   Insert reads it on the UI thread at command construction
   (`project_gateway.cpp:343`) and captures it into the writer-thread closure. Delete
   reads it inside `delete_selection` at the same point it already reads
   `state.selection()` (`cells.cpp:128-129`, the D-cells_remove-3 resolution point)
   and marshals it into the command by value. No new field is read from a background
   thread that a UI frame concurrently mutates.
4. **Root behavior is byte-for-byte unchanged.** With `entered == nullopt` (or a
   scope that fails the fail-safe), `add_cell`/`remove_cells`/`selected_removals`
   resolve to `root_composition` exactly as today, and `pick_targets` returns the
   identical all-cells-plus-all-cameras list. The defaulted `entered = std::nullopt`
   on `pick_targets` means unchanged call sites keep root behavior; the existing
   `isolation_scope` and `cells_remove` suites must stay green untouched.
5. **Delete honors the scope on both rails: resolution and membership.**
   `selected_removals` walks the *active* composition's cells (so an in-scope cell's
   layer is found), and `remove_cells`' membership gate validates against the *active*
   composition (so a stray out-of-scope id — if one ever reaches the batch — is
   skipped, opening no transaction). Deleting a selection that, post-confinement,
   holds only in-scope cells removes exactly those, in one journal entry
   (D-one_action_one_entry-2), undoable whole.
6. **Insert lands in, and only in, the active composition.** The minted cell attaches
   to `active_composition(document, entered)`; it appears in `scene::cells(document,
   registry, entered_composition)` and in the re-rooted Layers list, and is **not** in
   the root cell list while entered.
7. **Pointer/keyboard/drag confinement is expressed once, at `pick_targets`.** No
   per-gesture filtering in `click_selection`/`marquee`/`hit_cell` (A17: the policy
   core names no `scene` type). The single adapter drops out-of-scope cells and all
   cameras while entered; select-all, marquee, body-click, gizmo-hit, and
   frame-selection all inherit the confined set because they read the same
   per-frame `pick_targets`.
8. **Cameras are not pickable while entered.** A camera is `Content`+`Layer` at the
   project level (A14/D19), not a member of a nested composition; the scrim dims it
   and `remove_cells` would skip it, so leaving it selectable would be an
   "select-but-can't-act" inconsistency. While entered, `pick_targets` appends no
   camera; the user climbs the breadcrumb to Root (one click) to manipulate cameras.
   This is an accepted consequence, recorded in D29.
9. **No render, `dock`, or gateway-signature change.** The `set_scope` channel, the
   dim scrim, and the `ProjectGateway` virtuals (`insert_cell`, `can_delete`,
   `delete_selected`) are untouched — scope resolution lives entirely in L1/L4.
10. **The edits stay ordinary transactions (D15).** Scoped insert is still one entry
    via `create_content_and_attach`; scoped batch delete is still one entry via
    `remove_contents`; entering/climbing/fail-safe add none. Undo/redo of a scoped
    edit restores on the same `ObjectId`s in the same (now nested) composition.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate`
green (check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — primary structural assertion.** Asserted by
  inspection and by the lint: `src/interact/pick_targets.cpp` gains only a
  `scene::active_composition` / `scene::cells(…, composition)` call and **no**
  `ace/commands` include; `src/commands/cells.cpp` gains **no** `ace/interact`
  include; `src/scene/` gains no new include at all; `src/dock/` is unchanged; no
  ImGui/GL/SDL crosses into L1. No entry in `scripts/check_levels.py` changes.

- **L1 logic — Catch2 unit** (`tests/cells_scoped_edit_test.cpp`, new file added to
  the `ace_tests` source list at `CMakeLists.txt:268-285`; naming follows
  `tests/isolation_scope_test.cpp` and `tests/cells_remove_test.cpp`, i.e.
  `TEST_CASE("cells scoped edit: …")`). Over a probe document with a nested
  composition (reuse the `isolation_scope_test.cpp` fixture builder):
  - **Insert lands in the entered composition (Constraint 6):** `add_cell(...,
    entered=child)` produces a cell present in `scene::cells(doc, reg, child)` and
    **absent** from `scene::cells(doc, reg)` (root); `entered=nullopt` lands it in
    root, exactly as today. Journal depth grows by **1** either way.
  - **Insert fail-safe (Constraint 2):** `add_cell(..., entered=<stale/foreign id>)`
    lands in root (the cell is in the root list), adding one entry — the vanished
    scope degrades, it never refuses or crashes.
  - **Delete removes the in-scope cell (Constraint 5):** with a child cell selected
    and `entered=child`, `delete_selection` removes exactly it (one entry, gone from
    `scene::cells(doc, reg, child)`), and `undo` restores it on its same `ObjectId`
    into the child.
  - **Delete of a root id while entered is a no-op (Constraint 5):** a selection
    holding a *root* cell id with `entered=child` resolves to zero removals (the
    resolver walks the child list) and/or is skipped by the membership gate; journal
    depth and revision are unchanged.
  - **`selected_removals` is scope-parameterised and order-preserving:** over an
    entered scope it returns `{content, layer}` pairs from the child composition in
    selection order; over Root it matches today's behavior byte-for-byte.
  - **`pick_targets` confinement (Constraints 4/7/8):** `pick_targets(doc, reg,
    entered=child)` returns exactly the child composition's cells, **no** cameras and
    **no** root cells; `pick_targets(doc, reg, nullopt)` is identical to the
    two-argument overload (all root cells + all cameras). A fail-safe scope resolves
    to the root set.
  - **Dirty / transient (Constraint 10, D15):** resolving the scope, degrading a
    stale scope, and building a confined pick set add **zero** journal entries and
    leave `is_dirty()` untouched; a scoped insert/delete dirties exactly as an
    unscoped one does.

- **Rendered output — golden.** One new golden `tests/goldens/cells_scoped_insert_64x64.rgba8`
  proves an insert *while entered* lands its pixels **inside the nested child**, not
  in root space: build the nested-composition probe, enter the child, insert a
  distinctly-colored cell via `add_cell(..., entered=child)`, render the whole
  document at 64×64 through `render::render_document_srgb8`, and
  `ace_test::compare_golden`. Because the child is placed by its wrapping nested
  cell's affine, a cell that (incorrectly) landed in root would render at a different
  location/scale — the golden is the byte-exact discriminator between "landed in the
  scope" and "landed in root". Rendered with the scrim **off** (no entered scope on
  the render path) so the golden pins placement, not dimming.

- **UI e2e — ImGui Test Engine** (extend `tests/isolation_scope_e2e_test.cpp`
  (`CMakeLists.txt:347`) with a new `IM_REGISTER_TEST(engine, "isolation_scope",
  "scoped_edit")`, reusing its enter/breadcrumb driving and gateway/edit-runner
  wiring; a separate `tests/cells_scoped_edit_e2e_test.cpp` is acceptable if the
  fixture is cleaner). Assertions on model state, never pixels:
  - **enter → insert lands in child:** double-click a nested cell to enter, invoke the
    rail Insert; the new cell appears in `scene::cells(doc, reg, entered)` and the
    re-rooted Layers list, and is absent from the root cell list;
  - **out-of-scope click is inert (Constraints 7/8):** while entered, a canvas click on
    a dimmed root cell leaves `selection().empty()` — the object the scrim shows locked
    cannot be grabbed; a marquee over the out-of-scope region selects nothing;
  - **select-all is confined:** `Ctrl+A` while entered selects only the child's cells;
  - **delete is confined:** with the child's cells selected, `Delete` removes them
    (they leave `scene::cells(doc, reg, entered)`) and `Ctrl+Z` restores them into the
    child;
  - **climb restores root editing:** clicking the `Root` breadcrumb crumb makes a
    subsequent insert land in root and root cells pickable again — the confinement is
    strictly scoped to being entered.

- **Threading (ASan/TSan).** One case appended to `tests/canvas_host_test.cpp`: drive
  a scoped `add_cell`/`remove_cells` through `CanvasHost::apply_edit` (scope captured
  by value into the closure) while the render thread walks the same document over
  `pin()` **and** consumes the `set_scope` channel that `isolation_scope` feeds. This
  pins Constraint 3 — the edit's captured scope id shares no mutable state with the
  render-thread scope generation — under the existing ASan and TSan lanes; no new
  lane, no new suppression. (No genuinely new threading surface: the scope is a value,
  and the transaction machinery is the already-covered single-writer seam.)

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed lines;
  clang-format + build clean. Tests ship with the task.

- **Deferred WBS work.** **None.** Every out-of-scope item has a scheduled owner or
  needs no task: an Overview-driven scoped insert is `editor.panels.overview`
  (`.tji:616`, which consumes these same verbs — no plumbing of its own); scoped
  paint/import land when `editor.paint.*` / `editor.import.*` build those verbs (they
  read the same `entered_composition`, no seam owed here); the group-transform gizmo
  (`transform_cells_command`, `cells.cpp:81-113`) is **already** scope-safe because it
  validates each layer against the live pin and operates on the (now-confined)
  selection — no change and no new task. `scene::reorder_cell` already takes an
  explicit `composition` and the Layers panel already passes the active one.

## Decisions

- **D-scoped_edit-1 — Both edit verbs take a `std::optional<arbc::ObjectId> entered`
  and resolve the target composition through `scene::active_composition`, not a
  pre-resolved `ObjectId`.**
  `add_cell` and `remove_cells` gain the optional and swap `root_composition(*state)`
  (`cell.cpp:252`, `:284`) for the fail-safe resolver.
  *Rationale:* passing the *raw scope* and resolving it **inside** the verb, against
  the pinned generation the mutation lands on, re-validates the scope at apply time —
  a scope that went stale between the click and the writer-thread apply degrades to
  Root there and then (Constraint 2), which a value resolved earlier at L4 could not
  do. `active_composition` already encapsulates exactly that fail-safe
  (`cell.cpp:366-379`), so the change is one line per verb and reuses shipped,
  tested logic.
  *Alternative rejected:* pass `AppState&` into the `scene` verbs so they read
  `entered_composition` themselves. Illegal — that is a `scene → commands` back-edge
  (`scripts/check_levels.py`), and it would drag UI-thread session state onto the
  writer thread.
  *Alternative rejected:* resolve `active_composition` at L4 and pass a concrete
  `arbc::ObjectId composition` down. It moves the fail-safe above the pin the
  mutation uses, so a scope invalidated between resolution and apply would name a dead
  composition; and it duplicates the resolver's live-composition check at the call
  site.
  **Doc delta: D29 (see below).**

- **D-scoped_edit-2 — The scope is read from `AppState::entered_composition()` at the
  L4/`commands` boundary and threaded by value; it is never read from a writer-thread
  closure as a live `AppState` field.**
  Insert captures it at command construction on the UI thread
  (`project_gateway.cpp:343`); delete reads it inside `delete_selection` at the
  D-cells_remove-3 resolution point (`cells.cpp:128-129`), beside `state.selection()`.
  *Rationale:* `entered_composition` is transient UI-thread session state
  (`app_state.hpp:141-148`); capturing the plain optional by value into the
  writer-thread lambda gives the closure an immutable snapshot, exactly as placement
  and the removal list are already marshaled by value. Delete already reads
  `state.selection()` at that spot with the same threading posture, so the scope read
  is byte-for-byte the established pattern — no new cross-thread field access.
  *Alternative rejected:* have the writer-thread closure read `state.entered_composition()`
  directly. It reads UI-thread session state from the writer thread — a race the
  by-value capture avoids for free.
  **No doc delta required.**

- **D-scoped_edit-3 — `selected_removals` walks the ACTIVE composition's cells, not
  the root-only list; this is a correctness requirement, not an optimization.**
  It reads `scene::cells(document, registry, active_composition(document, entered))`
  in place of `scene::cells(document, registry)` (`cells.cpp:38`).
  *Rationale:* while entered, an in-scope cell is **not** in the root cell list, so
  the shipped root-only resolver never finds its layer and returns an empty batch —
  the "delete does nothing" breakage. The composition-parameterised overload already
  ships (`cell.cpp:336-346`); switching the resolver's reader to it is the whole fix.
  Cameras stay resolved from `scene::cameras(document)` for the Root path; while
  entered, the pick confinement (D-scoped_edit-4) keeps camera ids out of the
  selection, and `remove_cells`' membership gate is the backstop that skips any that
  slip through.
  *Alternative rejected:* leave the resolver root-only and rely on `remove_cells`'
  membership gate alone. The gate skips *out-of-scope* ids but cannot *find* an
  in-scope layer the root walk never enumerated — it is the wrong half of the
  problem.
  **No doc delta required.**

- **D-scoped_edit-4 — Pointer/keyboard/drag confinement is expressed once, at the
  single `interact::pick_targets` assembly adapter, via a defaulted `entered`
  parameter.**
  When `entered` resolves to a non-root live composition, `pick_targets` walks only
  the child's cells and appends no cameras; at Root it is unchanged. The canvas
  hit-test, nav-aid, and frame-selection builds pass `state_.entered_composition()`.
  *Rationale:* A17 (`docs/01-architecture.md:432`) makes `pick_targets` the **one**
  place the pickable set is assembled — the policy core (`click_selection`, `marquee`,
  `hit_cell`, `selected_extent`) names no `scene` type and operates over whatever span
  it is handed. Confining the adapter therefore confines *every* pointer, keyboard,
  and drag path — click-select, marquee, select-all, gizmo body-click, gizmo-hit,
  frame-selection — with no per-gesture logic and no policy-core change, and it does so
  with the same target-set the delete resolver now sees, so selection and deletion
  agree. `interact → scene` is already legal and exercised; the `entered` optional is a
  plain parameter, so no `interact → commands` edge appears.
  *Alternative rejected:* filter inside each policy call (`click_selection` /
  `marquee` / `hit_cell`). It scatters the rule across N call sites, reintroduces the
  per-site discrimination A17 exists to centralize, and would need each of them to
  learn about `scene` compositions, which the primitive-only convention forbids.
  *Alternative rejected:* confine cells but keep cameras pickable while entered. It
  leaves a "select the camera, but delete skips it and the scrim dims it"
  inconsistency (Constraint 8) and lets a dimmed object be grabbed — contradicting the
  visual lock the scrim asserts.
  **Doc delta: D29 (records the camera-unpickable-while-entered consequence).**

- **D-scoped_edit-5 — The `ProjectGateway` virtuals keep their signatures; `dock`
  stays ignorant of the scope.**
  `insert_cell`, `can_delete`, and `delete_selected` are unchanged; the scope is read
  and threaded entirely inside L1 `commands` and L4 `app`.
  *Rationale:* `dock` may not include `ace/scene` or `ace/commands`
  (`scripts/check_levels.py`), and the entered scope is an `AppState` concern the
  gateway already owns. Keeping the virtuals fixed means the rail item, the
  Delete/Backspace chord, and every gateway fake across the existing e2e suites need
  no churn — the scope confinement is invisible above the gateway, exactly where D19
  says project state lives.
  *Alternative rejected:* add a `current_scope()` query virtual to `ProjectGateway`.
  Nothing in `dock` needs to *know* the scope — only L4/L1 consume it — so the virtual
  would be a seam with no reader.
  **No doc delta required.**

- **D-scoped_edit-6 — Every scope consumer degrades to Root through the one
  `active_composition` fail-safe, so a vanished scope can never strand the user in an
  un-editable phantom composition.**
  `add_cell`, `remove_cells`, `selected_removals`, and `pick_targets` all resolve the
  raw `entered` optional through `active_composition`.
  *Rationale:* the scope can go stale mid-session (the entered nested cell is deleted,
  undone away, or GC'd). A single, shared fallback point means the edit side degrades
  identically to how the render side already does (`composition_focus_quad` →
  "no dim", D-isolation_scope-4) and the list side does (Layers `active_composition`,
  D-look_through-7) — so canvas dimming, list rooting, and edit targeting can never
  disagree about whether the scope is live. Pinned by the insert/pick fail-safe cases.
  *Alternative rejected:* let each consumer branch on `entered.has_value()` itself. It
  duplicates the live-composition check four times and risks the four drifting — the
  precise failure the shared resolver prevents.
  **No doc delta required.**

### Doc delta — D29 (`docs/00-design.md`)

D17 (`docs/00-design.md:484`) fixes only the **view** semantics of entering a
composition (canvas/list/overview show the child, outside dims, breadcrumb). It is
silent on editing, which left the gap this leaf closes. A new row **D29** records the
editing-confinement promise, extending D17:

> **D29 | Scoped editing** | Entering a composition (D17) confines **editing**, not
> just the view: while a composition is entered, an **insert lands in it**, and
> **delete, selection, and all pointer/keyboard/drag interaction act only on cells
> within it** — out-of-scope objects are **dimmed** (D17's view half,
> `editor.canvas.isolation_scope`) **and interactively locked** (`editor.cells.scoped_edit`).
> **Cameras** are project-level viewport tools (D19/A14), not members of a nested
> composition, so they are **not pickable while entered** — climb the breadcrumb to
> Root to manipulate them. A scope that names a **vanished** composition
> (GC'd/undone-away/foreign) **degrades to Root** editing (the same fail-safe the dim
> and the list use). The edits stay ordinary transactions (D15); entering, climbing,
> and the fail-safe add no journal entry. Realized by `editor.cells.scoped_edit`.

Same-commit rule: this row lands in the closer's commit with the code. No
architecture (`A<n>`) delta is needed — no new component, no new DAG edge, no new
external dependency (Constraint 1).

## Open questions

(none — all decided.)

## Status

**Done** — 2026-07-30.

- Threaded `std::optional<arbc::ObjectId> entered` into `scene::add_cell` and `scene::remove_cells`; both now resolve the target composition through `scene::active_composition` instead of hard-coding `root_composition` (`src/scene/cell.cpp`, `src/scene/ace/scene/cell.hpp`).
- Extended `commands::selected_removals`, `insert_cell_command`, `remove_cells_command`, and `delete_selection` to carry the entered scope by value from `AppState::entered_composition()` at the UI-thread boundary (`src/commands/cells.cpp`, `src/commands/ace/commands/cells.hpp`).
- Confined `interact::pick_targets` with a defaulted `entered` parameter; while entered, only the child composition's cells are returned and cameras are dropped (`src/interact/pick_targets.cpp`, `src/interact/ace/interact/pick.hpp`).
- Wired three L4 call sites in `AppProjectGateway::insert_cell`, `canvas_view` hit-test, and frame-selection `pick_targets` builds to pass `state_.entered_composition()` (`src/app/project_gateway.cpp`, `src/app/canvas_view.cpp`).
- Added `cells_scoped_edit_test.cpp` (10 Catch2 unit cases: insert-in-child/fail-safe, scoped delete+undo, root-id no-op, `selected_removals` scoping, `pick_targets` confinement, transient/dirty) to `ace_tests` in `CMakeLists.txt`.
- Added `tests/goldens/cells_scoped_insert_64x64.rgba8` golden proving scoped insert lands inside the nested child.
- Added `tests/cells_scoped_edit_e2e_test.cpp` (5 ImGui Test Engine cases: enter→insert, inert out-of-scope click/marquee, confined Ctrl+A, confined Delete+Undo, climb restores root).
- Appended ASan/TSan anchor to `tests/canvas_host_test.cpp` (scoped add/remove through `apply_edit` while render thread walks same document over `pin()` and `set_scope` channel).
- D29 ("Scoped editing") landed in `docs/00-design.md`, extending D17 with the editing-confinement promise. Realized by `editor.cells.scoped_edit`.
