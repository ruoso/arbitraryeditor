# editor.project.open — Open a project directory; create a new project

## TaskJuggler entry

`tasks/00-editor.tji:94-99` — `task open` under `editor.project`. Effort `2d`,
`allocate team`, `depends editor.foundation.render_probe` (`:97`). The `note`
(`:98`) cites **Design D16** and names this refinement. (The `.tji` note
currently points its back-link at the flat `tasks/refinements/project_open.md`;
the real landing path is `tasks/refinements/editor/open.md`, matching the
existing `editor/` refinement set — the closer fixes the note back-link per the
ritual in `tasks/refinements/README.md:57-68`.)

Downstream dependents: `editor.project.app_state` (`:100-104`, `depends !open` —
the single-`Document`-per-process owner), and through it `editor.project.save`
(`:112-116`, the re-dump publish step), `editor.project.exec_new` (`:106-110`),
`editor.project.undo` (`:118-122`), and `editor.project.gc` (`:124-128`).
`editor.canvas.*` all depend transitively on `app_state`, so the whole editor
assumes this leaf can turn a directory into a live `Document`.

## Effort estimate

**2 days** (from the `.tji`). This is the editor's **first filesystem-backed
project I/O** — every prior `Document` was anonymous and in-memory
(`build_probe_document`, `src/project/project.cpp:15-29`). The estimate is
dominated by getting the **map-the-workspace-or-rebuild-from-canonical** open
strategy right against libarbc's two-layer persistence model (workspace file vs.
canonical `project.arbc`), and by exercising the **first workspace-backed
(checkpointable) `Document`** — which spawns the library's live
`HousekeepingThread` (A4) — cleanly under the sanitizer lane. The scaffolding,
the `.gitignore`, and the round-trip units are mechanical once the open strategy
and the `load_document` wiring (Registry + KindBridge + AssetSource) are pinned.
No UI, no dockspace, no editor-owned thread, no save/dump path.

## Inherited dependencies

**Settled (from `editor.foundation.render_probe`).** The `project` L1 component
exists and already owns and constructs a real `arbc::Document` in-process (A1:
real objects, no FFI) — `src/project/ace/project/project.hpp`,
`src/project/project.cpp:15-29` (`build_probe_document`). Its header already
includes `<arbc/runtime/document.hpp>` and its `.cpp` already includes
`<ace/platform/platform.hpp>` (`src/project/project.cpp:2`), so the
`project → platform` edge is wired even though `FileSystem` is not yet consumed.
The **golden harness** is stood up: `tests/golden_support.hpp` (reusable
byte-compare, `ACE_GOLDEN_DIR` at `CMakeLists.txt:181-182`) and the committed
uniform golden `tests/goldens/render_probe_64x64.rgba8` — this leaf **reuses
both** to prove load fidelity. render_probe fixed the scope boundary this leaf
respects: the **interactive/threaded** path (`HostViewport`/
`InteractiveRenderer`/`TileCache`/`WorkerPool`) is deliberately not built here —
it belongs to `editor.canvas.*` (D-render_probe-2).

**Settled (from `editor.foundation.platform_services`).** The injectable I/O
seam `ace::platform::FileSystem` / `NativeFileSystem` exists
(`src/platform/ace/platform/filesystem.hpp:18-61`): `exists` (`:22`),
`list_directory` (`:26`), `read_file` (`:31`), `write_file` (`:35`),
`make_directories` (`:40`, mkdir -p), and D16 `atomic_replace` (`:45`). File ops
return a typed `Result<T>` / `std::error_code` — never throw across the seam
(D-platform_services-6). Crucially the seam header (`:13-17`, D-platform_services-4)
records the split this leaf must honor: `FileSystem` fronts the **editor's own**
file needs (enumerate a project dir, scaffold it, write local state) and
**deliberately does NOT model libarbc's mmap workspace** — the workspace backing
goes through the **library's own** `Document`/workspace API, not through
`PlatformServices`. The seam is injected at L4 bootstrap, not a singleton
(D-platform_services-2). `platform_services.hpp:59-66` already names *this leaf*
as the consumer that "enumerates a project directory before handing paths to
libarbc."

