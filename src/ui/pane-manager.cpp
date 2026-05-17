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

bool PaneManager::isDual() const noexcept {
  return panes_.size() > 1;
}

std::size_t PaneManager::count() const noexcept {
  return panes_.size();
}

std::size_t PaneManager::activeIndex() const noexcept {
  return activeIndex_;
}

PaneController& PaneManager::active() {
  return *panes_[activeIndex_];
}

const PaneController& PaneManager::active() const {
  return *panes_[activeIndex_];
}

PaneController& PaneManager::at(std::size_t i) {
  return *panes_[i];
}

const PaneController& PaneManager::at(std::size_t i) const {
  return *panes_[i];
}

}  // namespace fast_explorer::ui
