# editor.project.save_as_rollback — remove a partially published Save As target when the publish fails

## TaskJuggler entry

`tasks/00-editor.tji:171-176`, under `task editor { task project { … } }`:

```
task save_as_rollback "Remove a partially published Save As target when the publish fails" {
  effort 1d
  allocate team
  depends !save_as_outcome
  note "…  Source-of-debt: tasks/refinements/editor/save_as_outcome.md. Design: D27, arch A3/A12/A13."
}
```

**Closer ritual** (`tasks/refinements/README.md:47-78`): append `complete 100`
after `allocate team`, append
`Refinement: tasks/refinements/editor.project/save_as_rollback.md` to the
task's `note`, register the one deferred follow-up named below as a real WBS
leaf wired into `milestones.m9_editor` (`tasks/99-milestones.tji:6-10`, which
already depends on `editor.project`), and confirm
`tj3 project.tjp 2>&1 | grep -iE "error|warning"` is silent.

Note on location: the sibling refinements for this area still live in
`tasks/refinements/editor/`; this file lands in `tasks/refinements/editor.project/`
per the newer fully-qualified-area layout already used by
`tasks/refinements/editor.cells/`, `editor.cameras/`, `editor.dock/`. The
back-link in the `.tji` note must match this path.

## Effort estimate

**1d.** The work is one new virtual on an L0 seam (declaration + doc comment +
native implementation), a six-file mechanical override sweep across the existing
`FileSystem` test doubles, roughly eight lines in `project::save_project_as`, and
the tests that pin the new state invariant — including two that pin what must
*not* be deleted. The estimate is dominated by the safety tests, not by the
primitive.

## Inherited dependencies

**Settled** (all predecessors are `complete 100`):

- `editor.project.save_as_outcome` (`tasks/refinements/editor/save_as_outcome.md`,
  Done 2026-07-24) — the direct `depends`. Shipped `SaveAsStage` /
  `SaveAsResult` (`src/commands/ace/commands/app_state.hpp:345-360`), the fourth
  `ProjectEntryOutcome` enumerator `publish_failed`, and the message
  *"Could not save a copy there."* It explicitly deferred **this** leaf and
  scoped it: *"making the state recoverable needs a recursive-remove faculty on
  `platform::FileSystem` (native impl + test double + the A3 WASM seam), which is
  a new L0 method and outside a 0.5d reporting change."*
- `editor.project.dir_is_project` (`tasks/refinements/editor/dir_is_project.md`,
  Done 2026-07-24) — `D-dir_is_project-1` put the exists-at-all guard in the L1
  primitives and claimed a refused Save As *"still writ[es] nothing."* True of
  the **refusal**; this leaf pays for the case the claim does not cover, a
  failure *after* the guard.
- `editor.project.entry_outcome` (`tasks/refinements/editor/entry_outcome.md`,
  Done 2026-07-24) — `A24`, the outcome vocabulary this leaf deliberately does
  not touch.
- `editor.project.save_as` (`tasks/refinements/editor/save_as.md`, Done
  2026-07-18) — `D-save_as-1` (the copy is a re-publish, not a byte copy) and
  `D-save_as-2` (the current session is untouched).
- `editor.project.gc` (`tasks/refinements/editor/gc.md`, Done 2026-07-18) —
  `D-gc-2` / its Constraint 8, the standing *"the library owns deletion"* posture
  and its *"no new `FileSystem` primitive"* scope statement, which this leaf
  narrows (see `A26` and `D-save_as_rollback-1`).

**Pending:** none. Nothing this leaf needs is unbuilt.

## What this task is

`project::save_project_as` refuses a target that exists and then publishes into
it — and the publish is what *creates* the directory. So every failure between
the guard and the return leaves a directory on disk that the guard will refuse
forever after. This leaf adds one primitive to the `platform::FileSystem` seam,
`remove_tree`, and calls it on exactly the two post-guard failure branches of
`project::save_project_as`, so a failed Save As leaves the filesystem as it found
it and the retry the shipped message already invites actually works.

Out of scope, deliberately: the outcome vocabulary (`ProjectEntryOutcome`,
`SaveAsStage`, `SaveAsResult` and every user-visible string are byte-identical
after this leaf — `A25` already made the *message* honest); the `spawn_failed`
path, where the copy is complete and must survive; plain `project::save_project`,
which publishes into a root it did not create; and `project::create_project`'s
identical partial-scaffold trap, which is real, is named below, and is deferred
because rolling back a scaffold that already holds a live mmap-backed
`arbc::Document` is a destruction-ordering problem this leaf's pure-write path
does not have.

## Why it needs to be done

The failure is a *loop*, not a cosmetic wart. Concretely, on a full disk or an
unwritable parent:

1. The user picks a parent and types `my-copy`. `project::save_project_as`
   passes D27's guard (`src/project/save.cpp:205`), then `save_project`
   `make_directories`-es `<parent>/my-copy/assets/` (`src/project/save.cpp:106-108`),
   materializing `<parent>/my-copy/`, and then a `SaveError` comes back from the
   canonical publish (`:228-232`) or the trailing `.gitignore` write (`:237-239`).