**Settled (from `editor.foundation.build`).** `project` is L1, linking the
library: `ace_component(project DEPENDS base platform LIBS arbc::arbc)`
(`CMakeLists.txt:141`). `check_levels` whitelists `<arbc/...>` includes for
`project` (`scripts/check_levels.py:46`, `EXTERNAL_ALLOWED["arbc"]`) and lists
`project` in **none** of the imgui/sdl/gl sets — so the new library includes
this leaf adds need **no lint edit**, and the L1-core no-ImGui/GL/SDL rule
(`docs/01-architecture.md:176-179`) is a compile-time invariant here.

**Pending (this leaf owns them).** The `open_project` / `create_project`
functions in `project`; the canonical bundle-path resolver; the
map-the-workspace-vs-rebuild-from-canonical strategy; the built-in-kind Registry
+ KindBridge + `FilesystemAssetSource` wiring for `load_document`; the
`.gitignore`-the-workspace scaffold step; and the Catch2 round-trip/error units
plus the load-fidelity golden.

## What this task is

Turn a **project directory** into a live libarbc `Document`, and scaffold a new
one. Per D16 a project is a directory with the library's canonical layout —
`project.arbc` (portable snapshot) + `assets/` (owned bytes) + `workspace/`
(live mmap arena + checkpoints) [+ `exports/`]. **Open** resolves those paths and
either **maps the workspace** (recovers the crash-durable last root — the fast,
durable-by-default path) or, when the workspace is absent or unusable (a fresh
clone, another machine, a truncated file), **rebuilds from the canonical
`project.arbc`** (create a fresh workspace file, `load_document` the canonical
bytes into it, checkpoint). **New** scaffolds the directory (`assets/`,
`workspace/`, `exports/`, a `.gitignore` excluding `workspace/`) and mints a
fresh workspace-backed `Document`. Everything lives in the `project` L1 core:
headless, ImGui/GL/SDL-free, errors-as-values.

This leaf owns the **load** serialize direction only. The **dump** (`Save` =
re-emit `project.arbc` + owned assets) is `editor.project.save`; GC/consolidate
is `editor.project.gc`; single-`Document`-per-process ownership, selection, and
command dispatch are `editor.project.app_state`; the in-app New/Open menu +
folder picker is a deferred UI leaf (below). This leaf produces the `Document`
and the resolved paths; who owns it for the process lifetime is `app_state`'s
scope (A7).

## Why it needs to be done

`editor.project.app_state` depends on this leaf (`tasks/00-editor.tji:103`): the
app owns exactly one `Document` for its whole lifetime (A7/D19), and that
`Document` has to come from *somewhere* — a directory on disk. render_probe
proved the editor can build and render an in-memory `Document`; it did **not**
touch the filesystem, `project.arbc`, `assets/`, or `workspace/`
(`tasks/00-editor.tji:98` frames this leaf as exactly that gap). Every leaf below
`app_state` — the canvas stack, cameras, cells, panels, save, undo, gc — assumes
a real on-disk project has been opened into the process's `Document`. Landing the
directory→`Document` open now, before `app_state`, is what makes "one process =
one project" (A7) a concrete bootstrap rather than a promise, and it is the first
exercise of libarbc's **workspace-backed, checkpointable** `Document` — the
persistence contract (A4) the whole editor rides on.

## Inputs / context

**Design docs (normative — the constitution).**

- `docs/00-design.md` **D16** (`:477`, *"Project = a directory"*) — the governing
  row: a project is a directory with `project.arbc` (portable snapshot, doc-08
  interchange/VCS format) + `assets/` (owned bytes) + `workspace/` (live mmap
  arena + checkpoints, doc 15, same-machine) [+ `exports/`]; **`workspace/` makes
  the project crash-durable by default**; **Save = re-dump `project.arbc`**;
  portable core = `.arbc` + `assets/`; `workspace/` is machine-local scratch,
  **rebuilt from the core**, excluded from sharing/VCS.
