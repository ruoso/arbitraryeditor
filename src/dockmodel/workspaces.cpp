#include <ace/dockmodel/view_registry.hpp>
#include <ace/dockmodel/workspaces.hpp>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <system_error>
#include <utility>

namespace ace::dockmodel {
namespace {

constexpr std::string_view k_layout_header = "ace-workspace 1";
constexpr std::string_view k_store_header = "ace-workspace-store 1";
constexpr std::string_view k_preset_prefix = "preset ";
constexpr std::string_view k_store_filename = "workspaces.acews";

// --- serialization ---------------------------------------------------------

std::string ratio_to_string(double ratio) {
  // Shortest decimal that round-trips to the exact same double (locale-free), so
  // parse ∘ serialize preserves operator== equality bit-for-bit.
  char buf[32];
  const std::to_chars_result res = std::to_chars(buf, buf + sizeof(buf), ratio);
  return std::string(buf, res.ptr);
}

void write_node(const DockNode& node, std::string& out) {
  if (node.is_leaf()) {
    out += "leaf ";
    out += std::to_string(node.active);
    for (const std::string& id : node.tabs) {
      out += ' ';
      out += id;
    }
    out += '\n';
    return;
  }
  out += "split ";
  out += (node.orientation == SplitOrientation::Horizontal ? 'H' : 'V');
  out += ' ';
  out += ratio_to_string(node.ratio);
  out += '\n';
  write_node(node.children[0], out);
  write_node(node.children[1], out);
}

// The node-only body shared by the single-layout and multi-preset formats:
// `empty` for the empty layout, otherwise the pre-order node encoding.
void write_body(const DockLayout& layout, std::string& out) {
  if (layout.empty()) {
    out += "empty\n";
    return;
  }
  write_node(layout.root(), out);
}

// --- parsing ---------------------------------------------------------------

// Split on '\n' into lines (a trailing '\r' stripped for CRLF tolerance) and
// drop the trailing empty lines a final newline produces.
std::vector<std::string> split_lines(std::string_view text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t nl = text.find('\n', start);
    const std::size_t end = (nl == std::string_view::npos) ? text.size() : nl;
    std::string_view line = text.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    lines.emplace_back(line);
    if (nl == std::string_view::npos) {
      break;
    }
    start = nl + 1;
  }
  while (!lines.empty() && lines.back().empty()) {
    lines.pop_back();
  }
  return lines;
}

std::vector<std::string_view> tokenize(std::string_view line) {
  std::vector<std::string_view> tokens;
  std::size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && line[i] == ' ') {
      ++i;
    }
    const std::size_t begin = i;
    while (i < line.size() && line[i] != ' ') {
      ++i;
    }
    if (i > begin) {
      tokens.push_back(line.substr(begin, i - begin));
    }
  }
  return tokens;
}

bool parse_size(std::string_view token, std::size_t& out) {
  const char* begin = token.data();
  const char* end = token.data() + token.size();
  const std::from_chars_result res = std::from_chars(begin, end, out);
  return res.ec == std::errc() && res.ptr == end;
}

bool parse_ratio(std::string_view token, double& out) {
  const char* begin = token.data();
  const char* end = token.data() + token.size();
  const std::from_chars_result res = std::from_chars(begin, end, out);
  return res.ec == std::errc() && res.ptr == end;
}

