#include "core/file-model-store.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace fast_explorer::core {

FileModelStore::FileModelStore(std::wstring rootPath,
                               std::size_t arenaReserveBytes)
    : rootPath_(std::move(rootPath)),
      nameArena_(arenaReserveBytes),
      entries_(std::make_unique<FileEntry[]>(kMaxEntries)),
      visibleOrder_(std::make_unique<std::uint32_t[]>(kMaxEntries)) {}

void FileModelStore::setLogger(RingLogger* logger) noexcept {
  nameArena_.setLogger(logger);
}

void FileModelStore::reset(std::wstring newRoot) {
  rootPath_ = std::move(newRoot);
  workerSize_.store(0, std::memory_order_release);
  publishedCount_.store(0, std::memory_order_release);
  nameArena_.reset();
  ++generation_;
}

AppendResult FileModelStore::appendEntry(const FileEntry& source) {
  // Single-writer: this method runs on the enumeration worker thread
  // and is the only place workerSize_ ever advances, so a relaxed
  // load of the current size is sufficient — no other producer can
  // be racing this one.
  const std::uint32_t size =
      workerSize_.load(std::memory_order_relaxed);
  if (size >= kMaxEntries) {
    return AppendResult::CapacityFull;
  }
  const std::wstring_view inputName = nameView(source);
  if (inputName.size() > UINT16_MAX) {
    return AppendResult::NameTooLong;
  }
  const std::wstring_view interned = nameArena_.intern(inputName);
  if (!inputName.empty() && interned.empty()) {
    return AppendResult::ArenaFull;
  }
  FileEntry copy = source;
  copy.namePtr = interned.data();
  copy.nameLength = static_cast<std::uint16_t>(interned.size());
  entries_[size] = copy;
  visibleOrder_[size] = size;
  // Release-store so any reader that acquire-loads workerSize_ and
  // sees this new value also sees the entry/permutation writes above.
  workerSize_.store(size + 1, std::memory_order_release);
  return AppendResult::Stored;
}

std::size_t FileModelStore::appendBatch(std::span<const FileEntry> batch) {
  std::size_t stored = 0;
  for (const FileEntry& e : batch) {
    if (appendEntry(e) != AppendResult::Stored) {
      break;
    }
    ++stored;
  }
  return stored;
}

const FileEntry& FileModelStore::entryAt(std::size_t index) const noexcept {
  assert(index < workerSize_.load(std::memory_order_acquire));
  return entries_[index];
}

const FileEntry& FileModelStore::visibleAt(std::size_t visibleIndex) const noexcept {
  assert(visibleIndex < workerSize_.load(std::memory_order_acquire));
  const std::uint32_t raw = visibleOrder_[visibleIndex];
  assert(raw < workerSize_.load(std::memory_order_acquire));
  return entries_[raw];
}

void FileModelStore::sort(SortSpec spec) {
  // UI-thread mutator. The caller must guarantee that the
  // enumeration worker is no longer appending; under that condition
  // workerSize_ is stable and we can sort the visible permutation
  // in place without any further synchronization. Debug builds
  // assert the worker-quiesce contract: when the worker is idle it
  // has published every entry it produced, so publishedCount and
  // workerSize must agree. Catching that here keeps the race this
  // file's raw-array layout was rebuilt to eliminate from sneaking
  // back in through a future caller that forgets the precondition.
  const auto size = workerSize_.load(std::memory_order_acquire);
  assert(size == publishedCount_.load(std::memory_order_acquire));
  const auto* entriesRef = entries_.get();
  std::sort(visibleOrder_.get(), visibleOrder_.get() + size,
            [entriesRef, spec](std::uint32_t a, std::uint32_t b) {
              return lessEntries(entriesRef[a], entriesRef[b], spec);
            });
}

void FileModelStore::applySortedOrder(std::vector<std::uint32_t> order) {
  assert(order.size() == publishedCount());
  // Copy element-by-element into the fixed-size raw buffer to keep
  // the kMaxEntries reservation intact. The caller's vector is
  // discarded.
  std::copy(order.begin(), order.end(), visibleOrder_.get());
}

}  // namespace fast_explorer::core