- `docs/00-design.md` **§9 prose — "Files & the project directory"** (`:374-412`)
  — the normative detail D16 compresses. The canonical on-disk shape (`:379-385`);
  *"The editor defines this bundle layout and points the library's asset-dir and
  workspace paths at `assets/` and `workspace/`"* (`:391`); the data-file-vs-dump
  framing (`:393-400`); *"`workspace/` … **rebuilt from the canonical core** on
  open if missing or opened on another machine … the editor writes a `.gitignore`
  for it"* (`:402-408`) — **the exact open strategy this leaf implements**; and
  the verb list *"New / Open (a project folder) / Save / Save As; recent
  projects; a dirty indicator …"* (`:410-412`).
- `docs/00-design.md` **D13** (`:474`) — owned-vs-borrowed assets and GC: `assets/`
  is content-addressed owned bytes; borrowed files and `workspace/` are never
  GC'd; a **missing borrow → placeholder + relink** (the library's `load_document`
  already delivers this: an unresolved nested/external ref loads as a placeholder
  and *"never makes a project unopenable"*). GC/consolidate itself is
  `editor.project.gc`, not this leaf.
- `docs/00-design.md` **D19** (`:480`) / **D18** (`:479`) — one process = one
  project (open-another = a new `exec`); layout is local UI state in
  `workspace/`/prefs, never in `project.arbc`. The *last-active per-project layout*
  into `workspace/` is flagged (D21 `:482`, D-workspaces-3) as a later
  `editor.project.*` concern — **not** this leaf; this leaf only opens/creates the
  document, not layout persistence.
- `docs/01-architecture.md` **A4** (`:61-82`, log row `:254`) — the concurrency
  contract adopted verbatim: single-writer/render-thread-confined cache,
  leaf-only dispatch, one shared `WorkerPool`, **one `HousekeepingThread` per
  `Document`** that checkpoints the workspace. This leaf mints the first
  workspace-backed `Document`, so its `create`/`checkpoint`/`open` path is the
  first to run that housekeeping thread — the sanitizer scope below.
- `docs/01-architecture.md` **A6** (`:107-110`) — restates the I/O wiring: *"open
  a project **directory** → the library maps `workspace/`; Save → serialize to
  `project.arbc` + `assets/`; 'Clean up' → `gc_project_directory` (D16)."* This
  leaf is the first clause; the other two are `save`/`gc`.
- `docs/01-architecture.md` **A7** (log row `:257`) — process-per-project; the app
  owns exactly one `Document` for its lifetime; GC root-set is that one document.
  This leaf produces the `Document`; lifetime ownership is `app_state`.
- `docs/01-architecture.md` **§8 / A8** (`:162-179`, log row `:258`) — the
  levelization DAG. `project` is L1 (`:167`), may depend only on `base`,
  `platform`, **libarbc**, and must never include ImGui/GL/SDL (`:176-179`). This
  leaf's path is `app`(L4)→…→`project`(L1)→`platform`/`base`(L0) + libarbc — all
  strictly-downward legal edges, no new edge.
- `docs/01-architecture.md` **§9 / A9** (`:181-208`, log row `:259`) — the layered
  DoD. The *L1 logic* row (`:187`, Catch2 headless) is the bulk here; the
  *Rendered output* row (`:188`, `render_offline` golden) is reused for load
  fidelity; the *Threading & smoke* row (`:190`, ASan/TSan) covers the
  workspace-backed housekeeping thread.

**Library API surface (fetched under `build/dev/_deps/arbc-src/`; the `<arbc/...>`
include roots are the released v0.1.0 surface).**

