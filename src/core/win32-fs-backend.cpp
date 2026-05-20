#include "core/win32-fs-backend.h"

#include <windows.h>
#include <lm.h>          // NET_API_STATUS, NERR_*
#include <winnetwk.h>    // WNetOpenEnum / WNetEnumResource

#include <cstring>
#include <utility>
#include <vector>

#include "core/name-arena.h"

namespace fast_explorer::core {

namespace {

class Win32Handle : public EnumerationHandle {
 public:
  Win32Handle() noexcept : EnumerationHandle(BackendKind::Win32) {}

  // Two enumeration sources sit behind the same handle type so we
  // keep a single BackendKind / static_cast site in the dispatch
  // path. FileFind is the FindFirstFileEx loop used for ordinary
  // directories. ShareEnum holds the share-name list collected from
  // NetShareEnum on a server-only UNC path ("\\server"); next()
  // walks the vector and synthesises FileEntry rows so the rest of
  // the UI sees them as folders.
  enum class Mode { FileFind, ShareEnum };
  Mode mode = Mode::FileFind;

  // FileFind state.
  HANDLE findHandle = INVALID_HANDLE_VALUE;
  WIN32_FIND_DATAW data{};
  bool hasPendingFirstResult = false;

  // ShareEnum state. Names are pre-copied out of the NetApi buffer
  // so we can free that buffer in openEnumeration and keep handle
  // lifetime decoupled from NetApi.
  std::vector<std::wstring> shareNames;
  std::size_t shareIndex = 0;

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
    // Network-side "can't reach this UNC location" failures land
    // here. Map them to PathNotFound so the user sees "경로를 찾을
    // 수 없습니다" instead of a vague "내부 오류".
    case ERROR_BAD_NETPATH:          // 53 — network path not found
    case ERROR_BAD_NET_NAME:         // 67 — share name not found
    case ERROR_NETNAME_DELETED:      // 64 — share went away mid-op
    case ERROR_NETWORK_UNREACHABLE:  // 1231
    case ERROR_HOST_UNREACHABLE:     // 1232
    case ERROR_DUP_NAME:             // 52 — duplicate server name
      return EnumerationError::PathNotFound;
    // Auth / domain failures: surface as access denied so the user
    // knows the network found the share but rejected credentials.
    case ERROR_LOGON_FAILURE:        // 1326
    case ERROR_NO_LOGON_SERVERS:     // 1311
    case ERROR_SESSION_CREDENTIAL_CONFLICT:  // 1219
      return EnumerationError::AccessDenied;
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
  // dwReserved0 carries the reparse tag only when REPARSE_POINT is set;
  // its value is undefined otherwise, hence the gate.
  if ((d.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
      d.dwReserved0 == IO_REPARSE_TAG_SYMLINK) {
    entry.flags |= file_entry_flags::kIsSymlink;
  }
  return entry;
}

// True for any "server-only" UNC, in either internal form
// ("\\?\UNC\server[\]") or raw form ("\\server[\]"). The path
// reaching openEnumeration may be either because the upstream
// pane code preserves the display form, not the internal form.
// Local "C:\..." paths skip the UNC branch entirely.
bool isServerOnlyUncInternal(const std::wstring& path) noexcept {
  static constexpr std::wstring_view kPrefix = L"\\\\?\\UNC\\";
  std::wstring_view body;
  if (path.size() > kPrefix.size() &&
      path.compare(0, kPrefix.size(), kPrefix) == 0) {
    body = std::wstring_view(path.data() + kPrefix.size(),
                              path.size() - kPrefix.size());
  } else if (path.size() >= 2 &&
             (path[0] == L'\\' || path[0] == L'/') &&
             (path[1] == L'\\' || path[1] == L'/')) {
    body = std::wstring_view(path.data() + 2, path.size() - 2);
  } else {
    return false;
  }
  while (!body.empty() && (body.back() == L'\\' || body.back() == L'/')) {
    body.remove_suffix(1);
  }
  if (body.empty()) return false;
  return body.find_first_of(L"\\/") == std::wstring_view::npos;
}

// Extracts the server name from either internal-form
// ("\\?\UNC\server[\...]") or raw-form ("\\server[\...]") UNC
// paths. Returns empty if the input doesn't look like UNC.
std::wstring extractServerName(const std::wstring& path) {
  static constexpr std::wstring_view kPrefix = L"\\\\?\\UNC\\";
  std::wstring tail;
  if (path.size() > kPrefix.size() &&
      path.compare(0, kPrefix.size(), kPrefix) == 0) {
    tail = path.substr(kPrefix.size());
  } else if (path.size() >= 2 &&
             (path[0] == L'\\' || path[0] == L'/') &&
             (path[1] == L'\\' || path[1] == L'/')) {
    tail = path.substr(2);
  } else {
    return {};
  }
  const size_t sep = tail.find_first_of(L"\\/");
  if (sep == std::wstring::npos) return tail;
  return tail.substr(0, sep);
}

EnumerationError mapWNetError(DWORD s) noexcept {
  switch (s) {
    case NO_ERROR:                     return EnumerationError::None;
    case ERROR_ACCESS_DENIED:          return EnumerationError::AccessDenied;
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_NET_NAME:
    case ERROR_NOT_FOUND:
    case ERROR_NO_NETWORK:
    case ERROR_NETWORK_UNREACHABLE:
    case ERROR_HOST_UNREACHABLE:       return EnumerationError::PathNotFound;
    case ERROR_LOGON_FAILURE:          return EnumerationError::AccessDenied;
    default:                           return EnumerationError::Internal;
  }
}

Result<std::unique_ptr<EnumerationHandle>> openShareEnum(
    const std::wstring& server) {
  // WNetOpenEnum + WNetEnumResource matches what `net view \\server`
  // does — they go through the multi-provider router (mpr.dll) and
  // talk to whatever network provider claims the path. NetShareEnum
  // (netapi32) talks RPC-over-SMB to LANMAN named pipes directly,
  // which modern NAS / SMB2-only hosts often block while still
  // serving regular SMB file ops just fine. We hit that exact gap
  // against the user's NAS at 10.10.10.23.
  std::wstring remoteName = L"\\\\";
  remoteName.append(server);
  NETRESOURCEW spec{};
  spec.dwScope = RESOURCE_GLOBALNET;
  spec.dwType = RESOURCETYPE_DISK;
  spec.dwDisplayType = RESOURCEDISPLAYTYPE_SERVER;
  spec.dwUsage = RESOURCEUSAGE_CONTAINER;
  spec.lpRemoteName = const_cast<LPWSTR>(remoteName.c_str());
  HANDLE hEnum = nullptr;
  const DWORD openStatus = WNetOpenEnumW(
      RESOURCE_GLOBALNET, RESOURCETYPE_DISK, 0, &spec, &hEnum);
  if (openStatus != NO_ERROR) {
    return Result<std::unique_ptr<EnumerationHandle>>::failure(
        mapWNetError(openStatus));
  }
  auto handle = std::make_unique<Win32Handle>();
  handle->mode = Win32Handle::Mode::ShareEnum;
  // 16 KB scratch buffer. WNetEnumResource returns as many entries
  // as fit; loop until ERROR_NO_MORE_ITEMS. NETRESOURCE structs are
  // packed at the front, the string data they point into trails at
  // the end of the same buffer.
  constexpr DWORD kBufBytes = 16 * 1024;
  auto buf = std::make_unique<unsigned char[]>(kBufBytes);
  while (true) {
    DWORD count = static_cast<DWORD>(-1);  // ask for as many as fit
    DWORD bufSize = kBufBytes;
    const DWORD enumStatus = WNetEnumResourceW(
        hEnum, &count, buf.get(), &bufSize);
    if (enumStatus == ERROR_NO_MORE_ITEMS) break;
    if (enumStatus != NO_ERROR) {
      WNetCloseEnum(hEnum);
      return Result<std::unique_ptr<EnumerationHandle>>::failure(
          mapWNetError(enumStatus));
    }
    auto* entries = reinterpret_cast<NETRESOURCEW*>(buf.get());
    for (DWORD i = 0; i < count; ++i) {
      const NETRESOURCEW& e = entries[i];
      if (e.lpRemoteName == nullptr) continue;
      // lpRemoteName is "\\server\share" — extract the share leaf.
      std::wstring_view full(e.lpRemoteName);
      const size_t sep = full.rfind(L'\\');
      if (sep == std::wstring_view::npos ||
          sep + 1 >= full.size()) continue;
      std::wstring_view share = full.substr(sep + 1);
      // Hide admin shares (ending with $) so users see the same
      // list Win Explorer shows.
      if (!share.empty() && share.back() == L'$') continue;
      handle->shareNames.emplace_back(share);
    }
  }
  WNetCloseEnum(hEnum);
  return Result<std::unique_ptr<EnumerationHandle>>::success(std::move(handle));
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

  // Server-only UNC (e.g. "\\10.10.10.23" or "\\?\UNC\server") —
  // list the server's shares via WNetEnumResource instead of
  // FindFirstFile, which would bail on a path with no share to walk.
  // Matches Win Explorer.
  if (isServerOnlyUncInternal(path)) {
    const std::wstring server = extractServerName(path);
    if (server.empty()) {
      return Result<std::unique_ptr<EnumerationHandle>>::failure(
          EnumerationError::InvalidSyntax);
    }
    return openShareEnum(server);
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

  // ShareEnum mode: walk the pre-collected share-name vector and
  // synthesise one directory FileEntry per share. No I/O happens
  // here — NetShareEnum did all the round-tripping in openEnumeration.
  if (h->mode == Win32Handle::Mode::ShareEnum) {
    if (h->shareIndex >= h->shareNames.size()) {
      return Result<std::optional<FileEntry>>::success(std::nullopt);
    }
    const std::wstring& name = h->shareNames[h->shareIndex++];
    const std::wstring_view interned = h->nameArena.intern(name);
    FileEntry e{};
    e.namePtr = interned.data();
    e.nameLength = static_cast<uint16_t>(interned.size());
    e.extensionOffset = kNoExtension;
    e.attributes = FILE_ATTRIBUTE_DIRECTORY;
    e.flags = file_entry_flags::kIsDirectory;
    return Result<std::optional<FileEntry>>::success(
        std::optional<FileEntry>(e));
  }

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
