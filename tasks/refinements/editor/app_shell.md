# editor.foundation.app_shell — SDL3 window + GL context + ImGui + main loop

## TaskJuggler entry

`tasks/00-editor.tji` → `editor.foundation.app_shell` (`tasks/00-editor.tji:37-42`).
`depends !build` (the sibling `editor.foundation.build`, the hand-built scaffold).
Downstream: `editor.foundation.platform_services` and
`editor.foundation.render_probe` both `depends !app_shell`
(`tasks/00-editor.tji:46,52`), and `editor.dock.dockspace`
`depends editor.foundation.app_shell` (`tasks/00-editor.tji:62`).

## Effort estimate

**2d** (`tasks/00-editor.tji:38`).

## Inherited dependencies

**Settled (from `editor.foundation.build` — `tasks/refinements/foundation_build.md`):**

- The buildable, levelized 12-component `src/` skeleton, one static library per
  component (`ace::<name>`), wired by the `ace_component()` glob function in the
  top-level `CMakeLists.txt:44-78`. All headers are stubs exposing
  `const char* name();`.
- libarbc consumed via `FetchContent` (`CMakeLists.txt:19-38`); `arbc::arbc`
  links into `project`; binding proven by `tests/binding_test.cpp`.
- `scripts/check_levels.py` enforcing the §8 DAG **and** the A8 seam — the L1
  core (`project/scene/interact/commands/dockmodel`) may not include
  ImGui/GL/SDL; the external allow-lists already grant `imgui`→{views,dock,app},
  `sdl`→{app}, `gl_api`→{gl,render,views,dock,app}
  (`scripts/check_levels.py:42-48`).
- `scripts/gate` — the universal per-task gate (check_levels · clang-format ·
  configure · build · ctest) (`scripts/gate:17-37`).
- `CMakePresets.json` — `dev`/`release`/`asan`/`tsan`/`coverage`
  (`CMakePresets.json:4-56`).
- Catch2 harness (reused from libarbc, `Catch2::Catch2WithMain`); `ace_tests`
  executable + `add_test` in `CMakeLists.txt:81-85`.
- CI (`.github/workflows/ci.yml`): `lint` + `build-test` matrix
  (gcc/clang × dev/release/asan/tsan, all run `ctest --preset`) + `coverage`
  with a `--fail-under=90` diff-coverage gate.

**Pending (this leaf owns them — foundation.build explicitly deferred them,
`tasks/refinements/foundation_build.md:40-44`):** SDL3 + Dear ImGui (docking),
the real window / GL context / main loop, and — being the first ImGui surface —
the ImGui Test Engine e2e harness + the offscreen-GL headless smoke.

## What this task is

Stand up the actual application shell. `FetchContent` SDL3 and Dear ImGui
(docking branch); open a window; create an OpenGL context targeting the
GLES3 / WebGL2 common subset; initialize Dear ImGui with the SDL3 + OpenGL3
backends; run the frame loop (poll SDL events → `NewFrame` → build UI →
`Render` → swap). The UI is **self-rendered** — no native widgets — so the WASM
target stays reachable by construction (A2/A3, D18). Today the shell shows only
ImGui chrome (a dockspace-less window with a placeholder pane); rendering a real
`Document` is `render_probe`, and the split-tree dockspace is
`editor.dock.dockspace`.

