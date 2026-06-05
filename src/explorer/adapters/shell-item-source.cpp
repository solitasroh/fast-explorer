#include "explorer/adapters/shell-item-source.h"

#include <memory>

#include "explorer/pane-controller.h"

namespace fast_explorer::ui::adapters {

ShellItemSource::ShellItemSource(PaneController* const& activeCell) noexcept
    : cell_(std::addressof(activeCell)) {}

bool ShellItemSource::navigateTo(const std::wstring& location) {
  PaneController* c = *cell_;
  if (!c) return false;
  // PaneController::openFolder validates the path (rejects empty /
  // relative / certain UNC forms) and kicks off the async worker.
  // It returns false on validation failure; we surface that to the
  // port caller so the host can skip its post-navigation refresh
  // (clear list view, sync address bar) instead of clearing for a
  // navigation that never started.
  return c->openFolder(location);
}

const std::wstring& ShellItemSource::currentLocation() const {
  static const std::wstring kEmpty;
  PaneController* c = *cell_;
  return c ? c->currentPath() : kEmpty;
}

std::size_t ShellItemSource::count() const {
  PaneController* c = *cell_;
  if (!c) return 0;
  return c->store().displayedCount();
}

ports::ItemId ShellItemSource::idAt(std::size_t index) const {
  PaneController* c = *cell_;
  if (!c) return ports::kInvalidItemId;
  if (!c->store().rawIndexForVisibleRow(index).has_value()) {
    return ports::kInvalidItemId;
  }
  // 1-based to keep 0 reserved as the invalid-id sentinel. Dispatcher
  // / activator reverse the +1 to look up the entry.
  return static_cast<ports::ItemId>(index + 1);
}

}  // namespace fast_explorer::ui::adapters
