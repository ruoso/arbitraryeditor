# editor.cells.group_transform — Group transform: apply one affine delta across a multi-object selection

## TaskJuggler entry

- Task: `group_transform` under `task cells` (`tasks/00-editor.tji:540`), fully-qualified `editor.cells.group_transform`.
- Effort: `1.5d`; `allocate team`.
- Depends: `!gizmo` (`editor.cells.gizmo`, the sibling single-object transform gizmo — shipped, `tasks/00-editor.tji:533`, `complete 100`).
- Note (verbatim): *"Transform a multi-object selection: apply one affine delta about a shared pivot (the union of the selected placed extents, `interact::selected_extent`, `pick.hpp:136-137`) across every selected cell's placing layer, coalesced into one journal entry via `doc.transact(name).coalesce(key)` and a batch `commands::Command` (the natural home for the `transform_cells_command` this leaf's single-object path did not need — D-gizmo-3). Kept out of scope of `editor.cells.gizmo` because the single-object gizmo is the D7/SS6 primitive and the batch-coalesce is a distinct concern. Source-of-debt: `tasks/refinements/editor/gizmo.md`. Design: `docs/00-design.md` D7/D8, `docs/01-architecture.md` A17."*
- Closer ritual: append the `## Status` block, add `complete 100` after `allocate team`, and end the `.tji` note with `Refinement: tasks/refinements/editor/group_transform.md` (`tasks/refinements/README.md:47-68`). Do **not** hand-edit the `.tji` here.

## Effort estimate

`1.5d`, broken down:

