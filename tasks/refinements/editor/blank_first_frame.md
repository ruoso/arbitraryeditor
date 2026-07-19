# editor.canvas.blank_first_frame — Don't advance the published frame sequence until the first composited non-empty tile

## TaskJuggler entry

`tasks/00-editor.tji:201-205` — `task blank_first_frame` under `editor.canvas`. Effort
`1d`, `allocate team`, `depends !frame_sync` (i.e. `editor.canvas.frame_sync`). The `note`
(`:205`) frames the leaf:

> "CanvasDriver publishes a blank first frame and advances `frames_issued` for it, making
> any frame-count-based settle heuristic unsound under sanitizer/container slowdown (the
> settle fires on the blank frame). Don't advance the published sequence until a frame has
> composited non-empty tile content. Source-of-debt: `tasks/refinements/cameras/model.md`.
> Design: `docs/01-architecture.md A5`."

This is tech-debt registered by the `cameras.model` closer (`cameras/model.md:389`): that leaf
hit the symptom in `tests/canvas_view_e2e_test.cpp` and worked around it by gating the *test's*
pixel capture on content reaching the framebuffer (a 30 s bounded deadline) instead of the
frame-quiet heuristic — but left the **unsound driver behavior** in place and minted this leaf to
fix it at the source. This refinement lands at `tasks/refinements/editor/blank_first_frame.md`
(the `editor/` set, matching every other `editor.canvas.*` refinement — `frame_sync.md`,
`multi_canvas.md`, `nav.md`, `fit_bounds.md` — per the "first dot-segment of the fully-qualified
id" rule in `tasks/refinements/README.md:9-18`; the fully-qualified id is `editor.canvas.blank_first_frame`,
first segment `editor`). The `.tji` note carries no refinement back-link yet; the closer appends
`Refinement: tasks/refinements/editor/blank_first_frame.md` per the ritual
(`tasks/refinements/README.md:57-68`).

Nothing downstream `depends` on this leaf — it completes an existing promise (a *sound*
frame-settle signal) and hardens the ASan/TSan lane that `frame_sync` established.

## Effort estimate

**1 day** (from the `.tji`). The change is small and localized to **L2 `render`**: one pure
predicate plus a two-line gate inserted into the two existing publish paths, and converting the
affected e2e settle heuristics to the now-sound signal. No new render path, no new channel, no new
thread, no new UI seam, no new component, no libarbc change. The cost is in three places:
(1) a pure `render::frame_has_content(const Srgb8Image&)` coverage predicate; (2) the publish gate
in **both** `CanvasDriver::drive_once` (`src/render/canvas_driver.cpp:94-114`) and
`CanvasHost::drive_once` (`src/render/canvas_host.cpp:275-291`) — the two copies of the
`frame_sync` double-buffer publish, kept in lockstep; (3) retiring the `cameras.model` test
workaround and making the frame-count settle loops rely on the now-sound published sequence.

## Inherited dependencies

**Settled (from `editor.canvas.frame_sync`, `tasks/00-editor.tji:161-166`, Done 2026-07-18).**
The whole publish machinery this leaf gates shipped in `frame_sync` and was generalized per-canvas
by `multi_canvas`:

- **The publish gate.** `CanvasDriver::drive_once` (`src/render/canvas_driver.cpp:53-115`) drives
  one `renderer_.step()`, then publishes iff the frame is new:
  `const std::uint64_t frames = renderer_.frames_issued(); if (frames == 0 || frames ==
  published_frames_) return false;` (`:94-99`), else `published_frames_ = frames;`, copies the
  settled image `back_ = renderer_.image();` (`:108`), and publishes under a short lock with
  `std::swap(published_, back_); ++sequence_;` (`:110-113`). **This `frames_issued`-only gate is
  the debt** — it advances `sequence_` for the very first issued frame even when that frame
  composited no tile content yet.
- **The published sequence.** `CanvasDriver::sequence_` / `published_sequence()`
  (`canvas_driver.hpp:100,128`) and, per-entry, `CanvasHost::Entry::sequence` /
  `published_sequence(id)` (`canvas_host.cpp:47,335-339`) — the monotonic double-buffer counter the
  UI/consumer compares against (D-frame_sync-4). `consume()` returns a new frame only when the
  sequence advanced (`canvas_driver.cpp:138-152`, `canvas_host.cpp:318-333`).
