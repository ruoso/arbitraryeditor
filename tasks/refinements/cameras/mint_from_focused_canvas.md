# editor.cameras.mint_from_focused_canvas — Promote the focused canvas's framing, not canvas#1's

## TaskJuggler entry

- **Task:** `editor.cameras.mint_from_focused_canvas`, `tasks/00-editor.tji:320-325`, inside
  `task cameras "Cameras"` (`tasks/00-editor.tji:257`), inside `task editor`
  (`tasks/00-editor.tji:26`).
- **Effort:** `0.5d` · `allocate team`.
- **Depends:** `!new_shot_from_view` (i.e. `editor.cameras.new_shot_from_view`,
  `tasks/00-editor.tji:313-319`, `complete 100`).
- **Note (`.tji:324`):** *"CanvasView::primary_framing() (src/app/canvas_view.cpp:523-534)
  always returns the lowest-id sized presenter, so with two canvases open 'New Shot From View'
  promotes canvas#1 even when the user works in canvas#2. Add focused-canvas view id to
  CanvasView (set from ImGui::IsWindowFocused at draw time, fallback to lowest-id), expose
  focused_framing(), bind the gateway's framing provider to it, move insert_cell's provisional
  placement to the same source, and add a multi_canvas e2e. Source-of-debt:
  tasks/refinements/cameras/new_shot_from_view.md. Design: docs/00-design.md D23, D18."*
- **Back-link:** this refinement lands at
  `tasks/refinements/cameras/mint_from_focused_canvas.md`. The closer appends
  `Refinement: tasks/refinements/cameras/mint_from_focused_canvas.md` to the `.tji` note and
  adds `complete 100` after `allocate team`. **Do not** hand-edit the `.tji` here.
- **Source of debt:** `tasks/refinements/cameras/new_shot_from_view.md:450-465`, which
  registered this leaf verbatim, and its Open questions (`:639-646`), which routed the
  *product-shape* half of the question — focus-following versus an explicit "promote this
  canvas" designation — to `tasks/parking-lot.md:126-132` for human review while chartering the
  engineering call here.
- **Downstream dependents:** none declare `depends !mint_from_focused_canvas`.
  `editor.panels.overview` (`.tji:371`) will later swap a drag-derived affine into the same
  `insert_cell` placement seam this leaf re-sources; `editor.cameras.export` (`.tji:326-331`)
  consumes whatever cameras this verb mints.
- **Milestone:** already wired into `m9_editor` (`tasks/99-milestones.tji:8`) through the
  `editor.cameras` container dependency.

## Effort estimate

**Half a day.** Every seam this touches is shipped and tested; the leaf adds one piece of
UI-thread state, one selection rule, and one binding swap — plus one coherence fix the rule
exposes (below), which is four lines.

- **The framing seam is already a single, indirected provider.** The shell binds one closure
  (`src/app/shell.cpp:288`, `app_gateway->set_view_framing([&canvas] { return
  canvas.primary_framing(); });`) which `AppProjectGateway` reads through
  `live_view_framing()` (`src/app/project_gateway.cpp:172-180`) and `view_framing()`
  (`:182-198`). **Both** framing consumers — `insert_cell`'s provisional placement
  (`project_gateway.cpp:250-252`) and `new_shot_from_view`'s mint (`:324`, `:334-335`) — read
  that one provider, so the `.tji`'s "move insert_cell's provisional placement to the same
  source" is *already true by construction*: swapping line 288 moves both.
  **No gateway change, no `dock` change, no `interact` change, no `commands` change.**
- **The ImGui fact is one call in a function that already includes ImGui.**
  `src/app/canvas_view.cpp` includes `<imgui.h>` at `:16`, and `draw_content`
  (`:76-93`) runs *inside* the canvas's own `ImGui::Begin` — `src/dock/dock.cpp:575-591`
  begins each view window with the **view id as the window name** (`"canvas#1"`,
  `"canvas#2"`, no `###` split) and the Canvas body is dispatched from
  `src/app/shell.cpp:219-226`. So `ImGui::IsWindowFocused(...)` there answers "is *this*
  canvas focused?" with no plumbing.
- **The presenter map already carries everything the rule needs.**
  `std::map<std::string, Presenter, std::less<>> presenters_`
  (`src/app/ace/app/canvas_view.hpp:199`) is id-ordered with heterogeneous lookup;
  `Presenter::camera` (`:138`) and `Presenter::requested_width/height` (`:133-134`, written at
  `canvas_view.cpp:127-129`) are the two halves of a `ViewFraming`.
- **The two-canvas e2e recipe is shipped.** `tests/multi_canvas_e2e_test.cpp:225-241` opens
  `canvas#2` through the rail's Views launcher and asserts two distinct dock nodes;
  `:246-254` already documents the background-tab caveat and calls `ctx->WindowFocus("canvas#2")`.
  `tests/new_shot_from_view_e2e_test.cpp` carries the full mint harness (real
  `AppProjectGateway` + real `CanvasView` + `CameraInspector` over a `ScratchDir`).

