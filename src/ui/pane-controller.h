#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "core/file-model-store.h"
#include "core/file-sort.h"
#include "core/fs-watcher.h"
#include "core/win32-fs-backend.h"

namespace fast_explorer::ui {

// Owns one pane's FileModelStore and drives DirectoryEnumerator on
// a worker jthread, posting WM_FE_ENUM_* messages to hostWindow_.
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

  // Apply a sort by the given key. If `key` matches the current sort
  // key, the direction is toggled; otherwise the sort restarts in
  // ascending direction. Returns false when an enumeration worker is
  // still running (sort would race with the worker's appends) or when
  // the store is empty; returns true after the underlying sort has
  // been applied and currentSortSpec() / hasSortApplied() reflect the
  // new state.
  bool requestSort(fast_explorer::core::SortKey key);

  fast_explorer::core::SortSpec currentSortSpec() const noexcept {
    return sortSpec_;
  }
  bool hasSortApplied() const noexcept { return sorted_; }

  bool canGoBack() const noexcept { return !backStack_.empty(); }
  bool canGoForward() const noexcept { return !forwardStack_.empty(); }

  // Test helper: block until the current worker finishes.  In
  // production the worker's lifecycle is owned by the jthread
  // destructor; tests need an explicit synchronization point to
  // assert results.
  void joinForTest();

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
  std::jthread worker_;
};

}  // namespace fast_explorer::ui
