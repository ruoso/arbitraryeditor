# editor.cameras.new_shot_from_view — Rail action: promote the viewport's current framing into a saved shot

## TaskJuggler entry

- **Task:** `editor.cameras.new_shot_from_view`, `tasks/00-editor.tji:313-318`, inside
  `task cameras "Cameras"` (`tasks/00-editor.tji:257`), inside `task editor`.
- **Effort:** `0.5d` · `allocate team`.
- **Depends:** `!frame_selection` (i.e. `editor.cameras.frame_selection`,
  `tasks/00-editor.tji:306-312`, `complete 100`).
- **Note (`.tji:317`):** *"Add a second Edit-section rail item 'New Shot From
  View###new_shot_from_view' plus a ProjectGateway virtual pair; L4 impl reads ViewFraming
  (src/app/ace/app/view_framing.hpp, bound at src/app/shell.cpp:288), calls the
  already-shipped interact::new_shot_from_view (src/interact/ace/interact/interact.hpp:109),
  and dispatches commands::add_camera_command (shipped by editor.cameras.frame_selection) —
  plus an e2e. Closes the camera_inspector.cpp:41 'No cameras — create a shot from the view.'
  lie. Source-of-debt: tasks/refinements/cameras/frame_selection.md. Design:
  docs/00-design.md D23, :64-66."*
- **Back-link:** this refinement lands at `tasks/refinements/cameras/new_shot_from_view.md`.
  The closer appends `Refinement: tasks/refinements/cameras/new_shot_from_view.md` to the
  `.tji` note and adds `complete 100` after `allocate team`. **Do not** hand-edit the `.tji`
  here.
- **Source of debt:** `tasks/refinements/cameras/frame_selection.md:580-596`, which registered
  this leaf verbatim: *"It exists because `new_shot_from_view` has had no production caller
  since `editor.cameras.model` shipped it, and `src/app/camera_inspector.cpp:41` tells the user
  'No cameras — create a shot from the view.', which is currently false."*
- **Downstream dependents:** none declare `depends !new_shot_from_view`. `editor.panels.inspector`
  (`.tji:359`) will reuse the `ProjectGateway` pair this leaf ships, exactly as it reuses
  `can_frame_selection`/`frame_selection`.
- **Milestone:** already wired into `m9_editor` (`tasks/99-milestones.tji:8`) through the
  `editor.cameras` container dependency.

## Effort estimate

**Half a day.** Every moving part already exists and is tested; this leaf is the wiring that
turns four shipped pieces into a user-reachable verb.

- **The L1 derivation is shipped and unit-tested.**
  `interact::new_shot_from_view(const arbc::Affine& camera, int pane_w, int pane_h)` →
  `ShotFraming{frame, width, height}` at `src/interact/ace/interact/interact.hpp:109`
  (struct `:94-98`, impl `src/interact/interact.cpp:97-111`). It has **no production call
  site** today — only `tests/camera_model_test.cpp:539,561,569` and
  `tests/look_through_test.cpp:63,79`. This leaf is its first caller.
- **The framing source is already plumbed into the gateway.**
  `app::ViewFraming{arbc::Affine camera; int pane_w; int pane_h;}`
  (`src/app/ace/app/view_framing.hpp:17-21`), produced by
  `CanvasView::primary_framing()` (`src/app/canvas_view.cpp:523-534`), bound once at
  `src/app/shell.cpp:288` (`app_gateway->set_view_framing([&canvas] { return
  canvas.primary_framing(); });`) and read through the private
  `AppProjectGateway::view_framing()` (`src/app/project_gateway.cpp:172-188`). **No
  `shell.cpp` change is needed.**
- **The command verb is shipped.** `commands::add_camera_command(registry, name, resolution,
  frame, outcome)` (`src/commands/ace/commands/cameras.hpp:50-52`, impl
  `src/commands/cameras.cpp:11`) plus `commands::next_camera_name`
  (`cameras.hpp:62`, impl `cameras.cpp:29-45`), both landed by
  `editor.cameras.frame_selection`. Its one production call site is
  `src/app/project_gateway.cpp:288-291`; this leaf adds the second.
- **The rail seam is shipped.** `draw_edit_section(Dockspace&, ProjectGateway&)`
  (`src/dock/dock.cpp:201-227`, called at `:405`) already carries `Insert Cell…`,
  `Delete Selected` and `Frame Selection` in the exact `"Label###id"` +
  `BeginDisabled/EndDisabled` shape this item copies.
- **The writer-thread seam is shipped.** `AppProjectGateway::run_edit`
  (`src/app/project_gateway.cpp:116-125`), bound to `CanvasView::apply_edit` at
  `src/app/shell.cpp:283`.

