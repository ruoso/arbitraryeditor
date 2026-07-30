# editor.panels.color — sRGB picker · eyedropper (through camera) · the linear boundary

## TaskJuggler entry

- **Task:** `editor.panels.color` (`tasks/00-editor.tji:639-644`, under
  `task panels "Info panels"` at `:588`).
- **Effort:** `2d` · `allocate team`.
- **Depends:** `editor.dock.view_registry`, `editor.paint.brush`,
  `editor.canvas.tool_dispatch` (`tasks/00-editor.tji:642`).
- **Note (`.tji:643`):** *"A conventional sRGB/HSV/hex picker + straight-alpha
  opacity; the editor is the invisible translator to/from the library's
  premultiplied-linear working space. Eyedropper samples the displayed sRGB
  through the active camera. Design: D10."* The framing is exact — the governing
  row is **D10** (color boundary, `docs/00-design.md:477`), elaborated by **§7**
  (`docs/00-design.md:271-294`).
- **Back-link:** the `.tji` note currently ends `Refinement:
  tasks/refinements/color.md` (the flat interim path). This refinement lands at
  **`tasks/refinements/panels/color.md`** (the path the orchestrator assigned).
  The closer updates the note back-link to the real path and adds `complete 100`
  immediately after `allocate team` (`tasks/refinements/README.md:47-68`).
  **Do not** hand-edit the `.tji` here.
- **Resolves parking-lot item:** *"Authoritative active paint color home and
  `editor.panels.color` dependency edge"* (`tasks/parking-lot.md:290-296`),
  logged by `editor.paint.brush` (D-brush-6 / Open Question 1). This refinement
  settles the structural home (D-color-1) and the dependency-edge question
  (D-color-5); the closer can strike the parking-lot entry.

## Effort estimate

**2 days.** Nothing here is new architecture — every seam already exists:

- The **active-color state** slots onto `commands::AppState` exactly as
  `selection_` / `hovered_object_` / `entered_composition_` already do
  (`src/commands/ace/commands/app_state.hpp:138-163`) — a value field, two
  accessors, one derived converter; no new component, no new DAG edge.
- The **sRGB↔premul-linear conversion** is a thin wrapper over the library
  primitives the editor already calls for the filled-background composite
  (`src/render/render.cpp:64-71`) and the contact sheet
  (`src/commands/contact_sheet.cpp:83-85`) — no hand-rolled transfer function.
- The **picker** is `ImGui::ColorPicker4` in an L4 body registered through the
  shipped `views::register_view_body` seam (the inspector precedent,
  `src/app/shell.cpp:358-363`).
- The **eyedropper** fills the already-wired-but-inert `dispatch_eyedropper` arm
  (`src/app/canvas_view.cpp:623-629`) using the byte-exact `arbc::render_offline`
  path the export leaf already drives from L1 `commands`
  (`src/commands/export.cpp`).
- The **brush rewire** is a one-line read-site swap
  (`src/app/canvas_view.cpp:601`).

