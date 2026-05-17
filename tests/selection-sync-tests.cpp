#include <windows.h>
#include <commctrl.h>

#include <cstdint>

#include "bench-fs-helper.h"
#include "bench/dataset-generator.h"
#include "test-harness.h"
#include "ui/pane-controller.h"
#include "ui/selection-sync.h"

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

FE_TEST_CASE(SelectionSync_HandleItemChanged_SelectTransition_RoutesToPane) {
  TempDir tmp(L"selsync-select");
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, tmp.path(), 1).error,
               GenerateError::None);
  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();
  SelectionSync sync(nullptr, pane);

  const auto order = pane.store().visibleOrder();
  FE_ASSERT_TRUE(order.size() >= 2);
  NMLISTVIEW nm = makeItemChanged(0, 0, LVIS_SELECTED);
  sync.handleItemChanged(asHdr(nm));
  FE_ASSERT_EQ(pane.selectedCount(), static_cast<std::size_t>(1));
  FE_ASSERT_TRUE(pane.isRawSelected(order[0]));
}

FE_TEST_CASE(SelectionSync_HandleItemChanged_DeselectTransition_RoutesToPane) {
  TempDir tmp(L"selsync-deselect");
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, tmp.path(), 1).error,
               GenerateError::None);
  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();
  SelectionSync sync(nullptr, pane);

  const auto order = pane.store().visibleOrder();
  FE_ASSERT_TRUE(order.size() >= 1);
  pane.selectRaw(order[0]);
  FE_ASSERT_EQ(pane.selectedCount(), static_cast<std::size_t>(1));

  NMLISTVIEW nm = makeItemChanged(0, LVIS_SELECTED, 0);
  sync.handleItemChanged(asHdr(nm));
  FE_ASSERT_EQ(pane.selectedCount(), static_cast<std::size_t>(0));
  FE_ASSERT_FALSE(pane.isRawSelected(order[0]));
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
