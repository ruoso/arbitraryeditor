# objectid_field_picker — Render an ObjectId insert-schema field as a composition picker, not free text

## TaskJuggler entry

`tasks/00-editor.tji:645-650`, under `task cells "Cells & manipulation"`:

```
task objectid_field_picker "Render ObjectId insert-schema field as a composition picker, not free text" {
  effort 1.5d
  allocate team
  depends editor.canvas.arbc_v070, editor.cells.insert_schema
  note "The v0.5.0 org.arbc.nested insert schema advertises one `child` field of type `ObjectId` … "
}
```

This refinement lands at **`tasks/refinements/editor.cells/objectid_field_picker.md`** per the
area-subdir layout (`tasks/refinements/README.md:9-18`); the closer appends the back-link to the
note and adds `complete 100` after `allocate team` (`tasks/refinements/README.md:47-68`). **Do not**
hand-edit the `.tji` here.

**Source of debt:** `tasks/refinements/canvas/arbc_v070.md` — D-arbc_v070-4 shipped the
`InsertFieldType::ObjectId` enumerator and its `map_field_type` arm so the library's hint survives
into the scene layer, then **explicitly deferred the picker**: "the L3 insert modal renders an
`InsertFieldType::ObjectId` field … as a labelled text field — adequate for the pin leaf, not
adequate for the user." This leaf is that planned realization; it is not a deviation.

## Effort estimate

**1.5d.** Every seam this leaf needs is shipped; nothing is greenfield.

- The `ObjectId` type hint already flows library → scene: `arbc::KindInsertField::Type::ObjectId`
  → `scene::map_field_type` (`src/scene/cell.cpp:32-44`, the `ObjectId` arm at `:38-39`) →
  `scene::InsertFieldType::ObjectId` (`src/scene/ace/scene/cell.hpp:48-56`) carried on
  `scene::InsertField` (`:59-67`).
- The generic "does this cell wrap a composition" test exists: `scene::nested_composition_of`
  (`src/scene/ace/scene/cell.hpp:304-305`, def `src/scene/cell.cpp:439-455`), reading
  `arbc::Content::composition_ref()` with **no** `kind_id` switch.
- The Root-descent DAG walk exists to copy: `PathBuilder::descend`
  (`src/scene/cell.cpp:548-594`), the same primitive `scene::composition_path` uses.
- The L3 modal render loop and the confirm/collect flow are one function:
  `draw_insert_cell_modal` (`src/dock/dock.cpp:136-196`), render at `:160-166`, confirm at
  `:174-188`, buffer seam at `:647-678`.
- The dock-local POD boundary and its L4 marshalling are one file each: `InsertFieldSpec`
  (`src/dock/ace/dock/dock.hpp:48-52`) and `AppProjectGateway::insert_kinds`
  (`src/app/project_gateway.cpp:295-315`).

The work is: one new L1 `scene` enumerator, a presentation-only widening of the dock POD, a combo
branch in the modal, and the marshalling that fills it — plus the three test layers.

## Inherited dependencies

**Settled:**

- **`editor.cells.insert_schema`** (Done 2026-07-28) — the editor "renders fields and hands back
  strings"; field lists, types, defaults and the config-assembly `assemble` thunk are the **kind's
  own** (D-insert_schema-1/-2). `scene::build_config` delegates to the library `assemble`
  (`src/scene/cell.cpp:242-268`), and values are consumed **positionally**, `id` being only the
  modal's label-pairing key (D-insert_schema-3). This leaf changes **how a single field's string is
  produced in the modal**; it does not touch `build_config`, `assemble`, or the positional contract.
- **`editor.canvas.arbc_v070`** (Done 2026-08-09) — bumped the pin to v0.7.0 and added the
  `InsertFieldType::ObjectId` enumerator + the `map_field_type` arm (D-arbc_v070-4), so
  `org.arbc.nested`'s v0.5.0 schema (one labelled `child` field, `Type::ObjectId`, min 1, no
  default) already surfaces through `scene::insert_schemas` (`src/scene/cell.cpp:199-240`) with its
  type preserved. The modal renders it as free text today; this leaf upgrades that.

