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

  // True when more than one pane is currently open. Stays false in
  // the atom-1 skeleton; atom 2 will flip it when dual layout is
  // wired.
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