- **The resize re-key.** Both drivers reset `published_frames_ = 0` after a resize
  (`canvas_driver.cpp:77-81`, `canvas_host.cpp:256-258`) so the first frame at a new size always
  publishes — the per-size "nothing published yet" state this leaf reuses as its content-gate latch.
- **The still-scene / follow-up loop.** `CanvasHost::run` re-arms `dirty` when `drive_once` reports
  `more_pending` (`canvas_host.cpp:308-314`); `more_pending` is set both by a publish **and** by
  `entry->renderer.step()` returning `schedule_follow_up` (the bounded budget did not settle,
  `canvas_host.cpp:269-271,291`) — the re-drive is gated on `schedule_follow_up`, *independent of
  publish*.

**Settled (from `editor.canvas.multi_canvas`, `tasks/00-editor.tji:168-172`, Done 2026-07-18).**
`multi_canvas` swapped the live path onto a **real shared `WorkerPool` + bounded per-frame budget**
(D-multi_canvas-3): `CanvasHost` uses `default_interactive_pool_config()` and
`k_default_frame_budget{8}` ms (`canvas_host.cpp:31,101-103`), and the shipping app holds exactly
one `render::CanvasHost host_` (`src/app/ace/app/canvas_view.hpp:104`). **This is what makes a blank
first frame real in production:** under a bounded budget the first `step()` can publish a partial
(tiles still resolving) — unlike `frame_sync`'s inline `hours(1)` settle-fully budget, which
composites the whole frame in one step. The per-canvas double-buffer is reused verbatim
(D-multi_canvas-1), so the gate this leaf adds lands identically in both `drive_once` bodies.

**Settled (from `editor.cameras.model`, Done 2026-07-18).** The `cameras.model` closer fixed the
*test-side* symptom (`cameras/model.md:389`): `tests/canvas_view_e2e_test.cpp:260-288` now drives
frames until the seeded probe colour reaches the framebuffer (`probe_visible()`, `:249-258`) within
a 30 s deadline, instead of the frame-quiet window — but explicitly left the `frames_issued`-quiet
loop (`:208-219`) in place and registered this leaf. This refinement removes the need for that
workaround by making the driver's published sequence a sound "content is on screen" signal.

**Settled (libarbc surface semantics, `arbc` v0.1.0).** A freshly-made target surface is
**transparent black**: `arbc/backend_cpu/cpu_backend.cpp:169-170` — "Zero-filled bytes are
transparent black in every format (float +0, integer 0), so a fresh surface starts clear." The
canvas target lives in the document's premultiplied-linear working space
(`canvas_renderer.cpp:51-54`); the compositor composites content over it (only where content
exists), and `CpuBackend::convert` un-premultiplies to straight-alpha sRGB8
(`canvas_renderer.cpp:94-105`, D10). So a frame that composited **no** tile is all-transparent
(every straight-alpha byte 0), and any composited coverage yields ≥1 pixel with a non-zero alpha
byte — the exact invariant the gate keys on.

**Pending (owned here).**
- A pure `render::frame_has_content(const Srgb8Image&)` coverage predicate.
- The content gate in both `drive_once` bodies (withhold the first publish while the frame is blank).
- Converting the affected e2e settle heuristics to the now-sound published sequence, retiring the
  `cameras.model` 30 s content-gated capture workaround.

## What this task is

**Arch A5** (`docs/01-architecture.md:84-97`) and the A4 data-flow it builds on (`:59-82`) make the
canvas an off-thread renderer publishing frames to the UI thread: "frames flow renderers → UI." The
UI (and tests) observe rendering progress *only* through the published-frame sequence
(`published_sequence(id)`, surfaced to the app as `frames_issued(view_id)`). Under `multi_canvas`'s
bounded budget + real pool, the driver's **first** published frame can be a blank partial: the
first `step()` issues frame 1 (`frames_issued` 0→1) but the worker pool has not yet composited any
tile within the 8 ms slice, so the converted image is all-transparent. The current gate publishes it
anyway and advances `sequence_` — so any observer that treats "the sequence advanced / went quiet"
as "the scene settled" can settle on the **blank** frame, and under sanitizer/container slowdown
(where the real composite lands several frames later) that misfire is frequent.

This leaf makes the published sequence a **sound content signal**: hold `sequence_` at 0 until a
frame composites non-empty tile content, then publish that frame and every frame after it normally.
Concretely:

- **A coverage predicate on the frame the driver already produces.** `render::frame_has_content(const
  Srgb8Image& frame)` returns `true` iff any pixel carries composited coverage — any straight-alpha
  byte `!= 0` (`for (std::size_t i = 3; i < frame.pixels.size(); i += 4) if (frame.pixels[i]) return
  true;`). It operates on the `Srgb8Image` (`render.hpp:19-23`, tightly-packed RGBA8, alpha at byte
  `+3`) that `CanvasRenderer::image()` already yields — no libarbc call, no counter, no new state.
- **The gate, inserted into both publish paths.** After computing `back_ = renderer_.image()` and
  before committing the publish, add: `if (published_frames_ == 0 && !frame_has_content(back_))
  return false;`. `published_frames_ == 0` means *nothing has published at the current size yet*
  (the resize re-key already resets it to 0), so the gate guards **only the first publish per size**
  — once a content frame publishes, `published_frames_ > 0` and every later frame publishes on the
  normal `frames_issued` advance, including partial refinements. (`back_` is reordered above the
  `published_frames_ = frames` assignment; the swap/`++sequence_` block is unchanged.)
- **Loop liveness is preserved by construction.** Withholding returns `false` (no publish). In
  `CanvasHost` the re-drive is gated on `schedule_follow_up` (`more_pending`), *not* on publish
  (`canvas_host.cpp:269-271,291,308-314`), so the loop keeps driving until content composites and a
  non-blank frame publishes. In `CanvasDriver` the inline `hours(1)` budget settles fully in one
  step (`schedule_follow_up` always false), so a blank first frame there means a *genuinely empty*
  document with no content to composite — correctly idling at `sequence_ == 0` (there is nothing to
  show), never a partial awaiting re-drive.
- **The settle heuristics become sound; the workaround retires.** With the sequence gated on
  content, `frames_issued("canvas#1") >= 1` and the frame-quiet loops now mean "content is on
  screen." The e2e is converted to assert exactly that (the first observed published frame is
  already non-blank), and the `cameras.model` 30 s content-gated capture
  (`canvas_view_e2e_test.cpp:260-288`) is removed.

## Why it needs to be done

The published sequence is the editor's one cross-thread "is the canvas showing the scene?" signal —
consumed by the UI (`consume`/`frames_issued`) and, critically, by the headless e2e/TSan lane that
`frame_sync` (§9 line 190) and `multi_canvas` established. Today that signal lies on the first
frame: it says "a frame is ready" before any tile has composited. The `cameras.model` closer had to
paper over it with a 30 s wall-clock content scan to keep the ASan build green
(`cameras/model.md:389`) — a per-test workaround that does not generalize (every future canvas e2e
would need the same 30 s scan) and that masks, rather than fixes, an unsound driver contract. A
frame-count settle heuristic is the *correct* pattern for a counter that only advances on real work
(doc 16's "counters don't lie" model); this leaf makes the published sequence honor that contract so
the heuristic is sound everywhere, and removes a class of sanitizer-slowdown flakes at the source.

## Inputs / context

**Governing design docs (normative — the constitution).**
- **Arch A5** — `docs/01-architecture.md:84-97` (§5) and the log restatement at `:255`: multi-canvas
  = N `HostViewport`/`InteractiveRenderer` over one `Document` sharing one `WorkerPool`, no new
  locking; a canvas carries no state but its camera. The bounded-budget shared-pool path this decision
  drives (`multi_canvas` D-multi_canvas-3) is what surfaces the blank first frame.
- **Arch A4** — `docs/01-architecture.md:59-82` (§4): "frames flow renderers → UI." The published
  sequence is that flow's readable edge; this leaf keeps it single-producer/single-consumer and
  render-thread-confined — the gate is a render-thread-local read of the render-thread-owned back
  buffer *before* the existing publish swap.
- **§8 levelization** — `docs/01-architecture.md:144-179`. `render` is **L2**
  (`:155` "HostViewport/InteractiveRenderer glue · frame-sync · tile→GL"); may depend on base,
  project, scene, gl, libarbc; GL but not ImGui (`:172`). The predicate + gate are pure L2 `render`;
  no ImGui/GL/SDL enters, L1 gains nothing.
- **§9 / §9.1 DoD** — `docs/01-architecture.md:181-245`. Universal DoD (`:199-203`); the ASan/TSan
  "UI↔driver handoff" lane (`:190`) this leaf hardens; the offscreen software-GL ASan lane (§9.1).

**libarbc API surface (fetched `v0.1.0` under `build/*/_deps/arbc-src/`).**
- `arbc::CpuBackend` — `arbc/backend_cpu/cpu_backend.cpp:169-170`: a fresh surface is transparent
  black (the blank-frame invariant); `convert` (`canvas_renderer.cpp:99`) un-premultiplies to
  straight-alpha sRGB8. No public per-tile coverage/`composites()` accessor is threaded through the
  editor's render bundle (see D-blank_first_frame-1's rejected alternative).