// Recursive-descent over lines[idx, end): consumes exactly one node subtree,
// advancing idx. nullopt on any malformed token / truncation.
std::optional<DockNode> parse_node(const std::vector<std::string>& lines, std::size_t& idx,
                                   std::size_t end) {
  if (idx >= end) {
    return std::nullopt;
  }
  const std::vector<std::string_view> tokens = tokenize(lines[idx]);
  ++idx;
  if (tokens.empty()) {
    return std::nullopt;
  }
  if (tokens[0] == "leaf") {
    if (tokens.size() < 3) { // "leaf" + active + at least one id
      return std::nullopt;
    }
    std::size_t active = 0;
    if (!parse_size(tokens[1], active)) {
      return std::nullopt;
    }
    std::vector<std::string> tabs;
    for (std::size_t i = 2; i < tokens.size(); ++i) {
      if (!parse_view_id(tokens[i])) { // reject unknown / malformed view ids
        return std::nullopt;
      }
      tabs.emplace_back(tokens[i]);
    }
    return DockNode::leaf(std::move(tabs), active);
  }
  if (tokens[0] == "split") {
    if (tokens.size() != 3) {
      return std::nullopt;
    }
    SplitOrientation orientation = SplitOrientation::Horizontal;
    if (tokens[1] == "H") {
      orientation = SplitOrientation::Horizontal;
    } else if (tokens[1] == "V") {
      orientation = SplitOrientation::Vertical;
    } else {
      return std::nullopt;
    }
    double ratio = 0.0;
    if (!parse_ratio(tokens[2], ratio)) {
      return std::nullopt;
    }
    std::optional<DockNode> first = parse_node(lines, idx, end);
    if (!first) {
      return std::nullopt;
    }
    std::optional<DockNode> second = parse_node(lines, idx, end);
    if (!second) {
      return std::nullopt;
    }
    return DockNode::split(orientation, ratio, std::move(*first), std::move(*second));
  }
  return std::nullopt;
}

// Parse the node body in lines[begin, end) to a valid layout, or nullopt.
std::optional<DockLayout> parse_body(const std::vector<std::string>& lines, std::size_t begin,
                                     std::size_t end) {
  if (begin >= end) {
    return std::nullopt;
  }
  if (lines[begin] == "empty") {
    if (begin + 1 != end) { // trailing garbage after `empty`
      return std::nullopt;
    }
    return DockLayout{};
  }
  std::size_t idx = begin;
  std::optional<DockNode> node = parse_node(lines, idx, end);
  if (!node || idx != end) { // truncated, or extra unconsumed lines
    return std::nullopt;
  }
  DockLayout layout(std::move(*node));
  if (!layout.valid()) { // duplicate id / bad ratio / out-of-range active
    return std::nullopt;
  }
  return layout;
}

// --- the multi-preset store file -------------------------------------------

std::string serialize_store(const std::vector<WorkspacePreset>& user) {
  std::string out(k_store_header);
  out += '\n';
  for (const WorkspacePreset& preset : user) {
    out += k_preset_prefix;
    out += preset.name;
    out += '\n';
    write_body(preset.layout, out);
  }
  return out;
}

bool is_preset_line(const std::string& line) {
  return line.compare(0, k_preset_prefix.size(), k_preset_prefix) == 0;
}

std::optional<std::vector<WorkspacePreset>> parse_store(std::string_view text) {
  const std::vector<std::string> lines = split_lines(text);
  if (lines.empty() || lines[0] != k_store_header) {
    return std::nullopt;
  }
  std::vector<WorkspacePreset> user;
  std::size_t i = 1;
  while (i < lines.size()) {
    if (!is_preset_line(lines[i])) {
      return std::nullopt;
    }
    std::string name = lines[i].substr(k_preset_prefix.size());
    if (name.empty()) {
      return std::nullopt;
    }
    ++i;
    std::size_t body_end = i;
    while (body_end < lines.size() && !is_preset_line(lines[body_end])) {
      ++body_end;
    }
    std::optional<DockLayout> layout = parse_body(lines, i, body_end);
    if (!layout) {
      return std::nullopt;
    }
    user.push_back(WorkspacePreset{std::move(name), std::move(*layout), false});
    i = body_end;
  }
  return user;
}

// --- built-in arrangements (D21) -------------------------------------------

// A Canvas pane beside a tab-stack of the named singleton views. The Canvas uses
// the minted-form id `canvas#1` so a parse round-trip and ViewRegistry::adopt see
// a well-formed multi-instance id.
DockLayout builtin_layout(std::vector<std::string> stack) {
  return DockLayout(DockNode::split(SplitOrientation::Horizontal, 0.62,
                                    DockNode::leaf({"canvas#1"}),
                                    DockNode::leaf(std::move(stack))));
}

} // namespace

std::string serialize_layout(const DockLayout& layout) {
  std::string out(k_layout_header);
  out += '\n';
  write_body(layout, out);
  return out;
}

