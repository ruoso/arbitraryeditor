# editor.canvas.arbc_router_split_pin — Bump libarbc pin to the release that splits DamageRouter registration into attach/detach_settler

## TaskJuggler entry

- **Task:** `editor.canvas.arbc_router_split_pin` (`tasks/00-editor.tji:378-383`).
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
  `tasks/refinements/canvas/arbc_router_split_pin.md`. **The closer** appends
  `Refinement: …` to the `.tji` note and adds `complete 100` after
  `allocate team`. **Do not** hand-edit the `.tji` here.
- **Source of debt:** `tasks/refinements/canvas/settler_attach_split.md`
  (re-defer 2026-07-28 — arbc#25 split only the settler slot, not the router
  registrant), and the parking-lot entry "DamageRouter registrant split —
  libarbc arbc#25 successor needed to unblock settler_attach_split."
- **Downstream dependents:** `editor.canvas.arbc_router_attach_split`
  (`!arbc_router_split_pin`) consumes the new API in `src/`; landing it unblocks
  `editor.canvas.settler_attach_split`.
- **Milestone:** M9E (`m9_editor` → `editor.canvas`; this leaf is already
  transitively covered — no new milestone edge).

## Effort estimate

**Half a day.** This is a pure **pin bump** — one string at `CMakeLists.txt:25`
plus the discipline of absorbing whatever the successor release brings, renewing
the compile-time staleness guard, and truing up the one design row this chain
touched. It is deliberately smaller than `editor.canvas.arbc_v040` (1d) for two
reasons:

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
characterize the bump *before* it lands (the `arbc_v040` method); (2) writing the
new `arbc_pin_test.cpp` staleness witnesses that name the router-registrant
deferral surface; (3) truing up A4.1a. Where it does **not** go: any `src/`
edit beyond the tag, any golden re-baseline (expected byte-identical), any new
component or DAG edge.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.canvas.arbc_v040`** (Done 2026-07-28) — landed the v0.4.0 settler
  split (`Config::install_settler`, `attach_settler()`/`detach_settler()`,
  arbc#25) and the pin-guard discipline this leaf reuses verbatim:
  `ARBC_GIT_TAG` is `CACHE` **without** `FORCE` (`CMakeLists.txt:25`), the
  staleness hazard is closed **by the compiler** in `tests/arbc_pin_test.cpp`,
  and the settler-split witnesses already live at `tests/arbc_pin_test.cpp:323-329`.
- **`editor.canvas.settler_attach_split`** (Re-deferred 2026-07-28) — established
  the exact gap this pin fills: its load-bearing assumption that
  `install_settler = false` deferred **both** the settler slot and the
  `DamageRouter` registration was falsified against v0.4.0, where the router
  registration remains **welded unconditionally to the ctor** with no internal
  synchronization. Its Constraint 3 ("the single posted closure must carry
  *every* writer-thread-only mutation") is the acceptance the whole chain exists
  to satisfy.

**Pending (owned here):** nothing new — this leaf owns only the pin string, the
guard renewal, and the doc truing-up. The single unresolved item is a
**precondition, not a sub-task**: the upstream successor release must exist
(see Constraints §1 and the return summary's parking-lot note).

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
`attach_router()`/`detach_router()` pair). Three deliverables, in dependency
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
   established at `tests/arbc_pin_test.cpp:255-267,323-329`.

**Out of scope, by charter:**

- **Consuming the new API in `src/`** — construct the viewport on the render
  thread with both the settler slot *and* the router registrant deferred and
  post the complete attach pair — is `editor.canvas.arbc_router_attach_split`.
  Until it lands, the editor keeps constructing-and-posting the whole ctor
  through `submit_sync` exactly as today (`src/render/canvas_renderer.cpp:149-159`),
  so this pin is **behaviourally inert in the editor**.
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
  accumulator is a `HostViewport`-internal member registered via `Config::router`,
  and the editor has no API to register it independently of the ctor
  (D-settler_attach_split-2). The correctness rests entirely on upstream
  deferring it, so `arbc_router_attach_split` (and, through it,
  `settler_attach_split`) cannot start until this pin lands.
- **Isolating the pin isolates attribution.** Bumping in its own leaf, with no
  `src/` consumption, keeps any library-side re-baseline distinguishable from an
  editor bug — the same attribution hygiene `arbc_v040` enforced.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **A4.1a** (`docs/01-architecture.md:157-175`) — the settler-split amendment.
  It already *describes* both concerns (settler slot **and** DamageRouter
  registrant) moving into `attach_settler()`/`detach_settler()`, but its
  realization pointer ("Realized by `editor.canvas.settler_attach_split`")
  overstates what v0.4.0 shipped: arbc#25 delivered the settler half only. This
  leaf trues that up (see Decisions → doc delta).
- **A5** (`docs/01-architecture.md:215-228`) — multi-canvas is N `HostViewport`/
  `InteractiveRenderer` over one `Document` sharing one `WorkerPool`, "no new
  locking." The shared `DamageRouter` is what fans a commit out to every canvas;
  moving its registration off the ctor must not add locking or change the
  N-viewports-per-document semantics.
- **§8 levelization DAG** and **§9 testing/DoD** — a pin bump touches neither the
  component graph nor the level rules.

**libarbc — what is actually pinned today (v0.4.0):**

- `Config::install_settler` (bool), `HostViewport::attach_settler()` (`void()`),
  `HostViewport::detach_settler()` (`void() noexcept`) — witnessed at
  `tests/arbc_pin_test.cpp:323-329`.
- `DamageRouter::register_sink` / `Document::set_damage_sink` remain **welded to
  the `HostViewport` ctor**, with no internal synchronization in the router
  (per `settler_attach_split.md`'s falsification against the tag).

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
  string this leaf edits; lines 22/26 document the `-DARBC_GIT_TAG=<branch>` and
  `-DARBC_SOURCE_DIR=` co-development overrides that `CACHE`-without-`FORCE`
  preserves.
- `src/render/canvas_renderer.cpp:125-159` — the `HostViewport::Config`
  construction and the posted ctor. The comment at `:149-159` records that the
  ctor "installs the document's WRITER-THREAD-ONLY settler slot **and** registers
  this viewport's accumulator with the `DamageRouter`," which is why the whole
  construction is posted today. **Untouched by this leaf**; it is where
  `arbc_router_attach_split` will act.
- `src/render/canvas_host.cpp:318` — `drive_once` builds the same posted ctor via
  `on_writer`. Also untouched here.
- `tests/arbc_pin_test.cpp` — the pin guard; `:255-267` and `:323-329` are the
  witness patterns this leaf extends. `:1-40` documents why the guard is the
  compiler (no `arbc_version` symbol; `<NAME>_VERSION` invisible after
  `FetchContent_MakeAvailable`).
- `tests/canvas_host_test.cpp:1639` — the `writer_thread` TSan acceptance anchor;
  its guard note (`:1632-1638`) is the arbiter of a surviving router-registrant
  race. Must stay green **unmodified** across this pin (behaviour is unchanged).

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
2. **The pin plus a truthful `arbc_pin_test.cpp` and A4.1a are the whole change.**
   No `src/` edit beyond `CMakeLists.txt:25`. Consumption is
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
   retargeted to the new version; the `:323-329` settler witnesses stay.
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
  The five existing behavioural cases pass with prose retargeted to the successor
  version. **New unevaluated `static_assert` witnesses** name the router-registrant
  deferral surface (the new flag and/or `attach_router`/`detach_router` or the
  folded `attach_settler` behaviour), such that configuring against `v0.4.0`
  **fails to compile** on those lines. This is the primary functional assertion.
- **Rendered output — goldens.** Every `tests/goldens/*` byte-identical
  (`export_camera_64x64.png` and the `.rgba8` set); **no re-baseline is
  expected** — the split is a threading refactor, not a pixel change. Any golden
  movement is investigated against the release changelog and treated as a
  regression unless the changelog names a cause (D-arbc_v040-2 discipline).
- **Threading — ASan/TSan lanes.** `gcc-tsan` and ASan green with
  `tests/canvas_host_test.cpp`'s anchors (the `writer_thread` anchor at `:1639`,
  the `edit_render_sync`/`cells.remove`/`look_through`/`cameras.manip`/`export`/
  `contact_sheet` anchors) passing **unmodified** — the pin changes no editor
  threading behaviour, so a green TSan run is the proof of inertness. No new
  `tests/lsan.supp` entry.
- **UI e2e — N/A, justified.** A pin bump with no `src/` consumption drives no
  new UI; the ImGui Test Engine suites pass unmodified.
- **Coverage.** `diff-cover --fail-under=90` on the changed lines — the changed
  lines are `CMakeLists.txt:25` (not code-covered), the pin-test witnesses
  (compile-time), and the A4.1a prose; no new executable `src/` line is added,
  so there is no coverage gap to fill.
- **Doc delta (same commit).** A4.1a truing-up rides in the closer's commit (see
  Decisions).
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
- **D-arbc_router_split_pin-4 — true up A4.1a to record the two-phase delivery
  (doc delta).** A4.1a currently states both the settler slot and the
  DamageRouter registrant move into `attach_settler()`/`detach_settler()`,
  "Realized by `editor.canvas.settler_attach_split`." That overstates what
  v0.4.0 shipped: arbc#25 delivered the settler half only, and the router half is
  still ctor-welded. *Rationale:* leaving the constitution claiming the router
  already moved is a landmine for the next reader of A4.1a and of
  `settler_attach_split`'s re-defer; a pin that lands the router API is the
  idiomatic place to true up the row (pins "true up the docs"). The amendment is
  surgical: it scopes the delivery as two-phase (settler by
  `settler_attach_split`; router by this pin + `arbc_router_attach_split`) and
  does **not** claim the router half is *realized* — accurate at pin-land time,
  since the editor still posts the whole ctor until the consumer lands.
  *Alternative rejected:* a brand-new `A<n>` row — heavier than the fact
  warrants; this is a scoping correction to the existing amendment, recorded the
  same way A4.1a itself records the v0.4.0 amendment. **Doc delta:**
  `docs/01-architecture.md` A4.1a (§4), same commit as this leaf.

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

**Re-deferred** — 2026-07-28.

The upstream precondition (Constraint 1) is unmet. `ruoso/arbitrarycomposer` has
tags only through `v0.4.0`; no successor tag exists. At v0.4.0
(`src/runtime/host_viewport.cpp:66-71`) `DamageRouter::register_sink` and
`Document::set_damage_sink` are unconditionally welded to the `HostViewport`
ctor — there is no `attach_router`/`detach_router` pair, no
`install_router`-style flag, and no router-registrant deferral of any kind.

- Bumping `ARBC_GIT_TAG` to a non-existent successor tag → FetchContent fetch
  failure → red gate.
- Writing the mandated compile-time pin-test witnesses naming the
  router-registrant deferral surface → compile failure against v0.4.0 (the only
  available tag) → red gate.
- Weakening the witnesses to name only existing v0.4.0 surface would violate
  Constraint 4 (no-test-weakening / false pin).

No coherent, gate-green commit is possible until the upstream successor release
is filed, merged, and tagged. The parking-lot entry "DamageRouter registrant
split — libarbc arbc#25 successor needed to unblock settler_attach_split"
tracks the human action required. This task stays uncompleted (`no complete 100`)
until that tag exists.