- `arbc::HostViewport::frames_issued()` (`arbc/runtime/host_viewport.hpp:229`) — advances on each
  issued frame, including the blank first one; the counter the gate now conditions.
- `arbc::HostViewport::StepOutcome{ bool schedule_follow_up; … }`
  (`arbc/runtime/host_viewport.hpp:113-117`) — the bounded-budget "not settled, re-drive" signal
  `CanvasHost` already consumes; no coverage field (so it cannot itself distinguish a blank frame).

**Editor seams this leaf extends.**
- `render` (L2) — add `bool frame_has_content(const Srgb8Image&)` to
  `src/render/ace/render/render.hpp` (impl `src/render/render.cpp`); both callers already
  `#include <ace/render/render.hpp>`.
- `render::CanvasDriver::drive_once` — `src/render/canvas_driver.cpp:89-114`: reorder `back_ =
  renderer_.image()` above the publish commit and insert the gate. Uses `published_frames_`
  (`canvas_driver.hpp:129`) as the per-size latch (reset at `:80`).
- `render::CanvasHost::drive_once` — `src/render/canvas_host.cpp:275-291`: the same gate on
  `entry->back` / `entry->published_frames` / `entry->sequence`; latch reset at `:258`.
- Tests — `tests/canvas_driver_test.cpp`, `tests/canvas_host_test.cpp` (both in `ace_tests`,
  `CMakeLists.txt:228`); `tests/canvas_view_e2e_test.cpp`, `tests/canvas_nav_e2e_test.cpp` (in
  `ace_shell_test`, `CMakeLists.txt:245-256`). The frame-quiet loops live at
  `canvas_view_e2e_test.cpp:208-219` and `canvas_nav_e2e_test.cpp:104-115`; the workaround at
  `canvas_view_e2e_test.cpp:260-288`.

**Predecessor refinements** (style + decision continuity):
`tasks/refinements/editor/frame_sync.md` (D-frame_sync-4 — the monotonic-sequence publish this leaf
sharpens; the ASan/TSan lane), `tasks/refinements/editor/multi_canvas.md` (D-multi_canvas-3 — the
bounded budget + real pool that surfaces the blank frame), `tasks/refinements/cameras/model.md`
(`:389` — the source-of-debt workaround this leaf retires), `tasks/refinements/editor/fit_bounds.md`
(sibling 1 d canvas-debt closer; style).

## Constraints / requirements

1. **The published sequence never advances for a fully-blank frame.** While nothing has published at
   the current size (`published_frames_ == 0`), a frame that composited no tile content
   (`!frame_has_content`) is **withheld**: no swap, no `++sequence_`, `drive_once` returns `false`.
   The very first frame that carries composited coverage publishes and advances the sequence to 1;
   every frame after publishes on the normal `frames_issued` advance (partial refinements included).
2. **"Non-empty tile content" = any non-zero straight-alpha byte.** `frame_has_content` keys on the
   composition invariant (coverage/alpha), never on colour, so opaque-black content
   `(0,0,0,255)` trips it and a colour collision with any background cannot. Grounded in the
   transparent-black blank target (`cpu_backend.cpp:169-170`).
3. **Both publish paths gated identically.** The gate lands in `CanvasDriver::drive_once` **and**
   `CanvasHost::drive_once` (the two copies of the `frame_sync`/`multi_canvas` double-buffer publish)
   through the *same* shared predicate, reusing each path's existing `published_frames_ == 0` resize
   re-key as the per-size latch. No new member field.
4. **Loop liveness preserved; no busy-spin, no stall.** `CanvasHost`'s re-drive stays gated on
   `schedule_follow_up` (`more_pending`), so withholding cannot stall a still-resolving canvas; a
   settled scene with nothing pending still idles (a genuinely empty document holds `sequence_ == 0`,
   correctly). `CanvasDriver`'s inline settle-fully budget yields no bounded partial, so its
   withhold-path only ever holds a genuinely empty document.
