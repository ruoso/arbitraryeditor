# editor.cells.model — Insert a cell of any registered kind via the Registry

## TaskJuggler entry

- **Task:** `editor.cells.model` (`tasks/00-editor.tji:310-315`, under
  `task cells "Cells & manipulation"` at `:309`).
- **Effort:** `2d` · `allocate team`.
- **Depends:** `editor.canvas.view` (`:313`).
- **Note (`.tji:314`):** "Insert a cell of any registered kind
  (org.arbc.image/raster/nested/solid) off the library `Registry::ids()`; place it
  in the composition by affine. Design: D3." Plus the pre-exec decisions of
  2026-07-19: "the editor keeps **NO kind allowlist** — enumerate ALL
  Registry-advertised kinds uniformly (arbitrariness is the core principle);
  kind-specific content acquisition (raster resolution, image/nested file/URI,
  solid color) is the kind's concern, **surfaced generically**, not a hard-coded
  editor set. Cell resolution is a first-class user input at insert (NOT a fixed
  default — resolves the D3/`00-design.md:116` open marker): the user specifies
  resolution, then PLACES the cell in the overview wireframe to see how it fits
  the composition. That placement gesture wants `editor.panels.overview` (not yet
  built) — **this leaf ships the model-level insert (resolution + affine) with a
  provisional placement**; the overview-wireframe placement lands with/after
  `editor.panels.overview`."
- **Back-link:** the `.tji` note currently ends `Refinement:
  tasks/refinements/cell_model.md` (the flat interim path). This refinement lands
  at **`tasks/refinements/editor.cells/model.md`** per the area-subdir layout
  (`tasks/refinements/README.md:9-18`); the closer updates the note back-link to
  the real path and adds `complete 100` after `allocate team`
  (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji` here.
- **Downstream dependents** (why this is the cells keystone): `editor.cells.selection`
  (`!model`, `:319`), `editor.panels.layers` (`editor.cells.model`, `:347`),
  `editor.paint.brush` (`editor.cells.model`, `:376`), `editor.import.image`
  (`editor.cells.model`, `:398`). Transitively that is the whole of
  `editor.cells`, `editor.panels`, `editor.paint`, and `editor.import`.

## Effort estimate

**2 days.** The mould already exists and is the reason 2d is realistic:
`scene::add_camera` (`src/scene/camera.cpp:501-528`) is the exact
mint-Content-then-attach-Layer transaction shape, `project::seed_kind_bridge`
(`src/project/project.cpp:19-24`) already interns every registered kind, and the
rail-plus-modal chrome has two working precedents (`draw_new_project_modal`,
`draw_gc_modal`, `src/dock/dock.cpp:52-113`). The genuinely new work is (a) the
**registry-driven, allowlist-free** kind enumeration + generic field descriptors
in L1 `scene`, (b) one `interact` placement helper, (c) the `dock::ProjectGateway`
insert seam (A16) and its L4 impl. No new component, no new thread, no new
external dependency. Coverage is Catch2-heavy with one golden and one e2e.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.canvas.view`** (`tasks/refinements/editor/canvas_view.md`) — the live
  canvas over the one shared `Document`, and the transient viewport camera
  `app::CanvasView::Presenter::camera` (`src/app/ace/app/canvas_view.hpp:99`) that
  this leaf **reads by value** to compute the provisional placement. D-canvas_view-5
  fixed that the canvas frames the root composition; there is no per-canvas cell
  state to thread.
- **`editor.cameras.model`** (`tasks/refinements/cameras/model.md`) — the object
  model this leaf mirrors. D-model-1 (`:309-330`) established the
  one-`Content`-plus-one-`Layer` placed-object shape (A14) and, critically for
  this leaf, that a camera layer must be **distinguishable by kind** so cell code
  can filter it out. D-model-5 (`:362-370`) put the scene-object accessors in L1
  `scene` with registration wired above — the same home this leaf uses.
  D-model-4 (`:355-360`) fixed that mutations ride `commands::Command`/`dispatch`.
- **`editor.cameras.rename_stable_id`** (`tasks/refinements/cameras/rename_stable_id.md`)
  — D-rename-4: `add_camera`'s **two-entry create** (`Document::add_content`
  self-commits, then one `transact` for add_layer + attach) is a libarbc shape,
  not an editor bug; the D15 observable contract still holds. Cell insert inherits
  the identical two-entry create, and this refinement inherits the recorded
  deviation rather than re-litigating it.
- **`editor.canvas.edit_render_sync`** (`tasks/refinements/editor/edit_render_sync.md`)
  — Constraint 1, which **names cells explicitly**: every UI-thread `Document`
  mutation runs inside `CanvasHost::apply_edit`
  (`src/render/ace/render/canvas_host.hpp:91-100`), reached from L4 through
  `CanvasView::apply_edit` (`src/app/ace/app/canvas_view.hpp:76`). No shipped path
  may mutate then merely `poke()`.
- **`editor.canvas.single_writer`** (`tasks/refinements/editor/single_writer.md`)
  — supersedes the *mechanism* (`doc_mu` retired at arbc v0.2.0) but not the seam;
  **A4.1** (`docs/01-architecture.md:84-98`) is the governing rule: writes are
  bound to one stable OS-thread *identity*, reads are lock-free via `pin()`.
- **`editor.project.app_state`** (`tasks/refinements/editor/app_state.md`) — the
  single owned `Document` + `arbc::Registry` (`src/commands/ace/commands/app_state.hpp:50`,
  `:93`), the `Command{name, apply}` / `dispatch` seam (`:110-126`), and
  `register_editor_kinds` (`src/commands/app_state.cpp:29`) — the one list of
  editor-side kind registrations.
- **`editor.project.exec_new` / `editor.project.save` / `editor.dock.tool_rail`**
  (A12/A13, `tasks/refinements/editor/exec_new.md`, `save.md`) — the
  `dock::ProjectGateway` dependency-inversion pattern (`src/dock/ace/dock/dock.hpp:52-119`)
  that lets the L3 rail reach the L4 session without a `dock → commands` edge, and
  the rail/modal chrome this leaf extends.
- **`editor.canvas.fit_bounds`** (`tasks/refinements/editor/fit_bounds.md`) —
  `project::root_composition_size(const arbc::Document&)`
  (`src/project/ace/project/project.hpp:67`) is the composition-extent read this
  leaf uses to prefill the resolution field and to size the provisional placement.