std::optional<DockLayout> parse_layout(std::string_view text) {
  const std::vector<std::string> lines = split_lines(text);
  if (lines.empty() || lines[0] != k_layout_header) {
    return std::nullopt;
  }
  return parse_body(lines, 1, lines.size());
}

const std::vector<WorkspacePreset>& workspace_builtins() {
  static const std::vector<WorkspacePreset> builtins = {
      {"Paint", builtin_layout({"layers", "inspector", "color"}), true},
      {"Compose", builtin_layout({"overview", "layers"}), true},
      {"Review", builtin_layout({"history", "export"}), true},
  };
  return builtins;
}

WorkspaceStore::WorkspaceStore(std::filesystem::path root, platform::FileSystem& filesystem)
    : root_(std::move(root)), filesystem_(filesystem) {}

std::filesystem::path WorkspaceStore::store_file() const { return root_ / k_store_filename; }

std::vector<WorkspacePreset> WorkspaceStore::load_user() const {
  const std::filesystem::path path = store_file();
  if (!filesystem_.exists(path)) {
    return {}; // a fresh store: no user presets, built-ins only
  }
  const platform::Result<std::string> contents = filesystem_.read_file(path);
  if (!contents.has_value()) {
    return {};
  }
  std::optional<std::vector<WorkspacePreset>> parsed = parse_store(contents.value());
  if (!parsed) {
    // Corrupt store — log and fall back to the built-ins (D16/D21, never abort).
    std::fprintf(stderr, "workspaces: corrupt preset store at %s — using built-ins\n",
                 path.string().c_str());
    return {};
  }
  return std::move(*parsed);
}

bool WorkspaceStore::write_user(const std::vector<WorkspacePreset>& user) const {
  if (std::error_code ec = filesystem_.make_directories(root_)) {
    return false;
  }
  return !static_cast<bool>(filesystem_.atomic_replace(store_file(), serialize_store(user)));
}

std::vector<WorkspacePreset> WorkspaceStore::presets() const {
  std::vector<WorkspacePreset> out = workspace_builtins();
  std::vector<WorkspacePreset> user = load_user();
  std::sort(user.begin(), user.end(),
            [](const WorkspacePreset& a, const WorkspacePreset& b) { return a.name < b.name; });
  const auto known = [&out](std::string_view name) {
    return std::any_of(out.begin(), out.end(),
                       [&name](const WorkspacePreset& p) { return p.name == name; });
  };
  for (WorkspacePreset& preset : user) {
    if (known(preset.name)) { // shadow guard: a built-in name or an earlier dup
      continue;
    }
    out.push_back(std::move(preset));
  }
  return out;
}

std::optional<DockLayout> WorkspaceStore::apply(std::string_view name) const {
  for (const WorkspacePreset& preset : presets()) {
    if (preset.name == name) {
      return preset.layout;
    }
  }
  return std::nullopt;
}

bool WorkspaceStore::save(std::string_view name, const DockLayout& layout) {
  if (name.empty() || !layout.valid()) {
    return false;
  }
  for (const WorkspacePreset& builtin : workspace_builtins()) {
    if (builtin.name == name) {
      return false; // built-ins are immutable (D21)
    }
  }
  std::vector<WorkspacePreset> user = load_user();
  user.erase(std::remove_if(user.begin(), user.end(),
                            [&name](const WorkspacePreset& p) { return p.name == name; }),
             user.end());
  user.push_back(WorkspacePreset{std::string(name), layout, false});
  return write_user(user);
}

bool WorkspaceStore::remove(std::string_view name) {
  for (const WorkspacePreset& builtin : workspace_builtins()) {
    if (builtin.name == name) {
      return false; // cannot delete a built-in
    }
  }
  std::vector<WorkspacePreset> user = load_user();
  const auto it = std::find_if(user.begin(), user.end(),
                               [&name](const WorkspacePreset& p) { return p.name == name; });
  if (it == user.end()) {
    return false; // unknown user preset
  }
  user.erase(it);
  return write_user(user);
}

} // namespace ace::dockmodel
