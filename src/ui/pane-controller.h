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
#include "ui/pane-sort-coordinator.h"
#include "ui/shell-worker.h"

namespace fast_explorer::ui {

// Owns one pane's FileModelStore and drives DirectoryEnumerator on
// a worker jthread, posting WM_FE_ENUM_* messages to hostWindow_.
// Sort-specific state and the optional background sort worker live
// in PaneSortCoordinator (composed below); methods on this class
// delegate to it.
class PaneController {
 public:
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

  // Sort delegation. requestSort returns Rejected while the
  // enumeration worker is still running because sorting the store
  // mid-append would race; the coordinator handles the toggle vs
  // restart policy and the synchronous-vs-background split.
  SortDispatch requestSort(fast_explorer::core::SortKey key) {
    return sortCoord_.requestSort(
        key, workerActive_.load(std::memory_order_acquire));
  }
  void applyPendingSort(std::uint32_t gen) {
    if (workerActive_.load(std::memory_order_acquire)) {
      return;
    }
    sortCoord_.applyPendingSort(gen);
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
  std::wstring currentPath_;
  std::vector<std::wstring> backStack_;
  std::vector<std::wstring> forwardStack_;
  fast_explorer::core::FileModelStore store_;
  fast_explorer::core::Win32FsBackend backend_;
  fast_explorer::core::FsWatcher fsWatcher_;
  // True while the enumeration worker thread is still appending to
  // store_. The sort coordinator reads this through requestSort's
  // enumerationActive flag to refuse sorts that would race append.
  std::atomic<bool> workerActive_{false};
  std::unordered_set<std::uint32_t> selectedRaws_;
  PaneSortCoordinator sortCoord_;
  ShellWorker shellWorker_;
  std::jthread worker_;
};

}  // namespace fast_explorer::ui
