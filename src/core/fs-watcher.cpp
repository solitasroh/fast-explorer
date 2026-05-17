#include "core/fs-watcher.h"

#include <stop_token>

#include "core/path-utils.h"

namespace fast_explorer::core {

FsWatcher::~FsWatcher() {
  stop();
}

bool FsWatcher::watch(const std::wstring& path, HWND target, UINT message,
                      std::size_t paneIndex) {
  stop();

  std::wstring internal;
  if (toInternal(path, internal) != PathConvertError::None) {
    return false;
  }

  dirHandle_ = CreateFileW(
      internal.c_str(), FILE_LIST_DIRECTORY,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
  if (dirHandle_ == INVALID_HANDLE_VALUE) {
    return false;
  }

  iocp_ = CreateIoCompletionPort(dirHandle_, nullptr, 0, 1);
  if (iocp_ == nullptr) {
    CloseHandle(dirHandle_);
    dirHandle_ = INVALID_HANDLE_VALUE;
    return false;
  }

  target_ = target;
  message_ = message;
  paneIndex_ = paneIndex;
  worker_ = std::jthread([this](std::stop_token tok) { workerLoop(tok); });
  return true;
}

void FsWatcher::stop() noexcept {
  if (worker_.joinable()) {
    worker_.request_stop();
    if (dirHandle_ != INVALID_HANDLE_VALUE) {
      CancelIoEx(dirHandle_, nullptr);
    }
    if (iocp_ != nullptr) {
      // Synthetic completion to unblock GetQueuedCompletionStatus.
      PostQueuedCompletionStatus(iocp_, 0, 0, nullptr);
    }
    worker_.join();
  }
  if (dirHandle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(dirHandle_);
    dirHandle_ = INVALID_HANDLE_VALUE;
  }
  if (iocp_ != nullptr) {
    CloseHandle(iocp_);
    iocp_ = nullptr;
  }
  target_ = nullptr;
  message_ = 0;
  paneIndex_ = 0;
}

void FsWatcher::workerLoop(std::stop_token tok) {
  alignas(8) BYTE buffer[8192];
  OVERLAPPED overlapped{};
  constexpr DWORD kNotifyFilter =
      FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
      FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;

  while (!tok.stop_requested()) {
    // Re-zero the OVERLAPPED so the kernel-written Internal /
    // InternalHigh fields from the previous completion do not leak
    // into the next ReadDirectoryChangesW call.
    overlapped = OVERLAPPED{};
    DWORD bytesReturned = 0;
    const BOOL ok = ReadDirectoryChangesW(dirHandle_, buffer, sizeof(buffer),
                                          FALSE, kNotifyFilter, &bytesReturned,
                                          &overlapped, nullptr);
    if (!ok && GetLastError() != ERROR_IO_PENDING) {
      break;
    }

    DWORD bytes = 0;
    ULONG_PTR key = 0;
    OVERLAPPED* completedOverlapped = nullptr;
    const BOOL completed =
        GetQueuedCompletionStatus(iocp_, &bytes, &key, &completedOverlapped,
                                  INFINITE);
    if (!completed) {
      break;  // ERROR_OPERATION_ABORTED on stop, or genuine error
    }
    if (completedOverlapped == nullptr) {
      break;  // synthetic wakeup from stop()
    }
    if (tok.stop_requested()) {
      break;
    }
    if (bytes > 0 && target_ != nullptr) {
      // WPARAM packing mirrors ui/messages.h::makePaneWParam (low 32
      // bits = generation = 0 for fs-change, high 8 bits at offset 32
      // = paneIndex). Inlined here so the core/ layer does not depend
      // on ui/.
      const WPARAM wp = (static_cast<UINT_PTR>(paneIndex_ & 0xFFu) << 32);
      PostMessageW(target_, message_, wp, 0);
    }
  }
}

}  // namespace fast_explorer::core