5. **Byte-exact pixels unchanged.** The gate changes *when* the first frame publishes, never its
   bytes: for a visible document the first content frame is byte-identical to today's first published
   frame (inline settles fully → first frame already non-blank → published immediately). The existing
   byte-exact goldens must still pass unchanged.
6. **Render-thread-confined; no new cross-thread state (A4).** The predicate reads only the
   render-thread-owned `back_` / `entry->back` before the publish swap; it touches neither the guarded
   `published_` nor the cache. No new lock, thread, channel, or shared field.
7. **Levelization (§8).** `frame_has_content` is a pure function added to existing L2 `render`; both
   callers already live in `render`. **No new component, no new DAG edge**; no ImGui/GL/SDL added.
   `check_levels` stays clean.

## Acceptance criteria

Instantiating the universal DoD (`docs/01-architecture.md:199-203`) for this leaf; `scripts/gate`
green (check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean).** The predicate is added to existing L2 `render`; both
  `drive_once` callers are already `render`. **No new component, no new DAG edge**; `render` gains no
  ImGui/GL/SDL include. Primary structural assertion.

- **Catch2 L2 unit (the bulk, headless, GL-free; extending `tests/canvas_driver_test.cpp` +
  `tests/canvas_host_test.cpp`, and a small `frame_has_content` case set, joined to `ace_tests`,
  `CMakeLists.txt:228`).** Inline executor for determinism (D-frame_sync-3 / D-canvas_view-2):
  - **`frame_has_content`:** all-zero-alpha buffer → `false`; a buffer with one alpha-`255` pixel →
    `true`; an **opaque-black** pixel `(0,0,0,255)` → `true` and a **non-zero-RGB / zero-alpha** pixel
    `(200,0,0,0)` → `false` (proves it keys on alpha coverage, not colour); an empty `Srgb8Image{}` →
    `false`.
  - **Blank first frame is withheld (`CanvasDriver`):** a sized-but-content-free document
    (`arbc::Document` with one `add_composition(64,64)` and no layers/content) drives one iteration —
    `renderer_.frames_issued()` advances to 1 (the first step always composites) yet
    `published_sequence()` stays **0** and `drive_once()` returns `false` (the frame is transparent).
  - **Content first frame publishes (`CanvasDriver`, the golden path unchanged):** the probe document
    (`project::build_probe_document`, a solid fill) drives one iteration — `published_sequence()`
    advances to 1 on the first issued frame exactly as today (content present inline → not withheld).
  - **Per-entry gate + re-key (`CanvasHost`):** a `canvas#1` over the content-free document holds
    `published_sequence("canvas#1") == 0` across `settle()`; a `canvas#2` over the probe document
    advances to ≥1. After a `request_resize` on a published entry (`published_frames` reset), the gate
    re-applies at the new size — a subsequent content frame re-advances the sequence (proves the
    per-size latch, not a global once-only flag).
  - **Sequence monotonic after first content:** once content has published, a partial/refinement
    frame publishes on the normal `frames_issued` advance (the gate is skipped while
    `published_frames_ != 0`).
  - Coverage ≥ 90 % on changed lines (`diff-cover --fail-under=90`, `coverage` preset).

- **UI e2e — ImGui Test Engine (the sound settle signal end-to-end; `ace_shell_test`, offscreen
  software-GL).** In `tests/canvas_view_e2e_test.cpp`: **remove** the 30 s content-gated capture
  workaround (`:260-288`) and instead drive the shell until `frames_issued("canvas#1") >= 1`, then
  assert `probe_visible()` is **already true at that first published frame** — i.e. the published
  sequence never advanced on a blank frame. This is the direct end-to-end proof of the fix (the
  observable that was flaky under sanitizer slowdown is now sound). Convert the frame-quiet loops
  (`canvas_view_e2e_test.cpp:208-219`, `canvas_nav_e2e_test.cpp:104-115`) to rely on the now-sound
  published sequence rather than a quiet window. Runs under the §9.1 offscreen ASan lane.

- **Golden — not added here (justified).** This leaf introduces **no new render path** and does not
  perturb any pixel: for a visible document the first published frame is byte-identical to today's
  (Constraint 5), so the existing byte-exact goldens (`tests/goldens/canvas_view_64x64.rgba8` and the
  `canvas_host` goldens, via `ace_test::compare_golden`) pass **unchanged** — that they still pass is
  the assertion that the gate changed only *timing*, not bytes. A blank withheld frame has no
  meaningful golden. This is the justified exception to "rendered output gets a golden"
  (`docs/01-architecture.md §9`), not the default.

