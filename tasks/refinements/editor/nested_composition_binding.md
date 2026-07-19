# editor.canvas.nested_composition_binding — Wire a real KindBridge/Registry DocumentBinding into the interactive CanvasRenderer

## TaskJuggler entry

`tasks/00-editor.tji:208-212` — `task nested_composition_binding` under `editor.canvas`. Effort
`1d`, `allocate team`, `depends !blank_first_frame` (i.e. `editor.canvas.blank_first_frame`). The
`note` (`:212`) frames the leaf:

> "The interactive `CanvasRenderer` does not wire a `KindBridge`/`Registry` `DocumentBinding`, so a
> nested-composition canvas renders no coverage (blank) in production. Wire the `DocumentBinding` so
> nested cells render correctly in the interactive path. Source-of-debt:
> `tasks/refinements/editor/blank_first_frame.md` (2026-07-19). Design: `docs/01-architecture.md A5`."

This is tech-debt registered by the `blank_first_frame` closer (`editor/blank_first_frame.md:378,380`):
that leaf, hardening the frame-settle signal, discovered that a nested child composition "renders no
straight-alpha coverage via bridge binding the interactive host doesn't wire" and had to work around
it in the `seed_nested` e2e fixture (a full-frame inline solid background, `canvas_nav_e2e_test.cpp:77-98`)
to keep the content-gated sequence advancing. It left the **unwired interactive binding** in place and
minted this leaf to fix it at the source. The refinement lands at
`tasks/refinements/editor/nested_composition_binding.md` (the `editor/` set, matching every other
`editor.canvas.*` refinement — `canvas_view.md`, `frame_sync.md`, `multi_canvas.md`,
`blank_first_frame.md` — per the first-dot-segment rule in `tasks/refinements/README.md:9-18`). The
`.tji` note carries no refinement back-link yet; the closer appends
`Refinement: tasks/refinements/editor/nested_composition_binding.md` per the ritual
(`tasks/refinements/README.md:57-68`).

Nothing downstream `depends` on this leaf — it completes an existing promise (a canvas is *the* live
view of the shared document, A5) by making the interactive render path honour libarbc's once-per-frame
external-arrival settle, exactly as the save/load paths already honour the same `Registry`/`KindBridge`
discipline (A14).

## Effort estimate

**1 day** (from the `.tji`). The change is small and localized to **L2 `render`** plus one line of
**L4 `app`** wiring: thread the process-persistent `arbc::Registry` the app already owns down into
`CanvasRenderer`, seed a `KindBridge` from it once, and replace the default-constructed
`arbc::HostViewport::DocumentBinding{}` at `src/render/canvas_renderer.cpp:83` with a populated
`DocumentBinding{&bridge_, &registry_}`. No new render path, no new channel, no new thread, no new
component, no new DAG edge, no libarbc change. The cost is in four small places: (1) two `CanvasRenderer`
ctor signatures gain an optional `const arbc::Registry*` and the Impl gains a seeded `KindBridge`
member; (2) `CanvasHost::add` gains the same optional registry and forwards it to each `Entry`'s
renderer; (3) the app wires `state_.registry()` in at `src/app/canvas_view.cpp:46`; (4) a Catch2 L2
unit that builds a document with a **deferred** external nested child (libarbc's `DeferringAssetSource`
pattern), drives the wired renderer, and asserts the child settles-and-composites where an empty binding
leaves it blank — byte-exact against the offline-settled reference.

## Inherited dependencies

**Settled (from `editor.canvas.blank_first_frame`, `tasks/00-editor.tji:201-206`, Done 2026-07-19).**
The gating machinery that turned this latent bug into an observable, and the fixture that documents it:

- **The content-gated published sequence.** `blank_first_frame` withholds the published sequence until
  a frame composites non-empty tile content (`render::frame_has_content`,
  `src/render/render.cpp`; gate in both `CanvasDriver::drive_once` and `CanvasHost::drive_once`). This
  is *why* a blank nested-composition canvas is now visible as a bug: before, the driver published the
  blank frame and any settle heuristic "settled" on it; now the sequence never advances for a canvas
  whose only content is an unresolved nested child, so the canvas stays dark and `frames_issued` never
  advances (`editor/blank_first_frame.md:378`).
