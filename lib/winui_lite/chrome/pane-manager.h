#pragma once

#include <windows.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace fast_explorer::ui {

// Resolves the initial folder for a freshly-opened second pane.
// `requested` is the session-restored path (empty when there is no
// persisted second_path or when the user hit Ctrl+2 fresh). The
// fallback is the active pane's current folder so a manual Ctrl+2
// lands on the user's working location instead of an empty list.
// Returns a reference into one of the inputs so noexcept is honest
// (no allocation); the caller keeps the chosen source alive while
// the reference is used.
[[nodiscard]] inline const std::wstring& chooseSecondPaneInitialPath(
    const std::wstring& requested, const std::wstring& fallback) noexcept {
  return requested.empty() ? fallback : requested;
}

// Owns the active set of panes for an app. `TPane` is the app-supplied
// pane type — winui_lite does not know what a "pane" actually is
// (file-list view, terminal, web view, etc.). TPane must be heap-
// constructible as `TPane(HWND host, std::size_t paneIdx)`; the
// manager creates each pane via std::make_unique so callers can hold
// long-lived references via active() / at().
template <class TPane>
class PaneManager {
 public:
  PaneManager() = default;
  ~PaneManager() = default;
  PaneManager(const PaneManager&) = delete;
  PaneManager& operator=(const PaneManager&) = delete;
  PaneManager(PaneManager&&) = delete;
  PaneManager& operator=(PaneManager&&) = delete;

  static constexpr std::size_t kMaxPanes = 4;

  // Opens the first pane on `host`. Must be called exactly once
  // before any active() / at() call. Returns 0.
  std::size_t openInitial(HWND host) {
    panes_.push_back(std::make_unique<TPane>(host, 0));
    activeIndex_ = 0;
    return 0;
  }

  // Appends a slot at index count(). Returns the new slot index
  // (1..kMaxPanes - 1). The `initialPath` argument is reserved for
  // future tab-aware extensions; callers must invoke the app's load
  // hook on the returned slot to populate it. openPane does NOT
  // auto-load — it is a pure "create slot" primitive to preserve the
  // single-enum contract on dual-mode entry.
  // No-op + returns count() when already at kMaxPanes.
  std::size_t openPane(HWND host, const std::wstring& initialPath) {
    if (panes_.size() >= kMaxPanes) {
      return panes_.size();
    }
    const std::size_t newIdx = panes_.size();
    panes_.push_back(std::make_unique<TPane>(host, newIdx));
    (void)initialPath;
    return newIdx;
  }

  // Pops the last slot. No-op when count() == 1. activeIndex_
  // clamped to count() - 1 after the pop.
  void closePane() noexcept {
    if (panes_.size() <= 1) return;
    panes_.pop_back();
    if (activeIndex_ >= panes_.size()) {
      activeIndex_ = panes_.size() - 1;
    }
  }

  bool setActive(std::size_t idx) noexcept {
    if (idx >= panes_.size()) return false;
    activeIndex_ = idx;
    return true;
  }

  [[nodiscard]] std::size_t count() const noexcept { return panes_.size(); }
  [[nodiscard]] std::size_t activeIndex() const noexcept {
    return activeIndex_;
  }
  [[nodiscard]] TPane&       active()       { return *panes_[activeIndex_]; }
  [[nodiscard]] const TPane& active() const { return *panes_[activeIndex_]; }
  [[nodiscard]] TPane&       at(std::size_t i)       { return *panes_[i]; }
  [[nodiscard]] const TPane& at(std::size_t i) const { return *panes_[i]; }

 private:
  std::vector<std::unique_ptr<TPane>> panes_;
  std::size_t activeIndex_ = 0;
};

}  // namespace fast_explorer::ui
