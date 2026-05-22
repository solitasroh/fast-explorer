#include "test-harness.h"

#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>
#include <string_view>
#include <vector>

#include "core/file-entry.h"
#include "core/file-grouping.h"
#include "core/file-model-store.h"
#include "core/file-sort.h"
#include "core/name-arena.h"
#include "ui/pane-sort-coordinator.h"

using fast_explorer::core::FileEntry;
using fast_explorer::core::FileModelStore;
using fast_explorer::core::GroupKey;
using fast_explorer::core::NameArena;
using fast_explorer::core::SortDirection;
using fast_explorer::core::SortKey;
using fast_explorer::ui::PaneSortCoordinator;
using fast_explorer::ui::SortDispatch;

namespace {

constexpr int kSyncDatasetSize = 100;
constexpr std::uint32_t kBackgroundThreshold = 50;
constexpr std::uint32_t kLargeThreshold = 1000;
constexpr std::size_t kNameBufLen = 32;

FileEntry makeEntry(std::wstring_view name, NameArena& backing,
                    uint64_t size = 0) {
  const auto interned = backing.intern(name);
  FileEntry e{};
  e.namePtr = interned.data();
  e.nameLength = static_cast<uint16_t>(interned.size());
  e.extensionOffset = fast_explorer::core::kNoExtension;
  e.size = size;
  return e;
}

void fillStore(FileModelStore& store, NameArena& backing, int count) {
  for (int i = 0; i < count; ++i) {
    wchar_t buf[kNameBufLen];
    std::swprintf(buf, kNameBufLen, L"file_%05d.txt",
                  static_cast<int>((static_cast<unsigned>(i) * 31337u) %
                                   static_cast<unsigned>(count)));
    store.appendEntry(makeEntry(buf, backing, static_cast<uint64_t>(i)));
  }
  store.publish(static_cast<std::uint32_t>(store.itemCount()));
}

// Bundles the store, the name arena that backs it, and the
// coordinator under test. Default-populates with kSyncDatasetSize so
// the common sync-path tests can construct one line and start.
struct SortFixture {
  FileModelStore store{L"X:\\d"};
  NameArena backing;
  PaneSortCoordinator coord{store, nullptr};

  explicit SortFixture(int populate = kSyncDatasetSize) {
    if (populate > 0) {
      fillStore(store, backing, populate);
    }
  }
};

}  // namespace

FE_TEST_CASE(PaneSortCoordinator_Default_HasNoSortApplied) {
  SortFixture fx(0);
  FE_ASSERT_FALSE(fx.coord.hasSortApplied());
}

FE_TEST_CASE(PaneSortCoordinator_RequestSort_EnumerationActive_Rejected) {
  SortFixture fx;
  FE_ASSERT_EQ(
      static_cast<int>(fx.coord.requestSort(SortKey::Name, /*enumerationActive=*/true)),
      static_cast<int>(SortDispatch::Rejected));
  FE_ASSERT_FALSE(fx.coord.hasSortApplied());
}

FE_TEST_CASE(PaneSortCoordinator_RequestSort_EmptyStore_Rejected) {
  SortFixture fx(0);
  FE_ASSERT_EQ(static_cast<int>(fx.coord.requestSort(SortKey::Name, false)),
               static_cast<int>(SortDispatch::Rejected));
}

FE_TEST_CASE(PaneSortCoordinator_RequestSort_AppliesSyncBelowThreshold) {
  SortFixture fx;
  FE_ASSERT_EQ(static_cast<int>(fx.coord.requestSort(SortKey::Name, false)),
               static_cast<int>(SortDispatch::AppliedSync));
  FE_ASSERT_TRUE(fx.coord.hasSortApplied());
  FE_ASSERT_EQ(static_cast<int>(fx.coord.currentSortSpec().key),
               static_cast<int>(SortKey::Name));
  FE_ASSERT_EQ(static_cast<int>(fx.coord.currentSortSpec().direction),
               static_cast<int>(SortDirection::Ascending));
}

FE_TEST_CASE(PaneSortCoordinator_RequestSort_SameKey_TogglesDirection) {
  SortFixture fx;
  fx.coord.requestSort(SortKey::Name, false);
  FE_ASSERT_EQ(static_cast<int>(fx.coord.currentSortSpec().direction),
               static_cast<int>(SortDirection::Ascending));
  fx.coord.requestSort(SortKey::Name, false);
  FE_ASSERT_EQ(static_cast<int>(fx.coord.currentSortSpec().direction),
               static_cast<int>(SortDirection::Descending));
  fx.coord.requestSort(SortKey::Name, false);
  FE_ASSERT_EQ(static_cast<int>(fx.coord.currentSortSpec().direction),
               static_cast<int>(SortDirection::Ascending));
}

FE_TEST_CASE(PaneSortCoordinator_RequestSort_DifferentKey_ResetsToAscending) {
  SortFixture fx;
  fx.coord.requestSort(SortKey::Name, false);
  fx.coord.requestSort(SortKey::Name, false);  // now Descending
  fx.coord.requestSort(SortKey::Size, false);
  FE_ASSERT_EQ(static_cast<int>(fx.coord.currentSortSpec().key),
               static_cast<int>(SortKey::Size));
  FE_ASSERT_EQ(static_cast<int>(fx.coord.currentSortSpec().direction),
               static_cast<int>(SortDirection::Ascending));
}

