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

  // Drag state populated by WM_LBUTTONDOWN, cleared on WM_LBUTTONUP /
  // WM_CAPTURECHANGED.
  bool dragging = false;
  POINT dragStartScreen{0, 0};
  int   axisLengthAtStart = 0;       // parent client extent along drag axis
  float ratioAtStart = 0.0f;
  int   lastGhostPos = -1;
};

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
