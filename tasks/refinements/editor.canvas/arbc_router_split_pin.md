# editor.canvas.arbc_router_split_pin — Bump libarbc pin to the release that splits DamageRouter registration into attach/detach_settler

## TaskJuggler entry

- **Task:** `editor.canvas.arbc_router_split_pin` (`tasks/00-editor.tji:396-401`).
- **Effort:** `0.5d` · `allocate team`.
- **Depends:** `!arbc_v040` (the settler-slot pin this successor builds on).
- **Note (abridged):** placeholder for the next libarbc pin that moves
  `DamageRouter::register_sink` (and `Document::set_damage_sink`) out of the
  `HostViewport` ctor/dtor into the `attach_settler`/`detach_settler` pair (or a
  dedicated `attach_router`/`detach_router` pair), gated by an
  `install_settler`-style flag. arbc#25 (v0.4.0) delivered only the settler-slot
  split; a successor issue must be filed upstream and merged before this pin
  task can land. When the upstream release ships: bump `ARBC_GIT_TAG`, confirm
  all goldens and TSan lanes green, add `complete 100`.
- **Back-link:** this refinement lands at
  `tasks/refinements/editor.canvas/arbc_router_split_pin.md`. **The closer**
  appends `Refinement: …` to the `.tji` note and adds `complete 100` after
  `allocate team`. **Do not** hand-edit the `.tji` here. (A predecessor copy of
  this refinement lives at `tasks/refinements/canvas/arbc_router_split_pin.md`
  under the pre-migration area name; the closer should retire it so the `.tji`
  pointer names exactly one path — see the return summary.)
- **Source of debt:** `tasks/refinements/canvas/settler_attach_split.md`
  (re-defer 2026-07-28 — arbc#25 split only the settler slot, not the router
  registrant), and the parking-lot entry "DamageRouter registrant split —
  libarbc arbc#25 successor needed to unblock settler_attach_split."
- **Downstream dependents:** `editor.canvas.arbc_router_attach_split`
  (`!arbc_router_split_pin`) consumes the new API in `src/`; landing it unblocks
  `editor.canvas.settler_attach_split`.
- **Milestone:** M9E (`m9_editor` → `editor.canvas`); this leaf is already
  transitively covered and registers no new WBS task, so no new milestone edge.

## Effort estimate

**Half a day.** This is a pure **pin bump** — one string at `CMakeLists.txt:25`
plus the discipline of absorbing whatever the successor release brings and
renewing the compile-time staleness guard. It is deliberately smaller than
`editor.canvas.arbc_v040` (1d) for two reasons:

- **The editor consumes none of the new surface in `src/`.** Consumption — the
  render-thread construction that opts the router registrant out of the ctor —
  is `editor.canvas.arbc_router_attach_split`'s job, exactly as `arbc_v040`
  witnessed the settler-split API without consuming it and left consumption to
  `editor.canvas.settler_attach_split` (D-arbc_v040-1). So this leaf's `src/`
  change is the tag string and nothing else.
- **The split is a threading/lifetime refactor upstream, not a pixel change.**
  Moving `DamageRouter::register_sink` off the ctor changes *when* the
  accumulator is registered, not what any frame composites, so — unlike
  `arbc_v040`'s tile-apron pixel churn — **no golden is expected to move.** The
  acceptance weight sits on the TSan/ASan lanes staying green and the
  compile-time guard, not on re-baselines.

