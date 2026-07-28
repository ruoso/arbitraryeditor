# editor.canvas.settler_attach_split — render thread owns HostViewport lifetime via attach/detach_settler

## TaskJuggler entry
- **Task:** `editor.canvas.settler_attach_split` — `tasks/00-editor.tji:372-377`, under
  `task canvas "Canvas & rendering"` (`tasks/00-editor.tji:214`).
- **Effort:** `1.5d` · `allocate team`
- **Depends:** `!arbc_v040` → `editor.canvas.arbc_v040` (**Done** — `complete 100`);
  `!pending_removes_order` → `editor.canvas.pending_removes_order` (**Done** — `complete 100`).
- **Note (`.tji:376`):** *"ruoso/arbitrarycomposer#25 shipped `HostViewport::attach_settler()`/
  `detach_settler()` plus `Config::install_settler = false` in v0.4.0 — precisely the seam
  D-writer_thread-8 asked for. […] Construct with `install_settler = false` on the render
  thread, post ONLY the `attach_settler()`/`detach_settler()` pair, and the render thread owns
  the viewport lifetime again. Upstream leaves the install counting and the
  N-viewports-per-document rule unchanged — only WHEN they happen moves […] the proof
  obligation is the existing TSan anchors in tests/canvas_host_test.cpp passing unmodified,
  plus the tripwire that `doc.on_writer_thread()` holds inside the attach/detach closures and
  NOWHERE else."*
- **Back-link:** lands at `tasks/refinements/canvas/settler_attach_split.md`. **The closer**
  appends `Refinement: …` to the `.tji` note and adds `complete 100`. **Do not** hand-edit
  the `.tji`.
- **Source of debt:** `tasks/parking-lot.md` 'HostViewport settler attach/detach split';
  `tasks/refinements/editor/writer_thread.md` **D-writer_thread-8** ("Alternative (not a
  dependency)") + Open question 2. Cleared by arbc#25 landing in the v0.4.0 pin.
- **Downstream dependents:** none registered today. This leaf is a lifetime/ownership
  cleanup of the render↔writer handoff; it changes no observable pixel or UI behaviour.
- **Milestone:** `m9_editor` (`tasks/99-milestones.tji:6`), via the `editor.canvas` container.

## Effort estimate
**1.5d.** The change is small in line count but load-bearing in threading: it moves
`HostViewport` construction/destruction off the writer thread and reduces the posted closure
to the minimal `attach_settler()`/`detach_settler()` pair. The budget goes to (a) getting the
`install_settler = false` + attach/detach shape exactly right at all three sites — add, remove
(teardown), and the resize `rebuild()` — under the D-writer_thread-6 refused-post contract;
(b) an **anti-vacuity** test seam + Catch2 case proving construction runs on the render thread
and only attach/detach cross to the writer (the note's central proof obligation — a
construction that quietly stayed posted passes every functional test while delivering none of
the benefit); (c) re-running the existing gcc-tsan lane to confirm the settler-slot **and**
DamageRouter-registrant races stay closed.

New code: an `install_settler = false` field set on `HostViewport::Config`; render-thread
construction/destruction of the viewport; a `submit_sync`-posted `attach_settler()`/
`detach_settler()` pair replacing the posted ctor/dtor; one test-only observation seam +
one Catch2 case. NO new component, NO new DAG edge, NO new external dependency, NO new golden,
NO user-facing behaviour change.

## Inherited dependencies
**Settled (consumed as-is):**
- **`editor.canvas.arbc_v040`** — pins libarbc at `v0.4.0`, which *ships* the surface this leaf
  consumes but uses none of it (D-arbc_v040-1). `tests/arbc_pin_test.cpp:323-329` already
  carries compile-time witnesses: `&arbc::HostViewport::attach_settler` is
  `void (arbc::HostViewport::*)()`, `&…::detach_settler` is `void (…::*)() noexcept`, and
  `arbc::HostViewport::Config::install_settler` is `bool`. So the symbols provably exist and
  compile against the pin; this leaf is their first production consumer.
