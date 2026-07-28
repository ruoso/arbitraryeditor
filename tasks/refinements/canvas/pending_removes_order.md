# editor.canvas.pending_removes_order — a remove pre-empts a still-queued add for the same id

## TaskJuggler entry

- **Task:** `editor.canvas.pending_removes_order` (`tasks/00-editor.tji:340-345`).
- **Effort:** `0.5d` (`.tji:341`).
- **Depends:** `!writer_thread` (`.tji:343`) — the migration that deleted the
  document lease and left `drive_once`'s pending-map reconciliation as the sole
  render↔UI handoff.
- **Note:** `.tji:344` — "CanvasHost::drive_once services removes with a bulk
  clear (`src/render/canvas_host.cpp:319-329`) while it consumes
  pending_resizes/pending_cameras PER-ID … a remove PRE-EMPTS a still-queued
  add — the entry never surfaces."
- **Back-link:** the closer appends `Refinement:
  tasks/refinements/canvas/pending_removes_order.md` to the `.tji` note and adds
  `complete 100` after `allocate team` in the same commit that lands the
  implementation. Do not hand-edit the `.tji`.
- **Source of debt:** `tasks/parking-lot.md` 'CanvasHost pending_removes drop
  window' (`D-canvas_host-pending_removes-drop`), a fixer note raised during
  `editor.canvas.accent_palette` (`tasks/refinements/canvas/accent_palette.md:436`
  records the sibling pending-resize/camera drop-window fix); triaged and
  decided 2026-07-24.
- **Downstream dependents:** `editor.canvas.settler_attach_split` (`.tji:371`,
  `depends !arbc_v040, !pending_removes_order`) — it rewrites how the viewport
  entries the pending maps name are CONSTRUCTED, and depends on this leaf so the
  add/remove pre-emption rule is settled *before* the construction path moves
  underneath it (`.tji:375`, ORDERING).
- **Milestone:** `m9_editor` (via `editor.canvas`, `tasks/99-milestones.tji:8`).

## Effort estimate

**0.5d.** This is a narrow correctness fix inside one function
(`CanvasHost::drive_once`), one class (`src/render/canvas_host.cpp` /
`ace/render/canvas_host.hpp`), and one test file (`tests/canvas_host_test.cpp`).

Where the budget goes:

- The per-id remove-servicing loop: cancel a removed id's still-queued add in
  `pending_adds`, alongside the existing entries-map extract and the existing
  `pending_resizes`/`pending_cameras` erase.
- A test-only drive-phase seam so the "remove interleaved between the swap and
  the lock" window is reproducible deterministically (see Decisions).
- Three Catch2 cases in `tests/canvas_host_test.cpp`.

Where it does NOT go: no new component, no new levelization edge, no rendered
output change (a suppressed entry never surfaces, so no golden moves), no UI
seam, no libarbc surface. The pin bump and its churn are
`editor.canvas.arbc_v040`'s, already `complete 100`, and are not touched here.

## Inherited dependencies

**Settled (consumed as-is):**

- `editor.canvas.writer_thread` (`complete 100`) — deleted the writer-priority
  document lease (`D-writer_thread-11`) and `CanvasHost::apply_edit`, so
  `drive_once`'s pending-map reconciliation is now the only render↔UI handoff.
  The pending maps (`pending_adds`/`pending_removes`/`pending_resizes`/
  `pending_cameras`, `canvas_host.cpp:121-124`) and the two-lock structure of
  `drive_once` are the surviving shape this leaf edits. The `on_writer` post
  seam (`canvas_host.cpp:111-117`, `D-writer_thread-8`) is used unchanged.

**Pending (owned here):** nothing. `writer_thread` is `complete 100`. This leaf
lands against the host as it stands today (`.tji:375` deliberately sequences it
before `settler_attach_split` so it is not rebased onto a restructured
lifetime).

## What this task is

Close the drop window where a `CanvasHost::remove(id)` fails to pre-empt a
same-id `add(id)` that is still queued, so the removed canvas resurrects one
iteration later. Concretely:

