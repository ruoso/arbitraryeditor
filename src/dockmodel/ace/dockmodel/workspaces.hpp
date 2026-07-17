#pragma once

#include <ace/dockmodel/dockmodel.hpp>
#include <ace/platform/filesystem.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ace::dockmodel {

// Saved layout presets (docs/00-design.md D18/D21 — "Saved workspaces"). This is
// the headless L1 core of editor.dock.workspaces: a pure-data text projection of
// DockLayout, the three immutable built-in arrangements, and a store that
// persists user presets through the injectable platform::FileSystem seam (A3/A11
// — no ImGui, no OS handles). See docs/01-architecture.md §8 (dockmodel stays
// {base, platform}-only).

// Serialize a layout to the versioned, line-oriented text format (D-workspaces-2):
// a `ace-workspace 1` header, then a pre-order encoding — `split <H|V> <ratio>`
// with its two subtrees following, `leaf <active> <id...>`, or `empty`. The ratio
// uses the shortest round-trippable decimal so parse ∘ serialize is bit-exact.
std::string serialize_layout(const DockLayout& layout);

// Parse the text format back to a layout. TOTAL and non-throwing (mirrors
// parse_view_id): any defect — bad version tag, truncation, a ratio outside
// (0,1), a duplicate/unknown view id, an out-of-range active, a stray token —
// yields nullopt. A returned layout always satisfies valid() (D-workspaces-1).
std::optional<DockLayout> parse_layout(std::string_view text);

// A named layout preset: the display name plus the layout it seeds. `builtin`
// marks the three immutable D21 arrangements (which save/remove refuse to touch).
struct WorkspacePreset {
  std::string name;
  DockLayout layout;
  bool builtin = false;

  bool operator==(const WorkspacePreset&) const = default;
};

// The three immutable built-in arrangements over the D18 view catalog (D21):
// Paint (Canvas | Layers·Inspector·Color), Compose (Canvas | Overview·Layers),
// Review (Canvas | History·Export). Identical every call; each is valid().
const std::vector<WorkspacePreset>& workspace_builtins();

// The per-user preset store (D-workspaces-1/-3). Constructed with a prefs root
// path the app resolves (XDG/AppData/OPFS — L4's concern) and an injected
// FileSystem, so it is fully unit-testable over a ScratchDir + NativeFileSystem
// and WASM-swappable. User presets persist to a single versioned text file under
// the root; a missing or corrupt store falls back to the built-ins (D16/D21).
class WorkspaceStore {
public:
  WorkspaceStore(std::filesystem::path root, platform::FileSystem& filesystem);

  // Built-ins ∪ user presets: the three built-ins first, then user presets in
  // name order. A user preset shadowing a built-in name is ignored (defensive
  // against a hand-edited store); a corrupt store yields just the built-ins.
  std::vector<WorkspacePreset> presets() const;

  // The layout `name` seeds (built-in or user), or nullopt if no such preset.
  std::optional<DockLayout> apply(std::string_view name) const;

  // Save `layout` as a user preset `name`, publishing the store atomically
  // (D16). Rejects (returns false) an empty name, a name colliding with a
  // built-in, an invalid layout, or an I/O failure.
  bool save(std::string_view name, const DockLayout& layout);

  // Remove the user preset `name`. Refuses (returns false) a built-in or an
  // unknown name; otherwise rewrites the store atomically.
  bool remove(std::string_view name);

private:
  std::filesystem::path store_file() const;
  std::vector<WorkspacePreset> load_user() const;
  bool write_user(const std::vector<WorkspacePreset>& user) const;

  std::filesystem::path root_;
  platform::FileSystem& filesystem_;
};

} // namespace ace::dockmodel