2. `commands::save_project_as` maps the `SaveError` to
   `SaveAsStage::publish_failed` (`src/commands/app_state.cpp:236-240`),
   `AppProjectGateway::save_as` re-vocabularizes it to
   `ProjectEntryOutcome::publish_failed` (`src/app/project_gateway.cpp:241-254`),
   and the rail says *"Could not save a copy there."* Correct, and it invites a
   retry.
3. The modal stays open (`D-save_as_outcome-6`). The user frees some disk and
   presses **Save Copy** again with the same name. D27's guard now sees the
   debris and returns `std::errc::file_exists`, which L1 buckets as
   `SaveAsStage::refused` (`app_state.cpp:236-240`), and the rail says
   *"Enter a project name that does not already exist here."*

Step 3 is a false statement produced by step 1, and no amount of retyping
resolves it — the only escape is leaving the editor and deleting a directory the
tool created and never mentioned. `A25` documented exactly this loop for the
`spawn_failed` case and fixed it with a message, because there the copy is
genuinely on disk and valid. For `publish_failed` there is nothing worth keeping,
so the honest fix is state, not words.

Downstream, the primitive is the thing: the seam is what the A3 WASM port swaps
(`src/platform/ace/platform/filesystem.hpp:13-17`), and the very next consumer is
already named — `editor.project.create_rollback` closes the identical hole in
`project::create_project` (`src/project/project_open.cpp:301-352`), where a
failure at any of four steps leaves a scaffold behind and the same D27 guard
(`:312-314`) then refuses the retry.

## Inputs / context

**Design docs (normative).**

- `docs/00-design.md:483` — **D16**, project = a directory (`project.arbc` +
  `assets/` + `workspace/` [+ `exports/`]); the portable core is what Save As
  publishes.
- `docs/00-design.md:494` — **D27**, *"A project directory is created, never
  adopted."* The guard this leaf must not weaken, and the source of the
  refuse-forever behaviour. Note D27's own phrasing about the refusal — *"with
  the target left untouched"* — is a statement about the refusal path only.
- `docs/01-architecture.md:365` — **A3**, the don't-block-WASM seams, including
  *"a `PlatformServices` interface (file/threads/clock)"*. Any file faculty the
  editor needs belongs here.
- `docs/01-architecture.md:374` / `:375` — **A12** / **A13**, the
  `dock::ProjectGateway` inversion and its session verbs. Cited by the `.tji`
  note; this leaf changes nothing at that seam, and saying so is part of the
  work order.
- `docs/01-architecture.md:387` — **A25**, the shipped Save As outcome contract:
  *"a spawn failure leaves a **complete, valid copy on disk**"*, which is the
  boundary this leaf must not cross.
- `docs/01-architecture.md:388` — **A26**, this leaf's doc delta (written with
  this refinement; rides in the closer's commit). See *Decisions*.
- `docs/01-architecture.md:255-291` — §8, the levelization DAG.
- `docs/01-architecture.md:293-357` — §9, the layered DoD, and §9.1, the
  offscreen software-GL ASan lane.

**Source seams.**

