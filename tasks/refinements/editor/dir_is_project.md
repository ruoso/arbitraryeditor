# editor.project.dir_is_project — Project = directory: New and Save As refuse any existing target

## TaskJuggler entry

`tasks/00-editor.tji:163-168`:

```
task dir_is_project "Project = directory: New and Save As refuse any existing target" {
  effort 1d
  allocate team
  depends !open, !save_as
  note "The project directory IS the project (D16, project-as-a-directory): ..."
}
```

No `complete 100` yet. On completion the closer adds `complete 100` immediately
after `allocate team`, fixes the note's back-link (it currently reads
`Refinement: tasks/refinements/dir_is_project.md` — a flat path; the file lives
at `tasks/refinements/editor/dir_is_project.md`, matching the `editor/` set),
appends this refinement's `## Status` block, propagates to the milestone at
`tasks/99-milestones.tji:8` if this is the last leaf it waits on, and confirms
`tj3 project.tjp 2>&1 | grep -iE "error|warning"` is silent
(`tasks/refinements/README.md:47-78`).

## Effort estimate

**1d.** Two one-line L1 guards plus their contract prose, one new `OpenError`
enumerator + message, one L4 pre-check, a gateway signature change from
`void save_as()` to `bool save_as(parent, name)`, one parameterization of the
already-extracted compose modal, a second modal instance on `Dockspace` — and
then the test work, which is the bulk: three shipped Catch2 cases whose premise
the new rule invalidates, two L4 gateway cases rewritten for the new signature,
one e2e rewritten, and four fake gateways re-signed.

## Inherited dependencies

**Settled (from `editor.project.open`, `tasks/refinements/editor/open.md`).**
`project::create_project(fs, root) -> platform::Result<OpenedProject>`
(`src/project/ace/project/project.hpp:194-195`, impl
`src/project/project_open.cpp:299-338`) scaffolds `assets/`, `workspace/`,
`exports/` and the `workspace/`-excluding `.gitignore`, mints a
workspace-backed `Document`, and writes no `project.arbc` (D-open-4/5).
Errors are **values, not throws** (D-open-6): `platform::Result<T>` carrying a
`std::error_code`, with `OpenError` (`project.hpp:115-120`) as the open/create
vocabulary and `make_error_code(OpenError)` bridging it. `ProjectLayout` +
`project_layout(root)` (`project.hpp:82-93`) are the canonical path arithmetic.
Directory open/create lives in L1 `project` over the existing `platform` edge —
no new component, no new DAG edge (D-open-1).

**Settled (from `editor.project.save_as`,
`tasks/refinements/editor/save_as.md`).**
`project::save_project_as(fs, target_root, doc, registry, post_writer)`
(`src/project/ace/project/save.hpp:134-138`, impl `src/project/save.cpp:190-249`)
re-publishes the live document's portable core into a target root rather than
byte-copying a tree (D-save_as-1); the current session is untouched and the copy
opens in a sibling `exec` (D-save_as-2, D19/A7); the split is one L1 `project`
primitive + one L1 `commands` orchestrator with the L4 gateway driving the
picker (D-save_as-3).

**Reversed by this leaf (the source of debt).** **D-save_as-4** —
"*Refuse to clobber an existing project*… `save_project_as` errors if the target
already holds a `project.arbc`", whose rejected alternative was explicitly "*a
mandatory 'empty directory only' rule (over-restrictive — a target that exists
but has no `project.arbc` is a fine destination)*"
(`tasks/refinements/editor/save_as.md:429-440`). This leaf goes further than
even that rejected alternative: **any** existing target is refused, empty ones
included. The narrow guard is restated in three more places that the
implementation must leave alone — `save_as.md` Constraint 6 (`:263-267`), its
acceptance bullet (`:310-311`) and its parking-lot item (`:453-456`) are the
historical record of why the task existed and are not edited
(`tasks/refinements/README.md:51-56`); only the *code* contract at
`src/project/ace/project/save.hpp:117-131` and `src/project/save.cpp:196-204`
changes.

**Settled (from `editor.project.open_ui`).** The two entry helpers this leaf
reuses rather than reinvents: `project::is_project_directory(fs, root)`
(`project.hpp:203`) and `project::compose_new_project_target(parent, name)`
(`project.hpp:205-210`) — pure path arithmetic, no I/O, `nullopt` for an empty
parent, an empty/blank name, or a name that is not a single path component
(D-open_ui-4).

**Settled (from `editor.project.welcome`, A22).** The compose modal is already
extracted: `dock::NewProjectModal` (`src/dock/ace/dock/dock.hpp:257-274`) plus
one draw routine `dock::draw_new_project_modal` (`dock.hpp:287`, impl
`src/dock/dock.cpp:401-435`), hosted by both `dock::Dockspace`
(`src/dock/dock.cpp:285-292`) and `dock::WelcomeWindow`
(`src/dock/welcome.cpp:69-79,120-122`). Its header comment already names this
leaf as the reason it was extracted (`dock.hpp:249-256`). The L4 gateway is
split into a session-free `app::ProjectEntryGateway`
(`src/app/ace/app/project_gateway.hpp:34-69`) holding the five entry verbs and
`AppProjectGateway final : ProjectEntryGateway` (`:89-210`) adding the session
verbs (A22).

