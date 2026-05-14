#include "core/memory-fs-backend.h"

#include <cassert>
#include <utility>

namespace fast_explorer::core {

namespace {

class DirHandle : public EnumerationHandle {
 public:
  DirHandle() noexcept : EnumerationHandle(BackendKind::Memory) {}

  std::wstring path;
  std::vector<FileEntry> entries;
  std::size_t position = 0;
  std::atomic<int>* handleCounter = nullptr;

  ~DirHandle() override {
    if (handleCounter != nullptr) {
      handleCounter->fetch_sub(1, std::memory_order_acq_rel);
    }
  }
};

}  // namespace

MemoryFsBackend::MemoryFsBackend() = default;

MemoryFsBackend::~MemoryFsBackend() {
  assert(handleCount_.load(std::memory_order_acquire) == 0 &&
         "MemoryFsBackend destroyed while enumeration handles are still alive");
}

void MemoryFsBackend::addDirectory(const std::wstring& path) {
  tree_[path];
}

void MemoryFsBackend::addEntry(const std::wstring& directory,
                               std::wstring_view name,
                               uint64_t size,
                               uint8_t flags) {
  DirState& state = tree_[directory];
  const std::wstring_view interned = nameArena_.intern(name);
  assert((name.empty() || !interned.empty()) &&
         "NameArena exhausted while populating MemoryFsBackend");
  FileEntry entry{};
  entry.namePtr = interned.data();
  entry.nameLength = static_cast<uint16_t>(interned.size());
  entry.extensionOffset = kNoExtension;
  entry.size = size;
  entry.flags = flags;
  state.entries.push_back(entry);
}

EnumerationError MemoryFsBackend::openHook(const std::wstring& /*path*/) {
  return EnumerationError::None;
}

EnumerationError MemoryFsBackend::nextHook(const std::wstring& /*path*/,
                                            std::size_t /*position*/) {
  return EnumerationError::None;
}

Result<std::unique_ptr<EnumerationHandle>> MemoryFsBackend::openEnumeration(
    const std::wstring& path, std::stop_token tok) {
  if (tok.stop_requested()) {
    return Result<std::unique_ptr<EnumerationHandle>>::failure(
        EnumerationError::Canceled);
  }
  const EnumerationError injected = openHook(path);
  if (injected != EnumerationError::None) {
    return Result<std::unique_ptr<EnumerationHandle>>::failure(injected);
  }
  auto it = tree_.find(path);
  if (it == tree_.end()) {
    return Result<std::unique_ptr<EnumerationHandle>>::failure(
        EnumerationError::PathNotFound);
  }
  auto handle = std::make_unique<DirHandle>();
  handle->path = path;
  handle->entries = it->second.entries;
  handle->handleCounter = &handleCount_;
  handleCount_.fetch_add(1, std::memory_order_acq_rel);
  return Result<std::unique_ptr<EnumerationHandle>>::success(std::move(handle));
}

Result<std::optional<FileEntry>> MemoryFsBackend::next(
    EnumerationHandle& handle, std::stop_token tok) {
  if (tok.stop_requested()) {
    return Result<std::optional<FileEntry>>::failure(
        EnumerationError::Canceled);
  }
  if (handle.kind() != BackendKind::Memory) {
    return Result<std::optional<FileEntry>>::failure(
        EnumerationError::Internal);
  }
  auto* dirHandle = static_cast<DirHandle*>(&handle);
  const EnumerationError injected =
      nextHook(dirHandle->path, dirHandle->position);
  if (injected != EnumerationError::None) {
    return Result<std::optional<FileEntry>>::failure(injected);
  }
  if (dirHandle->position >= dirHandle->entries.size()) {
    return Result<std::optional<FileEntry>>::success(std::nullopt);
  }
  const FileEntry entry = dirHandle->entries[dirHandle->position];
  ++dirHandle->position;
  return Result<std::optional<FileEntry>>::success(
      std::optional<FileEntry>(entry));
}

// ---------------------------------------------------------------------------
// FaultInjectingMemoryFsBackend
// ---------------------------------------------------------------------------

void FaultInjectingMemoryFsBackend::setOpenError(const std::wstring& directory,
                                                  EnumerationError e) {
  faults_[directory].openError = e;
}

void FaultInjectingMemoryFsBackend::setNextError(const std::wstring& directory,
                                                  std::size_t pos,
                                                  EnumerationError e) {
  FaultSpec& spec = faults_[directory];
  spec.nextErrorPos = pos;
  spec.nextError = e;
}

EnumerationError FaultInjectingMemoryFsBackend::openHook(
    const std::wstring& path) {
  auto it = faults_.find(path);
  if (it == faults_.end()) {
    return EnumerationError::None;
  }
  return it->second.openError;
}

EnumerationError FaultInjectingMemoryFsBackend::nextHook(
    const std::wstring& path, std::size_t position) {
  auto it = faults_.find(path);
  if (it == faults_.end()) {
    return EnumerationError::None;
  }
  const FaultSpec& spec = it->second;
  if (spec.nextError != EnumerationError::None &&
      position == spec.nextErrorPos) {
    return spec.nextError;
  }
  return EnumerationError::None;
}

}  // namespace fast_explorer::core