- **The `seed_nested` workaround this leaf inspects.** `tests/canvas_nav_e2e_test.cpp:77-98` adds a
  full-frame inline `SolidContent` background under the nested child specifically so the first frame is
  non-blank, with the comment "a nested child composition renders no straight-alpha coverage in-view on
  its own (it settles via a bridge binding the interactive host does not wire)". That comment is the
  source-of-debt observation this leaf acts on.

**Settled (from `editor.canvas.view`, `tasks/refinements/editor/canvas_view.md`, Done).**
`render::CanvasRenderer` is the interactive driver bundle: a `CpuBackend`, `SurfacePool`, `TileCache`,
one persistent working-space target `Surface`, one `InteractiveRenderer`, and a `HostViewport` bound via
the **`Document&` constructor** (`host_viewport.hpp:176-178`) against the app's one owned `Document`
(`src/render/canvas_renderer.cpp:82-84`). It exposes `resize`/`set_camera`/`step`/`image` and is GL-free
and ImGui-free (headless-unit- and golden-testable). **The load-bearing line this leaf changes is the
`DocumentBinding` argument** at `canvas_renderer.cpp:83` — today `arbc::HostViewport::DocumentBinding{}`.

**Settled (from `editor.canvas.multi_canvas`, Done 2026-07-18).** `CanvasHost` owns one shared
`WorkerPool` and a per-`Document` `DamageRouter`; each `Entry` holds a `CanvasRenderer` built with the
borrowing ctor `renderer(doc, pool, router, budget)` (`src/render/canvas_host.cpp:42,215-216`).
`CanvasHost::add(std::string id, arbc::Document& document)` (`canvas_host.hpp:67`) is document-only —
the seam this leaf widens to carry the registry. The shipping app holds exactly one `render::CanvasHost`
inside `app::CanvasView`, wired document-only at `src/app/canvas_view.cpp:46`
(`host_.add(std::string(view_id), state_.document())`).

**Settled (from `editor.cameras.model` / `.reopen_codec`, Done 2026-07-18).** The
`Registry`/`KindBridge` discipline this leaf reuses is already established for **save** and **load**:

- `commands::AppState` owns the **process-persistent, lifetime-scoped** `arbc::Registry registry_`
  (`src/commands/ace/commands/app_state.hpp:93`, accessor `registry()` `:50-51`), seeded **once** in the
  ctor via `register_editor_kinds` → `scene::register_camera_kind` (`src/commands/app_state.cpp:29,36-45`,
  D-open-7). It is not mutated after construction (the reopen path builds its *own* transient load
  registry, `src/project/project_open.cpp:87-98`).
- `project::seed_kind_bridge(arbc::KindBridge&, const arbc::Registry&)` (`src/project/project.cpp:19-24`,
  decl `src/project/ace/project/project.hpp:97`) interns every registry id+version into a `KindBridge`
  so custom-kind tokens match. Save (`src/project/save.cpp:115-116`) and load
  (`src/project/project_open.cpp:95`) each build a transient bridge this way. The central rule
  (`cameras/model.md:207-210`, A14): *thread registration through a `Registry`/codec-table callback —
  never add a `project→scene` edge.* This leaf applies that same pair to the **render/settle** path.

**Pending (owned here).**
- An optional `const arbc::Registry*` on both `CanvasRenderer` ctors + a `KindBridge` member seeded
  once from it; the populated `DocumentBinding{&bridge_, registry_}` at `canvas_renderer.cpp:83`.
- The same optional registry on `CanvasHost::add`, forwarded to each `Entry`'s renderer.
- The app wiring at `canvas_view.cpp:46`.
- The Catch2 L2 unit proving a deferred external nested child settles-and-composites through the wired
  binding (blank under an empty one), byte-exact vs the offline-settled reference.

## What this task is