New code: one rail `Selectable` (~6 lines, `src/dock/dock.cpp`), two `ProjectGateway`
virtuals with inert defaults (`src/dock/ace/dock/dock.hpp`), one ~20-line
`AppProjectGateway` override plus a small refactor splitting `view_framing()` into a
live-only reader and its fallback wrapper (`src/app/project_gateway.{hpp,cpp}`), one reworded
string (`src/app/camera_inspector.cpp:41`), one new L1 test file, one new e2e file, two
`CMakeLists.txt` source-list lines, and cases appended to
`tests/app_project_gateway_test.cpp`. **No new component, no new DAG edge, no new external
dependency, no libarbc change.** One doc delta (**D23, amended**).

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cameras.frame_selection`** (`tasks/refinements/cameras/frame_selection.md`, Done
  2026-07-23) — the sibling mint verb. This leaf consumes: **D-frame_selection-5** (the
  selection/framing → geometry → command join lives at L4 `app` inside `run_edit`, because
  `commands` may not include `interact` and vice versa), **D-frame_selection-6** (the
  `commands/cameras.{hpp,cpp}` pair and `add_camera_command`'s by-reference `registry`/`outcome`
  contract), **D-frame_selection-8** (rail action only: no chord, no modal tool, no confirm,
  no inspector button), **D-frame_selection-9** (`Camera <n>`, first free `n`, via
  `next_camera_name`), **D-frame_selection-10** (a mint touches neither the selection nor any
  canvas's look-through camera), and **D-frame_selection-2**, whose rejected-alternative
  paragraph (`frame_selection.md:641-655`) explicitly *grants* the pane-derived resolution
  rule to this verb: *"The WYSIWYG argument that justifies it for* new shot from view *(where
  the pane **is** the thing being promoted) does not transfer to* frame selection."
- **`editor.cameras.model`** (`tasks/refinements/cameras/model.md`, Done 2026-07-18) —
  `interact::new_shot_from_view` / `ShotFraming` (Constraint 6: the command takes the
  viewport framing as **values**, keeping the derivation pure L1), `scene::add_camera`
  (`src/scene/ace/scene/camera.hpp:151-153`, writer-thread only, two journal entries / one
  observable undo), `scene::cameras(const arbc::Document&)` (`camera.hpp:134`), and
  **D-model-3** (the transient viewport camera stays session state; only shot cameras are
  persisted — so *reading* `ViewFraming` is not a transaction, only the mint is).
- **`editor.cameras.look_through`** (`tasks/refinements/editor/look_through.md`, Done
  2026-07-19) — `interact::viewport_camera_for_shot(frame, native_w, native_h, out_w, out_h)`
  (`interact.hpp:157-158`), the exact inverse of `new_shot_from_view`, and its round-trip law
  (`look_through.md:260-267`). This leaf's WYSIWYG claim is that law, asserted end-to-end.
- **`editor.cells.model`** (`tasks/refinements/editor.cells/model.md`) — the
  `ViewFraming`-by-value seam (its Constraint 7) and the `set_view_framing` binding this leaf
  reuses without change.
- **`editor.cells.remove`** (`tasks/refinements/editor.cells/remove.md`) —
  **D-cells_remove-6** (new `ProjectGateway` virtuals are **non-pure with inert defaults**, so
  the six existing gateway fakes need no churn) and **D-cells_remove-3** (targets resolve
  *inside* the edit closure, on the writer thread, against the live document).

**Pending (owned here):** nothing. Every predecessor is `complete 100`.

## What this task is

1. **A second mint item in the rail's Edit section.** `src/dock/dock.cpp:201-227` gains
   `ImGui::Selectable("New Shot From View###new_shot_from_view")` immediately after the
   `Frame Selection` item (`dock.cpp:220-223`), inside its own
   `BeginDisabled/EndDisabled` pair — **disabled, not hidden**, per the rule the section's own
   comment states (`dock.cpp:206-208, :216-217`). D23's "two mint verbs" become two adjacent,
   independently gated rail actions.
2. **A `ProjectGateway` virtual pair.** `dock::ProjectGateway`
   (`src/dock/ace/dock/dock.hpp:80-206`) gains
   `virtual bool can_new_shot_from_view() const { return false; }` and
   `virtual bool new_shot_from_view() { return false; }` beside
   `can_frame_selection`/`frame_selection` (`:204-205`) — two `bool`s, no dock-local POD,
   non-pure with inert defaults.
3. **The L4 join.** `app::AppProjectGateway` (`src/app/ace/app/project_gateway.hpp:34`,
   impl `src/app/project_gateway.cpp`) overrides both. `new_shot_from_view()` reads the
   **live** `ViewFraming`, calls `interact::new_shot_from_view(vf.camera, vf.pane_w,
   vf.pane_h)`, and dispatches `commands::add_camera_command(registry,
   next_camera_name(document), scene::Resolution{shot.width, shot.height}, shot.frame,
   outcome)` — the whole sequence inside `run_edit`, on the writer thread.
   `can_new_shot_from_view()` answers "is there a live, sized canvas pane?".
4. **A small refactor of `view_framing()`.** `AppProjectGateway::view_framing()`
   (`project_gateway.cpp:172-188`) currently folds two things together: the live canvas
   framing and a root-composition fallback for when no canvas is sized. This leaf splits it
   into `std::optional<ViewFraming> live_view_framing() const` (the live reader) and the
   existing `view_framing()` as its fallback wrapper, so `insert_cell`'s provisional
   placement keeps the fallback and the mint does not get it (D-new_shot_from_view-2).
5. **Closing the inspector's lie.** `src/app/camera_inspector.cpp:41` currently reads
   `"No cameras — create a shot from the view."` with no affordance in the product that does
   so. It becomes true, and the string is reworded to name the item:
   `"No cameras — use New Shot From View in the rail."`

Out of scope, by inheritance: a keyboard chord (D-frame_selection-8 — §11's input map is still
open), an inspector button (`editor.panels.inspector`, `.tji:359`, a two-line reuse of this
pair), and any change to `interact::new_shot_from_view` itself
(D-new_shot_from_view-1).

## Why it needs to be done

Three concrete debts close here.

- **A shipped, tested L1 primitive has no production caller.**
  `interact::new_shot_from_view` landed with `editor.cameras.model` in July 2026 and is
  reachable only from tests. `editor.cameras.look_through` then shipped its exact inverse
  (`viewport_camera_for_shot`) and *is* wired — so the editor can look *through* a shot it has
  no way to create from the view.
- **The product tells the user to do something it does not let them do.**
  `src/app/camera_inspector.cpp:41`. `editor.cameras.frame_selection` shipped the first
  camera-creation affordance, but it mints from the *selection*; with nothing selected the
  inspector's instruction is still unactionable.
- **D23 names two mint verbs; only one exists.** `docs/00-design.md:490` and the §2 prose at
  `docs/00-design.md:64-66` (*"'new shot from view' promotes the viewport's current framing
  into a saved shot"*) both treat the pair as the camera-creation story. Shipping one half
  leaves the constitution half-implemented, and `editor.cameras.export` (`.tji:319`) — the
  next consumer — assumes cameras exist to export.

Downstream, `editor.panels.inspector` reuses the pair verbatim, and `editor.cameras.export`
gains a way to produce an export spec that matches what the user is looking at.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **`docs/00-design.md:490` — D23 "Minting a camera"** (amended by this leaf, see
  *Decisions*): *"Creating a camera is a rail **action**, never a mode (D20), and a mint always
  derives **both** knobs at once… Two mint verbs share **one derivation rule**: … **"new shot
  from view"** (§3) fits it to the viewport's current framing."* And, after this leaf's delta:
  *"For "new shot from view" that rule collapses to the pane itself… the resolution is the
  pane's **device-pixel size** and the frame is the viewport camera inverted… the clamp is
  **not** applied… The mint is **refused rather than guessed at** … for "new shot from view",
  **no live, sized canvas pane**."*
- **`docs/00-design.md:64-66` (§2, "The core model")** — *"'Look through Hero' makes Hero the
  active camera; 'new shot from view' promotes the viewport's current framing into a saved
  shot. Both appear in the same cameras list and are manipulated identically (D7)."* Together
  with `:58-61` (the viewport camera's *"sensor is the on-screen canvas (resolution = the
  canvas pixel size)"*), this is the whole semantic of the verb: resolution = the pane.
- **`docs/00-design.md:487` — D20**: *"ad-hoc crop = frame a camera (D14), not a mode"* — the
  rail-action-never-a-mode rule this item obeys.
- **`docs/00-design.md:476` — D9**: frame ≠ resolution; the inspector's W×H is the documented
  escape hatch for a resolution the mint chose.
- **`docs/00-design.md:452-456` (§10)** — *"The fixed rail is home base"*: fixed geometry, so a
  temporarily unavailable action is **disabled, not hidden**.
- **`docs/00-design.md:485` — D18**: the fully-uniform dockspace with *no keep-a-canvas
  guardrail* — **every canvas can be closed**. This is why the "no live pane" case is real and
  reachable rather than theoretical.
- **`docs/00-design.md:483` — D15**: the mint is an undoable scene transaction; look-through
  activation is transient session state.

**Governing architecture rows (`docs/01-architecture.md`):**

- **§8, `:185-220`** — the levelization DAG. `dock` (L3) → `{dockmodel, views, imgui}`;
  `interact` (L1) → `{base, scene}`; `commands` (L1) → `{base, project, scene}`; `app` (L4) may
  depend on everything. `:218-220`: *"All of L1 is the testable core and none of it may
  `#include <imgui.h>`"*.
