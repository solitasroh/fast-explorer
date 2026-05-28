#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "core/file-grouping.h"
#include "core/file-model-store.h"
#include "core/file-sort.h"
#include "core/fs-watcher.h"
#include "core/win32-fs-backend.h"
#include "explorer/filter-pattern.h"
#include "explorer/pane-sort-coordinator.h"
#include "explorer/shell-worker.h"

namespace fast_explorer::ui {

// Owns one pane's FileModelStore and drives DirectoryEnumerator on
// a worker jthread, posting WM_FE_ENUM_* messages to hostWindow_.
// Sort-specific state and the optional background sort worker live
// in PaneSortCoordinator (composed below); methods on this class
// delegate to it.
class PaneController {
 public:
  PaneController(HWND hostWindow, std::size_t paneIndex = 0);
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

  // Activate the file or folder at the given visible row. Folders
  // navigate via openFolder; files are handed to the OS shell
  // (ShellExecuteExW "open" verb) on the calling thread. Returns
  // false on out-of-range row or when the shell call fails; folder
  // activation propagates openFolder's return value.
  bool openItem(std::uint32_t row);

  // Queue a recycle-bin delete for the file/folder at the given
  // visible row. Returns false on out-of-range row (no command
  // enqueued); true means the command is on the ShellWorker queue
  // and PerformOperations will run on the STA worker. The watcher
  // refresh path observes the resulting file-system change.
  bool deleteItem(std::uint32_t row);

  // Queue a rename of the file/folder at the given visible row to
  // newName (leaf, not a full path). Returns false on empty
  // newName or out-of-range row; true means the command is on the
  // ShellWorker queue. The watcher refresh path picks up the
  // resulting directory change.
  bool renameItem(std::uint32_t row, const std::wstring& newName);

  // Queue creation of a new folder under currentPath_ with the
  // given leaf name. Returns false on empty name or when no
  // folder is currently open. Collision policy (suffixing the
  // name to avoid overwrites) is the caller's responsibility —
  // this method takes the leaf as given.
  bool createSubfolder(const std::wstring& name);

  // Drains the ShellWorker's accumulated operation results.
  // Invoked by the UI thread after kWmFeOperationResult.
  std::vector<OperationResult> drainShellResults() {
    return shellWorker_.drainResults();
  }

  // Sort delegation. requestSort returns Rejected while the
  // enumeration worker is still running because sorting the store
  // mid-append would race; the coordinator handles the toggle vs
  // restart policy and the synchronous-vs-background split.
  SortDispatch requestSort(fast_explorer::core::SortKey key) {
    return sortCoord_.requestSort(
        key,
        workerActive_.load(std::memory_order_acquire),
        groupBy_, groupNow_);
  }

  fast_explorer::core::GroupKey groupBy() const noexcept { return groupBy_; }
  uint64_t groupNow() const noexcept { return groupNow_; }

  // Sets the grouping key, captures `now`, and triggers a re-sort with
  // the current sort spec. Returns the SortDispatch from the underlying
  // requestSort so the caller can react to async vs sync sort completion.
  SortDispatch setGroupBy(fast_explorer::core::GroupKey key);
  void applyPendingSort(std::uint32_t gen) {
    if (workerActive_.load(std::memory_order_acquire)) {
      return;
    }
    sortCoord_.applyPendingSort(gen);
  }
  // Reapplies the persisted sort spec after an enumeration finishes
  // so refresh and navigation preserve the user's chosen column +
  // direction. Caller (MainWindow::onEnumComplete) gates on the
  // enumeration worker having joined.
  void reapplyPersistedSort() {
    if (workerActive_.load(std::memory_order_acquire)) {
      return;
    }
    sortCoord_.reapplyAfterEnumeration();
  }
  fast_explorer::core::SortSpec currentSortSpec() const noexcept {
    return sortCoord_.currentSortSpec();
  }
  bool hasSortApplied() const noexcept {
    return sortCoord_.hasSortApplied();
  }

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

  struct SelectionSummary {
    std::size_t selectedCount{};
    std::uint64_t selectedBytes{};   // directories excluded
  };

  // O(N) over the selected-raw set; N is the number of currently
  // selected rows. Folders are excluded from the byte total because
  // their size field is always zero (and Explorer-parity: the
  // status bar never sums folder content for the selection).
  // Worst case: Ctrl+A on a 100k folder iterates 100k entries once
  // — sub-millisecond in practice, and the caller debounces UI
  // updates so this is invoked at most once per ~100 ms.
  // Applies (or replaces) the current filter pattern. When pattern
  // is empty (default-constructed or explicitly empty query) the
  // store's display subset is cleared and every published row is
  // exposed again. Otherwise the visibleOrder is walked, each entry
  // checked against pattern.matches(nameView(e)), and the matching
  // raw indices are pushed into the store as the new display subset.
  // Worst case O(N) over publishedCount(); the caller debounces
  // user input so a Ctrl+A-equivalent of typing does not flood.
  void setFilter(const FilterPattern& pattern) {
    if (pattern.isEmpty()) {
      clearFilter();
      currentFilter_ = pattern;
      return;
    }
    const auto& view = store_.visibleOrder();
    const std::size_t bound = store_.publishedCount();
    std::vector<std::uint32_t> subset;
    subset.reserve(bound);
    for (std::size_t i = 0; i < bound; ++i) {
      const std::uint32_t raw = view[i];
      const auto& e = store_.entryAt(raw);
      if (pattern.matches(fast_explorer::core::nameView(e))) {
        subset.push_back(raw);
      }
    }
    store_.setDisplaySubset(std::move(subset));
    currentFilter_ = pattern;
  }