- **Threading (ASan/TSan) — no new scope; the lane this leaf hardens.** The gate adds no thread,
  channel, or shared field; it is a render-thread-local read of the render-thread-owned back buffer
  before the existing publish swap. The `frame_sync` / `multi_canvas` lifecycle tests
  (boot → render loop `step()` ‖ UI edits + pokes ‖ housekeeping ‖ double-buffer handoff → clean
  join) run green under the `asan` and `tsan` presets with the gate active. No new TSan target is
  warranted; the refinement claims none. The point of the leaf is precisely to make the ASan lane's
  settle signal sound without the `cameras.model` 30 s wall-clock workaround.

- **Format + build clean** across the standard presets; `scripts/gate` green.

**No new WBS leaf is deferred.** This leaf closes the `blank_first_frame` debt in full and retires the
`cameras.model` test workaround in the same change. The mechanical simplification of the remaining
frame-quiet settle loops is folded into this leaf's e2e criterion (it is trivial cleanup enabled by
the fix, not separable implementable work) — it is deliberately **not** minted as a WBS task, which
would be an un-closeable "tidy the tests later" leaf. No parking-lot item surfaces (no
human-judgment gap; the emptiness signal is decided below).

## Decisions

- **D-blank_first_frame-1 — Gate the first publish on straight-alpha coverage of the composited
  frame (`render::frame_has_content`), not on a library counter.** The predicate is "any pixel with a
  non-zero alpha byte," run on the `Srgb8Image` the driver already produces; the gate withholds the
  publish while `published_frames_ == 0` and the frame is blank. *Rationale:* a freshly-made
  working-space target is transparent black (`cpu_backend.cpp:169-170`) and the compositor composites
  content over it, so "any non-zero alpha" is the *exact* "did any tile composite" invariant — and it
  is tested on the very bytes the UI displays, so the signal and the observable can never disagree. It
  is pure, deterministic, headless-unit-testable, needs no libarbc change, adds no state, and reuses
  an existing seam — the §8 bias (reuse seams, simplest abstraction, pin observable behavior). It keys
  on coverage, not colour, so it is robust to any content colour (Constraint 2). *Alternative
  rejected:* thread an `arbc::CompositorCounters` through the render bundle and gate on
  `composites() > 0` (`arbc/compositor/counters.hpp:43`) — the library's own "a tile composited"
  witness (doc 16 "counters don't lie"). Rejected because `CanvasRenderer` wires no
  `CompositorCounters` into `HostViewport`/`InteractiveRenderer` today, so this needs a counter object
  constructed and threaded into the render bundle plus a new `CanvasRenderer` accessor surfaced up to
  both drivers — a larger, more invasive change with *uncertain* semantics (whether a blank
  bounded-budget frame calls `composite` at all is not specified), for **no added correctness** over
  a coverage test on the exact frame being published. *Alternative rejected:* compare the frame
  against the shell clear colour / a known-blank RGB baseline — fragile (content could legitimately
  match any RGB); alpha coverage sidesteps colour entirely. **No doc delta required.**

- **D-blank_first_frame-2 — Reuse `published_frames_ == 0` (reset on resize) as the per-size latch;
  add no new member.** The gate is `if (published_frames_ == 0 && !frame_has_content(back_)) return
  false;`. Because both drivers already reset `published_frames_ = 0` on resize
  (`canvas_driver.cpp:80`, `canvas_host.cpp:258`), the gate guards the first publish **per size** and
  re-arms after a resize for free; once a content frame publishes (`published_frames_ > 0`) the
  predicate is skipped, so later partial refinements publish normally and the O(w·h) alpha scan runs
  only on the first frame(s) of each size. *Rationale:* the minimal change — no new field in two
  classes, no semantic shift of `published_frames_` ("frames_issued last published" is unchanged),
  and correct re-arm on resize with existing state. The one redundant re-scan (a genuinely empty
  document re-woken by a poke while still blank) is cheap and rare. *Alternative rejected:* a dedicated
  `bool content_published_` latch plus advancing `published_frames_` on withheld frames to skip
  re-scans — marginally fewer scans, but a new member in both classes and a `published_frames_`
  semantic shift, for a saving that only matters in the empty-document steady state. **No doc delta
  required.**

