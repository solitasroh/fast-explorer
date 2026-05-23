#include "ui/pane-toolbar-row.h"

#include <uxtheme.h>

#include <algorithm>
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

// Picks Win11's Segoe Fluent Icons (the same font File Explorer's
// own chrome uses) when available, falls back to Win10's Segoe
// MDL2 Assets. Matching the OS font is the closest we can get to
// "looks like native Windows".
const wchar_t* pickIconFontFace() noexcept {
  static const wchar_t* cached = []() {
    if (isFontInstalled(L"Segoe Fluent Icons")) return L"Segoe Fluent Icons";
    if (isFontInstalled(L"Segoe MDL2 Assets"))  return L"Segoe MDL2 Assets";
    return L"";
  }();
  return cached;
}

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
// MDL2/Fluent glyph codepoint for a given packed-cmd button id.
// Returns a 1-char null-terminated wide string ready for DrawTextW;
// nullptr for unknown ids (the caller falls back to the default
// toolbar / button paint).
const wchar_t* glyphForButtonId(WORD btnId) noexcept {
  // Segoe Fluent Icons / Segoe MDL2 Assets shared codepoints —
  // the same glyphs Win11's File Explorer command bar uses, so
  // the toolbar reads as native chrome.
  //   E72B Back, E72A Forward, E74A Up, E72C Refresh, E712 More,
  //   E70D ChevronDown
  static const wchar_t kBack[]      = {0xE72B, 0};
  static const wchar_t kForward[]   = {0xE72A, 0};
  static const wchar_t kUp[]        = {0xE74A, 0};
  static const wchar_t kRefresh[]   = {0xE72C, 0};
  static const wchar_t kHamburger[] = {0xE712, 0};
  static const wchar_t kChevronDn[] = {0xE70D, 0};
  switch (btnId) {
    case kTbBack:             return kBack;
    case kTbForward:          return kForward;
    case kTbUp:               return kUp;
    case kTbRefresh:          return kRefresh;
    case kTbHamburger:        return kHamburger;
    case kTbAddressDropdown:  return kChevronDn;
  }
  return nullptr;
}

// Korean accessible name for the same button ids. Stored in the
// control's text field so MSAA / UIA / Narrator pick it up.
const wchar_t* labelForButtonId(WORD btnId) noexcept {
  switch (btnId) {
    case kTbBack:      return L"뒤로";
    case kTbForward:   return L"앞으로";
    case kTbUp:        return L"위로";
    case kTbRefresh:   return L"새로 고침";
    case kTbHamburger: return L"메뉴";
  }
  return nullptr;
}

// "라벨 (단축키)" tooltip text for the given button id. nullptr for
// unknown ids — the tooltip then renders nothing rather than the
// raw command number.
const wchar_t* tooltipForButtonId(WORD btnId) noexcept {
  switch (btnId) {
    case kTbBack:      return L"뒤로 (Alt+←)";
    case kTbForward:   return L"앞으로 (Alt+→)";
    case kTbUp:        return L"위로 (Alt+↑)";
    case kTbRefresh:   return L"새로 고침 (F5)";
    case kTbHamburger: return L"메뉴 (Alt+M)";
  }
  return nullptr;
}

// Reads the Windows "apps use light theme" registry preference.
// Cached for the call's duration; the row invalidates on
// WM_SETTINGCHANGE("ImmersiveColorSet") so each repaint after a
// theme flip re-reads.
bool isAppInDarkMode() noexcept {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion"
                    L"\\Themes\\Personalize",
                    0, KEY_READ, &key) != ERROR_SUCCESS) {
    return false;
  }
  DWORD value = 1;
  DWORD size = sizeof(value);
  LONG r = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
                            reinterpret_cast<BYTE*>(&value), &size);
  RegCloseKey(key);
  return r == ERROR_SUCCESS && value == 0;
}

struct RowTheme {
  COLORREF background;
  COLORREF text;
  COLORREF disabledText;
};

