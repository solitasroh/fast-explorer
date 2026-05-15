#pragma once

#include <windows.h>

#include <cstdint>
#include <thread>
#include <vector>

#include "core/file-model-store.h"
#include "core/file-sort.h"

namespace fast_explorer::ui {

enum class SortDispatch : uint8_t {
  Rejected = 0,
  AppliedSync,
  Dispatched,
};

// Sorting policy and background worker for one pane. Owns the sort
// spec, the threshold, the optional std::jthread that runs large
// sorts off the UI thread, and the pending permutation the worker
// hands back to the UI. The store is held by reference because its
// lifetime is owned by PaneController; this object must be declared
// in the owner so that ~PaneSortCoordinator joins the worker before
// the store goes away.
class PaneSortCoordinator {
 public:
  // Below this count the sync sort completes well inside the UI 50 ms
  // budget, so the jthread spin-up cost is not worth paying.
  static constexpr std::uint32_t kDefaultSortThresholdRows = 2000;

  PaneSortCoordinator(fast_explorer::core::FileModelStore& store,
                      HWND hostWindow);
  ~PaneSortCoordinator();

  PaneSortCoordinator(const PaneSortCoordinator&) = delete;
  PaneSortCoordinator& operator=(const PaneSortCoordinator&) = delete;
  PaneSortCoordinator(PaneSortCoordinator&&) = delete;
  PaneSortCoordinator& operator=(PaneSortCoordinator&&) = delete;

  // Apply a sort by the given key. enumerationActive must be true if
  // the pane's enumeration worker is still appending to the store —
  // in that case the sort is rejected because store mutation would
  // race the worker. Below sortThresholdRows_ the sort runs
  // synchronously and AppliedSync is returned; at or above it the
  // sort runs on sortWorker_ and Dispatched is returned, with the
  // result becoming visible only after applyPendingSort(gen) commits
  // it on the UI thread (typically from kWmFeSortComplete).
  SortDispatch requestSort(fast_explorer::core::SortKey key,
                           bool enumerationActive);

  // UI-thread commit of a pending background sort. Joins sortWorker_,
  // validates that the captured generation matches the live store
  // generation (so navigation between worker post and dispatch
  // discards the stale result), then swaps the permutation into
  // store_.visibleOrder_.
  void applyPendingSort(std::uint32_t gen);

  fast_explorer::core::SortSpec currentSortSpec() const noexcept {
    return sortSpec_;
  }
  bool hasSortApplied() const noexcept { return sorted_; }

  // Aborts any in-flight sort and discards the pending permutation.
  // Used by the pane controller before resetting the store so the
  // sort worker does not read entries_ that are about to be cleared.
  void cancel() noexcept;

  // Test seam: lowers the threshold so a small dataset can hit the
  // background path. No production caller.
  void setSortThresholdRowsForTest(std::uint32_t rows) noexcept {
    sortThresholdRows_ = rows;
  }

 private:
  fast_explorer::core::FileModelStore& store_;
  HWND host_;
  fast_explorer::core::SortSpec sortSpec_{
      fast_explorer::core::SortKey::Name,
      fast_explorer::core::SortDirection::Ascending};
  bool sorted_ = false;
  std::uint32_t sortThresholdRows_ = kDefaultSortThresholdRows;
  // Written by sortWorker_ when it finishes a background sort; read
  // by applyPendingSort on the UI thread. Synchronization is the
  // jthread::join() inside applyPendingSort, plus the requestSort
  // entry guard that clears any leftover from a prior dispatch.
  std::vector<std::uint32_t> pendingSortedOrder_;
  std::uint32_t pendingSortGen_ = 0;
  std::jthread sortWorker_;
};

}  // namespace fast_explorer::ui
