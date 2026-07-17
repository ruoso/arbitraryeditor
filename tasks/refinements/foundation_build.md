# editor.foundation.build — CMake, deps, presets, CI

## TaskJuggler entry

`tasks/00-editor.tji` → `editor.foundation.build`, the first leaf. No `depends`
(it is the root). Effort 2d.

## What this task is

The bootstrap. It cannot be produced by the orchestrator loop — the loop's
verification pipeline *is* `scripts/gate`, and the gate is this leaf's
deliverable — so it was **hand-built**. It stands up the buildable, levelized,
libarbc-linked project plus the gate and CI that every later leaf is verified
against.

## What landed

- **`CMakeLists.txt`** — C++20; **libarbc** consumed via `FetchContent` as a
  subproject (`ARBC_SOURCE_DIR`, a local checkout for co-development; its own
  test suite kept out via `BUILD_TESTING OFF`, its Catch2 reused). `arbc::arbc`
  links into `project`, and the binding is proven by a test.
- **`src/` skeleton** — the 12 levelized components (base/platform/gl ·
  project/scene/interact/commands/dockmodel · render · views/dock · app), one
  static library each, wired per the §8 DAG. One real L1 function
  (`interact::brush_units`).
- **`scripts/check_levels.py`** — enforces the level DAG *and* the A8 seam: the
  L1 core carries no ImGui/GL/SDL include, and libarbc enters only through the
  components that own the document/rendering (never base/platform/gl/dockmodel).
- **`scripts/gate`** — the universal per-task gate: `check_levels` ·
  clang-format · configure · build · ctest. Green.
- **Tests** — Catch2 harness; `tests/interact_test.cpp` (L1 unit),
  `tests/binding_test.cpp` (libarbc links + `CpuBackend::make_surface`).
- **CI** — `.github/workflows/ci.yml` (lint + gcc/clang ×
  debug/release/asan/tsan + coverage/diff-cover) and `.github/act/` for the
  orchestrator's in-loop replay (validated: `act -j lint` passes).
- **`orchestrator/`** — the WBS-driving loop, its verify chain pointed at the
  editor gate and its prompts retargeted to the editor's levelization +
  layered-tests DoD.

## Not this task (→ `editor.foundation.app_shell`)

SDL3 + Dear ImGui (docking), the actual window / GL context / dockspace / main
loop, and — being the first ImGui surface — the **ImGui Test Engine** e2e
harness + the offscreen-GL headless smoke that every later view leaf's e2e uses.

## Acceptance criteria

- `scripts/gate` green (check_levels · format · build · ctest). ✅
- `check_levels` clean over the skeleton, including the seam. ✅
- libarbc links and is usable (`binding_test`). ✅
- CI valid (`act -j lint` green). ✅

## Status

**Done** — 2026-07-17. Hand-bootstrapped; the orchestrator drives from here
(`app_shell` is the next READY leaf).