- **`editor.canvas.pending_removes_order`** — settled the `drive_once` add/remove pre-emption
  rule (D-pending_removes_order-1/2) and added the `set_after_adds_swap_hook` test seam
  (D-pending_removes_order-3). This leaf deliberately sequences *after* it (`.tji:376`
  ORDERING) so the pending-map consumption discipline is fixed before the *construction* of the
  entries those maps name moves. This leaf does **not** touch the pending-map servicing logic
  (`canvas_host.cpp:331-357`); it changes only how each `Entry`'s viewport is built/destroyed.

**Pending (owned here):** nothing — this is the tail of the writer-thread cleanup chain.

## What this task is
Retire D-writer_thread-8's posted **whole-`HostViewport`** construction/destruction. In order:

1. **Construct on the render thread with `install_settler = false`.** Set the new
   `config.install_settler = false` where `CanvasRenderer::Impl::rebuild()` builds the
   `HostViewport::Config` (`src/render/canvas_renderer.cpp:125-148`), and move the
   `std::make_unique<arbc::HostViewport>(…)` out of the `on_writer([&]{…})` posted closure
   (`canvas_renderer.cpp:155-162`) so it runs inline on the render thread — the viewport's
   ctor/dtor now do no writer-thread-only work.
2. **Post only `attach_settler()` / `detach_settler()`.** After the render-thread construction,
   post `viewport->attach_settler()` through `on_writer` (`canvas_renderer.cpp:68-74` →
   `writer->submit_sync`). On teardown/resize-out, post `viewport->detach_settler()` *before*
   destroying the viewport on the render thread, replacing the posted `viewport.reset()` in
   `destroy_viewport()` (`canvas_renderer.cpp:80-87`) with: post detach, then reset inline.
3. **Preserve the refused-post contract (D-writer_thread-6).** A refused `attach_settler` post
   (writer stopped — the document is going away) leaves the viewport constructed but
   *unattached*: it renders nothing new and receives no settler/damage, which is exactly right
   at teardown, and is never a fall-back to a second writer identity. A refused `detach_settler`
   post means the writer has already drained and JOINED, so its slots are quiescent — the render
   thread simply drops the viewport (mirrors the existing `canvas_renderer.cpp:76-87` logic).
4. **Anti-vacuity proof.** Add a test seam that captures `document.on_writer_thread()` at the
   construction site and inside the attach/detach closures, and a Catch2 case asserting
   construction-site = **false** (render thread) and attach/detach-closure = **true** (writer).

Out of scope, by charter: the `drive_once` pending-map servicing (owned by
`pending_removes_order`, consumed as-is); the document-scoped writer-owned `KindBridge` and the
single-settler-slot / last-installer-wins rule (owned by `editor.canvas.writer_thread`,
D-writer_thread-9 / A4.1b — this leaf preserves them, does not restructure them).

## Why it needs to be done
1. **It pays down the debt D-writer_thread-8 explicitly parked.** D-writer_thread-8 wrapped the
   whole `HostViewport` ctor/dtor in `submit_sync` *only* because one line inside the ctor
   (`Document::set_external_load_settler`) plus the `DamageRouter` registration are
   writer-thread-only. Its "Alternative (not a dependency)" — "ask upstream to split the settler
   install out of the constructor into an explicit writer-thread `attach` call, which would
   retire this posting entirely" — is now shipped as arbc#25. Consuming it is this leaf; leaving
   it unconsumed keeps the whole viewport construction executing on the writer thread for two
   small mutations.
2. **The render thread reclaims scoped ownership of its viewport.** Today the viewport's
   `unique_ptr` is constructed and destroyed *by the writer* on the render thread's behalf
   (`canvas_renderer.cpp:155-162`, `80-87`); the render thread cannot RAII-own a member whose
   lifetime it doesn't drive. After the split the render thread owns the `unique_ptr` outright
   and only borrows the writer for the attach/detach edge.
3. **The writer stops running heavyweight construction.** The posted closure shrinks from "build
   the whole `HostViewport` (which references the `InteractiveRenderer`, target `Surface`,
   `SurfacePool`, `TileCache`)" to "install the settler slot + register one accumulator", so the
   writer is not stalled running viewport construction and cannot be blocked by any worker
   interaction the ctor might do — tightening Constraint 3's "the render thread never blocks per
   frame; it may block on `submit_sync` at entry add/remove/resize only" so that the block
   carries the least possible work.

