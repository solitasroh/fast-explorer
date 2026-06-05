#include "explorer/navigation-tree-pane.h"

#include <commctrl.h>

#include <algorithm>
#include <utility>

#include "winui_lite/chrome/theme-watcher.h"

namespace fast_explorer::ui {

namespace {

constexpr wchar_t kNavigationTreeClassName[] =
    L"FastExplorer.NavigationTreePane";
constexpr int kNavigationTreeInsetLeftDip = 8;
constexpr int kNavigationTreeInsetTopDip = 6;
constexpr int kNavigationTreeInsetRightDip = 5;
constexpr int kNavigationTreeInsetBottomDip = 5;
constexpr int kNavigationTreeIndentDip = 18;
constexpr int kNavigationTreeRowHeightDip = 24;

int scaleForDpi(int value, UINT dpi) noexcept {
  return MulDiv(value, static_cast<int>(dpi), 96);
}

COLORREF navigationPanelBackground() noexcept {
  const RowTheme theme = currentRowTheme();
  return theme.background;
}

}  // namespace

int clampNavigationTreeWidthPx(
    int requestedPx, int clientWidthPx, UINT dpi) noexcept {
  const int minW = scaleForDpi(kNavigationTreeMinWidthDip, dpi);
  const int defaultW = scaleForDpi(kNavigationTreeDefaultWidthDip, dpi);
  const int maxW = scaleForDpi(kNavigationTreeMaxWidthDip, dpi);
  const int clientCap = std::max(0, clientWidthPx * 45 / 100);
  const int upper = std::min(maxW, clientCap);
  const int requested = requestedPx > 0 ? requestedPx : defaultW;
  if (upper <= 0) return 0;
  if (upper < minW) return upper;
  return std::clamp(requested, minW, upper);
}

NavigationTreePane::~NavigationTreePane() {
  destroy();
}

bool NavigationTreePane::create(HWND parent, HINSTANCE instance) {
  if (hwnd_ != nullptr) return true;

  WNDCLASSW wc{};
  wc.lpfnWndProc = &NavigationTreePane::wndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  wc.lpszClassName = kNavigationTreeClassName;
  RegisterClassW(&wc);

  hwnd_ = CreateWindowExW(0, kNavigationTreeClassName, L"",
                          WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                          0, 0, 0, 0, parent, nullptr, instance, this);
  if (hwnd_ == nullptr) return false;

  tree_ = CreateWindowExW(
      0, WC_TREEVIEWW, L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS |
          TVS_SHOWSELALWAYS | TVS_FULLROWSELECT | TVS_TRACKSELECT |
          TVS_NOHSCROLL,
      0, 0, 0, 0, hwnd_, nullptr, instance, nullptr);
  if (tree_ == nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    return false;
  }

  if (!controller_.attach(tree_)) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    tree_ = nullptr;
    return false;
  }
  applyVisualMetrics();
  layoutTree();
  return true;
}

void NavigationTreePane::destroy() noexcept {
  controller_.detach();
  if (hwnd_ != nullptr && IsWindow(hwnd_)) {
    DestroyWindow(hwnd_);
  }
  hwnd_ = nullptr;
  tree_ = nullptr;
}

void NavigationTreePane::setNavigateCallback(NavigateCallback callback) {
  onNavigate_ = std::move(callback);
}

void NavigationTreePane::setBounds(int x, int y, int width, int height) {
  if (hwnd_ == nullptr) return;
  SetWindowPos(hwnd_, nullptr, x, y, std::max(0, width), std::max(0, height),
               SWP_NOZORDER | SWP_NOACTIVATE);
  applyVisualMetrics();
  layoutTree();
}

void NavigationTreePane::show(bool visible) noexcept {
  if (hwnd_ != nullptr) {
    if (visible) {
      applyTheme();
      layoutTree();
    }
    ShowWindow(hwnd_, visible ? SW_SHOWNA : SW_HIDE);
  }
}

