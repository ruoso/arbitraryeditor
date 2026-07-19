#include <ace/project/project.hpp> // project::seed_kind_bridge
#include <ace/scene/camera.hpp>

#include <arbc/base/expected.hpp>
#include <arbc/base/time.hpp> // arbc::TimeRange
#include <arbc/contract/registry.hpp>
#include <arbc/model/model.hpp>   // DocRoot readers + Model::Transaction
#include <arbc/model/records.hpp> // LayerRecord
#include <arbc/runtime/document.hpp>
#include <arbc/runtime/document_serialize.hpp> // arbc::KindBridge

#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ace::scene {

// ---------------------------------------------------------------------------
// CameraContent — the non-rendering org.arbc.camera Content (A14, D-model-1).
// ---------------------------------------------------------------------------

CameraContent::CameraContent(std::string name, Resolution resolution) {
  // Mint the initial version and adopt it as the live base, so the camera has state
  // the moment it exists: `Document::add_content` calls `capture()` and records that
  // handle in the same transaction, giving the first rename a real undo `before`
  // (document.cpp:99-111). Single-threaded construction — the lock is for uniformity.
  const std::lock_guard<std::mutex> lock(d_mutex);
  d_base = arbc::StateHandle{mint_version(std::move(name), resolution)};
}

arbc::SlotIndex CameraContent::mint_version(std::string name, Resolution resolution) {
  arbc::SlotIndex slot;
  if (!d_free.empty()) {
    slot = d_free.back();
    d_free.pop_back();
    d_versions[slot] = Version{std::move(name), resolution, 0};
  } else {
    slot = static_cast<arbc::SlotIndex>(d_versions.size());
    d_versions.push_back(Version{std::move(name), resolution, 0});
  }
  return slot;
}

arbc::StateHandle CameraContent::capture() { return d_base; } // WRITER-THREAD ONLY

void CameraContent::restore(arbc::StateHandle state) {
  // The undo/redo path (document's `EditableRestoreSink`): adopt the navigated-to
  // version as the live base. WRITER-THREAD ONLY, so `d_base` needs no lock (only the
  // payload store is touched cross-thread, by `release` on the drain thread).
  d_base = state;
}

std::size_t CameraContent::state_cost(arbc::StateHandle state) const {
  const std::lock_guard<std::mutex> lock(d_mutex);
  if (!state.has_state() || state.slot >= d_versions.size()) {
    return 0;
  }
  return d_versions[state.slot].name.size() + sizeof(Resolution);
}

void CameraContent::retain(arbc::StateHandle state) {
  if (!state.has_state()) {
    return;
  }
  const std::lock_guard<std::mutex> lock(d_mutex);
  if (state.slot < d_versions.size()) {
    ++d_versions[state.slot].refcount;
  }
}

void CameraContent::release(arbc::StateHandle state) {
  // The DRAIN-THREAD half of the contract: a reclaimed record drops its pin. When the
  // last pin falls the version is unreachable (no live or undo-reachable record names
  // it), so recycle its slot — the base is always pinned, so it is never reclaimed
  // while live (mirrors `RasterStore::release_version`).
  if (!state.has_state()) {
    return;
  }
  const std::lock_guard<std::mutex> lock(d_mutex);
  if (state.slot >= d_versions.size()) {
    return;
  }
  Version& version = d_versions[state.slot];
  if (version.refcount == 0) {
    return; // defensive: never underflow (a release never precedes its retain)
  }
  if (--version.refcount == 0) {
    version = Version{}; // drop the payload; the slot is now free to reuse
    d_free.push_back(state.slot);
  }
}

void CameraContent::set_name(arbc::Model::Transaction& txn, arbc::ObjectId self,
                             std::string new_name) {
  arbc::StateHandle after;
  {
    const std::lock_guard<std::mutex> lock(d_mutex);
    Resolution resolution;
    if (d_base.has_state() && d_base.slot < d_versions.size()) {
      resolution = d_versions[d_base.slot].resolution; // rename preserves resolution
    }
    after = arbc::StateHandle{mint_version(std::move(new_name), resolution)};
  }
  d_base = after; // adopt the new version; the commit retains it and journals the prior
  txn.set_content_state(self, after);
}

std::string CameraContent::camera_name() const {
  const std::lock_guard<std::mutex> lock(d_mutex);
  if (!d_base.has_state() || d_base.slot >= d_versions.size()) {
    return {};
  }
  return d_versions[d_base.slot].name;
}

