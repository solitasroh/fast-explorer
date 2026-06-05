#include <unordered_set>

#include <objbase.h>
#include <shobjidl.h>

#include "bench-fs-helper.h"
#include "bench/dataset-generator.h"
#include "core/file-entry.h"
#include "core/file-grouping.h"
#include "core/file-sort.h"
#include "core/path-utils.h"
#include "test-harness.h"
#include "explorer/filter-pattern.h"
#include "explorer/pane-controller.h"

using fast_explorer::bench::generateDataset;
using fast_explorer::bench::GenerateError;
using fast_explorer::bench::PresetKind;
using fast_explorer::core::GroupKey;
using fast_explorer::core::SortDirection;
using fast_explorer::core::SortKey;
using fast_explorer::tests::TempDir;
using fast_explorer::ui::FilterMode;
using fast_explorer::ui::FilterPattern;
using fast_explorer::ui::PaneController;
using fast_explorer::ui::SortDispatch;

constexpr uint64_t kSmallPresetExpectedItems = 200;

namespace {

void createExistingDirectory(const std::wstring& path) {
  FE_ASSERT_TRUE(CreateDirectoryW(path.c_str(), nullptr) != 0);
}

std::wstring parentPathOf(const std::wstring& path) {
  const std::size_t pos = path.find_last_of(L"\\/");
  FE_ASSERT_TRUE(pos != std::wstring::npos);
  if (pos == 2 && path.size() >= 3 && path[1] == L':') {
    return path.substr(0, 3);
  }
  return path.substr(0, pos);
}

std::wstring extendedPathOf(const std::wstring& path) {
  return std::wstring(L"\\\\?\\") + path;
}

bool createShellLinkFile(const std::wstring& linkPath,
                         const std::wstring& targetPath) {
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool uninit = SUCCEEDED(init);
  if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
    return false;
  }

  IShellLinkW* link = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&link));
  if (SUCCEEDED(hr) && link != nullptr) {
    hr = link->SetPath(targetPath.c_str());
    if (SUCCEEDED(hr)) {
      hr = link->SetDescription(targetPath.c_str());
    }
  }

  IPersistFile* persist = nullptr;
  if (SUCCEEDED(hr) && link != nullptr) {
    hr = link->QueryInterface(IID_PPV_ARGS(&persist));
  }
  if (SUCCEEDED(hr) && persist != nullptr) {
    hr = persist->Save(linkPath.c_str(), TRUE);
  }

  if (persist != nullptr) {
    persist->Release();
  }
  if (link != nullptr) {
    link->Release();
  }
  if (uninit) {
    CoUninitialize();
  }
  return SUCCEEDED(hr);
}

}  // namespace

FE_TEST_CASE(PaneController_Default_HasZeroGenerationAndEmptyPath) {
  PaneController pc(nullptr);
  FE_ASSERT_EQ(pc.generation(), 0u);
  FE_ASSERT_TRUE(pc.currentPath().empty());
  FE_ASSERT_EQ(pc.hostWindow(), static_cast<HWND>(nullptr));
}

FE_TEST_CASE(PaneController_OpenFolder_ValidPath_AcceptsAndBumpsGeneration) {
  TempDir tmp(L"pane-open-valid");
  createExistingDirectory(tmp.path());
  PaneController pc(nullptr);
  const uint32_t before = pc.generation();
  FE_ASSERT_TRUE(pc.openFolder(tmp.path()));
  FE_ASSERT_WSTREQ(pc.currentPath(), tmp.path());
  FE_ASSERT_TRUE(pc.generation() != before);
}

