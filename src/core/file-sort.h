#pragma once

#include <cstdint>

namespace fast_explorer::core {

struct FileEntry;

// Sort key selects which field on FileEntry is the primary ordering.
// Values are stable for serialization (logs, perf events) — append only.
// `None` is the explicit "no sort" sentinel for columns that have no
// natural ordering (e.g. the Attributes column); compareEntries treats
// it as "always equal" so the deterministic name tiebreak still applies.
enum class SortKey : uint8_t {
  Name = 0,
  Size = 1,
  Modified = 2,
  Type = 3,
  None = 4,
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
                                 SortSpec spec);

[[nodiscard]] inline bool lessEntries(const FileEntry& a,
                                      const FileEntry& b,
                                      SortSpec spec) {
  return compareEntries(a, b, spec) < 0;
}

}  // namespace fast_explorer::core