The estimate is dominated by the conversion goldens, the eyedropper sampler +
its render-offline golden, and the e2e that proves the picker→brush wire — not
by new abstractions.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.dock.view_registry`** (`tasks/refinements/editor/view_registry.md`,
  Done) — *how the panel becomes a view.* `ViewType::Color` is a fixed
  **singleton** catalog entry in the L1 `dockmodel` view catalog (Color is a
  project-level view per D19). Today it draws a labeled placeholder; this leaf
  supplies its real body through
  `views::register_view_body(dockmodel::ViewType::Color, body)`
  (`src/views/ace/views/views.hpp:117`, dispatched by `draw_view` `:123`),
  bound in `src/app/shell.cpp` exactly as `CameraInspector`/`InspectorPanel` are
  (`shell.cpp:358-363`, teardown `:512`).
- **`editor.paint.brush`** (`tasks/refinements/paint/brush.md`, Done) — *the
  consumer that proves the wire.* The brush deposits `arbc::WorkingPixel` dabs
  into `org.arbc.raster` via `scene::brush_dab`
  (`src/scene/ace/scene/cell.hpp:386`, `src/scene/cell.cpp:567`); the color it
  paints with is read at `src/app/canvas_view.cpp:601`
  (`const arbc::WorkingPixel color = brush_color_;`) from the **placeholder**
  session field `arbc::WorkingPixel brush_color_ = {0,0,0,1}`
  (`src/app/ace/app/canvas_view.hpp:452`, opaque black premultiplied-linear).
  D-brush-6 explicitly reserved this field for `editor.panels.color` to own. This
  leaf **replaces** the placeholder with a read of the canonical active color.
- **`editor.canvas.tool_dispatch`** (`tasks/refinements/editor.canvas/tool_dispatch.md`,
  Done) — *the eyedropper routing.* The closed compile-exhaustive tool switch
  over `dockmodel::ToolId {Select, Brush, Eyedropper, Pan}`
  (`src/dockmodel/ace/dockmodel/tool_rail.hpp:18`) routes the Eyedropper tool to
  the **inert** `CanvasView::dispatch_eyedropper` arm
  (`src/app/ace/app/tool_dispatch.hpp:16,31-32`; arm at
  `src/app/canvas_view.cpp:623-629`, whose own comment names
  `editor.color.color` as its filler). This leaf fills that arm. The eyedropper
  is the *tool's* drag (D20), not a Select fallback.

**Pending (owned here):** the `commands::AppState` active-color field + accessors
+ the `active_working_color()` converter; the `commands` sRGB↔working conversion
pair + the sRGB color value type; the L1 `commands::sample_composited_color`
eyedropper sampler; the L4 `ColorPanel` body + the filled `dispatch_eyedropper`;
the brush read-site swap; and the Catch2 units, the render-offline goldens, the
ImGui Test Engine e2e, and the TSan case. Nothing downstream is blocked on an
unwritten predecessor — all three dependencies are Done.

## What this task is

Three things that together make the library's premultiplied-linear working space
**invisible** (D10):

1. **A canonical active paint color** — one project-level session value, stored
   as **sRGB straight-alpha** (the user-space truth), living on
   `commands::AppState` beside the existing cross-panel session state. Paint
   consumers read a derived **premultiplied-linear `arbc::WorkingPixel`** through
   one accessor; the picker reads/writes the sRGB value. The editor is the
   translator at exactly this boundary.
2. **A conventional sRGB picker panel** — `ImGui::ColorPicker4` (HSV area + hue,
   `#RRGGBB` hex, 0–255 RGB, straight-alpha opacity) drawn into the singleton
   `ViewType::Color` view, editing the canonical sRGB value. Nothing exotic.
