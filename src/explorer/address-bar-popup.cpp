#include "explorer/address-bar-popup.h"

#include <memory>

#include "explorer/messages.h"

namespace fast_explorer::ui {

namespace {

constexpr wchar_t kPopupClassName[] = L"FastExplorer.AddressBarPopup";
constexpr int kPopupWidth = 480;
constexpr int kPopupHeight = 420;

int scaleForDpi(int value, UINT dpi) noexcept {
  return MulDiv(value, static_cast<int>(dpi), 96);
}

thread_local AddressBarPopup* tMouseHookOwner = nullptr;

}  // namespace

AddressBarPopup::AddressBarPopup(HWND owner) : owner_(owner) {}

AddressBarPopup::~AddressBarPopup() {
  uninstallMouseHook();
  // Force PIDL release before the TreeView is destroyed by
  // DestroyWindow's child cascade; relying on WM_DESTROY/TVN_DELETEITEM
  // bubble timing is fragile across Win32 versions.
  treeController_.detach();
  if (popup_ && IsWindow(popup_)) {
    DestroyWindow(popup_);
  }
  popup_ = nullptr;
  tree_ = nullptr;
}

bool AddressBarPopup::isVisible() const noexcept {
  return popup_ != nullptr && IsWindowVisible(popup_);
}

void AddressBarPopup::hide() {
  uninstallMouseHook();
  if (popup_ && IsWindowVisible(popup_)) {
    ShowWindow(popup_, SW_HIDE);
  }
}

void AddressBarPopup::ensurePopupCreated() {
  if (popup_) return;
  HINSTANCE inst = reinterpret_cast<HINSTANCE>(
      GetWindowLongPtrW(owner_, GWLP_HINSTANCE));
  WNDCLASSW wc{};
  wc.style = CS_DROPSHADOW;
  wc.lpfnWndProc = &AddressBarPopup::popupWndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kPopupClassName;
  // Idempotent: ignore the duplicate-class error on second create.
  RegisterClassW(&wc);

  popup_ = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
      kPopupClassName, L"",
      WS_POPUP | WS_BORDER,
      0, 0, kPopupWidth, kPopupHeight,
      owner_, nullptr, inst, this);
  if (!popup_) return;

  // TVS_FULLROWSELECT is mutually exclusive with TVS_HASLINES.
  tree_ = CreateWindowExW(
      0, WC_TREEVIEWW, L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP |
          TVS_HASBUTTONS | TVS_SHOWSELALWAYS |
          TVS_FULLROWSELECT | TVS_TRACKSELECT,
      0, 0, kPopupWidth, kPopupHeight,
      popup_, nullptr, inst, nullptr);
  if (!tree_) {
    DestroyWindow(popup_);
    popup_ = nullptr;
    return;
  }
  if (!treeController_.attach(tree_)) {
    DestroyWindow(popup_);
    popup_ = nullptr;
    tree_ = nullptr;
    return;
  }
  SetWindowSubclass(tree_, &AddressBarPopup::treeSubclassProc, 0,
                    reinterpret_cast<DWORD_PTR>(this));
}

void AddressBarPopup::showFor(HWND anchor, const std::wstring& currentPath) {
  ensurePopupCreated();
  if (!popup_) return;
  anchor_ = anchor;
  // Re-apply theme on each open so a runtime light↔dark flip is
  // reflected without restarting; cheap (RegQueryValue + two
  // SendMessageW). The class hbrBackground is fixed at RegisterClass
  // time to COLOR_WINDOW, but the tree fully covers it via WM_SIZE.
  treeController_.applyTheme();
  InvalidateRect(popup_, nullptr, TRUE);
  treeController_.ensureRoots();
  RECT r;
  GetWindowRect(anchor, &r);
  const UINT dpi = GetDpiForWindow(owner_);
  const int minW = scaleForDpi(kPopupWidth, dpi);
  const int h = scaleForDpi(kPopupHeight, dpi);
  int x = r.left;
  int y = r.bottom;
  int w = r.right - r.left;
  if (w < minW) w = minW;
  SetWindowPos(popup_, HWND_TOPMOST, x, y, w, h,
               SWP_SHOWWINDOW | SWP_NOACTIVATE);
  installMouseHook();
  SetFocus(tree_);
  pendingPath_ = currentPath;
  if (!currentPath.empty()) treeController_.reflectLocation(currentPath);
}

void AddressBarPopup::reflectCurrentPath(const std::wstring& currentPath) {
  pendingPath_ = currentPath;
  if (popup_ && IsWindowVisible(popup_) && !currentPath.empty()) {
    treeController_.reflectLocation(currentPath);
  }
}

void AddressBarPopup::installMouseHook() {
  if (mouseHook_) return;
  tMouseHookOwner = this;
  mouseHook_ = SetWindowsHookExW(WH_MOUSE, &AddressBarPopup::mouseHookProc,
                                 nullptr, GetCurrentThreadId());
  if (!mouseHook_ && tMouseHookOwner == this) {
    tMouseHookOwner = nullptr;
  }
}

void AddressBarPopup::uninstallMouseHook() {
  if (mouseHook_) {
    UnhookWindowsHookEx(mouseHook_);
    mouseHook_ = nullptr;
  }
  if (tMouseHookOwner == this) {
    tMouseHookOwner = nullptr;
  }
}

bool AddressBarPopup::containsScreenPoint(POINT pt) const noexcept {
  if (!popup_) return false;
  RECT r{};
  return GetWindowRect(popup_, &r) && PtInRect(&r, pt);
}