New code: one `std::string` member + one `arbc::Affine` member on `CanvasView`/`Presenter`, one
`ImGui::IsWindowFocused` line and one effective-camera assignment in `draw_content`, a
three-line clear in `reconcile`, one new pure free function + POD in
`src/app/ace/app/view_framing.hpp` with a new `src/app/view_framing.cpp` (picked up by the
`file(GLOB _app_srcs …)` at `CMakeLists.txt:199` — **no CMake edit for the source**), two thin
`CanvasView` accessors, one changed line in `src/app/shell.cpp`, one new headless Catch2 file
and one new e2e file (**two** `CMakeLists.txt` source-list lines). **No new component, no new
DAG edge, no new external dependency, no libarbc change.** One doc delta (**D23, amended**).

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cameras.new_shot_from_view`** (`tasks/refinements/cameras/new_shot_from_view.md`,
  Done 2026-07-23) — the verb whose canvas-selection this leaf corrects. Consumed unchanged:
  **D-new_shot_from_view-2** (the mint requires a **live, sized** pane; the root-composition
  fallback in `view_framing()` is `insert_cell`'s alone and is never substituted into the
  mint — this leaf changes *which* pane is live-and-sized, never whether the refusal happens),
  **D-new_shot_from_view-3** (the `can_new_shot_from_view()`/`new_shot_from_view()` `bool`
  pair on `dock::ProjectGateway`, non-pure with inert defaults), **D-new_shot_from_view-4**
  (the `ViewFraming` → `interact` → `commands` join lives at L4 `app` inside `run_edit`, and
  the framing is read *inside* the closure as a freshness rule), and
  **D-new_shot_from_view-1** (`interact::new_shot_from_view` ships unchanged; the resolution is
  the pane in device pixels, unclamped).
- **`editor.cameras.frame_selection`** (`tasks/refinements/cameras/frame_selection.md`, Done
  2026-07-23) — **D-frame_selection-9** (`Camera <n>`, first free `n`, via
  `commands::next_camera_name`, so mint→undo→mint is deterministic and assertable) and
  **D-frame_selection-10** (a mint touches neither the selection nor any canvas's look-through
  camera). `frame_selection` itself derives from the *document*, not from `ViewFraming`
  (`project_gateway.cpp:278-307`), so it is **untouched** by this leaf.
- **`editor.cameras.look_through`** (`tasks/refinements/editor/look_through.md`, Done
  2026-07-19) — **D-look_through-2** (sizing a look-through pane's viewport to the shot's
  *crop* is what clips it, `canvas_view.cpp:124-131`) and **D-look_through-6** (nav is inert in
  look-through mode, so `Presenter::camera` is *preserved at its last free value*,
  `canvas_view.cpp:158-163`). Those two together are what make today's
  `primary_framing()` incoherent for a look-through pane — see D-mint_from_focused_canvas-5.
- **`editor.cells.model`** (`tasks/refinements/editor.cells/model.md`) — its Constraint 7, the
  `ViewFraming`-by-value seam and the `set_view_framing` binding, plus
  `interact::place_in_view` (`src/interact/ace/interact/interact.hpp:84-86`) as the provisional
  placement rule. This leaf re-sources that seam without changing it.
- **`editor.canvas.multi_canvas`** — `D-multi_canvas-5` (lazy per-`canvas#N` presenter
  registration at `canvas_view.cpp:83-92`, reconciled away at `:493-508` against
  `DockLayout::view_ids()`, driven from `src/app/shell.cpp:296-299`), and the shared-host
  e2e rig this leaf's new e2e borrows its second canvas from.
- **`editor.canvas.single_writer`** — `CanvasHost::apply_edit`
  (`src/render/canvas_host.cpp:201-214`) runs the edit **on the calling thread** under the
  writer-priority document lease. That is the fact that keeps this leaf off the threading
  surface entirely (Constraint 7).

**Pending (owned here):** nothing. Every predecessor is `complete 100`.

## What this task is

1. **`CanvasView` remembers which canvas the user last worked in.** A
   `std::string focused_view_id_` member beside `presenters_`
   (`src/app/ace/app/canvas_view.hpp:199`), stamped in `draw_content`
   (`src/app/canvas_view.cpp:76-93`) whenever
   `ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)` is true for that pane's
   window, and cleared in `reconcile` (`:493-508`) when that pane's presenter is dropped. It is
   **sticky** — a remembered hint, not a per-frame poll (D-mint_from_focused_canvas-1).
2. **A pane reports the framing it is actually showing.** `Presenter` gains
   `arbc::Affine framing_camera`, assigned once per frame as
   `shot_camera ? *shot_camera : p.camera` (`canvas_view.cpp:108-122`, `:158-171`). Today
   `primary_framing()` pairs the *free* camera (`Presenter::camera`, deliberately frozen in
   look-through mode by D-look_through-6) with the *shot's* crop size
   (`requested_width/height`, D-look_through-2) — an incoherent pair. The framing accessors
   read `framing_camera` instead (D-mint_from_focused_canvas-5).
3. **One pure selection rule, in one place.**
   `src/app/ace/app/view_framing.hpp` gains a POD `PaneFraming{std::string_view view_id;
   ViewFraming framing;}` and a free function
   `ViewFraming framing_for_focus(std::span<const PaneFraming> panes_by_id,
   std::string_view focused_view_id)` (impl in a new `src/app/view_framing.cpp`): the focused
   pane's framing when that pane is present **and** sized, else the first sized pane in view-id
   order, else the zero `ViewFraming` the header already documents as *"no live canvas"*
   (`view_framing.hpp:7-21`). `CanvasView::focused_framing()` and the existing
   `CanvasView::primary_framing()` (`canvas_view.hpp:119`) both become thin projections over
   it — `primary_framing()` passes an empty focus id, so its documented lowest-id semantics are
   preserved exactly (D-mint_from_focused_canvas-3, -6).
4. **One binding swap.** `src/app/shell.cpp:288` becomes
   `app_gateway->set_view_framing([&canvas] { return canvas.focused_framing(); });`. Because
   `insert_cell` and `new_shot_from_view` read the *same* installed provider
   (`project_gateway.cpp:250` and `:324`, both through `view_framing_` at
   `project_gateway.hpp:152`), the `.tji`'s "move insert_cell's provisional placement to the
   same source" needs no second edit (D-mint_from_focused_canvas-4).
5. **Two accessors for assertions.** `std::string_view CanvasView::focused_view_id() const`
   (so the e2e can pin the *tracking* separately from the *rule*) and
   `ViewFraming CanvasView::focused_framing() const`.
6. **The multi-canvas coverage the `.tji` asks for**, plus a headless matrix over the rule.

