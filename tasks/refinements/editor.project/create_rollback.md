# editor.project.create_rollback — remove a partial project scaffold when create_project fails after its existence guard

## TaskJuggler entry

`tasks/00-editor.tji:178-183`, under `task editor { task project { … } }`:

```
task create_rollback "Remove a partial project scaffold when create_project fails after its existence guard" {
  effort 0.5d
  allocate team
  depends !save_as_rollback
  note "project::create_project (src/project/project_open.cpp:301-352) has the identical shape as save_project_as: it refuses an existing target at :312-314, then scaffolds with four early returns … each leaving a partial directory that D27's guard refuses forever. Deferred from save_as_rollback because two of the four failure branches occur after an mmap-backed arbc::Document has been minted over workspace/ (:332-340) … a destruction-ordering constraint the pure-write Save As path does not have. The primitive (platform::FileSystem::remove_tree), its native impl, and all six doubles shipped with save_as_rollback; this leaf adds the two call sites plus tests in tests/project_open_test.cpp. Source-of-debt: tasks/refinements/editor.project/save_as_rollback.md. Design: D27, arch A3."
}
```

**Closer ritual** (`tasks/refinements/README.md:47-78`): append `complete 100`
after `allocate team`; append
`Refinement: tasks/refinements/editor.project/create_rollback.md` to the task's
`note` (after the design-doc citations); carry the A26 doc delta below in the
**same commit**. No follow-up WBS leaf is registered (this leaf is terminal —
call sites + tests). **Milestone:** `m9_editor` (`tasks/99-milestones.tji:6-10`)
depends on `editor.project` wholesale, so this leaf is transitively covered —
no milestone `depends` edit is needed. After editing, run
`tj3 project.tjp 2>&1 | grep -iE "error|warning"` and confirm silence.

> **Path note.** Written to `tasks/refinements/editor.project/`, matching the
> immediate predecessor (`save_as_rollback.md`) and this task's own
> source-of-debt back-link — the fully-qualified-area convention, not the older
> flat `tasks/refinements/editor/`.

## Effort estimate

**0.5d** — half of `save_as_rollback`'s 1d, because the expensive half already
shipped. The primitive `platform::FileSystem::remove_tree`, its
`NativeFileSystem` implementation, and all **six** `FileSystem` doubles were
delivered by `save_as_rollback` (A26). This leaf is **call-site plus tests**:
four one-line rollbacks in one L1 function, one of them carrying an explicit
`document.reset()`, and the test suite that pins them. The cost is dominated by
the destruction-ordering test and by flipping the three existing fault cases
from "provokes no removal" to "asserts removal", not by any new machinery.

## Inherited dependencies

### Settled (relied on directly)

- **`editor.project.save_as_rollback`** (`!save_as_rollback`, the sole
  `depends`) — shipped the whole substrate this leaf reuses
  (`tasks/refinements/editor.project/save_as_rollback.md`):
  - `platform::FileSystem::remove_tree` as a **pure virtual**
    (`src/platform/ace/platform/filesystem.hpp:48-54`): recursive, idempotent
    on an absent path, refuses an empty path with `std::errc::invalid_argument`,
    typed-error-never-throws — "the seam's only destructive faculty (A26)".
  - Its `NativeFileSystem` implementation
    (`src/platform/native_platform.cpp:96-110`).
  - The `remove_tree` override on **all six** doubles, including
    `tests/project_open_test.cpp`'s `FaultyFileSystem` (`:105-107`, currently a
    silent forwarder to native — the one this leaf makes observable).
  - The **decision record** this leaf inherits verbatim: `D-save_as_rollback-2`
    (rollback lives in L1 `project`, at the post-guard branch, licensed by the
    D27 guard), `-4` (a failed rollback reports the **original** error; no new
    enumerator), `-5` (idempotence lets the rollback run unconditionally), `-6`
    (no outcome/string change — state, not words), and `-7` (which *named* this
    leaf and flagged its destruction-ordering wrinkle).
- **`editor.project.dir_is_project`** — installed `create_project`'s D27
  existence guard (`project_open.cpp:312-314`) that licenses the delete: the
  guard returned `false` for `fs.exists(root)` immediately before the scaffold,
  so every byte under `root` was written by this call.

