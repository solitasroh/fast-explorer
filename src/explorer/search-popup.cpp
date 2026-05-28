#include "explorer/search-popup.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>

#include "winui_lite/chrome/dpi-scale.h"
#include "explorer/messages.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace fast_explorer::ui {

namespace {

constexpr const wchar_t* kPopupClassName = L"FastExplorer.SearchPopup";
constexpr int kPopupWidthDip   = 560;  // Spotlight-ish wide entry
constexpr int kPopupHeightDip  = 64;   // single-line tall enough for 20pt
constexpr int kEditMarginDip   = 16;   // padding around the edit
constexpr int kFontPointSize   = 20;
constexpr int kTopOffsetDip    = 120;  // distance from owner's client top
constexpr UINT_PTR kEditSubclassId = 1;

HFONT createSpotlightFont(UINT dpi) noexcept {
  LOGFONTW lf{};
  // -MulDiv((point * dpi) / 72): Win32 idiom for "give me an N-pt
  // font on the current DPI". Negative height means cell, not lead.
  lf.lfHeight = -MulDiv(kFontPointSize, static_cast<int>(dpi), 72);
  lf.lfWeight = FW_NORMAL;
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfQuality = CLEARTYPE_QUALITY;
  // Segoe UI matches Win11 chrome; Variable falls back to a sensible
  // system font on older Windows.
  lstrcpyW(lf.lfFaceName, L"Segoe UI Variable");
  HFONT f = CreateFontIndirectW(&lf);
  if (f) return f;
  lstrcpyW(lf.lfFaceName, L"Segoe UI");
  return CreateFontIndirectW(&lf);
}

void applyRoundedCorners(HWND hwnd) noexcept {
  // DWM rounded corners (Win11+). Older Windows silently ignores
  // the call and the popup keeps square corners — graceful
  // fallback, no need to feature-gate the API access.
  // 2 = DWMWCP_ROUND, 36 = DWMWA_WINDOW_CORNER_PREFERENCE.
  const DWORD pref = 2;
  DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */,
                        &pref, sizeof(pref));
}

}  // namespace

SearchPopup::SearchPopup(HWND owner) : owner_(owner) {}

SearchPopup::~SearchPopup() {
  uninstallMouseHook();
  if (font_) {
    DeleteObject(font_);
    font_ = nullptr;
  }
  if (popup_) {
    DestroyWindow(popup_);
    popup_ = nullptr;
  }
}

bool SearchPopup::isVisible() const noexcept {
  return popup_ != nullptr && IsWindowVisible(popup_);
}

void SearchPopup::hide() noexcept {
  uninstallMouseHook();
  if (popup_ && IsWindowVisible(popup_)) {
    ShowWindow(popup_, SW_HIDE);
  }
  // Return keyboard focus to the owner so the active pane regains
  // it without the caller having to do this explicitly.
  if (owner_) SetFocus(owner_);
}

std::wstring SearchPopup::currentText() const {
  if (!edit_) return {};
  const int len = GetWindowTextLengthW(edit_);
  if (len <= 0) return {};
  std::wstring out(static_cast<std::size_t>(len), L'\0');
  GetWindowTextW(edit_, out.data(), len + 1);
  out.resize(static_cast<std::size_t>(len));
  return out;
}

void SearchPopup::ensurePopupCreated() {
  if (popup_) return;
  HINSTANCE inst = reinterpret_cast<HINSTANCE>(
      GetWindowLongPtrW(owner_, GWLP_HINSTANCE));
  WNDCLASSW wc{};
  wc.style = CS_DROPSHADOW;  // drop shadow under the rounded popup
  wc.lpfnWndProc = &SearchPopup::popupWndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kPopupClassName;
  RegisterClassW(&wc);  // idempotent
  popup_ = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
      kPopupClassName, L"",
      WS_POPUP,
      0, 0, kPopupWidthDip, kPopupHeightDip,
      owner_, nullptr, inst, this);
  if (!popup_) return;
  applyRoundedCorners(popup_);

  const UINT dpi = GetDpiForWindow(owner_);
  const int margin = scaleForDpi(kEditMarginDip, dpi);
  const int w = scaleForDpi(kPopupWidthDip, dpi);
  const int h = scaleForDpi(kPopupHeightDip, dpi);
  edit_ = CreateWindowExW(
      0, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
      margin, margin, w - 2 * margin, h - 2 * margin,
      popup_, nullptr, inst, nullptr);
  if (!edit_) {
    DestroyWindow(popup_);
    popup_ = nullptr;
    return;
  }
  if (!font_) font_ = createSpotlightFont(dpi);
  if (font_) {
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
  }
  SetWindowSubclass(edit_, &SearchPopup::editSubclassProc,
                    kEditSubclassId,
                    reinterpret_cast<DWORD_PTR>(this));
}

