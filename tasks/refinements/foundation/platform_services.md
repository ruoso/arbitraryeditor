# editor.foundation.platform_services — PlatformServices seam (file / threads / clock)

## TaskJuggler entry

`tasks/00-editor.tji:44-48` — `task platform_services` under `editor.foundation`.
Effort `2d`, `allocate team`, `depends !app_shell`. The `note` cites **Arch A3**
and names this refinement.

## Effort estimate

**2 days** (from the `.tji`). The scope is a single L0 component fleshed out from
a stub plus its headless tests — small, self-contained, no rendering and no UI.
The estimate is dominated by getting the three faculties' error/edge behaviour
right and the sanitizer-clean thread test, not by surface area.

## Inherited dependencies

**Settled (from `editor.foundation.build`).** The 12-component levelized
skeleton exists; `src/platform/` is already a compiling stub in the `§8` DAG
(`src/platform/ace/platform/platform.hpp:1-8`, `src/platform/platform.cpp:1-8`),
built by `ace_component(platform DEPENDS base)` (`CMakeLists.txt:136`). The
levelization lint already declares this leaf's legal edges:
`scripts/check_levels.py:21-48` lists `"platform": {"base"}` in `ALLOWED` and
`platform` in **none** of the `EXTERNAL_ALLOWED` sets — so `platform` may not
include ImGui, SDL, GL, or `arbc/`. The headless Catch2 harness
(`ace_tests`, `CMakeLists.txt:169-172`, Catch2 reused from libarbc), the
`asan`/`tsan`/`coverage` presets, and `scripts/gate` are all in place.

**Settled (from `editor.foundation.app_shell`).** `app_shell` reached SDL
directly from `ace::app` for the window / input / GL context / main loop and
**explicitly deferred the richer file/thread/clock seam to this leaf**
(`tasks/refinements/editor/app_shell.md`, Constraint 3). It added no
platform/filesystem/thread code — `src/platform/` is untouched since the
skeleton. The ImGui Test Engine e2e rig and the offscreen-GL smoke exist but are
not exercised here (this leaf has no UI and no rendered output).

**Pending (this leaf owns them).** The `PlatformServices` interface (filesystem
/ thread-spawn / monotonic clock), its native implementation over the C++
standard library, and the Catch2 + sanitizer tests that pin its behaviour.

## What this task is

Turn the `src/platform/` stub into the **PlatformServices seam**: a thin,
injectable interface over three OS faculties — **directory/file access**,
**thread spawn**, and a **monotonic clock** — with a native implementation built
entirely on the C++ standard library (`<filesystem>`, `<thread>`, `<chrono>`).
This is the one seam the future web/WASM port swaps (File System Access API /
OPFS + Emscripten pthreads) without touching any app or UI code. The task
delivers the seam and its native impl plus headless tests; it does **not** wire
the seam into consumers — that happens in the leaves that need it
(`editor.project.open`, `editor.dock.workspaces`, `editor.cameras.export`), all
already in the WBS.

## Why it needs to be done

Every later leaf that touches the local filesystem, spawns an editor-owned
thread, or needs monotonic timing must go through one abstraction so the WASM
target stays reachable by construction (A3). Concretely, the seam is the L0
foundation under three pending consumers already in the DAG:
`project` (L1, `CMakeLists.txt:141` — enumerate a project directory before
handing paths to libarbc: `editor.project.open`) and `dockmodel` (L1,
`CMakeLists.txt:145` — persist local UI layout presets:
`editor.dock.workspaces`), plus the later async export path
(`editor.cameras.export`). Building it now — before any consumer — keeps those
leaves from each reaching for `std::filesystem`/`std::thread` directly and
fragmenting the WASM swap point across the codebase.

## Inputs / context

**Design docs (normative — the constitution).**

- `docs/01-architecture.md` **A3** (`:43-57`, log row `:253`) — the governing
  decision. The `PlatformServices` bullet (`:49-52`): *"file/directory access,
  threading spawn, and clock behind a thin interface. Native impl = std threads
  + filesystem; the later web impl = File System Access API / OPFS + Emscripten
  pthreads (Web Workers + SharedArrayBuffer, needing COOP/COEP on the host)."*
  The workspace bullet (`:54-57`) explicitly keeps the mmap/OPFS workspace
  backing **out** of this seam — the editor goes through libarbc's
  workspace/document API, so the workspace is a *library* port (doc 15), not a
  PlatformServices concern. `§7` (`:142`): when WASM is scoped, the Emscripten
  preset "reuses everything but `platform/`" — i.e. this directory is exactly
  the swap point.
