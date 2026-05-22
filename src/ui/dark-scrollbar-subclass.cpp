#include "ui/dark-scrollbar-subclass.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>

namespace fast_explorer::ui {

namespace {

// Unique subclass id for SetWindowSubclass. Any constant works; we
// pick a magic value that's unlikely to collide with the listview's
// existing HeaderDark subclass (0xFE10A0) or any future subclass.
constexpr UINT_PTR kSubclassId = 0xDA12C5B1u;

// Win11-style dark scrollbar palette. Track is just barely lighter
// than the row background (RGB(24,24,24) is the inactive-pane bg in
// applyActivePaneAppearance) so the track edge blends, while the
// thumb gets a clearly visible grey that lifts further on hover and
// brightens again on press.
constexpr COLORREF kTrackColor        = RGB(24, 24, 24);
constexpr COLORREF kThumbColor        = RGB(60, 60, 60);
constexpr COLORREF kThumbHoverColor   = RGB(90, 90, 90);
constexpr COLORREF kThumbPressedColor = RGB(110, 110, 110);

// Per-instance subclass state. Allocated with `new` when the
// subclass is installed and freed in WM_NCDESTROY.
struct DarkScrollbarState {
  bool dark             = true;
  bool vThumbHovered    = false;
  bool hThumbHovered    = false;
  bool vThumbPressed    = false;
  bool hThumbPressed    = false;
  bool mouseTrackingOn  = false;
};

// All rect math below uses **window-local** coordinates: (0,0) is the
// top-left corner of the window (NOT the client area, NOT the screen).
// GetWindowDC is set up to draw in window-local coordinates, so the
// scrollbar rect we compute here can be passed straight to FillRect.

// Returns the vertical scrollbar rect (window-local) iff the listview
// has WS_VSCROLL visible; otherwise an empty rect.
RECT computeVScrollRect(HWND hwnd) noexcept {
  RECT empty{};
  const LONG style = GetWindowLong(hwnd, GWL_STYLE);
  if ((style & WS_VSCROLL) == 0) return empty;
  RECT wnd{};
  RECT client{};
  GetWindowRect(hwnd, &wnd);
  GetClientRect(hwnd, &client);
  const int wndW = wnd.right - wnd.left;
  const int wndH = wnd.bottom - wnd.top;
  // Map client (0,0) to screen so we can derive the NC margins. Then
  // translate to window-local by subtracting wnd.left/wnd.top.
  POINT clientTLScreen{0, 0};
  ClientToScreen(hwnd, &clientTLScreen);
  const int leftBorder   = clientTLScreen.x - wnd.left;
  const int topBorder    = clientTLScreen.y - wnd.top;
  const int rightBorder  = wndW - (leftBorder + (client.right - client.left));
  const int bottomBorder = wndH - (topBorder + (client.bottom - client.top));
  // Vertical scrollbar sits inside the right NC strip. The strip is
  // exactly `rightBorder` wide; treat the whole strip as the scrollbar
  // track since the listview reserves rightBorder == SM_CXVSCROLL when
  // WS_VSCROLL is on.
  RECT r;
  r.right  = wndW;
  r.left   = r.right - rightBorder;
  r.top    = topBorder;
  // The vertical track stops at the top of the horizontal track (when
  // both are visible) so the bottom-right corner can be filled
  // separately. bottomBorder already excludes the client area, so
  // wndH - bottomBorder lands exactly on the H-scroll's top edge.
  r.bottom = wndH - bottomBorder;
  return r;
}

// Returns the horizontal scrollbar rect (window-local) iff WS_HSCROLL
// is visible; empty otherwise.
RECT computeHScrollRect(HWND hwnd) noexcept {
  RECT empty{};
  const LONG style = GetWindowLong(hwnd, GWL_STYLE);
  if ((style & WS_HSCROLL) == 0) return empty;
  RECT wnd{};
  RECT client{};
  GetWindowRect(hwnd, &wnd);
  GetClientRect(hwnd, &client);
  const int wndW = wnd.right - wnd.left;
  const int wndH = wnd.bottom - wnd.top;
  POINT clientTLScreen{0, 0};
  ClientToScreen(hwnd, &clientTLScreen);
  const int leftBorder   = clientTLScreen.x - wnd.left;
  const int topBorder    = clientTLScreen.y - wnd.top;
  const int rightBorder  = wndW - (leftBorder + (client.right - client.left));
  const int bottomBorder = wndH - (topBorder + (client.bottom - client.top));
  RECT r;
  r.left   = leftBorder;
  // Share the corner with the vertical scrollbar so the corner rect
  // can be painted separately without overlapping either track.
  r.right  = wndW - rightBorder;
  r.top    = wndH - bottomBorder;
  r.bottom = wndH;
  return r;
}

// Bottom-right corner where vertical and horizontal scrollbars meet.
RECT computeCornerRect(HWND hwnd) noexcept {
  RECT empty{};
  const LONG style = GetWindowLong(hwnd, GWL_STYLE);
  if ((style & WS_VSCROLL) == 0 || (style & WS_HSCROLL) == 0) return empty;
  RECT wnd{};
  RECT client{};
  GetWindowRect(hwnd, &wnd);
  GetClientRect(hwnd, &client);
  const int wndW = wnd.right - wnd.left;
  const int wndH = wnd.bottom - wnd.top;
  POINT clientTLScreen{0, 0};
  ClientToScreen(hwnd, &clientTLScreen);
  const int leftBorder   = clientTLScreen.x - wnd.left;
  const int topBorder    = clientTLScreen.y - wnd.top;
  const int rightBorder  = wndW - (leftBorder + (client.right - client.left));
  const int bottomBorder = wndH - (topBorder + (client.bottom - client.top));
  RECT r;
  r.left   = wndW - rightBorder;
  r.right  = wndW;
  r.top    = wndH - bottomBorder;
  r.bottom = wndH;
  return r;
}

// Computes the thumb sub-rect inside the given track rect for a
// vertical scrollbar using SCROLLINFO range/page/pos.
RECT computeVThumb(HWND hwnd, const RECT& track) noexcept {
  RECT thumb = track;
  SCROLLINFO si{};
  si.cbSize = sizeof(si);
  si.fMask  = SIF_ALL;
  if (!GetScrollInfo(hwnd, SB_VERT, &si)) return thumb;
  const int trackH = track.bottom - track.top;
  if (trackH <= 0) return thumb;
  const int range = static_cast<int>(si.nMax) - static_cast<int>(si.nMin) + 1;
  if (range <= 0) return thumb;
  const int page = static_cast<int>(si.nPage);
  if (page >= range) {
    // Whole content fits — no thumb. Return empty so caller skips.
    thumb.top = thumb.bottom;
    return thumb;
  }
  // Thumb height proportional to visible page over total range.
  int thumbH = (trackH * page) / range;
  thumbH = std::max(thumbH, 20);
  thumbH = std::min(thumbH, trackH);
  const int posDen = std::max(1, range - page);
  const int posNum = std::max(0, static_cast<int>(si.nPos) - static_cast<int>(si.nMin));
  const int slack  = trackH - thumbH;
  const int thumbTop = track.top + (slack * posNum) / posDen;
  thumb.top    = thumbTop;
  thumb.bottom = thumbTop + thumbH;
  return thumb;
}

// Horizontal counterpart.
RECT computeHThumb(HWND hwnd, const RECT& track) noexcept {
  RECT thumb = track;
  SCROLLINFO si{};
  si.cbSize = sizeof(si);
  si.fMask  = SIF_ALL;
  if (!GetScrollInfo(hwnd, SB_HORZ, &si)) return thumb;
  const int trackW = track.right - track.left;
  if (trackW <= 0) return thumb;
  const int range = static_cast<int>(si.nMax) - static_cast<int>(si.nMin) + 1;
  if (range <= 0) return thumb;
  const int page = static_cast<int>(si.nPage);
  if (page >= range) {
    thumb.left = thumb.right;
    return thumb;
  }
  int thumbW = (trackW * page) / range;
  thumbW = std::max(thumbW, 20);
  thumbW = std::min(thumbW, trackW);
  const int posDen = std::max(1, range - page);
  const int posNum = std::max(0, static_cast<int>(si.nPos) - static_cast<int>(si.nMin));
  const int slack  = trackW - thumbW;
  const int thumbLeft = track.left + (slack * posNum) / posDen;
  thumb.left  = thumbLeft;
  thumb.right = thumbLeft + thumbW;
  return thumb;
}

// Paints the scrollbars using GetWindowDC. The DC origin is the
// top-left of the window (NOT client area), so we can fill the NC
// rects directly. Clipping the client area out lets us safely fill
// rects that *might* overlap (defensive) without disturbing row
// content; in practice the rects we compute already exclude client.
void paintScrollbars(HWND hwnd, DarkScrollbarState* st) noexcept {
  HDC dc = GetWindowDC(hwnd);
  if (dc == nullptr) return;
  // Defensive clip: never let our paint bleed into the client (row)
  // area. GetWindowDC gives us a window-coord DC where client coords
  // start at (leftBorder, topBorder), so translate the client rect
  // into window-local before excluding.
  RECT client{};
  GetClientRect(hwnd, &client);
  POINT clientTLScreen{0, 0};
  ClientToScreen(hwnd, &clientTLScreen);
  RECT wnd{};
  GetWindowRect(hwnd, &wnd);
  const int leftBorder = clientTLScreen.x - wnd.left;
  const int topBorder  = clientTLScreen.y - wnd.top;
  RECT clientInWnd{
      leftBorder,
      topBorder,
      leftBorder + (client.right - client.left),
      topBorder + (client.bottom - client.top),
  };
  ExcludeClipRect(dc, clientInWnd.left, clientInWnd.top,
                  clientInWnd.right, clientInWnd.bottom);

  HBRUSH trackBrush = CreateSolidBrush(kTrackColor);

  // Vertical
  RECT vTrack = computeVScrollRect(hwnd);
  if (vTrack.right > vTrack.left && vTrack.bottom > vTrack.top) {
    FillRect(dc, &vTrack, trackBrush);
    RECT vThumb = computeVThumb(hwnd, vTrack);
    if (vThumb.bottom > vThumb.top) {
      const COLORREF c = st->vThumbPressed ? kThumbPressedColor
                       : st->vThumbHovered ? kThumbHoverColor
                       :                     kThumbColor;
      HBRUSH thumbBrush = CreateSolidBrush(c);
      HPEN nullPen = static_cast<HPEN>(GetStockObject(NULL_PEN));
      HGDIOBJ oldBrush = SelectObject(dc, thumbBrush);
      HGDIOBJ oldPen   = SelectObject(dc, nullPen);
      // Inset the thumb slightly so the rounded corners sit inside
      // the track instead of butting against the row edge.
      RECT inset = vThumb;
      InflateRect(&inset, -2, 0);
      RoundRect(dc, inset.left, inset.top, inset.right, inset.bottom, 6, 6);
      SelectObject(dc, oldBrush);
      SelectObject(dc, oldPen);
      DeleteObject(thumbBrush);
    }
  }

  // Horizontal
  RECT hTrack = computeHScrollRect(hwnd);
  if (hTrack.right > hTrack.left && hTrack.bottom > hTrack.top) {
    FillRect(dc, &hTrack, trackBrush);
    RECT hThumb = computeHThumb(hwnd, hTrack);
    if (hThumb.right > hThumb.left) {
      const COLORREF c = st->hThumbPressed ? kThumbPressedColor
                       : st->hThumbHovered ? kThumbHoverColor
                       :                     kThumbColor;
      HBRUSH thumbBrush = CreateSolidBrush(c);
      HPEN nullPen = static_cast<HPEN>(GetStockObject(NULL_PEN));
      HGDIOBJ oldBrush = SelectObject(dc, thumbBrush);
      HGDIOBJ oldPen   = SelectObject(dc, nullPen);
      RECT inset = hThumb;
      InflateRect(&inset, 0, -2);
      RoundRect(dc, inset.left, inset.top, inset.right, inset.bottom, 6, 6);
      SelectObject(dc, oldBrush);
      SelectObject(dc, oldPen);
      DeleteObject(thumbBrush);
    }
  }

  // Corner block where both scrollbars meet — paint solid track colour
  // so the system's default white square doesn't leak through.
  RECT corner = computeCornerRect(hwnd);
  if (corner.right > corner.left && corner.bottom > corner.top) {
    FillRect(dc, &corner, trackBrush);
  }

  DeleteObject(trackBrush);
  ReleaseDC(hwnd, dc);
}

// Converts a screen-space NC mouse point (from WM_NCMOUSEMOVE lParam)
// to window-local coords for hit-testing against our scrollbar rects.
POINT screenToWindow(HWND hwnd, POINT screenPt) noexcept {
  RECT wnd{};
  GetWindowRect(hwnd, &wnd);
  return POINT{screenPt.x - wnd.left, screenPt.y - wnd.top};
}

// Invalidates the scrollbar NC regions so the next WM_NCPAINT redraws
// with the new hover/pressed state. We use RedrawWindow on the window
// rect with RDW_FRAME|RDW_INVALIDATE so common-controls reposts
// WM_NCPAINT; targeting just the NC rect would require WindowFromDC
// gymnastics that aren't worth the complexity here.
void invalidateScrollbars(HWND hwnd) noexcept {
  RedrawWindow(hwnd, nullptr, nullptr,
               RDW_FRAME | RDW_INVALIDATE | RDW_NOCHILDREN);
}

LRESULT CALLBACK darkScrollbarSubclassProc(HWND hwnd, UINT msg, WPARAM wParam,
                                           LPARAM lParam, UINT_PTR idSubclass,
                                           DWORD_PTR refData) {
  auto* state = reinterpret_cast<DarkScrollbarState*>(refData);
  if (state == nullptr) {
    return DefSubclassProc(hwnd, msg, wParam, lParam);
  }

  switch (msg) {
    case WM_NCDESTROY: {
      RemoveWindowSubclass(hwnd, &darkScrollbarSubclassProc, idSubclass);
      delete state;
      return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    case WM_NCPAINT: {
      // Let the default proc paint the 1-px border first. We overdraw
      // the scrollbar tracks/thumbs after — ExcludeClipRect in
      // paintScrollbars keeps the dark fill out of the client area.
      LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
      if (state->dark) {
        paintScrollbars(hwnd, state);
      }
      return r;
    }
    case WM_PAINT: {
      // Listview's WM_PAINT handler reaches into the scrollbar via
      // SetScrollInfo, which can re-paint the scrollbar over our
      // earlier WM_NCPAINT draw. Repaint scrollbars again after the
      // default WM_PAINT runs so our colours win the final frame.
      LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
      if (state->dark) {
        paintScrollbars(hwnd, state);
      }
      return r;
    }
    case WM_NCMOUSEMOVE: {
      if (state->dark) {
        const POINT scr{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const POINT pt = screenToWindow(hwnd, scr);
        RECT vTrack = computeVScrollRect(hwnd);
        RECT vThumb = computeVThumb(hwnd, vTrack);
        const bool vHover = (vThumb.bottom > vThumb.top) &&
                            PtInRect(&vThumb, pt);
        RECT hTrack = computeHScrollRect(hwnd);
        RECT hThumb = computeHThumb(hwnd, hTrack);
        const bool hHover = (hThumb.right > hThumb.left) &&
                            PtInRect(&hThumb, pt);
        if (vHover != state->vThumbHovered ||
            hHover != state->hThumbHovered) {
          state->vThumbHovered = vHover;
          state->hThumbHovered = hHover;
          invalidateScrollbars(hwnd);
        }
        if (!state->mouseTrackingOn) {
          TRACKMOUSEEVENT tme{};
          tme.cbSize    = sizeof(tme);
          tme.dwFlags   = TME_LEAVE | TME_NONCLIENT;
          tme.hwndTrack = hwnd;
          if (TrackMouseEvent(&tme)) {
            state->mouseTrackingOn = true;
          }
        }
      }
      break;
    }
    case WM_NCMOUSELEAVE: {
      state->mouseTrackingOn = false;
      if (state->vThumbHovered || state->hThumbHovered) {
        state->vThumbHovered = false;
        state->hThumbHovered = false;
        invalidateScrollbars(hwnd);
      }
      break;
    }
    case WM_NCLBUTTONDOWN: {
      if (state->dark) {
        const POINT scr{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const POINT pt = screenToWindow(hwnd, scr);
        RECT vThumb = computeVThumb(hwnd, computeVScrollRect(hwnd));
        RECT hThumb = computeHThumb(hwnd, computeHScrollRect(hwnd));
        if (vThumb.bottom > vThumb.top && PtInRect(&vThumb, pt)) {
          state->vThumbPressed = true;
          invalidateScrollbars(hwnd);
        } else if (hThumb.right > hThumb.left && PtInRect(&hThumb, pt)) {
          state->hThumbPressed = true;
          invalidateScrollbars(hwnd);
        }
      }
      break;  // fall through to DefSubclassProc to actually start drag
    }
    case WM_NCLBUTTONUP:
    case WM_LBUTTONUP: {
      if (state->vThumbPressed || state->hThumbPressed) {
        state->vThumbPressed = false;
        state->hThumbPressed = false;
        invalidateScrollbars(hwnd);
      }
      break;
    }
    case WM_VSCROLL:
    case WM_HSCROLL: {
      // Let the listview process the scroll first so nPos is up to
      // date, then repaint the thumb at its new position. Without
      // this the thumb visually lags one event behind the content.
      LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
      invalidateScrollbars(hwnd);
      return r;
    }
    case WM_MOUSEWHEEL: {
      LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
      invalidateScrollbars(hwnd);
      return r;
    }
    case WM_SIZE:
    case WM_THEMECHANGED: {
      LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
      invalidateScrollbars(hwnd);
      return r;
    }
    default:
      break;
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

}  // namespace

void applyDarkScrollbarSubclass(HWND hwnd, bool dark) noexcept {
  if (hwnd == nullptr) return;
  DWORD_PTR refData = 0;
  const BOOL installed = GetWindowSubclass(hwnd, &darkScrollbarSubclassProc,
                                            kSubclassId, &refData);
  if (!dark) {
    // Light mode — uninstall any existing subclass and force a full
    // NC repaint so the system-themed scrollbar reappears clean.
    if (installed) {
      auto* state = reinterpret_cast<DarkScrollbarState*>(refData);
      RemoveWindowSubclass(hwnd, &darkScrollbarSubclassProc, kSubclassId);
      delete state;
      RedrawWindow(hwnd, nullptr, nullptr,
                   RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW |
                       RDW_NOCHILDREN);
    }
    return;
  }
  if (installed) {
    if (auto* state = reinterpret_cast<DarkScrollbarState*>(refData)) {
      state->dark = true;
    }
    invalidateScrollbars(hwnd);
    return;
  }
  auto* state = new DarkScrollbarState();
  state->dark = true;
  SetWindowSubclass(hwnd, &darkScrollbarSubclassProc, kSubclassId,
                    reinterpret_cast<DWORD_PTR>(state));
  invalidateScrollbars(hwnd);
}

}  // namespace fast_explorer::ui
