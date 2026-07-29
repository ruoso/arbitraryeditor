# editor.cells.insert_schema — Drive the insert dialog off the library's per-kind insert schema

## TaskJuggler entry

`task insert_schema` in [`tasks/00-editor.tji`](../../00-editor.tji) (§ `editor.cells`, line 546).
Back-link: `Refinement: tasks/refinements/editor.cells/insert_schema.md` (the closer appends this to the task `note`).

## Effort estimate

`2d` (from the `.tji`).

## Inherited dependencies

- **`depends editor.canvas.arbc_v040`** — *settled.* The v0.4.0 pin is live
  (`CMakeLists.txt:25` = `v0.4.0`; refinement `tasks/refinements/canvas/arbc_v040.md`
  **Done 2026-07-28**). That leaf bumped the pin, re-baselined four goldens, and added a
  **compile-time existence/shape witness** for the `Registry`'s `KindInsertSchema` seam to
  `tests/arbc_pin_test.cpp` (a stale v0.3.0 tree fails to *compile*). It deliberately holds
  **no behavioural spec** for arbc#21/#22 — "behavioural consumption is each downstream
  leaf's job." This leaf is that job for #21/#22.
- **`depends !model`** (`editor.cells.model`) — *settled.* Shipped **Done 2026-07-22**
  (`tasks/refinements/editor.cells/model.md`). It built the entire editor-side insert stack:
  `scene::insert_schemas`/`build_config`/`probe_bounds`/`add_cell`/`cells`
  (`src/scene/ace/scene/cell.hpp`, `src/scene/cell.cpp`), the home-grown
  `KindInsertSchema`/`InsertField`/`InsertFieldType` types, the raster/solid/nested grammar
  adapters, the `k_raw_config_field` fallback, the two `dock::ProjectGateway` virtuals
  (`insert_kinds()`/`insert_cell(...)`) and their dock-local POD
  (`InsertKindSpec`/`InsertFieldSpec`), the L3 rail modal, and the L4 marshalling. **This
  leaf rewires the internals of that stack; it does not rebuild it.**
- **Transitively settled and relied on:** `editor.cells.one_action_one_entry`
  (**Done 2026-07-28**) — `scene::add_cell` already routes through
  `Document::create_content_and_attach`, so a create is **one** journal entry / one undo. This
  leaf preserves that; it changes only how the config string is produced, not how the content
  is minted.

*No pending dependency.* Everything this leaf consumes exists on disk (the fetched v0.4.0
library at `build/dev/_deps/arbc-src/`).

## What this task is

Replace the editor's home-grown per-kind **grammar adapters** — the raster `"<w>x<h>"`,
solid `"r,g,b,a"`, nested `"<decimal ObjectId>"` branches in `scene::insert_schemas`
(`src/scene/cell.cpp:185-195`) and `scene::build_config` (`:221-269`), keyed off the three
literals `k_raster_kind`/`k_solid_kind`/`k_nested_kind` (`:32-34`) — with libarbc v0.4.0's
**`arbc::KindInsertSchema`**, read back through **`Registry::insert_schema(id)`**. After this
leaf the editor **renders fields and hands back strings**: the field list, their types,
defaults, ranges and units come from the kind's own advertised schema, and the config string
is produced by the kind's own **`assemble` thunk**. The editor stops knowing that solid's
separator is a comma or that raster's is an `x`.

Two things ride along:

