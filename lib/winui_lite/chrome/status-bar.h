// status-bar.h — STATUSCLASSNAMEW wrapper with dark-mode subclass.
//
// The Win32 status-bar common control has no native dark theme — even
// SetWindowTheme(L"DarkMode_...") is a no-op for it. This wrapper owns
// the bottom-of-window child HWND plus a subclass that paints the
// background and SB_GETTEXTW string itself when isAppInDarkMode()
// returns true.
//
// The wrapper produces nothing user-readable: callers stay responsible
// for the wchar_t* string passed to setText(). It only knows how to
// create the HWND, lay out a single full-width part, and tint itself
// to match the surrounding chrome.

#pragma once

#include <windows.h>

namespace fast_explorer::ui {

class StatusBar {
 public:
  // Creates the STATUSCLASSNAMEW child of `parent` and installs the
  // dark-mode subclass. Returns false on CreateWindowExW failure.
  // Idempotent: calling create() again on a live wrapper is a no-op
  // and returns true.
  bool create(HWND parent, HINSTANCE instance);

  // SB_SETPARTS to a single part that extends to the right edge.
  // Idempotent. No-op if the wrapper is uncreated.
  void applySinglePart();

  // SB_SETTEXTW on `part`. No-op if `text` is null, `part` is
  // negative, or the wrapper is uncreated.
  void setText(int part, const wchar_t* text);

  // Window height in pixels (window rect; includes whatever the
  // theme contributes). 0 if uncreated.
  int height() const;

  // Forward a WM_SIZE to the status bar so it autopositions itself
  // at the parent's bottom edge. Parent must call this from its own
  // WM_SIZE handler.
  void forwardSize();

  HWND hwnd() const { return hwnd_; }
  bool valid() const { return hwnd_ != nullptr; }

 private:
  HWND hwnd_ = nullptr;
};

}  // namespace fast_explorer::ui
