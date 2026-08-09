# Parking lot — human/legal judgment items (not WBS tasks)

Items surfaced by refinements that a WBS implementer cannot decide and that do
**not** gate any leaf. Reviewed by a human; not scheduled by the orchestrator.

Split on one axis, because the two halves want different things from a reviewer
(added at the 2026-07-28 triage — the file used to mix them, so every pass
re-read the whole list as open):

- **Decidable now** — every input exists; the item is waiting on a human to
  choose, and re-reading it next cycle will not produce new information.
- **Waiting on evidence** — the call is blocked on something that does not exist
  yet: real use, profiling data, or a leaf that has not shipped. Each names its
  own trigger. Do not re-litigate these until the trigger fires.

**A trigger that names an upstream fix must also name the issue that asks for
it** (added at the 2026-08-07 triage). That triage found that *none* of the six
"Human action: file an upstream issue" items had ever been filed — the highest
issue on `ruoso/arbitrarycomposer` was still `#27`, from the pre-v0.4.0 batch, so
every library-gated item was waiting on a release that nobody had requested. The
v0.4.0 triage collapsed 22 items to 6 precisely *because* eleven issues had been
filed and closed; an unfiled item is not waiting on evidence, it is stalled. So:
an item whose trigger is "a new libarbc release that…" is **not** correctly
parked until it carries an issue number. File first, park second.

**Strengthened at the 2026-08-08 triage:** this applies to **any specific feature
missing from the library**, not only to items whose trigger already names a
release, and *not only when something here is blocked on it*. "Nothing is waiting
on this" is a reason to say so in the issue and let the maintainer prioritise it —
never a reason to leave the library unaware. Two entries had been reframed into
editor-side questions and so escaped the rule: the per-layer blend facet (held back
because D10 forbids the *editor* from exposing it — but whether the *library* should
model blend is an independent question) and `org.arbc.camera` insertability (read as
a D7/A14 allowlist call — but the reason no exception is possible is that `Registry`
cannot express "registered, not host-insertable"). Both are now filed. When an entry
says "the editor cannot fix this from here", that is the tell: write the issue.
Every library-gated item in this file now carries an issue number.

**A triage must also look for what is missing, not only re-check what is here**
(added at the 2026-08-07 triage). Walking the 26 existing items validated all of
them and found one fired trigger. Walking the *refinements* instead — every one
landed since the previous triage, grepped for text routing an item to this file —
found **three items that refinements explicitly said belonged here and that no
closer ever added**. A
refinement's return summary is not a delivery mechanism; nothing enforces that the
closer transcribes it. So each pass should re-run that grep over refinements added
since the last triage, and treat "the refinement says it parked this" as a claim
to verify rather than a fact.

**Verify the claim, not the reasoning** (added at the 2026-08-07 triage, which
learned it the hard way). That pass promoted one recovered item to *Decidable now*
because its stated trigger — "whoever builds the export path" — had demonstrably
fired. The trigger had fired and the item was still latent: the exposure it
described needs a deferring asset source, and the shipped one answers inline. A
fired trigger licenses a **look**, not a conclusion. Before promoting anything
here, read the code path that would actually be exercised and confirm the failure
is reachable; a chain of correct-sounding inferences is not evidence.

