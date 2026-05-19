#include "ui/pane-toolbar-row.h"

#include "ui/messages.h"

namespace fast_explorer::ui {

namespace {
constexpr wchar_t kClassName[] = L"FastExplorer.PaneToolbarRow";

// Width of one nav toolbar button in DIP. Stock icons are 16x16
// (small) at 96 DPI; the toolbar pads ~6 px on each side for the
// hot-track frame. 26 px gives a comfortable click target without
// shrinking the address bar significantly.
constexpr int kNavButtonDipW = 26;
constexpr int kNavButtonCount = 4;
constexpr int kNavBarDipW = kNavButtonDipW * kNavButtonCount;
constexpr int kHamburgerDipW = 28;

int scaleDip(int dip, UINT dpi) noexcept {
  return MulDiv(dip, static_cast<int>(dpi), 96);
}
}  // namespace

PaneToolbarRow::~PaneToolbarRow() { destroy(); }

bool PaneToolbarRow::registerClassOnce(HINSTANCE instance) {
  static bool registered = false;
  if (registered) return true;
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = &PaneToolbarRow::wndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  // No background brush — child controls cover the row entirely once
  // they are positioned, so a paint here would just flash on resize.
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.lpszClassName = kClassName;
  registered = RegisterClassExW(&wc) != 0;
  return registered;
}

bool PaneToolbarRow::create(HWND parent, HINSTANCE instance,
                            std::size_t paneIdx) {
  if (hwnd_ != nullptr) return true;
  if (!registerClassOnce(instance)) return false;
  paneIdx_ = paneIdx;
  hwnd_ = CreateWindowExW(
      0, kClassName, L"",
      WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
      0, 0, 0, 0, parent, nullptr, instance, this);
  if (hwnd_ == nullptr) return false;
  if (!createNavToolbar(instance)) {
    // Toolbar creation is non-fatal; the address bar can still own
    // the full row. Leaves navToolbar_ as nullptr so layout() skips it.
  }
  if (!createHamburger(instance)) {
    // Same fallback policy as the nav toolbar.
  }
  return true;
}

bool PaneToolbarRow::createHamburger(HINSTANCE instance) {
  // Plain push button with a glyph label. The packed command ID lands
  // in WM_COMMAND alongside the nav-toolbar IDs and is routed by
  // MainWindow::onCommand to TrackPopupMenuEx.
  hamburger_ = CreateWindowExW(
      0, L"BUTTON", L"≡",  // ≡
      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT, 0, 0, 0, 0, hwnd_,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(
          packCmd(kTbHamburger, paneIdx_))),
      instance, nullptr);
  return hamburger_ != nullptr;
}