- `docs/01-architecture.md` **A4** (`§4`, `:59-82`, log row `:254`) — the
  concurrency contract this leaf's thread-spawn faculty must **not** violate:
  single-writer/render-thread-confined tile cache, leaf-only dispatch, **one
  shared `WorkerPool`**, one `HousekeepingThread` per `Document`, all owned by
  libarbc. The editor "invents almost no new systems"; the render pool is the
  library's, not this seam's.
- `docs/01-architecture.md` **A8 / §8** (`:144-179`, log row `:258`) — the
  levelization DAG and testability seam. `platform` is **L0**, depends on `base`
  only, and is structurally forbidden from ImGui/GL/SDL. Only `project` and
  `dockmodel` list `platform` among their direct deps.
- `docs/01-architecture.md` **A9 / §9** (`:181-208`, log row `:259`) — the
  layered DoD this leaf's Acceptance criteria instantiate.
- `docs/00-design.md` **D16** (Save = a publish step, "not a crash race") —
  motivates the atomic-replace requirement on the filesystem faculty for the
  editor's own local-state writes.

**Source seams this leaf extends.**

- `src/platform/ace/platform/platform.hpp:1-8` / `src/platform/platform.cpp:1-8`
  — the bare stub (`const char* name();`) to grow into the seam.
- `scripts/check_levels.py:21-48` — `ALLOWED["platform"] = {"base"}` and the
  `EXTERNAL_ALLOWED` allow-lists (`platform` in none): the edges this leaf must
  stay inside **without editing the lint**.
- `CMakeLists.txt:118` (`ace_component()`), `:136` (the `platform` library),
  `:169-172` (the `ace_tests` headless Catch2 binary this leaf's unit test joins).

## Constraints / requirements

1. **Pure-std native impl; no new external dependency and no lint edit.** The
   native implementation uses only `<filesystem>`, `<thread>`, `<chrono>` (and
   friends). It must **not** include ImGui/SDL/GL/`arbc/` — `platform` is in no
   `EXTERNAL_ALLOWED` set, and SDL is `app`-only. `scripts/check_levels.py` must
   stay **unedited**: the `platform:{base}` edge already exists, so no new
   component and no new DAG edge is introduced. (If the implementation ever finds
   it needs an edge the lint forbids, that is a levelization change requiring an
   explicit `A<n>` delta — not expected here.)

2. **Injectable interface, not a global or a compile-time `#ifdef` switch.** The
   seam is expressed as abstract interfaces (one small interface per faculty, or
   a `PlatformServices` aggregate holding them) in `ace::platform`; the native
   impl is a concrete subclass in the same component. Consumers receive it by
   reference/pointer, injected at `app` bootstrap (L4). No `#ifdef __EMSCRIPTEN__`
   inside `platform` and no process-global singleton — the WASM port supplies a
   different concrete impl at the same seam, and tests supply a fake.

3. **Thread-spawn is a primitive, not a pool — and never usurps libarbc's
   concurrency (A4).** The faculty exposes a minimal `spawn(callable) →
   join-handle` (plus join/detach), for editor-owned auxiliary threads only
   (e.g. the later async export-with-progress). It must **not** wrap, replace, or
   duplicate libarbc's `WorkerPool`/`HousekeepingThread`, and must not touch the
   tile cache. The render worker pool stays the library's; the single-writer /
   leaf-only-dispatch rules are honoured because this seam plumbs no rendering.

4. **Filesystem is directory/file oriented and does NOT model the workspace
   mmap.** Faculties: list a directory, read a whole file, write a whole file,
   `exists`, `mkdir -p`, and an **atomic replace** (write-temp-then-rename) for
   the editor's own local-state writes (D16). Paths are `std::filesystem::path`.
   The libarbc mmap workspace backing is out of scope (A3 `:54-57`) — this seam
   fronts the editor's *own* file needs (enumerate a project dir, read/write
   layout presets, write export output), then hands paths to libarbc for the
   document/workspace itself.

