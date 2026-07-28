# editor.canvas.arbc_v040 — Bump libarbc pin to v0.4.0

## TaskJuggler entry

- **Task:** `editor.canvas.arbc_v040` — `tasks/00-editor.tji:346-351`, inside
  `task canvas "Canvas & rendering"` (`tasks/00-editor.tji:171`), inside `task editor`.
- **Effort:** `1d` · `allocate team`
- **Depends:** `editor.canvas.writer_thread` (`complete 100`, `tasks/00-editor.tji:333-339`).
- **Note (`.tji:350`, abridged):** *"Bump ARBC_GIT_TAG v0.3.0->v0.4.0 (CMakeLists.txt:25) … UNLIKE
  v0.2.0 and v0.3.0 this pin is NOT purely additive — three consumer-visible changes land with it and
  this leaf OWNS absorbing them. (1) ONE RENDER PATH: `arbc::render_frame` is REMOVED and
  `render_offline` renders through the tiled driver … (2) TILE APRON: every tile grows 256² -> 264²
  and paints only its own cell through `Backend::composite_windowed` … (3) `arbc::Backend` gains
  `composite_windowed`/`clear_rect` … the editor implements no `Backend` … so this is inert here.
  EXPECTED TEST CHURN, owned here: re-baseline … goldens … and JUSTIFY each diff against the v0.4.0
  changelog … Also retarget tests/arbc_pin_test.cpp at v0.4.0 … THREE PARKED ITEMS CLEAR WITH THE PIN
  … ONE CONTRACT CHANGE WITH NO EDITOR WORK: #23 states Content::bounds() ANY THREAD … NEW SURFACE
  CONSUMED DOWNSTREAM, each by its own leaf … Design: docs/01-architecture.md A1/A5/A6."*
- **Back-link:** this refinement lands at `tasks/refinements/canvas/arbc_v040.md`. **The closer**
  appends `Refinement: tasks/refinements/canvas/arbc_v040.md` to the `.tji` note and adds
  `complete 100` after `allocate team`. **Do not** hand-edit the `.tji` here.
- **Source of debt:** `tasks/parking-lot.md` triage 2026-07-28 (commit `d0843c5`), which incorporated
  22 parked items to 6 and filed the eleven downstream leaves this pin unblocks.
- **Downstream dependents (already registered):** the whole v0.4.0 consumer fan-out
  `depends !arbc_v040` — `editor.canvas.nested_real_pool` (`:352`), `editor.canvas.magnified_raster_idle`
  (`:358`), `editor.canvas.history_snapshot_adopt` (`:364`), `editor.canvas.settler_attach_split`
  (`:370`), plus cross-area leaves reached through `editor.canvas.arbc_v040` at
  `tasks/00-editor.tji:208,481,487,530,536` (reconstructing reopen, insert schema, one-action-one-entry,
  bundled font, export pinned). This leaf registers **no new** follow-up (D-arbc_v040-7).
- **Milestone:** `m9_editor` (`tasks/99-milestones.tji`), reached through the `editor.canvas`
  container dependency.

## Effort estimate

**One day.** The mechanical bump is again one string; the budget goes into **absorbing the first
non-additive pin this repo has taken** — proving the four goldens that move move for a reason the
changelog names, proving the seven that must not move stay byte-identical, and truing up the
constitution. This is strictly more than `editor.canvas.arbc_v030`'s 0.5d, and the delta is exactly
the golden churn `v0.3.0` did not have.

- **The mechanical bump is ~1 line.** `CMakeLists.txt:25` — `set(ARBC_GIT_TAG "v0.3.0" CACHE STRING
  "libarbc git ref")` → `"v0.4.0"`. It remains the **only** place in built code that names the
  version (README/`.github/` carry none; `scripts/check_levels.py` knows only the `arbc/` include
  prefix; no test asserts a library version; there is no `arbc_version` symbol).
- **The bump is empirically characterized.** A trial configure + build + `ctest` against a v0.4.0
  worktree of `arbitrarycomposer` (`ARBC_SOURCE_DIR`), on this checkout otherwise unmodified, was run
  while refining: **configure OK, build exit 0 with no `ace_*` warning** (no deprecation, no
  `render_frame`/`Backend`/`composite_windowed` diagnostic), `ace_shell_test` **passed** (48.3s), and
  `ace_tests` reported **4 failed assertions out of 59918 — every one a `compare_golden` mismatch**,
  and nothing else. So this leaf is not a discovery exercise; its cost is the four justified
  re-baselines and the doc truth-up, not the change itself.
- **Where the budget goes (1): the four golden re-baselines, each justified.** The four movers are
  `look_through_shot_64x64.rgba8` (`tests/look_through_test.cpp:192`),
  `camera_manip_recrop_64x64.rgba8` (`tests/camera_manip_test.cpp:338`),
  `contact_sheet_3cam.rgba8` (`tests/contact_sheet_test.cpp:1471`) and
  `contact_sheet_latin1.rgba8` (`tests/contact_sheet_test.cpp:1552`). All four are
  **minification / re-crop / thumbnail composites** — exactly the phase where the v0.3.0 untiled
  `render_offline` and the v0.4.0 tiled path disagree (D-arbc_v040-2). The `.tji` note *predicted*
  `export_camera_64x64.png` would re-baseline; empirically it does **not** — a 1:1 camera export
  composites at an integral phase where the paths agree, so it stays byte-identical. That is a
  correction the implementer must carry: re-baseline the four the trial names, not the one the note
  guessed.