**Pending:** none — both predecessors are Done.

## What this task is

An `scene::InsertFieldType::ObjectId` insert-schema field is currently rendered by the L3 insert
modal as a labelled free-text box, exactly as every other field (`src/dock/dock.cpp:160-166` has no
per-type branch, because the dock-local POD deliberately drops the type). This leaf renders such a
field as a **picker over the compositions that exist in the document** — the set discovered
generically by descending `arbc::Content::composition_ref()` from Root — and, when the user selects
one, resolves the chosen composition's `arbc::ObjectId` into the **decimal string** the field
collects, so the downstream `build_config` → `assemble` → `registry.factory(id)` path is
byte-identical to a hand-typed decimal. The library itself intends this: `Type::ObjectId` "says the
value NAMES a composition, so a host can offer a picker; the grammar — a bare decimal — stays the
kind's own" (arbc `builtin_kinds.cpp:287-292`).

The one field this reaches today is `org.arbc.nested`'s `child`, but the picker is keyed on the
**field type the registry declares**, not on the kind id — it applies to any `ObjectId` field any
kind advertises, holding no allowlist (A16 preserved).

## Why it needs to be done

Minting an in-document nested composition requires naming an existing composition's `ObjectId`. A
decimal `ObjectId` is the single hardest config value in the editor to produce by hand — a user has
no way to know which integer names which composition, and a typo silently names some other object
or fails the factory's min-1 check. arbc filed `#33` and shipped `Type::ObjectId` precisely so a
host can offer a picker instead (`builtin_kinds.cpp:287-296`); this leaf is the host side of that
contract. It is the last rough edge on the Registry-driven insert dialog: `insert_schema` made every
field the kind's own, `arbc_v070` made the `ObjectId` hint survive the map, and this makes the hint
mean something to the user.

## Inputs / context

**Governing design docs (normative):**

- `docs/01-architecture.md` **A16** (`:433`) — Registry-driven, **no-allowlist** cell insert:
  `scene::insert_schemas` emits exactly one entry per `registry.ids()` entry, unconditionally, in
  registration order; the raw-config fallback is the null path, not an allowlist. This leaf holds
  that line — the picker keys on the declared field type, never on a kind id.
- `docs/01-architecture.md` **A12/A13** (`:428`+) — the L3 `dock` reaches L1 through
  `dock::ProjectGateway` exchanging **dock-local POD** (`InsertKindSpec`/`InsertFieldSpec`, the
  `GcSummary` precedent); `dock` may not include `ace/scene` or `ace/commands`.
- `docs/01-architecture.md` **§8** (`:310-346`) — the levelization DAG: `scene` is L1
  (`base, project, libarbc`, no ImGui/GL/SDL); `dock` is L3; `app` is L4 and sees everything.
- `docs/01-architecture.md` **§9** (`:348-375`) — the universal definition of done.
- `docs/00-design.md` **D3** (`:470`) — cell kinds include nested compositions.

**Library API consumed (already pinned, v0.7.0):**

- `arbc::ObjectId { std::uint64_t value{0}; }` (`.../arbc/base/ids.hpp:11-16`) — the decimal string
  form is `std::to_string(id.value)`.
- `org.arbc.nested` insert schema: `KindInsertSchema{{Field{"child", Type::ObjectId, "", 1.0, {},
  {}}}, joined(',')}` (`.../src/builtin_kinds.cpp:297-299`); `make_nested` parses "a positive
  decimal ObjectId" (`:181-187`) and refuses the empty string with its own message (min 1 unmet).
  The intent comment (`:287-296`) is the spec for this leaf.

**Editor seams this leaf extends (real paths + lines):**