**The 2026-08-08 triage: seven items closed at once, and the rule that produced
them.** libarbc tagged **v0.5.0** on 2026-08-08 closing every one of the seven
issues the 2026-08-07 triage filed (`#28`–`#34`) — so seven library-gated items
left this file in a single pass, one day after being filed. That is the "file
first, park second" rule paying out, and it is worth stating plainly for the next
reader: those items had sat here for **up to eleven days doing nothing** because
nobody had asked upstream for the fix. The asking, not the waiting, was the
bottleneck. Closed: the DamageRouter split (#28), the nested worker-detach race
(#29), the GC's blindness to owned images (#30), the missing resample verb (#31),
live nested resolution (#32), the nested insert schema (#33), and in-place
content-config update (#34). Each became a WBS leaf under `editor.canvas.arbc_v050`
rather than a note here; git history and those leaf notes are the record.

**The 2026-08-08 v0.6.0 triage: four more closed, one promoted, and a claim
corrected twice.** libarbc tagged **v0.6.0** closing all four issues the same day's
earlier pass had filed (`#35`–`#38`). Closed here: the offline-render silence (#35,
answered as a *report* rather than as settling — upstream kept `render_offline`
byte-exact and fixed the silence instead, which is the better answer), registry
insertability (#37, which turned the `org.arbc.camera` item from a D7/A14 allowlist
amendment into one field on the editor's own registration), and the workspace-arena
question (#38). **#36** did not close the blend item so much as halve it — the
library half shipped, so it moved to *Decidable now* with only D10 left to answer.

**#38's answer is worth reading rather than filing away:** the arena replays the
params a content was **captured** with and never re-captures, so a kind that mutates
its own *params* reverts on a mapped reopen. Upstream answered it as documentation
and tests — the rule held by construction and was written down nowhere — and changed
no behaviour. It is benign here only because `CameraContent` keeps its mutable
`{name, resolution}` in versioned **content state** through the `Editable` facet
(`scene/camera.hpp:35,42,55-68`), not in params, which is why the tripwire passed and
why it passed for a reason nobody had stated. Any future editor kind that keeps
mutable state in params must route edits through `update_content_config` (#34) or it
will silently revert.

*Sweep boundary:* the 2026-08-07 pass swept **all 94** refinements, not just those
added since the previous triage — which is how it caught
`nested_composition_binding.md`, a file older than the last triage whose parked
item had been missed by it too. 50 files route something here; every one was
resolved as **represented**, **closed at a prior triage**, or **superseded by a
later design row**. Three of the supersessions are worth naming so they are not
re-opened as apparent gaps: Save-As overwrite-confirm (`save_as.md` D-save_as-4) is
void under **D27**, which refuses any target that exists at all; the ImGui Test
Engine licence question (`app_shell.md`) was answered as **A10**; and the
canonical-floor / autosave suggestion (`workspace_reopen_slab.md` D-slab-3,
`reopen_slab_adopt.md`) lost its motivation when `editor.project.reconstructing_reopen`
made the map path reconstruct content, leaving only the residual already carried
below as the camera mutable-state item. **A future pass need only sweep the delta
since 2026-08-07.**

---

# Decidable now

The 2026-07-28 triage decided the three items that stood here (bundled font,
welcome-window treatment, worker-backed tile dispatch), and the 2026-08-07 triage
decided two more — the **Assets view real-body owner** (subsumed by the shipped
Layers panel; no asset-browser leaf) and the **camera frame gizmo chrome
unification** (declined for v1). Git history is the record of each; the reasoning
that survives is carried in the leaf note and in the architecture rows the
decisions touch, not here.

It was empty on 2026-08-07 (one item was promoted there and **withdrawn the same
day** when the claim behind it did not survive checking). The 2026-08-08 v0.6.0
triage put one item here — per-layer blend, whose upstream half shipped, leaving
only a D10 scope call. **That is what this section is for: the evidence arrived, so
the item stopped waiting and started needing an answer.**

---

## Per-layer blend mode — the library half shipped; D10 is now the only question

**Source:** `tasks/refinements/panels/inspector.md` (panels.inspector, 2026-07-29) — D-inspector-5.

**Upstream:** [`arbc#36`](https://github.com/ruoso/arbitrarycomposer/issues/36) **shipped in v0.6.0.**

*Promoted from Waiting on evidence at the 2026-08-08 v0.6.0 triage.* This item's
trigger had two halves — an upstream facet **and** a human decision to reverse
D10's v1 no-blend constraint. The upstream half is done, so the only thing left is
the decision, which is what *Decidable now* is for.

`LayerRecord` now carries a `BlendMode` (`arbc/media/blend_mode.hpp`), set through
`Model::Transaction::set_blend` and read through `LayerRecord::blend()`. The
vocabulary is the **separable** modes of PDF 1.4 / CSS Compositing Level 1 —
multiply, screen, overlay, darken, lighten, colour-dodge, colour-burn, hard-light,
soft-light, difference, exclusion — taken as a reference rather than invented, so a
document's `multiply` means what every other tool means. It is evaluated in the
composition's working space on unpremultiplied values, rides the **layer** rather
than the kind (no facet, no capability virtual), persists by name and is omitted
when normal, and `BlendMode::Normal` is premultiplied source-over exactly.

**The decision is D10's, and it is narrow.** D10 (`docs/00-design.md:477`) forbids
a v1 blend-space toggle. Note what the shipped design does to that objection: the
mode is evaluated in the composition's own working space, so exposing blend modes
does **not** require exposing a blend *space* toggle. Those were plausibly the same
question when D10 was written and they are not the same question now. So the call
is specifically: **does v1 offer per-layer blend modes in the Inspector's appearance
block?** — not "does v1 open the blend-space can of worms."

If yes, it is a small leaf: the appearance block already ships opacity + visibility,
and `set_cell_opacity` / `set_cell_visible` are the implementation mould. If no, the
editor writes no blend and nothing else changes — **but note the round-trip
obligation either way**, which `editor.canvas.arbc_v060` owns: a document authored
elsewhere may carry a mode, an unknown name is preserved verbatim and rendered
source-over, and the save path must not drop the field. Declining to *author* blend
is not licence to *discard* it.

---

# Waiting on evidence

## Provisional input bindings across three shipped panels (§11 input map)

**Source:** `tasks/refinements/editor.panels/layers.md` (editor.panels.layers,
2026-07-29); `tasks/refinements/editor/overview.md` (editor.panels.overview,
2026-07-30); `tasks/refinements/editor.panels/color_eyedrop_cell.md`
(panels.color_eyedrop_cell, 2026-07-30) — D-eyedrop_cell-4.

**Trigger:** §11 input map settled in `docs/00-design.md` (still listed under
*Not yet designed* → "Full input map", verified 2026-08-07).

*Consolidated at the 2026-08-07 triage* from three separate entries. They shared
one trigger, will be answered in one input-map pass, and re-reading them as three
open items each cycle overstated how much was outstanding. The three sets of
provisional bindings:

- **Layers panel** (`editor.panels.layers`) — single-click select,
  disclosure-triangle expand, double-click enter, crumb-click climb. The
  input-map owner may want expand on a key chord, or a different
  double-click-vs-modifier boundary.
- **Overview panel** (`editor.panels.overview`) — double-click-to-enter,
  zoom-control chord, fit-to-camera click.
- **Eyedropper modifier** (`editor.panels.color_eyedrop_cell`) — **Alt** for the
  active-cell isolated sample, read from `CanvasInput.alt`
  (`src/views/ace/views/views.hpp:69`). Alt is free in the Eyedropper context and
  is the conventional "sample this specific thing" modifier, so it is a
  defensible v1 binding; the chord may be confirmed, remapped, or given a
  dedicated modifier. (D24's deep-zoom navigation bindings carry the same
  provisional status, stated in the design row itself rather than here.)

All three are **cheap to move**, which is why none is a WBS task: in each case
the model is independent of the binding and only one L4 dispatch site is
rebindable — the panel body for Layers, `OverviewPanel` for the overview, and
`in.alt` in `dispatch_eyedropper` (`src/app/canvas_view.cpp`) for the
eyedropper. The scope model (`AppState::entered_composition`), the breadcrumb
derivation, the L1 path and geometry helpers, and the navigator seam all stay put
when the bindings move. Human design call gated on the input map.

---

## `org.arbc.tone` is offered in a still-image editor's insert dialog

**Source:** the 2026-08-08 v0.6.0 triage, while checking whether `insert_offer`
(arbc#37) fully closes the insert-dialog question. It does not.

**Filed upstream:** [`ruoso/arbitrarycomposer#39`](https://github.com/ruoso/arbitrarycomposer/issues/39) (2026-08-08).

**Trigger:** a libarbc release closing **#39** — a kind-modality declaration on the
registration, so a host can filter its insert menu to what its domain can present.

arbc#37 gave a kind a way to say *whether anyone should mint it*; there is still no
way to say *what it produces*. `org.arbc.tone` registers with no `insertability`
argument, so it defaults to `KindInsertability::Host` — which is **correct**, a user
genuinely can mint one, and an audio tool should offer it. It also advertises a good
schema, so this editor's dialog renders it nicely as "Tone — frequency (Hz),
amplitude".

And then `ToneContent::bounds()` returns an empty `Rect{}` (`tone_content.cpp:18`)
and its render is a culled stub painting transparent (`:24-35`). So a user picks
Tone, types 440 Hz, and gets a cell that is invisible, has no extent, and can never
show anything. Arguably worse than the fade/crossfade case #37 fixed: those at least
*refuse*; this one succeeds and produces nothing.

**The editor cannot fix this from here**, which is why it is upstream. The signals
that would answer it — an empty `bounds()`, a non-null `audio()` — live on `Content`,
i.e. on an **instance**, and the question has to be answered when building the menu,
before any instance exists. The only host-side fix is hard-coding "skip
`org.arbc.tone`", which is the per-kind allowlist A16 forbids and the registry seam
exists to prevent — and it would fix only the kinds this editor happens to know.

**Severity: a dead menu entry, not data loss.** `editor.cells.insert_offer` ships
with this limitation and says so; nothing waits on #39.

---

## Own-colour sampling for operator cells

**Source:** `tasks/refinements/editor.panels/color_eyedrop_nested.md`
(panels.color_eyedrop_nested, 2026-07-30) — D-eyedrop_nested-3. The refinement
states in three places that this belongs here; the closer never added it.
Recovered by the 2026-08-07 triage's gap check.

**Trigger:** the still-image editor gains an operator-cell authoring surface.

The Alt-modifier eyedropper's isolation gate returns `std::nullopt` for operator
cells, so an Alt-sample over one falls back gracefully to the composited colour —
a strict superset of prior behaviour, not a regression. Making an operator report
its *own* colour would mean rebuilding the operator plus its input closure into an
anonymous `Document` via the registry codec `deserialize` (`registry.hpp:76-78`),
anchoring it, and calling `render_offline`. That mechanism is understood and ready.

It is not scheduled because there would be **nothing to schedule it against** — no
authorable fixture exists to validate it and nothing to close it cleanly.

*Re-derived at the 2026-08-07 triage, because the refinement's own reason for this
is imprecise and would not survive a check.* It says the editor authors "only
leaves", the library's `fade` / `crossfade` / `tone` being time/audio-domain
operators with no still-image surface. But the editor holds **no kind allowlist**:
`scene::insert_schemas` emits one entry per `arbc::Registry::ids()` entry with no
filter by id, metadata, or "is it visual" (`cell.hpp:36-42,85-88` — the same
property that puts `org.arbc.camera` in the dialog, its own entry below). So
whatever is registered *is* insertable, and `org.arbc.tone` is in fact registered
by `register_builtin_kinds` and does appear in the dialog, with a labelled
frequency (Hz) field.

**That re-derivation was itself wrong on its facts, and the v0.6.0 triage caught
it.** It claimed `register_builtin_kinds` registers **four** kinds. It registers
**six**: `org.arbc.fade` and `org.arbc.crossfade` go in too, via
`FadeContent::kind_id` / `CrossfadeContent::kind_id` constants rather than string
literals — which is exactly why a grep for `org.arbc.` in `builtin_kinds.cpp`
misses them, and why two passes in a row got this wrong by grepping instead of
reading. Both **are** operators, and both have been listed in this editor's insert
dialog all along.

The conclusion still holds, now for the reason that actually obtains rather than the
third guess at it. `arbc::is_operator(content)` is just `!content->inputs().empty()`
(`operator_graph.hpp:84-86`). Of the six: `solid`, `raster` and `tone` are sources
with no inputs, so they are leaves however audio-domain they sound; `org.arbc.nested`
*is* an operator, and the eyedropper gate already carves it out by
`composition_ref().valid()`, routing it to the anchored render (`color.cpp:94-100`);
and `fade`/`crossfade` are operators whose factories **refuse every `ContentConfig`
there is** — an operator's input edges cannot travel one — so no string a user types
can mint them. v0.6.0 marks both `KindInsertability::Internal` and
`editor.cells.insert_offer` stops offering them. So there is still no way for a user
to author the non-nested operator cell this item is about; what changed is that the
reason is *impossible construction*, not *absent registration*.

That is also what makes the trigger real rather than rhetorical: `register_extra_kinds`
is a live hook (`project_open.cpp:136,310`), so a plugin host or an editor-authored
kind with non-empty `inputs()` would surface in the dialog automatically and create
the first consumer overnight.

The genuine question underneath — *should* the editor ever author operator cells —
is a product call, and this item resolves as a side effect of answering it.

---

## Full camera gizmo on overview frames

**Source:** `tasks/refinements/editor.panels/overview_gizmo.md`
(editor.panels.overview_gizmo, 2026-07-30) — D-overview_gizmo-4, which routes it
here explicitly ("it belongs in the parking lot as an aesthetic call beside
D-gizmo-6, never a self-perpetuating 'revisit' WBS leaf"). Never added; recovered
by the 2026-08-07 triage's gap check.

**Trigger:** real use showing users reach for camera recrop from the overview
rather than climbing to the canvas.

`editor.panels.overview_gizmo` ships the full scale/rotate/shear gizmo on
schematic **cell** boxes, reusing the cell-gizmo math. Camera frames stay
**move-only** — they are draggable, but carry no scale/rotate/shear handles
(`overview_panel.cpp:303`, D-overview_gizmo-4/7). The charter scopes the leaf to
"schematic overview boxes", and `editor.cells.gizmo` likewise left the canvas
camera frame chrome as-is (D-gizmo-6). So what is missing is specifically
**aspect-locked recrop and dutch on a camera from the overview**, not camera
manipulation as such.

*Distinguished from the D-gizmo-6 item the 2026-08-07 triage closed.* That one was
a pure chrome **refactor** — redrawing already-shipped camera frame handles over
shared helpers, no behaviour change — and was declined for v1 as churn. This one
adds a **capability** the editor does not have anywhere: aspect-locked recrop and
dutch on a camera, driven from the overview. The two look alike and are not; do
not read the closure of that as having decided this.

If wanted, it is a small reuse of the canvas camera-gizmo path, not new geometry.

---

## One shared render thread vs N render threads for multi-canvas

**Source:** `tasks/refinements/editor/multi_canvas.md` (multi_canvas, 2026-07-18), D-multi_canvas-2.

**Trigger:** observed head-of-line blocking in real multi-canvas use.

`render::CanvasHost` drives all N canvases from one shared render thread. A5 mandates a shared `WorkerPool` but is silent on render-thread count; D-multi_canvas-2 chose one thread as the conservative baseline (single pool drainer, smaller TSan surface, correct for the realistic N of 2–3 canvases). If future profiling shows head-of-line blocking is observable (e.g. one heavy canvas visibly slowing the others), moving to N render threads would require revisiting the borrowed-pool concurrent-submitter contract with libarbc. That is a monitor-and-decide call gated on real profiling data, not implementable work today. No WBS task was created.

---

## "New Shot From View" — focused canvas vs explicit designation

**Source:** `tasks/refinements/cameras/new_shot_from_view.md` (cameras.new_shot_from_view, 2026-07-23) — Open questions.

**Trigger:** multi-canvas layouts in real use.

`editor.cameras.mint_from_focused_canvas` implements the "follow focus with lowest-id fallback" approach: the gateway reads `CanvasView::focused_framing()` and the mint always promotes the canvas the user was most recently working in. The alternative — an explicit "promote this canvas" affordance in the canvas camera picker — would let the user designate a specific canvas regardless of focus. These two shapes have different discoverability tradeoffs: focus tracking is invisible until it matters (multi-canvas layout); explicit designation adds picker chrome that is meaningless in single-canvas use. Whether the right end state is the focus-tracking rail item alone, the explicit picker, or both is a D23/D18 design call for a human once multi-canvas layouts are in real use. The WBS task ships the focus-tracking path; if explicit designation is the preferred answer, `mint_from_focused_canvas` may need to be reconsidered or extended.

---

## Suppress the focused-canvas indicator on a single-canvas dock?

**Source:** `tasks/refinements/canvas/focused_canvas_indicator.md`
(canvas.focused_canvas_indicator, 2026-07-23) — Open questions /
D-focused_canvas_indicator-3.

**Trigger:** the always-on border reading as chrome noise in real single-canvas use.

D-focused_canvas_indicator-3 chose to draw the hairline accent border
unconditionally — even on a single-canvas dock where it conveys no
disambiguating information — because suppressing it would trade a one-pixel
constant for a conditional invariant ("bordered pane = where the verb lands"
would stop being universally true) and suppress the affordance precisely when a
new user first learns what it means. The rationale is on the record in the
refinement. If, in practice, the always-on border reads as chrome noise when only
one canvas is open, the fix is a one-condition check in the draw block plus one
e2e phase; the Catch2 rule matrix is unaffected. This is a product-taste call
that warrants real use before deciding.

---

## Sync-submit latency behind a deep async burst

**Source:** `tasks/refinements/editor/writer_thread.md` (canvas.writer_thread, 2026-07-23) — Open questions #3.

**Trigger:** profiling on the real pool — **after** `editor.canvas.settler_attach_split` lands.

`editor.canvas.writer_thread` ships all-sync for result-carrying verbs (D-3). A streamed gesture burst could build a queue depth that makes a subsequent sync `undo` wait. The coalescing key bounds the *commit* cost, not the queue depth. Whether this is observable in practice is unknown; if it bites, adding a bounded depth or a gesture-drop policy would be the fix. Measure on the real pool with a realistic workload before designing anything — this is a data-gated decision, not implementable work today. No WBS task until profiling data exists.

*Re-checked at the 2026-08-08 triage: the chain now has a path.* One day earlier
this was gated on an upstream release that did not exist; **#28 shipped in
v0.5.0**, so the blocking chain is now ordinary scheduled work —
`editor.canvas.arbc_v050` → `arbc_router_attach_split` → `settler_attach_split`.
This item stays parked until that chain lands, but it is no longer waiting on
anyone outside this repo. The amendment below still governs *why* it must wait:
measuring before the split retires the posted ctor/dtor would characterise a queue
shape that is about to be retired.

*Amended at the v0.4.0 triage (2026-07-28):* do not measure yet — the queue shape
is about to change. `editor.canvas.settler_attach_split` (consuming libarbc
v0.4.0's arbc#25 `attach_settler`/`detach_settler`) removes a **known** contributor
to the depth this item is about: today every canvas add, every remove and every
resize rebuild posts a whole `HostViewport` construction or destruction to the
writer thread via `submit_sync` (`src/render/canvas_renderer.cpp:73,150`,
`src/render/canvas_host.cpp:116`), purely because the settler install lives in the
ctor. After that leaf only the attach/detach pair is posted. Profiling the current
queue would characterise a shape that is being retired, so the measurement this
item asks for belongs after that leaf, not before it.

---

## Export destination re-seed: project/layout identity vs service `instance()` on in-process reopen

**Source:** `tasks/refinements/cameras/export_destination_reseed.md` (cameras.export_destination_reseed, 2026-07-24) — Open questions / D-reseed-1 rejected alternative.

**Trigger:** an in-process project reopen ever shipping.

The Export panel's re-seed keys on `ExportService::instance()` — a monotonic per-service id — which is a faithful proxy for the live project today because the shipped model is 1:1 service-per-process (`project_gateway.hpp:21-33`, `app_state.hpp:284-285`). If the editor ever grows an in-process project reopen (swapping `Document`/`AppState` under a **surviving** `ExportService` instead of the current detached-sibling-`exec` model), `instance()` would not change across the swap, so the panel would retain the previous project's destination. The fix in that future would be to key the re-seed on a project/layout identity rather than the service `instance()` — D-reseed-1's "rejected alternative", which is correct only under that future architecture. This is a design call contingent on an architecture decision no leaf owns today; the WBS records the trigger: if in-process reopen ever ships, revisit `views.cpp:315-317` and `ExportPanel::owner` to key on a stable project identity instead.

*Clarified at the v0.4.0 triage (2026-07-28):* libarbc v0.4.0 does **not** move this
trigger, and the reopen work landing alongside it should not be read as having done
so. `editor.project.reconstructing_reopen` changes how a project's document is
*reconstructed on open*; it does not make the editor reopen a different project
**in process**. Opening another project is still a detached sibling `exec`
(`editor.project.exec_new`), so a surviving `ExportService` across a project swap
remains hypothetical and `instance()` remains a faithful proxy. Unchanged.

---

## add(X)→remove(X)→add(X) collapsing in one drive iteration

**Source:** `tasks/refinements/canvas/pending_removes_order.md` (canvas.pending_removes_order, 2026-07-28) — Open questions / D-pending_removes_order-1 accepted consequence.

**Trigger:** real multi-canvas use where a caller posts a rapid add→remove→add triple that collapses into one `drive_once` iteration.

With remove-pre-empts-add (D-pending_removes_order-1), a UI sequence of `add(X)` → `remove(X)` → `add(X)` whose three calls collapse into one drive iteration cancels the final re-add: `pending_removes` erases the queued add from `pending_adds`, and the second `add(X)` lands in the freshly-cleared `pending_adds` before the next swap, so X does not surface until the following iteration. This follows directly from the fact that `pending_adds`/`pending_removes` are unsequenced vectors — the host cannot reconstruct sub-iteration ordering — and matches the triaged "remove pre-empts" rule. No agent-implementable fix exists that preserves the pre-emption rule without adding per-call ordering metadata (a sequenced queue, not a set of unordered vectors). Surface this only if the rapid triple is observed in practice causing a user-visible missed canvas; if it is, the fix would require a sequenced submission model, which is an A5/D-pending_removes_order-1 amendment, not a narrow bug fix.

---

## A18 snapshot migration for all panels — cross-panel architecture call

**Source:** `tasks/refinements/panels/inspector.md` (panels.inspector, 2026-07-29) — D-inspector-6; implementer return summary parking-lot item (b).

**Trigger:** a human decision that all info panels should read `Document`-structure through A18 published writer-built snapshots rather than through the shipped UI-thread scene read-seam.

`InspectorPanel` reads `scene::cells`/`scene::cameras`/`resolution_health` on the UI thread via the shipped read-seam, exactly as `CameraInspector` does, covered by the TSan case added to `tests/canvas_host_test.cpp`. A18 (`docs/01-architecture.md:433`) mandates `Document`-structure reads go through a writer-built immutable snapshot; the History panel already uses one (`commands::HistoryPublisher` → `journal().history()`). Whether all panels should migrate to a unified snapshot model is a cross-panel architecture-consistency call: the threading owner (editor canvas work) must decide the policy and migrate all panels together. It is not a per-leaf "audit" task — migrating only the inspector would fork the threading model mid-panel-set. D-inspector-6 records the decision and its rationale; this parking-lot entry is the trigger for revisiting it when the architecture owner deems it necessary.

**Human action:** assign to a threading / architecture review once the canvas threading work is complete enough to mandate the migration policy.

*Re-checked at the 2026-08-07 triage:* not yet — the canvas threading work is not
complete. Its three open leaves (`arbc_router_split_pin` →
`arbc_router_attach_split` → `settler_attach_split`) all wait on upstream **#28**,
so the threading owner does not yet have a settled model to mandate a policy from.
Revisit after that chain lands.

---

## Overview visual-polish open items (§5:204-206)

**Source:** `tasks/refinements/editor/overview.md` (editor.panels.overview, 2026-07-30) — Open questions / §5:204-206.

**Trigger:** a design pass after the overview has been used in real compositions.

The overview ships provisional defaults for the three §5:204-206 open polish items: the exact hatch style and semi-opacity level (currently a defensible visual default); the pattern-count threshold before color must carry the load (the `overview_pattern` fallback is parameterized but the threshold is provisional); and the camera visual language (frame outline + label, no affordance chrome beyond the look-through click target). All three are human/design-taste calls that retune without touching the model or geometry. No WBS task — retune the L4 draw constants when a design pass produces a preferred value.

*A fourth item folded in at the 2026-08-07 triage:* the overview **gizmo handle chrome** — the weight and colour of the drag handles on schematic boxes, which `editor.panels.overview_gizmo` ships by reusing the canvas gizmo's existing palette rather than choosing its own. Its refinement routes it here and explicitly groups it with these items ("minor visual polish… not a design question, and does not gate the leaf"). Same trigger, same one-pass fix, same L4 constants — it was never added separately, and it should not be.

---

## Active color foreground/background slot + fg↔bg swap

**Source:** `tasks/refinements/panels/color.md` (editor.panels.color, 2026-07-30) — D-color-5.

**Trigger:** a paint tool that reads a background color is scheduled (e.g. an eraser or fill tool that needs a distinct background value), or a design row in `docs/00-design.md` explicitly materalizes the foreground/background pair.

*Verified not fired at the 2026-08-07 triage:* no eraser and no fill tool appears anywhere in `tasks/00-editor.tji` or `docs/00-design.md`, so no consumer of a background slot exists or is scheduled.

`editor.panels.color` ships a single foreground active color on `commands::AppState`. D-color-5 explicitly defers a foreground/background pair + swap: the v1 modal set is closed (`{Select, Brush, Eyedropper, Pan}`) with no eraser or fill that reads a background, so materializing the pair now is machinery ahead of a requirement. When a consumer exists, adding a background slot is a small, edge-free extension. No WBS task — enter the WBS when a concrete product need exists.

---

## Cross-cell strokes and advanced brush modes (hardness / eraser / blend / pressure)

**Source:** `tasks/refinements/paint/brush.md` (editor.paint.brush, 2026-07-30) — Open questions / D-brush-7.

**Trigger:** a design row in `docs/00-design.md` demanding one of these features, or a concrete consumer that cannot be served by the current single-cell soft round dab.

`editor.paint.brush` ships a single soft `round_dab` that paints only the selection-primary `org.arbc.raster` cell. D-brush-7 explicitly defers cross-cell stroke distribution and brush hardness/eraser/blend-mode/pressure as parking-lot observations: the arbc dab API supports hard/soft and explicit masks (`raster_content.hpp:99-104`) so each is cheap to add when a consumer asks, but minting controls now is machinery ahead of a requirement. No WBS task — enter the WBS when a design row or concrete product need exists.

---

## Anisotropic (elliptical) dab shape under non-uniform placement

**Source:** `tasks/refinements/editor/paint_res.md` (editor.paint.paint_res, 2026-07-31) — D-paint_res-3 / Open questions.

**Trigger:** a design row in `docs/00-design.md` demanding affine-correct (anisotropic) dabs, **or** the ring-vs-mark mismatch observed in real use on a non-uniformly-scaled cell.

The shipped brush paints an isotropic circular `round_dab` (circular in content px). Under a non-uniform placement scale (an edge-dragged cell, D8 edges are 1D) a screen-circular brush ring maps to an ellipse in content px, so the painted mark does not exactly match the on-screen ring. The detail-floor cue is already worst-axis-correct (`placement.max_scale()`, D-paint_res-3), so the *floor verdict* is honest regardless; only the *mark shape* is approximate. This is agent-implementable when wanted — build an affine-correct `CoverageSampler` in L1 `interact` (map each content pixel back through the composed `camera∘placement` to evaluate the screen-space radial falloff) and add a `scene::brush_dab` overload taking that sampler — but no design row demands elliptical marks and the isotropic dab is exact under the common uniform placement. When the trigger fires, this becomes WBS leaf **`editor.paint.anisotropic_dab`** (~1.5d) wired into the `editor.packaging.package` gather.

*Feasibility confirmed at the 2026-08-07 triage* — worth recording, because "agent-implementable when wanted" is the load-bearing claim here and it was previously only asserted. The seam the fix needs already exists at the pin: `RasterContent::paint` has a `coverage` overload taking a **caller-supplied** `CoverageSampler` (`raster_content.hpp:339-340`), and `CoverageSampler` is a plain `std::function<float(int gx, int gy)>` (`:92`) — so an affine-correct sampler is an ordinary lambda the editor can build in L1 `interact`, with no library change and no new kind knowledge. The library's own comment names "an explicit alpha mask" as an intended use. This item can be scheduled as written whenever a design row asks for it.

---

## Raw-RGBA clipboard scope and image encoder choice

**Source:** `tasks/refinements/editor/paste.md` (editor.import.paste, 2026-07-31) — D-paste-4 / Open questions.

**Trigger:** real use showing that raw-RGBA-only clipboards are a common source of paste failures, **and** a decision on which image encoder to vendor (imdec is decode-only).

`editor.import.paste` ships encoded-clipboard-only (prefer `image/png`, then any imdec-decodable mime); a clipboard with only raw pixels (no encoded form) is a graceful no-op. Extending v1 to accept raw RGBA would require vendoring an image encoder — imdec is decode-only, so a new dependency (e.g. `stb_image_write.h`, already adjacent to the existing `stb_image_write.h` vendored for export, or `libpng`) would be needed. The correct encoder choice is a dependency/product call, and the limitation is only observable on applications that place raw RGBA on the clipboard without also placing a PNG form (uncommon in practice). No WBS task until the limitation is confirmed painful in real use and an encoder is chosen.

---
