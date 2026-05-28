#include "winui_lite/widgets/tab-strip.h"

#include <windowsx.h>

#include <string>

#include "winui_lite/chrome/theme-watcher.h"

namespace fast_explorer::ui {

namespace {

constexpr const wchar_t* kClassName = L"FE_TabStrip";

void ensureClassRegistered() {
  static bool done = false;
  if (done) return;
  WNDCLASSW wc{};
  wc.lpfnWndProc = TabStrip::WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<LONG_PTR>(COLOR_WINDOW + 1));
  wc.lpszClassName = kClassName;
  RegisterClassW(&wc);
  done = true;
}

}  // namespace

TabStrip::TabStrip(HWND parent, std::size_t paneIdx)
    : hwnd_(nullptr), paneIdx_(paneIdx) {
  refreshPalette();
  ensureClassRegistered();
  hwnd_ = CreateWindowExW(
      0, kClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
      0, 0, 100, 28, parent,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(paneIdx + 0x9000)),
      GetModuleHandleW(nullptr), this);
  if (hwnd_) {
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(this));
  }
}

TabStrip::~TabStrip() {
  if (hwnd_) DestroyWindow(hwnd_);
}

int TabStrip::preferredHeight() const {
  // Geometry default; future DPI-aware override goes here.
  return TabStripGeometry({}, models_.size()).metrics().height;
}

