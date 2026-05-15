#include <unordered_set>

#include "bench-fs-helper.h"
#include "bench/dataset-generator.h"
#include "core/file-sort.h"
#include "test-harness.h"
#include "ui/pane-controller.h"

using fast_explorer::bench::generateDataset;
using fast_explorer::bench::GenerateError;
using fast_explorer::bench::PresetKind;
using fast_explorer::core::SortDirection;
using fast_explorer::core::SortKey;
using fast_explorer::tests::TempDir;
using fast_explorer::ui::PaneController;
using fast_explorer::ui::SortDispatch;

constexpr uint64_t kSmallPresetExpectedItems = 200;

FE_TEST_CASE(PaneController_Default_HasZeroGenerationAndEmptyPath) {
  PaneController pc(nullptr);
  FE_ASSERT_EQ(pc.generation(), 0u);
  FE_ASSERT_TRUE(pc.currentPath().empty());
  FE_ASSERT_EQ(pc.hostWindow(), static_cast<HWND>(nullptr));
}

FE_TEST_CASE(PaneController_OpenFolder_ValidPath_AcceptsAndBumpsGeneration) {
  PaneController pc(nullptr);
  const uint32_t before = pc.generation();
  FE_ASSERT_TRUE(pc.openFolder(L"C:\\tmp"));
  FE_ASSERT_WSTREQ(pc.currentPath(), L"C:\\tmp");
  FE_ASSERT_TRUE(pc.generation() != before);
}

FE_TEST_CASE(PaneController_OpenFolder_EmptyPath_Rejected) {
  PaneController pc(nullptr);
  FE_ASSERT_TRUE(pc.openFolder(L"C:\\initial"));
  const uint32_t before = pc.generation();
  const std::wstring beforePath = pc.currentPath();
  FE_ASSERT_FALSE(pc.openFolder(L""));
  FE_ASSERT_EQ(pc.generation(), before);
  FE_ASSERT_WSTREQ(pc.currentPath(), beforePath);
}

FE_TEST_CASE(PaneController_OpenFolder_RelativePath_Rejected) {
  PaneController pc(nullptr);
  FE_ASSERT_FALSE(pc.openFolder(L"some\\relative\\path"));
  FE_ASSERT_TRUE(pc.currentPath().empty());
}

FE_TEST_CASE(PaneController_OpenFolder_UncPath_Rejected) {
  PaneController pc(nullptr);
  FE_ASSERT_FALSE(pc.openFolder(L"\\\\server\\share"));
  FE_ASSERT_TRUE(pc.currentPath().empty());
}

FE_TEST_CASE(PaneController_OpenFolder_Twice_BumpsGenerationEachTime) {
  PaneController pc(nullptr);
  FE_ASSERT_TRUE(pc.openFolder(L"C:\\a"));
  const uint32_t g1 = pc.generation();
  FE_ASSERT_TRUE(pc.openFolder(L"C:\\b"));
  const uint32_t g2 = pc.generation();
  FE_ASSERT_TRUE(g2 != g1);
  FE_ASSERT_WSTREQ(pc.currentPath(), L"C:\\b");
}

FE_TEST_CASE(PaneController_OpenFolder_DrivesEnumerationOnRealFs) {
  TempDir tmp(L"pane-open");
  const auto gen = generateDataset(PresetKind::Small, tmp.path(), 1);
  FE_ASSERT_EQ(gen.error, GenerateError::None);

  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();
  FE_ASSERT_EQ(pane.store().itemCount(), kSmallPresetExpectedItems);
}

FE_TEST_CASE(PaneController_Back_NoHistory_ReturnsFalse) {
  PaneController pc(nullptr);
  FE_ASSERT_FALSE(pc.back());
  FE_ASSERT_FALSE(pc.canGoBack());
}

FE_TEST_CASE(PaneController_Forward_NoHistory_ReturnsFalse) {
  PaneController pc(nullptr);
  FE_ASSERT_FALSE(pc.forward());
  FE_ASSERT_FALSE(pc.canGoForward());
}

FE_TEST_CASE(PaneController_OpenThenBack_RestoresPrior) {
  PaneController pc(nullptr);
  FE_ASSERT_TRUE(pc.openFolder(L"C:\\a"));
  FE_ASSERT_TRUE(pc.openFolder(L"C:\\b"));
  FE_ASSERT_TRUE(pc.canGoBack());
  FE_ASSERT_TRUE(pc.back());
  FE_ASSERT_WSTREQ(pc.currentPath(), L"C:\\a");
  FE_ASSERT_TRUE(pc.canGoForward());
  FE_ASSERT_FALSE(pc.canGoBack());
}