**Arch A5** (`docs/01-architecture.md:84-97`) makes a canvas view "one `HostViewport` +
`InteractiveRenderer` over the shared `Document`" — the canvas is *the* live render of the document,
not a partial one. libarbc's `HostViewport` has a documented obligation the editor is currently not
meeting: a `Document` may hold **nested content whose child bytes arrive from a deferring `AssetSource`
after the document loaded** (`org.arbc.nested` external references, `kind_nested/nested_content.hpp:58-85`).
Installing an arrived child is a writer-thread publish, and `HostViewport::step()` is designed to run it
first, every frame, as `settle_external_loads(doc, *binding.bridge, *binding.registry)`
(`host_viewport.hpp:156-159`, `document_serialize.hpp:245-252`) — an arrival *is* damage, so the same
step that settles the child composites it. **That hook is derived only when the `DocumentBinding` carries
both a non-null `bridge` and `registry`** (`host_viewport.hpp:119-133`). The interactive
`CanvasRenderer` passes `arbc::HostViewport::DocumentBinding{}` (`canvas_renderer.cpp:83`) — both null —
so the hook is never derived: a pending nested child never installs, and the canvas composites no
coverage for it (blank). `blank_first_frame`'s content gate then correctly refuses to advance the
sequence, so such a canvas stays dark.

This leaf wires the binding. Concretely:

- **Thread the app's persistent `Registry` into the render path.** `CanvasHost::add` gains an optional
  `const arbc::Registry* registry = nullptr`; the app passes `&state_.registry()` at
  `canvas_view.cpp:46`. Each `Entry` forwards it to its `CanvasRenderer`; both `CanvasRenderer` ctors
  gain the same optional `const arbc::Registry*`. All three are libarbc types crossing the seam — **no
  `commands→render` edge**; the L4 app owns both `AppState` and `CanvasHost` and does the wiring.
