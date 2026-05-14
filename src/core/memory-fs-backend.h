#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/file-entry.h"
#include "core/fs-backend.h"
#include "core/name-arena.h"

namespace fast_explorer::core {

class MemoryFsBackend : public IFsBackend {
 public:
  MemoryFsBackend();
  ~MemoryFsBackend() override;

  void addDirectory(const std::wstring& path);

  // Adds a child entry under `directory`. Name is interned into the
  // backend's NameArena so the returned FileEntry::namePtr stays
  // valid for the backend's lifetime.
  void addEntry(const std::wstring& directory,
                std::wstring_view name,
                uint64_t size = 0,
                uint8_t flags = 0);

  Result<std::unique_ptr<EnumerationHandle>> openEnumeration(
      const std::wstring& path, std::stop_token tok) override;

  Result<std::optional<FileEntry>> next(
      EnumerationHandle& handle, std::stop_token tok) override;

 protected:
  // Subclasses may override to inject errors at openEnumeration / next
  // time. Returning anything other than None aborts the call with the
  // returned error.
  virtual EnumerationError openHook(const std::wstring& path);
  virtual EnumerationError nextHook(const std::wstring& path,
                                    std::size_t position);

 private:
  struct DirState {
    std::vector<FileEntry> entries;
  };

  NameArena nameArena_;
  std::unordered_map<std::wstring, DirState> tree_;
  std::atomic<int> handleCount_{0};

  friend struct MemoryFsDirHandleAccess;
};

class FaultInjectingMemoryFsBackend : public MemoryFsBackend {
 public:
  void setOpenError(const std::wstring& directory, EnumerationError e);
  void setNextError(const std::wstring& directory,
                    std::size_t pos,
                    EnumerationError e);

 protected:
  EnumerationError openHook(const std::wstring& path) override;
  EnumerationError nextHook(const std::wstring& path,
                            std::size_t position) override;

 private:
  struct FaultSpec {
    EnumerationError openError = EnumerationError::None;
    std::size_t nextErrorPos = 0;
    EnumerationError nextError = EnumerationError::None;
  };

  std::unordered_map<std::wstring, FaultSpec> faults_;
};

}  // namespace fast_explorer::core
