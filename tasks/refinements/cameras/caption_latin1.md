# editor.cameras.caption_latin1 — Contact-sheet captions: Latin-1 camera names

## TaskJuggler entry

- **Task:** `editor.cameras.caption_latin1` — *"Contact-sheet captions: Latin-1
  camera names"* (`tasks/00-editor.tji:391-396`), under `task cameras`
  (`tasks/00-editor.tji:306`).
- **Effort:** `0.5d` · `allocate team`.
- **Depends:** `!contact_sheet` — `editor.cameras.contact_sheet`, `complete 100`
  (`tasks/00-editor.tji:384-389`).
- **Note (`.tji:395`):** *"Decode the camera name from UTF-8 to code points
  inside commands, extend the embedded glyph table with the printable Latin-1
  range (U+00A0–U+00FF), drive text_width/fit_text/draw_text off code points
  instead of bytes, and keep the fallback box for anything outside the table.
  Motivation: camera names are free text with no charset constraint
  (editor.cameras.rename_stable_id), and D3/D23's arbitrariness principle argues
  against silently boxing a name the user typed — 'Café' rendering as 'Caf□' is
  a visible defect. Tests: table-driven Catch2 cases over accented names and
  malformed UTF-8, plus one golden with an accented caption. Source-of-debt:
  tasks/refinements/editor/contact_sheet.md. Design: docs/00-design.md D23."*
- **Back-link:** this document, `tasks/refinements/cameras/caption_latin1.md`;
  the closer appends `Refinement: tasks/refinements/cameras/caption_latin1.md`
  to the `.tji` note and adds `complete 100` after `allocate team`
  (`tasks/refinements/README.md:47-68`). **Do not** hand-edit the `.tji` here.
  *(Layout note for the closer: the predecessor is filed at
  `tasks/refinements/editor/contact_sheet.md` and the grandparent at
  `tasks/refinements/editor.cameras/export.md`, while this leaf is filed at
  `tasks/refinements/cameras/` with the other camera leaves, as the orchestrator
  directed. The three-way split of the camera area is pre-existing and not this
  leaf's to normalize.)*
- **Downstream dependents:** none scheduled. Any future consumer of an offline
  text blit — watermarks, slates, a printed shot map from
  `editor.panels.overview` (`.tji:439-441`) — inherits the widened face, but
  nothing depends on this leaf.
- **Milestone:** `m9_editor` (`tasks/99-milestones.tji:8`), reached through the
  `editor.cameras` container dependency — no new milestone wiring, and this leaf
  registers no follow-up that would need any.

## Effort estimate

**0.5 day**, unchanged from the `.tji` and from the budget the predecessor
reserved when it registered this leaf
(`tasks/refinements/editor/contact_sheet.md:538-554`). Nothing structural
happens here: no new file, no new component, no new type, no new API signature,
no layout number changes. The leaf is *one loop's iteration unit* (byte → code
point), *one more data block*, and the tests that pin both.

- **~0.15d — the 96 Latin-1 cells.** The half of the work that looks expensive
  and is not, because **D-latin1-3** fixes a composition rule (base letterform +
  a diacritic row) instead of asking for 96 free-hand 5×7 drawings.
- **~0.1d — the decoder and the two range tests.** `next_code_point` is a
  ~30-line total function; `is_mapped` / `glyph_rows` each gain one branch;
  `fit_text`'s byte walk becomes a code-point walk.
- **~0.25d — the tests.** The bulk, as the DoD intends: the malformed-UTF-8
  decoder table, the table-integrity laws over all 191 cells, the retargeted
  run-collapse case, the ASCII-invariance pin, and the second golden.

## Inherited dependencies

**Settled (consumed as-is):**

