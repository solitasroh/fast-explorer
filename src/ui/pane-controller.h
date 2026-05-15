#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "core/file-model-store.h"
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
  std::jthread worker_;
};

}  // namespace fast_explorer::ui