- **`render` seeds and owns a per-renderer `KindBridge`.** When a registry is supplied, `CanvasRenderer::Impl`
  seeds a `KindBridge` member once (ctor) via `project::seed_kind_bridge(bridge_, *registry)` (render →
  project is a legal L2→L1 edge, `check_levels.py:30`) and holds it for the renderer's lifetime — the
  pointer the binding hands to `settle_external_loads`. The bridge is **render-thread-confined** (owned
  by the renderer, mutated only by that renderer's `step()`), so it carries no new cross-thread state.
- **Populate the binding.** At `canvas_renderer.cpp:83`, `arbc::HostViewport::DocumentBinding{}` becomes
  `arbc::HostViewport::DocumentBinding{&bridge_, registry_}` when a registry was supplied, and stays
  `{}` when it was not. Per the libarbc contract (`host_viewport.hpp:122-124`) an empty binding is "the
  right shape for a programmatically-built document with no external references," so the null path is
  the correct, unchanged behaviour for the existing headless unit fixtures that build ref-free documents.
- **The rest of the `Document&` binding is already correct and unchanged.** The `ContentResolver`
  (`doc.resolve`), the damage-sink install, and the per-frame content-graph binding (`bind_operators`
  over `Document::for_each_content`, which already renders *in-document* nesting, fades, crossfades) are
  derived from the `Document&` regardless of the binding (`host_viewport.hpp:147-168`). This leaf adds
  **only** the missing external-arrival settle hook.

## Why it needs to be done

A canvas is the document's live view (A5). A document loaded from — or edited into — a project that
references a sub-composition by URI carries pending external loads that only `settle_external_loads`
resolves, and the interactive canvas is the *only* render path that runs each frame, so it is the path
that must own the settle. Today it does not: it hands `HostViewport` an empty binding, the settle hook
is dropped, and the referenced child never appears — the canvas silently renders less than the document
contains. The editor already wires the identical `{Registry, KindBridge}` pair through **save** and
**load** (`save.cpp:115-116`, `project_open.cpp:95`, A14); the render path is the one place the pair was
not threaded. `blank_first_frame` made the gap observable (a blank nested canvas now genuinely stays
dark rather than publishing a masked blank frame) and papered its own e2e over it with an inline
background (`canvas_nav_e2e_test.cpp:77-85`). This leaf closes the gap at the source, restoring the A5
promise and matching the kind-registration discipline the cameras work established everywhere else.

## Inputs / context

**Governing design docs (normative — the constitution).**
- **Arch A5** — `docs/01-architecture.md:84-97` (§5) and the log restatement at `:255`: a canvas view
  is one `HostViewport`/`InteractiveRenderer` over the shared `Document`; N canvases share one
  `WorkerPool`, no new locking. The canvas is the live, complete view of the document — the promise a
  dropped settle hook breaks.
- **Arch A6** — `docs/01-architecture.md` §6: "plugin kinds load via the `Registry` seam." The
  `Registry` is the editor's kind seam; this leaf routes it to the render/settle path.
- **Arch A14** — the editor's first custom `Content` kind (cameras) and the rule *thread kind
  registration through a `Registry`/codec-table callback, no `project→scene` edge*
  (`cameras/model.md:207-210`). This leaf is the same discipline applied to render.
- **§8 levelization** — `docs/01-architecture.md:144-179`; `scripts/check_levels.py:24-48`. `render`
  is **L2**, may depend on `base project scene gl` and include `arbc` (`check_levels.py:30,47`); GL but
  not ImGui. Threading a `const arbc::Registry*` and calling `project::seed_kind_bridge` uses only the
  existing `render→project`/`render→scene`/`render→arbc` edges — **no new edge**.
- **§9 / §9.1 DoD** — `docs/01-architecture.md:181-245`. Universal DoD (`:199-203`); the golden
  cross-check vs `render_offline`; the ASan/TSan UI↔driver lane (`:190`).

**libarbc API surface (fetched `v0.1.0` under `build/*/_deps/arbc-src/`).**
- `arbc::HostViewport::DocumentBinding { KindBridge* bridge{nullptr}; const Registry* registry{nullptr}; }`
  (`runtime/arbc/runtime/host_viewport.hpp:130-133`). Both non-null ⇒ the `Document&` ctor derives
  `settle_external_loads(doc, *binding.bridge, *binding.registry)`, run at the top of every `step()`
  (`:119-133,156-159`). A default `{}` derives none — correct for a ref-free document (`:122-124`). An
  explicit `Config::settle_external_loads` (`:88-106`) wins over the derived hook (the escape hatch).
- `arbc::settle_external_loads(Document&, KindBridge&, const Registry&) → std::size_t`
  (`runtime/arbc/runtime/document_serialize.hpp:251`) — installs arrived external children, drains to
  quiescence over successive calls, returns the count installed; "cheap and safe to call with nothing
  pending" (`:248`). Not undoable (`:249`).
- `arbc::Document::pending_external_loads() const noexcept` (`runtime/arbc/runtime/document.hpp:322`) —
  the count of outstanding external loads; the observable the acceptance test drains.
- `arbc::NestedContent` (`kind_nested/arbc/kind_nested/nested_content.hpp:58-85`) — an in-document child
  (`NestedContent(ObjectId child)`) or an external ref (`NestedContent(ObjectId child, std::string ref)`,
  `child == ObjectId{}` until the ref resolves). Registered `org.arbc.nested` via
  `make_nested`/`register_builtin_kinds` (`api/arbc/builtin_kinds.hpp:39`).
- `arbc::AssetSource` + the **`DeferringAssetSource`** test double
  (`build/dev/_deps/arbc-src/tests/async_external_load.t.cpp:80-137`): `request()` records `(uri, on_ready)`
  and fires nothing; `deliver_all()` releases them; a document loaded through it holds
  `pending_external_loads() == 1` and only `settle_external_loads` lands the child (`:295-349`). This is
  the recipe the acceptance test replicates (the double is defined in-test, not a public utility).
- `arbc::render_offline(const Document&, const Viewport&, Backend&)` (`runtime/arbc/runtime/offline.hpp:21`)
  — **does not settle**; it renders the pinned in-memory graph. So the offline reference for a
  settle-dependent frame must **pre-settle** (loop `settle_external_loads` to 0) before rendering.

**Editor seams this leaf extends.**
- `src/render/canvas_renderer.cpp:82-84` — the `DocumentBinding{}` argument to replace; ctors at
  `src/render/ace/render/canvas_renderer.hpp:51,57`.
- `src/render/canvas_host.cpp:42,113,215-216` — `Entry` ctor + `add` + `Entry` construction; decl
  `src/render/ace/render/canvas_host.hpp:67`.
- `src/app/canvas_view.cpp:46` — the app `host_.add(...)` wiring.
- `commands::AppState::registry()` — `src/commands/ace/commands/app_state.hpp:50-51`.
- `project::seed_kind_bridge` — `src/project/ace/project/project.hpp:97`, `src/project/project.cpp:19`.
- Tests — `tests/canvas_host_test.cpp` / `tests/canvas_driver_test.cpp` (`ace_tests`, `CMakeLists.txt:228`);
  `tests/canvas_nav_e2e_test.cpp` (`ace_shell_test`). The offline cross-check helper
  `ace::render::render_document_srgb8` (`src/render/render.cpp:22-43`) is the reference builder.

**Predecessor / sibling refinements** (style + decision continuity):
`tasks/refinements/editor/blank_first_frame.md` (source-of-debt; the content gate + `seed_nested`
workaround), `tasks/refinements/editor/canvas_view.md` (the `CanvasRenderer` bundle + the
`Document&`-ctor binding site), `tasks/refinements/editor/multi_canvas.md` (`CanvasHost::add`, the
shared pool + per-entry renderer), `tasks/refinements/cameras/model.md` + `.../reopen_codec.md` (the
`Registry`/`KindBridge` + `seed_kind_bridge` discipline, threaded through a registry callback with no
`project→scene` edge).

## Constraints / requirements

1. **The interactive binding is populated from the app's persistent registry.** When the app supplies
   its `AppState::registry()`, `CanvasRenderer` constructs
   `arbc::HostViewport::DocumentBinding{&bridge_, registry_}` (both non-null), so `HostViewport::step()`
   derives and runs `settle_external_loads` each frame. A supplied registry is the **same immutable,
   process-persistent** `arbc::Registry` the save/load paths use — not a fresh one (so custom kinds like
   `org.arbc.camera` are honoured identically across save, load, and render).
2. **The `KindBridge` is seeded once and render-thread-confined.** It is seeded in the `CanvasRenderer`
   ctor via `project::seed_kind_bridge(bridge_, *registry)` (not per `rebuild()`), owned by the
   renderer's `Impl`, and touched only by that renderer's `step()` — no sharing across canvases, no new
   cross-thread mutable state beyond the writer-thread install libarbc's `step()` already performs.
3. **The empty-binding path is preserved for ref-free documents.** With no registry supplied, the
   binding stays `arbc::HostViewport::DocumentBinding{}` — byte-for-byte the current behaviour, which
   the libarbc contract blesses for programmatically-built ref-free documents (`host_viewport.hpp:122-124`).
   Existing headless unit fixtures (ref-free docs) are unchanged and their goldens still pass.
4. **In-document nesting, fades, crossfades, and byte-exactness are untouched.** This leaf adds only the
   settle hook; the resolver, damage sink, and `bind_operators` content-graph binding are unchanged
   (`host_viewport.hpp:147-168`). A document with **no** pending external loads renders byte-identically
   to today through both the wired and the empty binding (`settle_external_loads` on nothing pending is a
   no-op queue check, `document_serialize.hpp:248`) — the existing canvas_host / canvas_driver goldens
   and the `render_document_srgb8` cross-checks must pass unchanged.
5. **No new component, no new DAG edge (§8).** The wiring crosses only existing edges: L4 `app` already
   sees both `commands` (registry) and `render`; L2 `render` already depends on `project`
   (`seed_kind_bridge`), `scene`, and `arbc`. `render` gains no ImGui/GL/SDL include and no
   `commands` include (it receives `const arbc::Registry*`, a libarbc type). `check_levels` stays clean.
6. **Threading: no new lock; the render-thread settle install participates in the existing
   UI↔render document-access contract.** `settle_external_loads` performs a writer-thread publish on the
   render thread inside `step()` — libarbc's designed model (`host_viewport.hpp:89-95`). It is serialized
   with that renderer's own reads (same thread) and adds no lock. A concurrent UI-thread edit during a
   settle is the **same pre-existing race** chartered to `editor.canvas.edit_render_sync`
   (`tasks/00-editor.tji:195-200`, pending) — this leaf introduces no new synchronization obligation
   beyond the one that task owns, and its own tests drive the canvas with no concurrent UI edit during
   the settle window (so they are TSan-clean as shipped).

## Acceptance criteria

Instantiating the universal DoD (`docs/01-architecture.md:199-203`) for this leaf; `scripts/gate` green
(check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean).** No new component, no new DAG edge; `render` gains no
  ImGui/GL/SDL and no `commands` include; the registry crosses as `const arbc::Registry*` and
  `project::seed_kind_bridge` uses the existing `render→project` edge. Primary structural assertion.

- **Catch2 L2 unit — the settle hook is wired (the core proof; `tests/canvas_host_test.cpp` +/or
  `tests/canvas_driver_test.cpp`, `ace_tests`, inline deterministic executor for determinism).**
  Replicating libarbc's `DeferringAssetSource` recipe (`async_external_load.t.cpp:80-137`):
  - Build a `Document` with an **external** `NestedContent` whose child is delivered by a
    `DeferringAssetSource`, so after load `doc.pending_external_loads() == 1` (child not yet installed).
    Seed a `Registry` (`register_builtin_kinds`) + supply it to the renderer.
  - **Wired binding drains and composites:** driving the wired `CanvasRenderer`/`CanvasHost` to settle
    runs `settle_external_loads` each `step()`, so `doc.pending_external_loads()` reaches **0** and the
    published frame carries the child's straight-alpha coverage (`render::frame_has_content(frame)` is
    `true` / `published_sequence` advances past 0).
  - **Empty binding leaves it blank (the before-state):** the same document driven through a renderer
    constructed with **no** registry (empty `DocumentBinding{}`) never drains
    (`pending_external_loads()` stays `1`) and composites no coverage — the frame is withheld
    (`published_sequence == 0`), reproducing the debt exactly.
  - **Byte-exact vs the offline-settled reference:** the wired settled frame equals
    `render_document_srgb8` of the **same document pre-settled to quiescence** (loop
    `arbc::settle_external_loads(doc, bridge, registry)` until it returns 0, then render) — proving the
    interactive settle converges to the offline result, byte-for-byte (the established cross-check
    pattern, `canvas_host_test.cpp:225,438,527`).
  - **Ref-free regression (Constraint 4):** a ref-free document renders byte-identically through the
    wired and the empty binding (settle is a no-op) — the existing goldens/cross-checks unchanged.
  - Coverage ≥ 90 % on changed lines (`diff-cover --fail-under=90`, `coverage` preset).