- **~0.4d** — the L1 `interact` group-delta pure helper (§ Decisions D-group_transform-3) + its Catch2 units (delta about a shared pivot, composition onto N start placements, D8 placement-only invariance, degenerate guards).
- **~0.4d** — `commands::transform_cells_command` beside `remove_cells_command`, its one-`transact`-block body, and the dispatch/one-entry Catch2 proof.
- **~0.4d** — the L4 `app::CanvasView` group-gizmo grab/preview/release: transient Presenter session state mirroring the single-object cell path, snap reuse, inert-drag guard, batch build on release.
- **~0.3d** — the ImGui Test Engine e2e, the group-scale golden, and the ASan/TSan `canvas_host_test.cpp` case.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cells.gizmo`** (`tasks/refinements/editor/gizmo.md`, Done 2026-07-29) ships the single-object transform vocabulary this leaf generalizes: the pure L1 verbs `interact::move_cell` / `scale_cell` / `rotate_cell` / `shear_cell` / `cell_pivot` / `snap_placement` / `CellHandle` (`src/interact/ace/interact/interact.hpp:330-368`, `pick.hpp:185+`), and the L4 grab→preview→commit-on-release loop (`src/app/canvas_view.cpp:563-620`). **D-gizmo-3** explicitly named this leaf as the home for the batch `transform_cells_command` it declined to mint for one object, and **D-gizmo-6** scoped the single-object gizmo to exactly one selected cell, naming `editor.cells.group_transform` as the multi-object follow-up.
- **`editor.cells.selection`** (`tasks/refinements/editor.cells/selection.md`) ships `interact::selected_extent(targets, ids)` (`src/interact/ace/interact/pick.hpp:136-137`) — the axis-aligned composition-space union of the selected targets' placed extents, kind-agnostic, unbounded-skipping, `nullopt` when nothing bounded remains (`pick.hpp:119-135`) — and the project-level `commands::Selection` on `AppState` read via `state_.selection().items()` (`src/app/canvas_view.cpp:244`).
- **`editor.cells.one_action_one_entry`** / **`editor.project.undo`** ship the `commands::Command` / `dispatch` / `DispatchOutcome` seam (`src/commands/ace/commands/app_state.hpp:262-278`), the batch-command precedent `remove_cells_command` (`src/commands/cells.cpp:64-78`), and `AppState::next_gesture_key()` (`app_state.hpp:212`).

**Pending (owned here):** the group-delta interact helper, the `transform_cells_command`, the L4 group-gizmo interaction, and their tests.

## What this task is

When two or more objects are selected, this leaf lets the user drag **one** transform gizmo — anchored on the bounding box of the whole selection — and have the same affine delta (move / scale / rotate / shear) apply to **every** selected object at once, about a **shared pivot** (the center of `interact::selected_extent`, draggable). The whole gesture lands as **one journal entry / one revision bump / one undo press** and swaps in **atomically** (an observer never sees a half-transformed group). It reuses the single-object gizmo's shipped transform math on the union box, mints the batch `commands::transform_cells_command` that D-gizmo-3 deferred, and flips the single-object gizmo's "two-cells-selected → no gizmo" behavior to "two-or-more selected → the group gizmo."

## Why it needs to be done

The single-object gizmo (`editor.cells.gizmo`) is the D7/§6 primitive but is scoped to exactly one selected cell (D-gizmo-6); with a multi-selection today the canvas shows **no** gizmo, so arranging several cells means moving them one at a time and undoing each separately. D7's one-shape/one-select-tool model and §6's "the same gizmos and verbs" promise both assume a group can be dragged as a unit. This leaf closes that gap and, in doing so, mints the batch `commands::transform_cells_command` that D-gizmo-3 named as "genuinely useful for the batch case" — the reusable home any future multi-object placement edit consumes. Downstream, `editor.panels.overview` reuses the exact same interact helper + command on the schematic boxes when it lands (no new work there).

## Inputs / context

**Governing design docs (normative — the constitution):**

- **§6 — Direct manipulation** (`docs/00-design.md:208-269`), esp. *"In the editable overview: the same gizmos and verbs … coarse arrangement of many cells/cameras"* (`:265-268`) and **"Placement is not resampling — the load-bearing rule"** (`:237-244`): *"Dragging handles changes the affine … it never touches stored pixels … a handle-drag is never a resample."*
- **D7 — Manipulation model** (`docs/00-design.md:474`): cells and cameras share **one shape** and **one select tool**, differing only in direction. Kind-agnostic selection is the basis for a kind-agnostic group transform.
- **D8 — Cell scale ≠ resample** (`:475`): handle-drag changes placement (affine), never resolution — non-destructive. Applies per-member here.
- **D15 — Undo = library transactions** (`:482`): scene edits are transactions; *continuous gestures coalesce to one step*. A group commit-on-release is a single discrete commit = one step (see D-group_transform-1).
- **D19 / A7 — project-scoped selection** (`:486`): one project-level `Selection`; canvases are only cameras.
- **D23 / D24** (`:490-491`): the `selected_extent` union geometry — the axis-aligned union of the selected objects' placed extents — is the same primitive the frame-selection mint and the deep-zoom aids consume; the group pivot reuses it.

**Governing architecture rows:**

- **A17** (`docs/01-architecture.md:432`): hit-testing and the pick-target policy live in L1 `interact` (`pick.hpp`); the transform math stays primitive-only, headless-testable, `scene`-free (the assembly adapter `pick_targets` is the one exception, and it is not on this leaf's write path). The group-delta helper lands here.
- **A4 / A5 / A7** (`:419-422`): single-writer cache, N canvases over one `Document`, the app owns one `Document`; every edit-path call (including the batch commit) runs on the one writer thread, reached through `CanvasView::apply_edit`.
- **§8 levelization DAG** (CI-enforced by `scripts/check_levels.py`) and **§9 DoD** (Catch2 L1 units, `render_offline` goldens, ImGui Test Engine e2e, ASan/TSan, ≥90 % diff coverage, `check_levels` clean).

**libarbc API surface (FetchContent, `build/dev/_deps/arbc-src/`):**

- `arbc::Document::transact(std::string name = {}) -> Model::Transaction` — the public host-facing edit seam (`src/runtime/arbc/runtime/document.hpp:289`, WRITER-THREAD ONLY).
- `Model::Transaction::set_transform(ObjectId layer, const Affine&)` (`src/model/arbc/model/model.hpp:501`) — replace one layer's transform inside an open transaction; `::commit()` (`:617`) *"assembles ONE journal entry"* however many layers were touched; `::coalesce(CoalesceKey)` (`:602`) folds *consecutive separate commits* (not needed here — see D-group_transform-1).
- `arbc::Document::set_layer_transform(ObjectId, const Affine&)` (`document.hpp:244`) — the single-object convenience wrapper the cell gizmo uses; its body is `begin(); set_transform; commit;` (`src/runtime/document.cpp:462-466`), i.e. **one transaction per call** — which is why calling it N times would cost N entries.

**Editor seams this leaf extends:**

- `interact::selected_extent` (`src/interact/ace/interact/pick.hpp:136-137`) — the shared-pivot geometry.
- Single-object transform verbs `interact::{move,scale,rotate,shear}_cell`, `cell_pivot`, `snap_placement`, `CellHandle` (`src/interact/ace/interact/interact.hpp:330-368`).
- `commands::Command` / `dispatch` / `DispatchOutcome` (`src/commands/ace/commands/app_state.hpp:262-278`) and the batch precedent `remove_cells_command` (`src/commands/ace/commands/cells.hpp:79`, impl `src/commands/cells.cpp:64-78`).
- The L4 cell-gizmo grab/preview/release loop and its transient Presenter session state (`src/app/canvas_view.cpp:392-434` frame path, `:527-620` cell path; inert-drag guard `:432`; commit through `apply_edit`, `canvas_view.hpp:92`).

**Predecessor / sibling refinements:** `tasks/refinements/editor/gizmo.md` (D-gizmo-3, D-gizmo-6, the commit-on-release + transient-preview shape), `tasks/refinements/editor.cells/selection.md` (`selected_extent`, `SelectionChange`), `tasks/refinements/editor.cells/one_action_one_entry.md` / `tasks/refinements/editor/undo.md` (the one-entry contract and its `dispatch` instrument).

**Test rigs:** `tests/gizmo_test.cpp` (`CMakeLists.txt:246`), `tests/camera_manip_test.cpp` (`:245`) for L1 units; `tests/gizmo_e2e_test.cpp` / `tests/selection_e2e_test.cpp` (`:291`, `:294`) for the ImGui Test Engine e2e mouse-drive recipe; `tests/canvas_host_test.cpp` (`:241`) for the real-pool ASan/TSan suite; `tests/golden_support.hpp` + `tests/goldens/*.rgba8` for `render_offline` goldens.

## Constraints / requirements

1. **One user action = one journal entry, atomically.** A completed group drag commits **exactly one** libarbc transaction → one journal entry → one revision bump → one undo press, and the N transforms swap in one atomic publish (no observer ever sees a partially-transformed group). Proven with `dispatch`'s `journal_entries_added == 1` and a single `revision` bump, and by `undo()` restoring **every** member's prior `Affine` in one press.
2. **Placement is not resampling — the load-bearing rule (D8).** Every member's edit is a `set_transform` on its placing layer and touches nothing else: not the content's native resolution, not its stored pixels, not `content_bounds`. Asserted at L1 (native resolution + bounds byte-identical before/after for each member) and pinned by a golden showing the group scaled softer, never cropped or re-gridded.
3. **Shared pivot from the kind-agnostic union.** The pivot is the center of `interact::selected_extent(pick_targets, selection.items())` and is draggable (default = union center), mirroring the single-object `cell_pivot`. Rotate/shear/pivot-anchored scale compose about it.
4. **One delta, every selected placed object.** The gesture yields a single affine delta `D`; each member's new placement is `D` composed onto that member's start placement. Applies to every selected object that carries a placing layer — cells **and** cameras alike (D7 one-shape; see D-group_transform-4).
5. **Preview is transient; the journal sees only the release.** The in-progress group transform is Presenter session state (mirroring D-gizmo-4's `gizmo_cell_start`); no entry is opened mid-drag. An inert gesture — an identity delta, a Space-held pan, or a batch that would touch zero live layers — commits **nothing** (mirroring the `!(preview == start)` guard at `canvas_view.cpp:432`).
6. **The group gizmo replaces "no gizmo" for a multi-selection.** Shown iff **≥2** objects are selected **and** `selected_extent` is non-`nullopt`. A single selection stays the single-object gizmo's domain (D-gizmo-6). A ≥2 selection with no bounded member (unbounded fills only) shows no group gizmo — there is no box to manipulate and no pivot to derive (refuse-rather-than-guess, D23/D24).
7. **Snapping reuses the shipped engine.** `interact::snap_placement` is reused with the **union extent** as the single moving box against the non-selected `pick_targets`; Cmd/Ctrl (`in.ctrl || in.super`) bypasses snap, exactly as the single-object gizmo (D-gizmo-5). No new snap machinery; grid snapping stays deferred to `editor.canvas.grid`.
8. **Levelization (`check_levels` clean) — the primary structural assertion.** No new component, no new DAG edge. The group-delta helper is a pure `arbc::Affine` verb added to L1 `interact` (no `scene`/ImGui/GL/SDL include); `transform_cells_command` is a pure L1 `commands` verb over `arbc::` + the `transact` seam; L4 `app` does the interaction. The L1 core gains no ImGui/GL/SDL include.
9. **Writer-thread discipline (A4/A7).** The batch commit runs only through `CanvasView::apply_edit` on the one writer thread; the UI-thread reads (`selected_extent`, `pick_targets`, preview composition) open no transaction.

## Acceptance criteria

- **Levelization** — `scripts/check_levels.py` (via `scripts/gate`) stays clean: no new component, no new edge, no `check_levels` edit; L1 `interact`/`commands` gain no ImGui/GL/SDL include.
- **L1 Catch2 — new `tests/group_transform_test.cpp`** (added to `ace_tests`, `CMakeLists.txt:232-246`, naming `TEST_CASE("group transform: …")` after `tests/gizmo_test.cpp`). Sub-cases:
  - the group-delta helper: a move/scale/rotate/shear gesture on the union extent yields delta `D`, and `D` composed onto each of N hand-built start placements matches the per-member expected affine; order-independent; pivot = `selected_extent` center.
  - **D8 invariance** — after building the batch, each member's native resolution and `content_bounds` are byte-identical to before (placement-only).
  - **degenerate guards** — a 1-member selection produces no group batch; an all-unbounded selection yields `nullopt` extent → no gizmo/refuse; an identity delta and a stale-layer member add nothing.
  - **one-entry + atomicity proof** — `dispatch(state, transform_cells_command(batch))` returns `journal_entries_added == 1`, bumps `revision` once, leaves **all** N layers carrying the new transform (no partial), and `undo()` restores every prior `Affine` in one press.
- **Golden — new `tests/goldens/group_transform_scale_64x64.rgba8`** (modelled on `gizmo_scale_64x64.rgba8`): two bounded raster cells rendered 64×64, group-scaled ~2× about the union center, committed, compared byte-exact via `render_offline` / `ace_test::compare_golden` — softer, never cropped or re-gridded (pins Constraint 2 at the batch level; no tolerance).
- **UI e2e — new `tests/group_transform_e2e_test.cpp`** (added to `ace_shell_test`, `CMakeLists.txt:280-296`, modelled on `tests/gizmo_e2e_test.cpp` / `tests/selection_e2e_test.cpp`, reusing the raw-position mouse-drive recipe). Drives the canvas widget: marquee-select two cells → the **group** gizmo appears (flipping the single-object e2e's "two cells → no gizmo"); corner-drag scale updates **both** placements and journal `depth` +1; body-move + snap-to-neighbor + Cmd/Ctrl snap-bypass; Shift rotate 15°; undo restores both; a group drag over a camera+cell mixed selection re-places both (D7).
- **Threading (ASan/TSan)** — one case appended to `tests/canvas_host_test.cpp`'s real-pool suite (`default_interactive_pool_config()`) driving repeated `apply_edit(… transform_cells_command batch …)` against a live `CanvasHost` while the render thread `drive_once`s over the lock-free `pin()`.
- **Coverage** — `diff-cover --fail-under=90` on changed lines; the umbrella `scripts/gate` (check_levels · clang-format · build · ctest) is green.
- **No new deferred WBS leaf.** The Overview's group-transform wiring is `editor.panels.overview`'s own consumption of this leaf's shipped seam (not new work); grid snapping stays with the existing `editor.canvas.grid`. Nothing new to register.

## Decisions

**D-group_transform-1 — Batch atomicity is ONE `doc.transact(name)` committing N `Transaction::set_transform` calls, NOT N coalesced `set_layer_transform` self-commits.** On release the command opens a single `auto txn = doc.transact("group_transform")`, calls `txn.set_transform(member.layer, member.new_placement)` for each member, and `txn.commit()` once.
*Rationale:* `Transaction::commit()` *"assembles ONE journal entry"* however many layers it touched (`model.hpp:613-617`), so a single batch transaction is **natively** one entry, one revision bump, and — decisively — one **atomic** publish: observers see the old or the new group, never a half-transformed intermediate (`model.hpp:611-612`). This is the same batch-atomicity `remove_cells_command` gets from `scene::remove_cells` over the whole batch (`cells.cpp:64-78`), and it satisfies D15's "one step" more strongly than folding would. The `.coalesce(key)` mechanism folds *consecutive separate commits* of a continuous per-frame gesture into one undo unit — but this gesture, like the single-object gizmo (D-gizmo-4), previews in transient session state and commits exactly **once** on release, so there are no consecutive commits to fold, and N self-committing `set_layer_transform` calls (each `begin/set/commit`, `document.cpp:462-466`) would instead cost N entries, N revision bumps, N damage flushes, and a visible partial-group tear.
*Alternative rejected:* N coalesced `set_layer_transform` self-commits under one `next_gesture_key()`. This is what the WBS note's "coalesced … via `doc.transact(name).coalesce(key)`" phrasing reaches for; it achieves one *undo unit* but not one atomic publish, and it is strictly worse on every axis above. The note is WBS prose, not a `D`/`A` row; the one-entry promise it states is honored — more strongly — by the batch transaction, so refining the *mechanism* here needs **no doc delta** (D15's "one step" and D-app_state-5's "exactly one libarbc transaction via `transact`" both already describe the batch path).

**D-group_transform-2 — Mint `commands::transform_cells_command` now, beside `remove_cells_command`.** A new L1 `commands` verb (`src/commands/ace/commands/cells.hpp` + `cells.cpp`) takes the batch of `(layer, new_placement)` pairs and returns a `Command` whose `apply` runs the single-`transact` body of D-group_transform-1.
*Rationale:* D-gizmo-3 declined to mint this for one object — "machinery ahead of a consumer" — and named this leaf as its home; the consumer now exists (this leaf, and `editor.panels.overview` next), so minting it is reuse-driven, not speculative. It sits beside `remove_cells_command` because both are batch scene-edit `Command`s over the selection.
*Alternative rejected:* inline the `transact` loop in L4 `app::CanvasView`. That pushes a batch scene edit into ImGui-linked code headless Catch2 cannot reach and duplicates it the moment the Overview needs the same edit — the same reasoning A17 uses for keeping picking in L1. **No doc delta.**

**D-group_transform-3 — The group delta reuses the shipped single-object verbs on the union extent; the only new L1 code is a pure compose helper.** The gesture is applied to `selected_extent` treated as one virtual cell via the existing `move_cell`/`scale_cell`/`rotate_cell`/`shear_cell` verbs, yielding the union's new placement; the delta is `D = new_union ∘ old_union.inverse()`, and each member's new placement is `D ∘ start_placement[member]`.
*Rationale:* it reuses the exact, already-tested transform + handle math (`interact.hpp:330-368`) instead of re-deriving group-specific scale/rotate/shear; the genuinely new surface is one small pure `arbc::Affine` helper (compose-delta-onto-each), which stays in L1 `interact` and is headless-testable — respecting A17's "transform math lives in L1 `interact`" and D-gizmo-1's precedent.
*Alternative rejected:* bespoke per-kind group transform math. More code, a second copy of the scale/rotate/shear logic to keep in sync, no benefit over composing one delta.

**D-group_transform-4 — Kind-agnostic: the batch applies to every selected placed object with a layer (cells AND cameras).** The pivot comes from the kind-agnostic `selected_extent` (a camera contributes its output rect, `pick.hpp:127-129`), and the delta composes onto each member's layer transform whether it is a cell's placement or a camera's frame (cameras commit through `set_layer_transform` too, `canvas_view.cpp:433-434`).
*Rationale:* D7 makes cells and cameras one shape under one select tool; a group drag over a marquee that caught both should move both together, which is what a user expects and what §6's "arrangement of many cells/cameras" states. Filtering cameras out would need a kind test `interact` deliberately does not impose and would contradict D7.
*Alternative rejected:* cells-only group transform. Contradicts D7, needs a new kind filter, and surprises a user who marquee-selected a camera with the cells. **No doc delta.**

**D-group_transform-5 — Preview is transient Presenter session state; the release is the only journal event; an inert drag commits nothing.** New Presenter fields (group start placements keyed by layer, group pivot, group grab point) mirror the single-object `gizmo_cell_start` family; on release the batch is built and dispatched once; an identity delta / Space-held / zero-live-member drag is guarded out.
*Rationale:* it is the shipped gizmo's proven shape (D-gizmo-4 + the `!(preview == start)` guard, `canvas_view.cpp:432`); the journal stays one-entry-per-completed-gesture and the transient camera/preview never leaks into undo (D15's transient-vs-scene line). **No doc delta.**

## Open questions

(none — all decided.) *Note:* multi-object **align / distribute** commands are a natural future sibling that would reuse `transform_cells_command`, but no `D`/`A` row promises them and nothing in this leaf's scope needs them — they are neither in scope nor deferred here (registering them would be inventing scope). If ever specified, they become their own WBS leaf consuming this seam.

## Status

**Done** — 2026-07-29.

- Pure L1 `group_transform` compose-delta helper added to `src/interact/ace/interact/interact.hpp` + `src/interact/interact.cpp`: computes `D = new_union ∘ old_union⁻¹` and composes it onto each member; degenerate (all-unbounded / 1-member / identity) → unchanged. `finite_affine` guard added.
- `commands::LayerTransform` + `commands::transform_cells_command` added to `src/commands/ace/commands/cells.hpp` + `src/commands/cells.cpp`: one `doc.transact("group_transform")`, live-layer-validated, N `set_transform` calls, one commit — exactly one journal entry, atomic publish.
- L4 `app::CanvasView` group-gizmo fields, `draw_group_gizmo` (union-box grab/preview/snap/release → batch dispatch) added to `src/app/ace/app/canvas_view.hpp` + `src/app/canvas_view.cpp`; wired after `draw_cell_gizmos`; `draw_selection` press-guard extended; `draw_frame_gizmos` defers a selected camera in a ≥2 selection to the group gizmo.
- L1 Catch2 units `tests/group_transform_test.cpp` (5 cases: pure-delta / order-independence, identity + degenerate guards, one-entry + atomicity + undo incl. stale/empty, D8 invariance).
- ImGui Test Engine e2e `tests/group_transform_e2e_test.cpp` (group gizmo scale, snap, Cmd bypass, rotate, shear, pivot no-op, mixed camera+cell).
- `render_offline` golden `tests/goldens/group_transform_scale_64x64.rgba8` (two bounded rasters group-scaled ~2×, byte-exact, pins D8).
- ASan/TSan anchor appended to `tests/canvas_host_test.cpp` (group batch vs render + `pick_targets`).
- Both new test files registered in `CMakeLists.txt`.
- `tests/gizmo_e2e_test.cpp` section (vi) updated to assert the shipped always-on group gizmo behavior (body drag over a 2-cell selection now moves both as one journal entry) — a stale-assumption update required by the group gizmo flip.
