#pragma once

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/layout-orientation.h"
#include "core/layout-preset.h"
#include "ui/splitter-ratios.h"

namespace fast_explorer::ui {

// Seam orientation lives in core for settings round-trip; re-exported
// here so the existing `ui::LayoutOrientation::*` call sites
// (resolveLayoutToggle, MainWindow) compile unchanged.
// Vertical = panes side-by-side (default). Horizontal =
// panes stacked top-over-bottom.
using LayoutOrientation = fast_explorer::core::LayoutOrientation;

// Action the layout-toggle resolver wants the caller to perform.
enum class LayoutAction : std::uint8_t {
  EnterDual,           // currently single → open second pane in `target`
  SwitchOrientation,   // currently dual at the other seam → flip in place
  ExitToSingle,        // currently dual at the same seam → close second pane
};

struct LayoutTransition {
  LayoutAction action{};
  LayoutOrientation target{};
};

// Maps "user pressed the toggle key for `pressed`" to the action the
// view layer should take, given `isDual` + `currentOrientation`.
// Rule: pressing the toggle key matching the active seam exits dual
// mode; pressing the other key while already dual just flips the
// seam; pressing either key from single mode enters dual mode in
// the chosen orientation. The resolver is policy-only (no Win32),
// so the rule is testable without an HWND (and constexpr-evaluable
// at unit-test sites via static_assert).
[[nodiscard]] constexpr LayoutTransition resolveLayoutToggle(
    bool isDual,
    LayoutOrientation currentOrientation,
    LayoutOrientation pressed) noexcept {
  if (!isDual) {
    return {LayoutAction::EnterDual, pressed};
  }
  if (currentOrientation == pressed) {
    return {LayoutAction::ExitToSingle, currentOrientation};
  }
  return {LayoutAction::SwitchOrientation, pressed};
}

enum class SplitterOrientation : std::uint8_t { Vertical = 0, Horizontal = 1 };

struct SplitterRect {
  RECT hitRect{};
  RECT visualRect{};
  SplitterOrientation orient{SplitterOrientation::Vertical};
  std::uint8_t ratioId{0};
};

struct PaneLayoutResult {
  std::array<RECT, 4> slots{};
  std::array<SplitterRect, 3> splitters{};
  std::size_t slotCount{0};
  std::size_t splitterCount{0};
};

// Computes a full layout result (pane slots + splitter descriptors) for
// the given preset and splitter ratios. reservedTop/reservedBottom are
// pixel heights excluded from the pane area (address bar / status bar).
[[nodiscard]] PaneLayoutResult computePaneLayout(
    fast_explorer::core::LayoutPreset preset,
    const SplitterRatios& ratios,
    int clientWidth,
    int clientHeight,
    int reservedTop,
    int reservedBottom) noexcept;

}  // namespace fast_explorer::ui