- **Golden — no new stored golden (justified).** This leaf adds no new render path and perturbs no pixel
  for any document without pending external loads (Constraint 4), so the existing byte-exact goldens
  (`tests/goldens/*.rgba8`, `canvas_host`/`canvas_driver` cross-checks) pass **unchanged** — that they
  still pass is the assertion that the wiring changed only *which content settles*, not the compositor.
  The settle-dependent frame is verified by the in-test byte-exact cross-check against the pre-settled
  `render_document_srgb8` above, not a committed golden file (the child bytes are fixture-defined, not a
  stable on-disk baseline). This is the justified exception to "rendered output gets a golden"
  (`docs/01-architecture.md §9`), consistent with `canvas_view`/`multi_canvas`.

- **UI e2e — ImGui Test Engine (`ace_shell_test`, offscreen software-GL).** The `seed_nested` fixture
  (`tests/canvas_nav_e2e_test.cpp:87-98`) uses an **in-document** nested child (a composition attached
  as a layer, no external ref), which already resolves via `bind_operators` — the interactive
  `anchor_depth` deep-zoom test descends that nested chain today (`canvas_host_test.cpp:488-508`), so its
  inline background is a first-frame-content aid, **not** settled by this leaf's hook. This leaf therefore
  does **not** modify the shell e2e: no external-load fixture is added to `ace_shell_test` (an offscreen-GL
  deferring-source e2e would be new harness for no coverage the L2 unit does not already give). The
  interactive settle path is proven at the L2 unit tier where it is deterministic; the e2e is unchanged
  and stays green. (See D-nested_composition_binding-3 for why the in-document workaround is left in place.)

