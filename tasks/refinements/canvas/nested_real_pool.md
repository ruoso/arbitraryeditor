# editor.canvas.nested_real_pool — assert the deferred-external nested child composites under the real WorkerPool

## TaskJuggler entry

- **Task:** `editor.canvas.nested_real_pool` (`tasks/00-editor.tji:354-359`).
- **Effort:** `0.5d` (`.tji:355`).
- **Depends:** `!arbc_v040` (`.tji:357`) — the pin bump that landed libarbc
  v0.4.0, which carries `ruoso/arbitrarycomposer#17` (`runtime.interactive`
  routes a deferred EXTERNAL nested child's arrival), the fix this leaf asserts.
- **Note:** `.tji:358` — "`tests/canvas_host_test.cpp:1622-1628` DOCUMENTS but
  does not assert the gap … the real-pool case can now assert the composite
  directly. Assert it there, and DELETE the NOTE that says this leaf does not own
  it … The surrounding install-counting assertions (D-writer_thread-10's three
  idempotent paths) are unaffected and must pass unmodified." (The `:1622-1628`
  citation is stale from line drift; the live disclaimer is
  `tests/canvas_host_test.cpp:1702-1706` and the install-counting assertions are
  `:1710-1714` — see Inputs / context.)
- **Back-link:** the closer appends `Refinement:
  tasks/refinements/canvas/nested_real_pool.md` to the `.tji` note and adds
  `complete 100` after `allocate team` in the same commit that lands the
  implementation. Do not hand-edit the `.tji`.
- **Source of debt:** `tasks/parking-lot.md` 'Deferred-external nested child
  composites blank under real WorkerPool' — a parked libarbc defect graduated to
  this WBS leaf at the v0.4.0 triage (2026-07-28), because `#17` shipping in the
  pin gave it an agent-implementable deliverable (an assertion), which a parking
  lot item is not.
- **Downstream dependents:** none. This is a leaf assertion; nothing depends on
  `nested_real_pool`.
- **Milestone:** `m9_editor` (via `editor.canvas`, `tasks/99-milestones.tji:8`).

## Effort estimate

**0.5d.** This is a pure test change plus a stale-comment deletion in one file
(`tests/canvas_host_test.cpp`). There is **no editor production change**: the
routing fix is `#17`, shipped in libarbc v0.4.0 and already pinned by
`editor.canvas.arbc_v040` (`complete 100`). This leaf only *asserts* the fix the
pin already delivered.

Where the budget goes:

- The composite read-back added to the existing real-pool anchor case
  (`tests/canvas_host_test.cpp:1639`): after the arrival drains, wait for a
  content frame, `consume` it, and `CHECK(ace::render::frame_has_content(frame))`
  — the direct refutation of "composited BLANK under the real WorkerPool".
- Deleting the now-false disclaimer comment (`:1702-1706`) that forward-pointed
  at this leaf, replaced by a one-line pointer to where the composite is now
  asserted.

Where it does NOT go: no new component, no new levelization edge, no new golden
(offline cannot composite a nested composition — see Decisions), no new test
case (the assertion lands in the existing real-pool anchor per the note), no new
include (`consume`, `frame_has_content`, `Srgb8Image`, `pump_until` are all
already used in the file), no `src/` change, no libarbc surface, no doc delta.

## Inherited dependencies

**Settled (consumed as-is):**