Where the budget goes: (1) an empirical trial configure+build+ctest against a
local `arbitrarycomposer` successor worktree via `-DARBC_SOURCE_DIR=` to
characterize the bump *before* it lands (the `arbc_v040` method); (2) writing
the new `arbc_pin_test.cpp` staleness witnesses that name the router-registrant
deferral surface. Where it does **not** go: any `src/` edit beyond the tag, any
golden re-baseline (expected byte-identical), any new component or DAG edge, and
— unlike the predecessor draft of this refinement — **no A4.1a doc delta**: the
row was already trued up to the two-phase framing by
`editor.canvas.nested_real_pool` (see Decisions → D-arbc_router_split_pin-4).

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.canvas.arbc_v040`** (Done 2026-07-28) — landed the v0.4.0 settler
  split (`Config::install_settler`, `attach_settler()`/`detach_settler()`,
  arbc#25) and the pin-guard discipline this leaf reuses verbatim:
  `ARBC_GIT_TAG` is `CACHE` **without** `FORCE` (`CMakeLists.txt:25`), the
  staleness hazard is closed **by the compiler** in `tests/arbc_pin_test.cpp`,
  and the settler-split witnesses already live at
  `tests/arbc_pin_test.cpp:325-331`.
- **`editor.canvas.settler_attach_split`** (Re-deferred 2026-07-28) — established
  the exact gap this pin fills: its load-bearing assumption that
  `install_settler = false` deferred **both** the settler slot and the
  `DamageRouter` registration was falsified against v0.4.0, where the router
  registration remains **welded unconditionally to the ctor** with no internal
  synchronization. Its Constraint 3 ("the single posted closure must carry
  *every* writer-thread-only mutation") is the acceptance the whole chain exists
  to satisfy.
- **`editor.canvas.nested_real_pool`** (Done) — amended A4.1a to record that the
  settler-split delivery is two-phase (router-registrant half deferred to this
  pin + `arbc_router_attach_split`), which retires this leaf's only prospective
  doc delta before it was ever owed.

**Pending (owned here):** nothing new — this leaf owns only the pin string and
the guard renewal. The single unresolved item is a **precondition, not a
sub-task**: the upstream successor release must exist (see Constraints §1 and
the return summary's parking-lot note).

## What this task is

The successor to `arbc_v040`'s settler-slot pin. libarbc arbc#25 (v0.4.0) moved
only *one* of the two writer-thread-only concerns the `HostViewport` ctor/dtor
folds in — the external-load settler slot — behind `Config::install_settler`.
The second concern, registering the viewport's `DamageAccumulator` into the
shared `DamageRouter` whose registrant list a commit's flush walks
(`DamageRouter::register_sink` + `Document::set_damage_sink`), stayed welded to
the ctor. That is precisely why `editor.canvas.settler_attach_split` re-deferred:
constructing on the render thread with `install_settler = false` still races the
router's registrant vector against concurrent writer-thread damage flushes.

This leaf bumps `ARBC_GIT_TAG` to the libarbc release that finishes the split —
moving `DamageRouter::register_sink`/`set_damage_sink` off the ctor/dtor and
behind an `install_settler`-style flag (either folded into the existing
`attach_settler()`/`detach_settler()` pair, or a dedicated
`attach_router()`/`detach_router()` pair). Two deliverables, in dependency
order:

1. **The pin.** Bump `CMakeLists.txt:25` to the successor tag, keeping `CACHE`
   without `FORCE`.
2. **Absorb whatever the release brings, byte-for-byte accountably.** Configure,
   build, ctest against the tag; every golden expected byte-identical (§Effort);
   any movement is investigated against the release's changelog, not
   re-baselined by reflex (the `arbc_v040` D-arbc_v040-2 discipline).
3. **Renew the compile-time staleness guard.** Add `arbc_pin_test.cpp`
   unevaluated-`static_assert` witnesses that name the router-registrant
   deferral surface (whatever shape it ships in), so a tree still resolving
   v0.4.0 fails to *compile* rather than passing green — the mechanism
   established at `tests/arbc_pin_test.cpp:260-269,325-331`.

**Out of scope, by charter:**

- **Consuming the new API in `src/`** — construct the viewport on the render
  thread with both the settler slot *and* the router registrant deferred and
  post the complete attach pair — is `editor.canvas.arbc_router_attach_split`.
  Until it lands, the editor keeps constructing-and-posting the whole ctor
  through the writer thread exactly as today
  (`src/render/canvas_renderer.cpp:149-162`), so this pin is **behaviourally
  inert in the editor**.
- **Unblocking `settler_attach_split`** happens when the *consumer* lands, not
  this pin.

## Why it needs to be done

- **It is the missing half of D-writer_thread-8's ask.** `writer_thread.md`'s
  Open question 2 asked upstream to "split the settler install out of
  `HostViewport`'s constructor into an explicit writer-thread `attach`" —
  but under-specified it as *settler-only*. arbc#25 delivered exactly the
  under-specified ask; `settler_attach_split` discovered empirically that the
  router registrant was the other, unnamed half. This pin corrects the ask.
- **It is the sole in-repo unblock for a whole chain.** With the router
  registrant welded to the ctor, there is **no editor-side workaround** — the
  accumulator is a `HostViewport`-internal member registered via
  `Config::router`, and the editor has no API to register it independently of
  the ctor (D-settler_attach_split-2). The correctness rests entirely on
  upstream deferring it, so `arbc_router_attach_split` (and, through it,
  `settler_attach_split`) cannot start until this pin lands.
- **Isolating the pin isolates attribution.** Bumping in its own leaf, with no
  `src/` consumption, keeps any library-side re-baseline distinguishable from an
  editor bug — the same attribution hygiene `arbc_v040` enforced.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **A4.1a** (`docs/01-architecture.md:157-187`) — the settler-split amendment,
  **already** trued up (by `editor.canvas.nested_real_pool`) to record that
  delivery is two-phase: "arbc#25/v0.4.0 shipped only the settler-slot half …
  `DamageRouter::register_sink` / `set_damage_sink` stay welded to the ctor …
  The router-registrant half moves off the ctor only when a successor libarbc
  release lands it … pinned by `editor.canvas.arbc_router_split_pin` and
  consumed by `editor.canvas.arbc_router_attach_split`." The realization pointer
  is already qualified — "Realized (settler half) by
  `editor.canvas.settler_attach_split`; the router-registrant half is completed
  by … (the successor pin) + … (the consumer)" (`:184-187`). This leaf therefore
  owes **no** amendment to the row.
- **A5** (`docs/01-architecture.md:227-234`) — multi-canvas is N `HostViewport`/
  `InteractiveRenderer` over one `Document` sharing one `WorkerPool`, "no new
  locking." The shared `DamageRouter` is what fans a commit out to every canvas;
  moving its registration off the ctor must not add locking or change the
  N-viewports-per-document semantics.
- **§8 levelization DAG** and **§9 testing/DoD** — a pin bump touches neither the
  component graph nor the level rules.

**libarbc — what is actually pinned today (v0.4.0):**

- `Config::install_settler` (bool), `HostViewport::attach_settler()` (`void()`),
  `HostViewport::detach_settler()` (`void() noexcept`) — witnessed at
  `tests/arbc_pin_test.cpp:325-331`.
- `DamageRouter::register_sink` / `Document::set_damage_sink` remain **welded to
  the `HostViewport` ctor** — independently re-verified 2026-07-29 against the
  local `arbitrarycomposer` checkout: latest tag `v0.4.0`, and
  `src/runtime/host_viewport.cpp:63-70` unconditionally calls
  `d_router->register_sink(d_sink)` / `d_model.set_damage_sink(&d_sink)` in the
  ctor, with no `install_router` flag and no `attach_router`/`detach_router`
  pair (per `settler_attach_split.md`'s falsification against the tag).

**libarbc — what the successor release must ship (the precondition):** a release
that moves `DamageRouter::register_sink` (and `set_damage_sink`) out of the
`HostViewport` ctor/dtor into `attach_settler()`/`detach_settler()` (or a
dedicated `attach_router`/`detach_router` pair), gated by an
`install_settler`-style flag whose **default preserves ctor registration** (so
the editor's default-`Config` construction is unchanged until
`arbc_router_attach_split` opts out). This release **does not exist yet**; the
successor issue must be filed and merged upstream first.

**Editor call sites the bump touches (read-only for this leaf):**

- `CMakeLists.txt:25` — `set(ARBC_GIT_TAG "v0.4.0" CACHE STRING …)`, the one
  string this leaf edits; the comment at `:22` and the use at `:34`
  (`GIT_TAG "${ARBC_GIT_TAG}"`) document the `-DARBC_GIT_TAG=<branch>` and
  `-DARBC_SOURCE_DIR=` co-development overrides that `CACHE`-without-`FORCE`
  preserves.
- `src/render/canvas_renderer.cpp:125-162` — the `HostViewport::Config`
  construction (`config.router` set at `:134-138`) and the posted `on_writer`
  ctor closure (`:155-162`). The comment at `:149-154` records that the ctor
  "installs the document's WRITER-THREAD-ONLY settler slot **and** registers
  this viewport's accumulator with the `DamageRouter`," which is why the whole
  construction is posted today. **Untouched by this leaf**; it is where
  `arbc_router_attach_split` will act.
- `src/render/canvas_host.cpp:318` — inside `drive_once` (from `:269`), the same
  posted ctor built via `on_writer`. Also untouched here.
- `tests/arbc_pin_test.cpp` — the pin guard; `:260-269` (the "trio is PRESENT at
  the pin" witnesses) and `:325-331` (the settler-split witnesses) are the
  patterns this leaf extends. `:6-18` documents why the guard is the compiler
  ("The guard is therefore the COMPILER"; no `arbc_version` symbol to assert at
  runtime; `<NAME>_VERSION` invisible after `FetchContent_MakeAvailable`).
- `tests/canvas_host_test.cpp:1881` — the `writer_thread` TSan acceptance anchor
  (`TEST_CASE` at `:1881-1882`); its guard note (`:1878-1880`, "Regression-guard
  note (NOT a tautology)") is the arbiter of a surviving router-registrant race.
  Must stay green **unmodified** across this pin (behaviour is unchanged).

**Harness:** `scripts/gate` reconfigures in place (`check_levels` · clang-format ·
build · ctest); the five CI lanes (including `gcc-tsan`, ASan) are the
acceptance surface; committed goldens live under `tests/goldens/`.

## Constraints / requirements

1. **This pin is gated on an upstream release that does not yet exist.** The
   successor libarbc issue (a targeted successor to arbc#25) must be filed,
   merged, and tagged on `ruoso/arbitrarycomposer` before this leaf can land.
   Filing/merging it is **cross-repo human work**, already parked
   ("DamageRouter registrant split — libarbc arbc#25 successor needed"); it is
   **not** a WBS task and this refinement does not create one. Until the tag
   exists, this leaf stays uncompleted (no `complete 100`).
2. **The pin plus a truthful `arbc_pin_test.cpp` are the whole change.** No
   `src/` edit beyond `CMakeLists.txt:25`, and **no doc delta** — A4.1a already
   records the two-phase delivery (see D-arbc_router_split_pin-4). Consumption is
   `arbc_router_attach_split`.
3. **Keep `ARBC_GIT_TAG` `CACHE` without `FORCE`.** Adding `FORCE` is **not
   acceptable** — it breaks the `-DARBC_GIT_TAG=<branch>` / `-DARBC_SOURCE_DIR=`
   overrides. The stale-`build/` hazard (a green gate still linked to the old
   tag, because `scripts/gate` reconfigures in place and CI has no build cache)
   is re-closed **by the compiler**, via new pin-test witnesses that fail to
   *compile* against v0.4.0 — never by a CMake `VERSION_LESS` guard (there is no
   version symbol to test).
4. **New pin-test witnesses name version-only surface.** They must reference the
   router-registrant deferral API the successor release adds (a new flag and/or
   attach/detach entry point) as unevaluated operands, so a v0.4.0 tree fails at
   the compiler. The existing five behavioural cases stay green with prose
   retargeted to the new version; the `:325-331` settler witnesses stay.
5. **Behaviourally inert in `src/`.** The editor's default-`Config` construction
   must still install the settler slot **and** register the router in the ctor
   after the pin (the flag defaults to ctor-registration). If the successor
   release instead changed the *default* so a default-constructed viewport no
   longer registers with the router, a commit would stop fanning out to that
   canvas — a "canvas goes stale on edit" regression this leaf's tests must
   **catch** (interactive/golden behaviour) and which signals the upstream
   default is wrong: surface it upstream, do not paper over it with an editor
   opt-in (that opt-in is `arbc_router_attach_split`'s work, not a pin fixup).
6. **Levelization untouched.** No component added, no DAG edge added; the L1
   core gains no ImGui/GL/SDL include; `scripts/check_levels.py` stays clean.
7. **No new TSan/ASan/LSan suppression.** No `tests/lsan.supp` entry is added;
   the `gcc-tsan` and ASan lanes stay green with the anchors unmodified.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate`
