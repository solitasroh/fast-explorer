#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "core/file-model-store.h"
#include "core/file-sort.h"
#include "core/fs-watcher.h"
#include "core/win32-fs-backend.h"

namespace fast_explorer::ui {

// Outcome of a requestSort() call. The two success cases require
// different UI follow-up: AppliedSync means the store is already in
// the new order and the caller can repaint immediately; Dispatched
// means a background worker accepted the request and the caller must
// wait for kWmFeSortComplete before reading store.visibleAt.
enum class SortDispatch : uint8_t {
  Rejected = 0,
  AppliedSync,
  Dispatched,
};

// Owns one pane's FileModelStore and drives DirectoryEnumerator on
// a worker jthread, posting WM_FE_ENUM_* messages to hostWindow_.
class PaneController {
 public:
  // Item count at and above which requestSort runs on a background
  // worker thread; smaller stores sort synchronously on the UI thread
  // because the worker hand-off overhead would dominate the sort.
  static constexpr std::uint32_t kDefaultSortThresholdRows = 2000;

  explicit PaneController(HWND hostWindow);
  ~PaneController();

  PaneController(const PaneController&) = delete;
  PaneController& operator=(const PaneController&) = delete;
  PaneController(PaneController&&) = delete;
  PaneController& operator=(PaneController&&) = delete;

  // Validates `path`, cancels any in-flight worker, resets the
  // underlying store, then spawns a new worker thread.  Pushes the
  // current path onto the back stack and clears the forward stack
  // on success.  Returns false on invalid path (empty / relative /
  // UNC / etc.) and leaves the controller unchanged.
  bool openFolder(const std::wstring& path);

  // Navigate back / forward through the in-pane history.  Returns
  // false when the respective stack is empty.
  bool back();
  bool forward();

  // Navigate to the parent of currentPath_.  Returns false when the
  // path has no addressable parent (drive root, empty).
  bool up();

  // Re-enumerate currentPath_ without touching the history stacks.
  // Used by F5 and the coalesced fs-change refresh.
  bool refresh();

  // Apply a sort by the given key. If `key` matches the current sort
  // key, the direction is toggled; otherwise the sort restarts in
  // ascending direction. Returns Rejected when an enumeration worker
  // is still running (sort would race with the worker's appends) or
  // when the store is empty.
  //
  // Below `sortThresholdRows_` the sort runs synchronously on the UI
  // thread (returns AppliedSync; hasSortApplied() is true). At or
  // above it the sort is dispatched to a background worker (returns
  // Dispatched) and the result becomes visible only after the host
  // receives kWmFeSortComplete and calls applyPendingSort().
  SortDispatch requestSort(fast_explorer::core::SortKey key);

  // UI-thread entry point for committing a background-sort result.
  // Joins the sort worker if it is still running and swaps the
  // pending permutation into the store's visibleOrder_. `gen` is the
  // store generation snapshot the sort worker captured; mismatched
  // generations (e.g. navigation happened between the worker post
  // and the UI dispatch) cause the pending order to be discarded.
  // The caller (MainWindow) invokes this on kWmFeSortComplete and
  // is responsible for the subsequent ListView_RedrawItems.
  void applyPendingSort(std::uint32_t gen);

  fast_explorer::core::SortSpec currentSortSpec() const noexcept {
    return sortSpec_;
  }
  bool hasSortApplied() const noexcept { return sorted_; }

  // Selection is tracked by raw entries_ index so the selection
  // survives sort reorderings: a row may move to a different
  // visibleOrder slot but the underlying FileEntry keeps the same
  // raw index until the next navigation. openFolder / back / forward /
  // up / refresh clear the selection (the underlying entries are
  // replaced by reset()), so callers do not need to manage that.
  void selectRaw(std::uint32_t rawIndex);
  void deselectRaw(std::uint32_t rawIndex) noexcept;
  void clearSelection() noexcept;
  bool isRawSelected(std::uint32_t rawIndex) const noexcept;
  std::size_t selectedCount() const noexcept {
    return selectedRaws_.size();
  }
  // Returns the visible-row positions of every selected raw index
  // under the current visibleOrder permutation, sorted ascending.
  // Used by MainWindow after a sort apply to reapply LVIS_SELECTED to
  // the new row positions.
  std::vector<int> selectedRowsUnderCurrentOrder() const;

  bool canGoBack() const noexcept { return !backStack_.empty(); }
  bool canGoForward() const noexcept { return !forwardStack_.empty(); }

  // Test helper: block until the current worker finishes.  In
  // production the worker's lifecycle is owned by the jthread
  // destructor; tests need an explicit synchronization point to
  // assert results.
  void joinForTest() noexcept;

  // Test helper: lowers the sort threshold so a small dataset can
  // exercise the background path. Has no production caller — the
  // sortThresholdRows_ field defaults to kDefaultSortThresholdRows
  // and is set this way only from the test suite.
  void setSortThresholdRowsForTest(std::uint32_t rows) noexcept {
    sortThresholdRows_ = rows;
  }

  uint32_t generation() const noexcept;
  const std::wstring& currentPath() const noexcept { return currentPath_; }
  const fast_explorer::core::FileModelStore& store() const noexcept {
    return store_;
  }
  HWND hostWindow() const noexcept { return hostWindow_; }

 private:
  bool navigateInternal(const std::wstring& path);

  // Declared in destruction-reverse order so worker_ joins (via its
  // std::jthread destructor) before backend_ / store_ go away.
  HWND hostWindow_;
  std::wstring currentPath_;
  std::vector<std::wstring> backStack_;
  std::vector<std::wstring> forwardStack_;
  fast_explorer::core::FileModelStore store_;
  fast_explorer::core::Win32FsBackend backend_;
  fast_explorer::core::FsWatcher fsWatcher_;
  // True while the enumeration worker thread is still appending to
  // store_. requestSort() bails out in that window because sort()
  // mutates visibleOrder_ on the UI thread while the worker is
  // mutating entries_ — std::vector offers no synchronization there.
  std::atomic<bool> workerActive_{false};
  fast_explorer::core::SortSpec sortSpec_{
      fast_explorer::core::SortKey::Name,
      fast_explorer::core::SortDirection::Ascending};
  bool sorted_ = false;
  std::uint32_t sortThresholdRows_ = kDefaultSortThresholdRows;
  // pendingSortedOrder_ and pendingSortGen_ are written by sortWorker_
  // and read by applyPendingSort() on the UI thread. They MUST be
  // declared before sortWorker_ so destruction joins the thread
  // before the data it captures is destroyed; the same invariant is
  // why worker_ is declared last.
  std::vector<std::uint32_t> pendingSortedOrder_;
  std::uint32_t pendingSortGen_ = 0;
  std::unordered_set<std::uint32_t> selectedRaws_;
  std::jthread sortWorker_;
  std::jthread worker_;
};

}  // namespace fast_explorer::ui