- **A12 (`:303`)** — project-entry actions are dependency-inverted behind the `dock`-declared
  abstract `dock::ProjectGateway` with the concrete impl in L4 `app`.
- **A13 (`:304`)** — the gateway carries session verbs against the one `AppState`.
- **A14 (`:305`)** — a camera is an editor-defined **non-rendering** `Content` of kind
  `org.arbc.camera`; frame = the `Layer`'s `Affine`, resolution + name = serialized `Content`
  state. This is why the mint produces **no** pixel change (the golden argument below).
- **A16 (`:307`)** — the gateway may exchange only primitives or **dock-local POD**, because
  `dock` may include neither `ace/scene` nor `ace/commands`.
- **§9 (`:222-249`)** and **§9.1 (`:251-286`)** — the four verification layers and the
  offscreen software-GL ASan lane the e2e runs in.

**libarbc API surface (v0.2.0 pin, `editor.canvas.arbc_v020`):** `arbc::Affine` and
`Affine::inverse()` (`arbc/base/transform.hpp`), `arbc::ObjectId`, `arbc::Document` — all
reached through the existing `scene`/`interact`/`commands` wrappers. **No new libarbc surface
is touched.**

**Editor seams this leaf extends:**

- `src/dock/dock.cpp:201-227` — `draw_edit_section`; the `Frame Selection` item at `:218-223`
  is the line-for-line template. Rail window title `tool_rail_title()` →`"Tool Rail"`
  (`src/dock/ace/dock/dock.hpp:365`, impl `src/dock/dock.cpp:371`), begun at `dock.cpp:523`;
  section called at `dock.cpp:405` under `if (ProjectGateway* gateway =
  dockspace.project_gateway())` (`:404`).
- `src/dock/ace/dock/dock.hpp:204-205` — the `can_frame_selection`/`frame_selection` pair and
  its doc-comment shape (`:190-203`).
- `src/app/ace/app/project_gateway.hpp:91-120` — the Frame Selection override block, the
  `set_view_framing` installer (`:111`) and the private `view_framing()` (`:117`).
- `src/app/project_gateway.cpp:116-125` (`run_edit`), `:168-188` (`set_view_framing` /
  `view_framing`), `:269-298` (`frame_selection`, the join this one mirrors).
- `src/app/ace/app/view_framing.hpp:7-21` — `ViewFraming`, *"A zero pane means 'no live
  canvas'"*.
- `src/app/canvas_view.cpp:523-534` — `primary_framing()`: the **lowest-id sized presenter**
  wins (`"canvas#1"` over `"canvas#2"`), *"a deterministic choice, not the most-recently-drawn
  one"*; an unsized pane carries no framing.
- `src/app/canvas_view.cpp:236-245` — the per-canvas camera picker (`"Viewport"` +
  one `SmallButton` per camera name); the e2e reads it to assert the mint did **not** change
  look-through.
- `src/app/camera_inspector.cpp:35-52` — the empty-state branch (`:39-42`) and the `###cam_<i>`
  chooser; W/H fields at `:80-81`.
- `src/interact/ace/interact/interact.hpp:88-115` — the `new_shot_from_view` block;
  `:117` `k_max_mint_resolution = 8192`; `:141` `shot_from_extent`; `:157-158`
  `viewport_camera_for_shot`.
- `src/commands/ace/commands/cameras.hpp:34-70` — `AddCameraOutcome`, `add_camera_command`,
  `next_camera_name`, `can_frame_selection` (impl `src/commands/cameras.cpp:47`, literally
  `!state.selection().empty()`).

**Predecessor refinements:** `tasks/refinements/cameras/frame_selection.md` (esp. `:580-604`
registering this leaf, `:630-665` D-frame_selection-2, `:711-735` D-frame_selection-5,
`:787-811` D-frame_selection-8), `tasks/refinements/cameras/model.md:249-253` (Constraint 6),
`tasks/refinements/editor/look_through.md:260-267` (the round-trip law).

**Test rigs:**

