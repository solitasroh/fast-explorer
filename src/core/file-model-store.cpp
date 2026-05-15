#include "core/file-model-store.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace fast_explorer::core {

FileModelStore::FileModelStore(std::wstring rootPath,
                               std::size_t arenaReserveBytes)
    : rootPath_(std::move(rootPath)),
      nameArena_(arenaReserveBytes) {
  entries_.reserve(kMaxEntries);
  visibleOrder_.reserve(kMaxEntries);
}

void FileModelStore::setLogger(RingLogger* logger) noexcept {
  nameArena_.setLogger(logger);
}

void FileModelStore::reset(std::wstring newRoot) {
  rootPath_ = std::move(newRoot);
  entries_.clear();
  visibleOrder_.clear();
  nameArena_.reset();
  publishedCount_.store(0, std::memory_order_release);
  ++generation_;
}

AppendResult FileModelStore::appendEntry(const FileEntry& source) {
  // entries_ and visibleOrder_ are reserve()d up to kMaxEntries so
  // push_back never reallocates, which is what gives UI-thread readers
  // pointer stability on entries_[i] / visibleOrder_[i]. Crossing the
  // reserve would silently reallocate and break that invariant, so we
  // refuse the append rather than corrupt the read path.
  if (entries_.size() >= kMaxEntries) {
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
  const std::uint32_t newIndex = static_cast<std::uint32_t>(entries_.size());
  entries_.push_back(copy);
  visibleOrder_.push_back(newIndex);
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

const FileEntry& FileModelStore::entryAt(std::size_t index) const {
  assert(index < entries_.size());
  return entries_[index];
}

const FileEntry& FileModelStore::visibleAt(std::size_t visibleIndex) const {
  assert(visibleIndex < visibleOrder_.size());
  const std::uint32_t raw = visibleOrder_[visibleIndex];
  assert(raw < entries_.size());
  return entries_[raw];
}

void FileModelStore::sort(SortSpec spec) {
  assert(visibleOrder_.size() == entries_.size());
  const auto& entriesRef = entries_;
  std::sort(visibleOrder_.begin(), visibleOrder_.end(),
            [&entriesRef, spec](std::uint32_t a, std::uint32_t b) {
              return lessEntries(entriesRef[a], entriesRef[b], spec);
            });
}

}  // namespace fast_explorer::core
