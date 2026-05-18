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