void TabStrip::setTabs(std::span<const TabModel> models) {
  models_.assign(models.begin(), models.end());
  if (active_ >= models_.size() && !models_.empty()) {
    active_ = models_.size() - 1;
  }
  rebuildRects();
  if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void TabStrip::setActive(std::size_t idx) {
  if (idx >= models_.size()) return;
  active_ = idx;
  if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void TabStrip::rebuildRects() {
  RECT cr{};
  if (hwnd_) GetClientRect(hwnd_, &cr);
  TabStripGeometry g({}, models_.size());
  rects_ = g.layout(cr.right - cr.left, scrollOffset_);
}

int TabStrip::hitTabAt(int x) {
  TabStripGeometry g({}, models_.size());
  return g.hitTest(rects_, x);
}

int TabStrip::hitCloseAt(int x) {
  TabStripGeometry g({}, models_.size());
  return g.hitTestCloseX(rects_, x);
}

void TabStrip::refreshPalette() noexcept {
  const bool dark = isAppInDarkMode();
  if (dark) {
    palette_.inactiveBg    = RGB(0x2A, 0x2A, 0x2A);
    palette_.activeBg      = RGB(0x1A, 0x1A, 0x1A);
    palette_.border        = RGB(0x4A, 0x4A, 0x4A);
    palette_.text          = RGB(0xE0, 0xE0, 0xE0);
  } else {
    palette_.inactiveBg    = RGB(0xF0, 0xF0, 0xF0);
    palette_.activeBg      = RGB(0xFF, 0xFF, 0xFF);
    palette_.border        = RGB(0xA0, 0xA0, 0xA0);
    palette_.text          = RGB(0x00, 0x00, 0x00);
  }
  // Explorer red — same in both modes
  palette_.hoverCloseX   = RGB(0xE8, 0x11, 0x23);
  palette_.dropIndicator = palette_.border;
}

void TabStrip::paint(HDC dc, const RECT& client) {
  // Fill strip background with inactive tab colour
  HBRUSH bgBrush = CreateSolidBrush(palette_.inactiveBg);
  FillRect(dc, &client, bgBrush);
  DeleteObject(bgBrush);

  for (std::size_t i = 0; i < rects_.size(); ++i) {
    const auto& r = rects_[i];
    RECT tab{r.left, client.top, r.right, client.bottom};
    const bool isActive = (i == active_);

    // Tab background
    HBRUSH tabBg = CreateSolidBrush(
        isActive ? palette_.activeBg : palette_.inactiveBg);
    FillRect(dc, &tab, tabBg);
    DeleteObject(tabBg);

    // Border
    HPEN pen = CreatePen(PS_SOLID, 1, palette_.border);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, tab.left, tab.bottom - 1, nullptr);
    LineTo(dc, tab.left, tab.top);
    LineTo(dc, tab.right - 1, tab.top);
    LineTo(dc, tab.right - 1, tab.bottom - 1);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    // Title
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, palette_.text);
    RECT textRect{tab.left + 8, tab.top, r.closeXLeft - 4, tab.bottom};
    DrawTextW(dc, models_[i].title.c_str(),
              static_cast<int>(models_[i].title.size()),
              &textRect,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    // Close X — drawn only on hover or active
    if (models_[i].hasCloseX &&
        (isActive || hoveredTab_ == static_cast<int>(i))) {
      RECT xRect{r.closeXLeft, tab.top + 6,
                 r.closeXRight, tab.bottom - 6};
      const bool xHover = (hoveredCloseX_ == static_cast<int>(i));
      if (xHover) {
        HBRUSH hb = CreateSolidBrush(palette_.hoverCloseX);
        FillRect(dc, &xRect, hb);
        DeleteObject(hb);
      }
      SetTextColor(dc, palette_.text);
      DrawTextW(dc, L"✕", 1, &xRect,
                DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    }
  }

  // Drop indicator line (during drag)
  if (dropIndicatorX_ >= 0) {
    HPEN pen = CreatePen(PS_SOLID, 2, palette_.dropIndicator);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, dropIndicatorX_, client.top, nullptr);
    LineTo(dc, dropIndicatorX_, client.bottom);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
  }

  // "+" button at right
  TabStripGeometry g({}, models_.size());
  const auto& mtr = g.metrics();
  RECT plus{client.right - mtr.plusButtonWidth, client.top,
            client.right, client.bottom};
  HBRUSH plusBg = CreateSolidBrush(palette_.inactiveBg);
  FillRect(dc, &plus, plusBg);
  DeleteObject(plusBg);
  SetTextColor(dc, palette_.text);
  SetBkMode(dc, TRANSPARENT);
  DrawTextW(dc, L"+", 1, &plus,
            DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
}

LRESULT TabStrip::handle(UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_SIZE:
      rebuildRects();
      InvalidateRect(hwnd_, nullptr, TRUE);
      return 0;

    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(hwnd_, &ps);
      RECT cr{};
      GetClientRect(hwnd_, &cr);
      paint(dc, cr);
      EndPaint(hwnd_, &ps);
      return 0;
    }

    case WM_ERASEBKGND:
      return 1;

    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
      refreshPalette();
      InvalidateRect(hwnd_, nullptr, TRUE);
      return 0;

    case WM_LBUTTONDOWN: {
      const int x = GET_X_LPARAM(lp);
      const int tab = hitTabAt(x);
      if (tab >= 0) {
        // arm a possible drag — actual switch fires on mouse-up
        dragArmed_ = true;
        dragFromIdx_ = static_cast<std::size_t>(tab);
        dragStartX_ = x;
        SetCapture(hwnd_);
      }
      return 0;
    }

    case WM_MOUSEMOVE: {
      const int x = GET_X_LPARAM(lp);
      const int tab = hitTabAt(x);
      const int xHit = hitCloseAt(x);
      if (tab != hoveredTab_ || xHit != hoveredCloseX_) {
        hoveredTab_ = tab;
        hoveredCloseX_ = xHit;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      if (dragArmed_ && !dragging_) {
        TabStripGeometry g({}, models_.size());
        if (g.exceedsDragThreshold(x - dragStartX_)) {
          dragging_ = true;
        }
      }
      if (dragging_) {
        dropIndicatorX_ = x;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      // Track mouse so we get WM_MOUSELEAVE.
      TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd_, 0};
      TrackMouseEvent(&t);
      return 0;
    }

    case WM_MOUSELEAVE: {
      hoveredTab_ = -1;
      hoveredCloseX_ = -1;
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    }

    case WM_LBUTTONUP: {
      const int x = GET_X_LPARAM(lp);
      const bool wasDragging = dragging_;
      const std::size_t fromIdx = dragFromIdx_;
      dragArmed_ = false;
      dragging_ = false;
      dropIndicatorX_ = -1;
      if (GetCapture() == hwnd_) ReleaseCapture();

      // Plus button hit-test
      TabStripGeometry g({}, models_.size());
      RECT cr{};
      GetClientRect(hwnd_, &cr);
      const int plusLeft = cr.right - g.metrics().plusButtonWidth;
      if (!wasDragging && x >= plusLeft) {
        if (onNew) onNew();
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
      }

      if (wasDragging) {
        const std::size_t to = g.dropIndex(rects_, fromIdx, x);
        if (to != fromIdx && onReorder) onReorder(fromIdx, to);
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
      }

      const int xHit = hitCloseAt(x);
      if (xHit >= 0 && models_[xHit].hasCloseX) {
        if (onClose) onClose(static_cast<std::size_t>(xHit));
        return 0;
      }
      const int tab = hitTabAt(x);
      if (tab >= 0 && onActivate) {
        onActivate(static_cast<std::size_t>(tab));
      }
      return 0;
    }

    case WM_MBUTTONUP: {
      const int x = GET_X_LPARAM(lp);
      const int tab = hitTabAt(x);
      if (tab >= 0 && models_[tab].hasCloseX && onClose) {
        onClose(static_cast<std::size_t>(tab));
      }
      return 0;
    }

    case WM_RBUTTONUP: {
      const int x = GET_X_LPARAM(lp);
      const int tab = hitTabAt(x);
      if (tab >= 0 && onContextMenu) {
        POINT pt{x, GET_Y_LPARAM(lp)};
        ClientToScreen(hwnd_, &pt);
        onContextMenu(static_cast<std::size_t>(tab), pt);
      }
      return 0;
    }

    case WM_CAPTURECHANGED: {
      // External capture stealer; abort drag cleanly.
      dragArmed_ = false;
      dragging_ = false;
      dropIndicatorX_ = -1;
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    }

    default:
      return DefWindowProcW(hwnd_, msg, wp, lp);
  }
}

LRESULT CALLBACK TabStrip::WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  auto* self = reinterpret_cast<TabStrip*>(
      GetWindowLongPtrW(h, GWLP_USERDATA));
  if (!self) return DefWindowProcW(h, m, wp, lp);
  return self->handle(m, wp, lp);
}

}  // namespace fast_explorer::ui