bool PaneToolbarRow::createNavToolbar(HINSTANCE instance) {
  navToolbar_ = CreateWindowExW(
      0, TOOLBARCLASSNAMEW, L"",
      WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TRANSPARENT |
          TBSTYLE_TOOLTIPS | TBSTYLE_LIST | CCS_NORESIZE | CCS_NODIVIDER |
          CCS_NOPARENTALIGN,
      0, 0, 0, 0, hwnd_, nullptr, instance, nullptr);
  if (navToolbar_ == nullptr) return false;

  // TB_BUTTONSTRUCTSIZE is required before any TB_ADDBUTTONS so the
  // common-controls v6 size is what the toolbar walks.
  SendMessageW(navToolbar_, TB_BUTTONSTRUCTSIZE,
               static_cast<WPARAM>(sizeof(TBBUTTON)), 0);

  // Icon strategy: Unicode arrow glyphs as button text rather than
  // bitmaps. SIID_FOLDERUP/SIID_REFRESH aren't in every SDK, and
  // shipping a custom icon set would expand v0.2 scope; native font
  // rendering scales cleanly per-DPI and matches the menu glyphs
  // we'll use in T4.
  const wchar_t* labels[kNavButtonCount] = {
      L"←",  // ← back
      L"→",  // → forward
      L"↑",  // ↑ up
      L"↻",  // ↻ refresh
  };
  INT_PTR strIdx[kNavButtonCount] = {};
  for (int i = 0; i < kNavButtonCount; ++i) {
    // TB_ADDSTRING wants a double-null-terminated block; pass one
    // string per call so the indices stay independent of insertion
    // order. The returned index is what TBBUTTON.iString uses.
    wchar_t buf[8]{};
    int n = 0;
    while (labels[i][n] != L'\0' && n < 6) {
      buf[n] = labels[i][n];
      ++n;
    }
    buf[n] = L'\0';
    buf[n + 1] = L'\0';
    strIdx[i] = SendMessageW(navToolbar_, TB_ADDSTRINGW, 0,
                              reinterpret_cast<LPARAM>(buf));
  }

  const WORD ids[kNavButtonCount] = {kTbBack, kTbForward, kTbUp, kTbRefresh};
  TBBUTTON btns[kNavButtonCount]{};
  for (int i = 0; i < kNavButtonCount; ++i) {
    btns[i].iBitmap = I_IMAGENONE;
    btns[i].idCommand = packCmd(ids[i], paneIdx_);
    btns[i].fsState = TBSTATE_ENABLED;
    btns[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
    btns[i].iString = strIdx[i];
  }
  SendMessageW(navToolbar_, TB_ADDBUTTONS,
               static_cast<WPARAM>(kNavButtonCount),
               reinterpret_cast<LPARAM>(btns));
  SendMessageW(navToolbar_, TB_AUTOSIZE, 0, 0);
  return true;
}

void PaneToolbarRow::destroy() {
  // Children (toolbar / hamburger) are owned by the row's HWND
  // lifetime and torn down by DestroyWindow cascade; explicit
  // nulling keeps the fields consistent if destroy() runs twice.
  navToolbar_ = nullptr;
  hamburger_ = nullptr;
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  addressBar_ = nullptr;
}

void PaneToolbarRow::setAddressBar(HWND addressBar) {
  addressBar_ = addressBar;
  if (addressBar != nullptr && hwnd_ != nullptr) {
    SetParent(addressBar, hwnd_);
  }
  layout();
}

void PaneToolbarRow::setNavButtonEnabled(int slotIdx, bool enabled) {
  if (navToolbar_ == nullptr) return;
  const WORD ids[kNavButtonCount] = {kTbBack, kTbForward, kTbUp, kTbRefresh};
  if (slotIdx < 0 || slotIdx >= kNavButtonCount) return;
  SendMessageW(navToolbar_, TB_ENABLEBUTTON,
               packCmd(ids[slotIdx], paneIdx_),
               static_cast<LPARAM>(MAKELONG(enabled ? 1 : 0, 0)));
}

void PaneToolbarRow::layout() {
  if (hwnd_ == nullptr) return;
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  const int w = rc.right - rc.left;
  const int h = rc.bottom - rc.top;
  if (w <= 0 || h <= 0) return;
  const UINT dpi = GetDpiForWindow(hwnd_);
  const int navW = navToolbar_ != nullptr ? scaleDip(kNavBarDipW, dpi) : 0;
  const int hambW = hamburger_ != nullptr ? scaleDip(kHamburgerDipW, dpi) : 0;
  if (navToolbar_ != nullptr) {
    SetWindowPos(navToolbar_, nullptr, 0, 0, navW, h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
  const int addrLeft = navW;
  const int addrW = w - addrLeft - hambW;
  if (addressBar_ != nullptr && addrW > 0) {
    SetWindowPos(addressBar_, nullptr, addrLeft, 0, addrW, h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
  if (hamburger_ != nullptr) {
    SetWindowPos(hamburger_, nullptr, w - hambW, 0, hambW, h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
}

LRESULT CALLBACK PaneToolbarRow::wndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                          LPARAM lParam) {
  PaneToolbarRow* self = nullptr;
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = static_cast<PaneToolbarRow*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    if (self) self->hwnd_ = hwnd;
  } else {
    self = reinterpret_cast<PaneToolbarRow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (self) {
    return self->handleMessage(hwnd, msg, wParam, lParam);
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT PaneToolbarRow::handleMessage(HWND hwnd, UINT msg, WPARAM wParam,
                                       LPARAM lParam) {
  switch (msg) {
    case WM_SIZE:
      layout();
      return 0;
    // Child controls send WM_COMMAND/WM_NOTIFY to their direct parent;
    // bubble them up to MainWindow so the existing accelerator and
    // address-bar dropdown routing keeps working unchanged.
    case WM_COMMAND:
    case WM_NOTIFY:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_DRAWITEM: {
      HWND parent = GetParent(hwnd);
      if (parent != nullptr) {
        return SendMessageW(parent, msg, wParam, lParam);
      }
      break;
    }
    case WM_NCDESTROY:
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      hwnd_ = nullptr;
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace fast_explorer::ui
