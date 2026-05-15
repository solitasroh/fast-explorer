#include "core/path-utils.h"

#include <shlobj.h>
#include <stdio.h>

#include <cwctype>
#include <vector>

namespace fast_explorer::core {

namespace {

constexpr std::wstring_view kDosPrefix = L"\\\\?\\";
constexpr std::wstring_view kDosUncPrefix = L"\\\\?\\UNC\\";

bool startsWith(std::wstring_view s, std::wstring_view prefix) noexcept {
  return s.size() >= prefix.size() &&
         s.compare(0, prefix.size(), prefix) == 0;
}

bool hasDriveLetterRoot(std::wstring_view s) noexcept {
  // e.g. "C:\foo" or "C:" (root only). Drive letter + ':' is required.
  if (s.size() < 2) {
    return false;
  }
  const wchar_t c = s[0];
  const bool letter = (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
  return letter && s[1] == L':';
}

// Validates the body of a path (drive prefix already consumed). Colons
// inside the body are rejected since NTFS treats them as the alternate
// data stream separator. Caller passes the substring AFTER the drive letter
// + ':' to keep that legal `C:` byte out of the scan.
bool containsInvalidPathChar(std::wstring_view body) noexcept {
  for (wchar_t c : body) {
    switch (c) {
      case L'<': case L'>': case L'"':
      case L'|': case L'?': case L'*':
      case L':':  // ADS separator — not allowed in regular path bodies.
        return true;
      default:
        if (static_cast<unsigned>(c) < 0x20u) {
          return true;
        }
        break;
    }
  }
  return false;
}

void normalizeSeparators(std::wstring& s) noexcept {
  for (auto& c : s) {
    if (c == L'/') {
      c = L'\\';
    }
  }
}

}  // namespace

namespace {

// True when `path` is just a root we cannot create (drive root or extended
// prefix root). CreateDirectoryW returns ERROR_ACCESS_DENIED in those cases.
bool isUncreatableRoot(std::wstring_view path) noexcept {
  // "C:\" or "\\?\C:\" — strip a single trailing backslash for the test.
  std::wstring_view trimmed = path;
  if (!trimmed.empty() && trimmed.back() == L'\\') {
    trimmed.remove_suffix(1);
  }
  if (trimmed.size() == 2 && trimmed[1] == L':') {
    return true;
  }
  // "\\?\C:" — the extended-prefix drive root.
  if (startsWith(trimmed, kDosPrefix) && trimmed.size() == kDosPrefix.size() + 2) {
    return true;
  }
  return false;
}

}  // namespace

bool ensureDirectoryRecursive(const wchar_t* path) noexcept {
  if (path == nullptr || path[0] == L'\0') {
    return false;
  }
  // Iterative + heap-backed buffer so long paths up to the \\?\ limit work
  // and we do not risk noexcept stack overflow on pathological input.
  std::wstring buffer;
  try {
    buffer.assign(path);
  } catch (...) {
    return false;
  }

  // Walk backwards, splitting off parents into a stack-like vector. Each
  // ancestor is created top-down at the end.
  std::vector<size_t> separatorAt;
  for (size_t i = buffer.size(); i > 0; --i) {
    if (buffer[i - 1] == L'\\' || buffer[i - 1] == L'/') {
      try {
        separatorAt.push_back(i - 1);
      } catch (...) {
        return false;
      }
    }
  }

  // Try each prefix from shortest to full. Existing dirs are OK; reaching a
  // root that cannot be created is OK; anything else fails.
  for (auto it = separatorAt.rbegin(); it != separatorAt.rend(); ++it) {
    const wchar_t saved = buffer[*it];
    buffer[*it] = L'\0';
    const wchar_t* prefix = buffer.c_str();
    if (prefix[0] != L'\0' && !isUncreatableRoot(prefix)) {
      if (!CreateDirectoryW(prefix, nullptr)) {
        const DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS && err != ERROR_ACCESS_DENIED) {
          buffer[*it] = saved;
          return false;
        }
      }
    }
    buffer[*it] = saved;
  }

