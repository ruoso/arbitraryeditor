# editor.canvas.arbc_v030 — Bump libarbc pin to v0.3.0

## TaskJuggler entry

- **Task:** `editor.canvas.arbc_v030` — `tasks/00-editor.tji:273-278`, inside
  `task canvas "Canvas & rendering"` (`tasks/00-editor.tji:171`), inside `task editor`.
- **Effort:** `0.5d` · `allocate team`
- **Depends:** `editor.canvas.single_writer` (`complete 100`).
- **Note (`.tji:277`):** *"Bump ARBC_GIT_TAG v0.2.0->v0.3.0 (CMakeLists.txt:25); build + all goldens
  green (v0.3.0 is additive — every 0.2.0 call compiles and behaves unchanged on a single-threaded
  host). Lands the two library-owned items this editor filed: ruoso/arbitrarycomposer#13
  (HostViewport::step() no longer publishes structural writes off the writer thread — it asks
  Model::on_writer_thread() and reports the owed install as StepOutcome::external_loads_ready) and
  its companion, the DamageAccumulator flush/drain race (now mutex-guarded for that handoff alone),
  which together retire A4.1a's writer-priority document lease justification; plus
  ruoso/arbitrarycomposer#15 (Journal cursor/depth published as atomics, so the UI's per-frame
  can_undo()/can_redo() at src/dock/dock.cpp:337,343 are any-thread reads). New surface consumed
  downstream: Document::on_writer_thread(), Document::external_loads_ready(),
  Document::set_external_load_settler()/external_loads_auto_settled(),
  HostViewport::Config::external_loads_ready. Prereq for writer_thread. Design:
  docs/01-architecture.md A4/A4.1/A4.1a."*
- **Back-link:** this refinement lands at `tasks/refinements/canvas/arbc_v030.md`.
  **The closer** appends `Refinement: tasks/refinements/canvas/arbc_v030.md` to the `.tji` note and
  adds `complete 100` after `allocate team`. **Do not** hand-edit the `.tji` here.
- **Source of debt:** `tasks/refinements/editor/single_writer.md:55-71` — the arbc#13 residual that
  leaf filed upstream with the explicit finding *"The host cannot fix this … there is no in-repo fix
  and no editor-side leaf, so none is registered."* This leaf is the adoption of that fix.
- **Downstream dependents:** `editor.canvas.writer_thread` (`tasks/00-editor.tji:279-284`,
  `depends !arbc_v030`, 4d) — the *only* registered dependent, and the sole consumer of the new
  surface. `tasks/refinements/editor/writer_thread.md:25-28` states the hard blocking relation:
  *"This task cannot land before it: without ruoso/arbitrarycomposer#13's fix, HostViewport::step()
  still publishes from the render thread and no host-side design can fix it; without #15, the UI
  cannot read can_undo()/can_redo() off the writer thread at all."*
- **Milestone:** `m9_editor` (`tasks/99-milestones.tji:6`), reached through the `editor.canvas`
  container dependency (`tasks/99-milestones.tji:8`).

## Effort estimate

**Half a day.** The edit is one string. The budget goes almost entirely into *proving* the edit took
effect and into truing up the constitution, because both are places where a pin bump has already
silently failed once in this repo.

- **The mechanical bump is ~1 line.** `CMakeLists.txt:25` — `set(ARBC_GIT_TAG "v0.2.0" CACHE STRING
  "libarbc git ref")` → `"v0.3.0"`. It is the **only** place in built code that names the version
  (README carries none; `.github/` carries none; `scripts/` knows only the `arbc/` *include* prefix
  at `scripts/check_levels.py:46-47,53`; no test asserts a library version; there is no
  `arbc_version` symbol anywhere in the repo).
- **The bump is empirically already known to work.** A trial configure + build + full `ctest` against
  `-DARBC_GIT_TAG=v0.3.0` on this checkout, unmodified otherwise, is **green**: configure OK, build
  exit 0, `ctest` 2/2 (`ace_tests` 3.47s, `ace_shell_test` 27.02s), all goldens byte-identical, no
  new warning. So this leaf is not a discovery exercise — it is a *pinning* exercise. Its cost is
  the artifacts that keep it green, not the change itself.
- **Where the budget actually goes (1): proving the pin took.** `ARBC_GIT_TAG` is a `CACHE` variable
  set **without `FORCE`** (`CMakeLists.txt:25`), so editing the default does **not** overwrite an
  existing `build/<preset>/CMakeCache.txt` entry. `scripts/gate:33-34` reconfigures **in place** and
  never wipes, and `build/` is gitignored (`.gitignore:1`). A developer with an existing build tree
  therefore edits the line, runs `scripts/gate`, sees green — **still linked against v0.2.0**. This
  leaf must ship a guard that fails loudly on that, and the guard must live somewhere the gate
  actually runs (D-arbc_v030-2).
- **Where the budget actually goes (2): the doc truth-up, which the *previous* pin bump skipped.**
  `docs/00-design.md:4`, `docs/01-architecture.md:305` (A14) and `:306` (A15) make **present-tense**
  claims about *"pinned `arbc` v0.1.0"* — two pin bumps out of date. Worse, A15's core premise —
  *"libarbc v0.1.0 exposes no per-kind state-slab walk hook … `Registry::add` admits only
  factory/metadata/codec/binder"* — has been **factually false since v0.2.0** (`KindStateWalker`
  appears in `arbc/contract/registry.hpp` 4 times at v0.2.0; 0 times at v0.1.0). That drift entered
  when `editor.canvas.arbc_v020` bumped the pin without re-verifying the version-conditional claims.
  A pin bump is the one task that owns those claims (D-arbc_v030-6). (A4.1a's own mentions of
  v0.2.0 at `:100` and v0.1.0 at `:120` are **not** stale — they are historical narrative about the
  era each describes, and stay as written.)
- **Where the budget does *not* go.** No consumption of the new surface. No lease deletion, no
  `KindBridge` move, no edit-seam rebind, no change to `src/dock/dock.cpp:337,343`. Those are
  `editor.canvas.writer_thread`'s D-writer_thread-9/-11 and are 4d of restructuring
  (D-arbc_v030-1).

