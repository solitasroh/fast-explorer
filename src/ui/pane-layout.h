#pragma once

#include <windows.h>

#include <array>
#include <cstddef>

namespace fast_explorer::ui {

// Result of laying out the client area below the address bar and
// above the status bar. paneCount controls how many pane slots are
// returned with non-empty rects; the unused slot stays zeroed.
struct PaneLayoutRects {
  std::array<RECT, 2> panes{};
};

// Computes pane rects for `paneCount` (1 or 2) inside a client area
// of (clientWidth, clientHeight), with `addressBarHeight` reserved
// at the top and `statusBarHeight` at the bottom. The split is a
// 50/50 vertical division of the remaining vertical strip; this
// matches the design §4.2 dual-horizontal layout choice (two panes
// side-by-side with a vertical seam, both filling full height).
//
// Returns zeroed rects when paneCount is out of range. The seam
// between dual panes lives entirely inside the right pane: the left
// pane's right edge is `clientWidth / 2` exactly, the right pane's
// left edge is `clientWidth / 2`, no explicit splitter widget yet.
[[nodiscard]] PaneLayoutRects computePaneRects(int clientWidth,
                                               int clientHeight,
                                               int addressBarHeight,
                                               int statusBarHeight,
                                               std::size_t paneCount) noexcept;

}  // namespace fast_explorer::ui
