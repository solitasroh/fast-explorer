// address-bar-popup.h — TreeView popup for the address bar's
// dropdown. Owns a top-level WS_POPUP window that hosts a real
// WC_TREEVIEW; nodes are lazy-expanded on TVN_ITEMEXPANDING so the
// shell-namespace walk cost stays proportional to what the user
// actually opens, not to the whole namespace.
//
// Lifetime: one instance per MainWindow, created during onCreate and
// destroyed with the owner. The popup HWND is created lazily on
// first show() to avoid registering a window class that's never used.
//
// Memory model: each TVITEM.lParam holds a CoTaskMemFree-able
// absolute PIDL. TVN_DELETEITEM frees them; TreeView_DeleteAllItems
// cascades the notification so cleanup happens once.

#pragma once

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>

#include <string>

namespace fast_explorer::ui {

class AddressBarPopup {
 public:
  explicit AddressBarPopup(HWND owner);
  ~AddressBarPopup();

  AddressBarPopup(const AddressBarPopup&) = delete;
  AddressBarPopup& operator=(const AddressBarPopup&) = delete;

  // Show the popup anchored under `anchor`. `currentPath` (optional)
  // is used to expand the tree along the path and highlight the leaf.
  void showFor(HWND anchor, const std::wstring& currentPath);

  // Forwarded to kWmFeAddressPopupPick::lParam so the owner can
  // route the pick to the originating pane.
  void setActivePane(std::size_t paneIdx) noexcept { activePaneIdx_ = paneIdx; }

  // Update the highlighted leaf to match `currentPath`. No-op if the
  // popup is not currently visible; called from openFolder so the
  // tree state stays consistent for the next show.
  void reflectCurrentPath(const std::wstring& currentPath);

  // Hide without destroying the popup window — show() can be called
  // again. Idempotent.
  void hide();

  bool isVisible() const noexcept;

 private:
  static LRESULT CALLBACK popupWndProc(HWND, UINT, WPARAM, LPARAM);
  static LRESULT CALLBACK treeSubclassProc(HWND, UINT, WPARAM, LPARAM,
                                            UINT_PTR, DWORD_PTR);
  static LRESULT CALLBACK mouseHookProc(int, WPARAM, LPARAM);
  LRESULT handlePopupMessage(HWND, UINT, WPARAM, LPARAM);
  LRESULT onTreeNotify(NMHDR* hdr);
  void onTreeExpanding(NMHDR* hdr);
  void onTreeDeleteItem(NMHDR* hdr);
  void onTreeClick();
  void ensurePopupCreated();
  void installMouseHook();
  void uninstallMouseHook();
  bool containsScreenPoint(POINT pt) const noexcept;
  void populateRoots();
  void expandNode(HTREEITEM node);
  void selectPath(const std::wstring& path);
  HTREEITEM findChildByPath(HTREEITEM parent, const std::wstring& fsPath);
  void commitSelection(HTREEITEM node);

  HWND owner_ = nullptr;
  HWND popup_ = nullptr;
  HWND tree_ = nullptr;
  HHOOK mouseHook_ = nullptr;
  std::wstring pendingPath_;
  bool rootsLoaded_ = false;
  std::size_t activePaneIdx_ = 0;
};

}  // namespace fast_explorer::ui
