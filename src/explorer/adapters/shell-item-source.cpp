#include "explorer/adapters/shell-item-source.h"

#include "explorer/pane-controller.h"

namespace fast_explorer::ui::adapters {

ShellItemSource::ShellItemSource(PaneController& pane) noexcept
    : pane_(&pane) {}

bool ShellItemSource::navigateTo(const std::wstring& location) {
  // PaneController::openFolder validates the path (rejects empty /
  // relative / certain UNC forms) and kicks off the async worker.
  // It returns false on validation failure; we surface that to the
  // port caller so the host can skip its post-navigation refresh
  // (clear list view, sync address bar) instead of clearing for a
  // navigation that never started.
  return pane_->openFolder(location);
}

const std::wstring& ShellItemSource::currentLocation() const {
  return pane_->currentPath();
}

std::size_t ShellItemSource::count() const {
  // publishedCount() is the UI-safe upper bound while the worker
  // may still be appending. It increases monotonically inside a
  // single navigation; navigateTo() resets the store underneath.
  return pane_->store().publishedCount();
}

ports::ItemId ShellItemSource::idAt(std::size_t index) const {
  if (index >= pane_->store().publishedCount()) {
    return ports::kInvalidItemId;
  }
  // 1-based to keep 0 reserved as the invalid-id sentinel. Dispatcher
  // / activator reverse the +1 to look up the entry.
  return static_cast<ports::ItemId>(index + 1);
}

}  // namespace fast_explorer::ui::adapters
