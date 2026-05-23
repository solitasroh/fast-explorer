#pragma once

#include <windows.h>
#include <ole2.h>
#include <shlobj.h>

#include <string>

#include "winui_lite/chrome/com-raii.h"

namespace fast_explorer::ui {

// SHParseDisplayName + SHBindToObject of an absolute filesystem path.
// Returns an empty ComPtr on any failure (path missing, ACL deny, etc.).
ComPtr<IShellFolder> bindFolderByPath(const std::wstring& path);

// IShellFolder::CreateViewObject(IID_IDropTarget) on the folder named
// by `path`. Empty ComPtr on failure.
ComPtr<IDropTarget> queryFolderDropTarget(const std::wstring& path,
                                           HWND ownerHwnd);

}  // namespace fast_explorer::ui
