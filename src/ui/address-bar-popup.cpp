#include "ui/address-bar-popup.h"

#include <shellapi.h>
#include <shlwapi.h>
#include <uxtheme.h>

#include <cwctype>
#include <memory>
#include <vector>

#include "ui/com-raii.h"
#include "ui/messages.h"

namespace fast_explorer::ui {

namespace {

constexpr wchar_t kPopupClassName[] = L"FastExplorer.AddressBarPopup";
constexpr int kPopupWidth = 480;
constexpr int kPopupHeight = 420;
// Per-expand cap so a runaway shell folder does not freeze the UI.
constexpr int kMaxChildrenPerExpand = 512;

int scaleForDpi(int value, UINT dpi) noexcept {
  return MulDiv(value, static_cast<int>(dpi), 96);
}

thread_local AddressBarPopup* tMouseHookOwner = nullptr;

// Local copy — main-window.cpp owns the canonical one but it's in its
// anon namespace, not exported. Cheap registry probe; the popup only
// (re)reads it on show / theme change.
bool prefersDarkMode() noexcept {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\"
                    L"Themes\\Personalize",
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

void applyTreePopupTheme(HWND popup, HWND tree) noexcept {
  if (tree == nullptr) return;
  const bool dark = prefersDarkMode();
  // DarkMode_Explorer also re-tints the chevron/expand glyphs so they
  // read against the dark row backdrop; plain Explorer keeps the
  // light-mode chevrons.
  SetWindowTheme(tree, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
  if (dark) {
    TreeView_SetBkColor(tree, RGB(32, 32, 32));
    TreeView_SetTextColor(tree, RGB(241, 241, 241));
  } else {
    TreeView_SetBkColor(tree, static_cast<COLORREF>(-1));
    TreeView_SetTextColor(tree, static_cast<COLORREF>(-1));
  }
  if (popup != nullptr) {
    InvalidateRect(popup, nullptr, TRUE);
  }
}

std::wstring strRetToString(STRRET& sr, LPCITEMIDLIST relative) {
  wchar_t buf[MAX_PATH];
  if (FAILED(StrRetToBufW(&sr, relative, buf,
                          static_cast<UINT>(std::size(buf))))) {
    return {};
  }
  return std::wstring(buf);
}

int systemIconIndexForPidl(LPCITEMIDLIST absolute) noexcept {
  SHFILEINFOW sfi{};
  const UINT flags = SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON;
  if (SHGetFileInfoW(reinterpret_cast<LPCWSTR>(absolute), 0, &sfi,
                     sizeof(sfi), flags) == 0) {
    return -1;
  }
  return sfi.iIcon;
}

HIMAGELIST systemSmallImageList() noexcept {
  SHFILEINFOW sfi{};
  return reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(
      L"x", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
      SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES));
}

PidlOwner knownFolderPidl(REFKNOWNFOLDERID id) noexcept {
  PIDLIST_ABSOLUTE p = nullptr;
  if (FAILED(SHGetKnownFolderIDList(id, KF_FLAG_DEFAULT, nullptr, &p))) {
    return {};
  }
  return PidlOwner(p);
}

// .zip/.cab/.iso advertise SFGAO_FOLDER; STREAM bit distinguishes them.
bool isRealDirectory(IShellFolder* parent, LPCITEMIDLIST relative) noexcept {
  if (!parent || !relative) return false;
  SFGAOF attrs = SFGAO_FOLDER | SFGAO_STREAM;
  if (FAILED(parent->GetAttributesOf(1, &relative, &attrs))) {
    return false;
  }
  return (attrs & SFGAO_FOLDER) != 0 && (attrs & SFGAO_STREAM) == 0;
}

ComPtr<IShellFolder> bindFolder(IShellFolder* desktop, LPCITEMIDLIST abs) {
  ComPtr<IShellFolder> out;
  if (!desktop || !abs) return out;
  if (FAILED(desktop->BindToObject(abs, nullptr, IID_IShellFolder,
                                   reinterpret_cast<void**>(out.put())))) {
    out.reset();
  }
  return out;
}

std::wstring pidlToFsPath(LPCITEMIDLIST abs) {
  wchar_t buf[MAX_PATH] = {};
  if (!SHGetPathFromIDListW(abs, buf)) return {};
  return std::wstring(buf);
}

std::wstring normalisePath(std::wstring p) {
  if (p.size() > 3 && p.back() == L'\\') p.pop_back();
  return p;
}

bool pathsEqual(const std::wstring& a, const std::wstring& b) noexcept {
  if (a.size() != b.size()) return false;
  return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
                              b.c_str(), static_cast<int>(b.size()),
                              TRUE) == CSTR_EQUAL;
}

// Sentinel lParam for the placeholder child that makes [+] visible
// before real children are loaded.
constexpr LPARAM kDummyLParam = static_cast<LPARAM>(-1);

bool isDummyItem(const TVITEMW& it) noexcept {
  return it.lParam == kDummyLParam;
}

void insertDummyChild(HWND tree, HTREEITEM parent) {
  TVINSERTSTRUCTW ins{};
  ins.hParent = parent;
  ins.hInsertAfter = TVI_LAST;
  ins.item.mask = TVIF_TEXT | TVIF_PARAM;
  wchar_t placeholder[] = L"";
  ins.item.pszText = placeholder;
  ins.item.lParam = kDummyLParam;
  TreeView_InsertItem(tree, &ins);
}

HTREEITEM insertNode(HWND tree, HTREEITEM parent, const std::wstring& name,
                     PidlOwner abs, bool hasChildren) {
  if (!tree || name.empty() || !abs) return nullptr;
  const int icon = systemIconIndexForPidl(abs.get());
  TVINSERTSTRUCTW ins{};
  ins.hParent = parent;
  ins.hInsertAfter = TVI_LAST;
  ins.item.mask = TVIF_TEXT | TVIF_PARAM |
                  ((icon >= 0) ? (TVIF_IMAGE | TVIF_SELECTEDIMAGE) : 0);
  ins.item.pszText = const_cast<wchar_t*>(name.c_str());
  ins.item.cchTextMax = static_cast<int>(name.size());
  ins.item.iImage = icon;
  ins.item.iSelectedImage = icon;
  ins.item.lParam = reinterpret_cast<LPARAM>(abs.get());
  HTREEITEM node = TreeView_InsertItem(tree, &ins);
  if (!node) return nullptr;
  (void)abs.release();
  if (hasChildren) {
    insertDummyChild(tree, node);
  }
  return node;
}

LPCITEMIDLIST itemPidl(HWND tree, HTREEITEM node) noexcept {
  if (!tree || !node) return nullptr;
  TVITEMW it{};
  it.mask = TVIF_PARAM;
  it.hItem = node;
  if (!TreeView_GetItem(tree, &it)) return nullptr;
  return reinterpret_cast<LPCITEMIDLIST>(it.lParam);
}

}  // namespace

AddressBarPopup::AddressBarPopup(HWND owner) : owner_(owner) {}

AddressBarPopup::~AddressBarPopup() {
  uninstallMouseHook();
  // Force PIDL release before the TreeView is destroyed by
  // DestroyWindow's child cascade; relying on WM_DESTROY/TVN_DELETEITEM
  // bubble timing is fragile across Win32 versions.
  if (tree_ && IsWindow(tree_)) {
    TreeView_DeleteAllItems(tree_);
  }
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
  HIMAGELIST sys = systemSmallImageList();
  if (sys) {
    TreeView_SetImageList(tree_, sys, TVSIL_NORMAL);
  }
  // Dark-mode aware theme + tree bg/text colours; falls back to plain
  // Explorer theme + system defaults when the user is in light mode.
  applyTreePopupTheme(popup_, tree_);
  SetWindowSubclass(tree_, &AddressBarPopup::treeSubclassProc, 0,
                    reinterpret_cast<DWORD_PTR>(this));
}

void AddressBarPopup::showFor(HWND anchor, const std::wstring& currentPath) {
  ensurePopupCreated();
  if (!popup_) return;
  // Re-apply theme on each open so a runtime light↔dark flip is
  // reflected without restarting; cheap (RegQueryValue + two
  // SendMessageW). The class hbrBackground is fixed at RegisterClass
  // time to COLOR_WINDOW, but the tree fully covers it via WM_SIZE.
  applyTreePopupTheme(popup_, tree_);
  if (!rootsLoaded_) {
    populateRoots();
    rootsLoaded_ = true;
  }
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
  if (!currentPath.empty()) selectPath(currentPath);
}

void AddressBarPopup::reflectCurrentPath(const std::wstring& currentPath) {
  pendingPath_ = currentPath;
  if (popup_ && IsWindowVisible(popup_) && !currentPath.empty()) {
    selectPath(currentPath);
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

void AddressBarPopup::populateRoots() {
  if (!tree_) return;
  ComPtr<IShellFolder> desktop;
  if (FAILED(SHGetDesktopFolder(desktop.put())) || !desktop) return;
  PidlOwner desktopAbs = knownFolderPidl(FOLDERID_Desktop);
  if (!desktopAbs) return;

  ComPtr<IEnumIDList> en;
  if (FAILED(desktop->EnumObjects(nullptr, SHCONTF_FOLDERS, en.put())) ||
      !en) return;

  LPITEMIDLIST relative = nullptr;
  ULONG fetched = 0;
  while (en->Next(1, &relative, &fetched) == S_OK && fetched == 1) {
    PidlOwner relOwner(relative);
    if (!isRealDirectory(desktop.get(), relOwner.get())) continue;
    STRRET sr{};
    if (FAILED(desktop->GetDisplayNameOf(relOwner.get(), SHGDN_NORMAL,
                                         &sr))) {
      continue;
    }
    std::wstring name = strRetToString(sr, relOwner.get());
    if (name.empty()) continue;
    LPITEMIDLIST absRaw = ILCombine(desktopAbs.get(), relOwner.get());
    if (!absRaw) continue;
    insertNode(tree_, TVI_ROOT, name, PidlOwner(absRaw), true);
  }

  PidlOwner computerAbs = knownFolderPidl(FOLDERID_ComputerFolder);
  if (computerAbs) {
    for (HTREEITEM r = TreeView_GetRoot(tree_); r;
         r = TreeView_GetNextSibling(tree_, r)) {
      LPCITEMIDLIST abs = itemPidl(tree_, r);
      if (abs && ILIsEqual(abs, computerAbs.get())) {
        TreeView_Expand(tree_, r, TVE_EXPAND);
        break;
      }
    }
  }
}

void AddressBarPopup::expandNode(HTREEITEM node) {
  if (!tree_ || !node) return;
  LPCITEMIDLIST abs = itemPidl(tree_, node);
  if (!abs) return;

  ComPtr<IShellFolder> desktop;
  if (FAILED(SHGetDesktopFolder(desktop.put())) || !desktop) return;
  ComPtr<IShellFolder> folder = bindFolder(desktop.get(), abs);
  if (!folder) {
    HTREEITEM child = TreeView_GetChild(tree_, node);
    if (child) TreeView_DeleteItem(tree_, child);
    return;
  }

  ComPtr<IEnumIDList> en;
  if (FAILED(folder->EnumObjects(nullptr, SHCONTF_FOLDERS, en.put())) ||
      !en) {
    HTREEITEM child = TreeView_GetChild(tree_, node);
    if (child) TreeView_DeleteItem(tree_, child);
    return;
  }

  HTREEITEM dummy = TreeView_GetChild(tree_, node);
  if (dummy) {
    TVITEMW it{};
    it.mask = TVIF_PARAM;
    it.hItem = dummy;
    if (TreeView_GetItem(tree_, &it) && isDummyItem(it)) {
      TreeView_DeleteItem(tree_, dummy);
    }
  }

  LPITEMIDLIST relative = nullptr;
  ULONG fetched = 0;
  int inserted = 0;
  while (en->Next(1, &relative, &fetched) == S_OK && fetched == 1 &&
         inserted < kMaxChildrenPerExpand) {
    PidlOwner relOwner(relative);
    if (!isRealDirectory(folder.get(), relOwner.get())) continue;
    STRRET sr{};
    if (FAILED(folder->GetDisplayNameOf(relOwner.get(), SHGDN_NORMAL,
                                        &sr))) {
      continue;
    }
    std::wstring name = strRetToString(sr, relOwner.get());
    if (name.empty()) continue;
    LPITEMIDLIST absRaw = ILCombine(abs, relOwner.get());
    if (!absRaw) continue;
    insertNode(tree_, node, name, PidlOwner(absRaw), true);
    ++inserted;
  }
}

HTREEITEM AddressBarPopup::findChildByPath(HTREEITEM parent,
                                            const std::wstring& fsPath) {
  if (!tree_) return nullptr;
  const std::wstring target = normalisePath(fsPath);
  HTREEITEM child = TreeView_GetChild(tree_, parent);
  while (child) {
    LPCITEMIDLIST abs = itemPidl(tree_, child);
    if (abs) {
      std::wstring p = normalisePath(pidlToFsPath(abs));
      if (!p.empty() && pathsEqual(p, target)) {
        return child;
      }
    }
    child = TreeView_GetNextSibling(tree_, child);
  }
  return nullptr;
}

void AddressBarPopup::selectPath(const std::wstring& path) {
  if (!tree_ || path.empty()) return;
  if (path.size() < 2 || !iswalpha(path[0]) || path[1] != L':') return;

  PidlOwner computerAbs = knownFolderPidl(FOLDERID_ComputerFolder);
  if (!computerAbs) return;
  HTREEITEM computer = nullptr;
  for (HTREEITEM r = TreeView_GetRoot(tree_); r;
       r = TreeView_GetNextSibling(tree_, r)) {
    LPCITEMIDLIST abs = itemPidl(tree_, r);
    if (abs && ILIsEqual(abs, computerAbs.get())) {
      computer = r;
      break;
    }
  }
  if (!computer) return;

  TreeView_Expand(tree_, computer, TVE_EXPAND);

  std::wstring drive;
  drive.push_back(static_cast<wchar_t>(towupper(path[0])));
  drive += L":\\";
  HTREEITEM driveNode = findChildByPath(computer, drive);
  if (!driveNode) return;
  TreeView_Expand(tree_, driveNode, TVE_EXPAND);

  std::wstring remainder = path.substr(drive.size());
  std::wstring cumulative = drive;
  if (!cumulative.empty() && cumulative.back() == L'\\') cumulative.pop_back();
  HTREEITEM current = driveNode;
  size_t pos = 0;
  while (pos < remainder.size()) {
    size_t sep = remainder.find(L'\\', pos);
    size_t end = (sep == std::wstring::npos) ? remainder.size() : sep;
    std::wstring segment = remainder.substr(pos, end - pos);
    pos = (sep == std::wstring::npos) ? remainder.size() : end + 1;
    if (segment.empty()) continue;
    cumulative += L'\\';
    cumulative += segment;
    HTREEITEM child = findChildByPath(current, cumulative);
    if (!child) break;
    current = child;
    TreeView_Expand(tree_, current, TVE_EXPAND);
  }
  TreeView_SelectItem(tree_, current);
  TreeView_EnsureVisible(tree_, current);
}

void AddressBarPopup::commitSelection(HTREEITEM node) {
  if (!tree_ || !node) return;
  LPCITEMIDLIST abs = itemPidl(tree_, node);
  if (!abs) return;
  std::wstring path = pidlToFsPath(abs);
  if (path.empty()) return;
  auto payload = std::make_unique<std::wstring>(std::move(path));
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
        if (!self->containsScreenPoint(mouse->pt)) {
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
      if (tree_) TreeView_DeleteAllItems(tree_);
      return 0;
    }
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT AddressBarPopup::onTreeNotify(NMHDR* hdr) {
  if (!hdr || hdr->hwndFrom != tree_) return 0;
  switch (hdr->code) {
    case TVN_ITEMEXPANDINGW:
      onTreeExpanding(hdr);
      return 0;
    case TVN_DELETEITEMW:
      onTreeDeleteItem(hdr);
      return 0;
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

void AddressBarPopup::onTreeExpanding(NMHDR* hdr) {
  auto* nm = reinterpret_cast<NMTREEVIEWW*>(hdr);
  if (nm->action != TVE_EXPAND) return;
  HTREEITEM child = TreeView_GetChild(tree_, nm->itemNew.hItem);
  if (!child) return;
  TVITEMW it{};
  it.mask = TVIF_PARAM;
  it.hItem = child;
  if (TreeView_GetItem(tree_, &it) && isDummyItem(it)) {
    expandNode(nm->itemNew.hItem);
  }
}

void AddressBarPopup::onTreeDeleteItem(NMHDR* hdr) {
  auto* nm = reinterpret_cast<NMTREEVIEWW*>(hdr);
  if (isDummyItem(nm->itemOld)) return;
  if (auto* p = reinterpret_cast<LPITEMIDLIST>(nm->itemOld.lParam)) {
    CoTaskMemFree(p);
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