Because this is the **first ImGui surface**, it also delivers the test rig every
later view leaf inherits: the **Dear ImGui Test Engine**
(`ocornut/imgui_test_engine`) e2e harness that drives the shell headless by
widget id, plus the **offscreen-GL headless smoke** ("init + N frames +
shutdown" against software GL). Landing that rig here is the reason the leaf
exists as much as the window is.

## Why it needs to be done

`editor.foundation.build` proved the component graph links but left `main()` a
one-line stub (`src/app/main.cpp:8-10`, prints
`"app_shell pending"`). Nothing renders and nothing drives a UI. Every
capability leaf downstream is a view or an interaction inside this window, and
every one of them owes an ImGui Test Engine e2e per the universal DoD
(`docs/01-architecture.md` §9) — so the harness must exist before the second UI
leaf. `platform_services` and `render_probe` build directly on the running
shell; `dockspace` splits it into the D18 dockspace. This leaf is the gate that
turns the scaffold into a program you can see and a UI you can test.

## Inputs / context

**Design docs (normative — the constitution):**

- `docs/01-architecture.md` **A2** (`:26-27`, log row `:215`): "The UI is
  **self-rendered** on **Dear ImGui (docking branch) + SDL3 + OpenGL**,
  rendering to the **GLES3 / WebGL2 common subset**." This settles the framework
  choice; the stale "Platform & framework … (open)" bullet in `docs/00-design.md`
  (`:491-492`) is superseded by A2 — no new decision is required here.
- `docs/01-architecture.md` **A3** (`:43-57`, log row `:216`): the
  don't-block-WASM seams — self-rendered UI, platform behind SDL (window, input,
  GL context, main loop; `:47-48`), the GLES3/WebGL2 render subset (`:53`).
- `docs/01-architecture.md` **A8** (`:177-179`, log row `:221`): only L3
  (views/dock) and L4 (app) see ImGui; the L1 core stays UI-agnostic —
  `check_levels`-enforced.
- `docs/01-architecture.md` **A9 / §9** (`:181-208`, log row `:222`): the
  layered DoD. The table (`:185-190`) names the four layers; `:196-197` states
  explicitly that **`app_shell` (the first ImGui surface) adds the ImGui Test
  Engine e2e harness + the offscreen-GL smoke.** §9 also flagged an open item —
  the ImGui Test Engine license — now **resolved** as decision **A10** (free
  under the OSI open-source carve-out; see Decisions/Open questions).
- `docs/01-architecture.md` **§7** (`:112-142`): `src/app/ main.cpp` is L4 —
  "bootstrap · main loop · wiring (SDL+GL+ImGui)" (`:134,153`). `src/gl/` is the
  L0 "GLES3/WebGL2 abstraction" (`:126`).
- `docs/00-design.md` **D18** (`:479`): the uniform dockspace paradigm the shell
  will grow into (drawn out at `dockspace`); **D19** (`:480`): process-per-project
  (one window = one project) — the shell owns one window for its lifetime.

**Source seams this leaf extends:**

- `src/app/main.cpp:1-11` — the thin stub `main()` to be replaced by the real
  shell entrypoint.
- `CMakeLists.txt:44-78` — the `ace_component()` function and component wiring;
  the SDL3 + ImGui + ImGui Test Engine `FetchContent` blocks and the `app`-lib
  split land here. Comments at `:42-43,72` already flag "SDL3 + Dear ImGui arrive
  with the window/dockspace work below."
- `scripts/check_levels.py:42-54` — the external allow-lists and regexes;
  `app`/`views`/`dock` are already permitted to include imgui/sdl/gl, so **no
  edit to `check_levels.py` is expected** (assert this).
- `.github/workflows/ci.yml` (`build-test` matrix, `:24-51`) — every lane runs
  `ctest --preset`, so a new e2e/smoke `add_test` flows into CI automatically;
  the lanes need software-GL packages installed to run the offscreen smoke.
- `tests/interact_test.cpp`, `tests/binding_test.cpp` — the existing Catch2
  pattern; the e2e/smoke test executables slot alongside them.

## Constraints / requirements

1. **Self-rendered only.** No native/OS widgets — all UI is ImGui draw data on
   the GL surface (A2/A3, D18). No dialog boxes, menus, or file pickers via the
   OS toolkit.
2. **GLES3 / WebGL2 common subset.** Request an ES3-capable context and restrict
   GL usage to the ES3/WebGL2 intersection — no desktop-GL-only calls — so the
   same GL path runs native and (later) on WebGL2 (A2 `:27`, A3 `:53`). Configure
   ImGui's OpenGL3 backend for the ES3 GLSL path
   (`IMGUI_IMPL_OPENGL_ES3`). Route raw GL through `ace::gl` (L0) where the shell
   touches it directly; ImGui owns its own backend GL.