1. **org.arbc.solid gains an extent grammar** (arbc#22): the schema advertises
   `r,g,b,a,x,y,w,h` (origin + size, defaulting to a placeable `256×256`) beside the still-valid
   four-field `r,g,b,a`. A factory-built solid is now **bounded** and takes real
   `interact::place_in_view` placement — retiring D-cells_model-3's accepted unbounded-solid
   consequence and giving `scene::probe_bounds` a real `Rect` for a default solid instead of
   the honest `nullopt`.
2. **The editor registers an insert schema for its OWN `org.arbc.camera` kind**
   (`src/scene/camera.cpp:441`), because the editor is now a plugin author held to the same
   rule it applies to everyone else.

The no-allowlist property (A16 / Constraint 2) is **preserved exactly**: `insert_schemas`
still emits one entry per `registry.ids()` entry, unconditionally and in registration order;
a kind that advertises no schema upstream (nested, fade, crossfade) keeps the editor's
raw-config fallback (`k_raw_config_field`) and stays insertable end-to-end.

## Why it needs to be done

`editor.cells.model` shipped the grammar-adapter table as an explicit stopgap. Its own
Open Question 1 (`model.md:705-708`) named the exit: *"a future `KindInsertSchema` on the
registry would let the [grammar-adapter] table shrink to zero"*, and D-cells_model-2 sent the
gap to `tasks/parking-lot.md` as an upstream-issue candidate — "arbc is a pinned external dep
and this leaf must not block on a cross-repo release." A16's own **Future fix** row promises
the same: *"a per-kind insert-schema hook on `arbc::Registry` would shrink the adapter table
to zero."* v0.4.0 (arbc#21) ships that hook. This leaf **realizes the anticipated fix**:

- **Correctness of the abstraction.** With the grammar in the editor, every kind the editor
  had not hand-written an adapter for (e.g. `org.arbc.tone`) fell to a raw-config text box even
  though the kind *could* describe its fields. Reading `Registry::insert_schema(id)` makes named
  fields appear for **any** kind that advertises them — automatically, with zero editor code per
  kind. That is the plugin seam working as designed.
- **One validator.** Grammar parsing lived twice — in the kind's factory and in the editor's
  `build_config`. Delegating to `assemble` (which only joins) leaves the kind's factory as the
  single validator; a malformed value surfaces the *kind's own* error string at `add_cell`,
  errors-as-values, with no editor-side re-implementation to drift.
- **The unbounded-solid trap.** A Registry-built solid was always unbounded, so its layer
  transform was a no-op: placing, scaling or rotating one changed nothing on screen. arbc#22
  fixes the grammar and the built-ins default the extent, so an inserted solid is placeable —
  which is what `interact::place_in_view` needs *before* `add_cell` mints anything (Constraint 6).

Downstream consumers are the whole cell-insert affordance (the "Insert Cell…" rail modal) and,
indirectly, `editor.panels.overview` (drag-placement of a now-bounded solid) and selection
(a bounded solid becomes marquee-selectable — see Constraints).

## Inputs / context

**Governing design docs (normative):**

- **`docs/01-architecture.md` A16** (line 431) — Registry-driven, no-allowlist cell insert;
  the L1 schema seam; the `ProjectGateway` POD marshalling; factory-only construction;
  placement-as-finished-`Affine`. Its **Future fix** clause is what this leaf closes. **Doc
  delta:** this leaf amends A16 with a parenthetical recording the adapter-table retirement, the
  solid extent adoption, the camera-schema registration, and the prefill retirement (written into
  `docs/01-architecture.md:431`, rides the closer's commit, same-commit rule).
- **`docs/00-design.md` D3** (line 470) — the cell-kind catalog (images · rasters · nested ·
  solids/procedural). Unchanged by this leaf.
- **`docs/00-design.md:116-119`** — "A new paint cell needs a working resolution — the user
  specifies it at insert (resolution is a first-class placement input, not a fixed default)…".
  The library's raster `width`/`height` fields keep resolution a first-class, visible, editable
  input, so this requirement is **still satisfied** after the editor's composition-matched
  prefill is retired (D3 mandates user-specified, not composition-matched).
- **`docs/00-design.md:505-517`** — Extensibility: "the editor consumes kinds only through the
  `Registry` seam … never a hard-coded kind set." The load-bearing rule the no-allowlist
  property enforces.

**Library API consumed (fetched at `build/dev/_deps/arbc-src/`):**

- `src/contract/arbc/contract/registry.hpp:107-116` — `struct KindInsertField { std::string name;
  enum class Type { Integer, Number, Text } type; std::string default_value; std::optional<double>
  min, max; std::string unit; }`.
- `:134-137` — `struct KindInsertSchema { std::vector<KindInsertField> fields;
  std::function<expected<std::string,std::string>(std::span<const std::string> values)> assemble; }`.
  `assemble` consumes the collected values **in `fields` order** (positional).
- `:176-181` — `Registry::add(id, factory, metadata, codec, binder, state_walker, insert_schema)`
  — the schema rides the **same atomic `add`** as the factory (a plugin cannot describe another
  kind's config after the fact).
- `:201-205` — `const KindInsertSchema* Registry::insert_schema(std::string_view id) const` —
  **nullptr** when the kind is absent or registered without a schema, "in which case a host
  falls back to a raw config text box, exactly as it must today."
- `src/builtin_kinds.cpp:88-128` — `make_solid` accepts `r,g,b,a` (unbounded) **or**
  `r,g,b,a,x,y,w,h` (bounded to `Rect{x, y, x+w, y+h}`; non-positive `w`/`h` is an **error
  value**; origin+size cannot express an inverted rect).
- `:244-270` — the built-in registrations and their schemas:
  - **solid**: 8 fields `red,green,blue,alpha` (`Number`, default `1`, `[0,1]`), `x,y`
    (`Number`, default `0`, unit `px`), `width,height` (`Number`, default `256`, unit `px`),
    `assemble = joined(',')`.
  - **tone**: `frequency` (`Number`, default `440`, `Hz`), `amplitude` (`Number`, default
    `0.5`, `[0,1]`), `joined(',')`.
  - **raster**: `width,height` (`Integer`, default `1024`, min `1`, `px`), `joined('x')`.
  - **fade / crossfade**: registered with **no schema** → `insert_schema(id) == nullptr`.
  - **nested** (`:270`): registered with **no schema** → `insert_schema(id) == nullptr`.

**Editor seams to rewire (real paths, current line numbers):**

- `src/scene/ace/scene/cell.hpp:43-48` — `enum class InsertFieldType { Text, Size, Color,
  ObjectRef }` (grammar-semantic; Size/Color/ObjectRef become obsolete).
- `:51-59` — `struct InsertField { std::string id; std::string label; InsertFieldType type;
  std::string initial; }` (no default/range/unit members today).
- `:62-70` — `struct KindInsertSchema { std::string kind_id; std::string human_name;
  std::vector<InsertField> fields; bool raw_config = false; }`.
- `:73` — `inline constexpr std::string_view k_raw_config_field = "config";`.
- `:75-77` — `k_fallback_resolution = 1024` (the composition-prefill fallback — retired here).
- `:83-85` — `insert_schemas(const arbc::Registry&, std::optional<project::CompositionSize>
  composition)` — the `composition` param is retired (the raster kind now owns its defaults).
- `:95-96` — `build_config(const KindInsertSchema&, const InsertValues&)`.
- `:104-105` — `probe_bounds(const arbc::Registry&, kind_id, config)` — unchanged surface; now
  returns a real `Rect` for a default solid.
- `:121-123` — `add_cell(...)` — unchanged (already routes through `create_content_and_attach`).
- `src/scene/cell.cpp:32-34` — the three adapter-key literals to **delete**.
- `:106-124` — `size_initial` (composition-aware resolution prefill) to **delete**.
- `:168-206` — `insert_schemas` body (the per-kind branches to **replace** with a
  `registry.insert_schema(id)` read).
- `:208-273` — `build_config` body (the per-kind grammar/validation to **replace** with an
  `assemble` delegation + verbatim raw-config path).
- `src/scene/camera.cpp:441-442` — `registry.add(CameraContent::kind_id, factory,
  KindMetadata{"Camera", …}, codec)` — the 4-arg form to extend with an `insert_schema` argument.
- `src/app/project_gateway.cpp:289-309` (`insert_kinds`) and `:311-349` (`insert_cell`) — the L4
  marshalling; `:294` passes `root_composition_size(document)` to `insert_schemas` (drop it);
  `:304` builds `InsertFieldSpec{field.id, field.label, field.initial}` (compose the label
  with the unit here).
- `src/dock/ace/dock/dock.hpp:48-64` — `InsertFieldSpec`/`InsertKindSpec` POD (**unchanged**:
  `{id,label,initial}` / `{kind_id,human_name,fields}` — the modal renders free text regardless).
- `src/dock/dock.cpp:136-196` — `draw_insert_cell_modal` (renders each field as `InputText`,
  collects `InsertValues` positionally at `:177-179`) — **unchanged**.

**Tests to update:**

- `tests/cell_model_test.cpp` — `insert_schemas` one-per-id (`:139`), `build_config` grammars
  (`:205`), resolution prefill (`:248,254` — retired), `add_cell` mints (`:271`), fade refusal
  (`:446`), `probe_bounds` (`:618`).
- `tests/cells_insert_e2e_test.cpp` — `insert_kinds().size() == registry.ids().size()` (`:184`),
  `>= 6` built-ins (`:185`), raster/unknown/malformed/fade drives (`:309-322`).
- `tests/goldens/cells_insert_nested_64x64.rgba8` + its byte companions (see Acceptance).
- `tests/canvas_host_test.cpp` — the insert TSan anchor.

## Constraints / requirements

1. **No-allowlist property is inviolate (A16 / Constraint 2).** `insert_schemas(registry)` emits
   **exactly one** `KindInsertSchema` per `registry.ids()` entry, unconditionally and in
   registration order. `insert_schemas(r).size() == r.ids().size()` must still hold. No `if (id ==
   …)` gate, no filter by metadata, by "is it visual", or by "does its factory succeed" anywhere
   on the enumeration path.

2. **The raw-config fallback survives and is the null path.** When `registry.insert_schema(id) ==
   nullptr`, the editor emits a `raw_config = true` schema carrying the single `k_raw_config_field`
   (`"config"`) `Text` field, passed to the factory verbatim. This is what nested, fade and
   crossfade now use (they advertise no upstream schema), and it is what keeps the property
   testable with an editor-unknown probe kind. It is **not** retired by this leaf.

3. **Grammar stays the kind's own.** The editor must contain **no** knowledge of any kind's
   separator or field order: the three literals `k_raster_kind`/`k_solid_kind`/`k_nested_kind`
   and every per-kind branch in `insert_schemas`/`build_config` are deleted. `build_config` for a
   library-backed schema pulls the collected values **positionally** (in field order — matching
   `assemble`'s `std::span<const std::string>` contract, robust to duplicate field names) and
   calls the kind's `assemble` thunk, returning its `expected<std::string,std::string>` verbatim.
   For a raw-config schema it returns the one `config` value verbatim.

4. **Validation is the factory's, not the editor's.** `assemble` only joins; it does not
   validate. A malformed value (non-numeric solid channel, non-positive solid size, zero raster
   extent) is caught by `registry.factory(id)` at `add_cell` and its **own** error string is
   returned with the `Document` untouched (no content minted, no transaction, zero journal
   entries — Constraint 3 of `model.md`). `build_config` no longer parses or rejects values.

5. **Field→POD marshalling carries the library's display metadata.** `insert_schemas` populates
   each editor `InsertField` from an `arbc::KindInsertField`: `id` = the field `name`, `label` =
   `name` plus `" (" + unit + ")"` when the unit is non-empty, `initial` = `default_value`, and
   `type` mapped from `arbc::KindInsertField::Type`. `min`/`max` are **not** surfaced (the factory
   validates). The dock POD (`InsertFieldSpec{id,label,initial}`) is unchanged.

6. **Placement still arrives as a finished `Affine` (Constraint 6, unchanged).** `add_cell` takes
   the placement as a computed `arbc::Affine`; the flow stays `probe_bounds(kind,config)` →
   `interact::place_in_view(view, pane_w, pane_h, bounds, fill_fraction)` → `add_cell`. A default
   solid's `probe_bounds` now returns a real `Rect` (256×256) rather than `nullopt`, so it is
   centred and scaled rather than identity-placed. The 4-field solid remains unbounded
   (`nullopt` → identity), the honest "background fill".

7. **One journal entry per insert (unchanged).** `add_cell` continues to route through
   `Document::create_content_and_attach` (D-one_action_one_entry-1): one entry, one undo, no
   intermediate attached-to-nothing state.

8. **The editor's own camera kind advertises a schema (`camera.cpp:441`).** Extend the
   `registry.add(...)` to pass an `arbc::KindInsertSchema` with **zero fields** and an `assemble`
   returning the empty string — the camera factory is a benign default-construct that takes no
   config, so no field is meaningful. `registry.insert_schema("org.arbc.camera")` then returns
   this schema and the editor emits a zero-field (non-`raw_config`) entry for it, rather than the
   raw-config box a null pointer would have produced.

9. **Levelization: no new component, no new DAG edge, no new external dependency.** All work is in
   L1 `scene` (reading a new arbc API already reachable through `scene`'s existing arbc
   dependency), L1 `camera`, and the existing L4 marshalling. L1 `scene` gains no ImGui/GL/SDL
   include. The dock POD and the `ProjectGateway` virtuals are unchanged. `scripts/check_levels.py`
   must stay clean.

## Acceptance criteria

The universal DoD (`docs/01-architecture.md` §9) instantiated here:

- **Levelization — `check_levels` clean.** No component or edge added; the L1 core gains no
  ImGui/GL/SDL include; the new `Registry::insert_schema`/`KindInsertSchema` usage rides `scene`'s
  existing arbc edge. Verified by `python scripts/check_levels.py` staying silent.

- **L1 Catch2 (`tests/cell_model_test.cpp`, the bulk of the coverage) — updated/added cases:**
  1. **No-allowlist preserved.** Register a foreign kind **with** a library schema and one
     **without**; assert `insert_schemas(r).size() == r.ids().size()`, that the schema-less kind
     emits `raw_config == true` with the single `k_raw_config_field`, and that the schema-bearing
     kind emits its advertised named fields.
  2. **Library-backed field mapping.** Assert solid emits 8 fields `red,green,blue,alpha,x,y,
     width,height` with defaults `1,1,1,1,0,0,256,256` and `px` units on `x,y,width,height`;
     raster emits `width,height`; **tone emits `frequency,amplitude` — a kind the editor never
     wrote an adapter for now gets named fields automatically** (the headline property).
  3. **`build_config` delegates to `assemble`.** Solid values → `"r,g,b,a,x,y,w,h"` (comma-joined
     by the kind); raster → `"WxH"` (x-joined by the kind); a raw-config kind → the value
     verbatim. A source-level check that `k_raster_kind`/`k_solid_kind`/`k_nested_kind` and the
     per-kind `build_config` branches are gone.
  4. **Validation moved to the factory.** A malformed solid channel and a non-positive solid
     `width` return the kind's error string **at `add_cell`** (not `build_config`), `Document`
     untouched, zero journal entries — the assertion `model.md` put on `build_config` moves to
     `add_cell`.
  5. **Bounded default solid.** `probe_bounds(registry, "org.arbc.solid", "1,1,1,1,0,0,256,256")`
     returns a non-null `Rect`; `add_cell` of a default solid yields a **non-identity** layer
     affine (placed via `place_in_view`). The 4-field `"1,1,1,1"` still probes `nullopt` and
     identity-places (the unbounded fill).
  6. **Journal contract unchanged.** `add_cell` = **one** entry; one undo restores baseline with
     no orphan content (inherited from `one_action_one_entry`, kept green).
  7. **Camera schema.** `insert_schemas` emits a **zero-field, non-`raw_config`** entry for
     `org.arbc.camera`; `registry.insert_schema("org.arbc.camera")` is non-null with empty
     `fields`.

- **Rendered output — golden + byte companions (`render::render_document_srgb8` at 64×64):**
  - `tests/goldens/cells_insert_nested_64x64.rgba8` — **unchanged / no re-baseline.** Nested now
    inserts through the raw-config fallback ("config" box), but the config string and resulting
    content are byte-identical, so the render is identical. (If any byte drifts, treat it as a
    regression, not a re-baseline.)
  - The **opaque-solid uniform-fill** byte companion (`model.md`) must be re-derived: a
    dialog-default (8-field) solid is now a **bounded, placed** rectangle, not a full-frame fill.
    Keep a uniform-fill assertion by exercising the explicit **4-field** `"r,g,b,a"` config (still
    unbounded → fills the frame), and add a companion asserting the 8-field default renders as a
    placed (non-full-frame) rectangle — pinning both grammars.
  - The transparent-raster byte-identity companion is unaffected (transparent stays transparent).

- **UI e2e (ImGui Test Engine, `tests/cells_insert_e2e_test.cpp`), driven headless by widget id
  (`Insert Cell…###insert_cell`, `Insert###insert_confirm`, `Cancel###insert_cancel`):**
  - `dockspace.insert_kinds().size() == state.registry().ids().size()` still holds (`:184`), still
    `>= 6` built-ins.
  - Selecting **solid** now renders **8** fields prefilled `1,1,1,1,0,0,256,256`; Insert mints a
    bounded, placed solid.
  - Selecting **raster** renders `width,height` prefilled `1024,1024` (library default — no
    composition-matched prefill).
  - An **editor-unknown kind** renders the single raw-config `config` box and is insertable.
  - **fade** renders the raw-config box; Insert surfaces the kind's own error string, modal stays
    open, `Document` untouched.
  - Ctrl+Z / Ctrl+Y round-trips a single insert (one entry) — inherited, kept green.

- **Threading (ASan/TSan).** No new threading surface: this leaf changes only L1 pure data
  (`insert_schemas`/`build_config`) and L4 marshalling; minting is the same `apply_edit`
  writer-identity path. The existing `tests/canvas_host_test.cpp` insert case is the anchor and
  must stay green under the `asan`/`tsan` presets. No new TSan case is required.

- **Diff coverage ≥ 90%** on changed lines (CI gate); tests ship with the task.

- **No deferred WBS follow-ups.** The core rewire, the solid extent adoption, the camera schema,
  and all test churn land in this leaf. Two items are **human-judgment**, not agent-implementable
  WBS work, and go to `tasks/parking-lot.md` (surfaced in the return summary), **not** the WBS —
  see Open questions.

## Decisions

- **D-insert_schema-1 — `insert_schemas` reads `Registry::insert_schema(id)`; the adapter table
  is deleted, not refactored.** For each `registry.ids()` entry the editor calls
  `registry.insert_schema(id)`: non-null → map its `KindInsertField`s to editor `InsertField`s
  and copy nothing else the modal needs; null → the `raw_config` fallback. The three literals and
  every per-kind branch in `insert_schemas`/`build_config` are removed.
  *Rationale:* this is the exact fix A16's "Future fix" row and `model.md` Open Question 1
  predicted; keeping any adapter branch would reintroduce editor-side kind knowledge the whole
  leaf exists to remove. *Alternative rejected:* keep the adapters as a "richer than the library"
  layer — pointless divergence, and it re-plants the allowlist coupling.

- **D-insert_schema-2 — `build_config` delegates to the kind's `assemble`; validation moves to
  the factory.** `build_config` collects values positionally and calls `schema.assemble(values)`
  (library kinds) or returns the one `config` value verbatim (raw-config). It no longer parses or
  rejects. *Rationale:* one validator (the kind's factory), errors-as-values surfaced at
  `add_cell`; the editor cannot drift from the kind's grammar because it never re-implements it.
  *Alternative rejected:* keep editor-side pre-validation for nicer inline errors — duplicates the
  kind's parser and is exactly the coupling #21 removes; the factory's error string is already the
  UI.

- **D-insert_schema-3 — values are consumed positionally, not by id lookup.** `assemble` takes a
  `std::span<const std::string>` in `fields` order; `build_config` iterates `schema.fields` by
  index and takes each collected value in order. *Rationale:* matches the library contract and is
  robust to a (hostile or careless) plugin advertising duplicate field names, which a
  `find_value(id)` lookup would mis-pair. The `id` remains only the modal's label-pairing key.

- **D-insert_schema-4 — the editor registers a zero-field schema for `org.arbc.camera`.** The
  camera factory is a benign default-construct taking no config, so its honest schema has no
  fields and an `assemble` returning `""`. *Rationale:* the editor is a plugin author under its
  own no-allowlist rule — its kind appears in the enumeration (it already did, as a raw-config
  box), so it should advertise a proper (empty) schema rather than show a meaningless text box for
  its own kind. *Alternative rejected:* leave camera schema-less (raw-config box) — the task note
  explicitly requires the editor to practise what it preaches, and a config box for a
  config-ignoring kind is dishonest UI. *Alternative rejected:* exclude camera from the insert
  enumeration — that is a kind allowlist, forbidden by A16 (and the exclusion, if wanted, is a
  human-judgment call about the cells-vs-cameras model, not this mechanical leaf — see Open
  questions).

- **D-insert_schema-5 — org.arbc.solid's extent grammar is adopted; D-cells_model-3's
  unbounded-solid consequence is retired.** The default insert produces a bounded `256×256` solid
  that `place_in_view` centres and scales; the 4-field grammar still yields the unbounded
  background fill. *Rationale:* arbc#22 gives the solid the extent it always supported, so
  `probe_bounds` returns a real answer and a placed solid is draggable/scalable (the trap #22
  fixes). *Consequence noted:* selection.md D-selection-5 excludes **unbounded** solids from
  marquee — a now-bounded default solid becomes marquee-selectable, which is a strict improvement
  and needs no selection-code change (the exclusion still correctly skips 4-field unbounded
  solids). No expansion of the selection leaf's scope; the insert e2e may assert the bounded solid
  is pickable.

- **D-insert_schema-6 — the composition-matched resolution prefill is retired; the raster kind
  owns its defaults.** `size_initial`, `k_fallback_resolution`, and the
  `std::optional<project::CompositionSize>` parameter of `insert_schemas` are removed; the raster
  schema's advertised `1024×1024` default is used verbatim. *Rationale:* `docs/00-design.md:116-119`
  requires resolution to be a **first-class, user-specified, editable** input, which the still-editable
  `width`/`height` fields satisfy; it does **not** mandate a composition-matched *default*. Keeping
  the prefill would require the editor to know that raster's first two fields mean "resolution in
  px" — reintroducing precisely the kind-specific coupling this leaf removes. *Alternative
  rejected:* a generic "seed numeric px fields from composition size" heuristic — brittle
  (misfires on any kind with px fields that are not a resolution) and still kind-guessing. If a
  composition-matched default is later wanted, it is a deliberate host feature layered back on,
  not a silent adapter.

- **D-insert_schema-7 — `InsertFieldType` mirrors the library's display types.** Replace the
  grammar-semantic `{Text, Size, Color, ObjectRef}` with the library's `{Integer, Number, Text}`
  (mapped from `arbc::KindInsertField::Type`). *Rationale:* Size/Color/ObjectRef were adapter
  hints for grammars the editor no longer owns; the remaining need is a display hint. *Low-stakes
  note:* the dock POD deliberately drops the type (the modal renders free text), so this is
  presentation-only today; mirroring the library keeps the L1 type honest and leaves the door open
  for numeric widgets without another schema change. *Alternative considered:* delete
  `InsertFieldType` entirely — defensible since it is unused downstream, but keeping a truthful
  three-value hint costs nothing and documents intent.

## Open questions

`(none blocking — the two items below are human-judgment calls surfaced to `tasks/parking-lot.md`,
not WBS work; neither blocks this leaf's implementation.)`

1. **Should `org.arbc.camera` be insertable through the cell-insert dialog at all?** The
   no-allowlist enumeration lists it (it did before this leaf too), and this leaf makes its entry
   honest (zero fields → a default placeholder camera). But a camera minted via `add_cell` skips
   the identity/state setup `scene::add_camera` performs, so a dialog-inserted camera may be a
   subtly under-initialised camera. This is a **pre-existing** property of the no-allowlist model
   (D7 / A14 make cells and cameras one shape) and a genuine design judgment about the
   cells-vs-cameras seam — not a mechanical fix and not an "audit" task. → parking lot.
2. **Should `org.arbc.nested` advertise an upstream insert schema?** Upstream v0.4.0 registers
   nested with **no** schema, so nested inserts fall to the editor's raw-config box (a decimal
   `ObjectId`), losing `model.md`'s labelled "Child composition (ObjectId)" field. This is the
   correct consequence of the kind owning its grammar, but a nicer nested insert UX needs a
   cross-repo change to `arbitrarycomposer` (petition #21's author for a nested schema). Cross-repo
   / external work → parking lot, not a WBS leaf.

## Status

**Done** — 2026-07-28.

- `src/scene/ace/scene/cell.hpp` — `InsertFieldType` replaced with `{Integer,Number,Text}`; `KindInsertSchema` gains an `assemble` thunk; dropped `composition` param and `k_fallback_resolution`.
- `src/scene/cell.cpp` — `insert_schemas` reads `Registry::insert_schema(id)` and mirrors each kind's fields (id/label+unit/default/type) plus carries its assembler; `build_config` collects positionally and delegates to `assemble`; deleted the three adapter literals, grammar parsers, and `size_initial`.
- `src/scene/camera.cpp` — registers a zero-field `KindInsertSchema` (`assemble` → `""`) for `org.arbc.camera`.
- `src/app/project_gateway.cpp` — dropped the composition arg at both `insert_schemas` call sites.
- `tests/cell_model_test.cpp` — Catch2 units for build_config-delegates-to-assemble; library-backed field mapping incl. tone/camera; foreign kind with a library schema covering the `Text` map; bounded-default-solid render companion beside the 4-field uniform-fill; malformed-value-moves-to-factory.
- `tests/cells_insert_e2e_test.cpp` — updated for raster `width`/`height` two-field schema, solid 8-field render, and factory-side malformed refusal.
- `tests/app_project_gateway_test.cpp`, `tests/multi_canvas_mint_e2e_test.cpp`, `tests/cells_remove_e2e_test.cpp` — fixed three shell-test sites that still drove raster insert via the retired single-`size` grammar; updated to two-field `{width,height}` shape.
- `docs/01-architecture.md` — A16 doc delta recording adapter-table retirement and solid-extent adoption rides this commit (same-commit rule).
