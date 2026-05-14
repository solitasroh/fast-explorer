#include "core/path-utils.h"

#include <shlobj.h>
#include <stdio.h>

#include <cwctype>

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

bool containsInvalidPathChar(std::wstring_view s) noexcept {
  // Characters forbidden by NTFS / Win32 path syntax. Backslash and colon are
  // allowed because they are path syntax. The trailing-NUL is implicit.
  for (wchar_t c : s) {
    switch (c) {
      case L'<': case L'>': case L'"':
      case L'|': case L'?': case L'*':
        return true;
      default:
        if (c < 0x20) {
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

bool ensureDirectoryRecursive(const wchar_t* path) noexcept {
  if (path == nullptr || path[0] == L'\0') {
    return false;
  }
  if (CreateDirectoryW(path, nullptr)) {
    return true;
  }
  const DWORD err = GetLastError();
  if (err == ERROR_ALREADY_EXISTS) {
    return true;
  }
  if (err != ERROR_PATH_NOT_FOUND) {
    return false;
  }
  wchar_t parent[MAX_PATH * 2];
  const size_t pathLen = wcsnlen(path, _countof(parent) - 1);
  if (pathLen == 0) {
    return false;
  }
  wcsncpy_s(parent, _countof(parent), path, _TRUNCATE);
  // Strip last component.
  bool stripped = false;
  for (size_t i = pathLen; i > 0; --i) {
    if (parent[i - 1] == L'\\' || parent[i - 1] == L'/') {
      parent[i - 1] = L'\0';
      stripped = true;
      break;
    }
  }
  if (!stripped || parent[0] == L'\0') {
    return false;
  }
  if (!ensureDirectoryRecursive(parent)) {
    return false;
  }
  return CreateDirectoryW(path, nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool isUncPath(std::wstring_view path) noexcept {
  if (startsWith(path, kDosUncPrefix)) {
    return true;
  }
  if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
    // \\?\ followed by a drive (e.g. \\?\C:\) is NOT UNC, just an extended
    // local path. The dedicated DOS UNC prefix handled above is the only UNC
    // form once \\?\ is in play.
    return !startsWith(path, kDosPrefix);
  }
  return false;
}

PathConvertError toInternal(std::wstring_view displayPath, std::wstring& out) {
  if (displayPath.empty()) {
    return PathConvertError::Empty;
  }
  if (isUncPath(displayPath)) {
    return PathConvertError::UncUnsupported;
  }

  // Copy + normalize separators first so the rest of the routine works on
  // backslashes only.
  out.assign(displayPath);
  normalizeSeparators(out);

  if (startsWith(out, kDosPrefix)) {
    // Already in internal form; only validate the residual after the prefix.
    std::wstring_view tail(out.data() + kDosPrefix.size(),
                           out.size() - kDosPrefix.size());
    if (tail.empty() || !hasDriveLetterRoot(tail)) {
      return PathConvertError::InvalidSyntax;
    }
    if (containsInvalidPathChar(tail)) {
      return PathConvertError::InvalidSyntax;
    }
    return PathConvertError::None;
  }

  if (!hasDriveLetterRoot(out)) {
    return PathConvertError::RelativeUnsupported;
  }
  if (containsInvalidPathChar(out)) {
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