3. **Platform behind SDL.** Window creation, input, GL-context creation, and the
   main loop go through SDL3 (A3 `:47-48`). Do **not** hand-roll platform code in
   the L1 core; the richer file/thread/clock seam is `platform_services` (the
   next leaf) — this leaf uses SDL directly from `app`.
4. **Levelization unchanged.** No new component and no new dependency edge. SDL3
   + ImGui + ImGui Test Engine includes appear only in `app` (and, for ImGui,
   `views`/`dock` when they grow) — all already allowed by
   `check_levels.py:42-48`. The L1 core gains nothing. `check_levels.py`'s
   `ALLOWED`/`EXTERNAL_ALLOWED` dicts require **no modification**; if the
   implementer finds they must change either, that is a levelization change and
   must be justified in the Status block.
5. **Testable-headless seam.** The shell must be drivable without a visible
   window: factor the shell logic (window/context/ImGui setup, one-frame step,
   shutdown) out of `main()` into a small **`ace::app` static library** (glob
   `src/app/*.cpp` **excluding** `main.cpp`, which stays a thin driver linking
   `ace::app`). Both the ImGui Test Engine e2e and the offscreen smoke link
   `ace::app` and drive `init → run N frames → shutdown`. Context creation must
   accept an offscreen/headless mode (SDL offscreen video driver + software GL)
   so CI with no display can run it. `app` is L4 and already sees SDL/GL/ImGui —
   this split stays within the DAG.
6. **License gate for the Test Engine — resolved (A10).** The e2e harness pulls
   `ocornut/imgui_test_engine`, whose `imgui_test_engine/` folder is under the
   *Dear ImGui Test Engine License* (rest MIT). That license is **permissive,
   not copyleft** — it does not infect the editor's source — and its free tier
   explicitly covers software released under an OSI-approved open-source license
   (and any <$2M-turnover entity). So linking it into the shipped binary is
   within the free terms; no test-only-link split is needed. Revisit only if the
   editor is distributed closed-source by a >$2M-turnover entity. See decision
   **A10** (`docs/01-architecture.md`).

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9) for this
specific leaf. `scripts/gate` green is the umbrella; the named tests below are
what makes it meaningful here.

- **Levelization (`check_levels` clean).** `python3 scripts/check_levels.py`
  passes unchanged — no new component, no new edge, no ImGui/GL/SDL include leaks
  into the L1 core. The `app`-library split stays inside `src/app/`. **No edit to
  `scripts/check_levels.py`.**
- **e2e — ImGui Test Engine (the central deliverable).** A new headless e2e test
  executable links `ace::app` + `imgui_test_engine`, boots the shell against the
  SDL offscreen video driver + software GL, pumps N frames, and — driving by
  widget id — asserts the shell's placeholder window/pane exists and is
  interactable, then captures a **screenshot baseline** of the empty shell. This
  is the rig every later view leaf's e2e reuses; register it via `add_test` so
  every `ctest --preset` lane runs it.
- **Rendered-output baseline.** The "rendered output" DoD layer is instantiated
  here by the **Test Engine screenshot baseline** of the shell chrome — **not** a
  libarbc `render_offline` golden, because this leaf composits no `Document`.
  `render_offline` byte-exact goldens begin at `render_probe`
  (`tasks/00-editor.tji:49-53`); state this explicitly so the omission reads as
  scoped, not skipped.
- **Offscreen-GL headless smoke.** A minimal "init + N frames + shutdown" smoke
  (SDL offscreen driver + Mesa llvmpipe / EGL-surfaceless, per §9 `:190`),
  registered as a `ctest` test, that runs clean in CI with no GPU/display. The
  CI `build-test` lanes install the software-GL runtime (`.github/workflows/ci.yml`
  gains the Mesa/EGL apt packages — the implementer edits ci.yml; it is outside
  this refinement's write scope).
- **L1 Catch2 units.** This leaf adds no L1 core logic — its work is L4 GL/UI
  glue — so its Catch2 contribution is thin by nature (unlike the L1-heavy leaves
  §9 `:187` targets). Any pure, UI-agnostic helper the shell factors out (e.g. a
  frame-loop lifecycle predicate) gets a Catch2 unit; do not manufacture L1 tests
  for L4 glue. Honesty about the layer that carries this leaf's coverage (e2e +
  smoke) is the requirement.