- `src/platform/ace/platform/filesystem.hpp:13-17` — the class comment naming
  the A3 swap (*"The WASM port swaps this for the File System Access API /
  OPFS"*); `:18-46` — the six pure virtuals `exists`, `list_directory`,
  `read_file`, `write_file`, `make_directories`, `atomic_replace`. **No
  deletion faculty exists.** `:40` — `make_directories`'s idempotence contract,
  the model for `remove_tree`'s. `:50-61` — `NativeFileSystem`'s overrides.
- `src/platform/native_platform.cpp:88-94` — `NativeFileSystem::make_directories`
  (`create_directories` + `ec`, never throws); `:96-124` — `atomic_replace`,
  including the two `std::filesystem::remove(temp, rm)` cleanups at `:112` and
  `:120` — the only deletes in `src/`, both single-file, both on a failure path.
  The new implementation sits beside these.
- `src/project/save.cpp:190-242` — `save_project_as`. `:195` the layout;
  `:205-207` D27's guard returning `std::errc::file_exists`; `:228-229` the
  publish delegating to `save_project`; `:230-232` the publish-failure return;
  `:237-239` the `.gitignore` `atomic_replace` and its `SaveError::IoError`.
- `src/project/save.cpp:106-108` — inside `save_project`,
  `fs.make_directories(layout.assets_dir)`: **the line that materializes
  `target_root`.** `save_project_as` never calls `make_directories` itself.
- `src/project/ace/project/save.hpp:33-37` — `SaveError { SerializeFailed,
  AssetWriteFailed, IoError }`; `:124-131` — `save_project_as`'s D27 contract in
  the doc comment, which this leaf extends with the rollback promise.
- `src/commands/app_state.cpp:192-252` — `commands::save_project_as`;
  `:236-240` the `file_exists`-versus-`SaveError` split (`D-save_as_outcome-3`);
  `:200-203` the empty-target guard whose `std::errc::invalid_argument` this leaf
  mirrors in the primitive.
- `src/app/project_gateway.cpp:207-255` — `AppProjectGateway::save_as`, the total
  `switch` over `SaveAsStage`. **Unmodified by this leaf.**
- `src/project/project_open.cpp:301-352` — `create_project`: guard at `:312-314`,
  scaffold at `:318-323`, `.gitignore` at `:326-328`, workspace mint at `:332-340`.
  Four early returns, no cleanup. The deferred follow-up's target.
- `src/project/gc.cpp:74-79` and `src/project/ace/project/gc.hpp:50-53` — the
  standing *"the library owns deletion (Constraint 8)"* comment, and the reason
  it does not generalize: libarbc's only deletion surface is
  `AssetReaper::remove_tile(hash)` under `assets/tiles/`
  (`arbc/serialize/asset_reaper.hpp:68`, `arbc/runtime/asset_gc.hpp:42-50`), so
  `arbc::gc_project_directory` structurally cannot remove a project root.
- `scripts/check_levels.py:21-40` — `ALLOWED`, with `"platform": {"base"}` and
  `"project": {"base", "platform"}`; `:45-56` — `EXTERNAL_ALLOWED`, in which
  `platform` appears nowhere.

**Existing test surface this leaf edits or pins.**

- `tests/platform_test.cpp:41-138` — the `NativeFileSystem` cases;
  `:179-215` — `FakeFileSystem`, the in-memory injectability proof;
  `:238` — `PlatformServices seam is injectable — a fake filesystem satisfies it`.
- `tests/save_as_test.cpp:132-171` — `FaultyFileSystem` (`fail_make_directories`,
  path-scoped `fail_atomic_replace_at`); `:174` publish-and-source-untouched;
  `:219-263` the three D27 refusal SECTIONs; `:265-282` the unwritable-root case;
  **`:284-305`** the gitignore-fault case, whose
  `CHECK(fs.exists(target_layout.canonical))` at `:303` this leaf deliberately
  flips; `:524` the `render_offline` golden.
- `tests/project_save_test.cpp` — plain-Save coverage; the home of the
  must-not-delete regression.
- `tests/app_project_gateway_test.cpp:87` — its own `FaultyFileSystem`;
  `:611`, `:645` — the `publish_failed` / `spawn_failed` cases `save_as_outcome`
  shipped.
- `tests/project_open_test.cpp:59`, `tests/export_test.cpp:119`,
  `tests/contact_sheet_test.cpp:158` — the remaining `FileSystem` doubles the
  new pure virtual breaks.
- `CMakeLists.txt:232-256` — the `ace_tests` source list (already contains
  `tests/platform_test.cpp`, `tests/project_save_test.cpp`,
  `tests/save_as_test.cpp`, `tests/project_open_test.cpp`);
  `:271-296` — the `ace_shell_test` list (contains
  `tests/app_project_gateway_test.cpp`). **No CMake edit is needed** — every test
  named below extends an already-registered file.
- `.github/workflows/ci.yml:145` — `diff-cover coverage.xml
  --compare-branch="$base" --fail-under=90`, filtered to `src/`
  (`:125-130`). `scripts/gate` has no coverage step, so this is CI-only.

## Constraints / requirements

1. **One new pure virtual on `platform::FileSystem`, appended after
   `atomic_replace`.**

   ```cpp
   // Recursively remove `path` and everything beneath it. Idempotent: an absent
   // path is success, mirroring `make_directories`. Returns the typed error
   // (never throws) on a partial or failed removal, and refuses an empty path
   // with `std::errc::invalid_argument`. This is the seam's only destructive
   // faculty (A26) — the caller owns the policy question of what it is entitled
   // to delete.
   virtual std::error_code remove_tree(const std::filesystem::path& path) const = 0;
   ```

   `= 0`, not defaulted: purity is the forcing function (`D-entry_outcome-3`,
   `A24`) — the six doubles must be updated by the compiler, not by grep.

2. **`NativeFileSystem::remove_tree` is `std::filesystem::remove_all` with the
   `error_code` overload**, in `src/platform/native_platform.cpp` beside
   `make_directories` (`:88-94`). Empty path → `std::errc::invalid_argument`
   before touching disk. `remove_all(p, ec)` on a missing path sets no error, so
   idempotence is free; the implementation must not treat the returned count as
   a result.

3. **Exactly two rollback call sites, both inside `project::save_project_as`**
   (`src/project/save.cpp`): the publish-failure branch (`:230-232`) and the
   `.gitignore`-failure branch (`:237-239`). Factor the two into one file-local
   helper so the *why* comment is written once:

   ```cpp
   namespace {
   // The D27 guard above returned false for `fs.exists(target_root)` immediately
   // before the publish, so every byte under `target_root` was written by THIS
   // call — removing the root can destroy nothing but our own debris (A26). The
   // removal's own error is DISCARDED on purpose: the caller's actionable fact is
   // why the SAVE failed, and a failed cleanup only restores the behaviour that
   // shipped before this leaf.
   void discard_partial_target(const platform::FileSystem& fs,
                               const std::filesystem::path& target_root) {
     (void)fs.remove_tree(target_root);
   }
   } // namespace
   ```

4. **Four paths must provably delete nothing.** These are the leaf's real risk
   surface and each gets a named test:
   - D27's refusal (`save.cpp:205-207`) — the target belongs to someone else.
   - `project::save_project` (`save.cpp:98`…) on any failure — plain Save
     publishes into the session's own live root, which it did not create;
     deleting it would be data loss of the user's actual project.
   - `SaveAsStage::spawn_failed` in `commands::save_project_as`
     (`app_state.cpp:247-248`) — `A25` ships *"Saved the copy, but could not
     start the editor."* precisely because the copy is complete and valid.
   - `commands::save_project_as`'s pre-publish refusals (empty target
     `:200-203`, canonicalize fault `:211-213`) — nothing was created.