FE_TEST_CASE(PaneController_BackThenForward_RestoresLatest) {
  PaneController pc(nullptr);
  pc.openFolder(L"C:\\a");
  pc.openFolder(L"C:\\b");
  pc.back();
  FE_ASSERT_TRUE(pc.forward());
  FE_ASSERT_WSTREQ(pc.currentPath(), L"C:\\b");
  FE_ASSERT_FALSE(pc.canGoForward());
}

FE_TEST_CASE(PaneController_OpenAfterBack_ClearsForward) {
  PaneController pc(nullptr);
  pc.openFolder(L"C:\\a");
  pc.openFolder(L"C:\\b");
  pc.back();
  FE_ASSERT_TRUE(pc.canGoForward());
  FE_ASSERT_TRUE(pc.openFolder(L"C:\\c"));
  FE_ASSERT_FALSE(pc.canGoForward());
}

FE_TEST_CASE(PaneController_Up_FromFolder_ReturnsParent) {
  PaneController pc(nullptr);
  pc.openFolder(L"C:\\foo\\bar");
  FE_ASSERT_TRUE(pc.up());
  FE_ASSERT_WSTREQ(pc.currentPath(), L"C:\\foo");
}

FE_TEST_CASE(PaneController_Up_FromDriveSubfolder_ReturnsDriveRoot) {
  PaneController pc(nullptr);
  pc.openFolder(L"C:\\foo");
  FE_ASSERT_TRUE(pc.up());
  FE_ASSERT_WSTREQ(pc.currentPath(), L"C:\\");
}

FE_TEST_CASE(PaneController_Up_FromDriveRoot_ReturnsFalse) {
  PaneController pc(nullptr);
  pc.openFolder(L"C:\\");
  FE_ASSERT_FALSE(pc.up());
}

FE_TEST_CASE(PaneController_Refresh_Empty_ReturnsFalse) {
  PaneController pc(nullptr);
  FE_ASSERT_FALSE(pc.refresh());
}

FE_TEST_CASE(PaneController_Refresh_BumpsGenerationWithoutHistoryPush) {
  PaneController pc(nullptr);
  FE_ASSERT_TRUE(pc.openFolder(L"C:\\a"));
  FE_ASSERT_TRUE(pc.openFolder(L"C:\\b"));
  const uint32_t before = pc.generation();
  const auto backDepthBefore = pc.canGoBack();
  FE_ASSERT_TRUE(pc.refresh());
  FE_ASSERT_TRUE(pc.generation() != before);
  FE_ASSERT_WSTREQ(pc.currentPath(), L"C:\\b");
  FE_ASSERT_EQ(pc.canGoBack(), backDepthBefore);
}

FE_TEST_CASE(PaneController_Up_FromExtendedPrefix_NormalizesToDisplay) {
  PaneController pc(nullptr);
  // openFolder accepts the \\?\ extended-length form and stores it
  // verbatim.  computeParent should still produce the canonical "C:\"
  // display form by toDisplay-normalising the input first.
  pc.openFolder(L"\\\\?\\C:\\foo");
  FE_ASSERT_TRUE(pc.up());
  FE_ASSERT_WSTREQ(pc.currentPath(), L"C:\\");
}

FE_TEST_CASE(PaneController_OpenFolder_Twice_CancelsAndReopens) {
  TempDir a(L"pane-a");
  TempDir b(L"pane-b");
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, a.path(), 1).error,
               GenerateError::None);
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, b.path(), 1).error,
               GenerateError::None);

  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(a.path()));
  FE_ASSERT_TRUE(pane.openFolder(b.path()));
  pane.joinForTest();
  FE_ASSERT_WSTREQ(pane.currentPath(), b.path());
  FE_ASSERT_TRUE(pane.generation() >= 2u);
}

// ---------------------------------------------------------------------------
// requestSort
// ---------------------------------------------------------------------------

FE_TEST_CASE(PaneController_Default_HasNoSortApplied) {
  PaneController pc(nullptr);
  FE_ASSERT_FALSE(pc.hasSortApplied());
}

FE_TEST_CASE(PaneController_RequestSort_EmptyStore_ReturnsRejected) {
  PaneController pc(nullptr);
  FE_ASSERT_EQ(static_cast<int>(pc.requestSort(SortKey::Name)),
               static_cast<int>(SortDispatch::Rejected));
  FE_ASSERT_FALSE(pc.hasSortApplied());
}