- `src/scene/ace/scene/cell.hpp:48-56` — `InsertFieldType` enum (`Integer, Number, Text, ObjectId`).
- `:59-67` — `struct InsertField { id; label; InsertFieldType type; initial; }`.
- `:70-83` — editor `struct KindInsertSchema { kind_id; human_name; fields; raw_config; assemble; }`.
- `:304-305` / `src/scene/cell.cpp:439-455` — `scene::nested_composition_of` (generic
  `composition_ref()` read).
- `src/scene/cell.cpp:32-44` — `map_field_type` (the `ObjectId` arm).
- `:199-240` — `scene::insert_schemas` (one entry per id; the enumeration this leaf must not touch).
- `:242-268` — `scene::build_config` (delegates to `assemble`; unchanged).
- `:548-594` — `PathBuilder::descend` (the Root-descent DAG walk to model the new enumerator on);
  `src/scene/ace/scene/cell.hpp:315-321` — the `Breadcrumb` display-label convention
  (kind id, or "Nested" when unresolvable) to reuse for the picker labels.
- `src/dock/ace/dock/dock.hpp:48-52` — `InsertFieldSpec { id; label; initial; }` (the POD to widen;
  its comment states it deliberately drops the type).
- `:57-61` — `InsertKindSpec`.
- `src/dock/dock.cpp:136-196` — `draw_insert_cell_modal`; render loop at `:160-166`, confirm/collect
  at `:174-188`, `Dockspace` buffer seam (`select_insert_kind`/`insert_field_buffer`/
  `insert_field_value`) at `:647-678`.
- `src/app/project_gateway.cpp:295-315` — `AppProjectGateway::insert_kinds` (forward marshalling,
  `:310` drops the type today); `:317-358` — `insert_cell`, `build_config` call at `:332`.

## Constraints / requirements

1. **No-allowlist (A16) is preserved and pinned.** Candidate discovery branches on
   `arbc::Content::composition_ref()`, never on `kind_id`; the picker is selected by
   `InsertFieldType::ObjectId`, never by "is it `org.arbc.nested`". `scene::insert_schemas` still
   emits exactly one entry per `registry.ids()` entry, untouched — `insert_schemas(r).size() ==
   r.ids().size()` still holds. A kind that declares an `ObjectId` field the editor has never heard
   of gets the picker for free.
2. **Levelization stays clean.** `scene` gains no ImGui/GL/SDL include. `dock` gains no `ace/scene`
   or `ace/commands` include — the chosen `arbc::ObjectId` is resolved to its decimal string
   **before** it crosses the POD boundary, so `dock` never names an `arbc` type. No new component,
   no new DAG edge, no new external dependency. No entry in `scripts/check_levels.py` changes.
3. **The collected string is byte-identical to a typed decimal.** Selecting a composition writes
   `std::to_string(id.value)` into the field's buffer; `build_config`, the library `assemble`, and
   `make_nested` see exactly what a hand-typed decimal produced. Downstream code and its journal
   contract are untouched — this leaf ends at the modal buffer.
4. **Errors stay values.** An empty candidate set (no sub-compositions to nest) renders an
   empty/disabled picker; confirming with no selection yields `make_nested`'s own min-1 refusal, the
   modal stays open, the `Document` is untouched — the exact behaviour the free-text box had when
   left blank.
5. **The picker resolution happens in one place.** The `ObjectId` → decimal-string convention lives
   in L1 `scene` (the new enumerator returns pre-resolved `value` strings), not scattered into
   `dock` or `app`.
