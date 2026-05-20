#pragma once

#include <windows.h>

#include <functional>

#include "ui/pane-layout.h"
#include "ui/splitter-ratios.h"

namespace fast_explorer::ui {

struct SplitterContext {
  SplitterOrientation orient = SplitterOrientation::Vertical;
  std::uint8_t ratioId = 0;
  SplitterRatios* ratios = nullptr;        // borrowed, MainWindow owns
  std::function<void()> onCommit;          // -> MainWindow::relayout

  // Geometry that defines how cursor-position-in-parent-client maps back
  // to a ratio. Updated by MainWindow::relayout() per splitter:
  //   * Vertical splitters:  axisOrigin=0, axisLength=clientW
  //     (preset math is `W * ratio[i]`).
  //   * Horizontal splitters: axisOrigin=reservedTop (=0 today),
  //     axisLength=totalH = clientH - reservedTop - reservedBottom
  //     (preset math is `top + totalH * ratio[i]`).
  // Using the FULL parent height for a horizontal splitter would be
  // off by the status-bar height — that was the v0.4.0 drag-feels-off
  // bug we're fixing here.
  int axisOriginInParent = 0;
  int axisLengthForRatio = 0;

  // Perpendicular extent of the splitter's logical span in parent-client
  // coords. Used to clip the ghost line so it never bleeds outside the
  // column/row this splitter actually divides (e.g. Tri_A's inner
  // horizontal splitter only spans the right column).
  int perpLow  = 0;
  int perpHigh = 0;

  // Drag state populated by WM_LBUTTONDOWN, cleared on WM_LBUTTONUP /
  // WM_CAPTURECHANGED.
  bool dragging = false;
  POINT dragStartScreen{0, 0};
  float ratioAtStart = 0.0f;
  int   lastGhostPos = -1;
};

// Maps a cursor position (in parent-client coords along the splitter's
// drag axis) back to a [0,1] ratio, clamped to [0.1, 0.9] so a splitter
// never collapses a pane to zero. Pure, header-inlinable, unit-tested.
[[nodiscard]] constexpr float computeRatioFromCursor(
    int cursorPos, int axisOrigin, int axisLength) noexcept {
  if (axisLength <= 0) return 0.5f;
  const float raw = static_cast<float>(cursorPos - axisOrigin) /
                    static_cast<float>(axisLength);
  if (raw < 0.1f) return 0.1f;
  if (raw > 0.9f) return 0.9f;
  return raw;
}

class PaneSplitter {
 public:
  // Register the window class. Idempotent. Returns false only on
  // unexpected RegisterClassEx failure.
  static bool registerClass(HINSTANCE instance) noexcept;

  // Create one splitter HWND as a child of `parent`. `ctx` is moved
  // into a heap allocation owned by the HWND; freed at WM_NCDESTROY.
  static HWND create(HINSTANCE instance, HWND parent, SplitterContext ctx);

  static constexpr const wchar_t* kClassName = L"FastExplorer.PaneSplitter";
};

}  // namespace fast_explorer::ui