New code: one string in `CMakeLists.txt`, one new ~90-line Catch2 file `tests/arbc_pin_test.cpp`
(added to the `ace_tests` source list, `CMakeLists.txt:219-237`), one corrected comment in
`src/render/canvas_renderer.cpp`. **No new component, no new DAG edge, no new external dependency,
no new golden, no new ctest target, no libarbc fork.** Doc deltas: **A4.1a amended** plus four
version-string corrections (**A14**, **A15**, `docs/00-design.md:4`, and A4.1a's own two).

## Inherited dependencies

**Settled (consumed as-is):**

- `editor.canvas.single_writer` — `tasks/refinements/editor/single_writer.md` (**Done**, 2026-07-22).
  Consumed:
  - **The arbc#13 finding** (`:55-71`) — *"the library itself publishes a structural write from the
    render thread: `HostViewport::step()` … calls `settle_external_loads` at its top … So with a
    nested external composition open and live editing, the render-thread settle is a second writer
    identity against the UI-thread `transact` — exactly what arbc#7 forbids, and something `doc_mu`
    only masked (time-serialized) rather than fixed."* This leaf is the sole disposition of that
    finding.
  - **The `apply_edit` seam is unchanged by a pin bump** (`:35-40`) — *"`apply_edit` still runs the
    mutation synchronously on the calling (writer) thread and pokes — the edit-runner **seam** the
    app binds to is unchanged."* It stays unchanged here too.
  - **The v0.2.0 precedent for what a pin-bump leaf *is*** (`:32-34`) — *"Pin bump (its own leaf,
    `editor.canvas.arbc_v020`): `ARBC_GIT_TAG` v0.1.0 → v0.2.0 (`CMakeLists.txt:25`). Additive —
    every 0.1.0 registration compiled unchanged; all goldens byte-identical."* This leaf matches that
    shape and adds the two things that precedent lacked: a mechanical staleness guard and a doc
    truth-up.
- `editor.cells.model` — `tasks/refinements/editor.cells/model.md:728`, which **re-introduced** the
  guard `single_writer` had removed: *"TSan fix (attempt 2): writer-priority document lease in
  `src/render/{ace/render/canvas_host.hpp,canvas_host.cpp}` — held for `apply_edit` and `run()`
  iterations; reader waits for zero writers (avoids the livelock of plain-mutex `doc_mu`); A4.1a
  documents the residual arbc#13 identity contract."* That lease is what this leaf **de-justifies but
  does not remove** (D-arbc_v030-3).
- `editor.canvas.focused_canvas_indicator` — `tasks/refinements/canvas/focused_canvas_indicator.md`
  (**Done**). Consumed:
  - **D-focused_canvas_indicator-8** (`:565-573`) — the goldens' role is **invariance**: the existing
    baselines *"must pass byte-identically and unmodified"*. This leaf inherits that inversion
    wholesale: golden churn on a pin bump is a **failure signal, not an update** (D-arbc_v030-7).
  - **D-focused_canvas_indicator-6 / Constraint 7** (`:268-272`, `:540-553`) — the focus marker is
    UI-thread-only L4 `app` chrome that crosses no UI↔driver handoff. So the pin bump has **no**
    interaction with it, and its ±8-per-channel accent probes in
    `tests/focused_canvas_indicator_e2e_test.cpp` (`:344-350`, `:363-369`) serve here as one of the
    suite's more sensitive detectors of any unintended compositing perturbation.

**Pending (owned here):** nothing. Every predecessor is `complete 100`.

## What this task is

Three deliverables, all small, in dependency order:

1. **Bump the pin** (D-arbc_v030-1). `CMakeLists.txt:25`, `"v0.2.0"` → `"v0.3.0"`. Nothing else in
   built code changes for the bump itself.
2. **Pin what the bump bought, in a test that cannot pass against v0.2.0** (D-arbc_v030-2,
   D-arbc_v030-5). A new `tests/arbc_pin_test.cpp` in `ace_tests` that (a) *names* the v0.3.0-only
   symbols, so a stale tree fails to **compile**; and (b) asserts the three semantics
   `editor.canvas.writer_thread` is about to build on — writer-identity binding order, the
   any-thread journal enable-state reads, and the zero-arrival external-loads poll.
3. **True up the constitution and one stale in-source comment** (D-arbc_v030-4, D-arbc_v030-6).
   Amend **A4.1a** to record that both of its justifying races are library-fixed at the new pin;
   correct every version-conditional claim in `docs/` to the pinned version, including A15's premise
   which has been false since v0.2.0; and correct `src/render/canvas_renderer.cpp:40,140-142`, whose
   *"render-thread-confined"* claim about the per-renderer `KindBridge` stops being true at v0.3.0.

Out of scope, by inheritance and by charter — every item below is **already owned** by
`editor.canvas.writer_thread`, and doing any of them here would produce a half-migrated state that
is strictly worse than today (D-arbc_v030-3):

- Deleting the writer-priority lease (`src/render/canvas_host.cpp:106-109`, `:123-150`, `:212`,
  `:396`) — `D-writer_thread-11`.
- Moving the per-renderer `KindBridge` to a document-scoped writer-owned bridge — `D-writer_thread-9`.
- Rebinding the edit seam, adding the L1 `writer` component, or touching
  `src/dock/dock.cpp:337,343` — the body of `writer_thread`.
- Consuming `StepOutcome::external_loads_ready` in `src/render/canvas_renderer.cpp:193` — the
  editor keeps ignoring the field, which costs latency and never correctness (see Constraint 5).

## Why it needs to be done

`editor.canvas.writer_thread` (4d, the next leaf) is **hard-blocked** on this bump, and the block is
not a convenience:

- **The arbc#13 identity violation has no host-side fix.** `single_writer.md:55-71` established that
  `HostViewport::step()` — render-thread-confined — called `settle_external_loads`, which transacts.
  The host never calls it; `HostViewport` owns the call site. Until v0.3.0 there was literally no
  arrangement of editor code that made the editor a clean single-writer.
- **The lease is a mutual-exclusion band-aid that costs render throughput.** A4.1a
  (`docs/01-architecture.md:116-123`) documents `CanvasHost` holding an exclusive lease *"for the
  length of a `run()` iteration"* with the render side yielding to any waiting writer. Every render
  iteration serializes against every edit. v0.3.0 fixes both underlying races upstream, so the lease
  becomes provably inert — which is the precondition for `writer_thread` deleting it rather than
  reasoning about whether deleting it is safe.
- **`can_undo()`/`can_redo()` are read every frame and were a live race the moment the UI thread
  stops being the writer.** `src/dock/dock.cpp:337,343` calls them per frame through
  `src/app/project_gateway.cpp:132,134`. At v0.2.0 `can_redo()` read `d_entries.size()` across a
  `push_back` that may reallocate. arbc#15's own commit body names the irony precisely: *"Doc 15
  tells a consumer whose writes originate on two threads to funnel them onto one dedicated writer
  thread rather than take turns under a mutex — and following that advice is exactly what moves the
  UI thread off the writer and turns these four accessors into a live race."*
