#include <ace/base/base.hpp>
#include <ace/platform/platform.hpp>
#include <ace/project/project.hpp>

#include <arbc/base/ids.hpp>
#include <arbc/base/transform.hpp>
#include <arbc/kind_solid/solid_content.hpp>
#include <arbc/model/model.hpp>   // DocRoot::find_first_composition (the read seam)
#include <arbc/model/records.hpp> // CompositionRecord::canvas_w / canvas_h
#include <arbc/runtime/document.hpp>

#include <memory>
#include <optional>

namespace ace::project {

const char* name() { return "project"; }

void seed_kind_bridge(arbc::KindBridge& bridge, const arbc::Registry& registry) {
  for (const std::string_view id : registry.ids()) {
    const arbc::KindMetadata* metadata = registry.metadata(id);
    bridge.intern(id, metadata != nullptr ? metadata->version : std::string_view{});
  }
}

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

std::optional<CompositionSize> root_composition_size(const arbc::Document& document) {
  // Pin a version and read the root composition through the intended lock-free
  // reader seam (A4 / D-fit_bounds-1): find_first_composition is libarbc's own
  // lowest-id-wins root rule — the exact anchor the compositor sources its frame
  // walk on, so the fit frames the identical composition that renders.
  const arbc::DocStatePtr state = document.pin();
  if (!state) {
    return std::nullopt;
  }
  arbc::ObjectId root_id;
  const arbc::CompositionRecord* rec = nullptr;
  if (!state->find_first_composition(root_id, rec) || rec == nullptr) {
    return std::nullopt; // no composition — nothing to fit (D-fit_bounds-3).
  }
  if (!(rec->canvas_w > 0.0) || !(rec->canvas_h > 0.0)) {
    return std::nullopt; // degenerate authored size — nothing to fit (Constraint 2).
  }
  return CompositionSize{rec->canvas_w, rec->canvas_h};
}

} // namespace ace::project
