#include "explorer/shell-bind.h"

#include <shellapi.h>

namespace fast_explorer::ui {

ComPtr<IShellFolder> bindFolderByPath(const std::wstring& path) {
  ComPtr<IShellFolder> folder;
  if (path.empty()) return folder;
  LPITEMIDLIST raw = nullptr;
  if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &raw, 0, nullptr)) ||
      raw == nullptr) {
    return folder;
  }
  PidlOwner pidl(raw);
  SHBindToObject(nullptr, pidl.get(), nullptr, IID_PPV_ARGS(folder.put()));
  return folder;
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

}  // namespace fast_explorer::ui
