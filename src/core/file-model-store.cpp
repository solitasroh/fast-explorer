#include "core/file-model-store.h"

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
  nameArena_.reset();
  ++generation_;
}

AppendResult FileModelStore::appendEntry(const FileEntry& source) {
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
  entries_.push_back(copy);
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

}  // namespace fast_explorer::core
