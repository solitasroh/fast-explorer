#include "ui/pane-splitter.h"

#include <windowsx.h>

namespace fast_explorer::ui {

namespace {

// XOR-draw a 2-px ghost line on the parent's client DC. Drawing
// twice at the same position erases (XOR is self-inverse). When
// pos is negative the call is a no-op so we can pair WM_MOUSEMOVE's
// erase+draw symmetrically even on the first frame.
//
// `perpLow`/`perpHigh` clip the line to the splitter's logical span
// (e.g. an inner horizontal splitter only spans one column). When
// both are zero we fall back to the full parent client rect — useful
// for the unparented unit-test path and for the very first frame
// before relayout has populated the clip extents.
void drawGhost(HWND parent, SplitterOrientation orient, int pos,
               int perpLow, int perpHigh) {
  if (pos < 0) return;
  HDC dc = GetDC(parent);
  if (!dc) return;
  const int prevRop = SetROP2(dc, R2_NOTXORPEN);
  HPEN pen = CreatePen(PS_SOLID, 2, RGB(160, 160, 160));
  HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
  int lo = perpLow;
  int hi = perpHigh;
  if (lo == 0 && hi == 0) {
    RECT prc;
    GetClientRect(parent, &prc);
    if (orient == SplitterOrientation::Vertical) {
      lo = prc.top;
      hi = prc.bottom;
    } else {
      lo = prc.left;
      hi = prc.right;
    }
  }
  if (orient == SplitterOrientation::Vertical) {
    MoveToEx(dc, pos, lo, nullptr);
    LineTo(dc, pos, hi);
  } else {
    MoveToEx(dc, lo, pos, nullptr);
    LineTo(dc, hi, pos);
  }
  SelectObject(dc, oldPen);
  DeleteObject(pen);
  SetROP2(dc, prevRop);
  ReleaseDC(parent, dc);
}

LRESULT CALLBACK splitterWndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  auto* ctx = reinterpret_cast<SplitterContext*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_NCCREATE: {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
      auto* incoming = static_cast<SplitterContext*>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(incoming));
      return TRUE;
    }
    case WM_NCDESTROY: {
      delete ctx;
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_SETCURSOR: {
      const HCURSOR c = LoadCursorW(nullptr,
          (ctx && ctx->orient == SplitterOrientation::Vertical)
              ? IDC_SIZEWE : IDC_SIZENS);
      SetCursor(c);
      return TRUE;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT rc;
      GetClientRect(hwnd, &rc);
      const HBRUSH brush = GetSysColorBrush(COLOR_3DSHADOW);
      if (ctx && ctx->orient == SplitterOrientation::Vertical) {
        const int midX = (rc.right - rc.left) / 2;
        RECT line = {midX, rc.top, midX + 1, rc.bottom};
        FillRect(hdc, &line, brush);
      } else {
        const int midY = (rc.bottom - rc.top) / 2;
        RECT line = {rc.left, midY, rc.right, midY + 1};
        FillRect(hdc, &line, brush);
      }
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_LBUTTONDOWN: {
      if (!ctx || !ctx->ratios) return 0;
      SetCapture(hwnd);
      ctx->dragging = true;
      POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      ClientToScreen(hwnd, &pt);
      ctx->dragStartScreen = pt;
      ctx->ratioAtStart = ctx->ratios->ratios[ctx->ratioId];

      HWND parent = GetParent(hwnd);
      POINT pcli = pt;
      ScreenToClient(parent, &pcli);
      ctx->lastGhostPos =
          (ctx->orient == SplitterOrientation::Vertical) ? pcli.x : pcli.y;
      drawGhost(parent, ctx->orient, ctx->lastGhostPos,
                ctx->perpLow, ctx->perpHigh);
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (!ctx || !ctx->dragging) return 0;
      POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      ClientToScreen(hwnd, &pt);
      HWND parent = GetParent(hwnd);
      POINT pcli = pt;
      ScreenToClient(parent, &pcli);
      const int newPos =
          (ctx->orient == SplitterOrientation::Vertical) ? pcli.x : pcli.y;

      drawGhost(parent, ctx->orient, ctx->lastGhostPos,
                ctx->perpLow, ctx->perpHigh);
      drawGhost(parent, ctx->orient, newPos,
                ctx->perpLow, ctx->perpHigh);
      ctx->lastGhostPos = newPos;
      return 0;
    }
    case WM_LBUTTONUP: {
      if (!ctx || !ctx->dragging) return 0;
      // Snapshot drag state into locals BEFORE ReleaseCapture. The
      // ReleaseCapture call synchronously dispatches WM_CAPTURECHANGED
      // on the same thread (Win32 contract), and that handler resets
      // ctx->lastGhostPos to -1. Reading the field AFTER the release
      // would feed -1 into computeRatioFromCursor, which then clamps
      // to the 0.1 floor — visible as "every drag snaps to 10%".
      // Clearing `dragging` here also turns the WM_CAPTURECHANGED
      // handler into a no-op so it does not double-erase the ghost.
      const int   commitPos       = ctx->lastGhostPos;
      const int   commitOrigin    = ctx->axisOriginInParent;
      const int   commitLength    = ctx->axisLengthForRatio;
      const auto  commitOrient    = ctx->orient;
      const int   commitPerpLow   = ctx->perpLow;
      const int   commitPerpHigh  = ctx->perpHigh;
      const std::uint8_t commitRatioId = ctx->ratioId;
      SplitterRatios* const commitRatios = ctx->ratios;
      auto commitCallback = ctx->onCommit;  // copy; ctx may relayout
      ctx->dragging = false;
      ctx->lastGhostPos = -1;
      HWND parent = GetParent(hwnd);
      drawGhost(parent, commitOrient, commitPos, commitPerpLow, commitPerpHigh);
      ReleaseCapture();

      const float clamped = computeRatioFromCursor(
          commitPos, commitOrigin, commitLength);
      if (commitRatios) {
        commitRatios->ratios[commitRatioId] = clamped;
      }
      if (commitCallback) commitCallback();
      return 0;
    }
    case WM_CAPTURECHANGED: {
      if (ctx && ctx->dragging) {
        drawGhost(GetParent(hwnd), ctx->orient, ctx->lastGhostPos,
                  ctx->perpLow, ctx->perpHigh);
        ctx->dragging = false;
        ctx->lastGhostPos = -1;
      }
      return 0;
    }
  }
  return DefWindowProcW(hwnd, msg, w, l);
}

}  // namespace

bool PaneSplitter::registerClass(HINSTANCE instance) noexcept {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = splitterWndProc;
  wc.hInstance = instance;
  wc.hCursor = nullptr;        // owned by WM_SETCURSOR
  wc.hbrBackground = nullptr;  // owned by WM_ERASEBKGND/WM_PAINT
  wc.lpszClassName = kClassName;
  const ATOM atom = RegisterClassExW(&wc);
  if (atom != 0) return true;
  return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND PaneSplitter::create(HINSTANCE instance, HWND parent,
                          SplitterContext ctx) {
  auto* heapCtx = new SplitterContext(std::move(ctx));
  HWND hwnd = CreateWindowExW(
      0, kClassName, L"",
      WS_CHILD | WS_CLIPSIBLINGS,
      0, 0, 0, 0,
      parent, nullptr, instance, heapCtx);
  if (hwnd == nullptr) {
    delete heapCtx;
  }
  return hwnd;
}

}  // namespace fast_explorer::ui
