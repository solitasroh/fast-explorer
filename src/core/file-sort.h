#pragma once

#include <cstdint>

namespace fast_explorer::core {

struct FileEntry;

// Sort key selects which field on FileEntry is the primary ordering.
// Values are stable for serialization (logs, perf events) — append only.
enum class SortKey : uint8_t {
  Name = 0,
  Size = 1,
  Modified = 2,
  Type = 3,
};

enum class SortDirection : uint8_t {
  Ascending = 0,
  Descending = 1,
};

struct SortSpec {
  SortKey key;
  SortDirection direction;
};

// Tri-state comparison: negative if a < b, 0 if equal, positive if a > b.
// Tiebreak is always name ascending (CompareStringOrdinal IgnoreCase) so the
// resulting order is deterministic regardless of std::sort stability or
// platform sort algorithm.
[[nodiscard]] int compareEntries(const FileEntry& a,
                                 const FileEntry& b,
                                 SortSpec spec) noexcept;

// Strict weak ordering predicate for std::sort / std::ranges::sort.
[[nodiscard]] inline bool lessEntries(const FileEntry& a,
                                      const FileEntry& b,
                                      SortSpec spec) noexcept {
  return compareEntries(a, b, spec) < 0;
}

}  // namespace fast_explorer::core
