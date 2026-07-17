# Arbitrary Composer — Editor (GUI) Design

> Working title: **Arbitrary Composer Editor**. A desktop GUI that uses
> `libarbc` (the resolution-independent 2D composition library, v0.1.0) as an
> **image editor** where you place *cells* — units of artwork — into a
> composition at arbitrary resolutions and arbitrary affine placements.
>
> This is a **host application**, not part of `libarbc`: it consumes the library
> through `find_package(arbc CONFIG)` exactly as `examples/host-interactive/`
> does, and imposes nothing back on the library's surface. Status: **design in
> progress.** Sections marked *(open)* are not yet designed.

## 1. What this is, and the one idea that shapes everything

Every mainstream raster editor makes the *document* a fixed pixel grid — W×H at
some DPI — and resolves everything to it. `libarbc` does the opposite: the
composition is an **unbounded coordinate space**, each piece of content carries
its **own native resolution**, and the renderer is resolution-independent with
deep zoom. So the editor cannot be a fixed-canvas raster editor. The closest
existing mental model is an **infinite-canvas tool (Figma / Illustrator)** —
except the cells are real raster content you can paint on.

**The load-bearing consequence:** there is no global resolution. "Resolution" is
a property that lives in two *other* places — on each cell, and on each camera —
never on "the document". Most of the design below is working out what that means
for a person sitting in front of the tool.

## 2. The core model: cells, cameras, resolution independence

Two first-class object families, plus the coordinate space they live in.

### Cells — the content

A **cell** is one placed unit of artwork = a `libarbc` **layer**:

- a `Content` of some **kind** — an imported image (`org.arbc.image`,
  referenced), a painted raster (`org.arbc.raster`), a nested composition
  (`org.arbc.nested`), or a solid / procedural fill (`org.arbc.solid`);
- an **arbitrary affine** placing it in composition space (translate, scale,
  rotate, shear);
- its **own native / working resolution** — the detail it holds.

A 4000×3000 photo and a 200×200 sketch coexist in one composition with no common
pixel grid. "Arbitrary resolution" is served by letting *each cell* pick its own
resolution, not by any single cell being infinitely detailed.

### Cameras — the observers

A **camera** is a first-class placed object that *looks at* the composition. It
is exactly the library's `Viewport`: an **output resolution** (device pixels)
plus a **region→device affine** (the rectangle of composition space it frames).

- Cameras are drawn on the canvas as **frames** you can move and resize (like a
  scene-view camera, not like fixed artboards).
- **The editing viewport is not a separate thing — it *is* a camera, the active
  one.** There is only one primitive (the camera); "the view" is just whichever
  camera is currently active. The family has two *roles*, not two *kinds*:
  - a **viewport camera** — the active, free-navigation one you look through; its
    "sensor" is the **on-screen canvas** (resolution = the canvas pixel size,
    which is why it is the *interactive* one), and its live framing is **transient
    session state**;
  - **shot cameras** (Hero, Thumb) — saved framings with their own **export
    resolution**; scene data that travels and versions with the document.
  "Look through Hero" makes Hero the active camera; "new shot from view" promotes
  the viewport's current framing into a saved shot. Both appear in the same
  cameras list and are manipulated identically (D7).
- **Export = render each camera to a file** at *its* resolution, through
  `render_offline(document, camera, backend)`. Multiple cameras fall out for
  free: a 4K hero and a 256px thumbnail of the same scene, batch export, contact
  sheets.

This reframing (from the user) is why the "what does 100%/zoom even mean?"
problem dissolves: **zoom is the editing camera's scale**, there is no phantom
native grid, and output resolution is a per-camera number.

### Resolution independence, stated precisely

- **Cells** provide detail at their own fixed native resolution.
- **Cameras** sample that detail at their own output resolution over their
  viewport.
- The **composition** has no resolution at all — only a coordinate space.

**Resolution health** is therefore *computable*, not a vibe: for any camera, compare
its pixel density over a cell's region against that cell's native detail
("hero camera samples the photo at 1.4× — slightly soft"). This readout is used
throughout the UI (see §4, brush).

## 3. The canvas

- An **infinite, pannable, deep-zoomable** surface rendering the active camera's
  view via `InteractiveRenderer` + `HostViewport`.