- **`editor.cameras.contact_sheet`** (`tasks/refinements/editor/contact_sheet.md`,
  Done 2026-07-23) — every seam this leaf edits:
  - The caption face's public surface — `text_cells`, `text_width`,
    `text_set_bits`, `fit_text`, `draw_text`
    (`src/commands/ace/commands/contact_sheet.hpp:79-100`, defined
    `src/commands/contact_sheet.cpp:120-203`). This leaf changes what these
    *iterate*, never what they *are*.
  - The table's geometry and storage contract — 5×7 face in a 6×8 cell, one byte
    per scanline, five columns in the LOW FIVE BITS, leftmost = bit 4, row 7
    blank because it is the shadow row (`contact_sheet.hpp:57-75`,
    `src/commands/glyphs.cpp:1-14,33-229`). Inherited verbatim; the new block
    obeys the same contract.
  - **D-sheet-4**'s pixel model — 1-bit glyphs, integer pixel replication,
    opaque black shadow at `(+scale,+scale)` then opaque white, no background
    read, no alpha ramp (`contact_sheet.cpp:178-203`). Untouched: widening the
    charset changes which bits are set, not how a set bit is written.
  - **D-sheet-4**'s fallback box, one per unmapped **run**
    (`contact_sheet.cpp:21-31,43-58`, `k_fallback_glyph` at `glyphs.cpp:228-229`)
    — kept, with the run redefined over code points (**D-latin1-4**).
  - **D-sheet-3**'s layout — caption strip height `k_glyph_cell_height *
    caption_scale` and `caption_scale = clamp(tile_edge / 128, 1, 4)`
    (`contact_sheet.cpp:225-226`), the fit at `place_contact_tile`
    (`contact_sheet.cpp:113`) and the draw at `compose_contact_sheet`
    (`contact_sheet.cpp:308`). All unchanged — the accent rows fit inside the
    existing 8-row cell, so **not one layout number moves**.
  - The one golden `tests/goldens/contact_sheet_3cam.rgba8` and its case
    (`tests/contact_sheet_test.cpp:995-1032`), which becomes this leaf's
    regression proof.
- **`editor.cameras.export`** (`tasks/refinements/editor.cameras/export.md`,
  Done 2026-07-23) — **D-export-6**'s `sanitize_stem`
  (`src/commands/export.cpp:74-104`), which drops every non-ASCII byte from a
  *filename* by design and falls back to `camera-<n>` when nothing portable
  survives. That is the path-traversal closure and it **stays as-is**: this leaf
  fixes what the caption *reads like*, not what the file is *called*.
- **`editor.cameras.rename_stable_id`**
  (`tasks/refinements/cameras/rename_stable_id.md`) — camera names are free text
  with **no charset constraint and no validation** beyond an identity guard.
  That is the whole reason this leaf exists.
- **`editor.cameras.model`** — `scene::Camera::name` is a `std::string`
  (`src/scene/ace/scene/camera.hpp:126`) carrying **UTF-8 bytes**: the JSON codec
  decodes `\uXXXX` escapes *into* UTF-8 (`src/scene/camera.cpp:301-311`) and
  emits bytes ≥ 0x80 raw (`src/scene/camera.cpp:177`), and `rename_camera`
  (`src/scene/camera.cpp:530`) passes free text through unchanged. The model's
  own tests already round-trip `"Aé中"` (`tests/camera_model_test.cpp:689-701`),
  so a non-ASCII camera name is not hypothetical — it is a shipped, tested state
  of the document that the caption path renders wrong.

**Pending (owned here):** the UTF-8 decoder in `commands`, the Latin-1 glyph
block, the code-point iteration in the five face functions, the code-point-safe
truncation, the retargeting of the shipped byte-law test, the second golden, and
the one-clause `A21` amendment.

## What this task is

A camera name is free text and reaches the contact sheet as UTF-8 bytes. The
caption face walks those **bytes** against a 95-cell ASCII table
(`src/commands/contact_sheet.cpp:27-58`), so every byte ≥ 0x80 is unmapped and a
maximal run of them collapses into one hollow fallback box: `"Café"` renders as
`Caf□`, `"Ñandú"` as `□and□`. This leaf makes the face walk **code points**
instead. `commands` gains a small total UTF-8 decoder; the embedded table gains a
second contiguous block covering the printable Latin-1 Supplement
(**U+00A0–U+00FF**, 96 cells, 768 bytes, total 1528); `text_cells`, `text_width`,
`text_set_bits`, `fit_text` and `draw_text` iterate decoded code points; and the
fallback box stays exactly where it was — one per maximal run of code points the
table does not cover, now including U+FFFD for ill-formed input.

Nothing else moves. No signature changes, no new file, no new component, no new
external, no runtime font load, no `assets/` directory, no widget, no panel
state, no layout arithmetic. `contact_sheet_layout` and `contact_sheet_cell`
return byte-identical results, and every ASCII caption in the project draws
exactly the pixels it drew before — which is why the shipped golden
`tests/goldens/contact_sheet_3cam.rgba8` must keep passing **unmodified**.

## Why it needs to be done

The predecessor named this defect while shipping it. `src/commands/contact_sheet.cpp:21-26`
carries the admission in the source: *"`editor.cameras.caption_latin1` is the
registered follow-up that decodes code points instead; until it lands, boxing is
the bounded, visible degradation rather than a silent drop."* The `.tji` note and
`tasks/refinements/editor/contact_sheet.md:538-554` register the scope, and
`docs/01-architecture.md:383` (A21) states the limitation as decided behavior —
so this is not opportunistic polish, it is the scheduled retirement of a known
bounded degradation.

The reason it is worth 0.5d is that the editor's core claim is *arbitrariness* —
the composition has no privileged resolution, no privileged kind, no privileged
anything (`docs/00-design.md:13-26,76-86`), and the editor keeps no allowlists
(`.tji:412`). A caption that silently boxes a name the user typed is a
privileged charset in the one place a document's own text reaches an exported
file. D23 (`docs/00-design.md:490`) makes the name *the user's data* — auto-named
`Camera <n>` then *"renamed in the inspector"* — and D-sheet-6 already conceded
that primacy once, letting the sheet's own filename move aside for a camera that
took the stem. Rendering `Café` as `Caf□` in a deliverable the user hands to
someone else is the same concession refused.

Latin-1 specifically, rather than "Unicode", because it is the range where the
cost is a data block and the benefit is most Western European names — and
because everything past it is the parked bundled-font question
(`tasks/parking-lot.md:269-273`), which is a human product call this leaf must
not pre-empt.

## Inputs / context

**Governing design docs (normative — the constitution):**

- **`docs/00-design.md` D23** (`:490`) — minting and naming: a camera is
  auto-named `Camera <n>`, *"renamed in the inspector"*. The name is user data.
- **`docs/00-design.md` D14** (`:481`) and §9 *"Export — a camera is the export
  spec"* (`:357-367`) — the contact sheet is a deliverable that *"falls out for
  free"*, i.e. a file the user hands on. `00-design.md` says **nothing** about
  captions or charsets (the word "caption" does not occur in it), so there is no
  D-row to amend; i18n remains open at `:514`.
- **`docs/00-design.md`** §1 (`:13-26`) and `:76-86` — the arbitrariness
  principle the `.tji` note invokes. *(The note cites this as "D3"; D3 at `:470`
  is actually **Cell kinds**. The principle is prose, not a D-row — cited here
  by line so the implementer does not chase a wrong reference.)*
- **`docs/01-architecture.md` A21** (`:383`) — the governing row, point (3):
  *"Captions are a `constexpr` table, not a dependency … 95 ASCII cells of 5×7
  glyphs in a 6×8 box (760 bytes, `src/commands/glyphs.cpp`) … **Unmapped bytes
  (including every byte of a multi-byte UTF-8 sequence) collapse to one fallback
  box per run.**"* That last clause is what this leaf amends (**D-latin1-6**).
  Its rejected alternative — *"vendoring `stb_easy_font` or a TTF rasterizer …
  a second external, and both emit geometry or antialiased coverage whose grey
  edge pixels are exactly the blend this row removed"* — bounds the design space
  and is **not** reopened here.
- **`docs/01-architecture.md` A20** (`:382`) — the export architecture and the
  one-vendored-encode-dependency ledger. Untouched: this leaf adds no external.
- **`docs/01-architecture.md` §8** (`:256-291`) — the levelization DAG.
  `commands` is L1 and may depend on `{base, project, scene}` only; no
  ImGui/GL/SDL. This is why the caption face is a table at all: *"§8 makes
  `views`/`dock` the only layer that sees ImGui, so a headless L1 job has no font
  atlas to borrow."*
- **`docs/01-architecture.md` §9** (`:293-320`) — the universal definition of
  done that the Acceptance criteria below instantiate.

**Editor seams this leaf extends:**

- `src/commands/ace/commands/contact_sheet.hpp:57-75` — the face's constants and
  `extern` declarations. `k_glyph_first` / `k_glyph_last` are `char` (`:67-68`)
  and become `char32_t`; `k_glyph_count` (`:66`) stays 95; the new
  `k_latin1_*` constants and `k_latin1_glyph_table` are declared beside them.
  Sole readers of the two retyped constants are `contact_sheet.cpp:29-30,38` —
  **no test and no other TU references them** (verified by grep across `src/`
  and `tests/`), so the retype is a two-line blast radius.
- `src/commands/ace/commands/contact_sheet.hpp:79-100` — the five function
  declarations; their signatures do **not** change. The comment at `:77-78`
  (*"one per mapped byte, and one per maximal run of unmapped bytes. `"Café"`
  (5 UTF-8 bytes) is four cells — `C`, `a`, `f`, box"*) is now wrong in its
  reason and must be rewritten.
- `src/commands/glyphs.cpp:33-224` — the ASCII block, **not to be touched**;
  `:228-229` the fallback box, kept; `:32` / `:230` the `clang-format off/on`
  fence that keeps the table readable as a picture (eight literals per line = one
  glyph per line), which the new block sits inside; `:12-14` the TU's
  "pure data, gcov attributes nothing here" note, which stays true.
- `src/commands/contact_sheet.cpp:21-31` — `is_mapped(char)`, its
  `unsigned char` cast, and the comment naming this very task.
- `src/commands/contact_sheet.cpp:34-40` — `glyph_rows(char)`, the single
  indexing site.
- `src/commands/contact_sheet.cpp:43-58` — `for_each_cell`, the **one** iteration
  primitive all five public functions route through. Changing this one loop is
  the leaf.
- `src/commands/contact_sheet.cpp:145-176` — `fit_text`; specifically the byte
  walk at `:162-174` where `take = i + 1` can land mid-sequence
  (**D-latin1-5**).
- `src/commands/contact_sheet.cpp:113,225-226,308` — the caption's callers and
  the strip geometry. Read-only for this leaf; cited to prove nothing there
  changes.
- `scripts/check_levels.py:31` (`"commands": {"base", "project", "scene"}`) and
  `:43-55` (`EXTERNAL_ALLOWED`) — both **unmodified**.
- `CMakeLists.txt:190` (`ace_component(commands DEPENDS base project scene)`,
  globbing `src/commands/*.cpp` at `:153-167`), `:253` (`contact_sheet_test.cpp`
  already in `ace_tests`), `:261-262` (`ACE_GOLDEN_DIR`), `:291`
  (`contact_sheet_e2e_test.cpp` in `ace_shell_test`) — **no CMake edit is
  needed**, including for the new golden.

**Predecessor refinements:**

- `tasks/refinements/editor/contact_sheet.md` — **D-sheet-2** (composition copies,
  never blends), **D-sheet-3** (layout), **D-sheet-4** (the face), and the
  Deferred-WBS-work block at `:538-554` that specified this leaf.
- `tasks/refinements/editor.cameras/export.md` — **D-export-6** (`sanitize_stem`),
  **D-export-11** (the encoder is pinned by `export_camera_64x64.png`, so this
  leaf needs no `.png` golden).
- `tasks/refinements/cameras/rename_stable_id.md` — names are free text.

**Test rigs:**

- `tests/contact_sheet_test.cpp` — the 17 shipped cases. Caption-face cases at
  `:485-526` (pixel law), `:540-573` (truncation + column containment),
  `:576-606` (the byte law this leaf retargets), golden at `:995-1032` with the
  deliberately-overlong third camera name at `:1015`.
- `tests/contact_sheet_e2e_test.cpp` — `IM_REGISTER_TEST(engine, "cameras",
  "contact_sheet_panel")` (`:306`); the rename phase at `:497-504` (which drives
  `scene::rename_camera` directly, because the Inspector ships no rename field
  yet) and the recompose-and-byte-compare at `:470-489`.
- `tests/golden_support.hpp:36` — `compare_golden`, resolving `ACE_GOLDEN_DIR /
  name`, writing a `.actual` sibling on mismatch.
- `tests/canvas_host_test.cpp` — the export TSan anchor, already extended with a
  `contact_sheet=true` variant by the predecessor.

## Constraints / requirements

1. **The public caption API keeps its signatures.** `text_cells`, `text_width`,
   `text_set_bits`, `fit_text`, `draw_text` keep taking `std::string_view` and
   returning what they return. `place_contact_tile` (`contact_sheet.cpp:113`),
   `compose_contact_sheet` (`:308`) and `src/views/views.cpp` are byte-unchanged.
2. **Decoding is total and always advances.** `next_code_point` must terminate on
   every input, including truncated, overlong, surrogate-encoding, out-of-range
   and 0xFE/0xFF bytes, and must never read past `text.size()`. Ill-formed input
   yields U+FFFD per **maximal subpart**, never a hang and never UB.
3. **`glyph_rows` may never index outside a table.** With a `char32_t` argument
   the two range tests are the only mapping; every other value returns
   `k_fallback_glyph.data()`.
4. **Row 7 of every new cell stays 0.** It is the shadow row
   (`contact_sheet.hpp:59-61`); a glyph that sets it puts a caption's shadow
   outside the strip the layout allocated.
5. **No new column.** Every scanline byte satisfies `row & ~0x1F == 0` — the face
   is 5 columns wide and the 6th is the advance.
6. **No layout number moves.** `contact_sheet_layout`, `contact_sheet_cell`,
   `caption_scale` and the strip height are untouched; the accents live inside
   the existing 8-row cell.
7. **No new external, no `assets/`, no runtime font load, no rasterizer.** A20's
   one-vendored-encode-dependency ledger and A21's rejected alternative both
   stand. The face stays compile-time data in `commands`.
8. **Levelization unchanged.** No new component, no new DAG edge, no
   `check_levels` edit, no ImGui/GL/SDL include. `src/base/**`, `src/render/**`,
   `src/gl/**`, `src/dock/**`, `src/views/**` and `src/app/**` are byte-unchanged.
9. **The ASCII block is not edited.** `glyphs.cpp:33-224` keeps its bytes and its
   one-glyph-per-line picture formatting inside the existing `clang-format` fence;
   the new block is appended as a separate array in the same style.
10. **No allocation and no locale in the walk.** No `std::locale`, no `<cwctype>`,
    no `wchar_t`, no floating point — the face must be bit-reproducible across
    platforms and toolchains. `fit_text`'s returned `std::string` stays the only
    allocation in the face.
11. **Existing goldens are not regenerated.** `tests/goldens/contact_sheet_3cam.rgba8`
    and every other `.rgba8`/`.png` golden in `tests/goldens/` keep their bytes.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella.

- **Levelization (`check_levels` clean) — the structural assertion.**
  `scripts/check_levels.py`'s `ALLOWED` (`:21-40`) and `EXTERNAL_ALLOWED`
  (`:43-55`) tables are **unmodified**. `src/commands/glyphs.cpp` gains no
  include beyond its current `<array>` / `<cstdint>`; `src/commands/contact_sheet.cpp`
  gains at most `<cstddef>` (already present). **No new component, no new DAG
  edge, no `check_levels` edit, no ImGui/GL/SDL include, no new external.**
  Verified by `python3 scripts/check_levels.py` (`scripts/gate:17-18`) plus a
  grep showing the only files under `src/` in the diff are
  `src/commands/glyphs.cpp`, `src/commands/contact_sheet.cpp` and
  `src/commands/ace/commands/contact_sheet.hpp`.

- **L1 logic — Catch2 unit (the bulk):** extend
  **`tests/contact_sheet_test.cpp`** (already in the `ace_tests` source list,
  `CMakeLists.txt:253`), keeping the `TEST_CASE("contact_sheet: …")` naming.

  1. **The decoder is total, correct, and always advances.**
     `TEST_CASE("contact_sheet: the UTF-8 decoder is total and always advances")`,
     table-driven over `{bytes, expected code points}`: ASCII; `C3 A9` → U+00E9;
     `E4 B8 AD` → U+4E2D; `F0 9F 98 80` → U+1F600; truncated 2-, 3- and 4-byte
     tails (`C3`, `E4 B8`, `F0 9F 98`); a lone continuation `80`; overlong
     `C0 80` and `E0 80 80`; a surrogate encoding `ED A0 80`; out-of-range
     `F5 80 80 80`; `FE` and `FF`; an embedded NUL; and the empty string. Each
     ill-formed **maximal subpart** yields exactly one U+FFFD. **Anti-vacuity:**
     the loop asserts `pos` strictly increases on every call and that the
     well-formed rows produce *no* U+FFFD, so a decoder that returned U+FFFD for
     everything (or that stalled) fails rather than passes.
  2. **The table covers exactly ASCII ∪ printable Latin-1.**
     `TEST_CASE("contact_sheet: the glyph table covers ASCII and printable Latin-1")`
     — over every code point in `[0x00, 0x0180]` plus U+4E2D, U+FFFD and
     U+1F600, membership is true exactly on `[0x20,0x7E] ∪ [0xA0,0xFF]`. In
     particular **DEL and the C1 controls `[0x7F,0x9F]` stay unmapped**.
     Asserted through the public surface (`text_cells` of the single-code-point
     UTF-8 encoding, mapped ⇒ 1 cell whose rows are not the fallback box).
  3. **Every cell is well-formed.**
     `TEST_CASE("contact_sheet: every glyph cell is well-formed")` — over all
     191 cells and the fallback box: `row & ~0x1F == 0` for every scanline
     (Constraint 5, catches a typo'd 6-bit literal) and `rows[7] == 0`
     (Constraint 4). **Anti-vacuity:** `text_set_bits` of each mapped code point
     is `> 0` **except** U+0020, U+00A0 (NBSP) and U+00AD (SHY), which are
     blank-by-decision (**D-latin1-3**) — so a forgotten row block of zeroes in
     the new array fails here instead of shipping as an invisible caption.
  4. **An accented glyph is distinct from its base letter.**
     `TEST_CASE("contact_sheet: an accented glyph differs from its base letter")`
     — for each family `(A,ÀÁÂÃÄÅ)`, `(E,ÈÉÊË)`, `(I,ÌÍÎÏ)`, `(N,Ñ)`,
     `(O,ÒÓÔÕÖØ)`, `(U,ÙÚÛÜ)`, `(Y,Ýÿ)`, `(C,Ç)` and their lowercase twins, the
     8-row bitmaps differ pairwise. This is the copy-paste detector: a block
     filled by duplicating `E` 96 times passes criteria 2 and 3 and fails here.
  5. **Retarget the shipped byte law.**
     `TEST_CASE("contact_sheet: unmapped bytes render as one fallback box, not
     one per byte")` (`tests/contact_sheet_test.cpp:576-606`) currently pins the
     **byte** rule and must be updated, not deleted:
     - `:583` `text_cells("\xc3\xa9\xc3\xa9") == 1` becomes **`== 2`** (two `é`
       cells) — the single assertion that **fails against today's code and passes
       after**.
     - `:580` `text_cells("Caf\xc3\xa9") == 4` still holds, now because `é` is a
       glyph rather than a box; `:604-605`'s rightmost-lit-pixel bounds must be
       re-derived from the `é` cell's own bits.
     - `:584` (`"\xc3\xa9-\xc3\xa9"` → 3) and `:585` (empty → 0) are unchanged.
     - `:587` (`"a\x7f\x01\x1f"` → 2) is unchanged — DEL and the C0 controls stay
       unmapped and still collapse as one run.
     - The run-collapse law is retargeted to genuinely unmapped code points:
       `text_cells("\xe4\xb8\xad\xe6\x96\x87")` (`"中文"`) `== 1`, and
       `text_cells("a\xe4\xb8\xad-\xe6\x96\x87")` `== 4`. Ill-formed bytes join
       the same run: `text_cells("a\xff\xfeb") == 3`.
  6. **A caption truncates on a code-point boundary.**
     `TEST_CASE("contact_sheet: a caption truncates on a code-point boundary")`
     — over a corpus of accented names and **every** `max_width` in `[0, 200]` at
     scale 1 and 2: the result is well-formed UTF-8 (re-decoding it introduces no
     U+FFFD the input did not have) and `text_width(result, scale) <= max_width`.
     **Anti-vacuity:** at least one width in the sweep must actually truncate
     (result `!= input` and ends with `"..."`).
  7. **ASCII behaviour is bit-for-bit unchanged.**
     `TEST_CASE("contact_sheet: the ASCII face is unchanged by the Latin-1
     extension")` — for a fixed ASCII corpus including `"Hero"`, `"Wide"` and the
     golden's `"A very long camera name that will not fit"`
     (`tests/contact_sheet_test.cpp:1015`), `text_cells` / `text_width` /
     `text_set_bits` / `fit_text` return literal pre-change values, and a
     `draw_text` of each into a scratch image is `memcmp`-identical to a
     reference bitmap built from the same literals. This is the law that catches
     a regression in the shared walk *at the unit level*, not only through the
     golden.
  8. **The pixel laws extend to Latin-1.** The shipped cases at `:485-526`
     (white-pixel count `== text_set_bits(name) * scale * scale`, shadow-only
     black, no third colour) and `:540-573` (nothing outside the caption column
     is touched) are re-run with an accented name at scale 1 and 2. **D-sheet-2**
     and **D-sheet-4** must survive the widening: still no blend, still no alpha
     ramp, still no read of what is underneath.

- **Rendered output — golden:**
  - **`tests/goldens/contact_sheet_3cam.rgba8` is byte-identical.** The shipped
    case (`tests/contact_sheet_test.cpp:995-1032`) passes with the golden file
    **unmodified and not regenerated**. This is the regression proof for
    Constraints 6, 9 and 11 — the diff must contain no change to that file.
  - **New golden `tests/goldens/contact_sheet_latin1.rgba8`** — a 2-camera sheet
    built by the same recipe the shipped golden uses
    (`tests/look_through_test.cpp:170-201` fixture, real
    `render::render_document_srgb8` bound exactly as `src/app/shell.cpp:385-394`
    binds it, `tile_edge = 96`), with cameras named **`"Café Extérieur"`** and
    **`"Ñandú 中"`** — one caption that is now fully drawn plus one mixing
    Latin-1 with a code point outside the table, so the golden pins *both* the
    new glyphs and the surviving fallback box in one image. Compared with
    `ace_test::compare_golden` (`tests/golden_support.hpp:36`), **byte-exact, no
    tolerance**. **Anti-vacuity** (mirroring the shipped case): the sheet is not
    uniformly background, and the caption strip of tile 0 is asserted to contain
    white pixels at columns that are background in a same-geometry sheet
    captioned `"Caf"` — so the accented glyphs are proven to be *drawn*, not
    merely *counted*.
  - **No `.png` golden.** The encoder is already pinned by
    `export_camera_64x64.png` (**D-export-11**); a second one would re-test
    `stb_image_write`, not this leaf.

- **UI e2e — ImGui Test Engine: no new e2e, and no shipped one changes shape
  (justified).** This leaf adds no widget, no panel state, no view and no
  `ProjectGateway` virtual — `src/views/**` is byte-unchanged, so there is no new
  user-drivable behaviour for an e2e to drive. One **existing** phase is
  strengthened instead: `tests/contact_sheet_e2e_test.cpp:497-504` already
  renames a camera through the shipped L1 verb; rename it to **`"Café"`**
  (in addition to the `contact-sheet` stem-collision rename the phase exists
  for, not instead of it) and let the phase's existing recompose-and-byte-compare
  (`:470-489`) run. That proves the UTF-8 name survives the full
  rename → writer → plan → compose → encode → file path, and it simultaneously
  pins that the *filename* is still the sanitized `camera-<n>`/`Caf` form —
  `sanitize_stem` (`src/commands/export.cpp:74-104`) is deliberately unchanged
  (**D-latin1-8**). The e2e runs in the existing offscreen software-GL ASan lane
  (§9.1) with **no new `tests/lsan.supp` suppression**.

- **Threading (ASan/TSan) — explicitly scoped.** This leaf adds **no thread, no
  shared state and no publication**: `run_export`'s sheet phase, the A18 progress
  snapshot and the cancel atomic are untouched, so the shipped
  `tests/canvas_host_test.cpp` TSan anchor (its `contact_sheet=true` variant)
  covers the composition path as-is and `ci-gcc-tsan` must stay clean **without
  extension**. ASan is the lane that matters here: the decoder is the only new
  pointer/index arithmetic in the leaf and malformed UTF-8 is exactly the input
  that overruns a naive one, so `ci-clang-asan` must run criteria 1–5 (the
  truncated-tail and 0xFE/0xFF rows in particular) and the new golden. A decoder
  that reads one byte past a truncated tail fails ASan rather than passing
  silently — that is the intended coverage, and criterion 1's `pos`-advance
  assertion is its non-sanitizer twin.

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`) on changed
  lines; clang-format + build clean. Tests ship with the task. The new data block
  in `src/commands/glyphs.cpp` carries no arcs (the TU note at `glyphs.cpp:12-14`
  already records why), so the diff coverage lands on `next_code_point`, the two
  range tests and the code-point walk in `contact_sheet.cpp` — criteria 1–6
  exercise every branch of all three, including all four sequence lengths, each
  ill-formed class, both mapped ranges and the fallback path. If gcov does
  attribute lines to the data block, `src/commands/glyphs.cpp` gets the exclusion
  treatment the predecessor already provided for, with the note saying why.

- **Doc delta (same commit).** One in-place amendment to **A21**
  (`docs/01-architecture.md:383`), appended to point (3) in the house
  `*(Amended by …)*` form (the shape A14, A15 and A20 already use), copy-paste
  ready:

  > *(Amended by `editor.cameras.caption_latin1`: the table covers **U+0020–U+007E
  > and U+00A0–U+00FF** — 191 cells, 1528 bytes, stored as two contiguous blocks
  > so the original ASCII bytes are untouched — and the caption face iterates
  > **decoded code points**, not bytes: `commands` decodes UTF-8 in-place, an
  > ill-formed maximal subpart decoding to U+FFFD. The fallback box and its
  > **one-per-run** rule stand, now over unmapped **code points** (so `"中文"` is
  > still one box and `"Café"` is now four glyphs). Still no rasterizer, no
  > `assets/` font, no second external — A20's ledger is unchanged.)*

  **No `docs/00-design.md` change:** D14 and D23 say nothing about captions or
  charsets — "caption" does not occur in that document — and this leaf makes a
  name the user already typed render as typed, which changes no UI/UX decision.
  General i18n stays where it is, open at `docs/00-design.md:514`. **No new
  `A<n>` and no new `D<n>`:** A21's structure (constexpr table in `commands`,
  1-bit glyphs, white-over-shadow, one box per unmapped run, no dependency) is
  entirely intact; only its charset clause and its byte count change, and the
  house instrument for that is an in-place amendment, not a new row.

- **Deferred WBS work: (none).** This leaf registers **no** follow-up, and that
  is a decision rather than an omission. Specifically, **no table-growth
  successor is registered** — ranges beyond Latin-1 (Latin Extended-A for
  `Łódź`/`Škoda`, Greek, Cyrillic, CJK) are governed by the parked
  bundled-font question (`tasks/parking-lot.md:269-273`), and scheduling
  "extend the hand-drawn table again" as a WBS leaf would commit the project by
  fiat to the very answer that entry exists to hold for a human. Everything else
  adjacent already has a scheduled owner or a standing decision: the **inspector
  rename field** is `editor.panels.inspector` (cited at
  `tests/contact_sheet_e2e_test.cpp:498-500`); **filename transliteration** is
  refused outright by **D-latin1-8** rather than deferred; **export from the shot
  map** is `editor.panels.overview` (`.tji:439-441`); and the **bundled font** is
  the parking-lot item above, not a task.

## Decisions

- **D-latin1-1 — `commands` decodes UTF-8 to code points internally; the public
  caption API keeps taking `std::string_view`.**
  A small total decoder is added beside the face in
  `src/commands/ace/commands/contact_sheet.hpp` and defined in
  `src/commands/contact_sheet.cpp`:

  ```cpp
  // The code point an ill-formed maximal subpart decodes to. Itself unmapped, so
  // malformed input joins the same fallback run as any other undrawable text.
  inline constexpr char32_t k_replacement_code_point = 0xFFFDU;

  // Decode the scalar at `pos` and advance `pos` past it. Total: an ill-formed
  // sequence yields `k_replacement_code_point` and consumes exactly one maximal
  // subpart, so `pos` strictly increases on every call and the walk terminates.
  char32_t next_code_point(std::string_view text, std::size_t& pos);
  ```

  `for_each_cell` (`contact_sheet.cpp:43-58`) drives it; `is_mapped` and
  `glyph_rows` take `char32_t`. The five public functions keep their signatures.
  *Rationale:* (i) **The input genuinely is a byte string.** `scene::Camera::name`
  is a `std::string` of UTF-8 (`src/scene/ace/scene/camera.hpp:126`,
  `src/scene/camera.cpp:301-311`); a caller holding one should not have to
  transcode to call `draw_text`. (ii) **The blast radius is one loop.** Every
  public function already routes through `for_each_cell`, so decoding there fixes
  measurement, truncation and drawing at once, and `place_contact_tile` /
  `compose_contact_sheet` / `views.cpp` need no edit at all. (iii) **The decoder
  is directly testable** because it is declared, not hidden in the anonymous
  namespace — which is what makes the malformed-input table (criterion 1)
  possible without going through pixels. (iv) **Maximal-subpart substitution** is
  the standard, predictable rule for ill-formed input: it bounds a bad byte's
  damage to itself and makes `pos`-advance a provable law rather than a hope.
  *Alternative rejected:* **change the API to `std::u32string_view` and decode at
  the call site** — five signature changes, a transcode allocation at every
  caller, and the decoding duty pushed onto every future consumer of the face,
  all to move a loop across a function boundary. *Alternative rejected:* **treat
  the name's bytes as Latin-1 directly** (byte 0xE9 → `é`, no decoder) — a
  three-line change that mojibakes every accented name the editor itself wrote:
  the codec emits UTF-8 (`src/scene/camera.cpp:177,301-311`), so `"Café"` saved
  and reloaded would caption as `"CafÃ©"`. The bytes on the wire are UTF-8; the
  face must read them as UTF-8.

- **D-latin1-2 — The table grows by a SECOND contiguous block, U+00A0–U+00FF; the
  shipped 760 ASCII bytes are not touched.**
  `contact_sheet.hpp` gains `k_latin1_first = char32_t{0x00A0}` (NBSP),
  `k_latin1_last = char32_t{0x00FF}`, `k_latin1_glyph_count = 96` and
  `extern const std::array<std::uint8_t, k_latin1_glyph_count *
  k_glyph_cell_height> k_latin1_glyph_table;` (768 bytes, appended to
  `glyphs.cpp` after the fallback box in the same one-glyph-per-line style inside
  the same `clang-format` fence). `k_glyph_first` / `k_glyph_last` are retyped
  `char` → `char32_t` with unchanged values; `glyph_rows` becomes two range tests
  and two index computations.
  *Rationale:* (i) **The ASCII block's diff is empty**, which is the *structural*
  half of "the shipped golden is unchanged" — the other half is criterion 7. A
  reviewer can see at a glance that no existing glyph moved. (ii) **`[0x7F,0x9F]`
  must stay unmapped.** DEL and the C1 controls are not printable; a caption
  should box them, not draw blanks that silently swallow a control character.
  (iii) **Two dense ranges want two dense arrays.** The lookup stays two
  subtractions and a compare — no search, no sortedness invariant, no per-glyph
  key to keep in sync.
  *Alternative rejected:* **one `0x20..0xFF` array with blank cells for
  `0x7F..0x9F`** — 264 wasted bytes *and* it still needs the hole test to keep
  criterion 2's C1 behaviour, so it buys a single subtraction for a worse table.
  *Alternative rejected:* **a sparse `{code point, rows}` table with binary
  search** — a lookup cost, a sortedness invariant to assert, and a key per
  glyph to typo, for two ranges that are contiguous by construction.

- **D-latin1-3 — Every code point in U+00A0–U+00FF gets a real glyph; no holes.
  The letters are composed from their ASCII base plus a diacritic row.**
  All 96 cells are drawn, including the awkward ones (`©`, `®`, `¼`, `½`, `¾`,
  `×`, `÷`, `ß`, `æ`, `ø`, `þ`, `ð`, `µ`, `¶`, `§`, `¿`, `¡`). U+00A0 (NBSP) and
  U+00AD (SHY) are **mapped but blank** — zero ink, full advance — like U+0020,
  because they are space/format characters and drawing anything for them would be
  wrong. The letterforms follow one rule, not 96 independent drawings:
  - an accented **lowercase** letter is its ASCII base bitmap unchanged (the
    x-height forms already occupy rows 2–6 — see `a`, `c`, `e`, `o` at
    `glyphs.cpp:164-193`) with the diacritic on **row 0**;
  - an accented **uppercase** letter is its ASCII base compressed into rows
    **1–6** with the diacritic on **row 0**;
  - **row 7 stays blank in every cell** (Constraint 4).

  *Rationale:* (i) **A hole is the defect this leaf exists to remove.** Leaving
  `©` or `½` boxed means a user who types one still sees `□`, and "some non-ASCII
  works" is harder to explain than either "all of Latin-1 works" or "none does".
  (ii) **It keeps `is_mapped` two range tests** rather than a set-membership
  predicate with its own table to maintain and test. (iii) **The composition rule
  is what fits 96 glyphs into 0.15d** and makes them reviewable: a reviewer
  checks the accent row and that the base matches the ASCII cell, instead of
  proofreading 768 binary literals as free-hand art. It also guarantees
  criterion 4 (accented ≠ base) by construction.
  *Alternative rejected:* **draw only the accented letters and box the symbols**
  — cheaper by perhaps an hour, and it re-introduces a charset cliff at an
  arbitrary line the user cannot predict. *Alternative rejected:* **decompose
  accents at draw time** (draw the base cell, then OR a diacritic row from a
  small accent table) — clever, ~200 bytes smaller, and it needs a
  code-point → (base, accent) mapping table that is itself 96 entries plus new
  branches inside the hot draw loop, i.e. more code and more test surface than
  the data it saves.

- **D-latin1-4 — The fallback box stays, and stays one-per-RUN — but the run is
  now over code points.**
  A21's clause becomes "unmapped **code points** collapse to one fallback box per
  run". `"中文"` still draws one box; `"A中B"` still draws three cells; a
  malformed tail joins the run through U+FFFD.
  *Rationale:* (i) **It is the shipped law**, and this leaf's mandate is to widen
  the charset, not to renegotiate the degradation policy for what remains outside
  it. (ii) **A row of identical boxes is noise, not signal.** One box says "there
  is text here this face cannot draw"; six say the same thing while eating the
  caption's width budget — and it is exactly the names the editor *cannot* draw
  that would get truncated soonest. (iii) It keeps `for_each_cell`'s
  `in_unmapped_run` state machine intact, so the change really is "the iteration
  unit", nothing more.
  *Alternative rejected:* **one box per unmapped code point** — more faithful to
  character count, but it changes a decided behaviour for no user-visible gain
  and widens captions for the worst-served names. *Alternative rejected:*
  **dropping unmappable code points silently** — already refused by **D-sheet-4**
  ("bounded, visible degradation rather than a silent drop"), and it would make a
  CJK-named camera caption as an empty strip.

- **D-latin1-5 — `fit_text` truncates on a code-point boundary.**
  The byte walk at `contact_sheet.cpp:162-174` sets `take = i + 1` per accepted
  *byte*, so a cut can land inside a multi-byte sequence. Today that is harmless
  (every such byte boxes anyway); once decoded it is a real bug — the returned
  caption would carry a truncated sequence, and re-measuring it (`text_width` on
  the fit result, as `tests/contact_sheet_test.cpp:1078` does) would disagree with
  the cell count the truncation computed. The walk advances by decoded code point
  and `take` becomes the byte offset *after* the last accepted one.
  *Rationale:* the fit result is a `std::string` that outlives the call — it is
  stored in `ContactTile::caption` (`contact_sheet.hpp:163`), re-measured and
  drawn later. A function that can emit invalid UTF-8 from valid UTF-8 is a
  defect regardless of whether today's consumers notice.
  *Alternative rejected:* **leave the byte walk and let the truncated tail box**
  — it "works" pixel-wise but makes `fit_text` a UTF-8 corrupter, and the
  `budget - 3` accounting would occasionally be off by one cell.
  **No doc delta** (A21 does not describe truncation).

- **D-latin1-6 — The doc delta is an in-place amendment to A21; no new `A<n>`,
  no `D<n>`.**
  A21's decided structure survives entirely — constexpr table in `commands`, 1-bit
  glyphs, integer replication, white-over-shadow, one box per unmapped run, no
  second external. Only its charset clause ("95 ASCII cells … unmapped **bytes**")
  and its byte count change.
  *Rationale:* the house instrument for a later leaf revising a clause of a
  standing row is the italic `*(Amended by …)*` parenthetical (A14, A15 and A20
  all carry one, the last from this leaf's own predecessor); a new row would
  duplicate A21's reasoning to change two numbers and a noun. And there is no
  `docs/00-design.md` row to amend because the design document never made a
  charset promise to break.
  *Alternative rejected:* **a new `A22` "caption charset" row** — it would split
  one decision across two rows, forcing every future reader of A21 to find A22 to
  learn what the table actually contains.

- **D-latin1-7 — No new file, no new component; the decoder and the extended
  lookup live where the face already lives.**
  Data goes in `src/commands/glyphs.cpp` (still pure data, still no arcs, so the
  coverage story at `glyphs.cpp:12-14` stays true); the decoder and the two range
  tests go in `src/commands/contact_sheet.cpp` beside the readers; declarations go
  in the existing face block of `contact_sheet.hpp:57-100`.
  *Rationale:* two call sites, one component, one narrative comment. §8 is
  unaffected either way, so this is a file-organization question, and the seam
  that already exists wins.
  *Alternative rejected:* **extract `ace/commands/glyphs.hpp` + a `text.cpp`** —
  it splits the header comment that explains *why* captions are a table away from
  the layout it serves, for zero dependency benefit and a CMake/`check_levels`
  re-verification the leaf does not otherwise need.

- **D-latin1-8 — `sanitize_stem` is NOT changed; the caption and the filename are
  deliberately different surfaces.**
  An accented camera name now captions correctly and still produces an ASCII-only
  filename: `sanitize_stem` (`src/commands/export.cpp:74-104`) keeps dropping
  every non-ASCII byte, falling back to `camera-<n>` when nothing portable
  survives.
  *Rationale:* (i) that drop is **D-export-6**'s path-traversal closure — the
  property that a camera name cannot address anything outside the destination
  directory is structural precisely because the portable set is an allowlist;
  (ii) a caption is *pixels the user reads*, a filename is *bytes a filesystem,
  an archive and someone else's OS must accept*, and the two have genuinely
  different constraints; (iii) the `.tji` note scopes this leaf to captions.
  *Alternative rejected:* **transliterate accented letters for filenames**
  (`é` → `e`) — it is a locale-dependent policy (`ö` → `o` or `oe`?), it makes
  two distinct camera names collide into one stem where they did not before, and
  it widens a security-relevant allowlist for cosmetics. Not deferred to a task
  either — this is a decision to *not* do it, not work postponed.

## Open questions

`(none — all decided.)`

One item stays with the human review queue rather than the WBS, and this leaf is
careful not to pre-empt it: **whether the editor should eventually ship a real
bundled font for offline text rendering** — the `assets/ icons, fonts (bundled)`
slot `docs/01-architecture.md:247` reserves and nothing has ever filled. It is
already recorded at `tasks/parking-lot.md:269-273` by the predecessor. This leaf
extends the existing table *within* **D-sheet-4** and A21's standing decision and
takes no position on the successor question; **D-latin1-3**'s composition rule and
**D-latin1-2**'s two-block storage are both chosen so that a future bundled font
replaces the data without disturbing the face's API or its callers.

## Status

**Done** — 2026-07-24.

- `src/commands/ace/commands/contact_sheet.hpp` — retyped `k_glyph_first/last` to `char32_t`; added `k_latin1_first/last/glyph_count/glyph_table` constants and extern; declared `next_code_point` and `k_replacement_code_point`; updated comment at `:77-78`.
- `src/commands/glyphs.cpp` — appended 96-cell `k_latin1_glyph_table` (U+00A0–U+00FF, base+diacritic composition, NBSP/SHY blank) in the same one-glyph-per-line fenced style; ASCII block byte-unchanged.
- `src/commands/contact_sheet.cpp` — added total UTF-8 decoder `next_code_point` (Unicode 3.9 maximal-subpart); `is_mapped`/`glyph_rows`/`for_each_cell` now walk `char32_t` code points over both ASCII and Latin-1 ranges; `fit_text` truncates on a code-point boundary (D-latin1-5).
- `docs/01-architecture.md` — in-place `*(Amended by editor.cameras.caption_latin1 …)*` appended to A21 point (3).
- `tests/contact_sheet_test.cpp` — retargeted byte-law case (now correctly counts `é` as one mapped cell, not one unmapped run); added 8 new Catch2 cases: decoder totality, table coverage, cell well-formedness, accent≠base distinctness, code-point truncation, ASCII invariance, Latin-1 pixel laws, and the new golden case.
- `tests/contact_sheet_e2e_test.cpp` — phase 7 also renames a camera to `"Café"` and byte-compares the recomposed sheet; pins sanitized `Caf.png` filename.
- `tests/goldens/contact_sheet_latin1.rgba8` — new 2-camera golden: `"Café Extérieur"` (all glyphs drawn) + `"Ñandú 中"` (Latin-1 glyphs + one fallback box for CJK).