1. **Cancel the queued add on remove.** In `drive_once`'s remove-servicing loop
   (`canvas_host.cpp:319-329`), for each id in `pending_removes` also erase any
   queued `PendingAdd` for that id from `impl_->pending_adds` — in addition to
   the existing live-entry extract/erase and the existing
   `pending_resizes`/`pending_cameras` erase. A remove now pre-empts a
   still-queued add: the entry never surfaces.
2. **Keep removes fully consumed per iteration.** `pending_removes.clear()`
   (`:329`) stays: a remove naming an id that is neither live, nor queued as an
   add, nor carrying a queued resize/camera is a silent no-op — not an error,
   not left queued — matching `add`'s existing idempotence
   (`canvas_host.cpp:294-296`). (Removes are NOT left queued the way resizes are:
   a resize outlives its iteration because the add that gives it an entry may
   arrive later; a remove has nothing to wait for — a later `add(id)` is a NEW
   canvas the user asked for and a stale remove must not kill it.)
3. **Preserve the ordering invariant.** The fresh-entry insert (`:315-317`)
   continues to precede remove servicing (`:319-329`), so a remove pre-empts an
   add serviced *in the same iteration* (the add is inserted, then extracted
   into `dying`). The new `pending_adds` scan covers the complementary case: an
   add posted *after* the iteration's `pending_adds.swap` (`:288-292`), still
   sitting in `pending_adds` when removes run.
4. **A test-only drive-phase seam** exposing the between-swap-and-lock window so
   case (3) is a deterministic Catch2 regression, not a flaky race (see
   Decisions).

Out of scope, by charter:

- Viewport/settler lifetime restructuring — `editor.canvas.settler_attach_split`
  (`.tji:371`) owns moving HostViewport construction/destruction to
  `attach_settler`/`detach_settler`; this leaf leaves that path exactly as it is.
- Resize/camera deferral semantics — already settled and tested
  (`tests/canvas_host_test.cpp:691-713`, "a resize/camera for a not-yet-live
  entry is DEFERRED not dropped"); this leaf only mirrors the *per-id* discipline
  onto the remove path and reuses that test's construction.

## Why it needs to be done

`drive_once` swaps `pending_adds` under the first lock (`:288-292`) and builds
the fresh entries OFF the lock (`:293-310`, so the blocking `on_writer`
DamageRouter build never holds `mu`), then re-takes the lock to insert the fresh
entries, service removes, and snapshot per-entry resize/camera (`:312-358`).

The removes step iterates `pending_removes` per-id but only reconciles the
entries map and the resize/camera maps; it never touches `pending_adds`, then
bulk-`clear()`s the remove list. That leaves a window:

- The UI thread posts `add(X)` **after** the iteration's `pending_adds.swap`, so
  `X` is in `pending_adds` but not in the swapped-out `adds` local.
- The UI thread posts `remove(X)`.
- Under the second lock, removes run: `X` is not in `entries` (never added), so
  the remove does nothing observable and is cleared.
- The next iteration swaps `pending_adds` (still `[X]`) and inserts `X` — the
  entry the caller removed resurrects.

This is the same class of bug the sibling `pending_resizes`/`pending_cameras`
per-id fix already closed (`canvas_host.cpp:331-341`; the `accent_palette` fixer
note at `accent_palette.md:436`), just on the add/remove axis instead of the
size/camera axis. Settling it now — before `settler_attach_split` moves the
construction path — keeps the pre-emption rule fixed under the smaller leaf and
saves a rebase (`.tji:375`).

## Inputs / context

**Governing design docs (normative — the constitution):**

- `docs/01-architecture.md` A5 (§5, lines 197-208): "Multi-canvas = N
  `HostViewport`/`InteractiveRenderer` over one `Document` sharing one
  `WorkerPool`; **no new locking**." The pending-map reconciliation is the "no
  new locking" surface — this fix adds no lock and no cross-thread edge.
- `docs/01-architecture.md` A6 (§6, lines 211-242): the display path; unaffected
  (no rendered-output change — a pre-empted entry never composites).
- `docs/01-architecture.md` A4.1b (§4, lines 157-193): "the document owns ONE
  writer thread; the UI and render threads are pure submitters and readers." The
  UI thread's `add`/`remove`/`request_resize`/`request_camera` are pure submits
  onto host-owned queues drained on the render thread; this leaf keeps them so.
- `docs/01-architecture.md` §8 (lines 276-312): `render` is L2 (`base, project,
  scene, gl, writer, libarbc`; GL, not ImGui). This fix stays inside `render`.
- `docs/01-architecture.md` §9 (lines 314-378): the universal DoD.

  (The pending add/remove/resize/camera maps and `drive_once` are code-level
  `CanvasHost`/multi_canvas contract, not named in the architecture doc — the
  A-rows above are the constitution the reconciliation lives under, and the
  concrete map plumbing is in the source below.)

**Sources to change:**

- `src/render/canvas_host.cpp:259-358` — `drive_once`. The swap
  (`:288-292`), the off-lock fresh build (`:293-310`), the fresh insert
  (`:315-317`), the remove loop (`:319-329`, the change site), and the per-id
  resize/camera consume (`:331-357`).
- `src/render/canvas_host.cpp:74-79,121-124` — `PendingAdd` and the four pending
  members. `pending_adds` is `std::vector<PendingAdd>`; the cancel is an
  erase-by-id (`std::erase_if`/`remove_if`+`erase`) matching on `PendingAdd::id`.
- `src/render/canvas_host.cpp:212-229` — `add`/`remove` (the UI-thread posters);
  unchanged, but the test seam posts through them.
- `src/render/ace/render/canvas_host.hpp:87-142` — the public contract; the
  `add`/`remove` idempotence prose (`:87-89,106-108`) and `drive_once`'s
  "Public for deterministic single-threaded unit tests" note (`:137-142`) are
  where the test seam's sibling doc-comment belongs.

**Tests that must keep passing (regression floor):**

- `tests/canvas_host_test.cpp:256-273` — "add/remove mutate the live set" (a
  removed *live* id goes quiet).
- `tests/canvas_host_test.cpp:439-542` — the two TSan anchors (edit stream and
  content-REMOVAL stream) against the live render read; must pass **unmodified**
  (`.tji:344`).
- `tests/canvas_host_test.cpp:691-713` — "a resize/camera for a not-yet-live
  entry is DEFERRED not dropped" (the sibling per-id contract; the new
  add-then-remove case reuses its `settle(host)` construction).

