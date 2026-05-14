#include "core/path-utils.h"

#include <shlobj.h>
#include <stdio.h>

namespace fast_explorer::core {

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
