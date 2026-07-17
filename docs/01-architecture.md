# Arbitrary Composer Editor — Architecture

> Companion to `00-design.md` (the UI/UX design, D1–D18). This doc covers *how
> the editor is built*: language, toolkit, threading, and the project layout.
> Status: **architecture decided (A1–A6); not yet scaffolded.**

## 1. The one idea

The editor **invents almost no new systems** — it plumbs `libarbc`'s existing
concurrency and data model into the dockspace. The library already owns the hard
parts: a shared, versioned `Document` (doc 14), transactional edits with undo, an
off-thread interactive renderer, a **single-writer** tile cache, a shared
`WorkerPool`, and a checkpointed mmap workspace (doc 15). The editor is a **host**
that wires those to windows and input — not a re-implementation of any of them.

## 2. Binding — native C++, linked directly

**A1.** The editor is a **native C++20 application that links `arbc::arbc`
directly** (`find_package(arbc CONFIG REQUIRED)`, as `examples/host-interactive/`
does). No FFI, no language seam: a canvas holds real `HostViewport` / `Document` /
`Backend` objects and shares memory with the renderer. For a compositor moving
tiles every frame, any cross-language boundary would sit exactly on the hot path.

## 3. Toolkit — Dear ImGui + SDL3 + OpenGL (GLES3/WebGL2 subset)

**A2.** The UI is **self-rendered** on **Dear ImGui (docking branch) + SDL3 +
OpenGL**, rendering to the **GLES3 / WebGL2 common subset**.

*Why:* the driving constraint is **native now, self-rendered WASM later, one
codebase** (whole UI drawn to a GPU canvas, identical native and in a WebGL
canvas). That rules out DOM/hybrid (wrong kind of web) and native-widget
toolkits, and favors a self-rendering GPU UI that compiles to both via Emscripten.
Among those, ImGui+SDL is the proven path: its **docking branch is the D18 model**
(split-tree, tabs, drag-dock) out of the box, multi-canvas tiles-as-textures is
trivial, SDL gives native + Emscripten platform layers from one source, and the
dependency is lean and vendorable. Slint (retained, also web-capable) and
Qt-for-WebAssembly were weighed; Slint adds unknowns (newer, Rust core, custom
canvas + docking to verify), Qt-WASM adds weight (binary size, SharedArrayBuffer
threading friction that bites a thread-heavy library).

*WASM is not scoped now* — the choice only keeps it **reachable by construction**.

**A3. Don't-block-WASM seams (cost nothing today).** Native-first, but structure
so the web port is swap-in implementations, never a rewrite:

- **Self-rendered UI** — no native widgets; the whole editor is ImGui draw data.
- **Platform behind SDL** — window, input, GL context, main loop; SDL has native
  and Emscripten backends.
- **`PlatformServices` interface** — file/directory access, threading spawn, and
  clock behind a thin interface. Native impl = std threads + filesystem; the
  later web impl = **File System Access API / OPFS** + Emscripten **pthreads**
  (Web Workers + `SharedArrayBuffer`, needing COOP/COEP on the host).
- **GLES3/WebGL2 render subset** — the same GL code runs native and on WebGL2.
- **Workspace through the library abstraction** — the editor never assumes real
  `mmap`; it goes through `libarbc`'s workspace/document API, so the browser
  workspace backing (OPFS/in-memory arena) is a *library* port (doc 15), not an
  editor concern.

## 4. Threading & data flow — obey the library's contract

**A4.** The editor adopts `libarbc`'s concurrency rules verbatim (docs 02/14/15):
the tile cache is **single-writer / render-thread-confined**, worker dispatch is
**leaf-only**, there is **one shared `WorkerPool`**, and **one `HousekeepingThread`
per `Document`** checkpoints the workspace. Threads:

```
  UI thread            input · dockspace layout · paint frames · SUBMIT edits
      │  (never touches the cache directly)
      ▼
  Transaction ─▶ Document (writer)  ──────────────▶ damage
      │  undo/redo = doc-14 journal, free                 │
      ▼                                                   ▼
  per-canvas driver   HostViewport + InteractiveRenderer  (own cache, replans)
      │  leaf renders                                     │
      ▼                                                   ▼
  shared WorkerPool ─▶ tiles ─▶ (that canvas's) cache ─▶ frame ─▶ GL texture ─▶ screen
      ▲
  HousekeepingThread  checkpoints the mmap workspace
```

Edits flow UI → writer → damage → renderers; frames flow renderers → UI. The UI
thread stays responsive because rendering is never on it.

## 5. Multi-canvas — N observers, one document

