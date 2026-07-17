# editor.foundation.render_probe — Render a trivial Document into a canvas texture

## TaskJuggler entry

`tasks/00-editor.tji:51-56` — `task render_probe` under `editor.foundation`.
Effort `2d`, `allocate team`, `depends !app_shell`. The `note` (`:55`) cites
**Arch A1/A6** and names this refinement. (The `.tji` note currently points at
`tasks/refinements/render_probe.md`; the real path is
`tasks/refinements/foundation/render_probe.md` — the closer fixes the back-link
per the ritual in `tasks/refinements/README.md:57-68`.)

## Effort estimate

**2 days** (from the `.tji`). The probe is a thin end-to-end thread, but it
touches five components (`gl`, `project`, `render`, `views`, `app`) and stands up
the **first `render_offline` golden** and its byte-compare harness — the DoD
infrastructure every later canvas/export golden inherits. The estimate is
dominated by getting the working-space→sRGB8 conversion exactly right (so the
golden is byte-exact) and by wiring the one-shot build→render→upload→display
without leaking arbc/GL orchestration into the shell — not by surface area. No
threading, no dockspace, one static pane.

## Inherited dependencies

**Settled (from `editor.foundation.build`).** The 12-component levelized
skeleton exists and every edge the probe needs is already declared. `gl` is L0
(`CMakeLists.txt:139`, `ace_component(gl DEPENDS base LIBS ${ACE_GLES_LIBRARY})`),
`project` is L1 and already links the library
(`CMakeLists.txt:141`, `ace_component(project DEPENDS base platform LIBS arbc::arbc)`),
`render` is L2 (`CMakeLists.txt:147`, `DEPENDS base project scene gl` — so it
receives `arbc::arbc` transitively through `project`'s PUBLIC link), `views` is
L3 (`CMakeLists.txt:149`), `app` is L4 (`CMakeLists.txt:156-165`). The
levelization lint already whitelists every external include the probe adds:
`scripts/check_levels.py:45` puts `gl`/`render`/`views` in
`EXTERNAL_ALLOWED["gl_api"]`, `:46-47` puts `project`/`render`/`views` in
`EXTERNAL_ALLOWED["arbc"]`, and `:43` puts `views` in
`EXTERNAL_ALLOWED["imgui"]` — **no lint edit is required**. The **libarbc
binding proof** already links the real library and drives its CPU backend
(`tests/binding_test.cpp:1-13`: `arbc::CpuBackend` + `make_surface(4,4,
arbc::k_working_rgba32f)`) — but it stops at surface allocation; it builds no
`Document` and calls no render entry point.

**Settled (from `editor.foundation.app_shell`).** The `Shell` surface exists
(`src/app/ace/app/shell.hpp:32-63`) with the lifecycle
`init() → (new_frame(); draw_ui(); render())* → shutdown()`. `draw_ui()`
currently draws a single placeholder pane and a `"Placeholder"` button that
exists only to give the e2e a stable widget id (`src/app/shell.cpp:97-105`);
`shell.hpp:30-31` explicitly names *"a real Document is
editor.foundation.render_probe"* as the successor to that placeholder. The
`render()` method exposes a `before_present` callback
(`src/app/shell.hpp:48`, `src/app/shell.cpp:114-116`) — the readback/compositing
seam, run while the GL context is current. The **ImGui Test Engine e2e rig**
(`tests/shell_e2e_test.cpp:28-98`) and its reusable `glReadPixels` capture
(`:28-32`) exist and run headless under the offscreen-GL env pinned on
`ace_shell_test` (`CMakeLists.txt:180-193`). Per `D-app_shell-4` the shell e2e
deliberately captured pixels **in memory only** and stored **no golden** —
`shell_e2e_test.cpp:17-19` records that byte-exact `render_offline` goldens
*"begin at render_probe"*. **No `tests/goldens/` directory exists yet.**

**Pending (this leaf owns them).** The GL texture-upload primitive in `gl`; the
trivial-`Document` builder in `project`; the offline render→sRGB8 conversion in
`render`; the texture-displaying pane in `views`; the one-shot wiring in `app`;
the first `render_offline` golden + its byte-compare test helper; and the e2e
that drives the probe pane.

## What this task is

Prove the **binding + display path end to end** with the smallest real render:
build a trivial libarbc `Document` in-process (one solid-color cell in a
composition), render it **offline** into a CPU surface, convert that surface to
straight-alpha sRGB8, upload it as a GLES3 texture, and display it in **one
ImGui pane**. This is a scaffold proof, not the real canvas — it surfaces any
library gap early (A1) and lays down the exact seams the real canvas extends: the
`gl` texture-upload primitive (A6's tile→GL step), the `project` document
ownership, the `render` offline path, and the first `render_offline` golden. It
does **not** stand up the interactive renderer, the tile cache, the frame
handoff, or any editor thread — those are `editor.canvas.view`,
`editor.canvas.frame_sync`, and `editor.canvas.multi_canvas`, already in the WBS.

## Why it needs to be done

`editor.project.open` depends on this leaf (`tasks/00-editor.tji:92`): everything
downstream assumes the editor can construct a `Document` and see it on screen.
The binding proof (`tests/binding_test.cpp`) shows the library *links*; it does
not show the editor can drive a `Document` through the render surface into a GL
texture into a pane. Proving that thread now — before `project.open` and the
whole canvas stack (`editor.canvas.*`) — de-risks the single most important
assumption in the project (A1: real objects, shared memory, no FFI) and forces
any missing library capability to be filed against the library repo early
(`tasks/00-editor.tji:9-12`) rather than deep into the canvas work. It also seeds
the reusable seams (texture upload, offline render, golden harness) so the canvas
leaves extend them instead of inventing them.

## Inputs / context

**Design docs (normative — the constitution).**

- `docs/01-architecture.md` **A1** (`:18-22`, log row `:251`) — the binding
  decision the probe proves: the editor is *"a native C++20 application that
  links `arbc::arbc` directly … No FFI, no language seam: a canvas holds real
  `HostViewport` / `Document` / `Backend` objects and shares memory with the
  renderer."* (Caveat: A1's prose quotes `find_package(arbc CONFIG REQUIRED)`,
  but the operative acquisition for this build is **FetchContent from a git ref**
  — `CMakeLists.txt:19-38`, §7 `:119`, `foundation.build` open item `:264-268`;
  the probe is held to direct-native-linkage, not to the `find_package` wording.)
- `docs/01-architecture.md` **A6** (`:101-105`, log row `:256`) — the display
  path this leaf implements: *"`CpuBackend` yields CPU tile surfaces; the canvas
  view uploads them as GL textures (GLES3/WebGL2) and composites to the pane. A
  GPU `Backend` later … is behind the `Backend` seam — no editor change."* The
  probe's `gl::upload_rgba8` is exactly the tile→GL step, sourced from an
  **offline** surface rather than the interactive tile cache.
- `docs/01-architecture.md` **A4** (`:61-79`, log row `:254`) and **A5**
  (`:86-97`, log row `:255`) — the concurrency + canvas contract the probe
  deliberately does **not** enter: single-writer/render-thread-confined cache,
  leaf-only dispatch, one shared `WorkerPool`, one `HousekeepingThread` per
  `Document`; a canvas = `HostViewport` + `InteractiveRenderer`. The data-flow
  row `:76` (`… ─▶ frame ─▶ GL texture ─▶ screen`) is the display tail the probe
  reaches via the *offline* driver, leaving the threaded head to `frame_sync`.
- `docs/01-architecture.md` **§8 / A8** (`:162-179`, log row `:258`) — the
  levelization DAG and testability seam. The probe's path `app`(L4) → `views`(L3)
  → `render`(L2) → `project`(L1) → `platform`/`base`(L0), with `render`→`gl`(L0)
  and libarbc, is all strictly-downward legal edges; `:177-179` fixes that the L1
  core (`project`/`scene`/…) may never include ImGui/GL/SDL — the probe honours
  this by keeping the doc-builder in `project` GL-free and all GL in
  `gl`/`render`, all ImGui in `views`.
- `docs/01-architecture.md` **§9 / A9** (`:185-208`, log row `:259`) — the
  layered DoD. The *Rendered output* row (`:188`, *"golden compare, reusing
  libarbc's byte-exact `render_offline`"*) is the row this leaf first
  instantiates; the *Threading & smoke* row (`:190`) is the offscreen-GL lane the
  e2e runs under. §9.1 (`:210-245`) documents that offscreen software-GL lane
  (SDL offscreen + llvmpipe, `tests/lsan.supp`).
- `docs/00-design.md` **D10** — the editor is the *"invisible translator"* to/from
  the library's premultiplied-linear working space; the probe honours this by
  letting the library do the sRGB encode (`CpuBackend::convert`), not hand-rolled
  color math. The library-mapping table (`docs/00-design.md:504-513`) pins the
  primitives: rendering backend = `CpuBackend`; export/offline =
  `render_offline(document, viewport, backend)`.

**Library API surface (fetched source under `build/dev/_deps/arbc-src/`; the
`<arbc/...>` include roots are the released v0.1.0 surface).**

- `<arbc/runtime/document.hpp>` — `arbc::Document` (non-copyable). Anonymous
  in-process ctor `explicit Document(DocumentHousekeepingConfig = {})`; mutators
  `ObjectId add_composition(double w, double h)`,
  `ObjectId add_content(std::shared_ptr<Content>, std::uint64_t kind = 0)`,
  `ObjectId add_layer(ObjectId content, const Affine&, double opacity = 1.0)`,
  `void attach_layer(ObjectId composition, ObjectId layer)`; `pin()` /
  `resolve(ObjectId)` for introspection. No transaction object needed for a
  static cell.
- `<arbc/kind_solid/kind_solid/solid_content.hpp>` — `arbc::SolidContent(Rgba
  premultiplied_color, std::optional<Rect> bounds = std::nullopt)`,
  `kind_id = "org.arbc.solid"`; `Rgba{float r,g,b,a}` is **premultiplied
  linear-light**. Build via `std::make_shared<SolidContent>(...)`.
- `<arbc/runtime/offline.hpp>` (`offline.hpp:20-21`) —
  `expected<std::unique_ptr<Surface>, SurfaceError> render_offline(const
  Document&, const Viewport&, Backend&)`. Synchronous, single-threaded,
  byte-exact; **no `WorkerPool`**. Returns the frame in the composition's working
  space (`k_working_rgba32f`).
- `<arbc/compositor/compositor.hpp>` — `struct Viewport { int width; int height;
  Affine camera; ObjectId anchor; }`; `anchor` **must** be set to the composition
  id or the frame is empty.
- `<arbc/backend_cpu/cpu_backend.hpp>` (the include proven by
  `tests/binding_test.cpp:1`) — `arbc::CpuBackend` (default-ctor);
  `make_surface(int w, int h, SurfaceFormat)`, `convert(Surface& dst, const
  Surface& src)` (transcodes to `dst`'s tag triple, doing un-premultiply +
  gamma-encode).
- `<arbc/surface/surface.hpp>` — `Surface`: `width()`, `height()`, `format()`,
  `cpu_bytes()`, and typed `span<PixelFormat::Rgba8Srgb>()` → tightly-packed
  `std::span<std::uint8_t>` of `w*h*4` samples (no stride), directly GL-uploadable
  as `RGBA8`.
- `<arbc/media/surface_format.hpp>` — `k_working_rgba32f` (`:36`, premultiplied
  linear) and `k_fast_rgba8srgb` (`:49`, **straight-alpha sRGB8** — the display /
  golden format). Scalar helpers (`linear_to_srgb8`, …) in
  `<arbc/media/pixel_traits.hpp>` if hand-encoding is ever needed.

**Source seams this leaf extends.**

- `src/gl/ace/gl/gl.hpp:11-14` / `src/gl/gl.cpp:10-15` — the minimal GL seam
  (`set_viewport`, `clear`) to grow a texture upload/destroy onto, matching the
  existing "raw GL stays behind this seam" comment.
- `src/render/ace/render/render.hpp:1-8` / `src/render/render.cpp` — the `render`
  stub (`const char* name();`) to grow the offline render→sRGB8 function into.
- `src/views/ace/views/views.hpp:1-8` — the `views` stub to grow the probe pane
  into.
- `src/app/shell.cpp:97-105` (`draw_ui`, the placeholder pane) and `:107-119`
  (`render` + `before_present`) — the shell hooks the probe wires into.
- `CMakeLists.txt:169-173` (`ace_tests`, headless Catch2 — the golden + doc-build
  units join here) and `:180-193` (`ace_shell_test`, offscreen-GL e2e — the probe
  e2e joins here).

## Constraints / requirements

1. **No lint edit; no new component; no new DAG edge.** Every include the probe
   adds is already whitelisted (`scripts/check_levels.py:42-48`): `gl_api` in
   `gl`/`render`/`views`, `arbc` in `project`/`render`/`views`, `imgui` in
   `views`. `scripts/check_levels.py` must stay **unedited**. The L1 doc-builder
   in `project` includes only `<arbc/...>` + standard headers — never GL/ImGui/SDL
   (`:177-179`). If the implementation ever needs a forbidden edge, that is a
   levelization change requiring an explicit `A<n>` delta — **not expected here**.

2. **Offline render path only — no interactive renderer, no WorkerPool, no
   editor thread.** The probe uses `render_offline` + `CpuBackend`
   (`<arbc/runtime/offline.hpp>`), which is synchronous and single-threaded. It
   must **not** construct `HostViewport`, `InteractiveRenderer`, `TileCache`, or a
   `WorkerPool`, and must not add any editor-owned thread. The interactive/threaded
   display path is `editor.canvas.view` + `editor.canvas.frame_sync` (A4/A5).

3. **Component homes follow §8.** Doc construction lives in `project` (L1,
   GL-free); the offline render + sRGB8 conversion lives in `render` (L2); the GL
   texture create/upload/destroy lives in `gl` (L0); the ImGui pane lives in
   `views` (L3); the one-shot lifecycle wiring lives in `app` (L4). No component
   reaches upward or skips the seam (e.g. `project` never touches `gl`/`render`;
   the shell issues no raw `glGenTextures`).

4. **The editor is the invisible translator to sRGB8 (D10), via the library.**
   `render_offline` returns a `k_working_rgba32f` (premultiplied-linear) surface;
   the probe allocates a `k_fast_rgba8srgb` target and calls
   `CpuBackend::convert` to get straight-alpha sRGB8 for both the GL upload and
   the golden. It must **not** hand-roll the gamma/un-premultiply math — that is
   the library's job (the scalar helpers exist only as a fallback). The sRGB8
   surface is straight-alpha (`k_fast_rgba8srgb` is `Premultiplied::No`); the GL
   upload accounts for that.

5. **The GL-free render function is the byte-exact golden surface.** The
   `render` offline function that produces the sRGB8 bytes must be callable
   **without a GL context** (it issues no GL — only `render_offline` + `convert`),
   so the golden runs under the plain `ace_tests` headless lane, not the flaky
   software-GL lane. Texture upload is a **separate** step (`gl::upload_rgba8`),
   exercised only by the e2e.

6. **Determinism.** The trivial doc is a **solid-color cell covering the full
   frame**, so the sRGB8 golden is a uniform, byte-exact color — maximally robust
   against any float variance and the safest possible first golden. The solid
   color is chosen distinct from the shell clear color
   (`src/app/shell.cpp:112`, `0.10,0.10,0.12`) so the e2e can unambiguously assert
   the pane shows the rendered texture, not the background. A richer geometric
   golden is `editor.canvas.view`'s job, not the probe's.

7. **One pane, no dockspace.** The probe shows a single `ImGui::Begin/Image/End`
   window. The dockspace is `editor.dock.dockspace`; the probe must not anticipate
   it. The probe pane replaces the app_shell placeholder pane (per
   `shell.hpp:30-31`); the existing shell e2e's `"Placeholder"` assertion
   (`tests/shell_e2e_test.cpp:54`) is updated to target the probe pane's stable
   id (the placeholder existed only as an e2e anchor and is now superseded).

8. **Texture lifetime is explicit.** The texture is created **once** after
   `Shell::init()` (GL context current), reused every frame, and destroyed before
   `Shell::shutdown()` (context still valid) — no per-frame re-upload (the doc is
   static). The probe's texture handle + dims are owned by the app layer, not
   baked into the generic `Shell`.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella. This leaf is the **first** to exercise the golden and the
Document-render layers, so each is named concretely below.

- **Levelization (`check_levels` clean).** `python3 scripts/check_levels.py`
  passes **with no edit**: the new `project` doc-builder includes only
  `<arbc/...>` + std; `render` includes `<arbc/...>` + `<ace/gl/...>` (no ImGui);
  `gl` includes only `<GLES3/...>` + `<ace/...>`; `views` includes `<imgui.h>` +
  the GL texture handle. No new component and no new DAG edge.
- **L1 logic — Catch2 unit (doc builder).** A new `tests/render_probe_test.cpp`
  case (headless, joined to `ace_tests`, `CMakeLists.txt:169-173`) builds the
  trivial `Document` via the `project` builder and asserts its structure: the
  composition and layer `ObjectId`s are valid (non-default) and `resolve()` binds
  the solid `Content`. GL-free, no render.
- **Rendered output — the first `render_offline` golden.** A Catch2 case
  (same `tests/render_probe_test.cpp`, linking `ace::render` + `ace::project` +
  `arbc::arbc`) calls the `render` offline function, obtains the sRGB8 bytes, and
  **byte-compares** them against a committed golden at
  `tests/goldens/render_probe_<W>x<H>.rgba8` (raw straight-alpha sRGB8 RGBA, no
  PNG). On mismatch the helper dumps the actual bytes next to the golden for
  triage. This runs under the plain headless lane (no GL context). The small
  byte-compare helper is reusable test support (like app_shell's e2e rig) — later
  canvas/export goldens inherit it; **it is not a separate WBS task.**
- **UI e2e — ImGui Test Engine (probe pane displays the texture).** A case in
  `ace_shell_test` (`CMakeLists.txt:180-193`, offscreen-GL env) drives the probe
  pane by its stable widget id, asserts the pane exists, and uses the existing
  `glReadPixels` capture (`tests/shell_e2e_test.cpp:28-32`) to assert the captured
  frame contains the solid render color (not the shell clear color) — proving the
  build→render→upload→`ImGui::Image` thread on-screen. This is a **presence /
  distinct-color** assertion, not a byte-exact golden (software-GL pixels are
  flaky by construction — the byte-exactness lives in the CPU golden above).
- **Threading (ASan/TSan) — N/A as a new target; stays clean.** The probe adds no
  editor thread and no `WorkerPool`; `render_offline` is synchronous. The e2e
  simply stays clean under the existing `asan` offscreen lane (§9.1); TSan gains no
  new target here — the UI↔driver handoff is scoped to `editor.canvas.frame_sync`.
- **Coverage.** ≥90% diff coverage on changed lines (`diff-cover
  --fail-under=90` under the `coverage` preset), including the
  conversion/upload paths and the golden-mismatch branch.
- **Format/build.** `clang-format --dry-run --Werror` clean; `dev` and `release`
  presets build; `scripts/gate` green.

**No follow-up WBS task is deferred.** The probe's interactive/threaded successor
already exists as WBS leaves — `editor.canvas.view` (HostViewport +
InteractiveRenderer + tile→GL, reusing `gl::upload_rgba8`),
`editor.canvas.frame_sync` (the off-thread handoff / A4), and
`editor.canvas.multi_canvas` — so nothing new is registered. The golden
byte-compare helper ships as test support within this leaf, not as a standalone
task.

## Decisions

- **D-render_probe-1 — Decompose the probe across its natural §8 level homes,
  not inline in `app`.** `project` builds the `Document`, `render` renders it
  offline to sRGB8, `gl` uploads it, `views` displays it, `app` wires the
  one-shot. *Rationale:* each DoD layer lands in the component the DAG already
  gives it a test home (`project`→Catch2 unit, `render`→golden, `views`→e2e), and
  the probe seeds exactly the seams `editor.canvas.view`/`frame_sync` extend.
  *Alternative rejected:* build the whole thread inside `app` (L4) — legal
  levelization-wise (app may include everything) but untestable per-layer, leaks
  arbc/render orchestration into the generic shell, and leaves no reusable seam.

- **D-render_probe-2 — Use the synchronous `render_offline` + `CpuBackend` path,
  not `HostViewport`/`InteractiveRenderer`/`WorkerPool`.** *Rationale:*
  `render_offline` needs no worker pool, is single-threaded and byte-exact (the
  §9 golden path), and defers all A4/A5 threading + tile-cache + frame-sync scope
  to the leaves that own it. *Alternative rejected:* stand up the interactive
  renderer now — pulls the entire concurrency + frame-handoff contract into a
  "probe," duplicating `canvas.view`+`frame_sync`, and its progressive refinement
  / deadlines make a byte-exact golden impossible.

- **D-render_probe-3 — GL texture create/upload/destroy is a new primitive in the
  `gl` L0 seam; `render`/`app` issue no raw GL.** A `gl::upload_rgba8(const void*,
  int w, int h) → texture handle` + `gl::destroy_texture(handle)` alongside the
  existing `set_viewport`/`clear`. *Rationale:* keeps raw GL behind the single
  seam A8 designates (matching `src/gl/gl.hpp:11`), and `canvas.view` reuses the
  identical primitive for settled tiles (A6's tile→GL). *Alternative rejected:*
  raw `glGenTextures` in `render` or `app` — spreads GL calls A8 localizes and
  forces `canvas.view` to reinvent the primitive.

- **D-render_probe-4 — Convert `k_working_rgba32f`→`k_fast_rgba8srgb` via
  `CpuBackend::convert`; the sRGB8 bytes are both the display texture and the
  golden.** *Rationale:* GL displays 8-bit sRGB and a byte-exact golden must be a
  deterministic 8-bit encoding; `convert` does the un-premultiply + gamma-encode
  the library owns, keeping the editor the *invisible translator* (D10) rather
  than an owner of color math. Straight-alpha (`Premultiplied::No`) matches the GL
  upload. *Alternative rejected:* golden the float32 linear-premul surface — not
  display-ready, 4× larger, and re-implements the sRGB encode the editor must not
  own.

- **D-render_probe-5 — The offline render→sRGB8 function is GL-free so the golden
  runs without a GL context; texture upload is a separate step.** *Rationale:*
  keeps the byte-exact golden on the deterministic plain `ace_tests` lane instead
  of the flaky software-GL lane, and lets the e2e own the GL+ImGui display
  assertion separately. *Alternative rejected:* golden by `glReadPixels` off the
  uploaded texture — software-GL pixels are flaky (exactly why app_shell's e2e
  captured but did not golden, `shell_e2e_test.cpp:17-19`), defeating byte-exactness.

- **D-render_probe-6 — Goldens live under a new `tests/goldens/`, compared as raw
  sRGB8 RGBA bytes; the probe stands up the first golden + a reusable byte-compare
  helper.** *Rationale:* raw-bytes compare is unambiguously byte-exact and dodges
  PNG-encoder nondeterminism; a tiny helper mirrors how app_shell shipped the
  reusable e2e rig as test support, not a WBS leaf. *Alternative rejected:* a
  committed PNG golden — introduces encoder-dependent bytes and a decode step for
  no benefit at this size.

- **D-render_probe-7 — The trivial doc is a full-frame solid-color cell (uniform
  golden), in a color distinct from the shell clear color.** *Rationale:* a
  uniform fill is the most robust possible first golden (a single exact 8-bit
  value everywhere, immune to sub-ULP float variance) and lets the e2e assert
  "the pane shows the render, not the background" unambiguously; geometric/affine
  goldens are `canvas.view`'s scope. *Alternative rejected:* a placed/bounded cell
  over a background now — reintroduces edge-AA and placement nondeterminism into
  the very first golden for no probe-level benefit.

## Open questions

- _None._ A1 fixes the binding the probe proves; A6 fixes the display path; A4/A5
  fix that the interactive/threaded path belongs to the canvas leaves (so the
  probe stays offline); §8 already declares every edge the probe needs and §9
  fixes the golden model. The library API is concrete in the fetched v0.1.0
  surface (`Document` + `SolidContent` + `render_offline` + `CpuBackend::convert`
  + `Surface::span<Rgba8Srgb>`). Every choice above is settled against the
  constitution with **no doc delta required** — no new dependency, no new
  component, no new DAG edge, no deviation from a decided behavior.

## Status

**Done** — 2026-07-17.

- `project::build_probe_document()` builds a trivial one-solid-cell `arbc::Document` (GL-free, L1 component home) — `src/project/ace/project/project.hpp`, `src/project/project.cpp`.
- `render::render_offline_rgba8()` renders the document and converts `k_working_rgba32f`→`k_fast_rgba8srgb` via `CpuBackend::convert` — `src/render/ace/render/render.hpp`, `src/render/render.cpp`.
- `gl::upload_rgba8()` / `gl::destroy_texture()` new tile→GL primitives added to the existing seam — `src/gl/ace/gl/gl.hpp`, `src/gl/gl.cpp`.
- `views::draw_probe_pane()` shows the texture in one ImGui pane (L3 home) — `src/views/ace/views/views.hpp`, `src/views/views.cpp`.
- `ProbeView` (app layer) owns the texture lifecycle; `Shell::set_draw_content()` seam wires it in — `src/app/ace/app/probe.hpp`, `src/app/probe.cpp`, `src/app/ace/app/shell.hpp`, `src/app/shell.cpp`.
- First `render_offline` golden: uniform `(0,188,0,255)` sRGB8, byte-exact at `tests/goldens/render_probe_64x64.rgba8`; reusable `golden_support.hpp` byte-compare helper at `tests/golden_support.hpp`.
- Catch2 units (doc-builder + golden compare) in `tests/render_probe_test.cpp`; ImGui Test Engine e2e in `tests/shell_e2e_test.cpp` (rewrote placeholder assertion to drive probe pane by stable id and assert solid render color on-screen).
- `CMakeLists.txt`: one edge added (`views LIBS imgui`, within A8-whitelisted seam); `tests/goldens/.gitignore` gitignores `.actual` dump files.
</content>
</invoke>