5. **No outcome-vocabulary change, and no L3/L4 code change.**
   `dock::ProjectEntryOutcome`, `commands::SaveAsStage`,
   `commands::SaveAsResult`, `entry_feedback`'s strings,
   `AppProjectGateway::save_as` and every ImGui host are byte-identical after
   this leaf. `project::save_project_as` keeps
   `platform::Result<SaveOutcome>` and its existing error codes. The observable
   change is that a retry with the same name now proceeds.

6. **A failed rollback returns the original error.** No `SaveError` enumerator
   is added; no compound error is minted. `SaveError` continues to mean *why the
   save failed*.

7. **Doc-comment deltas ride with the code.**
   `src/project/ace/project/save.hpp:124-131` gains the rollback promise
   alongside the D27 contract; `filesystem.hpp`'s new declaration carries the
   contract in Constraint 1. The `A26` row (`docs/01-architecture.md:388`) is
   written with this refinement and lands in the closer's commit.

8. **Levelization — no new component, no new DAG edge, no lint edit.**
   `filesystem.hpp` gains a method whose signature names only `std::filesystem`
   and `std::error_code`, so `platform` acquires no include and stays out of
   every `EXTERNAL_ALLOWED` set (`scripts/check_levels.py:45-56`).
   `src/project/save.cpp` calls it through the already-declared
   `project → platform` edge (`ALLOWED["project"] = {"base", "platform"}`,
   `:28`). `native_platform.cpp` already includes `<filesystem>`.
   **Zero `ALLOWED` / `EXTERNAL_ALLOWED` edits**, and the L1 core gains no
   ImGui/GL/SDL include.

9. **No new threads, no new shared state.** `remove_tree` is called on the same
   thread, inside the same synchronous call, as the publish it is cleaning up
   after. The `post_writer` capture (`D-writer_thread-7`) has already completed
   by the time `save_project` returns, so no writer-thread write can land in
   `target_root` after the removal. Nothing here is reachable from a render
   thread.