FE_TEST_CASE(PaneController_OpenFolder_EmptyPath_Rejected) {
  TempDir tmp(L"pane-open-empty-initial");
  createExistingDirectory(tmp.path());
  PaneController pc(nullptr);
  FE_ASSERT_TRUE(pc.openFolder(tmp.path()));
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

FE_TEST_CASE(PaneController_OpenFolder_UncPath_Accepted) {
  // Server-only UNC paths are valid; Win32FsBackend enumerates shares
  // with WNetEnumResource because there is no share folder to stat.
  PaneController pc(nullptr);
  FE_ASSERT_TRUE(pc.openFolder(L"\\\\server"));
  FE_ASSERT_WSTREQ(pc.currentPath(), L"\\\\server");
}

FE_TEST_CASE(PaneController_OpenFolder_ShellThisPc_ListsDriveRoots) {
  PaneController pc(nullptr);
  FE_ASSERT_TRUE(pc.openFolder(L"shell:ThisPC"));
  FE_ASSERT_WSTREQ(pc.currentPath(), L"shell:ThisPC");
  FE_ASSERT_WSTREQ(pc.currentLocation(), L"shell:ThisPC");
  FE_ASSERT_TRUE(pc.store().publishedCount() > 0);
  FE_ASSERT_TRUE(fast_explorer::core::isDirectory(pc.store().visibleAt(0)));
}

FE_TEST_CASE(PaneController_ShellThisPc_RowMapsToDriveRootTarget) {
  PaneController pc(nullptr);
  FE_ASSERT_TRUE(pc.openFolder(L"shell:ThisPC"));
  FE_ASSERT_TRUE(pc.store().publishedCount() > 0);

  std::wstring target;
  FE_ASSERT_TRUE(pc.sourcePathForVisibleRow(0, target));
  FE_ASSERT_FALSE(target.empty());
  const DWORD attrs = GetFileAttributesW(target.c_str());
  FE_ASSERT_TRUE(attrs != INVALID_FILE_ATTRIBUTES);
  FE_ASSERT_TRUE((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

FE_TEST_CASE(PaneController_ShellThisPc_RowExposesDriveIconLocation) {
  PaneController pc(nullptr);
  FE_ASSERT_TRUE(pc.openFolder(L"shell:ThisPC"));
  FE_ASSERT_TRUE(pc.store().publishedCount() > 0);

  std::wstring target;
  std::wstring iconLocation;
  FE_ASSERT_TRUE(pc.sourcePathForVisibleRow(0, target));
  FE_ASSERT_TRUE(pc.iconLocationForVisibleRow(0, iconLocation));
  FE_ASSERT_WSTREQ(iconLocation, target);
}

FE_TEST_CASE(PaneController_ShellThisPc_RefreshPreservesFilter) {
  PaneController pc(nullptr);
  FE_ASSERT_TRUE(pc.openFolder(L"shell:ThisPC"));
  FE_ASSERT_TRUE(pc.store().publishedCount() > 0);

  const auto& first = pc.store().visibleAt(0);
  const std::wstring query(first.namePtr, first.nameLength);
  pc.setFilter(FilterPattern(query, FilterMode::Plain));
  FE_ASSERT_EQ(pc.store().displayedCount(), static_cast<std::size_t>(1));

  FE_ASSERT_TRUE(pc.refresh());
  FE_ASSERT_TRUE(pc.hasActiveFilter());
  FE_ASSERT_EQ(pc.store().displayedCount(), static_cast<std::size_t>(1));
}

FE_TEST_CASE(PaneController_ShellRoot_FileOperationsAreRejected) {
  PaneController pc(nullptr);
  FE_ASSERT_TRUE(pc.openFolder(L"shell:ThisPC"));
  FE_ASSERT_FALSE(pc.deleteItem(0));
  FE_ASSERT_FALSE(pc.renameItem(0, L"renamed"));
  FE_ASSERT_FALSE(pc.createSubfolder(L"new-folder"));
}

FE_TEST_CASE(PaneController_OpenFolder_ShellNetwork_AcceptsVirtualLocation) {
  PaneController pc(nullptr);
  FE_ASSERT_TRUE(pc.openFolder(L"shell:Network"));
  FE_ASSERT_WSTREQ(pc.currentPath(), L"shell:Network");
  FE_ASSERT_WSTREQ(pc.currentLocation(), L"shell:Network");
  FE_ASSERT_TRUE(pc.generation() > 0);
}

FE_TEST_CASE(PaneController_OpenFolder_GenericShellNamespace_AcceptsRecycleBin) {
  PaneController pc(nullptr);
  FE_ASSERT_TRUE(
      pc.openFolder(L"::{645FF040-5081-101B-9F08-00AA002F954E}"));
  FE_ASSERT_WSTREQ(pc.currentPath(), L"shell:RecycleBinFolder");
  FE_ASSERT_WSTREQ(pc.currentLocation(), L"shell:RecycleBinFolder");
  FE_ASSERT_TRUE(pc.generation() > 0);
}

FE_TEST_CASE(PaneController_GenericShellNamespace_FileOperationsAreRejected) {
  PaneController pc(nullptr);
  FE_ASSERT_TRUE(
      pc.openFolder(L"::{645FF040-5081-101B-9F08-00AA002F954E}"));
  FE_ASSERT_FALSE(pc.createSubfolder(L"new-folder"));
  FE_ASSERT_FALSE(pc.deleteItem(0));
  FE_ASSERT_FALSE(pc.renameItem(0, L"renamed"));
}

FE_TEST_CASE(PaneController_OpenFolder_NetworkShortcutFolder_NavigatesToTarget) {
  TempDir tmp(L"pane-network-shortcut-folder");
  createExistingDirectory(tmp.path());

  const std::wstring shortcutDir =
      fast_explorer::core::joinPath(tmp.path(), L"Home");
  const std::wstring targetDir =
      fast_explorer::core::joinPath(tmp.path(), L"actual-home");
  createExistingDirectory(shortcutDir);
  createExistingDirectory(targetDir);
  fast_explorer::tests::writeEmptyDiskFile(
      fast_explorer::core::joinPath(targetDir, L"inside.txt"));

  const std::wstring desktopIni =
      fast_explorer::core::joinPath(shortcutDir, L"desktop.ini");
  FE_ASSERT_TRUE(WritePrivateProfileStringW(
      L".ShellClassInfo", L"CLSID2",
      L"{0AFACED1-E828-11D1-9187-B532F1E9575D}",
      desktopIni.c_str()) != 0);
  FE_ASSERT_TRUE(createShellLinkFile(
      fast_explorer::core::joinPath(shortcutDir, L"target.lnk"), targetDir));

  PaneController pc(nullptr);
  FE_ASSERT_TRUE(pc.openFolder(shortcutDir));
  pc.joinForTest();
  FE_ASSERT_EQ(pc.store().publishedCount(), static_cast<std::size_t>(1));
  FE_ASSERT_WSTREQ(
      std::wstring(fast_explorer::core::nameView(pc.store().visibleAt(0))),
      L"inside.txt");
}

FE_TEST_CASE(PaneController_OpenFolder_Twice_BumpsGenerationEachTime) {
  TempDir a(L"pane-open-twice-a");
  TempDir b(L"pane-open-twice-b");
  createExistingDirectory(a.path());
  createExistingDirectory(b.path());
  PaneController pc(nullptr);
  FE_ASSERT_TRUE(pc.openFolder(a.path()));
  const uint32_t g1 = pc.generation();
  FE_ASSERT_TRUE(pc.openFolder(b.path()));
  const uint32_t g2 = pc.generation();
  FE_ASSERT_TRUE(g2 != g1);
  FE_ASSERT_WSTREQ(pc.currentPath(), b.path());
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
  TempDir a(L"pane-history-a");
  TempDir b(L"pane-history-b");
  createExistingDirectory(a.path());
  createExistingDirectory(b.path());
  PaneController pc(nullptr);
  FE_ASSERT_TRUE(pc.openFolder(a.path()));
  FE_ASSERT_TRUE(pc.openFolder(b.path()));
  FE_ASSERT_TRUE(pc.canGoBack());
  FE_ASSERT_TRUE(pc.back());
  FE_ASSERT_WSTREQ(pc.currentPath(), a.path());
  FE_ASSERT_TRUE(pc.canGoForward());
  FE_ASSERT_FALSE(pc.canGoBack());
}

FE_TEST_CASE(PaneController_BackThenForward_RestoresLatest) {
  TempDir a(L"pane-forward-a");
  TempDir b(L"pane-forward-b");
  createExistingDirectory(a.path());
  createExistingDirectory(b.path());
  PaneController pc(nullptr);
  pc.openFolder(a.path());
  pc.openFolder(b.path());
  pc.back();
  FE_ASSERT_TRUE(pc.forward());
  FE_ASSERT_WSTREQ(pc.currentPath(), b.path());
  FE_ASSERT_FALSE(pc.canGoForward());
}

FE_TEST_CASE(PaneController_OpenAfterBack_ClearsForward) {
  TempDir a(L"pane-forward-clear-a");
  TempDir b(L"pane-forward-clear-b");
  TempDir c(L"pane-forward-clear-c");
  createExistingDirectory(a.path());
  createExistingDirectory(b.path());
  createExistingDirectory(c.path());
  PaneController pc(nullptr);
  pc.openFolder(a.path());
  pc.openFolder(b.path());
  pc.back();
  FE_ASSERT_TRUE(pc.canGoForward());
  FE_ASSERT_TRUE(pc.openFolder(c.path()));
  FE_ASSERT_FALSE(pc.canGoForward());
}

FE_TEST_CASE(PaneController_Up_FromFolder_ReturnsParent) {
  TempDir tmp(L"pane-up-parent");
  createExistingDirectory(tmp.path());
  const std::wstring child =
      fast_explorer::core::joinPath(tmp.path(), L"child");
  createExistingDirectory(child);
  PaneController pc(nullptr);
  pc.openFolder(child);
  FE_ASSERT_TRUE(pc.up());
  FE_ASSERT_WSTREQ(pc.currentPath(), tmp.path());
}

FE_TEST_CASE(PaneController_Up_FromDriveSubfolder_ReturnsDriveRoot) {
  TempDir tmp(L"pane-up-real-parent");
  createExistingDirectory(tmp.path());
  PaneController pc(nullptr);
  pc.openFolder(tmp.path());
  FE_ASSERT_TRUE(pc.up());
  FE_ASSERT_WSTREQ(pc.currentPath(), parentPathOf(tmp.path()));
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
  TempDir a(L"pane-refresh-a");
  TempDir b(L"pane-refresh-b");
  createExistingDirectory(a.path());
  createExistingDirectory(b.path());
  PaneController pc(nullptr);
  FE_ASSERT_TRUE(pc.openFolder(a.path()));
  FE_ASSERT_TRUE(pc.openFolder(b.path()));
  const uint32_t before = pc.generation();
  const auto backDepthBefore = pc.canGoBack();
  FE_ASSERT_TRUE(pc.refresh());
  FE_ASSERT_TRUE(pc.generation() != before);
  FE_ASSERT_WSTREQ(pc.currentPath(), b.path());
  FE_ASSERT_EQ(pc.canGoBack(), backDepthBefore);
}

FE_TEST_CASE(PaneController_Up_FromExtendedPrefix_NormalizesToDisplay) {
  TempDir tmp(L"pane-up-extended");
  createExistingDirectory(tmp.path());
  const std::wstring child =
      fast_explorer::core::joinPath(tmp.path(), L"child");
  createExistingDirectory(child);
  PaneController pc(nullptr);
  // openFolder accepts the \\?\ extended-length form and stores it
  // verbatim. computeParent should still produce the canonical
  // display form by toDisplay-normalising the input first.
  pc.openFolder(extendedPathOf(child));
  FE_ASSERT_TRUE(pc.up());
  FE_ASSERT_WSTREQ(pc.currentPath(), tmp.path());
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

FE_TEST_CASE(PaneController_Selection_RowsFollowActiveFilter) {
  TempDir tmp(L"pane-sel-filter");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);
  fast_explorer::tests::writeEmptyDiskFile(tmp.path() + L"\\alpha.txt");
  fast_explorer::tests::writeEmptyDiskFile(tmp.path() + L"\\target.txt");

  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();

  const auto& store = pane.store();
  std::uint32_t targetRaw = UINT32_MAX;
  for (std::uint32_t raw = 0;
       raw < static_cast<std::uint32_t>(store.publishedCount()); ++raw) {
    const auto& entry = store.entryAt(raw);
    if (std::wstring_view(entry.namePtr, entry.nameLength) == L"target.txt") {
      targetRaw = raw;
      break;
    }
  }
  FE_ASSERT_TRUE(targetRaw != UINT32_MAX);

  pane.setFilter(FilterPattern(L"target", FilterMode::Plain));
  FE_ASSERT_EQ(pane.store().displayedCount(), static_cast<std::size_t>(1));
  pane.selectRaw(targetRaw);

  const auto rows = pane.selectedRowsUnderCurrentOrder();
  FE_ASSERT_EQ(rows.size(), static_cast<std::size_t>(1));
  FE_ASSERT_EQ(rows[0], 0);
}

FE_TEST_CASE(PaneController_SourcePathForVisibleRow_HonorsActiveFilter) {
  TempDir tmp(L"pane-sourcepath-filter");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);
  fast_explorer::tests::writeEmptyDiskFile(tmp.path() + L"\\alpha.txt");
  const std::wstring targetPath = tmp.path() + L"\\target.txt";
  fast_explorer::tests::writeEmptyDiskFile(targetPath);

  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();

  pane.setFilter(FilterPattern(L"target", FilterMode::Plain));
  FE_ASSERT_EQ(pane.store().displayedCount(), static_cast<std::size_t>(1));

  std::wstring actual;
  FE_ASSERT_TRUE(pane.sourcePathForVisibleRow(0, actual));
  FE_ASSERT_WSTREQ(actual, targetPath);
}

FE_TEST_CASE(PaneController_FilterRefinement_NarrowsExistingSubset) {
  TempDir tmp(L"pane-filter-refine");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);
  fast_explorer::tests::writeEmptyDiskFile(tmp.path() + L"\\alpha.txt");
  fast_explorer::tests::writeEmptyDiskFile(tmp.path() + L"\\alphabet.txt");
  fast_explorer::tests::writeEmptyDiskFile(tmp.path() + L"\\beta.txt");

  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();

  pane.setFilter(FilterPattern(L"alpha", FilterMode::Plain));
  FE_ASSERT_EQ(pane.store().displayedCount(), static_cast<std::size_t>(2));

  pane.setFilter(FilterPattern(L"alphabet", FilterMode::Plain));
  FE_ASSERT_EQ(pane.store().displayedCount(), static_cast<std::size_t>(1));
  const auto& entry = pane.store().visibleAt(0);
  FE_ASSERT_WSTREQ(std::wstring(entry.namePtr, entry.nameLength),
                   L"alphabet.txt");
}

FE_TEST_CASE(PaneController_OpenFolder_MissingPathPreservesCurrentState) {
  TempDir tmp(L"pane-missing-preserve");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);
  fast_explorer::tests::writeEmptyDiskFile(tmp.path() + L"\\keep.txt");

  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();
  const auto beforeGeneration = pane.generation();
  const std::wstring beforePath = pane.currentPath();
  const auto beforeCount = pane.store().publishedCount();
  FE_ASSERT_TRUE(beforeCount > 0);
  pane.selectRaw(0);

  const std::wstring missing =
      fast_explorer::core::joinPath(tmp.path(), L"does-not-exist");
  FE_ASSERT_FALSE(pane.openFolder(missing));

  FE_ASSERT_EQ(pane.generation(), beforeGeneration);
  FE_ASSERT_WSTREQ(pane.currentPath(), beforePath);
  FE_ASSERT_EQ(pane.store().publishedCount(), beforeCount);
  FE_ASSERT_EQ(pane.selectedCount(), static_cast<std::size_t>(1));
  FE_ASSERT_TRUE(pane.isRawSelected(0));
}

// ---------------------------------------------------------------------------
// openItem
// ---------------------------------------------------------------------------

FE_TEST_CASE(PaneController_OpenItem_OutOfRange_ReturnsFalse) {
  PaneController pc(nullptr);
  // Empty store: row 0 is already past publishedCount().
  FE_ASSERT_FALSE(pc.openItem(0));
}

FE_TEST_CASE(PaneController_OpenItem_OutOfRange_AfterPopulate_ReturnsFalse) {
  TempDir tmp(L"pane-openitem-oob");
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, tmp.path(), 1).error,
               GenerateError::None);
  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();
  const std::uint32_t count =
      static_cast<std::uint32_t>(pane.store().publishedCount());
  FE_ASSERT_FALSE(pane.openItem(count));
  FE_ASSERT_FALSE(pane.openItem(count + 100));
}