**Harness:** `tests/canvas_host_test.cpp` with `make_inline_host()`
(`:68`, `WorkerPoolConfig{}` inline-degenerate + hour budget so one
`drive_once` settles) and `settle(host)` (`:614-620`). Deterministic,
single-threaded, thread-free. `scripts/gate` (check_levels · clang-format ·
build · ctest) is the umbrella; `gcc-tsan`/`clang-asan` lanes run the anchors.

## Constraints / requirements

1. **Remove pre-empts a still-queued add.** A `remove(id)` serviced while an
   `add(id)` sits un-swapped in `pending_adds` cancels that add; the entry never
   surfaces on any subsequent iteration. (The DECIDED semantics, `.tji:344` /
   parking-lot triage 2026-07-24.)
2. **Per-id consumption, symmetric with resize/camera.** Each removed id
   reconciles the entries map, `pending_adds`, `pending_resizes`, and
   `pending_cameras` individually. No bulk operation may drop a request the
   remove did not name.
3. **Idempotent no-op for an unknown id.** A remove naming an id that is neither
   live, nor a queued add, nor a queued resize/camera does nothing and is not an
   error — matching `add`'s idempotence (`:294-296`).
4. **Ordering invariant preserved.** Fresh-entry insert precedes remove
   servicing (a same-iteration add is pre-empted by extract-into-`dying`); the
   `pending_adds` scan covers the post-swap-queued add. Do not reorder the fresh
   insert after the removes.
5. **No new locking, no new thread edge, no `mu`-held blocking.** The cancel runs
   under the existing second lock (`:312-358`), touches only host-owned queues,
   and posts nothing to the writer thread while holding `mu` (`:277-280` —
   nothing added here posts at all). `render` stays L2 (§8); no
   ImGui/GL/SDL/libarbc surface is added.
6. **The test seam is inert in the app.** Null by default; only the headless
   inline fixtures set it; `run()` and the shipped bootstrap never touch it.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella.

