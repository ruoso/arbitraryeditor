#pragma once

// Reusable byte-exact golden compare (the first render_offline golden harness,
// docs §9 / D-render_probe-6). Mirrors how app_shell shipped its e2e rig as test
// support, not a WBS leaf — later canvas/export goldens inherit this. Goldens are
// raw straight-alpha sRGB8 RGBA bytes under ACE_GOLDEN_DIR (set by CMake).

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace ace_test {

inline std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>());
}

inline void write_file_bytes(const std::filesystem::path& path,
                             const std::vector<std::uint8_t>& bytes) {
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

// Byte-compare `actual` against <ACE_GOLDEN_DIR>/<name>. On mismatch (or a
// missing golden) writes "<name>.actual" beside the golden for triage and
// returns false; returns true only on an exact match.
inline bool compare_golden(const std::string& name, const std::vector<std::uint8_t>& actual) {
  const std::filesystem::path golden = std::filesystem::path(ACE_GOLDEN_DIR) / name;
  const std::vector<std::uint8_t> expected = read_file_bytes(golden);
  const bool match = !expected.empty() && expected == actual;
  if (!match) {
    std::filesystem::path dump = golden;
    dump += ".actual";
    write_file_bytes(dump, actual);
  }
  return match;
}

} // namespace ace_test
