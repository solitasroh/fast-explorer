// tab-strip-geometry.h — pure layout math for the tab strip.
//
// Shell-unaware. Caller supplies a strip width, the tab list, an
// optional scroll offset, and asks where each tab sits, which tab a
// mouse-x hits, and where a dragged tab should drop.
//
// This file lives in winui_lite — no shell tokens, no <shlobj.h>.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace fast_explorer::ui {

struct TabModel {
  // Title rendered on the tab. Empty title is allowed (the renderer
  // shows "Untitled" but the geometry layer doesn't care).
  std::wstring title;
  // When false the renderer omits the X button; geometry then reports
  // hasCloseButton(idx) == false for hit-tests on that tab.
  bool hasCloseX = true;
};

struct TabRect {
  int left;
  int right;
  int closeXLeft;   // hit region for hover X; closeXLeft == closeXRight
  int closeXRight;  // when the tab has no close button
};

struct StripMetrics {
  int height           = 28;   // total strip height in pixels at 96 DPI
  int minTabWidth      = 64;
  int maxTabWidth      = 200;
  int paddingLeft      = 4;    // before first tab
  int plusButtonWidth  = 24;   // "+" button on the right
  int chevronWidth     = 18;   // each scroll chevron when overflowing
  int closeButtonWidth = 16;
  int closeButtonInset = 4;    // gap between tab right edge and X button
  int dragThresholdPx  = 6;
};

class TabStripGeometry {
 public:
  TabStripGeometry(StripMetrics m, std::size_t tabCount)
      : metrics_(m), tabCount_(tabCount) {}

  // Recomputes per-tab rects for the given strip width and current
  // scroll offset. Tabs share evenly within [minTabWidth, maxTabWidth].
  // When the total exceeds available width, all tabs clamp to
  // minTabWidth and chevrons + scroll offset become meaningful.
  std::vector<TabRect> layout(int stripWidth, int scrollOffset) const;

  // Returns true if the tabs at maxTabWidth would exceed stripWidth.
  bool overflows(int stripWidth) const;

  // Index of the tab under mouseX, or -1 if none (background / plus
  // button / chevron region).
  int hitTest(const std::vector<TabRect>& rects, int mouseX) const;

  // Returns -1 if not over a close-X region.
  int hitTestCloseX(const std::vector<TabRect>& rects, int mouseX) const;

  // For a tab being dragged from `from`, with the mouse currently at
  // `mouseX`, what's the target drop index? Returns `from` itself if
  // the cursor is outside any tab or the drop would be a no-op.
  std::size_t dropIndex(const std::vector<TabRect>& rects,
                        std::size_t from, int mouseX) const;

  // True when |dx| meets the threshold from initial mouse-down to
  // current position. Used to gate "is this a click or a drag?".
  bool exceedsDragThreshold(int dx) const noexcept {
    return std::abs(dx) >= metrics_.dragThresholdPx;
  }

  const StripMetrics& metrics() const noexcept { return metrics_; }

 private:
  StripMetrics metrics_;
  std::size_t tabCount_;
};

}  // namespace fast_explorer::ui
