# editor.project.undo — Undo/redo wired to library transactions

## TaskJuggler entry

`tasks/00-editor.tji:136-141` — `task undo` under `editor.project`. Effort
`2d`, `allocate team`, `depends !app_state` (`:139`, i.e.
`editor.project.app_state`). The `note` (`:140`) states **undo/redo IS libarbc's
transaction journal (doc 14), not a reimplementation**, that continuous gestures
(a stroke, a drag) **coalesce into one step**, and pins the transient-vs-scene
line: **viewport-camera navigation is transient (not undoable); a saved shot's
framing is scene data (undoable)**. It cites **Design D15** and names this
refinement. (The `.tji` note back-links to the flat `tasks/refinements/undo.md`;
the real landing path is `tasks/refinements/editor/undo.md`, matching the
existing `editor/` refinement set — the closer fixes the note back-link per the
ritual in `tasks/refinements/README.md:57-68`, exactly as `app_state.md` /
`save.md` / `save_as.md` did.)

Downstream dependents: every scene-edit leaf (`editor.cells.*`, `editor.cameras.*`,
`editor.paint.*`, `editor.import.*`) that dispatches a transaction inherits undo
for free through the journal this leaf drives, and the gesture-authoring leaves
(`editor.paint.brush`'s stroke, `editor.cells.gizmo`'s drag) consume the
coalescing seam this leaf ships. The deferred `editor.panels.history` view (this
refinement registers it) renders the journal this leaf exposes.

## Effort estimate

**2 days** (from the `.tji`). The library does all of the heavy lifting: the
document-wide journal (`arbc::Journal`, `undo()`/`redo()`/`can_undo()`/`can_redo()`,
gesture coalescing via `Transaction::coalesce(CoalesceKey)`) is fully built and
already reached through the shipped dispatch seam
(`src/commands/app_state.cpp:29-39` reads `doc.journal().depth()` and
`doc.pin()->revision()` today). This leaf is thin wiring in three small places:
(1) two L1 `commands` verbs — `commands::undo(AppState&)` / `commands::redo(AppState&)`
driving `state.document().journal().undo()/redo()` and reporting the outcome;
(2) a `commands` gesture-coalescing seam — a monotonic `CoalesceKey` allocator on
`AppState` so continuous gestures collapse to one journal step and two consecutive
distinct gestures never collide; (3) UI affordance — four `dock::ProjectGateway`
virtuals (`undo`/`redo`/`can_undo`/`can_redo`), their L4 `AppProjectGateway`
overrides, and a `Ctrl+Z` / `Ctrl+Shift+Z` keyboard shortcut wired in the L3
dockspace beside the existing rail buttons. No new component, no new DAG edge, no
new library machinery, no off-thread driver.

## Inherited dependencies

**Settled (from `editor.project.app_state`, `tasks/refinements/editor/app_state.md`):**

- **The one owned session** — `ace::commands::AppState` holds the single
  workspace-backed `arbc::Document` for the process lifetime (move-only, A7),
  reached by `state.document()` (`src/commands/ace/commands/app_state.hpp:44-45`).
  One process = one project = one journal (D19/A7); the GC/undo root-set is
  trivially that document.
- **The dispatch seam and its one-command-one-entry invariant** — a `Command`
  (`{std::string name; std::function<void(arbc::Document&)> apply;}`) applied by
  `DispatchOutcome dispatch(AppState&, const Command&)`
  (`app_state.hpp:94-110`, impl `app_state.cpp:29-39`) runs `apply` on the writer
  `Document`, synchronous single-writer (A4); each well-formed command self-commits
  **exactly one libarbc transaction → one journal entry → one revision bump**.
  `DispatchOutcome` already exposes `journal_entries_added` and `revision`. **This
  leaf reads the same journal from the other side** (cursor navigation), never
  reimplementing a stack (D-app_state-5 explicitly deferred coalescing here).
- **The dirty model** — `AppState::is_dirty()` = `!saved_revision_ ||
  *saved_revision_ != document_->pin()->revision()` and `mark_saved(revision)`
  (`app_state.hpp:60-75`), conservative toward "dirty" (never a false clean,
  D-save-4). Undo/redo interact with this baseline — see Constraint 4 / Decision
  D-undo-4.
- **The gateway seam** — the L4 `app::AppProjectGateway` implements the L3
  `dock::ProjectGateway` virtuals against the held `AppState`
  (`src/app/project_gateway.cpp`); the rail reaches session verbs only through the
  gateway, never a direct `dock → commands` edge (established by `save.md`/`save_as.md`;
  A13). This leaf extends that same seam.