void NavigationTreePane::applyTheme() noexcept {
  controller_.applyTheme();
  if (hwnd_ != nullptr) {
    InvalidateRect(hwnd_, nullptr, TRUE);
  }
}

void NavigationTreePane::ensureRoots() {
  controller_.ensureRoots();
}

void NavigationTreePane::reflectLocation(const std::wstring& location) {
  if (!available() || location.empty()) return;
  suppressSelectionCommit_ = true;
  controller_.reflectLocation(location);
  suppressSelectionCommit_ = false;
}

LRESULT CALLBACK NavigationTreePane::wndProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  NavigationTreePane* self = nullptr;
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = reinterpret_cast<NavigationTreePane*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<NavigationTreePane*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (self != nullptr) {
    return self->handleMessage(hwnd, msg, wParam, lParam);
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT NavigationTreePane::handleMessage(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_ERASEBKGND: {
      HDC hdc = reinterpret_cast<HDC>(wParam);
      RECT rc{};
      GetClientRect(hwnd, &rc);
      HBRUSH brush = CreateSolidBrush(navigationPanelBackground());
      FillRect(hdc, &rc, brush);
      DeleteObject(brush);
      return 1;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT rc{};
      GetClientRect(hwnd, &rc);
      HBRUSH brush = CreateSolidBrush(navigationPanelBackground());
      FillRect(hdc, &rc, brush);
      DeleteObject(brush);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_SIZE:
      if (tree_ != nullptr) {
        applyVisualMetrics();
        layoutTree();
      }
      return 0;
    case WM_NOTIFY:
      return handleTreeNotify(reinterpret_cast<NMHDR*>(lParam));
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
      applyTheme();
      return 0;
    case WM_NCDESTROY:
      if (hwnd == hwnd_) {
        controller_.detach();
        hwnd_ = nullptr;
        tree_ = nullptr;
      }
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT NavigationTreePane::handleTreeNotify(NMHDR* hdr) {
  if (hdr == nullptr || hdr->hwndFrom != tree_) return 0;
  controller_.handleNotify(hdr);
  if (hdr->code == TVN_SELCHANGEDW && !suppressSelectionCommit_) {
    auto* tv = reinterpret_cast<NMTREEVIEWW*>(hdr);
    const auto selection = controller_.selectionForItem(tv->itemNew.hItem);
    if (selection && !selection->location.empty() && onNavigate_) {
      onNavigate_(selection->location);
    }
  }
  return 0;
}

void NavigationTreePane::applyVisualMetrics() noexcept {
  if (tree_ == nullptr) return;
  const UINT dpi = GetDpiForWindow(hwnd_ != nullptr ? hwnd_ : tree_);
  if (dpi == 0 || dpi == appliedDpi_) return;
  appliedDpi_ = dpi;
  TreeView_SetItemHeight(tree_, scaleForDpi(kNavigationTreeRowHeightDip, dpi));
  TreeView_SetIndent(tree_, scaleForDpi(kNavigationTreeIndentDip, dpi));
}

void NavigationTreePane::layoutTree() noexcept {
  if (hwnd_ == nullptr || tree_ == nullptr) return;
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  const UINT dpi = GetDpiForWindow(hwnd_);
  const int left = scaleForDpi(kNavigationTreeInsetLeftDip, dpi);
  const int top = scaleForDpi(kNavigationTreeInsetTopDip, dpi);
  const int right = scaleForDpi(kNavigationTreeInsetRightDip, dpi);
  const int bottom = scaleForDpi(kNavigationTreeInsetBottomDip, dpi);
  const int clientW = static_cast<int>(rc.right - rc.left);
  const int clientH = static_cast<int>(rc.bottom - rc.top);
  const int width = std::max(0, clientW - left - right);
  const int height = std::max(0, clientH - top - bottom);
  SetWindowPos(tree_, nullptr, left, top, width, height,
               SWP_NOZORDER | SWP_NOACTIVATE);
}

}  // namespace fast_explorer::ui
