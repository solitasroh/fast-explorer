#pragma once

#include <windows.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace fast_explorer::ui {

class PaneController;

// Owns one or more PaneControllers and tracks which one accelerators
// currently target. Skeleton ships single-pane only — atom 1 of the
// M9 multi-pane block. The dual-pane layout split + per-pane message
// dispatch + per-pane coordinators + active-pane focus + layout
// shortcuts land in subsequent atoms; isDual() / second-pane methods
// are stubs that always operate on a single pane until then.
class PaneManager {
 public:
  PaneManager();
  ~PaneManager();

  PaneManager(const PaneManager&) = delete;
  PaneManager& operator=(const PaneManager&) = delete;
  PaneManager(PaneManager&&) = delete;
  PaneManager& operator=(PaneManager&&) = delete;

  // Opens the first pane on `host`. Returns the new pane's index.
  // Must be called exactly once before any active() / at() call.
  std::size_t openInitial(HWND host);

  // Opens the second pane on `host`. Returns the new pane's index
  // (always 1). No-op + returns 1 if the second pane already exists.
  // Does not change activeIndex_; the caller decides whether to focus
  // the new pane.
  std::size_t openSecond(HWND host);

  // Tears down the second pane and resets activeIndex_ to 0. No-op
  // when only the initial pane is open.
  void closeSecond() noexcept;

  // Sets activeIndex_ to `idx`, clamped to the currently open pane
  // range. Returns false when the index is out of range (no change).
  bool setActive(std::size_t idx) noexcept;

  [[nodiscard]] bool isDual() const noexcept;

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