FE_TEST_CASE(PaneController_OpenItem_FolderBranch_NavigatesIntoChild) {
  // Build tmp/<child>/ and open it through openItem so the directory
  // branch is exercised end-to-end without poking the shell.
  TempDir tmp(L"pane-openitem-folder");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);
  const std::wstring childPath =
      fast_explorer::core::joinPath(tmp.path(), L"child");
  FE_ASSERT_TRUE(CreateDirectoryW(childPath.c_str(), nullptr) != 0);
  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();

  // Find the visible row whose name is L"child".
  const auto& store = pane.store();
  std::uint32_t childRow = UINT32_MAX;
  for (std::uint32_t i = 0;
       i < static_cast<std::uint32_t>(store.publishedCount()); ++i) {
    const auto& entry = store.visibleAt(i);
    if (std::wstring_view(entry.namePtr, entry.nameLength) == L"child") {
      childRow = i;
      break;
    }
  }
  FE_ASSERT_TRUE(childRow != UINT32_MAX);

  FE_ASSERT_TRUE(pane.openItem(childRow));
  pane.joinForTest();
  FE_ASSERT_WSTREQ(pane.currentPath(), childPath);
}

FE_TEST_CASE(PaneController_OpenItem_FolderBranch_PreservesActiveFilter) {
  TempDir tmp(L"pane-openitem-folder-filter");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);
  const std::wstring childPath =
      fast_explorer::core::joinPath(tmp.path(), L"needle-folder");
  FE_ASSERT_TRUE(CreateDirectoryW(childPath.c_str(), nullptr) != 0);
  fast_explorer::tests::writeEmptyDiskFile(
      fast_explorer::core::joinPath(childPath, L"needle.txt"));
  fast_explorer::tests::writeEmptyDiskFile(
      fast_explorer::core::joinPath(childPath, L"other.txt"));

  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();
  pane.setFilter(FilterPattern(L"needle", FilterMode::Plain));
  FE_ASSERT_TRUE(pane.hasActiveFilter());
  FE_ASSERT_EQ(pane.store().displayedCount(), static_cast<std::size_t>(1));

  FE_ASSERT_TRUE(pane.openItem(0));
  pane.joinForTest();
  FE_ASSERT_WSTREQ(pane.currentPath(), childPath);
  FE_ASSERT_TRUE(pane.hasActiveFilter());
  pane.setFilter(pane.currentFilter());
  FE_ASSERT_EQ(pane.store().displayedCount(), static_cast<std::size_t>(1));
  const auto& entry = pane.store().visibleAt(0);
  FE_ASSERT_EQ(std::wstring_view(entry.namePtr, entry.nameLength),
               std::wstring_view(L"needle.txt"));
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

