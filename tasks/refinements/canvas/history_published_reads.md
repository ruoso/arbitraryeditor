# editor.canvas.history_published_reads — Publish journal entry names for any-thread History-panel reads

## TaskJuggler entry

- **Task:** `editor.canvas.history_published_reads` — *"Publish journal entry names for any-thread History-panel reads"*
- **Effort:** `1d`
- **Depends:** `!arbc_v030` (`editor.canvas.arbc_v030`)
- **Note (`tasks/00-editor.tji:284`):** *"arbc::Journal::can_undo()/can_redo()/depth()/cursor() became atomics in v0.3.0 (#15), but entry_at() (arbc/model/journal.hpp:132) did not — it returns a reference into the writer-owned d_entries vector, excluded explicitly by #15. The History panel calls it three times per frame from the UI thread (src/views/views.cpp:187,190,211). Safe today only because the UI thread IS the writer — the exact property editor.canvas.writer_thread removes. Fix: have commands::AppState maintain a published shared_ptr<const std::vector<std::string>> of entry names, refreshed on the writer thread after each commit/navigate and loaded atomically by the panel, so views.cpp reads no journal internals at all. Catch2 coverage for the publish/refresh logic in L1 commands plus a TSan-lane concurrent-read case; existing tests/history_e2e_test.cpp must pass unmodified. Must land BEFORE editor.canvas.writer_thread — otherwise that leaf ships a known TSan race. Source-of-debt: tasks/refinements/canvas/arbc_v030.md. Design: docs/01-architecture.md A4.1, A4.1a."*
- **Back-link:** `tasks/00-editor.tji:280-285`, under `task canvas "Canvas & rendering"`.
- **Source of debt:** `tasks/refinements/canvas/arbc_v030.md` — its one registered follow-up (`D-arbc_v030-8`).
- **Downstream dependents:** `editor.canvas.writer_thread` (`tasks/00-editor.tji:286-291`, `depends !arbc_v030, !history_published_reads`).
- **Milestone:** `m9_editor` (`tasks/99-milestones.tji:8`), reached through the `editor.canvas` container dependency.

## Effort estimate

