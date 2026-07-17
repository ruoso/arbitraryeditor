# Parking lot — human/legal judgment items (not WBS tasks)

Items surfaced by refinements that a WBS implementer cannot decide and that do
**not** gate any leaf. Reviewed by a human; not scheduled by the orchestrator.

---

## History view real-body owner

**Source:** `tasks/refinements/editor/view_registry.md` (view_registry, 2026-07-17)

The History view type is registered and draws a labeled placeholder. Its real
body is a design judgment call: does it get a dedicated panel implementation, or
does its content fold into `editor.project.undo` (`tasks/00-editor.tji` — the
transaction-journal wiring, which already owns the undo history data)? No new
WBS leaf was created; the choice is parked here for human review before a
downstream panel-content task is scheduled.

---

## Assets view real-body owner

**Source:** `tasks/refinements/editor/view_registry.md` (view_registry, 2026-07-17)

The Assets view type is registered and draws a labeled placeholder. Its real
body is a design judgment call: does it warrant a dedicated asset-browser leaf,
or is it subsumed by the Layers list's referenced-vs-painted surface
(`editor.panels.layers`, per D11)? No new WBS leaf was created; the choice is
parked here for human review before a downstream panel-content task is
scheduled.
