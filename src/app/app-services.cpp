#include "app/app-services.h"

#include <windows.h>

#include "core/crash-handler.h"

namespace fast_explorer::app {

AppServices::AppServices() noexcept
    : memory_(logger_) {}

AppServices::~AppServices() {
  stop();
}

bool AppServices::start() {
  loggerStarted_ = logger_.start();
  if (!loggerStarted_) {
    OutputDebugStringW(L"[AppServices] RingLogger::start failed\n");
  }

  crashInstalled_ = fast_explorer::core::CrashHandler::install(perf_);
  if (loggerStarted_) {
    if (crashInstalled_) {
      logger_.info(L"crash handler installed");
    } else {
      logger_.warn(L"crash handler failed to install");
    }
  }

  memoryStarted_ = memory_.start();
  return loggerStarted_;
}

void AppServices::stop() {
  if (memoryStarted_) {
    memory_.stop();
    memoryStarted_ = false;
  }
  if (crashInstalled_) {
    fast_explorer::core::CrashHandler::uninstall();
    crashInstalled_ = false;
  }
  if (loggerStarted_) {
    logger_.stop();
    loggerStarted_ = false;
  }
}

}  // namespace fast_explorer::app
