#include <ace/dockmodel/recent_projects.hpp>

#include <algorithm>
#include <string>
#include <system_error>
#include <utility>

// editor.project.open_ui — the recent-projects MRU prefs store (D22). Mirrors the
// WorkspaceStore shape (versioned line-oriented text, atomic_replace publish,
// rebuild-from-default on a missing/corrupt file); the list is capped,
// most-recent-first, and pruned on load through an injected predicate so
// dockmodel never depends on `project` (§8).

namespace ace::dockmodel {
namespace {

constexpr std::string_view k_header = "ace-recent-projects 1";
constexpr std::string_view k_line_prefix = "path ";
constexpr std::string_view k_store_filename = "recent-projects.acerp";

// Canonicalize to an absolute path (Constraint 6): `absolute` unconditionally
// roots at the CWD (lexical, no existence needed), then `weakly_canonical`
// normalizes `.`/`..`/symlinks where they resolve. Neither throws (ec overload).
std::filesystem::path canonicalize(const std::filesystem::path& dir) {
  std::error_code ec;
  std::filesystem::path resolved = std::filesystem::absolute(dir, ec);
  if (ec) {
    return dir;
  }
  std::filesystem::path canonical = std::filesystem::weakly_canonical(resolved, ec);
  return ec ? resolved : canonical;
}

// Split on '\n' (a trailing '\r' stripped for CRLF tolerance), dropping the
// trailing empty lines a final newline produces — mirrors workspaces.cpp.
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

} // namespace

RecentProjects::RecentProjects(std::filesystem::path root, platform::FileSystem& filesystem)
    : root_(std::move(root)), filesystem_(filesystem) {}

std::filesystem::path RecentProjects::store_file() const { return root_ / k_store_filename; }

std::vector<std::filesystem::path> RecentProjects::read_entries() const {
  const std::filesystem::path path = store_file();
  if (!filesystem_.exists(path)) {
    return {}; // a fresh store: no recents
  }
  const platform::Result<std::string> contents = filesystem_.read_file(path);
  if (!contents.has_value()) {
    return {};
  }
  const std::vector<std::string> lines = split_lines(contents.value());
  // A malformed store degrades to empty (rebuild-from-default, D21): a missing or
  // wrong header, or any line that is not a `path <abs>` entry, discards the lot
  // rather than trusting a partially-parsed file.
  if (lines.empty() || lines.front() != k_header) {
    return {};
  }
  std::vector<std::filesystem::path> entries;
  for (std::size_t i = 1; i < lines.size(); ++i) {
    const std::string& line = lines[i];
    if (line.compare(0, k_line_prefix.size(), k_line_prefix) != 0) {
      return {};
    }
    std::string value = line.substr(k_line_prefix.size());
    if (value.empty()) {
      return {};
    }
    entries.emplace_back(std::move(value));
  }
  return entries;
}

bool RecentProjects::write_entries(const std::vector<std::filesystem::path>& entries) const {
  if (std::error_code ec = filesystem_.make_directories(root_)) {
    return false;
  }
  std::string out(k_header);
  out += '\n';
  for (const std::filesystem::path& entry : entries) {
    out += k_line_prefix;
    out += entry.string();
    out += '\n';
  }
  return !static_cast<bool>(filesystem_.atomic_replace(store_file(), out));
}

std::vector<std::filesystem::path> RecentProjects::load(const Validator& is_valid) const {
  const std::vector<std::filesystem::path> stored = read_entries();
  std::vector<std::filesystem::path> kept;
  bool pruned = false;
  for (const std::filesystem::path& entry : stored) {
    if (is_valid && is_valid(entry)) {
      kept.push_back(entry);
    } else {
      pruned = true;
    }
  }
  if (pruned) {
    // Self-heal the file so the stale entry does not linger for the next reader.
    write_entries(kept);
  }
  return kept;
}

bool RecentProjects::add(const std::filesystem::path& dir) {
  if (dir.empty()) {
    return false;
  }
  const std::filesystem::path resolved = canonicalize(dir);
  std::vector<std::filesystem::path> entries = read_entries();
  // De-dup: drop any existing occurrence, then push to the front (MRU move-up).
  entries.erase(std::remove(entries.begin(), entries.end(), resolved), entries.end());
  entries.insert(entries.begin(), resolved);
  if (entries.size() > k_max_entries) {
    entries.resize(k_max_entries); // drop the oldest past the cap
  }
  return write_entries(entries);
}

} // namespace ace::dockmodel
