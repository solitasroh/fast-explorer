#include "ui/shell-actions.h"

#include <shellapi.h>

#include <cstring>

namespace fast_explorer::ui {

namespace {

bool tryLaunch(HWND owner, const wchar_t* file, const std::wstring& args,
               const std::wstring& workDir) noexcept {
  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
  info.hwnd = owner;
  info.lpVerb = L"open";
  info.lpFile = file;
  info.lpParameters = args.empty() ? nullptr : args.c_str();
  info.lpDirectory = workDir.empty() ? nullptr : workDir.c_str();
  info.nShow = SW_SHOWNORMAL;
  return ShellExecuteExW(&info) != FALSE;
}

}  // namespace

void openInExplorer(const std::wstring& path) noexcept {
  if (path.empty()) return;
  ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr,
                SW_SHOWNORMAL);
}

void launchTerminalInFolder(const std::wstring& path, HWND owner) noexcept {
  if (path.empty()) return;
  const std::wstring quotedPath = L"\"" + path + L"\"";
  if (tryLaunch(owner, L"wt.exe", L"-d " + quotedPath, path)) return;
  if (tryLaunch(owner, L"pwsh.exe",
                L"-NoExit -WorkingDirectory " + quotedPath, path))
    return;
  if (tryLaunch(owner, L"cmd.exe", L"/K cd /D " + quotedPath, path)) return;
  MessageBeep(MB_ICONWARNING);
}

bool copyPathToClipboard(const std::wstring& path, HWND owner) noexcept {
  if (path.empty()) return false;
  if (!OpenClipboard(owner)) return false;
  EmptyClipboard();
  const size_t bytes = (path.size() + 1) * sizeof(wchar_t);
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (mem == nullptr) {
    CloseClipboard();
    return false;
  }
  void* dst = GlobalLock(mem);
  if (dst == nullptr) {
    GlobalFree(mem);
    CloseClipboard();
    return false;
  }
  std::memcpy(dst, path.c_str(), bytes);
  GlobalUnlock(mem);
  if (SetClipboardData(CF_UNICODETEXT, mem) == nullptr) {
    // SetClipboardData failure leaves ownership with us; otherwise
    // the system owns mem and we must NOT free it.
    GlobalFree(mem);
    CloseClipboard();
    return false;
  }
  CloseClipboard();
  return true;
}

void showFolderProperties(const std::wstring& path, HWND owner) noexcept {
  if (path.empty()) return;
  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  // SEE_MASK_INVOKEIDLIST tells the shell to look up the IDList for
  // the path and invoke the "properties" verb against it; this is
  // what surfaces the standard folder properties dialog.
  info.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_NO_UI;
  info.hwnd = owner;
  info.lpVerb = L"properties";
  info.lpFile = path.c_str();
  info.nShow = SW_SHOW;
  ShellExecuteExW(&info);
}

}  // namespace fast_explorer::ui