- **Threading (ASan/TSan) — no new scope; the lane `edit_render_sync` owns.** The wiring adds no thread,
  channel, or lock; the render-thread settle install is libarbc's designed `step()` behaviour and is
  serialized with that renderer's own reads. The `frame_sync`/`multi_canvas` lifecycle tests run green
  under the `asan`/`tsan` presets with the binding wired (the shipped tests drive with no concurrent UI
  edit during the settle window). The concurrent-edit-during-settle race is the pre-existing one
  chartered to `editor.canvas.edit_render_sync` (`tasks/00-editor.tji:195-200`); this leaf claims no new
  TSan target and adds no obligation beyond it.

- **Format + build clean** across the standard presets; `scripts/gate` green.

**No new WBS leaf is deferred.** This leaf closes the interactive-binding debt in full. The `seed_nested`
inline-background workaround is intentionally **left in place** (it aids first-frame content for an
in-document child this leaf does not settle — retiring it is not enabled by this fix; see
D-nested_composition_binding-3), so no "tidy the fixture" follow-up is minted. One adjacent concern —
the **offline/export** render path (`render_offline`) also does not settle deferred external loads — is
**out of scope** here (it renders a pinned in-memory snapshot; the editor has no export path to attach a
pre-settle to yet) and is surfaced to the parking lot in the return summary rather than minted as an
orphan WBS leaf.

