#include "explorer/shell-pidl-location.h"

#include <windows.h>
#include <knownfolders.h>
#include <shobjidl.h>

#include "core/location.h"

namespace fast_explorer::ui {

namespace {

bool pidlEqualsKnownFolder(LPCITEMIDLIST absolute, REFKNOWNFOLDERID id) {
  if (absolute == nullptr) {
    return false;
  }
  PIDLIST_ABSOLUTE known = nullptr;
  if (FAILED(SHGetKnownFolderIDList(id, KF_FLAG_DEFAULT, nullptr, &known)) ||
      known == nullptr) {
    return false;
  }
  const bool equal = ILIsEqual(absolute, known) != FALSE;
  CoTaskMemFree(known);
  return equal;
}

std::wstring parsingNameForPidl(LPCITEMIDLIST absolute) {
  PWSTR raw = nullptr;
  if (FAILED(SHGetNameFromIDList(absolute, SIGDN_DESKTOPABSOLUTEPARSING,
                                 &raw)) ||
      raw == nullptr) {
    return {};
  }
  std::wstring out(raw);
  CoTaskMemFree(raw);
  return out;
}

std::wstring serializeShellLocation(const std::wstring& text) {
  const auto location = fast_explorer::core::parseLocation(text);
  if (location.kind != fast_explorer::core::LocationKind::FileSystemPath) {
    return fast_explorer::core::serializeLocation(location);
  }
  return text;
}

}  // namespace

std::wstring addressLocationForPidl(LPCITEMIDLIST absolute) {
  if (absolute == nullptr) {
    return {};
  }

  if (pidlEqualsKnownFolder(absolute, FOLDERID_ComputerFolder)) {
    return L"shell:ThisPC";
  }
  if (pidlEqualsKnownFolder(absolute, FOLDERID_NetworkFolder)) {
    return L"shell:Network";
  }
  if (pidlEqualsKnownFolder(absolute, FOLDERID_NetHood)) {
    return L"shell:NetHood";
  }

  wchar_t path[MAX_PATH]{};
  if (SHGetPathFromIDListW(absolute, path) && path[0] != L'\0') {
    return std::wstring(path);
  }

  return serializeShellLocation(parsingNameForPidl(absolute));
}

}  // namespace fast_explorer::ui