- `tests/frame_selection_e2e_test.cpp` — the rail-action e2e template: `ScratchDir` (`:73`),
  `pump_until` (`:110`), `E2EState` + `ctx->Test->UserData` (`:102`, `:203` — `TestFunc` is a
  plain function pointer, `std::function` is disabled in this build), the real
  `AppProjectGateway` + real `CanvasView` + `CameraInspector` wiring (`:159-193`), the drain
  loop (`:289-300`), and the `BareGateway` inert-default case (`:318-339`).
- `tests/camera_manip_e2e_test.cpp:250-271` — the `"canvas#1/##canvas_nav"` pane-rect probe.
- `tests/canvas_nav_e2e_test.cpp` — the wheel pan/zoom recipe.
- `tests/app_project_gateway_test.cpp:434-560` — the headless L4 gateway suite (three
  frame_selection cases at `:438`, `:492`, `:528`).
- `tests/look_through_test.cpp:63,79` and `tests/camera_model_test.cpp:539,561,569` — the
  existing `new_shot_from_view` L1 coverage.
- `CMakeLists.txt:219-236` (`ace_tests`, links `ace::interact ace::project ace::render
  ace::platform ace::dockmodel ace::commands ace::scene arbc::arbc Catch2` — **no ImGui**) and
  `CMakeLists.txt:251-267` (`ace_shell_test`, links `ace::app`).
- `scripts/check_levels.py:24-40` (`ALLOWED`) and `:44-51` (`EXTERNAL_ALLOWED`);
  `scripts/gate` runs levels · format · build · ctest.

## Constraints / requirements

1. **Levelization (`check_levels` clean) — the primary structural assertion.** `dock` gains
   two `bool` virtuals and **no** new include: not `ace/scene`, not `ace/commands`, not
   `ace/interact` (`check_levels.py:35` allows `dock → {dockmodel, views}` only). `interact`
   and `commands` gain nothing. The `interact` → `commands` join stays at **L4 `app`**, the
   only level that may see both (`check_levels.py:36-39`). **No entry in
   `scripts/check_levels.py:24-51` changes.**
2. **The rail item is a D20 *action*, never a fifth modal `ToolId`.** It lives in the Edit
   section, immediately after `Frame Selection` (`dock.cpp:223`), with widget id
   `New Shot From View###new_shot_from_view` — visible label plus a slash-free `###` id so the
   test-engine ref `"Tool Rail/###new_shot_from_view"` parses.
3. **Disabled, not hidden.** The item is wrapped in `BeginDisabled(!can)/EndDisabled` and the
   click is re-checked (`&& can`), exactly as `Frame Selection` does (`dock.cpp:218-223`) —
   the rail's fixed geometry (`docs/00-design.md:452-456`) must not reflow.
4. **The mint is gated on a live, sized canvas pane, and refuses as a value otherwise.**
   `can_new_shot_from_view()` is false when no `Presenter` reports a positive
   `requested_width`/`requested_height`; a `new_shot_from_view()` call in that state mutates
   nothing and returns `false`. The root-composition fallback in `view_framing()`
   (`project_gateway.cpp:180-188`) belongs to `insert_cell`'s provisional placement and must
   **not** feed the mint (amended D23).
5. **Every mutation runs inside `run_edit`.** `scene::add_camera` is writer-thread-only
   (`src/scene/ace/scene/camera.hpp:145-153`); the whole read→derive→dispatch sequence goes
   inside the closure, mirroring `frame_selection` (`project_gateway.cpp:269-298`). Reading
   `ViewFraming` inside the closure is required for the same reason: the pane size the
   transaction records must be the one live when it lands.
6. **Exactly one `commands::add_camera_command` per click**, with the auto-name from
   `commands::next_camera_name` — no new command verb, no second `scene::add_camera` path.
   `registry` and `outcome` are held by reference by the returned `Command` and must outlive
   the synchronous `dispatch` (`cameras.hpp:50-52`).
7. **The mint changes neither the selection nor any canvas's look-through camera** (D23,
   D-frame_selection-10). Minting is scene data; look-through is transient session state
   (D15/D18).
8. **`interact::new_shot_from_view` ships unchanged.** No clamp, no rounding, no
   aspect-expansion is added to it, and its existing tests must not change
   (D-new_shot_from_view-1). The leaf is a caller, not an edit.
9. **No chord, no confirm, no modal, no inspector button** (D-frame_selection-8 inherited:
   §11's input map is still open, so minting a binding here would pre-empt the leaf that
   writes it).
10. **`src/app/camera_inspector.cpp:41`'s empty-state text must become true**, and is reworded
    to name the affordance. No behavioural change to the inspector otherwise.
11. **Gateway fakes must not churn.** Both virtuals ship non-pure with inert defaults
    (D-cells_remove-6), so the six existing fakes (`tests/open_ui_e2e_test.cpp:34`,
    `gc_ui_e2e_test.cpp:36`, `save_ui_e2e_test.cpp:34`, `save_as_ui_e2e_test.cpp:35`,
    `undo_ui_e2e_test.cpp:34`, `cells_remove_e2e_test.cpp:348`) compile untouched.