3. **An eyedropper tool** — filling the inert Eyedropper arm: on canvas click it
   samples the **composited result as displayed through the active viewport
   camera** (D10's "a render through a camera, not a lookup"), decodes that
   premultiplied-linear sample back to sRGB, and sets the active color.

It also **rewires the brush**: the placeholder `brush_color_` read becomes a read
of the canonical active color, so painting deposits the picked color — the
observable proof that the boundary works end to end.

## Why it needs to be done

D10 (`docs/00-design.md:477`) and §7 (`:271-294`) make the color boundary a
first-class promise: *"the user picks and reads sRGB everywhere, and the editor
decodes to linear-premul on the way in and encodes back to sRGB on the way out …
Premultiplied and linear values are never shown."* The brush shipped with an
**opaque-black placeholder** precisely because there was no authoritative color
home yet (`editor.paint.brush` D-brush-6 parked the structural call). This leaf
is where that call is made — it establishes the single active-color seam that
`editor.paint.brush` (now) and the future `editor.paint.paint_res` /
`editor.paint.retouch_stack` consumers all read, so no paint tool ever hard-codes
a color or reaches into a private canvas field again.

## Inputs / context

**Governing design rows (normative — the constitution):**

- `docs/00-design.md:477` **D10** — users work entirely in **sRGB /
  straight-alpha / hex-HSV**; the editor is the invisible translator to/from
  premultiplied-linear; **compositing stays linear** (no v1 blend-space toggle);
  eyedropper samples **displayed sRGB through the active camera** (composited
  result default, active-cell own straight color via modifier); v1 is
  **sRGB-gamut** (wide-gamut/HDR + display ICC ride `color.working_space` later).
- `docs/00-design.md:271-294` **§7 Color** — the boundary principle in full: the
  picker shape (HSV/HSL area + hue, `#RRGGBB` hex, 0–255 RGB, straight-alpha
  opacity, `:281-282`); "premultiplied and linear values are never shown"
  (`:279`); the eyedropper is a render through a camera (`:287-290`); v1 sRGB
  gamut scope (`:291-294`).
- `docs/01-architecture.md:422` **A7** — one owned `commands::AppState` per
  process, the single project session; the home for project-level transient
  session state.
- `docs/01-architecture.md:428` **A11** — `dockmodel` owns headless UI state
  (view catalog + layout + **active-tool** selection); it is
  ImGui/GL/SDL/**libarbc-free**. Relevant as the *rejected* home (see Decisions):
  a paint color is an arbc value, so it cannot live beside `ToolSelection`.
- `docs/01-architecture.md:308-346` **§8** the levelization DAG; `:348-375`
  **§9** the per-leaf DoD.

**Source seams:**

- Session-state home + precedent fields: `commands::AppState`
  (`src/commands/ace/commands/app_state.hpp:92`); the pattern to mirror —
  `selection()` (`:138-139`), `entered_composition()` (`:149-150`),
  `hovered_object()`/`set_hovered()` (`:162-163`) — each *"PROJECT-LEVEL
  transient UI-thread-only state … never persisted, never journaled (D15), never
  a transaction, moves cleanly with the defaulted move-construction"*
  (`:152-163`). Impl `src/commands/app_state.cpp`.
- Library color type + primitives (vendored libarbc, `<arbc/media/pixel_traits.hpp>`):
  `arbc::WorkingPixel = std::array<float,4>` (premultiplied linear-light RGBA
  float, `pixel_traits.hpp:17`); `arbc::srgb8_to_linear(uint8)` (`:43`),
  `arbc::linear_to_srgb8(float)` (`:48`), `arbc::premultiply`/`unpremultiply`
  (`:25,:30`), `unorm8_decode`.
- Existing editor use of exactly this conversion: filled-background composite
  `src/render/render.cpp:64-71` (`srgb8_to_linear` + `unorm8_decode` + `premultiply`);
  contact-sheet blend `src/commands/contact_sheet.cpp:83-85`
  (`srgb8_to_linear` / `linear_to_srgb8`) — **`commands` already does editor-side
  color conversion**, so it is the established home for the pair.
- Brush read site + placeholder: `src/app/canvas_view.cpp:601`
  (`const arbc::WorkingPixel color = brush_color_;`, captured by value into the
  writer closure `:606-609`); field `src/app/ace/app/canvas_view.hpp:452`;
  `state_` is `commands::AppState& state_` held by `CanvasView`.
- Eyedropper arm (inert): `src/app/canvas_view.cpp:623-629`; declared
  `src/app/ace/app/canvas_view.hpp:417`; routed
  `src/app/ace/app/tool_dispatch.hpp:16,31-32`; the active viewport camera the
  arm receives is `Presenter::camera` (fed to `interact::brush_footprint`,
  `canvas_view.cpp:593-594`).
- Render-offline sampler seam: `arbc::render_offline` as driven from L1
  `commands` today (`src/commands/export.cpp`, `src/commands/ace/commands/export.hpp`).
- View body registration: `views::register_view_body` / `views::ViewBody` /
  `views::draw_view` (`src/views/ace/views/views.hpp:117,111,123`); shell binding
  `src/app/shell.cpp:358-363` (teardown `:512`).
- Levelization lint: `scripts/check_levels.py` — `views` already depends on
  `commands` (`:34`); `commands` is in the `arbc` external allow-list (`:49-50`);
  `dockmodel` is **not** (`:49-50`) and depends only on `{base, platform}`.

## Constraints / requirements

1. **The canonical active color lives on `commands::AppState`** (D-color-1), as a
   project-level transient session field with the *exact* lifecycle of
   `selection_`/`hovered_object_`/`entered_composition_`: UI-thread-only, **never
   persisted, never journaled, never a transaction**, moving with the defaulted
   move-ctor. Not on `dockmodel` (fails `check_levels`), not on `interact` (a
   pure-function component), not a private `CanvasView` field (unreachable by
   other paint consumers).
2. **Stored as sRGB 8-bit straight-alpha; premul-linear is derived** (D-color-2).
   The stored value is the user-space truth (D10: hex/0–255 picker → 8-bit is the
   native v1 precision). `AppState::active_working_color()` returns the derived
   `arbc::WorkingPixel` via the `commands` conversion pair, which wraps
   `arbc::srgb8_to_linear` + `unorm8_decode` + `arbc::premultiply` — **no
   hand-rolled transfer function** (reuse the `render.cpp:64-71` primitives).
   Premultiplied/linear values are never surfaced to the user (D10 `:279`).
3. **The default is opaque black** (`{0,0,0,255}` straight → `{0,0,0,1}`
   working), byte-identical to the brush's current placeholder, so boot behavior
   is unchanged until the user picks a color.
4. **The picker is `ImGui::ColorPicker4`** (D-color-3) with the D10 feature set
   (HSV area + hue, hex, 0–255 RGB, straight-alpha opacity via
   `ImGuiColorEditFlags_AlphaBar`/`DisplayHex`/`DisplayRGB`), drawn in an **L4
   `app` body** registered through `views::register_view_body(ViewType::Color, …)`
   — never a bespoke picker widget, never ImGui code below L3. Float↔8-bit
   marshalling happens at the widget boundary in the L4 body.
5. **The eyedropper samples through a render, not a buffer lookup** (D-color-4,
   D10 `:287-290`). The default variant samples the **composited result** through
   the active viewport camera (`Presenter::camera`) at the click point via a 1×1
   `arbc::render_offline`, decodes premul-linear → sRGB (`unpremultiply` +
   `linear_to_srgb8`), and calls `set_active_color`. The composited sampler is
   **L1 `commands`** (`sample_composited_color`, byte-exact, headless-testable);
   the L4 arm builds the 1×1 viewport from `Presenter::camera` + the click point.
   The **active-cell own-straight-color modifier** (D10) is **deferred** — see
   Acceptance criteria.
6. **The brush rewire is the only change to `editor.paint.brush`'s code.**
   `canvas_view.cpp:601` becomes `state_.active_working_color()`; the
   `brush_color_` field (`canvas_view.hpp:452`) is removed. No change to
   `scene::brush_dab`, the coalesce loop, or the stroke key. The color still
   crosses to the writer thread **by value** (the closure captures a copied
   `WorkingPixel`, `canvas_view.cpp:606`), so no shared mutable color state
   crosses threads.
7. **Compositing stays linear; no blend control** (D10). This leaf adds a color
   *value*, not a blend mode; the picker's straight-alpha opacity is the alpha of
   the paint color and is distinct from the inspector's layer opacity
   (`editor.panels.inspector` D-inspector-3).
8. **Levelization (§8) holds with no new edge.** New state + conversion + sampler
   live in `commands` (already arbc-aware and already a color-conversion site);
   the picker body + eyedropper arm live in L4 `app`; the panel reaches the L1
   state through the L3 `views→commands` edge that already exists (`views` panels
   hold an `AppState&`). The L1 core gains **no** ImGui/GL/SDL include and
   `dockmodel` is untouched. `scripts/check_levels.py` is not edited.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella.

- **Levelization (`check_levels` clean) — the primary structural assertion.**
  No new component, no `scripts/check_levels.py` edit, `dockmodel` untouched.
  `src/commands/` gains the active-color field/accessors, the sRGB↔working
  conversion pair + sRGB color type, and `sample_composited_color` — all
  including only libarbc + existing commands headers; `src/app/` (the L4
  `ColorPanel` body + the filled `dispatch_eyedropper`) is the only ImGui/render
  facing code; `src/app/canvas_view.cpp` loses the `brush_color_` read. Asserted
  by inspection and by the lint.
- **L1 logic — Catch2 unit** (`tests/color_test.cpp`, new file appended to the
  `ace_tests` source list in the root `CMakeLists.txt`; naming follows
  `tests/selection_test.cpp`, i.e. `TEST_CASE("color: …")`):
  - **Conversion round-trip (Constraint 2):** `srgb_to_working` over opaque
    black, opaque white, mid-gray, a saturated primary, and a semi-transparent
    color equals the hand-composed `srgb8_to_linear`/`unorm8_decode`/`premultiply`
    result **byte-exactly**; the result is premultiplied (each rgb ≤ a).
    `working_to_srgb` inverts it to the original 8-bit triple for every
    representable value, and the pair round-trips a spread of 8-bit colors
    exactly.
  - **AppState active-color field (Constraints 1, 3):** default `active_color()`
    is `{0,0,0,255}` and `active_working_color()` is `{0,0,0,1}` (brush-placeholder
    continuity); `set_active_color` round-trips; `set_active_color` creates **no
    journal entry** (`journal().cursor()` unchanged — it is not a transaction);
    the active color **survives a defaulted move** of `AppState`.
  - **Composited sampler (Constraint 5) — render-offline byte-exact:** over a
    known small document (a solid cell over transparent), `sample_composited_color`
    at an interior point returns the exact composited `WorkingPixel`, and at an
    exterior point returns transparent — a byte-exact `arbc::render_offline`
    assertion on the sampled value (the golden-in-a-value form; see next).
- **Rendered output — golden.** The composited-sampler case above is the
  `render_offline` byte-exact check for this leaf (a single-pixel composite is
  pinned as an exact value rather than an image file, because the observable is
  one `WorkingPixel`, not a raster). The picker panel itself composes ImGui
  chrome over `AppState`, not a libarbc `Document` composition, so there is no
  panel image to pin (the `view_registry`/`inspector` precedent); a non-byte-exact
  ColorPicker4 screenshot baseline is captured in the e2e for signal only.
- **UI e2e — ImGui Test Engine** (`tests/color_e2e_test.cpp`, new file appended
  to the `ace_shell_test` list in the root `CMakeLists.txt`, offscreen
  software-GL; modelled on `tests/inspector_e2e_test.cpp`, reusing the
  boot-the-real-shell + `register_view_body(ViewType::Color, …)` rig and driving
  widgets by ref path under the Color window):
  - **Picker → state:** opening the Color view renders ColorPicker4; setting the
    hex / 0–255 RGB input drives `AppState::active_color()` to the entered sRGB
    value.
  - **Picker → brush (the boundary proof):** after picking a non-black color,
    selecting Brush and painting a stroke deposits dabs whose written
    `WorkingPixel` equals `srgb_to_working(active_color())` — asserted through
    the scene/raster, proving the sRGB→premul-linear wire end to end.
  - **Eyedropper → state (Constraint 5):** selecting Eyedropper and clicking a
    painted cell sets `active_color()` to the sampled pixel's sRGB (round-tripped
    from the composited premul-linear); a subsequent brush stroke paints with it.
  - **Boot continuity (Constraint 3):** with no picker interaction, a brush
    stroke paints opaque black (unchanged from the pre-color-panel behavior).
- **Threading (ASan/TSan).** One case appended to `tests/canvas_host_test.cpp`
  (over the inline `WorkerPoolConfig{}` pool per the
  `arbc-nested-render-worker-detach-race` memory / e2e rule): the UI thread runs
  `set_active_color` + `active_working_color()` feeding
  `apply_edit(brush_dab)` concurrent with a live render walk, TSan-clean — the
  `WorkingPixel` crosses to the writer as a copied value, so no shared mutable
  color state crosses threads. No new lane, no new suppression.
- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed
  lines; clang-format + build clean. Tests ship with the task.
- **Deferred WBS work.** One follow-up, named crisply for mechanical registration
  (closer wires it into the milestone that carries `editor.panels.color`):
  - **`editor.panels.color_eyedrop_cell`** (effort **1d**, `depends
    editor.panels.color`) — the D10 eyedropper **modifier** that samples the
    **active cell's own straight color** (not the composite): render only the
    selected cell in isolation through the active camera at the sample point,
    `unpremultiply` → `linear_to_srgb8` → `set_active_color`, bound to the
    eyedropper modifier (e.g. Alt-click). Separable because it needs isolated
    single-cell rendering (siblings hidden) the composited path does not; the base
    composited eyedropper this leaf ships is the common case. Note cites this
    refinement. *(Deferred to `editor.panels.color_eyedrop_cell`; closer registers
    in WBS.)*

## Decisions

- **D-color-1 — The canonical active paint color lives on `commands::AppState`,
  as project-level transient session state.**
  A stored sRGB value + `active_color()`/`set_active_color()` + a derived
  `active_working_color()`, mirroring `selection_`/`hovered_object_`/
  `entered_composition_` (`app_state.hpp:138-163`) — UI-thread-only, never
  journaled, never serialized, moving with the defaulted move-ctor.
  *Rationale:* `AppState` is the one owned project session (A7) and the
  established holder of exactly this shape of cross-panel transient state; Color
  is a project-level singleton view (D19), so a project-level home is right. Every
  paint consumer (`editor.paint.brush` now, `paint_res`/`retouch_stack` later)
  and the picker already hold an `AppState&`, so all read/write one seam with **no
  new DAG edge**. `commands` is arbc-aware (`check_levels.py:49-50`), so it can
  host the `WorkingPixel` converter.
  *Alternative rejected — `dockmodel` beside `ToolSelection`* (the
  brush.md Open-Question-1 candidate): `dockmodel` is **not** in the arbc
  external allow-list and depends only on `{base, platform}`
  (`check_levels.py:49-50`), so it structurally **cannot** name `arbc::WorkingPixel`
  or perform the conversion — modelling the color like the tool rail would fail
  `check_levels`. A tool *mode* is UI-agnostic; a paint *color* is an arbc value.
  *Alternative rejected — `interact`:* it holds **zero** mutable state today
  (pure functions + value structs); introducing session state there breaks its
  character for no benefit `AppState` doesn't already provide.
  *Alternative rejected — keep it the private `CanvasView::brush_color_` field:*
  unreachable by other paint consumers without reaching into a canvas's privates,
  which is exactly the coupling D-brush-6 parked.
  **No doc delta required:** this follows the settled precedent that AppState
  session-field additions are refinement-local decisions — `hovered_object_`
  (D-hatch_swatch-1) and `entered_composition_` (D-layers-3) were both added this
  way, without an A-row. It **resolves** the parked structural question rather than
  amending the constitution.
- **D-color-2 — Stored as sRGB 8-bit straight-alpha; premul-linear is derived at
  the boundary.**
  `AppState` stores a small `commands` sRGB color value (8-bit straight-alpha
  RGBA); `active_working_color()` derives the `arbc::WorkingPixel` through a
  `commands` conversion pair wrapping `arbc::srgb8_to_linear` + `unorm8_decode` +
  `arbc::premultiply`.
  *Rationale:* D10 makes sRGB straight-alpha the user-space truth and forbids
  ever showing premultiplied/linear values; v1 is sRGB-gamut with a hex/0–255
  picker, so 8-bit is the native precision, not a lossy compromise. The
  conversion reuses the exact primitives the editor already calls
  (`render.cpp:64-71`, `contact_sheet.cpp:83-85`) — no new EOTF code, byte-exact
  against the library.
  *Alternative rejected — store the premul-linear `WorkingPixel` canonically:*
  would force a reverse transform for every picker display and round-trip the
  user's color through premultiplication, which D10 (`:279`) forbids surfacing;
  float-sRGB storage (for a smooth wide-gamut picker) rides the D10 wide-gamut
  future, not v1. **No doc delta required** (realizes D10).
- **D-color-3 — The picker is `ImGui::ColorPicker4` in an L4 body via the L3
  views seam.**
  *Rationale:* D10's picker — HSV area + hue, `#RRGGBB` hex, 0–255 RGB,
  straight-alpha opacity — is precisely ColorPicker4's feature set, so a bespoke
  widget would reimplement it. The L4-body-registered-through-`views` pattern is
  the shipped inspector precedent (`shell.cpp:358-363`); ImGui already vendored,
  so **no new external dependency**. **No doc delta required.**
- **D-color-4 — The eyedropper samples the composited result via a 1×1
  `render_offline` through the active camera; the active-cell modifier is
  deferred.**
  The default variant renders one pixel through `Presenter::camera` at the click
  point (L1 `commands::sample_composited_color` over `arbc::render_offline`),
  decodes premul-linear → sRGB, and sets the active color.
  *Rationale:* D10 (`:287-290`) is explicit — *"the color here is a render
  through a camera, not a lookup"* — so a render, not a GL framebuffer readback;
  `render_offline` is byte-exact and already driven from L1 `commands`
  (`export.cpp`), giving a headless golden. The **active-cell own-straight-color
  modifier** needs isolated single-cell rendering (siblings hidden) the composited
  path does not, so it is separable, real, agent-implementable work → deferred to
  `editor.panels.color_eyedrop_cell` (a named WBS leaf, not an audit task).
  *Alternative rejected — read back the composited GL framebuffer pixel:* it is
  display-space and simple, but GL-coupled (L2/L4, not L1-goldenable) and D10
  explicitly rules out the lookup. **No doc delta required.**
- **D-color-5 — The `editor.paint.brush` dependency is correct and retained; v1
  is a single foreground color.**
  This leaf rewires the brush's placeholder read to `active_working_color()` and
  the e2e verifies the paint path, so `editor.panels.color` genuinely depends on
  `editor.paint.brush` — the parking-lot dependency-edge question
  (`parking-lot.md:296`) resolves to **keep the edge**. v1 ships a **single**
  active (foreground) color; a foreground/background pair + swap has **no v1
  consumer** — the modal set is the closed `{Select, Brush, Eyedropper, Pan}`
  (`tool_rail.hpp:18`), with no eraser/fill that reads a background — so it is not
  materialized (YAGNI; the simpler abstraction with the one call site today).
  *Rationale:* speculative fg/bg state with no reader would be untested surface;
  adding a background slot is a small, edge-free extension when a consumer
  arrives. **No doc delta required.**

## Open questions

(none — all decided.) One item is surfaced to the parking lot in the return
summary rather than encoded as a WBS leaf, because it is a human/design call with
no current consumer to test against: whether/when the active color grows a
**background slot + foreground↔background swap** (it materializes when an eraser
or fill tool that reads a background color is scheduled). The active-cell
eyedropper **modifier** is *not* an open question — it is a concrete, scheduled
WBS leaf (`editor.panels.color_eyedrop_cell`).

## Status

**Done** — 2026-07-30.

- Added `commands::AppState` active-color field (sRGB 8-bit straight-alpha) with `active_color()`/`set_active_color()`/`active_working_color()` accessors and sRGB↔premul-linear conversion pair (`src/commands/ace/commands/app_state.hpp`, `src/commands/app_state.cpp`).
- New `commands::SrgbColor` type + `srgb_to_working`/`working_to_srgb` conversion pair and `sample_composited_color` headless eyedropper sampler (`src/commands/ace/commands/color.hpp`, `src/commands/color.cpp`).
- New L4 `ColorPanel` body registered on `ViewType::Color` via `views::register_view_body` (`src/app/ace/app/color_panel.hpp`, `src/app/color_panel.cpp`), wired in `src/app/shell.cpp`.
- Filled `CanvasView::dispatch_eyedropper` arm — 1×1 `arbc::render_offline` through active camera → sRGB decode → `set_active_color` (`src/app/canvas_view.cpp`).
- Brush rewired: `brush_color_` placeholder removed from `src/app/ace/app/canvas_view.hpp`; read site updated to `state_.active_working_color()` (`src/app/canvas_view.cpp`).
- Catch2 unit `tests/color_test.cpp`: conversion byte-exact/round-trip; `AppState` default/round-trip/no-journal/survives-move; `sample_composited_color` render-offline byte-exact golden.
- ImGui Test Engine e2e `tests/color_e2e_test.cpp`: boot-continuity, picker→state, picker→brush boundary, eyedropper→state (4 phases).
- TSan case appended to `tests/canvas_host_test.cpp`; both test files added to `CMakeLists.txt`.
- Deferred follow-up registered in WBS: `editor.panels.color_eyedrop_cell` (active-cell own-straight-color eyedropper modifier, effort 1d).
