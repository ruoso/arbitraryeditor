# editor.canvas.accent_palette — Hoist the canvas accent colour into a shared alpha-parameterized constant

## TaskJuggler entry

- **Task:** `editor.canvas.accent_palette` — *"Hoist the canvas accent colour into a shared
  alpha-parameterized constant"*, `tasks/00-editor.tji:261-266`, under
  `task canvas "Canvas & rendering"` (`tasks/00-editor.tji:171`).
- **Effort:** `0.5d` · `allocate team`.
- **Depends:** `!focused_canvas_indicator` → `editor.canvas.focused_canvas_indicator`
  (**Done** — `complete 100`, `tasks/00-editor.tji:255-260`).
- **Note (`.tji:265`):** *"IM_COL32(120, 200, 255, α) now appears at five sites in
  src/app/canvas_view.cpp with no shared definition and three distinct alphas (40, 220, 255;
  the amber at :296 is a different hue). A blanket rename cannot consolidate them — the alphas
  differ — so this needs a decomposed constexpr RGB triplet plus an accent(uint8_t alpha) helper
  applied at every site, with the existing goldens and e2e pixel probes proving byte-identical
  output. Prerequisite for any future theming. Source-of-debt:
  tasks/refinements/canvas/focused_canvas_indicator.md. Design: docs/00-design.md D18."*
- **Back-link:** this refinement lands at `tasks/refinements/canvas/accent_palette.md`.
  **The closer** appends `Refinement: tasks/refinements/canvas/accent_palette.md` to the `.tji`
  note and adds `complete 100` after `allocate team`. **Do not** hand-edit the `.tji` here.
- **Source of debt:** `tasks/refinements/canvas/focused_canvas_indicator.md` —
  **D-focused_canvas_indicator-7** (`:555-562`) and its *Deferred WBS work* stanza (`:396-411`).
