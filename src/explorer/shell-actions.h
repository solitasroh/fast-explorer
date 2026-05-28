// shell-actions.h — single-shot Win32 shell verbs invoked from
// hamburger menu / accelerator handlers.
//
// Lives outside MainWindow so the registration-only TU
// (main-window-commands.cpp) can call them without dragging the
// entire main-window.cpp anonymous namespace along. Every entry
// point is best-effort: failures surface through the return value
// (bool) or via MessageBeep, never via exceptions.

#pragma once

#include <windows.h>

#include <string>

namespace fast_explorer::ui {

// ShellExecute "open" on `path`. No-op if `path` is empty.
void openInExplorer(const std::wstring& path) noexcept;

// Tries wt.exe → pwsh.exe → cmd.exe with the appropriate
// working-directory flag. All three are launched via ShellExecuteEx
// with SEE_MASK_FLAG_NO_UI so a missing binary surfaces silently and
// the next candidate runs. MessageBeep on full failure (rare; cmd is
// part of every Windows install). No-op for empty path.
void launchTerminalInFolder(const std::wstring& path, HWND owner) noexcept;

// Puts `path` on the system clipboard as CF_UNICODETEXT. Returns
// true on success; false on any of (empty path, OpenClipboard
// failure, GlobalAlloc / SetClipboardData failure). Caller may flash
// a status-bar message on false but should not abort.
bool copyPathToClipboard(const std::wstring& path, HWND owner) noexcept;

// Shows the shell folder-properties dialog for `path`. No-op for
// empty path.
void showFolderProperties(const std::wstring& path, HWND owner) noexcept;

}  // namespace fast_explorer::ui
