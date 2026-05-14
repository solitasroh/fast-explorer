#pragma once

#include "core/perf-tracker.h"
#include "core/process-memory.h"
#include "core/ring-logger.h"

namespace fast_explorer::app {

// Owns the four diagnostic / runtime services that wWinMain previously
// reached through Meyers singletons. Lifetime is bound to wWinMain's stack
// so destruction order is deterministic: memory service stops first, the
// logger drains last, crash handler is uninstalled in between.
//
// Construction order (members below) starts the logger before everything
// else so the other services can write into it from their constructors.
// `start()` / `stop()` cascade the same order; the destructor calls
// `stop()` defensively.
class AppServices {
 public:
  AppServices() noexcept;
  ~AppServices();
  AppServices(const AppServices&) = delete;
  AppServices& operator=(const AppServices&) = delete;

  // Brings up every service. Returns true if the core services (logger +
  // crash handler) initialized; the memory service is best-effort.
  bool start();

  // Tears down in reverse order. Safe to call multiple times.
  void stop();

  fast_explorer::core::RingLogger& logger() noexcept { return logger_; }
  fast_explorer::core::PerfTracker& perf() noexcept { return perf_; }
  fast_explorer::core::ProcessMemoryService& memory() noexcept { return memory_; }

  bool loggerOk() const noexcept { return loggerStarted_; }
  bool crashOk() const noexcept { return crashInstalled_; }
  bool memoryOk() const noexcept { return memoryStarted_; }

 private:
  fast_explorer::core::RingLogger logger_;
  fast_explorer::core::PerfTracker perf_;
  fast_explorer::core::ProcessMemoryService memory_;

  bool loggerStarted_ = false;
  bool crashInstalled_ = false;
  bool memoryStarted_ = false;
};

}  // namespace fast_explorer::app
