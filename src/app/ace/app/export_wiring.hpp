#pragma once

#include <ace/commands/export.hpp>

// The session aggregate (editor.project.app_state) lives in the L1 `commands`
// core; forward-declared here so this seam does not drag the arbc-owning session
// header into every consumer — the .cpp owns the real include.
namespace ace::commands {
class AppState;
}

namespace ace::app {

// The batch export renderer wiring (editor.cameras.export_pinned / A20), factored out of
// `run_editor` so the SHIPPED closure — not a hand-copied test twin — is the one under
// test. The returned `RenderFn` forwards the batch's ONE pin into libarbc #27's
// caller-pinned `render_offline` (via `render::render_document_srgb8_pinned` and its
// filled-background sibling `_over_pinned`), so every item and the contact sheet render
// one frozen version; the pin rides through as data, so `commands` still never calls
// `render` (Constraint 2). Captures `app_state` by reference: it must outlive the
// `ExportService` the renderer is bound to (Constraint 8, the destruction ordering
// `run_editor` makes explicit).
commands::RenderFn make_export_renderer(commands::AppState& app_state);

} // namespace ace::app