**A5.** A canvas view is **one `HostViewport` + `InteractiveRenderer` over the
shared `Document`**; multiple canvases are **multiple renderers sharing one
`WorkerPool`** (`runtime.shared_worker_pool` is exactly this). Each cache stays
render-thread-confined, so multi-canvas needs **no new locking** — "paint through
Viewport ‖ look through Hero" (D18) is two renderers, two cameras, one document.
Selection and the shared panels are **project-level**, not per-canvas (D19): a
canvas is *only* a camera and carries no selection or inspection state.

The app is **single-project per process** (D19): opening another project **execs
a new instance**, so there is no multi-document state to manage — the app owns
exactly one `Document` for its lifetime, and the GC root-set is trivially that
document. (WASM analog: a project is a tab/instance.)

## 6. Display path & backend

**A6.** `CpuBackend` yields CPU tile surfaces; the canvas view **uploads them as
GL textures** (GLES3/WebGL2) and composites to the pane. A **GPU `Backend`** later
(rendering straight into textures) is behind the `Backend` seam — **no editor
change** (the display path already speaks textures). This is the doc-09 promise
the editor gets for free.

Project I/O and plugins are thin wiring: open a project **directory** → the
library maps `workspace/`; Save → serialize to `project.arbc` + `assets/`; "Clean
up" → `gc_project_directory` (D16); plugin kinds load via the `Registry` seam and
populate "insert cell."

## 7. Project layout & components

Directory-per-component under `src/`, one dir per levelized component (§8), so
the level DAG is enforceable by path:

```
arbitraryeditor/
├── CMakeLists.txt      FetchContent libarbc (git ref) + Dear ImGui (docking) + SDL3
├── scripts/gate        the universal per-task gate (levels · format · build · ctest)
├── docs/               00-design.md · 01-architecture.md · mockup.html
├── src/
│   ├── base/           value types, ids, small helpers            (L0)
│   ├── platform/       PlatformServices seam + native impl        (L0)
│   ├── gl/             GLES3/WebGL2 abstraction                   (L0)
│   ├── project/        libarbc Document, project-dir open/save/gc (L1)
│   ├── scene/          cells · cameras · selection · z-order      (L1)
│   ├── interact/       hit-test · gizmo · snapping · brush math   (L1, UI-agnostic)
│   ├── commands/       actions → libarbc transactions · undo      (L1)
│   ├── dockmodel/      view registry + layout data                (L1)
│   ├── render/         HostViewport/InteractiveRenderer · tile→GL (L2)
│   ├── views/          ImGui panels (canvas/layers/inspector/…)   (L3)
│   ├── dock/           dockspace shell + tool rail (ImGui docking)(L3)
│   └── app/ main.cpp   bootstrap · main loop · wiring             (L4)
├── tests/              Catch2 (L1 unit) · goldens · ImGui Test Engine e2e
└── assets/             icons, fonts (bundled)
```

Build: native desktop (Linux/macOS/Windows) first; `libarbc` arrives via
FetchContent from a git ref (a local checkout during co-development, a released
tag once its editor-facing API stabilizes). An Emscripten preset is added when
WASM is scoped, reusing everything but `platform/` and the host page (COOP/COEP).

## 8. Components & levelization

The editor is **levelized** — its own component DAG (not the library's kernel
L0–L6) — and levelization is the mechanism that **enforces the testability seam
(A8)**: the UI-agnostic core lives in components structurally forbidden from
including ImGui / GL / SDL, so "testable headless" is a compile-time invariant,
not a hope.

```
 L4  app · main            bootstrap · main loop · wiring (SDL+GL+ImGui)
 L3  dock · views          ImGui draw code — the ONLY layer that sees ImGui
 L2  render                HostViewport/InteractiveRenderer glue · frame-sync · tile→GL
 L1  project · scene ·     ── the UI-agnostic CORE (no ImGui/GL/SDL) ──
     interact · commands ·    unit-tested headless
     dockmodel
 L0  base · platform · gl  value types · PlatformServices seam · GL abstraction
```

| Component | L | May depend on | ImGui/GL |
|---|---|---|---|
| `base` | 0 | — | no |
| `platform` | 0 | base | no |
| `gl` | 0 | base | GL only |
| `project` | 1 | base, platform, **libarbc** | no |
| `scene` | 1 | base, project, libarbc | no |
| `interact` | 1 | base, scene, libarbc | **no** |
| `commands` | 1 | base, project, scene | no |
| `dockmodel` | 1 | base, platform | **no** |
| `render` | 2 | base, project, scene, gl, libarbc | GL, not ImGui |
| `views` | 3 | scene, interact, commands, render, dockmodel, **imgui** | yes |
| `dock` | 3 | dockmodel, views, imgui | yes |
| `app` / `main` | 4 | everything | yes |