12. **`src/app/shell.cpp` is not modified.** `set_view_framing` is already bound at `:288`;
    needing a shell change would mean the seam was wrong.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate` green
(check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.**
  `src/dock/ace/dock/dock.hpp` gains two `bool` virtuals and **no** include;
  `src/dock/dock.cpp` gains no include. `src/interact/**` and `src/commands/**` are
  unmodified. The join and the `ViewFraming` read stay in `src/app/**`. No entry in
  `scripts/check_levels.py:24-51` changes; no component gains an `ImGui`/`GL`/`SDL` include
  it did not already have.

- **L1 logic — Catch2 unit.** New `tests/new_shot_from_view_test.cpp`, added to the
  `ace_tests` source list (`CMakeLists.txt:219-236`), naming follows
  `tests/frame_selection_test.cpp` (`TEST_CASE("new shot from view: …")`). It pins the laws
  this leaf's L4 join depends on — none of which is currently asserted:
  - **The promoted shot reproduces the view exactly (the WYSIWYG law).** For a nav-produced
    viewport camera `C = {s,0,0,s,tx,ty}` and a pane `pw×ph`,
    `viewport_camera_for_shot(new_shot_from_view(C,pw,ph).frame, pw, ph, pw, ph) == C`
    componentwise, for several `(s, tx, ty, pw, ph)` including `s < 1`, `s > 1` and
    non-square panes (`look_through.md:260-267`, asserted here against **this** verb's
    producer).
  - **Resolution is the pane in device pixels** (amended D23): `shot.width == pw &&
    shot.height == ph` for every non-degenerate input, including `pw*ph` above
    `k_max_mint_resolution²` — i.e. **the clamp is deliberately not applied**
    (D-new_shot_from_view-1).
  - **Pixels are square and the frame is the inverted camera** (D9's aspect-lock): for a
    uniform-scale `C`, `frame.a == frame.d` exactly, and `frame` maps `[0,pw]×[0,ph]` onto the
    composition-space region the viewport shows.
  - **The two mint verbs agree at a 1:1 viewport, and only there** (D23's "one derivation
    rule", pinning D-frame_selection-2 (ii) — `frame_selection.md:641-646`): with
    `C = {1,0,0,1,tx,ty}`, `new_shot_from_view(C,pw,ph)` and
    `shot_from_extent(Rect{-tx, -ty, pw-tx, ph-ty})` produce an identical `frame` and identical
    `width`/`height`; at `s = 2` the resolutions differ by the zoom factor, which is the
    documented, intended divergence.
  - **Degenerate inputs refuse rather than guess** (Constraint 4's sentinel): `pane_w <= 0`,
    `pane_h <= 0`, and a non-invertible `C` (zero scale) each yield an identity `frame`, and
    the `{width, height}` a caller must reject.
  - **Name allocation is shared across both mint verbs** (D-new_shot_from_view-7):
    `next_camera_name` over a document already holding `Camera 1` (minted by either verb)
    returns `Camera 2`; after an undo it returns `Camera 1` again (deterministic, therefore
    assertable in the e2e).

- **Rendered output — golden: N/A, justified.** **No new golden file.** A camera is a
  **non-rendering** `Content` (A14, `docs/01-architecture.md:305`), so minting one cannot
  change any rendered byte; that invariance is already pinned by
  `editor.cameras.frame_selection`'s golden case against
  `tests/goldens/cells_insert_nested_64x64.rgba8`, and this leaf mints through the *identical*
  `commands::add_camera_command`, adding no render surface. The rendered-output claim that
  *is* new here — "the shot renders what the viewport showed" — is not comparable against an
  on-screen software-GL frame, so it is asserted as the exact affine round-trip law in the L1
  unit above rather than as a pixel golden. Anti-vacuity: that law is asserted to **fail** for
  a deliberately mismatched resolution (`viewport_camera_for_shot(frame, pw, ph, 2*pw, 2*ph)
  != C`), so an implementation returning `C` unconditionally cannot pass.

- **UI e2e — ImGui Test Engine.** New `tests/new_shot_from_view_e2e_test.cpp`, added to the
  `ace_shell_test` source list (`CMakeLists.txt:251-265`), registered
  `IM_REGISTER_TEST(engine, "cameras", "new_shot_from_view_rail")`, built on
  `tests/frame_selection_e2e_test.cpp`'s harness (real `AppProjectGateway` + real `CanvasView`
  + `CameraInspector` over a `ScratchDir` project) and asserting on **model state, never
  pixels** (the stated file-header policy at `frame_selection_e2e_test.cpp:1-17`). Addressed as
  `const std::string item = ace::dock::tool_rail_title() + std::string("/###new_shot_from_view")`.
  Phases:
  1. The item **exists** and is **disabled** before `canvas#1` has been sized; after
     `pump_until(ctx, [&]{ return canvas.frames_issued("canvas#1") >= 1; })` its
     `ItemInfo(...).ItemFlags & ImGuiItemFlags_Disabled` is clear (Constraints 3-4).
  2. One `ctx->ItemClick(item)` mints **exactly one** camera: `scene::cameras(document).size()
     == 1`, named `"Camera 1"`, whose `resolution` equals the canvas pane's device size (read
     via the `"canvas#1/##canvas_nav"` pane-rect probe, `camera_manip_e2e_test.cpp:250-271`)
     and whose `frame` inverts to the presenter camera
     (`interact::viewport_camera_for_shot(cam.frame, cam.resolution.width,
     cam.resolution.height, …)` matches within `near()`).
  3. **Nothing else moved** (Constraint 7): the selection is still empty, and the canvas's
     camera picker still has `"Viewport"` active — the minted `"Camera 1"` button appears
     (`canvas_view.cpp:236-245`) but was not clicked for the user.
  4. Wheel-pan/zoom the canvas (`canvas_nav_e2e_test.cpp`'s recipe), click again → `"Camera
     2"` with the **same resolution** and a **different** frame — the promotion tracks the
     live view, not a cached one.
  5. `ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z)` removes `"Camera 2"`; `"Camera 1"` stands;
     a third click re-mints `"Camera 2"` (first-free-`n`, D-frame_selection-9).
  6. The minted camera reaches both readers: `ctx->ItemExists("canvas#1/Camera 1")` and, after
     `ctx->WindowFocus("Inspector"); ctx->ItemClick("Inspector/###cam_0")`,
     `ctx->ItemReadAsInt("Inspector/Width")` equals the pane width.
  7. **The two mint verbs are independently gated** (D23): with an empty selection,
     `###frame_selection` is disabled while `###new_shot_from_view` is enabled; selecting a
     cell enables both.
  8. **Closing every canvas disables the item** (D18 has no keep-a-canvas guardrail): close
     the Canvas view through the dockspace, pump, then assert the item is disabled and a click
     leaves `scene::cameras()` unchanged (D-new_shot_from_view-2).
  Plus a second `TEST_CASE` with a local `BareGateway` (the
  `frame_selection_e2e_test.cpp:318-339` pattern) asserting the inert defaults:
  `CHECK_FALSE(bare.can_new_shot_from_view()); CHECK_FALSE(bare.new_shot_from_view());`
  (Constraint 11).

- **L4 join coverage — Catch2, headless.** Cases appended to
  `tests/app_project_gateway_test.cpp` after the frame_selection block (`:434-560`):
  - With `set_view_framing` installed returning a known `ViewFraming{C, pw, ph}`, one
    `new_shot_from_view()` returns `true` and produces one camera whose `resolution ==
    {pw, ph}` and whose `frame == C.inverse()`.
  - With **no** provider installed (and with one returning a zero pane),
    `can_new_shot_from_view()` is `false`, `new_shot_from_view()` returns `false`, and
    `scene::cameras(document)` stays **empty** — the root-composition fallback is **not**
    substituted (Constraint 4). A companion assertion keeps `insert_cell` working in that same
    state, proving the `view_framing()` refactor did not regress the fallback consumer.
  - The mint runs **through the installed edit runner**: a recording runner installed via
    `set_edit_runner` observes exactly one invocation per click and the document is unchanged
    before it runs (Constraint 5).
  - Two mints then `commands::undo` twice leaves zero cameras; the journal-entry accounting
    matches `add_camera`'s documented two-entries/one-observable-undo shape
    (`model.md:391`).

- **Threading (ASan/TSan).** No new threading case and no new lane: the write path is
  byte-identical to `frame_selection`'s (`run_edit` → `CanvasView::apply_edit` →
  `CanvasHost::apply_edit` → one `add_camera_command`), already anchored by the real-pool
  `CanvasHost` case `editor.cameras.frame_selection` appended to `tests/canvas_host_test.cpp`.
  This leaf's sanitizer coverage is the new e2e running in the existing offscreen software-GL
  ASan lane (`docs/01-architecture.md` §9.1) — clean, with **no new `tests/lsan.supp`
  suppression**. If the mint is ever moved outside `run_edit`, that lane is what fails.

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed lines;
  clang-format + build clean. Tests ship with the task.

- **Doc delta (same commit).** `docs/00-design.md:490` — **D23 amended** with the
  per-verb resolution clause and the per-verb refusal clause. See D-new_shot_from_view-1 and
  D-new_shot_from_view-2. No `docs/01-architecture.md` row changes: A12/A13/A16 already cover
  a new `ProjectGateway` primitive-only verb pair, and A14 already covers the camera kind.

- **Deferred WBS work.** One named follow-up, for the closer to register mechanically:
  - **`editor.cameras.mint_from_focused_canvas`** — *"Promote the focused canvas's framing,
    not canvas#1's"*, **0.5d**, `allocate team`, `depends !new_shot_from_view`, under
    `task cameras` (`tasks/00-editor.tji:257`), already wired into `m9_editor`
    (`tasks/99-milestones.tji:8`) through the `editor.cameras` container dependency. Scope:
    `CanvasView::primary_framing()` (`src/app/canvas_view.cpp:523-534`) deliberately returns
    the **lowest-id** sized presenter (*"a deterministic choice, not the most-recently-drawn
    one"*), so with two canvases open (D18/D5's multi-canvas payoff) "New Shot From View"
    always promotes `canvas#1`'s framing even when the user is working in `canvas#2`. Add a
    focused-canvas view id to `CanvasView` (set from the per-canvas window's
    `ImGui::IsWindowFocused` at draw time, falling back to the lowest-id rule when no canvas
    holds focus), expose `focused_framing()`, and bind the gateway's framing provider to it —
    plus a `multi_canvas` e2e that focuses `canvas#2`, pans it, mints, and asserts the shot
    matches `canvas#2`. `insert_cell`'s provisional placement moves to the same source, since
    it has the identical surprise. Source-of-debt:
    `tasks/refinements/cameras/new_shot_from_view.md`. Design: `docs/00-design.md` D23, D18.
  - Everything else out of scope already has a scheduled owner: the inspector affordance
    (an empty-state **button** rather than a reworded label) is `editor.panels.inspector`
    (`.tji:359`, a two-line reuse of the pair this leaf ships); a keyboard chord waits on §11's
    input map, still unwritten and deliberately not pre-empted (D-frame_selection-8);
    rendering a minted camera to a file is `editor.cameras.export` (`.tji:319`); an
    overview-driven mint is `editor.panels.overview` (`.tji:371`); `camera↔cell bounds`
    snapping is `editor.cells.gizmo` (`.tji:336-341`).

## Decisions

- **D-new_shot_from_view-1 — This leaf calls `interact::new_shot_from_view` **unchanged**; the
  resolution is the pane in device pixels, with D23's rounding, clamp and aspect-expansion
  deliberately not applied.**
  `interact.hpp:109` / `interact.cpp:97-111` set `shot.width = pane_w`, `shot.height = pane_h`
  and `shot.frame = camera.inverse()`. The leaf adds no arithmetic on top.
  *Rationale:* (i) **WYSIWYG is the point of the verb.** `docs/00-design.md:64-66` says the
  verb *promotes* the viewport's framing, and `:58-61` says the viewport camera's resolution
  *is* the canvas pixel size — so keeping the pane's pixel count is what makes the shot,
  rendered at its own resolution, reproduce what was on screen. D-frame_selection-2's rejected
  alternative (`frame_selection.md:650-655`) grants exactly this rule to this verb by name.
  (ii) **The rounding and expansion steps are exact no-ops here.** A pane is already a whole
  number of pixels, and the frame is the *exact* inverse of a uniform-scale nav camera
  (`interact::pan`/`zoom`/`fit` all produce `{s,0,0,s,tx,ty}`), so `frame.a == frame.d` holds
  identically — D9's aspect-lock is satisfied without a fix-up. (iii) **The clamp's hazard is
  structurally absent.** `k_max_mint_resolution` exists because *"a composition-scale selection
  must not mint a terapixel camera"* (`interact.hpp:113-116`) — an extent is unbounded above,
  a pane is bounded by the display.
  *Alternative rejected:* **clamp at L4** (scale the pane down when the longer side exceeds
  8192). It duplicates arithmetic that already exists at L1 in `shot_from_extent`, is
  untestable headless as an L1 law, and — because a uniform down-scale plus rounding perturbs
  the aspect by up to a pixel — would drag in the expand-about-center fix-up, i.e. minting a
  frame the user never framed, to protect against a case unreachable below an 8K-wide single
  pane. The inspector's W×H is D9's documented escape hatch for anyone who does mint one.
  *Alternative rejected:* **amend `interact::new_shot_from_view` to clamp.** It is shipped and
  unit-tested with 22 L1 cases, and `editor.cameras.look_through`'s round-trip law
  (`look_through.md:260-267`) is stated over its output; editing a settled primitive to serve
  its first caller inverts the dependency.
  **Doc delta: D23 amended** (`docs/00-design.md:490`) with the per-verb clause, because D23
  currently states one derivation rule as if both verbs applied every step of it, and
  `editor.cameras.export` will read that row to know what resolution a shot carries. This is
  the constitution being made to say what both implementations already do.

- **D-new_shot_from_view-2 — The mint requires a **live, sized** canvas pane; the
  root-composition fallback in `view_framing()` is `insert_cell`'s and is not extended to this
  verb.**
  `AppProjectGateway::view_framing()` (`project_gateway.cpp:172-188`) splits into
  `std::optional<ViewFraming> live_view_framing() const` (returns the provider's value only
  when `pane_w > 0 && pane_h > 0`) and the existing `view_framing()` = that, or the
  root-composition fallback. `can_new_shot_from_view()` is `live_view_framing().has_value()`;
  the mint reads the same optional inside `run_edit`.
  *Rationale:* D18 (`docs/00-design.md:485`) has **no keep-a-canvas guardrail** — every canvas
  can be closed — so "no viewport" is a reachable product state, not a test artifact. In that
  state the fallback frames the root composition at identity, which would mint a camera the
  user never framed while the item claimed to promote "the view". D23's own refusal principle
  (*"inventing one would be a guess"*) applies verbatim; disabling the item states the
  precondition honestly, in the shape Constraint 3 already mandates. Splitting rather than
  changing `view_framing()` keeps `insert_cell`'s provisional placement bit-identical, which
  the L4 suite asserts.
  *Alternative rejected:* **reuse `view_framing()` as-is**, making the item always enabled and
  minting a composition-fit camera with no canvas open. It is cheaper and makes the headless
  gateway test trivial, but it silently changes the verb's meaning in exactly the case where
  the user cannot see what they are promoting.
  *Alternative rejected:* **gate on the document instead** (a `commands::can_new_shot_from_view(
  const AppState&)` mirroring `can_frame_selection`). The precondition is about L4 session
  state — whether a pane exists — not about the document, so an L1 predicate would have
  nothing to read. See D-new_shot_from_view-6.
  **Doc delta: D23 amended** (the per-verb refusal clause).

- **D-new_shot_from_view-3 — The seam is a `ProjectGateway` `bool` pair,
  `can_new_shot_from_view()` / `new_shot_from_view()`, non-pure with inert defaults.**
  Declared in `src/dock/ace/dock/dock.hpp` beside `can_frame_selection`/`frame_selection`
  (`:204-205`), overridden in `app::AppProjectGateway`.
  *Rationale:* it is the shape the `.tji` note names and the shape every session verb on this
  gateway already has (`can_undo`/`undo`, `can_delete`/`delete_selected`,
  `can_frame_selection`/`frame_selection`). Two `bool`s carry everything the rail and the e2e
  need, so — as with `delete_selected` (D-cells_remove-6) — the seam needs not even a
  dock-local POD, which matters because `dock` may include neither `ace/scene` nor
  `ace/commands` (A12/A16, `check_levels.py:35`). Inert defaults keep the six existing gateway
  fakes compiling untouched.
  *Alternative rejected:* **a single `new_shot_from_view()` with no `can_`**, leaving the item
  always enabled (the `Insert Cell…` shape). It contradicts D-new_shot_from_view-2's honest
  precondition and would make phase 8 of the e2e unassertable.
  *Alternative rejected:* **returning the minted `ObjectId`** through the seam. `check_levels`
  would permit it (`arbc` is in `dock`'s `EXTERNAL_ALLOWED` set, `check_levels.py:44-51`), but
  the rail has no use for the id, and handing one over invites rail chrome to start reasoning
  about scene objects — the leak A16's POD-only rule exists to prevent.
  **Covered by A12/A13/A16 — no new architecture row.**

- **D-new_shot_from_view-4 — The `ViewFraming` → `interact` → `commands` join lives at L4
  `app`, entirely inside `run_edit`.**
  `AppProjectGateway::new_shot_from_view()` mirrors `frame_selection()`
  (`project_gateway.cpp:269-298`) line for line: read, derive, refuse-or-dispatch, all in the
  closure.
  *Rationale:* **forced** — `commands` may not include `interact` and `interact` may not
  include `commands` (`scripts/check_levels.py:31-32`, `docs/01-architecture.md:210-211`), so
  no L1 component can hold both halves; `views` (L3) could, but this is not a view. That is
  D-frame_selection-5 unchanged. Running inside the closure is the writer-thread requirement
  (`scene::add_camera` is writer-thread-only) *and* a freshness requirement: the pane size the
  transaction records must be the one live when it lands, not one sampled frames earlier —
  D-cells_remove-3's rule applied to the framing instead of to ids.
  **No doc delta required.**

- **D-new_shot_from_view-5 — The item sits in the Edit section immediately after `Frame
  Selection`, disabled-not-hidden, with no chord, no confirm and no modal.**
  `src/dock/dock.cpp:223`, widget id `New Shot From View###new_shot_from_view`.
  *Rationale:* D23 calls them "two mint verbs"; adjacency in the same section is the
  discoverable reading of that, and the Edit section is already where session verbs that mutate
  the document live. D20 rules camera creation a rail **action**, never a mode. No chord is
  D-frame_selection-8 inherited verbatim: §11's input map is still open, and minting a binding
  here would pre-empt the leaf that writes it. No confirm because the mint is undoable (D15)
  and additive.
  *Alternative rejected:* **a "Cameras" section of its own.** Two items do not justify a
  section, and the rail's fixed geometry (`docs/00-design.md:452-456`) makes every added
  separator permanent chrome.
  *Alternative rejected:* **putting it in the canvas's camera picker** (`canvas_view.cpp:236-245`),
  next to `"Viewport"`. That widget is look-through *activation* — transient session state —
  and mixing a scene transaction into it blurs exactly the D15/D18 line D23 draws.
  **Covered by D20/D23 — no new doc row beyond D23's amendment.**

- **D-new_shot_from_view-6 — `can_new_shot_from_view()` is answered at L4 from `ViewFraming`;
  no new `commands::` predicate is added.**
  Unlike `can_frame_selection` (`src/commands/cameras.cpp:47`, literally
  `!state.selection().empty()`), this precondition reads no document state.
  *Rationale:* the question is "does a canvas pane exist and have a size?", which lives in
  `CanvasView`/`Presenter` — L4/L2 session state that `commands` (L1, → `{base, project,
  scene}`) structurally cannot see. Adding an L1 predicate that takes the answer as a
  parameter would be a pass-through with no logic in it.
  *Alternative rejected:* **`commands::can_new_shot_from_view(const AppState&, ViewFraming)`**
  — `commands` cannot name `app::ViewFraming` (`check_levels.py:32`), and a `bool` parameter
  version has nothing left to compute.
  **No doc delta required.**

- **D-new_shot_from_view-7 — The minted camera inherits D23's naming, selection and
  look-through rules unchanged, sharing one `Camera <n>` sequence with `frame selection`.**
  `commands::next_camera_name(document)` (`src/commands/cameras.cpp:29-45`) returns the first
  free `n` across **all** existing cameras regardless of which verb minted them; the mint
  touches neither `state.selection()` nor any `Presenter::look_through`.
  *Rationale:* D23 states these as properties of *a mint*, not of one verb, and a shared
  sequence is what makes "frame-select, then shot-from-view" read as `Camera 1`, `Camera 2`
  rather than two colliding namespaces. First-free-`n` (not a monotonic counter) keeps
  mint→undo→mint deterministic, which is what lets the e2e assert names at all
  (D-frame_selection-9). Not auto-selecting and not auto-activating is D-frame_selection-10:
  promoting the view must not *change* the view, or the user cannot promote twice from the
  same place.
  *Alternative rejected:* **`Shot <n>`** for this verb, since the design prose calls it a
  "shot". The inspector section is titled `"Cameras"` (`camera_inspector.cpp:37`) and the
  canvas picker lists camera names, so two naming schemes would surface side by side in one
  list — and D7/`docs/00-design.md:66` insists both roles *"appear in the same cameras list and
  are manipulated identically"*.
  **Covered by D23 — no new doc row.**

- **D-new_shot_from_view-8 — The inspector's empty-state string is reworded to name the rail
  item; it does not become a button in this leaf.**
  `src/app/camera_inspector.cpp:41` becomes
  `ImGui::TextDisabled("No cameras — use New Shot From View in the rail.");`
  *Rationale:* the `.tji` note's deliverable is closing the *lie* — the sentence was
  unactionable, and the smallest honest fix is to make it true and point at the affordance that
  now exists. `CameraInspector` holds a `CanvasView&` but **no** `ProjectGateway`
  (`src/app/ace/app/camera_inspector.hpp:25`), so a button would mean either a constructor
  change rippling through `camera_manip_e2e_test.cpp` / `frame_selection_e2e_test.cpp` /
  `look_through_e2e_test.cpp`, or duplicating the L4 join inside the inspector — a second code
  path for one command, which Constraint 6 forbids.
  *Alternative rejected:* **wire the gateway into `CameraInspector` now.**
  `frame_selection.md:597-599` already charters the inspector affordance to
  `editor.panels.inspector` (`.tji:359`) as *"a two-line reuse of the two virtuals this leaf
  ships"* — the same reuse applies to this pair, and doing it here for half the verbs would
  leave the panel half-migrated.
  **No doc delta required.**

## Open questions

(none — all decided.)

One item is routed to `tasks/parking-lot.md` for human review rather than the WBS, because it
is a product-shape judgment rather than implementable work: **whether "New Shot From View"
should promote the framing of the *focused* canvas or of a canvas the user explicitly
designates** once multi-canvas layouts are in real use. This refinement makes the defensible
engineering call (follow focus, with the deterministic lowest-id rule as the fallback) and
registers it as `editor.cameras.mint_from_focused_canvas`; if the answer is instead an explicit
"promote this canvas" affordance in the picker, that is a design change to D23/D18 a human
decides, not an audit an implementer can close.

## Status

**Done** — 2026-07-23.

- Rail item `New Shot From View###new_shot_from_view` added to `src/dock/dock.cpp` (Edit section, after Frame Selection), disabled-not-hidden per D20/D23.
- Two `bool` virtuals `can_new_shot_from_view()` / `new_shot_from_view()` (non-pure, inert defaults) added to `src/dock/ace/dock/dock.hpp`; all six existing gateway fakes compile untouched.
- L4 join in `src/app/ace/app/project_gateway.hpp` and `src/app/project_gateway.cpp`: `view_framing()` split into `live_view_framing()` (live canvas only) + fallback wrapper so `insert_cell`'s provisional placement is unaffected (D-new_shot_from_view-2).
- `src/app/camera_inspector.cpp:41` reworded to `"No cameras — use New Shot From View in the rail."` (D-new_shot_from_view-8).
- `docs/00-design.md` D23 amended with per-verb resolution clause and per-verb refusal clause.
- L1 Catch2 unit `tests/new_shot_from_view_test.cpp` (6 cases: WYSIWYG round-trip + anti-vacuity, unclamped pane resolution, square pixels/frame orientation, two verbs agree only at 1:1, degenerate sentinels, shared `Camera <n>` sequence); registered in `CMakeLists.txt`.
- ImGui Test Engine e2e `tests/new_shot_from_view_e2e_test.cpp` (8-phase rail test `new_shot_from_view_rail` + `BareGateway` inert-defaults case); registered in `CMakeLists.txt`.
- 4 headless L4 cases appended to `tests/app_project_gateway_test.cpp` (mint, refusal with `insert_cell` fallback intact, edit-runner, undo accounting).
- Tech-debt follow-up registered in WBS: `editor.cameras.mint_from_focused_canvas`.