**Pending (this leaf owns them).** What "existing" means and where it is
checked; how a refusal reaches the user for a verb that spawns a *detached*
sibling; and the Save As entry UX, which cannot stay a native folder pick once
existing directories are categorically refused.

## What this task is

Make **creation mean creation**. D16 says the project directory *is* the
project, so the two verbs that bring a project into being — **New Project…**
and **Save As…** — must target a path that does **not yet exist**, and the tool
creates it. Today neither does: `project::create_project` runs an idempotent
`mkdir -p` with no existence check at all (`src/project/project_open.cpp:303-311`),
and `project::save_project_as` refuses only a target that already holds a
`project.arbc` (`src/project/save.cpp:198-204`), so an empty or merely populated
directory is silently adopted and partially overwritten.

This leaf tightens both L1 primitives to refuse **any** existing target
directory, adds an L4 pre-check on the New path so the refusal is visible
*before* a sibling process is spawned, switches **Save As** from the shipped
native-folder-pick UX to the same parent-location pick + typed project **name**
compose flow **New** already uses, and surfaces both refusals as one clear
in-app message through `dock::ProjectGateway`.

## Why it needs to be done

**The two verbs disagree today, and both are wrong.** New composes a
not-yet-existing target (D22) but nothing enforces it — `create_project` will
happily scaffold `assets/`, `workspace/`, `exports/` and a `.gitignore` into
somebody's Documents folder and mint a live mmap arena there. Save As refuses
only a *foreign project*, so it will publish `project.arbc` + `assets/` into a
directory full of unrelated files. Under D16 both are the same category error:
the directory is the project, so writing the project's structure into a
directory that already means something else silently merges two things.

**Refusing only "has a project.arbc" is the wrong predicate.** It answers "would
I destroy another project?" when D16's question is "is this a project directory
I am creating?". Any pre-existing content — even none — means the answer is no,
and an *empty* existing directory refused is what makes New and Save As
symmetric, which is the whole point of the pre-exec decision recorded in the
`.tji` note.

**The Save As UX cannot survive the rule unchanged.** A native folder dialog can
only return a directory that exists. Under the tightened guard the shipped Save
As flow (`src/app/project_gateway.cpp:182-197`) could never succeed. The rule
and the flow have to move together — which is why the `.tji`'s pre-exec decision
pairs them.

**Downstream.** `editor.project.entry_outcome` (`tasks/00-editor.tji:157-161`)
enriches the gateway's `bool` entry returns into a `ProjectEntryOutcome` POD; it
inherits whatever refusal this leaf adds and splits `refused_target` from
`spawn_failed` at that seam. Every future entry host (the WASM port at the same
A12/A3 seam) inherits the rule for free because it lives in the L1 primitives.

## Inputs / context

**Design docs (normative — the constitution).**

