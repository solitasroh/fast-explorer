#include "ui/clipboard-ops.h"

#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>

#include "ui/com-raii.h"

namespace fast_explorer::ui {

namespace {

ComPtr<IShellFolder> bindFolderByPath(const std::wstring& path) {
  ComPtr<IShellFolder> folder;
  if (path.empty()) return folder;
  LPITEMIDLIST raw = nullptr;
  SFGAOF attrs = 0;
  if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &raw, 0, &attrs)) ||
      raw == nullptr) {
    return folder;
  }
  PidlOwner pidl(raw);
  SHBindToObject(nullptr, pidl.get(), nullptr, IID_PPV_ARGS(folder.put()));
  return folder;
}

ComPtr<IDataObject> buildDataObject(
    IShellFolder* folder, const std::vector<std::wstring>& leaves) {
  ComPtr<IDataObject> data;
  if (folder == nullptr || leaves.empty()) return data;
  std::vector<PidlOwner> children;
  children.reserve(leaves.size());
  std::vector<LPCITEMIDLIST> raw;
  raw.reserve(leaves.size());
  for (const auto& leaf : leaves) {
    LPITEMIDLIST child = nullptr;
    ULONG eaten = 0;
    SFGAOF attrs = 0;
    if (FAILED(folder->ParseDisplayName(nullptr, nullptr,
                                         const_cast<LPWSTR>(leaf.c_str()),
                                         &eaten, &child, &attrs)) ||
        child == nullptr) {
      return data;
    }
    children.emplace_back(child);
    raw.push_back(children.back().get());
  }
  folder->GetUIObjectOf(nullptr, static_cast<UINT>(raw.size()), raw.data(),
                        IID_IDataObject, nullptr,
                        reinterpret_cast<void**>(data.put()));
  return data;
}

bool stampPreferredDropEffect(IDataObject* data, DWORD effect) {
  if (data == nullptr) return false;
  const UINT cf = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
  if (cf == 0) return false;
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
  if (mem == nullptr) return false;
  if (auto* p = static_cast<DWORD*>(GlobalLock(mem))) {
    *p = effect;
    GlobalUnlock(mem);
  } else {
    GlobalFree(mem);
    return false;
  }
  FORMATETC fe{};
  fe.cfFormat = static_cast<CLIPFORMAT>(cf);
  fe.dwAspect = DVASPECT_CONTENT;
  fe.lindex = -1;
  fe.tymed = TYMED_HGLOBAL;
  STGMEDIUM sm{};
  sm.tymed = TYMED_HGLOBAL;
  sm.hGlobal = mem;
  if (FAILED(data->SetData(&fe, &sm, TRUE))) {
    // SetData failed; the medium must be released by us.
    GlobalFree(mem);
    return false;
  }
  return true;
}

ComPtr<IDropTarget> queryFolderDropTarget(const std::wstring& path,
                                          HWND ownerHwnd) {
  ComPtr<IDropTarget> target;
  ComPtr<IShellFolder> folder = bindFolderByPath(path);
  if (!folder) return target;
  folder->CreateViewObject(ownerHwnd, IID_IDropTarget,
                           reinterpret_cast<void**>(target.put()));
  return target;
}

}  // namespace

bool ClipboardOps::copy(const std::wstring& folderPath,
                        const std::vector<std::wstring>& selectedLeaves,
                        bool cut) {
  ComPtr<IShellFolder> folder = bindFolderByPath(folderPath);
  if (!folder) return false;
  ComPtr<IDataObject> data = buildDataObject(folder.get(), selectedLeaves);
  if (!data) return false;
  stampPreferredDropEffect(data.get(),
                           cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY);
  if (FAILED(OleSetClipboard(data.get()))) {
    return false;
  }
  // Survive process exit so the user can paste into Explorer later.
  OleFlushClipboard();
  return true;
}

bool ClipboardOps::paste(const std::wstring& folderPath, HWND ownerHwnd) {
  if (folderPath.empty()) return false;
  ComPtr<IDataObject> data;
  if (FAILED(OleGetClipboard(data.put())) || !data) return false;
  ComPtr<IDropTarget> target = queryFolderDropTarget(folderPath, ownerHwnd);
  if (!target) return false;
  POINTL pt{0, 0};
  DWORD effect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
  if (FAILED(target->DragEnter(data.get(), MK_LBUTTON, pt, &effect))) {
    return false;
  }
  if (effect == DROPEFFECT_NONE) {
    target->DragLeave();
    return false;
  }
  effect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
  target->DragOver(MK_LBUTTON, pt, &effect);
  if (effect == DROPEFFECT_NONE) {
    target->DragLeave();
    return false;
  }
  return SUCCEEDED(target->Drop(data.get(), MK_LBUTTON, pt, &effect));
}

}  // namespace fast_explorer::ui
