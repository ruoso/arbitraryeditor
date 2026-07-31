#include <ace/commands/image_import.hpp>

#include <arbc/base/expected.hpp>
#include <arbc/contract/content.hpp>
#include <arbc/contract/registry.hpp>
#include <arbc/kind_image/image_content.hpp> // the ONLY editor TU that links the image archive (A29)

#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace ace::commands {

void register_image_kind(arbc::Registry& registry) {
  // Factory ONLY (no codec here): the runtime image codec lives in libarbc and is gated on
  // this factory being present, so `builtin_codecs(registry)` folds it in for free — the same
  // plugin-present-witness contract the loadable module's entry uses (image_plugin.cpp:16-20).
  // No insert schema is supplied, so `org.arbc.image` appears in the generic Insert list with
  // the raw-config fallback like any adapter-less kind — A16 forbids hiding it (D-image-6).
  // First-wins, exactly like `register_builtin_kinds` / `register_camera_kind`: a duplicate id
  // is designed idempotency, not an error.
  (void)registry.add(
      arbc::image::ImageContent::kind_id,
      [](arbc::ContentConfig config) { return arbc::image::make_image_content(config); },
      arbc::KindMetadata{"Image", "1"});
}

std::string image_config(std::string_view authored_uri, std::string_view resolved_uri,
                         std::string_view encoded_bytes) {
  return arbc::image::image_config(authored_uri, resolved_uri, encoded_bytes);
}

} // namespace ace::commands
