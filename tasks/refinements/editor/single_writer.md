# editor.canvas.single_writer — Retire doc_mu; render read lock-free via libarbc v0.2.0 COW

## TaskJuggler entry

`tasks/00-editor.tji` — `task single_writer` under `editor.canvas`, `depends
!arbc_v020`. Supersedes `editor.canvas.edit_render_sync` (D-edit_render_sync-1),
which serialized UI-thread edits against the render read with a `doc_mu` mutex held
across the whole render frame.

## What this task is

Retire `doc_mu` — the coarse per-frame lock `CanvasHost` held across all of
`drive_once` and inside every `apply_edit` — now that libarbc v0.2.0 makes the
render-thread document read lock-free.

`edit_render_sync` added `doc_mu` for a real v0.1.0 data race: the render thread's
`bind_operators` → `Document::for_each_content()` walk read the writer-owned
`d_contents` side-map while a UI-thread `add_content` mutated it (a plain
`unordered_map`, no synchronization). It filed the underlying gap upstream as
`ruoso/arbitrarycomposer#10`.

libarbc **v0.2.0** landed that fix (`#10`, closed by `#11`): `d_contents` is now a
`std::atomic<std::shared_ptr<const ContentBindings>>` (`document.hpp:428`);
`add_content` copies-appends-and-atomically-stores (`document.cpp:122-124`); and
`resolve()`/`for_each_content()` do a lock-free `d_contents.load()`
(`document.cpp:246-262`). The render walk now reads a stable snapshot while a
UI-thread edit rebinds — the race is gone in the library, so `doc_mu`'s read guard is
redundant.

## What landed

- **Pin bump** (its own leaf, `editor.canvas.arbc_v020`): `ARBC_GIT_TAG` v0.1.0 →
  v0.2.0 (`CMakeLists.txt:25`). Additive — every 0.1.0 registration compiled
  unchanged; all goldens byte-identical.
- **Retired `doc_mu`** in `src/render/canvas_host.cpp` /
  `src/render/ace/render/canvas_host.hpp`: removed the `std::mutex doc_mu`
  declaration, the `drive_once` hold across the whole iteration, and the `apply_edit`
  wrap. `apply_edit` still runs the mutation synchronously on the calling (writer)
  thread and pokes — the edit-runner **seam** the app binds to is unchanged; only the
  lock is gone.
- **Comment/contract reconciliation** across `src/app/{shell.cpp,canvas_view.cpp}`,
  `src/app/ace/app/{canvas_view.hpp,project_gateway.hpp}`,
  `src/app/project_gateway.cpp`, and the tests that described the `doc_mu` mechanism.
- **Test update** (`tests/canvas_host_test.cpp`): deleted the `doc_mu`
  mutual-exclusion contract test (it asserted the exact freezing that was removed);
  kept the real-pool streamed-edit overlap as the TSan anchor — now a proof of the
  **lock-free** COW read (writer-thread edits ‖ render read, race-free). Comment
  updates in `canvas_view_e2e_test.cpp`, `multi_canvas_e2e_test.cpp`,
  `app_project_gateway_test.cpp`.
- **Design doc** `docs/01-architecture.md` **A4.1**: the single-writer-*identity*
  rule (arbc#7) as a first-class constraint; reads lock-free via `pin()` + the COW
  content-binding snapshot, only *writes* are identity-bound.

## Known-latent residual — a LIBRARY bug (arbc#13)

`apply_edit` keeps every **UI** edit on the writer thread, and the COW read is
lock-free — but the editor is not yet a clean single-writer, because the **library
itself** publishes a structural write from the render thread:
`HostViewport::step()` (render-thread-confined) calls `settle_external_loads` at its
top (`host_viewport.cpp:116-117`), which does `model.transact()` / `add_content` /
`commit()` (`document_serialize.cpp:574-581`). So with a nested external composition
open and live editing, the render-thread settle is a *second* writer identity against
the UI-thread `transact` — exactly what arbc#7 forbids, and something `doc_mu` only
masked (time-serialized) rather than fixed.

The host cannot fix this: it never calls the settle; `HostViewport` owns that call
site. It is **inert on every path exercised today** (no test settles an external load
concurrently with an edit), and it is filed upstream as
`ruoso/arbitrarycomposer#13` — the library must honor its own single-writer contract
(route the arrival install to the writer thread). When #13 lands, the editor adopts
it; there is no in-repo fix and no editor-side leaf, so none is registered.

## Status

**Done** — 2026-07-22.

- Retired `doc_mu` (declaration + `drive_once` hold + `apply_edit` wrap) in
  `src/render/canvas_host.{cpp,hpp}`; `apply_edit` stays the writer-thread edit seam.
- Reconciled the `doc_mu`/mutual-exclusion narrative across app sources and tests;
  deleted the obsolete mutual-exclusion unit test, kept the streamed-edit TSan anchor
  as a lock-free-correctness proof.
- Added `docs/01-architecture.md` A4.1 (single-writer-identity contract); annotated
  `edit_render_sync.md` as superseded.
- Verified on real v0.2.0: full `dev` suite green (192 unit + 44 e2e cases) and the
  hermetic `gcc-tsan` CI lane green (both bundles, no data race).
- Residual render-thread settle writer-identity filed upstream as
  `ruoso/arbitrarycomposer#13` (library-owned; latent on current paths).
