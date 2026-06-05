#include "explorer/shell-tree-controller.h"

#include <knownfolders.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <uxtheme.h>

#include <array>
#include <cwctype>
#include <string_view>

#include "core/location.h"
#include "explorer/shell-pidl-location.h"
#include "winui_lite/chrome/com-raii.h"
#include "winui_lite/chrome/theme-watcher.h"

namespace fast_explorer::ui {

namespace {

constexpr int kMaxChildrenPerExpand = 512;
constexpr LPARAM kDummyLParam = static_cast<LPARAM>(-1);

constexpr std::wstring_view kGalleryClsid =
    L"::{e88865ea-0e1c-4e20-9aa6-edcd0212c87c}";
constexpr std::wstring_view kHomeClsid =
    L"::{f874310e-b6b7-47dc-bc84-b9e6b38f5903}";

struct RootSpec {
  const wchar_t* fallbackName;
  const KNOWNFOLDERID* knownFolder;
  std::wstring_view parseName;
};

const RootSpec kRootSpecs[] = {
    {L"Home", nullptr, kHomeClsid},
    {L"Gallery", nullptr, kGalleryClsid},
    {L"Desktop", &FOLDERID_Desktop, {}},
    {L"Downloads", &FOLDERID_Downloads, {}},
    {L"Documents", &FOLDERID_Documents, {}},
    {L"Pictures", &FOLDERID_Pictures, {}},
    {L"Music", &FOLDERID_Music, {}},
    {L"Videos", &FOLDERID_Videos, {}},
    {L"User profile", &FOLDERID_Profile, {}},
    {L"This PC", &FOLDERID_ComputerFolder, {}},
    {L"Network", &FOLDERID_NetworkFolder, {}},
    {L"Network Shortcuts", &FOLDERID_NetHood, {}},
    {L"Recycle Bin", &FOLDERID_RecycleBinFolder, {}},
};

std::wstring strRetToString(STRRET& sr, LPCITEMIDLIST relative) {
  wchar_t buf[MAX_PATH];
  if (FAILED(StrRetToBufW(&sr, relative, buf,
                          static_cast<UINT>(std::size(buf))))) {
    return {};
  }
  return std::wstring(buf);
}

std::wstring displayNameForPidl(LPCITEMIDLIST absolute,
                                std::wstring_view fallback) {
  if (absolute != nullptr) {
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetNameFromIDList(absolute, SIGDN_NORMALDISPLAY, &raw)) &&
        raw != nullptr) {
      std::wstring out(raw);
      CoTaskMemFree(raw);
      if (!out.empty()) return out;
    }
  }
  return std::wstring(fallback);
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

PidlOwner parsePidl(std::wstring_view parseName) noexcept {
  if (parseName.empty()) return {};
  PIDLIST_ABSOLUTE p = nullptr;
  SFGAOF attrs = 0;
  std::wstring text(parseName);
  if (FAILED(SHParseDisplayName(text.c_str(), nullptr, &p, 0, &attrs))) {
    return {};
  }
  return PidlOwner(p);
}

PidlOwner pidlForLocation(const std::wstring& location) noexcept {
  const auto parsed = fast_explorer::core::parseLocation(location);
  const std::wstring serialized =
      fast_explorer::core::serializeLocation(parsed);
  if (parsed.kind == fast_explorer::core::LocationKind::ShellKnownFolder) {
    switch (parsed.known) {
      case fast_explorer::core::KnownShellLocation::ThisPC:
        return knownFolderPidl(FOLDERID_ComputerFolder);
      case fast_explorer::core::KnownShellLocation::Network:
        return knownFolderPidl(FOLDERID_NetworkFolder);
      case fast_explorer::core::KnownShellLocation::NetworkShortcuts:
        return knownFolderPidl(FOLDERID_NetHood);
      case fast_explorer::core::KnownShellLocation::None:
        break;
    }
  }
  if (serialized == L"shell:RecycleBinFolder") {
    return knownFolderPidl(FOLDERID_RecycleBinFolder);
  }
  if (parsed.kind == fast_explorer::core::LocationKind::ShellNamespace) {
    return parsePidl(serialized);
  }
  if (!serialized.empty()) {
    PIDLIST_ABSOLUTE p = ILCreateFromPathW(serialized.c_str());
    return PidlOwner(p);
  }
  return {};
}

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

HTREEITEM insertNode(HWND tree, HTREEITEM parent, HTREEITEM insertAfter,
                     const std::wstring& name, PidlOwner abs,
                     bool hasChildren) {
  if (!tree || name.empty() || !abs) return nullptr;
  const int icon = systemIconIndexForPidl(abs.get());
  TVINSERTSTRUCTW ins{};
  ins.hParent = parent;
  ins.hInsertAfter = insertAfter;
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
  if (isDummyItem(it)) return nullptr;
  return reinterpret_cast<LPCITEMIDLIST>(it.lParam);
}

bool startsWithUnc(std::wstring_view text) noexcept {
  return text.size() >= 2 && text[0] == L'\\' && text[1] == L'\\';
}

}  // namespace