green (`check_levels` · clang-format · build · ctest) is the umbrella.

- **Levelization** — `scripts/check_levels.py` clean; no component or edge added
  (a pin bump cannot change the DAG).
- **Pin-effectiveness + guarantee pin — Catch2, headless, `tests/arbc_pin_test.cpp`.**
  The existing behavioural cases pass with prose retargeted to the successor
  version. **New unevaluated `static_assert` witnesses** name the
  router-registrant deferral surface (the new flag and/or
  `attach_router`/`detach_router` or the folded `attach_settler` behaviour), such
  that configuring against `v0.4.0` **fails to compile** on those lines. This is
  the primary functional assertion.
- **Rendered output — goldens.** Every `tests/goldens/*` byte-identical
  (`export_camera_64x64.png` and the `.rgba8` set); **no re-baseline is
  expected** — the split is a threading refactor, not a pixel change. Any golden
  movement is investigated against the release changelog and treated as a
  regression unless the changelog names a cause (D-arbc_v040-2 discipline).
- **Threading — ASan/TSan lanes.** `gcc-tsan` and ASan green with
  `tests/canvas_host_test.cpp`'s anchors passing **unmodified** — the
  `writer_thread` anchor at `:1881`, and the `edit_render_sync` (`:441`),
  `cells.remove` (`:491`), `cells.gizmo` (`:546`), `cells.group_transform`
  (`:603`), `look_through` (`:1377`), `cameras.manip` (`:1460`),
  `cameras.export` (`:1965`), and `cameras.contact_sheet` (`:2058`) anchors. The
  pin changes no editor threading behaviour, so a green TSan run is the proof of
  inertness. No new `tests/lsan.supp` entry.
