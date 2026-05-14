#pragma once

#include <memory>
#include <optional>
#include <stop_token>
#include <string>

#include "core/file-entry.h"
#include "core/fs-backend.h"

namespace fast_explorer::core {

// Production IFsBackend backed by FindFirstFileExW / FindNextFileW.
// Each enumeration handle owns its own NameArena, so FileEntry::namePtr
// stays valid for the handle's lifetime and the caller can copy names
// into a per-pane arena at consume time.
class Win32FsBackend : public IFsBackend {
 public:
  Win32FsBackend() = default;
  ~Win32FsBackend() override = default;

  Result<std::unique_ptr<EnumerationHandle>> openEnumeration(
      const std::wstring& path, std::stop_token tok) override;

  Result<std::optional<FileEntry>> next(
      EnumerationHandle& handle, std::stop_token tok) override;
};

}  // namespace fast_explorer::core