**1d.** The work is one small L1 type plus its publish points and one panel rewrite: a `HistorySnapshot`/`HistoryPublisher` pair in `src/commands/` (~90 lines with comments), a `navigate_to` verb beside the shipped `undo`/`redo` (`src/commands/app_state.cpp:67-84`), a post-edit hook on `CanvasHost` (~8 lines, one installer in `src/app/shell.cpp`), and a rewrite of `draw_history` (`src/views/views.cpp:176-231`) that *removes* code — it drops two libarbc includes and the click-to-jump loop. The test surface is one new Catch2 file and one doc-delta row. No new component, no build change beyond one `add_executable` source line.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.canvas.arbc_v030`** — `tasks/refinements/canvas/arbc_v030.md` (**Done**, commit `7abc9cb`). Consumed:
  - The pin is at libarbc v0.3.0 (`CMakeLists.txt:25`), so `Journal::depth()` (`arbc/model/journal.hpp:112`) and `Journal::cursor()` (`:114`) are relaxed-atomic **ANY THREAD** reads. This leaf uses them as the publisher's staleness stamp, and it is the *only* reason a stamp is legal off the writer.
  - Its `D-arbc_v030-1` — the bump *"consumes none of the new surface"*. This leaf is the first consumer, and it consumes exactly the two accessors #15 published.
  - Its explicit carve-out (`arbc_v030.md:242-246`): *"Still writer-thread-only after the bump (so nothing here may claim otherwise): `Journal::entry_at()` (`journal.hpp:132` — hands out a reference into the writer-owned vector) and `byte_cost()`."* That sentence is this leaf's entire problem statement.
  - Its `D-arbc_v030-3` — the writer-priority document lease **stays in the code**; only its justification is retired, and its removal belongs to `editor.canvas.writer_thread`. So this leaf may use `CanvasHost::apply_edit` (`src/render/canvas_host.cpp:201-222`) as a live writer-turn boundary rather than treating it as already-dead code.
- **`editor.project.history`** — `tasks/refinements/editor/history.md` (**Done**). Consumed: the shipped panel shape (`D-history-1` list model, `D-history-4` base row + chronological entries, `D-history-5` bounded single-step navigation, `D-history-3` L4-registered body capturing the one `AppState&`) and its `Constraint 1` — *the panel never mutates the journal directly*. All preserved verbatim. Its `D-history-6` (*"keeps no shadow copy"*, `src/views/views.cpp:177-179`, `src/views/ace/views/views.hpp:125-127`) is the one decision this leaf **reverses**, with rationale under `D-history_published_reads-1`.
- **`editor.project.undo`** — `tasks/refinements/editor/undo.md` (**Done**). Consumed: `commands::undo`/`commands::redo` (`src/commands/ace/commands/app_state.hpp:147,152`) as the *only* journal-navigation verbs, and `D-undo-1`'s framing that navigation is not a `Command` on `dispatch`. `navigate_to` composes them; it does not replace them.
- **`editor.canvas.single_writer` / `editor.canvas.edit_render_sync`** — the `run_edit`/`apply_edit` funnel (`src/app/project_gateway.cpp:116-126`, `src/app/shell.cpp:283-284`) that this leaf hangs the publish hook on.

**Pending (owned here):** nothing. This leaf is a prerequisite, not a dependent — everything it needs is shipped.

## What this task is

The History panel reads the libarbc journal's **entry vector** directly, three times per frame, on the UI thread (`src/views/views.cpp:187,190,211`). That vector is writer-owned and explicitly *not* published: `Journal::entry_at()` hands out a `const JournalEntry&` into `d_entries` (`arbc/model/journal.hpp:120-125,159`), which a concurrent commit may reallocate. It is safe today for exactly one reason — the UI thread *is* the writer — and `editor.canvas.writer_thread` deletes that reason.

This leaf moves the panel off the live structure and onto a **published immutable snapshot**. `commands::AppState` gains a `HistoryPublisher` holding an `std::atomic<std::shared_ptr<const HistorySnapshot>>`, where `HistorySnapshot` is a plain value: the chronological list of entry names plus the cursor that splits it into applied and redoable. The publisher is **refreshed on the writer thread** at every writer-turn boundary and **loaded atomically from any thread** by the panel. `draw_history` is rewritten to render entirely from one loaded snapshot per frame and to navigate through a new L1 verb, `commands::navigate_to(state, target_cursor)`, so `src/views/views.cpp` stops including `arbc/model/journal.hpp` and `arbc/model/journal_entry.hpp` altogether.

Scope boundary: this leaf does **not** move any write off the UI thread. It removes the *read* hazard so that `editor.canvas.writer_thread` — which does move the writes — does not have to ship a known TSan race alongside its own much larger migration.

## Why it needs to be done

1. **It is the blocking prerequisite for `editor.canvas.writer_thread`.** That leaf's own tripwire (`doc.on_writer_thread()` true inside every posted closure, false on the UI thread) is precisely the condition that makes `entry_at` from `draw_history` a use-after-realloc race. `arbc_v030.md`'s `D-arbc_v030-8` ordered this leaf *before* `writer_thread` rather than after, rejecting `depends !writer_thread` on the grounds that it *"schedules a known-race commit into main"*, and rejecting absorption into `writer_thread` as *"how scope silently grows"*. `tasks/00-editor.tji:289` encodes that ordering.
2. **The library will not fix it.** `arbc/model/journal.hpp:120-125` states the position outright: *"An off-thread history browser would need the entry list published copy-on-write; nothing needs that today (issue #15, explicitly out of scope)."* The host is the right owner. Whether libarbc should eventually publish an entry-name view is a library judgment call already parked by `arbc_v030.md`; nothing here waits on it.
3. **It is what doc 15's rule actually prescribes.** libarbc doc 15's guidance — *publish the few words a frame samples; never publish a structure it would have to walk* — is satisfied by a snapshot the writer builds and the reader loads by pointer, not by a mutex around a walk of writer-owned memory.
4. **The panel's current read is already the last direct L3→journal-internals reach.** Every other UI journal read is one of the four published atomics (`src/app/project_gateway.cpp:132,134` → `src/dock/dock.cpp:337,343`). Closing this one makes "the UI never touches writer-owned document structure" a statable invariant (the A18 delta), not a per-panel accident.

## Inputs / context

**Design docs (normative).**

- `docs/01-architecture.md` **§4 / A4** (`:59-82`) — the thread diagram; the UI thread *submits* edits and never touches the cache.
- **A4.1** (`:84-96`) — *"'Single writer' means every structural write to a `Document` must originate from one stable OS-thread identity… Reads stay lock-free via `pin()` and the copy-on-write content-binding snapshot (arbc#10/#11); only *writes* are identity-bound."* Note what that last clause does **not** cover: the journal entry vector is neither `pin()`-guarded nor copy-on-write, so a journal-inspection read is not one of the lock-free reads A4.1 blesses.
- **A4.1a** (`:98-123`, amended `:125-154`) — the amendment establishes that both v0.2.0-era races are library-fixed and the lease now guards nothing, and that *"Writer identity is unchanged from A4.1."* This leaf adds nothing to the identity story; it removes a read that identity alone cannot make safe.
- **§8 Components & levelization** (`:216-251`) — `commands` is L1 and may depend on `base, project, scene` (+ libarbc); `views` is L3 and may depend on `scene, interact, commands, render, dockmodel, imgui`; `render` is L2 over `base, project, scene, gl` and may **not** see `commands`. Mirrored in `scripts/check_levels.py:24-36` and `CMakeLists.txt:186,192`.
- **§9 Testing & definition of done** (`:253-280`) — the four-layer model this leaf's acceptance criteria instantiate.
- `docs/00-design.md` **D15** (`:482`) — *"Undo = library transactions… Scene edits are transactions (doc 14); continuous gestures coalesce to one step."* Unchanged: this leaf changes how the panel *reads* the journal, never what is journaled.
- `docs/00-design.md` **D18** (`:485`) — History is a relocatable view. Unchanged.

**The hazard, in the library header.** `build/dev/_deps/arbc-src/src/model/arbc/model/journal.hpp`:

- `:49-62` — *"The cursor and the entry count are PUBLISHED (relaxed atomics)… The entry vector itself is not published -- it stays writer-owned, so history INSPECTION (`entry_at`, `byte_cost`) remains writer-thread only."*
- `:106-114` — `can_undo()`, `can_redo()`, `depth()`, `cursor()`, all `std::atomic<std::size_t>` loads, marked **ANY THREAD**.
- `:120-125` — `entry_at`, marked **WRITER-THREAD ONLY**, with the reallocation hazard spelled out.
- `:159-161` — `std::vector<Stored> d_entries;  // writer-owned` beside `d_cursor`/`d_depth` `// published`.
- `arbc/model/journal_entry.hpp:44-50` — `struct JournalEntry`, `std::string name;` at `:45`. `name` is the only field the panel reads.