FE_TEST_CASE(PaneController_RequestSort_AppliesNameAscending) {
  TempDir tmp(L"pane-sort-name");
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, tmp.path(), 1).error,
               GenerateError::None);
  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();
  FE_ASSERT_EQ(static_cast<int>(pane.requestSort(SortKey::Name)),
               static_cast<int>(SortDispatch::AppliedSync));
  FE_ASSERT_TRUE(pane.hasSortApplied());
  FE_ASSERT_EQ(static_cast<int>(pane.currentSortSpec().key),
               static_cast<int>(SortKey::Name));
  FE_ASSERT_EQ(static_cast<int>(pane.currentSortSpec().direction),
               static_cast<int>(SortDirection::Ascending));
}

FE_TEST_CASE(PaneController_RequestSort_SameKeyTogglesDirection) {
  TempDir tmp(L"pane-sort-toggle");
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, tmp.path(), 1).error,
               GenerateError::None);
  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();
  pane.requestSort(SortKey::Name);
  FE_ASSERT_EQ(static_cast<int>(pane.currentSortSpec().direction),
               static_cast<int>(SortDirection::Ascending));
  pane.requestSort(SortKey::Name);
  FE_ASSERT_EQ(static_cast<int>(pane.currentSortSpec().direction),
               static_cast<int>(SortDirection::Descending));
  pane.requestSort(SortKey::Name);
  FE_ASSERT_EQ(static_cast<int>(pane.currentSortSpec().direction),
               static_cast<int>(SortDirection::Ascending));
}

FE_TEST_CASE(PaneController_RequestSort_DifferentKeyResetsToAscending) {
  TempDir tmp(L"pane-sort-switch");
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, tmp.path(), 1).error,
               GenerateError::None);
  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();
  pane.requestSort(SortKey::Name);
  pane.requestSort(SortKey::Name);  // now Descending
  FE_ASSERT_EQ(static_cast<int>(pane.currentSortSpec().direction),
               static_cast<int>(SortDirection::Descending));
  FE_ASSERT_EQ(static_cast<int>(pane.requestSort(SortKey::Size)),
               static_cast<int>(SortDispatch::AppliedSync));
  FE_ASSERT_EQ(static_cast<int>(pane.currentSortSpec().key),
               static_cast<int>(SortKey::Size));
  FE_ASSERT_EQ(static_cast<int>(pane.currentSortSpec().direction),
               static_cast<int>(SortDirection::Ascending));
}

FE_TEST_CASE(PaneController_OpenFolder_ResetsSortApplied) {
  TempDir tmp(L"pane-sort-reset");
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, tmp.path(), 1).error,
               GenerateError::None);
  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();
  pane.requestSort(SortKey::Name);
  FE_ASSERT_TRUE(pane.hasSortApplied());
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();
  FE_ASSERT_FALSE(pane.hasSortApplied());
}

FE_TEST_CASE(PaneController_BackgroundSort_FillsPendingAndAppliesOnDemand) {
  TempDir tmp(L"pane-sort-bg");
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, tmp.path(), 1).error,
               GenerateError::None);
  // sortThreshold = 50 forces the background path on the 200-entry
  // small preset.
  PaneController pane(nullptr);
  pane.setSortThresholdRowsForTest(50);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();

  FE_ASSERT_EQ(static_cast<int>(pane.requestSort(SortKey::Name)),
               static_cast<int>(SortDispatch::Dispatched));
  // Background sort: hasSortApplied() flips only on applyPendingSort.
  pane.applyPendingSort(pane.generation());
  FE_ASSERT_TRUE(pane.hasSortApplied());

  const auto& store = pane.store();
  std::wstring first(store.visibleAt(0).namePtr,
                     store.visibleAt(0).nameLength);
  std::wstring last(store.visibleAt(store.publishedCount() - 1).namePtr,
                    store.visibleAt(store.publishedCount() - 1).nameLength);
  FE_ASSERT_TRUE(first < last);
}

// ---------------------------------------------------------------------------
// Stable selection (selectRaw / clearSelection / selectedRowsUnderCurrentOrder)
// ---------------------------------------------------------------------------

FE_TEST_CASE(PaneController_Selection_Default_Empty) {
  PaneController pc(nullptr);
  FE_ASSERT_EQ(pc.selectedCount(), static_cast<std::size_t>(0));
  FE_ASSERT_FALSE(pc.isRawSelected(0));
  FE_ASSERT_TRUE(pc.selectedRowsUnderCurrentOrder().empty());
}