- **`editor.canvas.nested_composition_binding`**
  (`tasks/refinements/editor/nested_composition_binding.md`) — the interactive
  `CanvasRenderer(document, registry)` `DocumentBinding`
  (`src/render/ace/render/canvas_renderer.hpp:52-62`) that makes an inserted
  `org.arbc.nested` cell actually render; this leaf inserts nested cells and
  relies on that wiring already being in place.

**Pending (owned here):** the cell scene-object model (`scene::add_cell`,
`scene::cells`), the registry-driven insert-schema table, the `interact`
provisional placement helper, the `ProjectGateway` insert seam + rail modal, and
the A16 doc delta. Nothing downstream is blocked on an unwritten predecessor.

## What this task is

Make it possible to **put a cell into the composition** — the first time the
editor mints content of a *library* kind rather than its own. A cell is D3's
"one placed unit of artwork = a libarbc **layer**"
(`docs/00-design.md:34-41`): a `Content` of some **kind**, an **arbitrary affine**
placing it in composition space, and its **own native/working resolution**.

The load-bearing constraint is that the editor must not know what kinds exist.
`docs/00-design.md:505-511` closes the Extensibility question by naming *this
leaf*: "the editor consumes kinds only through the `Registry` seam
(`registry.ids()` + factory/codec/binder), **never a hard-coded kind set (see
`editor.cells.model`)**, so a future plugin host registers on the `Registry` and
its kinds surface in every affordance automatically." So the insert affordance is
built as a **loop over `registry.ids()`**, and the per-kind content-acquisition
inputs are rendered from a **data-driven field descriptor** the L1 core hands up,
with a **raw-config fallback** for any kind the editor has never heard of. A kind
is never filtered out; at worst it is offered with a free-text config field.

Concretely this leaf ships, all in the existing components:

1. **L1 `scene`** — `scene::insert_schemas(registry, composition_size)` enumerating
   every advertised kind with generic `InsertField` descriptors; a
   `scene::build_config(schema, values)` adapter that assembles the kind's opaque
   `arbc::ContentConfig` string; `scene::add_cell(document, registry, kind_id,
   config, placement)` (factory → kind token → `Document::add_content` →
   `transact` → `add_layer` → `attach_layer` → `commit`); and the read accessor
   `scene::cells(const arbc::Document&)`.
2. **L1 `commands`** — `commands::insert_cell_command(...)` returning a `Command`
   dispatched through the existing `dispatch(AppState&, const Command&)`.
3. **L1 `interact`** — `interact::place_in_view(...)`, the pure-affine provisional
   placement: centre the content's own extent in the currently visible
   composition region.
4. **L3 `dock` + L4 `app`** — the "Insert Cell…" rail affordance and its modal,
   reaching the session through two new `ProjectGateway` virtuals (**A16**), with
   the mutation routed through `CanvasView::apply_edit`.

It deliberately does **not** ship: the overview-wireframe placement gesture
(`editor.panels.overview` owns it, and its `.tji` note already says so, `:354`),
image-plugin linking (`editor.import.image`, `:395-399`), painting into a raster
(`editor.paint.brush`, `:373-377`), selection or the gizmo
(`editor.cells.selection`/`gizmo`), or deletion.

## Why it needs to be done

Today the editor can create exactly one kind of thing: a camera. Every cell in
every test is hand-built with a test-local `build_cells_doc()` fixture
(`tests/camera_manip_test.cpp:65-79`, `tests/look_through_test.cpp:43-57`);
`src/scene/` holds only `camera.hpp`/`camera.cpp`, and there is **no `cells()`
enumerator, no `add_cell`, and no Insert/Add UI anywhere** in the tree (the rail's
sections are Project, tools, view launcher, workspaces — `src/dock/dock.cpp:117-300`).
`org.arbc.image` appears nowhere in `src/` or `tests/` at all.

That makes this the cells keystone. Four leaves `depend` on it directly and cannot
start without it: `cells.selection` (nothing to select), `panels.layers` (nothing
to list), `paint.brush` (no raster to dab into), `import.image` (no cell to place
a borrowed photo as). It is also the first leaf that puts a **raster** cell into a
saved project, which is what makes the tile-store gap below real rather than
theoretical.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **D3 — Cell kinds** (`docs/00-design.md:470`): "Imported images (referenced) ·
  painted rasters · nested compositions · solids/procedural." The narrative is §2
  "Cells — the content" (`:32-45`): a cell is a `Content` of some kind
  (`org.arbc.image` referenced / `org.arbc.raster` painted / `org.arbc.nested` /
  `org.arbc.solid`), plus "an **arbitrary affine** placing it in composition space
  (translate, scale, rotate, shear)", plus "its **own native / working
  resolution**." "A 4000×3000 photo and a 200×200 sketch coexist in one
  composition with no common pixel grid" (`:43-45`).
