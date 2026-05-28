// search-popup.h — Mac-Spotlight-style floating search box.
//
// Owns a top-level WS_POPUP window centered above the parent client
// rect, hosting a large single-line EDIT control. Every keystroke
// posts kWmFeFilterQuery to the owner with the current text;
// debouncing + filter application happen on the owner side (atom
// F3). ESC + outside-click hide the popup and post kWmFeFilterDismiss.
//
// Lifetime: one instance per MainWindow, created during onCreate and
// destroyed with the owner. The popup HWND is created lazily on
// first show() to avoid registering a window class that's never
// used. show() / hide() are cheap and re-entrant safe.
//
// Active-pane carry: the popup remembers which pane triggered it so
// kWmFeFilterQuery / kWmFeFilterDismiss can route back to the
// originating pane even if the user activates the other pane
// between show and dismiss.

#pragma once

#include <windows.h>

#include <cstddef>
#include <string>

namespace fast_explorer::ui {

class SearchPopup {
 public:
  explicit SearchPopup(HWND owner);
  ~SearchPopup();

  SearchPopup(const SearchPopup&) = delete;
  SearchPopup& operator=(const SearchPopup&) = delete;

  // Show the popup over the parent, focus the EDIT, and remember
  // `paneIdx` for routing. Idempotent: subsequent show() calls just
  // re-focus the existing popup and update the captured pane.
  void show(std::size_t paneIdx);

  // Hide without destroying the popup window — show() can be called
  // again. Idempotent. Does NOT post kWmFeFilterDismiss; the caller
  // chooses when to clear the filter (e.g. ESC posts dismiss, but
  // re-show on the other pane keeps the filter live).
  void hide() noexcept;

  // Returns the EDIT text immediately (used by F3 debounce timer
  // to read the latest query without racing against keystrokes).
  // Empty if the popup is not visible.
  [[nodiscard]] std::wstring currentText() const;

  // Sets the active pane index without showing the popup. Used by
  // MainWindow::setActivePane so the popup, when next shown, routes
  // queries to the correct pane.
  void setActivePane(std::size_t paneIdx) noexcept { activePaneIdx_ = paneIdx; }

  [[nodiscard]] bool isVisible() const noexcept;

 private:
  static LRESULT CALLBACK popupWndProc(HWND, UINT, WPARAM, LPARAM);
  static LRESULT CALLBACK editSubclassProc(HWND, UINT, WPARAM, LPARAM,
                                           UINT_PTR, DWORD_PTR);
  static LRESULT CALLBACK mouseHookProc(int, WPARAM, LPARAM);
  LRESULT handlePopupMessage(HWND, UINT, WPARAM, LPARAM);
  void ensurePopupCreated();
  void installMouseHook();
  void uninstallMouseHook();
  [[nodiscard]] bool containsScreenPoint(POINT pt) const noexcept;
  void postQueryToOwner();
  void postDismissToOwner();
  void positionAboveOwner();

  HWND owner_ = nullptr;
  HWND popup_ = nullptr;
  HWND edit_ = nullptr;
  HFONT font_ = nullptr;
  HHOOK mouseHook_ = nullptr;
  std::size_t activePaneIdx_ = 0;
};

}  // namespace fast_explorer::ui