6. **Every UI-driven mutation still runs inside `CanvasView::apply_edit`** (A4.1 writer identity) —
   unchanged; this leaf adds a **read** (composition enumeration) on the modal-build path and does
   not change the write path.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate` green
(check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.** By inspection:
  `src/scene/` gains a new enumerator (`composition_options`) that includes only `base`/`project`/
  `libarbc` headers, no ImGui/GL/SDL; `src/dock/` gains a widened `InsertFieldSpec` and a combo
  branch but no `ace/scene`/`ace/commands` include (it handles pre-resolved `std::string`
  label/value pairs only); `src/app/project_gateway.cpp` (L4, already sees both) does the resolution.
  `python scripts/check_levels.py` stays clean with no entry in `check_levels.py:21-37` changed.
- **L1 logic — Catch2 unit** (`tests/cell_model_test.cpp`, extended,
  `TEST_CASE("cells model: …")`, following `tests/cell_model_test.cpp` and the
  `insert_schema`/`arbc_v070` cases already there):
  - `scene::composition_options(document, registry)` over a document holding a nested cell that
    wraps a sub-composition returns one option whose `value == std::to_string(child.value)` for that
    composition and a non-empty display `label`; deduplicates when two cells wrap the same
    composition; **excludes the root composition**; is empty for a single-composition document.
  - **Generic discovery (A16 witness):** a cell of an **editor-unknown** kind that exposes
    `composition_ref()` is still enumerated — discovery branches on the facet, not on `kind_id`
    (mirrors `nested_composition_of`'s generic contract, D-cells_model-8).
  - **Round-trip:** the `value` string from `composition_options` fed through
    `scene::build_config(nested_schema, {{"child", value}})` and `make_content`/`add_cell` mints a
    nested cell whose `composition_ref()` equals the chosen composition — proving the resolved
    string is exactly what the kind consumes.
  - New assertions land in `tests/cell_model_test.cpp` (already on the `ace_tests` source list; no
    new file needed for the unit layer).
- **Rendered output — golden: none added.** The picker changes only how the `child` field's
  **string** is produced in the modal; the resulting nested cell and its render are byte-identical to
  one minted from a typed decimal (Constraint 3), and a mint's *string provenance* is invisible to
  `render_offline`. The existing `tests/goldens/cells_insert_nested_64x64.rgba8` baseline remains the
  known-good fixture; if a render companion is wanted it asserts **byte-invariance** against that
  baseline, which is stronger than a new one.
- **UI e2e — ImGui Test Engine** (`tests/objectid_field_picker_e2e_test.cpp`, new, added to the
  `ace_shell_test` source list in `CMakeLists.txt`, `IM_REGISTER_TEST(engine, "cells",
  "objectid_field_picker")`, modelled on `tests/cells_insert_e2e_test.cpp` — reusing its `ScratchDir`,
  `pump_until`, gateway/edit-runner wiring, and the `Insert Cell…###insert_cell` /
  `Insert###insert_confirm` widget ids):
  - Open the Insert Cell modal, select `org.arbc.nested`; assert the `child` field renders as a
    **combo/picker** (by its stable widget id, e.g. `##child` under `###insert_cell`), not an
    `InputText`.
  - With a sub-composition present in the scratch document, drive the combo to pick it, confirm, and
    assert on **model state** — a nested cell now exists whose `composition_ref()` is the picked
    composition (never a pixel assertion, per the sibling convention).
  - **A16 e2e witness:** the picker is driven by field type, not kind id — assert it also appears for
    an editor-unknown probe kind that declares an `ObjectId` field (paralleling the insert-dialog
    no-allowlist e2e in `tests/cells_insert_e2e_test.cpp`).
- **Threading (ASan/TSan).** `composition_options` is a **pinned read** of the document on the
  UI/modal-build path while the driver may be rendering or mutating on its own thread. One case
  appended to `tests/canvas_host_test.cpp` on the real-pool `CanvasHost`
  (`default_interactive_pool_config()`, the D-edit_render_sync-3 anchor) exercises a concurrent
  `composition_options` read against a driver render, TSan-clean — mirroring how `scene::cells` /
  `interact::pick_targets` reads are already anchored. No new lane, no new suppression.
- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed lines; clang-format +
  build clean. Tests ship with the task.
- **Deferred WBS work: none.** The leaf is self-contained. The one item surfaced for human review —
  whether libarbc should expose a *nestable (cycle-free) compositions* query so the picker could
  pre-filter legal targets — is a cross-repo API judgment routed to `tasks/parking-lot.md`, not the
  WBS; it is **not** an "audit" task.