## Inputs / context
**Governing design docs (normative — the constitution):**
- `docs/01-architecture.md` **A4** (`:61-82`) — Edits flow UI → writer → damage → renderers;
  the render thread never runs on the UI thread.
- **A4.1** (`:84-96`) — single writer *identity*; two-thread writes must funnel to one writer
  via task-posting, never take turns under a lock.
- **A4.1a** (`:98-155`, incl. the v0.3.0 amendment) — the governing statement on settler install
  location; **this leaf amends it** (see Decisions / Doc delta).
- **A4.1b** (`:157-193`) — the document owns ONE writer thread; the writer-thread-only posting
  inventory includes `set_external_load_settler`; the `KindBridge` is document-scoped and
  writer-owned; the document holds ONE settler slot. **Preserved unchanged** by this leaf.
- **A5** (`:195-208`) — a canvas is one `HostViewport` + `InteractiveRenderer` over the shared
  `Document`; N canvases are N renderers over one document, one `WorkerPool`. The
  N-viewports-per-document rule this leaf must not disturb.
- **§8** (`:276-312`) — levelization DAG. `render` is **L2** (`:305`: `base, project, scene, gl,
  writer, libarbc`; GL, not ImGui). This leaf stays inside `render`.
- **§9** (`:314-341`) — the DoD; the "Threading & smoke" row (`:322`) names the UI↔driver
  handoff as ASan/TSan's charge.
- `docs/00-design.md` — **no** D-row governs the settler / render↔writer handoff; the entire
  model lives in the A4 family. No D-row change is needed.

**Sources to change:**
- `src/render/canvas_renderer.cpp` — `on_writer()` helper (`:68-74`); `destroy_viewport()`
  (`:80-87`); `rebuild()` Config build (`:125-148`) and posted ctor (`:149-162`); the `~Impl`
  teardown comment (`:57-63`). Members: `router` (`:191`), `writer` (`:212`), `viewport`
  (`:228`).
- `src/render/ace/render/canvas_renderer.hpp` — the ctor `writer` param doc (`:82-87`) becomes
  "the HostViewport's `attach_settler`/`detach_settler` are posted through it"; add the test-only
  observation seam if it lives here (a `std::function` member, null in the app).
- `tests/canvas_host_test.cpp` — add the anti-vacuity Catch2 case; update the now-stale
  regression-guard *comment* at `:1636-1638` (comment only — assertions unmodified).