- **Where the budget goes (2): the doc truth-up.** `docs/00-design.md:4` and `docs/01-architecture.md`
  A23 (`:385`) make **present-tense** claims about the pinned *v0.3.0*; both truth-up. **A6** — the
  display-path/backend row — gains an amendment recording that the `Backend` seam grew two virtuals
  and that `render_offline` now runs the tiled driver (D-arbc_v040-6). The v0.3.0 mentions in
  A4.1a/A14/A15/A18/A19 are **amendment-provenance** ("amended at the v0.3.0 pin") and stay as
  written — changing them would falsify history, exactly the distinction `arbc_v030` drew.
- **Where the budget does *not* go.** No consumption of the new surface in `src/`. No reconstructing
  reopen, no insert schema, no one-action-one-entry batching, no `Journal::history()` adoption, no
  settler attach/detach split, no pinned-export overload — each is its own already-registered leaf
  (D-arbc_v040-1, D-arbc_v040-7).

New code: one string in `CMakeLists.txt`; four regenerated `.rgba8` goldens; a retargeted
`tests/arbc_pin_test.cpp` (v0.4.0 staleness names + the guarantees the consumer leaves lean on); a
re-anchored comment/assertion in `tests/canvas_host_test.cpp:1406`. Doc deltas: **A6 amended** plus
two version-string corrections (`docs/00-design.md:4`, A23). **No new component, no new DAG edge, no
new external dependency, no new ctest target, no libarbc fork.**

## Inherited dependencies

**Settled (consumed as-is):**

- `editor.canvas.writer_thread` — `tasks/refinements/editor/writer_thread.md` (**Done**). Consumed:
  - **The single-writer funnel is in place.** The editor now writes through one dedicated
    `ace::writer::WriterThread`; the render thread only `step()`s. This pin does **not** touch that
    machinery — it changes the library the funnel drives, not the funnel. The writer-identity
    guarantees `arbc_v030` pinned in `tests/arbc_pin_test.cpp` (unbound-until-first-transaction,
    any-thread journal enable-state) stay true at v0.4.0 and their cases stay green.
  - **`HostViewport` construction is currently posted to the writer thread** for the one
    writer-thread-only `set_external_load_settler` line. v0.4.0 ships the `attach_settler`/`detach_settler`
    split that lets the render thread own the viewport lifetime again — but **consuming** it is
    `editor.canvas.settler_attach_split` (`.tji:370`, `depends !arbc_v040, !pending_removes_order`),
    **not** this leaf. This leaf leaves the posted construction exactly as `writer_thread` left it.
- `editor.canvas.arbc_v030` — `tasks/refinements/canvas/arbc_v030.md` (**Done**, 2026-07-23).
  Consumed as the **structural template** and as three inherited disciplines:
  - **The `CACHE`-without-`FORCE` staleness hazard is unchanged and must be re-guarded**
    (D-arbc_v030-2 → D-arbc_v040-3). `ARBC_GIT_TAG` is still `CACHE` without `FORCE`, `scripts/gate`
    still reconfigures in place, so a stale `build/` still links the old tag under a green gate. The
    guard is again the compiler: `tests/arbc_pin_test.cpp` names v0.4.0-only symbols.
  - **Version-string discipline** (D-arbc_v030-6): a pin bump *owns* the version-conditional claims
    in `docs/`, and it must separate **live** present-tense claims (truth-up) from **historical
    narrative** (leave). `arbc_v030` established both halves; this leaf applies them to the v0.4.0
    delta.
  - **`arbc_v030` inverted golden invariance** (D-arbc_v030-7) because v0.3.0 was additive and no
    golden could move. **This leaf deliberately breaks that inversion** for exactly four goldens,
    because v0.4.0 is the first non-additive pin (D-arbc_v040-2). Every *other* golden keeps the
    invariance obligation.

**Pending (owned here):** nothing. Every predecessor is `complete 100`.

## What this task is

Five deliverables, in dependency order:

1. **Bump the pin** (D-arbc_v040-1). `CMakeLists.txt:25`, `"v0.3.0"` → `"v0.4.0"`. Nothing else in
   built `src/` changes.
2. **Re-baseline exactly the four goldens the render-path unification moves, each justified against
   the v0.4.0 changelog** (D-arbc_v040-2). `look_through_shot_64x64.rgba8`,
   `camera_manip_recrop_64x64.rgba8`, `contact_sheet_3cam.rgba8`, `contact_sheet_latin1.rgba8` are
   regenerated; the remaining seven goldens stay byte-identical and a mismatch in any of them is a
   **failure, not a re-baseline**.
3. **Renew the staleness guard and pin the guarantees the consumer leaves lean on** (D-arbc_v040-3).
   `tests/arbc_pin_test.cpp` retargets at v0.4.0: its five existing cases stay true (in particular
   `Document::open` survives unchanged and is still record-only, so the last case's "no `Content`
   bound" assertions still hold — reconstruction is the *new* `arbc::open_document`, owned by
   `editor.project.reconstructing_reopen`); and it gains compile-time witnesses for the v0.4.0-only
   surface so a stale v0.3.0 tree fails to **build**.
4. **Re-anchor the `#23` any-thread `bounds()` case** (D-arbc_v040-5).
   `tests/canvas_host_test.cpp:1406` — the UI-thread `pick_targets` walk that calls
   `arbc::Content::bounds()` against the live render walk — stops being a *tripwire* for an
   unstated race and becomes an *assertion of a stated guarantee* (`Content::bounds()` is now
   documented ANY THREAD). `scene::CameraContent::bounds()` is constant-empty
   (`src/scene/ace/scene/camera.hpp:46-48`) and every other kind the editor uses is a library
   built-in, so no editor kind needs a change; the re-anchor is a comment/naming correction, not new
   synchronization.
5. **True up the constitution** (D-arbc_v040-6). Amend **A6** to record the `Backend`-seam growth and
   the one-render-path shift; correct the two live version strings (`docs/00-design.md:4`, A23 at
   `docs/01-architecture.md:385`).