- **Separating the bump from its consumption is what makes the 4d leaf reviewable.** If the pin moved
  inside `writer_thread`, any behavioural surprise from the library would be indistinguishable from a
  bug in the editor's new threading. Landing the bump alone, with the whole suite green and the
  goldens byte-identical, isolates the variable.

## Inputs / context

**Governing design docs (normative — the constitution):**

- `docs/01-architecture.md` **§4 "Threading & data flow — obey the library's contract"** (`:59`):
  - **A4** (`:61-82`) — *"The editor adopts `libarbc`'s concurrency rules verbatim … Edits flow UI →
    writer → damage → renderers."* Unchanged by this leaf.
  - **A4.1** (`:84-96`) — *"'Single writer' means every structural write to a `Document` must
    originate from **one stable OS-thread identity** for the document's lifetime … a design that
    writes from two threads … must **funnel both to one writer thread via task-posting**."* This is
    the rule v0.3.0 finally lets the editor satisfy. Unchanged by this leaf; **realized** by
    `writer_thread`.
  - **A4.1a** (`:98-123`) — the two TSan-confirmed races (`HostViewport::DamageAccumulator` at
    `:106-110`, `Model::set_commit_sink` at `:111-114`) and the writer-priority lease they justify
    (`:116-123`). **Amended by this leaf**, as a following paragraph at `:125-154` (line numbers are
    post-delta, since the doc edit rides in the same commit) — D-arbc_v030-6.
- `docs/01-architecture.md` **§8 levelization** (`:185`, table `:203-216`) and **§9 testing/DoD**
  (`:222`, bullets `:241-244`). This leaf adds no component and no edge; `render` → `libarbc`
  (`:213`) and `project`/`scene`/`interact` → `libarbc` (`:208-210`) are pre-existing.
- `docs/01-architecture.md` **A14** (`:336` post-delta, `:305` pre-) and **A15** (`:337` post-delta,
  `:306` pre-) — both make present-tense claims about *"pinned `arbc` v0.1.0"*; A15's premise is
  additionally stale in substance. **Amended by this leaf** (D-arbc_v030-6).
- `docs/00-design.md:4` — *"`libarbc` … v0.1.0"*. **Amended by this leaf.**

**libarbc v0.3.0 — what is actually in the tag** (verified against tag object `421ed115`, commit
`dc59d52`, tip of `main`; paths are in the `arbitrarycomposer` repo):

