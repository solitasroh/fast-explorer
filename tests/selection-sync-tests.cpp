#include <windows.h>
#include <commctrl.h>

#include <cstdint>

#include "bench-fs-helper.h"
#include "bench/dataset-generator.h"
#include "test-harness.h"
#include "explorer/pane-controller.h"
#include "explorer/selection-sync.h"

using fast_explorer::bench::generateDataset;
using fast_explorer::bench::GenerateError;
using fast_explorer::bench::PresetKind;
using fast_explorer::tests::TempDir;
using fast_explorer::ui::PaneController;
using fast_explorer::ui::SelectionSync;

namespace {

NMLISTVIEW makeItemChanged(int iItem, UINT oldState, UINT newState) {
  NMLISTVIEW nm{};
  nm.iItem = iItem;
  nm.iSubItem = 0;
  nm.uChanged = LVIF_STATE;
  nm.uOldState = oldState;
  nm.uNewState = newState;
  return nm;
}

NMHDR* asHdr(NMLISTVIEW& nm) {
  return reinterpret_cast<NMHDR*>(&nm);
}

}  // namespace

FE_TEST_CASE(SelectionSync_HandleItemChanged_NullHdr_NoOp) {
  PaneController pane(nullptr);
  SelectionSync sync(nullptr, pane);
  sync.handleItemChanged(nullptr);
  FE_ASSERT_EQ(pane.selectedCount(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(SelectionSync_HandleItemChanged_NotStateChange_NoOp) {
  PaneController pane(nullptr);
  SelectionSync sync(nullptr, pane);
  NMLISTVIEW nm = makeItemChanged(0, 0, LVIS_SELECTED);
  nm.uChanged = 0;
  sync.handleItemChanged(asHdr(nm));
  FE_ASSERT_EQ(pane.selectedCount(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(SelectionSync_HandleItemChanged_ListWideBroadcast_NoOp) {
  PaneController pane(nullptr);
  SelectionSync sync(nullptr, pane);
  NMLISTVIEW nm = makeItemChanged(-1, 0, LVIS_SELECTED);
  sync.handleItemChanged(asHdr(nm));
  FE_ASSERT_EQ(pane.selectedCount(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(SelectionSync_HandleItemChanged_RowBeyondPublished_NoOp) {
  PaneController pane(nullptr);
  SelectionSync sync(nullptr, pane);
  NMLISTVIEW nm = makeItemChanged(0, 0, LVIS_SELECTED);
  sync.handleItemChanged(asHdr(nm));
  FE_ASSERT_EQ(pane.selectedCount(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(SelectionSync_HandleItemChanged_NoStateTransition_NoOp) {
  TempDir tmp(L"selsync-noop");
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, tmp.path(), 1).error,
               GenerateError::None);
  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();
  SelectionSync sync(nullptr, pane);

  NMLISTVIEW nm = makeItemChanged(0, LVIS_SELECTED, LVIS_SELECTED);
  sync.handleItemChanged(asHdr(nm));
  FE_ASSERT_EQ(pane.selectedCount(), static_cast<std::size_t>(0));
}

// The select/deselect transition tests that used to live here
// asserted delta-based routing: NMLISTVIEW says "row 0 selected" →
// PaneController.selectedCount becomes 1. That contract changed in
// the resync rewrite — handleItemChanged now delegates to
// syncFromListView, which reads ListView_GetNextItem(LVNI_SELECTED)
// from the live list-view. Without a real HWND to query (the unit
// tests use listView=nullptr) syncFromListView correctly bails, so
// the old assertions can't be re-expressed here. The new routing
// is covered by integration testing only.
FE_TEST_CASE(SelectionSync_HandleItemChanged_NullListView_NoOp) {
  TempDir tmp(L"selsync-null-lv");
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, tmp.path(), 1).error,
               GenerateError::None);
  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();
  SelectionSync sync(nullptr, pane);

  // syncFromListView must not crash and must not mutate selection
  // when the list-view handle is null (e.g. during teardown).
  NMLISTVIEW nm = makeItemChanged(0, 0, LVIS_SELECTED);
  sync.handleItemChanged(asHdr(nm));
  FE_ASSERT_EQ(pane.selectedCount(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(SelectionSync_HandleItemChanged_StateChangeFiltering) {
  // No-state-change notifications still short-circuit before any
  // listview query — the previous regression that this guards
  // against was firing the resync for non-LVIF_STATE updates and
  // doing unnecessary work on every focus / image change.
  TempDir tmp(L"selsync-noop");
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, tmp.path(), 1).error,
               GenerateError::None);
  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();
  SelectionSync sync(nullptr, pane);
  pane.selectRaw(0);

  // newState == oldState → no transition, syncFromListView not called.
  NMLISTVIEW nm = makeItemChanged(0, LVIS_SELECTED, LVIS_SELECTED);
  sync.handleItemChanged(asHdr(nm));
  // Selection survives because we early-returned before touching it.
  FE_ASSERT_EQ(pane.selectedCount(), static_cast<std::size_t>(1));
}

FE_TEST_CASE(SelectionSync_ReapplyFromPane_NullListView_NoCrash) {
  // The non-null-listView path (broadcast clear + per-row SetItemState
  // loop + ScopedFlag reentrancy guard) requires a real list-view HWND
  // and is exercised through the integration path in MainWindow's
  // finalizeSortApply() after a sort reorder. Direct headless coverage
  // would need a UI automation harness, tracked as a follow-up.
  PaneController pane(nullptr);
  SelectionSync sync(nullptr, pane);
  pane.selectRaw(0);
  sync.reapplyFromPane();
  FE_ASSERT_EQ(pane.selectedCount(), static_cast<std::size_t>(1));
}
