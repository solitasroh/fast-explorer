#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace fast_explorer::ui {

// Shell context menu invocation for a pane's right-click. Synchronous:
// resolves PIDLs, calls IShellFolder::GetUIObjectOf (or
// CreateViewObject for empty-area background), TrackPopupMenuEx, and
// IContextMenu::InvokeCommand on the UI thread. Owner subclass
// forwards WM_INITMENUPOPUP / WM_DRAWITEM / WM_MEASUREITEM / WM_MENUCHAR
// to IContextMenu2/3 so owner-draw shell verbs (Open With, Share, etc.)
// render correctly.
class ShellContextMenu {
 public:
  // `folderPath` is the absolute path of the pane's current folder.
  // `selectedLeaves` is empty for an empty-area click (background
  // menu); otherwise it lists leaf names of every target item.
  // `screenPt` is the screen-coordinate anchor for TrackPopupMenuEx.
  static void show(HWND ownerHwnd, const std::wstring& folderPath,
                   const std::vector<std::wstring>& selectedLeaves,
                   POINT screenPt);
};

}  // namespace fast_explorer::ui
