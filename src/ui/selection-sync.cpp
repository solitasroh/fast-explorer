#include "ui/selection-sync.h"

#include <commctrl.h>

#include <cstddef>
#include <cstdint>
#include <new>
#include <span>

#include "core/file-model-store.h"
#include "ui/pane-controller.h"

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
  if (nmlv->iItem < 0) {
    // -1 is the LVS_OWNERDATA list-wide broadcast; reapplyFromPane()
    // re-derives selection on the next sort apply.
    return;
  }
  const auto row = static_cast<std::size_t>(nmlv->iItem);
  const auto& store = pane_.store();
  if (row >= store.publishedCount()) {
    return;
  }
  const std::span<const std::uint32_t> order = store.visibleOrder();
  if (row >= order.size()) {
    return;
  }
  const std::uint32_t raw = order[row];
  const bool wasSelected = (nmlv->uOldState & LVIS_SELECTED) != 0;
  const bool isSelected = (nmlv->uNewState & LVIS_SELECTED) != 0;
  if (wasSelected == isSelected) {
    return;
  }
  // selectRaw / deselectRaw use unordered_set and can throw bad_alloc;
  // we swallow the sync miss because reapplyFromPane() rebuilds
  // list-view state from the pane model on the next sort apply.
  try {
    if (isSelected) {
      pane_.selectRaw(raw);
    } else {
      pane_.deselectRaw(raw);
    }
  } catch (const std::bad_alloc&) {
  }
}

void SelectionSync::handleOdStateChanged(NMHDR* hdr) {
  if (hdr == nullptr || reapplying_) {
    return;
  }
  auto* nm = reinterpret_cast<NMLVODSTATECHANGE*>(hdr);
  // Walk [iFrom..iTo] (inclusive on both ends per common-controls
  // docs) and reconcile each row's LVIS_SELECTED against the pane
  // set. Without this loop, single-click deselections of previously-
  // selected rows under LVS_OWNERDATA never reach PaneController,
  // so selectedRaws_ grows on every click.
  const int from = nm->iFrom;
  const int to = nm->iTo;
  if (from < 0 || to < from) {
    return;
  }
  const bool wasSelected = (nm->uOldState & LVIS_SELECTED) != 0;
  const bool isSelected = (nm->uNewState & LVIS_SELECTED) != 0;
  if (wasSelected == isSelected) {
    return;
  }
  const auto& store = pane_.store();
  const std::span<const std::uint32_t> order = store.visibleOrder();
  const std::size_t published = store.publishedCount();
  const std::size_t high = static_cast<std::size_t>(to);
  if (high >= published || high >= order.size()) {
    return;
  }
  try {
    for (std::size_t row = static_cast<std::size_t>(from);
         row <= high; ++row) {
      const std::uint32_t raw = order[row];
      if (isSelected) {
        pane_.selectRaw(raw);
      } else {
        pane_.deselectRaw(raw);
      }
    }
  } catch (const std::bad_alloc&) {
    // Same recovery as handleItemChanged — reapplyFromPane() rebuilds
    // list-view state on the next sort apply.
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