- `<arbc/runtime/document.hpp>` — the two **file-backed factories** (a
  workspace-backed `Document` is not default-constructible: a file open can fail):
  `static expected<unique_ptr<Document>, WorkspaceFileError> create(const
  std::string& path, DocumentHousekeepingConfig = {})` (`:83`, *"Mint a fresh
  workspace-backed document over a newly created file"*) and `open(const
  std::string& path, …)` (`:84`, *"or recover one from an existing file's last
  durable root"*). A file this build cannot map comes back as a
  **`WorkspaceFileError` value, never a throw** (doc 10). `checkpoint()`
  (`:219`, WRITER-THREAD ONLY) commits the mmap workspace durably;
  `workspace_backed()` (`:222`) reports checkpointability.
- `<arbc/runtime/document_serialize.hpp>` — `load_document(std::string_view bytes,
  Document& doc, KindBridge& bridge, const Registry& registry, std::string
  base_uri = {}, AssetSource* assets = nullptr, …)` (`:217-220`): reads canonical
  `.arbc` bytes into `doc`; *"On any error the document is left unmutated
  (revision 0)"*; *"An unknown kind (no codec) round-trips as a
  `PlaceholderContent`"*; *"A missing widget file never makes a project
  unopenable."* `KindBridge` is default-constructible (`:45-53`). `save_document`
  (`:170`) is the **dump** direction — used here only inside tests to synthesize a
  `project.arbc` fixture, never in `open`'s own code (that path is
  `editor.project.save`).
- `<arbc/builtin_kinds.hpp>` — `void register_builtin_kinds(Registry&)` (`:39`):
  bootstraps the built-in leaf kinds (solid, raster, …) into a registry by direct
  `Registry::add`; deterministic, idempotent, plugin-`extern "C"` path never
  traveled at runtime.
- `<arbc/contract/registry.hpp>` — `class Registry` (`:115`), `Registry() = default`
  (`:116`); the plugin-present witness `load_document` consults for the
  placeholder path.
- `<arbc/runtime/filesystem_asset_source.hpp>` — `FilesystemAssetSource` (`:43`,
  default-constructible, `final : public AssetSource`): the built-in `AssetSource`
  that fetches bytes behind a resolved reference from the filesystem, deduping by
  resolved URI. Passed to `load_document` as `assets`, with `base_uri` = the
  document's own `project.arbc` URI, so any relative asset/nested reference
  resolves against the bundle.

**Source seams this leaf extends.**

- `src/project/ace/project/project.hpp` / `src/project/project.cpp` — the L1
  `project` header/impl. Today it exposes `name()`, the probe geometry, and
  `build_probe_document()`; this leaf **adds** `open_project` / `create_project` +
  the path resolver, includes `<ace/platform/filesystem.hpp>`,
  `<arbc/runtime/document_serialize.hpp>`, `<arbc/builtin_kinds.hpp>`,
  `<arbc/contract/registry.hpp>`, `<arbc/runtime/filesystem_asset_source.hpp>` +
  `<filesystem>`. The `.cpp` already pulls `<ace/platform/platform.hpp>`
  (`src/project/project.cpp:2`).
- `src/platform/ace/platform/filesystem.hpp:18-61` — the injected `FileSystem`
  seam the scaffolder/enumerator consumes (`make_directories`, `atomic_replace`,
  `exists`, `read_file`).
- `CMakeLists.txt:173-183` — `ace_tests` (headless Catch2; already links
  `ace::project ace::render ace::platform … arbc::arbc`) — the round-trip/error
  units and the load-fidelity golden join here.

**Test rigs.**

- `tests/platform_test.cpp` — the `ScratchDir` helper (a temp dir wiped on
  entry/exit); `workspaces.md:215` established it as the filesystem-round-trip
  pattern. This leaf's directory open/create tests reuse `ScratchDir`.
- `tests/golden_support.hpp` + `tests/goldens/render_probe_64x64.rgba8` — the
  reusable byte-compare and the committed uniform golden, reused to assert
  load fidelity through `ace::render`.

## Constraints / requirements

1. **No lint edit; no new component; no new DAG edge.** All new includes are
   `<arbc/...>` (whitelisted for `project`, `scripts/check_levels.py:46`) +
   `<ace/platform/...>` (existing `project → platform` edge) + std. `project`
   stays ImGui/GL/SDL-free (`docs/01-architecture.md:176-179`). If a forbidden
   edge is ever needed, that is an `A<n>` levelization delta — **not expected
   here**.

2. **Respect the FileSystem-vs-libarbc-workspace split (D-platform_services-4).**
   Directory *scaffolding and enumeration* (`make_directories`, the `.gitignore`
   write, `exists`, reading `project.arbc` bytes) go through
   `platform::FileSystem`. The *workspace file and the document* go through
   libarbc's `Document::create`/`open` and `load_document` — **never** through
   `FileSystem` (it deliberately does not model the mmap workspace). `open` reads
   `project.arbc` via `FileSystem::read_file` and hands the *bytes* to
   `load_document`; it hands the *workspace path* to `Document::create`/`open`.

3. **Map the workspace if usable, else rebuild from the canonical doc.** Open
   tries `Document::open(workspace_file)` when the workspace file exists; on a
   missing file **or** a returned `WorkspaceFileError`, it falls back to
   `Document::create(workspace_file)` + `load_document(read(project.arbc), …)` +
   `checkpoint()`. This is the literal D16/§9-prose contract (`:402-408`):
   durable-by-default recovery, rebuilt-from-core when missing or opened on
   another machine. A `project.arbc` that is *also* absent (neither layer present)
   is a "not a project" value; a `project.arbc` that fails to parse is a
   "corrupt document" value.

4. **`open` owns the load direction only — it never writes `project.arbc`.** New
   projects scaffold the directory and mint a fresh workspace-backed `Document`
   (durable by default via `create` + `checkpoint`); the canonical
   `project.arbc` is **not** emitted here — that is `editor.project.save`'s
   publish step. A freshly created project round-trips on reopen through its
   *workspace* file, not through a `project.arbc` that does not yet exist.

5. **Scaffold writes a `workspace/`-excluding `.gitignore`** (`docs/00-design.md:407`)
   via `FileSystem::atomic_replace`, so the machine-local scratch never leaks into
   VCS. Directory creation is `make_directories` (mkdir -p, idempotent): `assets/`,
   `workspace/`, `exports/`.

6. **Errors are values, never throws** (D-platform_services-6). `open_project` /
   `create_project` return a typed `Result<T>` carrying an `OpenError`
   (`NotADirectory`, `NoProject`, `WorkspaceUnusable`→fell back, `CorruptDocument`,
   `IoError`) — a caller (`app_state`) branches on the value. A corrupt/missing
   nested or borrowed asset does **not** fail the open (library `load_document`
   guarantee: null child + doc-05 placeholder, D13/relink).

7. **Synchronous only; no editor-owned thread, no `WorkerPool`.** `open`/`create`
   use the synchronous `create`/`open`/`load_document` (inline decode, null
   `TileDecodeDispatch`)/`checkpoint` path. The library spawns its own
   `HousekeepingThread` per workspace-backed `Document` (A4) — that is
   library-owned; the editor adds no thread and no locking. The interactive/
   threaded path is `editor.canvas.frame_sync`/`multi_canvas`.

8. **KindBridge/Registry/AssetSource are transient to the load.** Build a
   `Registry`, `register_builtin_kinds` it, a default `KindBridge`, and a
   `FilesystemAssetSource`, drive `load_document`, done. Persistent ownership of a
   kind registry (needed once plugin kinds land via the A6 Registry seam, and for
   the save round-trip) belongs to the single-`Document` owner
   `editor.project.app_state` — **not** this leaf. Built-in kinds re-register
   deterministically, so a fresh registry per open is correct today.

## Acceptance criteria

These instantiate the universal DoD (`docs/01-architecture.md §9`);
`scripts/gate` green (check_levels · clang-format · build · ctest) is the
umbrella.

- **Levelization (`check_levels` clean).** `python3 scripts/check_levels.py`
  passes **with no edit**: the new `project` code includes only `<arbc/...>`,
  `<ace/platform/...>`, and std — no ImGui/GL/SDL. No new component, no new DAG
  edge. This is the primary structural assertion.
- **L1 logic — Catch2 unit (the bulk).** A new `tests/project_open_test.cpp`
  (headless, joined to `ace_tests`, `CMakeLists.txt:173-183`), reusing the
  `ScratchDir` helper (`tests/platform_test.cpp`), asserting the observable
  contract:
  - **Scaffold:** `create_project` produces `assets/`, `workspace/`, `exports/`, a
    `.gitignore` naming `workspace/`, and a workspace-backed `Document`
    (`workspace_backed() == true`); no `project.arbc` is written.
  - **Map-the-workspace round-trip:** `create_project` → mutate/`checkpoint` →
    `open_project` returns `rebuilt_from_canonical == false` and a `Document`
    reflecting the checkpointed workspace state.
  - **Rebuild-from-canonical:** with a `project.arbc` fixture (synthesized in the
    test via `arbc::save_document` over a known document) present and `workspace/`
    removed, `open_project` returns `rebuilt_from_canonical == true`, and the
    loaded document has the expected structure.
  - **Workspace-unusable fallback:** a truncated/garbage workspace file *plus* a
    valid `project.arbc` → `open_project` falls back to rebuild
    (`rebuilt_from_canonical == true`), no error (the "opened on another machine"
    resilience).
  - **Error values:** non-directory path → `OpenError::NotADirectory`; empty dir
    (no workspace, no `project.arbc`) → `NoProject`; a corrupt `project.arbc` with
    `workspace/` removed → `CorruptDocument`. All returned, none thrown.
- **Rendered output — load-fidelity golden (reused harness).** A Catch2 case in
  `tests/project_open_test.cpp` builds the probe document, `save_document`s it to a
  scratch `project.arbc`, removes `workspace/`, `open_project`s it
  (rebuild-from-canonical), then `ace::render`-renders the *reopened* document and
  **byte-compares** against the committed `tests/goldens/render_probe_64x64.rgba8`
  via `tests/golden_support.hpp`. This proves the canonical load reconstructs a
  pixel-identical document end-to-end — reusing render_probe's golden, **no new
  golden committed**.
- **UI e2e — ImGui Test Engine N/A (justified).** `open` is headless L1 logic with
  no widget surface; the in-app New/Open menu + native folder picker + recent-
  projects list is deferred to `editor.project.open_ui` (below). The current
  entry points into `open` are the app bootstrap (a launch path / `exec`, A7) and
  `editor.project.exec_new` — neither adds a widget in this leaf. So no ImGui Test
  Engine e2e here; it arrives with `open_ui`.
- **Threading (ASan/TSan) — the first workspace-backed `Document`.** This leaf
  mints libarbc's first *checkpointable* editor `Document` and thus its first live
  `HousekeepingThread` (A4). The `create → checkpoint → open → recover` unit cases
  run under the sanitizer presets (ASan + TSan) and must be clean, exercising the
  writer-thread `checkpoint()` against the background checkpointer. This is a real,
  non-N/A sanitizer scope (render_probe used an anonymous, drain-only-housekeeper
  document; this is the first with a live checkpointer).
- **Coverage.** ≥90% diff coverage on changed lines (`diff-cover --fail-under=90`
  under the `coverage` preset), including the rebuild-fallback and every
  error-value branch.
- **Format/build.** `clang-format --dry-run --Werror` clean; `dev` and `release`
  presets build; `scripts/gate` green.

## Decisions

- **D-open-1 — Directory open/create lives in `project` (L1), consuming the
  existing `platform` edge; no new component or DAG edge.** Scaffolding/enumeration
  goes through `platform::FileSystem` (already a `project` dependency,
  `CMakeLists.txt:141`); the document/workspace goes through libarbc. *Rationale:*
  §7 names `project` as *"libarbc Document, project-dir open/save/gc"* (`:126`),
  and the seam is already wired (`src/project/project.cpp:2`,
  `platform_services.hpp:59-66`). *Alternative rejected:* a new `projectio`
  component or reaching the filesystem from `app` — a gratuitous DAG edge for a
  single call site, and it would pull document I/O out of its designated L1 home.

- **D-open-2 — Canonical bundle layout with an editor-chosen workspace filename
  `workspace/document.arbcws`.** The editor defines the bundle layout and points
  the library's workspace path into `workspace/` (`docs/00-design.md:391`);
  `Document::create`/`open` take a single *file* path, and libarbc mandates no
  name (`Model::create`/`open` accept any path). *Rationale:* a fixed, plainly
  named file inside the plainly named `workspace/` dir honors the "nothing here is
  hidden" principle (`:387-390`) and keeps the whole `workspace/` subtree under
  the one `.gitignore`. *Alternative rejected:* a workspace file at the project
  root, or borrowing a library default — the former breaks the D16 layout and the
  workspace-is-scratch VCS rule; the latter does not exist.

- **D-open-3 — Open = map the workspace if usable, else rebuild from the canonical
  `project.arbc`.** Try `Document::open(workspace_file)`; on missing-file or
  `WorkspaceFileError`, `Document::create` + `load_document(read(project.arbc))` +
  `checkpoint()`. *Rationale:* this is verbatim the §9-prose contract (`:402-408`)
  — durable-by-default crash recovery from the workspace, rebuilt-from-core when
  the workspace is missing or from another machine — and it makes a
  cross-machine clone (which carries only `.arbc` + `assets/`) openable. *Alternative
  rejected:* always reload from `project.arbc` (discards the crash-durable
  workspace and its unsaved-but-checkpointed edits — the exact "lost work" D16
  eliminates) or always trust the workspace (fails on another machine / a
  stale-or-truncated file).

- **D-open-4 — New scaffolds the directory + a fresh workspace-backed `Document`
  but does NOT write `project.arbc`.** *Rationale:* the canonical dump is a publish
  step owned by `editor.project.save` (`:112-116`); the workspace is durable by
  default, so a new project reopens through its workspace file with no
  `project.arbc` on disk yet. Keeping the write/serialize-out path entirely in
  `save` means this leaf carries only the *load* direction — a clean split and a
  smaller surface. *Alternative rejected:* emit an initial `project.arbc` in
  `open` — duplicates the serialize-out path `save` owns and couples open to the
  dump encoder.

- **D-open-5 — Scaffold writes a `workspace/`-excluding `.gitignore` atomically.**
  Via `FileSystem::atomic_replace` (`docs/00-design.md:407`). *Rationale:* the
  workspace is machine-local scratch with no portability promise; excluding it
  from VCS is part of the D16 portable-core-vs-scratch contract, and
  `atomic_replace` is the D16 publish primitive for the editor's own local state.
  *Alternative rejected:* no `.gitignore` (workspace leaks into VCS) or a
  non-atomic `write_file` (a crash mid-write leaves a truncated ignore file).

- **D-open-6 — Errors (and un-openable assets) are values, not throws.**
  `Result<T>` + an `OpenError` enum; a missing/corrupt `project.arbc` or an
  unusable workspace is a returned value the caller branches on. A missing nested
  widget or borrowed asset does **not** fail the open — the library's
  `load_document` loads a placeholder and re-saves the `ref` verbatim (D13
  relink). *Rationale:* matches D-platform_services-6 and doc-10 errors-as-values,
  and D16's "a missing borrow → placeholder + relink" so a broken reference never
  makes a project unopenable. *Alternative rejected:* exceptions across the seam,
  or failing the whole open on a single missing borrow.

- **D-open-7 — Registry + KindBridge + AssetSource are transient to the load;
  persistent kind-registry ownership is `app_state`'s.** `register_builtin_kinds`
  bootstraps built-ins deterministically per open. *Rationale:* the single-
  `Document`-per-process owner (A7, `editor.project.app_state`) is the natural home
  for a lifetime-scoped registry once plugin kinds (A6 Registry seam) and the save
  round-trip need one; over-scoping it into `open` would duplicate that ownership.
  Built-in kinds re-register identically, so a fresh registry per open is correct
  today. *Alternative rejected:* own a persistent registry/bridge in `open` — pre-
  empts `app_state`'s lifetime scope and the not-yet-scheduled plugin Registry seam.

### Named future task (closer registers in WBS)

- **`editor.project.open_ui`** — the in-app **New / Open** affordance: a File-menu
  (or tool-rail) New/Open entry, a **recent-projects** list persisted to the
  per-user prefs store, and a **native folder picker** (which requires a new
  `PlatformServices` dialog faculty — e.g. `SDL_ShowOpenFolderDialog` behind a
  seam, mirroring the `FileSystem` seam), wiring the chosen path to
  `ace::project::open_project` / `create_project` and, for open-another, to
  `editor.project.exec_new` (a new `exec`, A7/D19). Effort **~2d**, `allocate
  team`, `depends editor.project.exec_new, editor.dock.tool_rail`. Milestone:
  under `task project` → `editor.project` → **M9E** (`tasks/99-milestones.tji:6-8`,
  which depends on the `editor.project` container, so a new leaf inside it is
  covered). `note` citing this refinement. This is concrete, agent-implementable
  UI+platform work — the design explicitly calls for *"New / Open (a project
  folder) … recent projects"* (`docs/00-design.md:410`) and no current leaf owns
  it. *(Registered because the picker/menu is a distinct UI surface, not deferred
  core-open work — the core open/create logic ships complete in this leaf.)*

## Open questions

- _None — all decided against the constitution._ D16 + §9-prose fix the directory
  layout and the map-vs-rebuild open strategy; A4/A7 fix that lifetime ownership
  and the shared pool belong to `app_state`/the library (so this leaf stays
  synchronous and thread-free); A6/`save`/`gc` fix that the dump and GC directions
  are other leaves; §8/§9 fix the levelization and the test model. The library API
  is concrete in the fetched v0.1.0 surface (`Document::create`/`open`/`checkpoint`,
  `load_document`, `register_builtin_kinds`, `FilesystemAssetSource`). The one
  editor-chosen degree of freedom — the workspace filename — is settled in D-open-2.
  **No doc delta required:** no new dependency, no new component, no new DAG edge,
  no deviation from a decided behavior.

## Status

**Done** — 2026-07-17.

- `src/project/project_open.cpp` added: `open_project`/`create_project`, `project_layout`, `OpenError` + `make_error_code` (error-code category), transient Registry/KindBridge/AssetSource load wiring; map-workspace-else-rebuild-from-canonical strategy.
- `src/project/ace/project/project.hpp` extended: `ProjectLayout`, `OpenError`, `OpenedProject`, `open_project`/`create_project` declarations, `is_error_code_enum` specialization; new `<ace/platform/filesystem.hpp>` + std includes (whitelisted edges only).
- `src/render/ace/render/render.hpp` / `src/render/render.cpp` extended: extracted `render_document_srgb8(const arbc::Document&, w, h)` from `render_probe_srgb8` (additive, no new deps/edges) so the reopened document can be rendered through `ace::render` for the load-fidelity golden.
- `CMakeLists.txt` updated: `tests/project_open_test.cpp` added to `ace_tests`.
- `tests/project_open_test.cpp` added: Catch2 unit + golden suite — scaffold, map-workspace round-trip, rebuild-from-canonical, workspace-unusable fallback, `NotADirectory`/`NoProject`/`CorruptDocument` error values, injected-fault `IoError` branches, error messages, and load-fidelity render case byte-compared against `tests/goldens/render_probe_64x64.rgba8`. No new golden committed.
- Levelization clean (`check_levels` passed): only `<arbc/...>`, `<ace/platform/...>`, and std includes — no ImGui/GL/SDL, no new component, no new DAG edge.
- All CI lanes green (gate · lint · gcc/clang × debug/release/asan/tsan · coverage + diff-coverage gate).
