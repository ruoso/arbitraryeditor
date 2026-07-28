# editor.canvas.history_snapshot_adopt — Read the library's published history snapshot; retire the host-side mirror

## TaskJuggler entry

- **Task:** `editor.canvas.history_snapshot_adopt` — *"Read the library's published history snapshot; retire the host-side mirror"*
- **Definition:** `tasks/00-editor.tji:368-373` (`task history_snapshot_adopt` inside `editor.canvas`).
- **Effort:** `1d`. **Allocate:** `team`. **Depends:** `!arbc_v040`.
- **Note (verbatim):** ruoso/arbitrarycomposer#24 shipped `Journal::history()` in v0.4.0: an any-thread pinned immutable snapshot (the `Model::current()` shape) carrying exactly the projection a panel draws — entry name plus per-entry byte_cost — with unchanged rows shared by pointer. That is the library-side answer to the ask `editor.canvas.history_published_reads` answered host-side under A18. Retire `commands::HistoryPublisher` and every writer-thread refresh call site and have the History panel read `journal().history()` directly. Two properties must not regress; `history_e2e_test.cpp` must pass unmodified; `history_publish_test.cpp` is retargeted or retires with the publisher. Per-entry byte cost becomes available but DISPLAYING it is `editor.panels.history`'s call.
- **Source of debt:** `tasks/parking-lot.md` — *"arbc::Journal entry_at()/byte_cost() — upstream any-thread publication"* (triage 2026-07-28).
- **Design:** `docs/01-architecture.md` A18, A4.1.
- **Predecessors:** `editor.canvas.history_published_reads` (built the host mirror this leaf retires), `editor.canvas.arbc_v040` (the pin that shipped `Journal::history()`).
- **Milestone:** the `editor.canvas` / v0.4.0-adoption cluster (same milestone as `arbc_v040`). No new WBS leaf is spawned by this refinement.

## Effort estimate

`1d`. This is a **net deletion** with one small L1 accessor and a test retarget: two files (`history.hpp`, `history.cpp`) go away, one publisher member and its four refresh sites go away, one L4 post-edit hook goes away, and the panel's read path changes from `state.history().load()` (host snapshot) to `state.history()` (library snapshot via a thin accessor). The only genuinely new logic is a bounds-clamp helper (a handful of lines, unit-tested). The estimate is dominated by (a) re-reading the retargeted `history_publish_test.cpp` cases against the library snapshot and (b) confirming `history_e2e_test.cpp` passes byte-for-byte unmodified.

## Inherited dependencies

### Settled (consumed as-is)

