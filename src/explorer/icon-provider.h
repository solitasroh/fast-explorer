#pragma once

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "explorer/result-channel.h"

namespace fast_explorer::ui {

// Background icon resolver. Owns an STA jthread that pulls
// extension-resolution requests off a thread-safe queue. The actual
// SHGetFileInfoW lookup and PostMessage-back-to-UI hook arrives in a
// follow-up atom; this skeleton stands up the worker lifecycle
// (COM apartment init, stop-token-aware wait, dequeue, dummy
// processing) so the integration atom can drop the shell call into
// a single well-typed seam.
//
// Threading:
// - request() is callable from the UI thread.
// - The worker thread owns the COM apartment (CoInitializeEx with
//   COINIT_APARTMENTTHREADED) for the lifetime of the std::jthread.
// - processed_/processedForTest() are atomic so test code can wait
//   on completion without a private synchronization channel.
class IconProvider {
 public:
  IconProvider(HWND host, std::size_t paneIndex = 0);
  ~IconProvider();

  IconProvider(const IconProvider&) = delete;
  IconProvider& operator=(const IconProvider&) = delete;
  IconProvider(IconProvider&&) = delete;
  IconProvider& operator=(IconProvider&&) = delete;

  // A resolved icon ready for the UI to insert into its HIMAGELIST.
  // The UI thread owns the HICON once drainResults returns it and
  // must call DestroyIcon (after ImageList_AddIcon copies the bits)
  // or the OS will leak the icon handle. Made move-only so a stray
  // copy cannot leave two IconResults claiming the same HICON.
  struct IconResult {
    std::wstring extension;
    HICON icon = nullptr;

    IconResult() = default;
    IconResult(std::wstring ext, HICON h) noexcept
        : extension(std::move(ext)), icon(h) {}
    IconResult(const IconResult&) = delete;
    IconResult& operator=(const IconResult&) = delete;
    IconResult(IconResult&& other) noexcept
        : extension(std::move(other.extension)), icon(other.icon) {
      other.icon = nullptr;
    }
    IconResult& operator=(IconResult&& other) noexcept {
      if (this != &other) {
        extension = std::move(other.extension);
        icon = other.icon;
        other.icon = nullptr;
      }
      return *this;
    }
  };

  // Queues an extension for the worker to resolve. Safe to call from
  // any thread.
  void request(std::wstring extension);

  // UI-thread: removes every resolved result that the worker has
  // posted so far and returns them in arrival order. Call this when
  // the host receives kWmFeIconBatch.
  std::vector<IconResult> drainResults() {
    return results_.drainResults();
  }

  // Returns how many requests the worker has dequeued and finished
  // its dummy-processing pass over. Acquire-load semantics.
  std::size_t processedForTest() const noexcept {
    return processed_.load(std::memory_order_acquire);
  }

  // Test helper: blocks until processedForTest() >= expected. Uses
  // std::atomic::wait so the test does not busy-loop. Not for use
  // from production UI code — request() is fire-and-forget there.
  void waitForProcessedForTest(std::size_t expected) const noexcept;

 private:
  void workerMain(std::stop_token tok);
  // Worker-side helpers. processOne splits into the shell call
  // (resolveIconForExtension) and the result publication so each
  // path has a single reason to change.
  std::optional<std::wstring> dequeueOne(std::stop_token tok);
  void processOne(const std::wstring& extension);
  static HICON resolveIconForExtension(
      const std::wstring& extension) noexcept;

  // Queue + mutex_ guard pendingRequests_. cv_ is woken either by a
  // new request() or by the jthread's stop signal. The
  // condition_variable_any overload that takes a stop_token returns
  // promptly when the worker is being torn down.
  std::queue<std::wstring> pendingRequests_;
  mutable std::mutex mutex_;
  std::condition_variable_any cv_;
  ResultChannel<IconResult> results_;
  std::atomic<std::size_t> processed_{0};
  std::jthread worker_;
};

}  // namespace fast_explorer::ui