**The panel.** `src/views/views.cpp:176-231` (`draw_history`):

- `:180-182` — holds `arbc::Journal&` for the frame, reads `depth()` and `cursor()`.
- `:187` — `journal.entry_at(cursor - 1).name.c_str()` for the "Undo <name>" affordance.
- `:190` — `journal.entry_at(cursor).name.c_str()` for "Redo <name>".
- `:202-218` — the base row `"Base###base"` plus one `Selectable` per entry, label `journal.entry_at(i).name + "###entry" + std::to_string(i)`, dimmed for `i >= cursor`, selected at `i + 1 == cursor`.
- `:225-230` — the click-to-jump loop, whose *conditions* re-read `journal.cursor()` each iteration while calling `commands::undo`/`commands::redo`.
- `:7-9` — the `arbc/model/journal.hpp` + `arbc/model/journal_entry.hpp` includes this leaf deletes.
- `src/views/ace/views/views.hpp:118-131` — the header contract that must be truthed-up in the same commit (it currently promises *"reads `depth()/cursor()/entry_at(i).name` fresh each frame… keeps no shadow copy"*).

**The state that will hold the snapshot.** `src/commands/ace/commands/app_state.hpp:36-101`:

- `:40-43` — `AppState(AppState&&) = default;`. **Load-bearing constraint:** `open_or_create_app_state` (`:161-162`) returns `platform::Result<AppState>` by value, so any new member must be movable. `std::atomic<std::shared_ptr<T>>` is not.
- `:45-46` — `document()` is the *only* journal exposure; there is no `AppState::journal()`.
- `:88` — `next_gesture_key()`, the existing precedent for a small piece of session-owned, writer-thread-only state living on `AppState`.
- `:90-100` — the members; today none is atomic and none is published.
- `src/commands/app_state.cpp:33-53` — the constructor (the initial-publish point); `:55-65` `dispatch`; `:67-77` `undo`; `:79-84` `redo`.

**The writer-turn boundaries (where a refresh must happen).** The journal is mutated from three shapes of call site, and **only the first goes through `commands`**:

- `commands::dispatch` (`src/commands/app_state.cpp:55-65`), driven from `src/app/project_gateway.cpp:258` (insert cell), `:270` (delete), `:284`, `:323` (mints) — all wrapped in `run_edit`.
- `commands::undo`/`redo` (`:67-84`), driven from `src/app/project_gateway.cpp:106,112` inside `run_edit`, **and directly from `src/views/views.cpp:226,228`** — the panel's loop bypasses the gateway funnel entirely.
- **Bare `scene::` transactions inside a raw `canvas_.apply_edit` closure**, bypassing both `commands` and the gateway: `src/app/camera_inspector.cpp:92-95` (`scene::set_camera_resolution`) and `:117-121` (`scene::set_camera_resolution_and_frame`), plus the manipulator path at `src/views/canvas_view.cpp:403`. These journal entries — `"set_camera_resolution"` (`src/scene/camera.cpp:569,588`), `"rename_camera"` (`:547`), `"add_cell"` (`src/scene/cell.cpp:310`), `"add_camera"` (`src/scene/camera.cpp:523`) — appear in the History panel but are added by code `commands` never sees.

  The single point every one of them *does* pass through is `CanvasHost::apply_edit` (`src/render/canvas_host.cpp:201-222`, declared `src/render/ace/render/canvas_host.hpp:104`), bound as the gateway's edit runner at `src/app/shell.cpp:283-284`. That is the writer-turn boundary this leaf hooks.

**Tests and lanes.**