using fast_explorer::tests::diskPathExists;
using fast_explorer::tests::writeEmptyDiskFile;

FE_TEST_CASE(PaneController_DeleteItem_EmptyStore_ReturnsFalse) {
  PaneController pane(nullptr);
  FE_ASSERT_FALSE(pane.deleteItem(0));
  FE_ASSERT_FALSE(pane.deleteItem(100));
}

FE_TEST_CASE(PaneController_DeleteItem_OutOfRangeRow_ReturnsFalse) {
  TempDir tmp(L"pane-delete-oob");
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, tmp.path(), 1).error,
               GenerateError::None);
  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();
  const std::uint32_t count =
      static_cast<std::uint32_t>(pane.store().publishedCount());
  FE_ASSERT_FALSE(pane.deleteItem(count));
  FE_ASSERT_FALSE(pane.deleteItem(count + 100));
}

FE_TEST_CASE(PaneController_DeleteItem_ValidRow_RemovesFileFromDisk) {
  TempDir tmp(L"pane-delete-real");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);
  const std::wstring victim =
      fast_explorer::core::joinPath(tmp.path(), L"victim.txt");
  writeEmptyDiskFile(victim);
  FE_ASSERT_TRUE(diskPathExists(victim));

  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();

  std::uint32_t victimRow = UINT32_MAX;
  const auto& store = pane.store();
  for (std::uint32_t i = 0;
       i < static_cast<std::uint32_t>(store.publishedCount()); ++i) {
    const auto& entry = store.visibleAt(i);
    if (std::wstring_view(entry.namePtr, entry.nameLength) == L"victim.txt") {
      victimRow = i;
      break;
    }
  }
  FE_ASSERT_TRUE(victimRow != UINT32_MAX);

  FE_ASSERT_TRUE(pane.deleteItem(victimRow));
  pane.shellWorkerForTest().waitForProcessedForTest(1);
  FE_ASSERT_FALSE(diskPathExists(victim));
}

