#pragma once

#include <arbc/base/ids.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/runtime/document.hpp>

#include <memory>

namespace ace::project {

// The project component. See docs/01-architecture.md §8 (component levelization).
const char* name();

// The render_probe frame geometry — the single source of truth the render
// offline path, the golden (tests/goldens/render_probe_<W>x<H>.rgba8) and the
// app-layer texture all size against.
inline constexpr int k_probe_width = 64;
inline constexpr int k_probe_height = 64;

// The probe's full-frame solid fill: opaque, PREMULTIPLIED linear-light working
// colour (doc 07). Chosen distinct from the shell clear colour (0.10,0.10,0.12,
// src/app/shell.cpp) so the e2e can tell the rendered texture from the
// background (refinement Constraint 6 / D-render_probe-7).
inline constexpr arbc::Rgba k_probe_color{0.0F, 0.5F, 0.0F, 1.0F};

// The trivial probe document: one full-frame solid-colour cell in a composition.
// Owns the anonymous in-process libarbc Document (A1: real objects, no FFI); the
// ids are handed back so render/tests can introspect the built graph.
struct ProbeDocument {
  std::unique_ptr<arbc::Document> document;
  arbc::ObjectId composition;
  arbc::ObjectId layer;
  arbc::ObjectId content;
};

// Build the trivial probe document (GL-free, L1): add a composition, an unbounded
// solid content of k_probe_color, a layer at the identity transform, and attach
// the layer. The offline render (ace::render) drives this straight into a frame.
ProbeDocument build_probe_document();

} // namespace ace::project