Resolution CameraContent::resolution() const {
  const std::lock_guard<std::mutex> lock(d_mutex);
  if (!d_base.has_state() || d_base.slot >= d_versions.size()) {
    return {};
  }
  return d_versions[d_base.slot].resolution;
}

std::optional<arbc::Rect> CameraContent::bounds() const {
  // An EMPTY rect: the compositor culls the layer before it ever calls `render`, so a
  // camera contributes zero pixels (Constraint 5). Not `nullopt` — that would be
  // UNBOUNDED (paints everywhere), the opposite of what a non-rendering kind wants.
  return arbc::Rect{0.0, 0.0, 0.0, 0.0};
}

arbc::Stability CameraContent::stability() const { return arbc::Stability::Static; }

std::optional<arbc::TimeRange> CameraContent::time_extent() const { return std::nullopt; }

std::optional<arbc::RenderResult>
CameraContent::render(const arbc::RenderRequest& request,
                      std::shared_ptr<arbc::RenderCompletion> /*done*/) {
  // Never reached in practice (the empty `bounds()` culls the layer), but a valid
  // inline settlement that writes NO pixels keeps the invariant even if a caller
  // renders it directly: the target is left untouched.
  return arbc::RenderResult{request.scale, true};
}

// ---------------------------------------------------------------------------
// Codec params grammar: a flat JSON object `{"name":<string>,"width":<int>,
// "height":<int>}`. Hand-rolled (no JSON library in `scene`'s link surface): the
// core re-parses + canonicalizes whatever `serialize` emits, so this only needs to
// emit valid JSON and parse the canonical form back (D-model-2, Route A KindCodec).
// ---------------------------------------------------------------------------

namespace {

// Append `text` to `out` as a JSON string literal (RFC 8259 escaping).
void append_json_string(std::string& out, std::string_view text) {
  out.push_back('"');
  for (const char ch : text) {
    switch (ch) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(ch) < 0x20) {
        static const char* k_hex = "0123456789abcdef";
        out += "\\u00";
        out.push_back(k_hex[(static_cast<unsigned char>(ch) >> 4) & 0xF]);
        out.push_back(k_hex[static_cast<unsigned char>(ch) & 0xF]);
      } else {
        out.push_back(ch);
      }
    }
  }
  out.push_back('"');
}

// A minimal JSON cursor over a flat `{string: string|number}` object — enough for the
// camera params. Errors are values (a bool return); no exceptions cross the codec.
struct JsonCursor {
  std::string_view text;
  std::size_t pos = 0;

  void skip_ws() {
    while (pos < text.size() &&
           (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r')) {
      ++pos;
    }
  }
  bool consume(char expected) {
    skip_ws();
    if (pos < text.size() && text[pos] == expected) {
      ++pos;
      return true;
    }
    return false;
  }
  bool peek(char expected) {
    skip_ws();
    return pos < text.size() && text[pos] == expected;
  }

  // Parse a JSON string literal into `out` (unescaping). Assumes the opening quote is
  // the next non-ws char.
  bool parse_string(std::string& out) {
    if (!consume('"')) {
      return false;
    }
    out.clear();
    while (pos < text.size()) {
      const char ch = text[pos++];
      if (ch == '"') {
        return true;
      }
      if (ch == '\\') {
        if (pos >= text.size()) {
          return false;
        }
        const char esc = text[pos++];
        switch (esc) {
        case '"':
          out.push_back('"');
          break;
        case '\\':
          out.push_back('\\');
          break;
        case '/':
          out.push_back('/');
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          if (pos + 4 > text.size()) {
            return false;
          }
          unsigned code = 0;
          for (int i = 0; i < 4; ++i) {
            const char hx = text[pos++];
            code <<= 4;
            if (hx >= '0' && hx <= '9') {
              code |= static_cast<unsigned>(hx - '0');
            } else if (hx >= 'a' && hx <= 'f') {
              code |= static_cast<unsigned>(hx - 'a' + 10);
            } else if (hx >= 'A' && hx <= 'F') {
              code |= static_cast<unsigned>(hx - 'A' + 10);
            } else {
              return false;
            }
          }
          // Encode the (BMP) code point as UTF-8 — the camera name round-trips bytes.
          if (code < 0x80) {
            out.push_back(static_cast<char>(code));
          } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          }
          break;
        }
        default:
          return false;
        }
      } else {
        out.push_back(ch);
      }
    }
    return false; // unterminated
  }

  // Parse a non-negative integer literal into `out`.
  bool parse_int(int& out) {
    skip_ws();
    const std::size_t start = pos;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
      ++pos;
    }
    if (pos == start) {
      return false;
    }
    long value = 0;
    for (std::size_t i = start; i < pos; ++i) {
      value = value * 10 + (text[i] - '0');
      if (value > 1'000'000'000L) { // guard: resolutions are device-pixel sized
        return false;
      }
    }
    out = static_cast<int>(value);
    return true;
  }
};

