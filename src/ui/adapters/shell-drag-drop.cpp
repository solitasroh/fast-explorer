#include "ui/adapters/shell-drag-drop.h"

#include <shobjidl.h>

#include "core/file-entry.h"
#include "core/file-model-store.h"
#include "winui_lite/chrome/com-raii.h"
#include "ui/drop-source.h"
#include "ui/pane-controller.h"
#include "ui/shell-bind.h"

namespace fast_explorer::ui::adapters {

namespace {

std::vector<std::wstring> resolveLeaves(
    const PaneController& pane,
    const std::vector<ports::ItemId>& ids) {
  std::vector<std::wstring> out;
  out.reserve(ids.size());
  const auto& store = pane.store();
  const auto bound = store.publishedCount();
  for (ports::ItemId id : ids) {
    if (id == ports::kInvalidItemId) continue;
    const std::size_t row = static_cast<std::size_t>(id - 1);
    if (row >= bound) continue;
    const auto& entry = store.visibleAt(row);
    if (entry.namePtr == nullptr || entry.nameLength == 0) continue;
    out.emplace_back(entry.namePtr, entry.nameLength);
  }
  return out;
}

}  // namespace

ShellDragDrop::ShellDragDrop(const PaneController& pane,
                              HWND listView) noexcept
    : pane_(&pane), listView_(listView) {}

bool ShellDragDrop::beginDrag(const std::vector<ports::ItemId>& ids) {
  if (listView_ == nullptr) return false;
  const std::wstring& folderPath = pane_->currentPath();
  if (folderPath.empty()) return false;
  const auto leaves = resolveLeaves(*pane_, ids);
  if (leaves.empty()) return false;

  ComPtr<IShellFolder> folder = bindFolderByPath(folderPath);
  if (!folder) return false;
  std::vector<PidlOwner> childPidls;
  childPidls.reserve(leaves.size());
  std::vector<LPCITEMIDLIST> rawPidls;
  rawPidls.reserve(leaves.size());
  for (const auto& leaf : leaves) {
    LPITEMIDLIST child = nullptr;
    ULONG eaten = 0;
    SFGAOF attrs = 0;
    if (FAILED(folder->ParseDisplayName(nullptr, nullptr,
                                         const_cast<LPWSTR>(leaf.c_str()),
                                         &eaten, &child, &attrs)) ||
        child == nullptr) {
      return false;
    }
    childPidls.emplace_back(child);
    rawPidls.push_back(childPidls.back().get());
  }

  ComPtr<IDataObject> dataObj;
  if (FAILED(folder->GetUIObjectOf(listView_,
                                    static_cast<UINT>(rawPidls.size()),
                                    rawPidls.data(), IID_IDataObject,
                                    nullptr,
                                    reinterpret_cast<void**>(dataObj.put()))) ||
      !dataObj) {
    return false;
  }

  ComPtr<IDropSource> dropSource;
  dropSource.attach(new (std::nothrow) FileDropSource());
  if (!dropSource) return false;
  DWORD effect = 0;
  DoDragDrop(dataObj.get(), dropSource.get(),
             DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK,
             &effect);
  return true;
}

}  // namespace fast_explorer::ui::adapters