- `editor.canvas.arbc_v040` (`complete 100`,
  `tasks/refinements/canvas/arbc_v040.md`) — bumped `ARBC_GIT_TAG` to v0.4.0,
  absorbing `#17`: "a deferred external nested child composites under the real
  `WorkerPool`" (`arbc_v040.md:249-250`, described at `:156-158` and `:180-183`).
  The pin leaf deliberately left the behavioral assertion to this leaf
  (`arbc_v040.md:156-158`: "Asserting the deferred-external nested child
  composites under the real pool (#17) — `editor.canvas.nested_real_pool`"), and
  `tests/arbc_pin_test.cpp` pins only the compile-time existence of the v0.4.0
  surface, never `#17`'s runtime arrival routing. This leaf consumes the shipped
  behavior unchanged.
- `editor.canvas.writer_thread` (`complete 100`) — is the owner of the real-pool
  anchor case (`tests/canvas_host_test.cpp:1639`, the "writer_thread TSan
  anchor") and of the D-writer_thread-10 three-idempotent-paths install accounting
  (`:1710-1714`) this leaf's assertion sits beside and leaves unmodified. The
  fixtures (`load_pending_nested_raw`, `DeferringAssetSource`,
  `settle_external_loads`) and helpers (`build_on_writer`, `pump_until`) are its,
  reused verbatim.

**Pending (owned here):** nothing. Both predecessors are `complete 100`. The
v0.4.0 pin is in place, so `#17`'s routing is live in the build today.

## What this task is

The gap: **under the real interactive `WorkerPool`, a deferred-external nested
child composited BLANK — even when the document was pre-settled — while the
inline degenerate pool composited it byte-exact.** `#17` fixed the routing in
`runtime.interactive`, and the v0.4.0 pin brought the fix into the build. The
real-pool anchor case already exercises exactly this arrival (a deferred external
child settled while a live render loop runs on `default_interactive_pool_config`),
but it asserts only *install counters* and carries a disclaimer saying the
composite is a gap it "surfaces but does not own." This leaf closes that:

1. **Assert the composite in the real-pool anchor** (`tests/canvas_host_test.cpp:1639`).
   After the arrival drains (`:1700-1701`, `external_loads_ready() == 0` and
   `live_count() == 1`) and before `host.stop()` (`:1707`), add:
   `REQUIRE(pump_until([&]{ return host.published_sequence("canvas#1") >= 1; }))`,
   then `host.consume("canvas#1", seq, frame)` and
   `CHECK(ace::render::frame_has_content(frame))`. This is the first test in the
   suite to read back *pixels from a real-pool published frame* — every existing
   real-pool case asserts only `published_sequence` advancement; the composite
   read-back (`consume` + `frame_has_content`) has been inline-pool-only.
2. **Delete the stale disclaimer** (`:1702-1706`) — the comment claiming "the
   real interactive pool composites a deferred-external nested child blank even
   when the document is pre-settled — a pre-existing gap this leaf surfaces but
   does not own (see the `editor.canvas.nested_real_pool` follow-up)". It asserts
   a now-FALSE claim and forward-points at *this* leaf. Replace it with a one-line
   pointer stating the composite is now asserted here (non-blank).

Out of scope, by charter:

- The **install-counting assertions** (`:1710-1714`, the D-writer_thread-10
  three-idempotent-paths sum) must pass **unmodified** (`.tji:358`). This leaf
  adds a composite read-back beside them; it does not touch them.
- **Byte-exact convergence** of the composited pixels is already owned and pinned
  deterministically by the inline case at `:954` ("byte-exact vs the
  offline-settled reference"); this leaf does not duplicate it under the real
  pool (see Decisions D-nested_real_pool-2).
- **`render_offline`'s inability to render/settle a nested composition** is a
  separate offline/export-path gap already surfaced to the parking lot
  (`tests/canvas_host_test.cpp:985-994`); untouched here.

## Why it needs to be done

Before v0.4.0, the real interactive pool's frame path never routed a deferred
external nested child's arrival into the compositor, so the child composited
transparent-black and the content gate (`frame_has_content`,
`src/render/canvas_host.cpp:448`) withheld every frame — `published_sequence`
stayed 0 forever. The inline degenerate pool composited the same child byte-exact,
which is why the anchor could prove *install* (the counters drained) but not the
*composite* (the pixels were blank). The anchor documented this asymmetry in a
disclaimer and named `editor.canvas.nested_real_pool` as the follow-up that would
own the composite once the library fix landed.

`#17` landed in v0.4.0 and `editor.canvas.arbc_v040` pinned it. The real-pool
composite now works, so the disclaimer is stale — it asserts a closed gap and
points at a leaf (this one) whose whole job is to retire it. Per the note, a
stale disclaimer pointing at a closed gap is worse than none: it tells a future
reader the real pool is still broken. Asserting the composite turns the anchor's
counters-only story into a full arrival story (installed AND composited), and a
`pump_until(published_sequence >= 1)` that would *time out and fail on the
pre-#17 code* is a real regression guard against the routing silently regressing
in a later pin.

## Inputs / context

**Governing design docs (normative — the constitution):**

- `docs/01-architecture.md` A5 (§5, `:227-240`; table row `:420`): "Multi-canvas
  = N `HostViewport`/`InteractiveRenderer` over one `Document` sharing one
  `WorkerPool`; no new locking." The real-pool composite is A5's shared-pool
  renderer producing pixels; this leaf asserts that the shared-pool path now
  composites a deferred-external nested child, matching what the inline path
  already does. (The `.tji` note cites A5.)
- `docs/01-architecture.md` A6 (§6, `:242-268`, as amended by `arbc_v040`): the
  display path and the one-render-path/tiled-driver change v0.4.0 brought. The
  composite this leaf reads back is produced by that tiled driver on the real
  pool.
- `docs/01-architecture.md` §8 (`:308-345`): `render` is L2 (`base, project,
  scene, gl, writer, libarbc`; GL, not ImGui). This leaf edits only a test in
  that component's suite and adds no edge.
- `docs/01-architecture.md` §9 (`:346` onward): the universal DoD instantiated
  under Acceptance criteria.

**The change site — `tests/canvas_host_test.cpp` (real-pool anchor):**

- `:1639-1715` — `TEST_CASE("canvas_host: streamed edits + a live render loop + a
  canvas added/removed mid-stream + a deferred external arrival run clean
  (writer_thread TSan anchor)")`. Real pool at `:1661`
  (`CanvasHost host(arbc::default_interactive_pool_config(), …)`); the deferred
  arrival fires at `:1689` (`source.fire_all()`); the drain waits are
  `:1700-1701` (`external_loads_ready() == 0`, `live_count() == 1`).
- `:1702-1706` — **the stale disclaimer to delete.** "NOTE the composite itself
  is asserted by the deterministic inline cases above, not here: the real
  interactive pool composites a deferred-external nested child blank even when the
  document is pre-settled — a pre-existing gap this leaf surfaces but does not own
  (see the `editor.canvas.nested_real_pool` follow-up)."
- `:1707-1714` — `host.stop()`, `handle->join()`, and the D-writer_thread-10
  install-counting assertions (`counters.pending == 0` and
  `idle_installs + settles_installed() + auto_settled == 1`) that must stay
  **unmodified**. The new composite read-back inserts at `:1701`→`:1707`, before
  the stop.

**The pattern to mirror — the inline composite cases (same file):**

- `:954-1012` — `TEST_CASE("… the wired binding settles a deferred external
  nested child and composites it — byte-exact vs the offline-settled reference")`:
  `host.consume("canvas#1", s, frame)` (`:982`) then
  `CHECK(ace::render::frame_has_content(frame))` (`:983`) and the byte-exact
  `CHECK(frame.pixels == reference.pixels)` (`:1011`), all on `make_inline_host()`.
  This is the deterministic owner of byte-exactness; the real-pool assertion
  reuses its `consume` + `frame_has_content` shape (not its byte-exact compare —
  Decisions).
- `:1014-1036` — the negative twin ("the empty binding leaves … blank"): proves
  "blank" by `published_sequence == 0` + `CHECK_FALSE(host.consume(...))`. The
  real-pool assertion is the positive dual: `published_sequence >= 1` + `consume`
  succeeds + `frame_has_content`.
- `:1046-1093` / `:1095-1165` — the two D-writer_thread-10 cases; both settle via
  a real `WriterThread` but read the composite back on an *inline* host. This
  leaf is what finally reads it back on the *real interactive* host.

**Fixtures / helpers (reused verbatim, `tests/canvas_host_test.cpp`):**
`DeferringAssetSource` (`:880-908`), `nesting_doc`/`k_leaf` (16×16 parent, 8×8
green child, `:913-921`), `load_pending_nested_raw` (`:931-941`),
`settle_external_loads` (libarbc, `:999`/`:1668`), `build_on_writer` (`:119-123`),
`pump_until` (`:162-176`), `frame_has_content`
(`src/render/ace/render/render.hpp:79`, defined `src/render/render.cpp:93-103` —
any non-zero alpha byte), `CanvasHost::consume`
(`src/render/ace/render/canvas_host.hpp:159`), `published_sequence` (`:163`).

**Harness:** `scripts/gate` (check_levels · clang-format · build · ctest) is the
umbrella; the anchor is a `gcc-tsan`/`clang-asan` lane case
(`.github/workflows/ci.yml`), and the added `consume`/`frame_has_content` run in
those same lanes.

## Constraints / requirements

1. **Assert the composite on the real interactive pool, non-blank.** The read-back
   uses `default_interactive_pool_config` (already the anchor's pool), reads a
   published frame with `consume`, and `CHECK`s `frame_has_content`. This is the
   assertion the gap is about — the pre-#17 defect was blankness, not wrong
   pixels.
2. **Assert it in the existing real-pool anchor** (`:1639`), not in a new case.
   The note says "assert it *there*", and the install-counting assertions are
   named as "*surrounding*" and "unaffected" — the composite read-back belongs
   amid them, in the one fixture that already stands up the real-pool +
   `WriterThread` + deferred-arrival scaffold.
3. **Install-counting assertions unmodified.** `:1710-1714` (the
   D-writer_thread-10 three-idempotent-paths sum and `counters.pending == 0`) must
   pass byte-for-byte as they stand.
4. **Delete the stale disclaimer**, do not re-anchor it. It asserts a now-false
   present-tense claim (unlike a stale-but-true provenance line); replace it with
   a one-line pointer to where the composite is now asserted.
5. **Guard against regression, deterministically.** The
   `pump_until(published_sequence >= 1)` wait must be a bounded wait that FAILS
   (times out) on pre-#17 blank behavior — i.e. it is a real guard, not a
   tautology. (`pump_until` is the standing bounded poll, `:162-176`.)
6. **No new locking / edge / include / `src` change.** The change touches only
   `tests/canvas_host_test.cpp`, reuses symbols the file already includes, and
   adds no component or dependency edge. `render` stays L2; `scripts/check_levels.py`
   stays clean. No ImGui/GL/SDL/libarbc surface is introduced.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella.

- **Levelization (`check_levels` clean).** The change is confined to
  `tests/canvas_host_test.cpp` — a `consume` + `frame_has_content` read-back and a
  deleted comment. No new include (every symbol is already used in the file), no
  new component, no new dependency edge; `render` stays L2 (`base, project, scene,
  gl, writer, libarbc`; no ImGui/GL/SDL added). `scripts/check_levels.py` stays
  clean.
- **L2 logic / rendered output — Catch2 (`tests/canvas_host_test.cpp`), the
  real-pool anchor case (`:1639`) extended:** after the arrival drains
  (`:1700-1701`) and before `host.stop()` (`:1707`), assert
  `REQUIRE(pump_until([&]{ return host.published_sequence("canvas#1") >= 1; }))`,
  `REQUIRE(host.consume("canvas#1", seq, frame))`, and
  `CHECK(ace::render::frame_has_content(frame))`. This is the direct refutation of
  the `#17` gap ("composited BLANK under the real WorkerPool") and the first
  real-pool published-pixel read-back in the suite. It fails (the `pump_until`
  times out) on pre-#17 code and passes with the v0.4.0 pin.
- **Rendered output — golden: N/A, justified.** `render_offline` binds no
  operators and composites no nested composition at all (settled or not),
  yielding a blank frame for this document
  (`tests/canvas_host_test.cpp:985-994`), so an offline-`render_offline` golden
  would assert nothing here. The rendered-output guarantee is instead pinned as
  the byte-exact convergence against a *matched interactive compositor* — already
  owned deterministically by the inline case at `:954` — and the real-pool leaf's
  own contribution is the pool-specific property the gap was about: non-blank
  routing (`frame_has_content`). (See D-nested_real_pool-2.)
- **UI e2e — ImGui Test Engine: N/A, justified.** This is a host-internal
  render-thread composite below `views`/`dock`; no user-drivable widget behavior
  changes. The canvas nav e2e (`tests/canvas_nav_e2e_test.cpp`) must pass
  **unmodified**.
- **Threading (ASan/TSan).** The anchor IS the `writer_thread` TSan anchor
  (concurrent streamed edits ‖ live render loop ‖ mid-stream add/remove ‖ deferred
  external arrival). The added `consume` is a bounded MOVE under the host's
  existing short lock and `frame_has_content` reads the returned image — no new
  shared-state access, no new lock, no new writer post. The case must stay clean
  in the `gcc-tsan`/`clang-asan` lanes with the composite read-back added; no new
  `tests/lsan.supp` entry.
- **Regression — the existing suites must pass unmodified.** In particular the
  D-writer_thread-10 install-counting assertions (`:1710-1714`) pass **unmodified**
  (`.tji:358`), and the inline composite cases (`:954`, `:1014`, `:1046`, `:1095`)
  are untouched.
- **Coverage — ≥90% diff coverage.** The changed lines are the new test
  assertions themselves, all executed by the anchor; there is no `src/` change, so
  the diff is test-only and clears the CI gate. Tests ship in this commit.
- **Doc delta (same commit): none.** The composite-under-the-real-pool behavior is
  A5's shared-pool renderer producing pixels — already-decided architecture,
  delivered by the v0.4.0 pin (A6 as amended by `arbc_v040`). This leaf asserts
  it; it changes no decision.
- **Deferred WBS work: none.** No follow-up leaf is spawned. The offline nested-
  composition render gap is already parked (not this leaf's), and no new deferral
  arises.

## Decisions

**D-nested_real_pool-1** — assert the composite in the existing real-pool anchor
(`tests/canvas_host_test.cpp:1639`), not in a new dedicated case.

*Rationale:* (i) the `.tji` note says "assert it *there*" and names the
"*surrounding* install-counting assertions" as unaffected — phrasing that only
parses if the composite read-back is inserted amid those assertions, in the anchor
itself; (ii) the anchor is already the sole real-pool nested-arrival fixture (real
pool + `WriterThread` + `DeferringAssetSource` + live render loop), so co-locating
the composite check with the install-count check makes one fixture own the whole
real-pool arrival story — installed AND composited; (iii) it is the minimal change
— a `consume` + `frame_has_content` and a deleted comment. *Alternative rejected —
a dedicated deterministic real-pool case mirroring the inline `:954` case:* it
would rebuild the identical WriterThread+real-pool+deferred-arrival scaffold for
one extra CHECK and contradicts the note's "assert it there". *No doc delta
required.*

**D-nested_real_pool-2** — assert non-blank (`frame_has_content`) under the real
pool, not byte-exact pixel equality.

*Rationale:* (i) `#17` fixed the *routing* of the arrival into the compositor (a
pool-dependent concern — whether the real pool's frame path delivers the settled
child), NOT the compositing *math* (pool-independent: a tile's pixels are
identical regardless of which pool thread computes it); the gap the leaf closes is
blankness, and `frame_has_content` is its exact refutation. (ii) Byte-exact
convergence to the matched-compositor reference is already pinned deterministically
by the inline case at `:954`; since the real-pool composite is byte-identical to
the inline one by construction, a byte-exact real-pool compare would be redundant
with `:954`. (iii) The anchor runs a concurrent damage burst with a canvas
added/removed mid-stream — building a second pre-settled reference document inside
it to compare pixels would bulk a TSan case and add fragility for a claim `:954`
already proves. *Alternative rejected — add `CHECK(frame.pixels ==
reference.pixels)` in the anchor* (constructing a pre-settled empty-binding
reference as `:995-1011` does): redundant with `:954` and fragile in the
concurrent case. *No doc delta required.*

**D-nested_real_pool-3** — delete the stale disclaimer (`:1702-1706`) and replace
it with a one-line pointer, rather than re-anchoring it in place.

*Rationale:* (i) sibling refinements re-anchor a comment only when it describes
still-true behavior in stale terms (e.g. `arbc_v040`'s `#23` comment re-anchored
from "tripwire" to "assertion"); this comment instead asserts a now-FALSE
present-tense claim — "the real interactive pool composites a deferred-external
nested child blank even when the document is pre-settled" — so correcting it means
deleting the claim, not rewording it. (ii) The `.tji` directive is explicit:
"DELETE the NOTE … a stale disclaimer pointing at a closed gap is worse than
none." (iii) A one-line replacement pointer ("the composite under the real pool is
asserted below, non-blank") preserves the useful context — *why* the case reads a
frame back at all — without the false claim or the dead forward-reference to this
leaf. The install-counting comment and its assertions (`:1710-1714`) are a
separate, still-true note and stay untouched. *No doc delta required* — a test
comment is an implementation detail of the L2 suite, not an architectural
decision.

## Open questions

(none — all decided.)

## Status

**Done** — 2026-07-28.

- `tests/canvas_host_test.cpp` extended: composite read-back added to the existing real-pool anchor (writer_thread TSan anchor, `:1639`) — `REQUIRE(pump_until([&]{ return host.published_sequence("canvas#1") >= 1; }))`, `REQUIRE(host.consume("canvas#1", seq, frame))`, `CHECK(ace::render::frame_has_content(frame))`.
- Stale disclaimer (`:1702-1706`, claiming "the real interactive pool composites a deferred-external nested child blank") deleted and replaced with a one-line pointer to the new composite assertion below it.
- Install-counting assertions (`:1710-1714`, D-writer_thread-10 three-idempotent-paths sum and `counters.pending == 0`) left unmodified per constraint.
- All symbols reused from existing file includes (`Srgb8Image`, `ace::render::frame_has_content`, `host.consume`, `host.published_sequence`, `pump_until`, `std::uint64_t`) — no new include added.
- `docs/01-architecture.md`: A4.1a amended (settler-split delivery is two-phase; router-registrant half deferred to `arbc_router_split_pin` + `arbc_router_attach_split`).
- No new Catch2 case, no golden, no e2e, no `src/` change, no new levelization edge.
- No tech-debt follow-up tasks registered.