// Picks the colour set the row + its custom-drawn children use,
// based on the system theme. Dark values are tuned to roughly
// match Win11 File Explorer's command-bar tint.
RowTheme currentRowTheme() noexcept {
  if (isAppInDarkMode()) {
    return RowTheme{
        /*background*/   RGB(32, 32, 32),
        /*text*/         RGB(241, 241, 241),
        /*disabledText*/ RGB(120, 120, 120),
    };
  }
  return RowTheme{
      /*background*/   GetSysColor(COLOR_BTNFACE),
      /*text*/         GetSysColor(COLOR_BTNTEXT),
      /*disabledText*/ GetSysColor(COLOR_GRAYTEXT),
  };
}

// Subclass on the address-bar Edit that expands the non-client area
// on top + bottom (and left + right) so the Edit's client centres a
// single line of text with visual padding. Single-line Edits ignore
// EM_SETRECT/NP, so the only Win32 path to "padding around text" is
// to lie to the Edit about how big its client area is via
// WM_NCCALCSIZE. We then own WM_NCPAINT for the expanded NC strip
// so the dark / light row colour fills the padding seamlessly.
struct AddressEditPadState {
  bool valid = false;
  int vertPad = 0;
  int horizPad = 0;
};

LRESULT CALLBACK addressEditNcPaddingSubclass(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR idSubclass, DWORD_PTR refData) {
  auto* state = reinterpret_cast<AddressEditPadState*>(refData);
  if (msg == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, &addressEditNcPaddingSubclass, idSubclass);
    delete state;
    return DefSubclassProc(hwnd, msg, wParam, lParam);
  }
  if (msg == WM_NCCALCSIZE && wParam == TRUE && state != nullptr) {
    auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
    HFONT fnt = reinterpret_cast<HFONT>(
        SendMessageW(hwnd, WM_GETFONT, 0, 0));
    int textH = 14;
    if (fnt != nullptr) {
      HDC dc = GetDC(hwnd);
      if (dc != nullptr) {
        HGDIOBJ oldFnt = SelectObject(dc, fnt);
        TEXTMETRICW tm{};
        if (GetTextMetricsW(dc, &tm)) textH = tm.tmHeight;
        if (oldFnt != nullptr) SelectObject(dc, oldFnt);
        ReleaseDC(hwnd, dc);
      }
    }
    const int outerH = p->rgrc[0].bottom - p->rgrc[0].top;
    const int totalPad = outerH - textH;
    int topPad = totalPad > 0 ? totalPad / 2 : 0;
    int botPad = totalPad > 0 ? totalPad - topPad : 0;
    const UINT dpi = GetDpiForWindow(hwnd);
    const int padX = MulDiv(8, static_cast<int>(dpi), 96);
    p->rgrc[0].top    += topPad;
    p->rgrc[0].bottom -= botPad;
    p->rgrc[0].left   += padX;
    p->rgrc[0].right  -= padX;
    state->valid = true;
    state->vertPad = topPad;
    state->horizPad = padX;
    return 0;
  }
  if (msg == WM_NCPAINT) {
    HDC dc = GetWindowDC(hwnd);
    if (dc != nullptr) {
      RECT wr{};
      GetWindowRect(hwnd, &wr);
      const int w = wr.right - wr.left;
      const int h = wr.bottom - wr.top;
      RECT outer = {0, 0, w, h};
      // Match the Edit's CTLCOLOREDIT fill so the padding ring is
      // visually contiguous with the text bg. PaneToolbarRow's
      // WM_CTLCOLOREDIT returns RGB(40,40,40) in dark mode and
      // system COLOR_WINDOW in light.
      const bool dark = isAppInDarkMode();
      HBRUSH bg = CreateSolidBrush(
          dark ? RGB(40, 40, 40) : GetSysColor(COLOR_WINDOW));
      FillRect(dc, &outer, bg);
      // Subtle 1px border. Slightly lighter than the bg in dark
      // mode so the textbox edge reads, softer system colour in
      // light mode.
      HBRUSH borderBr = CreateSolidBrush(
          dark ? RGB(80, 80, 80) : RGB(180, 180, 180));
      FrameRect(dc, &outer, borderBr);
      DeleteObject(borderBr);
      DeleteObject(bg);
      ReleaseDC(hwnd, dc);
    }
    return 0;
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void installAddressEditNcPadding(HWND edit) noexcept {
  if (edit == nullptr) return;
  constexpr UINT_PTR kSubclassId = 0xFE10D0u;
  DWORD_PTR refData = 0;
  if (GetWindowSubclass(edit, &addressEditNcPaddingSubclass, kSubclassId,
                         &refData)) {
    return;
  }
  auto* state = new AddressEditPadState();
  SetWindowSubclass(edit, &addressEditNcPaddingSubclass, kSubclassId,
                    reinterpret_cast<DWORD_PTR>(state));
}

// Undocumented but stable since Windows 10 1809: uxtheme.dll
// ordinal 135 = SetPreferredAppMode. Telling Windows "AllowDark"
// here lets the dark-themed classes (DarkMode_CFD for combobox,
// DarkMode_Explorer for tree/list) actually take effect when we
// call SetWindowTheme below.
enum PreferredAppMode { PAM_Default = 0, PAM_AllowDark = 1,
                       PAM_ForceDark = 2, PAM_ForceLight = 3 };
using SetPreferredAppMode_t = int (WINAPI*)(PreferredAppMode);

void enableProcessDarkMode() noexcept {
  static bool tried = false;
  if (tried) return;
  tried = true;
  HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr,
                              LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (ux == nullptr) return;
  auto setMode = reinterpret_cast<SetPreferredAppMode_t>(
      GetProcAddress(ux, MAKEINTRESOURCEA(135)));
  if (setMode != nullptr) setMode(PAM_AllowDark);
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
  // Segoe UI Variable Display is the Win11 default for shell chrome
  // — slightly more refined letterforms than plain Segoe UI. Falls
  // back automatically to Segoe UI on Win10 where the variable
  // family isn't installed (CreateFontIndirect substitutes via
  // GDI font mapper).
  const wchar_t kFace[] = L"Segoe UI Variable Display";
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
  // hbrBackground stays null so WM_ERASEBKGND can paint with the
  // theme-aware colour (light vs dark) instead of the class-fixed
  // COLOR_BTNFACE — class brushes can't be swapped at runtime.
  wc.hbrBackground = nullptr;
  wc.lpszClassName = kClassName;
  registered = RegisterClassExW(&wc) != 0;
  return registered;
}

bool PaneToolbarRow::create(HWND parent, HINSTANCE instance,
                            std::size_t paneIdx) {
  if (hwnd_ != nullptr) return true;
  if (!registerClassOnce(instance)) return false;
  paneIdx_ = paneIdx;
  // WS_EX_CONTROLPARENT lets IsDialogMessage recurse into the row,
  // putting the nav toolbar / address bar / hamburger into the
  // Tab / Shift+Tab cycle alongside the list view. Without it
  // keyboard-only users cannot focus the row at all.
  hwnd_ = CreateWindowExW(
      WS_EX_CONTROLPARENT, kClassName, L"",
      WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
      0, 0, 0, 0, parent, nullptr, instance, this);
  if (hwnd_ == nullptr) return false;
  enableProcessDarkMode();  // one-shot per process; harmless if repeated
  iconFont_ = createIconFont(GetDpiForWindow(hwnd_));
  rowFont_ = createRowFont(GetDpiForWindow(hwnd_));
  if (!createNavToolbar(instance)) {
    // Toolbar creation is non-fatal; the address bar can still own
    // the full row. Leaves navToolbar_ as nullptr so layout() skips it.
  }
  if (!createHamburger(instance)) {
    // Same fallback policy as the nav toolbar.
  }
  if (!createHamburgerTooltip(instance)) {
    // Tooltip is non-essential — accessible name + label are already
    // set, so SR users still hear "메뉴".
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
  // WindowText is "메뉴" (Korean accessible name read by Narrator);
  // BS_OWNERDRAW lets us paint the More glyph ourselves in
  // drawHamburgerItem so the visual matches the nav toolbar's
  // custom-drawn glyphs without showing the text.
  hamburger_ = CreateWindowExW(
      0, L"BUTTON", L"메뉴",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW,
      0, 0, 0, 0, hwnd_,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(
          packCmd(kTbHamburger, paneIdx_))),
      instance, nullptr);
  if (hamburger_ != nullptr && iconFont_ != nullptr) {
    SendMessageW(hamburger_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(iconFont_), TRUE);
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
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBSTYLE_FLAT |
          TBSTYLE_TRANSPARENT | TBSTYLE_TOOLTIPS | CCS_NORESIZE |
          CCS_NODIVIDER | CCS_NOPARENTALIGN,
      0, 0, 0, 0, hwnd_, nullptr, instance, nullptr);
  if (navToolbar_ == nullptr) return false;
  // Pick up the shell's themed hot-track / hover treatment instead
  // of the classic flat-grey Win32 paint. Same trick Explorer uses
  // on its own command bar — gives buttons a rounded transparent
  // hover pill on Windows 10 / 11.
  SetWindowTheme(navToolbar_, L"Explorer", nullptr);

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

  // iString carries the *accessible name* (Korean label) — MSAA /
  // UIA pick it up via WindowText, Narrator announces "뒤로" instead
  // of "Private Use Area E72B". The button's visible glyph is drawn
  // separately in handleNavToolbarCustomDraw using the icon font
  // and the codepoint table in glyphForButtonId. Removing
  // BTNS_SHOWTEXT stops the toolbar from rendering the label
  // itself; the custom-draw path fills the button content.
  const WORD ids[kNavButtonCount] = {kTbBack, kTbForward, kTbUp, kTbRefresh};
  INT_PTR strIdx[kNavButtonCount] = {};
  for (int i = 0; i < kNavButtonCount; ++i) {
    const wchar_t* label = labelForButtonId(ids[i]);
    if (label == nullptr) { strIdx[i] = -1; continue; }
    // TB_ADDSTRING wants a double-null-terminated block; copy the
    // label into a local buffer so we can stamp the second null
    // without mutating a string literal.
    wchar_t buf[24]{};
    int n = 0;
    while (label[n] != L'\0' && n < 22) {
      buf[n] = label[n];
      ++n;
    }
    buf[n] = L'\0';
    buf[n + 1] = L'\0';
    strIdx[i] = SendMessageW(navToolbar_, TB_ADDSTRINGW, 0,
                              reinterpret_cast<LPARAM>(buf));
  }

  TBBUTTON btns[kNavButtonCount]{};
  for (int i = 0; i < kNavButtonCount; ++i) {
    btns[i].iBitmap = I_IMAGENONE;
    btns[i].idCommand = packCmd(ids[i], paneIdx_);
    btns[i].fsState = TBSTATE_ENABLED;
    btns[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_NOPREFIX;
    btns[i].iString = strIdx[i];
  }
  SendMessageW(navToolbar_, TB_ADDBUTTONS,
               static_cast<WPARAM>(kNavButtonCount),
               reinterpret_cast<LPARAM>(btns));
  if (iconFont_ != nullptr) {
    SendMessageW(navToolbar_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(iconFont_), TRUE);
  }
  // TB_AUTOSIZE intentionally omitted — with CCS_NORESIZE +
  // explicit TB_SETBUTTONSIZE the toolbar already has all its
  // sizing pinned by hand. Sending TB_AUTOSIZE here re-measured
  // against the parent's client size and made the toolbar collapse
  // out of the row at high DPI in earlier iterations.
  return true;
}

void PaneToolbarRow::destroy() {
  // Children (toolbar / hamburger / hamburger tooltip) are owned by
  // the row's HWND lifetime and torn down by DestroyWindow cascade;
  // explicit nulling keeps the fields consistent if destroy() runs
  // twice.
  navToolbar_ = nullptr;
  hamburger_ = nullptr;
  hamburgerTip_ = nullptr;
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  // Destroy the fonts after the children so any WM_SETFONT
  // handle reference is released first via the cascade above.
  if (iconFont_ != nullptr) {
    DeleteObject(iconFont_);
    iconFont_ = nullptr;
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
      SendMessageW(addressBar, WM_SETFONT,
                   reinterpret_cast<WPARAM>(rowFont_), TRUE);
    }
    installAddressEditNcPadding(addressBar);
  }
  layout();
}

void PaneToolbarRow::setAddressDropdownBtn(HWND btn) {
  addressDropdownBtn_ = btn;
  if (btn != nullptr && hwnd_ != nullptr) {
    SetParent(btn, hwnd_);
  }
  layout();
}

LRESULT PaneToolbarRow::handleNavToolbarCustomDraw(LPARAM lParam) {
  auto* cd = reinterpret_cast<LPNMTBCUSTOMDRAW>(lParam);
  if (cd == nullptr) return CDRF_DODEFAULT;
  switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
      // Ask for per-item callbacks so we can paint each button's
      // glyph; the toolbar itself still paints the hover/pressed
      // background frame for us.
      return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
      const WORD btnId = unpackButton(
          static_cast<WORD>(cd->nmcd.dwItemSpec));
      const wchar_t* glyph = glyphForButtonId(btnId);
      if (glyph == nullptr || iconFont_ == nullptr) {
        return CDRF_DODEFAULT;
      }
      HDC hdc = cd->nmcd.hdc;
      HGDIOBJ oldFont = SelectObject(hdc, iconFont_);
      const int oldBkMode = SetBkMode(hdc, TRANSPARENT);
      const RowTheme theme = currentRowTheme();
      const COLORREF color = (cd->nmcd.uItemState & CDIS_DISABLED)
                                 ? theme.disabledText
                                 : theme.text;
      const COLORREF oldColor = SetTextColor(hdc, color);
      DrawTextW(hdc, glyph, -1, &cd->nmcd.rc,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      SetTextColor(hdc, oldColor);
      SetBkMode(hdc, oldBkMode);
      SelectObject(hdc, oldFont);
      return CDRF_SKIPDEFAULT;
    }
  }
  return CDRF_DODEFAULT;
}