**Read-only context (must not change):**
- `src/render/canvas_host.cpp` — `on_writer()`/`submit_sync` (`:111-117`); the pending maps and
  `PendingAdd` (`:74-79, 121-126`); `drive_once` critical "nothing posts to the writer while
  holding `mu`" comment (`:287-290`); service-adds `DamageRouter` ctor via `on_writer` (`:318`)
  and `Entry` build (`:323-325`); service-removes (`:331-357`, `pending_removes_order`'s logic);
  `~Impl` teardown barrier (`:192`) and per-router reset (`:199`). This leaf must keep every
  posting OFF the `mu` lock exactly where the ctor/dtor posting already sat.
- `tests/arbc_pin_test.cpp:323-329` — the v0.4.0 symbol witnesses (do not edit; they are the pin).

**Tests that must keep passing (assertions unmodified):**
- `tests/canvas_host_test.cpp:1173` — `"canvas_host: on_writer_thread() is true inside every
  posted closure and false on the UI and render threads (D-writer_thread-1)"` — the identity
  tripwire.
- `tests/canvas_host_test.cpp:1639` — the primary TSan anchor (streamed edits + live render loop
  + mid-stream add/remove + deferred external arrival); its single-install-across-three-paths
  assertions (`:1712-1714`) must still hold.
- `tests/canvas_host_test.cpp:1237, 1320` — the additional look-through / manip TSan lanes.
- `tests/multi_canvas_e2e_test.cpp`, `tests/multi_canvas_mint_e2e_test.cpp` — exercise the
  D-8 posted viewport construction across N canvases; the N-viewports-per-document regression
  guard.

**Test rigs to borrow:** `set_after_adds_swap_hook` (D-pending_removes_order-3) is the precedent
for a null-in-app `std::function` observation seam on `Impl`. **Harness:** `scripts/gate`
(check_levels · clang-format · build · ctest) + the hermetic gcc-tsan lane (§9.1).

## Constraints / requirements
1. **`install_settler = false` construction is on the render thread.** The
   `std::make_unique<arbc::HostViewport>(…)` must NOT be inside an `on_writer([&]{…})` closure;
   the viewport's ctor and dtor must run on the render thread's identity
   (`document.on_writer_thread()` == false at those points).
2. **Only `attach_settler()` / `detach_settler()` cross to the writer.** These are the sole
   posted (`submit_sync`) calls in the viewport lifecycle. attach is posted on add and on
   resize-in (after construction); detach is posted on remove, resize-out, and teardown (before
   destruction). `submit_sync`'s bidirectional happens-before edge (D-writer_thread-2) is what
   makes attach-on-writer / use-on-render legal.
3. **The writer-thread-only attachment is complete.** The single posted closure must carry
   *every* writer-thread-only mutation the ctor/dtor used to do — the settler-slot install
   (`set_external_load_settler`, install-counted) **and** the registration of the viewport's
   `DamageAccumulator` into the shared `arbc::DamageRouter`. This leaf's load-bearing assumption
   is that arbc#25's `install_settler = false` defers *both* to `attach_settler()` (and
   `detach_settler()` reverses both) — this is what "retire this posting entirely"
   (D-writer_thread-8) and "post ONLY the pair" (`.tji`) require, since both were the reason the
   ctor was posted (`canvas_renderer.cpp:149-150`, `:57-59`). The TSan anchor at `:1639` is the
   arbiter: if a router-registrant race survives, the assumption is wrong (see Open questions).
4. **Postings stay off the `mu` lock.** The attach/detach `submit_sync` must sit exactly where
   the ctor/dtor posting sat — off-lock, inside `CanvasRenderer` driven by `drive_once` steps 1
   and 2 — honouring `canvas_host.cpp:287-290` (nothing posts to the writer while holding `mu`;
   deadlock hazard).
5. **The refused-post contract (D-writer_thread-6) holds on both halves.** A refused attach
   leaves the viewport constructed-but-unattached (renders nothing new, no second identity); a
   refused detach means the writer is already joined/quiescent and the render thread drops the
   viewport inline.
6. **Install counting and the N-viewports rule are unchanged** (A4.1b / D-writer_thread-9): the
   document still holds ONE settler slot; `attach_settler` increments the install count,
   `detach_settler` decrements; last-installer-wins across N viewports is preserved. Only *which
   call* installs moves off the ctor.
7. **Levelization unchanged.** No new component, no new DAG edge; the work stays inside `render`
   (L2). Any test-only seam is a `std::function` member (no ImGui/GL/SDL include).
8. **No pixel change.** Frames are byte-identical before and after; the existing goldens are a
   regression guard, not a re-baseline target.

## Acceptance criteria
These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate` green
(check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean).** No component added, no edge added; the change is
  confined to `render` (L2), which already depends on `writer` and `libarbc`. The L1 core gains
  no ImGui/GL/SDL include; the test-only observation seam is a `std::function`.
- **L1/threading logic — Catch2, headless (in `tests/canvas_host_test.cpp`).** A new
  anti-vacuity case, e.g. `"canvas_host: HostViewport construction runs on the render thread and
  only attach_settler/detach_settler cross to the writer (settler_attach_split anti-vacuity)"`,
  asserting: (i) at the construction site `document.on_writer_thread()` is **false** and
  `writer.on_writer_thread()` is **false**; (ii) inside the attach *and* the detach closure both
  are **true**. Pre-fix check (anti-vacuity of the anti-vacuity test): with the viewport
  construction left inside the posted closure (the old shape), assertion (i) flips to true and
  the case must FAIL — a construction that quietly stayed posted is caught here, nowhere else.
  The existing tripwire `:1173` passes unmodified.
