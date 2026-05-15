#pragma once

#include <windows.h>

#include <string>
#include <thread>

namespace fast_explorer::core {

// Background watcher around ReadDirectoryChangesW + IOCP.  On any
// change to the watched directory, posts `message` to `target`.
// Coalescing and event parsing are deliberately omitted at this layer;
// callers receive a "something changed, re-evaluate" signal.
//
// Threading: watch() / stop() are UI-thread-only.  The internal worker
// owns dirHandle_ + iocp_ for the duration of the watch.
class FsWatcher {
 public:
  FsWatcher() = default;
  ~FsWatcher();

  FsWatcher(const FsWatcher&) = delete;
  FsWatcher& operator=(const FsWatcher&) = delete;
  FsWatcher(FsWatcher&&) = delete;
  FsWatcher& operator=(FsWatcher&&) = delete;

  // Stops any previous watch, then arms a new one on `path`.  Returns
  // false on Win32 errors (path not openable, IOCP create failed,
  // ReadDirectoryChangesW failed); the watcher returns to the stopped
  // state on failure.  `path` must already be in the display form
  // accepted by core::toInternal (drive-letter root, no relative or
  // UNC).
  bool watch(const std::wstring& path, HWND target, UINT message);

  // Cancels pending I/O, unblocks GetQueuedCompletionStatus, joins the
  // worker, closes the directory + IOCP handles.  Idempotent.
  void stop() noexcept;

 private:
  void workerLoop(std::stop_token tok);

  HWND target_ = nullptr;
  UINT message_ = 0;
  HANDLE dirHandle_ = INVALID_HANDLE_VALUE;
  HANDLE iocp_ = nullptr;
  std::jthread worker_;
};

}  // namespace fast_explorer::core
