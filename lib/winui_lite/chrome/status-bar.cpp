#include "winui_lite/chrome/status-bar.h"

#include <commctrl.h>

#include <new>
#include <string>

#include "winui_lite/chrome/theme-watcher.h"

namespace fast_explorer::ui {

namespace {

// Cached brush + theme decision for the dark-mode subclass. Lives on
// the heap so neither registry probes nor GDI brush creation hit the
// WM_PAINT hot path (status bar is repainted on every marquee-select
// tick when dark-mode is on). One state object per status-bar HWND;
// freed at WM_NCDESTROY.
struct StatusBarTheme {
  bool dark = false;
  HBRUSH bgBrush = nullptr;
  COLORREF bgColor = 0;
  COLORREF textColor = 0;
  bool valid = false;
  void refresh() noexcept {
    dark = isAppInDarkMode();
    bgColor = dark ? RGB(32, 32, 32) : GetSysColor(COLOR_BTNFACE);
    textColor = dark ? RGB(241, 241, 241) : GetSysColor(COLOR_BTNTEXT);
    if (bgBrush != nullptr) DeleteObject(bgBrush);
    bgBrush = CreateSolidBrush(bgColor);
    valid = true;
  }
};

LRESULT CALLBACK statusBarSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR idSubclass, DWORD_PTR refData) {
  auto* state = reinterpret_cast<StatusBarTheme*>(refData);
  if (msg == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, &statusBarSubclassProc, idSubclass);
    if (state != nullptr) {
      if (state->bgBrush != nullptr) DeleteObject(state->bgBrush);
      delete state;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
  }
  if (msg == WM_THEMECHANGED || msg == WM_SYSCOLORCHANGE) {
    if (state != nullptr) state->refresh();
    InvalidateRect(hwnd, nullptr, TRUE);
    return DefSubclassProc(hwnd, msg, wParam, lParam);
  }
  if (msg == WM_ERASEBKGND) {
    return 1;  // WM_PAINT fills the whole client below.
  }
  if (msg == WM_PAINT && state != nullptr) {
    if (!state->valid) state->refresh();
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);
    FillRect(hdc, &client, state->bgBrush);
    HFONT statusFont = reinterpret_cast<HFONT>(
        SendMessageW(hwnd, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = statusFont ? SelectObject(hdc, statusFont) : nullptr;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, state->textColor);
    const int parts =
        static_cast<int>(SendMessageW(hwnd, SB_GETPARTS, 0, 0));
    for (int i = 0; i < parts; ++i) {
      RECT partRc{};
      SendMessageW(hwnd, SB_GETRECT, i,
                   reinterpret_cast<LPARAM>(&partRc));
      // Dynamic-size text fetch: long paths (\\?\... up to 32767
      // wchars) would silently truncate inside a fixed buffer.
      // SB_GETTEXTLENGTHW returns the length without the null
      // terminator; +2 covers it plus one byte of safety slack.
      const LRESULT lenLr =
          SendMessageW(hwnd, SB_GETTEXTLENGTHW, i, 0);
      const int len = LOWORD(lenLr);
      if (len > 0) {
        std::wstring buf(static_cast<size_t>(len) + 2, L'\0');
        SendMessageW(hwnd, SB_GETTEXTW, i,
                     reinterpret_cast<LPARAM>(buf.data()));
        buf.resize(static_cast<size_t>(len));
        RECT textRc = partRc;
        textRc.left += 6;  // matches the inset Win32 status bar uses
        DrawTextW(hdc, buf.data(), len, &textRc,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX |
                      DT_END_ELLIPSIS);
      }
    }
    if (oldFont) SelectObject(hdc, oldFont);
    EndPaint(hwnd, &ps);
    return 0;
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

}  // namespace

bool StatusBar::create(HWND parent, HINSTANCE instance) {
  if (hwnd_ != nullptr) return true;
  hwnd_ = CreateWindowExW(
      0, STATUSCLASSNAMEW, L"",
      WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
      parent, nullptr, instance, nullptr);
  if (hwnd_ == nullptr) return false;
  // Subclass takes over WM_PAINT so the status bar tints to dark
  // mode alongside the title bar + toolbar row. refData carries a
  // heap StatusBarTheme that caches the brush / theme decision so
  // WM_PAINT does not hit the registry or allocate a brush per
  // frame; the subclass owns the lifetime (deleted in WM_NCDESTROY).
  auto* state = new (std::nothrow) StatusBarTheme{};
  if (state != nullptr) {
    SetWindowSubclass(hwnd_, &statusBarSubclassProc, 0,
                      reinterpret_cast<DWORD_PTR>(state));
  }
  return true;
}

void StatusBar::applySinglePart() {
  if (hwnd_ == nullptr) return;
  // Always a single full-width part. The trailing -1 sentinel tells
  // Win32 SB_SETPARTS to extend the part to the right edge.
  int edges[1] = {-1};
  SendMessageW(hwnd_, SB_SETPARTS, 1,
               reinterpret_cast<LPARAM>(&edges[0]));
}

void StatusBar::setText(int part, const wchar_t* text) {
  if (hwnd_ == nullptr || text == nullptr || part < 0) return;
  SendMessageW(hwnd_, SB_SETTEXTW, static_cast<WPARAM>(part),
               reinterpret_cast<LPARAM>(text));
}

int StatusBar::height() const {
  if (hwnd_ == nullptr) return 0;
  RECT rc{};
  GetWindowRect(hwnd_, &rc);
  return rc.bottom - rc.top;
}

void StatusBar::forwardSize() {
  if (hwnd_ == nullptr) return;
  SendMessageW(hwnd_, WM_SIZE, 0, 0);
}

}  // namespace fast_explorer::ui
