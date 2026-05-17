#include "ui/pane-layout.h"

#include <algorithm>

namespace fast_explorer::ui {

PaneLayoutRects computePaneRects(int clientWidth,
                                 int clientHeight,
                                 int addressBarHeight,
                                 int statusBarHeight,
                                 std::size_t paneCount) noexcept {
  PaneLayoutRects out{};
  if (paneCount == 0 || paneCount > 2) {
    return out;
  }
  const int listH = std::max(0, clientHeight - addressBarHeight - statusBarHeight);
  if (listH <= 0 || clientWidth <= 0) {
    return out;
  }
  if (paneCount == 1) {
    out.panes[0] = {0, addressBarHeight, clientWidth,
                    addressBarHeight + listH};
    return out;
  }
  const int leftRight = clientWidth / 2;
  out.panes[0] = {0, addressBarHeight, leftRight,
                  addressBarHeight + listH};
  out.panes[1] = {leftRight, addressBarHeight, clientWidth,
                  addressBarHeight + listH};
  return out;
}

}  // namespace fast_explorer::ui