10. **The six `FileSystem` doubles are updated, not worked around.**
    `tests/platform_test.cpp:182` `FakeFileSystem` (in-memory: erase every key
    whose path is under the prefix — this is the WASM-shaped implementation and
    the fake should look like one), `tests/save_as_test.cpp:132`
    `FaultyFileSystem` (forward + a new `bool fail_remove_tree` for
    Constraint 6's test), `tests/project_open_test.cpp:59` `FaultyFileSystem`,
    `tests/app_project_gateway_test.cpp:87` `FaultyFileSystem`,
    `tests/export_test.cpp:119` and `tests/contact_sheet_test.cpp:158`
    `RecordingFileSystem` (plain forwarders).

## Acceptance criteria

`scripts/gate` (levels · clang-format · configure · build · ctest) is clean on
the `dev` preset, and the `clang-asan` lane (§9.1) is clean.

**Levelization.** `python3 scripts/check_levels.py` passes with
`scripts/check_levels.py` **unmodified** — no `ALLOWED` edit, no
`EXTERNAL_ALLOWED` edit, no new component, no new edge (Constraint 8).

**L0 Catch2 units — `tests/platform_test.cpp` (target `ace_tests`, already
registered at `CMakeLists.txt:232-256`).** New cases beside the existing
`NativeFileSystem` block (`:41-138`):

- `TEST_CASE("NativeFileSystem: remove_tree deletes a populated directory recursively")`
  — build `root/a/b/c.txt` + `root/d.txt`, remove `root`, assert no error and
  `!fs.exists(root)`.
- `TEST_CASE("NativeFileSystem: remove_tree on a missing path succeeds")` — the
  idempotence contract that makes the rollback safe when the publish failed
  before creating anything.
- `TEST_CASE("NativeFileSystem: remove_tree refuses an empty path")` — expects
  `std::errc::invalid_argument` and asserts nothing was touched.
- Extend `TEST_CASE("PlatformServices seam is injectable — a fake filesystem satisfies it")`
  (`:238`) with a `remove_tree` assertion against `FakeFileSystem`'s
  prefix-erase, so the in-memory (i.e. WASM-shaped) implementation is exercised,
  not merely compiled.

A native *undeletable-tree* case is deliberately **not** written: the portable
way to produce one is a permission-stripped parent, which does not hold when CI
runs as root. The failure branch is covered by injection instead — see the
rollback-fault case below. Naming the omission is the point; it is not a gap.

**L1 Catch2 units — `tests/save_as_test.cpp` (target `ace_tests`).** New cases,
using the existing `ScratchDir` + `FaultyFileSystem` + `build_saveable_probe`
fixtures:

- `TEST_CASE("save_project_as removes the partial target when the publish fails")`
  — `fail_atomic_replace_at = target_layout.canonical`; assert the returned code
  is a `SaveError` **and** `CHECK_FALSE(fs.exists(target))`.
- `TEST_CASE("save_project_as removes the partial target when the gitignore write fails")`
  — `fail_atomic_replace_at = target_layout.gitignore`; assert
  `SaveError::IoError` and `CHECK_FALSE(fs.exists(target))`.
- `TEST_CASE("save_project_as retried with the same name after a failed publish now succeeds")`
  — **the headline case.** Fault, fail, assert gone; clear the fault; call again
  with the identical `target`; assert `has_value()`, i.e. that D27's guard is no
  longer triggered by our own debris. This is the loop from *Why it needs to be
  done*, closed.
- `TEST_CASE("save_project_as never removes the target it refuses")` — pre-create
  a populated directory, call, assert `std::errc::file_exists` **and** that the
  directory and its contents are byte-unchanged (Constraint 4). The single most
  important test in this leaf.
- `TEST_CASE("save_project_as reports the publish fault, not the rollback fault, when cleanup also fails")`
  — `fail_atomic_replace_at` **and** `fail_remove_tree`; assert the returned code
  is the `SaveError` (Constraint 6) and that the debris survives, i.e. the
  pre-leaf behaviour is what a failed cleanup degrades to.
- **Amended, not added:** `TEST_CASE("save_project_as surfaces a gitignore write fault after a clean publish")`
  (`:284-305`) — its `CHECK(fs.exists(target_layout.canonical))` at `:303` is
  **deliberately flipped** to assert the whole target is gone, exactly as
  `D-dir_is_project-1` flipped `save_as_test`'s populated-target case. The
  error-code assertion at `:302` is unchanged.
- **Unchanged, must stay green:** `:174`, `:219-263` (the three D27 SECTIONs),
  `:265-282` (add `CHECK_FALSE(fs.exists(target))` — with
  `fail_make_directories` the root is never created, so the idempotent rollback
  is a no-op and the assertion documents that), `:308`, `:349`, `:367`, `:393`,
  `:427`, `:453`, `:494`.

**L1 Catch2 unit — `tests/project_save_test.cpp` (target `ace_tests`).**

- `TEST_CASE("save_project leaves the project directory intact when the publish fails")`
  — a real project root, an injected fault, and an assertion that the root,
  `project.arbc` and `assets/` all survive. Plain Save must never inherit the
  rollback (Constraint 4). Without this test, a future refactor that hoists the
  cleanup into `save_project` silently becomes data loss.

**L4 headless Catch2 — `tests/app_project_gateway_test.cpp` (target
`ace_shell_test`, tag `[app_project_gateway]`).**

- `TEST_CASE("AppProjectGateway::save_as retried with the same name after a publish fault publishes cleanly")`
  — drive the shipped seam with its own `FaultyFileSystem` (`:87`) and a
  `RecordingLauncher`: first call returns
  `dock::ProjectEntryOutcome::publish_failed`, second call with the fault
  cleared returns `succeeded` and records exactly one spawn. This pins the
  user-visible payoff at the level `A25` defined and pins Constraint 5 (the
  outcome vocabulary is unchanged).
- **Unchanged, must stay green:** `:611`, `:645`.

**Rendered output — golden.** The `render_offline` byte-exact golden
(`tests/goldens/render_probe_64x64.rgba8`, via `tests/golden_support.hpp`):

- `TEST_CASE("save_project_as's copy reloads and renders byte-exact against the probe golden")`
  (`tests/save_as_test.cpp:524`) is unchanged and must stay green.
- Extend the retry case above (or add a sibling) to reload the *post-rollback
  retry's* copy and compare it byte-exact against the same golden — the
  strongest available statement that the rollback left no poison behind and the
  retry produced a complete project, not a repaired one. Byte-exact, no
  tolerance.

**UI e2e.** **No new ImGui Test Engine test, and this is a justified omission,
not a gap:** this leaf changes no `views`/`dock`/`app` source, no widget, no
string and no outcome enumerator (Constraint 5), and the e2e fakes implement
`dock::ProjectGateway`, not `platform::FileSystem`, so none of them is even
recompiled. The retry behaviour is driven end-to-end at the highest level it
becomes observable — the `AppProjectGateway` case above. The existing e2e suites
must stay green untouched:
`IM_REGISTER_TEST(engine, "save_as", "rail_save_as")`
(`tests/save_as_ui_e2e_test.cpp:102`),
`IM_REGISTER_TEST(engine, "welcome", "three_verbs_and_feedback")`
(`tests/welcome_e2e_test.cpp:150`), and
`IM_REGISTER_TEST(engine, "open_ui", "rail_project_section")`
(`tests/open_ui_e2e_test.cpp:118`).

**Threading.** No new thread, no new shared state, no new lock (Constraint 9).
Scope: the `clang-asan` lane (§9.1) runs `ace_shell_test` and the offscreen
smoke under ASan/UBSan and must stay clean with **no new `tests/lsan.supp`
entry**. `remove_tree` allocates nothing that outlives the call. TSan needs no
new coverage — nothing in this leaf is reachable from the render or writer
threads.

**Build & coverage.** `clang-format` clean; `cmake --preset dev` +
`cmake --build --preset dev --parallel $(nproc)` + `ctest --preset dev` clean;
**no `CMakeLists.txt` edit** (every touched test file is already in `ace_tests`
or `ace_shell_test`). CI's `diff-cover coverage.xml --fail-under=90`
(`.github/workflows/ci.yml:145`) covers the changed `src/` lines: the
`native_platform.cpp` implementation (all three branches — recursive delete,
missing path, empty path — have a named case) and the two `save.cpp` rollback
branches plus the discard helper (two named cases plus the rollback-fault case).

**Doc delta.** `docs/01-architecture.md:388` — the new **A26** row, written with
this refinement, rides in the closer's commit (`tasks/refinements/README.md:84-92`).
No `docs/00-design.md` change: D27 already promises creation-not-adoption and
already says the refusal leaves the target untouched; A26 completes the *failure*
half of that contract without altering any decided user-facing behaviour or
string.

**One follow-up WBS task is deferred**, and only one:

- `editor.project.create_rollback` — **0.5d**, `depends !save_as_rollback` —
  *"Remove a partial project scaffold when `create_project` fails after its
  existence guard."* `project::create_project`
  (`src/project/project_open.cpp:301-352`) has the identical shape: it refuses an
  existing target at `:312-314` and then scaffolds, with four early returns
  (`:321` the `assets/`/`workspace/`/`exports/` mkdir loop, `:327` the
  `.gitignore` write, `:335` the workspace-document mint, `:339` the checkpoint)
  each leaving a partial directory that the same guard refuses forever — and
  because New runs its create in the spawned sibling (D22/A7) while the parent
  pre-checks `filesystem_.exists` (`D-dir_is_project-3`), the user hits the same
  loop across two processes. It is a separate leaf rather than an extension of
  this one for a real reason: the two later failure branches occur *after* an
  mmap-backed `arbc::Document` has been minted over `workspace/`
  (`:332-340`), so the rollback must destroy the `std::unique_ptr<arbc::Document>`
  before removing the tree — a destruction-ordering constraint the pure-write
  Save As path does not have, and one that needs its own tests
  (`tests/project_open_test.cpp`, whose `FaultyFileSystem` at `:59` this leaf
  already re-signs). The primitive, the native implementation and all six doubles
  ship here, so the follow-up is call-site plus tests. Deferred to
  `editor.project.create_rollback` (closer registers in WBS, under
  `editor.project`, which `milestones.m9_editor` already depends on).

## Decisions

**`D-save_as_rollback-1`** — The recursive remove is a **new pure virtual on
`platform::FileSystem`**, not a raw `std::filesystem::remove_all` in `project`.

*Rationale:* the seam is the A3 WASM swap point
(`filesystem.hpp:13-17`), and `remove_tree` maps directly onto the File System
Access API's `removeEntry({recursive: true})` — inlining a POSIX unlink in L1
`project` would put a WASM-hostile call in the one component A3 exists to keep
portable. It is also the only way the behaviour is *testable*: every disk
failure in this codebase is provoked by a `FileSystem` double
(`tests/save_as_test.cpp:132`, `tests/project_open_test.cpp:59`,
`tests/app_project_gateway_test.cpp:87`), so a raw call makes Constraint 6's
"the rollback itself failed" branch unreachable from a test.
*Alternative rejected:* raw `std::filesystem::remove_all`, on `D-gc-2`'s
precedent — but that precedent reads *"the **library** owns deletion"*, and it
holds because `arbc::gc_project_directory` genuinely does the unlinking behind
its own path-taking API. Here nothing below owns it: libarbc's entire deletion
surface is `AssetReaper::remove_tile(hash)` scoped to `assets/tiles/`
(`arbc/serialize/asset_reaper.hpp:68`), which structurally cannot remove a
project root. `gc.md`'s Constraint 8 said *"no new `FileSystem` primitive"* as a
scope statement for a leaf that needed none; this leaf needs one, and `A26`
records the narrowing rather than leaving it implicit.
*Alternative rejected:* a narrower `remove_project_tree(const ProjectLayout&)`
that unlinks only the known D16 members — it looks safer, but it leaves behind
anything else the publish wrote (and anything a future layout member adds),
which is the same half-state under a friendlier name; and it puts project
vocabulary into an L0 seam that must not know what a project is.

**`D-save_as_rollback-2`** — The rollback lives in **L1 `project`**, at the two
post-guard failure branches of `project::save_project_as`, not in
`commands::save_project_as` where the stages are named.

*Rationale:* the licence to delete comes from D27's `fs.exists(target_root)`
guard returning false four lines earlier — that, and only that, proves every byte
under the root is ours. Cleanup must sit in the same function as the proof, or
the invariant becomes a comment in one file about a check in another. It also
means every future host of the primitive (a CLI, the WASM port, a headless
batch publish) inherits the guarantee without restating it — the same argument
`project.hpp:204-207` makes for putting D27's check in the primitive.
*Alternative rejected:* roll back in `commands` on
`SaveAsStage::publish_failed` — it reads well next to the stage vocabulary, but
`commands` cannot distinguish "the publish failed after creating the root" from
"the publish failed before creating anything" without re-deriving what `project`
already knows, and it would leave `project::save_project_as` — a public API —
still able to strand a directory.

**`D-save_as_rollback-3`** — `SaveAsStage::spawn_failed` **does not** roll back,
and neither does plain `project::save_project`.

*Rationale:* these are the two cases where the bytes on disk are the user's, not
debris. `A25` ships *"Saved the copy, but could not start the editor."*
specifically because a spawn failure leaves a complete, valid project; deleting
it to tidy up a launcher fault would convert a recoverable annoyance into data
loss. Plain Save publishes into the session's own live root, which it did not
create and whose prior contents it has no claim on. Both are pinned by named
tests (Constraint 4) rather than left to the reader.
*Alternative rejected:* roll back on `spawn_failed` too, for a uniform
"failure means nothing happened" rule — uniform and wrong: the user's work is
in that directory, and A25's message would become a lie in the opposite
direction.

**`D-save_as_rollback-4`** — A failed rollback **reports the original error**
and adds no `SaveError` enumerator.

*Rationale:* the user's actionable fact is that the copy failed; that is what
*"Could not save a copy there."* says, and it stays true. A
`SaveError::RollbackFailed` would change the enum's meaning from *why the save
failed* to *what state the disk is in*, and it would surface — through A25's
mapper — a message about a directory the user never asked to create, in a
doubly-rare path (the publish failed **and** the cleanup failed) where the only
honest advice is the advice already given.
*Alternative rejected:* a fifth `ProjectEntryOutcome` enumerator for
"failed, and debris remains" — it forces a fifth string on every host of a
shared mapper (`A24`) to describe a state the user cannot act on differently.
*Alternative rejected:* logging the rollback error — the editor has no logging
faculty, and inventing one here would be a genuinely new L0 seam for a
diagnostic nobody has asked for.

**`D-save_as_rollback-5`** — `remove_tree` is **idempotent on an absent path**
and **refuses an empty path**.

*Rationale:* idempotence is what lets the rollback run unconditionally on both
failure branches without first asking whether the root was ever created — and
the `fail_make_directories` case (`tests/save_as_test.cpp:265-282`) is exactly
that situation: the publish failed before creating anything, and the rollback
must be a silent no-op rather than an error. It mirrors `make_directories`'s
own *"succeeds (no error) if the directory already exists"*
(`filesystem.hpp:38-39`), so the seam's two structural operations are
contract-symmetric. The empty-path refusal mirrors
`commands::save_project_as`'s guard (`app_state.cpp:200-203`) and stops the
one input whose recursive-delete semantics are least well defined.
*Alternative rejected:* an in-primitive safety guard (refuse `/`, refuse a path
with no parent, refuse anything outside a sandbox root) — policy does not belong
in an L0 primitive; the seam's other five methods are all thin, and the
entitlement question is answered at the call site by D27's guard
(`D-save_as_rollback-2`), which is a stronger check than any path heuristic.

**`D-save_as_rollback-6`** — The outcome vocabulary and every user-visible
string are **untouched**; this leaf changes state, not words.

*Rationale:* `A25` deliberately made the messages honest one leaf ahead of the
state, and both messages remain correct after this change —
*"Could not save a copy there."* on a failed publish, and the D27 refusal
string only when the target really does belong to someone else. Keeping L3/L4
byte-identical is also what keeps this a 1d leaf: no fake re-signing sweep, no
e2e churn, and the diff is confined to `platform` + `project` + tests.
*Alternative rejected:* a "we cleaned up after ourselves" notice — it narrates
tool bookkeeping the user never needed to know about, and D25's
announce-the-loss precedent is about losses the user *incurred*, which this is
the opposite of.

**`D-save_as_rollback-7`** — `project::create_project`'s identical trap is
**deferred**, not fixed here.

*Rationale:* the primitive is the shared cost and it ships here; the second call
site is genuinely different work because two of `create_project`'s four failure
branches occur after an mmap-backed `arbc::Document` has been minted over
`workspace/` (`project_open.cpp:332-340`), so the rollback carries a
destruction-ordering constraint — destroy the document, then remove the tree —
that the pure-write Save As path does not have and that needs its own tests. It
is named as a concrete 0.5d leaf under Acceptance criteria, not left as prose.
*Alternative rejected:* fix both in this leaf — it hides the ordering hazard
inside a task scoped, titled and estimated for Save As, and the .tji note draws
the boundary explicitly.

### Doc delta (rides in the closer's commit)

**`A26`**, appended after `A25` at `docs/01-architecture.md:388`. It records the
new destructive primitive, the single call site, the ownership argument that
makes the delete safe, the three paths that must never delete, the
report-the-original-error rule, and the narrowing of `gc.md`'s *"no new
`FileSystem` primitive"* — with `D-gc-2`'s *"the library owns deletion"*
preserved for asset reclamation, which libarbc still owns. `Realized by
editor.project.save_as_rollback.` No `docs/00-design.md` row: D27 already
promises creation-not-adoption, and A26 completes the failure half of that
promise without changing any decided user-facing behaviour.

## Open questions

_None — all decided against the constitution._

D27 (`docs/00-design.md:494`) settles that the target must not pre-exist and
that the refusal leaves it untouched, which is what licenses the removal on the
failure path and forbids it on the refusal path. A25
(`docs/01-architecture.md:387`) settles that a completed copy survives a spawn
failure, which draws the rollback's upper boundary. A3
(`docs/01-architecture.md:365`) settles that new file faculties belong on the
`PlatformServices`/`FileSystem` seam rather than inline. A12/A13
(`:374`/`:375`) settle that `dock` learns nothing about `project` or `platform`,
which is why this leaf stops at L1. §8 (`:255-291`) settles that
`project → platform` is an existing edge. `D-gc-2` / `gc.md` Constraint 8 is
narrowed rather than contradicted, and `A26` records the narrowing.

**Accepted, documented, and not deferred:** the TOCTOU window between D27's
`fs.exists(target_root)` and `save_project`'s `make_directories` — a third party
creating `target_root` inside that window would have its directory removed by
the rollback. This is the same window D27's guard already has (a third party
creating the target in it would have its contents published into), it is
unclosable without an exclusive-create primitive on the seam (a strictly larger
L0 change than this leaf, for a race no user gesture can produce), and the
editor is a single-project-per-process desktop tool whose targets are typed by
the user seconds earlier. It is stated in the `save_project_as` doc comment,
not encoded as a task.