- `tests/history_e2e_test.cpp:80-206` — the ImGui Test Engine e2e: real project, real `AppState`, three `dispatch`es, `register_view_body(ViewType::History, …)`, then `ItemExists`/`ItemClick` on `"history/###base"` and `"history/###entry" + N` with assertions on `journal.cursor()`, `journal.depth()`, `pin()->revision()`, `can_undo()`, `can_redo()`. Must pass **unmodified**.
- `tests/undo_test.cpp`, `tests/commands_test.cpp` — the L1 Catch2 pattern to follow (anonymous-namespace `ScratchDir` with a suite-distinct temp dir; a fixture `Command` that produces exactly one journal entry).
- `CMakeLists.txt:186` — `ace_component(commands DEPENDS base project scene)`; sources under `src/commands/` are globbed (`:155`), so new `.cpp` files need no CMake edit. `:219-238` — `ace_tests` sources are listed **explicitly**; a new test file must be added there. `:239-241` — it already links `ace::commands` and `arbc::arbc`.
- `CMakePresets.json:27-33` (`tsan` preset, `-fsanitize=thread`) and `.github/workflows/ci.yml:39-41` (`gcc-tsan` lane). There is **no per-test sanitizer tagging** — every test in `ace_tests` runs on every lane, so a `std::thread` in a Catch2 case is automatically TSan-covered.

## Constraints / requirements