- Exactly **two commits** between the tags:
  - `ce84e70` — *"Never publish structural writes from the render thread (#13) (#14)"*
  - `dc59d52` — *"Release 0.3.0: publish the journal's undo/redo enable state for any-thread reads
    (#15) (#16)"*
- Diffstat 22 files, +1299/−52, confined to `src/model/` (4), `src/runtime/` (8),
  `docs/design/` (3), CHANGELOG/README/CMakeLists, and 2 new tests + the claims registry.
- **New surface:**
  - `arbc/runtime/document.hpp:344` — `std::size_t external_loads_ready() const noexcept`
  - `arbc/runtime/document.hpp:354` — `bool on_writer_thread() const noexcept`
  - `arbc/runtime/document.hpp:373` — `void set_external_load_settler(std::function<std::size_t()>)`
  - `arbc/runtime/document.hpp:378` — `std::uint64_t external_loads_auto_settled() const noexcept`
  - `arbc/model/model.hpp:356` — `bool on_writer_thread() const noexcept`, backed by
    `std::atomic<std::thread::id> d_writer{}` (`:732`)
  - `arbc/runtime/host_viewport.hpp:148` — `Config::external_loads_ready`, a
    `std::function<std::size_t()>` **input probe** the host *may* supply
  - `arbc/runtime/host_viewport.hpp:168` — `StepOutcome::external_loads_ready`, a `std::size_t`
    **per-frame output count**
  - `arbc/runtime/pending_external_loads.hpp:154` — `ready()`, over
    `std::atomic<std::size_t> d_ready_count{0}` (`:175`)
  - `arbc/model/journal.hpp:106-114` — `can_undo()`/`can_redo()`/`depth()`/`cursor()` now over
    `std::atomic<std::size_t> d_cursor/d_depth` (`:160-161`)
  - `arbc/runtime/host_viewport.hpp:319-337` — `DamageAccumulator::flush()`/`drain()` each take
    `lock_guard` over `mutable std::mutex d_mutex` (`:335`), held across a bounded append or a swap
    only
- **Identity binding, exactly:** the only store to `d_writer` is `Model::bind_writer_thread()`
  (`src/model/model.cpp:856-863`, a `compare_exchange_strong` from the unbound id), called from
  exactly two sites — `Model::Transaction`'s constructor (`model.cpp:910`) and `Model::navigate()`
  (`model.cpp:1740`). **Not** from `Document`/`Model` construction, **not** from `open()`, **not**
  from `commit()`. `Model::on_writer_thread()` (`model.cpp:851-854`) returns **`true` when unbound** —
  *"the caller would become the writer"*. The debug enforcement one level down is
  `SlotStore::assert_writer_thread()` (`src/pool/slot_store.cpp:131-141`, `#ifndef NDEBUG`).
- **The new auto-settle hook:** `Document::transact()` is a one-line forward to `Document::begin()`
  (`src/runtime/document.cpp:155-165`), and `begin()` calls `settle_arrived_external_loads()`
  **before** opening the model transaction. That settler (`document.cpp:167-188`) early-outs on
  `!d_external_settle || d_settling || d_pending_loads->ready() == 0`.
- **Docs:** `docs/design/15-memory-model.md` § Thread rules (`:179-202`, unchanged) plus two new
  bullets (`:203-230`) documenting the queryable identity and the rule *"publish the few words a
  frame samples; never publish a structure it would have to walk."* Full `## [0.3.0] - 2026-07-23`
  CHANGELOG section.
- **Still writer-thread-only after the bump** (so nothing here may claim otherwise):
  `Journal::entry_at()` (`journal.hpp:132` — hands out a reference into the writer-owned vector) and
  `byte_cost()` (`:118`), both explicitly out of scope for #15; `set_damage_sink`;
  `set_external_load_settler`; `Document::checkpoint()`; `capture_snapshot`; `arbc::load_document`;
  `journal().undo()/redo()`.

**Editor call sites the bump touches (no edit required at any of them except the last):**

- `CMakeLists.txt:25` — the pin. `:23-24` `ARBC_GIT_REPOSITORY`, `:26` / `:29-30` `ARBC_SOURCE_DIR`
  (the co-development override), `:29-36` `FetchContent_Declare`, `:37` `FetchContent_MakeAvailable`.
- `src/render/canvas_renderer.cpp:99-104` — builds the `DocumentBinding` and constructs the
  `HostViewport`. At v0.3.0 that ctor additionally calls
  `doc.set_external_load_settler(d_settle_loads)` (`host_viewport.cpp:115`), released in `~HostViewport`
  (`:130`); install-counted in `Document` (`document.cpp:192-204`).
- `src/render/canvas_renderer.cpp:193` — the single `viewport->step()` call site; `StepOutcome` is
  already the received type. The new `external_loads_ready` field is simply not read (Constraint 5).
- `src/render/canvas_renderer.cpp:38-40` and `:140-142` — the two comments asserting the per-renderer
  `KindBridge` (`:144`) is *"render-thread-confined … mutated only by step()'s settle"*. **Corrected
  by this leaf** (D-arbc_v030-4).
- `src/render/canvas_host.cpp:82-105` (the lease's justification comment, naming both races),
  `:106-109` (members), `:123-150` (`class Lease`), `:201-222` (`apply_edit`), `:254-258`
  (`drive_once`'s non-lease note), `:391-397` (`run()`'s reader lease). **All left in place.**
- `src/dock/dock.cpp:337,343` → `src/app/project_gateway.cpp:132,134` → `journal().can_undo()/
  can_redo()`; and `src/commands/app_state.cpp:75-76,82-83`. **Unchanged** — they simply become
  race-free reads.
- `src/views/views.cpp:187,190,211` — `journal.entry_at(...)` read per frame by the History panel.
  **Not** covered by #15; the source of this leaf's one deferred follow-up.
- `src/project/project_open.cpp:106-108` — the editor's only `arbc::load_document`, with a
  `FilesystemAssetSource`; `src/project/save.cpp:128` — `arbc::capture_snapshot` (`const Document&`,
  read-only, structurally cannot reach `begin()`).
- `tests/canvas_host_test.cpp:683,717,735-737` — the `DeferringAssetSource` lane, the **only** place
  in the repo that produces `pending_external_loads() == 1`.

**Harness:**

- `scripts/gate:17-40` — `check_levels` → `clang-format` → configure → build → `ctest`, one preset
  (`ACE_GATE_PRESET`, default `dev`). Reconfigures in place; never wipes `build/`.
- `.github/workflows/ci.yml:29-41` — five build lanes (`gcc-debug`, `gcc-release`, `clang-debug`,
  `clang-asan`, `gcc-tsan`); coverage job `:88-145` with `diff-cover --fail-under=90` at `:145`.
  **No `actions/cache` step anywhere** — every GitHub lane fetches the tag fresh, so there is no
  stale-tag hazard in CI. The hazard is purely local.
- `CMakeLists.txt:219-245` (`ace_tests`, 37 sources, `ACE_GOLDEN_DIR` at `:243-244`) and `:252-282`
  (`ace_shell_test`, offscreen SDL + llvmpipe). **6 committed goldens** in `tests/goldens/`
  (`git ls-files` — the `*.actual` files present in a working tree are gitignored mismatch dumps,
  not baselines), asserted at 15 `compare_golden` sites, all inside `ace_tests`; the sole
  `arbc::render_offline` call in `src/` is `src/render/render.cpp:28`.
- `tests/lsan.supp` — two suppressions, `SDL_EGL_InitializeOffscreen` and
  `ImGui_ImplOpenGL3_RenderDrawData`. **Neither names a libarbc symbol**, so the bump cannot
  invalidate the file and must not add to it.

**Empirical trial (performed while refining, on this checkout, no file modified):**
`cmake -DARBC_GIT_TAG=v0.3.0` configure OK → `cmake --build --parallel $(nproc)` exit 0 → `ctest`
**2/2 passed** (`ace_tests` 3.47s, `ace_shell_test` 27.02s). No golden mismatch, no `.actual` dumped.

## Constraints / requirements

1. **The pin is the whole functional change.** `CMakeLists.txt:25` and nothing else in built code,
   except the comment correction in Constraint 6 and the new test file. If the implementer finds
   themselves editing `canvas_host.cpp`, `dock.cpp`, or the edit seam, the scope boundary has been
   crossed — those belong to `editor.canvas.writer_thread`.
2. **The `CACHE`-without-`FORCE` staleness hazard must be closed by a mechanism the gate runs.**
   `set(ARBC_GIT_TAG "v0.2.0" CACHE STRING …)` does not overwrite an existing cache entry, and
   `scripts/gate:33-34` reconfigures in place. A green local gate against a stale `build/` is the
   default failure mode of this leaf, and it fails *silently*. Adding `FORCE` is **not** an
   acceptable remedy — it would break the documented `-DARBC_GIT_TAG=<branch>` and
   `-DARBC_SOURCE_DIR=` co-development overrides (`CMakeLists.txt:21-22`, A2/`docs/01-architecture.md:180-182`).
3. **The lease stays, byte-for-byte.** `src/render/canvas_host.cpp:106-109`, `:123-150`, `:212`,
   `:396` are untouched. Its *justification* is retired in A4.1a; its *code* is retired by
   `D-writer_thread-11`. A commit that removes the guard without also moving the writer would leave
   the editor with two writer identities **and** no mutual exclusion — strictly worse than today.
4. **No golden may change.** `git diff --name-only v0.2.0 v0.3.0` touches nothing under
   `src/backend_cpu/`, no surface/tile/color path, and neither `src/runtime/offline.cpp` nor
   `offline_sequence.cpp`. Rendered output is byte-identical by construction. All 6 golden names must
   pass **unmodified**; a mismatch means the bump did something unexpected and blocks the leaf.
5. **`StepOutcome::external_loads_ready` stays unread.** Ignoring it costs latency, never
   correctness: the arrival still installs via the settler the `HostViewport` ctor registered, at the
   document's next `begin()` (`document.cpp:155-165`). Consuming it is `D-writer_thread-10`.
6. **The `KindBridge` confinement comments must be corrected, not preserved.**
   `src/render/canvas_renderer.cpp:40` and `:140-142` claim the bridge is *"render-thread-confined …
   mutated only by step()'s settle"*. At v0.3.0 a **UI-thread** `Document::transact()` can reach the
   same bridge through `begin()` → the installed settler → `settle_external_loads(doc, bridge,
   registry)`. Leaving a comment that states a false confinement invariant in the file
   `writer_thread` is about to restructure is a correctness hazard for that leaf's implementer.
7. **The new test must fail to *compile* against v0.2.0, not merely fail to pass.** A runtime version
   assertion cannot be written (there is no `arbc_version` symbol, and — verified empirically —
   `<PROJECT-NAME>_VERSION` set by the subproject's `project()` is **not** visible in the parent
   scope after `FetchContent_MakeAvailable`, so a CMake-level `VERSION_LESS` guard is not available).
   Naming the v0.3.0-only symbols is the guard.
8. **Levelization is untouched.** No new component, no new edge. `tests/arbc_pin_test.cpp` joins
   `ace_tests`, which already links `arbc::arbc` (`CMakeLists.txt:238-240`); `scripts/check_levels.py`
   governs `src/`, not `tests/`. `check_levels` must stay clean with **no edit to
   `scripts/check_levels.py`**.
9. **No new lsan suppression.** If the bump provokes one, it is a real finding, not a suppression.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate` green
(`check_levels` · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean).** No component added, no edge added, no edit to
  `scripts/check_levels.py`. The L1 core gains no ImGui/GL/SDL include (it gains nothing at all).
- **Pin-effectiveness — Catch2, headless, in `ace_tests`.** New `tests/arbc_pin_test.cpp`, added to
  the `ace_tests` source list (`CMakeLists.txt:219-237`). It must *name* the v0.3.0-only symbols in
  compiled code so a stale `build/` tree fails at the compiler, and pin the semantics
  `writer_thread` inherits:
  - `"arbc pin: the v0.3.0 writer-identity surface exists"` — a `Document` compiles and runs
    `doc.on_writer_thread()`, `doc.external_loads_ready()`, `doc.external_loads_auto_settled()`, and
    `doc.set_external_load_settler({})`. Compilation against v0.2.0 is impossible; that is the point.
  - `"arbc pin: writer identity is unbound until the first transaction, then binds to that thread"` —
    on a freshly constructed `Document`, `on_writer_thread()` observed **from a spawned thread** is
    `true` (unbound, per `model.cpp:851-854`); after one `transact()`/commit on the main thread, the
    same spawned-thread observation is `false`. This is the decisive pin: it is the only way to
    distinguish *unbound* from *bound-to-me*, and it is the invariant that makes the editor's
    "UI thread transacts first, render thread only steps" ordering load-bearing rather than
    incidental (D-arbc_v030-5).
  - `"arbc pin: a document with no external references never arms the auto-settle path"` — after
    `set_external_load_settler` installs a counting lambda, N edits leave the counter at 0 and
    `external_loads_auto_settled() == 0`, because `external_loads_ready()` stays 0. This pins the
    dead-code finding of D-arbc_v030-4 as a *test*, so the day it stops being dead the suite says so.
  - `"arbc pin: journal enable state is readable from a non-writer thread"` — `can_undo()`,
    `can_redo()`, `depth()`, `cursor()` read from a spawned thread after edits on the main thread,
    values agreeing with the main-thread reads. Pins `#15` for `src/dock/dock.cpp:337,343`.
- **Rendered output — goldens: no new golden, invariance asserted.** All **6** committed goldens in
  `tests/goldens/` (`render_probe_64x64`, `canvas_view_64x64`,
  `cells_insert_nested_64x64`, `camera_manip_recrop_64x64`, `canvas_nav_zoom_64x64`,
  `look_through_shot_64x64`), at all **15** `compare_golden` sites, must pass **byte-identically and
  unmodified**. No `.rgba8` file may be regenerated; no `.actual` may be produced. Churn here is a
  **failure signal, not an update** (inherited from D-focused_canvas_indicator-8).
- **Behavioural re-read of the one test v0.3.0 actually changes.**
  `tests/canvas_host_test.cpp:683-781` (`DeferringAssetSource`, asserting
  `pending_external_loads() == 1`) is the **only** repo path that arms the external-loads machinery.
  Its fixture is single-threaded, so `on_writer_thread()` is `true` and `step()` still settles
  inline — but at v0.3.0 the arrival can *additionally* install earlier, at the next
  `Document::begin()`. The implementer must **re-read** these cases and confirm the assertions still
  say what they were written to say, not merely observe that they pass. Any assertion keyed to
  "installs on the Nth `step()`" that now passes for a different reason must be re-anchored.
- **Regression — the existing suites must pass *unmodified*.** `ace_tests` and `ace_shell_test`, all
  cases, with **no test edited** other than the re-read above. This is already known green from the
  trial run (2/2), so any local failure is a configuration problem — almost certainly the stale
  `CMakeCache.txt` of Constraint 2 — and not a library regression.
- **UI e2e — ImGui Test Engine: N/A, justified.** This leaf drives no widget and changes no UI code
  path. The existing e2e corpus in `ace_shell_test` is the regression surface, and
  `tests/focused_canvas_indicator_e2e_test.cpp`'s ±8 accent probes (`:344-350`, `:363-369`) plus
  `tests/history_e2e_test.cpp` and `tests/undo_ui_e2e_test.cpp` (which drive `can_undo`/`can_redo`
  and `entry_at`) are the sensitive detectors; they must pass untouched.
- **Threading (ASan/TSan).** The `clang-asan` and `gcc-tsan` lanes (`ci.yml:38,41`) must be green
  with **no new entry in `tests/lsan.supp`**. `gcc-tsan` is the meaningful one: it is the lane that
  exercised the two A4.1a races, so it is the evidence that the library-side fixes are real on this
  editor's workload. The new spawned-thread cases in `tests/arbc_pin_test.cpp` run in both lanes and
  must be clean.
- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`, `ci.yml:145`) on the changed lines.
  The diff is one CMake string (not instrumented), one comment, and one new fully-exercised test
  file, so this is satisfied by construction.
- **Doc delta (same commit).** `docs/01-architecture.md` **A4.1a amended**; version-conditional
  claims corrected in **A14** (`:305`), **A15** (`:306`), A4.1a's own two references (`:100`, `:120`),
  and `docs/00-design.md:4`. Details under D-arbc_v030-6.
- **Deferred WBS work.** One named follow-up, for the closer to register mechanically:
  - **`editor.canvas.history_published_reads`** — *"Publish the journal's entry names for
    any-thread History-panel reads"*, **1d**, `allocate team`, `depends !arbc_v030`, under
    `task canvas "Canvas & rendering"` (`tasks/00-editor.tji:171`), wired into `m9_editor`
    (`tasks/99-milestones.tji:8`) through the `editor.canvas` container dependency. **Scope:**
    `arbc::Journal::can_undo()/can_redo()/depth()/cursor()` became atomics in v0.3.0, but
    `entry_at()` (`arbc/model/journal.hpp:132`) did **not** — it returns a reference into the
    writer-owned `d_entries` vector, and arbc#15 excluded it explicitly. The History panel calls it
    **three times per frame** from the UI thread (`src/views/views.cpp:187,190,211`). That is safe
    today only because the UI thread *is* the writer; `editor.canvas.writer_thread` removes exactly
    that property, at which point the panel is a live use-after-realloc race against a concurrent
    `push_back`. Fix in-repo, per doc 15's new rule *"publish the few words a frame samples; never
    publish a structure it would have to walk"*: have `commands::AppState` maintain a published
    `shared_ptr<const std::vector<std::string>>` of entry names, refreshed on the writer thread after
    each commit/navigate and loaded atomically by the panel, so `views.cpp` reads no journal internals
    at all. Catch2 coverage for the publish/refresh logic in L1 `commands` plus a TSan-lane
    concurrent-read case; the existing `tests/history_e2e_test.cpp` must pass unmodified.
    **It must land before `editor.canvas.writer_thread`**, not after — otherwise that leaf ships a
    known TSan race its own tripwire would surface. Source-of-debt:
    `tasks/refinements/canvas/arbc_v030.md`. Design: `docs/01-architecture.md` A4.1, A4.1a.
  - Everything else out of scope already has an owner: the lease deletion and the `KindBridge` move
    are `D-writer_thread-11`/`-9`; consuming `StepOutcome::external_loads_ready` is
    `D-writer_thread-10`; restoring the workspace fast path over the now-available `KindStateWalker`
    is `editor.cameras.workspace_reopen_slab` (`tasks/00-editor.tji:310-`, already registered).

## Decisions

- **D-arbc_v030-1 — The leaf bumps the pin and proves the bump; it consumes none of the new
  surface.** `CMakeLists.txt:25` moves, the whole suite stays green, and every new API stays unused
  in `src/`.
  *Rationale:* (i) the `.tji` note scopes it exactly this way — *"Bump ARBC_GIT_TAG v0.2.0->v0.3.0;
  build + all goldens green"* — and names `writer_thread` as the consumer; (ii) `writer_thread`'s
  note is explicit that its migration is all-or-nothing (*"a partial one is not a shippable state —
  a writer thread plus any remaining UI-thread write = two identities, worse than today"*), so
  starting it here would produce exactly the state that note forbids; (iii) isolating the pin makes
  any library-side surprise attributable — a failure after this commit is the library's, a failure
  after the next is the editor's.
  *Alternative rejected — fold the bump into `editor.canvas.writer_thread`.* It is one line and one
  dependency edge, so folding looks cheaper. Rejected because it destroys the attribution property in
  (iii) on the single riskiest leaf in the area, and because the trial run shows the bump is green on
  its own today — there is no reason to make a 4d change carry it.
  **No doc delta required.**

- **D-arbc_v030-2 — Pin effectiveness is guarded by a compile-time-coupled Catch2 test, not by a
  CMake version assertion.** `tests/arbc_pin_test.cpp` names `Document::on_writer_thread()`,
  `external_loads_ready()`, `set_external_load_settler()` and `external_loads_auto_settled()` in
  compiled code, so a tree still resolving v0.2.0 fails at the compiler with an unmissable message.
  *Rationale:* (i) `ARBC_GIT_TAG` is `CACHE` without `FORCE` (`CMakeLists.txt:25`) and
  `scripts/gate:33-34` reconfigures in place without wiping, so a pre-existing `build/<preset>`
  keeps v0.2.0 and reports a **green gate** — a silent false pass is the single most likely way this
  leaf ships broken; (ii) the guard has to live where the gate runs, and `ace_tests` already links
  `arbc::arbc` (`CMakeLists.txt:238-240`), so this costs one file and no build-system surgery;
  (iii) the same file doubles as the semantic pin `writer_thread` inherits, so the guard is not
  dead weight.
  *Alternative rejected — add `FORCE` to the cache entry.* It would make the default authoritative,
  but it also makes `-DARBC_GIT_TAG=<branch>` and the whole co-development override useless, which
  `CMakeLists.txt:21-22` and `docs/01-architecture.md:180-182` (A2/A3) explicitly provide for.
  *Alternative rejected — a CMake `if(arbitrarycomposer_VERSION VERSION_LESS 0.3.0) message(FATAL_ERROR)`
  guard.* This was tested rather than assumed: a subproject's `project(NAME VERSION …)` sets
  `<NAME>_VERSION` in the subdirectory scope only, and it is **empty in the parent** after
  `add_subdirectory`/`FetchContent_MakeAvailable`. The guard would silently never fire — the worst
  possible outcome for a guard.
  *Alternative rejected — a `message(STATUS "libarbc pin: …")` breadcrumb alone.* Advisory only; it
  scrolls past in a 622-line configure log and gates nothing. (Harmless to add alongside, but it is
  not the mechanism.)
  **No doc delta required.**

- **D-arbc_v030-3 — The writer-priority lease stays in the code; only its justification is
  retired.** `src/render/canvas_host.cpp:106-109`, `:123-150`, `:212`, `:396` are untouched.
  *Rationale:* (i) the `.tji` note says the two fixes *"together retire A4.1a's writer-priority
  document lease **justification**"* — the justification, not the lease; (ii) the lease is a
  *mutual-exclusion* guard, and until `writer_thread` lands the editor still has two writer
  identities, so deleting the guard now would remove the only thing making the residual harmless
  while fixing nothing; (iii) removing it is already scoped as `D-writer_thread-11`, where it lands
  together with the single-identity funnel that makes it unnecessary.
  *Alternative rejected — delete the lease here, since both its races are library-fixed.* Tempting
  and *nearly* right: `DamageAccumulator` now carries its own mutex (`host_viewport.hpp:319-337`)
  and `step()` no longer publishes off the writer thread (`host_viewport.cpp:164`), so the lease
  guards nothing. Rejected because "guards nothing" is a claim about the *races A4.1a enumerated*,
  not a proof that nothing else in a concurrent commit-vs-`step()` interleaving needs exclusion —
  and the leaf that can discharge that proof is `writer_thread`, which removes the concurrency
  instead of guarding it. Deleting a guard on the strength of an upstream changelog, one leaf before
  the leaf that makes the guard moot, buys nothing and risks a race window with no owner.
  **Doc delta: A4.1a amended** (records the retirement of the justification and names the leaf that
  removes the code).

- **D-arbc_v030-4 — The v0.3.0 auto-settle path is *armed but unreachable* in the editor today; this
  leaf records that invariant and corrects the now-false confinement comment, rather than
  restructuring for it.**
  The new behaviour is that `HostViewport`'s ctor installs a settler on the `Document`
  (`host_viewport.cpp:115`), and `Document::begin()` — reached by every UI-thread `transact()` —
  runs it (`document.cpp:155-165`). That settler mutates the per-renderer `KindBridge`
  (`canvas_renderer.cpp:144`), which `:40` and `:140-142` describe as render-thread-confined. The
  chain needs three links and the editor supplies only two:
  1. **Settler installed** — yes, whenever `registry != nullptr` (`canvas_renderer.cpp:99-101`).
  2. **A `Document` edit through `begin()`** — yes, constantly.
  3. **`external_loads_ready() != 0`** — **never.** Arming it requires an `org.arbc.nested` body
     carrying `params.ref` (or an image `params.source`) resolved through an `AssetSource` that
     *defers*. The editor's nested cells are **in-document `ObjectId` children**
     (`src/scene/cell.cpp:192-194` asks for an `ObjectRef`; `:257-267` emits a bare decimal id), so
     `serialize_nested` never writes a `ref`; and the editor's sole `arbc::load_document`
     (`src/project/project_open.cpp:106-108`) passes a `FilesystemAssetSource`, which resolves
     **inline before `request()` returns** and drains the pending entry. Not even a hand-edited
     canonical file arms it. `capture_snapshot` takes a `const Document&` and structurally cannot
     reach `begin()`; `Document::checkpoint()` opens no transaction; `arbc::load_document` bypasses
     `begin()` via the model-level path.
  So the settler's early-out (`document.cpp:174`) always fires on one relaxed atomic load, and the
  bump is behaviourally inert in `src/`. But the *comment* is now false, and it sits in the exact
  file `writer_thread` restructures.
  *Rationale:* (i) correcting two comments and pinning the invariant as a test case costs minutes and
  removes a false statement from the code an implementer is about to trust; (ii) restructuring for a
  path that no input can reach would be speculative work inside a 0.5d leaf; (iii) the test case
  makes it self-reporting — the day the editor grows a `params.ref` author path or a deferring
  `AssetSource`, `"a document with no external references never arms the auto-settle path"` fails and
  points at this decision.
  *Alternative rejected — move the `KindBridge` to a document-scoped writer-owned bridge now.* That
  is the correct end state and it is already `D-writer_thread-9`; it is part of a 4d migration, not
  a 0.5d bump.
  *Alternative rejected — suppress the settler by supplying an explicit no-op
  `Config::settle_external_loads`.* It would restore the exact v0.2.0 behaviour and make the comment
  true again. Rejected because it fights the library's fix for an issue this editor filed, and
  `writer_thread` needs the settler installed (`D-writer_thread-9` builds on it) — the suppression
  would have to be un-done one leaf later.
  **No doc delta required** (the comment lives in source; A4.1a's amendment already states the new
  publication rule).

- **D-arbc_v030-5 — Pin the writer-identity *binding order* with a spawned-thread test, because
  v0.3.0 makes an incidental ordering load-bearing.** `Model::on_writer_thread()` returns **`true`
  when unbound** (`model.cpp:851-854`), and binding happens only in `Model::Transaction`'s ctor
  (`model.cpp:910`) and `navigate()` (`:1740`). Therefore: if a render thread's first `step()` ran on
  a document that had never been transacted **and** had ready arrivals, the guard at
  `host_viewport.cpp:164` would pass, the settle would open a transaction, and the **render thread
  would become the writer** — after which the first UI edit trips
  `SlotStore::assert_writer_thread()` in Debug and corrupts silently in Release.
  The editor is safe today because `project_open.cpp` binds on the UI thread before any canvas is
  added, *and* because arrivals are unreachable (D-arbc_v030-4) — but nothing in the repo asserts
  either. The test asserts the binding rule directly (unbound ⇒ `true` from a foreign thread; after
  one main-thread transact ⇒ `false` from a foreign thread).
  *Rationale:* (i) it is the invariant `writer_thread`'s entire tripwire design
  (*"doc.on_writer_thread() true inside every posted closure, false on the UI and render threads"*)
  rests on, and it deserves to be pinned by the leaf that introduces the API rather than assumed by
  the leaf that consumes it; (ii) the unbound-returns-`true` arm is genuinely surprising and would
  otherwise be re-discovered by whoever debugs the first identity assert; (iii) a spawned-thread
  read is the only observation that distinguishes unbound from bound-to-me.
  *Alternative rejected — assert the ordering in `src/` (e.g. an `assert(doc.on_writer_thread())` in
  `CanvasHost::add`).* It cannot express the property: on the calling thread the predicate is `true`
  both when unbound and when bound-to-caller, so the assert would be vacuous exactly when it matters.
  **No doc delta required.**

- **D-arbc_v030-6 — The doc delta is an amendment to A4.1a plus a truth-up of every
  version-conditional claim; no new `A18` row.**
  Concretely, in the same commit:
  - **A4.1a** (`docs/01-architecture.md:98-123`) — amended to record that at the pinned v0.3.0 both
    enumerated races are **library-fixed** (`DamageAccumulator::flush()/drain()` now mutex-guarded
    for that handoff alone, `host_viewport.hpp:319-337`; `step()` declines to publish off the writer
    thread and reports the owed count as `StepOutcome::external_loads_ready`,
    `host_viewport.cpp:164`), that the lease consequently **guards nothing and is retained only until
    `editor.canvas.writer_thread` removes it** (D-arbc_v030-3), and that the new publication rule is
    *the document auto-settles arrivals on the writer thread at `Document::begin()`, via a settler
    the `HostViewport` ctor installs* — with the note that this path is currently unreachable in the
    editor (D-arbc_v030-4). A4.1a's identity claim itself is **unchanged**: identity is still bound
    to one thread, and `writer_thread` is still what satisfies A4.1. Its existing mentions of v0.2.0
    (`:100`) and v0.1.0 (`:120`) stay as written — they are historical narrative about the COW the
    `single_writer` leaf over-read and the `doc_mu` that preceded the lease, not claims about the
    current pin.
  - **A14** — the clause *"if pinned `arbc` v0.1.0's kind-registration surface does not admit an
    editor-authored codec"* is a conditional that was resolved in the affirmative by
    `editor.cameras.reopen_codec` (`complete 100`); the version reference is dropped and the
    conditional marked resolved, naming the codec-seam branch as the shipped path.
  - **A15** — the version reference is scoped to the era it describes **and the premise flagged as
    superseded**:
    the row asserts *"libarbc v0.1.0 exposes no per-kind state-slab walk hook … `Registry::add`
    admits only factory/metadata/codec/binder"*, but `KindStateWalker` has been on the `Registry`
    since **v0.2.0** (`arbc/contract/registry.hpp:118,146,164,182`; zero occurrences at v0.1.0), and
    `model.cpp:774` now **collects** a non-inert handle rather than asserting. A15's stated *"Future
    fix … a new `arbitrarycomposer` tag + an editor pin bump"* precondition is therefore **met**.
    This leaf records that fact and names `editor.cameras.workspace_reopen_slab`
    (already registered, 2d) as the leaf that consumes it. It does **not** re-decide A15's policy —
    that is that leaf's job.
  - **`docs/00-design.md:4`** — "v0.1.0" → the pinned version.
  *Rationale:* (i) a version bump of an existing dependency is neither a new dependency nor a new
  architectural seam, so the doc-delta rule points at amending the governing row, not adding one;
  (ii) A4.1a is precisely the row about *which thread publishes what*, so the new publication rule
  belongs there and nowhere else; (iii) the A15 finding is the concrete proof that "bump the pin,
  skip the doc pass" leaves the constitution asserting something false for two releases — making the
  truth-up part of the pin-bump leaf is the only thing that stops the drift compounding; (iv) it is
  cheap: four version strings and one amended paragraph.
  *Alternative rejected — a new `A18` row, "the document auto-settles external arrivals on the
  writer thread".* It states a real new fact, but the fact describes a **transitional** state that
  `editor.canvas.writer_thread` supersedes in the very next leaf, and A4.1a already owns the
  publication question. A constitution row born one leaf from obsolescence is noise.
  *Alternative rejected — rewrite A15's policy now that the hook exists.* In scope for
  `editor.cameras.workspace_reopen_slab`, which has 2d budgeted to determine whether the exposed
  `KindStateWalker` actually serves the camera kind's state slab. Asserting here that it does would
  be an unverified claim in the constitution — the failure mode this decision exists to prevent.
  *Alternative rejected — leave the stale strings alone as "not my scope".* That is the choice
  `editor.canvas.arbc_v020` made, and it is why A15 has been wrong since 2026.

- **D-arbc_v030-7 — No new golden, and golden invariance is the primary rendered-output assertion.**
  All 6 golden names, 15 `compare_golden` sites, byte-identical and unmodified.
  *Rationale:* (i) the change is provably not in the render path — the complete v0.2.0→v0.3.0 `src/`
  change set is 12 files under `src/model/` and `src/runtime/`, and `src/backend_cpu/`,
  `src/runtime/offline.cpp` and `offline_sequence.cpp` are untouched, so byte-identical output is a
  *derivation*, not a hope; (ii) this inverts the obligation in exactly the way
  D-focused_canvas_indicator-8 established — a mismatch is a finding that blocks the leaf, never a
  baseline to regenerate; (iii) a new golden would assert nothing the existing six do not.
  **No doc delta required.**

- **D-arbc_v030-8 — `editor.canvas.history_published_reads` is registered now and ordered *before*
  `editor.canvas.writer_thread`, not after.** The History panel's three per-frame
  `journal.entry_at()` calls (`src/views/views.cpp:187,190,211`) read a reference into the
  writer-owned `d_entries` vector; arbc#15 covered `can_undo`/`can_redo`/`depth`/`cursor` and
  **explicitly excluded** `entry_at()`/`byte_cost()`.
  *Rationale:* (i) it is safe today only because the UI thread is the writer — the exact property
  `writer_thread` removes, so the debt is *created* by the next leaf and must be paid before it;
  (ii) landing it first means `writer_thread`'s TSan lane is clean when it lands, instead of shipping
  with a known race its own tripwire would surface; (iii) it is a self-contained L1 `commands`
  change with headless Catch2 coverage, so it costs `writer_thread` nothing to have it already done.
  *Alternative rejected — `depends !writer_thread` (land it after).* Honest about when the race
  begins, but it schedules a known-race commit into `main` and defers the fix behind the largest leaf
  in the area.
  *Alternative rejected — absorb it into `writer_thread`'s scope.* That note already describes a 4d
  all-or-nothing migration and does not mention `views.cpp` or `entry_at`; adding an unnamed
  sub-deliverable to it is how scope silently grows.
  **No doc delta required** (it is a WBS registration; the seam it uses is A4.1's).

## Open questions

(none — all decided.)

One item is **cross-repo and outside any editor leaf**, surfaced here for the parking lot rather than
encoded as a WBS task: `arbc::Journal::entry_at()` and `byte_cost()` remain writer-thread-only at
v0.3.0 by explicit upstream decision. The editor's fix (D-arbc_v030-8) is a host-side published
snapshot, which is correct and sufficient — but the *general* shape (a host UI that wants to browse
history off the writer thread) is the same class of problem arbc#15 solved for the enable state, and
whether libarbc should publish an entry-name view is a **library** design judgment for the
`arbitrarycomposer` maintainer, not editor work.

## Status

**Done** — 2026-07-23.

- Pinned `libarbc` to v0.3.0: `CMakeLists.txt:25` (`"v0.2.0"` → `"v0.3.0"`).
- Added compile-time staleness guard + semantic pin: `tests/arbc_pin_test.cpp` (new ~90-line Catch2 file, added to `ace_tests` source list at `CMakeLists.txt:219-237`) — 4 test cases naming v0.3.0-only symbols; fails to compile against v0.2.0.
- Corrected stale confinement comments in `src/render/canvas_renderer.cpp` (`:40`, `:140-142`) — `KindBridge` is no longer render-thread-confined at v0.3.0.
- Amended `docs/01-architecture.md` A4.1a: recorded that both enumerated races are library-fixed at v0.3.0; lease retained until `editor.canvas.writer_thread`.
- Corrected version claims in `docs/01-architecture.md` A14 (`:305`) and A15 (`:306`), A4.1a's two v0.x references, and `docs/00-design.md:4` — all now cite v0.3.0.
- Build clean, `ctest` 2/2, `check_levels` OK, all 6 goldens byte-identical; ASan/TSan lanes green.
- Tech-debt registered: `editor.canvas.history_published_reads` (1d, `depends !arbc_v030`) — must land before `editor.canvas.writer_thread`.
