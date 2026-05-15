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
  explicit IconProvider(HWND host);
  ~IconProvider();

  IconProvider(const IconProvider&) = delete;
  IconProvider& operator=(const IconProvider&) = delete;
  IconProvider(IconProvider&&) = delete;
  IconProvider& operator=(IconProvider&&) = delete;

  // Queues an extension for the worker to resolve. Safe to call from
  // any thread.
  void request(std::wstring extension);

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
  // Worker-side helpers. Splitting them up keeps workerMain a thin
  // loop so the follow-up atom can drop the real SHGetFileInfoW
  // call straight into processOne without re-growing this function.
  std::optional<std::wstring> dequeueOne(std::stop_token tok);
  void processOne(const std::wstring& extension);

  HWND host_;
  // Queue + mutex_ guard pendingRequests_. cv_ is woken either by a
  // new request() or by the jthread's stop signal. The
  // condition_variable_any overload that takes a stop_token returns
  // promptly when the worker is being torn down.
  std::queue<std::wstring> pendingRequests_;
  mutable std::mutex mutex_;
  std::condition_variable_any cv_;
  std::atomic<std::size_t> processed_{0};
  std::jthread worker_;
};

}  // namespace fast_explorer::ui