- **Rendered output — golden: N/A, justified.** No compositing path changes; the seven
  byte-identical goldens plus the four `arbc_v040`-rebaselined goldens must pass
  **byte-identically** — any golden movement is a failure signal, not a re-baseline.
- **UI e2e — ImGui Test Engine: N/A new, justified.** No user-facing behaviour changes.
  `tests/multi_canvas_e2e_test.cpp` and `tests/multi_canvas_mint_e2e_test.cpp` pass **unmodified**
  as the N-viewports-per-document / attach-detach-across-N-canvases regression guard.
- **Threading (ASan/TSan).** The primary anchor `:1639` (settler-slot **and**
  DamageRouter-registrant races) stays clean on the hermetic gcc-tsan lane with its assertions
  unmodified — its regression-guard *comment* (`:1636-1638`) is updated to state that
  construction-on-the-render-thread is now the intended, safe shape *because* `install_settler =
  false` defers the writer-side attachment. The `:1237`/`:1320` lanes and the single-install
  assertions (`:1712-1714`) stay clean. No new `tests/lsan.supp` entry.
- **Regression — existing suites pass unmodified** (assertions): `:1173`, `:1237`, `:1320`,
  `:1639`, the two multi_canvas e2e suites, all goldens.
- **Coverage.** ≥90% diff coverage on changed lines (`diff-cover --fail-under=90`); the new
  Catch2 case + the exercised attach/detach/refused-post branches carry it.