## Decisions

**D-objectid_field_picker-1 — Candidate discovery is generic over `composition_ref()`, in a new L1
`scene` enumerator.** Add `scene::composition_options(const arbc::Document&, const arbc::Registry&)`
returning `std::vector<CompositionOption>` (`struct CompositionOption { std::string value; std::string
label; }`). It descends from Root over `arbc::Content::composition_ref()` — modelled on
`PathBuilder::descend` (`src/scene/cell.cpp:548-594`) and the generic read in
`scene::nested_composition_of` — collecting each composition it reaches, deduped by id.
*Rationale:* the discovery test **is** the no-allowlist property (A16 / Constraint 1); branching on
the facet, not on `kind_id`, is the same discipline `nested_composition_of` and `cells` already hold
(D-cells_model-8: "a `dynamic_cast` chain … would be the allowlist again, in the accessor"). Keeping
it in L1 makes the property provable in headless Catch2. *Alternative rejected:* filter to
`kind_id == "org.arbc.nested"` — an allowlist in the accessor, the exact anti-pattern A16 forbids.
**No doc delta required.**

**D-objectid_field_picker-2 — The picker's value is the composition `ObjectId` in canonical decimal
string form, resolved in L1 `scene`.** `CompositionOption::value` is `std::to_string(id.value)`; the
modal writes it verbatim into the field buffer, so `build_config` → `assemble` → `make_nested` are
byte-identical to a typed decimal. *Rationale:* the library states `Type::ObjectId` means "the value
NAMES a composition … the grammar — a bare decimal — stays the kind's own" (arbc
`builtin_kinds.cpp:290-292`); the decimal is the canonical string form of **any** `ObjectId`
(`ids.hpp:11-16`), not `org.arbc.nested`-specific grammar — so producing it preserves
D-insert_schema-1/-2's "editor renders fields, hands back strings; validation is the factory's." The
picker only produces the string differently. *Alternative rejected:* format the `ObjectId` in `dock`
or `app` — scatters the convention (Constraint 5) and drags `arbc::ObjectId` toward the ImGui layer,
which would force an `ace/scene`/`arbc` include into `dock` (levelization violation).
**No doc delta required.**

**D-objectid_field_picker-3 — The dock POD widens by a presentation-only `picker` flag + `choices`,
not by reviving the field `type`.** `InsertFieldSpec` (`src/dock/ace/dock/dock.hpp:48-52`) gains
`bool picker = false;` and `std::vector<FieldChoice> choices;` (`struct FieldChoice { std::string
label; std::string value; }`). `draw_insert_cell_modal` (`src/dock/dock.cpp:160-166`) renders a combo
over `choices` when `picker`, else the existing `InputText`; selecting a choice writes
`choice.value` into the field buffer through the `Dockspace` seam (`:647-678`). `AppProjectGateway::
insert_kinds` sets `picker`/`choices` from `scene::composition_options` when the scene field type is
`ObjectId`. *Rationale:* the predecessors deliberately dropped the field *type* from the dock POD so
that no dock code could branch on type to decide **whether** a kind is insertable (A16;
`dock.hpp:45-47`). A presentation-only `picker`/`choices` pair drives **rendering**, never
insertability — every kind still yields exactly one entry per id — and because the values are
pre-resolved strings, `dock` still never sees an `arbc` type. *Alternative rejected:* put the full
`scene::InsertFieldType` on the POD — revives the coupling the predecessors removed and reopens the
door to an insertability branch in the modal. **No doc delta required.**