- **UI e2e — N/A, justified.** A pin bump with no `src/` consumption drives no
  new UI; the ImGui Test Engine suites pass unmodified.
- **Coverage.** `diff-cover --fail-under=90` on the changed lines — the changed
  lines are `CMakeLists.txt:25` (not code-covered) and the pin-test witnesses
  (compile-time); no new executable `src/` line is added, so there is no
  coverage gap to fill.
- **Doc delta — none.** A4.1a already records the two-phase delivery (amended by
  `editor.canvas.nested_real_pool`); this leaf owes no doc edit (see
  D-arbc_router_split_pin-4).
- **Deferred WBS work — none.** The one downstream leaf
  (`editor.canvas.arbc_router_attach_split`) already exists in the WBS; this leaf
  registers no new task. The upstream-release precondition is a parking-lot item,
  not a WBS leaf.

## Decisions

- **D-arbc_router_split_pin-1 — a separate pin leaf, not folded into
  `arbc_router_attach_split`.** *Rationale:* (i) it mirrors the settled
  `arbc_v040` (pin) → `settler_attach_split` (consume) split, keeping library
  re-baselines attributable apart from editor bugs; (ii) the pin is gated on an
  external release while the consumer is pure editor work, so separating them
  lets the consumer be scheduled the moment the tag exists without re-litigating
  the bump. *Alternative rejected:* one combined "pin + consume" leaf — it
  entangles an external precondition with editor code and loses the attribution
  isolation every prior pin kept. **No doc delta** (structural WBS choice).