- **The active tool lives in `dockmodel`** (`dockmodel::ToolSelection`), NOT in
  `commands` (D-app_state-4) — irrelevant to undo, but it confirms the
  `commands ↮ dockmodel` non-edge this leaf must not introduce.

**Pending (do not block this leaf):**

- The concrete scene-edit taxonomy (`editor.cells.*`, `editor.cameras.*`,
  `editor.paint.*`) — those `Command`s land with their own leaves; this leaf ships
  the undo/coalescing machinery and proves it against fixture commands, exactly as
  `app_state.md` proved dispatch against a fixture `add_solid_content`.
- The History **view/panel** (visual entry list + jump-to-cursor) — deferred to
  `editor.panels.history` (this refinement registers it; see Acceptance criteria).
- Off-thread edit submission (`editor.canvas.frame_sync`) — undo/redo stay on the
  writer thread here (A4), same as `dispatch`.

## What this task is

Wire the editor's Undo and Redo to libarbc's document-wide transaction journal
(doc 14 / D15). Because every scene edit is already a library transaction that
appends one `JournalEntry` (the shipped `dispatch` seam), undo/redo are pure
**navigation** of that journal — `journal().undo()` steps the cursor back and
republishes the touched objects at their *before* edge as an ordinary forward
publish (revision +1); `redo()` steps forward to the *after* edge. History is
never mutated. This leaf adds: (1) the two `commands` verbs that drive the
journal cursor and report whether it moved plus the resulting revision /
can-undo / can-redo state; (2) a monotonic **gesture-coalescing key** seam so a
continuous gesture (a brush stroke, a handle drag — many transactions across many
frames) folds into **one** undo step; (3) the minimal user affordance — the
`Ctrl+Z` / `Ctrl+Shift+Z` (also `Ctrl+Y`) keyboard shortcuts routed through four
new gateway virtuals to those verbs. It ships the machinery and the seam, not the
edit taxonomy — the concrete gesture callers (brush, drag) live in their own
leaves and consume `next_gesture_key()`.

## Why it needs to be done

Undo is table-stakes for any editor, and D15 makes it a first-class,
zero-reimplementation capability: the library already stores the reversible
journal, so the editor's only job is to expose navigation and to mark gesture
boundaries. Without this leaf, every scene edit is a one-way commit — the
one-command-one-entry invariant `app_state` pinned would be inspectable but not
reversible. It is a `!app_state` dependent because it needs the one owned
`Document` and the dispatch seam; it is upstream (by consumption, not `depends`)
of every edit and gesture leaf, which all become undoable for free once the
journal is driven, and of the `editor.panels.history` view it registers. It sits
under the single `editor` milestone (`tasks/99-milestones.tji:5`), whose big
`depends` list already names `editor.project.undo` (`tasks/00-editor.tji:323`).

## Inputs / context

**Design docs (normative — the constitution):**

- **D15** (`docs/00-design.md:476`) — *Undo = library transactions.* Scene edits
  are transactions (doc 14); continuous gestures coalesce to one step. The line is
  **transient vs. scene:** the viewport camera's live framing (pan/zoom) is
  transient session state, NOT undoable (like scroll position); a saved shot's
  framing is scene data and IS. GC is a confirmed op (not undoable); consolidate is
  reversible. **The governing row for this leaf.**
- **D19** (`docs/00-design.md:480`) / **A7** — one process = one project = one
  `Document` = one journal; the undo root-set is trivially that document.
