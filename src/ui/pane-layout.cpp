#include "ui/pane-layout.h"

#include <algorithm>

namespace fast_explorer::ui {

PaneLayoutRects computePaneRects(int clientWidth,
                                 int clientHeight,
                                 int addressBarHeight,
                                 int statusBarHeight,
                                 std::size_t paneCount,
                                 LayoutOrientation orientation) noexcept {
  PaneLayoutRects out{};
  if (paneCount == 0 || paneCount > 2) {
    return out;
  }
  const int listH = std::max(0, clientHeight - addressBarHeight - statusBarHeight);
  if (listH <= 0 || clientWidth <= 0) {
    return out;
  }
  const int topY = addressBarHeight;
  const int bottomY = addressBarHeight + listH;
  if (paneCount == 1) {
    out.panes[0] = {0, topY, clientWidth, bottomY};
    return out;
  }
  if (orientation == LayoutOrientation::Horizontal) {
    const int midY = topY + listH / 2;
    out.panes[0] = {0, topY,  clientWidth, midY};
    out.panes[1] = {0, midY,  clientWidth, bottomY};
    return out;
  }
  const int midX = clientWidth / 2;
  out.panes[0] = {0,    topY, midX,        bottomY};
  out.panes[1] = {midX, topY, clientWidth, bottomY};
  return out;
}

}  // namespace fast_explorer::ui