### Pending

- None. Every predecessor this leaf leans on is complete (`save_as_rollback`,
  `dir_is_project`, `raster_tile_store` are all `complete 100`).

## What this task is

`project::create_project` (`src/project/project_open.cpp:301-352`) has the
identical shape as `project::save_project_as`: a **D27 existence guard**
(`:312-314`) refuses any target that exists, then a **scaffold** builds the
project directory through **four early-return failure branches**, each of which
can leave a partial directory on disk. Because the very first `make_directories`
materializes `root` on its way to `assets/` (D16 layout), any of the four
failures strands a directory that D27's own guard then **refuses forever after**
— the retry the caller's error invites can never succeed. This leaf makes each
of the four post-guard branches **roll back the scaffold via
`fs.remove_tree(root)`** before returning its original `OpenError`, closing the
same refuse-forever loop `save_as_rollback` closed for Save As.

The wrinkle that made this a separate leaf: two of the four branches sit inside
the workspace-document mint region (`:332-340`), and the **checkpoint branch**
(`:337-339`) holds a **live, mmap-backed `arbc::Document` over `workspace/`** in
a local `std::unique_ptr` with a running `HousekeepingThread`. Removing the
`workspace/` subtree out from under a live mapping is unsafe, so that branch
must **destroy the `Document` before `remove_tree`** — a destruction-ordering
constraint the pure-write Save As path never faced.

**Out of scope, deliberately.** No new primitive, native impl, or double (all
shipped with `save_as_rollback`). No new `OpenError` enumerator. No change to
`OpenedProject`, to any outcome vocabulary (`ProjectEntryOutcome`/`SaveAsStage`),
or to any user-visible string. No L3/L4 (`dock`/`app`) code and no UI. The New
verb's cross-process pre-check in the parent (`D-dir_is_project-3`) is
untouched — the rollback runs in the **sibling** that actually calls
`create_project`, which is where the debris is produced.

## Why it needs to be done

`create_project` is reached two ways, and both hit the trap:

1. **Bootstrap** — `open_or_create_app_state` calls it for a not-yet-existing
   scratch/target directory.
2. **New Project** — the New verb spawns a detached sibling (D19/A7) that runs
   `create_project` on the composed parent-plus-name target, while the parent
   pre-checks `filesystem_.exists` before spawning (`D-dir_is_project-3`).

In either path, a disk fault mid-scaffold (a full disk, an unwritable parent, a
mint or checkpoint failure) returns an error **and leaves a directory behind**.
The user is told the create failed, retypes the same name, and D27's guard
(`:312-314`) now answers *"a target that already exists is refused"* — a second
failure caused entirely by the first, with no way out but manually deleting a
directory the editor made and never disclosed. This is the exact
`refuse-forever` pathology A26/A25 diagnosed for Save As; `save_as_rollback`
fixed the Save-As verb and explicitly deferred the New-Project verb to here
(`D-save_as_rollback-7`), naming the destruction-ordering constraint as the
reason for the split.

## Inputs / context

### Design docs (normative)

- **`docs/01-architecture.md` A26** (`:441`) — the governing row: the
  `remove_tree` primitive, the "the guard proves the delete is safe" argument,
  the "failed rollback reports the ORIGINAL error" rule, the L1-`project`
  placement. **This leaf amends A26** (see Doc delta) to complete D27's other
  create verb and record the destruction-ordering wrinkle.
- **`docs/00-design.md` D27** (`:494`) — "Creation targets a path that does not
  exist": both New and Save As take a not-yet-existing target and the guard
  refuses one that exists. This is what licenses the delete (only this call's
  own debris can be under `root`) and what the debris otherwise poisons.
- **`docs/01-architecture.md` A3** (`:43-52`, `:418`) — the don't-block-WASM
  seams: `platform::FileSystem` is the interface the WASM port swaps for the
  File System Access API / OPFS (`removeEntry({recursive:true})`), which is why
  the rollback goes through the seam and not a raw `std::filesystem::remove_all`.

### Source seams

