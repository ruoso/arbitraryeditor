#include <ace/base/base.hpp>
#include <ace/platform/platform.hpp>
#include <ace/project/project.hpp>

#include <arbc/base/transform.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/runtime/document.hpp>

#include <memory>

namespace ace::project {

const char* name() { return "project"; }

ProbeDocument build_probe_document() {
  ProbeDocument probe;
  probe.document = std::make_unique<arbc::Document>();
  arbc::Document& doc = *probe.document;

  probe.composition =
      doc.add_composition(static_cast<double>(k_probe_width), static_cast<double>(k_probe_height));
  // Unbounded solid (no Rect): fills the whole device frame regardless of the
  // camera, so the sRGB8 golden is a single exact colour everywhere
  // (D-render_probe-7). Held by the document as an editable Content (A1).
  probe.content = doc.add_content(std::make_shared<arbc::SolidContent>(k_probe_color));
  probe.layer = doc.add_layer(probe.content, arbc::Affine::identity());
  doc.attach_layer(probe.composition, probe.layer);
  return probe;
}

} // namespace ace::project