// Parse the canonical params object into (name, resolution). Returns false on any
// malformed input — the codec surfaces that as an error value, never a throw.
bool parse_camera_params(std::string_view params_text, std::string& name, Resolution& resolution) {
  bool have_name = false;
  bool have_width = false;
  bool have_height = false;
  JsonCursor cur{params_text, 0};
  if (!cur.consume('{')) {
    return false;
  }
  if (cur.peek('}')) { // empty object is not a valid camera
    return false;
  }
  while (true) {
    std::string key;
    if (!cur.parse_string(key)) {
      return false;
    }
    if (!cur.consume(':')) {
      return false;
    }
    if (key == "name") {
      if (!cur.parse_string(name)) {
        return false;
      }
      have_name = true;
    } else if (key == "width") {
      if (!cur.parse_int(resolution.width)) {
        return false;
      }
      have_width = true;
    } else if (key == "height") {
      if (!cur.parse_int(resolution.height)) {
        return false;
      }
      have_height = true;
    } else {
      return false; // unknown field — the camera grammar is closed
    }
    if (cur.consume(',')) {
      continue;
    }
    break;
  }
  if (!cur.consume('}')) {
    return false;
  }
  return have_name && have_width && have_height;
}

std::string serialize_camera_params(const CameraContent& camera) {
  std::string out = "{\"name\":";
  append_json_string(out, camera.camera_name());
  out += ",\"width\":";
  out += std::to_string(camera.resolution().width);
  out += ",\"height\":";
  out += std::to_string(camera.resolution().height);
  out.push_back('}');
  return out;
}

} // namespace

void register_camera_kind(arbc::Registry& registry) {
  arbc::KindCodec codec;
  codec.kind_version = CameraContent::kind_version;
  codec.serialize = [](const arbc::Content& content) -> arbc::expected<std::string, std::string> {
    const auto* camera = dynamic_cast<const CameraContent*>(&content);
    if (camera == nullptr) {
      return arbc::unexpected<std::string>("org.arbc.camera: content is not a CameraContent");
    }
    return serialize_camera_params(*camera);
  };
  codec.deserialize = [](std::string_view params_text, std::span<const arbc::ContentRef> /*inputs*/,
                         arbc::ObjectId /*composition*/)
      -> arbc::expected<std::unique_ptr<arbc::Content>, std::string> {
    std::string name;
    Resolution resolution;
    if (!parse_camera_params(params_text, name, resolution)) {
      return arbc::unexpected<std::string>("org.arbc.camera: malformed params");
    }
    return std::unique_ptr<arbc::Content>(
        std::make_unique<CameraContent>(std::move(name), resolution));
  };

  // Factory: the plugin-present witness `builtin_codecs(registry)` keys off. The camera
  // is authored through `add_camera`, never a `ContentConfig` string, so the factory is
  // a benign default-construct (config ignored) — enough to mark the kind registered.
  arbc::ContentFactory factory = [](arbc::ContentConfig /*config*/)
      -> arbc::expected<std::unique_ptr<arbc::Content>, std::string> {
    return std::unique_ptr<arbc::Content>(std::make_unique<CameraContent>("", Resolution{}));
  };

  // First-wins (like `register_builtin_kinds`): a duplicate id is designed idempotency,
  // not an error — a session that registers the kind twice keeps the first.
  (void)registry.add(CameraContent::kind_id, std::move(factory),
                     arbc::KindMetadata{"Camera", CameraContent::kind_version}, std::move(codec));
}

