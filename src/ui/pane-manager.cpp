#include "ui/pane-manager.h"
#include "ui/pane-controller.h"

namespace fast_explorer::ui {

PaneManager::PaneManager() = default;
PaneManager::~PaneManager() = default;

std::size_t PaneManager::openInitial(HWND host) {
  panes_.push_back(std::make_unique<PaneController>(host, 0));
  activeIndex_ = 0;
  return 0;
}

std::size_t PaneManager::openPane(HWND host,
                                  const std::wstring& initialPath) {
  if (panes_.size() >= kMaxPanes) {
    return panes_.size();
  }
  const std::size_t newIdx = panes_.size();
  panes_.push_back(std::make_unique<PaneController>(host, newIdx));
  // Folder load is the caller's responsibility. openPane is a pure
  // "create slot" primitive; the caller must invoke openFolder on the
  // returned slot with the resolved path. This keeps the single-enum
  // contract on dual-mode entry (chooseSecondPaneInitialPath +
  // at(idx).openFolder in MainWindow drives the one authoritative
  // load) and avoids spurious backStack entries from a double-call.
  (void)initialPath;
  return newIdx;
}

void PaneManager::closePane() noexcept {
  if (panes_.size() <= 1) return;
  panes_.pop_back();
  if (activeIndex_ >= panes_.size()) {
    activeIndex_ = panes_.size() - 1;
  }
}

bool PaneManager::setActive(std::size_t idx) noexcept {
  if (idx >= panes_.size()) return false;
  activeIndex_ = idx;
  return true;
}

std::size_t PaneManager::count() const noexcept { return panes_.size(); }
std::size_t PaneManager::activeIndex() const noexcept { return activeIndex_; }
PaneController&       PaneManager::active()       { return *panes_[activeIndex_]; }
const PaneController& PaneManager::active() const { return *panes_[activeIndex_]; }
PaneController&       PaneManager::at(std::size_t i)       { return *panes_[i]; }
const PaneController& PaneManager::at(std::size_t i) const { return *panes_[i]; }

}  // namespace fast_explorer::ui