  void clearFilter() noexcept {
    store_.clearDisplaySubset();
    currentFilter_ = FilterPattern{};
  }

  [[nodiscard]] bool hasActiveFilter() const noexcept {
    return !currentFilter_.isEmpty();
  }

  [[nodiscard]] const FilterPattern& currentFilter() const noexcept {
    return currentFilter_;
  }

  SelectionSummary selectionSummary() const noexcept {
    SelectionSummary out;
    out.selectedCount = selectedRaws_.size();
    const std::size_t bound = store_.publishedCount();
    for (std::uint32_t raw : selectedRaws_) {
      if (raw >= bound) continue;
      const auto& e = store_.entryAt(raw);
      if (!fast_explorer::core::isDirectory(e)) {
        out.selectedBytes += e.size;
      }
    }
    return out;
  }
  // Returns the visible-row positions of every selected raw index
  // under the current visibleOrder permutation, sorted ascending.
  // Used by MainWindow after a sort apply to reapply LVIS_SELECTED to
  // the new row positions.
  std::vector<int> selectedRowsUnderCurrentOrder() const;

  bool canGoBack() const noexcept { return !backStack_.empty(); }
  bool canGoForward() const noexcept { return !forwardStack_.empty(); }
  // True when up() would succeed — mirrors the computeParent check in
  // up() so the toolbar's enabled state matches the actual action.
  bool canGoUp() const;

  // v0.2 view toggle. When false, FILE_ATTRIBUTE_HIDDEN entries are
  // dropped during enumeration. Takes effect on the *next* navigate
  // or refresh; the caller is responsible for triggering refresh()
  // after flipping the value to surface the change immediately.
  void setIncludeHidden(bool include) noexcept { includeHidden_ = include; }
  bool includeHidden() const noexcept { return includeHidden_; }

  // Test helper: block until the current worker finishes.  In
  // production the worker's lifecycle is owned by the jthread
  // destructor; tests need an explicit synchronization point to
  // assert results.
  void joinForTest() noexcept;

  void setSortThresholdRowsForTest(std::uint32_t rows) noexcept {
    sortCoord_.setSortThresholdRowsForTest(rows);
  }

  // Exposes the ShellWorker so tests can synchronize on
  // waitForProcessedForTest after a deleteItem/renameItem/createSubfolder
  // call. Returned by const reference so tests cannot enqueue
  // arbitrary commands behind the controller's back.
  const ShellWorker& shellWorkerForTest() const noexcept {
    return shellWorker_;
  }

  uint32_t generation() const noexcept;
  const std::wstring& currentPath() const noexcept { return currentPath_; }
  const fast_explorer::core::FileModelStore& store() const noexcept {
    return store_;
  }
  HWND hostWindow() const noexcept { return hostWindow_; }
  std::size_t paneIndex() const noexcept { return paneIndex_; }

 private:
  bool navigateInternal(const std::wstring& path);

  // Validates `row` against publishedCount() and writes the
  // absolute path of the visible entry at that row to `out`.
  // Returns false on out-of-range row (out is left untouched).
  // Shared by openItem / deleteItem / renameItem to keep the
  // row-to-source-path lookup in one place.
  bool resolveRowSourcePath(std::uint32_t row, std::wstring& out) const;

  // Declared in destruction-reverse order so worker_ joins (via its
  // std::jthread destructor) before sortCoord_, backend_, or store_
  // go away. sortCoord_ in turn joins its own sort worker before
  // releasing its reference to store_.
  HWND hostWindow_;
  std::size_t paneIndex_;
  std::wstring currentPath_;
  std::vector<std::wstring> backStack_;
  std::vector<std::wstring> forwardStack_;
  fast_explorer::core::FileModelStore store_;
  fast_explorer::core::Win32FsBackend backend_;
  fast_explorer::core::FsWatcher fsWatcher_;
  bool includeHidden_ = true;  // T8 view toggle, see setIncludeHidden
  // True while the enumeration worker thread is still appending to
  // store_. The sort coordinator reads this through requestSort's
  // enumerationActive flag to refuse sorts that would race append.
  std::atomic<bool> workerActive_{false};
  std::unordered_set<std::uint32_t> selectedRaws_;
  FilterPattern currentFilter_;
  PaneSortCoordinator sortCoord_;
  ShellWorker shellWorker_;
  fast_explorer::core::GroupKey groupBy_ =
      fast_explorer::core::GroupKey::None;
  uint64_t groupNow_ = 0;
  std::jthread worker_;
};

}  // namespace fast_explorer::ui