Out of scope, by inheritance and by charter: an **explicit** "promote this canvas" affordance in
the canvas camera picker (parked for human review, `tasks/parking-lot.md:126-132`); any change
to `interact::new_shot_from_view` or `interact::place_in_view` (D-new_shot_from_view-1); any
change to `dock::ProjectGateway` or the rail (D-new_shot_from_view-3, -5); a keyboard chord
(§11's input map is still unwritten, D-frame_selection-8).

## Why it needs to be done

- **The shipped verb promotes the wrong canvas.** D18 (`docs/00-design.md:485`) makes Canvas a
  view and promises *"multiple canvases through different cameras side by side"*;
  `editor.canvas.multi_canvas` delivered it. `primary_framing()`
  (`src/app/canvas_view.cpp:523-534`) then deliberately picks the **lowest-id** sized presenter
  — *"a deterministic choice, not the most-recently-drawn one"* — a placeholder that was
  correct while there was exactly one framing consumer and, in practice, one canvas. With two
  open, "New Shot From View" silently mints `canvas#1`'s framing while the user is panned and
  zoomed in `canvas#2`, and an inserted cell lands where `canvas#1` is looking. D23's
  *"promotes the viewport's current framing"* is simply false in that state.
- **The same wrongness is already latent in a single canvas.** A canvas in look-through mode
  reports `Presenter::camera` (frozen at its last *free* value, D-look_through-6) paired with
  the shot's crop size (D-look_through-2). Minting from it today produces a camera framing
  neither what the user sees nor what the pane last navigated to. This leaf makes focus
  load-bearing, which makes that path far more reachable, so the coherence fix rides here
  rather than being left as a trap for `editor.panels.overview` and `editor.import.image`,
  which will read the same seam.
- **`insert_cell` has the identical surprise and the same one-line cure.** `.tji:324` says so;
  the provider indirection means it costs nothing extra.
- **Two downstream leaves assume the seam is right.** `editor.panels.overview` (`.tji:371`)
  swaps a drag-derived affine into `insert_cell`'s placement; `editor.cameras.export`
  (`.tji:326-331`) renders whatever the mint produced. Both inherit the defect if it is not
  closed now.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **`docs/00-design.md:490` — D23 "Minting a camera"** (amended by this leaf, see *Decisions*).
  Before the delta it says only that *"new shot from view" (§3) fits it to the viewport's
  current framing*, with no answer for *which* viewport once D18 allows several. After the
  delta: *"**Which viewport, when more than one canvas is open (D18):** the **focused** one …
  falling back to the lowest-id live pane when no canvas has yet held focus. A pane offers the
  framing it is **actually showing** … The same focused pane sources a cell insert's provisional
  placement (A16)."*
- **`docs/00-design.md:485` — D18**: the fully-uniform dockspace — *"**Canvas is a view** →
  multiple canvases through different cameras side by side (paint-through-Viewport beside
  look-through-Hero)"*, with **no keep-a-canvas guardrail**. That sentence is simultaneously the
  bug's cause (N canvases) and the two states the rule must handle (free-nav and look-through).
- **`docs/00-design.md:486` — D19**: *"Selection and the shared panels … belong to the
  **project**, not any canvas — canvases are *only* cameras."* This bounds the fix: the leaf
  adds a per-canvas *hint*, not per-canvas project state, and no document state changes.
- **`docs/00-design.md:56-66` (§2, "The core model")** — the viewport camera *"is the active,
  free-navigation one you look through; its 'sensor' is the on-screen canvas (resolution = the
  canvas pixel size)"*, and *"'Look through Hero' makes Hero the active camera"*. The active
  camera of a look-through pane **is the shot** — the textual basis for D-mint_from_focused_canvas-5.
- **`docs/00-design.md:483` — D15**: the transient/scene line. `focused_view_id_` is transient
  session state on the far side of it — never a transaction, never in `project.arbc`.
- **`docs/00-design.md:452-456` (§10)** — the fixed rail; unchanged here (no rail edit).

**Governing architecture rows (`docs/01-architecture.md`):**

- **§8, `:185-220`** — the levelization DAG. Everything this leaf writes is in **L4 `app`**,
  which may depend on everything (`scripts/check_levels.py:36-39`) and is already permitted
  ImGui/SDL/GL (`:44-51`). `:218-220`: *"All of L1 is the testable core and none of it may
  `#include <imgui.h>`"* — which is exactly why the selection rule is extracted as a pure,
  ImGui-free function rather than left inside `draw_content`.
- **A11 (`:301`)** — *"`dockmodel` owns headless UI state incl. active-tool."* Read carefully
  and deliberately **not** extended here: see D-mint_from_focused_canvas-7.
- **A12/A13 (`:303-304`)** — the `dock`-declared `ProjectGateway` with the L4 impl; untouched.
- **A14 (`:305`)** — a camera is a **non-rendering** `Content`, which is why no golden moves.
- **A16 (`:307`)** — cell insert's placement *"arrives as a finished `arbc::Affine` computed by
  the pure helper `interact::place_in_view` … which is the seam `editor.panels.overview` later
  swaps a drag-derived affine into"*. Re-sourcing that affine's framing is inside A16's stated
  shape; no row changes.
- **§9 (`:222-249`)** and **§9.1 (`:251-286`)** — the four verification layers and the offscreen
  software-GL ASan lane the e2e runs in.

**libarbc API surface (v0.2.0 pin, `editor.canvas.arbc_v020`):** `arbc::Affine` only
(`arbc/base/transform.hpp`), already included by `view_framing.hpp:3`. **No new libarbc
surface.**

**Editor seams this leaf extends:**

- `src/app/ace/app/view_framing.hpp:1-23` — the whole file: `ViewFraming{arbc::Affine camera;
  int pane_w = 0; int pane_h = 0;}` and the header comment *"A zero pane means 'no live canvas',
  and the gateway falls back to framing the root composition itself."* Included only by
  `canvas_view.hpp:3` and `project_gateway.hpp:3` — both L4.
- `src/app/ace/app/canvas_view.hpp:70` (`draw_content`), `:89` (`reconcile`), `:94`
  (`frames_issued`), `:119` (`primary_framing`), `:130-166` (`Presenter`, with `camera` at
  `:138` and `requested_width/height` at `:133-134`), `:199` (`presenters_`, the id-ordered
  `std::map` with `std::less<>`).
- `src/app/canvas_view.cpp:16` (`#include <imgui.h>`), `:76-93` (degenerate-pane guard + lazy
  presenter creation), `:100-122` (the look-through resolve producing the local `shot_camera`
  and `target_w/h`), `:124-131` (`requested_width/height` assignment), `:152-171`
  (letterboxed vs. interactive draw, and *"Presenter::camera is preserved at its last free
  value"*), `:493-508` (`reconcile`), `:523-534` (`primary_framing`).
- `src/app/shell.cpp:219-226` (Canvas body registration), `:283-284` (the edit runner — the
  precedent for a shell-installed provider), `:288` (**the one line that changes**), `:291`,
  `:296-299` (`canvas.reconcile(dockspace.layout().view_ids())`).
- `src/dock/dock.cpp:575-591` — the per-view `ImGui::Begin(id.c_str(), &open)` loop: **the
  ImGui window name is the view id**, which is what makes `IsWindowFocused` inside
  `draw_content` meaningful without any new id plumbing.
- `src/app/project_gateway.cpp:168` (`set_view_framing`), `:172-180` (`live_view_framing`),
  `:182-198` (`view_framing` + the root-composition fallback), `:222-260` (`insert_cell`, the
  framing read at `:250` and `place_in_view` at `:251-252`), `:309-314`
  (`can_new_shot_from_view`), `:316-350` (`new_shot_from_view`, framing read inside the closure
  at `:324`). **None of these files is modified.**
- `src/render/canvas_host.cpp:201-214` — `apply_edit` *"Run the mutation on the CALLING thread
  — the UI/writer thread."* The provider closure therefore executes on the UI thread even
  though it is invoked from inside `run_edit`.
- `src/views/views.cpp:49-100` — `draw_canvas_interactive` and its `"##canvas_nav"`
  `InvisibleButton` (`:61`), the e2e's pan/zoom and pane-rect target. Note it is called **only**
  on the free-viewport path (`canvas_view.cpp:169-170`), which is why focus is read in
  `draw_content` rather than threaded through `views::CanvasInput`.
- `src/interact/ace/interact/interact.hpp:84-86` (`place_in_view`), `:93-97` (`ShotFraming`),
  `:109` (`new_shot_from_view`), `:157-158` (`viewport_camera_for_shot`) — all consumed
  unchanged.

**Predecessor refinements:** `tasks/refinements/cameras/new_shot_from_view.md` (esp. `:450-465`
registering this leaf, `:508-533` D-new_shot_from_view-2, `:554-566` D-new_shot_from_view-4,
`:639-646` the parked product question), `tasks/refinements/cameras/frame_selection.md`,
`tasks/refinements/editor/look_through.md:260-267` (the round-trip law).

**Parking lot:** `tasks/parking-lot.md:126-132` — *"'New Shot From View' — focused canvas vs
explicit designation"*. This leaf ships the focus-tracking path that entry describes and does
**not** pre-empt the human call on explicit designation.

**Test rigs:**

- `tests/multi_canvas_e2e_test.cpp:203-213` (engine boot + `IM_REGISTER_TEST(engine,
  "multi_canvas", …)` + `UserData`), `:225-241` (open `canvas#2` via
  `ctx->ItemClick(rail + "/Canvas")`, assert two distinct `DockNode`s), `:246-254` (the
  background-tab caveat — *"canvas#2 … body only runs while it is the active tab"* — and
  `ctx->WindowFocus("canvas#2")`), `:296` (`dockspace.close("canvas#2")` + reconcile drain).
- `tests/new_shot_from_view_e2e_test.cpp` — the mint harness: real `AppProjectGateway` + real
  `CanvasView` + `CameraInspector`, provider bound at `:198`, pane-rect probe vs.
  `primary_framing()` at `:243-255` and `:289-291`, and the *"no canvas → zero framing"*
  assertion at `:347`.
- `tests/camera_manip_e2e_test.cpp:250-271` (the `"canvas#1/##canvas_nav"` pane-rect probe),
  `:366-375` (open `canvas#2`, `WindowFocus`, then `canvas.set_look_through("canvas#2", hero)`).
- `tests/canvas_nav_e2e_test.cpp:171` — the wheel pan/zoom recipe.
- `tests/cells_insert_e2e_test.cpp:150` (provider binding), `:165` (the insert-modal recipe).
- `tests/app_project_gateway_test.cpp:564-700` — the four headless framing cases
  (`:564`, `:605`, `:642`, `:674`), which inject fake providers and are therefore **untouched**
  by this leaf.
- `tests/selection_e2e_test.cpp:378,386` — the other `primary_framing()` probes.
- `CMakeLists.txt:199-201` (`file(GLOB CONFIGURE_DEPENDS "src/app/*.cpp")` → a new
  `src/app/view_framing.cpp` needs **no** CMake edit), `:219-239` (`ace_tests` — **does not
  link `ace::app`**, so nothing in this leaf can be tested there), `:251-268`
  (`ace_shell_test`, links `ace::app`; the tail is `tests/new_shot_from_view_e2e_test.cpp`).
- `scripts/check_levels.py:24-40` (`ALLOWED`), `:44-51` (`EXTERNAL_ALLOWED`); `scripts/gate`
  runs levels · format · build · ctest.

## Constraints / requirements

1. **Levelization (`check_levels` clean).** Every changed and added source file is under
   `src/app/**` (L4), which already closes over every component and every external stack
   (`check_levels.py:36-39`, `:44-51`). `src/dock/**`, `src/views/**`, `src/interact/**`,
   `src/commands/**`, `src/scene/**`, `src/render/**` are **unmodified**. **No entry in
   `scripts/check_levels.py:24-51` changes**, and no component gains an ImGui/GL/SDL include it
   did not already have — in particular `src/app/ace/app/view_framing.hpp` stays **ImGui-free**
   (it gains only `<span>`, `<string_view>`; `arbc/base/transform.hpp` is already there), which
   is what keeps the selection rule unit-testable without a GL context.
2. **Focus is *sticky* UI-thread state, never document state.** `focused_view_id_` is written
   only in `draw_content` and cleared only in `reconcile`, both UI-thread; it is never
   serialized, never a transaction, never read by `commands`/`scene`/`project` (D15/D19).
3. **The rule must be total and must never regress the single-canvas case.** With exactly one
   sized pane, `focused_framing() == primary_framing()` **bit for bit**, focused or not — the
   fallback branch is what every existing single-canvas test exercises, and none of them may
   need editing.
4. **A stale or unsized focus id degrades to the fallback, never to the zero sentinel.** A
   focused pane that was closed, or that exists but has never been sized (the degenerate-pane
   early-out at `canvas_view.cpp:77-79` means a zero-area pane never even gets a presenter),
   must yield the lowest-id sized pane. Returning `ViewFraming{}` there would trip
   `live_view_framing()`'s `pane_w > 0` test (`project_gateway.cpp:172-180`) and *refuse* a mint
   the user can plainly perform (D-new_shot_from_view-2 must not be widened).
5. **A pane reports a coherent (camera, size) pair.** For a free-viewport pane that is
   `{Presenter::camera, pane device size}`; for a look-through pane it is `{the shot's derived
   comp→device camera, the shot's fitted crop size}` — never one from each
   (D-mint_from_focused_canvas-5).
6. **`primary_framing()` keeps its name, its signature and its documented semantics.** Three
   e2e files probe it (`new_shot_from_view_e2e_test.cpp:243-255,:347`,
   `selection_e2e_test.cpp:378,386`); it becomes the empty-focus projection of the shared rule,
   not a deleted or renamed member.
7. **No new threading surface.** `CanvasHost::apply_edit` runs the edit closure on the
   **calling** (UI) thread (`src/render/canvas_host.cpp:201-214`), so `focused_framing()` —
   invoked from inside `run_edit` at `project_gateway.cpp:324` — is read on the same thread that
   writes `focused_view_id_` and `presenters_` in `draw_content`. No mutex, no atomic, no new
   ownership rule. If this ever stops being true, the existing `primary_framing()` read has the
   identical exposure; the leaf must not be the thing that introduces a cross-thread read.
8. **Exactly one line of `src/app/shell.cpp` changes** (`:288`). Needing more would mean the
   provider seam was wrong.
9. **`src/app/project_gateway.{hpp,cpp}`, `src/dock/**` and the rail are not modified.** The
   verb, its gating, its refusal and its rail item are D-new_shot_from_view-2/-3/-5, shipped.
10. **No `dockmodel` change.** Window focus is an ImGui runtime fact, not part of the headless
    layout model (A11); mirroring it into `dockmodel::DockLayout` is explicitly rejected
    (D-mint_from_focused_canvas-7).
11. **`ImGuiFocusedFlags_RootAndChildWindows`, not the default flags.** The canvas pane hosts
    child windows and popups (the camera picker overlay, `canvas_view.cpp:236-245`), and a
    click into one of those must count as focusing the canvas.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate` green
(check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.** The diff touches
  only `src/app/ace/app/view_framing.hpp`, `src/app/view_framing.cpp` (new),
  `src/app/ace/app/canvas_view.hpp`, `src/app/canvas_view.cpp`, `src/app/shell.cpp`,
  `docs/00-design.md`, `CMakeLists.txt` (two test source lines) and `tests/**`. No component
  outside L4 `app` is modified; `view_framing.hpp` gains **no** `ace/` include and **no**
  `<imgui.h>`; no entry in `scripts/check_levels.py:24-51` changes.

- **Pure-logic unit — Catch2, headless.** New `tests/focused_framing_test.cpp`, added to the
  **`ace_shell_test`** source list (`CMakeLists.txt:251-268`) — *not* `ace_tests`, which does
  not link `ace::app` (`CMakeLists.txt:237-239`). It drives `ace::app::framing_for_focus` over
  hand-built `PaneFraming` arrays, with no ImGui context, no GL and no `CanvasView`:
  - **Focus wins over id order** (the whole point): panes `{"canvas#1" sized A, "canvas#2"
    sized B}`, focus `"canvas#2"` → `B`. **Anti-vacuity:** the same input with an empty focus id
    → `A`, so an implementation that ignores its `focused_view_id` argument fails.
  - **Empty focus id reproduces the lowest-id rule** exactly (Constraint 3/6), including
    skipping an unsized lower-id pane in favour of a sized higher-id one.
  - **A focused pane that is present but unsized** (`pane_w == 0`) falls back to the lowest-id
    *sized* pane, **not** to the zero sentinel (Constraint 4).
  - **A focused id naming no pane** (closed, or never seen) falls back identically
    (Constraint 4).
  - **The focused pane wins even when it is the only sized one** and every other pane is
    unsized.
  - **No panes, and all-unsized panes**, each return `ViewFraming{}` with `pane_w == 0` — the
    *"no live canvas"* sentinel `view_framing.hpp:7-21` documents and
    `AppProjectGateway::live_view_framing()` (`project_gateway.cpp:172-180`) keys off, so
    D-new_shot_from_view-2's refusal is preserved through the swap.
  - **The camera travels with the size** — the returned `ViewFraming::camera` is the selected
    pane's, asserted against a distinct affine per pane so a "right size, wrong camera"
    implementation fails.

- **Rendered output — golden: N/A, justified.** **No new golden file and no golden byte
  changes.** (i) A camera is a **non-rendering** `Content` (A14,
  `docs/01-architecture.md:305`), so no mint can change a rendered byte — the invariance is
  already pinned by `editor.cameras.frame_selection`'s case against
  `tests/goldens/cells_insert_nested_64x64.rgba8`. (ii) The one path that *could* move pixels —
  `insert_cell`'s provisional placement — is **bit-identical in every existing golden and e2e**,
  because all of them run a single canvas, where the focused and lowest-id rules coincide
  (Constraint 3). Any golden churn in this diff is therefore a **failure signal**, not an
  update. The multi-canvas claim that is genuinely new is asserted as model state in the e2e
  below, not as pixels: software-GL frames are explicitly not byte-comparable in this rig
  (`tests/multi_canvas_e2e_test.cpp:1-11`).

- **UI e2e — ImGui Test Engine.** New `tests/multi_canvas_mint_e2e_test.cpp`, added to the
  `ace_shell_test` source list (`CMakeLists.txt:251-268`), registered
  `IM_REGISTER_TEST(engine, "multi_canvas", "focused_canvas_mint_and_insert")`. It composes
  `tests/new_shot_from_view_e2e_test.cpp`'s mint harness (real `AppProjectGateway` + real
  `CanvasView` + `CameraInspector` over a `ScratchDir` project, provider bound the way
  `shell.cpp:288` binds it — to `focused_framing()`) with
  `tests/multi_canvas_e2e_test.cpp:225-241`'s second-canvas recipe, and asserts **model state,
  never pixels**. A new file rather than an extra `TEST_CASE` in `multi_canvas_e2e_test.cpp`:
  that file's settle/quiet loops (`:270-282`) are tuned around a pixel snapshot this test does
  not take. Phases:
  1. Boot; pump until `canvas.frames_issued("canvas#1") >= 1`. Open `canvas#2` through the rail
     Views launcher (`ctx->ItemClick(rail + "/Canvas")`); pump until
     `canvas.frames_issued("canvas#2") >= 1`.
  2. `ctx->WindowFocus("canvas#2")`, yield; assert `canvas.focused_view_id() == "canvas#2"`.
  3. **Make the two panes distinguishable**: wheel-zoom/pan `"canvas#2/##canvas_nav"`
     (`canvas_nav_e2e_test.cpp:171`'s recipe). Assert
     `canvas.focused_framing().camera` differs from `canvas.primary_framing().camera` — the
     precondition without which every later assertion is vacuous.
  4. **The mint promotes `canvas#2`.** `ctx->ItemClick(rail + "/###new_shot_from_view")` →
     exactly one camera, `"Camera 1"`, whose `resolution` equals `canvas#2`'s pane device size
     (pane-rect probe, `camera_manip_e2e_test.cpp:250-271`) and whose `frame` inverts
     (`interact::viewport_camera_for_shot`) to `canvas#2`'s presenter camera within `near()`;
     and, as the anti-vacuity guard, **does not** match `canvas#1`'s.
  5. **Focus survives the click that steals it** (D-mint_from_focused_canvas-1's decisive
     claim): assert `canvas.focused_view_id() == "canvas#2"` *after* phase 4 — the rail
     `Selectable` focuses the Tool Rail window, so a non-sticky implementation sees no focused
     canvas at mint time and falls back to `canvas#1`, failing phase 4.
  6. **Insert follows the same focus.** Drive `Insert Cell…###insert_cell`
     (`cells_insert_e2e_test.cpp:165`'s recipe) and assert the new cell's placed extent centres
     in the composition region `canvas#2` is showing, not `canvas#1`'s (Constraint: one
     provider, D-mint_from_focused_canvas-4).
  7. **The rule tracks, it does not latch.** `ctx->WindowFocus("canvas#1")`, yield, mint again →
     `"Camera 2"` matches `canvas#1`'s framing.
  8. **A look-through pane promotes the shot it is showing** (D-mint_from_focused_canvas-5):
     `canvas.set_look_through("canvas#2", camera_1_id)` (`camera_manip_e2e_test.cpp:366-375`'s
     recipe), focus `canvas#2`, pump, mint → the new camera's `frame` equals `Camera 1`'s frame
     within `near()` (the same composition-space rectangle) and its `resolution` equals the
     **letterboxed crop size** the pane is displaying (`interact::look_through`'s `out_w/out_h`,
     `canvas_view.cpp:112-118`), which is D23's "the size it occupies on screen". Anti-vacuity:
     assert the result is **not** the pane's own device size and **not** derived from
     `canvas#2`'s frozen free camera — the two ways today's incoherent pair could leak through.
  9. **Closing the focused canvas falls back cleanly.** `dockspace.close("canvas#2")`, pump the
     reconcile drain (`multi_canvas_e2e_test.cpp:296-301`); assert
     `canvas.focused_view_id()` is empty, `focused_framing() == primary_framing()`, and a mint
     produces a camera matching `canvas#1` — no crash, no stale hint.
  10. **Closing every canvas still refuses** (D-new_shot_from_view-2 preserved): close
      `canvas#1` too, pump, assert `canvas.focused_framing().pane_w == 0`, the rail item's
      `ItemInfo(...).ItemFlags & ImGuiItemFlags_Disabled` is set, and a click leaves
      `scene::cameras(document)` unchanged.

- **Regression — the single-canvas suites must pass *unmodified*.** `scripts/gate` green with
  **zero edits** to `tests/new_shot_from_view_e2e_test.cpp` (`:243-255`, `:289-291`, `:347`),
  `tests/selection_e2e_test.cpp` (`:378`, `:386`), `tests/cells_insert_e2e_test.cpp`,
  `tests/frame_selection_e2e_test.cpp`, `tests/cells_remove_e2e_test.cpp`,
  `tests/multi_canvas_e2e_test.cpp` and the four headless framing cases in
  `tests/app_project_gateway_test.cpp:564-700` (which inject fake providers and never see
  `CanvasView`). Those rigs may keep binding `primary_framing()` — with one canvas the two rules
  coincide, so the binding choice is unobservable there, and leaving them alone is what proves
  Constraint 3. **An edit to any of them is a signal the rule changed single-canvas behaviour.**

- **Threading (ASan/TSan).** No new threading case and no new lane, stated as a positive claim:
  `focused_view_id_`, `Presenter::framing_camera` and `presenters_` are all UI-thread-owned, and
  `CanvasHost::apply_edit` runs the edit closure **on the calling thread**
  (`src/render/canvas_host.cpp:201-214`), so the provider read at `project_gateway.cpp:324`
  never crosses a thread. Coverage is the new e2e running in the existing offscreen software-GL
  ASan lane (`docs/01-architecture.md` §9.1) — clean, with **no new `tests/lsan.supp`
  suppression**. The two-canvas + shared-`CanvasHost` + render-thread configuration that lane
  exercises is precisely the one where a mis-scoped focus read would report.

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed lines;
  clang-format + build clean. Tests ship with the task.

- **Doc delta (same commit).** `docs/00-design.md:490` — **D23 amended** with the *"Which
  viewport, when more than one canvas is open (D18)"* clause (focused pane, lowest-id fallback,
  the actually-showing rule for look-through panes, and the shared source for insert's
  provisional placement). See D-mint_from_focused_canvas-1, -2, -4, -5. **No
  `docs/01-architecture.md` row changes**: A11's `dockmodel` scope is deliberately not widened
  (D-mint_from_focused_canvas-7), A12/A13/A16 already cover the seam, and A14 already covers the
  camera kind.

- **Deferred WBS work.** One named follow-up, for the closer to register mechanically:
  - **`editor.canvas.focused_canvas_indicator`** — *"Show which canvas the framing-derived verbs
    act on"*, **0.5d**, `allocate team`, `depends editor.cameras.mint_from_focused_canvas`,
    under `task canvas "Canvas & rendering"` (`tasks/00-editor.tji:171`), wired into `m9_editor`
    (`tasks/99-milestones.tji:8`) through the `editor.canvas` container dependency. Scope: this
    leaf makes window focus semantically load-bearing — the focused canvas decides what "New
    Shot From View" promotes and where an inserted cell lands — but leaves it **invisible**,
    since ImGui's own focus chrome is a title-bar tint that is easy to miss in a two-pane dock
    and is absent while the rail holds focus. Draw a passive marker on the pane
    `CanvasView::focused_view_id()` names (a one-pixel accent border inside the pane rect, or a
    small badge beside the existing camera-picker overlay at `src/app/canvas_view.cpp:236-245`),
    driven from the sticky hint rather than from live ImGui focus so it persists across the rail
    click — plus an e2e phase asserting the marker follows `WindowFocus` and survives a rail
    interaction. Nothing in `dockmodel`, nothing in `dock` (D18's "no privileged editor area" is
    about *layout*, not about indicating where the pointer verb applies). Source-of-debt:
    `tasks/refinements/cameras/mint_from_focused_canvas.md`. Design: `docs/00-design.md` D23,
    D18.
  - Everything else out of scope already has an owner: the **explicit** "promote this canvas"
    designation is a human product call parked at `tasks/parking-lot.md:126-132` (never a WBS
    leaf); a drag-derived insert placement is `editor.panels.overview` (`.tji:371`); rendering a
    minted camera to a file is `editor.cameras.export` (`.tji:326-331`); the inspector's mint
    button is `editor.panels.inspector` (`.tji:359`); a keyboard chord waits on §11's input map
    (D-frame_selection-8).

## Decisions

- **D-mint_from_focused_canvas-1 — Focus is a **sticky remembered view id**, stamped in
  `draw_content` from `ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)`, not a
  poll evaluated when the framing is asked for.**
  `CanvasView` gains `std::string focused_view_id_`; `draw_content`
  (`src/app/canvas_view.cpp:76-93`) sets it to the current `view_id` on any frame where that
  pane's window is focused, and never clears it on a frame where it is not.
  *Rationale:* (i) **The mint click itself steals focus.** The rail item is an
  `ImGui::Selectable` in the Tool Rail window (`src/dock/dock.cpp:231-232`), so by the time
  `AppProjectGateway::new_shot_from_view()` runs, the focused window is `"Tool Rail"` and **no
  canvas is focused at all**. A non-sticky implementation would therefore see "nobody focused"
  on *every single mint* and fall back to `canvas#1` — i.e. it would reproduce the exact bug
  this leaf exists to fix. Stickiness is not a nicety here; it is the only shape that works.
  (ii) **A background dock tab does not draw.** `tests/multi_canvas_e2e_test.cpp:246-252`
  records that `canvas#2`'s *"body only runs while it is the active tab"*, so a pane that is
  focused-then-tabbed-behind would silently revert under a poll. The sticky hint means "the
  canvas the user most recently worked in", which is the semantic `.tji:324` asks for.
  (iii) `RootAndChildWindows` because the pane hosts the camera-picker overlay
  (`canvas_view.cpp:236-245`) and popups; clicking one of those is working in that canvas
  (Constraint 11).
  *Alternative rejected:* **thread focus out through `views::CanvasInput`**
  (`src/views/views.cpp:49-100`, which already exports `hovered`/`pressed`/`down`).
  `draw_canvas_interactive` is called **only** on the free-viewport path
  (`canvas_view.cpp:169-170`); a look-through pane calls `views::draw_letterboxed`
  (`:163`) and produces no `CanvasInput` at all, so a look-through canvas could never become
  the focused one. Reading it in `draw_content` covers both modes with one line.
  *Alternative rejected:* **most-recently-*drawn* instead of most-recently-*focused***. Every
  visible pane draws every frame, so "most recently drawn" is just the iteration order — the
  same arbitrary choice under a new name.
  **Doc delta: D23 amended** with the "which viewport" clause.

- **D-mint_from_focused_canvas-2 — The rule is *focused-and-sized, else lowest-id sized, else
  the zero sentinel*; a stale or unsized hint degrades to the fallback, and `reconcile` clears a
  hint whose presenter is dropped.**
  The rule is **total**: it never returns the zero `ViewFraming` while any sized pane exists.
  `reconcile` (`src/app/canvas_view.cpp:493-508`) additionally clears `focused_view_id_` when
  that presenter is erased.
  *Rationale:* the fallback must be the *existing* lowest-id rule, not "no framing", because
  `AppProjectGateway::live_view_framing()` (`project_gateway.cpp:172-180`) treats a zero pane as
  "no live canvas" and `can_new_shot_from_view()` (`:309-314`) then **disables the rail item**.
  A user with two canvases open who has not yet clicked either one must still be able to mint
  (Constraint 4) — widening D-new_shot_from_view-2's refusal would be a behaviour regression
  disguised as a fix. The `reconcile` clear is hygiene rather than correctness (the rule already
  treats a missing id as no-focus): view ids are minted from the layout and can be **reused**
  after a close, so without the clear a freshly-opened `canvas#2` would inherit the previous
  `canvas#2`'s focus without ever being focused. Two cheap mechanisms, one load-bearing
  invariant, both asserted.
  *Alternative rejected:* **most-recently-focused-among-live, kept as an ordered stack** so that
  closing the focused canvas falls back to the *previously* focused one rather than to
  `canvas#1`. Strictly nicer with three or more canvases, but it adds a container, an eviction
  rule and an ordering invariant to a 0.5d leaf, and D18's two-canvas payoff is the case that
  actually exists. The single-string hint is upgradeable to a stack later without moving the
  seam.
  *Alternative rejected:* **clear the hint whenever focus leaves the canvas** — that is
  D-mint_from_focused_canvas-1's rejected poll wearing a different hat, and it breaks on the
  rail click.

- **D-mint_from_focused_canvas-3 — The selection rule is extracted as a pure free function
  `ace::app::framing_for_focus(std::span<const PaneFraming>, std::string_view)` in
  `view_framing.{hpp,cpp}`, and both `focused_framing()` and `primary_framing()` are thin
  projections over it.**
  `CanvasView::focused_framing()` projects `presenters_` (already view-id ordered,
  `canvas_view.hpp:199`) into a small `std::vector<PaneFraming>` and calls it with
  `focused_view_id_`; `primary_framing()` calls it with an empty focus id.
  *Rationale:* (i) **It is the only way this logic gets headless coverage.** `CanvasView`
  needs a GL context, a render thread and an ImGui context; `ace_tests` does not even link
  `ace::app` (`CMakeLists.txt:237-239`). Left inline, a six-branch rule would be reachable only
  through e2e phases, and the three cases that matter most — focused-but-unsized,
  focused-but-closed, all-unsized — are exactly the ones an e2e cannot cheaply construct. The
  §8 principle (`docs/01-architecture.md:218-220`) is that logic belongs where Catch2 can see
  it; L4 cannot reach L1, so the next best thing is an ImGui-free, GL-free function inside
  `app`. (ii) **It unifies the two accessors**, which is what makes Constraint 3
  (single-canvas bit-identity) a structural fact rather than a claim about two hand-written
  loops. (iii) The projection allocates a handful of `string_view`/`Affine` pairs and runs
  **only on user action** (a mint or an insert), never per frame.
  *Alternative rejected:* **a nine-line member function on `CanvasView`, no extraction.**
  Smaller diff, and the happy path is covered by the e2e — but it leaves the fallback matrix
  untested and duplicates the sized-pane predicate across two members.
  *Alternative rejected:* **put the rule in L1 `interact`** so it lands in `ace_tests`.
  `interact` (L1 → `{base, scene}`) may not name `app::ViewFraming`
  (`scripts/check_levels.py:31`), so this would mean either duplicating the struct or inventing
  a primitive-only mirror of it — a new L1 concept with one caller, to relocate ten lines.
  **No doc delta required.**

- **D-mint_from_focused_canvas-4 — One provider, both consumers: only `src/app/shell.cpp:288`
  changes, and `insert_cell` moves to the focused source for free.**
  `insert_cell` reads the framing at `project_gateway.cpp:250` and `new_shot_from_view` at
  `:324`, both through the single `view_framing_` closure (`project_gateway.hpp:152`) installed
  by `set_view_framing`.
  *Rationale:* the `.tji` note lists "move insert_cell's provisional placement to the same
  source" as separate work; it is not, because `editor.cells.model` and
  `editor.cameras.new_shot_from_view` already converged on one provider. Keeping it that way is
  also the right *product* answer: D19 makes selection and the panels project-level while
  *"canvases are only cameras"*, so "the canvas the user is working in" is one session-level
  fact, and a cell inserted while the user works in `canvas#2` must land where `canvas#2` looks.
  Two verbs disagreeing about which canvas is current would be a worse bug than the one being
  fixed.
  *Alternative rejected:* **two providers** (`set_view_framing` for insert, a new
  `set_mint_framing` for the mint), on the theory that a mint is a deliberate act while a
  placement is incidental. It doubles the seam, doubles the shell binding, and the `.tji` note
  explicitly says the two verbs have *"the identical surprise"*.
  **Doc delta: D23 amended** (the shared-source clause).

- **D-mint_from_focused_canvas-5 — A pane reports the framing it is **actually showing**:
  `Presenter` gains `framing_camera`, set to the shot's derived camera in look-through mode and
  to the free camera otherwise. This fixes a pre-existing incoherence in `primary_framing()`.**
  Today `primary_framing()` (`canvas_view.cpp:523-534`) pairs `Presenter::camera` with
  `Presenter::requested_width/height`. In look-through mode those two come from different
  worlds: `requested_width/height` are set to the shot's fitted **crop** size
  (`:124-131`, D-look_through-2) while `Presenter::camera` is *"preserved at its last free
  value"* because nav is inert there (`:158-163`, D-look_through-6). The pair describes no
  actual view. `draw_content` therefore assigns `p.framing_camera = shot_camera ? *shot_camera
  : p.camera` once per frame, and both accessors read it.
  *Rationale:* (i) **`docs/00-design.md:56-66` settles the semantics** — the active camera of a
  look-through pane *is the shot* (*"'Look through Hero' makes Hero the active camera"*), and
  D23 promotes *the viewport's current framing*, so promoting a Hero-pane must yield Hero's
  framing at the size it is on screen. That is a coherent, useful gesture ("duplicate this shot
  at screen resolution"), and `interact::viewport_camera_for_shot`'s round-trip law
  (`look_through.md:260-267`) makes it exactly reproducible. (ii) **`insert_cell` needs it even
  more than the mint does**: `interact::place_in_view` centres new content in the region the
  framing describes, so an incoherent pair drops the cell somewhere the user is not looking.
  (iii) **This leaf is what makes the bug reachable.** It was survivable while the framing
  source was pinned to `canvas#1` (typically the free-nav paint pane in D18's
  *"paint-through-Viewport beside look-through-Hero"* layout); once focus decides, a user
  working in the Hero pane hits it immediately. Fixing it in the successor task would mean
  shipping a known-wrong `focused_framing()` and asking `editor.panels.overview` and
  `editor.import.image` to build on it.
  *Alternative rejected:* **treat a look-through pane as not a framing source** and skip it in
  the rule. Superficially tidy — "the viewport camera is the free one" — but it makes both verbs
  behave differently depending on an invisible per-pane mode, and with *every* open canvas in
  look-through mode both verbs would fall through to the zero sentinel: the mint refused and an
  insert silently placed against the root composition, in a session with two perfectly visible
  canvases.
  *Alternative rejected:* **report the free camera with the *pane's* device size** (fix the
  pair the other way). It is also coherent, and it is what a "the viewport camera never stops
  being the viewport camera" reading suggests — but it promotes a framing that is not on screen
  anywhere, which is precisely the WYSIWYG failure D23's per-verb clause exists to prevent.
  **Doc delta: D23 amended** (the "offers the framing it is actually showing" clause).

- **D-mint_from_focused_canvas-6 — `primary_framing()` is kept, public and unrenamed, as the
  empty-focus projection of the shared rule.**
  *Rationale:* it is still a meaningful query (the deterministic, focus-independent framing) and
  three shipped e2e files probe it as an oracle (`new_shot_from_view_e2e_test.cpp:243-255,:347`;
  `selection_e2e_test.cpp:378,386`). Keeping it lets the regression criterion above be stated as
  *"those files pass unmodified"*, which is a far stronger signal than a rename would allow.
  *Alternative rejected:* **rename to `lowest_id_framing()`** for accuracy now that it is no
  longer *primary* in any product sense. It churns four test files, weakens the regression
  claim, and the doc comment at `canvas_view.cpp:524-526` already says exactly what it does.
  *Alternative rejected:* **delete it and inline the empty-focus call at each probe site.** The
  probes would then assert the same code path they are meant to be independent of.
  **No doc delta required.**

- **D-mint_from_focused_canvas-7 — Focus stays an L4 ImGui fact; it is **not** promoted into
  `dockmodel` (A11) and `dock::ProjectGateway` gains nothing.**
  *Rationale:* A11 (`docs/01-architecture.md:301`) gives `dockmodel` the *headless* UI-state
  model — the view catalog, the layout, the active tool — state the editor **owns** and ImGui
  merely renders. Window focus is the opposite: ImGui owns it, mutates it on its own (a click, a
  tab change, a popup open/close), and there is no headless definition of it to model. Mirroring
  it into `dockmodel::DockLayout` would require the shell to push ImGui's focus in every frame
  and would leave the model lying whenever it changed without a redraw. No `dockmodel`, `views`
  or `dock` consumer needs the answer — the only consumer is the L4 framing provider, which sits
  in the same component as the ImGui call.
  *Alternative rejected:* **a `focused_view_id` field on `dockmodel::DockLayout`**, on the
  grounds that "which view is active" sounds like layout state. It would create a second source
  of truth for a fact ImGui already owns, and would put an ImGui-shaped concept into the one L1
  component whose whole point is being ImGui-free.
  *Alternative rejected:* **a `ProjectGateway::focused_canvas()` virtual** so the rail could
  reason about it. A16 restricts the gateway to primitives and dock-local POD, the rail has no
  use for a view id, and the framing already crosses that boundary as a value.
  **Covered by A11/A12/A16 — no new architecture row.**

## Open questions

(none — all decided.)

The one genuinely product-shaped question this area carries — **whether "New Shot From View"
should follow focus at all, or should promote a canvas the user explicitly designates** — was
already routed to `tasks/parking-lot.md:126-132` by
`tasks/refinements/cameras/new_shot_from_view.md:639-646` and remains there for human review. It
is not re-opened here: this leaf ships the focus-tracking path that entry describes, and
D-mint_from_focused_canvas-1/-2 are deliberately shaped so that an explicit designation, if a
human later chooses it, is an *additional* input to `framing_for_focus`'s `focused_view_id`
argument rather than a rewrite of the seam.

## Status

**Done** — 2026-07-23.

- `src/app/ace/app/view_framing.hpp` — added `PaneFraming` POD and pure `framing_for_focus` free-function declaration (ImGui-free, GL-free).
- `src/app/view_framing.cpp` (new) — implements `framing_for_focus`: focused-and-sized wins, else lowest-id sized, else zero sentinel.
- `src/app/ace/app/canvas_view.hpp` — added `focused_view_id_` sticky member, `framing_camera` to `Presenter`, and `focused_framing()` / `focused_view_id()` accessors.
- `src/app/canvas_view.cpp` — stamps `focused_view_id_` in `draw_content` from `ImGui::IsWindowFocused`; assigns `Presenter::framing_camera` coherently (shot camera in look-through, free camera otherwise); clears stale hint in `reconcile`; both `focused_framing()` and `primary_framing()` are thin projections over `framing_for_focus`.
- `src/app/shell.cpp:288` — single binding swap: `canvas.primary_framing()` → `canvas.focused_framing()`.
- `tests/focused_framing_test.cpp` (new) — 6 Catch2 headless cases exercising all `framing_for_focus` branches.
- `tests/multi_canvas_mint_e2e_test.cpp` (new) — 10-phase ImGui Test Engine e2e: dual-canvas mint, focus-sticky rail click, insert follow, look-through coherence, close-canvas fallback, empty-canvas refusal. Uses `std::optional<ace::scene::Camera>` snapshots throughout to avoid dangling-pointer UAF (ASan-confirmed).
- `CMakeLists.txt` — two source lines added to `ace_shell_test` for the two new test files.
- `docs/00-design.md` — D23 amended with the "which viewport" clause (focused pane, lowest-id fallback, actually-showing rule for look-through, shared source for insert placement).
