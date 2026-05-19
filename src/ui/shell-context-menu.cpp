#include "ui/shell-context-menu.h"

#include <commctrl.h>
#include <shlobj.h>

#include "ui/com-raii.h"

namespace fast_explorer::ui {

namespace {

constexpr UINT kCmdIdMin = 1;
// Upper bound of the shell verb id range passed to QueryContextMenu.
// Verb ids must fit in a 16-bit unsigned to round-trip through
// TrackPopupMenuEx (which returns the cmd as UINT but shell verbs
// are conventionally <= 0x7FFF).
constexpr UINT kCmdIdMax = 0x7FFFu;
constexpr UINT_PTR kSubclassId = 0x46435853u;

struct MenuForwardData {
  IContextMenu2* cm2 = nullptr;
  IContextMenu3* cm3 = nullptr;
};

LRESULT CALLBACK ownerSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                   UINT_PTR /*idSubclass*/, DWORD_PTR refData) {
  auto* data = reinterpret_cast<MenuForwardData*>(refData);
  if (data == nullptr) return DefSubclassProc(hwnd, msg, wp, lp);
  switch (msg) {
    case WM_MENUCHAR:
      if (data->cm3) {
        LRESULT result = 0;
        if (SUCCEEDED(data->cm3->HandleMenuMsg2(msg, wp, lp, &result))) {
          return result;
        }
      }
      break;
    case WM_INITMENUPOPUP:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
      if (data->cm3) {
        LRESULT result = 0;
        if (SUCCEEDED(data->cm3->HandleMenuMsg2(msg, wp, lp, &result))) {
          return (msg == WM_INITMENUPOPUP) ? 0 : TRUE;
        }
      } else if (data->cm2) {
        if (SUCCEEDED(data->cm2->HandleMenuMsg(msg, wp, lp))) {
          return (msg == WM_INITMENUPOPUP) ? 0 : TRUE;
        }
      }
      break;
    default:
      break;
  }
  return DefSubclassProc(hwnd, msg, wp, lp);
}

class SubclassGuard {
 public:
  SubclassGuard(HWND hwnd, SUBCLASSPROC proc, UINT_PTR id, DWORD_PTR data)
      : hwnd_(hwnd), proc_(proc), id_(id),
        installed_(SetWindowSubclass(hwnd, proc, id, data)) {}
  ~SubclassGuard() {
    if (installed_) RemoveWindowSubclass(hwnd_, proc_, id_);
  }
  SubclassGuard(const SubclassGuard&) = delete;
  SubclassGuard& operator=(const SubclassGuard&) = delete;

 private:
  HWND hwnd_;
  SUBCLASSPROC proc_;
  UINT_PTR id_;
  BOOL installed_;
};

PidlOwner parseAbsolute(const std::wstring& path) {
  LPITEMIDLIST pidl = nullptr;
  SFGAOF attrs = 0;
  if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, &attrs))) {
    return {};
  }
  return PidlOwner(pidl);
}

ComPtr<IShellFolder> bindFolder(LPCITEMIDLIST folderPidl) {
  ComPtr<IShellFolder> folder;
  if (folderPidl == nullptr) return folder;
  SHBindToObject(nullptr, folderPidl, nullptr, IID_PPV_ARGS(folder.put()));
  return folder;
}

std::vector<PidlOwner> parseChildren(
    IShellFolder* folder, const std::vector<std::wstring>& leaves) {
  std::vector<PidlOwner> out;
  out.reserve(leaves.size());
  for (const auto& leaf : leaves) {
    LPITEMIDLIST child = nullptr;
    ULONG eaten = 0;
    SFGAOF attrs = 0;
    if (FAILED(folder->ParseDisplayName(nullptr, nullptr,
                                        const_cast<LPWSTR>(leaf.c_str()),
                                        &eaten, &child, &attrs)) ||
        child == nullptr) {
      return {};
    }
    out.emplace_back(child);
  }
  return out;
}