- **Doc delta (same commit).** `docs/01-architecture.md` **A4.1a amended** — a new "amended at
  the v0.4.0 settler split" paragraph records that the settler no longer installs at
  construction; the render thread constructs and posts `attach_settler`/`detach_settler`, with
  install counting and the N-viewports rule unchanged (written ahead with this refinement; rides
  the closer's commit).
- **Deferred WBS work.** none.

## Decisions
- **D-settler_attach_split-1 — construct the `HostViewport` on the render thread with
  `install_settler = false`; post only the attach/detach pair.** Set
  `config.install_settler = false`, move `make_unique<HostViewport>` out of the posted closure so
  it runs inline on the render thread, and post `viewport->attach_settler()` (add / resize-in) /
  `viewport->detach_settler()` (remove / resize-out / teardown) through `on_writer`. The posted
  closure carries every writer-thread-only mutation the ctor/dtor used to fold in — the settler
  slot **and** the DamageRouter registrant.
  *Rationale:* (i) it is the exact seam arbc#25 shipped and the exact shape D-writer_thread-8's
  "Alternative" and the `.tji` note prescribe ("post ONLY the pair"); (ii) it restores
  render-thread RAII ownership of the viewport `unique_ptr`; (iii) it minimises the writer's
  posted work to two small mutations, tightening Constraint 3.
  *Alternative rejected — keep posting the whole construction (status quo):* leaves the writer
  running heavyweight viewport construction and denies the render thread scoped ownership; the
  whole point of the leaf.
  *Alternative rejected — construct on the render thread but leave `install_settler` default
  (true):* races the settler slot **and** the DamageRouter registrant (the `:1639` guard note
  documents exactly this failure); `install_settler = false` is the only safe way to move
  construction.
  **Doc delta: A4.1a amended** (`docs/01-architecture.md`).
- **D-settler_attach_split-2 — the single posted closure is the full writer-side attachment, not
  literally one method call.** The invariant is "no writer-thread-only mutation executes on the
  render thread," realised as `attach_settler()`/`detach_settler()`. This leaf assumes arbc#25's
  `install_settler = false` defers **both** the settler slot and the DamageRouter registrant to
  that pair.
  *Rationale:* (i) both were the ctor's reasons to be posted (`canvas_renderer.cpp:149-150`,
  dtor `:57-59`); (ii) D-writer_thread-8 asked for a split that "would retire this posting
  entirely" — a split covering only the settler would not retire it, so #25 delivering "precisely
  the seam" must cover both; (iii) the `:1639` TSan anchor makes the assumption falsifiable — a
  surviving router race fails the gate rather than shipping silently.
  *Alternative rejected — a separate editor-side router (de)registration call:* the accumulator
  is a `HostViewport`-internal member registered via `Config::router`; the editor has no API to
  register it independently of the ctor/attach, so a separate call is not even expressible — the
  correctness rests on #25 deferring it, which is why the TSan anchor is the arbiter.
  **No further doc delta required** (covered by A4.1a's amendment).
- **D-settler_attach_split-3 — preserve the `drive_once` lock discipline and the
  `pending_removes_order` pre-emption rule under the moved construction.** The attach/detach
  postings stay off the `mu` lock, exactly where the ctor/dtor posting sat (in `CanvasRenderer`,
  driven off-lock by `drive_once` steps 1/2); the pending-map servicing
  (`canvas_host.cpp:331-357`) is untouched.
  *Rationale:* (i) `canvas_host.cpp:287-290` forbids posting to the writer under `mu` (deadlock);
  (ii) sequencing after `pending_removes_order` (`.tji:376` ORDERING) means the pre-emption rule
  is already settled, so this leaf only reshapes construction, not consumption.
  *Alternative rejected — post attach under the entry-map lock for atomicity:* reintroduces the
  deadlock hazard the drive loop is structured to avoid.
  **No doc delta required.**
- **D-settler_attach_split-4 — honour the D-writer_thread-6 refused-post contract on both
  halves.** A refused attach leaves the viewport constructed-but-unattached (renders nothing new,
  never a second writer identity); a refused detach means the writer is already drained + JOINED
  (slots quiescent) and the render thread drops the viewport inline.
  *Rationale:* (i) it mirrors the existing `destroy_viewport()` / `rebuild()` refusal handling
  (`canvas_renderer.cpp:76-87, 152-154`); (ii) it keeps teardown correct without a second
  identity, which A4.1's identity rule forbids.
  *Alternative rejected — treat a refused attach as a hard error:* a stopped writer at teardown is
  the *expected* path, not an error; failing there would crash normal shutdown.
  **No doc delta required.**

## Open questions
(none — all decided.) The single load-bearing assumption — that arbc#25's `install_settler =
false` defers **both** the settler slot and the DamageRouter registrant to `attach_settler()` —
is decidable by the implementer against libarbc v0.4.0's `HostViewport` and is made falsifiable
by the `tests/canvas_host_test.cpp:1639` TSan anchor (a surviving router-registrant race fails
the gate). It is therefore resolved by the acceptance test, not a human-judgment item; nothing
goes to the parking lot.

## Status

**Re-deferred** — 2026-07-28.

- No files created or edited; the refinement's load-bearing assumption was falsified against the pinned libarbc v0.4.0 before any code change landed.
- `arbc#25` (v0.4.0) split only the settler slot (`set_external_load_settler`) out of `HostViewport` ctor/dtor into `attach_settler()`/`detach_settler()`; the `DamageRouter::register_sink` call (and `Document::set_damage_sink`) remains welded unconditionally to the ctor (`host_viewport.cpp:66-68`) with no internal synchronization (`damage_router.hpp:29-34`).
- Render-thread construction with `install_settler=false` still races the router's registrant vector against concurrent writer-thread damage flushes — exactly the race the `:1639` TSan anchor's guard note (`canvas_host_test.cpp:1636-1638`) warns about. The hermetic gcc-tsan lane would report it; there is no in-scope fix without an upstream libarbc change.
- WBS: `editor.canvas.arbc_router_split_pin` (placeholder pin for the future libarbc release that moves `DamageRouter` registration into `attach_settler`/`detach_settler`) and `editor.canvas.arbc_router_attach_split` (editor consumer leaf) registered in `tasks/00-editor.tji`; `editor.canvas.settler_attach_split` now depends on `!arbc_router_attach_split`, making it BLOCKED until the upstream fix ships.
- Parking-lot entry: "DamageRouter registrant split — libarbc arbc#25 successor needed to unblock settler_attach_split" added to `tasks/parking-lot.md`.
- Unblocked by: a new libarbc release that moves `DamageRouter::register_sink` (and `set_damage_sink`) out of `HostViewport` ctor/dtor into `attach_settler()`/`detach_settler()` (or a dedicated `attach_router`/`detach_router` pair), gated by an `install_settler`-style flag.
