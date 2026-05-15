#include "core/file-model-store.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace fast_explorer::core {

FileModelStore::FileModelStore(std::wstring rootPath,
                               std::size_t arenaReserveBytes)
    : rootPath_(std::move(rootPath)),
      nameArena_(arenaReserveBytes) {}

void FileModelStore::setLogger(RingLogger* logger) noexcept {
  nameArena_.setLogger(logger);
}

void FileModelStore::reset(std::wstring newRoot) {
  rootPath_ = std::move(newRoot);
  entries_.clear();
  visibleOrder_.clear();
  nameArena_.reset();
  ++generation_;
}

AppendResult FileModelStore::appendEntry(const FileEntry& source) {
  // visibleOrder_ stores raw indices as uint32_t (selected to match the
  // 100k entries upper bound from the design memory budget). Guard the
  // narrowing cast so a hypothetical >UINT32_MAX append is observable
  // rather than silently wrapping.
  assert(entries_.size() < UINT32_MAX);
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