- **Progressive refinement is a feature, not a glitch:** fast pan/zoom shows a
  coarse scale-rung first, then sharpens. The UI must *not* fight this; at most
  it shows a subtle "refining…" state.
- **Zoom is shown as a scale bar** (composition units per screen pixel), never as
  a "%" against a nonexistent native grid.
- Camera frames are overlaid as rectangles; cells are the content beneath.
- Deep-zoom navigation aids *(open)*: fit-to-frame, fit-to-cell,
  zoom-to-selection — plus the overview (§5), which subsumes the minimap — so
  users don't get lost in unbounded space.

## 4. The painting model

The crux of being an *image editor*: when a brush drags across a raster cell,
what grid do the dabs land in? Decomposed (from the user) into two independent
axes:

### Storage: cell-owned, fixed resolution

The dab is rasterized into the **cell's own fixed working grid** (matching
`org.arbc.raster` as it exists — a fixed tiled grid at a chosen resolution). The
camera never owns stored detail. Consequences:

- **A new paint cell needs a working resolution** — a project default (e.g.
  2048²) or one derived from the camera it's created through, always
  visible/editable in the inspector. *(open: exact default policy.)*
- **There is a detail floor at the cell's resolution, with a natural escape
  hatch.** Zoom in until a screen-sized brush maps below one cell pixel and you
  can no longer add real detail (you're painting sub-pixel). The fix is explicit:
  **resample the cell's grid up** ("this layer holds 2048² — raise to 4096² to
  keep detailing"), or drop a fresh higher-res cell.
- **Same brush, two cells, different real detail.** A stroke spanning a 512² and
  an 8000² cell feels identical on screen but lands at each cell's own
  resolution — correct and desirable; the per-cell health readout keeps it
  legible.

### Brush size: camera / screen space, expressed as **% of view**

The brush is a **screen-space tool**: its size is a percentage of the current
camera's view, so it *feels* the same size on screen at any zoom, and zooming in
lets a screen-sized brush touch a smaller, finer region of a cell.

Why not "pixels": there is no canonical pixel grid to count in (display, camera
output, and each cell are all different grids), and "px" smuggles in the
fixed-canvas assumption. `% of view` is self-documenting — the "of view" says
outright that it's measured against the camera — and it is zoom-stable (set 6%,
it stays 6% as you zoom; the artwork coverage changes underneath automatically).

- **Primary control:** a `Size = % of view` value, surfaced as a **live outline
  ring on the canvas** (the ring is the source of truth; the number is a handle).
- **% of the shorter view edge** (usually height), so 100% spans the visible
  short side and any brush ≤100% fits on screen in both axes. Cap at 100% (need
  broader coverage → zoom out).
- **Logarithmic slider** — real brushes live in the ~0.3–15% range — with a
  numeric field for precision.
- **Secondary readout:** translate live into the target cell — `≈ 48 px on
  ‹retouch›`. This is *not* the control; it changes with zoom and per cell, and
  it **doubles as the resolution-health cue** — when it approaches 1, you're at
  the detail floor and should resample the cell up. So "pixels" survives only as
  a description of the mark's real effect, never as the thing you set.
- **Screen-locked by default** (brush constant on screen). A *canvas-locked*
  mode (brush constant in composition units, growing on screen as you zoom — the
  Photoshop model) is a possible fast-follow, not v1. *(open: include the toggle
  or not.)*

## 5. The composition overview (wireframe)

A flat layer **list** shows stacking order and names but nothing about *space* —
and on an infinite, arbitrary-affine, multi-camera canvas there is no single
rendered view of "how everything is laid out" (the canvas is one camera's slice
at one zoom). The **overview** is a schematic, render-free view of the *whole*
composition that fills this gap. It is a permanent, interactive panel and
**subsumes the minimap**.

It contains:

- Every **cell** as a box at its true relative position / size / **rotation**,
  drawn with an **auto-generated fill pattern** unique to that layer (its visual
  identity) plus a **dotted border**.
- Every **camera** as a distinct **frame** (not a hatch-filled content box) with
  a label and a "look through" affordance. The user maintains **multiple
  cameras**, and the overview is where they are seen and managed; because cameras
  are what export, the overview doubles as an **export / shot map** — every crop
  and output framing at a glance, a view no fixed-canvas editor can offer.
