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

**A4.1 — single writer *identity*, not serialized access** (libarbc doc 15 § Thread
rules; ruoso/arbitrarycomposer#7). "Single writer" means every structural write to a
`Document` must originate from **one stable OS-thread identity** for the document's
lifetime — not merely from access an external mutex serializes. libarbc's lock-free
allocator/checkpoint path is written against one mutator (relaxed `high_water`,
non-atomic `SlabDirectory::publish`, the writer-thread checkpoint seal); a consumer
mutex only re-covers the accesses it wraps, and a checkpoint firing on a bounced
writer identity can seal the wrong chunk frontier. Debug builds bind the identity on
first write and assert it. **Consequence for the editor:** a design that writes from
two threads (e.g. UI-thread `transact` + a render-thread `settle_external_loads`
publish) must **funnel both to one writer thread via task-posting**, never take turns
under a lock. Reads stay lock-free via `pin()` and the copy-on-write content-binding
snapshot (arbc#10/#11); only *writes* are identity-bound.

**A4.1a — the identity rule is not a licence to drop the data-race guard.** A4.1
retires turn-taking as a *fix for writer identity*; it does **not** say a UI edit and
a driven render iteration may run concurrently. libarbc v0.2.0's COW covers exactly
one thing — the `d_contents` binding read — and `editor.canvas.single_writer` over-read
it into "no lock at all", which left two non-COW pieces of shared mutable state that a
commit and a `HostViewport::step()` both touch, both TSan-confirmed races on the
shipped `CanvasHost`:

- **`HostViewport::DamageAccumulator`** — a `Transaction::commit` on the UI thread
  `push_back`s into the same `std::vector<Damage>` that the render thread's `step()`
  swaps out in `drain()`. arbc calls it "single-owner, drained on the driving thread",
  i.e. it assumes writer == driver. This is a plain data race, **not** an identity
  question, and no library-side identity fix retires it.
- **`Model::set_commit_sink`** — `step()`'s `settle_external_loads` opens a
  `JournalSuspension` that rewrites the very commit-sink pointer a concurrent
  `Transaction::commit` reads (arbc#13). Live, not latent: any document with deferred
  external children settles on the render thread while the UI edits.

So `CanvasHost` holds a **writer-priority document lease** — exclusive for the length
of an `apply_edit` mutation and for the length of a `run()` iteration
(`canvas_host.cpp`). It is a *mutual-exclusion* guard only; it makes neither race
observable and makes no claim about writer identity, which stays exactly as A4.1
states (arbc#7/#13, upstream-owned). The one thing it must not repeat from the v0.1.0
`doc_mu` is starvation: a plain mutex let `run()`'s re-armed loop barge ahead of a
queued edit, freezing a streamed burst, so the render side additionally yields to any
waiting writer.

**A4.1a (amended at the v0.3.0 pin) — both races are now library-fixed; the lease
guards nothing and is retained only until the writer thread lands.** libarbc v0.3.0
(`editor.canvas.arbc_v030`) closes both items above at their real owner, the library:
`DamageAccumulator::flush()`/`drain()` each take a mutex held across a bounded append
or a swap only (`arbc/runtime/host_viewport.hpp:319-337`, arbc#13's companion), and
`HostViewport::step()` **no longer publishes off the writer thread** — it asks
`Model::on_writer_thread()` and, when the answer is no, settles nothing and reports the
owed install as `StepOutcome::external_loads_ready` (`host_viewport.cpp:164`, arbc#13).
So the lease's *justification* is retired: it is a guard over two races that no longer
exist. Its **code** is retired separately, by `editor.canvas.writer_thread`, which
removes the concurrency rather than guarding it — deleting the guard first would leave
the editor with two writer identities and no mutual exclusion, strictly worse than the
state this row describes.

The bump also changes **where an external arrival installs**, and this is now the
governing statement: a `Document`-bound `HostViewport` installs a **writer-thread
settler** at construction (`Document::set_external_load_settler`, install-counted,
released in the dtor), and the document runs it at the next `Document::begin()` —
i.e. an arrival installs on the writer thread ahead of the next edit, never on the
render thread. Ignoring `StepOutcome::external_loads_ready` therefore costs latency,
never correctness. In the editor this path is **armed but unreachable today**: nested
cells are in-document `ObjectId` children (never a serialized `params.ref`) and the
only load path passes a `FilesystemAssetSource`, which resolves inline, so
`Document::external_loads_ready()` is always zero and the settler always early-outs.
`writer_thread` is what makes it both reachable and correct. Writer *identity* is
unchanged from A4.1 — and note that `Model::on_writer_thread()` answers **true while
unbound** ("the caller would become the writer"; binding happens in `Model::Transaction`'s
ctor and `Model::navigate()`, nowhere else), so "the UI thread transacts before the
render thread ever steps" is a load-bearing ordering rule, not an incidental one.
Realized by `editor.canvas.arbc_v030`; superseded in part by `editor.canvas.writer_thread`.

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
│   ├── dockmodel/      view registry + layout + tool selection    (L1)
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
for that leaf. *(The ImGui Test Engine license is resolved — free under the OSI
open-source carve-out; see decision A10.)*

### 9.1 The offscreen software-GL ASan lane

The `clang-asan` CI lane runs the headless smoke/e2e tests (SDL3 `offscreen`
driver + Mesa `llvmpipe` software GL) under AddressSanitizer/UBSan. Three
environment requirements make it work; each masked the next when it was missing,
so they are recorded here rather than rediscovered:

1. **clang 20, not the distro clang 18.** Ubuntu noble's default clang 18 ships
   **no static ASan/UBSan runtime**, so the `asan` preset fails to *link*
   (`libclang_rt.asan*.a` not found). The clang lanes pin **clang 20** with
   `libclang-rt-20-dev` (from apt.llvm.org). This is set in *both*
   `.github/act/runner.Dockerfile` (the local `act` image) and the ci.yml
   "install clang-20" step, so GitHub and the orchestrator use the same toolchain.

2. **`llvm-symbolizer` on `PATH` in the runner image.** Without it, sanitizer
   reports are `<unknown module>` frames — undebuggable, and LSan suppressions
   (which match on function names) cannot match. The image installs `llvm-20`
   and exposes the unversioned `llvm-symbolizer` name the runtime auto-discovers.

3. **A sane `RLIMIT_NOFILE` on the `act` container.** Docker inherits the host's
   systemd `nofile` (~1e9, "infinity"). The sanitizer forks `llvm-symbolizer`,
   whose child `close()`s every fd up to that limit before `exec` — so a
   symbolizing ASan test spins through ~1e9 `close()` syscalls and looks hung.
   `orchestrator/driver.py` passes `--ulimit nofile=65536:65536` to `act`.
   GitHub-hosted runners already have a sane limit, so this is act-only.

**Mesa driver leaks are suppressed, not fixed.** LSan reports residual leaks that
are **not** editor bugs: `Shell::shutdown()` does the full teardown (ImGui
backends → ImGui context → GL context → window → `SDL_Quit`) and the tests invoke
it. The bytes are owned by the Mesa driver, which no application call can free —
`eglInitialize` caches its `_EGLDisplay` past `eglTerminate`, and `llvmpipe`
keeps first-draw JIT/rasterizer state for the process lifetime. `tests/lsan.supp`
suppresses these, scoped to the two named entry frames
(`SDL_EGL_InitializeOffscreen`, `ImGui_ImplOpenGL3_RenderDrawData`) so LSan stays
fully active for the editor's own allocations; it is wired in via `LSAN_OPTIONS`
in the ci.yml test step.

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
| A10 | **ImGui Test Engine license — resolved: free under the OSI open-source carve-out.** `ocornut/imgui_test_engine` is dual-licensed (the `imgui_test_engine/` folder under the *Dear ImGui Test Engine License*; everything else MIT). That license is **permissive, not copyleft** — it imposes no terms on our own source and does not "infect" the editor; redistribution only requires carrying its copyright + license text. Its free tier explicitly covers software *released publicly under an OSI-approved open-source license*, **and** any entity with <$2M USD annual turnover; a paid license (from DISCO HELLO, after a 45-day trial) is required only by a **>$2M-turnover entity**. So as long as the editor ships under an OSI open-source license, linking the engine into the shipped binary (`IMGUI_ENABLE_TEST_ENGINE`, current build) is within the free terms — no test-only-link split needed. **Revisit only** if the editor is later distributed **closed-source by a >$2M-turnover entity**, in which case: buy a license or move the engine to a test-only link (the hooks are inert without an engine context). Supersedes the parking-lot entry (was D-app_shell-5). |
| A11 | **`dockmodel` owns headless UI state incl. active-tool.** `dockmodel` (L1, ImGui/GL/SDL/libarbc-free) is the headless UI-state model — view catalog + layout **+ the tool rail's active-tool selection** (a `ToolId` enum, a `tool_catalog()`, and a trivial `ToolSelection` holder). Placing the active-tool state here (not in `interact`) lets **`dock` own the *whole* rail** — modal tools **and** the view launcher — within the existing `dock → {dockmodel, views}` edges, with **no `interact` edge and no new component/DAG edge**. `interact` stays pure math (hit-test/gizmo/snapping/brush). The **tool→interaction dispatch** (active tool actually driving canvas behavior) is **promoted into `interact` when a canvas consumer exists** (`editor.canvas.tool_dispatch`, D20). Rail chrome is L3 `dock` per §7. |
| A12 | **Project-entry actions are dependency-inverted behind a `dock`-declared `ProjectGateway`.** The tool rail's New/Open/Recent buttons (D22) call an abstract `dock::ProjectGateway` (open / new / pick-folder / recent) that L3 `dock` **declares and owns**; the concrete impl lives in **L4 `app`** — the only level permitted SDL (`check_levels` `EXTERNAL_ALLOWED["sdl"] = {app}`) — and is the sole holder of the SDL-backed native folder dialog (`SDL_ShowOpenFolderDialog`, async-callback), the `platform::ProcessLauncher` + `current_executable_path()` (A7 / exec_new), the `dockmodel::RecentProjects` prefs store, and the L1 `project` validate/compose helpers. So the rail reaches process-launch + SDL through **one abstraction it declares**, not by reaching down into `commands` / `platform`; `dock`'s includes stay within its own header + std, and the L1 core never sees any of it. **This refines `editor.project.exec_new`'s Constraint 6**, which anticipated exposing `process_launcher()` (and a dialog faculty) on the shared `platform::PlatformServices` aggregate: the gateway is the better swap point — it keeps the interactive SDL dialog out of the L0 services aggregate (whose native impl must stay L0-constructible) and hands the WASM port (File System Access API) a single seam. **No new component and no new DAG edge** — `ProjectGateway` is a `dock` type; its impl is L4 `app`, which already closes over everything. |
| A13 | **`ProjectGateway` also carries the in-session Save + dirty query (extends A12).** The rail's **Save** button and its **dirty indicator** (the front-door verbs of D16 / §9 — "New / Open / Save / Save As; … a dirty indicator") join the *same* `dock`-declared `ProjectGateway` abstraction as the entry actions (`save() -> bool`, `is_dirty() const -> bool`), rather than being drawn as separate app chrome or reaching into `commands` from L3. Unlike New/Open/Recent — which spawn an L4 sibling process (A7/D19) — Save acts on the **in-process session** the L4 shell already owns: the L4 `AppProjectGateway` (which holds the one `AppState`, A7) implements `save()` by calling **L1 `commands::save_project(app_state, fs)`** and `is_dirty()` by reading `app_state.is_dirty()`. **Dirty = session revision-drift** (the workspace-vs-snapshot signal of D16): `AppState` holds a `saved_revision_` baseline — set at a `rebuilt_from_canonical` open (the workspace was just built from `project.arbc`, so it is clean) and updated on each successful publish; `is_dirty()` compares it against the live `document().pin()->revision()`. A fresh `create_project` or a workspace-mapped open has **no known-published snapshot this session** → dirty until the first Save (conservative: never a false-clean, so the user is never told unpublished edits are safely in `project.arbc` when they are not). The canonical dump itself is **L1 `project::save_project`** (re-emit `project.arbc` via `arbc::save_document` / `capture_snapshot`, atomic-publish through `FileSystem::atomic_replace`, owned tiles/assets to `assets/` through a `project`-side `arbc::AssetSink`). **No new component, no new DAG edge, no new external dependency** — two methods on an existing `dock` type, implemented in L4 `app`, backed by an existing L1 `project → arbc` edge. **Save As** (publish + copy the directory + exec a sibling on the copy, A7/D19) is its own leaf (`editor.project.save_as`), not this seam. |
| A14 | **Cameras persist as an editor-defined libarbc `Content` kind in the `Document` — the editor's first custom kind.** *(Amended at the v0.3.0 pin, `editor.canvas.arbc_v030`: the codec-seam branch of the "**or**" below is the one that was taken — `editor.cameras.reopen_codec` registers the camera codec on the `Registry` directly, so the unknown-field-passthrough fallback is a recorded alternative, not the shipped path.)* D2 and §"How this maps onto `libarbc`" promise cameras "persist in the document (`project.arbc`) as scene objects" (`docs/00-design.md:519`), but libarbc's record set is fixed — `Composition`/`Layer`/`Content`/`LayerOrderChunk`, **no camera record** (`arbc/model/records.hpp:28-33`) — and `arbc::Viewport` is a **transient** compositor value (`arbc/compositor/compositor.hpp:16-36`), never a document object. So a **shot camera** is stored as **one `Content` of a new editor kind `org.arbc.camera` attached by one `Layer`** to the root composition: the **frame placement** is the `Layer`'s `Affine transform` (the D7 cell-shape — an affine you drag), the **output resolution** (W×H) + **name** are the `Content`'s serialized state, and the kind is **non-rendering** (contributes zero pixels — an observer, not paint; D2 click-through interior). A camera is therefore an `ObjectId`-addressable placed object *identical in shape to a cell* (D7 "one shape, one select tool"), so `editor.cells.selection` / `editor.cameras.manip` reuse the cell object/transform machinery (`document.hpp` `set_layer_transform`) rather than a bespoke camera path. Mutations (create · rename · "new shot from view") are **`commands` transactions** (D15 — a saved shot's framing IS scene data, undoable via the journal); the **transient viewport camera** (`app::CanvasView::Presenter::camera`, D-nav-1) is untouched session state. Persistence rides the **existing snapshot seam** (`project::save_project` → `arbc::save_document`/`capture_snapshot` over the `Registry` codec table, A13): the camera codec registers on the same `Registry`/codec seam as the builtin cell kinds — **or**, if the pinned `arbc`'s kind-registration surface does not admit an editor-authored codec, the camera persists through the **unknown-field passthrough** (`ContentSnapshot::unknown` → `PlaceholderContent`, non-rendering and transform-preserving for free), same observable roundtrip, no libarbc fork. The camera `Content` type + codec + read accessor (`scene::cameras(const Document&)`) live in **L1 `scene`** (its assigned home, §7); kind registration is wired at a level that already sees `scene` (`commands`/`app`), so `project`'s generic snapshot save serializes cameras with **no new `project→scene` edge, no new component, no new DAG edge, no new external dependency**. *Alternative rejected:* one `CamerasContent` list object holding all cameras — cleaner z-order separation, but a camera would then not be an `ObjectId`-addressable placed object, breaking the D7 uniform select/transform shape that `cells.selection` (selecting "a camera BORDER/label" through the *same* selection model) depends on. Realized by `editor.cameras.model`. |
| A15 | **An editor-defined *editable* kind forces rebuild-from-canonical on reopen — libarbc v0.1.0 exposed no per-kind state-slab walk hook (refines A14 / deviates from D-open-3 for custom kinds).** *(Amended at the v0.3.0 pin, `editor.canvas.arbc_v030`: **this row's premise no longer holds at the pinned version, and its "Future fix" precondition is met.** `arbc::KindStateWalker` has been on the `Registry` since **v0.2.0** — `arbc/contract/registry.hpp:118,146,164,182`, absent at v0.1.0 — and `Registry::add` now takes an optional `state_walker` (as A16 already records); `model.cpp` **collects** a non-inert `StateHandle` rather than asserting on it. The v0.1.0 reasoning below is retained as the historical record of why the editor-side policy exists. Whether the exposed hook actually serves the camera kind's state slab — and therefore whether the workspace-map fast path can be restored — is `editor.cameras.workspace_reopen_slab`'s call to make and verify; **until that leaf lands, the rebuild-from-canonical policy below remains in force**, because it is what ships today.)* A14's camera `Content` is an `arbc::Editable` that captures **live per-Content state** into a non-inert `arbc::StateHandle` (`scene/camera.hpp:101`, written via `txn.set_content_state` on rename). Persistence has **two** reopen routes (A13): the **canonical rebuild** (`load_document` over the `Registry` codec table — cameras restored to live typed `Content` by `editor.cameras.reopen_codec`), and the **workspace-map fast path** (`arbc::Document::open`, D-open-3's durable-by-default default when the workspace file is present). The fast path runs **no codec**; it re-binds the checkpointed `Model`, and `Model::rebuild_counts` **asserts every recovered `ContentRecord` carries an *inert* `StateHandle`** (arbc `model/model.cpp:771` — *"a persisted non-inert StateHandle needs a per-kind state-slab walk hook"*). **arbc `v0.1.0`** — the pin in force when this row was written — reserved that hook's location (`model.cpp:768-770`) but **did not expose it** (`Registry::add` admitted only factory/metadata/codec/binder), so a checkpointed workspace holding a camera **aborted (debug) / silently corrupted the handle (release)** on the map path — the built-in cell kinds dodge it only because their durable state rides the codec path, never the map. **Resolution (editor-side, fail-safe):** `project::open_project` **skips the map fast path and rebuilds from canonical whenever the caller registers editor kinds (a non-empty extra-kinds callback) *and* a canonical baseline exists** — the callback is the only crash-safe signal (an *unpublished* camera can sit in the map with canonical still camera-free, so canonical-content detection would fail open and abort). This trades the fast path's speed and its crash-recovery of *unpublished* edits (A13's dirty-recovery) for a correct, live reopen; for a camera-bearing workspace that is strictly better than the abort, for a camera-free editor session a conservative durability cost (pinned by a test). **Fallback:** when no canonical exists (a created-but-never-saved project, `project_open.cpp:199`) the map path is kept, leaving one residual the pinned lib cannot serve — a **never-saved project that holds a camera** (no canonical floor, no safe map), which the dirty model (A13/D16, "dirty until first Save") already frames as not-yet-durable. **Future fix (precondition now met — see the amendment above):** a libarbc release exposing the per-kind state-slab walk hook restores the fast path for custom editable kinds (cross-repo: a new `arbitrarycomposer` tag + an editor pin bump) — then A15's policy is superseded and the fast path carries a custom kind directly. The tag shipped in v0.2.0 and the pin bump in `editor.canvas.arbc_v030`; consuming it is `editor.cameras.workspace_reopen_slab`. **No new component, no new DAG edge, no new external dependency, no libarbc fork** — one guarded branch in L1 `project`. Realized by `editor.cameras.workspace_reopen_slab`. |
| A16 | **Cell insert is Registry-driven with no kind allowlist: L1 `scene` publishes a per-kind field schema (raw-config fallback for unknown kinds) and the L3 rail reaches it through two new `dock::ProjectGateway` virtuals (extends A12/A13).** `docs/00-design.md:505-511` closes Extensibility by naming the enforcement point — "the editor consumes kinds only through the `Registry` seam (`registry.ids()` + factory/codec/binder), **never a hard-coded kind set (see `editor.cells.model`)**" — but `arbc::ContentConfig` is a deliberately **opaque, kind-defined `std::string_view`** (`contract/registry.hpp:43`) and `Registry` advertises `factory`/`metadata`/`codec`/`binder`/`state_walker` and **nothing describing a kind's input fields** (`:142-164`), so "what do I ask the user for?" has no library answer. Resolution: **L1 `scene`** owns `insert_schemas(const Registry&, std::optional<project::CompositionSize>)`, which emits **one `KindInsertSchema` per `registry.ids()` entry, unconditionally and in registration order** — a kind the editor has a grammar adapter for (raster `"<w>x<h>"`, solid `"r,g,b,a"`, nested `"<decimal ObjectId>"`; `builtin_kinds.cpp:169-207`) gets named fields, and **every other kind gets a first-class raw-config fallback field** passed to its factory verbatim, so an unrecognized plugin kind is always insertable. Adapters are an **enhancement, never a gate**: there is no filter by id, by metadata, or by "is it visual", and a kind whose factory always fails (`org.arbc.fade`/`crossfade`) is still offered — its own error string is the UI (errors are values), with the `Document` untouched. Content is minted **only** via `registry.factory(id)`, never by naming a concrete arbc type; the accepted consequence is that a factory-built `org.arbc.solid` is **unbounded** (its config grammar admits no bounds) and therefore takes identity placement. `scene::add_cell(document, registry, kind_id, config, placement)` then follows the A14 mould exactly — factory → kind token via `project::seed_kind_bridge` → `Document::add_content` (self-committing, so a create is **two** journal entries, as for cameras) → one `transact` for `add_layer` + `attach_layer`; placement arrives as a finished `arbc::Affine` computed by the pure helper `interact::place_in_view` over primitives only (no `interact→scene` edge), which is the seam `editor.panels.overview` later swaps a drag-derived affine into and `editor.import.image` a native-px→units 1:1 affine, with **no change to `scene`**. Kind identity is read back through `KindBridge::lookup` on the `ContentRecord`'s token (`records.hpp:60-63` — `arbc::Content` has no kind-id virtual), `org.arbc.camera` layers filtered out of `scene::cells()`. **UI reach:** `dock` may not include `ace/commands` or `ace/scene`, so `ProjectGateway` gains `insert_kinds()` and `insert_cell(...)` exchanging **dock-local POD** (`InsertKindSpec`/`InsertFieldSpec`) exactly as `clean_up` exchanges `GcSummary` (`dock.hpp:32`, `:105`); the L4 impl marshals the L1 schema and runs the `commands::dispatch` inside `CanvasView::apply_edit` (edit_render_sync Constraint 1 / A4.1 writer identity). Keeping the schema in **L1** is what makes the no-allowlist property provable in headless Catch2 rather than only in an e2e — and structurally denies the L3 modal any place to keep an allowlist. This leaf registers **no new kind**, so A15's rebuild-from-canonical policy is untouched. *Alternative rejected:* a raw config text box for every kind — universal but makes "specify the resolution at insert" (`docs/00-design.md:116-119`) a hex-editor experience. *Alternative rejected:* an Insert **panel** as a ninth `dockmodel::ViewType` — the catalog is `editor.dock.view_registry`'s territory and inserting is a one-shot confirmed op (the two existing modals are the precedent). **No new component, no new DAG edge, no new external dependency, no libarbc fork.** *Future fix (cross-repo):* a per-kind insert-schema hook on `arbc::Registry` would shrink the adapter table to zero. Realized by `editor.cells.model`. |
| A17 | **Hit-testing lives in L1 `interact`, split into a primitive-only pick *policy* and one `interact → scene` *assembly* adapter — the first and deliberately only use of that declared edge (refines A11, narrows A16's "no `interact→scene` edge").** The project-level selection (`commands::Selection`, D19/A5/A7) is keyed by `arbc::ObjectId`, so turning a pointer into a selection needs the document's **z-ordered stack of placed objects with their extents** — cells (`scene::cells`) *and* cameras (`scene::cameras`), because D7 makes them one shape under one select tool (A14). That stack cannot be assembled anywhere below L3 except in `interact`: `commands` may not depend on `interact` (`check_levels.py:32`), `scene` may not depend on `interact` (`:30`), and `base` may not include `arbc/` at all (`EXTERNAL_ALLOWED`), so no lower component can even *name* a target type carrying an `arbc::Affine`. Assembling it at L3/L4 instead would push the one piece of picking logic that must know about z-order, the cells/cameras split, and unbounded content out of Catch2 reach and duplicate it across the canvas, the Layers list, and the Overview. Resolution: **`interact` gains `ace/interact/pick.hpp`** with (a) a **primitive-only policy core** — `PickTarget{id, layer, kind, placement, extent}` plus `pick`/`pick_stack`/`pick_behind`/`marquee`/`click_selection`/`marquee_selection` over `std::span<const PickTarget>`, naming no `scene` type, so the Overview can feed it schematic boxes and a test can feed it hand-built targets — and (b) **one assembly adapter** `pick_targets(const arbc::Document&, const arbc::Registry&)` that includes `<ace/scene/cell.hpp>` + `<ace/scene/camera.hpp>` and is the **single** place the two lists are merged (cells in layer order, cameras appended above them — a camera renders zero pixels per A14, so its outline is always-on-top chrome). The `interact → scene` edge is already in the §8 DAG (`docs/01-architecture.md:210`, `check_levels.py:31`) and has simply never been exercised; A16's parenthetical "(no `interact→scene` edge)" is hereby scoped to what it was about — the pure geometry helpers (`place_in_view`, `hit_frame`, `recrop_frame`, …), which **stay primitive-only** so `editor.panels.overview` and `editor.import.image` can keep swapping their own affines in. The selection **state** stays in `commands` (D-app_state-3) and the mapping from a pick to a state change is returned as a value (`SelectionChange`) rather than applied in `interact`, so no `interact → commands` edge is created; L4 `app::CanvasView` applies it. **No new component, no new DAG edge, no new external dependency** — one new header in an existing L1 component, using an edge the DAG already declares. *Alternative rejected:* assemble the target list in L3 `views` / L4 `app` (keeps `interact` scene-free) — legal, but it puts z-order, the cells/cameras merge, and the unbounded-content rule in ImGui-linked code that headless Catch2 cannot reach, and duplicates it once per consuming surface. *Alternative rejected:* add a `commands → interact` edge and put picking beside `Selection` — a genuinely new DAG edge, and it drags interaction geometry into the transaction component. Realized by `editor.cells.selection`. |

## Open / next

- **`foundation.build`** — the scaffold: `CMakeLists.txt` (FetchContent libarbc +
  ImGui + SDL3), the `src/` component skeleton (§7), the `check_levels` lint, the
  Catch2 + ImGui Test Engine harness, and `scripts/gate` — then a runnable window
  rendering a trivial `Document` (proving binding + display). This bootstraps the
  DoD every later leaf inherits (§9).
- ~~**ImGui Test Engine license**~~ — resolved: free under the OSI open-source
  carve-out (decision A10).
- **Extensibility** — runtime plugin-kind loading surfaced in "insert cell".
- **The WASM port** — deferred; the seams (A3) are what keep it a port.