1. **`src/views/views.cpp` must contain no journal read.** After this leaf, `#include <arbc/model/journal.hpp>` and `#include <arbc/model/journal_entry.hpp>` are gone from that file and the identifiers `journal(`, `entry_at`, `depth()`, `cursor()` do not appear in `draw_history`. This is the leaf's falsifiable deliverable — `check_levels` cannot enforce it (`views` is in `EXTERNAL_ALLOWED["arbc"]`, `scripts/check_levels.py:45-46`, and legitimately so: `canvas_view.cpp` uses `arbc::Affine`/`ObjectId`), so it is stated as an acceptance criterion and verified by grep.
2. **The panel renders from ONE snapshot per frame.** Names *and* cursor come from the same loaded `shared_ptr`. Mixing snapshot names with a live `journal.cursor()` re-read is forbidden: a cursor from a later generation can exceed the snapshot's `names.size()` and index out of range at the "Redo <name>" affordance.
3. **The published snapshot is immutable after publication.** `HistorySnapshot` is handed out as `shared_ptr<const>`; refresh *replaces* the pointer, never mutates the pointee. A reader holding a snapshot across an arbitrary number of subsequent commits sees a stable, self-consistent value — this is the property that retires the use-after-realloc race, and it is directly asserted.
4. **Refresh runs on the writer thread only** (it calls `entry_at`), and `load()` is callable from any thread with no lock held by the caller.
5. **`AppState` stays movable.** `AppState(AppState&&) = default` at `src/commands/ace/commands/app_state.hpp:40` must keep compiling and `open_or_create_app_state` must keep returning by value.
6. **Refresh is idempotent and cheap when nothing changed**, so it can be called liberally at every writer-turn exit without a per-call-site cost argument. The stamp is `(depth(), cursor())` — both v0.3.0 atomics.
7. **`render` gains no `commands` dependency.** The post-edit hook is an opaque `std::function<void()>`; `src/render/` gains no new include and `scripts/check_levels.py`'s `ALLOWED["render"]` is untouched.
8. **Navigation semantics are preserved exactly.** `D-history-5`'s bounded, end-stopped, single-step walk through `commands::undo`/`commands::redo` moves into L1 unchanged in behaviour: same clamping at the base and the tip, same "clicking the current head is a zero-step no-op", same "the panel never mutates the journal directly" (`Constraint 1`).
9. **Row ids are unchanged** — `"Base###base"` and `"###entry" + std::to_string(i)`, indexed by position in the snapshot. This is what lets the e2e pass unmodified.
10. **No behavioural change to what is journaled.** D15 is untouched: no new entry, no changed name, no changed coalescing.
11. **Teardown ordering.** The post-edit hook captures `AppState&`; it is installed in the same `src/app/shell.cpp` block as the edit runner (`:283-284`) and carries the identical lifetime argument — the canvas is stopped and joined before `AppState` is destroyed.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate` green (`check_levels` · clang-format · build · ctest) is the umbrella.

**Levelization.** `scripts/check_levels.py` clean. No new component and no new DAG edge: the new files live in the existing L1 `commands` (`CMakeLists.txt:186`, which already declares libarbc); `render` gains a `std::function<void()>` member and **no** include; `views` *loses* two libarbc includes. The L1 core gains no ImGui/GL/SDL include.

**L1 Catch2 units — new file `tests/history_publish_test.cpp`**, added to the `ace_tests` source list (`CMakeLists.txt:238`), following the `tests/undo_test.cpp` pattern with a suite-distinct `ScratchDir` (`/tmp/ace_history_publish_test`):

- `"history publish: a fresh session publishes a non-null empty snapshot"` — after `open_or_create_app_state`, `state.history().load()` is non-null with `names.empty()` and `cursor == 0`. Pins that the panel never sees a null pointer, including on frame 0 before any edit (`src/commands/app_state.cpp:33-53`).
- `"history publish: dispatch appends the entry name and advances the published cursor"` — three named `dispatch`es produce `names == {"edit#0","edit#1","edit#2"}` and `cursor == 3`. Pins the refresh point in `dispatch`.
- `"history publish: undo and redo move the published cursor and leave the names"` — after `undo`, `cursor == 2` and `names` unchanged; after `redo`, `cursor == 3`. Pins the refresh point in the navigation verbs and the applied/redoable split the panel dims on.
- `"history publish: a commit after an undo republishes the truncated name list"` — undo twice, then `dispatch("edit#3")` → `names.size() == 3`, `names.back() == "edit#3"`, `cursor == 3`. Pins that the snapshot tracks journal *trimming*, not just growth.
- `"history publish: a held snapshot is immutable across later commits"` — hold the `shared_ptr` from before three further `dispatch`es; its `names` and `cursor` are byte-identical afterwards, and `use_count() >= 2` while the publisher has moved on. **This is the assertion that retires the `entry_at` reallocation hazard** (Constraint 3).
- `"history publish: refresh is stamp-guarded and republishes nothing on an unchanged journal"` — `publish_history` twice in a row, and a `dispatch` of a `Command` with no `apply` (which adds no entry, `src/commands/app_state.cpp:58-60`), both leave `load()` returning the **same pointer**. Pins Constraint 6, and is what makes the belt-and-braces double refresh (verb + hook) free.
- `"history navigate_to: walks the cursor to an arbitrary target in both directions"` — from `cursor == depth`, `navigate_to(state, 1)` reports `steps == depth - 1` and `cursor == 1`; `navigate_to(state, depth)` walks back. Pins `D-history-5`'s loop, now in L1.
- `"history navigate_to: clamps out-of-range targets and end-stops"` — `navigate_to(state, depth + 10)` lands on `depth`; `navigate_to(state, 0)` lands on `0` with `can_undo() == false`. Pins the defensive end-stop.
- `"history navigate_to: targeting the current cursor is a zero-step no-op"` — `steps == 0` and `document().pin()->revision()` unchanged. Pins the "clicking the head does nothing" behaviour the e2e asserts at `tests/history_e2e_test.cpp:164-168`.
- `"history publish: a bare scene transaction refreshes through the writer-turn hook"` — a `CanvasHost`-free unit analogue: install `publish_history` as the post-edit hook on a `CanvasHost` fixture (mirroring `tests/canvas_host_test.cpp`), run `apply_edit` with a closure calling `scene::set_camera_resolution` directly, and assert the published `names` grew. Pins the gap that `commands`-level refresh alone cannot cover (`src/app/camera_inspector.cpp:92-95`).

**Threading (TSan).** `"history publish: a spawned reader walks published snapshots while the writer commits"` — same file, so it runs on every lane including `gcc-tsan` (`.github/workflows/ci.yml:39-41`; there is no per-test tagging). A `std::thread` loops `state.history().load()` and walks every name and the cursor for a bounded iteration count while the main thread runs `dispatch`/`undo`/`redo`; each loaded snapshot must satisfy `cursor <= names.size()` and every name must be non-empty. Clean under both the `tsan` and `asan` presets (`CMakePresets.json:18-33`). The inverted control: the *pre-change* code cannot be written this way at all — a reader thread calling `entry_at` is the race, which is the point.

**UI e2e.** `tests/history_e2e_test.cpp` passes **unmodified** — no new e2e file, justified: the panel's user-visible behaviour is unchanged by design, and the existing e2e is a sharp detector of the one way this leaf can fail silently. If the publisher is never refreshed, the panel renders an empty list and `ItemExists("history/###entry N")` (`:145-146`) fails on the first assertion; if the cursor is published wrongly, the head/base assertions at `:151-176` fail. A modified e2e would forfeit exactly that signal.

**Rendered output (goldens).** N/A, justified: no pixel path is touched. The six existing goldens must stay byte-identical, which `scripts/gate` already checks.

**Regression.** `tests/undo_test.cpp`, `tests/commands_test.cpp`, `tests/canvas_host_test.cpp`, `tests/canvas_view_e2e_test.cpp` and `tests/multi_canvas_e2e_test.cpp` pass unmodified. Churn in any of them is a failure signal, not an update.

**Contract truth-up (same commit).** `src/views/ace/views/views.hpp:118-131` no longer claims the panel *"reads `depth()/cursor()/entry_at(i).name` fresh each frame"* or *"keeps no shadow copy"*; `src/views/views.cpp:177-179`'s `D-history-6` comment is replaced by the published-snapshot rationale with a pointer to `D-history_published_reads-1`.

**Grep invariant.** `grep -n 'arbc/model/journal\|entry_at' src/views/views.cpp` returns nothing (Constraint 1).

**Coverage.** ≥90% diff coverage on changed lines (CI `coverage` job). Every new branch — empty journal, stamp-hit, stamp-miss, clamp-high, clamp-low, zero-step — has a named case above.

**Doc delta (same commit).** `docs/01-architecture.md` gains row **A18** at `:340` (see `D-history_published_reads-5`).

**Deferred WBS work.** None. Everything this leaf surfaces is either done here or already owned by `editor.canvas.writer_thread`.

## Decisions

- **D-history_published_reads-1 — The panel keeps a shadow copy now, reversing `D-history-6`, because the premise of that decision no longer holds.**
  `D-history-6` chose "no shadow copy, read fresh each frame" and it was right: a shadow copy of a structure the reader can read directly is pure staleness risk with no benefit. The premise was that the journal *is* directly readable. It is not — `entry_at` is writer-thread-only by construction (`arbc/model/journal.hpp:120-125`) and the UI thread only got away with it by being the writer. Once `editor.canvas.writer_thread` lands, "read fresh each frame" means "race a reallocating `push_back` every frame".
  *Rationale:* (i) the copy is not a *cache* the panel maintains — it is a **publication** the writer performs, which is the only shape doc 15 permits for cross-thread structure; (ii) staleness is bounded to at most the interval between a commit and the writer's own epilogue, which today is zero (the refresh runs in the same synchronous turn) and after `writer_thread` is one queue turn; (iii) the snapshot is `const` and shared by pointer, so the copy cost is one `vector<string>` build per *edit*, not per frame — strictly cheaper than the current three `entry_at` calls plus a string concatenation per row per frame.
  *Alternative rejected — keep the direct reads and put a mutex around the journal.* This is precisely what libarbc doc 15 § Thread rules forbids for the writer path and what A4.1 calls out: *"a consumer mutex only re-covers the accesses it wraps."* It would also require every commit site in `scene`/`project` to take the same lock, which is the two-identity anti-pattern `writer_thread` exists to remove.
  *Alternative rejected — have the panel call `entry_at` through the edit runner (a synchronous round-trip to the writer per frame).* It works, but it makes drawing a panel block on the writer queue every frame, coupling frame rate to edit latency — the exact inversion A4's *"the UI thread stays responsive because rendering is never on it"* is protecting.

- **D-history_published_reads-2 — The snapshot carries the cursor, not just the names; the panel takes its entire model from one loaded pointer.**
  The `.tji` note's literal shape is `shared_ptr<const std::vector<std::string>>`. This leaf publishes `shared_ptr<const HistorySnapshot>` where `HistorySnapshot { std::vector<std::string> names; std::size_t cursor; }`, and `depth` is simply `names.size()` (no third field).
  *Rationale:* (i) **correctness, not convenience** — the panel uses `cursor` to index `names` at `views.cpp:190` ("Redo <name>" reads index `cursor`); a `cursor` loaded from the live atomic after a snapshot from an earlier generation can be `> names.size()`, which is an out-of-bounds read introduced by the very change meant to remove one; (ii) it makes the applied/dimmed split (`views.cpp:206-210`) internally consistent — no frame can ever render a highlight position that disagrees with the list it is highlighting; (iii) `cursor` is `std::size_t`, so bundling it costs nothing.
  *Alternative rejected — publish names only, read `cursor()` live.* Tempting, because `cursor()` is a genuine any-thread atomic after v0.3.0 and "fresher is better" is the usual instinct. But freshness of one half of a two-part model is not an improvement; it is a tearing window. Clamping the live cursor to `names.size()` would paper over it while silently mis-rendering the head row.

- **D-history_published_reads-3 — The refresh hangs on the *writer-turn boundary* (a post-edit hook on `CanvasHost`), not only on the `commands` verbs.**
  `commands::dispatch`, `commands::undo`, `commands::redo` and `commands::navigate_to` each refresh before returning, **and** `CanvasHost::apply_edit` gains `set_post_edit_hook(std::function<void()>)`, invoked inside the writer-priority lease immediately after `edit()` (`src/render/canvas_host.cpp:212-214`). `src/app/shell.cpp` installs it as `[&app_state] { ace::commands::publish_history(app_state); }` in the same block as the edit runner (`:283-284`).
  *Rationale:* (i) **the `commands` verbs do not see every commit** — `src/app/camera_inspector.cpp:92-95,117-121` and `src/views/canvas_view.cpp:403` call `scene::` transaction helpers directly inside a raw `canvas_.apply_edit` closure, producing journal entries (`"set_camera_resolution"`, `"rename_camera"`) the History panel displays but `commands` never observes; verb-only refresh would leave the panel stale after every camera-inspector edit — a visible bug, not a theoretical one; (ii) `apply_edit` is the one seam **all three** shapes pass through, so the hook is *structural* coverage rather than call-site discipline a future edit site can forget; (iii) the verbs refresh too because the runner is optional (`src/app/project_gateway.cpp:116-126` runs the closure bare when unset, which is the headless-test path), and because it keeps `commands` self-consistent under Catch2 with no L4 present; (iv) double refresh is free by Constraint 6 — the second call hits the stamp and returns without rebuilding; (v) the hook is the exact shape `editor.canvas.writer_thread` needs: its per-closure epilogue on the writer thread, which is where this one-line responsibility migrates when `apply_edit` is retired.
  *Alternative rejected — refresh at each of the three `apply_edit` call sites instead of in `apply_edit`.* Fewer moving parts today, but it makes panel freshness a convention every future edit site must remember, with a silent-staleness failure mode. The hook makes it impossible to get wrong.
  *Alternative rejected — give `render` a dependency on `commands` and call `publish_history` directly.* Illegal: `ALLOWED["render"] = {base, project, scene, gl}` (`scripts/check_levels.py:33`, §8). The opaque `std::function<void()>` keeps the DAG intact, which is why the hook is a callback and not a typed call.

- **D-history_published_reads-4 — Click-to-jump moves into an L1 `commands::navigate_to` verb; the panel's loop goes away.**
  New: `struct NavigateOutcome { std::size_t steps; std::size_t cursor; std::uint64_t revision; bool can_undo; bool can_redo; };` and `NavigateOutcome navigate_to(AppState& state, std::size_t target_cursor);` declared beside `undo`/`redo` (`src/commands/ace/commands/app_state.hpp:147-152`). It clamps `target_cursor` to `[0, depth()]`, runs the same bounded, end-stopped single-step loop that `views.cpp:225-230` runs today over `commands::undo`/`commands::redo`, and refreshes the publisher once at the end. `draw_history` becomes `if (target) { commands::navigate_to(state, *target); }`.
  *Rationale:* (i) **it is required by Constraint 1** — the loop's conditions read `journal.cursor()` (`views.cpp:226,228`), so leaving the loop in `views` leaves a journal read in `views`; (ii) the loop is exactly the branchy, end-stop-sensitive logic §9 wants in a headless Catch2 test rather than behind an ImGui click; (iii) it collapses N cross-thread verb calls into **one** writer-thread unit of work, which is what `editor.canvas.writer_thread` must post as a single closure — doing it here means that leaf rebinds a call it already has instead of inventing a batching seam under time pressure; (iv) it refreshes once at the end rather than once per step, so a jump across 50 entries publishes one snapshot instead of 50.
  *Alternative rejected — add a `cursor` field to `UndoOutcome` and keep the loop in `views`.* Smaller diff, satisfies Constraint 1 literally. Rejected because it leaves navigation policy in L3 where it cannot be unit-tested, and it hands `writer_thread` a loop of N submissions to fix later — moving a cost rather than paying it.
  *Alternative rejected — a multi-step `journal().navigate_to()` upstream.* Not available; the library exposes single-step nav only (`arbc/model/journal.hpp:91-92`), as `D-history-5` already recorded. Asking for one is a library change on this leaf's critical path, and `arbc_v030` just closed a pin cycle.

- **D-history_published_reads-5 — This gets a constitution row (`A18`), unlike the v0.3.0 pin.**
  `arbc_v030`'s `D-arbc_v030-6` refused a new `A` row on the grounds that *"a constitution row born one leaf from obsolescence is noise."* That reasoning does not transfer: the pin's claims expire at the next bump, whereas "the UI reads writer-owned document structure only through a snapshot the writer publishes" is the standing rule that `editor.canvas.writer_thread` depends on and that **every future panel** touching writer-owned libarbc state must follow. Without a row it is a fact buried in one refinement, rediscovered by whoever writes the next inspection panel.
  *Doc delta:* one row appended at `docs/01-architecture.md:340`, immediately after `A17` (`:339`) and before the blank line preceding `## Open / next` (`:341`) —
  **A18** | *The UI thread reads writer-owned `Document` structure only through a published, immutable snapshot built on the writer thread — never through libarbc's writer-thread-only inspection APIs.* Covers: the `entry_at` carve-out (`arbc/model/journal.hpp:120-125`, arbc#15 out-of-scope) that A4.1's *"reads stay lock-free via `pin()` and the copy-on-write content-binding snapshot"* does **not** reach; the shape (`commands::HistoryPublisher`, an `std::atomic<std::shared_ptr<const HistorySnapshot>>` on the one `AppState`, rebuilt only when the published `(depth(), cursor())` stamp moves); and the refresh point (the writer-turn epilogue — `CanvasHost::apply_edit`'s post-edit hook today, `editor.canvas.writer_thread`'s per-closure epilogue after). No new component, no new DAG edge (`render` takes an opaque `std::function<void()>`), no new external dependency. *Alternative rejected: a consumer mutex around the journal — A4.1's "a consumer mutex only re-covers the accesses it wraps."* Realized by `editor.canvas.history_published_reads`; extended by `editor.canvas.writer_thread`.
  No amendment to A4.1 or A4.1a: neither says anything false. A18 fills the gap A4.1's last clause leaves open rather than correcting it, and cites it as such.

