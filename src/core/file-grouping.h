#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fast_explorer::core {

struct FileEntry;
class FileModelStore;
class FormatCache;

enum class GroupKey : uint8_t {
  None     = 0,
  Name     = 1,
  Modified = 2,
  Type     = 3,
};

// Returns the group ID this entry belongs to under `key`.
// `nowFiletime` is the 100ns-since-1601 timestamp used only for
// GroupKey::Modified; safe to pass 0 for the other keys.
[[nodiscard]] int32_t groupIdForEntry(GroupKey key,
                                      const FileEntry& entry,
                                      uint64_t nowFiletime) noexcept;

// Walks the store's displayed items (filter-aware via store.visibleAt)
// and returns the group IDs present, in render order. Empty groups
// are not included. Caller must hold the store stable for the call
// (e.g., enumeration worker not active).
[[nodiscard]] std::vector<int32_t> enumerateGroups(
    GroupKey key,
    const FileModelStore& store,
    uint64_t nowFiletime);

}  // namespace fast_explorer::core
