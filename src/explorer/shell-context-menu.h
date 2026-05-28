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
  // Optional extra submenu appended to the end of the shell context
  // menu. IDs in `items[].id` MUST be above the shell verb range
  // (0x7FFF) so they don't collide with shell-assigned verb IDs. When
  // the user picks an item from this submenu, the show() call returns
  // without invoking any shell verb; the caller is responsible for
  // dispatching the picked id via WM_COMMAND. show() posts the
  // WM_COMMAND on the caller's behalf (PostMessage, so TrackPopupMenuEx
  // fully unwinds before the dispatcher runs).
  struct ExtraSubmenu {
    std::wstring label;  // top-level entry text, e.g. L"분류 방법"
    struct Item {
      WORD id;
      std::wstring label;
    };
    std::vector<Item> items;
    // Radio-group display: ids in [radioFirst, radioLast] are rendered
    // as a radio group with `radioChecked` checked. Set all three to 0
    // to skip radio rendering. Items outside the radio range render
    // as normal menu strings.
    WORD radioFirst   = 0;
    WORD radioLast    = 0;
    WORD radioChecked = 0;
  };

  // `folderPath` is the absolute path of the pane's current folder.
  // `selectedLeaves` is empty for an empty-area click (background
  // menu); otherwise it lists leaf names of every target item.
  // `screenPt` is the screen-coordinate anchor for TrackPopupMenuEx.
  // `extra` may be nullptr to skip the augmentation (old behaviour).
  static void show(HWND ownerHwnd, const std::wstring& folderPath,
                   const std::vector<std::wstring>& selectedLeaves,
                   POINT screenPt,
                   const ExtraSubmenu* extra = nullptr);
};

}  // namespace fast_explorer::ui
