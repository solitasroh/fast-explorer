#include "winui_lite/chrome/pane-toolbar-row.h"

#include <uxtheme.h>

#include <algorithm>
#include <iterator>

#include "winui_lite/chrome/theme-watcher.h"

namespace fast_explorer::ui {

namespace {
constexpr wchar_t kClassName[] = L"winui_lite.PaneToolbarRow";

// Sizing for the row. kRowInnerDipH is the visual height every
// interactive child snaps to (toolbar buttons, address-bar textbox,
// hamburger button) so they share a baseline. The row itself is set
// from the host's WM_SIZE (typically 36 DIP), giving symmetric top
// and bottom padding = (rowH - kRowInnerDipH) / 2.
constexpr int kRowInnerDipH  = 28;
constexpr int kNavButtonDipW = 32;
constexpr int kHamburgerDipW = 32;
constexpr int kChevronDipW   = 22;

int scaleDip(int dip, UINT dpi) noexcept {
  return MulDiv(dip, static_cast<int>(dpi), 96);
}

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
        return 0;
      },
      reinterpret_cast<LPARAM>(&found), 0);
  ReleaseDC(nullptr, dc);
  return found;
}

// Picks Win11's Segoe Fluent Icons (the same font Explorer's chrome
// uses) when available, falls back to Win10's Segoe MDL2 Assets.
// Matching the OS font is the closest we can get to "looks like
// native Windows".
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
  if (msg == WM_THEMECHANGED) {
    // The border + fill colours come from currentRowTheme(), which now
    // reflects the flipped theme. RDW_FRAME re-runs our WM_NCPAINT so
    // the border + NC padding update. RDW_ERASE is what forces a
    // WM_ERASEBKGND, which is the only path that refills the Edit's
    // client interior with the *new* WM_CTLCOLOREDIT brush — without it
    // a plain RDW_INVALIDATE leaves the body stuck on the old theme
    // colour (WM_PAINT only repaints behind the glyphs, not the empty
    // remainder of the textbox) on a runtime light↔dark toggle.
    RedrawWindow(hwnd, nullptr, nullptr,
                 RDW_FRAME | RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    return DefSubclassProc(hwnd, msg, wParam, lParam);
  }
  if (msg == WM_NCPAINT) {
    HDC dc = GetWindowDC(hwnd);
    if (dc != nullptr) {
      RECT wr{};
      GetWindowRect(hwnd, &wr);
      const int w = wr.right - wr.left;
      const int h = wr.bottom - wr.top;
      RECT outer = {0, 0, w, h};
      const RowTheme theme = currentRowTheme();
      HBRUSH bg = CreateSolidBrush(theme.editBackground);
      FillRect(dc, &outer, bg);
      HBRUSH borderBr = CreateSolidBrush(theme.editBorder);
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

HFONT createRowFont(UINT dpi) noexcept {
  LOGFONTW lf{};
  lf.lfHeight = -MulDiv(11, static_cast<int>(dpi), 96);
  lf.lfWeight = FW_NORMAL;
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfQuality = CLEARTYPE_QUALITY;
  const wchar_t kFace[] = L"Segoe UI Variable Display";
  for (size_t i = 0; i < std::size(kFace); ++i) {
    lf.lfFaceName[i] = kFace[i];
  }
  return CreateFontIndirectW(&lf);
}

}  // namespace

PaneToolbarRow::~PaneToolbarRow() { destroy(); }

const NavButtonSpec* PaneToolbarRow::findNavButton(WORD id) const noexcept {
  for (const auto& b : config_.navButtons) {
    if (b.id == id) return &b;
  }
  return nullptr;
}

bool PaneToolbarRow::registerClassOnce(HINSTANCE instance) {
  static bool registered = false;
  if (registered) return true;
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = &PaneToolbarRow::wndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  wc.lpszClassName = kClassName;
  registered = RegisterClassExW(&wc) != 0;
  return registered;
}

bool PaneToolbarRow::create(HWND parent, HINSTANCE instance,
                            std::size_t paneIdx,
                            const PaneToolbarRowConfig& config) {
  if (hwnd_ != nullptr) return true;
  if (!registerClassOnce(instance)) return false;
  paneIdx_ = paneIdx;
  config_ = config;
  // WS_EX_CONTROLPARENT lets IsDialogMessage recurse into the row,
  // putting the nav toolbar / address bar / hamburger into the
  // Tab / Shift+Tab cycle alongside the host's main content.
  hwnd_ = CreateWindowExW(
      WS_EX_CONTROLPARENT, kClassName, L"",
      WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
      0, 0, 0, 0, parent, nullptr, instance, this);
  if (hwnd_ == nullptr) return false;
  enableProcessDarkMode();  // one-shot per process; harmless if repeated
  iconFont_ = createIconFont(GetDpiForWindow(hwnd_));
  rowFont_ = createRowFont(GetDpiForWindow(hwnd_));
  if (!config_.navButtons.empty()) {
    if (!createNavToolbar(instance)) {
      // Toolbar creation is non-fatal; the address bar can still own
      // the full row. Leaves navToolbar_ as nullptr so layout() skips it.
    }
  }
  if (config_.hamburger.id != 0) {
    if (!createHamburger(instance)) {
      // Same fallback policy as the nav toolbar.
    }
    if (!createHamburgerTooltip(instance)) {
      // Tooltip is non-essential — accessible name + label are already
      // set, so SR users still hear the configured label.
    }
  }
  return true;
}

bool PaneToolbarRow::createHamburger(HINSTANCE instance) {
  // BS_OWNERDRAW lets us paint the configured glyph ourselves in
  // drawHamburgerItem so the visual matches the nav toolbar's custom
  // glyphs. WindowText holds the configured accessible label, which
  // MSAA / UIA / Narrator pick up.
  const wchar_t* label = config_.hamburger.label != nullptr
                             ? config_.hamburger.label
                             : L"";
  hamburger_ = CreateWindowExW(
      0, L"BUTTON", label,
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW,
      0, 0, 0, 0, hwnd_,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(
          packCmd(config_.hamburger.id, paneIdx_))),
      instance, nullptr);
  if (hamburger_ != nullptr && iconFont_ != nullptr) {
    SendMessageW(hamburger_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(iconFont_), TRUE);
  }
  return hamburger_ != nullptr;
}

bool PaneToolbarRow::createNavToolbar(HINSTANCE instance) {
  navToolbar_ = CreateWindowExW(
      0, TOOLBARCLASSNAMEW, L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBSTYLE_FLAT |
          TBSTYLE_TRANSPARENT | TBSTYLE_TOOLTIPS | CCS_NORESIZE |
          CCS_NODIVIDER | CCS_NOPARENTALIGN,
      0, 0, 0, 0, hwnd_, nullptr, instance, nullptr);
  if (navToolbar_ == nullptr) return false;
  SetWindowTheme(navToolbar_, L"Explorer", nullptr);

  SendMessageW(navToolbar_, TB_BUTTONSTRUCTSIZE,
               static_cast<WPARAM>(sizeof(TBBUTTON)), 0);
  SendMessageW(navToolbar_, TB_SETBITMAPSIZE, 0, MAKELONG(0, 0));
  const UINT rowDpi = GetDpiForWindow(hwnd_);
  const int btnW = scaleDip(kNavButtonDipW, rowDpi);
  const int btnH = scaleDip(kRowInnerDipH, rowDpi);
  SendMessageW(navToolbar_, TB_SETBUTTONSIZE, 0, MAKELONG(btnW, btnH));

  // iString carries the accessible label — MSAA / UIA pick it up via
  // WindowText, Narrator announces the configured label instead of
  // raw Private Use Area codepoints. The visible glyph is drawn
  // separately in handleNavToolbarCustomDraw using the icon font and
  // the per-button glyph codepoint.
  const std::size_t count = config_.navButtons.size();
  std::vector<INT_PTR> strIdx(count, -1);
  for (std::size_t i = 0; i < count; ++i) {
    const wchar_t* label = config_.navButtons[i].label;
    if (label == nullptr) continue;
    // TB_ADDSTRING wants a double-null-terminated block; copy the
    // label into a local buffer so we can stamp the second null
    // without mutating a string literal.
    wchar_t buf[64]{};
    int n = 0;
    while (label[n] != L'\0' && n < 62) {
      buf[n] = label[n];
      ++n;
    }
    buf[n] = L'\0';
    buf[n + 1] = L'\0';
    strIdx[i] = SendMessageW(navToolbar_, TB_ADDSTRINGW, 0,
                              reinterpret_cast<LPARAM>(buf));
  }

  std::vector<TBBUTTON> btns(count);
  for (std::size_t i = 0; i < count; ++i) {
    btns[i].iBitmap = I_IMAGENONE;
    btns[i].idCommand = packCmd(config_.navButtons[i].id, paneIdx_);
    btns[i].fsState = TBSTATE_ENABLED;
    btns[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_NOPREFIX;
    btns[i].iString = strIdx[i];
  }
  SendMessageW(navToolbar_, TB_ADDBUTTONS,
               static_cast<WPARAM>(count),
               reinterpret_cast<LPARAM>(btns.data()));
  if (iconFont_ != nullptr) {
    SendMessageW(navToolbar_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(iconFont_), TRUE);
  }
  return true;
}

void PaneToolbarRow::destroy() {
  navToolbar_ = nullptr;
  hamburger_ = nullptr;
  hamburgerTip_ = nullptr;
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
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
      return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
      const WORD btnId = unpackButton(
          static_cast<WORD>(cd->nmcd.dwItemSpec));
      const NavButtonSpec* spec = findNavButton(btnId);
      if (spec == nullptr || spec->glyph == nullptr ||
          iconFont_ == nullptr) {
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
      DrawTextW(hdc, spec->glyph, -1, &cd->nmcd.rc,
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
  const wchar_t* glyph = config_.hamburger.glyph;
  if (glyph == nullptr) return;
  HDC hdc = dis->hDC;
  const RowTheme theme = currentRowTheme();
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
  const NavButtonSpec* spec = findNavButton(btnId);
  const wchar_t* text = spec != nullptr ? spec->tooltip : nullptr;
  if (text == nullptr) {
    tip->pszText[0] = L'\0';
    return;
  }
  int i = 0;
  for (; text[i] != L'\0' && i < tip->cchTextMax - 1; ++i) {
    tip->pszText[i] = text[i];
  }
  tip->pszText[i] = L'\0';
}

void PaneToolbarRow::fillTooltipNeedText(LPARAM lParam) {
  auto* tt = reinterpret_cast<LPNMTTDISPINFOW>(lParam);
  if (tt == nullptr) return;
  const wchar_t* text = nullptr;
  if (tt->hdr.idFrom != 0 &&
      reinterpret_cast<HWND>(tt->hdr.idFrom) == hamburger_) {
    text = config_.hamburger.tooltip;
  } else {
    const WORD btnId = unpackButton(static_cast<WORD>(tt->hdr.idFrom));
    const NavButtonSpec* spec = findNavButton(btnId);
    if (spec != nullptr) text = spec->tooltip;
  }
  if (text == nullptr) {
    tt->szText[0] = L'\0';
    tt->lpszText = tt->szText;
    return;
  }
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
  ti.lpszText = LPSTR_TEXTCALLBACK;
  SendMessageW(hamburgerTip_, TTM_ADDTOOLW, 0,
               reinterpret_cast<LPARAM>(&ti));
  SendMessageW(hamburgerTip_, TTM_ACTIVATE,
               static_cast<WPARAM>(TRUE), 0);
  return true;
}

void PaneToolbarRow::onDpiChanged(UINT newDpi) {
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
  if (slotIdx < 0 ||
      static_cast<std::size_t>(slotIdx) >= config_.navButtons.size()) {
    return;
  }
  SendMessageW(navToolbar_, TB_ENABLEBUTTON,
               packCmd(config_.navButtons[slotIdx].id, paneIdx_),
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
  const int hPad = scaleDip(8, dpi);
  const int gap  = scaleDip(12, dpi);

  const int navW = navToolbar_ != nullptr
                       ? scaleDip(kNavButtonDipW, dpi) *
                             static_cast<int>(config_.navButtons.size())
                       : 0;
  const int hambW = hamburger_ != nullptr ? scaleDip(kHamburgerDipW, dpi) : 0;

  int x = hPad;
  if (navToolbar_ != nullptr) {
    const int btnW = scaleDip(kNavButtonDipW, dpi);
    SendMessageW(navToolbar_, TB_SETBUTTONSIZE, 0,
                 MAKELONG(btnW, innerH));
    SetWindowPos(navToolbar_, nullptr, x, yOff, navW, innerH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    x += navW + gap;
  }

  const int dropW = addressDropdownBtn_ != nullptr
                        ? scaleDip(kChevronDipW, dpi)
                        : 0;
  const int hambX = w - hPad - hambW;
  const int dropX = hambX - gap - dropW;
  const int addrW = (dropW > 0 ? dropX : hambX) - gap - x;
  if (addressBar_ != nullptr && addrW > 0) {
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
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
      InvalidateRect(hwnd, nullptr, TRUE);
      break;
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
          HBRUSH pill = CreateSolidBrush(theme.hoverPill);
          FillRect(dis->hDC, &dis->rcItem, pill);
          DeleteObject(pill);
        }
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, theme.text);
        HFONT useFnt = iconFont_ != nullptr ? iconFont_ : rowFont_;
        HGDIOBJ oldFnt = useFnt != nullptr
                             ? SelectObject(dis->hDC, useFnt)
                             : nullptr;
        const wchar_t* glyph = config_.addressDropdown.glyph;
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
    case WM_CTLCOLOREDIT: {
      HWND ctrl = reinterpret_cast<HWND>(lParam);
      if (ctrl == addressBar_ && isAppInDarkMode()) {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        const RowTheme theme = currentRowTheme();
        static COLORREF cachedBg = 0;
        static HBRUSH cachedBrush = nullptr;
        const COLORREF wantBg = theme.editBackground;
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