Out of scope, by charter — every item below is **already owned** by a registered downstream leaf, and
doing any of them here would consume surface this leaf only pins:

- Reconstructing reopen / `open_document` / `rebind_content` / `recovered_content_state` (#19) —
  `editor.project.reconstructing_reopen` (`.tji:208`).
- `create_content_and_attach` / `remove_contents` one-action-one-entry (#20) —
  `editor.cells.one_action_one_entry` (`.tji:530`).
- `Registry::insert_schema` / `KindInsertSchema` / solid's `r,g,b,a,x,y,w,h` grammar (#21/#22) —
  `editor.cells.insert_schema` (`.tji:481`).
- `Journal::history()` adoption, retiring the host-side mirror (#24) —
  `editor.canvas.history_snapshot_adopt` (`.tji:364`).
- `attach_settler`/`detach_settler` lifetime split (#25) — `editor.canvas.settler_attach_split`
  (`.tji:370`).
- `render_offline(document, pinned, viewport, backend)` batch-coherent overload (#27) —
  `editor.cameras.export_pinned` (`.tji:487`).
- Asserting the deferred-external nested child composites under the real pool (#17) —
  `editor.canvas.nested_real_pool` (`.tji:352`); asserting a magnified raster reaches idle (#18) —
  `editor.canvas.magnified_raster_idle` (`.tji:358`).

## Why it needs to be done

The v0.4.0 pin is the sole gate on **eleven** downstream leaves — the entire consumer fan-out filed
in the 2026-07-28 parking-lot triage — and, unlike the additive v0.2.0/v0.3.0 bumps, it carries three
consumer-visible library changes that need a single owner:

- **A non-additive pin folded into a consumer leaf would make library behaviour indistinguishable
  from editor bugs.** Four goldens move the moment the tag flips. If that motion landed inside, say,
  `editor.cameras.export_pinned`, a reviewer could not tell the render-path re-baseline from a defect
  in the new pinned-export overload. Landing the bump alone — with the four movers justified against
  the changelog and the seven non-movers proven byte-identical — isolates the variable, exactly as
  `arbc_v030`'s D-arbc_v030-1 isolated its bump.
- **The golden re-baseline is a shared prerequisite that must settle once.** The contact-sheet
  goldens re-baseline here for the tile-apron/render-path shift; the bundled-font leaf
  (`editor.cameras`, `.tji:487` neighbourhood) then re-baselines them *again* for caption-shape
  changes. Its note is explicit that it `depends editor.canvas.arbc_v040` precisely *"so the
  tile-apron re-baseline settles FIRST and the captions are re-baselined once instead of twice."* If
  this leaf did not own the apron re-baseline, that ordering guarantee would be false.
- **Three parked defects clear with the pin and are asserted by their own leaves.** #17 (a
  deferred-external nested child now composites under the real `WorkerPool`), #18 (a magnified raster
  renders at the requested scale under `BestEffort`, so the render thread stops burning a core), and
  #26 (removed content is reclaimed once its removal leaves history, so insert/delete stops growing
  memory) all land in v0.4.0. #17 and #18 get assertion leaves; #26 needs no editor change. None of
  them is expressible until the tag moves.
- **One contract change retires a tripwire.** #23 documents `Content::bounds()` as ANY THREAD. The
  editor's UI-thread `pick_targets` read (`tests/canvas_host_test.cpp:1406`) was safe by accident of
  the kind set; at v0.4.0 it is safe by stated contract. That is a test the pin *upgrades*, and the
  upgrade belongs with the pin.

## Inputs / context

**Governing design docs (normative — the constitution):**

- `docs/01-architecture.md` **A1** (`:16-22`, decision-log row `:363`) — *"Native C++20 app links
  `arbc::arbc` directly — no FFI."* The editor holds real `HostViewport`/`Document`/`Backend`
  objects and shares memory with the renderer; a pin bump changes the fetched ref, not the binding.
  Unchanged.
- `docs/01-architecture.md` **A5** (`:195-208`, row `:367`) — *"Multi-canvas = N
  `HostViewport`/`InteractiveRenderer` over one `Document` sharing one `WorkerPool`; no new
  locking."* The #17 real-pool fix and the #23 any-thread `bounds()` contract both live in this
  row's territory; neither changes A5's structure (they make an existing read/composite *correct by
  contract* rather than by accident). Unchanged.
- `docs/01-architecture.md` **A6** (`:210-216`, row `:368`) — *"`CpuBackend` yields CPU tile
  surfaces; the canvas view uploads them as GL textures and composites to the pane. A GPU `Backend`
  later … is behind the `Backend` seam — no editor change."* This is the render-path/tiles/backend
  contract v0.4.0 touches: the `Backend` seam grows two virtuals and `render_offline` now runs the
  tiled driver. **Amended by this leaf** (D-arbc_v040-6). A6's core promise — the editor uses
  `CpuBackend` and needs no change behind the seam — **holds for the pin**, because the editor
  implements no `Backend`.
- `docs/01-architecture.md` **§8 levelization** (`:255-291`, table `:273-287`) and **§9 testing/DoD**
  (`:293-321`, bullets `:312-315`). This leaf adds no component and no edge; `render → libarbc`
  (`:284`; `EXTERNAL_ALLOWED["arbc"]` includes `render`, `scripts/check_levels.py:49`) is
  pre-existing, and `tests/` is not governed by `check_levels`.
- `docs/00-design.md:4` — *"`libarbc` … v0.3.0"*, a present-tense parenthetical pin. **Amended by
  this leaf.**

**libarbc v0.4.0 — what is actually in the tag** (verified against tag `9894b6fb`, release commit
`56ed889`; paths are in the `arbitrarycomposer` repo; `CHANGELOG.md` `## [0.4.0] - 2026-07-28`):

- The release states its own theme: *"The first release that is not purely additive, and the two
  breaks are worth naming up front: the untiled `arbc::render_frame` free function is removed, and
  `arbc::Backend` gains a `composite_windowed` virtual every implementation must supply. Both fall
  out of the same change — there is now ONE render path."*
- **Breaking (1) — one render path.** `render_offline` renders through `render_frame_interactive`,
  the tiled driver the interactive loop and the sequence exporter already ran; `Exactness` is the
  only axis separating them. The retired untiled path *"sized its temp to exactly the content, so a
  minification's outer resampling tap fell on that surface's transparent border and rang the value
  up"* — the precise reason a **minified** still composites differently now. `render_layer` is
  unaffected.
- **Breaking (2) — `Backend` virtuals.** `Backend::composite_windowed(dst, src, src_to_dst, opacity,
  device_clip, src_window)` and `clear_rect` are new virtuals *"a pixel-modelling implementation must
  supply."* `SurfaceRef` gained an optional `origin`.
- **Rendering change — tile apron.** Every tile surface grew `256²` → `264²` device px and paints
  only its own cell through `composite_windowed`; a finite `bounds()` is enforced in the tile's own
  pixels and `compositor.bounded_content_tile_clip`'s device-space round-out clip is retired, so a
  bounded edge *antialiases through the composite's own tap instead of being clipped to whole device
  pixels* and *an opaque fill is opaque across tile boundaries at a fractional composite phase*
  (invisible to any golden that composites at an integral phase).
- **Added surface (each consumed by its own downstream leaf):** `arbc::open_document` /
  `ReopenedDocument::unreconstructed` / `Document::rebind_content` / `recovered_content_state()` /
  `set_content_identity_capture` (#19); `Document::create_content_and_attach` / `remove_contents`
  (#20); `Registry` per-kind `KindInsertSchema` + solid `"r,g,b,a,x,y,w,h"` (#21/#22);
  `Journal::history()` any-thread snapshot (#24); `HostViewport::attach_settler()` /
  `detach_settler()` + `Config::install_settler` (#25); `render_offline(document, pinned, viewport,
  backend)` (#27).
- **Changed — `Content::bounds()` is documented ANY THREAD** (#23): *"No behaviour changed … it was
  safe only because every shipped kind fixes its extent at construction, which is an accident of the
  kind set rather than a contract."*
- **Fixed:** #26 removed content reclaimed once its removal leaves history; #17 a deferred external
  nested child composites under the real `WorkerPool`; #18 kinds render at the requested scale under
  `BestEffort`.
- **Compatibility line:** *"Every 0.3.0 call that neither implements `arbc::Backend` nor names
  `render_frame` compiles and behaves unchanged."* The editor does neither.

**Editor call sites the bump touches (no `src/` edit required at any):**

- `CMakeLists.txt:25` — the pin. `:23-24` `ARBC_GIT_REPOSITORY`, `:26` `ARBC_SOURCE_DIR`
  (co-development override), `:30-37` `FetchContent_Declare`/`MakeAvailable`.
- `src/render/render.cpp:24-30` (`render_document_srgb8`) and `:47-52` (`render_document_srgb8_over`)
  — the editor's only two `arbc::render_offline` calls, the 2-arg `(document, viewport, backend)`
  overload. Both keep compiling; both now route through the tiled driver.
- `src/render/render.cpp:26`, `:50`, `src/render/canvas_renderer.cpp:183`, `tests/binding_test.cpp:9`
  — every construction is `arbc::CpuBackend`. The editor implements **no** `arbc::Backend`, so the
  new virtuals are supplied by the library's own backend; the editor never sees them.
- `tests/arbc_pin_test.cpp` — the pin guard (retargeted, D-arbc_v040-3).
- `tests/canvas_host_test.cpp:1406` — the `#23` any-thread `bounds()` case (re-anchored,
  D-arbc_v040-5).
- The four golden-producing tests: `tests/look_through_test.cpp:192`,
  `tests/camera_manip_test.cpp:338`, `tests/contact_sheet_test.cpp:1471` and `:1552`.

**Harness:**

- `scripts/gate` — `check_levels` → `clang-format` → configure → build → `ctest`, one preset
  (`ACE_GATE_PRESET`, default `dev`). Reconfigures in place; never wipes `build/`; picks a versioned
  compiler (`g++-14` on this box) when `CXX` is unset.
- `.github/workflows/ci.yml` — five build lanes (`gcc-debug` `:30`, `gcc-release` `:31`,
  `clang-debug` `:36`, `clang-asan` `:38`, `gcc-tsan` `:41`) plus a coverage job gating
  `diff-cover --fail-under=90`. **No `actions/cache` step**, so every GitHub lane fetches the tag
  fresh — the stale-tag hazard is purely local (Constraint 2).
- **11 committed goldens** in `tests/goldens/` (`git ls-files`): `camera_manip_recrop_64x64.rgba8`,
  `canvas_nav_zoom_64x64.rgba8`, `canvas_view_64x64.rgba8`, `cells_insert_nested_64x64.rgba8`,
  `contact_sheet_3cam.rgba8`, `contact_sheet_latin1.rgba8`, `export_camera_64x64.png`,
  `export_camera_64x64.rgba8`, `export_filled_bg_64x64.rgba8`, `look_through_shot_64x64.rgba8`,
  `render_probe_64x64.rgba8`.

**Empirical trial (performed while refining, on this checkout, no committed file modified):**
a v0.4.0 worktree of `arbitrarycomposer` was built via `-DARBC_SOURCE_DIR`; `CXX=g++-14`,
Ninja, Debug. Configure OK → build **exit 0, no `ace_*` warning** → `ctest`: `ace_shell_test`
**passed** (48.3s); `ace_tests` **4 failed / 384 passed** cases, **4 failed / 59914 passed**
assertions — all four failures `compare_golden` mismatches on the four named goldens, nothing else.
No `.actual` was left behind (the compare helper dumps mismatches only when configured to).

## Constraints / requirements

1. **The pin plus four justified re-baselines are the whole functional change.** `CMakeLists.txt:25`,
   four regenerated `.rgba8` goldens, the retargeted pin test, and the re-anchored `#23` case. If the
   implementer finds themselves editing `render.cpp`, adding a `Backend` subclass, or consuming any
   `open_document`/`history()`/`attach_settler`/insert-schema surface, the scope boundary has been
   crossed — those belong to the named downstream leaves.
2. **The `CACHE`-without-`FORCE` staleness hazard must be re-closed by a mechanism the gate runs.**
   As at v0.3.0: `set(ARBC_GIT_TAG "v0.3.0" CACHE STRING …)` does not overwrite an existing cache
   entry, and `scripts/gate` reconfigures in place. Adding `FORCE` is **not** acceptable — it would
   break the documented `-DARBC_GIT_TAG=<branch>` and `-DARBC_SOURCE_DIR=` co-development overrides.
   The guard is the compiler (Constraint 8).
3. **Exactly four goldens re-baseline, and each diff is justified against the v0.4.0 changelog.** The
   four are `look_through_shot_64x64`, `camera_manip_recrop_64x64`, `contact_sheet_3cam`,
   `contact_sheet_latin1` — all minification / re-crop / thumbnail composites, all attributable to
   the one-render-path change (*"a minification's outer resampling tap … rang the value up"*) and the
   tile-apron extent-antialiasing change. A golden that moves for a reason the changelog does **not**
   name is a regression, not a re-baseline — including any of the seven below.
4. **The seven non-movers stay byte-identical.** `render_probe_64x64`, `canvas_view_64x64`,
   `cells_insert_nested_64x64`, `canvas_nav_zoom_64x64`, `export_camera_64x64.rgba8`,
   `export_camera_64x64.png`, `export_filled_bg_64x64` passed byte-identical in the trial. They
   composite at integral phase / 1:1 export, where the two render paths agree. Any mismatch here
   blocks the leaf. **In particular, do not re-baseline `export_camera_64x64.png` on the strength of
   the `.tji` note's prediction — the trial disproves it.**
5. **No fuzzy/tolerance compare.** The re-baselined goldens are compared byte-exact against a fresh
   `render_offline` capture, like every other golden. A tolerance would be the justified exception the
   §9 DoD reserves; nothing here justifies one.
6. **The `Backend` seam growth is inert and stays inert.** The editor implements no `arbc::Backend`
   and constructs only `arbc::CpuBackend`. Supplying an editor-side `Backend` double to satisfy the
   new virtuals would be speculative work with no call site; the correct disposition is to **record**
   the seam growth in A6 (D-arbc_v040-6), not to build for it.
7. **`Document::open` stays record-only; reconstruction is the *new* `arbc::open_document`.** The
   pin test's last case (`tests/arbc_pin_test.cpp:269-330`) asserts a reopened workspace binds **no**
   `Content`. That stays true at v0.4.0 — `Document::open` is unchanged. Do not "fix" that case to
   expect reconstruction; that behaviour arrives through `arbc::open_document`, owned by
   `editor.project.reconstructing_reopen`.
8. **The new pin-test names must fail to *compile* against v0.3.0, not merely fail to pass.** A
   runtime version assertion is unavailable (no `arbc_version` symbol; `<NAME>_VERSION` is not
   visible in the parent scope after `FetchContent_MakeAvailable`). Naming v0.4.0-only symbols in
   compiled code (or in an unevaluated `static_assert`, as the existing `#5`-trio witnesses do at
   `:255-267`) is the guard.
9. **Levelization is untouched.** No new component, no new edge, no edit to `scripts/check_levels.py`.
   The pin test and the golden tests are in `ace_tests`, which already links `arbc::arbc`; the L1
   core gains no ImGui/GL/SDL include (it gains nothing at all).
10. **No new lsan suppression.** The trial provoked none; if the bump provokes one it is a real
    finding, not a suppression.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate` green
(`check_levels` · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean).** No component added, no edge added, no edit to
  `scripts/check_levels.py`. The L1 core (`project`/`scene`/`interact`/`commands`) gains nothing.
- **Rendered output — goldens: four justified re-baselines, seven byte-identical.**
  - **Re-baseline (regenerate the `.rgba8`, and record the justification in the commit):**
    `look_through_shot_64x64.rgba8` (`tests/look_through_test.cpp:192`) and
    `camera_manip_recrop_64x64.rgba8` (`tests/camera_manip_test.cpp:338`) — a look-through / re-crop
    shot is a **minified** `render_offline`; the outer resampling tap that fell on the retired
    driver's exact-fit transparent border no longer rings the value up (changelog, *one render
    path*). `contact_sheet_3cam.rgba8` (`:1471`) and `contact_sheet_latin1.rgba8` (`:1552`) — each
    sheet tile is a **thumbnail render** through A21's injected `RenderFn`, so it picks up the same
    minification change plus the tile-apron extent-antialiasing at tile edges. The captions in
    `contact_sheet_latin1` are the *same* Latin-1 glyph table at this pin — this re-baseline is the
    render-path shift only; the caption-shape re-baseline is the bundled-font leaf's, which orders
    after this one on purpose.
  - **Byte-identical (a mismatch blocks the leaf):** `render_probe_64x64`, `canvas_view_64x64`,
    `cells_insert_nested_64x64`, `canvas_nav_zoom_64x64`, `export_camera_64x64.rgba8`,
    `export_camera_64x64.png`, `export_filled_bg_64x64` — all pass byte-identical in the trial
    (integral-phase / 1:1 composites). The `ace_shell_test` e2e goldens likewise pass unmodified.
- **Pin-effectiveness + guarantee pin — Catch2, headless, in `ace_tests`.** `tests/arbc_pin_test.cpp`
  retargeted at v0.4.0:
  - **The five existing cases stay green** (writer-identity surface, unbound-until-first-transaction,
    no-external-arms-the-settler, any-thread journal enable-state, and the checkpointed non-inert
    `StateHandle` reopen). Their v0.3.0 header prose is updated to v0.4.0 where it names the pin, but
    the assertions are unchanged — including the last case's "no `Content` bound" (Constraint 7).
  - **New compile-time staleness witnesses** name the v0.4.0-only surface so a stale v0.3.0 tree
    fails at the compiler: at minimum `arbc::open_document` / `ReopenedDocument::unreconstructed`,
    `Document::create_content_and_attach` / `remove_contents`, `Registry`'s `KindInsertSchema`
    seam, `Journal::history()`, `HostViewport::attach_settler` / `Config::install_settler`, and the
    4-arg `render_offline(document, pinned, viewport, backend)` overload — pinned by
    signature/`static_assert` (unevaluated, no odr-use), following the existing `#5`-trio pattern at
    `:255-267`. **Behavioural** consumption of each stays with its downstream leaf; this file pins
    only *existence and shape*, which is what makes the bump attributable.
  - **One behavioural pin this leaf owns:** the render-path unification is observable — a document
    rendered through the editor's 2-arg `render_offline` still yields a frame and the editor's own
    goldens are its byte-exact witnesses (above). No separate new golden is needed.
- **`#23` any-thread `bounds()` — re-anchored, ASan/TSan.** `tests/canvas_host_test.cpp:1406` (the
  UI-thread `pick_targets` read against the live render walk) has its comment/naming re-anchored from
  "tripwire for an unstated race" to "assertion of the ANY-THREAD `Content::bounds()` contract
  (#23)". The case itself must pass **unmodified** in the `clang-asan` and `gcc-tsan` lanes; the
  editor's only relevant kind, `scene::CameraContent::bounds()`, is constant-empty
  (`src/scene/ace/scene/camera.hpp:46-48`) and needs no change.
- **Regression — the existing suites pass with only the four goldens and the two test-comment/name
  edits changed.** `ace_tests` and `ace_shell_test` otherwise unmodified. The trial establishes this
  (4/4 failures are the four re-baselined goldens); any *other* local failure is a stale
  `CMakeCache.txt` (Constraint 2), not a library regression.
- **UI e2e — ImGui Test Engine: N/A, justified.** This leaf drives no widget and changes no UI code
  path. `ace_shell_test`'s e2e corpus (including `tests/focused_canvas_indicator_e2e_test.cpp`'s
  accent probes and the history/undo e2e that drive `can_undo`/`can_redo`) is the regression surface
  and passed untouched in the trial.
- **Threading (ASan/TSan).** `clang-asan` and `gcc-tsan` green with **no new `tests/lsan.supp`
  entry**. `gcc-tsan` is the meaningful lane for the `#23` re-anchor and the writer-identity cases.
- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on the changed lines. The diff is
  one CMake string (not instrumented), four binary goldens (not instrumented), a fully-exercised pin
  test, and two test-comment edits — satisfied by construction.
- **Doc delta (same commit).** `docs/01-architecture.md` **A6 amended**; version strings corrected in
  `docs/00-design.md:4` and A23 (`docs/01-architecture.md:385`). Details under D-arbc_v040-6.
- **Deferred WBS work — none.** Every consumer of the v0.4.0 surface, and every assertion leaf for
  the three cleared defects, is **already registered** `depends !arbc_v040` (D-arbc_v040-7). This
  leaf registers no new task.

## Decisions

- **D-arbc_v040-1 — The leaf bumps the pin and absorbs the three breaks; it consumes none of the new
  surface in `src/`.** `CMakeLists.txt:25` moves, four goldens re-baseline, the pin test and one
  `#23` case update, and every new API (`open_document`, `history()`, `attach_settler`,
  `insert_schema`, `create_content_and_attach`, the pinned `render_offline`) stays unused in `src/`.
  *Rationale:* (i) the `.tji` note scopes it exactly this way — *"this leaf OWNS absorbing them"* for
  the three breaks, and *"NEW SURFACE CONSUMED DOWNSTREAM, each by its own leaf"* for the additions;
  (ii) a non-additive pin folded into a consumer leaf makes library re-baselines indistinguishable
  from editor bugs on that leaf; (iii) the trial shows the bump's whole `src/`-visible effect is four
  minification goldens — there is nothing to consume here.
  *Alternative rejected — fold the bump into the first consumer leaf (e.g. `export_pinned`).* It is
  one line, so folding looks cheap. Rejected because it destroys attribution on exactly the leaf that
  most needs it (the pinned-export overload is itself a render-path consumer), and because the eleven
  downstream leaves would each then have to reason about whether a golden move is theirs or the pin's.
  **No doc delta.**

- **D-arbc_v040-2 — Exactly four goldens re-baseline, each justified against the changelog; the other
  seven stay byte-identical. This deliberately inverts `arbc_v030`'s golden-invariance rule, because
  v0.4.0 is the first non-additive pin.** The movers are `look_through_shot_64x64`,
  `camera_manip_recrop_64x64`, `contact_sheet_3cam`, `contact_sheet_latin1`.
  *Rationale:* (i) the changelog names the mechanism precisely — the retired untiled driver *"sized
  its temp to exactly the content, so a minification's outer resampling tap fell on that surface's
  transparent border and rang the value up"*, and the tile apron makes a bounded edge antialiase
  through the composite tap rather than clip to whole device pixels; both are minification / re-crop /
  fractional-phase effects, and all four movers are exactly those composites; (ii) the seven
  non-movers are integral-phase or 1:1 exports, where the two paths agree — proven byte-identical in
  the trial, so their invariance obligation is *derivation*, not hope; (iii) re-baselining once here
  is what lets the bundled-font leaf re-baseline the contact sheets only for caption shape, per its
  stated dependency.
  *Alternative rejected — accept the churn without per-golden justification.* The `.tji` note forbids
  it outright: *"JUSTIFY each diff against the v0.4.0 changelog rather than accepting it — a golden
  that moves for a reason the changelog does not name is a regression."*
  *Alternative rejected — trust the note's `export_camera_64x64.png` prediction and re-baseline it.*
  The trial disproves the prediction: a 1:1 camera export composites at integral phase and is
  byte-identical at v0.4.0. Re-baselining it would mask a future real regression on that path.
  *Alternative rejected — a tolerance compare so nothing "moves".* It would hide exactly the
  edge-attenuation change that is the point of the release, and §9 makes tolerance the exception, not
  the default.
  **No doc delta** (the goldens are test artifacts; A6's amendment states the render-path change that
  explains them).

- **D-arbc_v040-3 — The staleness guard is renewed, and the pin test additionally witnesses the
  v0.4.0 surface the consumer leaves lean on.** `tests/arbc_pin_test.cpp` names v0.4.0-only symbols
  so a stale tree fails to compile, keeps its five v0.3.0 cases green, and adds unevaluated
  `static_assert` witnesses for the downstream surface.
  *Rationale:* (i) the `CACHE`-without-`FORCE` hazard is byte-for-byte the one `arbc_v030` closed, so
  its remedy carries over; (ii) `ace_tests` already links `arbc::arbc`, so the guard costs one file's
  worth of edits and no build-system surgery; (iii) witnessing the downstream surface here (existence,
  not behaviour) means a stale tree fails at *this* leaf rather than confusingly at the first consumer
  leaf, and it documents in one place what the pin bought.
  *Alternative rejected — a CMake `VERSION_LESS` guard.* `arbc_v030` established empirically that a
  subproject's `<NAME>_VERSION` is empty in the parent scope after `FetchContent_MakeAvailable`, so
  the guard would silently never fire.
  *Alternative rejected — pin each downstream surface behaviourally here.* That is consuming it, which
  is each downstream leaf's job (D-arbc_v040-1); the witnesses stay compile-time.
  **No doc delta.**

- **D-arbc_v040-4 — The four movers are minification/re-crop composites, and the fixes that move them
  are #17/#18-adjacent render-path corrections that the editor gets passively.** The look-through and
  camera-manip goldens are shot-camera exports whose framing minifies content; the contact sheets are
  grids of thumbnail renders. Every one is a `render_offline` (2-arg) call routed, at v0.4.0, through
  the tiled driver.
  *Rationale:* (i) this is the causal link the changelog draws between *one render path* and the
  observable diff, and pinning it in the refinement is what lets the implementer confirm each diff is
  the named change rather than a stray perturbation; (ii) it explains why `export_camera` (1:1) and
  `render_probe`/`canvas_view` (integral phase) do **not** move, which is the other half of the proof;
  (iii) it scopes the contact-sheet re-baseline to the render path so the caption-shape re-baseline
  stays the bundled-font leaf's.
  *Alternative rejected — attribute the contact-sheet churn to caption changes.* The Latin-1 glyph
  table is unchanged at this pin; attributing the diff to captions would double-count the bundled-font
  leaf's re-baseline and hide the render-path cause.
  **No doc delta** (captured by A6's amendment).

- **D-arbc_v040-5 — The `#23` any-thread `bounds()` case is re-anchored from tripwire to
  stated-guarantee assertion, in place, with no new synchronization.**
  `tests/canvas_host_test.cpp:1406` calls `arbc::Content::bounds()` on the UI thread against the live
  render walk. At v0.3.0 that was safe only because every editor kind fixes its extent at
  construction; at v0.4.0 it is safe by the documented ANY-THREAD contract.
  *Rationale:* (i) the `.tji` note says exactly this — *"the TSan case … stops being a tripwire and
  becomes an assertion of a stated guarantee"*; (ii) `scene::CameraContent::bounds()` is
  constant-empty (`src/scene/ace/scene/camera.hpp:46-48`) and every other kind is a library built-in,
  so no editor kind must publish an extent atomically; (iii) the edit is a comment/naming change, not
  new locking, so the case must pass unmodified in ASan/TSan.
  *Alternative rejected — leave the case as an unexplained TSan anchor.* It would keep asserting a
  property the library now *states*, leaving the next reader to re-derive why a UI-thread `bounds()`
  read is legal — the same drift `arbc_v030`'s comment-correction discipline exists to prevent.
  **No doc delta** (A5's *"no new locking"* is unchanged; this is a test-record correction).

- **D-arbc_v040-6 — The doc delta amends A6 and truths-up the two live version strings; the v0.3.0
  mentions that are amendment-provenance stay.**
  Concretely, in the same commit:
  - **A6** (`docs/01-architecture.md:210-216`) — amended to record that at the pinned v0.4.0 the
    `Backend` seam grew `composite_windowed` and `clear_rect` (virtuals a pixel-modelling backend
    must supply), and that `render_offline` now renders through the tiled driver (one render path,
    `Exactness` the only axis) — with the note that both are **inert for the editor**, which
    implements no `Backend` and calls only the 2-arg `render_offline` through `arbc::CpuBackend`, so
    A6's *"no editor change"* promise holds and the only observable effect is the four justified
    golden re-baselines. A6's structural claim (`CpuBackend` tiles → GL textures → pane) is unchanged.
  - **`docs/00-design.md:4`** — *"v0.3.0"* → *"v0.4.0"*.
  - **A23** (`docs/01-architecture.md:385`) — the clause *"an exported public header of the pinned
    v0.3.0"* → *"the pinned v0.4.0"*; A23's `RasterTileStore` reasoning is otherwise unchanged
    (the header is still exported and pulls no JSON at v0.4.0).
  - **Left as written:** A4.1a (`:126-127`), A14 (`:376`), A15 (`:377`), A18 (`:380`), A19 (`:381`)
    each say *"amended at the v0.3.0 pin"* or narrate a v0.x-era capability — historical provenance,
    not a live pin claim. Changing them would falsify the record, exactly the distinction
    D-arbc_v030-6 drew between live claims and historical narrative.
  *Rationale:* (i) a version bump of an existing dependency is neither a new dependency nor a new
  seam, so the rule points at *amending the governing row* (A6, the display-path/backend row), not
  adding one; (ii) A6 is precisely the row about the backend seam and the render path, so the
  seam-growth and one-render-path facts belong there; (iii) the two live version strings are the only
  present-tense pin claims in the docs (verified by grep), and leaving them is the drift that made
  A15 wrong for two releases.
  *Alternative rejected — a new `A27` row for the render-path unification.* It states a real fact but
  A6 already owns the render-path/backend question, and a transitional fact (the editor consumes none
  of the new virtuals) does not warrant a new constitution row.
  *Alternative rejected — leave `00-design.md:4`/A23 as "not my scope".* That is the choice
  `editor.canvas.arbc_v020` made, and it is why A15 was stale for two releases; a pin bump owns its
  version strings.

- **D-arbc_v040-7 — No new WBS follow-up is registered; every consumer of the v0.4.0 surface already
  exists as a leaf.** The eleven downstream leaves (`nested_real_pool`, `magnified_raster_idle`,
  `history_snapshot_adopt`, `settler_attach_split`, and the cross-area `reconstructing_reopen`,
  `insert_schema`, `one_action_one_entry`, `export_pinned`, plus the bundled-font and
  pending-removes-adjacent leaves) are all `depends !arbc_v040` in the `.tji` already.
  *Rationale:* (i) the 2026-07-28 parking-lot triage filed the fan-out ahead of this pin, so the WBS
  is complete before the leaf runs; (ii) #26 (removed-content reclaim) needs no editor change and #23
  is discharged in this leaf's own re-anchor, so neither spawns a task; (iii) registering a
  duplicate would orphan or double-own an existing leaf.
  *Alternative rejected — register a "verify the v0.4.0 upgrade" follow-up.* That would be an
  audit/re-verify task with no implementable deliverable — precisely the self-perpetuating shape the
  refinement charter forbids. The verification is this leaf's own acceptance criteria.
  **No doc delta.**

## Open questions

(none — all decided.)

Two items are surfaced for the return summary / parking lot rather than encoded as WBS tasks, because
neither is agent-implementable work:

- **The `.tji` note's golden prediction was empirically wrong in one direction.** It named
  `export_camera_64x64.png` as a re-baseline; the trial shows it stays byte-identical and the actual
  movers are `look_through`, `camera_manip_recrop` and the two contact sheets. This is captured in
  Constraint 4 and D-arbc_v040-2 so the implementer re-baselines the empirical four, not the predicted
  one — no task needed, but worth the closer's awareness.
- **A future editor-side `Backend` would now owe `composite_windowed` + `clear_rect`.** A6's *"a GPU
  `Backend` later needs no editor change"* promise is one clause weaker at v0.4.0: a future
  pixel-modelling editor backend must supply two more virtuals. There is no such backend today and no
  leaf that builds one, so this is a note in A6, not a task — a human design call if and when a GPU
  backend is scheduled.

## Status

**Done** — 2026-07-28.

- Bumped `ARBC_GIT_TAG` `v0.3.0` → `v0.4.0` in `CMakeLists.txt:25` (the single mechanical change).
- Re-baselined four goldens justified against v0.4.0 changelog (one-render-path + tile-apron shifts): `tests/goldens/look_through_shot_64x64.rgba8`, `tests/goldens/camera_manip_recrop_64x64.rgba8`, `tests/goldens/contact_sheet_3cam.rgba8`, `tests/goldens/contact_sheet_latin1.rgba8` — all minification / re-crop / thumbnail composites; seven non-movers confirmed byte-identical.
- Retargeted `tests/arbc_pin_test.cpp` at v0.4.0: five existing cases stay green; new compile-time witnesses (unevaluated, no odr-use) for `open_document`/`ReopenedDocument::unreconstructed`/`rebind_content`, `create_content_and_attach`/`remove_contents`, `Registry::insert_schema`, `Journal::history`, `HostViewport::attach_settler`/`detach_settler`/`Config::install_settler`, 4-arg `render_offline`, and the two new `Backend` virtuals.
- Re-anchored `tests/canvas_host_test.cpp` `#23` block: UI-thread `pick_targets` `Content::bounds()` read is now an assertion of the documented ANY-THREAD contract, not a tripwire for an unstated race.
- Amended `docs/01-architecture.md` A6 to record `Backend`-seam growth (`composite_windowed`/`clear_rect`) and one-render-path shift; corrected live version strings in `docs/00-design.md:4` and A23.
- `.tji` note predicted `export_camera_64x64.png` would move; empirically it stays byte-identical (1:1 integral-phase export) — the actual movers are the four above (D-arbc_v040-2 correction captured in Constraint 4).
- No new WBS follow-up registered (D-arbc_v040-7 — every consumer leaf already registered `depends !arbc_v040`).
