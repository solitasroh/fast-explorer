#include "winui_lite/widgets/tab-strip-geometry.h"

#include <algorithm>
#include <cstdlib>

namespace fast_explorer::ui {

bool TabStripGeometry::overflows(int stripWidth) const {
  if (tabCount_ == 0) return false;
  const int reserved =
      metrics_.paddingLeft + metrics_.plusButtonWidth;
  const int totalAtMax =
      static_cast<int>(tabCount_) * metrics_.maxTabWidth;
  return totalAtMax + reserved > stripWidth;
}

std::vector<TabRect> TabStripGeometry::layout(int stripWidth,
                                              int scrollOffset) const {
  std::vector<TabRect> out;
  out.reserve(tabCount_);
  if (tabCount_ == 0) return out;

  const bool over = overflows(stripWidth);
  const int reserved = metrics_.paddingLeft +
      (over ? metrics_.chevronWidth * 2 : 0) + metrics_.plusButtonWidth;
  int avail = stripWidth - reserved;
  if (avail < metrics_.minTabWidth * static_cast<int>(tabCount_)) {
    avail = metrics_.minTabWidth * static_cast<int>(tabCount_);
  }

  int tabWidth = avail / static_cast<int>(tabCount_);
  tabWidth = std::clamp(tabWidth, metrics_.minTabWidth, metrics_.maxTabWidth);

  int x = metrics_.paddingLeft + (over ? metrics_.chevronWidth : 0)
        - scrollOffset;
  for (std::size_t i = 0; i < tabCount_; ++i) {
    TabRect r;
    r.left = x;
    r.right = x + tabWidth;
    r.closeXRight = r.right - metrics_.closeButtonInset;
    r.closeXLeft = r.closeXRight - metrics_.closeButtonWidth;
    out.push_back(r);
    x += tabWidth;
  }
  return out;
}

int TabStripGeometry::hitTest(const std::vector<TabRect>& rects,
                              int mouseX) const {
  for (std::size_t i = 0; i < rects.size(); ++i) {
    if (mouseX >= rects[i].left && mouseX < rects[i].right) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int TabStripGeometry::hitTestCloseX(
    const std::vector<TabRect>& rects, int mouseX) const {
  for (std::size_t i = 0; i < rects.size(); ++i) {
    if (mouseX >= rects[i].closeXLeft &&
        mouseX <  rects[i].closeXRight) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

std::size_t TabStripGeometry::dropIndex(
    const std::vector<TabRect>& rects,
    std::size_t from, int mouseX) const {
  if (rects.empty()) return from;
  if (mouseX < rects.front().left) return 0;
  if (mouseX >= rects.back().right) return rects.size() - 1;
  for (std::size_t i = 0; i < rects.size(); ++i) {
    const int mid = (rects[i].left + rects[i].right) / 2;
    if (mouseX < mid) {
      return (i == 0) ? 0 : (i <= from ? i : i - 1);
    }
  }
  return from;
}

}  // namespace fast_explorer::ui