void PaneToolbarRow::drawHamburgerItem(LPARAM lParam) {
  auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
  if (dis == nullptr) return;
  const wchar_t* glyph = glyphForButtonId(kTbHamburger);
  if (glyph == nullptr) return;
  HDC hdc = dis->hDC;
  const RowTheme theme = currentRowTheme();
  // Background: theme-aware fill. Skips the system-colour brush so
  // dark mode actually picks up the dark tone (system COLOR_BTNFACE
  // is light-grey in both themes — it doesn't track Personalize).
  HBRUSH bgBrush = CreateSolidBrush(theme.background);
  FillRect(hdc, &dis->rcItem, bgBrush);
  DeleteObject(bgBrush);
  if (dis->itemState & ODS_SELECTED) {
    HBRUSH frame = CreateSolidBrush(theme.disabledText);
    FrameRect(hdc, &dis->rcItem, frame);
    DeleteObject(frame);
  }
  if (iconFont_ != nullptr) {
    HGDIOBJ oldFont = SelectObject(hdc, iconFont_);
    const int oldBkMode = SetBkMode(hdc, TRANSPARENT);
    const COLORREF color = (dis->itemState & ODS_DISABLED)
                               ? theme.disabledText
                               : theme.text;
    const COLORREF oldColor = SetTextColor(hdc, color);
    DrawTextW(hdc, glyph, -1, &dis->rcItem,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(hdc, oldColor);
    SetBkMode(hdc, oldBkMode);
    SelectObject(hdc, oldFont);
  }
  if (dis->itemState & ODS_FOCUS) {
    DrawFocusRect(hdc, &dis->rcItem);
  }
}

void PaneToolbarRow::fillToolbarTooltip(LPARAM lParam) {
  auto* tip = reinterpret_cast<LPNMTBGETINFOTIPW>(lParam);
  if (tip == nullptr || tip->pszText == nullptr || tip->cchTextMax <= 0) {
    return;
  }
  const WORD btnId = unpackButton(static_cast<WORD>(tip->iItem));
  const wchar_t* text = tooltipForButtonId(btnId);
  if (text == nullptr) {
    tip->pszText[0] = L'\0';
    return;
  }
  // Cap copy to cchTextMax - 1 wchars + null term. Tooltips are
  // single-line, ~1024 chars max from common controls — our strings
  // are far shorter so a plain copy is fine.
  int i = 0;
  for (; text[i] != L'\0' && i < tip->cchTextMax - 1; ++i) {
    tip->pszText[i] = text[i];
  }
  tip->pszText[i] = L'\0';
}

void PaneToolbarRow::fillTooltipNeedText(LPARAM lParam) {
  auto* tt = reinterpret_cast<LPNMTTDISPINFOW>(lParam);
  if (tt == nullptr) return;
  // For toolbar buttons, idFrom is the packed command id (HIWORD
  // of MAKELONG, or the lParam's hdr.idFrom holds the raw cmd).
  // For TTF_IDISHWND tooltips (our hamburger), idFrom is the HWND
  // we registered; map that to the hamburger command id.
  WORD btnId = 0;
  if (tt->hdr.idFrom != 0 &&
      reinterpret_cast<HWND>(tt->hdr.idFrom) == hamburger_) {
    btnId = kTbHamburger;
  } else {
    btnId = unpackButton(static_cast<WORD>(tt->hdr.idFrom));
  }
  const wchar_t* text = tooltipForButtonId(btnId);
  if (text == nullptr) {
    tt->szText[0] = L'\0';
    tt->lpszText = tt->szText;
    return;
  }
  // The tooltip control reads from lpszText. Point it directly at
  // our static-storage literal so we don't have to worry about
  // szText's 80-char limit truncating Korean strings.
  tt->lpszText = const_cast<LPWSTR>(text);
  tt->hinst = nullptr;
}

bool PaneToolbarRow::createHamburgerTooltip(HINSTANCE instance) {
  if (hamburger_ == nullptr) return false;
  hamburgerTip_ = CreateWindowExW(
      WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
      WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
      CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
      hwnd_, nullptr, instance, nullptr);
  if (hamburgerTip_ == nullptr) return false;
  TOOLINFOW ti{};
  ti.cbSize = sizeof(ti);
  ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  ti.hwnd = hwnd_;
  ti.uId = reinterpret_cast<UINT_PTR>(hamburger_);
  // LPSTR_TEXTCALLBACK forces the tooltip to ask for text every
  // time via TTN_NEEDTEXTW → fillTooltipNeedText. Resolves a class
  // of cases where the cached lpszText pointer goes stale relative
  // to localization or theme changes.
  ti.lpszText = LPSTR_TEXTCALLBACK;
  SendMessageW(hamburgerTip_, TTM_ADDTOOLW, 0,
               reinterpret_cast<LPARAM>(&ti));
  SendMessageW(hamburgerTip_, TTM_ACTIVATE,
               static_cast<WPARAM>(TRUE), 0);
  return true;
}

void PaneToolbarRow::onDpiChanged(UINT newDpi) {
  // Atomic swap: build the new fonts first, then push them, then
  // free the old. Order matters because WM_SETFONT does not copy
  // the HFONT — controls hold the raw handle until told otherwise.
  HFONT newIcon = createIconFont(newDpi);
  HFONT newText = createRowFont(newDpi);
  HFONT oldIcon = iconFont_;
  HFONT oldText = rowFont_;
  iconFont_ = newIcon;
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

  // Font-derived inner height so the row stays a stable size across
  // layout calls (an earlier version measured the Edit's own client
  // and added back the border, which feedback-grew the row by 2px
  // per WM_SIZE). 6 DIP of padding around the cap-height gives the
  // textbox the right breathing room without overshooting other
  // children.
  int innerH = scaleDip(kRowInnerDipH, dpi);
  if (rowFont_ != nullptr) {
    HDC dc = GetDC(hwnd_);
    if (dc != nullptr) {
      HGDIOBJ oldFnt = SelectObject(dc, rowFont_);
      TEXTMETRICW tm{};
      if (GetTextMetricsW(dc, &tm)) {
        const int desired = tm.tmHeight + scaleDip(8, dpi);
        if (desired > innerH) innerH = desired;
      }
      if (oldFnt != nullptr) SelectObject(dc, oldFnt);
      ReleaseDC(hwnd_, dc);
    }
  }

  const int yOff = (h - innerH) / 2;
  // A9: visual review called the 4 DIP outer / 8 DIP intra-group
  // values "cramped" and "asymmetric" against Win11 File Explorer
  // / Files app spacing. Bumped to 8 DIP outer / 12 DIP intra-group;
  // chevron-on-combobox vs hamburger no longer crash together at
  // the right edge of the row.
  const int hPad = scaleDip(8, dpi);
  const int gap  = scaleDip(12, dpi);

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

  // Dropdown ˅ button sits between the address bar and the hamburger.
  // 22 DIP wide is just enough room for the glyph + 2px padding either
  // side, matches Win11 Explorer's chevron button proportion.
  const int dropW = addressDropdownBtn_ != nullptr ? scaleDip(22, dpi) : 0;
  const int hambX = w - hPad - hambW;
  const int dropX = hambX - gap - dropW;
  const int addrW = (dropW > 0 ? dropX : hambX) - gap - x;
  if (addressBar_ != nullptr && addrW > 0) {
    // Full-height Edit; the NC subclass (installed at setAddressBar)
    // expands the non-client area on top + bottom so the Edit's
    // internal client centres the text vertically + leaves visual
    // breathing room above and below. EM_SETRECT/NP would have been
    // cleaner but it's a no-op for single-line edits.
    SetWindowPos(addressBar_, nullptr, x, yOff, addrW, innerH,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  }
  if (addressDropdownBtn_ != nullptr) {
    SetWindowPos(addressDropdownBtn_, nullptr, dropX, yOff, dropW, innerH,
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
    case WM_ERASEBKGND: {
      HDC hdc = reinterpret_cast<HDC>(wParam);
      RECT rc{};
      GetClientRect(hwnd, &rc);
      const RowTheme theme = currentRowTheme();
      HBRUSH br = CreateSolidBrush(theme.background);
      FillRect(hdc, &rc, br);
      DeleteObject(br);
      return 1;
    }
    // System theme flipped (light ↔ dark): redraw with the new
    // RowTheme. MainWindow already calls applySystemTheme on
    // WM_SETTINGCHANGE("ImmersiveColorSet") for the title bar; the
    // notification also broadcasts to children, so the row sees it.
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
      InvalidateRect(hwnd, nullptr, TRUE);
      break;
    // WM_NOTIFY from our own nav toolbar: intercept NM_CUSTOMDRAW so
    // we can paint the icon-font glyph ourselves (visual). Everything
    // else (TBN_GETINFOTIP, etc.) is forwarded to MainWindow.
    case WM_NOTIFY: {
      auto* hdr = reinterpret_cast<NMHDR*>(lParam);
      if (hdr != nullptr) {
        if (hdr->hwndFrom == navToolbar_ &&
            hdr->code == NM_CUSTOMDRAW) {
          return handleNavToolbarCustomDraw(lParam);
        }
        if (hdr->code == TBN_GETINFOTIPW) {
          fillToolbarTooltip(lParam);
          return 0;
        }
        // TTN_NEEDTEXTW: sent by every TOOLTIPS_CLASS instance
        // when it's about to display. Covers both the toolbar's
        // internal tooltip (auto-created by TBSTYLE_TOOLTIPS) and
        // our explicit hamburgerTip_. ANSI variant is unlikely on
        // Win10+ but handled below for completeness.
        if (hdr->code == TTN_NEEDTEXTW) {
          fillTooltipNeedText(lParam);
          return 0;
        }
      }
      HWND parent = GetParent(hwnd);
      if (parent != nullptr) {
        return SendMessageW(parent, msg, wParam, lParam);
      }
      break;
    }
    // BS_OWNERDRAW hamburger + address-bar ˅ button paint themselves
    // so we can pick light vs dark colours. Other DRAWITEM senders
    // bubble so future controls don't silently break.
    case WM_DRAWITEM: {
      auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
      if (dis != nullptr && dis->hwndItem == hamburger_) {
        drawHamburgerItem(lParam);
        return TRUE;
      }
      if (dis != nullptr && dis->hwndItem == addressDropdownBtn_) {
        const RowTheme theme = currentRowTheme();
        HBRUSH bg = CreateSolidBrush(theme.background);
        FillRect(dis->hDC, &dis->rcItem, bg);
        DeleteObject(bg);
        if ((dis->itemState & (ODS_HOTLIGHT | ODS_SELECTED)) != 0) {
          COLORREF pillColor = isAppInDarkMode() ? RGB(56, 56, 56)
                                                  : RGB(225, 225, 225);
          HBRUSH pill = CreateSolidBrush(pillColor);
          FillRect(dis->hDC, &dis->rcItem, pill);
          DeleteObject(pill);
        }
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, theme.text);
        // Use the Segoe Fluent icon font for crisp Win11-style chevron;
        // fall back to the row text font (then a Unicode chevron) if
        // the icon font failed to load.
        HFONT useFnt = iconFont_ != nullptr ? iconFont_ : rowFont_;
        HGDIOBJ oldFnt = useFnt != nullptr
                             ? SelectObject(dis->hDC, useFnt)
                             : nullptr;
        const wchar_t* glyph = glyphForButtonId(kTbAddressDropdown);
        if (glyph == nullptr) glyph = L"v";
        DrawTextW(dis->hDC, glyph, -1, &dis->rcItem,
                  DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        if (oldFnt != nullptr) SelectObject(dis->hDC, oldFnt);
        return TRUE;
      }
      HWND parent = GetParent(hwnd);
      if (parent != nullptr) {
        return SendMessageW(parent, msg, wParam, lParam);
      }
      break;
    }
    // WM_CTLCOLOREDIT for the address-bar Edit — dark mode returns
    // our dark brush + sets text/bg colours on the HDC. The plain
    // Edit (not ComboBoxEx) honours these directly because there's
    // no themed Combo wrapping it to ignore the return.
    case WM_CTLCOLOREDIT: {
      HWND ctrl = reinterpret_cast<HWND>(lParam);
      if (ctrl == addressBar_ && isAppInDarkMode()) {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        const RowTheme theme = currentRowTheme();
        // Edit text area sits a bit brighter than the row chrome
        // so the textbox reads as its own surface.
        static COLORREF cachedBg = 0;
        static HBRUSH cachedBrush = nullptr;
        const COLORREF wantBg = RGB(40, 40, 40);
        if (cachedBrush == nullptr || cachedBg != wantBg) {
          if (cachedBrush != nullptr) DeleteObject(cachedBrush);
          cachedBrush = CreateSolidBrush(wantBg);
          cachedBg = wantBg;
        }
        SetTextColor(hdc, theme.text);
        SetBkColor(hdc, wantBg);
        return reinterpret_cast<LRESULT>(cachedBrush);
      }
      HWND parent = GetParent(hwnd);
      if (parent != nullptr) {
        return SendMessageW(parent, msg, wParam, lParam);
      }
      break;
    }
    // Other CTLCOLOR / WM_COMMAND bubble up to MainWindow so the
    // existing accelerator + button routing keeps working.
    case WM_COMMAND:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
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