- **D-history_published_reads-6 — The publisher is held through a `std::unique_ptr` so `AppState` stays movable, and the atomic is `std::atomic<std::shared_ptr<const HistorySnapshot>>`.**
  `std::atomic<std::shared_ptr<T>>` is neither copyable nor movable, so a bare member would delete `AppState(AppState&&) = default` (`src/commands/ace/commands/app_state.hpp:40`) and break `open_or_create_app_state`'s return-by-value (`:161-162`) — a compile error at the factory, not a subtle one, but a blocking one. The member is `std::unique_ptr<HistoryPublisher> history_;`, constructed in the `AppState` ctor beside the registry seeding (`src/commands/app_state.cpp:33-53`) and never null, matching the existing `std::unique_ptr<arbc::Document> document_` (`:91`). Accessors `HistoryPublisher& history()` / `const HistoryPublisher& history() const`.
  *Rationale:* (i) it is the minimum change that preserves the move; (ii) it mirrors the member the class already has, so no new ownership idiom enters `commands`; (iii) the extra indirection is once per frame on a pointer load.
  *Alternative rejected — hand-written move operations that load/store the atomic.* More code, and a moved-from `AppState` with a live publisher is a worse state than a moved-from one with a null `unique_ptr` (which every other member already implies).
  *Alternative rejected — `std::shared_ptr<HistoryPublisher>` so readers can hold it independently.* No reader needs to outlive `AppState`: `draw_history` takes `AppState&` (`src/views/ace/views/views.hpp:131`) and the shell clears the body before teardown (`src/app/shell.cpp:320-322`). Shared ownership with no second owner is decoration.
  *Toolchain note:* the C++20 `std::atomic<std::shared_ptr<T>>` specialization is available in libstdc++ 12+ (both CI lanes, `.github/workflows/ci.yml:30-41`, use libstdc++). If a target lacks it, the behaviour-identical fallback is a `std::mutex`-guarded holder inside `HistoryPublisher` — the class's public API (`load()` / `refresh()`) is deliberately narrow enough that the substitution is invisible to every call site.