FE_TEST_CASE(PaneController_RenameItem_EmptyStore_ReturnsFalse) {
  PaneController pane(nullptr);
  FE_ASSERT_FALSE(pane.renameItem(0, L"whatever"));
  FE_ASSERT_FALSE(pane.renameItem(100, L"whatever"));
}

FE_TEST_CASE(PaneController_RenameItem_EmptyNewName_ReturnsFalse) {
  TempDir tmp(L"pane-rename-empty-name");
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, tmp.path(), 1).error,
               GenerateError::None);
  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();
  FE_ASSERT_FALSE(pane.renameItem(0, L""));
}

FE_TEST_CASE(PaneController_RenameItem_OutOfRangeRow_ReturnsFalse) {
  TempDir tmp(L"pane-rename-oob");
  FE_ASSERT_EQ(generateDataset(PresetKind::Small, tmp.path(), 1).error,
               GenerateError::None);
  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();
  const std::uint32_t count =
      static_cast<std::uint32_t>(pane.store().publishedCount());
  FE_ASSERT_FALSE(pane.renameItem(count, L"target.txt"));
  FE_ASSERT_FALSE(pane.renameItem(count + 100, L"target.txt"));
}

FE_TEST_CASE(PaneController_CreateSubfolder_EmptyName_ReturnsFalse) {
  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(L"C:\\"));
  pane.joinForTest();
  FE_ASSERT_FALSE(pane.createSubfolder(L""));
}

