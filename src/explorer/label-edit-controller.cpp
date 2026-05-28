#include "explorer/label-edit-controller.h"

#include <commctrl.h>

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "core/file-entry.h"
#include "core/file-model-store.h"
#include "explorer/folder-name.h"
#include "explorer/pane-controller.h"

namespace fast_explorer::ui {

LabelEditController::LabelEditController(HWND listView,
                                         PaneController& pane) noexcept
    : listView_(listView), pane_(pane) {}

LabelEditController::~LabelEditController() = default;

void LabelEditController::beginRenameFocused() {
  if (listView_ == nullptr || GetFocus() != listView_) {
    return;
  }
  const int focused = ListView_GetNextItem(listView_, -1, LVNI_FOCUSED);
  if (focused < 0) {
    return;
  }
  ListView_EditLabel(listView_, focused);
}

void LabelEditController::beginCreateSubfolder() {
  if (pane_.currentPath().empty()) {
    return;
  }
  const auto& store = pane_.store();
  const std::uint32_t count = store.publishedCount();
  std::vector<std::wstring_view> existing;
  existing.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto& entry = store.visibleAt(i);
    existing.emplace_back(entry.namePtr, entry.nameLength);
  }
  std::wstring leaf = uniqueFolderLeaf(existing, L"New folder");
  if (!pane_.createSubfolder(leaf)) {
    return;
  }
  pendingFolderName_ = std::move(leaf);
}

void LabelEditController::maybeStartPendingEdit() {
  if (pendingFolderName_.empty() || listView_ == nullptr) {
    return;
  }
  // Swap-then-clear so a delayed second onEnumComplete cannot
  // retrigger the auto-edit even if any step below throws.
  std::wstring target;
  target.swap(pendingFolderName_);
  const auto& store = pane_.store();
  const std::uint32_t count = store.publishedCount();
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto& entry = store.visibleAt(i);
    if (std::wstring_view(entry.namePtr, entry.nameLength) == target) {
      ListView_SetItemState(listView_, static_cast<int>(i),
                            LVIS_FOCUSED | LVIS_SELECTED,
                            LVIS_FOCUSED | LVIS_SELECTED);
      // ListView_EditLabel requires the list-view to have focus.
      SetFocus(listView_);
      ListView_EditLabel(listView_, static_cast<int>(i));
      return;
    }
  }
}

LRESULT LabelEditController::handleBeginEdit() {
  // The edit control is owned by the list-view and pre-filled via
  // LVN_GETDISPINFO with the current entry name.
  return FALSE;
}

LRESULT LabelEditController::handleEndEdit(NMHDR* hdr) {
  if (hdr == nullptr) {
    return FALSE;
  }
  auto* disp = reinterpret_cast<NMLVDISPINFOW*>(hdr);
  // pszText is null when the user cancels with Escape.
  if (disp->item.pszText == nullptr || disp->item.iItem < 0) {
    return FALSE;
  }
  const std::wstring newName(disp->item.pszText);
  if (newName.empty()) {
    return FALSE;
  }
  pane_.renameItem(static_cast<std::uint32_t>(disp->item.iItem), newName);
  // Always FALSE under LVS_OWNERDATA: the visible row refreshes once
  // the watcher observes the on-disk rename and re-enumerates.
  return FALSE;
}

}  // namespace fast_explorer::ui