- **D-history_published_reads-7 — Refresh is guarded by a `(depth(), cursor())` stamp, and that is an optimization with a test, not a correctness dependency.**
  `refresh` loads the two v0.3.0 atomics, compares them to the published snapshot's `(names.size(), cursor)`, and returns without rebuilding when they match.
  *Rationale:* (i) it is what makes `D-history_published_reads-3`'s belt-and-braces double refresh free, so neither the verb nor the hook needs to know about the other; (ii) it keeps a jump across a long journal from rebuilding per step; (iii) **correctness never rests on it** — the stamp only decides whether to *skip* work, and the pinned case (`"refresh is stamp-guarded…"`) asserts pointer identity so a regression to unconditional rebuild is caught as a behaviour change rather than a silent cost. The one case where the stamp could theoretically miss a change — a coalesced commit folding into the existing entry (`AppState::next_gesture_key`, `src/commands/ace/commands/app_state.hpp:78-88`) — leaves depth, cursor *and* the entry's `name` all unchanged, so there is nothing to republish.
  *Alternative rejected — a monotonic publish counter maintained by the editor.* Another piece of state to keep in sync with a journal that already publishes two atomics for exactly this purpose. `arbc_v030` paid for those atomics; using them is the point.
  *Alternative rejected — rebuild unconditionally on every writer turn.* Simpler, and honestly acceptable at today's journal depths. Rejected because the hook fires on *every* edit including every frame of a coalescing drag (`src/views/canvas_view.cpp:403`), where an O(depth) string-vector rebuild per frame is a real cost introduced silently by a leaf that is supposed to make the panel cheaper.

