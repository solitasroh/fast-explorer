#pragma once

#include <atomic>
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
  ArenaFull,      // NameArena commit budget exhausted
  CapacityFull,   // entries_ has reached kMaxEntries
  NameTooLong,
};

// Per-pane storage of FileEntry records. Owns a NameArena so the
// FileEntry::namePtr pointers it returns stay valid for the store's
// lifetime, independent of the backend that produced them.
//
// Thread-safety: the worker thread appends and calls publish(); the
// UI thread reads only entries below publishedCount() and never calls
// any mutating method while workerActive. entries_ and visibleOrder_
// are reserve()d up to kMaxEntries at construction so push_back never
// reallocates — that keeps entries_[i] / visibleOrder_[i] pointers
// stable for the UI thread as long as i < publishedCount().
//
// publishedCount() ordering: the worker store-releases after the
// matching push_backs are visible; the UI acquire-loads before
// dereferencing entries_/visibleOrder_, so reads in [0, published)
// observe fully-initialized FileEntry records.
class FileModelStore {
 public:
  // Cap on entries per pane. entries_ and visibleOrder_ are reserved
  // up to this value at construction so push_back never reallocates;
  // UI-thread reads of entries_[i] / visibleOrder_[i] therefore see
  // stable pointers for any i below publishedCount().
  static constexpr std::uint32_t kMaxEntries = 100'000;

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
  // itemCount() returns the raw entries_.size(). While a worker thread
  // is appending, this read is non-atomic and races the worker's
  // size mutation — call publishedCount() from the UI thread instead.
  // Safe to call from the worker itself, or from any thread once the
  // worker has joined.
  std::size_t itemCount() const noexcept { return entries_.size(); }

  // Visibility boundary between worker (writer) and UI (reader). The
  // worker calls publish(n) after a batch of appends is fully written
  // and the UI calls publishedCount() before any entry read; only
  // indices in [0, publishedCount()) are safe to dereference from the
  // UI thread while the worker is still active. publish() is intended
  // to be called from the worker only — UI-side calls would race the
  // worker's append stream without any added synchronization.
  void publish(std::uint32_t count) noexcept {
    publishedCount_.store(count, std::memory_order_release);
  }
  std::uint32_t publishedCount() const noexcept {
    return publishedCount_.load(std::memory_order_acquire);
  }

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
  // From the UI thread, guard the index against publishedCount() rather
  // than itemCount() so the worker's append stream stays race-free.
  const FileEntry& visibleAt(std::size_t visibleIndex) const;
  // The span's data()/size() are non-atomic; callers concurrent with
  // a running worker must bound their iteration by publishedCount().
  std::span<const std::uint32_t> visibleOrder() const noexcept {
    return {visibleOrder_.data(), visibleOrder_.size()};
  }

  // Reorders the visible permutation by the given SortSpec. O(n log n).
  // Does not touch entries_ or the name arena, so namePtr stability and
  // append-time invariants are unaffected.
  void sort(SortSpec spec);

  // Replaces visibleOrder_ with a permutation produced elsewhere
  // (typically by a background sort worker). The caller must guarantee
  // `order.size() == publishedCount()` and that every value is a valid
  // entries_ index. Intended for the UI thread to apply the result of
  // a worker-thread sort under the same single-mutator policy as
  // sort().
  void applySortedOrder(std::vector<std::uint32_t> order);

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
  std::atomic<std::uint32_t> publishedCount_{0};
};

}  // namespace fast_explorer::core