ComPtr<IContextMenu> queryContextMenuFromChildren(
    IShellFolder* folder, HWND ownerHwnd,
    const std::vector<PidlOwner>& children) {
  ComPtr<IContextMenu> menu;
  if (children.empty()) return menu;
  std::vector<LPCITEMIDLIST> raw;
  raw.reserve(children.size());
  for (const auto& c : children) raw.push_back(c.get());
  folder->GetUIObjectOf(ownerHwnd, static_cast<UINT>(raw.size()), raw.data(),
                        IID_IContextMenu, nullptr,
                        reinterpret_cast<void**>(menu.put()));
  return menu;
}

ComPtr<IContextMenu> queryBackgroundMenu(IShellFolder* folder, HWND ownerHwnd) {
  ComPtr<IContextMenu> menu;
  folder->CreateViewObject(ownerHwnd, IID_IContextMenu,
                           reinterpret_cast<void**>(menu.put()));
  return menu;
}

// Verbs whose shell extensions ignore the multi-PIDL bundle inside
// a single InvokeCommand call and only act on the first item. For
// these we fan the invocation out per file the way Explorer does.
// Compared case-insensitively against the verb-name string the
// extension reports via GetCommandString(GCS_VERBA). Lower-case
// because shell verbs are conventionally lower-case but defensive
// against an extension that reports a capitalized canonical name.
bool isPerItemFanOutVerb(const char* verbA) noexcept {
  if (verbA == nullptr || verbA[0] == '\0') return false;
  static constexpr const char* kPerItemVerbs[] = {
      "install",          // Fontext.dll font installer
      "installallusers",  // per-machine font install
      "print",            // batch print (per-document fan-out)
      "printto",          // print to specific printer
  };
  for (const char* v : kPerItemVerbs) {
    if (_stricmp(verbA, v) == 0) return true;
  }
  return false;
}

void invokeIdCommand(IContextMenu* menu, UINT cmdId, HWND ownerHwnd,
                     POINT screenPt, const std::wstring& folderPathW) {
  if (cmdId < kCmdIdMin || cmdId > kCmdIdMax) return;
  CMINVOKECOMMANDINFOEX info{};
  info.cbSize = sizeof(info);
  info.fMask = CMIC_MASK_PTINVOKE;
  info.hwnd = ownerHwnd;
  // ID verbs go only in the ANSI field. Some shell extensions
  // dereference lpVerbW as a string when it is non-null even with
  // IS_INTRESOURCE, which crashes for MAKEINTRESOURCE ids.
  const UINT verbId = cmdId - kCmdIdMin;
  info.lpVerb = MAKEINTRESOURCEA(verbId);
  info.lpDirectoryW = folderPathW.c_str();
  info.nShow = SW_SHOWNORMAL;
  info.ptInvoke = screenPt;
  menu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info));
}

// Fan-out: build a fresh single-PIDL IContextMenu per file and
// invoke the verb by name. Each iteration runs synchronously
// (no CMIC_MASK_ASYNCOK) so the next install does not race the
// previous one's shell-extension Initialize() call.
void invokeVerbPerItem(IShellFolder* folder, HWND ownerHwnd,
                       const std::vector<std::wstring>& leaves,
                       const char* verbA,
                       const std::wstring& folderPathW) {
  for (const auto& leaf : leaves) {
    auto child = parseChildren(folder, {leaf});
    if (child.empty()) continue;
    auto singleMenu = queryContextMenuFromChildren(folder, ownerHwnd, child);
    if (!singleMenu) continue;
    CMINVOKECOMMANDINFOEX info{};
    info.cbSize = sizeof(info);
    info.hwnd = ownerHwnd;
    info.lpVerb = verbA;  // canonical verb name (e.g. "install")
    info.lpDirectoryW = folderPathW.c_str();
    info.nShow = SW_SHOWNORMAL;
    singleMenu->InvokeCommand(
        reinterpret_cast<CMINVOKECOMMANDINFO*>(&info));
  }
}