FE_TEST_CASE(PaneController_Selection_SelectAndDeselect) {
  PaneController pc(nullptr);
  pc.selectRaw(3);
  pc.selectRaw(7);
  FE_ASSERT_EQ(pc.selectedCount(), static_cast<std::size_t>(2));
  FE_ASSERT_TRUE(pc.isRawSelected(3));
  FE_ASSERT_TRUE(pc.isRawSelected(7));
  FE_ASSERT_FALSE(pc.isRawSelected(5));

  pc.deselectRaw(3);
  FE_ASSERT_FALSE(pc.isRawSelected(3));
  FE_ASSERT_EQ(pc.selectedCount(), static_cast<std::size_t>(1));
}

FE_TEST_CASE(PaneController_Selection_DoubleSelectIsIdempotent) {
  PaneController pc(nullptr);
  pc.selectRaw(4);
  pc.selectRaw(4);
  FE_ASSERT_EQ(pc.selectedCount(), static_cast<std::size_t>(1));
}

FE_TEST_CASE(PaneController_Selection_ClearEmptiesSet) {
  PaneController pc(nullptr);
  pc.selectRaw(0);
  pc.selectRaw(1);
  pc.selectRaw(2);
  pc.clearSelection();
  FE_ASSERT_EQ(pc.selectedCount(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(PaneController_Selection_ClearedOnOpenFolder) {
  TempDir tmp(L"pane-sel-clear");
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, tmp.path(), 1).error,
               GenerateError::None);
  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();
  pane.selectRaw(0);
  pane.selectRaw(1);
  FE_ASSERT_EQ(pane.selectedCount(), static_cast<std::size_t>(2));
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();
  FE_ASSERT_EQ(pane.selectedCount(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(PaneController_Selection_RowsFollowSortReorder) {
  TempDir tmp(L"pane-sel-sort");
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, tmp.path(), 1).error,
               GenerateError::None);
  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();

  // Capture the raw indices that sit at rows 0 and 1 *before* the
  // sort, then sort and verify those raws now appear at whatever
  // new visible rows they ended up in.
  const auto& store = pane.store();
  const auto orderBefore = store.visibleOrder();
  FE_ASSERT_TRUE(orderBefore.size() >= 2);
  const std::uint32_t rawA = orderBefore[0];
  const std::uint32_t rawB = orderBefore[1];
  pane.selectRaw(rawA);
  pane.selectRaw(rawB);

  pane.requestSort(SortKey::Name);
  pane.requestSort(SortKey::Name);  // toggle to Descending — meaningful reorder

  const auto rowsAfter = pane.selectedRowsUnderCurrentOrder();
  FE_ASSERT_EQ(rowsAfter.size(), static_cast<std::size_t>(2));
  // Verify the rows really point back at the originally selected raw
  // indices under the new permutation.
  const auto orderAfter = store.visibleOrder();
  std::unordered_set<std::uint32_t> rawsAtSelectedRows;
  for (int row : rowsAfter) {
    rawsAtSelectedRows.insert(orderAfter[static_cast<std::size_t>(row)]);
  }
  FE_ASSERT_TRUE(rawsAtSelectedRows.count(rawA) == 1);
  FE_ASSERT_TRUE(rawsAtSelectedRows.count(rawB) == 1);
}

FE_TEST_CASE(PaneController_RequestSort_ActuallyReordersVisible) {
  TempDir tmp(L"pane-sort-order");
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, tmp.path(), 1).error,
               GenerateError::None);
  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();

  pane.requestSort(SortKey::Name);
  const auto& storeAsc = pane.store();
  std::wstring firstAsc(storeAsc.visibleAt(0).namePtr,
                        storeAsc.visibleAt(0).nameLength);
  std::wstring lastAsc(
      storeAsc.visibleAt(storeAsc.itemCount() - 1).namePtr,
      storeAsc.visibleAt(storeAsc.itemCount() - 1).nameLength);
  FE_ASSERT_TRUE(firstAsc < lastAsc);

  pane.requestSort(SortKey::Name);  // toggle to Desc
  const auto& storeDesc = pane.store();
  std::wstring firstDesc(storeDesc.visibleAt(0).namePtr,
                         storeDesc.visibleAt(0).nameLength);
  std::wstring lastDesc(
      storeDesc.visibleAt(storeDesc.itemCount() - 1).namePtr,
      storeDesc.visibleAt(storeDesc.itemCount() - 1).nameLength);
  FE_ASSERT_TRUE(firstDesc > lastDesc);
  FE_ASSERT_TRUE(firstAsc == lastDesc);
  FE_ASSERT_TRUE(lastAsc == firstDesc);
}
