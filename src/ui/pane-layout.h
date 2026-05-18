#pragma once

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace fast_explorer::ui {

// Orientation of the seam in a 2-pane layout. Vertical splits the
// client area along a vertical seam (panes side-by-side, original
// behaviour). Horizontal splits along a horizontal seam (panes
// stacked top-over-bottom). The enum is stored as an explicit
// uint8_t because it is also serialized into settings.json.
enum class LayoutOrientation : std::uint8_t { Vertical = 0, Horizontal = 1 };

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

// Result of laying out the client area below the address bar and
// above the status bar. paneCount controls how many pane slots are
// returned with non-empty rects; the unused slot stays zeroed.
struct PaneLayoutRects {
  std::array<RECT, 2> panes{};
};

// Computes pane rects for `paneCount` (1 or 2) inside a client area
// of (clientWidth, clientHeight), with `addressBarHeight` reserved
// at the top and `statusBarHeight` at the bottom. For paneCount==2
// the split is a 50/50 division of the remaining strip, along the
// `orientation` seam:
//   - Vertical   (default): left | right, each pane full height
//   - Horizontal:           top  / bottom, each pane full width
//
// Returns zeroed rects when paneCount is out of range or the strip
// is non-positive. For odd divisions the first pane absorbs the
// rounded-down half (the second pane covers the remainder).
[[nodiscard]] PaneLayoutRects computePaneRects(
    int clientWidth,
    int clientHeight,
    int addressBarHeight,
    int statusBarHeight,
    std::size_t paneCount,
    LayoutOrientation orientation = LayoutOrientation::Vertical) noexcept;

}  // namespace fast_explorer::ui