- **Levelization (`check_levels` clean).** The change is confined to
  `src/render/canvas_host.{cpp,hpp}`: an erase-by-id in an existing loop plus a
  `std::function<void()>` member and setter. No new include, no new component,
  no new dependency edge; `render` stays L2 (`base, project, scene, gl, writer,
  libarbc`; no ImGui/GL/SDL added). `scripts/check_levels.py` stays clean.
- **L2 logic — Catch2 (`tests/canvas_host_test.cpp`), three new cases:**
  - `"canvas_host: an add and a remove for the same id in one iteration — the
    entry never surfaces"` — post `add("canvas#1", doc)` then `remove("canvas#1")`
    before a single `settle(host)`; REQUIRE `live_count() == 0`,
    `published_sequence("canvas#1") == 0`, and `consume("canvas#1", …) == false`.
    Guards the fresh-insert-then-extract path and pins the pre-emption semantic.
  - `"canvas_host: a remove interleaved between the pending-adds swap and the
    entry-map lock pre-empts the queued add"` — using the drive-phase seam
    (Decisions), post `add("canvas#1", doc)` + `remove("canvas#1")` from the hook
    that fires after the swap and before the second lock; drive to quiescence and
    REQUIRE the entry never surfaces (`live_count() == 0`,
    `published_sequence == 0`) across the following iterations. This is the case
    that fails on today's code (the entry resurrects) and passes with the fix.
  - `"canvas_host: a remove for an unknown id is a silent no-op"` — with nothing
    live or queued, `remove("ghost")` then `settle(host)`; REQUIRE no throw,
    `live_count()` unchanged, and a subsequent legitimate `add("ghost", doc)`
    still surfaces (the stale remove did not poison the later add).
- **Rendered output — golden: N/A, justified.** A pre-empted entry never builds
  a viewport and never composites, so no canvas frame changes and no
  `render_offline` golden moves. Introducing a golden here would assert nothing.
- **UI e2e — ImGui Test Engine: N/A, justified.** The reconciliation is a
  host-internal render-thread step below `views`/`dock`; no user-drivable widget
  behavior changes. The existing canvas nav e2e
  (`tests/canvas_nav_e2e_test.cpp`) must pass **unmodified**.
- **Threading (ASan/TSan).** The change adds no lock, no shared-state edge, and
  no writer post; the two standing anchors
  (`tests/canvas_host_test.cpp:439-542`, edit stream and content-removal stream)
  must pass **unmodified** in the `gcc-tsan`/`clang-asan` lanes as the
  concurrent-overlap coverage. No new `tests/lsan.supp` entry.
- **Regression — the existing suites must pass unmodified.** In particular
  `tests/canvas_host_test.cpp:256-273` (removed live id goes quiet) and
  `:691-713` (resize/camera deferral) are unchanged by this leaf.
- **Coverage — ≥90% diff coverage.** The three cases exercise every changed
  line (the `pending_adds` erase, the unknown-id no-op path, and the seam), so
  diff coverage clears the CI gate; tests ship in this commit.
- **Doc delta (same commit): none.** The pre-emption semantics are code-level
  multi_canvas contract already decided at parking-lot triage; A5/A6/A4.1b are
  unaffected (no new locking, no display change, submit/read roles unchanged).
- **Deferred WBS work: none.** No follow-up leaf is spawned (see Open questions
  for the one accepted-consequence noted for the human queue, not the WBS).

## Decisions

**D-pending_removes_order-1** — a remove pre-empts a still-queued add by
cancelling it in `pending_adds`, rather than being retained/queued.

*Rationale:* (i) it is the DECIDED semantics (`.tji:344`, parking-lot triage
2026-07-24) — "the entry never surfaces"; (ii) it is symmetric with the sibling
per-id resize/camera reconciliation already in place
(`canvas_host.cpp:331-341`), so the drive loop has one uniform discipline —
every removed id reconciles the entries map and all three queues it can name;
(iii) it is the minimal change — one erase-by-id in the existing remove loop,
under the existing lock. *Alternative rejected — leave the remove queued (like a
resize) until a live entry exists:* wrong, because a remove has nothing to wait
for. A resize is retained because a later add gives it meaning; a stale remove
retained across iterations would kill a genuinely-new later `add(id)` the user
asked for — the exact poisoning Constraint 3 forbids. *No doc delta required.*

