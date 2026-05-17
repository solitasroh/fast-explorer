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
  void reapplyFromPane();

 private:
  HWND listView_;
  PaneController& pane_;
  bool reapplying_ = false;
};

}  // namespace fast_explorer::ui
