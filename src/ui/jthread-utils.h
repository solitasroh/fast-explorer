#pragma once

#include <thread>

namespace fast_explorer::ui {

// Idempotently retires a jthread: signals stop_token then joins.
// Used wherever a worker captures pane state that is about to be
// replaced — joining without the stop signal first could leave the
// worker running through state the caller already considered void.
inline void stopAndJoin(std::jthread& thread) noexcept {
  if (thread.joinable()) {
    thread.request_stop();
    thread.join();
  }
}

}  // namespace fast_explorer::ui