ShellTreeController::~ShellTreeController() {
  detach();
}

bool ShellTreeController::attach(HWND tree) {
  if (tree == nullptr) return false;
  detach();
  tree_ = tree;
  if (HIMAGELIST sys = systemSmallImageList()) {
    TreeView_SetImageList(tree_, sys, TVSIL_NORMAL);
  }
  applyTheme();
  return true;
}

void ShellTreeController::detach() noexcept {
  if (tree_ && IsWindow(tree_)) {
    TreeView_DeleteAllItems(tree_);
  }
  tree_ = nullptr;
  rootsLoaded_ = false;
  lastReflectedLocation_.clear();
}

void ShellTreeController::applyTheme() noexcept {
  if (tree_ == nullptr) return;
  const bool dark = isAppInDarkMode();
  SetWindowTheme(tree_, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
  if (dark) {
    const RowTheme theme = currentRowTheme();
    TreeView_SetBkColor(tree_, theme.background);
    TreeView_SetTextColor(tree_, theme.text);
  } else {
    TreeView_SetBkColor(tree_, static_cast<COLORREF>(-1));
    TreeView_SetTextColor(tree_, static_cast<COLORREF>(-1));
  }
  InvalidateRect(tree_, nullptr, TRUE);
}

void ShellTreeController::ensureRoots() {
  if (!tree_ || rootsLoaded_) return;
  populateRoots();
  rootsLoaded_ = true;
}

void ShellTreeController::resetRoots() {
  if (tree_) {
    TreeView_DeleteAllItems(tree_);
  }
  rootsLoaded_ = false;
  lastReflectedLocation_.clear();
}

void ShellTreeController::reflectLocation(const std::wstring& location) {
  if (!tree_ || location.empty()) return;
  ensureRoots();
  if (location == lastReflectedLocation_) return;

  const auto parsed = fast_explorer::core::parseLocation(location);
  if (parsed.kind == fast_explorer::core::LocationKind::FileSystemPath) {
    if (startsWithUnc(location)) {
      selectNetworkRoot();
    } else {
      selectPath(location);
    }
  } else {
    selectShellLocation(fast_explorer::core::serializeLocation(parsed));
  }
  lastReflectedLocation_ = location;
}

std::optional<ShellTreeSelection> ShellTreeController::selectionForItem(
    HTREEITEM item) const {
  if (!tree_ || !item) return std::nullopt;
  LPCITEMIDLIST abs = itemPidl(tree_, item);
  if (!abs) return std::nullopt;
  std::wstring location = addressLocationForPidl(abs);
  if (location.empty()) return std::nullopt;
  return ShellTreeSelection{std::move(location)};
}

LRESULT ShellTreeController::handleNotify(NMHDR* hdr) {
  if (!hdr || hdr->hwndFrom != tree_) return 0;
  switch (hdr->code) {
    case NM_CUSTOMDRAW:
      return onCustomDraw(hdr);
    case TVN_ITEMEXPANDINGW:
      onTreeExpanding(hdr);
      return 0;
    case TVN_DELETEITEMW:
      onTreeDeleteItem(hdr);
      return 0;
    default:
      return 0;
  }
}

void ShellTreeController::populateRoots() {
  if (!tree_) return;
  applyTheme();
  for (const RootSpec& root : kRootSpecs) {
    PidlOwner pidl = root.knownFolder != nullptr
                         ? knownFolderPidl(*root.knownFolder)
                         : parsePidl(root.parseName);
    if (!pidl) continue;
    if (findItemByPidl(pidl.get()) != nullptr) continue;
    std::wstring name = displayNameForPidl(pidl.get(), root.fallbackName);
    insertNode(tree_, TVI_ROOT, TVI_LAST, name, std::move(pidl),
               /*hasChildren*/ true);
  }

  PidlOwner computerAbs = knownFolderPidl(FOLDERID_ComputerFolder);
  if (computerAbs) {
    if (HTREEITEM computer = findItemByPidl(computerAbs.get())) {
      TreeView_Expand(tree_, computer, TVE_EXPAND);
    }
  }
}

void ShellTreeController::expandNode(HTREEITEM node) {
  if (!tree_ || !node) return;
  applyTheme();
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

  bool isNetworkExpansion = false;
  {
    PidlOwner netPidl = knownFolderPidl(FOLDERID_NetworkFolder);
    if (netPidl && ILIsEqual(abs, netPidl.get())) {
      isNetworkExpansion = true;
    }
  }

  ComPtr<IEnumIDList> en;
  const SHCONTF enumFlags = isNetworkExpansion
      ? static_cast<SHCONTF>(SHCONTF_FOLDERS | SHCONTF_NONFOLDERS |
                              SHCONTF_INCLUDEHIDDEN)
      : SHCONTF_FOLDERS;
  if (FAILED(folder->EnumObjects(nullptr, enumFlags, en.put())) || !en) {
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
    if (isNetworkExpansion) {
      LPCITEMIDLIST relRaw = relOwner.get();
      SFGAOF attrs = SFGAO_FOLDER;
      if (FAILED(folder->GetAttributesOf(1, &relRaw, &attrs))) continue;
      if ((attrs & SFGAO_FOLDER) == 0) continue;
    } else if (!isRealDirectory(folder.get(), relOwner.get())) {
      continue;
    }
    STRRET sr{};
    if (FAILED(folder->GetDisplayNameOf(relOwner.get(), SHGDN_NORMAL, &sr))) {
      continue;
    }
    std::wstring name = strRetToString(sr, relOwner.get());
    if (name.empty()) continue;
    LPITEMIDLIST absRaw = ILCombine(abs, relOwner.get());
    if (!absRaw) continue;
    insertNode(tree_, node, TVI_LAST, name, PidlOwner(absRaw),
               /*hasChildren*/ true);
    ++inserted;
  }
}

void ShellTreeController::onTreeExpanding(NMHDR* hdr) {
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

void ShellTreeController::onTreeDeleteItem(NMHDR* hdr) {
  auto* nm = reinterpret_cast<NMTREEVIEWW*>(hdr);
  if (isDummyItem(nm->itemOld)) return;
  if (auto* p = reinterpret_cast<LPITEMIDLIST>(nm->itemOld.lParam)) {
    CoTaskMemFree(p);
  }
}

LRESULT ShellTreeController::onCustomDraw(NMHDR* hdr) {
  auto* draw = reinterpret_cast<NMTVCUSTOMDRAW*>(hdr);
  switch (draw->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
      return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
      const RowTheme theme = currentRowTheme();
      draw->clrText = theme.text;
      draw->clrTextBk = theme.background;
      return CDRF_DODEFAULT;
    }
    default:
      return CDRF_DODEFAULT;
  }
}

HTREEITEM ShellTreeController::findChildByPath(
    HTREEITEM parent, const std::wstring& fsPath) {
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

HTREEITEM ShellTreeController::findItemByPidl(LPCITEMIDLIST target) const {
  if (!tree_ || !target) return nullptr;
  for (HTREEITEM r = TreeView_GetRoot(tree_); r;
       r = TreeView_GetNextSibling(tree_, r)) {
    LPCITEMIDLIST abs = itemPidl(tree_, r);
    if (abs && ILIsEqual(abs, target)) {
      return r;
    }
  }
  return nullptr;
}

void ShellTreeController::selectPath(const std::wstring& path) {
  if (!tree_ || path.empty()) return;
  if (path.size() < 2 || !iswalpha(path[0]) || path[1] != L':') return;

  PidlOwner computerAbs = knownFolderPidl(FOLDERID_ComputerFolder);
  if (!computerAbs) return;
  HTREEITEM computer = findItemByPidl(computerAbs.get());
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

void ShellTreeController::selectShellLocation(const std::wstring& location) {
  PidlOwner target = pidlForLocation(location);
  if (!target) return;
  HTREEITEM item = findItemByPidl(target.get());
  if (!item) return;
  TreeView_SelectItem(tree_, item);
  TreeView_EnsureVisible(tree_, item);
}

void ShellTreeController::selectNetworkRoot() {
  PidlOwner target = knownFolderPidl(FOLDERID_NetworkFolder);
  if (!target) return;
  HTREEITEM item = findItemByPidl(target.get());
  if (!item) return;
  TreeView_SelectItem(tree_, item);
  TreeView_EnsureVisible(tree_, item);
}

}  // namespace fast_explorer::ui