## Decisions

- **D-nested_composition_binding-1 — Wire the real `DocumentBinding{&bridge, &registry}` by threading
  the app's persistent `Registry` into the render path; `render` owns a per-renderer `KindBridge` seeded
  from it.** The app passes `&state_.registry()` through `CanvasHost::add` into each `CanvasRenderer`,
  which seeds a `KindBridge` once via `project::seed_kind_bridge` and hands `HostViewport` a populated
  binding. *Rationale:* it reuses the exact `{Registry, KindBridge}` seam the save/load paths already
  established (A14, `save.cpp:115-116`, `project_open.cpp:95`), so custom kinds resolve identically
  across save/load/render; it crosses only existing levelization edges (L4 app → commands + render; L2
  render → project/scene/arbc), adding no `commands→render` edge and no new component; and it keeps the
  bridge render-thread-confined (owned per renderer, mutated only by `step()`), introducing no shared
  mutable state. *Alternative rejected — `AppState` owns the `KindBridge` and the app passes a
  `KindBridge&` down:* symmetric with the registry it already owns, but the bridge is mutated by
  `settle_external_loads` (`KindBridge&`, non-const), so one AppState-owned bridge shared across N
  render-thread canvases is new cross-thread mutable surface for no benefit — a per-renderer bridge is
  strictly simpler and race-free. *Alternative rejected — set `Config::settle_external_loads` explicitly
  (the escape hatch, `host_viewport.hpp:88-106`):* only warranted for a bespoke settle policy; the
  derived hook is exactly the standard `settle_external_loads(doc, bridge, registry)` the editor wants,
  so deriving it from the binding is the minimal, canonical wiring. **No doc delta required** — reuses
  A5/A6/A14 seams, no new architectural seam or dependency.

- **D-nested_composition_binding-2 — The registry crosses as an optional `const arbc::Registry*`
  (default `nullptr` ⇒ empty `DocumentBinding{}`), mirroring libarbc's own pointer-because-absence
  design.** *Rationale:* `DocumentBinding` uses pointers precisely because "absence is a real and common
  state" (`host_viewport.hpp:125`); an empty binding is the *correct* shape for a ref-free
  programmatically-built document (`:122-124`). A nullable parameter preserves every existing headless
  `CanvasDriver`/`CanvasHost` unit fixture (ref-free docs) on the null path unchanged — no churn, no
  golden movement — while the app path and the new nested unit pass a real registry. It also matches the
  optional-callback pattern the load path already uses (`open_project`'s
  `const std::function<void(arbc::Registry&)>&` extra-kinds arg defaults empty, `project_open.cpp:79`).
  *Alternative rejected — make the registry a required parameter everywhere:* forces updating every
  existing driver/host test call site and the golden fixtures for no behavioural gain, and would demand a
  registry where the ref-free unit path legitimately wants none. **No doc delta required.**

