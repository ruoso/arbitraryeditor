# editor.dock.view_registry — View abstraction: open / close / reopen

## TaskJuggler entry

`tasks/00-editor.tji:69-73` — `task view_registry` under `editor.dock`. Effort
`2d`, `allocate team`, `depends !dockspace` (i.e. `editor.dock.dockspace`). The
`note` (`:73`) cites **Design D18** and names this refinement. (The `.tji` note
currently points at `tasks/refinements/view_registry.md`; the real path is
`tasks/refinements/editor/view_registry.md` — the closer fixes the back-link per
the ritual in `tasks/refinements/README.md:57-68`.) Downstream: the tool rail /
view launcher hangs directly off this leaf — `editor.dock.tool_rail` `depends
!view_registry` (`tasks/00-editor.tji:78`) — and the info-panel content tasks
consume it: `editor.panels.inspector` `depends editor.cells.selection,
editor.dock.view_registry` (`:218`) and `editor.panels.color` `depends
editor.dock.view_registry, editor.paint.brush` (`:236`).

## Effort estimate

**2 days** (from the `.tji`). The surface is a small L1 addition to `dockmodel`
(the view catalog + instance-id minting + open/close/reopen helpers over the
existing `DockLayout`), a per-view **draw-dispatch seam** in L3 `views`, and the
L3 `dock` frame-loop change that renders open views by id and syncs ImGui's
close button back into the model — plus a new Catch2 unit suite and a new Test
Engine e2e. The estimate is dominated by getting the open/close/reopen semantics
right (singleton idempotence, monotonic multi-instance ids, close-everything →
reopen round-trip) so that `tool_rail` and the panel-content tasks inherit a
solid seam — not by surface area. No threading, no Document render, no real
panel content (that is downstream, per D-view-registry-5).

## Inherited dependencies

**Settled (from `editor.dock.dockspace`, `tasks/refinements/editor/dockspace.md`).**
The L1 `DockLayout` value type is complete and is exactly the seam this task
extends. `src/dockmodel/ace/dockmodel/dockmodel.hpp` gives:

- `DockNode` (`:25-46`) — the unified leaf/split node; a leaf is an ordered
  `std::vector<std::string> tabs` of **stable string view ids** plus an `active`
  index (`:32-33`).
- `DockLayout` (`:53-101`) — `std::optional<DockNode> root_` (may be empty, D18
  "anything can be closed"), with the operations open/close/reopen build on:
  `insert_tab(target_view, new_view)` (`:80`), `split_leaf(...)` (`:86-87`),
  `remove_view(view)` — collapses an emptied leaf, promotes the sibling,
  empties the root when the last view leaves (`:92`), `activate(view)` (`:95`),
  plus `contains` / `view_ids` / `valid` (`:70-75`). Ids are unique across the
  tree by construction, and every mutation preserves that.

