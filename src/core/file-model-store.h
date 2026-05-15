#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/file-entry.h"
#include "core/file-sort.h"
#include "core/name-arena.h"

namespace fast_explorer::core {

class RingLogger;

enum class AppendResult : uint8_t {
  Stored = 0,
  ArenaFull,
  NameTooLong,
};

// Per-pane storage of FileEntry records. Owns a NameArena so the
// FileEntry::namePtr pointers it returns stay valid for the store's
// lifetime, independent of the backend that produced them.
//
// Thread-safety: NOT thread-safe. A single worker thread mutates the
// store; the UI thread reads from it only after the worker publishes
// a batch through the higher-level message boundary.
class FileModelStore {
 public:
  explicit FileModelStore(
      std::wstring rootPath,
      std::size_t arenaReserveBytes = NameArena::kDefaultReserveBytes);

  void setLogger(RingLogger* logger) noexcept;

  // Discards every entry, decommits the name arena, and bumps the
  // generation. The new root takes effect immediately.
  void reset(std::wstring newRoot);

  // Append a single entry. The name aliased by source.namePtr is
  // copied into the store's arena and the stored FileEntry's namePtr
  // is rewritten to point at that copy. ArenaFull means the arena is
  // exhausted; NameTooLong means the interned name would not fit in
  // FileEntry::nameLength (uint16_t).
  AppendResult appendEntry(const FileEntry& source);

  // Append multiple entries. Returns the number of entries that were
  // actually stored; stops early on the first AppendResult that is
  // not Stored.
  std::size_t appendBatch(std::span<const FileEntry> batch);

  std::uint32_t generation() const noexcept { return generation_; }
  const std::wstring& rootPath() const noexcept { return rootPath_; }
  std::size_t itemCount() const noexcept { return entries_.size(); }

  // entryAt's reference is invalidated by any subsequent append; only
  // FileEntry::namePtr remains valid because it points into the arena.
  // Precondition: index < itemCount(). Violations are UB in release.
  const FileEntry& entryAt(std::size_t index) const;

  // Visible-order accessors. The store maintains a permutation of
  // entry indices so the UI can reorder rows (sort) without rewriting
  // the underlying FileEntry array. Until sort() is called the
  // permutation is identity (0, 1, ..., itemCount-1), so visibleAt(i)
  // and entryAt(i) return the same record. Newly appended entries are
  // always pushed onto the tail of visibleOrder, so insertion order
  // is preserved until the next sort.
  // Precondition: visibleIndex < itemCount(). Violations are UB in release.
  const FileEntry& visibleAt(std::size_t visibleIndex) const;
  std::span<const std::uint32_t> visibleOrder() const noexcept {
    return {visibleOrder_.data(), visibleOrder_.size()};
  }

  // Reorders the visible permutation by the given SortSpec. O(n log n).
  // Does not touch entries_ or the name arena, so namePtr stability and
  // append-time invariants are unaffected.
  void sort(SortSpec spec);

  std::size_t entriesBytes() const noexcept {
    return entries_.capacity() * sizeof(FileEntry);
  }
  std::size_t nameArenaCommittedBytes() const noexcept {
    return nameArena_.committedBytes();
  }
  std::size_t nameArenaUsedBytes() const noexcept {
    return nameArena_.usedBytes();
  }
  std::size_t totalBytes() const noexcept {
    return entriesBytes() + nameArenaCommittedBytes();
  }

 private:
  std::wstring rootPath_;
  std::uint32_t generation_ = 0;
  NameArena nameArena_;
  std::vector<FileEntry> entries_;
  std::vector<std::uint32_t> visibleOrder_;
};

}  // namespace fast_explorer::core