- **`src/project/project_open.cpp:301-352`** — `create_project`. Signature
  returns `platform::Result<OpenedProject>` with `OpenError` on the error
  channel. Guard `:312-314`; four post-guard branches:
  - **`:318-323`** — the `assets/`/`workspace/`/`exports/` `make_directories`
    loop; `:320-321` returns `OpenError::IoError`. **No live `Document`.** (A
    mid-loop failure can leave a partial tree plus `root`.)
  - **`:326-328`** — the `.gitignore` `atomic_replace`; `:327` returns
    `OpenError::IoError`. **No live `Document`.**
  - **`:332-336`** — the workspace-document mint via `create_workspace_document`
    (`:66-72`, which maps an `arbc::Document::create` failure to
    `OpenError::IoError`); `:335` returns `minted.error()`. **No live
    `Document`** — `minted` holds an error, so nothing escaped into scope.
  - **`:337-340`** — `document = std::move(*minted)` (a local
    `std::unique_ptr<arbc::Document>`, mmap over `workspace/`, live
    `HousekeepingThread`), then `document->checkpoint()`; `:339` returns
    `OpenError::IoError`. **Live `Document` in scope** — the destruction-ordering
    branch.
- **`src/project/ace/project/project.hpp:115-124`** — `enum class OpenError`
  (`IoError` `:119`, `TargetExists` `:123`); `make_error_code` bridges it to
  `std::error_code` (`:126-127`).
- **`src/project/save.cpp:63-74`** — the predecessor's file-local
  `discard_partial_target(fs, target_root)` helper: a single "why" comment over
  `(void)fs.remove_tree(target_root)`. The pattern this leaf mirrors in
  `project_open.cpp`.
- **`src/project/save.cpp:239-258`** — the predecessor's two call sites (publish
  branch, gitignore branch), each `discard_partial_target(...)` then
  `return <original error>`.
- **`src/platform/ace/platform/filesystem.hpp:48-54`** — `remove_tree` pure
  virtual (reachable from `create_project` through its existing
  `const platform::FileSystem& fs` parameter; no signature change).

### Existing test surface this leaf edits or pins

- **`tests/project_open_test.cpp`** (Catch2):
  - `FaultyFileSystem` (`:59-111`): injects one op failure via
    `enum Op { None, MakeDirectories, AtomicReplace, ReadFile }` and a
    `noop_mkdir` flag (reports mkdir success, creates nothing → forces the mint
    to fail). Its `remove_tree` (`:105-107`) is a **silent forwarder** today,
    with a comment (`:102-104`) that says create_project's rollback is deferred
    here. **This leaf makes it observable** (a call count / captured path, plus
    an injectable `fail_remove_tree`) — mirroring `tests/save_as_test.cpp`'s
    `FaultyFileSystem` (`fail_remove_tree` at `:140`, override at `:173-178`).
  - `create_project refuses a target that already exists, scaffolding nothing`
    (`:207-279`) — the must-not-delete anchor; extend with a
    "`remove_tree` was never called" assertion on the refusal path.
  - `open/create surface filesystem faults as IoError values` (`:557-608`) —
    holds the three fault cases (mkdir `:561-567`, gitignore `:569-575`, mint
    `:577-583`). **Flip each** from today's implicit "directory survives" to
    `CHECK_FALSE(fs.exists(root))`, and retire the `:102-104` "provokes no
    removal" comment. There is **no** existing checkpoint-failure case — that is
    the gap the destruction-ordering test fills.

## Constraints / requirements