- **Resolution at insert** (`docs/00-design.md:116-119`, §4 "Storage: cell-owned,
  fixed resolution"): "**A new paint cell needs a working resolution** — the user
  specifies it at insert (resolution is a first-class placement input, not a fixed
  default), then places the cell in the overview wireframe to see how it fits the
  composition; always visible/editable in the inspector thereafter." *This is
  already-settled doc text* (the `*(open: exact default policy.)*` marker was
  closed in commit `4d72883`); the leaf implements it, it does not re-decide it.
- **Extensibility / no allowlist** (`docs/00-design.md:505-511`) — quoted above;
  names `editor.cells.model` as the enforcement point.
- **The libarbc mapping** (`docs/00-design.md:523-524`): "Cell placement | a
  `Layer`'s `Affine` in a composition"; "Cell kinds | `org.arbc.{image,raster,nested,solid}`
  via `Registry` / `register_builtin_kinds`".
- **D7 — Manipulation model** (`:474`, §6 `:208-217`): cells and cameras share one
  shape — affine placement + a resolution number — and one select tool. The cell
  object this leaf mints must be `ObjectId`-addressable in exactly the shape A14
  gave the camera.
- **D8 — Cell scale ≠ resample** (`:475`): handle-drag changes placement, never
  resolution. Consequence here: the placement affine and the content's resolution
  are **independent inputs at insert**, never derived from one another.
- **D11/D12 — asset axes and import paths** (`:478-479`): out of scope for this
  leaf but they constrain the seam — `import.image`/`paste`/`nested` must be able
  to reach the same `add_cell` with their own config, which is why config is an
  opaque string the caller supplies rather than a fixed struct.
- **D15 — transactional undo** and **A13** (`docs/01-architecture.md:279`): every
  mutation is a journal transaction; dirty is revision drift.
- **A4/A4.1/A5** (`docs/01-architecture.md:270-271`, `:84-98`) — single-writer
  *identity*; UI thread submits edits, never touches the cache.
- **A8/§8** (`:274`, `:160-195`) — the levelization DAG; **A12/A13** (`:278-279`) —
  the `ProjectGateway` dependency inversion; **A14/A15** (`:280-281`) — the
  editor-kind persistence seam and the reopen consequence.
- **§9** (`:197-224`) — the layered DoD this leaf's Acceptance criteria instantiate.

**libarbc API surface** (fetched at tag `v0.2.0`, `CMakeLists.txt:25`; headers
under `build/dev/_deps/arbc-src/src/`):

- `arbc::Registry` (`contract/arbc/contract/registry.hpp:131`):
  `add(...)` `:142`, `factory(id)` `:149`, `metadata(id)` `:152`, `codec(id)` `:156`,
  `binder(id)` `:159`, `ids()` `:173` (**registration order**).
  `using ContentConfig = std::string_view` `:43` — *opaque, kind-defined*;
  `using ContentFactory = std::function<expected<std::unique_ptr<Content>,std::string>(ContentConfig)>`
  `:47`; `struct KindMetadata { human_name; version; }` `:34`.
- `arbc::register_builtin_kinds(Registry&)` (`arbc/builtin_kinds.hpp:39`) registers
  six kinds *factory+metadata only*: `org.arbc.solid` (config `"r,g,b,a"`,
  premultiplied working floats, **unbounded**), `org.arbc.tone`
  (`"<hz>,<amp>"`), `org.arbc.raster` (`"<width>x<height>"` → a **transparent**
  `RasterContent` at `k_default_tile_edge`), `org.arbc.nested`
  (`"<decimal ObjectId>"`), and `org.arbc.fade`/`org.arbc.crossfade` whose
  factories **always return an error value** ("construct through document
  deserialize"). Impl: `builtin_kinds.cpp:169-207`.
  `org.arbc.image` ships in the out-of-lib `arbc-plugin-image` and is **not
  registered and not linked** by the editor today.
- `arbc::Content` (`contract/arbc/contract/content.hpp:485`): `bounds() const ->
  std::optional<Rect>` `:487` — the generic extent query the placement math needs;
  `nullopt` means unbounded. There is **no kind-id virtual** on `Content`.
- `arbc::Rect` (`base/arbc/base/geometry.hpp:15`), `arbc::Affine`
  (`base/arbc/base/transform.hpp`): `identity()` `:21`, `translation` `:22`,
  `scaling` `:23`, `inverse()` `:30`, `map_rect()` `:38`, free `compose()` `:44`.
- `arbc::Document` (`runtime/arbc/runtime/document.hpp`):
  `add_content(shared_ptr<Content>, kind_token)` `:107` — **self-commits** (it is
  the only call that binds a `Content` vtable); `transact(name)` `:179`
  (**writer-thread only**); `pin()` `:262`; `resolve(ObjectId)` `:266`;
  `remove_content(content, composition, layer)` `:131` — atomic, **one** entry.
- `arbc::Model::Transaction` (`model/arbc/model/model.hpp:397-600`):
  `add_layer(content, transform, opacity)` `:405`, `attach_layer(comp, layer)`
  `:498`, `set_transform` `:447`, `commit()` `:543`.
  `Transaction::add_content(kind)` `:408` mints an **inert, unbound** record and is
  therefore *not* usable for a cell insert.
- `arbc::DocRoot` (`model/arbc/model/model.hpp:41`): `find_first_composition`
  `:72`, `for_each_layer_in` `:103` (bottom-to-top), `find_layer` `:53`,
  `find_content` `:54`. `arbc::ContentRecord { std::uint64_t kind; StateHandle state; }`
  (`model/arbc/model/records.hpp:60-63`) — the kind **token** is the only
  kind identity on a record.
- `arbc::KindBridge` (`runtime/arbc/runtime/document_serialize.hpp:45`):
  `intern(id, version)` `:58`, `lookup(token, id&, ver&)` `:63` — the reverse map
  `scene::cells()` needs; `builtin_codecs(const Registry&)` `:127`,
  `builtin_codecs(const Registry&, RasterTileStore*)` `:134`,
  `load_document(..., RasterTileStore*, TileDecodeDispatch*)` `:218`.

**Editor seams this leaf extends:**

- `src/scene/ace/scene/camera.hpp:151` `add_camera(...)` and its impl
  `src/scene/camera.cpp:501-528` — **the transaction mould**; `camera.cpp:452-456`
  `camera_token(registry)` (a locally-seeded `KindBridge` + `intern`) — **the token
  mould**; `camera.cpp:471-499` the `pin()` → `find_first_composition` →
  `for_each_layer_in` → `find_layer` → `resolve` walk — **the accessor mould**.
- `src/project/ace/project/project.hpp:97` `seed_kind_bridge(KindBridge&, const Registry&)`
  (impl `project.cpp:19-24`) — interns every `registry.ids()` with its metadata
  version; the save path and any token mint **must** seed from the same registry.
- `src/project/ace/project/project.hpp:67` `root_composition_size(const Document&)`.
- `src/commands/ace/commands/app_state.hpp:110-126` `Command` / `DispatchOutcome`
  / `dispatch`; `:50`,`:93` the owned `arbc::Registry`; `src/commands/app_state.cpp:29`
  `register_editor_kinds`.
- `src/interact/ace/interact/interact.hpp` — the pure-math home: `fit` `:54`,
  `new_shot_from_view` `:77` (the "make an affine from the current view" shape
  `place_in_view` mirrors), `hit_frame` `:148`, `recrop_frame` `:156`.
- `src/dock/ace/dock/dock.hpp:52-119` `class ProjectGateway` (+ the dock-local POD
  `GcSummary` at `:32`, the precedent for a dock-local value type crossing the
  seam); `src/dock/dock.cpp:52-79` `draw_new_project_modal` and `:88-113`
  `draw_gc_modal` — the modal shape (state on `Dockspace` `dock.hpp:193-214`, drawn
  every frame so `BeginPopupModal` stays balanced, stable `###id`s); `:117-200` the
  rail's Project section.
- `src/app/ace/app/project_gateway.hpp:53-69` `AppProjectGateway` — the L4 impl and
  the edit-runner binding; `src/app/ace/app/canvas_view.hpp:76` `apply_edit`;
  `src/app/ace/app/camera_inspector.hpp:25-42` — the precedent for an L4 UI object
  routing edits through `apply_edit`.
- `src/render/ace/render/canvas_renderer.hpp:52-62` — the `{Registry, KindBridge}`
  `DocumentBinding` that makes nested cells render interactively.

**Test rigs:** `ace_tests` (`CMakeLists.txt:219-240`, `ACE_GOLDEN_DIR=tests/goldens`),
`ace_shell_test` (`:247-269`, offscreen SDL + llvmpipe + `tests/lsan.supp`);
`tests/golden_support.hpp:36` `ace_test::compare_golden`;
`render::render_document_srgb8` (`src/render/render.cpp:22`);
`tests/camera_model_test.cpp` and `tests/history_e2e_test.cpp` are the two
convention models.

## Constraints / requirements

1. **Levelization (the primary structural assertion).** No new component, no new
   DAG edge, no `scripts/check_levels.py` edit. `scene` (L1, may reach
   `base, project, libarbc`) gains the cell model; `interact` (L1, `base, scene`)
   gains the placement math; `commands` (L1, `base, project, scene`) gains the
   verb; `dock` (L3) gains the modal and two `ProjectGateway` virtuals; `app` (L4)
   gains the impl. Nothing in `scene`/`interact`/`commands` may include ImGui, GL,
   or SDL. **`dock` must not gain an `ace/commands` or `ace/scene` include** — the
   insert seam crosses as dock-local POD, exactly as `GcSummary` does
   (`dock.hpp:32`, D-gc-5). `interact` must not gain a `scene` type in these
   signatures: `place_in_view` takes primitive `arbc::Affine`/`Rect`/ints only
   (the D-manip-2 rule).

2. **No kind allowlist — the enforcement point.** `scene::insert_schemas` iterates
   `registry.ids()` and emits **one entry per id, unconditionally**. There is no
   `if (id == "org.arbc.raster")` gate anywhere on the enumeration path, and no
   filter by id, by metadata, or by "is it visual." A kind the editor has never
   seen gets the raw-config fallback schema (a single free-text `config` field
   carrying the kind's `ContentConfig` verbatim) and is fully insertable. The
   per-kind **grammar adapters** in `build_config` are enhancements layered on the
   universal enumeration, never a gate on it — a test asserts
   `insert_schemas(r).size() == r.ids().size()` for a registry seeded with an
   editor-unknown probe kind.

3. **Errors are values; a failing factory mutates nothing.** `org.arbc.fade` and
   `org.arbc.crossfade` factories always return an error
   (`builtin_kinds.cpp:169-207`), and a bad user config can fail any factory.
   `scene::add_cell` must call the factory **first**, and on
   `!expected` return the kind's own error string with the `Document` untouched
   (no content minted, no transaction opened, zero journal entries). The modal
   renders that string inline and stays open — the rail's established
   errors-are-values contract (`dock.hpp:45-51`).

4. **The insert is a `commands` transaction, and it costs two journal entries.**
   The verb is a `commands::Command{name, apply}` run through
   `dispatch(AppState&, const Command&)` so `AppState`'s revision/dirty bookkeeping
   (A13) stays correct. `Document::add_content` self-commits
   (`document.hpp:107`) and is the only call that binds a vtable, so a create is
   **two** entries — content, then add_layer+attach — exactly as `add_camera` is
   (`camera.hpp:143-150`, D-rename-4). Tests assert
   `journal_entries_added == 2`, and assert the observable D15 contract: one
   `undo` detaches the layer (the cell disappears from `scene::cells`), one `redo`
   restores it, id-stable. Do **not** paper over this with a bespoke
   single-entry path.

5. **Every UI-driven insert runs inside `apply_edit`.** The L4 gateway impl wraps
   the `dispatch` call in `CanvasView::apply_edit`
   (`canvas_view.hpp:76` → `canvas_host.hpp:91-100`), never a bare `dispatch` +
   `poke()` — `edit_render_sync` Constraint 1, which names cells. Headless
   gateway tests with no canvas get the direct-invoke default runner.

6. **Placement is a value the caller computes.** `scene::add_cell` takes the
   placement as a finished `arbc::Affine`; it never reads a viewport camera or a
   pane size. The view→placement conversion is `interact::place_in_view`, pure math
   over primitives — the same purity rule `new_shot_from_view` follows (D-model-6
   in `cameras/model.md`). This is what lets `editor.panels.overview` later swap in
   a drag-derived affine with **no change to `scene`**, and lets
   `editor.import.image` supply its own native-px→units 1:1 affine (D12).

7. **Provisional placement is honest and replaceable.** The default placement
   centres the content's own `bounds()` extent in the currently visible
   composition region, uniformly scaled to occupy a fixed fraction of the shorter
   visible edge; **unbounded** content (`bounds() == nullopt`, e.g. a
   factory-built `org.arbc.solid`) gets `Affine::identity()` because scaling an
   unbounded fill is meaningless. Degenerate inputs (non-invertible view affine,
   zero-area pane, empty rect) fall back to identity rather than producing NaNs —
   the D-fit_bounds-3 fallback discipline. The modal must label this placement as
   provisional so the overview handoff is visible to the user, not silent.

8. **Resolution is a specified, visible input — never a silently-applied default.**
   For the one built-in kind that takes an extent (`org.arbc.raster`, config
   `"<w>x<h>"`) the schema declares a **required** `size` field, always rendered,
   always editable, prefilled from `project::root_composition_size` (clamped to
   ≥1, falling back to `1024x1024` when the composition is absent or degenerate).
   Insert is blocked on a value that does not parse to two positive integers.
   This is `docs/00-design.md:116-119` — "the user specifies it at insert" — and
   D8's independence rule: the resolution field never derives from the placement
   affine, and the placement never derives from the resolution.

9. **Kind identity is read back through the `KindBridge`, not a guess.**
   `arbc::Content` has no kind-id virtual and `ContentRecord` carries only a
   `std::uint64_t kind` token (`records.hpp:60-63`). `scene::cells()` seeds a
   `KindBridge` via `project::seed_kind_bridge` from the *same* registry and uses
   `KindBridge::lookup(token, id, ver)` (`document_serialize.hpp:63`) to name each
   layer's kind. Layers whose kind is `org.arbc.camera` are **excluded** from
   `cells()` — the cells/cameras split A14 and `cameras/model.md` Constraint 4
   require. A layer whose token does not resolve is reported with an empty
   `kind_id` rather than dropped (an unknown-passthrough cell is still a cell).

10. **Persistence must round-trip without registering a new kind.** This leaf adds
    **no** editor kind: every kind it inserts is already in `registry.ids()`, and
    `seed_kind_bridge` already interns all of them, so `project::save_project`
    (`src/project/save.cpp:96-152`, `arbc::builtin_codecs(registry)`) and
    `project::open_project` serialize cells generically. `register_editor_kinds`
    (`src/commands/app_state.cpp:29`) is **not** touched, so A15's
    rebuild-from-canonical branch is unchanged. A roundtrip test pins this.

11. **No new external dependency, no libarbc fork.** Content is constructed
    **only** through `registry.factory(id)` — never by naming a concrete arbc type
    — because direct construction is precisely the hard-coded kind set
    `docs/00-design.md:505-511` forbids. The consequence (a factory-built
    `org.arbc.solid` is unbounded, since its config grammar admits no bounds) is
    accepted and documented, not worked around by special-casing solid.

12. **Threading.** No new concurrency seam. Mutations are UI/writer-thread
    `transact`s funnelled through the one edit-runner (A4.1 identity rule); the
    render thread only ever `pin()`s. Multi-canvas fan-out is the existing
    `apply_edit` wake (`canvas_host.hpp:91-100`).

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`); `scripts/gate`
green (check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.** No
  new component, no new DAG edge, no lint edit. Explicitly asserted by inspection:
  `src/dock/` gains no `ace/commands` or `ace/scene` include; `src/interact/`
  gains no `ace/scene` include from the new signatures; `src/scene/`,
  `src/interact/`, `src/commands/` gain no ImGui/GL/SDL include.

- **L1 logic — Catch2 unit** (`tests/cell_model_test.cpp`, new file added to the
  `ace_tests` source list at `CMakeLists.txt:219-240`):
  - **No-allowlist enumeration:** `scene::insert_schemas(registry, …)` returns
    exactly one entry per `registry.ids()` entry, in registration order, for a
    registry seeded with `register_builtin_kinds` **plus a locally-registered
    editor-unknown probe kind**; the probe kind's schema is the raw-config
    fallback and is still insertable end-to-end. This is the test that would fail
    the moment someone adds a filter.
  - **Schema → config adapters:** `build_config` produces `"1024x768"` for raster
    from a `size` field, `"0.5,0,0,1"` for solid from a colour field, the decimal
    id for nested, and passes the raw field through verbatim for the fallback
    schema; malformed field values are rejected with an error, not a silent
    default.
  - **Resolution prefill (Constraint 8):** the raster schema's `size` initial value
    equals the root composition size for a probe document, `1024x1024` for a
    document with no composition; the field is always present and never
    auto-applied — an empty/invalid `size` fails the insert.
  - **`scene::add_cell` happy path:** for each of `org.arbc.solid`,
    `org.arbc.raster`, `org.arbc.nested`, the content and layer are minted,
    attached to the root composition, and `scene::cells()` reads them back in
    z-order with the expected `kind_id` and placement `Affine`.
  - **Failure paths mutate nothing (Constraint 3):** `org.arbc.fade`,
    `org.arbc.crossfade`, an unknown kind id, and a malformed raster config each
    return the kind's error string with `journal().depth()`, `revision()`, and
    `scene::cells().size()` all unchanged.
  - **Cells/cameras split (Constraint 9):** a document holding both a camera
    (`scene::add_camera`) and a cell reports exactly one from `scene::cells()` and
    exactly one from `scene::cameras()`; a content with an unresolvable token
    surfaces as a cell with an empty `kind_id`.
  - **Journal contract (Constraint 4):** `dispatch` of the insert command reports
    `journal_entries_added == 2`; one `undo` removes the cell from `scene::cells()`
    and one `redo` restores it with the same `ObjectId`.
  - **`interact::place_in_view`** (extending `tests/interact_test.cpp`): a known
    view affine + pane W×H + a known content `Rect` yields the expected placement
    affine (centred, uniform scale, fill fraction honoured); unbounded content
    yields identity; non-invertible view / zero pane / empty rect yield identity
    with no NaN.
  - **Persistence roundtrip (Constraint 10):** insert one cell of each of solid /
    raster / nested → `project::save_project` → `project::open_project` →
    `scene::cells()` returns identical kind/placement/order. Asserts that **no new
    kind registration** was needed.

- **Rendered output — golden** (`tests/goldens/cells_insert_nested_64x64.rgba8`,
  compared via `ace_test::compare_golden`, rendered by
  `render::render_document_srgb8` at 64×64). The fixture is
  `project::build_probe_document()` (green 64×64) plus a second composition holding
  an opaque solid, inserted into the root as an `org.arbc.nested` cell through the
  **shipped `scene::add_cell` path** at a placement computed by
  `interact::place_in_view`. Nested is the only factory-constructible **bounded,
  visible** built-in kind, so this is the one golden that proves factory route +
  computed placement + attach order compose to exact pixels — a solid-only golden
  would be a flat fill that passes even with the placement wrong. Two companion
  Catch2 byte-equality assertions in the same file pin the degenerate ends:
  inserting a freshly factory-built (transparent) `org.arbc.raster` leaves the
  rendered bytes **identical**, and inserting an opaque unbounded `org.arbc.solid`
  makes them **uniformly** that colour.

- **UI e2e — ImGui Test Engine** (`tests/cells_insert_e2e_test.cpp`, new file added
  to the `ace_shell_test` source list at `CMakeLists.txt:247-269`; modelled on
  `tests/history_e2e_test.cpp` and `tests/gc_ui_e2e_test.cpp`). Drives, by stable
  widget id, against a real `AppState` over a `ScratchDir` project:
  - open the modal from the rail (`Insert Cell…###insert_cell`) and assert the kind
    list length **equals `registry.ids().size()`** — the no-allowlist assertion at
    the UI layer;
  - select `org.arbc.raster`, assert the `size` field is present and prefilled,
    type a resolution, confirm (`Insert###insert_confirm`), and assert
    `scene::cells(state.document())` grew by one with the layer attached to the
    root composition;
  - select `org.arbc.fade`, confirm, and assert the modal stays open with the
    kind's error string rendered inline and `scene::cells()` unchanged;
  - `Cancel###insert_cancel` mutates nothing.

- **Threading (ASan/TSan).** The UI-driven insert runs inside
  `CanvasView::apply_edit` → `CanvasHost::apply_edit`; one case appended to
  `tests/canvas_host_test.cpp` drives repeated inserts through `apply_edit` on the
  **real-pool** `CanvasHost` path (`default_interactive_pool_config()`, the
  D-edit_render_sync-3 anchor) against a live rendering canvas and asserts
  sanitizer-clean plus a consistent final `scene::cells()` count. No new lock, no
  new thread; A4.1 writer-identity holds because every edit runs on the caller.

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed
  lines; clang-format + build clean. Tests ship with the task.

- **Deferred WBS work.** Two follow-ups, both concrete and agent-implementable
  (closer registers each as a real leaf with `effort`, `allocate team`, `depends`,
  and a `note` citing this refinement; both sit under existing containers that
  `tasks/99-milestones.tji:8` `m9_editor` already depends on, so no milestone
  `depends` edit is needed beyond the container):
  - **`editor.project.raster_tile_store`** — *1.5d*, `depends editor.cells.model`,
    under `task project` (milestone `m9_editor` via `editor.project`). Thread an
    `arbc::RasterTileStore` (and `TileDecodeDispatch`) through
    `project::save_project` → `arbc::builtin_codecs(registry, &tiles)`
    (`document_serialize.hpp:134`) and `project::open_project` →
    `arbc::load_document(..., tiles, decode)` (`:218`), so raster-cell tiles are
    memoized instead of re-hashed on every save. Today `save.cpp:120` passes null
    tiles and `project_open.cpp:97-98` passes none — correct but O(all tiles) per
    save. This leaf is the first that puts raster cells into a saved project, which
    is what makes it load-bearing.
  - **`editor.cells.remove`** — *1d*, `depends editor.cells.selection`, under
    `task cells` (milestone `m9_editor` via `editor.cells`). Delete the selected
    cell(s) via `arbc::Document::remove_content(content, composition, layer)`
    (`document.hpp:131` — atomic, **one** journal entry, undoable), with the Delete
    chord and a rail/inspector affordance plus an e2e. Insert without delete is the
    gap this leaf opens; the library verb already exists, so it is mechanical.

  Nothing else is deferred: the overview-wireframe placement is
  `editor.panels.overview` (`:350-355`, whose note already names this leaf's
  inserts as its payload), image-plugin linking + acquisition is
  `editor.import.image` (`:395-399`), painting into a raster is
  `editor.paint.brush` (`:373-377`), and selection/gizmo/resolution editing are
  `editor.cells.selection`/`gizmo`/`resolution` (`:316-333`) — all already
  scheduled leaves.

## Decisions

- **D-cells_model-1 — The insert affordance is a loop over `registry.ids()` with a
  raw-config fallback; there is no kind allowlist anywhere on the enumeration
  path.** `scene::insert_schemas(const arbc::Registry&, std::optional<project::CompositionSize>)`
  emits one `KindInsertSchema` per advertised id, unconditionally; a kind the
  editor has a grammar adapter for gets named fields, every other kind gets a
  single free-text `config` field passed to its factory verbatim.
  *Rationale:* `docs/00-design.md:505-511` closes the Extensibility question by
  naming *this leaf* as the enforcement point — "the editor consumes kinds only
  through the `Registry` seam (`registry.ids()` + factory/codec/binder), never a
  hard-coded kind set (see `editor.cells.model`)" — and the `.tji` note restates it
  as "arbitrariness is the core principle." Making the fallback a *first-class
  path* rather than an error is what makes the property testable: a probe kind the
  editor has never seen is inserted end-to-end in the unit suite.
  *Alternative rejected:* a switch over the four D3 kinds — it is exactly the
  hard-coded set the design forbids, and it would make `org.arbc.image` invisible
  until someone remembered to extend the switch, defeating the "surface in every
  affordance automatically" promise.
  *Alternative rejected:* filtering the enumeration to "kinds whose factory
  succeeds" — un-probeable without a config (the factory *is* the validator), and
  it would silently hide any plugin kind whose config is non-trivial.
  *Alternative rejected:* hiding `org.arbc.fade`/`crossfade` because their
  factories always fail — that is an allowlist by another name; instead
  Constraint 3 makes the kind's own error message the UI, which is honest and
  automatically correct for future kinds. **Doc delta: A16.**

- **D-cells_model-2 — The per-kind `ContentConfig` grammar lives in one L1 adapter
  table (`scene::build_config`), because libarbc exposes no insert-schema hook.**
  `arbc::ContentConfig` is `std::string_view` (`registry.hpp:43`) — deliberately
  opaque and kind-defined — and `Registry` advertises `factory`/`metadata`/`codec`/
  `binder`/`state_walker` but nothing describing a kind's *input fields*
  (`registry.hpp:142-164`). So some editor-side knowledge of the built-in grammars
  is unavoidable if the raster resolution field, the solid colour swatch, and the
  nested child picker are ever to be more than a raw string.
  *Rationale:* the honest framing is **adapters as enhancement, never as gate** —
  the enumeration (D-cells_model-1) is universal and the table only makes known
  kinds nicer. Keeping the table in L1 `scene` (not in `dock`) means it is
  Catch2-testable headless and the L3 modal renders whatever fields it is handed,
  so the UI layer *structurally cannot* hold an allowlist.
  *Alternative rejected:* a raw config text box for every kind — universal and
  allowlist-free, but it makes "specify the resolution at insert"
  (`docs/00-design.md:116-119`) a hex-editor experience and fails D3's promise that
  cells are ordinary things a user places.
  *Alternative rejected:* petitioning libarbc for a per-kind field-schema on
  `Registry` — the right long-term fix, but arbc is a pinned external dep and this
  leaf must not block on a cross-repo release; the gap goes to
  `tasks/parking-lot.md` as an upstream-issue candidate.
  **No new external dependency. Covered by A16.**

- **D-cells_model-3 — Content is constructed only through `registry.factory(id)`,
  and a factory-built `org.arbc.solid` is accepted as unbounded.**
  `SolidContent`'s C++ constructor takes `std::optional<Rect> bounds`, but its
  registered factory grammar is `"r,g,b,a"` with no bounds
  (`builtin_kinds.cpp:90-104`), so a solid inserted through the Registry paints
  everywhere — a legitimate "background fill" cell whose layer affine is a no-op.
  *Rationale:* bypassing the factory to pass bounds would require naming the
  concrete arbc type, i.e. exactly the per-kind construction D-cells_model-1
  forbids; and the alternative — special-casing solid — is the first brick in the
  allowlist wall. Constraint 7 makes the consequence explicit and safe (unbounded
  content gets identity placement rather than a meaningless scale), and the
  behaviour is pinned by the uniform-colour byte assertion.
  *Alternative rejected:* constructing `SolidContent` directly with a bounds rect
  derived from the view — nicer solids today, an allowlist tomorrow, and it would
  diverge from what `import`/plugin paths can do. The libarbc gap (solid's config
  grammar admits no bounds) goes to the parking lot as an upstream-issue
  candidate. **No doc delta required.**

- **D-cells_model-4 — Resolution is a required, always-visible field prefilled from
  the root composition size — not a blank field and not a silently-applied
  default.** `docs/00-design.md:116-119` (as amended by `4d72883`) requires that
  "the user specifies it at insert (resolution is a first-class placement input,
  not a fixed default)". *Rationale:* what the doc rejects is a value the user
  never sees and never chose — the old `2048²`-or-derived-from-camera text. A
  visible, editable field seeded from the composition's own size satisfies
  "specifies at insert" while giving a meaningful starting point, and blocking
  insert on an unparseable value keeps the user in the loop. It also keeps D8's
  independence: the field never derives from the placement affine.
  *Alternative rejected:* an empty field the user must fill — maximally literal,
  but it makes the common case a typing chore and gives no anchor for "how big is
  big here"; the doc's concern is visibility and authorship, not blankness.
  *Alternative rejected:* deriving the resolution from the camera the cell is
  created through — this is the *exact* wording the amended doc removed.
  **No doc delta required.**

- **D-cells_model-5 — The model lives in L1 `scene`, the placement math in L1
  `interact`, and the UI reaches them through a new `dock::ProjectGateway` insert
  seam implemented in L4 `app`.** `docs/01-architecture.md:143` assigns "cells ·
  cameras · selection · z-order" to `scene`; `dock` may not include `ace/commands`
  or `ace/scene`, so the two new virtuals exchange **dock-local POD**
  (`InsertKindSpec`/`InsertFieldSpec`), exactly as `clean_up` exchanges `GcSummary`
  (`dock.hpp:32`, `:105`). *Rationale:* this is A12/A13's established inversion —
  every session-acting rail verb (Save, Save As, Clean up, Undo/Redo) already
  crosses this way — so it costs no new component and no new DAG edge, and the L1
  half stays headless-testable. Putting the *schema* in `scene` rather than `dock`
  is what makes the no-allowlist property provable in `ace_tests` instead of only
  in the e2e.
  *Alternative rejected:* an Insert **panel** as a ninth `dockmodel::ViewType` —
  the view catalog is `editor.dock.view_registry`'s territory
  (`view_registry.hpp:19`, `k_view_type_count = 8`), and inserting is a one-shot
  confirmed op, which is what the two existing modals are for.
  *Alternative rejected:* drawing the modal in `views` (which *may* reach `scene`
  and `commands` directly, avoiding the POD marshalling) — but the trigger has to
  live on the rail, which is `dock`, and splitting a single modal across two L3
  components for a marshalling shortcut is worse than the POD.
  **Doc delta: A16** (`docs/01-architecture.md`), added in this task's commit.

- **D-cells_model-6 — Placement enters `scene::add_cell` as a finished
  `arbc::Affine`; the provisional view-centred placement is a pure `interact`
  helper.** `interact::place_in_view(view, pane_w, pane_h, content_bounds,
  fill_fraction)` takes primitives only. *Rationale:* it mirrors
  `interact::new_shot_from_view` (`interact.hpp:77`) and D-manip-2's rule that
  `interact` never takes a `scene` type, so no `interact→scene` edge is exercised;
  and it is the seam that lets `editor.panels.overview` later hand in a
  drag-derived affine, and `editor.import.image` a native-px→units 1:1 affine
  (D12), **without touching `scene`**. The `.tji` note's split — "this leaf ships
  the model-level insert (resolution + affine) with a provisional placement; the
  overview-wireframe placement lands with/after `editor.panels.overview`" — is
  realized as *which affine the caller computes*, nothing more.
  *Alternative rejected:* having `add_cell` read the active viewport camera and
  compute placement itself — it would drag session state into L1 `scene` and make
  the overview handoff a rewrite instead of a call-site change.
  *Alternative rejected:* shipping no UI at all this leaf and waiting for the
  overview — but the overview is four leaves downstream
  (`overview → layers → inspector → selection → model`), and `editor.paint.brush`
  (a direct dependent) needs a user-creatable raster cell before then. A
  provisional, clearly-labelled placement unblocks the chain honestly.
  **No doc delta required.**

- **D-cells_model-7 — The insert reuses the `commands` transaction/undo seam and
  inherits the two-journal-entry create; no bespoke atomicity.** *Rationale:*
  `Document::add_content` self-commits and is the only vtable-binding call
  (`document.hpp:107`; `Transaction::add_content` at `model.hpp:408` mints an inert
  unbound record), so a create is structurally two entries. `cameras/model.md`
  already shipped and recorded this (`model.md:391`, D-rename-4), and the D15
  observable contract holds — one undo detaches the layer and the cell disappears.
  Reproducing that shape keeps cells and cameras identical in the journal, which
  is what `editor.project.undo`'s history panel and `editor.cells.selection`
  assume. *Alternative rejected:* a compensating editor-side "collapse the two
  entries" wrapper — it would fabricate an undo semantics the library does not
  provide and would diverge cells from cameras. The libarbc gap (no atomic
  create-content-and-attach) is already a recorded cross-repo item, not a new one.
  **No doc delta required.**

- **D-cells_model-8 — Cell kind identity is read back through a seeded
  `KindBridge`, and camera layers are filtered out of `scene::cells()`.**
  `arbc::Content` exposes no kind-id virtual and `ContentRecord` carries only a
  token (`records.hpp:60-63`), so `cells()` seeds a bridge via
  `project::seed_kind_bridge` from the same registry and calls
  `KindBridge::lookup` (`document_serialize.hpp:63`). *Rationale:* it is the only
  registry-agnostic way to name a kind — a `dynamic_cast` chain would be the
  allowlist again, in the accessor. Filtering `org.arbc.camera` honours A14's
  "identical in shape to a cell" while keeping the two lists distinct, which
  `cameras/model.md` Constraint 4 requires and `panels.layers` (a cameras section
  plus a layers section, `:344-348`) depends on. Unresolvable tokens surface as a
  cell with an empty `kind_id` rather than vanishing, so an unknown-passthrough
  cell from a foreign `project.arbc` is still listable.
  *Alternative rejected:* a `dynamic_cast<CameraContent*>` test — works for the one
  editor kind today, silently misclassifies every future editor kind, and requires
  `scene` to know each kind's C++ type. **No doc delta required.**

- **D-cells_model-9 — This leaf registers no new kind, so A15's reopen policy is
  untouched.** Every kind it inserts is already in `registry.ids()` and already
  interned by `seed_kind_bridge`; `register_editor_kinds`
  (`src/commands/app_state.cpp:29`) is not modified. *Rationale:* A15
  (`docs/01-architecture.md:281`) forces `open_project` off the workspace-map fast
  path whenever a non-empty extra-kinds callback is supplied; adding a kind here
  would widen that policy's blast radius for no gain, since the built-in cell
  kinds' durable state already rides the codec path. The roundtrip test pins that
  save/open stay generic. **No doc delta required.**

## Open questions

(none — all decided.) Three items are genuinely cross-repo or human-judgment and
go to `tasks/parking-lot.md` rather than the WBS (none of them is an "audit"
task; each is an upstream-issue candidate a human decides whether to file):

1. `arbc::Registry` advertises no per-kind **insert schema** (field descriptors for
   the opaque `ContentConfig`), which is why the editor must carry the
   D-cells_model-2 grammar-adapter table; a future `KindInsertSchema` on the
   registry would let the table shrink to zero.
2. `org.arbc.solid`'s registered factory grammar admits **no bounds**
   (`builtin_kinds.cpp:90-104`), so a Registry-constructed solid cell is always
   unbounded (D-cells_model-3).
3. `arbc::Document` has no atomic **create-content-and-attach** (the mirror of
   `remove_content`, `document.hpp:131`), which is why every create costs two
   journal entries (D-cells_model-7) — already observed by `cameras/model.md`.

Both deferred *implementation* follow-ups (`editor.project.raster_tile_store`,
`editor.cells.remove`) are named under Acceptance criteria for mechanical
registration.

## Status

**Done** — 2026-07-22.

- `scene::{insert_schemas,build_config,probe_bounds,add_cell,cells}` in new files `src/scene/ace/scene/cell.hpp` + `src/scene/cell.cpp` — registry-driven, allowlist-free enumeration, grammar adapters for raster/solid/nested/fallback, factory-first error propagation (Constraint 3), two-entry create (Constraint 4), `KindBridge`-based read-back filtering cameras out (Constraint 9).
- `interact::place_in_view` added to `src/interact/ace/interact/interact.hpp` + `src/interact/interact.cpp` — pure-affine provisional placement (centred, fill-fraction, unbounded→identity, degenerate-input→identity per Constraint 7).
- `commands::insert_cell_command` in new files `src/commands/ace/commands/cells.hpp` + `src/commands/cells.cpp` — verb through `dispatch`/`AppState` (Constraint 4/5).
- "Insert Cell…" rail modal in `src/dock/{ace/dock/dock.hpp,dock.cpp}` + two new `ProjectGateway` virtuals in `src/app/{ace/app/project_gateway.hpp,project_gateway.cpp,ace/app/canvas_view.hpp,canvas_view.cpp}`; `src/app/shell.cpp` + `src/project/project_open.cpp` updated to seed `KindBridge` on canonical-rebuild path; A4.1a note added to `docs/01-architecture.md`; `src/app/ace/app/view_framing.hpp` added for dock-local POD crossing the A12 seam.
- TSan fix (attempt 2): writer-priority document lease in `src/render/{ace/render/canvas_host.hpp,canvas_host.cpp}` — held for `apply_edit` and `run()` iterations; reader waits for zero writers (avoids the livelock of plain-mutex `doc_mu`); A4.1a documents the residual arbc#13 identity contract.
- Tests: `tests/cell_model_test.cpp` (13 Catch2 cases: no-allowlist enumeration incl. editor-unknown probe kind e2e, grammar adapters, resolution prefill, add_cell happy/failure paths, cells/cameras split, 2-entry journal + undo/redo, save/open roundtrip); golden `tests/goldens/cells_insert_nested_64x64.rgba8` with two byte-equality companions; `tests/cells_insert_e2e_test.cpp` (3 ImGui Test Engine cases); 4 `place_in_view` cases in `tests/interact_test.cpp`; one TSan/ASan streamed-insert case in `tests/canvas_host_test.cpp`; `CMakeLists.txt` updated to include new source files and tests.
- Recorded deviations: `scene::cells` takes `(document, registry)` (Constraint 9 requires registry to seed `KindBridge`); `project_open.cpp` canonical-rebuild path now calls `seed_kind_bridge` before `load_document` (tokens would be file-encounter-ordered without it; bytes unchanged).
- Tech-debt follow-ups registered in WBS: `editor.project.raster_tile_store` (1.5d) and `editor.cells.remove` (1d).