- **D16** (`docs/00-design.md:483`, backed by §9 "Files & the project
  directory", `:380-418`) — "*A project is a **directory***… whose canonical
  layout *is* the library's expected on-disk shape, so the editor opens a
  *folder*". §9's verb list is one clause for each: "*New / Open (a project
  folder) / Save (re-dump the snapshot) / Save As (copy the directory)*"
  (`:416-417`).
- **D19** (`:486`) — process-per-project: one process = one project; opening a
  different project is a new `exec`; no in-process switching.
- **D22** (`:489`) — the rail's entry affordances. Load-bearing sentence:
  "***Open** picks an existing project directory through the OS native folder
  dialog… **New** composes a **not-yet-existing** target directory (a chosen
  parent location + a project name) so the sibling's bootstrap create-vs-open
  branch scaffolds it*". Note D22 assigns the native folder dialog to **Open**,
  and compose to **New**; it says nothing about Save As's picker, which is why
  this leaf's switch is a gap-fill rather than a contradiction — see the doc
  delta below.
- **D26** (`:493`) — the welcome offers "*the same parent-location + typed-name
  compose for New*", so "*a launcher and a project window… refuse the same
  targets*". That sentence is a forward reference to exactly this leaf's rule.
- **A7** (`docs/01-architecture.md:369`) / **A12** (`:374`) / **A22** (`:384`) —
  one `Document` per process; project-entry actions dependency-inverted behind
  the `dock`-declared `ProjectGateway` whose only impl is L4 `app` (the sole
  holder of the SDL folder dialog, `ProcessLauncher` and the L1 validate/compose
  helpers); and A22's explicit charge: "*New's compose modal is extracted to a
  dock-local `NewProjectModal` value + one draw function both hosts call… so
  `editor.project.dir_is_project` tightens **one** implementation of 'parent +
  typed name → `new_project`'*".
- **§8** (`docs/01-architecture.md:255-291`) — the levelization DAG. Relevant
  edges: `project` (L1) → base, platform, libarbc; `commands` (L1) → base,
  project, scene; `dock` (L3) → dockmodel, views, imgui **only** (no `platform`,
  no `commands`, no `project`); `app` (L4) → everything, and the only level
  permitted SDL.
- **§9** (`:293-357`) — the layered verification model and the
  encoded-in-every-leaf definition of done.

**Source seams this leaf extends.**

- `src/project/ace/project/project.hpp:115-120` — `enum class OpenError`
  (`NotADirectory = 1, NoProject, CorruptDocument, IoError`); message switch at
  `src/project/project_open.cpp:38-48`.
- `src/project/ace/project/project.hpp:190-195` +
  `src/project/project_open.cpp:299-338` — `create_project`. The scaffold loop
  at `:303-311` is the insertion point; there is **no** existence check before
  it.
- `src/project/ace/project/save.hpp:114-138` +
  `src/project/save.cpp:190-249` — `save_project_as`. The narrow guard is
  `save.cpp:196-204` (`if (fs.exists(layout.canonical))` →
  `std::errc::file_exists`); the header contract to rewrite is `save.hpp:117-131`
  ("*A populated directory WITHOUT a `project.arbc` is a fine destination, so
  the guard is narrow*").
- `src/commands/app_state.cpp:156-174` — `open_or_create_app_state`, whose
  `if (fs.exists(root))` branch means production never reaches `create_project`
  with an existing root; the sole production caller.
- `src/commands/app_state.cpp:192-243` (decl
  `src/commands/ace/commands/app_state.hpp:333-338`) —
  `commands::save_project_as`: empty-target rejection (`:198-200`),
  canonicalization (`:205-212`), publish (`:223-224`), then
  `open_another_project` (`:232-234`). The publish failure already
  short-circuits the exec.
- `src/dock/ace/dock/dock.hpp:88-91` — `virtual bool new_project(parent, name)`;
  `:117-124` — `virtual void save_as()` with the doc comment stating the async
  native-dialog contract this leaf reverses; `:257-274` — `NewProjectModal`;
  `:287` + `src/dock/dock.cpp:401-435` — `draw_new_project_modal`.
- `src/dock/dock.cpp:248-336` — `Dockspace::draw_project_section`: Save
  (`:255-261`), the Save As selectable `###save_as` (`:272-274`), New
  (`:285-292`), Open (`:293-306`), Recent (`:307-324`), the inline feedback
  `ImGui::TextWrapped` (`:325-327`), the shared compose modal (`:332-333`).
  Modal state accessors `dock.hpp:366-374`, members `dock.hpp:448`.
- `src/dock/welcome.cpp:69-79,120-122` — the welcome's New; it has **no** Save
  As (it is session-free) but shares the modal.
- `src/app/project_gateway.cpp:60-70` — `ProjectEntryGateway::new_project`
  (compose → `spawn`, with no existence check); `:79-82` — `pick_folder`;
  `:182-197` — `AppProjectGateway::save_as`, whose own comment records the
  defect: "*a returned error value from the orchestrator is swallowed here (no
  rail feedback channel across the async pick this leaf)*".
- `src/app/ace/app/project_gateway.hpp:34-69` — `ProjectEntryGateway`, including
  the inert `void save_as() override {}` at the session-verb block.

**Test rigs.**

- `tests/project_open_test.cpp` — `:173` create scaffolding, `:454` the
  `open/create surface filesystem faults as IoError values` fault-injection
  sections (`:458`, `:466`, `:474`), `:507`
  `"OpenError messages are populated for every value"` (gates a new enumerator).
- `tests/save_as_test.cpp` — `:121` fresh-root publish, `:164` the clobber
  refusal, `:189` the unwritable-root IoError case (**pre-creates the target as
  a regular file**), `:204` the gitignore-fault case (the fault-injecting
  `FileSystem` pattern to reuse), `:227-320` the `commands::save_project_as`
  cases, `:322` the byte-exact reload golden.
- `tests/app_project_gateway_test.cpp` — `:160` session-free gateway, `:224`
  inert session verbs, `:308` `new_project` composes-and-spawns, `:414`/`:443`
  the two `save_as` cases built on the native pick.
- `tests/save_as_ui_e2e_test.cpp:69,90,101` — `IM_REGISTER_TEST(engine,
  "save_as", "rail_save_as")` driving `###save_as` and asserting
  `gateway.save_as_calls == 1`.
- `tests/open_ui_e2e_test.cpp:97-195` and `tests/welcome_e2e_test.cpp:146-260` —
  the compose-modal refs `"New Project/Name"`, `"New Project/Create"`,
  `"New Project/Cancel"` that must keep working unchanged.
- `tests/save_ui_e2e_test.cpp`, `tests/open_ui_e2e_test.cpp`,
  `tests/welcome_e2e_test.cpp` — fake gateways carrying inert `save_as()`
  overrides that the signature change re-signs.
- `tests/raster_tile_store_test.cpp:328-344` — a `save_project_as` call whose
  target must not pre-exist.
- `tests/commands_test.cpp:74-267` — many `create_project` call sites to audit
  for pre-created roots.

## Constraints / requirements

1. **"Existing" means exists-at-all, and the check is in the L1 primitive.**
   `project::create_project` returns a refusal when `fs.exists(root)` is true —
   whether `root` is an empty directory, a populated non-project directory, an
   existing project, or a regular file — *before* the scaffold loop at
   `src/project/project_open.cpp:303-311`, so a refused create leaves nothing on
   disk. `project::save_project_as` widens `fs.exists(layout.canonical)` to
   `fs.exists(target_root)` at `src/project/save.cpp:196-204`, still returning
   `std::errc::file_exists` and still writing nothing.

2. **`create_project` gains one `OpenError` enumerator, appended.**
   `OpenError::TargetExists` is added at the **end** of the enum
   (`src/project/ace/project/project.hpp:115-120`) so no existing value's
   integer changes, with a message case in `src/project/project_open.cpp:38-48`.
   `save_project_as` keeps `std::errc::file_exists` — two vocabularies on one
   `std::error_code` channel is already the shipped shape (`SaveError` +
   `std::errc` in `save.hpp:33-37`).

3. **The New path pre-checks the target in L4, before spawning.**
   `ProjectEntryGateway::new_project` (`src/app/project_gateway.cpp:60-70`)
   returns `false` when `filesystem_.exists(*target)`, between
   `compose_new_project_target` and `spawn`. New spawns a **detached sibling**
   (D19/A7); a refusal raised only inside the child is invisible to the user.
   The MRU is not touched on a refusal (it already is not on this path).

4. **`dock::ProjectGateway::save_as` becomes a synchronous compose verb.**
   `virtual void save_as() = 0;` (`src/dock/ace/dock/dock.hpp:124`) becomes
   `virtual bool save_as(const std::filesystem::path& parent, const std::string& name) = 0;`,
   returning whether the copy was published and the sibling spawned. Its doc
   comment's async-native-dialog paragraph (`dock.hpp:117-123`) is rewritten.
   `ProjectEntryGateway`'s inert override
   (`src/app/ace/app/project_gateway.hpp`) becomes
   `bool save_as(const std::filesystem::path&, const std::string&) override { return false; }`
   — the launcher still has no session to copy.

5. **The rail's Save As runs the same two-step New runs.** `###save_as`
   (`src/dock/dock.cpp:272-274`) calls `gateway.pick_folder(...)` and, on a
   resolved parent, opens a **second** `NewProjectModal` on `Dockspace`
   (`save_as_modal_`, with accessors mirroring `dock.hpp:366-374`). Its Create
   button calls `gateway.save_as(parent, name)`. The native folder dialog is
   still reached only through `pick_folder` — A12's "sole holder of the SDL
   dialog" statement is unchanged.

6. **One compose-modal implementation, parameterized — not a second copy.**
   `draw_new_project_modal` is generalized to a spec-taking
   `draw_compose_target_modal(NewProjectModal&, std::string& feedback, const ComposeModalSpec&)`
   where `ComposeModalSpec` is a dock-local POD carrying `const char* popup_id`,
   `const char* submit_label` and
   `std::function<bool(const std::filesystem::path&, const std::string&)> submit`.
   `draw_new_project_modal(modal, gateway, feedback)` **stays** as a one-line
   forwarder binding `{"New Project", "Create", …gateway.new_project…}` so the
   `"New Project/Name"`, `"New Project/Create"` and `"New Project/Cancel"` refs
   driven by `tests/open_ui_e2e_test.cpp:163-195` and
   `tests/welcome_e2e_test.cpp:243-257` do not churn. Save As draws with
   `{"Save Project As", "Save Copy", …gateway.save_as…}`.

7. **One refusal string, in the channel the rail already uses.** The compose
   modal's failure text becomes **"Enter a project name that does not already
   exist here."**, replacing `"Enter a valid project name."`
   (`src/dock/dock.cpp:410`). Save As's synchronous `false` also writes
   `Dockspace::project_feedback_` (rendered at `dock.cpp:325-327`), the same
   inline channel Save/Open/Recent already write (`:260`, `:304`, `:320`) — not
   the one-shot modal notice (`dock.cpp:83-124`), whose own doc comment reserves
   it for passive conditions rather than actions the user just took.

8. **Levelization: no new component, no new DAG edge, no new external
   dependency.** `project` gains one `fs.exists` call over its existing
   `platform` edge. `dock` gains only `<functional>` (already included for
   `pick_folder`'s callback) and its own header — no `ace/platform/`,
   `ace/project/`, `ace/commands/`, `arbc/` or SDL. `app` reaches
   `project::compose_new_project_target` and `commands::save_project_as`, both
   already reached. `scripts/check_levels.py` is unmodified.

9. **No behaviour change to Open, Recent, Save, the bootstrap, or the welcome's
   verb set.** `open_project`, `is_project_directory`, `open_recent`,
   `commands::open_another_project`, `RecentProjects` and
   `open_or_create_app_state`'s create-vs-open branch are untouched;
   `save_project` (plain Save, publishing over the live project's own root) must
   **not** inherit the guard — it exists to overwrite its own canonical.
   `WelcomeWindow` gains no Save As.

10. **Shipped tests whose premise the rule invalidates are repaired, not
    deleted.** Each keeps the behaviour it was pinning; only its fixture moves
    off a pre-created target. `tests/save_as_test.cpp:189` in particular must
    keep asserting `SaveError::IoError`, so its fault has to be injected through
    the fault-injecting `FileSystem` (`:204`'s pattern) against a
    **non-existent** target rather than by pre-creating the root as a regular
    file.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md` §9, `:293-357`);
`scripts/gate` green (check_levels · clang-format · build · ctest · coverage) is
the umbrella. Specifically:

- **Levelization (`check_levels` clean).** `src/project/*` includes only
  `<arbc/…>`, `<ace/platform/…>`, `<ace/base/…>` and std; `src/dock/*` includes
  only `ace/dock/…`, `ace/dockmodel/…`, `ace/views/…`, `imgui` and std — the new
  `ComposeModalSpec` carries a `std::function`, not a gateway-external type, and
  introduces no include. No component added, no edge added,
  `scripts/check_levels.py` unmodified. Primary structural assertion.

- **L1 logic — Catch2 unit (the bulk), in `tests/project_open_test.cpp` and
  `tests/save_as_test.cpp`** (both already joined to `ace_tests`), reusing
  `ScratchDir`, header-comment `editor.project.dir_is_project`, sentence-style
  `TEST_CASE`s:
  - **`"create_project refuses a target that already exists, scaffolding nothing"`**
    — `SECTION`s for an **empty** existing directory, a populated non-project
    directory, an existing project directory, and an existing regular file. Each
    asserts `REQUIRE_FALSE(created.has_value())`,
    `CHECK(created.error() == make_error_code(project::OpenError::TargetExists))`,
    and — the load-bearing half — that the target is **byte-for-byte unchanged**:
    no `assets/`, no `workspace/`, no `exports/`, no `.gitignore` appeared, and
    any pre-existing file's contents are intact.
  - **`"create_project still scaffolds a target that does not exist"`** — the
    happy path, guarding against an over-eager guard (extend `:173` rather than
    duplicate it if the fixture already uses a fresh path).
  - **`"OpenError messages are populated for every value"`** (`:507`) extended
    with `TargetExists`; the enumerator is appended so no existing value's
    integer moves.
  - **`"save_project_as refuses ANY existing target directory"`** — the rewrite
    of `:164`, with `SECTION`s for an **empty** existing directory (the case
    that would succeed today — the direct D-save_as-4 reversal witness), a
    populated non-project directory, and a directory holding a foreign
    `project.arbc` (whose bytes are asserted unchanged). Each asserts
    `saved.error() == std::errc::file_exists` and that **nothing** was written
    under the target.
  - **`"save_project_as surfaces an unwritable target root as a SaveError
    value"`** (`:189`) repaired per Constraint 10 — still asserting
    `SaveError::IoError`, now via the fault-injecting `FileSystem` on a
    non-existent target, so the IoError branch keeps its coverage.
  - **`"commands::save_project_as short-circuits a refused target and never
    execs"`** — a new sibling of `:280`: the orchestrator returns
    `std::errc::file_exists`, the `ProcessLauncher` spy records **zero**
    launches, and the current session's dirty state is unchanged (D-save_as-2).
  - Fixture audit: every pre-created target in `tests/save_as_test.cpp:121-333`,
    `tests/raster_tile_store_test.cpp:328-344`,
    `tests/project_open_test.cpp:140-500` and `tests/commands_test.cpp:74-267`
    moves to a fresh path; `tests/project_open_test.cpp:454`'s three
    fault-injection `SECTION`s keep asserting `IoError`, not `TargetExists`.

- **L4 gateway units — Catch2, in `tests/app_project_gateway_test.cpp`**
  (already joined to `ace_tests`):
  - **`"new_project refuses an existing target without spawning"`** — a
    `parent`/`name` composing onto a pre-created directory: `new_project`
    returns `false`, the `ProcessLauncher` spy records zero launches, the MRU is
    unchanged, and the directory is untouched. Pairs with the shipped
    composes-and-spawns case (`:308`).
  - **`"save_as composes parent + name, publishes a copy, and execs a
    sibling"`** — the rewrite of `:414` onto the new signature: no folder dialog
    is opened by `save_as` itself, the composed target now exists and holds
    `project.arbc` + `assets/` + `.gitignore`, one launch is recorded, and it
    returns `true`.
  - **`"save_as refuses an existing target and an invalid name without
    publishing or spawning"`** — the rewrite of `:443`: `false` for a
    pre-existing composed target and `false` for an empty / traversing name
    (`compose_new_project_target` → `nullopt`), with zero writes and zero
    launches in both.
  - **`"a session-free gateway answers every session verb inertly"`** (`:224`)
    extended for the new `save_as(parent, name)` signature returning `false`.

- **Rendered output — golden.** `tests/save_as_test.cpp:322`
  (`"save_project_as's copy reloads and renders byte-exact against the probe
  golden"`) is retained unchanged in intent, with its target moved to a
  not-yet-existing path: the tightened guard must not perturb what a successful
  Save As produces, and the byte-exact `render_offline` compare is the assertion
  that says so. No new golden and no tolerance is introduced.

- **UI e2e — ImGui Test Engine, headless, driven by widget id, in
  `tests/save_as_ui_e2e_test.cpp`** (already joined to `ace_shell_test`),
  rewriting `IM_REGISTER_TEST(engine, "save_as", "rail_save_as")` (`:90`):
  - `ctx->ItemClick(rail_ref("###save_as"))` resolves the fake gateway's
    `pick_folder` to a parent, the `"Save Project As"` popup appears, typing into
    `"Save Project As/Name"` and clicking `"Save Project As/Save Copy"` records
    exactly one `save_as(parent, name)` call with the typed values, and the
    modal closes.
  - A **refusal** pass: the fake gateway returns `false`, the modal stays open,
    and `Dockspace::project_feedback()` reads the refusal string.
  - A **cancel** pass: `"Save Project As/Cancel"` records zero `save_as` calls
    and closes the modal; a cancelled `pick_folder` opens no modal at all.
  - **No churn** in `tests/open_ui_e2e_test.cpp:97-195` or
    `tests/welcome_e2e_test.cpp:146-260` beyond the refusal string and the
    re-signed inert `save_as` override — the `"New Project/…"` refs are
    unchanged by construction (Constraint 6). The New-refusal assertion in
    `tests/open_ui_e2e_test.cpp` asserts the new string.

- **Threading (ASan/TSan).** No new thread, no new shared state: the guards are
  synchronous L1 calls on the UI thread, and `save_project_as` keeps its
  existing `WriterPost` handoff (`src/project/save.cpp:190-249`) unchanged. The
  rewritten `save_as` e2e runs on the `clang-asan` offscreen-GL lane
  (`docs/01-architecture.md` §9.1, `:322-357`) with the rest of
  `ace_shell_test`, and the `commands::save_project_as` units run under the TSan
  lane; both must be clean. The change *removes* a cross-frame async hop (the
  pick callback that published), so the sanitizer surface strictly shrinks.

- **Coverage.** ≥90% diff coverage (`diff-cover --fail-under=90`, `coverage`
  preset) on the changed lines. The branches that must be exercised: both arms
  of `create_project`'s existence guard, both arms of `save_project_as`'s, both
  arms of `ProjectEntryGateway::new_project`'s pre-check, the compose/refuse
  arms of `AppProjectGateway::save_as`, and both submit bindings of the
  parameterized compose-modal draw.

- **Format / build.** `clang-format --dry-run --Werror` clean on all touched
  files; `dev` and `release` presets build clean; `scripts/gate` green.

**No follow-up WBS task is deferred.** The one thing that looks like debt —
that a `bool` return cannot tell the modal *why* the compose failed — is already
a scheduled leaf, `editor.project.entry_outcome` (`tasks/00-editor.tji:157-161`,
0.5d, `depends !welcome`), and it deliberately does **not** split these two
causes either (its POD is `succeeded / refused_target / spawn_failed`, and both
an invalid name and a taken name are `refused_target`), so the single string
this leaf ships is the settled answer, not a stopgap — see
D-dir_is_project-6. Nothing else is deferred.

## Decisions

**D-dir_is_project-1 — "Existing" means exists-at-all, and both creation
primitives check it in L1 before writing anything.** `create_project` refuses
when `fs.exists(root)`; `save_project_as` widens its guard from
`fs.exists(layout.canonical)` to `fs.exists(target_root)`.

*Rationale:* D16 makes the directory *be* the project, so a creation verb whose
target already exists is being asked to *adopt* a directory, not create one —
and adoption is what Open does, through a different verb with a different
validation (`is_project_directory`). Refusing even an empty directory is what
makes New and Save As accept exactly the same targets, which is the pre-exec
decision recorded at `tasks/00-editor.tji:167`; it also deletes a whole class of
judgment call ("is a directory holding only `.DS_Store` empty?") and every
partial-scaffold state a half-adopted directory could leave. Putting the check
in the L1 primitive rather than only in its callers means the invariant travels
with the verb: the WASM port, a future CLI, and `open_or_create_app_state`'s
create branch all inherit it without restating it.

*Alternative rejected:* keep the narrow `project.arbc` guard and merely add "an
empty directory is fine" — the rejected-alternative wording inside D-save_as-4
itself. It leaves New and Save As accepting different targets (New composes a
non-existent path, Save As accepts an existing empty one), which is precisely
the asymmetry this leaf exists to remove, and it puts the tool in the business
of deciding what "empty" means.
*Alternative rejected:* check only in `commands` / the L4 gateway, leaving the
primitives permissive — the guard would then be a property of one call path
rather than of the verb, and the next caller reintroduces the hole.

**D-dir_is_project-2 — `create_project` gets a new appended `OpenError`
enumerator; `save_project_as` keeps `std::errc::file_exists`.** `TargetExists`
joins `OpenError` at the end of the enum with a message case.

*Rationale:* `OpenError` is documented as "one enum, every way open/create can
fail" and is gated by `tests/project_open_test.cpp:507`, so a new failure mode
belongs in it; appending keeps every existing enumerator's integer stable.
`save_project_as` already speaks `std::errc::file_exists` for this exact
condition and one shipped test asserts it — `file_exists` is also the precise
POSIX meaning, and `save.hpp` already runs two vocabularies (`SaveError` +
`std::errc`) over one `std::error_code` channel, so this is house style rather
than an inconsistency.

*Alternative rejected:* unify both onto one new shared error enum — a
cross-component refactor of two error vocabularies with no consumer that needs
to distinguish them, inside a 1d leaf.
*Alternative rejected:* have `create_project` also return
`std::errc::file_exists` — cheaper by one enumerator, but it puts a failure mode
outside the enum that documents `create_project`'s failure modes and outside the
message table that CI checks is total.

**D-dir_is_project-3 — The New path pre-checks the target in L4 before spawning;
the L1 check is the invariant, not the message.** `ProjectEntryGateway::new_project`
returns `false` on an existing composed target, before `spawn`.

*Rationale:* New does not create anything in this process — it spawns a
**detached sibling** (D19/A7) whose bootstrap create-branch scaffolds the
directory. A refusal raised only inside the child is a window that never
appears: the child exits and the user is left staring at an unchanged screen
with no explanation. The pre-check is therefore the entire user-visible half of
the rule on the New path, and it is cheap — the gateway already holds
`filesystem_` and already uses it for `open_project`'s and `recent_projects`'s
validation (`src/app/project_gateway.cpp:52,85`). The L1 check remains, because
between the parent's check and the child's create the directory can appear; L1
is what guarantees nothing is written into it.

*Alternative rejected:* rely solely on the child's `create_project` refusal —
correct on disk, invisible to the user, and it burns a process launch per
mistake.
*Alternative rejected:* have the parent create the target directory itself and
then spawn — it would make the refusal decidable in-process, but it puts project
scaffolding in the launcher/rail process, which is exactly what A22's
zero-`Document` argument and D26's no-litter argument forbid: a failed spawn
would then leave a stray empty project directory behind, the litter D26 exists
to stop.

**D-dir_is_project-4 — Save As switches from the native folder pick to the same
parent-pick + typed-name compose, and its gateway verb becomes synchronous.**
`virtual void save_as()` → `virtual bool save_as(const std::filesystem::path& parent, const std::string& name)`.

*Rationale:* first, forced — a native folder dialog can only return a directory
that exists, so under D-dir_is_project-1 the shipped Save As flow could never
succeed; a UI whose only outcome is refusal is not a UI. Second, correct —
"Save As" under D16 *creates a new project directory*, which is the same act
New performs, so it should be the same gesture with the same validation and the
same refusal string. Third, it repays a defect the shipped code documents
against itself: `src/app/project_gateway.cpp:182-197` notes that the
orchestrator's error value "*is swallowed here (no rail feedback channel across
the async pick)*". Moving the pick to `pick_folder` (which the rail already
drives for New) leaves `save_as` itself synchronous, so its outcome lands in
`project_feedback_` on the same frame — the async gap that swallowed the error
simply stops existing.

*Alternative rejected:* keep the folder pick and refuse every existing
directory it returns — see above; every Save As fails.
*Alternative rejected:* keep the folder pick, treat the picked directory as the
*parent*, and auto-derive a name (`<project> copy`) — one fewer click, but it
silently invents a project name the user never chose and then, on a name
collision, fails for a reason the user cannot see or fix.
*Alternative rejected:* keep `void save_as()` and route the outcome back through
a new one-shot notice — a second feedback mechanism for a verb whose outcome is
now synchronous, contradicting `src/dock/dock.cpp:83-97`'s stated split between
"feedback on an action the user just took" (inline) and "a passive condition"
(modal notice).

**D-dir_is_project-5 — One compose-modal implementation, parameterized by a
dock-local spec; `draw_new_project_modal` survives as a forwarder and
`NewProjectModal` keeps its name.**
`draw_compose_target_modal(NewProjectModal&, std::string& feedback, const ComposeModalSpec&)`
with `{popup_id, submit_label, submit}`; New binds
`{"New Project", "Create", …}`, Save As binds `{"Save Project As", "Save Copy", …}`.

*Rationale:* A22 states the goal outright — "*one implementation of 'parent +
typed name → `new_project`'*" — and this leaf adds a second *submit* to that one
widget rather than a second widget. Keeping `draw_new_project_modal` as a
one-line forwarder makes the refactor invisible to `WelcomeWindow`
(`src/dock/welcome.cpp:120-122`) and to every shipped e2e ref, so no currently
green test churns; that is the same additive-diff tactic D-welcome-7 used when
it extracted the modal in the first place. `NewProjectModal` keeps its name
because Save As *is* composing a new project directory — the value is a parent
path plus a typed name either way, and renaming it would churn A22's prose and
six `Dockspace` accessors for no behavioural gain.

*Alternative rejected:* copy the draw function for Save As — the exact
drift D-welcome-7 removed, and this leaf is the proof that the second host
arrives.
*Alternative rejected:* put a `bool save_as_mode` flag on `NewProjectModal` and
branch inside the draw — the modal value would then know which gateway verbs
exist, whereas a `std::function` submit keeps it a pure parent-plus-text buffer
and lets the third host (WASM, A3) bind whatever it has.

**D-dir_is_project-6 — One refusal string covers both compose failures, and that
is the settled answer rather than a stopgap.** The modal renders "Enter a
project name that does not already exist here." for an invalid name *and* for a
target that exists.

*Rationale:* the gateway seam is a `bool`, and the leaf that enriches it —
`editor.project.entry_outcome` (`tasks/00-editor.tji:157-161`) — collapses both
causes into a single `refused_target`, so even after it lands the modal renders
one string; splitting them would need a third outcome nobody has specified. From
the user's seat the two failures call for the identical corrective act: type a
different name. Keeping the shipped string ("Enter a valid project name.",
`src/dock/dock.cpp:410`) is not an option — after this leaf the *dominant*
failure is a perfectly valid name whose directory happens to exist, and that
message would be actively misleading.

*Alternative rejected:* introduce the outcome POD here — it duplicates
`entry_outcome`'s whole deliverable inside a leaf that does not depend on it,
and whichever landed second would rewrite the other.
*Alternative rejected:* two strings via a second `bool` out-parameter on
`new_project` / `save_as` — a bespoke second channel on a seam that is already
scheduled to grow a proper one.

**D-dir_is_project-7 — Plain Save does not inherit the guard.**
`project::save_project` (`src/project/ace/project/save.hpp:108-112`) keeps
overwriting its own root; only `save_project_as` and `create_project` are
tightened.

*Rationale:* D16 §9 makes Save *"re-dump the canonical `project.arbc`"* over the
live project's existing directory — overwriting an existing target is the entire
function. The guard belongs to the verbs that bring a project into being, and
stating that here keeps the next reader from "fixing" the asymmetry.

### Doc delta (rides in the closer's commit)

**`docs/00-design.md` gains `D27 — Creation targets a path that does not
exist`** (appended after D26 at `docs/00-design.md:493`). The gap is genuine:
D16 §9 lists "*Save As (copy the directory)*" with no destination rule at all,
and D22 states the not-yet-existing + parent-plus-name rule only for **New**
while assigning the native folder dialog to **Open**. Making that rule
categorical (exists-at-all, empty included) and extending it to **Save As** —
which changes a shipped, user-visible gesture — is a UI/UX decision that
constrains every future entry host, so it belongs in the constitution rather
than only in this refinement. No `A<n>` delta is needed: no component, DAG edge,
external dependency or architectural seam changes — `ProjectGateway` remains
A12's L3-declared / L4-implemented inversion with a different signature on one
virtual, and A22's "one implementation of parent + typed name" is honoured, not
amended.

## Open questions

_None blocking — all decided against the constitution._ D16 settles what a
project is and therefore what creating one means; D22 and D26 settle the
parent-plus-name compose gesture and that a launcher and a project window
"*refuse the same targets*"; D19/A7 settle that New spawns rather than creates
in-process, which is what forces the L4 pre-check; A12/A22 settle where the
gateway and its one compose modal live. The only genuine gap — that no D row
stated the exists-at-all rule or gave Save As a destination rule — is closed by
the **D27** delta above.

**Retired, not deferred.** `tasks/refinements/editor/save_as.md:453-456` parked
"*overwrite-with-confirmation UX when the chosen target already contains a
project — which layer confirms and how*". This leaf makes that question moot:
an existing target of any kind is categorically refused, so there is no
overwrite to confirm. It was never actually written into `tasks/parking-lot.md`
(grep is clean), so nothing needs striking — this paragraph is the record that
the question is answered, not shelved.

## Status

**Done** — 2026-07-24.

- `src/project/ace/project/project.hpp`, `src/project/project_open.cpp` — `OpenError::TargetExists` appended + `create_project`'s `fs.exists(root)` guard inserted before the scaffold loop; nothing written on refusal
- `src/project/ace/project/save.hpp`, `src/project/save.cpp` — `save_project_as` guard widened from `layout.canonical` to `target_root`, refusing any existing target directory
- `src/commands/ace/commands/app_state.hpp` — orchestrator contract prose updated for the new seam
- `src/dock/ace/dock/dock.hpp`, `src/dock/dock.cpp` — `save_as(parent, name)` synchronous seam; `ComposeModalSpec` + `draw_compose_target_modal` with `draw_new_project_modal` as one-line forwarder; rail Save As = `pick_folder` → second `NewProjectModal` (`save_as_modal_`) on `Dockspace`; one refusal string
- `src/app/ace/app/project_gateway.hpp`, `src/app/project_gateway.cpp` — L4 pre-check in `new_project` before spawning; `save_as` becomes synchronous compose (no self-opened dialog)
- `docs/00-design.md` — D27 appended (creation targets a path that does not exist; extends D22's not-yet-existing rule to Save As)
- `tests/project_open_test.cpp` — four-`SECTION` `create_project` refusal case + happy-path guard + `TargetExists` message extension
- `tests/save_as_test.cpp` — three-`SECTION` `save_project_as` refusal case; unwritable-root and gitignore-fault cases repaired via `FaultyFileSystem` on non-existent targets; `commands::save_project_as` short-circuit case added
- `tests/app_project_gateway_test.cpp` — `new_project` refusal case; both `save_as` cases rewritten for synchronous signature; session-free inert check extended
- `tests/save_as_ui_e2e_test.cpp` — `rail_save_as` rewritten (cancelled pick → no modal; compose → one recorded `save_as(parent,name)`; refusal → modal stays open + feedback string; Cancel → zero calls)
- `tests/open_ui_e2e_test.cpp`, `tests/welcome_e2e_test.cpp` — refusal string updated; inert `save_as(parent, name)` overrides re-signed in 7 e2e fake gateways