namespace {

// The `ContentRecord.kind` token for `org.arbc.camera`, computed from a bridge seeded
// identically to `project::save_project`'s (via `project::seed_kind_bridge`) so the
// token stored on a freshly-created camera matches what the save-side bridge resolves
// back to the kind string (D-model-2). `registry` must have `register_camera_kind`
// applied (else the token is a meaningless post-builtin value and the save skips it).
std::uint64_t camera_token(const arbc::Registry& registry) {
  arbc::KindBridge bridge;
  project::seed_kind_bridge(bridge, registry);
  return bridge.intern(CameraContent::kind_id, CameraContent::kind_version);
}

// The root (lowest-id) composition, or an invalid id when the document has none — the
// same root the compositor anchors on (`find_first_composition`, the v0.1 root rule).
arbc::ObjectId root_composition(const arbc::DocRoot& state) {
  arbc::ObjectId root_id;
  const arbc::CompositionRecord* rec = nullptr;
  if (!state.find_first_composition(root_id, rec) || rec == nullptr) {
    return arbc::ObjectId{};
  }
  return root_id;
}

} // namespace

std::vector<Camera> cameras(const arbc::Document& document) {
  std::vector<Camera> result;
  const arbc::DocStatePtr state = document.pin();
  if (!state) {
    return result;
  }
  arbc::ObjectId root_id;
  const arbc::CompositionRecord* root_rec = nullptr;
  if (!state->find_first_composition(root_id, root_rec) || root_rec == nullptr) {
    return result; // no composition — no cameras
  }
  // Walk the root composition's layers in bottom-to-top membership order and surface
  // each one whose content is a CameraContent (Constraint 7). `resolve` reads the
  // writer-owned content side-map, so this accessor is UI/writer-thread, like
  // `capture_snapshot`.
  state->for_each_layer_in(root_id, [&](arbc::ObjectId layer_id) {
    const arbc::LayerRecord* layer = state->find_layer(layer_id);
    if (layer == nullptr || !layer->content.valid()) {
      return;
    }
    const auto* camera = dynamic_cast<const CameraContent*>(document.resolve(layer->content));
    if (camera == nullptr) {
      return; // a cell (or other kind), not a camera
    }
    result.push_back(Camera{layer->content, layer_id, camera->camera_name(), camera->resolution(),
                            layer->transform});
  });
  return result;
}

arbc::ObjectId add_camera(arbc::Document& document, const arbc::Registry& registry,
                          const std::string& name, Resolution resolution,
                          const arbc::Affine& frame) {
  arbc::ObjectId composition;
  {
    const arbc::DocStatePtr state = document.pin();
    if (!state) {
      return arbc::ObjectId{};
    }
    composition = root_composition(*state);
  }
  if (!composition.valid()) {
    return arbc::ObjectId{}; // no root composition to place the camera in
  }

  // The content vtable can only be bound through `Document::add_content` (its own
  // transaction); the frame layer is then attached in one further transaction. Two
  // journal entries — libarbc exposes no atomic content+layer+attach for a vtable
  // content — but a single undo detaches the layer (the camera leaves `cameras()`)
  // and a single redo restores it.
  const arbc::ObjectId content = document.add_content(
      std::make_shared<CameraContent>(name, resolution), camera_token(registry));
  auto txn = document.transact("add_camera");
  const arbc::ObjectId layer = txn.add_layer(content, frame);
  txn.attach_layer(composition, layer);
  txn.commit();
  return content;
}

bool rename_camera(arbc::Document& document, const arbc::Registry& /*registry*/,
                   arbc::ObjectId camera, const std::string& new_name) {
  // Resolve the content vtable and confirm it is a camera. `resolve` returns nullptr
  // for an unknown id, and the `dynamic_cast` returns nullptr for a non-camera content
  // — both are the no-op-returning-false path (Constraint 3). No composition walk is
  // needed: the edit addresses the content by `ObjectId`, not by layer membership.
  auto* content = dynamic_cast<CameraContent*>(document.resolve(camera));
  if (content == nullptr) {
    return false;
  }

  // Rename IN PLACE (D-rename-1): one `set_content_state` transaction that path-copies
  // the camera's ContentRecord to the new-name version and journals the prior handle
  // as the undo `before`. The camera keeps its `ObjectId`, binding layer, frame,
  // resolution, and order — only its name changes — so a consumer holding the id
  // (the shared selection) keeps its handle, and `undo` restores the old name on the
  // same object. ONE journal entry (down from two).
  auto txn = document.transact("rename_camera");
  content->set_name(txn, camera, new_name);
  txn.commit();
  return true;
}

} // namespace ace::scene
