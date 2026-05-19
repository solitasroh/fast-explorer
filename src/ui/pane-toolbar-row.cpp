#include "ui/pane-toolbar-row.h"

#include <iterator>

#include "ui/messages.h"

namespace fast_explorer::ui {

namespace {
constexpr wchar_t kClassName[] = L"FastExplorer.PaneToolbarRow";

// Sizing for the row. kRowInnerDipH is the visual height every
// interactive child snaps to (toolbar buttons, address-bar textbox,
// hamburger button) so they share a baseline. The row itself is set
// from MainWindow::onSize (currently 36 DIP), giving symmetric top
// and bottom padding = (rowH - kRowInnerDipH) / 2.
constexpr int kRowInnerDipH  = 28;
constexpr int kNavButtonDipW = 32;
constexpr int kNavButtonCount = 4;
constexpr int kNavBarDipW = kNavButtonDipW * kNavButtonCount;
constexpr int kHamburgerDipW = 32;

int scaleDip(int dip, UINT dpi) noexcept {
  return MulDiv(dip, static_cast<int>(dpi), 96);
}

// True when a font family with `face` is installed system-wide.
// Used to pick Segoe Fluent Icons (Windows 11) when available and
// fall back to Segoe MDL2 Assets (Windows 10) when not.
bool isFontInstalled(const wchar_t* face) noexcept {
  HDC dc = GetDC(nullptr);
  if (dc == nullptr) return false;
  LOGFONTW lf{};
  lf.lfCharSet = DEFAULT_CHARSET;
  for (size_t i = 0; face[i] != L'\0' && i < LF_FACESIZE - 1; ++i) {
    lf.lfFaceName[i] = face[i];
  }
  bool found = false;
  EnumFontFamiliesExW(
      dc, &lf,
      [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM lParam) -> int {
        *reinterpret_cast<bool*>(lParam) = true;
        return 0;  // stop enumeration
      },
      reinterpret_cast<LPARAM>(&found), 0);
  ReleaseDC(nullptr, dc);
  return found;
}

// Picks the system-shipped icon font with a Windows-version fallback
// chain: Fluent Icons (Win11) → MDL2 Assets (Win10) → arrow.
// `outFace` receives the chosen face name so callers (notably the
// glyph codepoint table) can branch when the older MDL2 codepoints
// differ from Fluent.
const wchar_t* pickIconFontFace() noexcept {
  static const wchar_t* cached = []() {
    if (isFontInstalled(L"Segoe Fluent Icons")) return L"Segoe Fluent Icons";
    if (isFontInstalled(L"Segoe MDL2 Assets"))  return L"Segoe MDL2 Assets";
    // Final fallback: the empty face name lets CreateFontIndirectW
    // substitute the system default, glyph IDs will render as
    // missing-glyph boxes but the buttons still function.
    return L"";
  }();
  return cached;
}

// System-shipped icon font, 13pt scaled to row DPI. Both Segoe
// Fluent Icons (Win11) and Segoe MDL2 Assets (Win10) share the
// codepoint range we use for nav (E72A/E72B/E70E/E72C); the
// hamburger glyph (E712 More) is in both as well.
HFONT createIconFont(UINT dpi) noexcept {
  LOGFONTW lf{};
  lf.lfHeight = -MulDiv(13, static_cast<int>(dpi), 96);
  lf.lfWeight = FW_NORMAL;
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfQuality = CLEARTYPE_QUALITY;
  const wchar_t* face = pickIconFontFace();
  for (size_t i = 0; face[i] != L'\0' && i < LF_FACESIZE - 1; ++i) {
    lf.lfFaceName[i] = face[i];
  }
  return CreateFontIndirectW(&lf);
}
// System text font applied to the address bar so its visible
// textbox is bigger than the OS default 9pt. 11pt strikes a
// balance — readable, matches the bumped row height, and keeps
// the address still under the row's vertical budget.
HFONT createRowFont(UINT dpi) noexcept {
  LOGFONTW lf{};
  lf.lfHeight = -MulDiv(11, static_cast<int>(dpi), 96);
  lf.lfWeight = FW_NORMAL;
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfQuality = CLEARTYPE_QUALITY;
  const wchar_t kFace[] = L"Segoe UI";
  for (size_t i = 0; i < std::size(kFace); ++i) {
    lf.lfFaceName[i] = kFace[i];
  }
  return CreateFontIndirectW(&lf);
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
  mdl2Font_ = createIconFont(GetDpiForWindow(hwnd_));
  rowFont_ = createRowFont(GetDpiForWindow(hwnd_));
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
  // Plain push button labeled with the MDL2/Fluent "More" glyph
  // (U+E712 ⋯) — the per-context action-menu glyph Microsoft uses
  // in File Explorer / Settings command bars and OneDrive context
  // overflows. The packed command ID lands in WM_COMMAND alongside
  // the nav-toolbar IDs and is routed by MainWindow::onCommand to
  // TrackPopupMenuEx.
  hamburger_ = CreateWindowExW(
      0, L"BUTTON", L"",  // Segoe MDL2/Fluent More (E712)
      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT, 0, 0, 0, 0, hwnd_,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(
          packCmd(kTbHamburger, paneIdx_))),
      instance, nullptr);
  if (hamburger_ != nullptr && mdl2Font_ != nullptr) {
    SendMessageW(hamburger_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(mdl2Font_), TRUE);
  }
  return hamburger_ != nullptr;
}

bool PaneToolbarRow::createNavToolbar(HINSTANCE instance) {
  // TBSTYLE_LIST puts text to the right of the image; with our
  // text-only (image-less) buttons it collapses the layout to a
  // left-anchored text and looks visibly off-center. Dropping it
  // lets the toolbar fall back to the default "text centered in
  // button" layout.
  navToolbar_ = CreateWindowExW(
      0, TOOLBARCLASSNAMEW, L"",
      WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TRANSPARENT |
          TBSTYLE_TOOLTIPS | CCS_NORESIZE | CCS_NODIVIDER |
          CCS_NOPARENTALIGN,
      0, 0, 0, 0, hwnd_, nullptr, instance, nullptr);
  if (navToolbar_ == nullptr) return false;

  // TB_BUTTONSTRUCTSIZE is required before any TB_ADDBUTTONS so the
  // common-controls v6 size is what the toolbar walks.
  SendMessageW(navToolbar_, TB_BUTTONSTRUCTSIZE,
               static_cast<WPARAM>(sizeof(TBBUTTON)), 0);
  // No image area — buttons are text-only (Lucide glyph as label).
  // Without this the toolbar reserves space for a default 16x15
  // bitmap and the buttons end up taller than the row.
  SendMessageW(navToolbar_, TB_SETBITMAPSIZE, 0, MAKELONG(0, 0));
  // Pin button size to (width=kNavButtonDipW, height=kRowInnerDipH).
  // Width gives glyph breathing room; height matches the address
  // bar's textbox so the row reads as one aligned strip.
  const UINT rowDpi = GetDpiForWindow(hwnd_);
  const int btnW = scaleDip(kNavButtonDipW, rowDpi);
  const int btnH = scaleDip(kRowInnerDipH, rowDpi);
  SendMessageW(navToolbar_, TB_SETBUTTONSIZE, 0, MAKELONG(btnW, btnH));

  // Segoe Fluent Icons / Segoe MDL2 Assets codepoints — present in
  // both fonts at the same code points, so the same source works
  // on Win10 and Win11. Stored as int literals so the source is
  // plain ASCII and survives editors that strip PUA characters.
  //   E72B Back, E72A Forward, E70E ChevronUp, E72C Refresh
  static const wchar_t kBack[]    = {0xE72B, 0};
  static const wchar_t kForward[] = {0xE72A, 0};
  static const wchar_t kUp[]      = {0xE70E, 0};
  static const wchar_t kRefresh[] = {0xE72C, 0};
  const wchar_t* labels[kNavButtonCount] = {kBack, kForward, kUp, kRefresh};
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
  if (mdl2Font_ != nullptr) {
    SendMessageW(navToolbar_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(mdl2Font_), TRUE);
  }
  // TB_AUTOSIZE intentionally omitted — with CCS_NORESIZE +
  // explicit TB_SETBUTTONSIZE the toolbar already has all its
  // sizing pinned by hand. Sending TB_AUTOSIZE here re-measured
  // against the parent's client size and made the toolbar collapse
  // out of the row at high DPI in earlier iterations.
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
  // Destroy the fonts after the children so any WM_SETFONT
  // handle reference is released first via the cascade above.
  if (mdl2Font_ != nullptr) {
    DeleteObject(mdl2Font_);
    mdl2Font_ = nullptr;
  }
  if (rowFont_ != nullptr) {
    DeleteObject(rowFont_);
    rowFont_ = nullptr;
  }
  addressBar_ = nullptr;
}

void PaneToolbarRow::setAddressBar(HWND addressBar) {
  addressBar_ = addressBar;
  if (addressBar != nullptr && hwnd_ != nullptr) {
    SetParent(addressBar, hwnd_);
    if (rowFont_ != nullptr) {
      // Apply to the ComboBoxEx itself (propagates to dropdown list)
      // and to the inner edit subcontrol (which is what actually
      // owns the visible textbox font). Without the explicit
      // CBEM_GETEDITCONTROL hop the edit keeps using the 9pt
      // default from window-class registration.
      SendMessageW(addressBar, WM_SETFONT,
                   reinterpret_cast<WPARAM>(rowFont_), TRUE);
      HWND edit = reinterpret_cast<HWND>(
          SendMessageW(addressBar, CBEM_GETEDITCONTROL, 0, 0));
      if (edit != nullptr) {
        SendMessageW(edit, WM_SETFONT,
                     reinterpret_cast<WPARAM>(rowFont_), TRUE);
      }
    }
  }
  layout();
}

void PaneToolbarRow::onDpiChanged(UINT newDpi) {
  // Atomic swap: build the new fonts first, then push them, then
  // free the old. Order matters because WM_SETFONT does not copy
  // the HFONT — controls hold the raw handle until told otherwise.
  HFONT newIcon = createIconFont(newDpi);
  HFONT newText = createRowFont(newDpi);
  HFONT oldIcon = mdl2Font_;
  HFONT oldText = rowFont_;
  mdl2Font_ = newIcon;
  rowFont_ = newText;
  if (navToolbar_ != nullptr && newIcon != nullptr) {
    SendMessageW(navToolbar_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(newIcon), TRUE);
  }
  if (hamburger_ != nullptr && newIcon != nullptr) {
    SendMessageW(hamburger_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(newIcon), TRUE);
  }
  if (addressBar_ != nullptr && newText != nullptr) {
    SendMessageW(addressBar_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(newText), TRUE);
    HWND edit = reinterpret_cast<HWND>(
        SendMessageW(addressBar_, CBEM_GETEDITCONTROL, 0, 0));
    if (edit != nullptr) {
      SendMessageW(edit, WM_SETFONT,
                   reinterpret_cast<WPARAM>(newText), TRUE);
    }
  }
  if (oldIcon != nullptr) DeleteObject(oldIcon);
  if (oldText != nullptr) DeleteObject(oldText);
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

  // The address bar's visible textbox is the visual anchor — its
  // height is derived from the system font, not from anything we
  // pass to SetWindowPos. Query the inner edit's client rect and
  // use that as the "common inner height" all other controls match.
  // Falls back to kRowInnerDipH when the combo isn't ready yet
  // (first layout before WM_SHOWWINDOW).
  int innerH = scaleDip(kRowInnerDipH, dpi);
  if (addressBar_ != nullptr) {
    HWND comboEdit = reinterpret_cast<HWND>(
        SendMessageW(addressBar_, CBEM_GETEDITCONTROL, 0, 0));
    if (comboEdit != nullptr) {
      RECT editRc{};
      GetClientRect(comboEdit, &editRc);
      const int editH = editRc.bottom - editRc.top;
      // Combobox draws a ~2-px themed border above and below the
      // edit; add it back so the combobox's visual outer height
      // matches the toolbar / hamburger button height.
      if (editH > 0) innerH = editH + scaleDip(4, dpi);
    }
  }

  const int yOff = (h - innerH) / 2;
  const int hPad = scaleDip(4, dpi);   // left / right outer margin
  const int gap  = scaleDip(8, dpi);   // between groups

  const int navW = navToolbar_ != nullptr ? scaleDip(kNavBarDipW, dpi) : 0;
  const int hambW = hamburger_ != nullptr ? scaleDip(kHamburgerDipW, dpi) : 0;

  int x = hPad;
  if (navToolbar_ != nullptr) {
    // Re-pin the toolbar's button height to the freshly-measured
    // innerH so the icons sit at the same vertical center as the
    // address-bar textbox. Width is kept at kNavButtonDipW for
    // glyph horizontal headroom.
    const int btnW = scaleDip(kNavButtonDipW, dpi);
    SendMessageW(navToolbar_, TB_SETBUTTONSIZE, 0,
                 MAKELONG(btnW, innerH));
    SetWindowPos(navToolbar_, nullptr, x, yOff, navW, innerH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    x += navW + gap;
  }

  const int hambX = w - hPad - hambW;
  const int addrW = hambX - gap - x;
  if (addressBar_ != nullptr && addrW > 0) {
    SetWindowPos(addressBar_, nullptr, x, yOff, addrW, innerH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
  if (hamburger_ != nullptr) {
    SetWindowPos(hamburger_, nullptr, hambX, yOff, hambW, innerH,
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