void SearchPopup::positionAboveOwner() {
  if (!owner_ || !popup_) return;
  RECT owr{};
  GetClientRect(owner_, &owr);
  POINT topLeft{0, 0};
  ClientToScreen(owner_, &topLeft);
  const UINT dpi = GetDpiForWindow(owner_);
  const int w = scaleForDpi(kPopupWidthDip, dpi);
  const int h = scaleForDpi(kPopupHeightDip, dpi);
  const int clientW = owr.right - owr.left;
  // Center horizontally, anchor a fixed offset from the client top
  // so the popup floats over the address-bar / list area in the
  // Spotlight-style "screen-top" pose.
  const int x = topLeft.x + std::max(0, (clientW - w) / 2);
  const int y = topLeft.y + scaleForDpi(kTopOffsetDip, dpi);
  SetWindowPos(popup_, HWND_TOPMOST, x, y, w, h,
               SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

void SearchPopup::show(std::size_t paneIdx) {
  activePaneIdx_ = paneIdx;
  ensurePopupCreated();
  if (!popup_) return;
  // Select-all on re-show so a quick Ctrl+F twice clears the prior
  // query without forcing the user to manually erase it. Matches
  // the Windows Explorer / browser convention.
  if (edit_) SendMessageW(edit_, EM_SETSEL, 0, -1);
  positionAboveOwner();
  installMouseHook();
  // The popup itself is WS_EX_TOOLWINDOW + non-focusable on show,
  // but SetFocus on the EDIT child works correctly because the
  // edit is a regular WS_CHILD without the no-activate restriction.
  SetForegroundWindow(popup_);
  if (edit_) SetFocus(edit_);
}

LRESULT CALLBACK SearchPopup::popupWndProc(HWND hwnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam) {
  SearchPopup* self = nullptr;
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = static_cast<SearchPopup*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<SearchPopup*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (self) {
    return self->handlePopupMessage(hwnd, msg, wParam, lParam);
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT SearchPopup::handlePopupMessage(HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_COMMAND:
      // EN_CHANGE on the edit child → forward latest text to owner.
      if (HIWORD(wParam) == EN_CHANGE && edit_ &&
          reinterpret_cast<HWND>(lParam) == edit_) {
        postQueryToOwner();
      }
      return 0;
    case WM_NCDESTROY:
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK SearchPopup::editSubclassProc(HWND hwnd, UINT msg,
                                                WPARAM wParam, LPARAM lParam,
                                                UINT_PTR /*id*/,
                                                DWORD_PTR refData) {
  auto* self = reinterpret_cast<SearchPopup*>(refData);
  if (msg == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, &SearchPopup::editSubclassProc,
                         kEditSubclassId);
    return DefSubclassProc(hwnd, msg, wParam, lParam);
  }
  if (msg == WM_KEYDOWN && wParam == VK_ESCAPE && self) {
    self->postDismissToOwner();
    self->hide();
    return 0;
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void SearchPopup::postQueryToOwner() {
  if (!owner_) return;
  std::wstring text = currentText();
  // Heap-owned payload — receiver deletes after reading. mirrors the
  // address-bar popup's pick-payload pattern so the message-pump
  // boundary stays move-friendly without a static buffer race.
  auto* payload = new (std::nothrow) std::wstring(std::move(text));
  if (!payload) return;
  if (!PostMessageW(owner_, kWmFeFilterQuery,
                    static_cast<WPARAM>(activePaneIdx_),
                    reinterpret_cast<LPARAM>(payload))) {
    delete payload;
  }
}

void SearchPopup::postDismissToOwner() {
  if (!owner_) return;
  PostMessageW(owner_, kWmFeFilterDismiss,
               static_cast<WPARAM>(activePaneIdx_), 0);
}

// --- Outside-click hook -------------------------------------------
// Mirrors AddressBarPopup pattern: thread_local hook owner so two
// concurrent popups (impossible here but cheap to future-proof)
// cannot cross-fire. The hook only filters left-button-down
// outside the popup rect and forwards everything else.

namespace {
thread_local SearchPopup* tHookOwner = nullptr;
}

void SearchPopup::installMouseHook() {
  if (mouseHook_) return;
  tHookOwner = this;
  mouseHook_ = SetWindowsHookExW(WH_MOUSE, &SearchPopup::mouseHookProc,
                                 nullptr, GetCurrentThreadId());
}

void SearchPopup::uninstallMouseHook() {
  if (mouseHook_) {
    UnhookWindowsHookEx(mouseHook_);
    mouseHook_ = nullptr;
  }
  if (tHookOwner == this) tHookOwner = nullptr;
}

LRESULT CALLBACK SearchPopup::mouseHookProc(int code, WPARAM wParam,
                                             LPARAM lParam) {
  if (code >= 0 && wParam == WM_LBUTTONDOWN && tHookOwner) {
    const auto* m = reinterpret_cast<const MOUSEHOOKSTRUCT*>(lParam);
    if (m && !tHookOwner->containsScreenPoint(m->pt)) {
      tHookOwner->postDismissToOwner();
      tHookOwner->hide();
    }
  }
  return CallNextHookEx(nullptr, code, wParam, lParam);
}

bool SearchPopup::containsScreenPoint(POINT pt) const noexcept {
  if (!popup_ || !IsWindowVisible(popup_)) return false;
  RECT r{};
  GetWindowRect(popup_, &r);
  return PtInRect(&r, pt) != FALSE;
}

}  // namespace fast_explorer::ui