5. **Clock is monotonic (steady), not wall-clock.** `now() → duration` off
   `std::chrono::steady_clock`, for frame pacing, gesture-coalescing windows, and
   timeouts. Calendar/wall-clock time (e.g. a file's mtime) belongs to the
   filesystem faculty's metadata, not this clock — exposing a non-monotonic
   `system_clock` here would invite time-goes-backwards bugs.

6. **Errors are surfaced, not thrown across the seam blindly.** File operations
   that can fail (missing file, permission, partial write) return a typed
   result/`std::error_code`-style outcome the L1 consumers can branch on, so a
   headless test can assert the failure path without relying on exception
   escape. (Interface choice — `expected`-like return vs. documented throwing —
   is fixed in Decisions.)

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate`
green is the umbrella. Because this leaf is a headless L0 infrastructure seam
with no UI and no rendered output, the golden and ImGui-Test-Engine layers are
**N/A** (stated below so their absence is a decision, not an omission).

- **Levelization (`check_levels` clean).** `python3 scripts/check_levels.py`
  passes **with no edit** to the script; the new `platform` sources include only
  `<ace/base/...>` and standard-library headers — no ImGui/SDL/GL/`arbc/`. This
  is the primary structural assertion for the leaf.
- **L0/L1 logic — Catch2 units (the bulk).** A new headless `tests/platform_test.cpp`
  joined to the existing `ace_tests` binary (`CMakeLists.txt:169-172`, adding
  `ace::platform` to its link libs), registered via the existing `add_test`.
  Cases:
  - *Filesystem:* write→read round-trip in a Catch2 temp directory; `exists`
    true/false; `mkdir -p` idempotence; directory listing returns the expected
    set; **atomic-replace** leaves either the old or the new content on an
    interrupted/failed write — never a truncated file; reading a missing file
    yields the typed error, not a crash.
  - *Clock:* two successive `now()` reads are non-decreasing (monotonic).
  - *Thread-spawn:* spawn N workers incrementing a shared `std::atomic`, join
    all, assert the sum — and a fakeability check proving a consumer can inject
    an alternate impl (an inline in-test fake filesystem satisfies the
    interface).
- **Threading (ASan/TSan) — scoped here explicitly.** This is the first
  editor-owned thread surface, so the thread-spawn test is the leaf's designated
  sanitizer target: `ace_tests` runs clean under the `asan` and `tsan` presets
  (concurrent spawn/join over a shared atomic, no data race, no leak). libarbc's
  `WorkerPool` concurrency remains scoped to `editor.canvas.frame_sync`, not
  this leaf.
- **Rendered output — N/A.** No canvas composition or export here, so no
  libarbc `render_offline` golden.
- **UI e2e — N/A.** No ImGui surface, so no Dear ImGui Test Engine e2e and no
  screenshot baseline.
- **Coverage.** ≥90% diff coverage on the changed lines (`diff-cover
  --fail-under=90` under the `coverage` preset); the error/edge branches
  (missing file, failed atomic replace) are covered, not just the happy path.
- **Format/build.** `clang-format --dry-run --Werror` clean; the `dev` and
  `release` presets build; `scripts/gate` green.

No follow-up work is deferred: the three consumers that wire this seam
(`editor.project.open`, `editor.dock.workspaces`, `editor.cameras.export`)
already exist as WBS leaves and each consumes the seam as part of its own scope.
If a consumer later needs a *reusable* in-memory filesystem fake (beyond the
inline test fake here), that consumer's task promotes it — it is test support,
not a standalone leaf, so no new WBS task is registered.

## Decisions

- **D-platform_services-1 — Scope the seam to exactly the three A3 faculties:
  filesystem, thread-spawn, monotonic clock. Nothing more.** A3 (`:49-52`) names
  precisely these; env/config/logging/network stay out. Rationale: the seam has
  one or two call sites per faculty today, and every faculty added is more to
  re-implement for the WASM port. *Alternative rejected:* a kitchen-sink "OS
  services" facade — YAGNI, and a wider surface makes the OPFS/pthreads swap
  strictly larger for no present benefit.

- **D-platform_services-2 — Express the seam as injectable interfaces + a native
  concrete impl in `ace::platform`, wired at `app` bootstrap; no singleton, no
  `#ifdef` switch.** Rationale: injection is what makes the L1 consumers
  headless-testable (a fake filesystem, no disk), keeps `platform` free of
  platform macros, and gives the web port a single concrete class to substitute
  at the same seam. *Alternatives rejected:* (a) free functions with compile-time
  `#ifdef __EMSCRIPTEN__` switching — not injectable, so consumer tests can't
  supply a fake, and it couples L0 to Emscripten macros; (b) a process-global
  singleton — hidden dependency that blocks parallel tests using different fakes.

- **D-platform_services-3 — Thread-spawn is a bare primitive that explicitly
  does not own or wrap libarbc's `WorkerPool`/`HousekeepingThread` (A4).**
  Rationale: A4 (`:59-82`, log row `:254`) says the editor adopts the library's
  concurrency verbatim; the render pool and per-`Document` housekeeping are the
  library's. This primitive exists only for editor-owned auxiliary threads and to
  give the WASM port one place to map Emscripten pthreads. *Alternative
  rejected:* building an editor-side executor/thread-pool — it would duplicate
  the library's `WorkerPool` and risk breaking the single-writer /
  leaf-only-dispatch contract.

- **D-platform_services-4 — The filesystem faculty fronts the editor's own file
  needs only; it does not model the libarbc mmap workspace.** Rationale: A3
  (`:54-57`) routes the workspace backing through libarbc's document/workspace
  API (a doc-15 *library* port); a VFS that also fronted the mmap would duplicate
  the library and force the OPFS port to reconcile two abstractions.
  *Alternative rejected:* a unified VFS over both editor files and the workspace
  — overlaps a library responsibility and widens the swap point.

- **D-platform_services-5 — The clock is a monotonic steady clock, not
  wall-clock.** Native = `std::chrono::steady_clock`. Rationale: frame pacing,
  undo gesture-coalescing windows, and timeouts need monotonicity; calendar time
  (file mtime) is filesystem metadata, obtained where a real timestamp is needed.
  *Alternative rejected:* exposing `system_clock` from this faculty — invites
  time-goes-backwards bugs across NTP steps / DST for no current caller.

- **D-platform_services-6 — File operations that can fail return a typed
  outcome (an `expected`-like value / `std::error_code`), not an unconditional
  throw across the seam.** Rationale: L1 consumers (open, save, workspaces) must
  branch on "missing file" / "write failed" as ordinary control flow, and the
  headless tests assert those branches deterministically. *Alternative rejected:*
  throwing exceptions for expected filesystem failures — turns a routine
  branch into stack-unwinding and makes the failure path awkward to test.

## Open questions

- _None._ A3 fully specifies the seam's three faculties and the native/web
  implementations; A4 fixes the concurrency boundary the thread primitive must
  respect; the levelization edges already exist in the lint. Every design choice
  above is settled against A3/A4/A8 with no doc delta required — no new
  dependency, no new component, no new DAG edge.

## Status

**Done** — 2026-07-17.

- `src/platform/ace/platform/result.hpp` — `Result<T>` typed-outcome (C++20 stand-in for `expected`).
- `src/platform/ace/platform/clock.hpp` — `Clock` abstract interface + `NativeClock` (`std::chrono::steady_clock`).
- `src/platform/ace/platform/threads.hpp` — `Threads`/`JoinHandle` abstract interfaces + `NativeThreads` (bare `std::thread` spawn primitive).
- `src/platform/ace/platform/filesystem.hpp` — `FileSystem` abstract interface + `NativeFileSystem` (`std::filesystem`).
- `src/platform/ace/platform/platform_services.hpp` — `PlatformServices` aggregate interface + `NativePlatformServices`.
- `src/platform/native_platform.cpp` — all native impls (std filesystem/thread/steady_clock).
- `tests/platform_test.cpp` — Catch2 headless unit suite: filesystem round-trip, `exists`, `mkdir -p` idempotence, directory listing, missing-file typed error, `write_file` error branch, atomic-replace publish + failure-preserves-old, monotonic clock, thread-spawn/join over shared atomic (ASan/TSan target), detach, and inline-fake fakeability check.
- `CMakeLists.txt` — `ace::platform` links `Threads::Threads`; `ace_tests` gains `tests/platform_test.cpp` + `ace::platform` link.
