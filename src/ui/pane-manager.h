#pragma once

#include <windows.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace fast_explorer::ui {

class PaneController;

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

class PaneManager {
 public:
  PaneManager();
  ~PaneManager();
  PaneManager(const PaneManager&) = delete;
  PaneManager& operator=(const PaneManager&) = delete;
  PaneManager(PaneManager&&) = delete;
  PaneManager& operator=(PaneManager&&) = delete;

  static constexpr std::size_t kMaxPanes = 4;

  // Opens the first pane on `host`. Must be called exactly once
  // before any active() / at() call. Returns 0.
  std::size_t openInitial(HWND host);

  // Appends a slot at index count(). Returns the new slot index
  // (1..kMaxPanes - 1). The `initialPath` argument is reserved for
  // future tab-aware extensions; callers must invoke openFolder on
  // the returned slot to populate it. openPane does NOT auto-load —
  // it is a pure "create slot" primitive to preserve the single-enum
  // contract on dual-mode entry.
  // No-op + returns count() when already at kMaxPanes.
  std::size_t openPane(HWND host, const std::wstring& initialPath);

  // Pops the last slot. No-op when count() == 1. activeIndex_
  // clamped to count() - 1 after the pop.
  void closePane() noexcept;

  bool setActive(std::size_t idx) noexcept;
  [[nodiscard]] std::size_t count() const noexcept;
  [[nodiscard]] std::size_t activeIndex() const noexcept;
  [[nodiscard]] PaneController& active();
  [[nodiscard]] const PaneController& active() const;
  [[nodiscard]] PaneController& at(std::size_t i);
  [[nodiscard]] const PaneController& at(std::size_t i) const;

 private:
  std::vector<std::unique_ptr<PaneController>> panes_;
  std::size_t activeIndex_ = 0;
};

}  // namespace fast_explorer::ui