- **D-nested_composition_binding-3 — The acceptance test targets a *deferred external* nested child (the
  case contractually tied to the binding); the in-document `seed_nested` inline-background workaround is
  left in place.** The binding's sole added effect is deriving `settle_external_loads`, which installs
  children delivered by a **deferring** `AssetSource` — the unambiguous, libarbc-contract-defined
  behaviour (`host_viewport.hpp:156-159`, `document_serialize.hpp:251`). *In-document* nesting (a
  composition attached as a layer, as in `seed_nested` and `build_nested_doc`) resolves via
  `bind_operators` regardless of the binding: the interactive `anchor_depth` test descends that nested
  chain and re-anchors into it today (`canvas_host_test.cpp:488-508`), proving the compositor already
  reaches an in-document child. So `seed_nested`'s inline background is a first-frame-content aid whose
  removal this fix does **not** enable; retiring it is not part of this leaf. *Rationale:* pinning the
  test to the deferred-external path makes it a rock-solid before/after (`pending_external_loads()`
  drains 1→0 only with the binding; blank without it), avoids coupling the leaf to the murkier
  in-document-composition-as-layer coverage question, and matches libarbc's own
  `async_external_load.t.cpp` methodology. *Alternative rejected — hinge acceptance on removing the
  `seed_nested` workaround and asserting the in-document child now renders:* the anchor_depth evidence
  says in-document nesting already resolves without the binding, so removing the background could break
  the e2e for a reason orthogonal to this fix (a framing/first-frame artifact, not a settle gap) —
  un-defensible as this leaf's proof. **No doc delta required.**

- **D-nested_composition_binding-4 — The render-thread settle install adds no new synchronization; the
  concurrent-edit race is `edit_render_sync`'s charter.** `settle_external_loads` writes on the render
  thread inside `step()` (libarbc's designed model). It is serialized with that renderer's own reads
  (same thread) and takes no lock. A concurrent UI-thread `dispatch` edit during a settle is the
  identical writer/render-read race already registered as `editor.canvas.edit_render_sync`
  (`tasks/00-editor.tji:195-200`, pending). *Rationale:* that task is chartered to serialize writer-thread
  `Document` edits against the render-read path; the settle-write is one more render-thread writer it
  already must cover, so this leaf inherits rather than duplicates the obligation and claims no new TSan
  target — the shipped tests drive with no concurrent edit during settle and are TSan-clean.
  *Alternative rejected — add a lock around the settle here:* premature and out of level — the
  document-access serialization is one contract owned by one task, not a per-call-site lock. **No doc
  delta required.**

## Open questions

`(none — all decided.)` The one adjacent concern — that the **offline/export** path (`render_offline`)
also does not settle deferred external loads, so an export of a project with a pending external nested
comp would render blank — is a genuine but *separate* gap with no editor seam to attach a fix to yet (no
export path exists; `render_offline` renders a pinned snapshot and must not silently gain settle
semantics that would change its byte-exact-reference contract). It is deliberately **not** minted as a
WBS leaf (that would be an un-closeable "handle export settle later" without a home) and is surfaced to
the parking lot via the return summary for whoever builds the export path (`packaging` area) to address
in-place. No human-judgment item blocks *this* leaf.

## Status

**Done** — 2026-07-19.

- `src/render/ace/render/canvas_renderer.hpp` — both ctors gain optional `const arbc::Registry*`; forward-declare `arbc::Registry`.
- `src/render/canvas_renderer.cpp` — `Impl` gains a `registry` ptr + a `KindBridge bridge` seeded once via `project::seed_kind_bridge` (render→project L2→L1); `rebuild()` populates `DocumentBinding{&bridge, registry}` when a registry is supplied, else stays `{}`.
- `src/render/ace/render/canvas_host.hpp` / `src/render/canvas_host.cpp` — `add` and `Entry`/`PendingAdd` carry the optional `const arbc::Registry*`, forwarded to each entry's renderer.
- `src/app/canvas_view.cpp` — wires `&state_.registry()` into `host_.add(...)`.
- `tests/canvas_host_test.cpp` — three Catch2 L2 unit cases (ace_tests) via a `DeferringAssetSource` double: (1) wired binding drains `pending_external_loads` 1→0 and composites the child, byte-exact vs a settled reference; (2) empty binding leaves it blank (pending stays 1, sequence 0); (3) ref-free doc renders byte-identically through wired vs empty binding.
- Scoped-test deviation (justified): `render_document_srgb8` offline reference is unattainable — `render_offline` never calls `bind_operators`, so it composites no nested-composition operator. Convergence proved instead against an independent copy pre-settled to quiescence rendered through an empty-binding interactive host (byte-exact).
- Parking lot: `render_offline` cannot render nor settle nested compositions at all (binds no operators) — broader than the noted "doesn't settle external loads." Surfaced for whoever builds the export path (`packaging`).
