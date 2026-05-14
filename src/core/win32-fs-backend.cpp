#include "core/win32-fs-backend.h"

#include <windows.h>

#include <cstring>
#include <utility>

#include "core/name-arena.h"

namespace fast_explorer::core {

namespace {

class Win32Handle : public EnumerationHandle {
 public:
  Win32Handle() noexcept : EnumerationHandle(BackendKind::Win32) {}

  HANDLE findHandle = INVALID_HANDLE_VALUE;
  WIN32_FIND_DATAW data{};
  bool hasPendingFirstResult = false;
  // 1 MB per handle is plenty for a single FindFirstFileExW pass —
  // even 100k entries × average 24 wide chars = ~4.8 MB, but typical
  // single-directory enumeration stays well under one chunk.
  NameArena nameArena{1ull * 1024 * 1024};

  ~Win32Handle() override {
    if (findHandle != INVALID_HANDLE_VALUE) {
      FindClose(findHandle);
    }
  }
};

EnumerationError mapWin32Error(DWORD err) {
  switch (err) {
    case ERROR_PATH_NOT_FOUND:
      return EnumerationError::PathNotFound;
    case ERROR_FILE_NOT_FOUND:
      return EnumerationError::FileNotFound;
    case ERROR_ACCESS_DENIED:
      return EnumerationError::AccessDenied;
    case ERROR_SHARING_VIOLATION:
      return EnumerationError::SharingViolation;
    case ERROR_NOT_READY:
      return EnumerationError::NotReady;
    case ERROR_DIRECTORY:
      return EnumerationError::DirectoryNotSupported;
    default:
      return EnumerationError::Internal;
  }
}

bool isDotOrDotDot(const wchar_t* name) noexcept {
  if (name[0] != L'.') {
    return false;
  }
  if (name[1] == L'\0') {
    return true;
  }
  return name[1] == L'.' && name[2] == L'\0';
}

uint16_t findExtensionOffset(std::wstring_view name) noexcept {
  // Skip a leading '.' so dotfiles ("readme", ".bashrc") are treated
  // as having no extension. Scan from the end for the first '.'.
  for (std::size_t i = name.size(); i > 1; --i) {
    if (name[i - 1] == L'.') {
      return static_cast<uint16_t>(i - 1);
    }
  }
  return kNoExtension;
}

struct AttrFlagMapping {
  DWORD win32Attr;
  uint8_t entryFlag;
};

// The two cloud-placeholder bits use literal values because older
// Windows SDK headers do not expose FILE_ATTRIBUTE_RECALL_ON_*. The
// platform contract for the literal values themselves is stable.
constexpr AttrFlagMapping kAttrFlagTable[] = {
    {FILE_ATTRIBUTE_DIRECTORY,     file_entry_flags::kIsDirectory},
    {FILE_ATTRIBUTE_HIDDEN,        file_entry_flags::kIsHidden},
    {FILE_ATTRIBUTE_SYSTEM,        file_entry_flags::kIsSystem},
    {FILE_ATTRIBUTE_REPARSE_POINT, file_entry_flags::kIsReparse},
    {0x00400000u,                  file_entry_flags::kIsCloudPlaceholder},
    {0x00040000u,                  file_entry_flags::kIsCloudPlaceholder},
    {FILE_ATTRIBUTE_OFFLINE,       file_entry_flags::kIsCloudPlaceholder},
};

uint8_t mapAttributesToFlags(DWORD attrs) noexcept {
  uint8_t flags = 0;
  for (const auto& m : kAttrFlagTable) {
    if (attrs & m.win32Attr) {
      flags |= m.entryFlag;
    }
  }
  return flags;
}

FileEntry buildEntry(NameArena& arena, const WIN32_FIND_DATAW& d) {
  const std::wstring_view nameInput(d.cFileName);
  const std::wstring_view interned = arena.intern(nameInput);
  FileEntry entry{};
  entry.namePtr = interned.data();
  entry.nameLength = static_cast<uint16_t>(interned.size());
  entry.extensionOffset = findExtensionOffset(interned);
  entry.size = (static_cast<uint64_t>(d.nFileSizeHigh) << 32) |
               static_cast<uint64_t>(d.nFileSizeLow);
  entry.modifiedTime100ns =
      (static_cast<uint64_t>(d.ftLastWriteTime.dwHighDateTime) << 32) |
      static_cast<uint64_t>(d.ftLastWriteTime.dwLowDateTime);
  entry.attributes = d.dwFileAttributes;
  entry.flags = mapAttributesToFlags(d.dwFileAttributes);
  return entry;
}

}  // namespace

Result<std::unique_ptr<EnumerationHandle>> Win32FsBackend::openEnumeration(
    const std::wstring& path, std::stop_token tok) {
  if (tok.stop_requested()) {
    return Result<std::unique_ptr<EnumerationHandle>>::failure(
        EnumerationError::Canceled);
  }
  if (path.empty()) {
    return Result<std::unique_ptr<EnumerationHandle>>::failure(
        EnumerationError::InvalidSyntax);
  }

  std::wstring pattern = path;
  if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/') {
    pattern.push_back(L'\\');
  }
  pattern.append(L"*");

