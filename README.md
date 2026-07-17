# Arbitrary Composer Editor

A native C++ desktop **image editor** built on
[libarbc](https://github.com/ruoso/arbitrarycomposer) — the resolution-independent
2D composition library. You place *cells* (imported images, painted rasters,
nested compositions, solids) into a composition by arbitrary affine, and observe
them through *cameras* that each own a resolution and a framing.

The library is consumed as an external dependency via CMake **FetchContent** from
a git ref; this repo depends only on the library's public surface, never its
source tree.

## Status

**Design complete; scaffolding pending.** The conceptual design (D1–D19) and the
architecture (A1–A9) are fully specified:

- [`docs/00-design.md`](docs/00-design.md) — the UI/UX design: infinite canvas +
  cameras (the editing viewport is itself a camera), the % of view brush over
  cell-fixed resolution, the list + patterned-fill overview, one-shape
  cell/camera manipulation, sRGB-over-linear color, borrowed/owned assets,
  project-as-a-directory, a fully-uniform dockspace, process-per-project.
- [`docs/01-architecture.md`](docs/01-architecture.md) — Dear ImGui (docking) +
  SDL3 + OpenGL (GLES3/WebGL2 subset), native-first with WASM reachable by
  construction; the library's concurrency contract; a levelized component DAG
  that enforces a UI-agnostic testable core; and a layered testing model
  (Catch2 units + `render_offline` goldens + ImGui Test Engine e2e).
- [`docs/mockup.html`](docs/mockup.html) — a wireframe of the dockspace + the
  two-canvas split.

Toolkit: **Dear ImGui + SDL3 + OpenGL**. Rendering backend comes from libarbc.

## Work breakdown

The build is decomposed as a TaskJuggler WBS (`project.tjp` + `tasks/`), driven
by the same orchestrator loop as the library (`orchestrator/`). The next leaf is
`editor.foundation.build`, which stands up CMake + the component skeleton + the
`check_levels` lint + the test harness + `scripts/gate`.

```sh
python3 scripts/unblocked.py m9_editor   # what's ready to build
```

**Definition of done (every task):** respects the levelization DAG; lands its
tests (L1 logic → Catch2; renders → golden; has UI → an ImGui Test Engine e2e;
threads → sanitizer-clean); clang-format + build clean — all via `scripts/gate`.
