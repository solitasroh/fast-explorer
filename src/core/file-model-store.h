#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
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
// Storage layout: entries_ and visibleOrder_ are fixed-size raw
// arrays of kMaxEntries elements, allocated once at construction.
// workerSize_ tracks how many slots the worker has populated and is
// atomic so the UI thread can acquire-load a value that synchronizes
// with the worker's writes. The earlier std::vector layout relied on
// reserve() + non-atomic m_size; that left vector::size() formally a
// data race even when reallocation was impossible. Plain arrays plus
// an explicit atomic size make the same access pattern standard-
// compliant: the worker is the only thread that writes workerSize_,
// readers (entryAt / visibleAt / itemCount) take an acquire load,
// and publishedCount_ still gates "fully constructed batch boundary"
// visibility on top of that.
class FileModelStore {
 public:
  // Cap on entries per pane. entries_ and visibleOrder_ are sized to
  // this value at construction so push_back never reallocates and
  // UI-thread reads of entries_[i] / visibleOrder_[i] see stable
  // pointers for any i below publishedCount().
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
  // FileEntry::nameLength (uint16_t); CapacityFull means kMaxEntries
  // has been reached.
  AppendResult appendEntry(const FileEntry& source);

  // Append multiple entries. Returns the number of entries that were
  // actually stored; stops early on the first AppendResult that is
  // not Stored.
  std::size_t appendBatch(std::span<const FileEntry> batch);

  std::uint32_t generation() const noexcept { return generation_; }
  const std::wstring& rootPath() const noexcept { return rootPath_; }
  // workerSize_ acquire-load. UI-thread callers MUST NOT use this to
  // bound an entryAt/visibleAt loop while a worker is in flight: the
  // worker may have advanced workerSize_ past the most recent batch
  // boundary, and an entry between publishedCount() and itemCount()
  // can be observed in the middle of being written by the next
  // batch's appendEntry. Use publishedCount() for UI indexing; this
  // accessor is for memory accounting and worker-internal logic.
  std::size_t itemCount() const noexcept {
    return workerSize_.load(std::memory_order_acquire);
  }

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
  // The span's data() is stable for the store's lifetime — using it
  // after the store is destroyed is UB, and forwarding it to another
  // thread requires the caller to keep the store alive for that
  // thread's lifetime. The span's size reflects workerSize_ at the
  // call site; callers concurrent with a running worker must bound
  // their iteration by publishedCount().
  std::span<const std::uint32_t> visibleOrder() const noexcept {
    return {visibleOrder_.get(),
            workerSize_.load(std::memory_order_acquire)};
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
  // sort(). After this call only the [0, publishedCount()) prefix of
  // visibleOrder_ is meaningful — if the caller subsequently advances
  // publishedCount past that prefix, the unsorted tail (identity
  // values written by appendEntry) will be exposed.
  void applySortedOrder(std::vector<std::uint32_t> order);

  std::size_t entriesBytes() const noexcept {
    return static_cast<std::size_t>(kMaxEntries) * sizeof(FileEntry);
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
  std::unique_ptr<FileEntry[]> entries_;
  std::unique_ptr<std::uint32_t[]> visibleOrder_;
  // workerSize_ is single-writer (the enumeration worker, via
  // appendEntry on its own thread). All readers — including UI-thread
  // accessors — see writes through the acquire/release pair below.
  std::atomic<std::uint32_t> workerSize_{0};
  std::atomic<std::uint32_t> publishedCount_{0};
};

}  // namespace fast_explorer::core
