#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/file-sort.h"

namespace fast_explorer::ui {
class FormatCache;
}  // namespace fast_explorer::ui

namespace fast_explorer::core {

struct FileEntry;
class FileModelStore;

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

// Returns the Korean header string for a given (key, id). For Type
// groups (id >= 2), reads the extension's display name via `cache`.
// Caller owns the returned wstring. Never returns empty for valid IDs.
[[nodiscard]] std::wstring groupTitleForId(
    GroupKey key, int32_t id,
    const FileModelStore* store,
    const fast_explorer::ui::FormatCache* cache);

// Tri-state comparator that uses groupId as the primary key, falling
// through to the existing compareEntries comparator on group ties.
// When `gk == GroupKey::None`, behaviour is exactly compareEntries.
[[nodiscard]] int compareWithGroup(const FileEntry& a,
                                   const FileEntry& b,
                                   SortSpec spec,
                                   GroupKey gk,
                                   uint64_t nowFiletime) noexcept;

}  // namespace fast_explorer::core
