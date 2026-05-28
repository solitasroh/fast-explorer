#include "explorer/pane-tab-host.h"

#include <shlobj.h>

#include <algorithm>

#include "explorer/main-window.h"
#include "explorer/pane-tab-host-state.h"

namespace fast_explorer::ui {

PaneTabHost::PaneTabHost(MainWindow* host, std::size_t paneIdx,
                         PaneController*& activeCell)
    : host_(host), paneIdx_(paneIdx), activeCell_(activeCell) {
  if (host_ && host_->handle()) {
    strip_ = std::make_unique<TabStrip>(host_->handle(), paneIdx);
    strip_->onActivate = [this](std::size_t j) { activateTab(j); };
    strip_->onClose    = [this](std::size_t j) { closeTab(j); };
    strip_->onNew      = [this]() { openNewTab(); };
    strip_->onReorder  = [this](std::size_t f, std::size_t t) {
      moveTab(f, t);
    };
    strip_->onContextMenu = [this](std::size_t j, POINT pt) {
      host_->showTabContextMenu(paneIdx_, j, pt);
    };
  }
}

PaneTabHost::~PaneTabHost() = default;

// static
std::wstring PaneTabHost::resolveHomeFolder() {
  PWSTR p = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &p))) {
    std::wstring out{p};
    CoTaskMemFree(p);
    return out;
  }
  return L"C:\\";
}

std::wstring PaneTabHost::homeFolder() const {
  // Resolve once per host instance; the value cannot change at runtime
  // outside of a user re-logon, which would also re-launch the app.
  static thread_local const std::wstring cached = resolveHomeFolder();
  return cached;
}

PaneController& PaneTabHost::activeTab() noexcept {
  return *tabs_[activeTab_];
}

PaneController& PaneTabHost::tabAt(std::size_t idx) noexcept {
  return *tabs_[idx];
}

HWND PaneTabHost::stripHandle() const noexcept {
  return strip_ ? strip_->handle() : nullptr;
}

int PaneTabHost::stripPreferredHeight() const {
  return strip_ ? strip_->preferredHeight() : 28;
}

void PaneTabHost::refreshTabTitles() { rebuildStrip(); }

void PaneTabHost::rebuildStrip() {
  if (!strip_) return;
  std::vector<TabModel> models;
  models.reserve(tabs_.size());
  for (const auto& t : tabs_) {
    TabModel m;
    // Title is leaf of currentPath or "Home" if empty.
    const auto& full = t->currentPath();
    auto slash = full.find_last_of(L"\\/");
    m.title = (slash == std::wstring::npos)
        ? full
        : (slash + 1 < full.size() ? full.substr(slash + 1) : full);
    if (m.title.empty()) m.title = L"Home";
    m.hasCloseX = true;
    models.push_back(std::move(m));
  }
  strip_->setTabs(models);
  strip_->setActive(activeTab_);
}

// ---- Task 15: activateTab + openNewTab ----------------------------------

void PaneTabHost::activateTab(std::size_t idx) {
  if (idx >= tabs_.size()) return;
  if (host_ && !host_->tryActivateTab(paneIdx_, idx)) return;
  activeTab_ = idx;
  activeCell_ = tabs_[idx].get();
  if (host_) {
    host_->bindListViewToActiveTab(paneIdx_);
    host_->refreshPaneChrome(paneIdx_);
  }
  if (strip_) strip_->setActive(idx);
}

void PaneTabHost::openNewTab() {
  if (!host_) return;
  auto pc = std::make_unique<PaneController>(host_->handle(), paneIdx_);
  pc->openFolder(homeFolder());
  tabs_.push_back(std::move(pc));
  rebuildStrip();
  activateTab(tabs_.size() - 1);
}

void PaneTabHost::openInNewTab(const std::wstring& path) {
  if (!host_) return;
  auto pc = std::make_unique<PaneController>(host_->handle(), paneIdx_);
  if (!pc->openFolder(path)) return;
  tabs_.push_back(std::move(pc));
  rebuildStrip();
  // background open — do NOT activate
}

// ---- Task 16: closeTab --------------------------------------------------

void PaneTabHost::closeTab(std::size_t idx) {
  if (idx >= tabs_.size()) return;
  if (tabs_.size() == 1) {
    // Last tab → reset to Home rather than empty the pane.
    tabs_[0]->openFolder(homeFolder());
    rebuildStrip();
    if (host_) {
      host_->bindListViewToActiveTab(paneIdx_);
      host_->refreshPaneChrome(paneIdx_);
    }
    return;
  }
  activeTab_ = detail::adjustActiveAfterErase(activeTab_, idx, tabs_.size());
  tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(idx));
  rebuildStrip();
  activateTab(activeTab_);
}

// ---- Task 17: closeOtherTabs + closeTabsToRight -------------------------

void PaneTabHost::closeOtherTabs(std::size_t keepIdx) {
  if (keepIdx >= tabs_.size() || tabs_.size() == 1) return;
  auto kept = std::move(tabs_[keepIdx]);
  tabs_.clear();
  tabs_.push_back(std::move(kept));
  activeTab_ = 0;
  rebuildStrip();
  activateTab(0);
}

void PaneTabHost::closeTabsToRight(std::size_t idx) {
  if (idx + 1 >= tabs_.size()) return;
  tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(idx) + 1,
              tabs_.end());
  if (activeTab_ > idx) {
    activeTab_ = idx;
  }
  rebuildStrip();
  activateTab(activeTab_);
}

// ---- Task 18: moveTab + cycle helpers -----------------------------------

void PaneTabHost::moveTab(std::size_t from, std::size_t to) {
  if (from >= tabs_.size() || to >= tabs_.size() || from == to) return;
  activeTab_ = detail::adjustActiveAfterMove(activeTab_, from, to);
  auto moving = std::move(tabs_[from]);
  tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(from));
  tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(to),
               std::move(moving));
  // activeCell_ still points at the correct controller — pointers are
  // unchanged by vector erase/insert of unique_ptr (move semantics).
  rebuildStrip();
  if (strip_) strip_->setActive(activeTab_);
}

void PaneTabHost::cycleNext() {
  if (tabs_.empty()) return;
  activateTab((activeTab_ + 1) % tabs_.size());
}

void PaneTabHost::cyclePrev() {
  if (tabs_.empty()) return;
  activateTab((activeTab_ + tabs_.size() - 1) % tabs_.size());
}

// ---- Task 19: session restore + capture ---------------------------------

void PaneTabHost::restoreFromSession(const core::PaneSessionV6& panel) {
  tabs_.clear();
  if (panel.tabs.empty()) {
    openNewTab();   // creates Home tab + activates
    return;
  }
  for (const auto& t : panel.tabs) {
    auto pc = std::make_unique<PaneController>(host_->handle(), paneIdx_);
    if (t.path.empty()) {
      pc->openFolder(homeFolder());
    } else if (!pc->openFolder(t.path)) {
      pc->openFolder(homeFolder());   // fall back if path no longer valid
    }
    tabs_.push_back(std::move(pc));
  }
  activeTab_ = std::min(panel.activeTab, tabs_.size() - 1);
  rebuildStrip();
  activateTab(activeTab_);
}

core::PaneSessionV6 PaneTabHost::captureSession() const {
  core::PaneSessionV6 out;
  out.tabs.reserve(tabs_.size());
  for (const auto& t : tabs_) {
    out.tabs.push_back(core::TabRecordV6{t->currentPath()});
  }
  out.activeTab = activeTab_;
  return out;
}

}  // namespace fast_explorer::ui
