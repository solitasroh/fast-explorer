#pragma once

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

#include "explorer/messages.h"

namespace fast_explorer::ui {

// Coalesced result channel from a background worker to a host
// window. Threading: workers call publish(T) from any thread;
// the UI thread calls drainResults() after observing the
// configured PostMessage. publish() pushes the result and gates
// a single PostMessage per accumulated batch — drainResults()
// clears the gate inside the same mutex critical section that
// swaps out the queue, sealing the worker-publish-between-swap-
// and-clear race.
//
// Template parameter T must be move-constructible. The channel
// does not own any per-item resources; if T owns a handle
// (HICON, etc.) the caller drains in their own dtor.
template <class T>
class ResultChannel {
 public:
  ResultChannel(HWND host, UINT message, std::size_t paneIndex = 0) noexcept
      : host_(host), message_(message), paneIndex_(paneIndex) {}

  ResultChannel(const ResultChannel&) = delete;
  ResultChannel& operator=(const ResultChannel&) = delete;
  ResultChannel(ResultChannel&&) = delete;
  ResultChannel& operator=(ResultChannel&&) = delete;

  // Rvalue-only sink: lvalues are intentionally rejected so a
  // copyable payload holding an owned handle cannot be silently
  // duplicated into the channel and then again into the drain.
  void publish(T&& item) {
    {
      std::lock_guard lk(mutex_);
      ready_.push_back(std::move(item));
    }
    bool expected = false;
    if (postPending_.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
      if (host_ != nullptr) {
        PostMessageW(host_, message_, makePaneWParam(paneIndex_, 0), 0);
      } else {
        postPending_.store(false, std::memory_order_release);
      }
    }
  }

  std::vector<T> drainResults() {
    std::vector<T> out;
    std::lock_guard lk(mutex_);
    out.swap(ready_);
    postPending_.store(false, std::memory_order_release);
    return out;
  }

  // Test-only snapshot of the pending count. Acquires the mutex.
  std::size_t pendingForTest() const {
    std::lock_guard lk(mutex_);
    return ready_.size();
  }

 private:
  HWND host_;
  UINT message_;
  std::size_t paneIndex_;
  std::vector<T> ready_;
  mutable std::mutex mutex_;
  std::atomic<bool> postPending_{false};
};

}  // namespace fast_explorer::ui