void AddressBarPopup::commitSelection(HTREEITEM node) {
  if (!tree_ || !node) return;
  auto selection = treeController_.selectionForItem(node);
  if (!selection || selection->location.empty()) return;
  auto payload = std::make_unique<std::wstring>(
      std::move(selection->location));
  if (PostMessageW(owner_, kWmFeAddressPopupPick,
                   reinterpret_cast<WPARAM>(payload.get()),
                   static_cast<LPARAM>(activePaneIdx_))) {
    [[maybe_unused]] auto* leaked = payload.release();
  }
  hide();
}

LRESULT CALLBACK AddressBarPopup::treeSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR id, DWORD_PTR ref) {
  auto* self = reinterpret_cast<AddressBarPopup*>(ref);
  switch (msg) {
    case WM_NCDESTROY:
      RemoveWindowSubclass(hwnd, &AddressBarPopup::treeSubclassProc, id);
      break;
    case WM_KEYDOWN: {
      if (wParam == VK_ESCAPE && self && self->popup_) {
        self->hide();
        return 0;
      }
      if (wParam == VK_RETURN && self && self->tree_) {
        HTREEITEM sel = TreeView_GetSelection(self->tree_);
        if (sel) self->commitSelection(sel);
        return 0;
      }
      break;
    }
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK AddressBarPopup::mouseHookProc(int nCode, WPARAM wParam,
                                                 LPARAM lParam) {
  if (nCode == HC_ACTION) {
    auto* self = tMouseHookOwner;
    auto* mouse = reinterpret_cast<MOUSEHOOKSTRUCT*>(lParam);
    if (self && mouse && self->popup_ && IsWindowVisible(self->popup_)) {
      if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN ||
          wParam == WM_NCLBUTTONDOWN) {
        // Skip the auto-hide if the click lands on the anchor (the
        // address-bar Edit that opened us). Otherwise click-to-toggle
        // in the Edit subclass would hide → my onCommand handler
        // would immediately reshow, defeating the toggle.
        bool onAnchor = false;
        if (self->anchor_ != nullptr && IsWindow(self->anchor_)) {
          RECT a{};
          GetWindowRect(self->anchor_, &a);
          onAnchor = mouse->pt.x >= a.left && mouse->pt.x < a.right &&
                     mouse->pt.y >= a.top && mouse->pt.y < a.bottom;
        }
        if (!onAnchor && !self->containsScreenPoint(mouse->pt)) {
          PostMessageW(self->popup_, kWmFeAddressPopupHide, 0, 0);
        }
      }
    }
  }
  // hhk arg to CallNextHookEx is ignored by the hook chain; pass
  // nullptr to avoid a TOCTOU race with uninstallMouseHook nulling
  // mouseHook_ between the check above and the chain call.
  return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK AddressBarPopup::popupWndProc(HWND hwnd, UINT msg,
                                                WPARAM wParam, LPARAM lParam) {
  AddressBarPopup* self = nullptr;
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = reinterpret_cast<AddressBarPopup*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<AddressBarPopup*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (self) {
    return self->handlePopupMessage(hwnd, msg, wParam, lParam);
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT AddressBarPopup::handlePopupMessage(HWND hwnd, UINT msg,
                                             WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case kWmFeAddressPopupHide: {
      hide();
      return 0;
    }
    case kWmFeAddressPopupClick: {
      auto node = reinterpret_cast<HTREEITEM>(wParam);
      if (node) commitSelection(node);
      return 0;
    }
    case WM_SIZE: {
      if (tree_) {
        SetWindowPos(tree_, nullptr, 0, 0, LOWORD(lParam), HIWORD(lParam),
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      return 0;
    }
    case WM_NOTIFY:
      return onTreeNotify(reinterpret_cast<NMHDR*>(lParam));
    case WM_KEYDOWN: {
      if (wParam == VK_ESCAPE) {
        hide();
        return 0;
      }
      if (wParam == VK_RETURN) {
        HTREEITEM sel = TreeView_GetSelection(tree_);
        if (sel) commitSelection(sel);
        return 0;
      }
      break;
    }
    case WM_DESTROY: {
      uninstallMouseHook();
      treeController_.detach();
      return 0;
    }
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT AddressBarPopup::onTreeNotify(NMHDR* hdr) {
  if (!hdr || hdr->hwndFrom != tree_) return 0;
  treeController_.handleNotify(hdr);
  switch (hdr->code) {
    case NM_CLICK:
      onTreeClick();
      return 0;
    case NM_DBLCLK:
    case NM_RETURN: {
      HTREEITEM sel = TreeView_GetSelection(tree_);
      if (sel) commitSelection(sel);
      return 0;
    }
    default:
      return 0;
  }
}

void AddressBarPopup::onTreeClick() {
  POINT pt;
  GetCursorPos(&pt);
  ScreenToClient(tree_, &pt);
  TVHITTESTINFO ht{};
  ht.pt = pt;
  HTREEITEM hit = TreeView_HitTest(tree_, &ht);
  const UINT itemFlags = TVHT_ONITEM | TVHT_ONITEMLABEL | TVHT_ONITEMICON |
                         TVHT_ONITEMRIGHT;
  if (hit && (ht.flags & itemFlags)) {
    PostMessageW(popup_, kWmFeAddressPopupClick,
                 reinterpret_cast<WPARAM>(hit), 0);
  }
}

}  // namespace fast_explorer::ui
