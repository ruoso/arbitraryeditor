#pragma once

#include <arbc/contract/registry.hpp>

#include <string>
#include <string_view>

namespace ace::commands {

// The kind id of the out-of-lib borrowed-image kind (A29). Named here so callers (the L4
// import verb, the tests) do not hard-code the string and do not need the image archive's
// header — only THIS component (`commands`) links `arbc-plugin-image-impl` (A29), the one
// build/link edge this leaf adds.
inline constexpr const char* image_kind_id = "org.arbc.image";

// Register `org.arbc.image` on `registry` by static-linking `arbc-plugin-image-impl` and
// installing its factory (A29 / D-image-3) — the exact `scene::register_camera_kind` mould,
// first-wins/idempotent like `register_builtin_kinds`. Registering the factory is what turns
// on the runtime image serialize codec (`builtin_codecs(registry)` keys off it), so borrowed
// images round-trip on save/reopen. Called from `register_editor_kinds`, so it is restored on
// both persist sides. The decode dependency (`imdec`) stays PRIVATE to the archive and enters
// no editor TU.
void register_image_kind(arbc::Registry& registry);

// Assemble the opaque `org.arbc.image` config frame from a borrowed asset (D-image-5): the
// authored + resolved borrowed URI and the embedded encoded bytes. The bytes are embedded so
// the image decodes at import; on save the gated codec persists only the URI. A thin wrapper
// over `arbc::image::image_config`, kept here so the image archive stays confined to `commands`.
std::string image_config(std::string_view authored_uri, std::string_view resolved_uri,
                         std::string_view encoded_bytes);

} // namespace ace::commands
