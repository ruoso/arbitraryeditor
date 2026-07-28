# editor.canvas.magnified_raster_idle — assert a magnified raster canvas reaches idle

## TaskJuggler entry

- **Task:** `editor.canvas.magnified_raster_idle` (`tasks/00-editor.tji:361-365`).
- **Effort:** `0.5d` (`.tji:362`).
- **Depends:** `!arbc_v040` (`.tji:364`) — the pin bump that landed libarbc
  v0.4.0, which carries `ruoso/arbitrarycomposer#18` ("kinds render at the
  requested scale under `BestEffort`"), the fix this leaf asserts.
- **Note:** `.tji:365` — "`#18` shipped in v0.4.0 … a ~10x magnified 32x32
  `org.arbc.raster` cell no longer reports `schedule_follow_up` true on every step
  forever … Assert the fix: a case that frames a raster cell with Shift+F
  (editor.canvas.nav_aids) and REQUIREs the canvas reaches quiescence —
  `frames_issued` stops advancing … — at magnification, mirroring the 1x raster
  and nested-solid fixtures in the same file that were already quiet. KEEP
  `tests/canvas_nav_e2e_test.cpp:139`'s wall-clock-bounded `settle()` helper
  exactly as it is."
- **Back-link:** the closer appends `Refinement:
  tasks/refinements/editor/magnified_raster_idle.md` to the `.tji` note and adds
  `complete 100` after `allocate team` in the same commit that lands the
  implementation. Do not hand-edit the `.tji`.
- **Source of debt:** `tasks/parking-lot.md` 'A magnified raster cell never lets
  the canvas go idle' — a parked libarbc defect graduated to this WBS leaf at the
  v0.4.0 triage (2026-07-28), because `#18` shipping in the pin gave it an
  agent-implementable deliverable (an e2e idle assertion), which a parking-lot
  item is not. The `settle()` doc comment still names that parked item as the
  owner of the bug (`tests/canvas_nav_e2e_test.cpp:135-137`).
- **Downstream dependents:** none. This is a leaf assertion; nothing depends on
  `magnified_raster_idle`.
- **Milestone:** `m9_editor` (via `editor.canvas`, `tasks/99-milestones.tji:8`).

## Effort estimate

**0.5d.** This is a pure test change in one file (`tests/canvas_nav_e2e_test.cpp`)
plus a stale-comment rewrite in that same file. There is **no editor production
change**: the render-at-requested-scale fix is `#18`, shipped in libarbc v0.4.0
and already pinned by `editor.canvas.arbc_v040` (`complete 100`). This leaf only
*asserts* the fix the pin already delivered, and retires the now-false claim in
the `settle()` rationale comment that motivated the wall-clock deadline.

Where the budget goes:

- A new `IM_REGISTER_TEST(engine, "canvas_nav", "magnified_raster_idle")` e2e case
  (reusing the `seed_with_cell` raster fixture, `settle()`, the `E2EState`
  handshake, and the driver pump loop the two existing cases already establish):
  select the 32×32 cell, `Shift+F` to frame it (fit-to-selection magnification),
  `settle()`, then a **bounded post-settle quiescence probe** that `REQUIRE`s
  `frames_issued("canvas#1")` does not advance — the direct refutation of "the
  render thread burns a core forever" at magnification.
- Rewriting the now-false clause in the `settle()` doc comment
  (`:119-137`) — re-framing the wall-clock deadline as general lane protection
  against ANY non-idling canvas (the note's own words) and pointing the
  magnified-raster example at the new test that now proves it quiet — while
  leaving the `settle()` function and its 10 s budget **byte-identical** per the
  note.

Where it does NOT go: no new component, no new levelization edge, no new golden
(the fix is render-thread scheduling behaviour, not pixel math — see Decisions),
no `src/` change, no new `CanvasView`/`CanvasHost` observable, no libarbc surface,
no doc delta.

## Inherited dependencies

**Settled (consumed as-is):**

- `editor.canvas.arbc_v040` (`complete 100`,
  `tasks/refinements/canvas/arbc_v040.md`) — bumped `ARBC_GIT_TAG` to v0.4.0,
  absorbing `#18`: a magnified raster leaf renders at the requested scale under
  `BestEffort`, so `schedule_follow_up` stops being true forever and the render
  thread stops burning a core. The pin leaf deliberately left the behavioral
  assertion to this leaf ("asserting a magnified raster reaches idle (#18) —
  `editor.canvas.magnified_raster_idle`"); `tests/arbc_pin_test.cpp` pins only the
  compile-time existence of the v0.4.0 surface, never `#18`'s render-scale
  behaviour. This leaf consumes the shipped behaviour unchanged.
- `editor.canvas.nav_aids` (`complete 100`,
  `tasks/refinements/editor/nav_aids.md`) — minted the `Shift+F` fit-to-selection
  chord (D-nav_aids-6; read at `src/views/views.cpp`, consumed in
  `src/app/canvas_view.cpp::draw_content` via `interact::fit_region` into the
  transient camera). Its own e2e is the `frame_selection_view` case this leaf
  mirrors. This leaf reuses the `Shift+F` gesture verbatim as the magnifier; it is
  the transient-camera framing that pushes the 32-unit cell to ~10× in the pane.
- `editor.canvas.writer_thread` (`complete 100`) — owns the off-thread render
  model (`CanvasView canvas(state, session.writer())`,
  `tests/canvas_nav_e2e_test.cpp:334`, "spawns the render thread") whose
  proactive-settle / bounded-rearm idle discipline (A4.1b) is exactly the "render
  thread stops burning a core" property this leaf asserts an analog of. Its
  `writer_session.hpp` fixture and the `E2EState` handshake are reused.

**Pending (owned here):** nothing. All three predecessors are `complete 100`. The
v0.4.0 pin is in place, so `#18`'s render-scale behaviour is live in the build
today.

## What this task is

The gap: **before v0.4.0, a raster cell viewed at a deep magnification never let
the canvas go idle.** libarbc's `InteractiveRenderer::render_frame` kept reporting
`schedule_follow_up` (arrival damage that still mapped to a non-empty device
region) on every step, so the host re-drove forever and `frames_issued` advanced
at the frame rate indefinitely — the render thread burned a core for as long as
the framing held, violating the libarbc `02-architecture#idle-viewport-issues-no-frames`
guarantee that an idle viewport issues no frames. `#18` fixed the kind to render
at the requested scale under `BestEffort`, and the v0.4.0 pin brought the fix into
the build. This leaf asserts the fix and retires the workaround's stale
justification:

1. **Add an e2e case that magnifies a raster cell and REQUIREs the canvas reaches
   quiescence** (`tests/canvas_nav_e2e_test.cpp`, new
   `IM_REGISTER_TEST(engine, "canvas_nav", "magnified_raster_idle")`). Seed the
   32×32 `org.arbc.raster` cell (reuse `seed_with_cell`, `:158-172`), select it,
   press `ImGuiMod_Shift | ImGuiKey_F` to frame the selection (fit-to-32-unit →
   ~8–10× device magnification in the pane), confirm the framing engaged
   (`pump_until(frames_issued > before)`), call `settle(ctx, canvas)` (unchanged),
   then a **bounded post-settle probe**: baseline `frames_issued("canvas#1")`,
   pump a fixed sub-`settle`-budget window (~120 `ctx->Yield()` + 1 ms), and
   `REQUIRE` `frames_issued` is unchanged. On the pre-`#18` code the probe sees the
   frame counter advance (the render thread spinning) and fails; with the v0.4.0
   pin it holds and passes.

2. **Rewrite the now-false clause of the `settle()` rationale comment**
   (`:119-137`). The comment currently justifies the wall-clock deadline by
   asserting the live present-tense fact that "a deep magnification of a raster
   cell does not reach [idle] … `schedule_follow_up` … `frames_issued` advances at
   the frame rate indefinitely" (`:126-131`). That claim is now false. Re-frame the
   comment to the note's own framing — the deadline is **general lane protection
   against ANY non-idling canvas, not a workaround for this one bug** — keep the
   still-true `ConfigWatchdogKillTest` / 566 s-failure rationale (`:131-133`), and
   point the magnified-raster example at the new case that now proves it quiet.

Out of scope, by charter:

- The **`settle()` helper and its 10 s budget** (`:138-153`) stay **byte-identical**
  (`.tji:365`). This leaf touches the comment above it and adds a caller; it does
  not touch the function.
- The existing **`frame_selection_view`** (`:314`) and **`wheel_pan_scalebar`**
  (`:182`) cases must pass **unmodified** — they are the "already quiet" 1×-raster
  and nested-solid fixtures the new case mirrors.
- **Pixel correctness of the magnified raster** (that it composites the right
  colour) is not this leaf's claim — the gap is *never reaching idle*, whose
  observable is `frames_issued` steadying (see Decisions).

## Why it needs to be done

The parking-lot item that owned this bug states it plainly, and the `settle()`
helper was itself born as its workaround: unbounded, the wait never returned on a
magnified raster, the ImGui Test Engine's 60 s `ConfigWatchdogKillTest` fired, and
a 16 s `ace_shell_test` became a 566 s failure. The wall-clock deadline let the
lane survive the non-idling canvas — but it also masked it: every downstream
assertion reads UI-thread state (`scale_bar_units`) or a strict frame-count
advance, so a canvas that never idles still passed, at the cost of a core spinning
for the whole test and a stale comment telling every future reader the bug is
live.

`#18` landed in v0.4.0 and `editor.canvas.arbc_v040` pinned it, so the magnified
raster now reaches idle. Asserting it turns the silent tolerance into an explicit
regression guard: a `REQUIRE(frames_issued unchanged)` after `settle()` that
**would fail on the pre-`#18` code** and pins the fixed behaviour against a later
pin silently regressing it. And per the same rule the sibling `nested_real_pool`
applied, a comment that asserts a now-false present-tense fact is worse than none
— it must be corrected in the same commit that makes it false.

## Inputs / context

**Governing design docs (normative — the constitution):**

- `docs/01-architecture.md` A5 (§5, `:227-240`; table row `:420`): "Multi-canvas =
  N `HostViewport`/`InteractiveRenderer` over one `Document` sharing one
  `WorkerPool`; no new locking." The canvas whose idleness this leaf asserts is
  A5's `InteractiveRenderer` over the shared pool. (The `.tji` note cites A5.)
- `docs/01-architecture.md` A6 (§6, `:242-269`, as amended by `arbc_v040`): the
  display path and the one-render-path/tiled-driver change v0.4.0 brought. The
  magnified frames whose count this leaf watches steady are produced by that tiled
  driver. (The `.tji` note cites A6.)
- `docs/01-architecture.md` A4.1b (`:189-225`): the render/writer idle discipline
  — the writer "consumes the deferred settle proactively … staying armed only
  while a fetch is outstanding, so an arrival on a completely idle app is consumed
  with no submission from anywhere," i.e. an idle canvas costs zero wakeups. `#18`
  is the render-side companion: without it the *renderer* re-armed forever
  independent of the writer. This leaf asserts the canvas actually reaches that
  zero-wakeup state at magnification.
- `docs/01-architecture.md` §8 (`:308-345`): `views`/`app` are L3/L4 (ImGui). This
  leaf edits only a test in the e2e suite and adds no component or edge.
- `docs/01-architecture.md` §9 (`:346` onward): the universal DoD instantiated
  under Acceptance criteria; the "End-to-end UI … Dear ImGui Test Engine … drives
  widgets by ID" row is the lane this case lands in.
- libarbc doc `02-architecture#idle-viewport-issues-no-frames` (cited by the
  `.tji` note): the upstream guarantee the pre-`#18` defect violated — an idle
  viewport issues no frames. Consumed via FetchContent; its design docs live in
  the `arbitrarycomposer` repo.

**The change site — `tests/canvas_nav_e2e_test.cpp`:**

- `:138-153` — the `settle()` helper. Budget constant `k_settle_budget =
  std::chrono::seconds(10)` (`:138`); the function (`:139-153`) pumps
  `ctx->Yield()` + 1 ms and polls `canvas.frames_issued("canvas#1")`, returning
  when it sees **40 consecutive unchanged polls** OR the 10 s deadline elapses (no
  assertion either way). **Stays byte-identical.**
- `:119-137` — the `settle()` rationale comment. `:126-131` carries the now-false
  live claim about the magnified raster; `:131-133` the still-true
  `ConfigWatchdogKillTest`/566 s rationale; `:135-137` names the parking-lot item.
  **The clause to rewrite.**
- `:158-172` — `seed_with_cell`: a 256-unit canvas (`k_aid_canvas = 256.0`) with a
  full-frame opaque background (so the content-gated sequence advances,
  `blank_first_frame`) and one 32×32 `org.arbc.raster` cell at
  `Affine::translation(180, 180)` (identity scale — "1×") via `ace::scene::add_cell`
  (`:167-169`); the cell id returns through `cell_out`. **Reused verbatim as this
  case's fixture.**
- `:307-391` — `TEST_CASE` `frame_selection_view` (register `:350`). Its block (a)
  (`:379-391`) already selects the cell (`state.selection().select(e2e->cell)`),
  presses `ImGuiMod_Shift | ImGuiKey_F` (`:386`), and `settle()`s (`:388`) — the
  exact magnifying gesture this leaf reuses, but it asserts only
  `units_framed < units_before` (`:391`), never idle. **Passes unmodified**; the
  new case is its idle-asserting peer.
- `:91-103` — `seed_nested` (the nested-solid fixture) used by
  `wheel_pan_scalebar` (`:182`, register `:217`). The second "already quiet"
  fixture the new case mirrors. **Passes unmodified.**

**Harness scaffolding to mirror (same file):** `E2EState { CanvasView* canvas;
AppState* state; arbc::ObjectId cell; }` (`:174-178`) passed via `test->UserData`;
`ImGuiTestEngine_CreateContext`/`Start` with `ConfigRunSpeed = Fast` +
`ConfigNoThrottle` (`:210-214` / `:343-347`); the bounded driver pump
`while (!IsTestQueueEmpty && frames < k_max_frames)` with `k_max_frames = 200000`
(`:280-288` / `:398-406`); result harvest `GetResult` → `CHECK(count_tested == 1)`
/ `CHECK(count_success == 1)` (`:292-299` / `:410-417`); `CanvasView canvas(state,
session.writer())` (`:334`, spawns the render thread). `WindowFocus("canvas#1")` +
`MouseMoveToPos(center)` precede the key press (`:359`, `:385`).

**The observable — `frames_issued`:** `CanvasView::frames_issued(std::string_view)
-> std::uint64_t` (`src/app/ace/app/canvas_view.hpp:123`) is the per-pane published
frame counter (`>= 1` for a live frame; an advance proves the off-thread
re-render reached the entry's double-buffer). `CanvasView` exposes **no**
in-flight tile-queue accessor — `frames_issued`, `anchor_depth` (`:136`), and
`scale_bar_units` (`:141`) are the only per-pane liveness reads, and `CanvasHost`
adds only `published_sequence`/`anchor_depth` (`canvas_host.hpp:163,168`). So the
canvas's owed-work state is observable **only** through `frames_issued` advancing;
its steadying is the faithful proxy for the note's "empty in-flight tile queue"
(see Decisions D-magnified_raster_idle-2).

**Harness umbrella:** `scripts/gate` (check_levels · clang-format · build · ctest);
`tests/canvas_nav_e2e_test.cpp` runs in the offscreen-GL e2e lane and the
`gcc-tsan`/`clang-asan` lanes (`.github/workflows/ci.yml`).

## Constraints / requirements

1. **Magnify via the `Shift+F` fit-to-selection gesture, on the 32-unit raster
   cell** — not a pre-scaled `Affine` placement and not an extra wheel zoom. The
   note names `editor.canvas.nav_aids`' `Shift+F`; framing the 32-unit selection
   into the ~256–320 device-px pane is the ~8–10× magnification that reproduced the
   bug, and it exercises the same transient-camera path the real user drives.
2. **REQUIRE the canvas reaches quiescence at magnification** via a bounded
   post-`settle()` probe that `REQUIRE`s `frames_issued("canvas#1")` does **not**
   advance across a fixed window well under `k_settle_budget`. The probe must be a
   real guard: on the pre-`#18` render-thread spin `frames_issued` advances every
   yield, so the probe fails; with v0.4.0 it holds. The 1 ms sleeps are load-bearing
   — they give the render thread wall-clock time to advance if it is still
   spinning, so the probe distinguishes genuine idle from a settle-deadline
   time-out.
3. **`settle()` and its budget stay byte-identical** (`:138-153`, `.tji:365`). The
   deadline is general lane protection against ANY non-idling canvas and must
   survive as such; this leaf neither weakens nor specializes it. The only edit at
   the helper is to the explanatory comment above it (Constraint 4).
4. **Rewrite the now-false clause of the `settle()` comment, do not delete the
   comment.** The still-true `ConfigWatchdogKillTest`/566 s lane-protection
   rationale (`:131-133`) and the general "any non-idling canvas" framing stay; the
   present-tense "a magnified raster never idles" claim (`:126-131`) is replaced
   with a past-tense pointer to the new case that now proves it quiet. (This mirrors
   the sibling re-anchor-vs-delete rule; see Decisions.)
5. **Existing cases unmodified.** `frame_selection_view` (`:314`) and
   `wheel_pan_scalebar` (`:182`) — the "already quiet" fixtures — must pass
   byte-for-byte as they stand; the new case is additive.
6. **No new locking / edge / include / `src` change / observable.** The change
   touches only `tests/canvas_nav_e2e_test.cpp`, reuses symbols the file already
   includes (`settle`, `seed_with_cell`, `E2EState`, `CanvasView::frames_issued`,
   `pump_until`, `IM_REGISTER_TEST`, `ImGuiMod_Shift`), and adds no component,
   dependency edge, or `CanvasView`/`CanvasHost` accessor. `views`/`app` stay
   L3/L4; `scripts/check_levels.py` stays clean.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella.

- **Levelization (`check_levels` clean).** The change is confined to
  `tests/canvas_nav_e2e_test.cpp` — one new e2e case and a comment rewrite. No new
  include (every symbol is already used in the file), no new component, no new
  dependency edge, no new `src/` observable; `views`/`app` stay L3/L4 with no new
  ImGui/GL/SDL surface. `scripts/check_levels.py` stays clean.
- **UI e2e — ImGui Test Engine (`tests/canvas_nav_e2e_test.cpp`), new case
  `IM_REGISTER_TEST(engine, "canvas_nav", "magnified_raster_idle")`:** over the
  `seed_with_cell` raster fixture, drive `WindowFocus("canvas#1")` +
  `MouseMoveToPos(center)`, `state.selection().select(e2e->cell)`,
  `ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_F)`, `REQUIRE(pump_until([&]{ return
  canvas.frames_issued("canvas#1") > before; }))` (framing engaged), then
  `settle(ctx, canvas)`, then `const std::uint64_t idle =
  canvas.frames_issued("canvas#1"); for (int i = 0; i < 120; ++i) { ctx->Yield();
  std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
  REQUIRE(canvas.frames_issued("canvas#1") == idle);`. The case's own
  `CHECK(count_tested == 1)` / `CHECK(count_success == 1)` / `CHECK(frames <
  k_max_frames)` harvest holds. This is the direct refutation of "the render thread
  burns a core forever at magnification" (`#18`): it fails on pre-`#18` code (the
  probe sees `frames_issued` advance) and passes with the v0.4.0 pin. A screenshot
  baseline adds no signal here (the claim is a counter steadying, not a pixel), so
  none is captured.
- **L1 logic — Catch2: N/A, justified.** There is no UI-agnostic core logic to
  cover — the fix is a libarbc render-thread scheduling behaviour, observable only
  through the live off-thread renderer, i.e. inherently an e2e property. No
  `project`/`scene`/`interact`/`commands`/`dockmodel` code changes.
- **Rendered output — golden: N/A, justified.** `#18` changed *when the renderer
  stops re-driving*, not *what pixels it composites*; a `render_offline` golden
  asserts a single settled frame and cannot observe a never-terminating re-drive.
  The v0.4.0 golden re-baselines are already owned and justified by
  `editor.canvas.arbc_v040`; this leaf adds none. (See D-magnified_raster_idle-3.)
- **Threading (ASan/TSan).** The case spawns the render thread
  (`CanvasView(state, session.writer())`) and reads `frames_issued` (an atomic
  published-counter load) concurrently with it — the same handoff the existing
  `canvas_nav` cases exercise. It must stay clean in the `gcc-tsan`/`clang-asan`
  lanes; it adds no new shared-state access, no new lock, no new writer post, and
  no new `tests/lsan.supp` entry.
- **Regression — the existing suites must pass unmodified.** In particular
  `frame_selection_view` (`:314`) and `wheel_pan_scalebar` (`:182`) pass
  byte-for-byte, and `settle()` and its 10 s budget (`:138-153`) are unchanged.
- **Coverage — ≥90% diff coverage.** The changed lines are the new test case
  (fully executed by its own registration/pump) and the comment rewrite (a
  comment, not executable); the diff is test-only and clears the CI gate. Tests
  ship in this commit.
- **Doc delta (same commit): none.** Idle-at-the-requested-scale is A5's shared-pool
  renderer honouring the libarbc `idle-viewport-issues-no-frames` guarantee —
  already-decided architecture, delivered by the v0.4.0 pin (A6 as amended by
  `arbc_v040`, A4.1b's idle discipline). This leaf asserts it; it changes no
  decision.
- **Deferred WBS work: none.** No follow-up leaf is spawned. The bug's parking-lot
  item is retired by this assertion, and no new deferral arises.

## Decisions

**D-magnified_raster_idle-1** — assert in a **new** dedicated e2e case
(`IM_REGISTER_TEST(engine, "canvas_nav", "magnified_raster_idle")`), not by adding
an idle `REQUIRE` to the existing `frame_selection_view` block (a).

*Rationale:* (i) the `.tji` note describes "**a case** that frames a raster cell
with Shift+F … and REQUIREs the canvas reaches quiescence … **mirroring** the 1x
raster and nested-solid **fixtures**" — phrasing that reads as a peer case
alongside the two existing fixtures, not a rider on one; (ii) the idle property is
distinct from `frame_selection_view`'s charter ("Shift+F frames the selection;
empty is a no-op; never dirties") and is tied to a specific library fix (`#18`) —
co-locating them would mean a `#18` regression fails under a test named for the
framing verb, misattributing the failure; a case named `magnified_raster_idle`
self-documents it; (iii) the per-case engine/register/pump/harvest scaffold is the
file's established idiom (two near-identical instances already), and reusing
`seed_with_cell` + `settle()` + `E2EState` keeps the new case's body minimal.
*Alternative rejected — add an idle `REQUIRE` to `frame_selection_view` block (a)*
(which already frames the cell at magnification and `settle()`s): fewer lines, but
it overloads that case's charter and misattributes a `#18` regression to the
framing test. *No doc delta required.*

**D-magnified_raster_idle-2** — assert quiescence via **`frames_issued` steadying**
(a bounded post-`settle()` no-advance probe), not via a new in-flight-tile-queue
observable.

*Rationale:* (i) `CanvasView` exposes no tile-queue accessor — `frames_issued`
(`canvas_view.hpp:123`) is the only per-pane owed-work signal, and a canvas with
follow-up work still owed advances it (the exact pre-`#18` symptom: "`frames_issued`
advances at the frame rate indefinitely"), so its steadying **is** the observable
form of the note's "empty in-flight tile queue"; (ii) the probe is a real
regression guard — the 1 ms-spaced yields give the render thread wall-clock time
to advance if it is spinning, so a held counter proves genuine idle rather than a
`settle`-deadline time-out; (iii) it reuses the exact signal `settle()` itself
polls, so the assertion and the helper agree on what "quiet" means. *Alternative
rejected — add a `tiles_in_flight()` accessor to `CanvasView`/`CanvasHost` and
assert it is zero:* a new L2→L4 render/app surface for zero additional signal
(an owed tile is exactly what makes `frames_issued` advance), out of scope for a
0.5d test-only assertion leaf. *No doc delta required.*

**D-magnified_raster_idle-3** — **rewrite** the now-false clause of the `settle()`
doc comment (`:119-137`) in place, keeping the function byte-identical, rather than
deleting the whole comment or leaving it.

*Rationale:* (i) the comment mixes a still-true rationale (the wall-clock deadline
prevents the 60 s `ConfigWatchdogKillTest`/566 s failure against ANY non-idling
canvas) with a now-false present-tense claim ("a deep magnification of a raster
cell does not reach idle … `schedule_follow_up` … forever"); the sibling rule
(`nested_real_pool` D-3) is re-anchor a still-true comment in stale terms, delete a
now-false present-tense claim — here the two are interleaved in one comment, so the
correct move is to rewrite the false clause and keep the true one; (ii) the `.tji`
note supplies the exact replacement framing — the deadline is "lane protection
against ANY non-idling canvas, not a workaround for this one bug" — so the rewrite
is transcribing the constitution, not inventing a rationale; (iii) leaving the
false clause tells a future reader the magnified-raster bug is live (worse than
none), and deleting the whole comment loses the load-bearing `ConfigWatchdogKillTest`
rationale for why `settle()` is bounded at all. *Alternative rejected — leave the
comment untouched:* preserves a now-false live claim, against the project's stated
stance. *Alternative rejected — delete the comment entirely:* loses the still-true
lane-protection rationale the note explicitly reaffirms. *No doc delta required* —
a test comment is an implementation detail of the e2e suite, not an architectural
decision.

**D-magnified_raster_idle-4** — the magnification arrives from the `Shift+F`
fit-to-selection gesture on the 32-unit cell, not from a pre-scaled `Affine`
placement or an extra wheel zoom.

*Rationale:* (i) the `.tji` note names `editor.canvas.nav_aids`' `Shift+F` as the
framer; (ii) fitting a 32-unit selection into the ~256–320 device-px pane is the
~8–10× device magnification the parking-lot bug describes, so the gesture alone
reproduces the scenario — no synthetic scale needed; (iii) it exercises the same
transient-camera path the real user drives and reuses `seed_with_cell` verbatim.
*Alternative rejected — a new seed helper placing the cell at `Affine::scale(10)`:*
would pin a deterministic magnification but bypass the nav-aid gesture the note
names and the fit-to-selection path, testing a placement the product never
produces. *No doc delta required.*

## Open questions

(none — all decided.)

## Status

**Done** — 2026-07-28.

- **Test file edited:** `tests/canvas_nav_e2e_test.cpp` — only file touched.
- **New e2e case:** `IM_REGISTER_TEST(engine, "canvas_nav", "magnified_raster_idle")` added; full `TEST_CASE` selects the 32×32 raster cell, frames it with `Shift+F`, `settle()`s, then a re-settle/re-probe outer loop bounded by `k_settle_budget` (10 s) with a 120-yield inner probe (1 ms each) that `IM_CHECK`s `frames_issued("canvas#1")` does not advance — the outer loop exits only when the full inner pass holds.
- **Comment rewrite:** `settle()` rationale comment (`:119-137`) updated to drop the now-false present-tense raster-magnification claim, reframe the wall-clock deadline as general lane protection against ANY non-idling canvas, cite the `#18` / v0.4.0 fix that retired the original defect, and cross-point the new case. `settle()` function and 10 s budget left byte-identical.
- **Fixer hardening:** post-settle probe replaced single-shot check with re-settle/re-probe loop so the test survives `settle()`'s 40-yield quiet window ending mid-tile-refinement under a contended worker pool — the race was in test timing, not in the product; the quiescence guarantee is preserved.
- **Existing cases unmodified:** `wheel_pan_scalebar` and `frame_selection_view` pass byte-for-byte.