- The **live editing viewport** as a highlighted rect (drag it to drive the
  camera).

**Z-order without reading a list — via patterned fills.** The wireframe's
classic weakness (overlapping outlines hide stacking order) is solved by the
fills: a front cell's pattern **occludes** the one behind it in the overlap, so
"which is in front" reads immediately, while the **dotted border stays on top**
so the occluded cell's full extent is still traceable underneath. Semi-opaque
**hatching** (lines with gaps, not solid) keeps the behind pattern faintly
visible — "front dominant, back present-but-behind" in one glance. Reordering in
the list flips the occlusion live.

**The pattern is shared identity across views.** The same pattern swatch appears
beside the layer's row in the list, so a box in the overview and a row in the
list are matched at a glance; hovering either cross-highlights the other. A
rotated cell's hatch tilts with it, so orientation reads for free.

This makes the overview **co-primary** with the list rather than a secondary
minimap: the **list** owns exact total order + hierarchy + naming/visibility/lock
toggles; the **overview** owns space + local z-read + camera crops; both index
one shared selection.

*(open: hatch style and semi-opacity level; whether pattern density is screen- or
content-space; how many auto-distinct patterns before color must carry the load;
whether the overview is an editable layout surface (drag boxes to place) or
navigation-only — leaning editable, for a layout-first workflow; the camera
visual language.)*

## 6. Direct manipulation — cells and cameras

**The unifying shape.** A cell and a camera are the *same* object shape: an
**affine placement** (a rectangle you drag on the canvas) plus a **resolution**
(a pixel count you type). They differ only in **direction**: a cell is a
*source* — its resolution is the detail it *emits* over its placement; a camera
is a *sink* — its resolution is the detail it *samples* over its frame. The
editing view is a live camera; export is a camera reading the cells. So the
whole interaction reduces to one rule: **drag the spatial extent, type the
resolution — for both — and the two are always independent.**

**Selection (one tool).** Click a cell's **body** to select it; click a camera's
**border or label** — a camera's **interior is click-through** to the cells it
frames, so a camera never blocks editing the art inside it. Stacked cells:
topmost first, Cmd/Ctrl-click cycles down ("select behind"). Marquee to
multi-select, Shift-click to add. Selection is **shared** across canvas, list,
and overview (§5). A nested-composition cell can be **expanded** in the list to
peek its children (still editing the parent) or **double-clicked to enter** — an
*isolation* scope where canvas, list, and overview show the child's cells,
everything outside dims, and a **breadcrumb** (Root ▸ … ▸ child) climbs back out.
Select ≠ expand ≠ enter.

**Cell gizmo.** Bounding box with move (drag body; arrow-nudge), **scale** (8
handles — corners **proportional by default**, Shift = free distort; edges = 1D
stretch), **rotate** (outside a corner; Shift snaps 15°), **shear** (modifier +
edge; advanced), a draggable **pivot** (Alt = transform from pivot/center).
Inspector: position, placed **size in composition units**, rotation, (shear) —
*and separately* the cell's **native resolution** with a resample control.

**Placement is not resampling — the load-bearing rule.** Dragging handles
changes the *affine* (how much composition space the cell covers); it **never**
touches stored pixels. Scaling a cell up is non-destructive — it just gets softer
per unit area, and the **health badge** flags it and offers the explicit fix:
**"resample to crisp"** for a painted raster (grow its working grid), or
**"source-limited — no crisper detail exists"** for a *referenced image* (the
file is the floor, nothing to resample to). Resampling is always deliberate and
separate; a handle-drag is never a resample.

