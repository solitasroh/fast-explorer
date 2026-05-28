#include "explorer/selection-sync.h"

#include <commctrl.h>

#include <cstddef>
#include <cstdint>
#include <new>
#include <span>

#include "core/file-model-store.h"
#include "explorer/pane-controller.h"

namespace fast_explorer::ui {

namespace {

// RAII guard for the reentrancy flag. The destructor must run before
// the catch handler in reapplyFromPane() so the flag clears even on
// std::bad_alloc — a C++ exception escaping wndProc is UB on Win32.
class ScopedFlag {
 public:
  explicit ScopedFlag(bool& flag) noexcept : flag_(flag) { flag_ = true; }
  ~ScopedFlag() noexcept { flag_ = false; }
  ScopedFlag(const ScopedFlag&) = delete;
  ScopedFlag& operator=(const ScopedFlag&) = delete;
  ScopedFlag(ScopedFlag&&) = delete;
  ScopedFlag& operator=(ScopedFlag&&) = delete;

 private:
  bool& flag_;
};

}  // namespace

SelectionSync::SelectionSync(HWND listView, PaneController& pane) noexcept
    : listView_(listView), pane_(pane) {}

SelectionSync::~SelectionSync() = default;

void SelectionSync::handleItemChanged(NMHDR* hdr) {
  if (hdr == nullptr || reapplying_) {
    return;
  }
  auto* nmlv = reinterpret_cast<NMLISTVIEW*>(hdr);
  if ((nmlv->uChanged & LVIF_STATE) == 0) {
    return;
  }
  const UINT oldSel = nmlv->uOldState & LVIS_SELECTED;
  const UINT newSel = nmlv->uNewState & LVIS_SELECTED;
  if (oldSel == newSel) {
    return;  // not a selection-state change
  }
  // Authoritative resync — see syncFromListView comment for why we
  // do not trust per-row deltas under LVS_OWNERDATA.
  syncFromListView();
}

void SelectionSync::handleOdStateChanged(NMHDR* hdr) {
  if (hdr == nullptr || reapplying_) {
    return;
  }
  auto* nm = reinterpret_cast<NMLVODSTATECHANGE*>(hdr);
  const UINT oldSel = nm->uOldState & LVIS_SELECTED;
  const UINT newSel = nm->uNewState & LVIS_SELECTED;
  if (oldSel == newSel) {
    return;
  }
  syncFromListView();
}

void SelectionSync::syncFromListView() {
  if (listView_ == nullptr || reapplying_) {
    return;
  }
  // Why a full resync instead of applying per-row deltas:
  // LVS_OWNERDATA list-views silently drop selection notifications
  // in some scenarios (deselect-on-click of the previously-selected
  // row in particular doesn't reliably reach either LVN_ITEMCHANGED
  // or LVN_ODSTATECHANGED on every Windows version). Tracking the
  // delta therefore leaks: selectedRaws_ grows on every click. By
  // reading ListView_GetNextItem(LVNI_SELECTED) we always end up
  // with the truth no matter which notifications fired.
  //
  // Cost: O(visible rows) per selection change. For a Ctrl+A on
  // 100k rows this is ~ms. Acceptable; the per-row delta version
  // was correct in theory but unreliable in practice.
  const auto& store = pane_.store();
  const std::span<const std::uint32_t> order = store.visibleOrder();
  const std::size_t published = store.publishedCount();
  try {
    pane_.clearSelection();
    int row = -1;
    while ((row = ListView_GetNextItem(listView_, row, LVNI_SELECTED)) !=
           -1) {
      const std::size_t r = static_cast<std::size_t>(row);
      if (r >= published || r >= order.size()) break;
      pane_.selectRaw(order[r]);
    }
  } catch (const std::bad_alloc&) {
    // Same recovery as the prior delta version — reapplyFromPane()
    // rebuilds list-view state on the next sort apply.
  }
}

void SelectionSync::reapplyFromPane() {
  if (listView_ == nullptr) {
    return;
  }
  ScopedFlag guard(reapplying_);
  // -1 broadcasts the state mask to every row, clearing LVIS_SELECTED
  // list-wide before we re-set it on the rows the pane reports as
  // selected.
  ListView_SetItemState(listView_, -1, 0, LVIS_SELECTED);
  try {
    for (int row : pane_.selectedRowsUnderCurrentOrder()) {
      ListView_SetItemState(listView_, row, LVIS_SELECTED, LVIS_SELECTED);
    }
  } catch (const std::bad_alloc&) {
    // The broadcast clear above already left the list-view in a
    // coherent "nothing selected" state.
  }
}

}  // namespace fast_explorer::ui