- **D-arbc_router_split_pin-2 — acceptance is the compile-time guard + green
  TSan, not goldens.** *Rationale:* the split relocates *when* the accumulator
  registers, not what any frame paints, so goldens are expected byte-identical
  and carry no signal; the real risk is a threading regression, caught by the
  ASan/TSan anchors, and pin-effectiveness, caught by the compiler. *Alternative
  rejected:* treating this like `arbc_v040` and budgeting for golden churn —
  wrong model for a non-pixel change; it would invite reflexive re-baselines that
  hide a real regression. **No doc delta.**
- **D-arbc_router_split_pin-3 — the pin must be behaviourally inert; the flag
  default must preserve ctor registration.** *Rationale:* the editor consumes the
  new surface only in `arbc_router_attach_split`; a pin that silently changed
  fan-out behaviour would violate the settled "pin isolates attribution" property
  and could ship a stale-canvas regression. The constraint is expressed as an
  acceptance gate (Constraint 5) so a mis-defaulted upstream release is *caught*
  here rather than absorbed. *Alternative rejected:* let the pin also flip the
  editor to the deferred construction — that is the consumer leaf's 1d of work
  and would re-entangle pin and consumption. **No doc delta.**
- **D-arbc_router_split_pin-4 — A4.1a needs no truing-up here; it already records
  the two-phase delivery.** The predecessor draft of this refinement scoped a
  doc delta to true up A4.1a (which then claimed both concerns "Realized by
  `editor.canvas.settler_attach_split`"). That truing-up **has since landed** via
  `editor.canvas.nested_real_pool` (`nested_real_pool.md:363`): A4.1a
  (`docs/01-architecture.md:157-187`) now states the delivery is two-phase, that
  `DamageRouter::register_sink`/`set_damage_sink` stay ctor-welded at v0.4.0, and
  that the router half is "completed by … (the successor pin) +
  `editor.canvas.arbc_router_attach_split` (the consumer)." *Rationale:* the row
  is already accurate at pin-land time — it names this pin, does not overclaim
  the router half as *realized*, and future tense there is correct until the
  consumer lands. Any final flip to past tense at realization rides with
  `arbc_router_attach_split` (which actually realizes it), not this pin.
  *Alternative rejected:* re-editing A4.1a here anyway — it would duplicate a
  landed amendment and risk drift. **No doc delta for this leaf.**

## Open questions

(none — all decided.) The exact upstream API shape (folded `attach_settler`
vs. a dedicated `attach_router`/`detach_router` pair, and the flag's name) is
**not** an open question this leaf resolves — it is fixed by the successor
release when it ships, and Constraint 4 / the acceptance are written to witness
whichever shape lands. The only genuinely-open item is the **precondition** that
the successor libarbc release be filed, merged, and tagged upstream — cross-repo
human work, already in the parking lot and surfaced in this refinement's return
summary rather than encoded as a WBS task.

## Status

**Re-deferred** — 2026-07-29.

- Upstream precondition (Constraint 1) confirmed unmet: no successor libarbc tag exists. Latest tag on `ruoso/arbitrarycomposer` is `v0.4.0`; `DamageRouter::register_sink` / `Document::set_damage_sink` remain unconditionally ctor-welded (`host_viewport.cpp:66-71`) with no `install_router` flag and no `attach_router`/`detach_router` pair on any branch or in any tag.
- No edits landed: pin bump and `arbc_pin_test.cpp` witnesses cannot be written against v0.4.0 (the only available release); a `static_assert` naming the router-registrant deferral API would fail to compile.
- Parking-lot entry "DamageRouter registrant split — libarbc arbc#25 successor needed" updated to record this second re-defer (2026-07-29).
- CI flake fixed in same commit: `tests/multi_canvas_e2e_test.cpp` snapshot-timing race (pre-existing, unrelated to this task) repaired by the fixer sub-agent — `grab_frame` now refreshes the both-open snapshot to the latest frame rather than racing the first post-focus frame.
- This leaf stays uncompleted (no `complete 100`) until the upstream successor tag ships.