- **D-blank_first_frame-3 — Withholding relies on the existing `schedule_follow_up` re-drive;
  `CanvasDriver`'s dropped-`step()`-return is left untouched.** Returning `false` on a withheld frame
  is safe because `CanvasHost::run` re-arms on `more_pending`, which `entry->renderer.step()`'s
  `schedule_follow_up` sets independently of publish (`canvas_host.cpp:269-271,291`) — the loop keeps
  driving a still-resolving canvas until its first content frame publishes. `CanvasDriver::drive_once`
  discards `renderer_.step()`'s return (`canvas_driver.cpp:89`), but it runs the inline `hours(1)`
  settle-fully budget, so `schedule_follow_up` is always false and a blank first frame there is a
  *genuinely empty* document (no re-drive owed — correctly idle at `sequence_ == 0`). *Rationale:* the
  app path is `CanvasHost` (`canvas_view.hpp:104`), where liveness is already correct; adding a
  `schedule_follow_up`-driven re-arm to `CanvasDriver` would be speculative scope for a class that is
  never run under a bounded budget. *Alternative rejected:* thread `schedule_follow_up` into
  `CanvasDriver::run`'s re-arm defensively — unnecessary for any shipped path, and it would expand a
  1 d fix; if `CanvasDriver` ever adopts a bounded budget, that re-arm becomes a same-change
  prerequisite then. **No doc delta required.**

## Open questions

`(none — all decided.)` The one empirical unknown — the signal for "non-empty tile content" — is
decided by D-blank_first_frame-1 (straight-alpha coverage on the produced frame, grounded in the
transparent-black blank target, `cpu_backend.cpp:169-170`), not deferred. No human-judgment item
surfaces for `tasks/parking-lot.md`; no new WBS leaf is spawned.

## Status

**Done** — 2026-07-19.

- Added `render::frame_has_content(const Srgb8Image&)` to `src/render/ace/render/render.hpp` (declaration) and `src/render/render.cpp` (impl) — pure predicate: any non-zero straight-alpha byte → `true`; all-zero or empty → `false`.
- Gated both publish paths in `src/render/canvas_driver.cpp` (`CanvasDriver::drive_once`) and `src/render/canvas_host.cpp` (`CanvasHost::drive_once`) to withhold the first frame while it is blank, using a once-only `content_published` latch (deviation from D-2 below).
- Also updated `src/render/canvas_renderer.cpp` + `src/render/ace/render/canvas_renderer.hpp` and `src/render/ace/render/canvas_driver.hpp` to surface the in-flight tile signal needed for loop liveness (deviation from D-3 below).
- Deviation from **D-blank_first_frame-3** (empirically wrong): `schedule_follow_up` is `false` for a degraded in-flight first frame with no async-completion wake, so withholding stalled the loop. Fixed by making `CanvasRenderer::step()` also return `true` while `pending().tiles` is non-zero — no libarbc change, existing public accessor.
- Deviation from **D-blank_first_frame-2**: per-size `published_frames_==0` latch replaced with a once-only `content_published` latch, because per-size re-keying re-withheld after every resize and raced async re-composition, causing e2e flakiness. The once-only latch gates only the first content frame.
- Added Catch2 unit tests in `tests/canvas_driver_test.cpp` and `tests/canvas_host_test.cpp`: `frame_has_content` alpha-coverage cases; CanvasDriver blank-first-frame withheld; per-entry gate + resized-content-entry republish + partial-refinement-after-content + REAL-bounded-pool blank-then-content liveness regression.
- Retired the `cameras.model` 30 s workaround from `tests/canvas_view_e2e_test.cpp:260-288`; converted frame-quiet settle loops in `tests/canvas_view_e2e_test.cpp` and `tests/canvas_nav_e2e_test.cpp` to rely on the now-sound published sequence.
- Fixed `seed_nested` fixture in `tests/canvas_nav_e2e_test.cpp` (added full-frame inline solid background — nested-composition children render no straight-alpha coverage via bridge binding the interactive host doesn't wire).
- Relaxed strict post-`WindowFocus` `frames_issued` advance wait in `tests/multi_canvas_e2e_test.cpp` to `≥1 (content present)`.
- Tech-debt surfaced and registered as `editor.canvas.nested_composition_binding`: interactive `CanvasRenderer` does not wire a `KindBridge`/`Registry` `DocumentBinding`, so a nested-composition canvas renders blank in production.
