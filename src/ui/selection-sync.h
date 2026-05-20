#pragma once

#include <windows.h>

namespace fast_explorer::ui {

class PaneController;

// Bridges list-view LVN_ITEMCHANGED notifications into PaneController's
// raw-index selection model, and reapplies LVIS_SELECTED from the pane
// after a sort reordering. Owns the reentrancy guard so the reapply's
// own SetItemState calls do not route back through handleItemChanged
// and clobber the freshly restored selection.
class SelectionSync {
 public:
  SelectionSync(HWND listView, PaneController& pane) noexcept;
  ~SelectionSync();

  SelectionSync(const SelectionSync&) = delete;
  SelectionSync& operator=(const SelectionSync&) = delete;
  SelectionSync(SelectionSync&&) = delete;
  SelectionSync& operator=(SelectionSync&&) = delete;

  void handleItemChanged(NMHDR* hdr);
  // Range deselect/select broadcast specific to LVS_OWNERDATA lists.
  // Routes the same as handleItemChanged internally — both delegate
  // to syncFromListView so we read the listview's actual current
  // state instead of trying to track deltas that comctl32 sometimes
  // drops on the floor (the cause of the "select count keeps growing
  // on single click" bug).
  void handleOdStateChanged(NMHDR* hdr);
  // Authoritative sync: clear the pane's selection set and rebuild it
  // by iterating LVNI_SELECTED rows on the list-view. Bulletproof
  // against missing / out-of-order notifications at the cost of
  // O(n) per selection change. Public so MainWindow's debounced
  // refresh can force a resync if it suspects drift.
  void syncFromListView();
  void reapplyFromPane();

 private:
  HWND listView_;
  PaneController& pane_;
  bool reapplying_ = false;
};

}  // namespace fast_explorer::ui