**D-pending_removes_order-2** — keep the fresh-entry insert ahead of remove
servicing; add the `pending_adds` scan for the complementary window.

*Rationale:* (i) the swapped-and-built add is already handled correctly today —
insert into `entries` (`:315-317`) then extract into `dying` (`:319-325`) — so
the same-iteration add+remove path needs no change beyond a regression test;
(ii) the ONLY uncovered case is the add posted *after* the swap, which lives in
`pending_adds` at remove time, so scanning `pending_adds` there closes exactly
that gap; (iii) inserting-then-extracting the fresh entry is correct even though
briefly wasteful — the entry is created and destructed while `mu` is held across
the whole insert→remove→snapshot sequence, so no consumer ever observes it and
it is never stepped or published. *Alternative rejected — filter `fresh` before
insert to skip building a to-be-removed entry:* a micro-optimization for a rare
race that adds a second reconciliation site; the pre-emption's job is that the
entry never SURFACES (satisfied), not that a doomed Entry is never transiently
built. Deferred as unnecessary. *No doc delta required.*

**D-pending_removes_order-3** — a test-only drive-phase seam
(`set_after_adds_swap_hook(std::function<void()>)`) makes the
between-swap-and-lock window deterministic.

*Rationale:* (i) single-threaded, nothing runs between the swap (`:292`) and the
second lock (`:313`), so the post-swap-queued-add window is unreachable through
the public API alone — a hook that fires once there, after the swap releases the
first lock and before `fresh` is built, lets a headless test post the exact
`add`+`remove` interleave the bug needs; (ii) it is inert in the app (null by
default; only the inline fixtures set it; `run()` and the bootstrap never touch
it) and levelization-clean (a `std::function<void()>` member — no new include,
no edge), in the same spirit as `drive_once` being "Public for deterministic
single-threaded unit tests" (`canvas_host.hpp:137-142`); (iii) it yields a
deterministic pass/fail regression rather than a flaky window race.
*Alternative rejected — a threaded stress that hammers same-id add/remove under
the real render loop and asserts the id stays removed:* it exercises real
overlap but is non-deterministic — it can go green with the bug present when the
narrow window is not hit, which is a false regression guard. The standing
content-removal TSan anchor (`:489-542`) already provides the concurrent-overlap
signal, so the new case is deterministic instead. *No doc delta required* — a
test-only seam is an implementation detail of the L2 component, not an
architectural decision.

## Open questions

(none — all decided.)

One accepted consequence is recorded for the human queue, NOT the WBS: with
remove-pre-empts-add, a UI sequence of `add(X)` → `remove(X)` → `add(X)` whose
three calls collapse into one drive iteration cancels the *final* re-add, so X
does not surface until a later poke re-adds it. This follows directly from
D-pending_removes_order-1 (the host cannot reconstruct sub-iteration ordering —
`pending_adds`/`pending_removes` are unsequenced vectors) and matches the
triaged "remove pre-empts" rule; it is surfaced to the parking lot for
awareness, not as a task (no agent-implementable deliverable — it is the
intended semantics).

## Status

**Done** — 2026-07-28.

- `src/render/ace/render/canvas_host.hpp`: declared `set_after_adds_swap_hook(std::function<void()>)` test seam; added matching `after_adds_swap_hook` member to `Impl`.
- `src/render/canvas_host.cpp`: added `after_adds_swap_hook` Impl member + setter; fires the hook off-lock between the pending-adds swap and the entry-map lock; in the remove loop, `std::erase_if(pending_adds, …)` cancels a still-queued same-id add.
- `tests/canvas_host_test.cpp`: three new Catch2 cases — "an add and a remove for the same id in one iteration — the entry never surfaces"; "a remove interleaved between the pending-adds swap and the entry-map lock pre-empts the queued add" (uses the seam; confirmed to FAIL on unfixed code); "a remove for an unknown id is a silent no-op".
- The test seam is inert in the app (null by default); `render` stays L2, no new edge, no golden/e2e move.
- One accepted consequence noted for the parking lot: `add(X)→remove(X)→add(X)` collapsing in one drive iteration cancels the final re-add (intended semantics per D-pending_removes_order-1).
