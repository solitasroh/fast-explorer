#include "ui/shell-worker.h"

#include <objbase.h>

#include <utility>

namespace fast_explorer::ui {

ShellWorker::ShellWorker(HWND host)
    : host_(host),
      worker_([this](std::stop_token tok) { workerMain(tok); }) {}

ShellWorker::~ShellWorker() {
  // Stop and join the worker explicitly so the follow-up sub-step
  // has a clean seam to add result-queue / COM-handle cleanup
  // between the join and any owned shell resources. Matches the
  // IconProvider pattern.
  if (worker_.joinable()) {
    worker_.request_stop();
    worker_.join();
  }
}

void ShellWorker::request(ShellCommand command) {
  {
    std::lock_guard lk(mutex_);
    pendingCommands_.push(std::move(command));
  }
  cv_.notify_one();
}

void ShellWorker::waitForProcessedForTest(
    std::size_t expected) const noexcept {
  std::size_t current = processed_.load(std::memory_order_acquire);
  while (current < expected) {
    processed_.wait(current, std::memory_order_acquire);
    current = processed_.load(std::memory_order_acquire);
  }
}

std::optional<ShellCommand> ShellWorker::dequeueOne(std::stop_token tok) {
  std::unique_lock lk(mutex_);
  cv_.wait(lk, tok, [this] { return !pendingCommands_.empty(); });
  if (tok.stop_requested()) {
    // Policy: pending commands are dropped on stop. The worker is
    // only torn down when the host window is closing, so a queued
    // file-system mutation that has not yet started is the user's
    // decision to abandon.
    return std::nullopt;
  }
  ShellCommand command = std::move(pendingCommands_.front());
  pendingCommands_.pop();
  return command;
}

void ShellWorker::processOne(const ShellCommand& command) {
  // Dummy processing pass: the actual IFileOperation call arrives
  // in the follow-up sub-step. The release-store on processed_
  // anchors the happens-before for any result data the next
  // sub-step publishes from inside processOne.
  (void)command;
  processed_.fetch_add(1, std::memory_order_release);
  processed_.notify_all();
}

void ShellWorker::workerMain(std::stop_token tok) {
  // IFileOperation requires the STA apartment; the next sub-step's
  // CoCreateInstance call will rely on this initialisation.
  const HRESULT coInitResult =
      CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  while (!tok.stop_requested()) {
    auto cmd = dequeueOne(tok);
    if (!cmd) {
      break;
    }
    processOne(*cmd);
  }

  if (SUCCEEDED(coInitResult)) {
    CoUninitialize();
  }
}

}  // namespace fast_explorer::ui