- **Threading (ASan/TSan).** The main loop is single-threaded — the UI↔driver /
  multi-canvas threading arrives at `frame_sync`/`multi_canvas`. So the TSan
  scope here is: the offscreen smoke + e2e run **clean under the existing `tsan`
  lane** (init + N frames + shutdown introduces no data race), and clean under
  `asan` (no leaks in editor code; third-party SDL/GL/ImGui driver leaks
  suppressed via a checked-in suppressions file if they surface). Real
  concurrency TSan coverage is scoped at `frame_sync`.
- **Coverage.** ≥90% diff coverage on changed lines (the CI `coverage` lane's
  `diff-cover --fail-under=90`); the e2e + smoke + any L1 helper ship with the
  task.
- **Format/build.** `clang-format --dry-run --Werror` clean; `dev`/`release`
  build clean; `scripts/gate` green end to end.

No new WBS follow-up leaves are deferred: the downstream consumers
(`platform_services`, `render_probe`, `dockspace`) already exist in the WBS and
depend on this leaf. The Test Engine license — the one item this leaf surfaced
for human judgment — is now **resolved** (decision A10); nothing remains open.

## Decisions

- **D-app_shell-1 — No doc delta; A2 already decides the framework.** SDL3 + Dear
  ImGui (docking) + OpenGL GLES3/WebGL2 subset is fixed by A2
  (`docs/01-architecture.md:26-27,215`) and A9 already mandates the ImGui Test
  Engine (`:222`). The stale "Platform & framework (open)" bullet in
  `docs/00-design.md:491-492` predates A2 and is informationally superseded, not
  a live gap. *Alternative rejected:* adding a `D20` row to close the open bullet
  — declined because the architecture constitution (the authority for
  structure/build) already decides it, and duplicating the decision in the UI doc
  invites drift. Left as a note here rather than a doc edit to keep scope tight.
- **D-app_shell-2 — Split the shell into an `ace::app` static library; `main.cpp`
  stays a thin driver.** Required by the testable-headless DoD: the e2e and smoke
  must link and drive the shell, and they cannot link an executable that owns
  `main()`. `app` is L4 and already permitted SDL/GL/ImGui, so the split changes
  no level and no `check_levels` rule. *Alternatives rejected:* (a) leave all
  shell logic in `main.cpp` — untestable headless, fails A9; (b) add a new
  `shell` component — a needless 13th node that would expand the §8 DAG and the
  `check_levels` `ALLOWED` dict for no benefit, violating "simpler abstraction
  with one or two call sites."
- **D-app_shell-3 — Headless mechanism: SDL offscreen video driver + software GL
  (Mesa llvmpipe / EGL-surfaceless).** §9 (`:190`) lists "SDL dummy /
  EGL-surfaceless / llvmpipe"; the SDL offscreen driver + llvmpipe is the most
  portable and keeps a single code path from the windowed shell (only the SDL
  driver hint changes), and it is the closest analog to the eventual
  Emscripten/headless story. *Alternatives rejected:* a real X/Wayland display in
  CI (heavier, flakier, GPU-dependent); mocking ImGui's backend (would test a
  fake, not the shell).
- **D-app_shell-4 — This leaf's "rendered output" DoD is a Test Engine screenshot
  baseline, not a `render_offline` golden.** Nothing is composited through
  libarbc yet, so a byte-exact `render_offline` golden has no subject. The shell
  chrome's screenshot baseline is the honest instantiation; `render_offline`
  goldens start at `render_probe`. Recorded so a reviewer reads the absence as
  scoped.
- **D-app_shell-5 — Resolve the Test Engine license by adoption; flag commercial
  distribution for human review.** A9/§9 already chose the tool; wiring it for
  headless testing is within its free terms and satisfies the constitution.
  Whether shipping the editor commercially later needs a paid license is a legal
  call a WBS implementer cannot make — surfaced to the parking lot. *Alternative
  rejected:* a WBS "audit the Test Engine license" leaf — forbidden by the
  refinement policy (an audit/decide-later task can't be closed by an
  implementer; it self-perpetuates).
  - **Resolution (2026-07-17, decision A10):** the parking-lot review concluded
    the license is **permissive, not copyleft** (it does not infect the editor)
    and its free tier explicitly covers OSI open-source distribution. As long as
    the editor ships open-source, linking the engine into the shipped binary is
    within the free terms — no paid license, no test-only-link split. Revisit
    only if distributed closed-source by a >$2M-turnover entity. Parking-lot
    entry removed.