**Camera gizmo.** The frame plus move (drag border/label = pan what's framed),
**resize** = change the region it covers (re-crop / field of view),
**aspect-locked to the camera's resolution** so pixels stay square, and **holds
resolution** — a bigger frame captures more scene at the same pixel count (wider,
lower effective PPI); smaller = tighter, higher PPI. **Modifier-gated** dutch
**rotation** (like cell shear — advanced, not a default handle).
Inspector: **output resolution** (W×H + aspect presets) — editing it changes
pixel count and aspect (the frame follows) *independently* of which region the
frame covers; a **"look through"** button snaps the editing view to the camera; a
per-camera **resolution-health** read of the cells it captures.

**Modifiers & snapping (shared).** Shift constrains (aspect / 15° / axis-lock);
Alt from center; Cmd/Ctrl select-behind and bypass-snap; **Space pans the
*view*** (the editing camera) — distinct from dragging a camera *object*. Snap
targets: cell↔cell edges/centers with alignment guides, **cell↔camera frame**
(compose to the crop), **camera↔cell bounds** ("frame this cell exactly"), the
composition grid, aspect presets, rotation angles. A **"frame selection"**
command mints a camera fit to the current selection.

**In the editable overview (§5):** the same gizmos and verbs on the schematic
boxes — ideal for *coarse* arrangement of many cells/cameras and for editing
z-order (bring-forward/send-back, watching the occlusion flip), while the canvas
is for *precise* placement and painting. Both edit the same affines live;
painting stays canvas-only.

## 7. Color

**The boundary principle.** `libarbc` composites in **premultiplied linear-light**
float (doc 07); people think in **sRGB, straight-alpha, hex/HSV**; displays are
sRGB. The editor's job at the color boundary is to make the library's
linear-premultiplied truth **invisible**: the user picks and reads sRGB
everywhere, and the editor decodes to linear-premul on the way *in* to the
library and encodes back to sRGB on the way *out* (display, eyedropper; export is
already the library's codec). Premultiplied and linear values are never shown.

- **Picker:** a conventional sRGB picker — HSV/HSL area + hue, `#RRGGBB` hex,
  0–255 RGB, and a **straight-alpha** opacity slider. Nothing exotic.
- **Compositing stays linear** — the library default (physically correct
  source-over; red-over-green blends right, not the gamma-space way). Not a user
  toggle in v1; a *perceptual* option for specific cases (gradients) is a possible
  future **per-operation** choice, never a global mode.
- **Eyedropper** samples the color **as displayed** (sRGB) through the **active
  camera** — with no fixed pixel buffer, "the color here" is a render through a
  camera, not a lookup. Default: the **composited result** (what's on screen);
  modifier: the **active cell's own** straight color.
- **Scope (v1): sRGB gamut.** The working space is linear-sRGB primaries, so the
  picker is sRGB; **wide-gamut / HDR** pickers and **display-ICC** management ride
  the library's future *configurable working space* (`color.working_space`), not
  v1.

## 8. Import & assets

**Two independent axes classify every pixel that enters a project**, and almost
every import/asset behavior is a consequence of them:

1. **Editable?** — a **painted raster** (`org.arbc.raster`, editable) or a
   **referenced image** (`org.arbc.image`, read-only; you retouch by *stacking* a
   raster over it). The **non-destructive** axis.
2. **Bytes where?** — **owned** (inside the project's `assets/`; the project
   stores, dedups and GCs them) or **borrowed** (an external file pointed at by
   URI; never stored, never GC'd, but breakable if it moves). The **portability**
   axis.

Common corners: an imported photo is **borrowed + read-only**; painting is
**owned + editable**; a pasted bitmap (no source file) is **owned + read-only**
(the project mints it); **Consolidate** moves a reference from borrowed → owned.
**GC touches only owned bytes** — a borrowed file is never the project's to
delete. The list (§5) shows both axes: editable-or-not, and a source-file (with
relink) vs a project-footprint readout. *(This resolves the earlier
"referenced-vs-painted styling" open item.)*

**Import paths.**
- **Drop / Place an image file** → a **borrowed** read-only cell at the drop
  point, sized **native px → composition units 1:1** so pixel dimensions carry
  real relative scale (a 4000px photo genuinely dwarfs a 200px sketch); the user
  rescales freely. *(open: 1:1 vs fit-to-camera on drop.)*
- **Paste / clipboard** (no source file) → **owned** read-only image; the project
  mints the asset, otherwise it behaves like an import.
- **Place another `.arbc`** → a **nested composition** cell (the library's
  external nested reference).

**The non-destructive stack (signature workflow).** Read-only photos can't be
painted; attempting to paint one offers **"add a retouch layer above"** — a new
owned raster over the photo's bounds. Original untouched, corrections owned. One
gesture — this is what makes non-destructiveness *structural* rather than a mode.

**Missing / moved references.** A broken borrow shows a placeholder and a
**relink** ("locate file") / reveal affordance; the rest of the project keeps
working. Only *borrowed* pixels can break.

**Portability — Consolidate.** By default nothing is duplicated (borrows stay
external). **"Consolidate project"** copies borrowed files into `assets/` and
relativizes their URIs, producing a self-contained, movable bundle (the NLE
"collect files" move).

**On disk & GC.** A project is a **directory** with the library's canonical
layout (§9) — the document, `assets/`, and the live `workspace/`. Owned bytes
in `assets/` (painted tiles *and* consolidated/pasted images) are
content-addressed, dedup'd, and **garbage-collected**: the editor is the host that
runs the library's mark-sweep (`gc_project_directory`) on an explicit **"Clean up
/ compact"** and on close, with **all open documents as roots** — safe for the
single-editor case; the cross-process case stays a host policy we do not invite
(parking-lot). GC touches **`assets/` only** — never the workspace or borrowed
files.

**Formats:** whatever the library decodes (PNG/JPG/… via imdec). Still-only (D1),
so a single frame per image.

## 9. Export, history, and files

### Export — a camera *is* the export spec

No separate export settings: a camera already carries a resolution and a framing,
so export is **pick camera(s) → render each at its resolution → file(s)** via
`render_offline`. Select one, several, or "all"; batch writes one file per camera
(named by camera), and a **contact sheet** (all cameras tiled into one image)
falls out for free. An ad-hoc crop is not a mode — **frame a camera** (D7's "frame
selection") and export it. v1 format: **PNG** (working→sRGB8 encode); EXR/TIFF
float formats ride the wider-gamut future (D10). Knobs: transparent-vs-filled
background, an optional **N× scale multiplier** (render this camera above its set
resolution). Heavy renders run async with progress.

### Undo/redo — the scene is transactional, the view is not

Every scene edit (place, move, scale, rotate, reorder, paint, import, delete,
resolution change) is a **library transaction** (doc 14), so undo/redo is the
document's journal, not an editor reimplementation. Continuous gestures
**coalesce into one step** (a whole stroke, a whole drag — not per-dab, not
per-frame). The dividing line matches everything else: **navigating the editing
view (pan/zoom) is transient and NOT undoable; editing a camera *object* is scene
state and IS.** GC is a deliberate maintenance op (confirmed, not undoable);
consolidate is reversible.

### Files & the project directory

**A project is a directory**, not a loose file — a bundle whose canonical layout
*is* the library's expected on-disk shape, so the editor opens a *folder*:

```
MyProject/
├── project.arbc     canonical document (doc 08) — the portable, diffable snapshot
├── assets/          owned bytes: painted tiles + consolidated/pasted images (doc 08)
├── workspace/       live mmap arena + crash-consistent checkpoints (doc 15) — same-machine
└── exports/         rendered camera outputs (PNG …), by default
```

**Nothing here is hidden.** The dot-prefix (`.assets`) only earned its keep when
these sat *loose* in the user's own folder beside a file, needing to stay out of
the way. Inside a dedicated project directory the contents *are* the project's
structure, so they're plainly named. The editor defines this bundle layout and
points the library's asset-dir and workspace paths at `assets/` and `workspace/`.

**Data file vs. its dump.** The library frames the two persistence layers exactly
this way (doc 15:274-278): `workspace/` is the **live data file** — the mmap
arena the document actually mutates, checkpointed and crash-consistent, so **the
project is durable by default** (no "lost work since last save"); `project.arbc`
is the **dump** — the portable, canonical, content-addressed snapshot that is the
library's *interchange and version-control format*. So **Save = re-dump the
canonical `project.arbc`** (+ owned assets) from the live workspace: a publish
step, not a race against a crash.

**Portable core vs. local scratch.** `project.arbc` + `assets/` are the portable
core (JSON + content-addressed bytes); `workspace/` is a **same-machine session
artifact** — native endianness and padding, *no portability promise*
(doc 15:274-275) — **rebuilt from the canonical core** on open if missing or
opened on another machine. So it is excluded from sharing / version control (the
editor writes a `.gitignore` for it), and moving a project between machines
carries `.arbc` + `assets/` and regenerates `workspace/`.

New / Open (a project folder) / Save (re-dump the snapshot) / Save As (copy the
directory); recent projects; a dirty indicator reflects workspace-vs-snapshot
drift; exports default into `exports/` but any destination is fine.

## 10. Workspace & panels — a uniform dockspace

Fixed rails don't scale: inspector + layers + overview + color + history all
compete for one edge. So the shell is a **dockspace** — a **recursive tree of
split containers**, each a **tab-group of views**, with **any view in any
container**. **Fully uniform** (Blender / Dear-ImGui style, going further than VS
Code's privileged central editor): there is no special "editor area," and **no
guardrail** forcing a canvas to stay open — a specialized context (a dedicated
kind/layer editor, an operator-graph editor) may legitimately have no canvas at
all, so requiring one would be the special-case, not the safety.

- **Views are content, decoupled from place.** Canvas, Layers, Inspector,
  Overview, Cameras, Color, History, Assets, Export — each draggable into any
  container, stacked as a tab, or split off into a new region. Drag a tab → drop
  zones (center = add-as-tab, edge = split); splitters resize; the tree splits
  H/V recursively, the same model all the way down.
- **The canvas is a view too — the multi-canvas payoff.** Because a canvas view
  is "look at the composition through a camera" (D2) and the **viewport is itself
  a camera**, you can open **two canvas views through different cameras side by
  side**: paint through the free **Viewport** while another **looks through Hero**
  and shows its exact export framing live; a detail-zoom beside a full-frame; A/B
  shots.
- **Panels belong to the *project*, not a canvas.** Selection and the shared
  views (Inspector / Layers / Overview) reflect the **project** — canvases are
  *only* cameras, so N of them share one project-level selection and inspection
  (D19). The rail-as-home-base and workspaces are likewise per-project.
- **One process = one project.** Opening a different project is a **new `exec`**
  of the editor — a fully independent OS process with its own `Document`,
  workspace, threads, and window. No multi-project-in-one-window, no in-process
  project switching; the editor is single-project for its whole lifetime, which
  makes the GC root-set trivially "this one document" (D19). *(WASM analog: a
  project is a tab / instance.)*
- **The fixed rail is home base.** The thin left **tool rail** (modal tools —
  always needed) and the top/status bars are chrome, not content. Because the
  dockspace is uniform and *anything* can be closed (including every canvas), the
  rail is also the **view launcher** — where you open or reopen any view. Nothing
  can be lost: close it all, the rail brings it back.
- **Saved workspaces.** Named layout presets — "Paint", "Compose", "Review" —
  one click, like VS Code / Blender.
- **Layout is local UI state**, never scene data: it lives in the machine-local
  `workspace/` (or global prefs), not in the portable `project.arbc`
  (data-file-vs-dump, §9).
- **Deferred:** floating / tear-off windows (past v1).

## Decisions log

| # | Area | Decision |
|---|---|---|
| D1 | Scope | Still compositor only — no timeline, no audio (the library supports both; deferred). |
| D2 | Canvas model | Infinite canvas; **cameras** are the only observer primitive (= `Viewport`s). The **editing viewport is itself a camera** — the active one, resolution = the on-screen canvas — not a separate "view". Two *roles*: the **viewport camera** (active, free-nav, transient framing) and **shot cameras** (saved framings, own export resolution). Export = render-through-camera. |
| D3 | Cell kinds | Imported images (referenced) · painted rasters · nested compositions · solids/procedural. |
| D4 | Paint storage | Cell-owned **fixed** resolution; **resample-up** is the detail escape hatch. |
| D5 | Brush size | **`% of view`** (shorter edge, log slider) + on-canvas ring; "px" appears only as a per-cell effect/health readout; **screen-locked**. |
| D6 | Structure views | Keep the layer **list** (order/hierarchy/naming) and add a **wireframe overview** (space/z-read/cameras) as **co-primary**, over one shared selection. Z-order shown by per-layer **patterned fills** (front occludes back) + always-on-top dotted borders; the pattern is a shared list↔overview identity. Multiple cameras are managed in the overview, which doubles as an export/shot map. The overview is **editable** (drag boxes to place), not navigation-only. |
| D7 | Manipulation model | Cells and cameras share **one shape** (affine placement + a resolution number) and **one select tool**, differing only in direction (cell *emits* / camera *samples*). Drag the extent, type the resolution; the two are always independent. Cells grabbed by body, cameras by border/label with click-through interiors. |
| D8 | Cell scale ≠ resample | Handle-drag changes **placement (affine)**, never resolution — non-destructive. Corners proportional-by-default (Shift free), edges 1D. Resampling is a separate explicit act: raster grows its grid; a referenced image is source-limited. |
| D9 | Camera frame ≠ resolution | Frame resize = **re-crop**, aspect-locked to resolution, **holds resolution**; resolution (pixel count + aspect) is edited separately in the inspector. Dutch rotation is **modifier-gated**. **Space pans the active viewport camera** (transient) — distinct from re-framing a saved shot camera (scene edit). |
| D10 | Color boundary | Users work entirely in **sRGB / straight-alpha / hex-HSV**; the editor is the invisible translator to/from the library's premultiplied-linear working space. Compositing stays **linear** (no v1 blend-space toggle). Eyedropper samples the **displayed sRGB through the active camera** (composited result by default, active-cell via modifier). v1 is sRGB-gamut; wide-gamut/HDR + display ICC ride `color.working_space` later. |
| D11 | Two asset axes | Every brought-in pixel is classified by **editable?** (painted raster vs referenced image — non-destructive axis) and **bytes where?** (owned in `assets/` vs borrowed external file — portability axis). Photo = borrowed+read-only; paint = owned+editable; paste = owned+read-only; consolidate = borrowed→owned. The list surfaces both. |
| D12 | Import paths | Drop image file → borrowed read-only cell (native px→comp units 1:1 at drop point); paste/clipboard → owned read-only; place `.arbc` → nested composition. Non-destructive retouch = one-gesture "paint layer above" a read-only photo. |
| D13 | Assets, GC & portability | A **project is a directory** (§9); owned bytes in `assets/` (painted tiles + consolidated/pasted files) content-addressed, dedup'd, GC'd via explicit "Clean up" + on close, roots = all open docs; borrowed files **and `workspace/`** never GC'd. "Consolidate" copies borrows into `assets/` + relativizes URIs. Missing borrow → placeholder + relink. |
| D14 | Export via cameras | No separate export spec — a camera **is** the spec. Export = pick camera(s) → render each at its resolution (`render_offline`) → file(s); batch (one file per camera) + contact-sheet fall out; ad-hoc crop = frame a camera. v1 PNG (sRGB8); knobs: transparent/filled bg, N× scale multiplier; async for heavy renders. |
| D15 | Undo = library transactions | Scene edits are transactions (doc 14); continuous gestures coalesce to one step. The line is **transient vs. scene:** the **viewport camera's** live framing (pan/zoom) is transient session state, NOT undoable (like scroll position); a **saved shot's** framing is scene data and IS. GC is a confirmed op (not undoable); consolidate is reversible. |
| D16 | Project = a directory | A project is a **directory** with the library's canonical layout: `project.arbc` (portable snapshot — the doc-08 interchange/VCS format) + `assets/` (owned bytes) + `workspace/` (live mmap arena + checkpoints, doc 15, same-machine) [+ `exports/`]. **Data-file-vs-dump:** `workspace/` makes the project **crash-durable by default**; **Save = re-dump `project.arbc`**. Portable core = `.arbc` + `assets/`; `workspace/` is machine-local scratch, rebuilt from the core, excluded from sharing/VCS. |
| D17 | Nested scope | A nested-composition cell can be **expanded** (peek children, still editing parent) or **entered** (double-click → isolation scope: canvas/list/overview show the child, outside dims, breadcrumb climbs out). Select ≠ expand ≠ enter. |
| D18 | Uniform dockspace | The shell is a **fully-uniform dockspace** (Blender/ImGui-style — no privileged editor area, **no keep-a-canvas guardrail**; a specialized context may have none): a recursive split-tree of containers, each a tab-group of relocatable **views** (Canvas, Layers, Inspector, Overview, Color, History, …). Any view → any container; drag-to-dock, split, resize. **Canvas is a view** → multiple canvases through different cameras side by side (paint-through-Viewport beside look-through-Hero). The fixed **tool rail** hosts modal tools **and the view launcher** (reopen anything). Saved workspaces. Layout is **local UI state** (`workspace/`/prefs), not `project.arbc`. Floating windows deferred. |
| D19 | Project-scoped state; process-per-project | Selection and the shared panels (Inspector/Layers/Overview) belong to the **project**, not any canvas — canvases are *only* cameras, so N of them share one project-level selection and inspection. **One process = one project**: opening a different project is a new **`exec`** of the editor (its own `Document`, workspace, threads, window) — no multi-project-in-one-window, no in-process switching; single-project for its whole lifetime (GC root-set = this one document). WASM analog: a project is a tab/instance. |
| D20 | Tool rail modal set | The rail's **modal tools** — the persistent pointer modes that determine what a plain canvas drag does — are **Select · Brush · Eyedropper · Pan**. "Select" is the *one* select tool (D7, cells+cameras, differing only in direction); the WBS note's "camera" is **folded into Select** (cameras grabbed by border/label), **not** a separate mode. **Pan** appears as a selectable rail mode for discoverability/touch even though **Space** is its transient shortcut (D9). **Import** is **not** a modal tool — it is drop/paste/place-gesture-driven (D12) and surfaces as a rail **action** (wired by `editor.import.*`), never a mode; likewise ad-hoc crop = frame a camera (D14), not a mode. The active tool is **headless UI state** (A11); wiring it to canvas interaction behavior is downstream (no consumer at rail-build time). |
| D21 | Saved workspace presets | Named layout presets — **Paint · Compose · Review** — are **cross-project** local UI state (they span projects, unlike per-project scratch), so the named-preset store lives in the **per-user prefs store**, resolving D18's "`workspace/` or prefs" toward **prefs** for named presets; the portable `project.arbc` never holds layout. A preset is a `dockmodel::DockLayout` projection persisted as a single **versioned line-oriented text file** (local scratch, no portability promise — §9); applying one **seeds a fresh layout** and the dockspace rebuilds its ImGui tree (`io.IniFilename` stays `nullptr`, D-dockspace-3). The three **immutable built-ins** draw over the D18 view catalog: **Paint** = a Canvas beside a Layers·Inspector·Color tab-stack; **Compose** = a Canvas beside an Overview·Layers stack (the multi-camera/shot map, D6); **Review** = a Canvas beside a History·Export stack. Users may **save** the current layout under a new name and **delete** their own presets; built-ins cannot be overwritten. Writes publish via `platform::FileSystem::atomic_replace` (D16); a missing or corrupt store falls back to the built-ins (rebuild-from-default, §9). Persisting the *last-active* per-project layout into `workspace/` is a separate later concern (owned by `editor.project.*`), not this row. |

## Not yet designed *(open)*

The conceptual interaction layer (D1–D17) is complete. What remains is
**next-phase** — implementation planning and polish, not "what is the tool":

- ~~**Default workspace presets.**~~ *Resolved by **D21**:* the "Paint / Compose /
  Review" arrangements, the per-user prefs storage, and the versioned text format
  are now drawn (implemented by `editor.dock.workspaces`).
- **Full input map.** The complete keyboard/shortcut set and tool palette
  (the pieces are decided; the map isn't written).
- **Platform & framework.** Native toolkit choice (Qt / Dear ImGui / …), how the
  C++ editor binds `libarbc`, threading of the async renderer against the UI.
- **Extensibility.** Whether the editor loads `libarbc` plugins (new kinds) at
  runtime, and how they surface in the "insert cell" affordances.
- **Preferences, accessibility, i18n.**

## How this maps onto `libarbc`

Nothing here needs new library machinery — the editor is a host over existing
primitives:

| Editor concept | Library primitive |
|---|---|
| Camera (editing) | `HostViewport` + `InteractiveRenderer` |
| Camera (export) | `Viewport` + `render_offline(document, viewport, backend)` |
| Cell placement | a `Layer`'s `Affine` in a composition |
| Cell kinds | `org.arbc.{image,raster,nested,solid}` via `Registry` / `register_builtin_kinds` |
| Painting | `org.arbc.raster` editable facet (brush dabs) |
| Non-destructive import | `org.arbc.image` (referenced, never re-stored) |
| Undo/redo | data-model transactions (doc 14) |
| Live working store | in-project `workspace/` — mmap arena + crash-consistent checkpoints (doc 15) |
| Canonical save (Save) | `project.arbc` JSON + `assets/` — the doc-08 interchange / version-control format |
| Rendering backend | `CpuBackend` (a GPU backend later needs no editor change) |

Cameras persist **in the document** (`project.arbc`) as scene objects — they live
"in the scene," so they travel with it and version alongside the cells.