  if (isUncreatableRoot(buffer)) {
    // The caller asked us to "create" a drive root; treat as success if it
    // already exists, since we cannot create roots.
    return GetFileAttributesW(buffer.c_str()) != INVALID_FILE_ATTRIBUTES;
  }
  if (CreateDirectoryW(buffer.c_str(), nullptr)) {
    return true;
  }
  return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool isUncPath(std::wstring_view path) noexcept {
  if (startsWith(path, kDosUncPrefix)) {
    return true;
  }
  // Detect both backslash and forward-slash UNC forms before separator
  // normalization. Boundary helper, so we cannot rely on having normalized
  // separators yet.
  auto isSep = [](wchar_t c) noexcept { return c == L'\\' || c == L'/'; };
  if (path.size() >= 2 && isSep(path[0]) && isSep(path[1])) {
    // \\?\ followed by a drive (e.g. \\?\C:\) is NOT UNC, just an extended
    // local path. The dedicated DOS UNC prefix handled above is the only UNC
    // form once \\?\ is in play.
    return !startsWith(path, kDosPrefix);
  }
  return false;
}

namespace {

// True for "X:\..." (drive letter + ':' + separator). A bare "X:" is a
// drive-relative path, not a valid \\?\-internal form.
bool isAbsoluteDrivePath(std::wstring_view s) noexcept {
  return s.size() >= 3 && hasDriveLetterRoot(s) &&
         (s[2] == L'\\' || s[2] == L'/');
}

}  // namespace

PathConvertError toInternal(std::wstring_view displayPath, std::wstring& out) {
  if (displayPath.empty()) {
    return PathConvertError::Empty;
  }
  // UNC detection must happen before separator normalization so that both
  // \\server\share and //server/share are caught here instead of falling
  // through to the relative-path branch.
  if (isUncPath(displayPath)) {
    return PathConvertError::UncUnsupported;
  }

  out.assign(displayPath);
  normalizeSeparators(out);

  if (startsWith(out, kDosPrefix)) {
    // Already in internal form; only validate the residual after the prefix.
    std::wstring_view tail(out.data() + kDosPrefix.size(),
                           out.size() - kDosPrefix.size());
    if (!isAbsoluteDrivePath(tail)) {
      // Bare "C:" or empty tail is not a usable normalized path.
      return PathConvertError::InvalidSyntax;
    }
    // Skip the drive prefix (e.g. "C:") when scanning for ADS colons.
    if (containsInvalidPathChar(tail.substr(2))) {
      return PathConvertError::InvalidSyntax;
    }
    return PathConvertError::None;
  }

  if (!isAbsoluteDrivePath(out)) {
    return PathConvertError::RelativeUnsupported;
  }
  if (containsInvalidPathChar(std::wstring_view(out).substr(2))) {
    return PathConvertError::InvalidSyntax;
  }

  std::wstring prefixed;
  prefixed.reserve(kDosPrefix.size() + out.size());
  prefixed.append(kDosPrefix);
  prefixed.append(out);
  out.swap(prefixed);
  return PathConvertError::None;
}

std::wstring toDisplay(std::wstring_view internalPath) {
  if (startsWith(internalPath, kDosUncPrefix)) {
    // \\?\UNC\server\share -> \\server\share. We do not produce these
    // internally, but the helper is symmetric for completeness.
    std::wstring out;
    out.reserve(internalPath.size());
    out.append(L"\\\\");
    out.append(internalPath.substr(kDosUncPrefix.size()));
    return out;
  }
  if (startsWith(internalPath, kDosPrefix)) {
    return std::wstring(internalPath.substr(kDosPrefix.size()));
  }
  return std::wstring(internalPath);
}

bool resolveAppDataSubdir(const wchar_t* sub, std::wstring& out) {
  if (sub == nullptr || sub[0] == L'\0') {
    return false;
  }

  wchar_t portable[MAX_PATH];
  const DWORD portableLen = GetEnvironmentVariableW(
      L"FAST_EXPLORER_PORTABLE_ROOT", portable, _countof(portable));
  if (portableLen > 0 && portableLen < _countof(portable)) {
    out.assign(portable, portableLen);
    out.append(L"\\");
    out.append(sub);
    return true;
  }

  PWSTR localAppData = nullptr;
  const HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData);
  if (FAILED(hr) || localAppData == nullptr) {
    if (localAppData) {
      CoTaskMemFree(localAppData);
    }
    return false;
  }
  out.assign(localAppData);
  CoTaskMemFree(localAppData);
  out.append(L"\\FastExplorer\\");
  out.append(sub);
  return true;
}

}  // namespace fast_explorer::core