## Open questions

- _None._ The one item this leaf raised — **ImGui Test Engine commercial-license
  acceptance** — was resolved by the parking-lot review as decision **A10**
  (permissive/non-copyleft, free under the OSI open-source carve-out; revisit
  only if distributed closed-source by a >$2M-turnover entity). See
  D-app_shell-5 Resolution.

## Status

**Done** — 2026-07-17.

- **Shell factored into an `ace::app` static library** (`src/app/shell.cpp`,
  `src/app/ace/app/shell.hpp`) with `main.cpp` reduced to a one-line driver over
  `ace::app::run_editor` (`src/app/main.cpp`) — the testable-headless split
  (D-app_shell-2). `Shell::init/new_frame/draw_ui/render/shutdown` open an SDL3
  window, create a GLES3 context, and init Dear ImGui's SDL3 + OpenGL3(ES3)
  backends; `draw_ui()` shows only the placeholder pane (dockspace/Document are
  later leaves).
- **SDL3 + Dear ImGui (docking) + ImGui Test Engine wired via `FetchContent`**
  and built as plain static libs (`CMakeLists.txt`): SDL built static with
  audio/camera off (dodges the PipeWire-header break under GCC 14), ImGui with
  `IMGUI_IMPL_OPENGL_ES3` + `IMGUI_ENABLE_TEST_ENGINE`, the engine with the
  std::thread coroutine impl. Test Engine linked into the shipped binary — within
  its free terms (A10, D-app_shell-5); no test-only split.
- **`ace::gl` gained the GLES3/WebGL2-subset draw calls the shell issues**
  (`set_viewport`/`clear`, `src/gl/gl.cpp` + header) behind the L0 GL seam; ImGui
  owns its own backend GL.
- **Test rig every later view leaf inherits** (`docs/01-architecture.md` §9):
  the pure frame-loop predicate as a Catch2 unit (`tests/app_loop_test.cpp`), the
  offscreen "init + N frames + shutdown" smoke (`tests/shell_smoke_test.cpp`),
  and the **ImGui Test Engine e2e** that drives the placeholder pane by widget id
  and captures a screenshot baseline (`tests/shell_e2e_test.cpp`). All three run
  headless against the SDL offscreen driver + Mesa llvmpipe, registered as one
  `ace_shell_test` ctest with the software-GL path pinned via test ENVIRONMENT.
- **CI stood up the offscreen software-GL lanes** (`.github/workflows/ci.yml`,
  `.github/act/runner.Dockerfile`): the Mesa/EGL/GLES apt packages, and the
  toolchain the `asan` lane needs — **clang 20** + `libclang-rt-20-dev` (noble's
  clang 18 ships no static ASan/UBSan runtime) and `llvm-symbolizer`. The
  third-party Mesa driver leaks LSan reports (EGL `_EGLDisplay` + llvmpipe
  first-draw state — not editor bugs; `Shell::shutdown()` tears down fully) are
  suppressed via the checked-in `tests/lsan.supp` (anticipated by the
  Threading/ASan acceptance criterion), wired through `LSAN_OPTIONS`. The local
  `act` runner also caps `nofile` so the symbolizer fork doesn't spin
  (`orchestrator/driver.py`). Documented in `docs/01-architecture.md` §9.1.
- **Levelization unchanged** — no new component, no new edge, no
  `scripts/check_levels.py` edit; the `ace::app` split stays inside `src/app/`
  and `app` already saw SDL/GL/ImGui under the A8 seam.
- **Verification:** `scripts/gate` green; the full local CI replay (`ci.yml` via
  `act`) — lint, gcc/clang × debug/release, `clang-asan`, `gcc-tsan`, and
  `coverage` with its diff-coverage gate — green.