  auto handle = std::make_unique<Win32Handle>();
  if (!handle->nameArena.valid()) {
    return Result<std::unique_ptr<EnumerationHandle>>::failure(
        EnumerationError::Internal);
  }

  handle->findHandle = FindFirstFileExW(
      pattern.c_str(),
      FindExInfoBasic,
      &handle->data,
      FindExSearchNameMatch,
      nullptr,
      FIND_FIRST_EX_LARGE_FETCH);

  if (handle->findHandle == INVALID_HANDLE_VALUE) {
    const DWORD err = ::GetLastError();
    if (err == ERROR_FILE_NOT_FOUND) {
      // FindFirstFileEx returns FILE_NOT_FOUND when the wildcard finds
      // zero entries — '.' / '..' are normally guaranteed, so this
      // only happens for non-traversable inputs.
      return Result<std::unique_ptr<EnumerationHandle>>::failure(
          EnumerationError::PathNotFound);
    }
    return Result<std::unique_ptr<EnumerationHandle>>::failure(
        mapWin32Error(err));
  }

  handle->hasPendingFirstResult = true;
  return Result<std::unique_ptr<EnumerationHandle>>::success(std::move(handle));
}

Result<std::optional<FileEntry>> Win32FsBackend::next(
    EnumerationHandle& base, std::stop_token tok) {
  if (tok.stop_requested()) {
    return Result<std::optional<FileEntry>>::failure(
        EnumerationError::Canceled);
  }
  if (base.kind() != BackendKind::Win32) {
    return Result<std::optional<FileEntry>>::failure(
        EnumerationError::Internal);
  }
  auto* h = static_cast<Win32Handle*>(&base);
  if (h->findHandle == INVALID_HANDLE_VALUE) {
    return Result<std::optional<FileEntry>>::failure(
        EnumerationError::Internal);
  }

  while (true) {
    if (h->hasPendingFirstResult) {
      h->hasPendingFirstResult = false;
    } else {
      if (!FindNextFileW(h->findHandle, &h->data)) {
        const DWORD err = ::GetLastError();
        if (err == ERROR_NO_MORE_FILES) {
          return Result<std::optional<FileEntry>>::success(std::nullopt);
        }
        return Result<std::optional<FileEntry>>::failure(mapWin32Error(err));
      }
    }
    if (isDotOrDotDot(h->data.cFileName)) {
      continue;
    }
    return Result<std::optional<FileEntry>>::success(
        std::optional<FileEntry>(buildEntry(h->nameArena, h->data)));
  }
}

}  // namespace fast_explorer::core