- **`editor.canvas.arbc_v040`** — bumped `ARBC_GIT_TAG` to v0.4.0, which ships `Journal::history()` (arbc#24). The pin leaf **consumes none** of the new surface (`D-arbc_v040-1`); it only pins existence/shape via a compile-time witness in `tests/arbc_pin_test.cpp:320` (`std::is_same_v<decltype(&arbc::Journal::history), …>`). This leaf is the named downstream consumer of that surface (`tasks/refinements/canvas/arbc_v040.md:150-151`).
- **`editor.canvas.history_published_reads`** (Done 2026-07-23) — built the host-side `HistoryPublisher`/`HistorySnapshot` mirror and added constitution row **A18**. Its decisions are the baseline this leaf supersedes:
  - **`D-history_published_reads-1`** — the panel keeps a published shadow, reversing `D-history-6`; the premise was *"`entry_at` is writer-thread-only, so the journal is not directly readable off the writer thread."* Still true, and it is exactly why the **library** now publishes the projection.
  - **`D-history_published_reads-2`** — the host snapshot bundles the **cursor** with the names *"because `views.cpp` indexes `names` at `cursor` for the Redo affordance; a live cursor from a later generation can exceed `names.size()` → out-of-bounds."* This is the correctness constraint this leaf must re-satisfy against a library snapshot that does **not** bundle the cursor (see Decision D-history_snapshot_adopt-3).
  - **`D-history_published_reads-3`** — refresh hangs on the writer-turn epilogue via a `CanvasView` post-edit hook, so `scene::` edits that bypass the `commands` verbs (camera inspector, manipulators) still refresh the panel. This leaf retires the hook (Decision D-history_snapshot_adopt-4).
  - **`D-history_published_reads-4`** — click-to-jump is the L1 verb `commands::navigate_to` returning `NavigateOutcome`. **Preserved** by this leaf (Decision D-history_snapshot_adopt-5).

### Pending (owned here)

None. `Journal::history()` is present in the pinned library; no further pin or upstream change is required. `byte_cost` display is already the separately-registered leaf `editor.panels.history` and is **not** owned here.

## What this task is

The predecessor built a **host-side** mirror of the journal's history projection because libarbc v0.3.0 published only the cursor/depth atoms and kept `entry_at()` writer-thread-only. v0.4.0 (arbc#24) closes that gap library-side: `Journal::history()` returns an any-thread immutable snapshot of the exact projection a panel draws — one `HistoryRow{name, byte_cost}` per stored entry, rows shared by pointer so a commit copies N pointers, not N strings. This leaf **retires the host mirror in favour of the library's own publication**: delete `commands::HistoryPublisher`, `commands::HistorySnapshot`, the `publish_history` free function and its four writer-turn refresh call sites, and the L4 `CanvasView` post-edit hook that bound it; the History panel instead reads `journal().history()` through a thin L1 `commands` accessor. The `commands::navigate_to` verb and the never-`entry_at()` invariant are preserved unchanged; only the publisher and its plumbing go away.

## Why it needs to be done

There are now **two publishers of the same fact** — the host `HistoryPublisher` and the library's `Journal::history()` — and keeping both is the debt the parking-lot triage flagged. The library publisher is strictly better on every axis the host one was measured against: it republishes on **every** mutation regardless of which code path produced the commit (so the `D-history_published_reads-3` hook, which existed only to catch `scene::` edits that bypass the `commands` verbs, is redundant), it shares unchanged rows by pointer (so an undo republishes without allocating a row), and it carries **per-entry `byte_cost`** — a projection the host mirror never built, and the one `editor.panels.history` will need to show memory pressure per entry. Retiring the mirror removes the host publisher's atomic, its `unique_ptr` movability workaround (`D-history_published_reads-6`), its stamp guard (`D-history_published_reads-7`), the writer-turn hook, and one `std::function` member on `CanvasView` — code that only existed because the library had no answer, and now does.

## Inputs / context

**Governing design (constitution — normative):**
- `docs/01-architecture.md` **A18** (`:433`) — *"The UI thread reads writer-owned `Document` structure only through a published, immutable snapshot … never through libarbc's writer-thread-only inspection APIs."* This leaf **amends A18** (doc delta below): the rule stands, but its publisher moves from host to library.
- `docs/01-architecture.md` **A4.1 / A4.1b** (`:84-96`, `:189-225`) — single writer identity; the writer-thread-only posting inventory. `journal().history()`, `cursor()`, `can_undo()`, `can_redo()` are all **any-thread** published reads (they are *not* in the writer-thread-only list), so the UI-thread panel may call them off the writer thread. `entry_at()` remains writer-thread-only and is the read this leaf must keep out of `views`.
- `docs/01-architecture.md` **§8** (`:308-344`) — levelization. `commands` is L1 (UI-agnostic core); `views` is L3 (the ImGui layer). `views → commands` is legal; `commands` may include `arbc/` (`scripts/check_levels.py:49-50` lists `commands` under `EXTERNAL_ALLOWED["arbc"]`). `views` is likewise arbc-allowed, but A18's grep invariant deliberately keeps `arbc/model/journal.*` out of `views.cpp`.
- `docs/01-architecture.md` **§9** (`:346-373`) — the layered DoD: L1 logic → Catch2 headless; threading → ASan/TSan; UI → ImGui Test Engine e2e; `check_levels` clean.

**Library surface (pinned v0.4.0, consumed by this leaf):**
- `arbc/model/journal.hpp:79-90` — `struct HistoryRow { std::string name; std::size_t byte_cost{0}; };` and `using HistoryView = std::vector<std::shared_ptr<const HistoryRow>>;` (rows oldest-first, shared by pointer).
- `arbc/model/journal.hpp:161-174` — `std::shared_ptr<const HistoryView> history() const noexcept` (ANY THREAD; loads `d_history` with acquire).
- `arbc/model/journal.hpp:145-159` — `cursor()` (ANY THREAD; applied-entry count in `[0, depth()]`), `can_undo()`, `can_redo()`, `byte_cost()` (journal-total, distinct from per-row). **The cursor is published SEPARATELY from the history projection** — `HistoryView` carries no cursor.
- `arbc/model/journal.hpp:190-211` — publish ordering: `publish_history()` swaps the projection in **before** the `(cursor, depth)` word is stored, so a reader that sees a depth sees a history at least that new. (Bounds *how often* a two-load tear is observable; does not by itself bound the index — see D-3.)
- `tests/arbc_pin_test.cpp:320` — the existing compile-time witness that `arbc::Journal::history` exists with the pinned shape.

**Editor call sites to change (host mirror — the deletion surface):**
- `src/commands/ace/commands/history.hpp` (79 lines) — `HistorySnapshot { std::vector<std::string> names; std::size_t cursor; }` (`:27-30`) and `HistoryPublisher` (`atomic<shared_ptr<const HistorySnapshot>>`, `load()`/`refresh()`). **Retires whole.**
- `src/commands/history.cpp` (48 lines) — `HistoryPublisher::refresh` rebuilds `names` via `journal.entry_at(i).name` (`:38` — the one writer-thread-only journal read in the host mirror). **Retires whole.**
- `src/commands/ace/commands/app_state.hpp` — `#include <ace/commands/history.hpp>` (`:3`); the `history()` accessor + doc comment (`:117-123`); the `std::unique_ptr<HistoryPublisher> history_` member (`:202-207`); the `publish_history` declaration (`:287-293`). `document()` is at `:86-87`. The `NavigateOutcome`/`navigate_to` seam (`:261-285`) is **kept**.
- `src/commands/app_state.cpp` — `history_->refresh(...)` in the ctor (`:89`); `publish_history` definition (`:92`); its calls in `dispatch` (`:107`), `undo` (`:120`), `redo` (`:126`), `navigate_to` (`:151`). All retire; the verbs otherwise unchanged.
- `src/app/shell.cpp:431` — `canvas.set_post_edit_hook([&app_state]{ ace::commands::publish_history(app_state); });` (the sole binding). Retires.
- `src/app/canvas_view.cpp:598-599,610-611` and `src/app/ace/app/canvas_view.hpp:107,281` — the `set_post_edit_hook` setter, the `post_edit_hook_` member, and the invocation. Dead once the binding is gone; retire (Decision D-4).
- `src/views/views.cpp` — `draw_history` (`:243-310`): reads `state.history().load()` → `history->names[i]` / `history->cursor` (`:260-262,267,270,292`); mutates only via `commands::navigate_to` (`:308`). Repointed to the library snapshot.
- Comment truth-ups: `src/views/ace/views/views.hpp:135`, `src/app/ace/app/canvas_view.hpp:101-102`.
- `CMakeLists.txt` — drop `src/commands/history.cpp` from the `ace_commands` sources; the `history_publish_test` target (added at `CMakeLists.txt:238`) stays but is retargeted.

**Tests:**
- `tests/history_publish_test.cpp` (446 lines, 10+ Catch2 units over the host publisher) — retargeted (Decision D-6).
- `tests/history_e2e_test.cpp` (210 lines, ImGui Test Engine, drives `history/###entryN`/`###base`, asserts journal cursor/revision) — **must pass unmodified**.
- `tests/arbc_pin_test.cpp` — the `Journal::history` existence witness stays as-is.

## Constraints / requirements

1. **The host mirror is fully retired.** `HistoryPublisher`, `HistorySnapshot`, `publish_history`, the `history_` member, the ctor refresh, and all four writer-turn refresh calls are deleted; `history.hpp` and `history.cpp` are removed and dropped from `CMakeLists.txt`. `grep -rn 'HistoryPublisher\|publish_history\|HistorySnapshot' src/` returns nothing after the change.
2. **The panel reads the library snapshot through L1 `commands`, not through a journal include in `views`.** A new `commands::AppState::history()` (any-thread, `const`) returns the panel's whole model derived from `journal().history()` + `journal().cursor()`. `views.cpp` calls that accessor and includes **no** `arbc/model/journal.*` header and names no `entry_at`. The A18 grep invariant is preserved: `grep -n 'arbc/model/journal\|entry_at' src/views/views.cpp` returns nothing.
3. **Every panel index is provably in-bounds.** The library snapshot carries no cursor, so the accessor clamps `journal().cursor()` into `[0, rows->size()]` **against the same `HistoryView` instance the caller will index**, and the applied/redoable split and the undo/redo affordance labels are derived from that clamped cursor against the snapshot's own size — never from `journal().can_undo()/can_redo()` (which are published from a possibly-different generation). This re-satisfies `D-history_published_reads-2`'s no-out-of-bounds guarantee without re-bundling the cursor on the writer thread.
4. **The `navigate_to` verb is preserved.** `commands::navigate_to(state, target)` keeps its `NavigateOutcome` contract and its clamped, end-stopped single-step walk (`D-history_published_reads-4`); only its now-redundant trailing `publish_history` call is removed (the library republishes per commit). `dispatch`/`undo`/`redo` likewise drop their publish calls and are otherwise unchanged.
5. **No journal read regresses into a race.** `views.cpp` reads only any-thread published surface (via the accessor); `entry_at()` stays writer-thread-only and uncalled off the writer thread. The concurrent-reader case (Constraint / test below) is the runtime backstop under TSan.
6. **Levelization stays clean.** No new component, no new DAG edge. `commands` already carries the `arbc` external edge (`check_levels.py:49`); adding `journal().history()` reads to it introduces no ImGui/GL/SDL include into L1. `views` **loses** a `commands::HistorySnapshot` name (and any transitive journal reach) rather than gaining one. `render`/`app` only lose the retired hook. `scripts/check_levels.py` is unmodified and stays green.
7. **Contract comments are trued-up in the same commit** — `views.hpp:135` and `canvas_view.hpp:101-102` no longer describe the retired publisher/hook; A18 is amended (doc delta).

## Acceptance criteria

`scripts/gate` green (`check_levels` · clang-format · build · ctest) is the umbrella. Specifically:

- **Levelization** — `scripts/check_levels.py` clean and **unmodified**; L1 `commands` gains no ImGui/GL/SDL include; `views.cpp` grep-invariant (`arbc/model/journal\|entry_at`) empty (Constraints 2, 6).
- **L1 Catch2 (`tests/history_publish_test.cpp`, retargeted, headless)** — the editor-behavior cases survive, rewritten to read `state.history()`:
  - a fresh session yields a non-null empty snapshot (rows empty, cursor 0);
  - `dispatch` appends the entry name and advances the clamped cursor; the published rows agree with `journal().cursor()`/`depth()`;
  - `undo`/`redo` move the cursor and leave the rows;
  - a commit after an undo republishes the truncated row list;
  - **a bare `scene::set_camera_resolution` transaction run through `CanvasView::apply_edit` (no `commands` verb) shows up in `state.history()`** — the case that proves the retirement is sound: the panel sees journal-published entries on **every** path with the post-edit hook gone;
  - `navigate_to` walks the cursor both directions, clamps out-of-range targets and end-stops, and is a zero-step no-op at the current cursor (the `NavigateOutcome` cases, unchanged in intent);
  - **a `commands::clamp_history_cursor(rows, raw_cursor)` unit** fed hand-built `(HistoryView, cursor)` pairs including `cursor > rows.size()` asserts the result lands in `[0, rows.size()]` and that the derived undo/redo affordances index in-bounds (Constraint 3 — the correctness-critical branch, exercised with synthetic out-of-bounds inputs a single-threaded test cannot otherwise reach).
- **Threading (ASan + TSan)** — the retargeted concurrent-reader case: a spawned reader loops `state.history()` (rows + clamped cursor), walking every row and the split, while the main thread runs `dispatch`/`undo`/`redo`; every observation satisfies `cursor <= rows->size()` and every name is non-empty. Clean under the `tsan` and `asan` presets. This is the editor's proof that the two-independent-atomic-loads-plus-clamp read is data-race-free and never out-of-bounds (Constraints 3, 5).
- **UI e2e (`tests/history_e2e_test.cpp`)** — passes **unmodified**. The panel renders the same rows with the same widget ids (`history/###entryN`, `history/###base`) and `navigate_to` is unchanged, so the byte-identical e2e is the silent-failure detector: if the accessor ever returned empty or mis-clamped, `ItemExists("history/###entry N")` or the cursor assertions fail (justified — no new e2e file).
- **Goldens** — N/A, justified: no pixel/composition path is touched (the History panel is text/rows, not a rendered surface).
- **Regression suites** — `undo_test`, `commands_test`, `canvas_host_test`, `canvas_view_e2e_test`, `multi_canvas_e2e_test` pass unmodified.
- **Coverage** — ≥90% diff coverage on the changed lines; tests ship in this commit.
- **Doc delta** — the A18 amendment (below) lands in the same commit.
- **No deferred WBS work.** `byte_cost` display is the already-registered `editor.panels.history`; this leaf makes the field available and stops there.

## Decisions

**D-history_snapshot_adopt-1 — Retire the host `HistoryPublisher` and read `journal().history()`; do not keep both publishers.**
The library snapshot dominates the host mirror on every axis A18 measured (any-thread, immutable, pointer-shared, path-independent, and it carries `byte_cost`), and the parking-lot triage explicitly decided *"retire the mirror rather than keep two publishers of the same fact."* *Rationale:* (i) the host publisher existed only because v0.3.0 published no entry view — `D-history_published_reads-1`'s stated premise — and v0.4.0 removes that premise; (ii) keeping both means two code paths for one fact that can silently diverge; (iii) the library republishes on **every** commit, subsuming the `D-history_published_reads-3` hook's whole reason to exist. *Alternative rejected — keep the host publisher, ignore the library API:* leaves the debt the triage flagged and the dead `entry_at` read at `history.cpp:38`. *Alternative rejected — keep the host publisher but source it from `journal().history()` instead of `entry_at`:* still a writer-thread republish, an atomic, a stamp guard and a hook, all to re-wrap a snapshot the library already publishes any-thread.

**D-history_snapshot_adopt-2 — The accessor stays in L1 `commands`; `views` never includes the journal header.**
`AppState::history()` (any-thread, `const`) returns the panel's model built from `journal().history()`; `views.cpp` calls `state.history()` and holds the result with `auto`. *Rationale:* (i) A18's grep invariant deliberately keeps `arbc/model/journal.*` and `entry_at` out of `views.cpp`, so the journal read must live one level down in `commands` (which is arbc-allowed, `check_levels.py:49`); (ii) it keeps `views` a pure renderer of a published value, exactly as under A18 — the *shape* of the value changes, the *rule* does not; (iii) an `auto`-typed local resolves `arbc::HistoryView` transitively through `app_state.hpp` without `views.cpp` adding its own journal include. *Alternative rejected — `views.cpp` calls `state.document().journal().history()` directly ("read `journal().history()` directly" read literally):* re-introduces the `arbc/model/journal.hpp` include A18's predecessor removed and trips the grep invariant; "directly" means *without a writer-thread republish*, not *from inside `views`*.

**D-history_snapshot_adopt-3 — Clamp the separately-published cursor against the snapshot's own size; derive all affordances from the clamp.**
`HistoryView` carries no cursor, so the accessor reads `journal().cursor()` as a second atomic and clamps it into `[0, rows->size()]` against the very `HistoryView` instance it returns; the applied/redoable split and the "Undo <name>"/"Redo <name>" labels are computed from the clamped cursor against `rows->size()`, not from `journal().can_undo()/can_redo()`. *Rationale:* (i) this re-satisfies `D-history_published_reads-2`'s out-of-bounds guarantee — the reason the host snapshot bundled the cursor — now that the library does not bundle it; (ii) `depth()` is **not** monotonic (a commit-after-undo trims it), so the library's publish-ordering guarantee alone does not bound `rows[cursor]` — only a clamp against the snapshot's own size does, and clamping makes any one-frame tear cosmetically stale but never out-of-bounds, self-correcting next frame (the bounded staleness A18 already accepts); (iii) the clamp is a pure function (`clamp_history_cursor`), unit-testable headless with synthetic out-of-bounds inputs — the "branchy end-stop logic belongs in a headless Catch2 test" principle `D-history_published_reads-4` invoked. *Alternative rejected — re-bundle the cursor into a writer-thread-published value:* that is precisely the host mirror this leaf retires. *Alternative rejected — trust the library publish ordering and skip the clamp:* non-monotonic `depth()` defeats it; an out-of-bounds read is a crash, not a glitch.

**D-history_snapshot_adopt-4 — Retire the L4 `CanvasView` post-edit hook; the library covers its purpose.**
`set_post_edit_hook`/`post_edit_hook_` and the `shell.cpp:431` binding are removed. *Rationale:* (i) the hook was added by `D-history_published_reads-3` for **one** reason — `scene::` edits (camera inspector, manipulators) commit inside a raw `apply_edit` closure and never pass through the `commands` verbs, so verb-only refresh would leave the panel stale; the library now republishes `journal().history()` on **every** commit regardless of path, so that gap is closed structurally; (ii) `publish_history` is the hook's **sole** binding (`grep` confirms one `set_post_edit_hook` call, at `shell.cpp:431`), so unbinding it leaves a hook with zero callers — a vestigial seam that misleads future readers; (iii) the "bare `scene::set_camera_resolution` shows up in `state.history()`" acceptance case pins that the path-independence survives the hook's removal. *Alternative rejected — leave `set_post_edit_hook` present but unbound:* dead API with no consumer; A18's note that `editor.canvas.writer_thread` would "move that hook to its per-closure epilogue" is moot once the hook has nothing to publish — `writer_thread` builds its own epilogue seam if it needs one, and is not blocked by this removal.

**D-history_snapshot_adopt-5 — `navigate_to` is preserved verbatim minus its redundant publish.**
The L1 click-to-jump verb keeps its `NavigateOutcome` struct, its clamp-to-`[0, depth()]`, and its end-stopped single-step walk (`D-history_published_reads-4`); only the trailing `publish_history(state)` call is deleted. *Rationale:* (i) it is a **verb, not a read** — the task note is explicit that it stays in L1 and is untouched as a seam; (ii) its reads (`cursor()`/`depth()`) are any-thread published atoms, valid off the writer thread; (iii) after the walk, `journal().history()` already reflects the final state (each step's `undo`/`redo` republishes it internally), so the explicit publish is dead. **No behavioral change** to the verb's result.

**D-history_snapshot_adopt-6 — Retarget `history_publish_test.cpp`, do not wholesale-retire it.**
The cases that assert **editor behavior** (dispatch/undo/redo produce the right rows+cursor, the `navigate_to` `NavigateOutcome` matrix, the bypassing-`scene`-edit case, the concurrent-reader TSan case, plus the new `clamp_history_cursor` unit) survive, rewritten against `state.history()`. The cases that assert **host-publisher mechanics** (refresh stamp-guard / pointer-identity on an unchanged journal; the host snapshot's held-immutability) retire — their subject moved upstream and is a **library** guarantee, pinned for existence by `arbc_pin_test.cpp:320` and behaviorally owned by libarbc's own tests. *Rationale:* (i) the verbs and the read protocol are still editor code and still need L1 coverage; (ii) re-asserting the library's internal republish idempotence in the editor's suite tests code the editor no longer owns; (iii) the filename is kept to avoid `CMakeLists.txt`/target churn — a rename to `history_snapshot_test.cpp` is optional and not required. *Alternative rejected — delete the file with the publisher:* discards the still-valid verb, path-independence and concurrency coverage.

**D-history_snapshot_adopt-7 — Amend A18 in place (doc delta) rather than add a new A-row.**
A18's normative rule — *read history only through a published immutable snapshot, never via `entry_at()`* — is **unchanged**; what changes is the publisher (host → library) and the cursor sub-decision (`D-history_published_reads-2` re-bundling → clamp-on-read). A18's prose describes the host `HistoryPublisher` as the concrete mechanism, so leaving it unamended would make the constitution describe retired code. *Rationale:* (i) the same-commit rule requires the governing row to track a change to the mechanism it documents; (ii) the history-read rule belongs in **one** place, and A4.1a's precedent (dated in-place amendments for the v0.3.0 and v0.4.0 threading changes) plus A20/A21's inline `*(Amended by …)*` idiom are the house style; (iii) a new A-row would split the one rule across two rows. *Doc delta:* an `*(Amended by editor.canvas.history_snapshot_adopt: …)*` clause appended to A18 in `docs/01-architecture.md`.

## Open questions

(none — all decided)

## Status

**Done** — 2026-07-28.

- Deleted host-mirror files `src/commands/ace/commands/history.hpp` and `src/commands/history.cpp` (HistoryPublisher, HistorySnapshot, publish_history).
- Replaced `commands::AppState::history()` with a thin any-thread accessor returning the library's `journal().history()` snapshot + a clamped cursor (`src/commands/ace/commands/app_state.hpp`, `src/commands/app_state.cpp`).
- Added `commands::clamp_history_cursor` guard (clamping raw cursor into `[0, rows->size()]`) and retired all four writer-turn `publish_history` calls from dispatch/undo/redo/navigate_to.
- Retired the L4 `CanvasView` post-edit hook (`set_post_edit_hook` / `post_edit_hook_`) and its sole binding in `src/app/shell.cpp`; removed related declarations from `src/app/ace/app/canvas_view.hpp` and `src/app/canvas_view.cpp`.
- Repointed `draw_history` in `src/views/views.cpp` to the library snapshot via `state.history()`; updated comment truth-ups in `src/views/ace/views/views.hpp`.
- Retargeted `tests/history_publish_test.cpp`: retired host-publisher-mechanics cases (stamp-guard, held-immutability); added `clamp_history_cursor` bounds unit, bare `scene::set_camera_resolution`-via-`apply_edit` case, and concurrent-reader (TSan/ASan) case.
- A18 amendment (doc delta) landed in `docs/01-architecture.md` (already present in working tree).