`dockspace` also established the L3 host: `src/dock/ace/dock/dock.hpp` `Dockspace`
(`:32-48`) submits `DockSpaceOverViewport` and, once-guarded, translates a
`DockLayout` seed into ImGui's live tree via `DockBuilder` (`src/dock/dock.cpp`
`build_node`, `:20-35`; `Dockspace::draw`, `:61-80`). Views are addressed by the
same string id they `Begin()` under — `ImGui::DockBuilderDockWindow(id, node)`
(`dock.cpp:23`). The placeholder panels `panel_a_title()`/`panel_b_title()`
(`dock.hpp:15-16`) and the render_probe pane as canvas stand-in are the exact
scaffolding this leaf **replaces** with a real catalog — dockspace's `dock.hpp`
comment says so verbatim (`:11-13`: "Until `editor.dock.view_registry` supplies a
real view catalog…"). The app installs the host via `Shell::set_draw_content`
(`src/app/shell.cpp:155-159`), drawing `dockspace.draw()` then `probe.draw()`.

**Settled (from `editor.foundation.build` / `app_shell`).** The levelization
DAG and both test rigs exist. `dockmodel` is L1 (`docs/01-architecture.md:171`,
"view registry + layout data"; `CMakeLists.txt` `ace_component(dockmodel DEPENDS
base platform)`), forbidden every ImGui/GL/SDL/libarbc include
(`scripts/check_levels.py:29`, and `dockmodel` in none of `EXTERNAL_ALLOWED`,
`:42-48`). `views` (L3) and `dock` (L3) are whitelisted for `<imgui.h>`
(`check_levels.py:43,45`) and already depend on `dockmodel`
(`check_levels.py:31-32`). The Catch2 suite `ace_tests` (`CMakeLists.txt:170-179`,
existing `tests/dockmodel_test.cpp`) and the offscreen Test Engine e2e
`ace_shell_test` (`CMakeLists.txt:186-199`, existing `tests/dockspace_e2e_test.cpp`,
software-GL env at `:198-199`) both exist and are reused. **No lint edit and no
new DAG edge is required.**

**Pending — replaced, not blocked, by downstream leaves.** The real panel bodies
are owned elsewhere and land later against the draw-dispatch seam this task
ships: Canvas → `editor.canvas.view` (`tasks/00-editor.tji:131`) + multi-canvas
`editor.canvas.multi_canvas` (`:143`); Inspector → `editor.panels.inspector`
(`:215`); Layers → `editor.panels.layers` (`:221`); Overview →
`editor.panels.overview` (`:227`); Color → `editor.panels.color` (`:233`);
Export → `editor.cameras.export` (`:177`). Until each ships, its view type draws
a labeled placeholder (D-view-registry-5). The view launcher UI that *calls*
`open` is `editor.dock.tool_rail` (`:75`); layout persistence is
`editor.dock.workspaces` (`:81`) — both out of scope here.

## What this task is

Give the dockspace a **view abstraction**: a fixed catalog of the view *types*
D18 names, each of which can be **instantiated into any container, closed, and
reopened**. Concretely, three pieces across the levels §8 already assigns:

1. **L1 `dockmodel` — the view catalog + open/close/reopen over `DockLayout`.**
   A pure-data catalog of the eight view types the `.tji` note enumerates
   (Canvas, Layers, Inspector, Overview, Color, History, Assets, Export), each
   with a stable slug, a display title, and a `multi_instance` flag; a
   deterministic **instance-id minting** scheme (slug for singletons; `slug#N`
   for multi-instance) with a parse round-trip; and `open` / `close` / `reopen`
   helpers that drive the existing `DockLayout` mutations. This is the bulk of
   the logic and is entirely headless-testable.
2. **L3 `views` — the per-type draw-dispatch seam.** `views::draw_view(view_id)`
   parses the id to its type and draws that type's body. For this leaf every
   body is a labeled placeholder except **Canvas**, which reuses the existing
   render_probe pane path (D18 "the canvas is a view"). A registration hook lets
   each downstream panel task supply its real body without touching the catalog.
3. **L3 `dock` — render-open-views + close-button sync.** `Dockspace` stops
   hardcoding `panel_a`/`panel_b`; each frame it walks the current `DockLayout`
   and draws each open view via `views::draw_view` inside a window carrying a
   `bool* p_open`. When ImGui clears `p_open` (the tab's ✕), `dock` calls the
   L1 `close` helper so the `DockLayout` stays the single source of truth that
   `reopen` (and later `workspaces`) reads.

Not in scope: the launcher chrome (tool_rail), layout persistence (workspaces),
and any real panel content (the panel-content tasks above).

## Why it needs to be done

D18 makes the shell **fully uniform** — "any view in any container … anything
can be closed (including every canvas), the rail brings it back"
(`docs/00-design.md:414-450`). The dockspace leaf built the split/tab/drag
mechanics but only over throwaway placeholder panes; it deliberately deferred the
"real view catalog and open/close/reopen" to this task
(`tasks/refinements/editor/dockspace.md` D-dockspace-4; `dock.hpp:11-13`). Every
subsequent UI leaf needs a view to live in: `tool_rail` is literally "the view
launcher — where you open or reopen any view" (`docs/00-design.md:446-450`) and
cannot exist without a catalog to launch from; `inspector`/`layers`/`overview`/
`color`/`export` each *are* a view body that needs a registered type and a draw
seam to plug into. This leaf is the join point that turns "a tab is a string id"
into "a tab is an instance of a known view type you can close and bring back."

## Inputs / context

- **Design constitution.**
  - `docs/00-design.md:479` (**D18**) — "a recursive split-tree of containers,
    each a tab-group of relocatable **views** (Canvas, Layers, Inspector,
    Overview, Color, History, …). Any view → any container … **Canvas is a
    view** → multiple canvases through different cameras side by side … The fixed
    tool rail hosts modal tools **and the view launcher** (reopen anything)."
  - `docs/00-design.md:414-456` (**§10**) — the uniform-dockspace prose; esp.
    "Views are content, decoupled from place" (`:425-429`), "The canvas is a
    view too — the multi-canvas payoff" (`:430-435`), "Panels belong to the
    *project*, not a canvas … N of them share one project-level selection" (D19,
    `:436-439`), and "the rail is also the **view launcher** — where you open or
    reopen any view. Nothing can be lost" (`:446-450`).
  - `docs/00-design.md:480` (**D19**) — selection + shared panels
    (Inspector/Layers/Overview) are **project-level**, so those views are
    singletons; canvases "are *only* cameras," so Canvas is the multi-instance
    view.
  - `docs/00-design.md:467` (**D6**) — "Multiple cameras are managed in the
    overview, which doubles as an export/shot map." Cameras are surfaced *within*
    Overview/Layers, not as a standalone view type (D-view-registry-3).
- **Architecture constitution.**
  - `docs/01-architecture.md:130,171` — `dockmodel` (L1) is explicitly
    **"view registry + layout data"**: the view catalog is already assigned to
    the UI-agnostic core. No new component, no A-row delta.
  - `docs/01-architecture.md:152-179` (**§8**) — the DAG; `dockmodel` L1
    (ImGui-free, `:171,177-179`), `views`/`dock` L3 ("the ONLY layer that sees
    ImGui", `:154,173-174`).
  - `docs/01-architecture.md:185-203` (**§9**) — the per-leaf DoD: L1 logic →
    Catch2 (`:187`), UI → ImGui Test Engine e2e driving widgets by id (`:189`),
    threading → ASan/TSan (`:190`), plus `check_levels` clean + build/format
    (`:200-203`). §9.1 (`:210-245`) is the clang-asan offscreen lane.
  - `docs/01-architecture.md:257-259` (**A7/A8/A9**) — project-level
    selection/panels; the L1 ImGui-free seam; layered testing as per-leaf DoD.
- **Existing seams (extend, do not fork).**
  - `src/dockmodel/ace/dockmodel/dockmodel.hpp:53-101` — `DockLayout` + its
    `insert_tab`/`split_leaf`/`remove_view`/`activate`/`contains`/`view_ids`
    ops. Open/close/reopen call these; they already guarantee id-uniqueness and
    emptied-leaf collapse.
  - `src/views/ace/views/views.hpp:10-16` — `probe_pane_title()` and
    `draw_probe_pane(texture, w, h)`: the Canvas view's stand-in body and the
    stable-id pattern the e2e drives.
  - `src/dock/ace/dock/dock.hpp:15-48` + `src/dock/dock.cpp:20-80` — the
    `Dockspace` host, `build_node` DockBuilder translation, and the hardcoded
    `panel_a`/`panel_b` this leaf replaces with catalog-driven drawing.
  - `src/app/shell.cpp:155-159` — the `set_draw_content` install; the app keeps
    owning the Canvas texture (draws it after `dockspace.draw()`), while the
    dockspace assigns it to its node by the Canvas instance id.
- **Test rigs.** `tests/dockmodel_test.cpp` (Catch2 pattern, `TEST_CASE` per
  behavior) joined to `ace_tests` (`CMakeLists.txt:170-179`);
  `tests/dockspace_e2e_test.cpp` (Test Engine: `WindowInfo(id).ID != 0`,
  `GetWindowByRef`, `DockNode`/`TabBar` assertions, offscreen frame pump) joined
  to `ace_shell_test` (`CMakeLists.txt:186-199`).

## Constraints / requirements

1. **No lint edit; no new component; no new DAG edge.** The catalog is pure data
   in `dockmodel` (already `base`/`platform`/std); the draw dispatch is in
   `views` and the frame-loop change in `dock`, both already whitelisted for
   `<imgui.h>` + `<ace/dockmodel/...>` (`scripts/check_levels.py:31-32,43,45`).
   `scripts/check_levels.py` and the `CMakeLists.txt` component graph stay
   **unedited**. A forbidden edge would be an `A<n>` levelization change — **not
   expected here**; §7/§8 already put the view registry in `dockmodel`.

2. **`dockmodel`'s catalog is ImGui/GL/SDL/libarbc-free (A8).** The view types,
   descriptors, slug/title strings, `multi_instance` flags, instance-id minting,
   and open/close/reopen helpers are pure data over `base`/`platform`/std — no
   `<imgui.h>`, no `ImGuiID`, no `<arbc/...>`. A view is still identified only by
   its **stable string id**, so the model stays headless-testable and is the
   seam `workspaces` will serialize.

3. **Reuse `DockLayout`; do not fork a parallel open-set.** The set of open views
   **is** `DockLayout::view_ids()`; there is no second registry of "what's open."
   `open`/`close`/`reopen` are thin orchestration over the existing
   `insert_tab`/`split_leaf`/`remove_view`/`activate`. The registry adds only the
   static catalog and the per-multi-instance-type monotonic counter.

4. **Register exactly the eight note-named types (D-view-registry-3).** Canvas,
   Layers, Inspector, Overview, Color, History, Assets, Export — no more, no
   fewer. **Canvas** is `multi_instance = true` (D18/D19: canvases are cameras,
   many side by side); the other seven are singletons (D19: project-level
   panels). There is **no** standalone "Cameras" view — cameras are managed
   within Overview/Layers (D6, `docs/00-design.md:467`).

5. **Placeholder bodies only, except Canvas (D-view-registry-5).** Each view
   type draws a stably-id'd labeled placeholder; **Canvas** reuses the
   render_probe pane path (D18 "canvas is a view"). Real panel content is
   downstream (the panel-content leaves in *Inherited dependencies → Pending*).
   This leaf ships the **draw-dispatch registration seam** those tasks plug into
   and must not anticipate their content.

6. **No launcher chrome, no layout persistence, no imgui.ini.** The registry
   exposes the catalog + `open` API the launcher will call, but ships no rail UI
   (`tool_rail`'s scope). It persists nothing and must not re-enable
   `io.IniFilename` — layout persistence is `workspaces`' scope, targeting
   machine-local `workspace/`/prefs (D18/§9), never imgui.ini.

7. **Close is model-authoritative.** A view closed via ImGui's tab ✕ (a cleared
   `p_open`) must flow through the L1 `close` helper so the `DockLayout` is
   updated; the model, not ImGui's live tree, is the source of truth `reopen`
   reads. Closing the last view leaves an empty layout (D18); `reopen` from empty
   seeds a fresh root leaf.

8. **Component homes follow §8.** Catalog + minting + open/close/reopen in
   `dockmodel` (L1, headless); `draw_view` dispatch + placeholder bodies in
   `views` (L3); the frame-loop render-open-views + close sync in `dock` (L3);
   the `set_draw_content` install unchanged in `app` (L4). `dockmodel` touches no
   ImGui; the shell issues no catalog logic.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella.

- **Levelization (`check_levels` clean).** `python3 scripts/check_levels.py`
  passes **with no edit**: `dockmodel` still includes only
  `<ace/base/...>`/`<ace/platform/...>` + std (no ImGui/GL/SDL/arbc — the catalog
  adds none); `views`/`dock` include `<imgui.h>` + `<ace/dockmodel/...>`. No new
  component, no new DAG edge — the primary structural assertion.

- **L1 logic — Catch2 unit (`dockmodel`).** A new `tests/view_registry_test.cpp`
  (headless, joined to `ace_tests`, `CMakeLists.txt:170-179`) asserts:
  - **Catalog** — exactly eight view types, with the expected slug + title +
    `multi_instance` per type (Canvas multi, the other seven singletons); the
    slug→type and type→slug lookups round-trip.
  - **Instance-id minting** — a singleton mints its bare slug (`"inspector"`); a
    multi-instance type mints `slug#N` with a **monotonic, non-recycling** index
    (`"canvas#1"`, then `"canvas#2"`; closing `canvas#1` and opening again yields
    `canvas#3`, never a reused id); `parse_view_id` round-trips id→(type,index)
    and rejects malformed ids.
  - **open** — into an **empty** layout seeds a fresh root leaf holding the new
    id; into a non-empty layout inserts as a tab of the target leaf (default
    target = the active/last leaf when the caller names none); a **singleton**
    `open` when that view is already present is **idempotent** — it activates the
    existing tab, no duplicate (ids stay unique); a multi-instance `open` yields a
    distinct new id each call.
  - **close / reopen** — `close(view)` removes it via `DockLayout::remove_view`
    (emptied leaf collapses; last view → empty layout); `reopen` re-adds a
    singleton under its stable slug; the **close-everything → reopen** round-trip
    restores a valid layout containing the reopened views. `DockLayout::valid()`
    holds after every op. This suite is the bulk of the coverage.

- **UI behavior — ImGui Test Engine e2e (`dock`/`views`).** A new
  `tests/view_registry_e2e_test.cpp` (joined to `ace_shell_test`,
  `CMakeLists.txt:186-199`, offscreen software-GL) reuses the
  `dockspace_e2e_test.cpp` rig and drives **by stable view id**:
  - a registered singleton view (e.g. Inspector) is present —
    `ctx->WindowInfo(id).ID != 0` and its window has a `DockNode`;
  - **close** it (drive the tab ✕ / the model close path) → `WindowInfo` reports
    it gone and it no longer appears in the layout;
  - **reopen** via the registry `open` → the window reappears under the **same**
    id in a valid dock node;
  - **multi-instance** — opening a second Canvas yields two distinct
    `canvas#N` windows coexisting (assert both `WindowInfo(...).ID != 0`),
    proving "multiple canvases side by side" (D18). A screenshot **baseline** may
    be captured via the existing `capture_pixels` rig for signal, but is **not** a
    byte-exact assertion (software-GL pixels are flaky).

- **Rendered output — golden: N/A (justified).** Same rationale as `dockspace`
  (`tasks/refinements/editor/dockspace.md` acceptance §): this leaf composes
  ImGui chrome + placeholder bodies, not a libarbc `Document`, so there is no
  byte-exact `render_offline` golden to compare. Canvas reuses the existing
  render_probe pane, whose golden already lives in `tests/render_probe_test.cpp`;
  this leaf adds no new rendered surface. The real per-panel goldens ride the
  downstream panel-content leaves.

- **Threading — ASan/TSan scope (explicit).** This leaf introduces **no new
  threading**: the registry is single-threaded UI-side state mutated only on the
  UI thread inside the draw loop (the driver/frame handoff is `editor.canvas.*`).
  There is no new TSan surface; the new e2e runs under the existing clang-asan
  offscreen lane (§9.1, `docs/01-architecture.md:210-245`) and must be
  ASan/LSan-clean there.

- **Coverage.** ≥90% diff coverage on the changed lines; the Catch2 suite and the
  e2e ship **with** this task, not as a follow-up.

- **No deferred WBS leaf from this task.** The downstream owners of every real
  view body already exist as leaves (*Inherited dependencies → Pending*); this
  leaf registers no new WBS task. The two open questions about view *ownership*
  (History, Assets — see below) are WBS-shape judgment calls and go to the
  parking lot, not the WBS.

## Decisions

- **D-view-registry-1 — The view catalog lives in L1 `dockmodel` as pure data;
  the draw seam in L3 `views`; the frame-loop orchestration in L3 `dock`.**
  `docs/01-architecture.md:130,171` already names `dockmodel` "view registry +
  layout data," so the catalog + open/close/reopen belong in the UI-agnostic
  core — headless-testable, ImGui-free, and the same seam `workspaces` will
  serialize. Only the per-type *drawing* needs ImGui, so it lives in `views`
  (already the panels' home), and the frame loop that renders open views + syncs
  the close button lives in `dock` (it already owns the host loop). *Alternative
  rejected:* a new top-level `view_registry` component — needless, would duplicate
  the `dockmodel`/`views` split §8 already draws and require an `A<n>` delta.

- **D-view-registry-2 — `DockLayout` is the single source of truth for what's
  open; the registry adds only the catalog + a multi-instance counter.** The
  open-set is `DockLayout::view_ids()`; `open`/`close`/`reopen` are thin wrappers
  over the existing `insert_tab`/`remove_view`/`activate`. *Alternative rejected:*
  a parallel "open views" list beside the layout — two sources of truth that can
  desync (close a tab in ImGui, the shadow list goes stale), for no gain.

- **D-view-registry-3 — Register exactly the eight `.tji`-named types; Canvas is
  multi-instance, the rest are singletons; no standalone Cameras view.** The note
  (`tasks/00-editor.tji:73`) enumerates Canvas, Layers, Inspector, Overview,
  Color, History, Assets, Export. D19 makes Inspector/Layers/Overview (and by
  extension Color/History/Assets/Export) project-level ⇒ singletons; D18 makes
  Canvas the multi-camera view ⇒ multi-instance. §10 (`:426`) also lists a
  "Cameras" view, but D6 (`:467`) settles that cameras are managed *within* the
  Overview (and Layers) — so adding a ninth "Cameras" type would double-own that
  surface. *Alternative rejected:* a Cameras view type — contradicts D6; the
  overview is the camera map.

- **D-view-registry-4 — Instance ids: bare slug for singletons, `slug#N` with a
  monotonic non-recycling index for multi-instance.** Deterministic, human- and
  e2e-readable, and round-trips to (type,index) by parsing at the `#`. Monotonic
  and non-recycling means a stale id (held by ImGui, a workspace, or a test) can
  never alias a freshly opened pane. *Alternatives rejected:* recycling the
  smallest free index — reopens `canvas#1` after a close and risks aliasing a
  lingering reference; UUIDs — opaque, unstable across runs, and defeat the
  deterministic-id contract the e2e and `workspaces` depend on.

- **D-view-registry-5 — Placeholder bodies now; a draw-dispatch registration
  seam for the real ones; Canvas reuses the render_probe pane.** The eight real
  panel bodies are large, separately-scheduled leaves
  (inspector/layers/overview/color/export/canvas.view); replicating them here
  would blow the 2d budget and duplicate their scope. So this leaf draws labeled
  placeholders and exposes a per-type draw hook those tasks fill in, and points
  Canvas at the existing probe pane (D18 "the canvas is a view"). *Alternative
  rejected:* build the real bodies now — out of budget and out of scope; the
  downstream leaves already own them and `depends` on this seam.

- **D-view-registry-6 — Close is model-authoritative; reopen from empty re-seeds
  a root leaf.** ImGui's tab ✕ clears a `p_open` the dockspace owns; `dock` routes
  that to the L1 `close` helper so the `DockLayout` — not ImGui's transient tree —
  is what `reopen` and `workspaces` read. Closing the last view empties the
  layout (D18 "close it all"); `reopen` seeds a fresh root leaf ("the rail brings
  it back," `docs/00-design.md:449-450`). *Alternative rejected:* let ImGui's
  live tree be the truth and reverse-engineer open state from it each frame —
  fragile, and there is no ImGui state at all once every window is closed.

## Open questions

(none — all decided.) Two **WBS-ownership** questions are deliberately *not*
encoded as tasks and are routed to the parking lot instead (they are design
judgment, not implementable leaves): whether the **History** view gets its own
panel body or folds into `editor.project.undo` (`tasks/00-editor.tji:115`, the
transaction-journal wiring), and whether the **Assets** view gets a dedicated
asset browser or is subsumed by the Layers list's referenced-vs-painted surface
(`editor.panels.layers`, `:221`, per D11). Both types are *registered* here with
placeholder bodies regardless; only the eventual real-body owner is unresolved.

## Status

**Done** — 2026-07-17.

- L1 `dockmodel` view catalog added: `src/dockmodel/ace/dockmodel/view_registry.hpp` + `src/dockmodel/view_registry.cpp` — eight view types (Canvas multi-instance, seven singletons), monotonic non-recycling id minting (`slug#N`), `parse_view_id` round-trip, and `open`/`close`/`reopen` helpers over `DockLayout`.
- L3 `views` draw-dispatch seam: `src/views/ace/views/views.hpp` + `src/views/views.cpp` — `draw_view(view_id)` dispatch with per-type registration hook; Canvas reuses `draw_probe_pane` (D18 "canvas is a view"); all others render labeled placeholders.
- L3 `dock` catalog-driven render loop: `src/dock/ace/dock/dock.hpp` + `src/dock/dock.cpp` — `Dockspace` now walks `DockLayout::view_ids()` each frame and calls `draw_view` per open view; ImGui tab ✕ (`p_open` cleared) routes through the L1 `close` helper so `DockLayout` stays the single source of truth.
- App wiring: `src/app/ace/app/probe.hpp`, `src/app/probe.cpp`, `src/app/shell.cpp` — `run_editor` registers (and resets on exit) the Canvas view body so the app-owned probe texture is the Canvas body; standalone `draw_probe_pane`/`shell_e2e` path untouched.
- Catch2 unit suite: `tests/view_registry_test.cpp` (7 test cases, 126 assertions) joined to `ace_tests` — covers catalog, id minting, `parse_view_id`, open/close/reopen, close-everything→reopen round-trip.
- ImGui Test Engine e2e: `tests/view_registry_e2e_test.cpp` joined to `ace_shell_test` — drives present→✕-close→reopen-same-id and two coexisting `canvas#N` windows.
- `tests/dockspace_e2e_test.cpp` retargeted to the catalog-driven `Dockspace`; `CMakeLists.txt` wired both new test files.
- Two WBS-ownership questions (History real-body owner; Assets real-body owner) routed to the parking lot per acceptance criteria.