FE_TEST_CASE(PaneSortCoordinator_Background_DispatchesAndAppliesOnDemand) {
  SortFixture fx;
  fx.coord.setSortThresholdRowsForTest(kBackgroundThreshold);
  FE_ASSERT_EQ(static_cast<int>(fx.coord.requestSort(SortKey::Name, false)),
               static_cast<int>(SortDispatch::Dispatched));
  FE_ASSERT_FALSE(fx.coord.hasSortApplied());
  fx.coord.applyPendingSort(fx.store.generation());
  FE_ASSERT_TRUE(fx.coord.hasSortApplied());
}

FE_TEST_CASE(PaneSortCoordinator_ApplyPendingSort_StaleGen_DropsResult) {
  SortFixture fx;
  fx.coord.setSortThresholdRowsForTest(kBackgroundThreshold);
  fx.coord.requestSort(SortKey::Name, false);
  fx.coord.applyPendingSort(fx.store.generation() + 999);
  FE_ASSERT_FALSE(fx.coord.hasSortApplied());
}

FE_TEST_CASE(PaneSortCoordinator_Cancel_ResetsApplied) {
  SortFixture fx;
  fx.coord.requestSort(SortKey::Name, false);
  FE_ASSERT_TRUE(fx.coord.hasSortApplied());
  fx.coord.cancel();
  FE_ASSERT_FALSE(fx.coord.hasSortApplied());
}

FE_TEST_CASE(PaneSortCoordinator_SyncAfterDispatch_DropsStalePending) {
  // Regression guard: requestSort's sync branch must scrub any
  // pendingSortedOrder leftover from a previous background dispatch,
  // otherwise a stale kWmFeSortComplete would overwrite the sync
  // sort's visibleOrder.
  SortFixture fx;
  fx.coord.setSortThresholdRowsForTest(kBackgroundThreshold);
  FE_ASSERT_EQ(static_cast<int>(fx.coord.requestSort(SortKey::Name, false)),
               static_cast<int>(SortDispatch::Dispatched));
  const std::uint32_t dispatchedGen = fx.store.generation();

  fx.coord.setSortThresholdRowsForTest(kLargeThreshold);
  FE_ASSERT_EQ(static_cast<int>(fx.coord.requestSort(SortKey::Size, false)),
               static_cast<int>(SortDispatch::AppliedSync));
  FE_ASSERT_EQ(static_cast<int>(fx.coord.currentSortSpec().key),
               static_cast<int>(SortKey::Size));

  // Late stale dispatch arrives: the spec must still reflect the
  // sync Size sort, not the prior Name dispatch.
  fx.coord.applyPendingSort(dispatchedGen);
  FE_ASSERT_EQ(static_cast<int>(fx.coord.currentSortSpec().key),
               static_cast<int>(SortKey::Size));
}

FE_TEST_CASE(sort_coord_group_by_name_clusters_by_choseong) {
  SortFixture fx(0);  // empty store; fill manually
  for (auto name : {L"Banana", L"가나", L"하늘", L"Apple", L"강물", L"Apricot"}) {
    fx.store.appendEntry(makeEntry(name, fx.backing));
  }
  fx.store.publish(static_cast<std::uint32_t>(fx.store.itemCount()));

  FE_ASSERT_EQ(
      static_cast<int>(fx.coord.requestSort(
          SortKey::Name, false, GroupKey::Name, 0)),
      static_cast<int>(SortDispatch::AppliedSync));

  std::vector<std::wstring> names;
  for (std::size_t i = 0; i < fx.store.publishedCount(); ++i) {
    const auto& e = fx.store.visibleAt(i);
    names.emplace_back(e.namePtr, e.nameLength);
  }
  // First two: group 0 (가/강), then group 18 (하), then group 19
  // (Apple/Apricot, both ㄱ-equivalent ASCII 'A'), then group 20 (Banana, 'B').
  FE_ASSERT_TRUE(names[0] == L"가나" || names[0] == L"강물");
  FE_ASSERT_TRUE(names[1] == L"가나" || names[1] == L"강물");
  FE_ASSERT_WSTREQ(names[2], L"하늘");
  FE_ASSERT_WSTREQ(names[3], L"Apple");
  FE_ASSERT_WSTREQ(names[4], L"Apricot");
  FE_ASSERT_WSTREQ(names[5], L"Banana");
}

FE_TEST_CASE(sort_coord_reapplyAfterEnumeration_preserves_groupBy) {
  SortFixture fx(0);
  for (auto name : {L"Banana", L"가나", L"하늘", L"Apple"}) {
    fx.store.appendEntry(makeEntry(name, fx.backing));
  }
  fx.store.publish(static_cast<std::uint32_t>(fx.store.itemCount()));
  // Initial grouped sort.
  fx.coord.requestSort(SortKey::Name, false, GroupKey::Name, 0);
  // Simulate re-enumeration: caller would have cleared + repopulated the
  // store, but our entries are stable. Just call reapply.
  fx.coord.reapplyAfterEnumeration();
  std::vector<std::wstring> names;
  for (std::size_t i = 0; i < fx.store.publishedCount(); ++i) {
    const auto& e = fx.store.visibleAt(i);
    names.emplace_back(e.namePtr, e.nameLength);
  }
  // Group 0 (가) first, then group 18 (하), then group 19 (Apple),
  // then group 20 (Banana).
  FE_ASSERT_WSTREQ(names[0], L"가나");
  FE_ASSERT_WSTREQ(names[1], L"하늘");
  FE_ASSERT_WSTREQ(names[2], L"Apple");
  FE_ASSERT_WSTREQ(names[3], L"Banana");
}