- **Downstream dependents:** none in the WBS today. The seam it leaves is the one a future
  theming/preferences leaf would consume (`docs/00-design.md:513`, *"Not yet designed (open) —
  Preferences, accessibility, i18n"*).
- **Milestone:** `m9_editor` (`tasks/99-milestones.tji:6-8`), reached through the `editor.canvas`
  container dependency.

## Effort estimate

**Half a day.** This is a mechanical, byte-for-byte-preserving refactor of six literals in one
file, plus one new header and one new headless Catch2 file. Nothing about the *drawing* changes.

- **The whole consolidation set lives in one translation unit.** `grep -n IM_COL32 src/` returns
  eight hits and only two files: six in `src/app/canvas_view.cpp` and two in `src/views/views.cpp`
  (black background `:112`, white text `:133` — neither is accent-hued, neither is in scope).
  There is no cross-component sweep to do.
- **The values are already known and already commented.** `src/app/canvas_view.cpp:32-43` is a
  twelve-line comment block that already states the accent triplet, the reason for the opaque
  alpha, and the reason the other literals were not folded in. The refactor turns that prose into
  the code it describes.
- **The e2e pixel oracle already exists.** `tests/focused_canvas_indicator_e2e_test.cpp:78-81,
  171-194` probes `(120, 200, 255)` at ±8 per channel through llvmpipe. It is *not* modified by
  this task; it becomes the regression that says the hue survived.
- **No CMake edit for the header.** `ace_app` is built from `file(GLOB _app_srcs … src/app/*.cpp)`
  (`CMakeLists.txt:204-206`) and its include dir `src/app` is `PUBLIC` (`:208`), so a new header
  under `src/app/ace/app/` is picked up with no build-file change. The one CMake edit is adding
  `tests/accent_palette_test.cpp` to the `ace_shell_test` source list (`CMakeLists.txt:260-278`).

New code: one new header `src/app/ace/app/accent.hpp` (~30 lines including its comment), six
call-site rewrites in `src/app/canvas_view.cpp` (`:42`, `:352`, `:353`, `:526`, `:527`, `:554`)
plus deletion of the now-redundant half of the `:32-41` comment, one new
`tests/accent_palette_test.cpp` added to the `ace_shell_test` source list
(`CMakeLists.txt:260-278`). **No new component, no new DAG edge, no new external dependency, no
libarbc change, no `dock` change, no `dockmodel` change, no `views` change, no golden re-baseline,
no change to any existing test file.** No doc delta (see **D-accent_palette-7**).

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.canvas.focused_canvas_indicator`** (**Done**, `complete 100`) —
  `tasks/refinements/canvas/focused_canvas_indicator.md`. Consumed:
  - **D-focused_canvas_indicator-4** — *"`IM_COL32(120, 200, 255, 255)`, 1.0f thickness, no
    rounding, inset 0.5px inside the content rect … `(120, 200, 255)` is the established accent in
    this file … **alpha 255, not a translucent tint**, is chosen deliberately so the pixel lands
    unblended over whatever the pane is showing, which is what makes the e2e's ±8 accent-dominance
    probe robust under llvmpipe"*. This task must preserve that pixel exactly; the alpha stays a
    per-site choice, which is precisely why the helper is alpha-parameterized rather than a single
    named colour.
  - **D-focused_canvas_indicator-7** — *"The new draw site gets a file-local named accent constant;
    the four existing literals are* not *consolidated in this diff … consolidation is not a rename
    — it needs a decomposed RGB triplet plus an alpha-taking helper applied at every site."* That
    sentence is this task's specification.
  - **D-focused_canvas_indicator-3** — the marker is drawn unconditionally, on single- and
    multi-canvas docks alike. Preserved: this task touches the colour argument of
    `AddRect`, never the guard around it.
- **`editor.cameras.mint_from_focused_canvas`** (**Done**) —
  `tasks/refinements/cameras/mint_from_focused_canvas.md`. Consumed only indirectly: it is what
  made `canvas_view.cpp`'s frame-gizmo overlay (`:352-369`, the amber/accent active-vs-inactive
  pair) load-bearing, and its `tests/multi_canvas_mint_e2e_test.cpp` is one of the suites that must
  pass unmodified.

**Pending (owned here):** nothing. Every predecessor is `complete 100`.

## What this task is

`src/app/canvas_view.cpp` draws five pieces of chrome in the same accent hue `(120, 200, 255)` at
three different alphas, and one piece in an amber `(255, 210, 80)`, each written as a bare
`IM_COL32(…)` literal at its draw site. This task introduces a tiny `constexpr` colour vocabulary —
an `Rgb` triplet type, a `constexpr ImU32 rgba(Rgb, std::uint8_t)` packer, the two named triplets
`k_accent` / `k_camera_frame`, and the two convenience wrappers `accent(alpha)` /
`camera_frame(alpha)` — in a new header `src/app/ace/app/accent.hpp`, and rewrites all six sites to
call it. **The rendered output must be byte-identical**; this is a naming change, not a visual one.
Nothing about layout, hit-testing, draw order, alpha values or the composition changes. The header
is the seam a future theming leaf would repoint; today it has exactly one consumer.

## Why it needs to be done

Three literals of the same hue at three alphas, sitting beside a fourth literal of a *different*
hue, is a trap that a future editor will spring: a global search-and-replace of
`IM_COL32(120, 200, 255, 255)` silently misses the marquee (α 40 and α 220) and silently *hits*
nothing at all if the alpha is written differently, while a search for `120, 200, 255` cannot tell
the accent apart from an incidental triple. `focused_canvas_indicator` hit exactly this wall and
declined to consolidate inside its own 0.5d envelope (**D-focused_canvas_indicator-7**), leaving a
file that now carries a twelve-line comment (`src/app/canvas_view.cpp:32-41`) explaining that the
consolidation is owed. That comment is the debt marker; this task pays it and deletes it.

The forward-looking half is that `docs/00-design.md:513` leaves *Preferences, accessibility, i18n*
explicitly undesigned, and any of the three would need the chrome colours to be named values rather
than inline literals before it can begin — a contrast-ratio adjustment or a high-contrast mode is a
one-line edit against a named triplet and a six-site archaeology dig against literals. This task
does **not** build theming (see **D-accent_palette-6**); it removes the reason theming would have to
start with a refactor.

## Inputs / context

**Design docs (normative).**

- `docs/00-design.md` **D18** (`:485`) — the uniform dockspace; *"no privileged editor area"*.
  The only claim it makes on this task is negative: chrome that marks a pane must not be, and must
  not become, a layout privilege. A colour-naming refactor cannot violate it, which is why D18 is
  the `.tji` note's citation and why no amendment is needed.
- `docs/00-design.md` **D23** (`:490`) — *"the resolved target pane carries a **hairline accent
  border** drawn just inside its rect … It is passive chrome — no hit-testing, no layout, no effect
  on the composition or on any export"*. D23 names an *accent border* and deliberately stops there;
  it fixes no numeric colour. That is what leaves the triplet an implementation value this task may
  freely name, and equally what forbids changing it (the border must stay accent-hued and stay
  passive).
- `docs/00-design.md:513` — `- **Preferences, accessibility, i18n.**` under *"Not yet designed
  (open)"*. The bucket any future theming decision lands in. Cited to fix scope, not to expand it.
- `docs/01-architecture.md` **§8** (`:255-291`) — the levelization DAG. `app` is L4 and may depend
  on everything; `imgui` is allowed in `views`, `dock`, `app` and nowhere else
  (`scripts/check_levels.py:32-50`, `EXTERNAL_ALLOWED["imgui"] = {"views", "dock", "app"}`). A new
  header in `src/app/ace/app/` that includes `<imgui.h>` is inside the existing seam.
- `docs/01-architecture.md` **§9** (`:293-320`) — the four verification layers and the universal
  DoD. Instantiated for this leaf under *Acceptance criteria*.

**Sources to change.**

- `src/app/canvas_view.cpp:32-43` — the `focused_canvas_indicator` comment block plus
  `constexpr ImU32 k_focus_marker_color = IM_COL32(120, 200, 255, 255);` and
  `k_focus_marker_thickness`. Lines `:39-41` are the explicit debt note this task deletes.
- `src/app/canvas_view.cpp:297-299` — the focus-marker draw
  (`AddRect(marker_min, marker_max, k_focus_marker_color, …, k_focus_marker_thickness)`).
  The **only** consumer of `k_focus_marker_color`.
- `src/app/canvas_view.cpp:352-353` — `col_frame = IM_COL32(255, 210, 80, 200)` (amber, inactive
  camera frame) and `col_active = IM_COL32(120, 200, 255, 255)` (accent, active camera frame),
  selected between at `:363` and consumed by `AddQuad`/`AddRectFilled` at `:364-368`.
- `src/app/canvas_view.cpp:526-527` — the marquee: `AddQuadFilled(… IM_COL32(120, 200, 255, 40))`
  (the wash) and `AddQuad(… IM_COL32(120, 200, 255, 220), 1.0F)` (the band).
- `src/app/canvas_view.cpp:553-554` — the selected-cell outline,
  `AddQuad(…, IM_COL32(120, 200, 255, 255), 2.0F)`.
- `CMakeLists.txt:204-217` — `ace_app`: `file(GLOB _app_srcs … src/app/*.cpp)`,
  `target_include_directories(ace_app PUBLIC "${CMAKE_SOURCE_DIR}/src/app")`,
  `target_link_libraries(ace_app PUBLIC … imgui)`. The `PUBLIC` imgui link is what lets a test that
  links `ace::app` include a header that includes `<imgui.h>`.
- `CMakeLists.txt:260-279` — the `ace_shell_test` source list and
  `target_link_libraries(ace_shell_test PRIVATE ace::app Catch2::Catch2WithMain)`.

**Tests that must keep passing, unmodified.**

- `tests/focused_canvas_indicator_e2e_test.cpp` — `IM_REGISTER_TEST(engine, "multi_canvas",
  "focused_canvas_indicator")` (`:259`); local oracle constants `k_accent_r/g/b = 120/200/255`,
  `k_accent_tol = 8` (`:78-81`); the probe `accent_near(const Frame&, int, int)` (`:171-194`); the
  six phases (`:285-353`) whose two-sided assertions at `:320-321`, `:337-338`, `:353` are the
  pixel-level proof that the α-255 accent survived.
- `tests/selection_e2e_test.cpp` — marquee/pick chords; `capture_pixels` at `:145-150` is a
  screenshot baseline, so it also witnesses the α-40/α-220 marquee sites.
- `tests/camera_manip_e2e_test.cpp:258-262`, `tests/multi_canvas_mint_e2e_test.cpp:328-332`,
  `tests/frame_selection_e2e_test.cpp`, `tests/new_shot_from_view_e2e_test.cpp` — the suites that
  drive the frame gizmo and the rail verbs (model-state assertions).
- The six `render_offline` goldens under `tests/goldens/` compared byte-exactly by
  `ace_test::compare_golden` (`tests/golden_support.hpp:36-46`), resolved via
  `ACE_GOLDEN_DIR` (`CMakeLists.txt:251-252`).

**Precedent for the new test's home.** `tests/focused_framing_test.cpp:1-9` is a pure headless
Catch2 file that lives in `ace_shell_test` rather than `ace_tests` *"because only that target links
`ace::app` … even though the code under test is ImGui-free."* `tests/accent_palette_test.cpp`
follows the identical pattern (it is not ImGui-free, but it is GL-free and window-free).

## Constraints / requirements

1. **Byte-identical output.** Every one of the six `ImU32` values produced after the refactor must
   equal the literal it replaced, bit for bit. `(120,200,255)` at α ∈ {255, 255, 40, 220, 255} and
   `(255,210,80)` at α = 200. No alpha may be "rounded", unified or re-tiered.
2. **The alphas stay per-site.** The three accent alphas are semantically distinct (an unblended
   1 px marker, a 40/255 wash, a 220/255 band) and **D-focused_canvas_indicator-4** turns α = 255
   into a test-robustness property. The helper takes alpha as a parameter; it must not carry a
   default argument that would let a call site drift silently.
3. **`ImU32` packing must come from ImGui, not from hand-rolled shifts.** `IM_COL32` expands
   through `IM_COL32_R_SHIFT`/`IM_COL32_A_SHIFT`, which flip under `IMGUI_USE_BGRA_PACKED_COLOR`.
   The helper must be defined in terms of `IM_COL32`.
4. **`constexpr` end to end.** `Rgb`, `rgba`, the triplets and the wrappers are all `constexpr`, so
   every call site remains a compile-time constant and the generated draw calls are unchanged from
   today's literals.
5. **Levelization.** The new header lives in `src/app/ace/app/` (component `app`, L4) and may
   include `<imgui.h>`; `scripts/check_levels.py:32-50` allows `imgui` in `app`. No new component
   and no new dependency edge — `app` already links `imgui` `PUBLIC` (`CMakeLists.txt:216-217`).
   `check_levels` must stay clean.
6. **No existing test file is edited.** The suites listed above are the regression oracle; a proof
   that edits its own oracle proves nothing. In particular `tests/focused_canvas_indicator_e2e_test.cpp`
   keeps its own literal `k_accent_r/g/b` (see **D-accent_palette-5**).
7. **No golden re-baseline.** The goldens cover the `render_offline` composition path, which this
   task does not touch. If any golden moves, the refactor is wrong — a re-baseline is a failure
   signal, not a fix.
8. **The amber is renamed, not merged.** `(255, 210, 80)` is a different hue with a different
   meaning (inactive camera frame). It gets its own named triplet through the same packer; it must
   not be folded into `k_accent`, and `col_frame`/`col_active` must keep selecting between two
   distinct values at `:363`.
9. **Delete the debt comment.** `src/app/canvas_view.cpp:39-41` ("NOT consolidated with the four
   shipped literals … not a rename — D-focused_canvas_indicator-7") describes work this task
   completes; leaving it would make the file lie. The *design* half of the comment (`:32-38`: why
   the marker is opaque, why a hairline) stays.
10. **`clang-format` clean and no behavioural drift in the surrounding code.** The diff is
    confined to the colour arguments, the `constexpr` block at `:32-43`, and the new header/test.
11. **No new external dependency, no libarbc surface consumed, no threading surface touched.**

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9); `scripts/gate` green
(check_levels · clang-format · build · ctest) is the umbrella.

- **Levelization (`check_levels` clean).** `src/app/ace/app/accent.hpp` is in component `app`
  (L4) and includes `<imgui.h>`; `EXTERNAL_ALLOWED["imgui"] = {"views", "dock", "app"}`
  (`scripts/check_levels.py:32-50`) permits it. No component gains a dependency and the L1 core
  (`project`/`scene`/`interact`/`commands`/`dockmodel`/`writer`) is untouched, so no L1 file comes
  near an ImGui include. `python3 scripts/check_levels.py` exits 0.

- **Value-identity unit — Catch2, headless: `tests/accent_palette_test.cpp`**, added to the
  `ace_shell_test` source list (`CMakeLists.txt:260-278`), following
  `tests/focused_framing_test.cpp:1-9`'s precedent (no GL, no window, no ImGui context — only the
  `IM_COL32` macro). This is the primary proof of byte-identity, and it is deterministic where a
  software-GL pixel probe is not. Cases:
  - `"accent palette: the five accent sites reproduce their shipped literals"` — asserts
    `accent(255) == IM_COL32(120, 200, 255, 255)`, `accent(220) == IM_COL32(120, 200, 255, 220)`,
    `accent(40) == IM_COL32(120, 200, 255, 40)`, each written against the **literal**, so the test
    fails if the triplet is mistyped.
  - `"accent palette: the camera-frame hue is the amber, not the accent"` — asserts
    `camera_frame(200) == IM_COL32(255, 210, 80, 200)` **and**
    `camera_frame(200) != accent(200)`, pinning constraint 8.
  - `"accent palette: alpha is the only thing the parameter changes"` — for a runtime-supplied
    `std::uint8_t a`, asserts the three non-alpha bytes of `accent(a)` are invariant and that the
    alpha byte extracted via `IM_COL32_A_SHIFT` equals `a`. **The argument must be runtime, not a
    literal**, so the `constexpr` function bodies are actually executed and instrumented (see the
    Coverage bullet).
  - `"accent palette: rgba packs through IM_COL32"` — asserts
    `rgba(Rgb{1, 2, 3}, 4) == IM_COL32(1, 2, 3, 4)`, pinning constraint 3 (the test would fail if
    someone replaced the macro with hand-rolled shifts under a BGRA build).
  - `"accent palette: the focus marker constant is the opaque accent"` — asserts
    `k_focus_marker_color == accent(255)`, pinning **D-focused_canvas_indicator-4** at the value
    level. (Requires `k_focus_marker_color` to move into the header alongside the palette — see
    **D-accent_palette-4**.)

- **Rendered output — golden: N/A, justified.** The six `tests/goldens/*.rgba8` baselines cover
  libarbc's `render_offline` composition path; the six literals in scope are ImGui *draw-list*
  overlay chrome, drawn after and above the composited image and never present in a golden. No
  new golden is warranted and **no existing golden may be re-baselined** — see the regression
  bullet.

- **UI e2e — ImGui Test Engine: no new test, the existing accent probe is the e2e.**
  `tests/focused_canvas_indicator_e2e_test.cpp` already drives the real shell headless and probes
  the accent pixel two-sidedly at `:320-321`, `:337-338`, `:353` through
  `accent_near` (`:171-194`, ±8 per channel). It exercises the exact draw site this task rewrites
  (`src/app/canvas_view.cpp:297-299`), so it passing **unmodified** is the UI-level proof. A *new*
  e2e probing the α-40 marquee wash was considered and rejected: a 40/255 wash over pane content is
  a content-dependent expectation under llvmpipe, precisely the fragility
  **D-focused_canvas_indicator-4** chose α = 255 to avoid, and the value-identity unit above proves
  the same fact deterministically (**D-accent_palette-3**).

- **Threading (ASan/TSan): N/A for new surface, existing lanes stay green.** No thread, no shared
  state, no synchronisation is introduced — the change is `constexpr` values consumed on the UI
  thread. The `clang-asan` CI lane (`docs/01-architecture.md` §9.1, `:322-357`) already runs
  `ace_shell_test`, which links the changed translation unit, and must stay clean with
  `tests/lsan.supp` unchanged (no new suppression may be added for this task).

- **Regression — the existing suites must pass *unmodified*.** `ctest` green for both `ace_tests`
  and `ace_shell_test` with **zero** edits to any existing test file and zero changes to
  `tests/goldens/`. Specifically: `focused_canvas_indicator_e2e_test.cpp`,
  `selection_e2e_test.cpp`, `camera_manip_e2e_test.cpp`, `multi_canvas_mint_e2e_test.cpp`,
  `frame_selection_e2e_test.cpp`, `new_shot_from_view_e2e_test.cpp`, and the golden-bearing
  `render_probe_test.cpp` / `canvas_view_test.cpp` / `camera_manip_test.cpp` /
  `look_through_test.cpp` / `canvas_host_test.cpp`. A test-file edit in this diff is a review
  failure by constraint 6.

- **Coverage — ≥ 90 % diff coverage on changed lines** (`.github/workflows/ci.yml:88-145`,
  `diff-cover coverage.xml --compare-branch="$base" --fail-under=90`; only `src/` is measured).
  The hazard here is specific and must be designed for: a `constexpr` function whose every call
  site is constant-folded emits no runtime code, and gcov can report its body as an uncovered
  line. The `"alpha is the only thing the parameter changes"` case therefore calls `accent()` and
  `rgba()` with a **runtime** `std::uint8_t`, forcing an out-of-line instantiation that gcov
  instruments. The implementer runs the `coverage` preset locally
  (`CMakePresets.json:35-40`, `cmake --preset coverage`, build dir `build/coverage`) before
  closing rather than discovering this in CI.

- **Doc delta (same commit): none.** See **D-accent_palette-7** — naming an existing pixel value
  is implementation, not constitution, and **D-focused_canvas_indicator-4** already ruled
  pixel-level chrome specifics out of the design docs. D18/D23 are cited unchanged.

- **Deferred WBS work: none.** This task closes its own debt. Relocating `accent.hpp` from `app`
  down to `views` is deliberately **not** registered as a follow-up: it is a one-line include
  change that the first cross-component consumer will make as part of its own work
  (**D-accent_palette-2**), and a task whose only deliverable is "move a header when someone needs
  it" would sit unclosable in the WBS.

## Decisions

- **D-accent_palette-1 — An `Rgb` triplet + a `constexpr ImU32 rgba(Rgb, std::uint8_t)` packer +
  two named triplets (`k_accent`, `k_camera_frame`) + two thin wrappers (`accent(a)`,
  `camera_frame(a)`).**
  *Rationale:* (i) the `.tji` note asks for exactly *"a decomposed constexpr RGB triplet plus an
  `accent(uint8_t alpha)` helper"*, and the wrapper is what makes `accent(40)` read at a draw site
  where `rgba(k_accent, 40)` would not; (ii) hoisting the packer out as `rgba` costs three lines and
  is what lets the amber be named through the same mechanism instead of staying the one bare literal
  in a file of named ones (constraint 8); (iii) an `Rgb` aggregate keeps the hue a single value that
  a future theming layer can hand around, where three loose `constexpr std::uint8_t k_accent_r/g/b`
  could not be passed as one thing.
  *Alternative rejected — a single `constexpr ImU32 k_accent = IM_COL32(120, 200, 255, 255)` plus
  per-site alpha overwrite.* Rewriting the alpha byte of a packed `ImU32` means masking against
  `IM_COL32_A_MASK` and re-shifting at every site — more code than the packer, and it hard-codes
  the packing layout the macro exists to hide (constraint 3).
  *Alternative rejected — `ImVec4` + `ImGui::ColorConvertFloat4ToU32`.* Float round-tripping cannot
  guarantee the byte-identity constraint 1 demands, and it is a runtime call, not a `constexpr`.
  **No doc delta required.**

- **D-accent_palette-2 — The header lands at `src/app/ace/app/accent.hpp` (component `app`, L4),
  not in `views`.**
  *Rationale:* (i) the entire consumer set today is one file, `src/app/canvas_view.cpp`, and the
  refinement bias is the simpler abstraction with one or two call sites; (ii) `src/views/views.cpp`
  draws only black and white (`:112`, `:133`) and has no accent to share, so placing the header in
  `views` would create a component-crossing seam with zero users on the far side; (iii) `app` is L4
  and already links `imgui` `PUBLIC` and exports `src/app` as a `PUBLIC` include dir
  (`CMakeLists.txt:208, 216-217`), so the header is reachable from `ace_shell_test` with no build
  edit.
  *Alternative rejected — `src/views/ace/views/accent.hpp` as the "future theming home".* `views`
  (L3) is reachable from `dock` and `app`, so it is the strictly more general placement — but it is
  general for a consumer that does not exist. If and when a second component needs the accent, the
  move is one `git mv` plus one include line, and it will be made by the leaf that needs it with
  its own tests. Building the wider seam now would be speculative structure, and per the
  no-"revisit"-task rule it is not registered as deferred work either.
  *Alternative rejected — keep it file-local in `canvas_view.cpp`'s anonymous namespace.* Cheapest,
  but it makes the helper untestable: nothing outside the TU can link an anonymous-namespace
  symbol, so the only available proof would be the ±8 software-GL pixel probe, and the diff would
  have no unit coverage to satisfy the 90 % gate. A header is what buys the deterministic oracle.
  **No doc delta required.**

- **D-accent_palette-3 — The proof of byte-identity is a Catch2 value-identity test, not a new
  pixel probe.**
  *Rationale:* (i) the property being preserved is *"this expression evaluates to that 32-bit
  value"*, which a `==` against the literal pins exactly, where a framebuffer read pins it only to
  ±8 per channel after llvmpipe blending; (ii) the two α-255 accent sites are already probed
  end-to-end by `tests/focused_canvas_indicator_e2e_test.cpp:320-321,337-338,353`, so the e2e layer
  of §9 is genuinely covered by an existing test exercising the changed code; (iii) writing the
  unit assertions against the raw `IM_COL32(120, 200, 255, α)` literals — not against the new
  constants — keeps the test an independent oracle rather than a tautology.
  *Alternative rejected — a new marquee-drag e2e probing the α-40 wash and α-220 band.* Both are
  blended over pane content, so the expected pixel depends on what the canvas is showing; a
  tolerance wide enough to be stable under llvmpipe would be wide enough to accept a wrong hue.
  That is the exact failure mode **D-focused_canvas_indicator-4** chose an opaque marker to dodge.
  **No doc delta required.**

- **D-accent_palette-4 — `k_focus_marker_color` moves into the header as
  `accent(255)`; `k_focus_marker_thickness` stays in `canvas_view.cpp`.**
  *Rationale:* (i) the constant is a *colour*, and keeping it in the same place as the palette is
  what lets the unit test assert `k_focus_marker_color == accent(255)` and thereby pin
  **D-focused_canvas_indicator-4** at the value level; (ii) the thickness is not a colour — it is a
  draw-call parameter with one call site (`:299`) and no palette meaning, so hoisting it would be
  cargo-culting the move; (iii) the split keeps `accent.hpp` a colour vocabulary rather than a
  drifting bag of chrome constants.
  *Alternative rejected — inline `accent(255)` at `:297` and delete the named constant.* It would
  lose the name that `focused_canvas_indicator`'s comment block refers to, and lose the unit
  assertion that ties the marker's alpha to the e2e's robustness argument.
  **No doc delta required.**

- **D-accent_palette-5 — `tests/focused_canvas_indicator_e2e_test.cpp` keeps its own literal
  `k_accent_r/g/b = 120/200/255` (`:78-81`); it does *not* include the new header.**
  *Rationale:* a test that imports the value it is checking proves only that the value equals
  itself. The e2e's duplicated triplet is the *independent* statement of what a user should see, and
  it is the thing that would catch a refactor that changed the accent while remaining internally
  consistent. Constraint 6 makes this a hard rule for this diff, not a preference.
  *Alternative rejected — deduplicate by having the e2e include `accent.hpp`.* Superficially DRY,
  strictly weaker: it converts the only end-to-end hue assertion in the repo into a tautology.
  **No doc delta required.**

- **D-accent_palette-6 — This task ships a named palette, not a theming mechanism: no runtime
  mutability, no style struct, no preferences plumbing, no ImGui style-var integration.**
  *Rationale:* (i) `docs/00-design.md:513` leaves *Preferences, accessibility, i18n* explicitly
  undesigned, so a theming mechanism built now would be inventing constitution from a 0.5d
  refactor; (ii) `constexpr` is what guarantees constraint 4's byte-identity — the moment the
  values become runtime-mutable, "byte-identical output" stops being a compile-time fact; (iii) the
  `.tji` note's *"prerequisite for any future theming"* is a statement about what this task
  *unblocks*, not what it delivers.
  *Alternative rejected — a `struct ChromeStyle` singleton with settable members.* One consumer,
  no settings UI, no persistence format, and it would need its own thread-safety story once the
  writer thread exists (`editor.canvas.writer_thread`). Speculative, and it would make this diff a
  design task wearing a refactor's effort estimate.
  **No doc delta required.**

- **D-accent_palette-7 — No design-doc delta.**
  *Rationale:* the doc-delta rule fires on a new external dependency, a new architectural seam, or
  a deviation from a stated decision, and this task is none of the three: no dependency, no new
  component or DAG edge (**D-accent_palette-2**), and every visible pixel is unchanged by
  construction (constraint 1). **D-focused_canvas_indicator-4** already settled the general
  question — *"pixel-level chrome specifics are implementation, not constitution; D23's amended
  clause says 'hairline accent border' and stops there"* — and naming a value the docs deliberately
  never named does not promote it to constitutional text. D18 and D23 are cited unchanged; a
  theming decision, if one is ever made, amends `docs/00-design.md:513`'s open bucket then.
  **No doc delta required.**

- **D-accent_palette-8 — The `.tji` note's line references (`:296`, and "five sites") are stale;
  the implementer works from the line numbers in *Inputs / context*, and the `.tji` note is not
  corrected.**
  *Rationale:* (i) the note was written before `focused_canvas_indicator` landed, which inserted the
  marker constant at `:42` and shifted everything below; the amber it calls `:296` is now `:352`;
  (ii) the actual set is six `IM_COL32` lines in `canvas_view.cpp` — five accent-hued
  (`:42` α 255, `:353` α 255, `:526` α 40, `:527` α 220, `:554` α 255) and one amber (`:352`
  α 200) — which matches the note's "three distinct alphas (40, 220, 255)" exactly; the note's
  "five sites" counts the accent sites and the amber is called out separately, so the note is
  *accurate*, only its line anchors drifted; (iii) `.tji` files are the orchestrator's and closer's
  to edit (this refinement's File-scope rule), and rewriting historical note text would erase the
  provenance the note carries. This refinement's *Inputs / context* is the authoritative site list.
  **No doc delta required.**

## Open questions

(none — all decided.)

## Status

**Done** — 2026-07-23.

- `src/app/ace/app/accent.hpp` (new): `constexpr` chrome-colour vocabulary — `Rgb` triplet aggregate, `rgba(Rgb, uint8_t)` packer, `k_accent{120,200,255}`, `k_camera_frame{255,210,80}`, `accent(a)` / `camera_frame(a)` wrappers, and `k_focus_marker_color = accent(255)` (hoisted from `canvas_view.cpp`).
- `src/app/canvas_view.cpp`: six `IM_COL32(…)` literals at `:42/:352/:353/:526/:527/:554` replaced by `accent()`/`camera_frame()` call sites (byte-identical output); `k_focus_marker_color` moved to header; debt comment at `:39-41` deleted.
- `tests/accent_palette_test.cpp` (new): five Catch2 cases — `the five accent sites reproduce their shipped literals`, `camera-frame hue is the amber, not the accent`, `alpha is the only thing the parameter changes` (runtime alpha to force gcov instrumentation), `rgba packs through IM_COL32`, `the focus marker constant is the opaque accent`; added to `ace_shell_test` via `CMakeLists.txt`.
- `CMakeLists.txt`: `tests/accent_palette_test.cpp` added to `ace_shell_test` source list.
- Fixer also resolved a TSan stall in `src/render/canvas_host.cpp` (pending-resize/camera drop window) and added a regression test in `tests/canvas_host_test.cpp`; both land in this commit.
- No golden re-baseline; no existing test file edited; `check_levels` clean; `ace_shell_test [accent_palette]` + all e2e suites green.