**Parking-lot item (human judgment, not a WBS task):** none.

## Status

**Done** — 2026-07-24.

- `src/platform/ace/platform/filesystem.hpp` — new `remove_tree` pure virtual appended after `atomic_replace`, carrying idempotence + empty-path contract (A26, Constraint 1).
- `src/platform/native_platform.cpp` — `NativeFileSystem::remove_tree` implementation using `std::filesystem::remove_all` with `error_code` overload; empty path → `std::errc::invalid_argument` before touching disk.
- `src/project/save.cpp` — file-local `discard_partial_target` helper + two rollback call sites in `save_project_as` (publish-failure branch and `.gitignore`-failure branch); `save_project` unchanged (Constraints 3 & 4).
- `src/project/ace/project/save.hpp` — doc-comment on `save_project_as` extended with rollback promise alongside the D27 contract (Constraint 7).
- `tests/platform_test.cpp` — L0 cases: `remove_tree {deletes a populated directory recursively, on a missing path succeeds, refuses an empty path}` + `remove_tree` prefix-erase extension to the injectable-seam case.
- `tests/save_as_test.cpp` — L1 rollback cases: `{removes partial target on publish fail, removes partial target on gitignore write fail, retried with same name after failed publish now succeeds (byte-exact golden re-render), never removes the target it refuses, reports publish fault not rollback fault when cleanup also fails}`; existing gitignore-fault `CHECK` flipped to `CHECK_FALSE` (Constraint 4 / refinement-directed).
- `tests/project_save_test.cpp` — L1 plain-Save must-not-delete regression: `save_project leaves the project directory intact when the publish fails`.
- `tests/app_project_gateway_test.cpp` — L4 retry round-trip: `AppProjectGateway::save_as retried with the same name after a publish fault publishes cleanly`; `spawns` counter + path-scoped `fail_atomic_replace_at` added to the gateway's doubles.
- `tests/project_open_test.cpp`, `tests/export_test.cpp`, `tests/contact_sheet_test.cpp` — `FileSystem` doubles re-signed with `remove_tree` override (Constraint 10).
- `docs/01-architecture.md` — A26 row: the seam's only destructive faculty, ownership argument, three must-not-delete paths, report-the-original-error rule, narrowing of `gc.md`'s "no new `FileSystem` primitive".