## Open questions

(none — all decided.)

## Status

**Done** — 2026-07-23.

- `src/commands/ace/commands/history.hpp` + `src/commands/history.cpp` — new `HistorySnapshot` value type and `HistoryPublisher` (`std::atomic<std::shared_ptr<const HistorySnapshot>>` behind a `std::unique_ptr`) with stamp-guarded `refresh()` and any-thread `load()`.
- `src/commands/ace/commands/app_state.hpp` + `src/commands/app_state.cpp` — `AppState` gains `history_` (`unique_ptr<HistoryPublisher>`) initialized in the constructor, refreshed in `dispatch`, `undo`, `redo`, and the new `navigate_to` verb; `AppState(AppState&&) = default` preserved.
- `src/render/ace/render/canvas_host.hpp` + `src/render/canvas_host.cpp` — `CanvasHost::set_post_edit_hook(std::function<void()>)` added; hook invoked inside the writer-priority lease after every `apply_edit`, keeping the `render` layer free of any `commands` include.
- `src/app/ace/app/canvas_view.hpp` + `src/app/canvas_view.cpp` — `set_post_edit_hook` forwarded through `CanvasView`.
- `src/app/shell.cpp` — post-edit hook installed as `[&app_state]{ ace::commands::publish_history(app_state); }` beside the existing edit runner.
- `src/views/ace/views/views.hpp` + `src/views/views.cpp` — `draw_history` rewritten to render from one loaded `shared_ptr<const HistorySnapshot>` per frame; `arbc/model/journal.hpp` and `arbc/model/journal_entry.hpp` includes removed; click-to-jump loop replaced by `commands::navigate_to`; contract comment updated.
- `CMakeLists.txt` — `tests/history_publish_test.cpp` added to `ace_tests` source list.
- `docs/01-architecture.md` — A18 row added: *"The UI thread reads writer-owned Document structure only through a published, immutable snapshot built on the writer thread."*
- `tests/history_publish_test.cpp` — 11 Catch2 L1 units covering publish/refresh, stamp-guard, navigate_to clamping, immutability, and a TSan-lane concurrent-read case.
