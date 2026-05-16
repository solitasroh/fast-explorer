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

#include "ui/result-channel.h"

namespace fast_explorer::ui {

// Identifies which IFileOperation verb the worker should run on the
// command. Stable integer values keep cross-translation-unit
// switches simple; do not reorder.
enum class ShellCommandKind : uint8_t {
  Rename = 0,
  CreateFolder = 1,
  Delete = 2,
};

// One queued shell-side mutation. Field meaning depends on `kind`:
//   Rename       — sourcePath is the existing file/folder, newName is
//                  the leaf name to rename it to.
//   CreateFolder — sourcePath is the parent folder, newName is the
//                  leaf name of the new folder to create.
//   Delete       — sourcePath is the item to send to the recycle bin.
//                  newName is unused.
struct ShellCommand {
  // Default to Rename rather than Delete so a forgotten initializer
  // does not pick the most destructive operation by accident.
  ShellCommandKind kind = ShellCommandKind::Rename;
  std::wstring sourcePath;
  std::wstring newName;
};

// Per-command outcome posted back to the UI thread once the STA
// worker finishes the IFileOperation. sourcePath / newName mirror
// the request fields so the UI can format a "renamed X to Y"
// status without having to track the in-flight command separately.
struct OperationResult {
  ShellCommandKind kind = ShellCommandKind::Rename;
  std::wstring sourcePath;
  std::wstring newName;
  bool success = false;
};

// STA worker for IFileOperation-driven mutations. Owns a jthread
// that pulls commands off a thread-safe queue, runs them inside
// CoInitializeEx(COINIT_APARTMENTTHREADED) as IFileOperation
// requires, and publishes per-command OperationResults back to
// the host window via a coalesced kWmFeOperationResult.
class ShellWorker {
 public:
  explicit ShellWorker(HWND host);
  ~ShellWorker();

  ShellWorker(const ShellWorker&) = delete;
  ShellWorker& operator=(const ShellWorker&) = delete;
  ShellWorker(ShellWorker&&) = delete;
  ShellWorker& operator=(ShellWorker&&) = delete;

  // Queues a command for the worker to run. Safe to call from any
  // thread; ownership of the strings inside ShellCommand transfers.
  void request(ShellCommand command);

  // Drains all completed operation results into the returned
  // vector and resets the coalesce gate. Safe to call from the UI
  // thread after observing kWmFeOperationResult.
  std::vector<OperationResult> drainResults() {
    return results_.drainResults();
  }

  // Returns how many commands the worker has dequeued and finished
  // its dummy-processing pass over. Acquire-load semantics.
  std::size_t processedForTest() const noexcept {
    return processed_.load(std::memory_order_acquire);
  }

  // Test helper: blocks until processedForTest() >= expected. Uses
  // std::atomic::wait so the test does not busy-loop.
  void waitForProcessedForTest(std::size_t expected) const noexcept;

 private:
  void workerMain(std::stop_token tok);
  std::optional<ShellCommand> dequeueOne(std::stop_token tok);
  void processOne(const ShellCommand& command);

  std::queue<ShellCommand> pendingCommands_;
  mutable std::mutex mutex_;
  std::condition_variable_any cv_;
  std::atomic<std::size_t> processed_{0};
  ResultChannel<OperationResult> results_;
  std::jthread worker_;
};

}  // namespace fast_explorer::ui