FE_TEST_CASE(PaneController_CreateSubfolder_NoCurrentPath_ReturnsFalse) {
  PaneController pane(nullptr);
  FE_ASSERT_FALSE(pane.createSubfolder(L"any"));
}

FE_TEST_CASE(PaneController_CreateSubfolder_ValidName_CreatesOnDisk) {
  TempDir tmp(L"pane-create-real");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);

  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();

  FE_ASSERT_TRUE(pane.createSubfolder(L"NewSub"));
  pane.shellWorkerForTest().waitForProcessedForTest(1);

  const std::wstring childPath =
      fast_explorer::core::joinPath(tmp.path(), L"NewSub");
  FE_ASSERT_TRUE(diskPathExists(childPath));
  const DWORD attr = GetFileAttributesW(childPath.c_str());
  FE_ASSERT_TRUE((attr & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

FE_TEST_CASE(PaneController_RenameItem_ValidRow_RenamesOnDisk) {
  TempDir tmp(L"pane-rename-real");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);
  const std::wstring before =
      fast_explorer::core::joinPath(tmp.path(), L"before.txt");
  const std::wstring after =
      fast_explorer::core::joinPath(tmp.path(), L"after.txt");
  writeEmptyDiskFile(before);
  FE_ASSERT_TRUE(diskPathExists(before));

  PaneController pane(nullptr);
  FE_ASSERT_TRUE(pane.openFolder(tmp.path()));
  pane.joinForTest();

  std::uint32_t sourceRow = UINT32_MAX;
  const auto& store = pane.store();
  for (std::uint32_t i = 0;
       i < static_cast<std::uint32_t>(store.publishedCount()); ++i) {
    const auto& entry = store.visibleAt(i);
    if (std::wstring_view(entry.namePtr, entry.nameLength) == L"before.txt") {
      sourceRow = i;
      break;
    }
  }
  FE_ASSERT_TRUE(sourceRow != UINT32_MAX);

  FE_ASSERT_TRUE(pane.renameItem(sourceRow, L"after.txt"));
  pane.shellWorkerForTest().waitForProcessedForTest(1);
  FE_ASSERT_FALSE(diskPathExists(before));
  FE_ASSERT_TRUE(diskPathExists(after));
}

// ---------------------------------------------------------------------------
// setGroupBy / groupBy / groupNow
// ---------------------------------------------------------------------------

FE_TEST_CASE(PaneController_Default_GroupByIsNone) {
  PaneController pc(nullptr);
  FE_ASSERT_EQ(static_cast<int>(pc.groupBy()),
               static_cast<int>(GroupKey::None));
}

FE_TEST_CASE(PaneController_SetGroupBy_StoresKey) {
  PaneController pc(nullptr);
  pc.setGroupBy(GroupKey::Modified);
  FE_ASSERT_EQ(static_cast<int>(pc.groupBy()),
               static_cast<int>(GroupKey::Modified));
  // Wall-clock 'now' should be non-zero after setGroupBy with non-None key.
  FE_ASSERT_TRUE(pc.groupNow() != 0);
}

FE_TEST_CASE(PaneController_SetGroupBy_None_LeavesNowMonotonic) {
  PaneController pc(nullptr);
  pc.setGroupBy(GroupKey::Modified);   // captures now
  const uint64_t firstNow = pc.groupNow();
  pc.setGroupBy(GroupKey::None);       // captures again — non-decreasing
  FE_ASSERT_EQ(static_cast<int>(pc.groupBy()),
               static_cast<int>(GroupKey::None));
  FE_ASSERT_TRUE(pc.groupNow() >= firstNow);
}
