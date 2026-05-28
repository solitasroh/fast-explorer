#include "explorer/adapters/shell-change-notifier.h"

namespace fast_explorer::ui::adapters {

ShellChangeNotifier::ShellChangeNotifier(HWND target, UINT message,
                                          std::size_t paneIdx) noexcept
    : target_(target), message_(message), paneIdx_(paneIdx) {}

bool ShellChangeNotifier::watch(const std::wstring& location) {
  return watcher_.watch(location, target_, message_, paneIdx_);
}

void ShellChangeNotifier::stop() {
  watcher_.stop();
}

}  // namespace fast_explorer::ui::adapters