void runMenuAndInvoke(IContextMenu* menu, IShellFolder* folder,
                      HWND ownerHwnd, POINT screenPt,
                      const std::vector<std::wstring>& selectedLeaves,
                      const std::wstring& folderPathW) {
  MenuOwner popup(CreatePopupMenu());
  if (!popup) return;
  if (FAILED(menu->QueryContextMenu(popup.get(), 0, kCmdIdMin, kCmdIdMax,
                                    CMF_NORMAL | CMF_CANRENAME |
                                        CMF_EXPLORE))) {
    return;
  }
  MenuForwardData forward{};
  // Prefer IContextMenu3 (Unicode result for WM_MENUCHAR); fall back
  // to IContextMenu2. Both interfaces are owned by the same shell
  // object as `menu`, so we hold one extra ref via QueryInterface.
  if (FAILED(menu->QueryInterface(IID_PPV_ARGS(&forward.cm3)))) {
    menu->QueryInterface(IID_PPV_ARGS(&forward.cm2));
  }
  {
    SubclassGuard guard(ownerHwnd, ownerSubclassProc, kSubclassId,
                        reinterpret_cast<DWORD_PTR>(&forward));
    const UINT cmd = TrackPopupMenuEx(
        popup.get(),
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenPt.x, screenPt.y, ownerHwnd, nullptr);
    if (cmd != 0) {
      // Subclass must remain installed across InvokeCommand because
      // shell verbs (Open With, Share, ...) pump nested menus that
      // emit WM_INITMENUPOPUP / WM_DRAWITEM / WM_MEASUREITEM.
      //
      // Verb-name probe: some extensions (Fontext "install",
      // PrintTo, ...) ignore the multi-PIDL bundle inside a single
      // InvokeCommand and process only the first PIDL. We mirror
      // Explorer's workaround for those verbs by re-querying a
      // single-PIDL context menu per file and invoking by name.
      char verbA[64] = {0};
      const UINT verbId = (cmd >= kCmdIdMin) ? cmd - kCmdIdMin : 0;
      const bool gotVerb =
          cmd >= kCmdIdMin &&
          SUCCEEDED(menu->GetCommandString(verbId, GCS_VERBA, nullptr,
                                           verbA, sizeof(verbA)));
      if (gotVerb && selectedLeaves.size() > 1 &&
          isPerItemFanOutVerb(verbA) && folder != nullptr) {
        invokeVerbPerItem(folder, ownerHwnd, selectedLeaves, verbA,
                          folderPathW);
      } else {
        invokeIdCommand(menu, cmd, ownerHwnd, screenPt, folderPathW);
      }
    }
  }
  if (forward.cm3) forward.cm3->Release();
  if (forward.cm2) forward.cm2->Release();
}

}  // namespace

void ShellContextMenu::show(HWND ownerHwnd, const std::wstring& folderPath,
                            const std::vector<std::wstring>& selectedLeaves,
                            POINT screenPt) {
  if (ownerHwnd == nullptr || folderPath.empty()) return;

  PidlOwner folderPidl = parseAbsolute(folderPath);
  if (!folderPidl) return;

  ComPtr<IShellFolder> folder = bindFolder(folderPidl.get());
  if (!folder) return;

  ComPtr<IContextMenu> menu;
  if (selectedLeaves.empty()) {
    menu = queryBackgroundMenu(folder.get(), ownerHwnd);
  } else {
    auto children = parseChildren(folder.get(), selectedLeaves);
    if (children.empty()) return;
    menu = queryContextMenuFromChildren(folder.get(), ownerHwnd, children);
  }
  if (!menu) return;

  runMenuAndInvoke(menu.get(), folder.get(), ownerHwnd, screenPt,
                   selectedLeaves, folderPath);
}

}  // namespace fast_explorer::ui