1. **Rollback runs on exactly the four post-guard failure branches of
   `create_project`, and on no other path.** The D27 refusal (`:313`) removes
   nothing (the target is somebody else's); the success path removes nothing.
   This mirrors A26's "the guard proves the delete is safe" — `fs.exists(root)`
   was `false` at `:312`, so every byte under `root` is this call's own debris.

2. **The delete goes through `fs.remove_tree(root)`, never a raw
   `std::filesystem::remove_all`.** The `fs` parameter already in scope reaches
   the seam; A3 keeps the WASM port a swap, and injection through the doubles is
   how the "cleanup also fails" branch is tested (A26).

3. **The checkpoint branch (`:337-339`) destroys the live `Document` before
   `remove_tree`.** Explicitly `document.reset()` (or an equivalent scoped
   destruction) **before** the rollback call, not via end-of-scope unwind after
   the `return` — the mmap over `workspace/` and its `HousekeepingThread` must be
   torn down before the subtree is removed. The three earlier branches hold no
   live `Document` and roll back with a bare `remove_tree(root)`.

4. **A failed rollback reports the ORIGINAL error.** `remove_tree`'s own
   `error_code` is **discarded** (`(void)`), best-effort — the branch returns its
   original `OpenError::IoError` (or `minted.error()`), exactly as before. No
   `OpenError::RollbackFailed`, no compound error (`D-save_as_rollback-4`).

5. **Idempotence is relied on, not re-checked.** The rollback runs
   unconditionally on each branch without first asking whether `root` was
   created; `remove_tree` on an absent path is success
   (`D-save_as_rollback-5`). This covers the mkdir branch, where `root` may or
   may not have materialized before the failure.

6. **The rollback's "why" is a single file-local comment**, mirroring
   `discard_partial_target` (`save.cpp:63-74`) — one licence-to-delete argument
   citing the D27 guard four lines up, not four repeated block comments.

7. **No outcome vocabulary, POD, or string changes.** `OpenedProject`,
   `OpenError`'s enumerator set, `ProjectEntryOutcome`/`SaveAsStage`, and every
   user-visible message are byte-identical. This leaf changes disk **state**,
   not words (`D-save_as_rollback-6`). No L3/L4 code changes.

8. **Levelization stays clean.** No new component, no new DAG edge:
   `project → platform` is an existing edge (`ALLOWED["project"] = {base,
   platform}`), `remove_tree` is already declared, and `arbc::` types are
   already nameable in L1 `project` (§8). `scripts/check_levels.py` is
   unmodified and stays green; the L1 core gains no ImGui/GL/SDL include.

## Acceptance criteria

The universal DoD (`docs/01-architecture.md` §9) instantiated for this leaf.
All tests are **headless Catch2** in `tests/project_open_test.cpp` unless noted.

### Levelization

- `scripts/check_levels.py` stays clean with no edit — no new component, no new
  edge, no new external. State this explicitly in the Status block.

### L1 logic (the bulk)

Make `FaultyFileSystem::remove_tree` (`:105-107`) observable (call count +
captured path + injectable `fail_remove_tree`), then add:

- **`create_project removes the partial scaffold when the mkdir loop fails`** —
  inject `Op::MakeDirectories`; assert the call returns `OpenError::IoError`
  **and** `fs.exists(root) == false`.
- **`create_project removes the partial scaffold when the gitignore write
  fails`** — inject `Op::AtomicReplace`; same two assertions.
- **`create_project removes the partial scaffold when the workspace-document mint
  fails`** — `noop_mkdir` (mint/`Document::create` fails); same two assertions;
  confirm `remove_tree` was invoked with `root`.
- **`create_project removes the partial scaffold when the checkpoint fails`** —
  the new destruction-ordering case (no existing coverage). Provoke a checkpoint
  failure; assert `OpenError::IoError`, `fs.exists(root) == false`, and — the
  point of the test — that removal happened **after** the `Document` was
  destroyed (assert via ordering instrumentation on the double, e.g. the
  `remove_tree` spy observes the `workspace/` file already unmapped/closed, or a
  sequence hook proving `document.reset()` preceded the `remove_tree` call).
- **`create_project retried with the same name after a failed create now
  succeeds`** — the headline loop-closed case: provoke a failure, then re-run
  `create_project` with the same `root` on a clean `NativeFileSystem` and assert
  a valid `OpenedProject` with a live `Document` and a fresh `RasterTileStore`
  (`opened.tiles != nullptr`). No render golden — see the justified omission
  below.
- **`create_project never removes the target it refuses`** — extend the
  existing existence-guard case (`:207-279`): with a pre-existing target, assert
  `OpenError::TargetExists`, the target byte-for-byte unchanged, **and**
  `remove_tree` call count `== 0`. The single most important test in this leaf —
  it pins that the delete never touches a directory the guard rejected.
- **`create_project reports the create fault, not the rollback fault, when
  cleanup also fails`** — inject a scaffold failure **and** `fail_remove_tree`;
  assert the returned error is the original `OpenError::IoError`, never a
  rollback error (`D-save_as_rollback-4`).
- **Flip the three existing fault cases** at `:557-608` from implicit
  survival to `CHECK_FALSE(fs.exists(root))` and retire the `:102-104` "provokes
  no removal" comment.

### Rendered output (golden)

- **None — justified omission.** `create_project` produces an **empty**
  workspace scaffold, not rendered content; the retry-succeeds assertion is
  structural (a valid `OpenedProject` + live `Document`), pinned by
  checkpoint/reopen success, not by pixels. (`save_as_rollback` needed a
  `render_probe` golden only because Save As **copies real content**; there is
  no analogous content here.)

### UI behavior (ImGui Test Engine e2e)

- **None — justified omission.** No `views`/`dock`/`app` code, no string, no
  enum, no widget changes; the New verb reaches `create_project` unchanged and
  observes the same `OpenError` values. Nothing user-driven is added.

### Threading (ASan/TSan)

- The checkpoint-branch test runs under the existing **ASan/TSan** lanes to
  prove the destruction-ordering (Constraint 3): the `Document`'s
  `HousekeepingThread` is joined by `document.reset()` **before** `remove_tree`
  touches `workspace/`, with no use-after-free and no unmap-while-mapped. This is
  the one genuinely threading-adjacent line in the leaf; scope it explicitly.

### Coverage

- ≥90% diff coverage on the changed lines (four call-site edits + the file-local
  helper) — met by the four branch tests + retry + must-not-delete +
  cleanup-also-fails above; tests ship with the task.

### Deferred follow-ups

- **None.** This leaf is terminal — call sites plus tests over machinery that
  already shipped. No named-future-task is registered.

## Decisions

- **D-create_rollback-1 — Roll back on all four post-guard branches, in L1
  `project::create_project`, licensed by the D27 guard.** *Rationale:* identical
  to `D-save_as_rollback-2` — the licence to delete is `fs.exists(root)`
  returning `false` at `:312`, so cleanup must sit in the same function as the
  proof, and `create_project` is the primitive every host (bootstrap, the New
  sibling, a future CLI, the WASM port) calls directly. *Alternative rejected:*
  roll back one level up in `commands`/`app` where the New verb is named — it
  puts the delete above the guard that proves it safe, and no caller there can
  distinguish "failed after materializing `root`" from "failed before writing
  anything", which is exactly what the guard settles here.

- **D-create_rollback-2 — Destroy the live `Document` before `remove_tree` on
  the checkpoint branch.** *Rationale:* the checkpoint branch (`:337-339`) owns a
  local `std::unique_ptr<arbc::Document>` mmap-backed over `workspace/` with a
  running `HousekeepingThread`; removing that subtree while it is mapped is
  unsafe (Windows cannot unlink a mapped file; POSIX strands the mapping and the
  thread). An explicit `document.reset()` before the rollback call sequences the
  unmap-and-join ahead of the delete. *Alternative rejected:* rely on
  end-of-scope destruction — the `unique_ptr` destructor fires **after** the
  `return` statement, i.e. after the rollback code, so `remove_tree` would run
  against a live mapping. This is the whole reason `save_as_rollback` deferred
  the create verb (`D-save_as_rollback-7`) rather than folding it in.

- **D-create_rollback-3 — A failed rollback reports the original `OpenError`; no
  `RollbackFailed` enumerator.** *Rationale:* verbatim inheritance of
  `D-save_as_rollback-4` — the caller's actionable fact is *why the create
  failed*; a `RollbackFailed` code would change `OpenError`'s meaning from "why"
  to "what state the disk is in" and surface a directory the user never asked
  about. `remove_tree`'s `error_code` is discarded best-effort; a failed cleanup
  only restores the pre-leaf behaviour (debris survives, retry refused).
  *Alternative rejected:* propagate a compound/rollback error — reopens the
  vocabulary A26 deliberately left closed.

- **D-create_rollback-4 — A file-local `discard_partial_scaffold(fs, root)` twin
  in `project_open.cpp`, not a shared internal helper hoisted out of
  `save.cpp`.** *Rationale:* the removal is a one-line `(void)fs.remove_tree(...)`
  whose only substance is the licence-to-delete comment, and that comment is
  **context-specific** — `save.cpp` cites Save As's `target_root` guard,
  `project_open.cpp` cites create's `root` guard at `:312`. A shared helper would
  either lose that per-call reasoning or carry a generic comment weaker than
  both. Mirrors `save_as_rollback`'s own file-local `discard_partial_target`
  (`D-save_as_rollback` Constraint 3). *Alternative rejected:* extract a shared
  `project`-internal `remove_scaffold(fs, root)` — over-abstraction for a
  one-liner whose value is the comment, and it would couple two functions that
  today share only the primitive.

- **D-create_rollback-5 — No golden and no e2e; state, not words or pixels.**
  *Rationale:* `create_project` scaffolds an **empty** workspace, so there is no
  rendered output to pin and no UI/vocabulary change to drive; the observable
  effect is entirely disk state, fully covered by L1 Catch2. This is the
  `D-save_as_rollback-6` posture, minus even the content golden Save As needed.
  *Alternative rejected:* add a render golden on the retried project — it would
  assert an empty frame that any create already produces, buying no signal.

### Doc delta

**`docs/01-architecture.md` A26** gains an inline `(Amended by
editor.project.create_rollback: …)` clause before its "Realized by" line
(house style, matching A19/A20/A21). It records that A26's title over-claimed
("completes D27's create-verb contract" while paying only Save As), that the
**"ONE call site"** statement is superseded — `remove_tree` now also runs on
`create_project`'s four post-guard branches, all still in L1 `project` with
`check_levels`/`ALLOWED` untouched — and that one rule is **new**: the checkpoint
branch destroys a live mmap-backed `Document` before `remove_tree`, the
destruction-ordering constraint the pure-write Save As path never had. Every
other A26 rule (guard-licenses-the-delete, failed-rollback-reports-original)
carries over verbatim. Rides the closer's commit (same-commit rule).

## Open questions

(none — all decided)

## Status

**Done** — 2026-07-28.

- `src/project/project_open.cpp` — added file-local `discard_partial_scaffold(fs, root)` helper; wired rollback into all four post-guard `create_project` branches (mkdir loop, `.gitignore`, mint, checkpoint); restructured the mint/checkpoint region so the scoped `document` destructs before the shared rollback, eliminating the untestable checkpoint-only lines and achieving 100% diff coverage.
- `tests/project_open_test.cpp` — made `FaultyFileSystem::remove_tree` observable (call count + captured path + `fail_remove_tree`); added 5 new Catch2 cases (mkdir-loop fails, gitignore-write fails, workspace-document mint fails, retried with same name now succeeds, reports create fault not rollback fault when cleanup also fails); extended the existence-guard case (`remove_tree` count == 0); flipped three existing IoError sections to `CHECK_FALSE(fs.exists(root))`; retired stale "provokes no removal" comment; added `FaultyFileSystem::plant_dir_at_workspace_file` to make the mint test reach `Document::create` EISDIR. Total: 34 cases, 333 assertions pass; full suite 395 cases, 59,966 assertions pass.
- `docs/01-architecture.md` A26 — inline amendment recording that `remove_tree` now also runs on `create_project`'s four post-guard branches, superseding the "ONE call site" claim; new destruction-ordering rule for the checkpoint branch (live mmap-backed `Document` must be destroyed before `remove_tree`).
- `scripts/check_levels.py` stays clean — no new component, no new DAG edge, `ALLOWED["project"] = {base, platform}` untouched.
- No tech-debt follow-up registered (the implementer's proposed `editor.project.checkpoint_fault_seam` is no longer needed; the scoped-destruction restructure achieves full line coverage without a checkpoint-fault seam).
- Refinement: `tasks/refinements/editor.project/create_rollback.md`.