**D-objectid_field_picker-4 — Root is excluded from the candidate set; deeper cycles are the
library's invariant, refused at `add_cell` as values.** `composition_options` collects only the
compositions reached by descending from Root (which naturally excludes Root itself). A choice that
would form a deeper cycle (A nests B nests A) is left to the library, which refuses it at
`registry.factory(id)` / `add_cell` with its own message (errors-as-values, Constraint 4 /
D-cells_model-3's factory-first discipline). *Rationale:* the editor cheaply avoids the one trivial,
guaranteed self-cycle (nesting the document's own root) without owning general graph-cycle
detection, which would re-implement a library invariant and require knowing `org.arbc.nested`'s
semantics — an allowlist by another name. *Alternative rejected:* include Root — offers a
guaranteed-fail cyclic choice. *Alternative rejected:* editor-side full cycle filtering — owns a
library invariant and is kind-specific. **No doc delta required.**

**D-objectid_field_picker-5 — An empty candidate set renders an empty/disabled picker, never a
free-text fallback.** When `composition_options` is empty (a single-composition project with no
sub-compositions to nest), the combo shows a disabled placeholder (e.g. "(no compositions to nest)")
and confirming yields `make_nested`'s min-1 refusal, the modal open, the document untouched.
*Rationale:* the leaf's entire purpose is to stop rendering `ObjectId` as free text; a text-box
fallback would reintroduce exactly that failure mode. The honest empty state is correct — the
primary path to a nested cell is `editor.import.nested` (an external `.arbc`), not the in-document
insert, which is a power-user path that needs an existing sub-composition anyway. *Alternative
rejected:* fall back to an `InputText` when empty — defeats the leaf. **No doc delta required.**

**Labels are display-only (A16).** `CompositionOption::label` is derived — the wrapping cell's kind
id (via the `KindBridge`, as `PathBuilder::descend` and the `Breadcrumb` convention at
`cell.hpp:315-321` do) plus the composition's decimal id for disambiguation, or "Nested" when
unresolvable. It is never branched on. Exact label text is minor UI polish, not load-bearing.

## Open questions

(none — all decided.) One item surfaced for human review (not a WBS task, not an "audit"): whether
libarbc should expose a *nestable / cycle-free compositions* query so the picker can pre-filter legal
targets rather than relying on `add_cell` rejection — a cross-repo API judgment; a human decides
whether to file against `ruoso/arbitrarycomposer`. Recorded for `tasks/parking-lot.md`.

## Status

**Done** — 2026-08-09.

- Added `scene::composition_options(const arbc::Document&, const arbc::Registry&)` returning `std::vector<CompositionOption>` in `src/scene/ace/scene/cell.hpp` and `src/scene/cell.cpp`; descends from Root via `composition_ref()`, deduplicates by id, excludes Root, labels via `Breadcrumb` convention.
- Widened dock POD `InsertFieldSpec` (`src/dock/ace/dock/dock.hpp`) with `bool picker = false` and `std::vector<FieldChoice> choices`; `draw_insert_cell_modal` (`src/dock/dock.cpp`) renders a combo when `picker`, else the existing `InputText`; selecting writes `choice.value` into the field buffer.
- L4 marshalling in `src/app/project_gateway.cpp`: `insert_kinds` calls `scene::composition_options` and populates `picker`/`choices` when the scene field type is `ObjectId`; decimal `std::to_string(id.value)` is resolved in L1 `scene`, never in `dock`.
- Catch2 units added to `tests/cell_model_test.cpp`: `composition_options` enumeration, root-exclusion, empty-doc, dedup; A16 editor-unknown wrapping-kind witness (new `CompositionProbe`); value round-trip into a nested cell.
- Threading case added to `tests/canvas_host_test.cpp`: concurrent `composition_options` read vs. live real-pool render (D-edit_render_sync-3 anchor), TSan-clean.
- ImGui Test Engine e2e added at `tests/objectid_field_picker_e2e_test.cpp` (`IM_REGISTER_TEST(engine,"cells","objectid_field_picker")`): combo appears for `org.arbc.nested` `child` field; pick→confirm→`composition_ref()`==picked; A16 witness via editor-unknown probe kind.
- `CMakeLists.txt` updated to include `tests/objectid_field_picker_e2e_test.cpp` in the `ace_shell_test` source list.
- Parking-lot entry appended for the libarbc *nestable/cycle-free compositions* query (cross-repo API judgment for a human to file if wanted).