**All of L1 is the testable core** and none of it may `#include <imgui.h>` (or
GL/SDL). Enforced by a `check_levels`-style lint over the directory-per-component
layout, adapted from the library's `scripts/check_levels.py`.

## 9. Testing & definition of done

Verification is **layered**, not one gate:

| Layer | What | How |
|---|---|---|
| L1 logic (the bulk) | app-state, project, camera/cell/brush math, selection, dock model | **Catch2** unit tests — headless, no ImGui/GL |
| Rendered output | export, canvas composition | **golden** compare, reusing `libarbc`'s byte-exact `render_offline` |
| End-to-end UI | open→dock→select→drag→paint→look-through→export | **Dear ImGui Test Engine** (`ocornut/imgui_test_engine`), headless, drives widgets by ID + screenshot capture |
| Threading & smoke | UI↔driver handoff, per-platform "init + N frames" | **ASan/TSan** + headless smoke (offscreen GL: SDL dummy / EGL-surfaceless / llvmpipe) |

**Definition of done — encoded in every task, not a separate one.** The harness
and the levelization check are **not standalone leaves**; they are **acceptance
criteria on every leaf**. `foundation.build` stands up the Catch2 harness, the
`check_levels` lint, the component skeleton, and a single `scripts/gate`;
`app_shell` (the first ImGui surface) adds the **ImGui Test Engine** e2e harness
+ the offscreen-GL smoke. So from the first task onward every leaf's definition
of done is:

- **respects the levelization DAG** (`check_levels` clean);
- **lands its tests** — L1 logic → Catch2 unit; renders → golden; has UI → an
  ImGui Test Engine e2e; threads → sanitizer-clean;
- **clang-format + build clean.**

The orchestrator runs `scripts/gate` after every task; each refinement's
Acceptance-criteria section names the *specific* tests that instantiate this DoD
for that leaf. *(Open: the ImGui Test Engine license — resolve when
`foundation.build` wires it.)*

## Decisions log

| # | Decision |
|---|---|
| A1 | Native C++20 app links `arbc::arbc` directly — no FFI. |
| A2 | Self-rendered UI on **Dear ImGui (docking) + SDL3 + OpenGL (GLES3/WebGL2 subset)**; native-first, WASM reachable by construction. |
| A3 | Don't-block-WASM seams: self-rendered UI, platform-behind-SDL, a `PlatformServices` interface (file/threads/clock), GLES3/WebGL2 subset, workspace via the library abstraction. WASM not scoped now. |
| A4 | Adopt the library's concurrency verbatim — single-writer cache, leaf-only dispatch, shared pool, per-doc housekeeping. UI thread submits edits, never touches the cache. |
| A5 | Multi-canvas = N `HostViewport`/`InteractiveRenderer` over one `Document` sharing one `WorkerPool`; no new locking. |
| A6 | Display = `CpuBackend` tiles → GL textures; a GPU `Backend` later needs no editor change (behind the `Backend` seam). |
| A7 | **Process-per-project**: one process = one project (opening a project is a new `exec`); the app owns exactly one `Document` for its lifetime — no multi-doc management, GC root-set is that document. Selection + shared panels are **project-level**; canvases are only cameras (D19). |
| A8 | **Levelized components enforce the testability seam**: the editor has its own component DAG (§8); the UI-agnostic L1 core (project/scene/interact/commands/dockmodel) is structurally forbidden from including ImGui/GL/SDL, enforced by a `check_levels` lint. Only L3 (views/dock) sees ImGui. |
| A9 | **Layered testing as per-task acceptance criteria**: Catch2 L1 units + `render_offline` goldens + **ImGui Test Engine** headless e2e + ASan/TSan. The harness + `check_levels` + `scripts/gate` are stood up by `foundation.build` and are the **definition of done on every leaf**, not standalone tasks. |

## Open / next

- **`foundation.build`** — the scaffold: `CMakeLists.txt` (FetchContent libarbc +
  ImGui + SDL3), the `src/` component skeleton (§7), the `check_levels` lint, the
  Catch2 + ImGui Test Engine harness, and `scripts/gate` — then a runnable window
  rendering a trivial `Document` (proving binding + display). This bootstraps the
  DoD every later leaf inherits (§9).
- **ImGui Test Engine license** — resolve when wiring the harness.
- **Extensibility** — runtime plugin-kind loading surfaced in "insert cell".
- **The WASM port** — deferred; the seams (A3) are what keep it a port.
