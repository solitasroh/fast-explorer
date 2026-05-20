#include "ui/pane-splitter.h"

#include <windowsx.h>

#include <algorithm>

namespace fast_explorer::ui {

namespace {

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