- **§9** (`docs/00-design.md:366-372` per the design's editing narrative) and
  **arch §4** (`docs/01-architecture.md:59-83`) — writer-thread, single-writer
  edit discipline (A4); undo/redo are writer-thread publishes.
- **arch §8** (`docs/01-architecture.md:144-179`) — the levelization DAG.
  `commands` is L1 (`base, project, scene`), whitelisted for `<arbc/…>` includes,
  ImGui/GL/SDL-free; `dock`/`views` are the only ImGui layers (L3); `app` is L4.
- **arch §9** (`docs/01-architecture.md:181-208`) — the universal DoD (levels ·
  L1 Catch2 · goldens · Test-Engine e2e · sanitizers · ≥90% diff coverage).

**Library API (v0.1.0, fetched under `build/dev/_deps/arbc-src/`; local checkout
`/home/ruoso/devel/arbitrarycomposer`; doc 14
`docs/design/14-data-model-and-editing.md`):**

- `arbc::Journal` — `src/model/arbc/model/journal.hpp`. `bool undo()` / `bool redo()`
  (`:83-84`, WRITER-THREAD ONLY; return `false` when nothing to move to, cursor
  stays put), `bool can_undo()` / `bool can_redo()` (`:86-87`), `std::size_t
  depth()` (`:90`, stored undoable+redoable count), `std::size_t cursor()` (`:92`,
  applied-entry count in `[0, depth()]`), `std::size_t byte_cost()` (`:94`), `const
  JournalEntry& entry_at(std::size_t)` (`:97`, history-inspection seam). Undo/redo
  are **ordinary forward publishes (revision +1)** that rebind touched objects to
  the *before*/*after* edge and flush damage once — they never mutate history.
- `arbc::Document::journal()` → `Journal&` — `src/runtime/arbc/runtime/document.hpp:184-185`.
  The document's own `CommitSink`; the journal is default-constructed
  (`Journal d_journal{*d_model}` = `k_no_budget`, unbounded, `document.hpp:415`).
- `arbc::Model::Transaction::coalesce(CoalesceKey)` — `src/model/arbc/model/model.hpp:499`.
  Stamps a non-zero gesture key; the journal's `on_commit` **folds consecutive
  commits sharing a matching non-zero key into the tip entry** (one undo unit;
  each coalesced commit still publishes for live display). `CoalesceKey =
  std::uint64_t`, `k_no_coalesce = 0` (`src/model/arbc/model/journal_entry.hpp:15-17`).
  Obtained via `Document::transact(std::string name = {})` (`document.hpp:178`,
  WRITER-THREAD ONLY) → `t.coalesce(key)` → mutate → `t.commit()`.
- `JournalEntry { std::string name; CoalesceKey coalesce_key; … }`
  (`journal_entry.hpp:44-50`) — `name` labels the step ("Undo <name>" comes from
  `entry_at(cursor()-1).name`; a `editor.panels.history` concern).

**Editor seams this leaf extends:**

- `src/commands/ace/commands/app_state.hpp` / `src/commands/app_state.cpp` —
  `AppState`, `dispatch`, and the session verbs (`open_or_create_app_state`,
  `save_project`, `save_project_as`). New verbs `undo`/`redo` and the coalesce-key
  allocator land here.
- `src/dock/ace/dock/dock.hpp` — `dock::ProjectGateway` (virtuals `save()` `:69`,
  `is_dirty()` `:73`, `save_as()` `:82`, …). Add `undo`/`redo`/`can_undo`/`can_redo`.
- `src/app/ace/app/project_gateway.hpp` / `src/app/project_gateway.cpp` —
  `AppProjectGateway` overrides against the held `AppState`.
- `src/dock/dock.cpp` — the dockspace frame that draws the rail (Save `###save_project`,
  Save As `###save_as`); the keyboard shortcut is handled here alongside it, reusing
  the `ProjectGateway&` the rail already holds.
- `tests/commands_test.cpp` — the L1 `dispatch`/journal unit pattern (`ScratchDir`,
  the `add_solid_content` fixture, `depth()`/`can_undo()`/`revision` assertions,
  `:143-185`) this leaf mirrors in a new `tests/undo_test.cpp`.
- `tests/save_ui_e2e_test.cpp` / `tests/save_as_ui_e2e_test.cpp` — the fake
  `ProjectGateway` injection + Test-Engine drive pattern for the new e2e, and the
  inert-override requirement whenever a `ProjectGateway` virtual is added.

## Constraints / requirements

1. **Undo IS the library journal — never a reimplemented stack.** `commands::undo`
   / `commands::redo` call `state.document().journal().undo()` / `.redo()` and
   nothing more; no editor-side inverse-command list, no snapshot copies (D15,
   D-app_state-5). The journal already stores the reversible entries the shipped
   `dispatch` appended.
2. **Writer-thread, synchronous (A4).** Both verbs run on the calling (writer)
   thread against the one owned `Document`, like `dispatch`. No off-thread queue
   (that is `editor.canvas.frame_sync`). Errors are values: a `journal().undo()`
   that returns `false` (nothing to undo, or a rare writer-path allocation failure —
   cursor stays put) surfaces as `moved == false`, never a throw.
3. **A continuous gesture is ONE undo step.** A brush stroke or a handle drag emits
   many transactions across many frames; they must fold to one journal entry. The
   mechanism is `Transaction::coalesce(key)` with a **single non-zero key held for
   the gesture's lifetime**. Because the journal folds *consecutive* commits with
   the *same* key, two distinct gestures must never share a key — this leaf ships a
   **monotonic allocator** (`AppState::next_gesture_key()`, writer-thread, starts at
   1, never returns 0, never repeats within a session) so gesture leaves get a
   collision-free key. The gesture calls it once at gesture-start and stamps every
   frame's `doc.transact(name).coalesce(key)`; ending the gesture = stop using that
   key (the next edit's fresh key or `k_no_coalesce` starts a new unit).
4. **Undo/redo advance the revision monotonically — they never mark the session
   clean.** Because navigation is a *forward* publish (revision +1 each way),
   undoing back to the saved content lands on a **new, higher** revision than
   `saved_revision_`, so `AppState::is_dirty()` reports **dirty** even when the
   content matches the last save. This is correct under D-save-4's
   conservative-toward-dirty contract (never a false clean). `commands::undo`/`redo`
   therefore **must not** call `mark_saved`. Content-equality-based clean detection
   is a deliberate non-goal.
5. **The transient-vs-scene line (D15) is preserved.** Viewport-camera navigation
   (pan/zoom) is transient session state and must never be a `transact` — it stays
   off the journal, like `Selection` (proven journal-inert in `commands_test.cpp:137-140`).
   A saved shot's framing edit IS a scene transaction and is undoable like any
   other. This leaf does not implement camera nav (`editor.canvas.nav` /
   `editor.cameras.manip` own that) but its undo model **assumes** nav stays off the
   journal; the invariant is stated here so those leaves honor it, and is guarded by
   a unit test that transient state leaves `depth()` unchanged.
6. **Levelization stays clean (§8).** The verbs and the key allocator live in
   `commands` (L1, already `<arbc/…>`-whitelisted). The gateway virtuals live in
   `dock` (L3) / `app` (L4); the shortcut handling is in `dock` (L3, sees ImGui).
   **No new component, no new DAG edge** — `commands→arbc`, `dock→(app implements
   gateway)` already exist; the `commands ↮ dockmodel` non-edge is untouched.
   `check_levels` stays clean.
7. **Every existing fake `ProjectGateway` gets inert `undo`/`redo`/`can_undo`/`can_redo`
   overrides.** Adding a pure virtual to `dock::ProjectGateway` breaks every fake in
   the e2e suite (`open_ui_e2e_test.cpp`, `save_ui_e2e_test.cpp`,
   `save_as_ui_e2e_test.cpp`) until each provides an override — the same mechanical
   step `save_as.md` took.

## Acceptance criteria

The universal DoD (`docs/01-architecture.md:181-208`) instantiated for this leaf:

- **Levelization** — `scripts/check_levels.py` (via `scripts/gate`) stays clean; no
  new component, no new edge (Constraint 6). The L1 `commands` core gains no
  ImGui/GL/SDL include.
- **L1 Catch2 units** — `tests/undo_test.cpp` in the headless `ace_tests` binary,
  reusing the `ScratchDir` + `create_project` fixture pattern from
  `tests/commands_test.cpp`:
  1. **Round-trip.** `dispatch` a fixture edit (e.g. `add_composition` /
     `add_solid_content`), then `commands::undo(state)` → `moved == true`,
     `journal().cursor()` drops by 1, `can_redo()` flips true, revision advanced
     (forward publish); `commands::redo(state)` → cursor restored, `can_redo()`
     false. Multiple edits undo/redo in LIFO order.
  2. **Empty-journal safety.** `commands::undo` on a fresh session → `moved == false`,
     `can_undo() == false`, no throw; symmetric `redo` at the tip.
  3. **Gesture coalescing.** Dispatch N (≥2) fixture edits each opening
     `doc.transact("stroke").coalesce(key)` with **one** `key` from
     `next_gesture_key()`; assert `journal().depth()` grew by **exactly 1** (folded)
     and a single `commands::undo` restores the pre-gesture revision/state.
  4. **Key collision-freedom.** Two gestures with distinct `next_gesture_key()`
     values (K1 ≠ K2, both non-zero, monotonic) stay **two** undo units even when
     adjacent — depth grows by 2; `undo` reverts the second gesture, a second `undo`
     the first. `next_gesture_key()` never returns `k_no_coalesce`.
  5. **Dirty interaction (Constraint 4).** After `save_project` marks clean, an edit
     dirties; `undo` back to the saved content leaves `is_dirty() == true` (revision
     advanced past `saved_revision_`); `undo`/`redo` never call `mark_saved`.
  6. **Transient guard (Constraint 5).** A transient mutation (a `Selection` change,
     the D15 stand-in for camera nav) leaves `journal().depth()` and cursor
     unchanged and is invisible to undo.
- **UI e2e (ImGui Test Engine)** — a case in the `ace_shell_test` binary
  (`tests/undo_ui_e2e_test.cpp`) with a fake `ProjectGateway` injected via
  `ShellOptions::project_gateway`: drive an edit, inject `Ctrl+Z`, assert the
  gateway's `undo()` fired (and the journal cursor moved for a real gateway);
  inject `Ctrl+Shift+Z` / `Ctrl+Y`, assert `redo()` fired; assert the shortcut is
  gated (no-op when `can_undo()`/`can_redo()` is false). Add inert overrides to the
  existing fake gateways (Constraint 7).
- **Threading** — the L1 units and the e2e run under the `clang-asan` lane
  (ASan/UBSan, offscreen GL); undo/redo are writer-thread publishes over the live
  workspace-backed `Document` (its `HousekeepingThread` spans the case), so the lane
  covers the UI↔document handoff at the same scope `commands_test.cpp` /
  `app_state_e2e_test.cpp` already establish (A4/§9).
- **Coverage** — ≥90% diff coverage on the changed lines; tests ship with the task.
- **Rendered output** — no new golden. Undo republishes existing versions the render
  path already covers; the round-trip's *visual* correctness is the library's
  (doc-14 journal) contract, exercised by libarbc's own tests. The editor asserts
  cursor/revision/depth state, not pixels. (Should a gesture leaf later want a
  paint→undo→render golden, it belongs to that leaf, not here.)

**Deferred follow-up (closer registers in the WBS, wired into the `editor`
milestone, `tasks/99-milestones.tji`):**

- **`editor.panels.history`** — the History view/panel (D18/D21 "Review" workspace):
  render `journal().entry_at(i).name` as an ordered list with the `cursor()`
  position marked, "Undo <name>" / "Redo <name>" labels, and **click-an-entry to
  jump** (loop `undo()`/`redo()` to reach the target cursor — the library exposes
  single-step navigation only). L3 `views`, effort **~2.5d**, `depends
  editor.project.undo, editor.dock.view_registry`, `note` citing D15/D18/D21 and
  this refinement. (The view *type* "History" is already registered by
  `editor.dock.view_registry`, `tasks/00-editor.tji:74`; this leaf builds its
  contents.)

## Decisions

- **D-undo-1 — Two `commands` free-function verbs, not a `Command`/`dispatch`
  route.** Undo/redo are journal-cursor *navigation*, not scene *edits*: they add no
  `JournalEntry` and take no `apply` closure, so they do not fit the `Command`
  shape. Ship `platform::Result`-free plain verbs returning a small `UndoOutcome
  { bool moved; std::uint64_t revision; bool can_undo; bool can_redo; }` (mirroring
  `DispatchOutcome`'s observable-state style), living beside the other session verbs
  in `commands` (D-app_state-1). *Rejected:* modelling undo as a `Command` with an
  `apply` that calls `journal().undo()` — it would mis-report `journal_entries_added`
  (navigation is a publish, not a new entry) and muddy the one-command-one-entry
  invariant `app_state` pinned. *Rejected:* a `platform::Result` return — there is no
  error to carry (nothing-to-undo is normal control flow, not a fault); a bool
  `moved` is the honest signal.
- **D-undo-2 — Coalescing key is a monotonic allocator on `AppState`, exposed as
  `next_gesture_key()`.** The library folds *consecutive* same-key commits, so the
  editor's job is to hand out non-zero keys that never collide between distinct
  gestures. A per-session monotonic `std::uint64_t` counter (writer-thread, seeded at
  1) is the minimal correct seam and homes the gesture-boundary responsibility in the
  session object that already owns the writer `Document`. *Rejected:* letting each
  gesture leaf pick its own literal key — two drags both choosing `1` would silently
  merge if adjacent (Constraint 3). *Rejected:* a richer `GestureScope`/RAII handle
  that threads the key and auto-ends the gesture — no call site needs it yet (brush /
  drag are downstream leaves); the one- or two-line `next_gesture_key()` + convention
  is the simpler abstraction (bias toward one/two call sites today). If a gesture leaf
  later wants RAII ergonomics it can wrap this key locally.
- **D-undo-3 — Keyboard `Ctrl+Z` / `Ctrl+Shift+Z` (also `Ctrl+Y`) in the L3
  dockspace, through the gateway.** The canonical undo affordance is the keyboard
  chord; the dockspace already owns the rail and the `ProjectGateway&`, so the
  shortcut is handled there (ImGui `Shortcut`/`IsKeyChordPressed`) and routed to the
  new gateway virtuals, gated on `can_undo()`/`can_redo()`. *Rejected:* a menu-bar
  Edit menu — the shell is a menu-bar-free uniform dockspace (D18); undo has no menu
  home. *Rejected:* putting the affordance only in the History panel — that panel is
  deferred, and undo must be usable the moment this leaf lands. A general
  configurable keymap is out of scope; the chords are hard-wired here (no keymap seam
  invented, no "audit" task — a configurable keymap surfaces in the parking lot if it
  is ever wanted).
- **D-undo-4 — Undo/redo do not touch the dirty baseline.** Forward-publish
  semantics (revision +1 each navigation) mean the revision never returns to
  `saved_revision_`, so undo-to-saved reads dirty. Rather than fight this with content
  hashing, this leaf honors D-save-4's conservative-toward-dirty contract: navigation
  leaves `saved_revision_` untouched and `is_dirty()` derives correctly. *Rejected:*
  making `undo` restore-clean when the revision's *content* equals the saved snapshot —
  it would require a content hash the library does not expose on the revision and would
  invert D-save-4's "never a false clean" guarantee into a risk of false-clean; a
  harmless false-dirty is the safe side.
- **D-undo-5 — Unbounded undo (no editor byte budget) for v1.** The document's
  journal is default-constructed at `k_no_budget` (`document.hpp:415`) and the library
  exposes no editor-facing budget knob on `Document`. v1 leans on the crash-durable
  workspace and generous desktop memory; bounding undo memory would be a *library*
  change, not editor work. *Rejected:* deferring a WBS "bound undo memory" task — it is
  not agent-implementable editor work (it needs a library API); it is noted as a
  non-goal and, if it ever matters, surfaces in the parking lot.

No doc delta: D15 already settles undo = library transactions, the transient-vs-scene
line, and gesture coalescing; A13 already established the `ProjectGateway` extension
pattern for session verbs. This leaf turns those promises into wiring without amending
the constitution.

## Open questions

(none — all decided)

## Status

**Done** — 2026-07-18.

- `src/commands/ace/commands/app_state.hpp` — `next_gesture_key()` monotonic allocator; `UndoOutcome` struct; `undo`/`redo` verb declarations.
- `src/commands/app_state.cpp` — `undo`/`redo` impls driving `journal().undo()/redo()` (forward-publish, dirty-baseline-inert, no `mark_saved` call).
- `src/dock/ace/dock/dock.hpp` — four new `ProjectGateway` pure virtuals (`undo`/`redo`/`can_undo`/`can_redo`).
- `src/app/ace/app/project_gateway.hpp` / `src/app/project_gateway.cpp` — `AppProjectGateway` overrides against the held `AppState`.
- `src/dock/dock.cpp` — `handle_undo_shortcuts` (Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y via `IsKeyChordPressed`, gated on `can_undo`/`can_redo`), wired into `Dockspace::draw()`.
- `tests/undo_test.cpp` — Catch2 unit (7 cases: round-trip/LIFO, empty-journal safety, gesture coalescing, key collision-freedom, key monotonicity, dirty interaction, transient guard).
- `tests/undo_ui_e2e_test.cpp` — ImGui Test Engine e2e (chord-drives-gateway + gating assertions).
- `tests/open_ui_e2e_test.cpp`, `tests/save_ui_e2e_test.cpp`, `tests/save_as_ui_e2e_test.cpp` — inert gateway overrides added (Constraint 7).
- `tests/app_project_gateway_test.cpp` — `[app_project_gateway]` case covering the four new concrete `AppProjectGateway` methods (empty-journal inertness, one edit, undo→redo with can_undo/can_redo transitions).
- `CMakeLists.txt` — registered `tests/undo_test.cpp` and `tests/undo_ui_e2e_test.cpp`.
